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
#define LOG_NDEBUG 0
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
#include <time.h>
#include <utils/Timers.h>
#include <cutils/log.h>
#include <cutils/str_parms.h>
#include <cutils/properties.h>
#include <linux/ioctl.h>
#include <hardware/hardware.h>
#include <system/audio.h>
#include <hardware/audio.h>
#include <sound/asound.h>
#include <tinyalsa/asoundlib.h>
#include <audio_utils/resampler.h>
#include <audio_utils/echo_reference.h>
#include <hardware/audio_effect.h>
#include <audio_effects/effect_aec.h>
#include <audio_route/audio_route.h>

#include "libTVaudio/audio/audio_effect_control.h"



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

#define NSEC_PER_SECOND 1000000000ULL

static unsigned int  DEFAULT_OUT_SAMPLING_RATE  = 48000;

/* sampling rate when using MM low power port */
#define MM_LOW_POWER_SAMPLING_RATE 44100
/* sampling rate when using MM full power port */
#define MM_FULL_POWER_SAMPLING_RATE 48000
/* sampling rate when using VX port for narrow band */
#define VX_NB_SAMPLING_RATE 8000
#define MIXER_XML_PATH "/system/etc/mixer_paths.xml"

#define TSYNC_FIRSTAPTS "/sys/class/tsync/firstapts"
#define TSYNC_PCRSCR    "/sys/class/tsync/pts_pcrscr"
#define TSYNC_EVENT     "/sys/class/tsync/event"
#define TSYNC_APTS      "/sys/class/tsync/pts_audio"
#define SYSTIME_CORRECTION_THRESHOLD        (90000*10/100)
#define APTS_DISCONTINUE_THRESHOLD_MIN    (90000/1000*100)
#define APTS_DISCONTINUE_THRESHOLD_MAX    (5*90000)

#define MAX_STREAM_NUM   5 

static const struct pcm_config pcm_config_out = {
    .channels = 2,
    .rate = MM_FULL_POWER_SAMPLING_RATE,
    .period_size = DEFAULT_PERIOD_SIZE,
    .period_count = PLAYBACK_PERIOD_COUNT,
    .format = PCM_FORMAT_S16_LE,
};

static const struct pcm_config pcm_config_in = {
    .channels = 2,
    .rate = MM_FULL_POWER_SAMPLING_RATE,
    .period_size = DEFAULT_CAPTURE_PERIOD_SIZE,
    .period_count = CAPTURE_PERIOD_COUNT,
    .format = PCM_FORMAT_S16_LE,
};

static const struct pcm_config pcm_config_bt = {
    .channels = 1,
    .rate = VX_NB_SAMPLING_RATE,
    .period_size = DEFAULT_PERIOD_SIZE,
    .period_count = PLAYBACK_PERIOD_COUNT,
    .format = PCM_FORMAT_S16_LE,
};
#define AML_HAL_MIXER_BUF_SIZE  2*512*1024
struct aml_hal_mixer {
    unsigned char start_buf[AML_HAL_MIXER_BUF_SIZE];
    unsigned int wp;
    unsigned int rp;
    unsigned int buf_size;
};
struct aml_audio_device {
    struct audio_hw_device hw_device;

    pthread_mutex_t lock;       /* see note below on mutex acquisition order */
    int mode;
    audio_devices_t in_device;
    audio_devices_t out_device;
    int in_call;
    struct aml_stream_in *active_input;
    struct aml_stream_out *active_output[MAX_STREAM_NUM];
    unsigned char active_output_count;
    bool mic_mute;
    unsigned int card;
    struct audio_route *ar;
    struct echo_reference_itfe *echo_reference;
    bool low_power;
    struct aml_stream_out *hwsync_output;
    struct aml_hal_mixer hal_mixer;
    struct pcm *pcm;
};

#define HW_SYNC_STATE_HEADER 0
#define HW_SYNC_STATE_BODY   1
#define HW_SYNC_STATE_RESYNC 2
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
    uint64_t frame_write_sum;
    uint64_t last_frames_postion;	
    uint8_t hw_sync_header[16];
    int hw_sync_header_cnt;
    int hw_sync_state;
    int hw_sync_body_cnt;
    uint8_t body_align[64];
    uint8_t body_align_cnt;
    int32_t *tmp_buffer_8ch;
    int is_tv_platform;
    void *audioeffect_tmp_buffer;
    int has_SRS_lib;
    int has_EQ_lib;
    unsigned char pause_status;
    bool hw_sync_mode;
    bool first_apts_flag;//flag to indicate set first apts
    uint64_t first_apts;
    uint64_t last_apts_from_header;
    int has_aml_IIR_lib;
    float volume_l;
    float volume_r;
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
static void select_output_device(struct aml_audio_device *adev);
static void select_input_device(struct aml_audio_device *adev);
static void select_devices(struct aml_audio_device *adev);
static int adev_set_voice_volume(struct audio_hw_device *dev, float volume);
static int do_input_standby(struct aml_stream_in *in);
static int do_output_standby(struct aml_stream_out *out);
static uint32_t out_get_sample_rate(const struct audio_stream *stream);
static int out_pause(struct audio_stream_out *stream);
static inline short CLIP(int r)
{
    return (r >  0x7fff) ? 0x7fff :
           (r < -0x8000) ? 0x8000 :
           r;
}
//code here for audio hal mixer when hwsync with af mixer output stream output
//at the same,need do a software mixer in audio hal.
static int aml_hal_mixer_init(struct aml_hal_mixer *mixer)
{
    mixer->wp = 0;
    mixer->rp = 0;
    mixer->buf_size = AML_HAL_MIXER_BUF_SIZE;
    return 0;
}
//need called by device mutux locked
static int aml_hal_mixer_get_space(struct aml_hal_mixer *mixer)
{
    unsigned space;
    if (mixer->wp >= mixer->rp)
        space = mixer->buf_size - (mixer->wp - mixer->rp);
    else
        space = mixer->rp - mixer->wp;
    return space > 64 ? (space-64):0;
}
static int aml_hal_mixer_get_content(struct aml_hal_mixer *mixer)
{
    unsigned content = 0;
    if (mixer->wp >= mixer->rp)
        content = mixer->wp - mixer->rp;
    else
        content = mixer->wp - mixer->rp + mixer->buf_size;
    //ALOGI("wp %d,rp %d\n",mixer->wp,mixer->rp);
    return content;
}
//we assue the cached size is always smaller then buffer size
//need called by device mutux locked
static int aml_hal_mixer_write(struct aml_hal_mixer *mixer,void *w_buf,int size)
{
    unsigned space;
    unsigned write_size = size;
    unsigned tail = 0;
    space = aml_hal_mixer_get_space(mixer);
    if (space < size) {
        ALOGI("write data no space,space %d,size %d,rp %d,wp %d,reset all ptr\n",space,size,mixer->rp,mixer->wp);
        mixer->wp = 0;
        mixer->rp = 0;
    }
    //TODO
    if (write_size > space)
        write_size = space;
    if (write_size + mixer->wp > mixer->buf_size) {
        tail = mixer->buf_size - mixer->wp;
        memcpy(mixer->start_buf+mixer->wp,w_buf,tail);
        write_size -= tail;
        memcpy(mixer->start_buf,(unsigned char*)w_buf+tail,write_size);
        mixer->wp = write_size;
    }
    else {
        memcpy(mixer->start_buf+mixer->wp,w_buf,write_size);
        mixer->wp += write_size;
    }
    return size;
}
//need called by device mutux locked
static int aml_hal_mixer_read(struct aml_hal_mixer *mixer,void *r_buf,int size)
{
    unsigned cached_size;
    unsigned read_size = size;
    unsigned tail = 0;
    cached_size = aml_hal_mixer_get_content(mixer);
    // we always assue we have enough data to read when hwsync enabled.
    // if we do not have,insert zero data.
    if (cached_size < size) {
        ALOGI("read data has not enough data to mixer,read %d, have %d,rp %d,wp %d\n",size,cached_size,mixer->rp,mixer->wp);
        memset((unsigned char*)r_buf+cached_size,0,size-cached_size);
        read_size = cached_size;
    }
    if (read_size + mixer->rp > mixer->buf_size) {
        tail = mixer->buf_size - mixer->rp;
        memcpy(r_buf,mixer->start_buf+mixer->rp,tail);
        read_size -= tail;
        memcpy(mixer->start_buf,(unsigned char*)r_buf+tail,read_size);
        mixer->rp = read_size;
    }
    else {
        memcpy(r_buf,mixer->start_buf+mixer->rp,read_size);
        mixer->rp += read_size;
    }
    return size;
}
// aml audio hal mixer code end

static int getprop_bool(const char * path)
{
    char buf[PROPERTY_VALUE_MAX];
    int ret = -1;

    ret = property_get(path, buf, NULL);
    if (ret > 0) {
        if (strcasecmp(buf, "true") == 0 || strcmp(buf, "1") == 0) {
            return 1;
        }
    }
    return 0;
}
static int get_sysfs_int(const char * path)
{
    int val = 0;
    int fd = open(path, O_RDONLY);
    if (fd >= 0) {
        char  bcmd[16];
        read(fd, bcmd, sizeof(bcmd));
        val = strtol(bcmd, NULL, 10);
        close(fd);
    } else {
        LOGFUNC("[%s]open %s node failed! return 0\n", path, __FUNCTION__);
    }
    return val;
}
static int get_sysfs_int16(const char *path, unsigned *value)
{
    int fd;
    char valstr[64];
    unsigned  val = 0;
    fd = open(path, O_RDONLY);
    if (fd >= 0) {
        memset(valstr, 0, 64);
        read(fd, valstr, 64 - 1);
        valstr[strlen(valstr)] = '\0';
        close(fd);
    } else {
        ALOGE("unable to open file %s\n", path);
        return -1;
    }
    if (sscanf(valstr, "0x%lx", &val) < 1) {
        ALOGE("unable to get pts from: %s", valstr);
        return -1;
    }
    *value = val;
    return 0;
}
static int sysfs_set_sysfs_str(const char *path, const char *val)
{
    int fd;
    int bytes;
    fd = open(path, O_CREAT | O_RDWR | O_TRUNC, 0644);
    if (fd >= 0) {
        bytes = write(fd, val, strlen(val));
        close(fd);
        return 0;
    } else {
        ALOGE("unable to open file %s,err: %s", path, strerror(errno));
    }
    return -1;
}

