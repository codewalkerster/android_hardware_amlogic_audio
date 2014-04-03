/*
 * Copyright (C) 2011 The Android Open Source Project
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 *      http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 */

#define LOG_TAG "audio_hw_primary"
//#define LOG_NDEBUG 0
//#define LOG_NDEBUG_FUNCTION
#ifdef LOG_NDEBUG_FUNCTION
#define LOGFUNC(...) ((void)0)
#else
#define LOGFUNC(...) (ALOGD(__VA_ARGS__))
#endif

#include <errno.h>
#include <pthread.h>
#include <stdint.h>
#include <sys/time.h>
#include <stdlib.h>
#include <sys/stat.h>
#include <fcntl.h>

#include <cutils/log.h>
#include <cutils/str_parms.h>
#include <cutils/properties.h>

#include <hardware/hardware.h>
#include <system/audio.h>
#include <hardware/audio.h>

#include <tinyalsa/asoundlib.h>
#include <audio_utils/resampler.h>
#include <audio_utils/echo_reference.h>
#include <hardware/audio_effect.h>
#include <audio_effects/effect_aec.h>
#include "audio_route.h"
/* ALSA cards for AML */
#define CARD_AMLOGIC_BOARD 0 
#define CARD_AMLOGIC_USB 1
#define CARD_AMLOGIC_DEFAULT CARD_AMLOGIC_BOARD 
/* ALSA ports for AML */
#define PORT_MM 0
/* number of frames per period */
#define DEFAULT_PERIOD_SIZE  1024
#define DEFAULT_CAPTURE_PERIOD_SIZE  1024
static unsigned  PERIOD_SIZE  = DEFAULT_PERIOD_SIZE;
static unsigned  CAPTURE_PERIOD_SIZE = DEFAULT_CAPTURE_PERIOD_SIZE;
/* number of periods for low power playback */
#define PLAYBACK_PERIOD_COUNT 4
/* number of periods for capture */
#define CAPTURE_PERIOD_COUNT 4

/* minimum sleep time in out_write() when write threshold is not reached */
#define MIN_WRITE_SLEEP_US 5000

#define RESAMPLER_BUFFER_FRAMES (PERIOD_SIZE * 6)
#define RESAMPLER_BUFFER_SIZE (4 * RESAMPLER_BUFFER_FRAMES)

static unsigned int  DEFAULT_OUT_SAMPLING_RATE  = 48000;

/* sampling rate when using MM low power port */
#define MM_LOW_POWER_SAMPLING_RATE 44100
/* sampling rate when using MM full power port */
#define MM_FULL_POWER_SAMPLING_RATE 48000
/* sampling rate when using VX port for narrow band */
#define VX_NB_SAMPLING_RATE 8000

struct pcm_config pcm_config_out = {
    .channels = 2,
    .rate = MM_FULL_POWER_SAMPLING_RATE,
    .period_size = DEFAULT_PERIOD_SIZE,
    .period_count = PLAYBACK_PERIOD_COUNT,
    .format = PCM_FORMAT_S16_LE,
};

struct pcm_config pcm_config_in = {
    .channels = 2,
    .rate = MM_FULL_POWER_SAMPLING_RATE,
    .period_size = DEFAULT_CAPTURE_PERIOD_SIZE,
    .period_count = CAPTURE_PERIOD_COUNT,
    .format = PCM_FORMAT_S16_LE,
};

struct pcm_config pcm_config_bt = {
    .channels = 1,
    .rate = VX_NB_SAMPLING_RATE,
    .period_size = DEFAULT_PERIOD_SIZE,
    .period_count = PLAYBACK_PERIOD_COUNT,
    .format = PCM_FORMAT_S16_LE,
};

struct aml_audio_device {
    struct audio_hw_device hw_device;

    pthread_mutex_t lock;       /* see note below on mutex acquisition order */
    int mode;
    audio_devices_t in_device;
    audio_devices_t out_device;
    int in_call;
    struct aml_stream_in *active_input;
    struct aml_stream_out *active_output;

    bool mic_mute;
    struct audio_route *ar;
    struct echo_reference_itfe *echo_reference;
    bool low_power;
};

struct aml_stream_out {
    struct audio_stream_out stream;
    pthread_mutex_t lock;       /* see note below on mutex acquisition order */
    struct pcm_config config;
    struct pcm *pcm;
    struct resampler_itfe *resampler;
    char *buffer;
    size_t buffer_frames;
    bool standby;
    struct echo_reference_itfe *echo_reference;
    struct aml_audio_device *dev;
    int write_threshold;
    bool low_power;
};

#define MAX_PREPROCESSORS 3 /* maximum one AGC + one NS + one AEC per input stream */
struct aml_stream_in {
    struct audio_stream_in stream;
    pthread_mutex_t lock;       /* see note below on mutex acquisition order */
    struct pcm_config config;
    struct pcm *pcm;
    int device;
    struct resampler_itfe *resampler;
    struct resampler_buffer_provider buf_provider;
    int16_t *buffer;
    size_t frames_in;
    unsigned int requested_rate;
    bool standby;
    int source;
    struct echo_reference_itfe *echo_reference;
    bool need_echo_reference;
    effect_handle_t preprocessors[MAX_PREPROCESSORS];
    int num_preprocessors;
    int16_t *proc_buf;
    size_t proc_buf_size;
    size_t proc_frames_in;
    int16_t *ref_buf;
    size_t ref_buf_size;
    size_t ref_frames_in;
    int read_status;
    
    struct aml_audio_device *dev;
};

static char cache_buffer_bytes[64];
static uint cached_len=0;
static bool input_standby = true;
static bool output_standby = true;
static void select_output_device(struct aml_audio_device *adev);
static void select_input_device(struct aml_audio_device *adev);
static void select_devices(struct aml_audio_device *adev);
static int adev_set_voice_volume(struct audio_hw_device *dev, float volume);
static int do_input_standby(struct aml_stream_in *in);
static int do_output_standby(struct aml_stream_out *out);
static uint32_t out_get_sample_rate(const struct audio_stream *stream);

static int getprop_bool(const char * path)
{
    char buf[PROPERTY_VALUE_MAX];
    int ret = -1;

    ret = property_get(path, buf, NULL);
    if (ret > 0) {
        if(strcasecmp(buf,"true")==0 || strcmp(buf,"1")==0)
            return 1;
    }
    return 0;
}
static void select_devices(struct aml_audio_device *adev)
{
    LOGFUNC("%s(mode=%d, out_device=%#x)", __FUNCTION__, adev->mode, adev->out_device);
    int headset_on;
    int headphone_on;
    int speaker_on;
    int hdmi_on;
    int earpiece;   
    int mic_in;
    int headset_mic;

    headset_on = adev->out_device & AUDIO_DEVICE_OUT_WIRED_HEADSET;
    headphone_on = adev->out_device & AUDIO_DEVICE_OUT_WIRED_HEADPHONE;
    speaker_on = adev->out_device & AUDIO_DEVICE_OUT_SPEAKER;
    hdmi_on = adev->out_device & AUDIO_DEVICE_OUT_AUX_DIGITAL;
    earpiece =  adev->out_device & AUDIO_DEVICE_OUT_EARPIECE;
    mic_in = adev->in_device & ( AUDIO_DEVICE_IN_BUILTIN_MIC | AUDIO_DEVICE_IN_BACK_MIC);
    headset_mic = adev->in_device & AUDIO_DEVICE_IN_WIRED_HEADSET;
    
    LOGFUNC("~~~~ %s : hs=%d , hp=%d, sp=%d, hdmi=0x%x,earpiece=0x%x", __func__, headset_on, headphone_on, speaker_on,hdmi_on,earpiece);
    LOGFUNC("~~~~ %s : in_device(%#x), mic_in(%#x), headset_mic(%#x)", __func__, adev->in_device, mic_in, headset_mic);
    reset_mixer_state(adev->ar);
    LOGFUNC("****%s : output_standby=%d,input_standby=%d",__func__,output_standby,input_standby);
    if (hdmi_on)
        audio_route_apply_path(adev->ar, "hdmi");
    if (headphone_on || headset_on)
        audio_route_apply_path(adev->ar, "headphone");
    if (speaker_on ||earpiece )
        audio_route_apply_path(adev->ar, "speaker");
    if (mic_in)
        audio_route_apply_path(adev->ar, "main_mic");
    if (headset_mic)
        audio_route_apply_path(adev->ar, "headset-mic");
   
    update_mixer_state(adev->ar);

}

#if 0
static void select_output_device(struct aml_audio_device *adev)
{
    LOGFUNC("%s(mode=%d, out_device=%#x)", __FUNCTION__, adev->mode, adev->out_device);
    int headset_on;
    int headphone_on;
    int speaker_on;
    int hdmi_on;
    int earpiece;   
    //adev->cur_devices = adev->devices;

    headset_on = adev->out_device & AUDIO_DEVICE_OUT_WIRED_HEADSET;
    headphone_on = adev->out_device & AUDIO_DEVICE_OUT_WIRED_HEADPHONE;
    speaker_on = adev->out_device & AUDIO_DEVICE_OUT_SPEAKER;
    hdmi_on = adev->out_device & AUDIO_DEVICE_OUT_AUX_DIGITAL;
    earpiece =  adev->out_device & AUDIO_DEVICE_OUT_EARPIECE;
    LOGFUNC("~~~~ %s : hs=%d , hp=%d, sp=%d, hdmi=0x%x,earpiece=0x%x", __func__, headset_on, headphone_on, speaker_on,hdmi_on,earpiece);
    reset_mixer_state(adev->ar);
    
    if (hdmi_on)
        audio_route_apply_path(adev->ar, "hdmi");
    if (headphone_on || headset_on)
        audio_route_apply_path(adev->ar, "headphone");
    if (speaker_on ||earpiece )
        audio_route_apply_path(adev->ar, "speaker");
    update_mixer_state(adev->ar);
}

