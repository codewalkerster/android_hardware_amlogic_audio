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

#define LOG_TAG "usb_audio_hw"
//#define LOG_NDEBUG 0

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

#include "audio_resampler.h"

#define DEFAULT_OUT_SAMPLING_RATE 44100
#define RESAMPLER_BUFFER_SIZE 4096
#define DEFAULT_PERIOD_SIZE  1024
#define PERIOD_COUNT 4
#define BUFFSIZE 100000

struct pcm_config pcm_out_config = {
    .channels = 2,
    .rate = DEFAULT_OUT_SAMPLING_RATE,
    .period_size = DEFAULT_PERIOD_SIZE,
    .period_count = PERIOD_COUNT,
    .format = PCM_FORMAT_S16_LE,
};

struct pcm_config pcm_in_config = {
    .channels = 1,
    .rate = DEFAULT_OUT_SAMPLING_RATE,
    .period_size = DEFAULT_PERIOD_SIZE,
    .period_count = PERIOD_COUNT,
    .format = PCM_FORMAT_S16_LE,
};

struct aml_audio_device {
    struct audio_hw_device hw_device;

    pthread_mutex_t lock;       /* see note below on mutex acquisition order */
    int card;
    int card_device;
    audio_devices_t in_device;
    audio_devices_t out_device;
    struct aml_stream_in *active_input;
    struct aml_stream_out *active_output;

    bool mic_mute;
    bool standby;
};

struct aml_stream_out {
    struct audio_stream_out stream;
    pthread_mutex_t lock;       /* see note below on mutex acquisition order */
    struct pcm_config out_config;
    struct pcm *out_pcm;
    struct resample_para resampler;
    void *buffer;
    bool standby;

    struct aml_audio_device *dev;
};

struct aml_stream_in {
    struct audio_stream_in stream;
    pthread_mutex_t lock;       /* see note below on mutex acquisition order */
    struct pcm_config in_config;
    struct pcm *in_pcm;
    struct resampler_itfe *resampler;
    struct resampler_buffer_provider buf_provider;
    int16_t *buffer;
    size_t frames_in;
    unsigned int requested_rate;
    bool standby;
    int read_status;

    struct aml_audio_device *dev;
};
static int get_usb_card(struct aml_audio_device *dev);

int getnumOfRates(char *ratesStr){
    int i, size = 0;
    char *nextSRString, *temp_ptr;
    nextSRString = strtok_r(ratesStr, " ,", &temp_ptr);
    if (nextSRString == NULL) {
        ALOGE("ERROR: getnumOfRates: could not find rates string");
        return 0;
    }
    for (i = 1; nextSRString != NULL; i++) {
        size ++;
        nextSRString = strtok_r(NULL, " ,.-", &temp_ptr);
    }
    return size;
}

