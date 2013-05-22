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

#define LOG_TAG "audio_hw_hdmi"
#define LOG_NDEBUG 0
#define LOG_NDEBUG_FUNCTION
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

#include <cutils/log.h>
#include <cutils/str_parms.h>
#include <cutils/properties.h>

#include <hardware/hardware.h>
#include <system/audio.h>
#include <hardware/audio.h>

#include <tinyalsa/asoundlib.h>
#include <audio_utils/resampler.h>

/* ALSA cards for AML */
#define CARD_AMLOGIC_BOARD 0 
#define CARD_AMLOGIC_DEFAULT CARD_AMLOGIC_BOARD 
/* ALSA ports for AML */
#define PORT_MM 1
/* number of frames per period */
#define PERIOD_SIZE 256//1024
/* number of periods for low power playback */
#define PLAYBACK_PERIOD_COUNT 4
/* number of periods for capture */
#define CAPTURE_PERIOD_COUNT 4

/* minimum sleep time in out_write() when write threshold is not reached */
#define MIN_WRITE_SLEEP_US 5000

#define RESAMPLER_BUFFER_FRAMES (PERIOD_SIZE * 8)
#define RESAMPLER_BUFFER_SIZE (4 * RESAMPLER_BUFFER_FRAMES)

#define DEFAULT_OUT_SAMPLING_RATE 48000

/* sampling rate when using MM low power port */
#define MM_LOW_POWER_SAMPLING_RATE 44100
/* sampling rate when using MM full power port */
#define MM_FULL_POWER_SAMPLING_RATE 48000
/* sampling rate when using VX port for narrow band */
#define VX_NB_SAMPLING_RATE 8000

struct pcm_config pcm_config_out = {
    .channels = 8,
    .rate = MM_FULL_POWER_SAMPLING_RATE,
    .period_size = PERIOD_SIZE,
    .period_count = PLAYBACK_PERIOD_COUNT,
    .format = PCM_FORMAT_S32_LE,
};


struct aml_audio_device {
    struct audio_hw_device hw_device;

	pthread_mutex_t lock;       /* see note below on mutex acquisition order */

    int mode;
	audio_devices_t out_device;
    
 
	struct aml_stream_out *active_output;

	bool mic_mute;
};

struct aml_stream_out {
    struct audio_stream_out stream;
	pthread_mutex_t lock;       /* see note below on mutex acquisition order */
    struct pcm_config config;
    struct pcm *pcm;
    struct resampler_itfe *resampler;
    char *buffer;
    int standby;
    struct aml_audio_device *dev;
    int write_threshold;
};

static char cache_buffer_bytes[64];
static uint cached_len=0;

static int adev_set_voice_volume(struct audio_hw_device *dev, float volume);
static int do_output_standby(struct aml_stream_out *out);

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

static int check_output_stream(struct aml_stream_out *out)
{
    int ret = 0;
    unsigned int card = CARD_AMLOGIC_DEFAULT;
    unsigned int port = PORT_MM;
    int ext_card;
	ALOGI("*******%s*********", __FUNCTION__);

    ext_card = get_external_card(0);
    if(ext_card < 0)
    {
        card = CARD_AMLOGIC_DEFAULT;
    }
    else
    {
        card = ext_card;
    }

    out->config.start_threshold = PERIOD_SIZE * 2;
    out->config.avail_min = 0;//SHORT_PERIOD_SIZE;
	ALOGI("%s(out->config.rate=%d)", __FUNCTION__,out->config.rate);

    /* this assumes routing is done previously */
    out->pcm = pcm_open(card, port, PCM_OUT, &out->config);
    if (!pcm_is_ready(out->pcm)) {
        ALOGI("check_out_stream:cannot open pcm_out driver: %s", pcm_get_error(out->pcm));
        pcm_close(out->pcm);
        return -ENOMEM;
    }

    pcm_close(out->pcm);

    return 0;
}