static void select_input_device(struct aml_audio_device *adev)
{
    int mic_in = adev->in_device & ( AUDIO_DEVICE_IN_BUILTIN_MIC | AUDIO_DEVICE_IN_BACK_MIC);
    int headset_mic = adev->in_device & AUDIO_DEVICE_IN_WIRED_HEADSET;
    LOGFUNC("~~~~ %s : in_device(%#x), mic_in(%#x), headset_mic(%#x)", __func__, adev->in_device, mic_in, headset_mic);

    //reset_mixer_state(adev->ar);
    if (mic_in)
    {
            audio_route_apply_path(adev->ar, "main_mic");
    }
    if (headset_mic)
    {
        audio_route_apply_path(adev->ar, "headset-mic");
    }
    update_mixer_state(adev->ar);
    return;
}
#endif
static void force_all_standby(struct aml_audio_device *adev)
{
    struct aml_stream_in *in;
    struct aml_stream_out *out;

    LOGFUNC("%s(%p)", __FUNCTION__, adev);

    if (adev->active_output) {
        out = adev->active_output;
        pthread_mutex_lock(&out->lock);
        do_output_standby(out);
        pthread_mutex_unlock(&out->lock);
    }

    if (adev->active_input) {
        in = adev->active_input;
        pthread_mutex_lock(&in->lock);
        do_input_standby(in);
        pthread_mutex_unlock(&in->lock);
    }
}
static void select_mode(struct aml_audio_device *adev)
{
    LOGFUNC("%s(out_device=%#x)", __FUNCTION__, adev->out_device);
    LOGFUNC("%s(in_device=%#x)", __FUNCTION__, adev->in_device);
    return;
    force_all_standby(adev);
    /* force earpiece route for in call state if speaker is the
        only currently selected route. This prevents having to tear
        down the modem PCMs to change route from speaker to earpiece
        after the ringtone is played, but doesn't cause a route
        change if a headset or bt device is already connected. If
        speaker is not the only thing active, just remove it from
        the route. We'll assume it'll never be used initally during
        a call. This works because we're sure that the audio policy
        manager will update the output device after the audio mode
        change, even if the device selection did not change. */
    if ((adev->out_device & AUDIO_DEVICE_OUT_ALL) == AUDIO_DEVICE_OUT_SPEAKER)
        adev->in_device = AUDIO_DEVICE_IN_BUILTIN_MIC & ~AUDIO_DEVICE_BIT_IN;
    else
        adev->out_device &= ~AUDIO_DEVICE_OUT_SPEAKER;
    
    select_output_device(adev);
    select_input_device(adev);
    return;
}
#if 0
/*
  type : 0 -> playback, 1 -> capture  
*/
#define MAX_CARD_NUM    2
int get_external_card(int type)
{
    int card_num = 1;   // start num, 0 is defualt sound card.
    
    struct stat card_stat;
    char fpath[256];
    int ret;


    while(card_num <= MAX_CARD_NUM)
    {
        snprintf(fpath, sizeof(fpath), "/proc/asound/card%d", card_num);

        ret = stat(fpath, &card_stat);
        //ALOGV("stat : %s : %d\n", fpath, ret);

        if(ret < 0)
        {
            ret = -1;
        }
        else
        {
            snprintf(fpath, sizeof(fpath), "/dev/snd/pcmC%uD0%c", card_num, 
                type ? 'c' : 'p');
            
            ret = stat(fpath, &card_stat);
            //ALOGV("stat : %s : %d\n", fpath, ret);

            if(ret == 0)
            {                
                return card_num;
            }

        }

        card_num++;
    }

    return ret;
}
#endif

static int get_aml_card(){
    int card = -1, err = 0;
    int fd = -1;
    unsigned fileSize = 512;
    char *read_buf = NULL, *pd = NULL;
    static const char *const SOUND_CARDS_PATH = "/proc/asound/cards";
    fd = open(SOUND_CARDS_PATH, O_RDONLY);
    if (fd < 0) {
        ALOGE("ERROR: failed to open config file %s error: %d\n", SOUND_CARDS_PATH, errno);
        close(fd);
        return -EINVAL;
    }

    read_buf = (char *)malloc(fileSize);
    if (!read_buf) {
        ALOGE("Failed to malloc read_buf");
        close(fd);
        return -ENOMEM;
    }
    memset(read_buf, 0x0, fileSize);
    err = read(fd, read_buf, fileSize);
    if (fd < 0) {
        ALOGE("ERROR: failed to read config file %s error: %d\n", SOUND_CARDS_PATH, errno);
        close(fd);
        return -EINVAL;
    }
    pd = strstr(read_buf, "AML");
    card = *(pd - 3) - '0';

OUT:
    free(read_buf);
    close(fd);
    return card;
}

static int get_pcm_bt_port(){
    int port = -1, err = 0;
    int fd = -1;
    unsigned fileSize = 512;
    char *read_buf = NULL, *pd = NULL;
    static const char *const SOUND_PCM_PATH = "/proc/asound/pcm";
    fd = open(SOUND_PCM_PATH, O_RDONLY);
    if (fd < 0) {
        ALOGE("ERROR: failed to open config file %s error: %d\n", SOUND_PCM_PATH, errno);
        close(fd);
        return -EINVAL;
    }

    read_buf = (char *)malloc(fileSize);
    if (!read_buf) {
        ALOGE("Failed to malloc read_buf");
        close(fd);
        return -ENOMEM;
    }
    memset(read_buf, 0x0, fileSize);
    err = read(fd, read_buf, fileSize);
    if (fd < 0) {
        ALOGE("ERROR: failed to read config file %s error: %d\n", SOUND_PCM_PATH, errno);
        close(fd);
        return -EINVAL;
    }
    pd = strstr(read_buf, "pcm2bt-pcm");
    port = *(pd + 11) - '0';

OUT:
    free(read_buf);
    close(fd);
    return port;
}

static int get_spdif_port(){
    int port = -1, err = 0;
    int fd = -1;
    unsigned fileSize = 512;
    char *read_buf = NULL, *pd = NULL;
    static const char *const SOUND_PCM_PATH = "/proc/asound/pcm";
    fd = open(SOUND_PCM_PATH, O_RDONLY);
    if (fd < 0) {
        ALOGE("ERROR: failed to open config file %s error: %d\n", SOUND_PCM_PATH, errno);
        close(fd);
        return -EINVAL;
    }

    read_buf = (char *)malloc(fileSize);
    if (!read_buf) {
        ALOGE("Failed to malloc read_buf");
        close(fd);
        return -ENOMEM;
    }
    memset(read_buf, 0x0, fileSize);
    err = read(fd, read_buf, fileSize);
    if (fd < 0) {
        ALOGE("ERROR: failed to read config file %s error: %d\n", SOUND_PCM_PATH, errno);
        close(fd);
        return -EINVAL;
    }
    pd = strstr(read_buf, "SPDIF");
    port = *(pd -3) - '0';

OUT:
    free(read_buf);
    close(fd);
    return port;
}
/* must be called with hw device and output stream mutexes locked */
static int start_output_stream(struct aml_stream_out *out)
{
    struct aml_audio_device *adev = out->dev;
    unsigned int card = CARD_AMLOGIC_DEFAULT;
    unsigned int port = PORT_MM;
    int ret;

    LOGFUNC("%s(adev->out_device=%#x, adev->mode=%d)", __FUNCTION__, adev->out_device, adev->mode);

    adev->active_output = out;

    if (adev->mode != AUDIO_MODE_IN_CALL) {
        /* FIXME: only works if only one output can be active at a time */
        select_devices(adev);
    }
    
    card = get_aml_card();
    if (adev->out_device & AUDIO_DEVICE_OUT_ALL_SCO){
        port = get_pcm_bt_port();
        out->config = pcm_config_bt;
    } else {
        port = PORT_MM;
        out->config = pcm_config_out;
    }
    LOGFUNC("*%s, open card(%d) port(%d)-------", __FUNCTION__,card,port);
    //if(getprop_bool("media.libplayer.wfd")){
    //    out->config.period_size = PERIOD_SIZE/2;
    //}
    //else{
        out->config.period_size = PERIOD_SIZE;        
    //}

    /* default to low power: will be corrected in out_write if necessary before first write to
     * tinyalsa.
     */
    out->write_threshold = out->config.period_size*PLAYBACK_PERIOD_COUNT;
    out->config.start_threshold = out->config.period_size * PLAYBACK_PERIOD_COUNT;
    out->config.avail_min = 0;//SHORT_PERIOD_SIZE;
    
    out->pcm = pcm_open(card, port, PCM_OUT /*| PCM_MMAP | PCM_NOIRQ*/, &(out->config));

    if (!pcm_is_ready(out->pcm)) {
        ALOGE("cannot open pcm_out driver: %s", pcm_get_error(out->pcm));
        pcm_close(out->pcm);
        adev->active_output = NULL;
        return -ENOMEM;
    }
    if(out->config.rate != out_get_sample_rate(&out->stream.common)){
    
        LOGFUNC("%s(out->config.rate=%d, out->config.channels=%d)", 
                __FUNCTION__, out->config.rate, out->config.channels);
        ret = create_resampler(out_get_sample_rate(&out->stream.common),
                out->config.rate,
                out->config.channels,
                RESAMPLER_QUALITY_DEFAULT,
                NULL,
                &out->resampler);
        if (ret != 0)
        {
            ALOGE("cannot create resampler for output");
            return -ENOMEM;
        }
        out->buffer_frames = (pcm_config_out.period_size * out->config.rate) /
                out_get_sample_rate(&out->stream.common) + 1;
        out->buffer = malloc(pcm_frames_to_bytes(out->pcm, out->buffer_frames));
        if (out->buffer == NULL){
            ALOGE("cannot malloc memory for out->buffer");
            return -ENOMEM;
        }
    }

    LOGFUNC("channels=%d---format=%d---period_count%d---period_size%d---rate=%d---",
                         out->config.channels, out->config.format, out->config.period_count, 
                         out->config.period_size, out->config.rate);

    if (adev->echo_reference != NULL)
        out->echo_reference = adev->echo_reference;
    if (out->resampler){
        out->resampler->reset(out->resampler);
    }

    return 0;
}