static void
set_codec_type(int type)
{
    char buf[16];
    int fd = open("/sys/class/audiodsp/digital_codec", O_WRONLY);

    if (fd >= 0) {
        memset(buf, 0, sizeof(buf));
        snprintf(buf, sizeof(buf), "%d", type);

        write(fd, buf, sizeof(buf));
        close(fd);
    }
}
static inline bool hwsync_header_valid(uint8_t *header)
{
    return (header[0] == 0x55) &&
           (header[1] == 0x55) &&
           (header[2] == 0x00) &&
           (header[3] == 0x01);
}

static inline uint64_t hwsync_header_get_pts(uint8_t *header)
{
    return (((uint64_t)header[8]) << 56) |
           (((uint64_t)header[9]) << 48) |
           (((uint64_t)header[10]) << 40) |
           (((uint64_t)header[11]) << 32) |
           (((uint64_t)header[12]) << 24) |
           (((uint64_t)header[13]) << 16) |
           (((uint64_t)header[14]) << 8) |
           ((uint64_t)header[15]);
}

static inline uint32_t hwsync_header_get_size(uint8_t *header)
{
    return (((uint32_t)header[4]) << 24) |
           (((uint32_t)header[5]) << 16) |
           (((uint32_t)header[6]) << 8) |
           ((uint32_t)header[7]);
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
    mic_in = adev->in_device & (AUDIO_DEVICE_IN_BUILTIN_MIC | AUDIO_DEVICE_IN_BACK_MIC);
    headset_mic = adev->in_device & AUDIO_DEVICE_IN_WIRED_HEADSET;

    LOGFUNC("%s : hs=%d , hp=%d, sp=%d, hdmi=0x%x,earpiece=0x%x", __func__,
            headset_on, headphone_on, speaker_on, hdmi_on, earpiece);
    LOGFUNC("%s : in_device(%#x), mic_in(%#x), headset_mic(%#x)", __func__,
            adev->in_device, mic_in, headset_mic);
    audio_route_reset(adev->ar);
    if (hdmi_on) {
        audio_route_apply_path(adev->ar, "hdmi");
    }
    if (headphone_on || headset_on) {
        audio_route_apply_path(adev->ar, "headphone");
    }
    if (speaker_on || earpiece) {
        audio_route_apply_path(adev->ar, "speaker");
    }
    if (mic_in) {
        audio_route_apply_path(adev->ar, "main_mic");
    }
    if (headset_mic) {
        audio_route_apply_path(adev->ar, "headset-mic");
    }

    audio_route_update_mixer(adev->ar);

}
#if 0
static void force_all_standby(struct aml_audio_device *adev)
{
    struct aml_stream_in *in;
    struct aml_stream_out *out;

    LOGFUNC("%s(%p)", __FUNCTION__, adev);
    int i = 0;
    for (i = 0; i < MAX_STREAM_NUM;i++) {
        out = 	adev->active_output[i];
	  	
    }
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
#endif
static void select_mode(struct aml_audio_device *adev)
{
    LOGFUNC("%s(out_device=%#x)", __FUNCTION__, adev->out_device);
    LOGFUNC("%s(in_device=%#x)", __FUNCTION__, adev->in_device);
    return;
    //force_all_standby(adev);
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
    if ((adev->out_device & AUDIO_DEVICE_OUT_ALL) == AUDIO_DEVICE_OUT_SPEAKER) {
        adev->in_device = AUDIO_DEVICE_IN_BUILTIN_MIC & ~AUDIO_DEVICE_BIT_IN;
    } else {
        adev->out_device &= ~AUDIO_DEVICE_OUT_SPEAKER;
    }

    return;
}

static int get_aml_card(void)
{
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

static int get_pcm_bt_port(void)
{
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

static int get_spdif_port()
{
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
    port = *(pd - 3) - '0';

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
    int i  = 0;
    struct aml_stream_out *out_removed = NULL;	
    LOGFUNC("%s(adev->out_device=%#x, adev->mode=%d)",
            __FUNCTION__, adev->out_device, adev->mode);
    if (adev->mode != AUDIO_MODE_IN_CALL) {
        /* FIXME: only works if only one output can be active at a time */
        select_devices(adev);
    }
    if (out->hw_sync_mode == true) {
        adev->hwsync_output = out;
#if 0
    for (i = 0;i < MAX_STREAM_NUM;i++) {
        if (adev->active_output[i]) {
            out_removed = adev->active_output[i];
            pthread_mutex_lock(&out_removed->lock);
            if (!out_removed->standby) {
                ALOGI("hwsync start,force %p standby\n",out_removed);
                do_output_standby(out_removed);
            }
            pthread_mutex_unlock(&out_removed->lock);
        }
    }
#endif
    }
    card = get_aml_card();
    if (adev->out_device & AUDIO_DEVICE_OUT_ALL_SCO) {
        port = get_pcm_bt_port();
        out->config = pcm_config_bt;
    } else {
        port = PORT_MM;
    }
    LOGFUNC("*%s, open card(%d) port(%d)", __FUNCTION__, card, port);

    /* default to low power: will be corrected in out_write if necessary before first write to
     * tinyalsa.
     */
    out->write_threshold = out->config.period_size * PLAYBACK_PERIOD_COUNT;
    out->config.start_threshold = out->config.period_size * PLAYBACK_PERIOD_COUNT;
    out->config.avail_min = 0;//SHORT_PERIOD_SIZE;
//added by xujian for NTS hwsync/system stream mix smooth playback.
//we need re-use the tinyalsa pcm handle by all the output stream, including
//hwsync direct output stream,system mixer output stream.
//TODO we need diff the code with AUDIO_DEVICE_OUT_ALL_SCO.
//as it share the same hal but with the different card id.
//TODO need reopen the tinyalsa card when sr/ch changed,
    if (adev->pcm == NULL) {
        out->pcm = pcm_open(card, port, PCM_OUT /*| PCM_MMAP | PCM_NOIRQ*/, &(out->config));
        if (!pcm_is_ready(out->pcm)) {
            ALOGE("cannot open pcm_out driver: %s", pcm_get_error(out->pcm));
            pcm_close(out->pcm);
            return -ENOMEM;
        }
        if (out->config.rate != out_get_sample_rate(&out->stream.common)) {
            LOGFUNC("%s(out->config.rate=%d, out->config.channels=%d)",
            __FUNCTION__, out->config.rate, out->config.channels);
            ret = create_resampler(out_get_sample_rate(&out->stream.common),
                               out->config.rate,
                               out->config.channels,
                               RESAMPLER_QUALITY_DEFAULT,
                               NULL,
                               &out->resampler);
            if (ret != 0) {
                ALOGE("cannot create resampler for output");
                return -ENOMEM;
            }
            out->buffer_frames = (out->config.period_size * out->config.rate) /
                             out_get_sample_rate(&out->stream.common) + 1;
            out->buffer = malloc(pcm_frames_to_bytes(out->pcm, out->buffer_frames));
            if (out->buffer == NULL) {
                ALOGE("cannot malloc memory for out->buffer");
                return -ENOMEM;
            }
        }
        adev->pcm = out->pcm;
        ALOGI("device pcm %p\n",adev->pcm);
    }
    else {
        ALOGI("stream %p share the pcm %p\n",out,adev->pcm);
        out->pcm = adev->pcm;
    }
    LOGFUNC("channels=%d---format=%d---period_count%d---period_size%d---rate=%d---",
            out->config.channels, out->config.format, out->config.period_count,
            out->config.period_size, out->config.rate);

    if (adev->echo_reference != NULL) {
        out->echo_reference = adev->echo_reference;
    }
    if (out->resampler) {
        out->resampler->reset(out->resampler);
    }
    if (out->is_tv_platform == 1) {
        sysfs_set_sysfs_str("/sys/class/amhdmitx/amhdmitx0/aud_output_chs", "2:2");
    }
    //set_codec_type(0);
    if (out->hw_sync_mode == 1) {
        LOGFUNC("start_output_stream with hw sync enable %p\n",out);
    }
    for (i = 0;i < MAX_STREAM_NUM;i++) {
        if (adev->active_output[i] == NULL) {
            ALOGI("store out (%p) to index %d\n",out,i);
            adev->active_output[i] = out;
            adev->active_output_count++;
            break;
        }		
    }
    if (i == MAX_STREAM_NUM)
        ALOGE("error,no space to store the dev stream \n");	
    return 0;
}

static int check_input_parameters(uint32_t sample_rate, audio_format_t format, int channel_count)
{
    LOGFUNC("%s(sample_rate=%d, format=%d, channel_count=%d)", __FUNCTION__, sample_rate, format, channel_count);

    if (format != AUDIO_FORMAT_PCM_16_BIT) {
        return -EINVAL;
    }

    if ((channel_count < 1) || (channel_count > 2)) {
        return -EINVAL;
    }

    switch (sample_rate) {
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

static size_t get_input_buffer_size(unsigned int period_size,uint32_t sample_rate, audio_format_t format, int channel_count)
{
    size_t size;

    LOGFUNC("%s(sample_rate=%d, format=%d, channel_count=%d)", __FUNCTION__, sample_rate, format, channel_count);

    if (check_input_parameters(sample_rate, format, channel_count) != 0) {
        return 0;
    }

    /* take resampling into account and return the closest majoring
    multiple of 16 frames, as audioflinger expects audio buffers to
    be a multiple of 16 frames */
    if (period_size == 0) {
        period_size = (pcm_config_in.period_size * sample_rate) / pcm_config_in.rate;
    }

    size = period_size;
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
        if (adev->active_output[0] != NULL) {
            remove_echo_reference(adev->active_output[0], reference);
        }	
        release_echo_reference(reference);
        adev->echo_reference = NULL;
    }
}

static struct echo_reference_itfe *get_echo_reference(struct aml_audio_device *adev,
        audio_format_t format __unused,
        uint32_t channel_count,
        uint32_t sampling_rate)
{
    put_echo_reference(adev, adev->echo_reference);
    if (adev->active_output[0] != NULL) {
        struct audio_stream *stream = &adev->active_output[0]->stream.common;
        uint32_t wr_channel_count = popcount(stream->get_channels(stream));
        uint32_t wr_sampling_rate = stream->get_sample_rate(stream);

        int status = create_echo_reference(AUDIO_FORMAT_PCM_16_BIT,
                                           channel_count,
                                           sampling_rate,
                                           AUDIO_FORMAT_PCM_16_BIT,
                                           wr_channel_count,
                                           wr_sampling_rate,
                                           &adev->echo_reference);
        if (status == 0) {
            add_echo_reference(adev->active_output[0], adev->echo_reference);
        }
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
    ALOGV("~~pcm_get_buffer_size(out->pcm)=%d", pcm_get_buffer_size(out->pcm));
    /* adjust render time stamp with delay added by current driver buffer.
     * Add the duration of current frame as we want the render time of the last
     * sample being written. */
    buffer->delay_ns = (long)(((int64_t)(kernel_frames + frames) * 1000000000) /
                              out->config.rate);

    ALOGV("get_playback_delay time_stamp = [%ld].[%ld], delay_ns: [%d],"
          "kernel_frames:[%d]", buffer->time_stamp.tv_sec , buffer->time_stamp.tv_nsec,
          buffer->delay_ns, kernel_frames);
    return 0;
}

static uint32_t out_get_sample_rate(const struct audio_stream *stream)
{
    struct aml_stream_out *out = (struct aml_stream_out *)stream;
    //LOGFUNC("%s(%p)", __FUNCTION__, stream);
    return out->config.rate;
}

static int out_set_sample_rate(struct audio_stream *stream __unused, uint32_t rate __unused)
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
    size_t size = out->config.period_size;
    size = ((size + 15) / 16) * 16;
    return size * audio_stream_out_frame_size((struct audio_stream_out *)stream);
}

static audio_channel_mask_t out_get_channels(const struct audio_stream *stream __unused)
{
    return AUDIO_CHANNEL_OUT_STEREO;
}

static audio_format_t out_get_format(const struct audio_stream *stream __unused)
{
    return AUDIO_FORMAT_PCM_16_BIT;
}

static int out_set_format(struct audio_stream *stream __unused, audio_format_t format __unused)
{
    return 0;
}

/* must be called with hw device and output stream mutexes locked */
static int do_output_standby(struct aml_stream_out *out)
{
    struct aml_audio_device *adev = out->dev;
    int i = 0;
    	
    LOGFUNC("%s(%p)", __FUNCTION__, out);
    
    if (!out->standby) {
        //commit here for hwsync/mix stream hal mixer
        //pcm_close(out->pcm);
        //out->pcm = NULL;
        if (out->buffer) {
            free(out->buffer);
            out->buffer = NULL;
        }
        if (out->resampler) {
            release_resampler(out->resampler);
            out->resampler = NULL;
        }
        /* stop writing to echo reference */
        if (out->echo_reference != NULL) {
            out->echo_reference->write(out->echo_reference, NULL);
            out->echo_reference = NULL;
        }
        out->standby = 1;
	 for (i  = 0; i < MAX_STREAM_NUM;i++) {
	 	if (adev->active_output[i] == out) {
			adev->active_output[i]  = NULL;
			adev->active_output_count--;
			ALOGI("remove out (%p) from index %d\n",out,i);	
			break;
	 	}
	 }
	 if (adev->hwsync_output == out) {
//here to check if hwsync in pause status,if that,chear the status
//to release the sound card to other output stream
             if (out->pause_status == true) {
                 if (pcm_is_ready(out->pcm)) {
                     int r = pcm_ioctl(out->pcm, SNDRV_PCM_IOCTL_PAUSE, 0);
                     if (r < 0) {
                         ALOGE("here cannot resume channel\n");
                     } else {
                         r = 0;
                     }
                     ALOGI("clear the hwsync output pause status.rusume pcm\n");
                 }
                 out->pause_status = false;
             }
             adev->hwsync_output = NULL;
	 }
         if (i == MAX_STREAM_NUM) {
             ALOGE("error, not found stream in dev stream list\n");
         }
	 /* no active output here,we can close the pcm to release the sound card now*/
	 if (adev->active_output_count == 0) {
	     if (adev->pcm) {
                 ALOGI("close pcm %p\n",adev->pcm);
                 pcm_close(adev->pcm);
                 adev->pcm = NULL;
             }
             out->pause_status = false;
         }
    }
    return 0;
}

static int out_standby(struct audio_stream *stream)
{
    LOGFUNC("%s(%p)", __FUNCTION__, stream);
    struct aml_stream_out *out = (struct aml_stream_out *)stream;
    int status = 0;
    pthread_mutex_lock(&out->dev->lock);
    pthread_mutex_lock(&out->lock);
    status = do_output_standby(out);
    pthread_mutex_unlock(&out->lock);
    pthread_mutex_unlock(&out->dev->lock);
    return status;
}

static int out_dump(const struct audio_stream *stream __unused, int fd __unused)
{
    LOGFUNC("%s(%p, %d)", __FUNCTION__, stream, fd);
    return 0;
}
static int
out_flush(const struct audio_stream *stream)
{
    LOGFUNC("%s(%p)", __FUNCTION__, stream);
    struct aml_stream_out *out = (struct aml_stream_out *) stream;
    struct aml_audio_device *adev = out->dev;
    pthread_mutex_lock(&adev->lock);
    pthread_mutex_lock(&out->lock);
    do_output_standby(out);
    out->frame_write_sum  = 0;
    out->last_frames_postion = 0;
    pthread_mutex_unlock(&adev->lock);
    pthread_mutex_unlock(&out->lock);
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
            if (1/* out == adev->active_output[0]*/) {
                ALOGI("audio hw select device!\n");
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
                     (adev->out_device & AUDIO_DEVICE_OUT_DGTL_DOCK_HEADSET))) {
                    do_output_standby(out);
                }
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
        if (sr > 0) {
            struct pcm_config *config = &out->config;
            ALOGI("audio hw sampling_rate change from %d to %d \n", config->rate, sr);
            config->rate = sr;
            pthread_mutex_lock(&adev->lock);
            pthread_mutex_lock(&out->lock);
            if (!out->standby) {
                do_output_standby(out);
                start_output_stream(out);
                out->standby = 0;
            }
            pthread_mutex_unlock(&adev->lock);
            pthread_mutex_unlock(&out->lock);
        }
        goto exit;
    }
    int frame_size = 0;
    ret = str_parms_get_int(parms, AUDIO_PARAMETER_STREAM_FRAME_COUNT, &frame_size);
    if (ret >= 0) {
        if (frame_size > 0) {
            struct pcm_config *config = &out->config;
            ALOGI("audio hw frame size change from %d to %d \n", config->period_size, frame_size);
	    config->period_size =  frame_size;
            pthread_mutex_lock(&adev->lock);
            pthread_mutex_lock(&out->lock);
            if (!out->standby) {
                do_output_standby(out);
                start_output_stream(out);
                out->standby = 0;
            }
            pthread_mutex_unlock(&adev->lock);
            pthread_mutex_unlock(&out->lock);
        }
        goto exit;
    }
    int EQ_parameters[5] = {0, 0, 0, 0, 0};
    char tmp[2];
    int data = 0, i = 0;
    ret = str_parms_get_str(parms, AUDIO_PARAMETER_STREAM_EQ, value, sizeof(value));
    //ALOGI("audio effect EQ parameters are %s\n", value);
    if (ret >= 0) {
        for (i; i < 5; i++) {
            tmp[0] = value[2 * i];
            tmp[1] = value[2 * i + 1];
            data = atoi(tmp);
            EQ_parameters[i] = data - 10;
        }
        ALOGI("audio effect EQ parameters are %d,%d,%d,%d,%d\n", EQ_parameters[0],
              EQ_parameters[1], EQ_parameters[2], EQ_parameters[3], EQ_parameters[4]);
        ret = 0;
        HPEQ_setParameter(EQ_parameters[0], EQ_parameters[1],
                          EQ_parameters[2], EQ_parameters[3], EQ_parameters[4]);
        goto exit;
    }
    int SRS_parameters[5] = {0, 0, 0, 0, 0};
    char tmp1[3];
    ret = str_parms_get_str(parms, AUDIO_PARAMETER_STREAM_SRS, value, sizeof(value));
    //ALOGI("audio effect SRS parameters are %s\n", value);
    if (ret >= 0) {
        for (i; i < 5; i++) {
            tmp1[0] = value[3 * i];
            tmp1[1] = value[3 * i + 1];
            tmp1[2] = value[3 * i + 2];
            SRS_parameters[i] = atoi(tmp1);
        }
        ALOGI("audio effect SRS parameters are %d,%d,%d,%d,%d\n", SRS_parameters[0],
              SRS_parameters[1], SRS_parameters[2], SRS_parameters[3], SRS_parameters[4]);
        ret = 0;
        srs_setParameter(SRS_parameters);
        goto exit;
    }
    int SRS_gain[2] = {0, 0};
    ret = str_parms_get_str(parms, AUDIO_PARAMETER_STREAM_SRS_GAIN, value, sizeof(value));
    if (ret >= 0) {
        for (i; i < 2; i++) {
            tmp1[0] = value[3 * i];
            tmp1[1] = value[3 * i + 1];
            tmp1[2] = value[3 * i + 2];
            SRS_gain[i] = atoi(tmp1);
        }
        ALOGI("audio effect SRS input/output gain are %d,%d\n", SRS_gain[0], SRS_gain[1]);
        ret = 0;
        srs_set_gain(SRS_gain[0], SRS_gain[1]);
        goto exit;
    }
    int SRS_switch[3] = {0, 0, 0};
    ret = str_parms_get_str(parms, AUDIO_PARAMETER_STREAM_SRS_SWITCH, value, sizeof(value));
    if (ret >= 0) {
        for (i; i < 3; i++) {
            tmp[0] = value[2 * i];
            tmp[1] = value[2 * i + 1];
            SRS_switch[i] = atoi(tmp);
        }
        ALOGI("audio effect SRS switch %d, %d, %d\n", SRS_switch[0], SRS_switch[1], SRS_switch[2]);
        ret = 0;
        srs_surround_enable(SRS_switch[0]);
        srs_dialogclarity_enable(SRS_switch[1]);
        srs_truebass_enable(SRS_switch[2]);
        goto exit;
    }
    ret = str_parms_get_str(parms, "hw_av_sync", value, sizeof(value));
    if (ret >= 0) {
        int hw_sync_id = atoi(value);
        unsigned char sync_enable = (hw_sync_id == 12345678) ? 1 : 0;
        ALOGI("(%p)set hw_sync_id %d,%s hw sync mode\n",
			out,hw_sync_id, sync_enable ? "enable" : "disable");
        out->hw_sync_mode = sync_enable;
	    out->first_apts_flag = false;
        pthread_mutex_lock(&adev->lock);
        pthread_mutex_lock(&out->lock);
        out->frame_write_sum = 0;
        out->last_frames_postion = 0;
/* clear up previous playback output status */
        if (!out->standby) {
            do_output_standby (out);
        }
        //adev->hwsync_output = sync_enable?out:NULL;
        if (sync_enable) {
            ALOGI("init hal mixer when hwsync\n");
            aml_hal_mixer_init(&adev->hal_mixer);
        }
        pthread_mutex_unlock(&out->lock);
        pthread_mutex_unlock(&adev->lock);
		ret = 0;
        goto exit;
    }
exit:
    str_parms_destroy(parms);
    return ret;
}

static char * out_get_parameters(const struct audio_stream *stream __unused, const char *keys __unused)
{
    return strdup("");
}

static uint32_t out_get_latency(const struct audio_stream_out *stream)
{
    const struct aml_stream_out *out = (const struct aml_stream_out *)stream;
    uint32_t whole_latency;
    uint32_t ret;
    snd_pcm_sframes_t frames = 0;
    whole_latency = (out->config.period_size * out->config.period_count * 1000) / out->config.rate;
    if (!out->pcm || !pcm_is_ready(out->pcm)) {
        return whole_latency;
    }
    ret = pcm_ioctl(out->pcm, SNDRV_PCM_IOCTL_DELAY, &frames);
    if (ret < 0) {
        return whole_latency;
    }
    return (frames * 1000) / out->config.rate/* (out->pcm->config.rate)*/;
}

static int out_set_volume(struct audio_stream_out *stream, float left, float right)
{
    struct aml_stream_out *out = (struct aml_stream_out *) stream;
    out->volume_l = left;
    out->volume_r = right;
    return 0;
}

static int out_pause(struct audio_stream_out *stream)
{
    LOGFUNC("out_pause(%p)\n",stream);

    struct aml_stream_out *out = (struct aml_stream_out *) stream;
    struct aml_audio_device *adev = out->dev;
    int r = 0;
    pthread_mutex_lock(&adev->lock);
    pthread_mutex_lock(&out->lock);
    if (out->standby || out->pause_status == true) {
        goto exit;
    }
    if (pcm_is_ready(out->pcm)) {
        r = pcm_ioctl(out->pcm, SNDRV_PCM_IOCTL_PAUSE, 1);
        if (r < 0) {
            ALOGE("cannot pause channel\n");
        } else {
            r = 0;
        }
    }

    if (out->hw_sync_mode) {
        sysfs_set_sysfs_str(TSYNC_EVENT, "AUDIO_PAUSE");
    }
    out->pause_status = true;
exit:
    pthread_mutex_unlock(&adev->lock);
    pthread_mutex_unlock(&out->lock);
    return r;
}

static int out_resume(struct audio_stream_out *stream)
{
    LOGFUNC("out_resume (%p)\n",stream);
    struct aml_stream_out *out = (struct aml_stream_out *) stream;
    struct aml_audio_device *adev = out->dev;
    int r = 0;
    pthread_mutex_lock(&adev->lock);
    pthread_mutex_lock(&out->lock);
    if (out->standby || out->pause_status == false) {
        goto exit;
    }
    if (pcm_is_ready(out->pcm)) {
        r = pcm_ioctl(out->pcm, SNDRV_PCM_IOCTL_PAUSE, 0);
        if (r < 0) {
            ALOGE("cannot resume channel\n");
        } else {
            r = 0;
        }
    }
    if (out->hw_sync_mode) {
        sysfs_set_sysfs_str(TSYNC_EVENT, "AUDIO_RESUME");
    }
    out->pause_status = false;
exit:
    pthread_mutex_unlock(&adev->lock);
    pthread_mutex_unlock(&out->lock);
    return r;
}


static int audio_effect_process(struct audio_stream_out *stream,
                                short* buffer, int frame_size)
{
    struct aml_stream_out *out = (struct aml_stream_out *)stream;
    int output_size = frame_size << 2;

    if (out->has_SRS_lib) {
        output_size = srs_process(buffer, buffer, frame_size);
    }
    if (out->has_EQ_lib) {
        HPEQ_process(buffer, buffer, frame_size);
    }
    if (out->has_aml_IIR_lib) {
        short *ptr = buffer;
        short data;
        int i;
        for (i = 0; i < frame_size; i++) {
            data = (short)aml_IIR_process((int)(*ptr), 0);
            *ptr++ = data;
            data = (short)aml_IIR_process((int)(*ptr), 1);
            *ptr++ = data;
        }
    }
    return output_size;
}

static ssize_t out_write(struct audio_stream_out *stream, const void* buffer,
                         size_t bytes)
{
    int ret = 0;
    size_t oldBytes = bytes;
    struct aml_stream_out *out = (struct aml_stream_out *)stream;
    struct aml_audio_device *adev = out->dev;
    size_t frame_size = audio_stream_out_frame_size(stream);
    size_t in_frames = bytes / frame_size;
    size_t out_frames;
    bool force_input_standby = false;
    int16_t *in_buffer = (int16_t *)buffer;
    struct aml_stream_in *in;
    char output_buffer_bytes[RESAMPLER_BUFFER_SIZE + 128];
    uint ouput_len;
    char *data,  *data_dst;
    volatile char *data_src;
    uint i, total_len;
    int codec_type = 0;
    int samesource_flag = 0;
    uint32_t latency_frames = 0;
    int need_mix = 0;
    short *mix_buf = NULL;
    unsigned char enable_dump = getprop_bool("media.audiohal.outdump");
    /* acquiring hw device mutex systematically is useful if a low priority thread is waiting
     * on the output stream mutex - e.g. executing select_mode() while holding the hw device
     * mutex
     */
    pthread_mutex_lock(&adev->lock);
    pthread_mutex_lock(&out->lock);
	//here to check whether hwsync out stream and other stream are enabled at the same time.
	//if that we need do the hal mixer of the two out stream.
	if (out->hw_sync_mode == 1) {
	    int content_size = aml_hal_mixer_get_content(&adev->hal_mixer);
		//ALOGI("content_size %d\n",content_size);
        if (content_size > 0) {
		    //ALOGI("need do hal mixer\n");
		    need_mix = 1;
		}
	}
    /* if hwsync output stream are enabled,write  other output to a mixe buffer and sleep for the pcm duration time  */
    if (adev->hwsync_output != NULL && adev->hwsync_output != out) {
        //ALOGI("dev hwsync enable,hwsync %p) cur (%p),size %d\n",adev->hwsync_output,out,bytes);
        //      out->frame_write_sum += in_frames;
#if 0
        if (!out->standby)
            do_output_standby(out);
#endif
        if (out->standby) {
            ret = start_output_stream(out);
            if (ret != 0) {
                pthread_mutex_unlock(&adev->lock);
                ALOGE("start_output_stream failed");
                goto exit;
            }
            out->standby = false;
        }
        ret = -1;
        aml_hal_mixer_write(&adev->hal_mixer,buffer,bytes);
        pthread_mutex_unlock(&adev->lock);
        goto exit;
    }
    if (out->pause_status == true) {
        pthread_mutex_unlock(&adev->lock);
        pthread_mutex_unlock(&out->lock);
        ALOGI("call out_write when pause status (%p)\n",stream);
        return 0;
    }
    if ((out->standby) && (out->hw_sync_mode == 1)) {
        // todo: check timestamp header PTS discontinue for new sync point after seek
        out->first_apts_flag = false;
        out->hw_sync_state = HW_SYNC_STATE_HEADER;
        out->hw_sync_header_cnt = 0;
    }

#if 1
    if (enable_dump && out->hw_sync_mode == 0) {
        FILE *fp1 = fopen("/data/tmp/i2s_audio_out.pcm", "a+");
        if (fp1) {
            int flen = fwrite((char *)buffer, 1, bytes, fp1);
            fclose(fp1);
        }
    }
#endif

    if (out->hw_sync_mode == 1) {
        char buf[64] = {0};
        unsigned char *header;

        if (out->hw_sync_state == HW_SYNC_STATE_RESYNC) {
            int i = 0;
            uint8_t *p = (uint8_t *)buffer;
            while (i < bytes) {
                if (hwsync_header_valid(p)) {
                    ALOGI("HWSYNC resync.%p",out);
                    out->hw_sync_state = HW_SYNC_STATE_HEADER;
                    out->hw_sync_header_cnt = 0;
                    out->first_apts_flag = false;
                    bytes -= i;
                    buffer += i;
                    in_frames = bytes / frame_size;
                    ALOGI("in_frames = %d", in_frames);
                    in_buffer = (int16_t *)buffer;
                    break;
                } else {
                    i += 4;
                    p += 4;
                }
            }

            if (out->hw_sync_state == HW_SYNC_STATE_RESYNC) {
                ALOGI("Keep searching for HWSYNC header.%p",out);
                pthread_mutex_unlock(&adev->lock);
                goto exit;
            }
        }

        header = (unsigned char *)buffer;
    }
    if (out->standby) {
        ret = start_output_stream(out);
        if (ret != 0) {
            pthread_mutex_unlock(&adev->lock);
            ALOGE("start_output_stream failed");
            goto exit;
        }
        out->standby = false;
        /* a change in output device may change the microphone selection */
        if (adev->active_input &&
            adev->active_input->source == AUDIO_SOURCE_VOICE_COMMUNICATION) {
            force_input_standby = true;
        }
    }
    pthread_mutex_unlock(&adev->lock);
#if 1
    /* Reduce number of channels, if necessary */
    if (popcount(out_get_channels(&stream->common)) >
        (int)out->config.channels) {
        unsigned int i;

        /* Discard right channel */
        for (i = 1; i < in_frames; i++) {
            in_buffer[i] = in_buffer[i * 2];
        }

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
    if (enable_dump && out->hw_sync_mode == 1) {
        FILE *fp1 = fopen("/data/tmp/i2s_audio_out.pcm", "a+");
        if (fp1) {
            int flen = fwrite((char *)in_buffer, 1, out_frames * frame_size, fp1);
            LOGFUNC("flen = %d---outlen=%d ", flen, out_frames * frame_size);
            fclose(fp1);
        } else {
            LOGFUNC("could not open file:/data/i2s_audio_out.pcm");
        }
    }
#endif
#if 1
    codec_type = get_sysfs_int("/sys/class/audiodsp/digital_codec");
    samesource_flag = get_sysfs_int("/sys/class/audiodsp/audio_samesource");
    if (samesource_flag == 0 && codec_type == 0) {
        ALOGI("to enable same source,need reset alsa,type %d,same source flag %d \n", codec_type, samesource_flag);
        pcm_stop(out->pcm);
    }
#endif
    if (out->is_tv_platform == 1) {
        int16_t *tmp_buffer = (int16_t *)out->audioeffect_tmp_buffer;
        memcpy((void *)tmp_buffer, (void *)in_buffer, out_frames * 4);
        audio_effect_process(stream, tmp_buffer, out_frames);
        for (i = 0; i < out_frames; i ++) {
            out->tmp_buffer_8ch[8 * i] = ((int32_t)(in_buffer[2 * i])) << 16;
            out->tmp_buffer_8ch[8 * i + 1] = ((int32_t)(in_buffer[2 * i + 1])) << 16;
            out->tmp_buffer_8ch[8 * i + 2] = ((int32_t)(tmp_buffer[2 * i])) << 16;
            out->tmp_buffer_8ch[8 * i + 3] = ((int32_t)(tmp_buffer[2 * i + 1])) << 16;
            out->tmp_buffer_8ch[8 * i + 4] = 0;
            out->tmp_buffer_8ch[8 * i + 5] = 0;
            out->tmp_buffer_8ch[8 * i + 6] = 0;
            out->tmp_buffer_8ch[8 * i + 7] = 0;
        }
        /*if (out->frame_count < 5*1024) {
            memset(out->tmp_buffer_8ch, 0, out_frames * frame_size * 8);
        }*/
        ret = pcm_write(out->pcm, out->tmp_buffer_8ch, out_frames * frame_size * 8);
    } else {
        if (out->hw_sync_mode) {

            int remain = out_frames * frame_size;
            uint8_t *p = buffer;

            //ALOGI(" --- out_write %d, cache cnt = %d, body = %d, hw_sync_state = %d", out_frames * frame_size, out->body_align_cnt, out->hw_sync_body_cnt, out->hw_sync_state);

            while (remain > 0) {
                if (out->hw_sync_state == HW_SYNC_STATE_HEADER) {
                    //ALOGI("Add to header buffer [%d], 0x%x", out->hw_sync_header_cnt, *p);
                    out->hw_sync_header[out->hw_sync_header_cnt++] = *p++;
                    remain--;
                    if (out->hw_sync_header_cnt == 16) {
                        uint64_t pts;
                        if (!hwsync_header_valid(&out->hw_sync_header[0])) {
                            ALOGE("hwsync header out of sync! Resync.");
                            out->hw_sync_state = HW_SYNC_STATE_RESYNC;
                            break;
                        }
                        out->hw_sync_state = HW_SYNC_STATE_BODY;
                        out->hw_sync_body_cnt = hwsync_header_get_size(&out->hw_sync_header[0]);
                        out->body_align_cnt = 0;
                        pts = hwsync_header_get_pts(&out->hw_sync_header[0]);
                        pts = pts * 90 / 1000000;
#if 1
                        char buf[64] = {0};
                        if (out->first_apts_flag == false) {
                            uint32_t apts_cal;
                            ALOGI("HW SYNC new first APTS %lld,body size %d", pts, out->hw_sync_body_cnt);
                            out->first_apts_flag = true;
                            out->first_apts = pts;
                            out->frame_write_sum = 0;
                            out->last_apts_from_header =    pts;
                            sprintf(buf, "AUDIO_START:0x%lx", pts & 0xffffffff);
                            ALOGI("tsync -> %s", buf);
                            if (sysfs_set_sysfs_str(TSYNC_EVENT, buf) == -1) {
                                ALOGE("set AUDIO_START failed \n");
                            }
                        } else {
                            int64_t apts;
                            uint32_t latency = out_get_latency(out) * 90;
                            apts = (uint64_t)out->frame_write_sum * 90000 / DEFAULT_OUT_SAMPLING_RATE;
                            apts += out->first_apts;
                            // check PTS discontinue, which may happen when audio track switching
                            // discontinue means PTS calculated based on first_apts and frame_write_sum
                            // does not match the timestamp of next audio samples
                            apts -= latency;
                            if (apts < 0) {
                                apts = 0;
                            }
                            // here we use acutal audio frame gap,not use the differece of  caculated current apts with the current frame pts,
                            //as there is a offset of audio latency from alsa.
                            // handle audio gap 0.5~5 s
                            unsigned two_frame_gap = (unsigned)abs(out->last_apts_from_header - pts);
                            if (two_frame_gap > APTS_DISCONTINUE_THRESHOLD_MIN  && two_frame_gap < APTS_DISCONTINUE_THRESHOLD_MAX) {
                                /*   if (abs(pts -apts) > APTS_DISCONTINUE_THRESHOLD_MIN && abs(pts -apts) < APTS_DISCONTINUE_THRESHOLD_MAX) { */
                                ALOGI("HW sync PTS discontinue, 0x%llx->0x%llx(from header) diff %d,last apts %llx(from header)",
                                      apts, pts, two_frame_gap, out->last_apts_from_header);
                                //here handle the audio gap and insert zero to the alsa
                                int insert_size = 0;
                                int insert_size_total = 0;
                                int once_write_size = 0;
                                insert_size = two_frame_gap/*abs(pts -apts) */ / 90 * 48 * 4;
                                insert_size = insert_size & (~63);
                                insert_size_total = insert_size;
                                ALOGI("audio gap %d ms ,need insert pcm size %d\n", two_frame_gap/*abs(pts -apts) */ / 90, insert_size);
                                char *insert_buf = (char*)malloc(8192);
                                if (insert_buf == NULL) {
                                    ALOGE("malloc size failed \n");
                                    pthread_mutex_unlock(&adev->lock);
                                    goto exit;
                                }
                                memset(insert_buf, 0, 8192);
                                if (need_mix) {
                                    mix_buf = malloc(once_write_size);
                                    if (mix_buf == NULL) {
                                        ALOGE("mix_buf malloc failed\n");
                                        free(insert_buf);
                                        pthread_mutex_unlock(&adev->lock);
                                        goto exit;
                                    }
                                }
                                while (insert_size > 0) {
                                    once_write_size = insert_size > 8192 ? 8192 : insert_size;
                                    if (need_mix) {
                                        pthread_mutex_lock(&adev->lock);
                                        aml_hal_mixer_read(&adev->hal_mixer,mix_buf,once_write_size);
                                        pthread_mutex_unlock(&adev->lock);
                                        memcpy(insert_buf,mix_buf,once_write_size);
                                     }
#if 1
                                     if (enable_dump) {
                                         FILE *fp1 = fopen("/data/tmp/i2s_audio_out.pcm", "a+");
                                         if (fp1) {
                                             int flen = fwrite((char *)insert_buf, 1, once_write_size, fp1);
                                             fclose(fp1);
                                         }
				     }
#endif
                                    ret = pcm_write(out->pcm, (void *) insert_buf, once_write_size);
                                    if (ret != 0) {
                                        ALOGE("pcm write failed\n");
                                        free(insert_buf);
                                        if (mix_buf)
                                            free(mix_buf);
                                        pthread_mutex_unlock(&adev->lock);
                                        goto exit;
                                    }
                                    insert_size -= once_write_size;
                                }
                                if (mix_buf)
                                    free(mix_buf);
                                mix_buf = NULL;
                                free(insert_buf);
                                // insert end
                                //adev->first_apts = pts;
                                out->frame_write_sum +=  insert_size_total / frame_size;
#if 0
                                sprintf(buf, "AUDIO_TSTAMP_DISCONTINUITY:0x%lx", pts);
                                if (sysfs_set_sysfs_str(TSYNC_EVENT, buf) == -1) {
                                    ALOGE("unable to open file %s,err: %s", TSYNC_EVENT, strerror(errno));
                                }
#endif
                            } else {
                                unsigned long pcr = 0;
                                if (get_sysfs_int16(TSYNC_PCRSCR, &pcr) == 0) {
                                    uint32_t apts_cal = apts & 0xffffffff;
                                    if (abs(pcr - apts_cal) < SYSTIME_CORRECTION_THRESHOLD) {
                                        // do nothing
                                    } else {
                                        sprintf(buf, "0x%lx", apts_cal);
                                        ALOGI("tsync -> reset pcrscr 0x%x -> 0x%x, diff %d ms,frame pts %llx,latency pts %d", pcr, apts_cal, (int)(apts_cal - pcr) / 90, pts, latency);
                                        int ret_val = sysfs_set_sysfs_str(TSYNC_APTS, buf);
                                        if (ret_val == -1) {
                                            ALOGE("unable to open file %s,err: %s", TSYNC_APTS, strerror(errno));
                                        }
                                    }
                                }
                            }
                            out->last_apts_from_header = pts;
                        }
#endif

                        //ALOGI("get header body_cnt = %d, pts = %lld", out->hw_sync_body_cnt, pts);
                    }
                    continue;
                } else if (out->hw_sync_state == HW_SYNC_STATE_BODY) {
                    int align;
                    int m = (out->hw_sync_body_cnt < remain) ? out->hw_sync_body_cnt : remain;

                    //ALOGI("m = %d", m);

                    // process m bytes, upto end of hw_sync_body_cnt or end of remaining our_write bytes.
                    // within m bytes, there is no hw_sync header and all are body bytes.
                    if (out->body_align_cnt) {
                        // clear fragment first for alignment limitation on ALSA driver, which
                        // requires each pcm_writing aligned at 16 frame boundaries
                        // assuming data are always PCM16 based, so aligned at 64 bytes unit.
                        if ((m + out->body_align_cnt) < 64) {
                            // merge only
                            memcpy(&out->body_align[out->body_align_cnt], p, m);
                            p += m;
                            remain -= m;
                            out->body_align_cnt += m;
                            out->hw_sync_body_cnt -= m;
                            if (out->hw_sync_body_cnt == 0) {
                                // end of body, research for HW SYNC header
                                out->hw_sync_state = HW_SYNC_STATE_HEADER;
                                out->hw_sync_header_cnt = 0;
                                continue;
                            }
                            //ALOGI("align cache add %d, cnt = %d", remain, out->body_align_cnt);
                            break;
                        } else {
                            // merge-submit-continue
                            memcpy(&out->body_align[out->body_align_cnt], p, 64 - out->body_align_cnt);
                            p += 64 - out->body_align_cnt;
                            remain -= 64 - out->body_align_cnt;
                            //ALOGI("pcm_write 64, out remain %d", remain);

                            short *w_buf = (short*)&out->body_align[0];

                            if (need_mix) {
                                short mix_buf[32];
                                pthread_mutex_lock(&adev->lock);
                                aml_hal_mixer_read(&adev->hal_mixer,mix_buf,64);
                                pthread_mutex_unlock(&adev->lock);

                                for (i = 0;i < 64/2/2;i++) {
                                    int r;
                                    r = w_buf[2*i] * out->volume_l + mix_buf[2*i];
                                    w_buf[2*i] = CLIP(r);
                                    r = w_buf[2*i+1] * out->volume_r + mix_buf[2*i+1];
                                    w_buf[2*i+1] = CLIP(r);
                                }
                            } else {
                                for (i = 0;i < 64/2/2;i++) {
                                    int r;
                                    r = w_buf[2*i] * out->volume_l;
                                    w_buf[2*i] = CLIP(r);
                                    r = w_buf[2*i+1] * out->volume_r;
                                    w_buf[2*i+1] = CLIP(r);
                                }
                            }
#if 1
                            if (enable_dump) {
                                FILE *fp1 = fopen("/data/tmp/i2s_audio_out.pcm", "a+");
                                if (fp1) {
                                    int flen = fwrite((char *)w_buf, 1, 64, fp1);
                                    fclose(fp1);
                                }
                            }
#endif
                            ret = pcm_write(out->pcm, w_buf, 64);
                            out->frame_write_sum += 64 / frame_size;
                            out->hw_sync_body_cnt -= 64 - out->body_align_cnt;
                            out->body_align_cnt = 0;
                            if (out->hw_sync_body_cnt == 0) {
                                out->hw_sync_state = HW_SYNC_STATE_HEADER;
                                out->hw_sync_header_cnt = 0;
                            }
                            continue;
                        }
                    }

                    // process m bytes body with an empty fragment for alignment
                    align = m & 63;
                    if ((m - align) > 0) {
                        short *w_buf = (short*)p;
                        mix_buf = (short *)malloc(m - align);
                        if (mix_buf == NULL) {
                            ALOGE("!!!fatal err,malloc %d bytes fail\n",m - align);
							ret = -1;
							goto exit;
						}
                        if (need_mix) {
                            pthread_mutex_lock(&adev->lock);
                            aml_hal_mixer_read(&adev->hal_mixer,mix_buf,m - align);
                            pthread_mutex_unlock(&adev->lock);
                            for (i = 0;i < (m - align)/2/2;i++) {
                                int r;
                                r = w_buf[2*i] * out->volume_l + mix_buf[2*i];
                                mix_buf[2*i] = CLIP(r);
                                r = w_buf[2*i+1] * out->volume_r + mix_buf[2*i+1];
                                mix_buf[2*i+1] = CLIP(r);
                            }
                        } else {
                            for (i = 0;i < (m - align)/2/2;i++) {

                                int r;
                                r = w_buf[2*i] * out->volume_l;
                                mix_buf[2*i] = CLIP(r);
                                r = w_buf[2*i+1] * out->volume_r;
                                mix_buf[2*i+1] = CLIP(r);
                            }
                        }
#if 1
                        if (enable_dump) {
                            FILE *fp1 = fopen("/data/tmp/i2s_audio_out.pcm", "a+");
                            if (fp1) {
                               int flen = fwrite((char *)mix_buf, 1, m - align, fp1);
                               fclose(fp1);
                            }
                        }
#endif
                        ret = pcm_write(out->pcm, mix_buf, m - align);
                        free(mix_buf);
                        out->frame_write_sum += (m - align) / frame_size;

                        p += m - align;
                        remain -= m - align;
                        //ALOGI("pcm_write %d, remain %d", m - align, remain);

                        out->hw_sync_body_cnt -= (m - align);
                        if (out->hw_sync_body_cnt == 0) {
                            out->hw_sync_state = HW_SYNC_STATE_HEADER;
                            out->hw_sync_header_cnt = 0;
                            continue;
                        }
                    }

                    if (align) {
                        memcpy(&out->body_align[0], p, align);
                        p += align;
                        remain -= align;
                        out->body_align_cnt = align;
                        //ALOGI("align cache add %d, cnt = %d, remain = %d", align, out->body_align_cnt, remain);

                        out->hw_sync_body_cnt -= align;
                        if (out->hw_sync_body_cnt == 0) {
                            out->hw_sync_state = HW_SYNC_STATE_HEADER;
                            out->hw_sync_header_cnt = 0;
                            continue;
                        }
                    }
                }
            }

        } else {
            ret = pcm_write(out->pcm, in_buffer, out_frames * frame_size);
            out->frame_write_sum += out_frames;
        }
    }

exit:
     latency_frames = out_get_latency(out) * out->config.rate / 1000;
     if (out->frame_write_sum>= latency_frames)
	 	out->last_frames_postion = out->frame_write_sum - latency_frames;
     else
	 	out->last_frames_postion = out->frame_write_sum;	
    pthread_mutex_unlock(&out->lock);
    if (ret != 0) {
        usleep(bytes * 1000000 / audio_stream_out_frame_size(stream) /
               out_get_sample_rate(&stream->common)*15/16);
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
    return oldBytes;
}

static int out_get_render_position(const struct audio_stream_out *stream,
                                   uint32_t *dsp_frames)
{
    struct aml_stream_out *out = (struct aml_stream_out *)stream;

    *dsp_frames = out->last_frames_postion;

    return 0;
}

static int out_add_audio_effect(const struct audio_stream *stream __unused, effect_handle_t effect __unused)
{
    return 0;
}

static int out_remove_audio_effect(const struct audio_stream *stream __unused, effect_handle_t effect __unused)
{
    return 0;
}
static int out_get_next_write_timestamp(const struct audio_stream_out *stream __unused,
                                        int64_t *timestamp __unused)
{
    return -EINVAL;
}

//actually maybe it be not useful now  except pass CTS_TEST:
//  run cts -c android.media.cts.AudioTrackTest -m testGetTimestamp
static int out_get_presentation_position(const struct audio_stream_out *stream, uint64_t *frames, struct timespec *timestamp)
{
    struct aml_stream_out *out = (struct aml_stream_out *)stream;
    if (frames != NULL) {
        *frames = out->last_frames_postion;
    }

    if (timestamp != NULL) {
        clock_gettime(CLOCK_MONOTONIC, timestamp);
    }

    return 0;
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
    if (adev->in_device & AUDIO_DEVICE_IN_BLUETOOTH_SCO_HEADSET) {
        port = get_pcm_bt_port();
    } else if (getprop_bool("sys.hdmiIn.Capture")) {
        port = get_spdif_port();
    } else {
        port = PORT_MM;
    }
    LOGFUNC("*%s, open card(%d) port(%d)-------", __FUNCTION__, card, port);
    in->config.period_size = CAPTURE_PERIOD_SIZE;
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

static int in_set_sample_rate(struct audio_stream *stream __unused, uint32_t rate __unused)
{
    return 0;
}

static size_t in_get_buffer_size(const struct audio_stream *stream)
{
    struct aml_stream_in *in = (struct aml_stream_in *)stream;

    return get_input_buffer_size(in->config.period_size,in->config.rate,
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

static audio_format_t in_get_format(const struct audio_stream *stream __unused)
{
    return AUDIO_FORMAT_PCM_16_BIT;
}

static int in_set_format(struct audio_stream *stream __unused, audio_format_t format __unused)
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
#if 0
        LOGFUNC("%s : output_standby=%d,input_standby=%d",
                __FUNCTION__, output_standby, input_standby);
        if (output_standby && input_standby) {
            reset_mixer_state(adev->ar);
            update_mixer_state(adev->ar);
        }
#endif
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

static int in_dump(const struct audio_stream *stream __unused, int fd __unused)
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

    if (do_standby) {
        do_input_standby(in);
    }
    pthread_mutex_unlock(&in->lock);
    pthread_mutex_unlock(&adev->lock);

    int framesize = 0;
    ret = str_parms_get_int(parms, AUDIO_PARAMETER_STREAM_FRAME_COUNT, &framesize);

    if (ret >= 0) {
        if (framesize > 0) {
            ALOGI("Reset audio input hw frame size from %d to %d\n",
                  in->config.period_size * in->config.period_count, framesize);
            in->config.period_size = framesize / in->config.period_count;
            pthread_mutex_lock(&adev->lock);
            pthread_mutex_lock(&in->lock);

            if (!in->standby && (in == adev->active_input)) {
                do_input_standby(in);
                start_input_stream(in);
                in->standby = 0;
            }

            pthread_mutex_unlock(&in->lock);
            pthread_mutex_unlock(&adev->lock);
        }
    }

    str_parms_destroy(parms);
    return ret;
}

static char * in_get_parameters(const struct audio_stream *stream __unused,
                                const char *keys __unused)
{
    return strdup("");
}

static int in_set_gain(struct audio_stream_in *stream __unused, float gain __unused)
{
    return 0;
}

static void get_capture_delay(struct aml_stream_in *in,
                              size_t frames __unused,
                              struct echo_reference_buffer *buffer)
{
    /* read frames available in kernel driver buffer */
    size_t kernel_frames;
    struct timespec tstamp;
    long buf_delay;
    long rsmp_delay;
    long kernel_delay;
    long delay_ns;
    int rsmp_mul = in->config.rate / VX_NB_SAMPLING_RATE;
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
    /*ALOGV("get_capture_delay time_stamp = [%ld].[%ld], delay_ns: [%d],"
        " kernel_delay:[%ld], buf_delay:[%ld], rsmp_delay:[%ld], kernel_frames:[%d], "
         "in->frames_in:[%d], in->proc_frames_in:[%d], frames:[%d]",
         buffer->time_stamp.tv_sec , buffer->time_stamp.tv_nsec, buffer->delay_ns,
         kernel_delay, buf_delay, rsmp_delay, kernel_frames,
         in->frames_in, in->proc_frames_in, frames);*/

}

static int32_t update_echo_reference(struct aml_stream_in *in, size_t frames)
{
    struct echo_reference_buffer b;
    b.delay_ns = 0;

    ALOGV("update_echo_reference, frames = [%d], in->ref_frames_in = [%d],  "
          "b.frame_count = [%d]", frames, in->ref_frames_in, frames - in->ref_frames_in);
    if (in->ref_frames_in < frames) {
        if (in->ref_buf_size < frames) {
            in->ref_buf_size = frames;
            in->ref_buf = (int16_t *)realloc(in->ref_buf,
                                             in->ref_buf_size * in->config.channels * sizeof(int16_t));
        }

        b.frame_count = frames - in->ref_frames_in;
        b.raw = (void *)(in->ref_buf + in->ref_frames_in * in->config.channels);

        get_capture_delay(in, frames, &b);
        LOGFUNC("update_echo_reference  return ::b.delay_ns=%d", b.delay_ns);

        if (in->echo_reference->read(in->echo_reference, &b) == 0) {
            in->ref_frames_in += b.frame_count;
            ALOGV("update_echo_reference: in->ref_frames_in:[%d], "
                  "in->ref_buf_size:[%d], frames:[%d], b.frame_count:[%d]",
                  in->ref_frames_in, in->ref_buf_size, frames, b.frame_count);
        }
    } else {
        ALOGW("update_echo_reference: NOT enough frames to read ref buffer");
    }
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
                                    sizeof(effect_param_t) + psize,
                                    param,
                                    &size,
                                    &param->status);
    if (status == 0) {
        status = param->status;
    }

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
    int32_t delay_us = update_echo_reference(in, frames) / 1000;
    int i;
    audio_buffer_t buf;

    if (in->ref_frames_in < frames) {
        frames = in->ref_frames_in;
    }

    buf.frameCount = frames;
    buf.raw = in->ref_buf;

    for (i = 0; i < in->num_preprocessors; i++) {
        if ((*in->preprocessors[i])->process_reverse == NULL) {
            continue;
        }

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

    if (buffer_provider == NULL || buffer == NULL) {
        return -EINVAL;
    }

    in = (struct aml_stream_in *)((char *)buffer_provider -
                                  offsetof(struct aml_stream_in, buf_provider));

    if (in->pcm == NULL) {
        buffer->raw = NULL;
        buffer->frame_count = 0;
        in->read_status = -ENODEV;
        return -ENODEV;
    }

    if (in->frames_in == 0) {
        in->read_status = pcm_read(in->pcm, (void*)in->buffer,
                                   in->config.period_size * audio_stream_in_frame_size(&in->stream));
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

    if (buffer_provider == NULL || buffer == NULL) {
        return;
    }

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
                                                          frames_wr * audio_stream_in_frame_size(&in->stream)),
                                                  &frames_rd);
        } else {
            struct resampler_buffer buf = {
                { raw : NULL, },
frame_count :
                frames_rd,
            };
            get_next_buffer(&in->buf_provider, &buf);
            if (buf.raw != NULL) {
                memcpy((char *)buffer +
                       frames_wr * audio_stream_in_frame_size(&in->stream),
                       buf.raw,
                       buf.frame_count * audio_stream_in_frame_size(&in->stream));
                frames_rd = buf.frame_count;
            }
            release_buffer(&in->buf_provider, &buf);
        }
        /* in->read_status is updated by getNextBuffer() also called by
         * in->resampler->resample_from_provider() */
        if (in->read_status != 0) {
            return in->read_status;
        }

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

        if (in->echo_reference != NULL) {
            push_echo_reference(in, in->proc_frames_in);
        }

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
        if (out_buf.frameCount == 0) {
            continue;
        }

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
    size_t frames_rq = bytes / audio_stream_in_frame_size(&in->stream);

    /* acquiring hw device mutex systematically is useful if a low priority thread is waiting
     * on the input stream mutex - e.g. executing select_mode() while holding the hw device
     * mutex
     */
    pthread_mutex_lock(&adev->lock);
    pthread_mutex_lock(&in->lock);
    if (in->standby) {
        ret = start_input_stream(in);
        if (ret == 0) {
            in->standby = 0;
        }
    }
    pthread_mutex_unlock(&adev->lock);

    if (ret < 0) {
        goto exit;
    }

    if (in->num_preprocessors != 0) {
        ret = process_frames(in, buffer, frames_rq);
    } else if (in->resampler != NULL) {
        ret = read_frames(in, buffer, frames_rq);
    } else {
        ret = pcm_read(in->pcm, buffer, bytes);
    }

    if (ret > 0) {
        ret = 0;
    }

    if (ret == 0 && adev->mic_mute) {
        memset(buffer, 0, bytes);
    }

#if 0
    FILE *dump_fp = NULL;

    dump_fp = fopen("/data/audio_in.pcm", "a+");
    if (dump_fp != NULL) {
        fwrite(buffer, bytes, 1, dump_fp);
        fclose(dump_fp);
    } else {
        ALOGW("[Error] Can't write to /data/dump_in.pcm");
    }
#endif

exit:
    if (ret < 0)
        usleep(bytes * 1000000 / audio_stream_in_frame_size(stream) /
               in_get_sample_rate(&stream->common));

    pthread_mutex_unlock(&in->lock);
    return bytes;

}

static uint32_t in_get_input_frames_lost(struct audio_stream_in *stream __unused)
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
    if (status != 0) {
        goto exit;
    }

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

    if (status != 0) {
        goto exit;
    }

    in->num_preprocessors--;

    status = (*effect)->get_descriptor(effect, &desc);
    if (status != 0) {
        goto exit;
    }
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
                                   audio_io_handle_t handle __unused,
                                   audio_devices_t devices __unused,
                                   audio_output_flags_t flags __unused,
                                   struct audio_config *config,
                                   struct audio_stream_out **stream_out)
{
    struct aml_audio_device *ladev = (struct aml_audio_device *)dev;
    struct aml_stream_out *out;
    int channel_count = popcount(config->channel_mask);
    int ret;

    LOGFUNC("**enter %s(devices=0x%04x,format=%d, ch=0x%04x, SR=%d, flags=0x%x)", __FUNCTION__, devices,
            config->format, config->channel_mask, config->sample_rate, flags);

    out = (struct aml_stream_out *)calloc(1, sizeof(struct aml_stream_out));
    if (!out) {
        return -ENOMEM;
    }

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
    out->stream.get_presentation_position = out_get_presentation_position;
    out->stream.pause = out_pause;
    out->stream.resume = out_resume;
    out->stream.flush = out_flush;
    out->config = pcm_config_out;
    out->is_tv_platform = 0;
    out->volume_l = 1.0;
    out->volume_r = 1.0;
    out->dev = ladev;
    out->standby = true;
    out->frame_write_sum = 0;
    out->hw_sync_mode = false;
    out->first_apts_flag = false;
	ALOGI("adev_open_output_stream stream %p,flag %d\n",out,flags);
    if (0/*flags & AUDIO_OUTPUT_FLAG_HW_AV_SYNC*/) {
        out->hw_sync_mode = true;
        ALOGI("Output stream open with AUDIO_OUTPUT_FLAG_HW_AV_SYNC");
    }
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

    if (getprop_bool("ro.platform.has.tvuimode")) {
        out->config.channels = 8;
        out->config.format = PCM_FORMAT_S32_LE;
        out->is_tv_platform = 1;
        out->tmp_buffer_8ch = malloc(out->config.period_size * 4 * 8);
        if (out->tmp_buffer_8ch == NULL) {
            ALOGE("cannot malloc memory for out->tmp_buffer_8ch");
            return -ENOMEM;
        }
        out->audioeffect_tmp_buffer = malloc(out->config.period_size * 6);
        if (out->audioeffect_tmp_buffer == NULL) {
            ALOGE("cannot malloc memory for audioeffect_tmp_buffer");
            return -ENOMEM;
        }
        //EQ lib load and init EQ
        ret = load_EQ_lib();
        if (ret < 0) {
            ALOGE("%s, Load EQ lib fail!\n", __FUNCTION__);
            out->has_EQ_lib = 0;
        } else {
            ret = HPEQ_init();
            if (ret < 0) {
                out->has_EQ_lib = 0;
            } else {
                out->has_EQ_lib = 1;
            }
            HPEQ_enable(1);
        }
        //load srs lib and init it.
        ret = load_SRS_lib();
        if (ret < 0) {
            ALOGE("%s, Load SRS lib fail!\n", __FUNCTION__);
            out->has_SRS_lib = 0;
        } else {
            ret = srs_init(48000);
            if (ret < 0) {
                out->has_SRS_lib = 0;
            } else {
                out->has_SRS_lib = 1;
            }
        }
        //load aml_IIR lib
        ret = load_aml_IIR_lib();
        if (ret < 0) {
            ALOGE("%s, Load aml_IIR lib fail!\n", __FUNCTION__);
            out->has_aml_IIR_lib = 0;
        } else {
            char value[PROPERTY_VALUE_MAX];
            int paramter = 0;
            if (property_get("media.audio.LFP.paramter", value, NULL) > 0) {
                paramter = atoi(value);
            }
            aml_IIR_init(paramter);
            out->has_aml_IIR_lib = 1;
        }
    }
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
    struct aml_audio_device *adev = (struct aml_audio_device *)dev;
    LOGFUNC("%s(%p, %p)", __FUNCTION__, dev, stream);
    if (out->is_tv_platform == 1) {
        free(out->tmp_buffer_8ch);
        free(out->audioeffect_tmp_buffer);
    }
    out_standby(&stream->common);
    if (adev->hwsync_output == out)
        adev->hwsync_output = NULL;
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
        if (strcmp(value, AUDIO_PARAMETER_VALUE_ON) == 0) {
            adev->low_power = false;
        } else {
            adev->low_power = true;
        }
    }
    str_parms_destroy(parms);
    return ret;
}

static char * adev_get_parameters(const struct audio_hw_device *dev __unused,
                                  const char *keys __unused)
{
    struct aml_audio_device *adev = (struct aml_audio_device *)dev;
    if (!strcmp(keys, AUDIO_PARAMETER_HW_AV_SYNC)) {
        ALOGI("get hwsync id\n");
        return strdup("hw_av_sync=12345678");
    }
    return strdup("");
}

static int adev_init_check(const struct audio_hw_device *dev __unused)
{
    return 0;
}

static int adev_set_voice_volume(struct audio_hw_device *dev __unused, float volume __unused)
{
    return 0;
}

static int adev_set_master_volume(struct audio_hw_device *dev __unused, float volume __unused)
{
    return -ENOSYS;
}

static int adev_get_master_volume(struct audio_hw_device *dev __unused,
                                  float *volume __unused)
{
    return -ENOSYS;
}

static int adev_set_master_mute(struct audio_hw_device *dev __unused, bool muted __unused)
{
    return -ENOSYS;
}

static int adev_get_master_mute(struct audio_hw_device *dev __unused, bool *muted __unused)
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
    if (check_input_parameters(config->sample_rate, config->format, channel_count) != 0) {
        return 0;
    }

    return get_input_buffer_size(config->frame_count,config->sample_rate,
                                 config->format, channel_count);

}

static int adev_open_input_stream(struct audio_hw_device *dev,
                                  audio_io_handle_t handle __unused,
                                  audio_devices_t devices,
                                  struct audio_config *config,
                                  struct audio_stream_in **stream_in,
                                  audio_input_flags_t flags __unused)
{
    struct aml_audio_device *ladev = (struct aml_audio_device *)dev;
    struct aml_stream_in *in;
    int ret;
    int channel_count = popcount(config->channel_mask);
    LOGFUNC("%s(%#x, %d, 0x%04x, %d)", __FUNCTION__,
            devices, config->format, config->channel_mask, config->sample_rate);
    if (check_input_parameters(config->sample_rate, config->format, channel_count) != 0) {
        return -EINVAL;
    }

    in = (struct aml_stream_in *)calloc(1, sizeof(struct aml_stream_in));
    if (!in) {
        return -ENOMEM;
    }

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
    if (in->device & AUDIO_DEVICE_IN_ALL_SCO) {
        memcpy(&in->config, &pcm_config_bt, sizeof(pcm_config_bt));
    } else {
        memcpy(&in->config, &pcm_config_in, sizeof(pcm_config_in));
    }

    if (in->config.channels == 1) {
        config->channel_mask = AUDIO_CHANNEL_IN_MONO;
    } else if (in->config.channels == 2) {
        config->channel_mask = AUDIO_CHANNEL_IN_STEREO;
    } else {
        ALOGE("Bad value of channel count : %d", in->config.channels);
    }
    in->buffer = malloc(in->config.period_size *
                        audio_stream_in_frame_size(&in->stream));
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
    *stream_in = &in->stream;
    return 0;

err_open:
    if (in->resampler) {
        release_resampler(in->resampler);
    }

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
    if (in->proc_buf) {
        free(in->proc_buf);
    }
    if (in->ref_buf) {
        free(in->ref_buf);
    }

    free(stream);

    return;
}

static int adev_dump(const audio_hw_device_t *device __unused, int fd __unused)
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
    int card = CARD_AMLOGIC_DEFAULT;
    int ret;
    if (strcmp(name, AUDIO_HARDWARE_INTERFACE) != 0) {
        return -EINVAL;
    }

    adev = calloc(1, sizeof(struct aml_audio_device));
    if (!adev) {
        return -ENOMEM;
    }

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
    card = get_aml_card();
    if ((card < 0) || (card > 7)) {
        ALOGE("error to get audio card");
        return -EINVAL;
    }

    adev->card = card;
    adev->ar = audio_route_init(adev->card, MIXER_XML_PATH);

    /* Set the default route before the PCM stream is opened */
    adev->mode = AUDIO_MODE_NORMAL;
    adev->out_device = AUDIO_DEVICE_OUT_SPEAKER;
    adev->in_device = AUDIO_DEVICE_IN_BUILTIN_MIC & ~AUDIO_DEVICE_BIT_IN;

    select_devices(adev);

    *device = &adev->hw_device.common;
    return 0;
}

static const struct hw_module_methods_t hal_module_methods = {
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