/* must be called with hw device and output stream mutexes locked */
static int start_output_stream(struct aml_stream_out *out)
{
    struct aml_audio_device *adev = out->dev;
    unsigned int card = CARD_AMLOGIC_DEFAULT;
    unsigned int port = PORT_MM;
	int ret;

    adev->active_output = out;

    ALOGI("%s(adev->out_device=%#x, adev->mode=%d)", __FUNCTION__, adev->out_device, adev->mode);

	card = CARD_AMLOGIC_BOARD;
    port = PORT_MM;
	ALOGI("------------open on board audio-------");
	    out->config.rate = MM_FULL_POWER_SAMPLING_RATE;
    /* default to low power: will be corrected in out_write if necessary before first write to
     * tinyalsa.
     */
    out->write_threshold = PLAYBACK_PERIOD_COUNT * PERIOD_SIZE;
    out->config.start_threshold = PERIOD_SIZE * PLAYBACK_PERIOD_COUNT;
    out->config.avail_min = 0;//SHORT_PERIOD_SIZE;
    
	
    if(out->config.rate!=DEFAULT_OUT_SAMPLING_RATE){
	
		ret = create_resampler(DEFAULT_OUT_SAMPLING_RATE,
	            out->config.rate,
	            2,
	            RESAMPLER_QUALITY_DEFAULT,
	            NULL,
	            &out->resampler);
	    if (ret != 0)
	    {
	        ALOGI("cannot create resampler for output");
			return ENOMEM;
		}
		out->buffer = malloc(RESAMPLER_BUFFER_SIZE); /* todo: allow for reallocing */
		if (out->buffer == NULL)
			return ENOMEM;
    }

	ALOGI("channels=%d---format=%d---period_count%d---period_size%d---rate=%d---",
		                 out->config.channels, out->config.format, out->config.period_count, 
		                 out->config.period_size, out->config.rate);
    out->pcm = pcm_open(card, port, PCM_OUT, &(out->config));

    if (!pcm_is_ready(out->pcm)) {
        ALOGI("cannot open pcm_out driver: %s", pcm_get_error(out->pcm));
        pcm_close(out->pcm);
        adev->active_output = NULL;
        return -ENOMEM;
    }
    
    return 0;
}

static uint32_t out_get_sample_rate(const struct audio_stream *stream)
{
	
    ALOGI("%s(%p)", __FUNCTION__, stream);
    return DEFAULT_OUT_SAMPLING_RATE;
}

static int out_set_sample_rate(struct audio_stream *stream, uint32_t rate)
{
    ALOGI("%s(%p, %d)", __FUNCTION__, stream, rate);

    return 0;
}

static size_t out_get_buffer_size(const struct audio_stream *stream)
{
    struct aml_stream_out *out = (struct aml_stream_out *)stream;

    ALOGI("%s(out->config.rate=%d)", __FUNCTION__, out->config.rate);

   
    size_t size = (PERIOD_SIZE * DEFAULT_OUT_SAMPLING_RATE) / out->config.rate;
    
	size = ((size + 15) / 16) * 16;
    return size * audio_stream_frame_size((struct audio_stream *)stream);
}

static audio_channel_mask_t out_get_channels(const struct audio_stream *stream)
{
	ALOGI("%s(%p)", __FUNCTION__, stream);
    struct aml_stream_out *out = (struct aml_stream_out *)stream;

    if (out->config.channels == 1) {
        return AUDIO_CHANNEL_OUT_MONO;
    } else if (out->config.channels == 6) {
		return AUDIO_CHANNEL_OUT_5POINT1;
	} else if (out->config.channels == 8){
		return AUDIO_CHANNEL_OUT_7POINT1;
	} else {
        return AUDIO_CHANNEL_OUT_STEREO;
    }
}

static audio_format_t out_get_format(const struct audio_stream *stream)
{
    return AUDIO_FORMAT_PCM_16_BIT;
}

static int out_set_format(struct audio_stream *stream, int format)
{
    ALOGI("%s(%p)", __FUNCTION__, stream);

    return 0;
}

/* must be called with hw device and output stream mutexes locked */
static int do_output_standby(struct aml_stream_out *out)
{
    struct aml_audio_device *adev = out->dev;

    ALOGI("%s(%p)", __FUNCTION__, out);

    if (!out->standby){
        pcm_close(out->pcm);
        out->pcm = NULL;

        adev->active_output = 0;
        out->standby = 1;
    }
    return 0;
}

static int out_standby(struct audio_stream *stream)
{
    struct aml_stream_out *out = (struct aml_stream_out *)stream;
    int status;

    LOGFUNC("%s(%p)", __FUNCTION__, stream);

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
	ALOGI("%s\n", __FUNCTION__);
    struct aml_stream_out *out = (struct aml_stream_out *)stream;
    struct aml_audio_device *adev = out->dev;
    struct str_parms *parms;
    char *str;
    char value[32];
    int ret, val = 0;
    

    ALOGI("%s(kvpairs(%s), out_device=%#x)", __FUNCTION__, kvpairs, adev->out_device);
    parms = str_parms_create_str(kvpairs);
	
    ret = str_parms_get_str(parms, AUDIO_PARAMETER_STREAM_ROUTING, value, sizeof(value));
    if (ret >= 0) {
        val = atoi(value);
        pthread_mutex_lock(&adev->lock);
        pthread_mutex_lock(&out->lock);
        if (((adev->out_device & AUDIO_DEVICE_OUT_ALL) != val) && (val != 0)) {
            if (out == adev->active_output) {
                do_output_standby(out);
                
                /* force standby if moving to/from HDMI */
                if (((val & AUDIO_DEVICE_OUT_AUX_DIGITAL) ^
                        (adev->out_device & AUDIO_DEVICE_OUT_AUX_DIGITAL)) ||
                        ((val & AUDIO_DEVICE_OUT_DGTL_DOCK_HEADSET) ^
                        (adev->out_device & AUDIO_DEVICE_OUT_DGTL_DOCK_HEADSET)))
                    do_output_standby(out);
            }
            adev->out_device &= ~AUDIO_DEVICE_OUT_ALL;
            adev->out_device |= val;
        }
        pthread_mutex_unlock(&out->lock);
        pthread_mutex_unlock(&adev->lock);
    }

    str_parms_destroy(parms);
    return ret;

}