static int check_input_parameters(uint32_t sample_rate, audio_format_t format, int channel_count)
{
    LOGFUNC("%s(sample_rate=%d, format=%d, channel_count=%d)", __FUNCTION__, sample_rate, format, channel_count);

    if (format != AUDIO_FORMAT_PCM_16_BIT)
        return -EINVAL;

    if ((channel_count < 1) || (channel_count > 2))
        return -EINVAL;

    switch(sample_rate) {
    case 8000:
    case 11025:
    case 16000:
    case 22050:
    case 24000:
    case 32000:
    case 44100:
    case 48000:
        break;
    default:
        return -EINVAL;
    }

    return 0;
}

static size_t get_input_buffer_size(uint32_t sample_rate, audio_format_t format, int channel_count)
{
    size_t size;
    size_t device_rate;

    LOGFUNC("%s(sample_rate=%d, format=%d, channel_count=%d)", __FUNCTION__, sample_rate, format, channel_count);

    if (check_input_parameters(sample_rate, format, channel_count) != 0)
        return 0;

    /* take resampling into account and return the closest majoring
    multiple of 16 frames, as audioflinger expects audio buffers to
    be a multiple of 16 frames */
    size = (pcm_config_in.period_size * sample_rate) / pcm_config_in.rate;
    size = ((size + 15) / 16) * 16;

    return size * channel_count * sizeof(short);
}

static void add_echo_reference(struct aml_stream_out *out,
                               struct echo_reference_itfe *reference)
{
    pthread_mutex_lock(&out->lock);
    out->echo_reference = reference;
    pthread_mutex_unlock(&out->lock);
}

static void remove_echo_reference(struct aml_stream_out *out,
                                  struct echo_reference_itfe *reference)
{
    pthread_mutex_lock(&out->lock);
    if (out->echo_reference == reference) {
        /* stop writing to echo reference */
        reference->write(reference, NULL);
        out->echo_reference = NULL;
    }
    pthread_mutex_unlock(&out->lock);
}

static void put_echo_reference(struct aml_audio_device *adev,
                          struct echo_reference_itfe *reference)
{
    if (adev->echo_reference != NULL &&
            reference == adev->echo_reference) {
        if (adev->active_output != NULL)
            remove_echo_reference(adev->active_output, reference);
        release_echo_reference(reference);
        adev->echo_reference = NULL;
    }
}

static struct echo_reference_itfe *get_echo_reference(struct aml_audio_device *adev,
                                               audio_format_t format,
                                               uint32_t channel_count,
                                               uint32_t sampling_rate)
{
    put_echo_reference(adev, adev->echo_reference);
    if (adev->active_output != NULL) {
        struct audio_stream *stream = &adev->active_output->stream.common;
        uint32_t wr_channel_count = popcount(stream->get_channels(stream));
        uint32_t wr_sampling_rate = stream->get_sample_rate(stream);

        int status = create_echo_reference(AUDIO_FORMAT_PCM_16_BIT,
                                           channel_count,
                                           sampling_rate,
                                           AUDIO_FORMAT_PCM_16_BIT,
                                           wr_channel_count,
                                           wr_sampling_rate,
                                           &adev->echo_reference);
        if (status == 0)
            add_echo_reference(adev->active_output, adev->echo_reference);
    }
    return adev->echo_reference;
}

static int get_playback_delay(struct aml_stream_out *out,
                       size_t frames,
                       struct echo_reference_buffer *buffer)
{

    size_t kernel_frames;
    int status;
    status = pcm_get_htimestamp(out->pcm, &kernel_frames, &buffer->time_stamp);
    if (status < 0) {
        buffer->time_stamp.tv_sec  = 0;
        buffer->time_stamp.tv_nsec = 0;
        buffer->delay_ns           = 0;
        ALOGV("get_playback_delay(): pcm_get_htimestamp error,"
                "setting playbackTimestamp to 0");
        return status;
    }
    kernel_frames = pcm_get_buffer_size(out->pcm) - kernel_frames;
    ALOGV("~~pcm_get_buffer_size(out->pcm)=%d",pcm_get_buffer_size(out->pcm));
    /* adjust render time stamp with delay added by current driver buffer.
     * Add the duration of current frame as we want the render time of the last
     * sample being written. */
    buffer->delay_ns = (long)(((int64_t)(kernel_frames + frames)* 1000000000)/
                            DEFAULT_OUT_SAMPLING_RATE);

    ALOGV("get_playback_delay time_stamp = [%ld].[%ld], delay_ns: [%d],"
         "kernel_frames:[%d]",
        buffer->time_stamp.tv_sec , buffer->time_stamp.tv_nsec, buffer->delay_ns,
        kernel_frames);
    return 0;
}

static uint32_t out_get_sample_rate(const struct audio_stream *stream)
{
    //struct aml_stream_out *out = (struct aml_stream_out *)stream;
    //LOGFUNC("%s(%p)", __FUNCTION__, stream);
    //return out->config.rate;
    return DEFAULT_OUT_SAMPLING_RATE;
}

static int out_set_sample_rate(struct audio_stream *stream, uint32_t rate)
{
    return 0;
}

static size_t out_get_buffer_size(const struct audio_stream *stream)
{
    struct aml_stream_out *out = (struct aml_stream_out *)stream;

    //LOGFUNC("%s(out->config.rate=%d)", __FUNCTION__, out->config.rate);

    /* take resampling into account and return the closest majoring
         * multiple of 16 frames, as audioflinger expects audio buffers to
         * be a multiple of 16 frames
         */
    size_t size = (out->config.period_size * DEFAULT_OUT_SAMPLING_RATE) / out->config.rate;
    size = ((size + 15) / 16) * 16;
    return size * audio_stream_frame_size((struct audio_stream *)stream);
}

static audio_channel_mask_t out_get_channels(const struct audio_stream *stream)
{
    return AUDIO_CHANNEL_OUT_STEREO;
}

static audio_format_t out_get_format(const struct audio_stream *stream)
{
    return AUDIO_FORMAT_PCM_16_BIT;
}

static int out_set_format(struct audio_stream *stream, audio_format_t format)
{
    return 0;
}

/* must be called with hw device and output stream mutexes locked */
static int do_output_standby(struct aml_stream_out *out)
{
    struct aml_audio_device *adev = out->dev;

    LOGFUNC("%s(%p)", __FUNCTION__, out);

    if (!out->standby) {
        pcm_close(out->pcm);
        out->pcm = NULL;

        adev->active_output = 0;

        /* if in call, don't turn off the output stage. This will
        be done when the call is ended */
        if (adev->mode != AUDIO_MODE_IN_CALL) {
            /* FIXME: only works if only one output can be active at a time */

           // reset_mixer_state(adev->ar);
           // update_mixer_state(adev->ar);
        }

        /* stop writing to echo reference */
        if (out->echo_reference != NULL) {
            out->echo_reference->write(out->echo_reference, NULL);
            out->echo_reference = NULL;
        }

        out->standby = 1;
        output_standby = 1;
        LOGFUNC("%s : output_standby=%d,input_standby=%d", __FUNCTION__, output_standby,input_standby);
        if(output_standby && input_standby){
         //   reset_mixer_state(adev->ar);
         //   update_mixer_state(adev->ar);
        }
    }
    return 0;
}

static int out_standby(struct audio_stream *stream)
{
    struct aml_stream_out *out = (struct aml_stream_out *)stream;
    int status;

    pthread_mutex_lock(&out->dev->lock);
    pthread_mutex_lock(&out->lock);
    status = do_output_standby(out);
    pthread_mutex_unlock(&out->lock);
    pthread_mutex_unlock(&out->dev->lock);
    return status;
}

static int out_dump(const struct audio_stream *stream, int fd)
{
    LOGFUNC("%s(%p, %d)", __FUNCTION__, stream, fd);
    return 0;
}

