/*
 * Copyright (C) 2017 Amlogic Corporation.
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

#include <cutils/log.h>
#include <system/audio.h>
#include <hardware/audio.h>
#include <tinyalsa/asoundlib.h>

#include "audio_hw.h"
#include "alsa_manager.h"
#include "audio_hw_utils.h"
#include "alsa_device_parser.h"
#include "dolby_lib_api.h"

#define AML_ZERO_ADD_MIN_SIZE 1024

#define AUDIO_EAC3_FRAME_SIZE 16
#define AUDIO_AC3_FRAME_SIZE 4
#define AUDIO_TV_PCM_FRAME_SIZE 32
#define AUDIO_DEFAULT_PCM_FRAME_SIZE 4

#define MAX_AVSYNC_GAP (10*90000)
#define MAX_AVSYNC_WAIT_TIME (3*1000*1000)

/*
insert bytes of into audio effect buffer to clear intermediate data when exit
*/
static int insert_eff_zero_bytes(struct aml_audio_device *adev, size_t size)
{
    int ret = 0;
    size_t insert_size = size;
    size_t once_write_size = 0;
    size_t bytes_per_frame = audio_bytes_per_sample(AUDIO_FORMAT_PCM_16_BIT)
                             * audio_channel_count_from_out_mask(AUDIO_CHANNEL_OUT_STEREO);
    int16_t *effect_tmp_buf = NULL;

    if ((size % bytes_per_frame) != 0) {
        ALOGE("%s, size= %zu , not bytes_per_frame muliplier\n", __FUNCTION__, size);
        goto exit;
    }

    effect_tmp_buf = (int16_t *) adev->effect_buf;
    while (insert_size > 0) {
        once_write_size = insert_size > adev->effect_buf_size ? adev->effect_buf_size : insert_size;
        memset(effect_tmp_buf, 0, once_write_size);
        /*aduio effect process for speaker*/
        for (int i = 0; i < adev->native_postprocess.num_postprocessors; i++) {
            audio_post_process(adev->native_postprocess.postprocessors[i], effect_tmp_buf, once_write_size / bytes_per_frame);
        }
        insert_size -= once_write_size;
    }

exit:
    return 0;
}

int aml_alsa_output_open(struct audio_stream_out *stream)
{
    ALOGI("\n+%s stream %p", __func__, stream);
    struct aml_stream_out *aml_out = (struct aml_stream_out *)stream;
    struct aml_audio_device *adev = aml_out->dev;
    struct pcm_config *config = &aml_out->config;
    struct pcm_config config_raw;
    unsigned int device = aml_out->device;
    struct dolby_ms12_desc *ms12 = &(adev->ms12);

    if (eDolbyMS12Lib == adev->dolby_lib_type) {
        if (adev->ms12.dolby_ms12_enable) {
            config = &(adev->ms12_config);
            device = ms12->device;
            ALOGI("%s indeed choose ms12 [config and device(%d)]", __func__, ms12->device);
            if (aml_out->device != device) {
                ALOGI("%s stream device(%d) differ with current device(%d)!", __func__, aml_out->device, device);
                aml_out->is_device_differ_with_ms12 = true;
            }
        }
    } else if (eDolbyDcvLib == adev->dolby_lib_type) {
        if (aml_out->dual_output_flag && adev->optical_format != AUDIO_FORMAT_PCM_16_BIT) {
            device = I2S_DEVICE;
        } else if (adev->sink_format != AUDIO_FORMAT_PCM_16_BIT &&
                   aml_out->hal_format != AUDIO_FORMAT_PCM_16_BIT) {
            memset(&config_raw, 0, sizeof(struct pcm_config));
            int period_mul = (adev->sink_format  == AUDIO_FORMAT_E_AC3) ? 4 : 1;
            config_raw.channels = 2;
            config_raw.rate = MM_FULL_POWER_SAMPLING_RATE ;
            config_raw.period_size = DEFAULT_PLAYBACK_PERIOD_SIZE * period_mul;
            config_raw.period_count = PLAYBACK_PERIOD_COUNT;
            config_raw.start_threshold = DEFAULT_PLAYBACK_PERIOD_SIZE * PLAYBACK_PERIOD_COUNT;
            config_raw.format = PCM_FORMAT_S16_LE;
            config = &config_raw;
            device = DIGITAL_DEVICE;
        }
    }
    int card = aml_out->card;
    struct pcm *pcm = adev->pcm_handle[device];
    ALOGI("%s pcm %p", __func__, pcm);

    // close former and open with configs
    // TODO: check pcm configs and if no changes, do nothing
    if (pcm && device != DIGITAL_DEVICE) {
        ALOGI("pcm device already opened,re-use pcm handle %p", pcm);
    } else {
        /*
        there are some audio format when digital output
        from dd->dd+ or dd+ --> dd,we need reopen the device.
        */
        if (pcm) {
            pcm_close(pcm);
            adev->pcm_handle[device] = NULL;
            aml_out->pcm = NULL;
            pcm = NULL;
        }

        // mark: will there wil issue here? conflit with MS12 device?? zz
        if (card == alsa_device_get_card_index()) {
            device = alsa_device_get_pcm_index(device);
        }

        ALOGI("%s, audio open card(%d), device(%d)", __func__, card, device);
        ALOGI("ALSA open configs: channels %d format %d period_count %d period_size %d rate %d",
              config->channels, config->format, config->period_count, config->period_size, config->rate);
        ALOGI("ALSA open configs: threshold start %u stop %u silence %u silence_size %d avail_min %d",
              config->start_threshold, config->stop_threshold, config->silence_threshold, config->silence_size, config->avail_min);
        pcm = pcm_open(card, device, PCM_OUT, config);
        if (!pcm || !pcm_is_ready(pcm)) {
            ALOGE("%s, pcm %p open [ready %d] failed", __func__, pcm, pcm_is_ready(pcm));
            return -ENOENT;
        }
    }
    aml_out->pcm = pcm;
    adev->pcm_handle[device] = pcm;
    adev->pcm_refs[device]++;
    aml_out->dropped_size = 0;
    ALOGI("-%s, audio out(%p) device(%d) refs(%d) is_normal_pcm %d, handle %p\n\n",
          __func__, aml_out, device, adev->pcm_refs[device], aml_out->is_normal_pcm, pcm);

    return 0;
}