static char * out_get_parameters(const struct audio_stream *stream, const char *keys)
{
    ALOGI("%s(%p, %s)", __FUNCTION__, stream, keys);
    return strdup("");
}

static uint32_t out_get_latency(const struct audio_stream_out *stream)
{
	struct aml_stream_out *out = (struct aml_stream_out *)stream;   
	return (PERIOD_SIZE * PLAYBACK_PERIOD_COUNT * 1000) / out->config.rate;
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
		int kernel_frames;
		void *buf;
		char output_buffer_bytes[RESAMPLER_BUFFER_SIZE+128];
		uint ouput_len;
		char *data,  *data_dst;
		volatile char *data_src;
		short *dataprint;
		uint i, total_len;
		
		pthread_mutex_lock(&adev->lock);
		pthread_mutex_lock(&out->lock);
		if (out->standby) {
			ret = start_output_stream(out);
			if (ret != 0) {
				pthread_mutex_unlock(&adev->lock);
				goto exit;
			}
			out->standby = 0;
		}
		pthread_mutex_unlock(&adev->lock);

		/* only use resampler if required */
		if (out->config.rate != DEFAULT_OUT_SAMPLING_RATE) {
	        if (!out->resampler) {
				
	            ret = create_resampler(DEFAULT_OUT_SAMPLING_RATE,
	                    out->config.rate,
	                    2,
	                    RESAMPLER_QUALITY_DEFAULT,
	                    NULL,
	                    &out->resampler);
	            if (ret != 0)
	                goto exit;
	            out->buffer = malloc(RESAMPLER_BUFFER_SIZE); /* todo: allow for reallocing */
	            if (!out->buffer) {
	                ret = -ENOMEM;
	                goto exit;
	            }
	        }
			out->resampler->resample_from_input(out->resampler,
												(int16_t *)buffer,
												&in_frames,
												(int16_t *)out->buffer,
												&out_frames);
			buf = out->buffer;
		} else {
			out_frames = in_frames;
			buf = (void *)buffer;
		}

#if 1

	if(out->config.rate != DEFAULT_OUT_SAMPLING_RATE) {
		total_len = out_frames*frame_size + cached_len;


		ALOGI("total_len(%d) = resampler_out_len(%d) + cached_len111(%d)", total_len, out_frames*frame_size, cached_len);

		data_src = (char *)cache_buffer_bytes;
		data_dst = (char *)output_buffer_bytes;


	
		/*write_back data from cached_buffer*/
		if(cached_len){
			memcpy((void *)data_dst, (void *)data_src, cached_len);
			data_dst += cached_len;
		}
		
		ouput_len = total_len &(~0x3f);
		data = (char*)buf;
		
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
		/*frames not change, frame_size = frame_size * 2, 16bit  -> 32bit */
		
		size_t frame_sbytes = out_frames * frame_size / 2;
		int32_t * s2i = (int32_t *) malloc(frame_sbytes * sizeof(int32_t));
		if(s2i == NULL){
			ALOGI("s2i==NULL\n");
			ret = pcm_write(out->pcm, (void *)buf, out_frames * frame_size);
		} else{
			ALOGI("s2i malloc success\n");
			
			short * p_s2i;
			p_s2i = (short *)buf;

			int32_t * s2i_buf;
			s2i_buf = s2i;
			
			int i;
			for(i = 0; i < frame_sbytes; i++){
				*s2i_buf++ =  (*p_s2i) << 16;
				p_s2i++;
			}
			
			
			ret = pcm_write(out->pcm, (void *)s2i, frame_sbytes * sizeof(int32_t));
			free(s2i);
		}
		
		//ret = pcm_write(out->pcm, (void *)buf, out_frames * frame_size);
		//ret = pcm_mmap_write(out->pcm, (void *)buf, out_frames * frame_size);
	}
#endif

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
    ALOGI("%s(%p, %p)", __FUNCTION__, stream, dsp_frames);
    return -EINVAL;
}

static int out_add_audio_effect(const struct audio_stream *stream, effect_handle_t effect)
{
    LOGFUNC("%s(%p, %p)", __FUNCTION__, stream, effect);
    return 0;
}