static int out_set_parameters(struct audio_stream *stream, const char *kvpairs)
{
    struct aml_stream_out *out = (struct aml_stream_out *)stream;
    struct aml_audio_device *adev = out->dev;
    struct aml_stream_in *in;
    struct str_parms *parms;
    char *str;
    char value[32];
    int ret;
    uint val = 0;
    bool force_input_standby = false;

    LOGFUNC("%s(kvpairs(%s), out_device=%#x)", __FUNCTION__, kvpairs, adev->out_device);
    parms = str_parms_create_str(kvpairs);

    ret = str_parms_get_str(parms, AUDIO_PARAMETER_STREAM_ROUTING, value, sizeof(value));
    if (ret >= 0) {
        val = atoi(value);
        pthread_mutex_lock(&adev->lock);
        pthread_mutex_lock(&out->lock);
        if (((adev->out_device & AUDIO_DEVICE_OUT_ALL) != val) && (val != 0)) {
            if (out == adev->active_output) {
                do_output_standby(out);
                /* a change in output device may change the microphone selection */
                if (adev->active_input &&
                        adev->active_input->source == AUDIO_SOURCE_VOICE_COMMUNICATION) {
                    force_input_standby = true;
                }
                /* force standby if moving to/from HDMI */
                if (((val & AUDIO_DEVICE_OUT_AUX_DIGITAL) ^
                        (adev->out_device & AUDIO_DEVICE_OUT_AUX_DIGITAL)) ||
                        ((val & AUDIO_DEVICE_OUT_DGTL_DOCK_HEADSET) ^
                        (adev->out_device & AUDIO_DEVICE_OUT_DGTL_DOCK_HEADSET)))
                    do_output_standby(out);
            }
            adev->out_device &= ~AUDIO_DEVICE_OUT_ALL;
            adev->out_device |= val;
            select_devices(adev);
        }
        pthread_mutex_unlock(&out->lock);
        if (force_input_standby) {
            in = adev->active_input;
            pthread_mutex_lock(&in->lock);
            do_input_standby(in);
            pthread_mutex_unlock(&in->lock);
        }
        pthread_mutex_unlock(&adev->lock);
	 goto exit;	
    }
    int sr = 0; 
    ret = str_parms_get_int(parms, AUDIO_PARAMETER_STREAM_SAMPLING_RATE, &sr);
    if (ret >= 0) {
        if(sr > 0){
            ALOGI("audio hw sampling_rate change from %d to %d \n",DEFAULT_OUT_SAMPLING_RATE,sr);
            DEFAULT_OUT_SAMPLING_RATE = sr;
            pcm_config_out.rate = DEFAULT_OUT_SAMPLING_RATE;
            out->config.rate = DEFAULT_OUT_SAMPLING_RATE;
                  pthread_mutex_lock(&adev->lock);
                  pthread_mutex_lock(&out->lock);           
            if(!out->standby && (out == adev->active_output)){
                do_output_standby(out);
                start_output_stream(out);
                out->standby = 0;
                output_standby = 0;
            }
            pthread_mutex_unlock(&adev->lock);
            pthread_mutex_unlock(&out->lock);               
            
        }
	 	goto exit;	
    }
    int frame_size = 0; 
    ret = str_parms_get_int(parms, AUDIO_PARAMETER_STREAM_FRAME_COUNT, &frame_size);
    if (ret >= 0) {
        if(frame_size > 0){
            ALOGI("audio hw frame size change from %d to %d \n",PERIOD_SIZE,frame_size);
            PERIOD_SIZE = frame_size;///PLAYBACK_PERIOD_COUNT;
            pcm_config_out.period_size = PERIOD_SIZE;
            out->config.period_size = PERIOD_SIZE;
            pthread_mutex_lock(&adev->lock);
            pthread_mutex_lock(&out->lock);         
            if(!out->standby && (out == adev->active_output)){
                do_output_standby(out);
                start_output_stream(out);
                out->standby = 0;
                output_standby = 0;
            }
            pthread_mutex_unlock(&adev->lock);
            pthread_mutex_unlock(&out->lock);               
            
        }
    }   
exit:	
    str_parms_destroy(parms);
    return ret;
}

static char * out_get_parameters(const struct audio_stream *stream, const char *keys)
{
    return strdup("");
}

static uint32_t out_get_latency(const struct audio_stream_out *stream)
{
    struct aml_stream_out *out = (struct aml_stream_out *)stream;    
    return (out->config.period_size * out->config.period_count * 1000) / out->config.rate;
}

static int out_set_volume(struct audio_stream_out *stream, float left,
                          float right)
{
    return -ENOSYS;
}

static ssize_t out_write(struct audio_stream_out *stream, const void* buffer,
                         size_t bytes)
{
    int ret;
    struct aml_stream_out *out = (struct aml_stream_out *)stream;
    struct aml_audio_device *adev = out->dev;
    size_t frame_size = audio_stream_frame_size(&out->stream.common);
    size_t in_frames = bytes / frame_size;
    size_t out_frames;
    bool force_input_standby = false;
    int16_t *in_buffer = (int16_t *)buffer;
    struct aml_stream_in *in;
    char output_buffer_bytes[RESAMPLER_BUFFER_SIZE+128];
    uint ouput_len;
    char *data,  *data_dst;
    volatile char *data_src;
    uint i, total_len;
    //LOGFUNC("entring:%s(out->echo_reference=%p, in_frames=%d)", __FUNCTION__, out->echo_reference, in_frames);

    /* acquiring hw device mutex systematically is useful if a low priority thread is waiting
     * on the output stream mutex - e.g. executing select_mode() while holding the hw device
     * mutex
     */
    pthread_mutex_lock(&adev->lock);
    pthread_mutex_lock(&out->lock);
    if (out->standby) {
        ret = start_output_stream(out);
        if (ret != 0) {
            pthread_mutex_unlock(&adev->lock);
            goto exit;
        }
        out->standby = false;
        output_standby = false;
        /* a change in output device may change the microphone selection */
        if (adev->active_input &&
                adev->active_input->source == AUDIO_SOURCE_VOICE_COMMUNICATION)
            force_input_standby = true;
    }
    pthread_mutex_unlock(&adev->lock);
#if 1
    /* Reduce number of channels, if necessary */
    if (popcount(out_get_channels(&stream->common)) >
                 (int)out->config.channels) {
        unsigned int i;
    
        /* Discard right channel */
        for (i = 1; i < in_frames; i++)
            in_buffer[i] = in_buffer[i * 2];
    
        /* The frame size is now half */
        frame_size /= 2;
    }
#endif
    /* only use resampler if required */
    if (out->config.rate != out_get_sample_rate(&stream->common)) {
        out_frames = out->buffer_frames;
        out->resampler->resample_from_input(out->resampler,
                                            in_buffer, &in_frames,
                                            (int16_t*)out->buffer, &out_frames);
        in_buffer = (int16_t*)out->buffer;
    } else {
        out_frames = in_frames;
    }
    if (out->echo_reference != NULL) {

        struct echo_reference_buffer b;
        b.raw = (void *)buffer;
        b.frame_count = in_frames;
        get_playback_delay(out, out_frames, &b);
        out->echo_reference->write(out->echo_reference, &b);
    }

#if 0   
        FILE *fp1=fopen("/data/audio_out","a+"); 
        if(fp1){ 
            int flen=fwrite((char *)in_buffer,1,out_frames * frame_size,fp1); 
            LOGFUNC("flen = %d---outlen=%d ", flen, out_frames * frame_size);
            fclose(fp1); 
        }else{
            LOGFUNC("could not open file:audio_out");
        }
#endif
#if 0
    if(out->config.rate != DEFAULT_OUT_SAMPLING_RATE) {
        total_len = out_frames*frame_size + cached_len;


        LOGFUNC("total_len(%d) = resampler_out_len(%d) + cached_len111(%d)", total_len, out_frames*frame_size, cached_len);

        data_src = (char *)cache_buffer_bytes;
        data_dst = (char *)output_buffer_bytes;


    
        /*write_back data from cached_buffer*/
        if(cached_len){
            memcpy((void *)data_dst, (void *)data_src, cached_len);
            data_dst += cached_len;
        }
        
        ouput_len = total_len &(~0x3f);
        data = (char*)in_buffer;
        
        memcpy((void *)data_dst, (void *)data, ouput_len-cached_len);
        data += (ouput_len-cached_len);
        cached_len = total_len & 0x3f;
        data_src = (char *)cache_buffer_bytes;
        
        /*save data to cached_buffer*/
        if(cached_len){
            memcpy((void *)data_src, (void *)data, cached_len);
        }
        
        ret = pcm_write(out->pcm, (void *)output_buffer_bytes, ouput_len);

    }else{
        ret = pcm_write(out->pcm, in_buffer, out_frames * frame_size);
    }
#else
    ret = pcm_write(out->pcm, in_buffer, out_frames * frame_size);
#endif
    exit:
        pthread_mutex_unlock(&out->lock);
        //fixed me: It is not a good way to clear android audioflinger buffer,but when pcm write error, audioflinger can't break out.
        memset(in_buffer,0,bytes);
        if (ret != 0) {
            usleep(bytes * 1000000 / audio_stream_frame_size(&stream->common) /
                   out_get_sample_rate(&stream->common));
        }
    
        if (force_input_standby) {
            pthread_mutex_lock(&adev->lock);
            if (adev->active_input) {
                in = adev->active_input;
                pthread_mutex_lock(&in->lock);
                do_input_standby(in);
                pthread_mutex_unlock(&in->lock);
            }
            pthread_mutex_unlock(&adev->lock);
        }
    return bytes;
}