void aml_alsa_output_close(struct audio_stream_out *stream)
{
    ALOGI("\n+%s() stream %p\n", __func__, stream);
    struct aml_stream_out *aml_out = (struct aml_stream_out *)stream;
    struct aml_audio_device *adev = aml_out->dev;
    unsigned int device = aml_out->device;
    struct dolby_ms12_desc *ms12 = &(adev->ms12);

    if (eDolbyMS12Lib == adev->dolby_lib_type) {
        if (aml_out->is_device_differ_with_ms12) {
            ALOGI("%s stream out device(%d) truely use device(%d)\n", __func__, aml_out->device, ms12->device);
            device = ms12->device;
            aml_out->is_device_differ_with_ms12 = false;
        }
    }  else if (eDolbyDcvLib == adev->dolby_lib_type) {
        if (aml_out->dual_output_flag && adev->ddp.digital_raw == 1) {
            device = I2S_DEVICE;
        }
    }

    struct pcm *pcm = adev->pcm_handle[device];

    adev->pcm_refs[device]--;
    ALOGI("+%s, audio out(%p) device(%d), refs(%d) is_normal_pcm %d,handle %p",
          __func__, aml_out, device, adev->pcm_refs[device], aml_out->is_normal_pcm, aml_out->pcm);
    if (adev->pcm_refs[device] < 0) {
        adev->pcm_refs[device] = 0;
        ALOGI("%s, device(%d) refs(%d)\n", __func__, device, adev->pcm_refs[device]);
    }
    if (pcm && (adev->pcm_refs[device] == 0)) {
        ALOGI("%s(), pcm_close audio device[%d] pcm handle %p", __func__, device, pcm);
        // insert enough zero byte to clean audio processing buffer when exit
        // after test 8192*2 bytes should be enough, that is DEFAULT_PLAYBACK_PERIOD_SIZE*32
        insert_eff_zero_bytes(adev, DEFAULT_PLAYBACK_PERIOD_SIZE * 32);
        pcm_close(pcm);
        adev->pcm_handle[device] = NULL;
    }
    aml_out->pcm = NULL;
    ALOGI("-%s()\n\n", __func__);
}
static int aml_alsa_add_zero(struct aml_stream_out *stream, int size)
{
    int ret = 0;
    int retry = 10;
    char *buf = NULL;
    int write_size = 0;
    int adjust_bytes = size;
    struct aml_stream_out *aml_out = (struct aml_stream_out *)stream;

    while (retry--) {
        buf = malloc(AML_ZERO_ADD_MIN_SIZE);
        if (buf != NULL) {
            break;
        }
        usleep(10000);
    }
    if (buf == NULL) {
        return ret;
    }
    memset(buf, 0, AML_ZERO_ADD_MIN_SIZE);

    while (adjust_bytes > 0) {
        write_size = adjust_bytes > AML_ZERO_ADD_MIN_SIZE ? AML_ZERO_ADD_MIN_SIZE : adjust_bytes;
        ret = pcm_write(aml_out->pcm, (void*)buf, write_size);
        if (ret < 0) {
            ALOGE("%s alsa write fail when insert", __func__);
            break;
        }
        adjust_bytes -= write_size;
    }
    free(buf);
    return (size - adjust_bytes);
}