static int out_remove_audio_effect(const struct audio_stream *stream, effect_handle_t effect)
{
    return 0;
}
static int out_get_next_write_timestamp(const struct audio_stream_out *stream,
                                        int64_t *timestamp)
{
	ALOGI("%d\n",__FUNCTION__);
    return -EINVAL;
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

    ALOGI("%s(devices=0x%04x,format=%d, ch=0x%04x, SR=%d)", __FUNCTION__, devices,
                        config->format, channel_count, config->sample_rate);

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

    ret = check_output_stream(out);
	ALOGI("check_output_stream_ret=%d\n",ret);
    if(ret < 0)
    {
        ALOGI("fail to open out stream, change channel count frome %d to %d", out->config.channels, channel_count);
        out->config.channels = channel_count;
    }
	ALOGI("out->config.channels =%d\n",out->config.channels);
    if(out->config.channels == 1)
    {
        config->channel_mask = AUDIO_CHANNEL_OUT_MONO;
    }
    else if(out->config.channels == 2)
    {       
        config->channel_mask = AUDIO_CHANNEL_OUT_STEREO;
    }
	else if(out->config.channels == 6)
	{
		ALOGI("AUDIO_CHANNEL_OUT_5POINT1\n");
		config->channel_mask = AUDIO_CHANNEL_OUT_5POINT1;
	}
	else if(out->config.channels == 8)
	{
		ALOGI("AUDIO_CHANNEL_OUT_7POINT1\n");
		config->channel_mask = AUDIO_CHANNEL_OUT_7POINT1;
	}
    else
    {
        ALOGI("Bad value of channel count : %d", out->config.channels);
    }
    out->dev = ladev;
    out->standby = 1;

    config->format = out_get_format(&out->stream.common);
    config->channel_mask = out_get_channels(&out->stream.common);
    config->sample_rate = out_get_sample_rate(&out->stream.common);
    ALOGI("%s(devices=0x%04x,format=%d, ch=0x%04x, SR=%d)", __FUNCTION__, devices,
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
    ALOGI("%s(%p, %s)", __FUNCTION__, dev, kvpairs);
	
    return 0;
}

static char * adev_get_parameters(const struct audio_hw_device *dev,
                                  const char *keys)
{
    ALOGI("%s(%p, %s)", __FUNCTION__, dev, keys);
    return strdup("");
}

static int adev_init_check(const struct audio_hw_device *dev)
{
    LOGFUNC("%s(%p)", __FUNCTION__, dev);
    return 0;
}

static int adev_set_voice_volume(struct audio_hw_device *dev, float volume)
{
    struct aml_audio_device *adev = (struct aml_audio_device *)dev;

    

    LOGFUNC("%s(%p, %f)", __FUNCTION__, dev, volume);
   

    return 0;
}

static int adev_set_master_volume(struct audio_hw_device *dev, float volume)
{
    LOGFUNC("%s(%p, %f)", __FUNCTION__, dev, volume);
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

    return 0;
}

static int adev_get_mic_mute(const struct audio_hw_device *dev, bool *state)
{
    return -ENOSYS;
}

static int adev_set_mic_mute(struct audio_hw_device *dev, bool state)
{
    struct aml_audio_device *adev = (struct aml_audio_device *)dev;

    LOGFUNC("%s(%p, %d)", __FUNCTION__, dev, state);
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
    return -ENOSYS;
}

static void adev_close_input_stream(struct audio_hw_device *dev,
                                   struct audio_stream_in *stream)
{
}

static int adev_dump(const audio_hw_device_t *device, int fd)
{
    ALOGI("%s(%p, %d)", __FUNCTION__, device, fd);
    return 0;
}

static int adev_close(hw_device_t *device)
{
    struct aml_audio_device *adev = (struct aml_audio_device *)device;

    LOGFUNC("%s(%p)", __FUNCTION__, device);

    free(device);
    return 0;
}

static int adev_open(const hw_module_t* module, const char* name,
                     hw_device_t** device)
{
    struct aml_audio_device *adev;
    int ret;
    LOGFUNC("%s(%p, %s, %p)", __FUNCTION__, module, name, device);
    if (strcmp(name, AUDIO_HARDWARE_INTERFACE) != 0){
		ALOGI("%s\n",__FUNCTION__);
        return -EINVAL;
    }

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
    //adev->hw_device.get_master_volume = adev_get_master_volume;
    //adev->hw_device.set_master_mute = adev_set_master_mute;
    //adev->hw_device.get_master_mute = adev_get_master_mute;
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
        .name = "hdmi6 audio HW HAL",
        .author = "amlogic, Corp.",
        .methods = &hal_module_methods,
    },
};