static int out_get_render_position(const struct audio_stream_out *stream,
                                   uint32_t *dsp_frames)
{
    return -EINVAL;
}

static int out_add_audio_effect(const struct audio_stream *stream, effect_handle_t effect)
{
    return 0;
}

static int out_remove_audio_effect(const struct audio_stream *stream, effect_handle_t effect)
{
    return 0;
}
static int out_get_next_write_timestamp(const struct audio_stream_out *stream,
                                        int64_t *timestamp)
{
    return -EINVAL;
}

static int get_next_buffer(struct resampler_buffer_provider *buffer_provider, 
                                                         struct resampler_buffer* buffer);
static void release_buffer(struct resampler_buffer_provider *buffer_provider,
                                                         struct resampler_buffer* buffer);


/** audio_stream_in implementation **/

/* must be called with hw device and input stream mutexes locked */
static int start_input_stream(struct aml_stream_in *in)
{
    int ret = 0;
    unsigned int card = CARD_AMLOGIC_DEFAULT;
    unsigned int port = PORT_MM;

    struct aml_audio_device *adev = in->dev;
    LOGFUNC("%s(need_echo_reference=%d, channels=%d, rate=%d, requested_rate=%d, mode= %d)", 
        __FUNCTION__, in->need_echo_reference, in->config.channels, in->config.rate, in->requested_rate, adev->mode);
    adev->active_input = in;

    if (adev->mode != AUDIO_MODE_IN_CALL) {
        adev->in_device &= ~AUDIO_DEVICE_IN_ALL;
        adev->in_device |= in->device;
        select_devices(adev);
    }
    card = get_aml_card();

    ALOGV("%s(in->requested_rate=%d, in->config.rate=%d)", 
             __FUNCTION__, in->requested_rate, in->config.rate);
    if (adev->in_device & AUDIO_DEVICE_IN_BLUETOOTH_SCO_HEADSET){
        port = get_pcm_bt_port();
    } else if(getprop_bool("sys.hdmiIn.Capture")){
        port = get_spdif_port();
    } else {
        port = PORT_MM;
    }
    LOGFUNC("*%s, open card(%d) port(%d)-------", __FUNCTION__,card,port);

    if(getprop_bool("media.libplayer.wfd")){
        CAPTURE_PERIOD_SIZE = DEFAULT_CAPTURE_PERIOD_SIZE/2;
        in->config.period_size = CAPTURE_PERIOD_SIZE;
    }
    else
    {
        CAPTURE_PERIOD_SIZE = DEFAULT_CAPTURE_PERIOD_SIZE;
        in->config.period_size = CAPTURE_PERIOD_SIZE;
    }
    if (in->need_echo_reference && in->echo_reference == NULL) {
        in->echo_reference = get_echo_reference(adev,
                                        AUDIO_FORMAT_PCM_16_BIT,
                                        in->config.channels,
                                        in->requested_rate);
        LOGFUNC("%s(after get_echo_ref.... now in->echo_reference = %p)", __FUNCTION__, in->echo_reference);
    }
    /* this assumes routing is done previously */
    in->pcm = pcm_open(card, port, PCM_IN, &in->config);
    if (!pcm_is_ready(in->pcm)) {
        ALOGE("cannot open pcm_in driver: %s", pcm_get_error(in->pcm));
        pcm_close(in->pcm);
        adev->active_input = NULL;
        return -ENOMEM;
    }
    ALOGD("pcm_open in: card(%d), port(%d)", card, port);

    /* if no supported sample rate is available, use the resampler */
    if (in->resampler) {
        in->resampler->reset(in->resampler);
        in->frames_in = 0;
    }
    return 0;
}

static uint32_t in_get_sample_rate(const struct audio_stream *stream)
{
    struct aml_stream_in *in = (struct aml_stream_in *)stream;

    return in->requested_rate;
}

static int in_set_sample_rate(struct audio_stream *stream, uint32_t rate)
{
    return 0;
}

static size_t in_get_buffer_size(const struct audio_stream *stream)
{
    struct aml_stream_in *in = (struct aml_stream_in *)stream;

    return get_input_buffer_size(in->config.rate,
                                 AUDIO_FORMAT_PCM_16_BIT,
                                 in->config.channels);
}

static audio_channel_mask_t in_get_channels(const struct audio_stream *stream)
{
     struct aml_stream_in *in = (struct aml_stream_in *)stream;

    if (in->config.channels == 1) {
        return AUDIO_CHANNEL_IN_MONO;
    } else {
        return AUDIO_CHANNEL_IN_STEREO;
    }
}

static audio_format_t in_get_format(const struct audio_stream *stream)
{
    return AUDIO_FORMAT_PCM_16_BIT;
}

static int in_set_format(struct audio_stream *stream, audio_format_t format)
{
    return 0;
}

/* must be called with hw device and input stream mutexes locked */
static int do_input_standby(struct aml_stream_in *in)
{
    struct aml_audio_device *adev = in->dev;

    LOGFUNC("%s(%p)", __FUNCTION__, in);
    if (!in->standby) {
        pcm_close(in->pcm);
        in->pcm = NULL;

        adev->active_input = 0;
        if (adev->mode != AUDIO_MODE_IN_CALL) {
            adev->in_device &= ~AUDIO_DEVICE_IN_ALL;
            //select_input_device(adev);
        }

        if (in->echo_reference != NULL) {
            /* stop reading from echo reference */
            in->echo_reference->read(in->echo_reference, NULL);
            put_echo_reference(adev, in->echo_reference);
            in->echo_reference = NULL;
        }

        in->standby = 1;
        input_standby = true;
        LOGFUNC("%s : output_standby=%d,input_standby=%d", __FUNCTION__, output_standby,input_standby);
        if(output_standby && input_standby){
           // reset_mixer_state(adev->ar);
           // update_mixer_state(adev->ar);
        }
    }
    return 0;
}
static int in_standby(struct audio_stream *stream)
{
    struct aml_stream_in *in = (struct aml_stream_in *)stream;
    int status;
    LOGFUNC("%s(%p)", __FUNCTION__, stream);

    pthread_mutex_lock(&in->dev->lock);
    pthread_mutex_lock(&in->lock);
    status = do_input_standby(in);
    pthread_mutex_unlock(&in->lock);
    pthread_mutex_unlock(&in->dev->lock);
    return status;
}

static int in_dump(const struct audio_stream *stream, int fd)
{
    return 0;
}

static int in_set_parameters(struct audio_stream *stream, const char *kvpairs)
{
    struct aml_stream_in *in = (struct aml_stream_in *)stream;
    struct aml_audio_device *adev = in->dev;
    struct str_parms *parms;
    char *str;
    char value[32];
    int ret, val = 0;
    bool do_standby = false;

    LOGFUNC("%s(%p, %s)", __FUNCTION__, stream, kvpairs);
    parms = str_parms_create_str(kvpairs);

    ret = str_parms_get_str(parms, AUDIO_PARAMETER_STREAM_INPUT_SOURCE, value, sizeof(value));

    pthread_mutex_lock(&adev->lock);
    pthread_mutex_lock(&in->lock);
    if (ret >= 0) {
        val = atoi(value);
        /* no audio source uses val == 0 */
        if ((in->source != val) && (val != 0)) {
            in->source = val;
            do_standby = true;
        }
    }

    ret = str_parms_get_str(parms, AUDIO_PARAMETER_STREAM_ROUTING, value, sizeof(value));
    if (ret >= 0) {
        val = atoi(value) & ~AUDIO_DEVICE_BIT_IN;
        if ((in->device != val) && (val != 0)) {
            in->device = val;
            do_standby = true;
        }
    }

    if (do_standby)
        do_input_standby(in);
    pthread_mutex_unlock(&in->lock);
    pthread_mutex_unlock(&adev->lock);
    
    int framesize = 0;  
    ret = str_parms_get_int(parms, AUDIO_PARAMETER_STREAM_FRAME_COUNT, &framesize);
    
    if (ret >= 0) {
        if(framesize > 0){
            ALOGI("Reset audio input hw frame size from %d to %d\n",CAPTURE_PERIOD_SIZE*CAPTURE_PERIOD_COUNT,framesize);
            CAPTURE_PERIOD_SIZE = framesize/CAPTURE_PERIOD_COUNT;
            pcm_config_in.period_size = CAPTURE_PERIOD_SIZE;
            in->config.period_size = CAPTURE_PERIOD_SIZE;
            
            pthread_mutex_lock(&adev->lock);
            pthread_mutex_lock(&in->lock);
            
            if(!in->standby && (in == adev->active_input)){
                do_input_standby(in);
                start_input_stream(in);
                in->standby = 0;
                input_standby = false;
            }
            
            pthread_mutex_unlock(&in->lock);
            pthread_mutex_unlock(&adev->lock);
        }
    }

    str_parms_destroy(parms);
    return ret;
}

static char * in_get_parameters(const struct audio_stream *stream,
                                const char *keys)
{
    return strdup("");
}

static int in_set_gain(struct audio_stream_in *stream, float gain)
{
    return 0;
}