static int get_usb_cap(char *type, uint *channels, uint *sampleRate, int card)
{
    ALOGV("getCap for %s",type);
    long unsigned fileSize;
    FILE *fp;
    char path[32];
    char *buffer;
    int err = 1;
    int size = 0;
    int needRsmp = 1;
    int fd, i, lchannelsPlayback;
    char *read_buf, *str_start, *channel_start, *ratesStr, *ratesStrForVal,
    *ratesStrStart, *chString, *nextSRStr, *test, *nextSRString, *temp_ptr;
    struct stat st;
    memset(&st, 0x0, sizeof(struct stat));
    err = sprintf(path,"/proc/asound/card%d/stream0", card);
    ALOGD("path = %s",path);

    fd = open(path, O_RDONLY);
    if (fd <0) {
        ALOGE("ERROR: failed to open config file %s error: %d\n", path, errno);
        close(fd);
        return -EINVAL;
    }

    if (fstat(fd, &st) < 0) {
        ALOGE("ERROR: failed to stat %s error %d\n", path, errno);
        close(fd);
        return -EINVAL;
    }

    read_buf = (char *)malloc(BUFFSIZE);
    memset(read_buf, 0x0, BUFFSIZE);
    err = read(fd, read_buf, BUFFSIZE);
    str_start = strstr(read_buf, type);
    if (str_start == NULL) {
        ALOGE("ERROR:%s section not found in usb config file", type);
        close(fd);
        free(read_buf);
        return -EINVAL;
    }

    channel_start = strstr(str_start, "Channels:");
    if (channel_start == NULL) {
        ALOGE("ERROR: Could not find Channels information");
        close(fd);
        free(read_buf);
        return -EINVAL;
    }
    channel_start = strstr(channel_start, " ");
    if (channel_start == NULL) {
        ALOGE("ERROR: Channel section not found in usb config file");
        close(fd);
        free(read_buf);
        return -EINVAL;
    }

    lchannelsPlayback = atoi(channel_start);
    if (lchannelsPlayback == 1) {
        *channels = 1;
    } else {
        *channels = 2;
    }
    ratesStrStart = strstr(str_start, "Rates:");
    if (ratesStrStart == NULL) {
        ALOGE("ERROR: Cant find rates information");
        close(fd);
        free(read_buf);
        return -EINVAL;
    }

    ratesStrStart = strstr(ratesStrStart, " ");
    if (ratesStrStart == NULL) {
        ALOGE("ERROR: Channel section not found in usb config file");
        close(fd);
        free(read_buf);
        return -EINVAL;
    }

    //copy to ratesStr, current line.
    char *target = strchr(ratesStrStart, '\n');
    if (target == NULL) {
        ALOGE("ERROR: end of line not found");
        close(fd);
        free(read_buf);
        return -EINVAL;
    }
    size = target - ratesStrStart;
    ratesStr = (char *)malloc(size + 1) ;
    ratesStrForVal = (char *)malloc(size + 1) ;
    memcpy(ratesStr, ratesStrStart, size);
    memcpy(ratesStrForVal, ratesStrStart, size);
    ratesStr[size] = '\0';
    ratesStrForVal[size] = '\0';

    size = getnumOfRates(ratesStr);
    if (!size) {
        ALOGE("ERROR: Could not get rate size, returning");
        close(fd);
        free(ratesStrForVal);
        free(ratesStr);
        free(read_buf);
        return -EINVAL;
    }

    //populate playback rates array
    uint ratesSupported[size];
    nextSRString = strtok_r(ratesStrForVal, " ,", &temp_ptr);
    if (nextSRString == NULL) {
        ALOGE("ERROR: Could not get first rate val");
        close(fd);
        free(ratesStrForVal);
        free(ratesStr);
        free(read_buf);
        return -EINVAL;
    }

    ratesSupported[0] = atoi(nextSRString);
    ALOGV("ratesSupported[0] for %s:: %d", type, ratesSupported[0]);
    for (i = 1; i<size; i++) {
        nextSRString = strtok_r(NULL, " ,.-", &temp_ptr);
        ratesSupported[i] = atoi(nextSRString);
        ALOGV("ratesSupported[%d] for %s:: %d", i, type, ratesSupported[i]);
    }

    for (i = 0; i<size; i++) {
        if((*sampleRate == ratesSupported[i]) && (ratesSupported[i] <= 48000)) {
            needRsmp = 0;
            ALOGV("**sampleRate supports**");
        }
    }
    if (needRsmp) {
        *sampleRate = ratesSupported[size-1];
        ALOGE("sampleRate do not support!! Using Need resampler!!");
    }
    ALOGD("sampleRate: %d", *sampleRate);

    close(fd);
    free(ratesStrForVal);
    free(ratesStr);
    free(read_buf);
    ratesStrForVal = NULL;
    ratesStr = NULL;
    read_buf = NULL;
    return 0;
}

/**
 * NOTE: when multiple mutexes have to be acquired, always respect the
 * following order: hw device > out stream
 */

/* Helper functions */

/* must be called with hw device and output stream mutexes locked */
static int start_output_stream(struct aml_stream_out *out)
{
    ALOGD("%s", __func__);
    struct aml_audio_device *adev = out->dev;
    int i, err;
    if ((adev->card < 0) || (adev->card_device < 0)) {
        return -EINVAL;
    }
    if (adev->out_device & AUDIO_DEVICE_OUT_USB_DEVICE) {
        err = get_usb_cap("Playback:", &out->out_config.channels, &out->out_config.rate, adev->card);
        if (err) {
            ALOGE("ERROR: Could not get playback capabilities from usb device");
            return -EINVAL;
        }
    }
    out->buffer = NULL;
    if (out->out_config.rate != pcm_out_config.rate) {
        out->resampler.input_sr = pcm_out_config.rate;
        out->resampler.output_sr = out->out_config.rate;
        out->resampler.channels = out->out_config.channels;
        resampler_init(&out->resampler);
        int buffersize = DEFAULT_PERIOD_SIZE * out->out_config.rate / pcm_out_config.rate + 1;
        out->buffer = malloc(buffersize*4);
        ALOGE("out->buffer: %p, buffer_size = %d",out->buffer,buffersize);
        if (!out->buffer)
            return -ENOMEM;
    }

    out->out_pcm = pcm_open(adev->card, adev->card_device, PCM_OUT, &out->out_config);

    if (!pcm_is_ready(out->out_pcm)) {
        ALOGE("pcm_open() failed: %s", pcm_get_error(out->out_pcm));
        pcm_close(out->out_pcm);
        adev->active_output = NULL;
        return -ENOMEM;
    }
    return 0;
}

