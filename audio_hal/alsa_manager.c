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

int aml_alsa_output_open(struct audio_stream_out *stream)
{
    struct aml_stream_out *aml_out = (struct aml_stream_out *)stream;
    struct aml_audio_device *adev = aml_out->dev;
    struct pcm_config *config = &aml_out->config;
    unsigned int device = aml_out->device;

#ifdef DOLBY_MS12_ENABLE
    struct dolby_ms12_desc *ms12 = &(adev->ms12);
    if (adev->dolby_ms12_status) {
        ALOGI("%s use ms12 config indeed!", __func__);
        config = &(adev->ms12_config);
    }
#endif
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
    ALOGI("%s, audio pcm refs(%d) is_normal_pcm %d,handle %p", __func__, adev->pcm_refs[device],aml_out->is_normal_pcm,pcm);

    return 0;
}

void aml_alsa_output_close(struct audio_stream_out *stream)
{
    struct aml_stream_out *aml_out = (struct aml_stream_out *)stream;
    struct aml_audio_device *adev = aml_out->dev;
    unsigned int device = aml_out->device;
    struct pcm *pcm = adev->pcm_handle[aml_out->device];

    adev->pcm_refs[device]--;
    ALOGI("+%s, audio out(%p) device(%d), refs(%d) is_normal_pcm %d,handle %p",
          __func__, aml_out, device, adev->pcm_refs[device], aml_out->is_normal_pcm, aml_out->pcm);
    if (adev->pcm_refs[device] < 0) {
        adev->pcm_refs[device] = 0;
        ALOGI("%s, device(%d) refs(%d)\n", __func__, device, adev->pcm_refs[device]);
    }
    if (pcm && (adev->pcm_refs[device] == 0)) {
        ALOGI("%s(), pcm_close audio device[%d] pcm handle %p", __func__, device, pcm);
        pcm_close(pcm);
        adev->pcm_handle[device] = NULL;
    }
    aml_out->pcm = NULL;
    ALOGI("-%s()\n\n", __func__);
}

size_t aml_alsa_output_write(struct audio_stream_out *stream,
                        const void *buffer,
                        size_t bytes)
{
    struct aml_stream_out *aml_out = (struct aml_stream_out *)stream;
    struct aml_audio_device *adev = aml_out->dev;
    int ret = 0;
    //code here to handle audio start issue to make higher a/v sync precision
    if (adev->first_apts_flag) {
        aml_audio_start_trigger(stream);
        adev->first_apts_flag = false;
    }
#if 0
   FILE *fp1 = fopen("/data/pcm_write_alishan.pcm", "a+");
   if (fp1) {
	   int flen = fwrite((char *)buffer, 1, bytes, fp1);
	   //ALOGD("flen = %d---outlen=%d ", flen, out_frames * frame_size);
	   fclose(fp1);
   } else {
	   ALOGE("could not open file:/data/pcm_write_alishan.pcm");
   }
#endif

    ret = pcm_write(aml_out->pcm, buffer, bytes);
    if (ret < 0) {
        ALOGE("%s write failed,pcm handle %p err num %d",__func__,aml_out->pcm,ret);
    }
    return ret;
}

int aml_alsa_output_get_letancy(struct audio_stream_out *stream)
{
    // TODO: add implementation
    (void) stream;
    return 0;
}