static void get_capture_delay(struct aml_stream_in *in,
                       size_t frames,
                       struct echo_reference_buffer *buffer)
{
    /* read frames available in kernel driver buffer */
    size_t kernel_frames;
    struct timespec tstamp;
    long buf_delay;
    long rsmp_delay;
    long kernel_delay;
    long delay_ns;
    int rsmp_mul = in->config.rate/VX_NB_SAMPLING_RATE;
    if (pcm_get_htimestamp(in->pcm, &kernel_frames, &tstamp) < 0) {
        buffer->time_stamp.tv_sec  = 0;
        buffer->time_stamp.tv_nsec = 0;
        buffer->delay_ns           = 0;
        ALOGW("read get_capture_delay(): pcm_htimestamp error");
        return;
    }

    /* read frames available in audio HAL input buffer
     * add number of frames being read as we want the capture time of first sample
     * in current buffer */
    buf_delay = (long)(((int64_t)(in->frames_in + in->proc_frames_in * rsmp_mul) * 1000000000)
                                    / in->config.rate);
    /* add delay introduced by resampler */
    rsmp_delay = 0;
    if (in->resampler) {
        rsmp_delay = in->resampler->delay_ns(in->resampler);
    }

    kernel_delay = (long)(((int64_t)kernel_frames * 1000000000) / in->config.rate);

    delay_ns = kernel_delay + buf_delay + rsmp_delay;

    buffer->time_stamp = tstamp;
    buffer->delay_ns   = delay_ns;
    ALOGV("get_capture_delay time_stamp = [%ld].[%ld], delay_ns: [%d],"
        " kernel_delay:[%ld], buf_delay:[%ld], rsmp_delay:[%ld], kernel_frames:[%d], "
         "in->frames_in:[%d], in->proc_frames_in:[%d], frames:[%d]",
         buffer->time_stamp.tv_sec , buffer->time_stamp.tv_nsec, buffer->delay_ns,
         kernel_delay, buf_delay, rsmp_delay, kernel_frames,
         in->frames_in, in->proc_frames_in, frames);

}

static int32_t update_echo_reference(struct aml_stream_in *in, size_t frames)
{
    struct echo_reference_buffer b;
    b.delay_ns = 0;

    ALOGV("update_echo_reference, frames = [%d], in->ref_frames_in = [%d],  "
          "b.frame_count = [%d]",
         frames, in->ref_frames_in, frames - in->ref_frames_in);
    if (in->ref_frames_in < frames) {
        if (in->ref_buf_size < frames) {
            in->ref_buf_size = frames;
            in->ref_buf = (int16_t *)realloc(in->ref_buf,
                                             in->ref_buf_size *
                                                 in->config.channels * sizeof(int16_t));
        }

        b.frame_count = frames - in->ref_frames_in;
        b.raw = (void *)(in->ref_buf + in->ref_frames_in * in->config.channels);

        get_capture_delay(in, frames, &b);
        LOGFUNC("update_echo_reference  return ::b.delay_ns=%d", b.delay_ns);

        if (in->echo_reference->read(in->echo_reference, &b) == 0)
        {
            in->ref_frames_in += b.frame_count;
            ALOGV("update_echo_reference: in->ref_frames_in:[%d], "
                    "in->ref_buf_size:[%d], frames:[%d], b.frame_count:[%d]",
                 in->ref_frames_in, in->ref_buf_size, frames, b.frame_count);
        }
    } else
        ALOGW("update_echo_reference: NOT enough frames to read ref buffer");
    return b.delay_ns;
}

static int set_preprocessor_param(effect_handle_t handle,
                           effect_param_t *param)
{
    uint32_t size = sizeof(int);
    uint32_t psize = ((param->psize - 1) / sizeof(int) + 1) * sizeof(int) +
                        param->vsize;

    int status = (*handle)->command(handle,
                                   EFFECT_CMD_SET_PARAM,
                                   sizeof (effect_param_t) + psize,
                                   param,
                                   &size,
                                   &param->status);
    if (status == 0)
        status = param->status;

    return status;
}

static int set_preprocessor_echo_delay(effect_handle_t handle,
                                     int32_t delay_us)
{
    uint32_t buf[sizeof(effect_param_t) / sizeof(uint32_t) + 2];
    effect_param_t *param = (effect_param_t *)buf;

    param->psize = sizeof(uint32_t);
    param->vsize = sizeof(uint32_t);
    *(uint32_t *)param->data = AEC_PARAM_ECHO_DELAY;
    *((int32_t *)param->data + 1) = delay_us;

    return set_preprocessor_param(handle, param);
}

static void push_echo_reference(struct aml_stream_in *in, size_t frames)
{
    /* read frames from echo reference buffer and update echo delay
     * in->ref_frames_in is updated with frames available in in->ref_buf */
    int32_t delay_us = update_echo_reference(in, frames)/1000;
    int i;
    audio_buffer_t buf;

    if (in->ref_frames_in < frames)
        frames = in->ref_frames_in;

    buf.frameCount = frames;
    buf.raw = in->ref_buf;

    for (i = 0; i < in->num_preprocessors; i++) {
        if ((*in->preprocessors[i])->process_reverse == NULL)
            continue;

        (*in->preprocessors[i])->process_reverse(in->preprocessors[i],
                                               &buf,
                                               NULL);
        set_preprocessor_echo_delay(in->preprocessors[i], delay_us);
    }

    in->ref_frames_in -= buf.frameCount;
    if (in->ref_frames_in) {
        memcpy(in->ref_buf,
               in->ref_buf + buf.frameCount * in->config.channels,
               in->ref_frames_in * in->config.channels * sizeof(int16_t));
    }
}

static int get_next_buffer(struct resampler_buffer_provider *buffer_provider,
                                   struct resampler_buffer* buffer)
{
    struct aml_stream_in *in;

    if (buffer_provider == NULL || buffer == NULL)
        return -EINVAL;

    in = (struct aml_stream_in *)((char *)buffer_provider -
                                   offsetof(struct aml_stream_in, buf_provider));

    if (in->pcm == NULL) {
        buffer->raw = NULL;
        buffer->frame_count = 0;
        in->read_status = -ENODEV;
        return -ENODEV;
    }

    if (in->frames_in == 0) {
        in->read_status = pcm_read(in->pcm,
                                   (void*)in->buffer,
                                   in->config.period_size *
                                       audio_stream_frame_size(&in->stream.common));
        if (in->read_status != 0) {
            ALOGE("get_next_buffer() pcm_read error %d", in->read_status);
            buffer->raw = NULL;
            buffer->frame_count = 0;
            return in->read_status;
        }
        in->frames_in = in->config.period_size;
    }

    buffer->frame_count = (buffer->frame_count > in->frames_in) ?
                                in->frames_in : buffer->frame_count;
    buffer->i16 = in->buffer + (in->config.period_size - in->frames_in) *
                                                in->config.channels;

    return in->read_status;

}

static void release_buffer(struct resampler_buffer_provider *buffer_provider,
                                  struct resampler_buffer* buffer)
{
    struct aml_stream_in *in;

    if (buffer_provider == NULL || buffer == NULL)
        return;

    in = (struct aml_stream_in *)((char *)buffer_provider -
                                   offsetof(struct aml_stream_in, buf_provider));

    in->frames_in -= buffer->frame_count;
}

/* read_frames() reads frames from kernel driver, down samples to capture rate
 * if necessary and output the number of frames requested to the buffer specified */
static ssize_t read_frames(struct aml_stream_in *in, void *buffer, ssize_t frames)
{
    ssize_t frames_wr = 0;

    while (frames_wr < frames) {
        size_t frames_rd = frames - frames_wr;
        if (in->resampler != NULL) {
            in->resampler->resample_from_provider(in->resampler,
                    (int16_t *)((char *)buffer +
                            frames_wr * audio_stream_frame_size(&in->stream.common)),
                    &frames_rd);
        } else {
            struct resampler_buffer buf = {
                    { raw : NULL, },
                    frame_count : frames_rd,
            };
            get_next_buffer(&in->buf_provider, &buf);
            if (buf.raw != NULL) {
                memcpy((char *)buffer +
                           frames_wr * audio_stream_frame_size(&in->stream.common),
                        buf.raw,
                        buf.frame_count * audio_stream_frame_size(&in->stream.common));
                frames_rd = buf.frame_count;
            }
            release_buffer(&in->buf_provider, &buf);
        }
        /* in->read_status is updated by getNextBuffer() also called by
         * in->resampler->resample_from_provider() */
        if (in->read_status != 0)
            return in->read_status;

        frames_wr += frames_rd;
    }
    return frames_wr;
}

/* process_frames() reads frames from kernel driver (via read_frames()),
 * calls the active audio pre processings and output the number of frames requested
 * to the buffer specified */