/* API functions */

static uint32_t out_get_sample_rate(const struct audio_stream *stream)
{
    return pcm_out_config.rate;
}

static int out_set_sample_rate(struct audio_stream *stream, uint32_t rate)
{
    ALOGD("%s(%p, %d)", __FUNCTION__, stream, rate);

    return 0;
}

static size_t out_get_buffer_size(const struct audio_stream *stream)
{
    struct aml_stream_out *out = (struct aml_stream_out *)stream;

    /* take resampling into account and return the closest majoring
    * multiple of 16 frames, as audioflinger expects audio buffers to
    * be a multiple of 16 frames
    */
    size_t size = (pcm_out_config.period_size * DEFAULT_OUT_SAMPLING_RATE) / out->out_config.rate;
    size = ((size + 15) / 16) * 16;
    return size * audio_stream_frame_size((struct audio_stream *)stream);
}

static audio_channel_mask_t out_get_channels(const struct audio_stream *stream)
{
    //ALOGD("%s(%p)", __FUNCTION__, stream);
    /*struct aml_stream_out *out = (struct aml_stream_out *)stream;

    if (out->config.channels == 1) {
        return AUDIO_CHANNEL_OUT_MONO;
    } else {
        return AUDIO_CHANNEL_OUT_STEREO;
    }
    */
    return AUDIO_CHANNEL_OUT_STEREO;
}

static audio_format_t out_get_format(const struct audio_stream *stream)
{
    //ALOGD("%s(%p)", __FUNCTION__, stream);
    return AUDIO_FORMAT_PCM_16_BIT;
}

static int out_set_format(struct audio_stream *stream, audio_format_t format)
{
    //ALOGD("%s(%p)", __FUNCTION__, stream);
    return 0;
}

/* must be called with hw device and output stream mutexes locked */
static int do_output_standby(struct aml_stream_out *out)
{
    struct aml_audio_device *adev = out->dev;

    ALOGD("%s(%p)", __FUNCTION__, out);

    if (!out->standby) {
        pcm_close(out->out_pcm);
        out->out_pcm = NULL;

        adev->active_output = 0;

        if (out->buffer != NULL){
            free(out->buffer);
            out->buffer = NULL;
        }
        out->standby = true;
    }
    return 0;
}

static int out_standby(struct audio_stream *stream)
{
    struct aml_stream_out *out = (struct aml_stream_out *)stream;
    int status;

    ALOGD("%s(%p)", __FUNCTION__, stream);

    pthread_mutex_lock(&out->dev->lock);
    pthread_mutex_lock(&out->lock);
    status = do_output_standby(out);
    pthread_mutex_unlock(&out->lock);
    pthread_mutex_unlock(&out->dev->lock);
    return status;
}

static int out_dump(const struct audio_stream *stream, int fd)
{
    ALOGD("%s(%p, %d)", __FUNCTION__, stream, fd);
    return 0;
}

static int out_set_parameters(struct audio_stream *stream, const char *kvpairs)
{
    struct aml_stream_out *out = (struct aml_stream_out *)stream;
    struct aml_audio_device *adev = out->dev;

    //int ret = get_usb_card(adev);
    //if (ret < 0){
    //  ALOGE("out_set_parameters*****ERROR: Could not get usb card number");
    //}

    struct str_parms *parms;
    char value[32];
    
    int ret = 0;
    int routing = 0;
    ALOGD("******%s*****%s*", __func__, kvpairs);

    parms = str_parms_create_str(kvpairs);
    pthread_mutex_lock(&adev->lock);

    ret = str_parms_get_str(parms, "card", value, sizeof(value));
    if (ret >= 0)
        adev->card = atoi(value);

    ret = str_parms_get_str(parms, "device", value, sizeof(value));
    if (ret >= 0)
        adev->card_device = atoi(value);
    pthread_mutex_unlock(&adev->lock);
    str_parms_destroy(parms);
    return 0;
}