size_t aml_alsa_output_write(struct audio_stream_out *stream,
                             void *buffer,
                             size_t bytes)
{
    struct aml_stream_out *aml_out = (struct aml_stream_out *)stream;
    struct aml_audio_device *adev = aml_out->dev;
    int ret = 0;
    struct pcm_config *config = &aml_out->config;
    size_t frame_size = audio_stream_out_frame_size(stream);
    bool need_trigger = false;
    bool is_dtv = (adev->patch_src == SRC_DTV);
    bool is_dtv_live = 1;
    bool has_video = adev->is_has_video;
    unsigned int first_apts = 0;
    unsigned int first_vpts = 0;
    unsigned int cur_apts = 0;
    unsigned int cur_vpts = 0;
    unsigned int cur_pcr = 0;
    int av_diff = 0;
    int need_drop_inject = 0;
    int64_t pretime = 0;
    unsigned char*audio_data = (unsigned char*)buffer;

    // pre-check
    if (!has_video || !is_dtv) {
        goto write;
    }
    if (!adev->first_apts_flag) {
        goto write;
    }

    // video not comming. skip audio
    get_sysfs_uint(TSYNC_FIRSTVPTS, (unsigned int *)&first_vpts);
    if (first_vpts == 0) {
        ALOGI("[audio-startup] video not comming - skip this packet. size:%zu\n", bytes);
        aml_out->dropped_size += bytes;
        //memset(audio_data, 0, bytes);
        return bytes;
    }

    // av both comming. check need add zero or skip
    //get_sysfs_uint(TSYNC_FIRSTAPTS, (unsigned int *)&(first_apts));
    first_apts = adev->first_apts;
    get_sysfs_uint(TSYNC_VPTS, (unsigned int *) & (cur_vpts));
    get_sysfs_uint(TSYNC_PCRSCR, (unsigned int *) & (cur_pcr));
    if (cur_vpts <= first_vpts) {
        cur_vpts = first_vpts;
    }

    switch (adev->sink_format) {
    case AUDIO_FORMAT_E_AC3:
        frame_size = AUDIO_EAC3_FRAME_SIZE;
        break;
    case AUDIO_FORMAT_AC3:
        frame_size = AUDIO_AC3_FRAME_SIZE;
        break;
    default:
        frame_size = (aml_out->is_tv_platform == true) ? AUDIO_TV_PCM_FRAME_SIZE : AUDIO_DEFAULT_PCM_FRAME_SIZE;
        break;
    }

    cur_apts = (unsigned int)((int64_t)first_apts + (int64_t)(((int64_t)aml_out->dropped_size * 90) / (48 * frame_size)));
    av_diff = (int)((int64_t)cur_apts - (int64_t)cur_vpts);
    ALOGI("[audio-startup] av both comming.fa:0x%x fv:0x%x ca:0x%x cv:0x%x cp:0x%x d:%d fs:%zu diff:%d ms\n",
          first_apts, first_vpts, cur_apts, cur_vpts, cur_pcr, aml_out->dropped_size, frame_size, av_diff / 90);

    // Exception
    if (abs(av_diff) > MAX_AVSYNC_GAP) {
        adev->first_apts = cur_apts;
        aml_audio_start_trigger(stream);
        adev->first_apts_flag = false;
        ALOGI("[audio-startup] case-0: ca:0x%x cv:0x%x dr:%d\n",
              cur_apts, cur_vpts, aml_out->dropped_size);
        goto write;
    }

#if 0
    // avsync inside 100ms, start
    if (abs(av_diff) < 100 * 90) {
        adev->first_apts = cur_apts;
        aml_audio_start_trigger(stream);
        adev->first_apts_flag = false;
        ALOGI("[audio-startup] case-1: ca:0x%x cv:0x%x dr:%d\n",
              cur_apts, cur_vpts, aml_out->dropped_size);
        goto write;
    }
#endif
    // drop case
    if (av_diff < 0) {
        need_drop_inject = abs(av_diff) / 90 * 48 * frame_size;
        need_drop_inject &= ~(frame_size - 1);
        if (need_drop_inject >= (int)bytes) {
            aml_out->dropped_size += bytes;
            ALOGI("[audio-startup] av sync drop %d pcm. total dropped:%d need_drop:%d\n",
                  (int)bytes, aml_out->dropped_size, need_drop_inject);
            if (cur_pcr > cur_vpts) {
                // seems can not cache video in time
                adev->first_apts = cur_apts;
                aml_audio_start_trigger(stream);
                adev->first_apts_flag = false;
                ALOGI("[audio-startup] case-2: drop %d pcm. total dropped:%d need_drop:%d\n",
                      (int)bytes, aml_out->dropped_size, need_drop_inject);
            }
            return bytes;
        } else {
            //emset(audio_data, 0, need_drop_inject);
            ret = pcm_write(aml_out->pcm, audio_data + need_drop_inject, bytes - need_drop_inject);
            aml_out->dropped_size += bytes;
            cur_apts = first_apts + (aml_out->dropped_size * 90) / (48 * frame_size);
            adev->first_apts = cur_apts;
            aml_audio_start_trigger(stream);
            adev->first_apts_flag = false;
            ALOGI("[audio-startup] case-3: drop done. ca:0x%x cv:0x%x dropped:%d\n",
                  cur_apts, cur_vpts, aml_out->dropped_size);
            return bytes;
        }
    }

    // wait video sync
    cur_apts = (unsigned int)((int64_t)first_apts + (int64_t)(((int64_t)aml_out->dropped_size * 90) / (48 * frame_size)));
    pretime = aml_gettime();
    while (1) {
        usleep(MIN_WRITE_SLEEP_US);
        get_sysfs_uint(TSYNC_PCRSCR, (unsigned int *) & (cur_vpts));
        if (cur_vpts > cur_apts - 10 * 90) {
            break;
        }
        if (aml_gettime() - pretime >= MAX_AVSYNC_WAIT_TIME) {
            ALOGI("[audio-startup] add zero exceed %d ms quit.\n", MAX_AVSYNC_WAIT_TIME / 1000);
            break;
        }
    }
    adev->first_apts = cur_apts;
    aml_audio_start_trigger(stream);
    adev->first_apts_flag = false;
    ALOGI("[audio-startup] case-4: ca:0x%x cv:0x%x dropped:%d add zero:%d\n",
          cur_apts, cur_vpts, aml_out->dropped_size, need_drop_inject);

write:

    //code here to handle audio start issue to make higher a/v sync precision
    //if (adev->first_apts_flag) {
    //    aml_audio_start_trigger(stream);
    //    adev->first_apts_flag = false;
    //}
#if 0
    FILE *fp1 = fopen("/data/pcm_write.pcm", "a+");
    if (fp1) {
        int flen = fwrite((char *)buffer, 1, bytes, fp1);
        //ALOGD("flen = %d---outlen=%d ", flen, out_frames * frame_size);
        fclose(fp1);
    } else {
        ALOGE("could not open file:/data/pcm_write.pcm");
    }
#endif

    // SWPL-412, when input source is DTV, and UI set "parental_control_av_mute" command to audio hal
    // we need to mute audio output for PCM output here
    if (adev->patch_src == SRC_DTV && adev->parental_control_av_mute) {
        memset(buffer,0x0,bytes);
    }

    ret = pcm_write(aml_out->pcm, buffer, bytes);
    if (ret < 0) {
        ALOGE("%s write failed,pcm handle %p err num %d", __func__, aml_out->pcm, ret);
    }
    return ret;
}