static ssize_t process_frames(struct aml_stream_in *in, void* buffer, ssize_t frames)
{
    ssize_t frames_wr = 0;
    audio_buffer_t in_buf;
    audio_buffer_t out_buf;
    int i;

    //LOGFUNC("%s(%d, %p, %ld)", __FUNCTION__, in->num_preprocessors, buffer, frames);
    while (frames_wr < frames) {
        /* first reload enough frames at the end of process input buffer */
        if (in->proc_frames_in < (size_t)frames) {
            ssize_t frames_rd;

            if (in->proc_buf_size < (size_t)frames) {
                in->proc_buf_size = (size_t)frames;
                in->proc_buf = (int16_t *)realloc(in->proc_buf,
                                         in->proc_buf_size *
                                             in->config.channels * sizeof(int16_t));
                ALOGV("process_frames(): in->proc_buf %p size extended to %d frames",
                     in->proc_buf, in->proc_buf_size);
            }
            frames_rd = read_frames(in,
                                    in->proc_buf +
                                        in->proc_frames_in * in->config.channels,
                                    frames - in->proc_frames_in);
            if (frames_rd < 0) {
                frames_wr = frames_rd;
                break;
            }
            in->proc_frames_in += frames_rd;
        }

        if (in->echo_reference != NULL)
            push_echo_reference(in, in->proc_frames_in);

         /* in_buf.frameCount and out_buf.frameCount indicate respectively
          * the maximum number of frames to be consumed and produced by process() */
        in_buf.frameCount = in->proc_frames_in;
        in_buf.s16 = in->proc_buf;
        out_buf.frameCount = frames - frames_wr;
        out_buf.s16 = (int16_t *)buffer + frames_wr * in->config.channels;

        for (i = 0; i < in->num_preprocessors; i++)
            (*in->preprocessors[i])->process(in->preprocessors[i],
                                               &in_buf,
                                               &out_buf);

        /* process() has updated the number of frames consumed and produced in
         * in_buf.frameCount and out_buf.frameCount respectively
         * move remaining frames to the beginning of in->proc_buf */
        in->proc_frames_in -= in_buf.frameCount;
        if (in->proc_frames_in) {
            memcpy(in->proc_buf,
                   in->proc_buf + in_buf.frameCount * in->config.channels,
                   in->proc_frames_in * in->config.channels * sizeof(int16_t));
        }

        /* if not enough frames were passed to process(), read more and retry. */
        if (out_buf.frameCount == 0)
            continue;

        frames_wr += out_buf.frameCount;
    }
    return frames_wr;
}

static ssize_t in_read(struct audio_stream_in *stream, void* buffer,
                       size_t bytes)
{
        int ret = 0;
        struct aml_stream_in *in = (struct aml_stream_in *)stream;
        struct aml_audio_device *adev = in->dev;
        size_t frames_rq = bytes / audio_stream_frame_size(&stream->common);

        /* acquiring hw device mutex systematically is useful if a low priority thread is waiting
         * on the input stream mutex - e.g. executing select_mode() while holding the hw device
         * mutex
         */
        pthread_mutex_lock(&adev->lock);
        pthread_mutex_lock(&in->lock);
        if (in->standby) {
            ret = start_input_stream(in);
            if (ret == 0){
                in->standby = 0;
                input_standby = false;
            }
        }
        pthread_mutex_unlock(&adev->lock);

        if (ret < 0)
            goto exit;
        
        if (in->num_preprocessors != 0)
            ret = process_frames(in, buffer, frames_rq);
        else if (in->resampler != NULL)
            ret = read_frames(in, buffer, frames_rq);
        else
            ret = pcm_read(in->pcm, buffer, bytes);
    
        if (ret > 0)
            ret = 0;
    
        if (ret == 0 && adev->mic_mute){
            memset(buffer, 0, bytes);
        }
        
#if 0
    FILE *dump_fp = NULL;

    dump_fp = fopen("/data/audio_in.pcm", "a+");
    if (dump_fp != NULL) {
        fwrite(buffer, bytes, 1, dump_fp);
        fclose(dump_fp);
    }
    else {
        ALOGW("[Error] Can't write to /data/dump_in.pcm");
    }
#endif

    exit:
        if (ret < 0)
            usleep(bytes * 1000000 / audio_stream_frame_size(&stream->common) /
                   in_get_sample_rate(&stream->common));
    
        pthread_mutex_unlock(&in->lock);
        return bytes;

}

static uint32_t in_get_input_frames_lost(struct audio_stream_in *stream)
{
    return 0;
}

static int in_add_audio_effect(const struct audio_stream *stream,
                               effect_handle_t effect)
{
    struct aml_stream_in *in = (struct aml_stream_in *)stream;
    int status;
    effect_descriptor_t desc;

    pthread_mutex_lock(&in->dev->lock);
    pthread_mutex_lock(&in->lock);
    if (in->num_preprocessors >= MAX_PREPROCESSORS) {
        status = -ENOSYS;
        goto exit;
    }

    status = (*effect)->get_descriptor(effect, &desc);
    if (status != 0)
        goto exit;

    in->preprocessors[in->num_preprocessors++] = effect;

    if (memcmp(&desc.type, FX_IID_AEC, sizeof(effect_uuid_t)) == 0) {
        in->need_echo_reference = true;
        do_input_standby(in);
    }

exit:

    pthread_mutex_unlock(&in->lock);
    pthread_mutex_unlock(&in->dev->lock);
    return status;
}

static int in_remove_audio_effect(const struct audio_stream *stream,
                                  effect_handle_t effect)
{
    struct aml_stream_in *in = (struct aml_stream_in *)stream;
    int i;
    int status = -EINVAL;
    bool found = false;
    effect_descriptor_t desc;

    pthread_mutex_lock(&in->dev->lock);
    pthread_mutex_lock(&in->lock);
    if (in->num_preprocessors <= 0) {
        status = -ENOSYS;
        goto exit;
    }

    for (i = 0; i < in->num_preprocessors; i++) {
        if (found) {
            in->preprocessors[i - 1] = in->preprocessors[i];
            continue;
        }
        if (in->preprocessors[i] == effect) {
            in->preprocessors[i] = NULL;
            status = 0;
            found = true;
        }
    }

    if (status != 0)
        goto exit;

    in->num_preprocessors--;

    status = (*effect)->get_descriptor(effect, &desc);
    if (status != 0)
        goto exit;
    if (memcmp(&desc.type, FX_IID_AEC, sizeof(effect_uuid_t)) == 0) {
        in->need_echo_reference = false;
        do_input_standby(in);
    }

exit:

    pthread_mutex_unlock(&in->lock);
    pthread_mutex_unlock(&in->dev->lock);
    return status;
}

static int adev_open_output_stream(struct audio_hw_device *dev,
                                   audio_io_handle_t handle,
                                   audio_devices_t devices,
                                   audio_output_flags_t flags,
                                   struct audio_config *config,
                                   struct audio_stream_out **stream_out)
{
    struct aml_audio_device *ladev = (struct aml_audio_device *)dev;
    struct aml_stream_out *out;
    int channel_count = popcount(config->channel_mask);
    int ret;

    LOGFUNC("**enter %s(devices=0x%04x,format=%d, ch=0x%04x, SR=%d)", __FUNCTION__, devices,
                        config->format, config->channel_mask, config->sample_rate);

    out = (struct aml_stream_out *)calloc(1, sizeof(struct aml_stream_out));
    if (!out)
        return -ENOMEM;

    out->stream.common.get_sample_rate = out_get_sample_rate;
    out->stream.common.set_sample_rate = out_set_sample_rate;
    out->stream.common.get_buffer_size = out_get_buffer_size;
    out->stream.common.get_channels = out_get_channels;
    out->stream.common.get_format = out_get_format;
    out->stream.common.set_format = out_set_format;
    out->stream.common.standby = out_standby;
    out->stream.common.dump = out_dump;
    out->stream.common.set_parameters = out_set_parameters;
    out->stream.common.get_parameters = out_get_parameters;
    out->stream.common.add_audio_effect = out_add_audio_effect;
    out->stream.common.remove_audio_effect = out_remove_audio_effect;
    out->stream.get_latency = out_get_latency;
    out->stream.set_volume = out_set_volume;
    out->stream.write = out_write;
    out->stream.get_render_position = out_get_render_position;
    out->stream.get_next_write_timestamp = out_get_next_write_timestamp;
    out->config = pcm_config_out;

    out->dev = ladev;
    out->standby = true;
    output_standby = true;

   /* FIXME: when we support multiple output devices, we will want to
      * do the following:
      * adev->devices &= ~AUDIO_DEVICE_OUT_ALL;
      * adev->devices |= out->device;
      * select_output_device(adev);
      * This is because out_set_parameters() with a route is not
      * guaranteed to be called after an output stream is opened.
      */

    config->format = out_get_format(&out->stream.common);
    config->channel_mask = out_get_channels(&out->stream.common);
    config->sample_rate = out_get_sample_rate(&out->stream.common);
    LOGFUNC("**leave %s(devices=0x%04x,format=%d, ch=0x%04x, SR=%d)", __FUNCTION__, devices,
                        config->format, config->channel_mask, config->sample_rate);

    *stream_out = &out->stream;
    return 0;

err_open:
    free(out);
    *stream_out = NULL;
    return ret;
}

static void adev_close_output_stream(struct audio_hw_device *dev,
                                     struct audio_stream_out *stream)
{
    struct aml_stream_out *out = (struct aml_stream_out *)stream;

    LOGFUNC("%s(%p, %p)", __FUNCTION__, dev, stream);
    out_standby(&stream->common);
    if (out->buffer)
        free(out->buffer);
    if (out->resampler)
        release_resampler(out->resampler);

    free(stream);
}

static int adev_set_parameters(struct audio_hw_device *dev, const char *kvpairs)
{
    LOGFUNC("%s(%p, %s)", __FUNCTION__, dev, kvpairs);
    
    struct aml_audio_device *adev = (struct aml_audio_device *)dev;
    struct str_parms *parms;
    char *str;
    char value[32];
    int ret;

    parms = str_parms_create_str(kvpairs);
    ret = str_parms_get_str(parms, "screen_state", value, sizeof(value));
    if (ret >= 0) {
        if (strcmp(value, AUDIO_PARAMETER_VALUE_ON) == 0)
            adev->low_power = false;
        else
            adev->low_power = true;
    }

    str_parms_destroy(parms);
    return ret;
}