static char * out_get_parameters(const struct audio_stream *stream, const char *keys)
{
    //ALOGD("%s(%p, %s)", __FUNCTION__, stream, keys);
    return strdup("");
}

static uint32_t out_get_latency(const struct audio_stream_out *stream)
{
    struct aml_stream_out *out = (struct aml_stream_out *)stream;
    return (pcm_out_config.period_size * pcm_out_config.period_count * 1000) /
            out->out_config.rate;
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
    size_t out_frames = RESAMPLER_BUFFER_SIZE / frame_size;
    bool force_input_standby = false;
    //struct aml_stream_in *in;
    int kernel_frames;
    void *buf;
    //ALOGD("*****out_write**device=0x%x****",adev->out_device);
    pthread_mutex_lock(&out->dev->lock);
    pthread_mutex_lock(&out->lock);
    if (out->standby) {
        ret = start_output_stream(out);
        if (ret != 0) {
            pthread_mutex_unlock(&adev->lock);
            goto exit;
        }
        out->standby = false;
    }
#if 0
    FILE * fp=fopen("/data/audio_in","a+");
    if (fp) {
        int flen=fwrite((char *)buffer,1,bytes,fp);
        ALOGD("flen = %d---audio_in=%d ", flen, bytes);
        fclose(fp);
    }else{
        ALOGD("could not open file: audio_in");
    }
#endif
    /* only use resampler if required */
    if (out->out_config.rate != DEFAULT_OUT_SAMPLING_RATE) {
        out_frames = resample_process(&out->resampler, in_frames,
                        (short *)buffer, (short *)out->buffer);
        buf = out->buffer;
    } else {
        out_frames = in_frames;
        buf = (void *)buffer;
    }

    pcm_write(out->out_pcm, (void *)buf, out_frames * out->out_config.channels * 2);

    pthread_mutex_unlock(&out->lock);
    pthread_mutex_unlock(&out->dev->lock);

    return bytes;

exit:
    pthread_mutex_unlock(&out->lock);
    if (ret != 0) {
        usleep(bytes * 1000000 / audio_stream_frame_size(&stream->common) /
               out_get_sample_rate(&stream->common));
    }

    return bytes;
}

static int out_get_render_position(const struct audio_stream_out *stream,
                                   uint32_t *dsp_frames)
{
    //ALOGD("%s(%p, %p)", __FUNCTION__, stream, dsp_frames);
    return -EINVAL;
}