int aml_alsa_output_get_letancy(struct audio_stream_out *stream)
{
    // TODO: add implementation
    (void) stream;
    return 0;
}

void aml_close_continuous_audio_device(struct aml_audio_device *adev)
{
    int pcm_index = 0;
    int spdif_index = 1;
    struct pcm *continuous_pcm_device = adev->pcm_handle[pcm_index];
    struct pcm *continuous_spdif_device = adev->pcm_handle[1];
    ALOGI("\n+%s() choose device %d pcm %p\n", __FUNCTION__, pcm_index, continuous_pcm_device);
    ALOGI("%s maybe also choose device %d pcm %p\n", __FUNCTION__, spdif_index, continuous_spdif_device);
    if (continuous_pcm_device) {
        pcm_close(continuous_pcm_device);
        continuous_pcm_device = NULL;
        adev->pcm_handle[pcm_index] = NULL;
        adev->pcm_refs[pcm_index] = 0;
    }
    if (continuous_spdif_device) {
        pcm_close(continuous_spdif_device);
        continuous_spdif_device = NULL;
        adev->pcm_handle[spdif_index] = NULL;
        adev->pcm_refs[spdif_index] = 0;
    }
    ALOGI("-%s(), when continuous is at end, the pcm/spdif devices(single/dual output) are closed!\n\n", __FUNCTION__);
    return ;
}