static char * adev_get_parameters(const struct audio_hw_device *dev,
                                  const char *keys)
{
    return strdup("");
}

static int adev_init_check(const struct audio_hw_device *dev)
{
    return 0;
}

static int adev_set_voice_volume(struct audio_hw_device *dev, float volume)
{
    return 0;
}

static int adev_set_master_volume(struct audio_hw_device *dev, float volume)
{
    return -ENOSYS;
}

static int adev_get_master_volume(struct audio_hw_device *dev,
                                  float *volume)
{
    return -ENOSYS;
}

static int adev_set_master_mute(struct audio_hw_device *dev, bool muted)
{
    return -ENOSYS;
}

static int adev_get_master_mute(struct audio_hw_device *dev, bool *muted)
{
    return -ENOSYS;
}
static int adev_set_mode(struct audio_hw_device *dev, audio_mode_t mode)
{
    struct aml_audio_device *adev = (struct aml_audio_device *)dev;
    LOGFUNC("%s(%p, %d)", __FUNCTION__, dev, mode);

    pthread_mutex_lock(&adev->lock);
    if (adev->mode != mode) {
        adev->mode = mode;
        select_mode(adev);
    }
    pthread_mutex_unlock(&adev->lock);

    return 0;
}

static int adev_set_mic_mute(struct audio_hw_device *dev, bool state)
{
    struct aml_audio_device *adev = (struct aml_audio_device *)dev;

    adev->mic_mute = state;

    return 0;
}

static int adev_get_mic_mute(const struct audio_hw_device *dev, bool *state)
{
    struct aml_audio_device *adev = (struct aml_audio_device *)dev;

    *state = adev->mic_mute;

    return 0;

}

static size_t adev_get_input_buffer_size(const struct audio_hw_device *dev,
                                         const struct audio_config *config)
{
    size_t size;
    int channel_count = popcount(config->channel_mask);

    LOGFUNC("%s(%p, %d, %d, %d)", __FUNCTION__, dev, config->sample_rate,
                                config->format, channel_count);
    if (check_input_parameters(config->sample_rate, config->format, channel_count) != 0)
        return 0;

    return get_input_buffer_size(config->sample_rate,
                                config->format, channel_count);

}

static int adev_open_input_stream(struct audio_hw_device *dev,
                                  audio_io_handle_t handle,
                                  audio_devices_t devices,
                                  struct audio_config *config,
                                  struct audio_stream_in **stream_in)
{
    struct aml_audio_device *ladev = (struct aml_audio_device *)dev;
    struct aml_stream_in *in;
    int ret;
    int channel_count = popcount(config->channel_mask);
    LOGFUNC("**********%s(%#x, %d, 0x%04x, %d)", __FUNCTION__,
        devices, config->format, config->channel_mask, config->sample_rate);
    if (check_input_parameters(config->sample_rate, config->format, channel_count) != 0)
        return -EINVAL;

    in = (struct aml_stream_in *)calloc(1, sizeof(struct aml_stream_in));
    if (!in)
        return -ENOMEM;

    in->stream.common.get_sample_rate = in_get_sample_rate;
    in->stream.common.set_sample_rate = in_set_sample_rate;
    in->stream.common.get_buffer_size = in_get_buffer_size;
    in->stream.common.get_channels = in_get_channels;
    in->stream.common.get_format = in_get_format;
    in->stream.common.set_format = in_set_format;
    in->stream.common.standby = in_standby;
    in->stream.common.dump = in_dump;
    in->stream.common.set_parameters = in_set_parameters;
    in->stream.common.get_parameters = in_get_parameters;
    in->stream.common.add_audio_effect = in_add_audio_effect;
    in->stream.common.remove_audio_effect = in_remove_audio_effect;
    in->stream.set_gain = in_set_gain;
    in->stream.read = in_read;
    in->stream.get_input_frames_lost = in_get_input_frames_lost;

    in->requested_rate = config->sample_rate;

    in->device = devices & ~AUDIO_DEVICE_BIT_IN;
    if (in->device & AUDIO_DEVICE_IN_ALL_SCO){
        memcpy(&in->config, &pcm_config_bt, sizeof(pcm_config_bt));
    } else {
        memcpy(&in->config, &pcm_config_in, sizeof(pcm_config_in));
    }

    if(in->config.channels == 1)
    {
        config->channel_mask = AUDIO_CHANNEL_IN_MONO;
    }
    else if(in->config.channels == 2)
    {
        config->channel_mask = AUDIO_CHANNEL_IN_STEREO;
    }
    else
    {
        ALOGE("Bad value of channel count : %d", in->config.channels);
    }
    in->buffer = malloc(in->config.period_size *
                        audio_stream_frame_size(&in->stream.common));
    if (!in->buffer) {
        ret = -ENOMEM;
        goto err_open;
    }

    if (in->requested_rate != in->config.rate) {
        LOGFUNC("%s(in->requested_rate=%d, in->config.rate=%d)", 
                       __FUNCTION__, in->requested_rate, in->config.rate);
        in->buf_provider.get_next_buffer = get_next_buffer;
        in->buf_provider.release_buffer = release_buffer;
        ret = create_resampler(in->config.rate,
                               in->requested_rate,
                               in->config.channels,
                               RESAMPLER_QUALITY_DEFAULT,
                               &in->buf_provider,
                               &in->resampler);

        if (ret != 0) {
            ret = -EINVAL;
            goto err_open;
        }
    }

    in->dev = ladev;
    in->standby = 1;
    input_standby = true;

    *stream_in = &in->stream;
    return 0;

err_open:
    if (in->resampler)
        release_resampler(in->resampler);

    free(in);
    *stream_in = NULL;
    return ret;
}

static void adev_close_input_stream(struct audio_hw_device *dev,
                                   struct audio_stream_in *stream)
{
    struct aml_stream_in *in = (struct aml_stream_in *)stream;

    LOGFUNC("%s(%p, %p)", __FUNCTION__, dev, stream);
    in_standby(&stream->common);

    if (in->resampler) {
        free(in->buffer);
        release_resampler(in->resampler);
    }
    if (in->proc_buf)
        free(in->proc_buf);
    if (in->ref_buf)
        free(in->ref_buf);

    free(stream);

    return;
}

static int adev_dump(const audio_hw_device_t *device, int fd)
{
    return 0;
}

static int adev_close(hw_device_t *device)
{
    struct aml_audio_device *adev = (struct aml_audio_device *)device;

    audio_route_free(adev->ar);
    free(device);
    return 0;
}

static int adev_open(const hw_module_t* module, const char* name,
                     hw_device_t** device)
{
    struct aml_audio_device *adev;
    int ret;
    
    if (strcmp(name, AUDIO_HARDWARE_INTERFACE) != 0)
        return -EINVAL;

    adev = calloc(1, sizeof(struct aml_audio_device));
    if (!adev)
        return -ENOMEM;

    adev->hw_device.common.tag = HARDWARE_DEVICE_TAG;
    adev->hw_device.common.version = AUDIO_DEVICE_API_VERSION_2_0;
    adev->hw_device.common.module = (struct hw_module_t *) module;
    adev->hw_device.common.close = adev_close;

    adev->hw_device.init_check = adev_init_check;
    adev->hw_device.set_voice_volume = adev_set_voice_volume;
    adev->hw_device.set_master_volume = adev_set_master_volume;
    adev->hw_device.get_master_volume = adev_get_master_volume;
    adev->hw_device.set_master_mute = adev_set_master_mute;
    adev->hw_device.get_master_mute = adev_get_master_mute;
    adev->hw_device.set_mode = adev_set_mode;
    adev->hw_device.set_mic_mute = adev_set_mic_mute;
    adev->hw_device.get_mic_mute = adev_get_mic_mute;
    adev->hw_device.set_parameters = adev_set_parameters;
    adev->hw_device.get_parameters = adev_get_parameters;
    adev->hw_device.get_input_buffer_size = adev_get_input_buffer_size;
    adev->hw_device.open_output_stream = adev_open_output_stream;
    adev->hw_device.close_output_stream = adev_close_output_stream;
    adev->hw_device.open_input_stream = adev_open_input_stream;
    adev->hw_device.close_input_stream = adev_close_input_stream;
    adev->hw_device.dump = adev_dump;

    adev->ar = audio_route_init();

    /* Set the default route before the PCM stream is opened */
    adev->mode = AUDIO_MODE_NORMAL;
    adev->out_device = AUDIO_DEVICE_OUT_SPEAKER;
    adev->in_device = AUDIO_DEVICE_IN_BUILTIN_MIC & ~AUDIO_DEVICE_BIT_IN;

    select_devices(adev);

    *device = &adev->hw_device.common;
    return 0;
}

static struct hw_module_methods_t hal_module_methods = {
    .open = adev_open,
};

struct audio_module HAL_MODULE_INFO_SYM = {
    .common = {
        .tag = HARDWARE_MODULE_TAG,
        .module_api_version = AUDIO_MODULE_API_VERSION_0_1,
        .hal_api_version = HARDWARE_HAL_API_VERSION,
        .id = AUDIO_HARDWARE_MODULE_ID,
        .name = "aml audio HW HAL",
        .author = "amlogic, Corp.",
        .methods = &hal_module_methods,
    },
};