static int out_add_audio_effect(const struct audio_stream *stream, effect_handle_t effect)
{
    ALOGD("%s(%p, %p)", __FUNCTION__, stream, effect);
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


static uint32_t in_get_sample_rate(const struct audio_stream *stream)
{
    struct aml_stream_in *in = (struct aml_stream_in *)stream;

    //ALOGD("%s(%p)", __FUNCTION__, stream);
    return in->requested_rate;
}

static int in_set_sample_rate(struct audio_stream *stream, uint32_t rate)
{
    //ALOGD("%s(%p, %d)", __FUNCTION__, stream, rate);
    return 0;
}
static size_t get_input_buffer_size(uint32_t sample_rate, int format, int channel_count)
{
    size_t size;
    size_t device_rate;

    /* take resampling into account and return the closest majoring
    multiple of 16 frames, as audioflinger expects audio buffers to
    be a multiple of 16 frames */
    size = (pcm_in_config.period_size * sample_rate) / pcm_in_config.rate;
    size = ((size + 15) / 16) * 16;

    return size * channel_count * sizeof(short);
}

static size_t in_get_buffer_size(const struct audio_stream *stream)
{
    struct aml_stream_in *in = (struct aml_stream_in *)stream;

    //ALOGD("%s(%p)", __FUNCTION__, stream);
    return get_input_buffer_size(in->in_config.rate,
                                 AUDIO_FORMAT_PCM_16_BIT,
                                 in->in_config.channels);
}

static audio_channel_mask_t in_get_channels(const struct audio_stream *stream)
{
    struct aml_stream_in *in = (struct aml_stream_in *)stream;
    //ALOGD("%s(%p)", __FUNCTION__, stream);

    if (in->in_config.channels == 1) {
        return AUDIO_CHANNEL_IN_MONO;
    } else {
        return AUDIO_CHANNEL_IN_STEREO;
    }
}

static audio_format_t in_get_format(const struct audio_stream *stream)
{
    //ALOGD("%s(%p)", __FUNCTION__, stream);
    return AUDIO_FORMAT_PCM_16_BIT;
}

static int in_set_format(struct audio_stream *stream, audio_format_t format)
{
    //ALOGD("%s(%p, %d)", __FUNCTION__, stream, format);
    return 0;
}

/* must be called with hw device and input stream mutexes locked */
static int do_input_standby(struct aml_stream_in *in)
{
    struct aml_audio_device *adev = in->dev;
    ALOGD("******%s******", __func__);

    if (!in->standby) {
        pcm_close(in->in_pcm);
        in->in_pcm = NULL;
        adev->active_input = 0;
        in->standby = true;
    }
    return 0;
}
static int in_standby(struct audio_stream *stream)
{
    struct aml_stream_in *in = (struct aml_stream_in *)stream;
    int status;
    ALOGD("%s(%p)", __FUNCTION__, stream);

    pthread_mutex_lock(&in->dev->lock);
    pthread_mutex_lock(&in->lock);
    status = do_input_standby(in);
    pthread_mutex_unlock(&in->lock);
    pthread_mutex_unlock(&in->dev->lock);
    return status;
}

static int in_dump(const struct audio_stream *stream, int fd)
{
    //ALOGD("%s(%p, %d)", __FUNCTION__, stream, fd);
    return 0;
}

static int in_set_parameters(struct audio_stream *stream, const char *kvpairs)
{
    return 0;
}

static char * in_get_parameters(const struct audio_stream *stream,
                                const char *keys)
{
    //ALOGD("%s(%p, %s)", __FUNCTION__, stream, keys);
    return strdup("");
}

static int in_set_gain(struct audio_stream_in *stream, float gain)
{
    //ALOGD("%s(%p, %f)", __FUNCTION__, stream, gain);
    return 0;
}

#define USB_AUDIO_PCM "/proc/asound/usb_audio_info"

static int get_usb_card(struct aml_audio_device *dev)
{
    int card = -1,err=0;
    int fd;
    struct aml_audio_device *adev = dev;
    uint i=0,usbid;
    int string_length=32,str_len=8;
    char *read_buf, *str;
    int card_num;
    char *str_start;
    fd = open(USB_AUDIO_PCM, O_RDONLY);
    
    if (fd <0) {
        ALOGE("ERROR: failed to open config file %s error: %d\n", USB_AUDIO_PCM, errno);
        close(fd);
        return -EINVAL;
    }
  
    read_buf = (char *)malloc(string_length);
    str = (char*)malloc(str_len);
    memset(read_buf, 0x0, string_length);
    memset(read_buf, 0x0, str_len);
    err = read(fd, read_buf, string_length);
    memcpy(str,read_buf,8);
    //ALOGD("****str=%s****",str);
        
    usbid = strtoul(str, NULL, 16);
    card = atoi(read_buf + 9);
    adev->card = card;
    ALOGD("******get_usb_card***card=%d***",adev->card);
    adev->card_device= 0;
    
    free(read_buf);
    free(str);
    read_buf = NULL;
    str = NULL;
    close(fd);
    return err;


}

static int get_next_buffer(struct resampler_buffer_provider *buffer_provider,
                                   struct resampler_buffer* buffer)
{
    struct aml_stream_in *in;

    //ALOGD("%s(%p, %p)", __FUNCTION__, buffer_provider, buffer);
    if (buffer_provider == NULL || buffer == NULL)
        return -EINVAL;

    in = (struct aml_stream_in *)((char *)buffer_provider -
                                   offsetof(struct aml_stream_in, buf_provider));

    if (in->in_pcm == NULL) {
        buffer->raw = NULL;
        buffer->frame_count = 0;
        in->read_status = -ENODEV;
        return -ENODEV;
    }

    if (in->frames_in == 0) {
        in->read_status = pcm_read(in->in_pcm,
                                   (void*)in->buffer,
                                   in->in_config.period_size *
                                       audio_stream_frame_size(&in->stream.common));
        if (in->read_status != 0) {
            ALOGE("get_next_buffer() pcm_read error %d", in->read_status);
            buffer->raw = NULL;
            buffer->frame_count = 0;
            return in->read_status;
        }
        in->frames_in = in->in_config.period_size;
    }

    buffer->frame_count = (buffer->frame_count > in->frames_in) ?
                                in->frames_in : buffer->frame_count;
    buffer->i16 = in->buffer + (in->in_config.period_size - in->frames_in) *
                                                in->in_config.channels;

    return in->read_status;

}

static void release_buffer(struct resampler_buffer_provider *buffer_provider,
                                  struct resampler_buffer* buffer)
{
    struct aml_stream_in *in;

    //ALOGD("%s(%p, %p)", __FUNCTION__, buffer_provider, buffer);
    if (buffer_provider == NULL || buffer == NULL)
        return;

    in = (struct aml_stream_in *)((char *)buffer_provider -
                                   offsetof(struct aml_stream_in, buf_provider));

    in->frames_in -= buffer->frame_count;
}

static int start_input_stream(struct aml_stream_in *in)
{
    int err;
    struct aml_audio_device *adev = in->dev;
    adev->active_input = in;
    //get_usb_card(in);
    if ((adev->card < 0) || (adev->card_device < 0)) {
        ALOGE("ERROR: Could not get usb card info");
        return -EINVAL;
    }
#if 0
    err = get_usb_cap((char *)"Capture:", &in->in_config.channels, &in->in_config.rate, adev->card);
    if (err) {
        ALOGE("ERROR: Could not get capture capabilities from usb device");
        return -EINVAL;
    }
    if (in->requested_rate != in->in_config.rate) {
        in->buf_provider.get_next_buffer = get_next_buffer;
        in->buf_provider.release_buffer = release_buffer;
        ALOGD("Create resampler for input stream");
        err = create_resampler(in->in_config.rate,
                               in->requested_rate,
                               in->in_config.channels,
                               RESAMPLER_QUALITY_DEFAULT,
                               &in->buf_provider,
                               &in->resampler);
        if (err != 0) {
            err = -EINVAL;
            goto err;
        }
    }
#endif
    ALOGD("pcm_open in: card(%d), port(%d)", adev->card, adev->card_device);
    in->in_pcm = pcm_open(adev->card, adev->card_device, PCM_IN, &in->in_config);
    if (!pcm_is_ready(in->in_pcm)) {
        ALOGE("cannot open pcm_in driver: %s", pcm_get_error(in->in_pcm));
        pcm_close(in->in_pcm);
        adev->active_input = NULL;
        return -ENOMEM;
    }
    ALOGD("pcm_open in: card(%d), port(%d)", adev->card, adev->card_device);
    return 0;

err:
    if (in->resampler)
        release_resampler(in->resampler);
    return err;
}
/* read_frames() reads frames from kernel driver, down samples to capture rate
 * if necessary and output the number of frames requested to the buffer specified */
static ssize_t read_frames(struct aml_stream_in *in, void *buffer, ssize_t frames)
{
    ssize_t frames_wr = 0;
    //ALOGD("%s(in->resampler=%p, %ld)", __FUNCTION__, in->resampler, frames);
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

static ssize_t in_read(struct audio_stream_in *stream, void* buffer,
                       size_t bytes)
{
    // ALOGD("============in_read====bytes=%d=======",bytes);
    int ret = 0;
    struct aml_stream_in *in = (struct aml_stream_in *)stream;
    struct aml_audio_device *adev = in->dev;
    size_t frames_rq = bytes / audio_stream_frame_size(&stream->common);
    //ALOGD("%s(in->num_preprocessors=%d, frames_rq=%d)", __FUNCTION__, in->num_preprocessors, frames_rq);

    /* acquiring hw device mutex systematically is useful if a low priority thread is waiting
    * on the input stream mutex - e.g. executing select_mode() while holding the hw device
    * mutex
    */
    pthread_mutex_lock(&adev->lock);
    pthread_mutex_lock(&in->lock);
    if (in->standby) {
        ret = start_input_stream(in);
        if (ret == 0)
            in->standby = 0;
    }
    pthread_mutex_unlock(&adev->lock);

#if 0
    FILE * fp=fopen("/data/audio_in/temp.data","a+");
    if (fp) {
        int flen=fwrite((char *)buffer,1,bytes,fp);
        ALOGD("flen = %d---audio_in=%d ", flen, bytes);
        fclose(fp);
    }else{
        ALOGD("could not open file: audio_in");
    }
#endif

    if (ret < 0)
        goto exit;

    if (in->resampler != NULL)
        ret = read_frames(in, buffer, frames_rq);
    else
        ret = pcm_read(in->in_pcm, buffer, bytes);
    if (ret > 0)
        ret = 0;

    if (ret == 0 && adev->mic_mute) {
        memset(buffer, 0, bytes);
    }

#if 0
    FILE *dump_fp = NULL;

    dump_fp = fopen("/data/dump_in.pcm", "a+");
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
    return 0;
}

static int in_remove_audio_effect(const struct audio_stream *stream,
                                  effect_handle_t effect)
{
    return 0;
}

static int adev_open_output_stream(struct audio_hw_device *dev,
                                   audio_io_handle_t handle,
                                   audio_devices_t devices,
                                   audio_output_flags_t flags,
                                   struct audio_config *config,
                                   struct audio_stream_out **stream_out)
{
    struct aml_audio_device *adev = (struct aml_audio_device *)dev;
    struct aml_stream_out *out;
    struct mixer *usb_mixer;
    int ret = 0;

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
    //out->stream.common.set_voip_mode = out_set_voip_mode;
    out->stream.get_latency = out_get_latency;
    out->stream.set_volume = out_set_volume;
    out->stream.write = out_write;
    out->stream.get_render_position = out_get_render_position;
    out->stream.get_next_write_timestamp = out_get_next_write_timestamp;
    out->out_config = pcm_out_config;

    out->dev = adev;
    //ret = get_usb_card(adev);
    //if (ret < 0){
    //    ALOGE("ERROR: Could not get usb card number");
    //    goto err_open;
    //}

    usb_mixer = mixer_open(adev->card);
    if (!usb_mixer) {
        ALOGE("Unable to open the mixer, aborting.");
        goto err_open;
    }
    config->format = out_get_format(&out->stream.common);
    config->channel_mask = out_get_channels(&out->stream.common);
    config->sample_rate = out_get_sample_rate(&out->stream.common);
    out->standby = true;

    adev->card = -1;
    adev->card_device = -1;
    adev->out_device = devices;

    *stream_out = &out->stream;
    return 0;

err_open:
    mixer_close(usb_mixer);
    free(out);
    *stream_out = NULL;
    return ret;
}

static void adev_close_output_stream(struct audio_hw_device *dev,
                                     struct audio_stream_out *stream)
{
    struct aml_stream_out *out = (struct aml_stream_out *)stream;
    ALOGD("******%s******", __func__);//

    out_standby(&stream->common);
    free(stream);
}

static int adev_set_parameters(struct audio_hw_device *dev, const char *kvpairs)
{
    return 0;
}

static char * adev_get_parameters(const struct audio_hw_device *dev,
                                  const char *keys)
{
    //ALOGD("%s(%p, %s)", __FUNCTION__, dev, keys);
    return strdup("");
}

static int adev_init_check(const struct audio_hw_device *dev)
{
    //ALOGD("%s(%p)", __FUNCTION__, dev);
    return 0;
}

static int adev_set_voice_volume(struct audio_hw_device *dev, float volume)
{
    struct aml_audio_device *adev = (struct aml_audio_device *)dev;

    //adev->voice_volume = volume;

    //ALOGD("%s(%p, %f)", __FUNCTION__, dev, volume);
    //if (adev->mode == AUDIO_MODE_IN_CALL)
    //    ril_set_call_volume(&adev->ril, SOUND_TYPE_VOICE, volume);

    return 0;
}

static int adev_set_master_volume(struct audio_hw_device *dev, float volume)
{
    //ALOGD("%s(%p, %f)", __FUNCTION__, dev, volume);
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
    return 0;
}

static int adev_set_mic_mute(struct audio_hw_device *dev, bool state)
{
    return -ENOSYS;
}

static int adev_get_mic_mute(const struct audio_hw_device *dev, bool *state)
{
    return -ENOSYS;
}

static size_t adev_get_input_buffer_size(const struct audio_hw_device *dev,
                                         const struct audio_config *config)
{
    return 0;
}

static int adev_open_input_stream(struct audio_hw_device *dev,
                                  audio_io_handle_t handle,
                                  audio_devices_t devices,
                                  struct audio_config *config,
                                  struct audio_stream_in **stream_in)
{
    struct aml_audio_device *adev = (struct aml_audio_device *)dev;
    struct aml_stream_in *in;
    struct mixer *usb_mixer = NULL;
    int ret = 0;
   
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
    //in->stream.common.set_voip_mode = in_set_voip_mode;
    in->stream.set_gain = in_set_gain;
    in->stream.read = in_read;
    in->stream.get_input_frames_lost = in_get_input_frames_lost;

    in->requested_rate = config->sample_rate;
    in->in_config=pcm_in_config;

    //memcpy(&in->in_config, &pcm_config_in, sizeof(pcm_config_in));
    in->dev = adev;
    in->in_config.rate = in->requested_rate;

    //ret = get_usb_card(adev);
    //if (ret < 0){
    //    ALOGE("ERROR: Could not get usb card number");
    //    goto err_open;
    //}

    usb_mixer = mixer_open(adev->card);
    if (!usb_mixer) {
        ALOGE("Unable to open the mixer, aborting.");
        goto err_open;
    }
    
    ret = get_usb_cap("Capture:", &in->in_config.channels, &in->in_config.rate, adev->card);
    
    if (ret) {
        ALOGE("ERROR: Could not get capture capabilities from usb device");
        goto err_open;
    }
    if (in->requested_rate != in->in_config.rate) {
        in->buffer = malloc(in->in_config.period_size *
                            audio_stream_frame_size(&in->stream.common));
        if (!in->buffer) {
            ret = -ENOMEM;
            goto err_open;
        }
        in->buf_provider.get_next_buffer = get_next_buffer;
        in->buf_provider.release_buffer = release_buffer;
        ALOGD("Create resampler for input stream");
        ret = create_resampler(in->in_config.rate,
                               in->requested_rate,
                               in->in_config.channels,
                               RESAMPLER_QUALITY_DEFAULT,
                               &in->buf_provider,
                               &in->resampler);
        if (ret != 0) {
            ret = -EINVAL;
            goto err_open;
        }
    }

    config->format = in_get_format(&in->stream.common);
    config->channel_mask = in_get_channels(&in->stream.common);
    config->sample_rate = in_get_sample_rate(&in->stream.common);
    
    in->standby = true;
    *stream_in = &in->stream;
    return 0;

err_open:
    if (in->resampler)
        release_resampler(in->resampler);
    mixer_close(usb_mixer);
    free(in);
    *stream_in = NULL;
    return ret;
}

static void adev_close_input_stream(struct audio_hw_device *dev,
                                   struct audio_stream_in *stream)
{
    struct aml_stream_in *in = (struct aml_stream_in *)stream;
    ALOGD("******%s******", __func__);

    in_standby(&stream->common);
    free(stream);
}

static int adev_dump(const audio_hw_device_t *device, int fd)
{
    //ALOGD("%s(%p, %d)", __FUNCTION__, device, fd);
    return 0;
}

static int adev_close(hw_device_t *device)
{
    struct aml_audio_device *adev = (struct aml_audio_device *)device;

    ALOGD("%s(%p)", __func__, device);

    free(device);
    return 0;
}

#if 0
static uint32_t adev_get_supported_devices(const struct audio_hw_device *dev)
{
    return AUDIO_DEVICE_OUT_ALL_USB|AUDIO_DEVICE_IN_USB_DEVICE;
}

#endif
static int adev_open(const hw_module_t* module, const char* name,
                     hw_device_t** device)
{
    struct aml_audio_device *adev;
    int ret;
    ALOGD("%s(%p, %s, %p)", __FUNCTION__, module, name, device);
    if (strcmp(name, AUDIO_HARDWARE_INTERFACE) != 0)
        return -EINVAL;

    adev = calloc(1, sizeof(struct aml_audio_device));
    if (!adev)
        return -ENOMEM;

    adev->hw_device.common.tag = HARDWARE_DEVICE_TAG;
    adev->hw_device.common.version = AUDIO_DEVICE_API_VERSION_2_0;
    adev->hw_device.common.module = (struct hw_module_t *) module;
    adev->hw_device.common.close = adev_close;

    //adev->hw_device.get_supported_devices = adev_get_supported_devices;
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
        .name = "USB audio HW HAL",
        .author = "amlogic, Corp.",
        .methods = &hal_module_methods,
    },
};
