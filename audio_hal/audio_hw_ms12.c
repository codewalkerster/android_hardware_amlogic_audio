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

#ifdef DOLBY_MS12_ENABLE
#define LOG_TAG "audio_hw_primary"
//#define LOG_NDEBUG 0

#include <cutils/log.h>
#include <dolby_ms12.h>
#include <dolby_ms12_config_params.h>
#include <dolby_ms12_status.h>
#include <aml_android_utils.h>

#include "audio_hw_ms12.h"
#include "alsa_config_parameters.h"
#include "aml_ac3_parser.h"

#define DOLBY_DRC_LINE_MODE 0
#define DOLBY_DRC_RF_MODE   1

/*
 *@brief get dolby ms12 prepared
 */
int get_the_dolby_ms12_prepared (
    struct aml_stream_out *aml_out
    , audio_format_t input_format
    , audio_channel_mask_t input_channel_mask
    , int input_sample_rate)
{
    ALOGI ("+%s()", __FUNCTION__);
    struct aml_audio_device *adev = aml_out->dev;
    struct dolby_ms12_desc *ms12 = & (adev->ms12);
    int dolby_ms12_drc_mode = DOLBY_DRC_RF_MODE;
    pthread_mutex_lock (&ms12->lock);
    set_audio_system_format (AUDIO_FORMAT_PCM_16_BIT);
    set_audio_main_format (input_format);
    /*set the associate audio format*/
    if (adev->dual_decoder_support == true) {
        set_audio_associate_format (input_format);
        ALOGI ("%s set_audio_associate_format %#x", __FUNCTION__, input_format);
        dolby_ms12_set_asscociated_audio_mixing (adev->associate_audio_mixing_enable);
        dolby_ms12_set_user_control_value_for_mixing_main_and_associated_audio (adev->mixing_level);
        ALOGI ("%s associate_audio_mixing_enable %d mixing_level set to %d\n",
               __FUNCTION__, adev->associate_audio_mixing_enable, adev->mixing_level);
        int ms12_runtime_update_ret = aml_ms12_update_runtime_params (& (adev->ms12) );
        ALOGI ("aml_ms12_update_runtime_params return %d\n", ms12_runtime_update_ret);
    }
    dolby_ms12_set_drc_mode (dolby_ms12_drc_mode);
    ALOGI ("%s dolby_ms12_set_drc_mode %s", __FUNCTION__, (dolby_ms12_drc_mode == DOLBY_DRC_RF_MODE) ? "RF MODE" : "LINE MODE");
    int ret = 0;
    //init the dolby ms12
    if (aml_out->dual_output_flag) {
        dolby_ms12_set_dual_output_flag (aml_out->dual_output_flag);
        aml_ms12_config (ms12, input_format, input_channel_mask, input_sample_rate, adev->optical_format);
    } else {
        dolby_ms12_set_dual_output_flag (aml_out->dual_output_flag);
        aml_ms12_config (ms12, input_format, input_channel_mask, input_sample_rate, adev->sink_format);
    }

    adev->dolby_ms12_status = true;
    if (ms12->dolby_ms12_enable) {
        ALOGI ("%s dual_output_flag %d format sink %#x optical %#x ms12 output %#x",
               __FUNCTION__, aml_out->dual_output_flag, adev->sink_format, adev->optical_format, ms12->output_format);
        aml_out->device = usecase_device_adapter_with_ms12 (aml_out->device, adev->sink_format);
        memcpy ( (void *) & (adev->ms12_config), (const void *) & (aml_out->config), sizeof (struct pcm_config) );
        get_hardware_config_parameters (
            & (adev->ms12_config)
            , adev->sink_format
            , audio_channel_count_from_out_mask (ms12->output_channelmask)
            , ms12->output_samplerate
            , aml_out->is_tv_platform);

#ifdef REPLACE_OUTPUT_BUFFER_WITH_CALLBACK
        if (aml_out->dual_output_flag) {
            /*dual output, output format contains both AUDIO_FORMAT_PCM_16_BIT and AUDIO_FORMAT_AC3*/
            if (adev->sink_format == AUDIO_FORMAT_PCM_16_BIT) {
                ret = dolby_ms12_register_pcm_callback (pcm_output, (void *) aml_out);
                ALOGI ("%s() dolby_ms12_register_pcm_callback return %d", __FUNCTION__, ret);
            }
            if (adev->optical_format == AUDIO_FORMAT_AC3) {
                ret = dolby_ms12_register_bitstream_callback (bitstream_output, (void *) aml_out);
                ALOGI ("%s() dolby_ms12_register_bitstream_callback return %d", __FUNCTION__, ret);
            }
        } else {
            /*Single output, output format is AUDIO_FORMAT_PCM_16_BIT or AUDIO_FORMAT_AC3 or AUDIO_Format_E_AC3*/
            if (adev->sink_format == AUDIO_FORMAT_PCM_16_BIT) {
                ret = dolby_ms12_register_pcm_callback (pcm_output, (void *) aml_out);
                ALOGI ("%s() dolby_ms12_register_pcm_callback return %d", __FUNCTION__, ret);
            } else {
                ret = dolby_ms12_register_bitstream_callback (bitstream_output, (void *) aml_out);
                ALOGI ("%s() dolby_ms12_register_bitstream_callback return %d", __FUNCTION__, ret);
            }
        }
#endif
    }
    pthread_mutex_unlock (&ms12->lock);
    ALOGI ("-%s()", __FUNCTION__);
    return ret;
}

/*
 *@brief dolby ms12 main process
 *
 * input parameters
 *     stream: audio_stream_out handle
 *     buffer: data buffer address
 *     bytes: data size
 * output parameters
 *     use_size: buffer used size
 */

int dolby_ms12_main_process (
    struct audio_stream_out *stream
    , const void *buffer
    , size_t bytes
    , size_t *use_size)
{
    struct aml_stream_out *aml_out = (struct aml_stream_out *) stream;
    struct aml_audio_device *adev = aml_out->dev;
    struct dolby_ms12_desc *ms12 = & (adev->ms12);
    int ms12_output_size = 0;
    int dolby_ms12_input_bytes = 0;
    void *output_buffer = NULL;
    size_t output_buffer_bytes = 0;

    void *input_buffer = (void *) buffer;
    size_t input_bytes = bytes;
    int dual_decoder_used_bytes = 0;
    void *main_frame_buffer = input_buffer;/*input_buffer as default*/
    int main_frame_size = input_bytes;/*input_bytes as default*/
    void *associate_frame_buffer = NULL;
    int associate_frame_size = 0;


    if (adev->dolby_ms12_status && ms12->dolby_ms12_enable) {
        //ms12 input main
        int dual_input_ret = 0;
        if (adev->dual_decoder_support == true) {
            dual_input_ret = scan_dolby_main_associate_frame (input_buffer
                             , input_bytes
                             , &dual_decoder_used_bytes
                             , &main_frame_buffer
                             , &main_frame_size
                             , &associate_frame_buffer
                             , &associate_frame_size);
            if (dual_input_ret) {
                ALOGE ("%s used size %u dont find the iec61937 format header, rescan next time!\n", __FUNCTION__, *use_size);
                return 0;
            }
        }

        if (adev->dual_decoder_support == true) {
            /*if there is associate frame, send it to dolby ms12.*/
            char tmp_array[4096] = {0};
            if (!associate_frame_buffer || (associate_frame_size == 0) ) {
                associate_frame_buffer = (void *) &tmp_array[0];
                associate_frame_size = sizeof (tmp_array);
            }
            if (associate_frame_size < main_frame_size) {
                ALOGV ("%s() main frame addr %p size %d associate frame addr %p size %d, need a larger ad input size!\n",
                       __FUNCTION__, main_frame_buffer, main_frame_size, associate_frame_buffer, associate_frame_size);
                memcpy (&tmp_array[0], associate_frame_buffer, associate_frame_size);
                associate_frame_size = sizeof (tmp_array);
            }
            dolby_ms12_input_associate (ms12->dolby_ms12_ptr
                                        , (const void *) associate_frame_buffer
                                        , (size_t) associate_frame_size
                                        , ms12->input_config_format
                                        , audio_channel_count_from_out_mask (ms12->config_channel_mask)
                                        , ms12->config_sample_rate
                                       );
        }

        if (main_frame_buffer && (main_frame_size > 0) ) {
            /*input main frame*/
            dolby_ms12_input_bytes =
                dolby_ms12_input_main (
                    ms12->dolby_ms12_ptr
                    , main_frame_buffer
                    , main_frame_size
                    , ms12->input_config_format
                    , audio_channel_count_from_out_mask (ms12->config_channel_mask)
                    , ms12->config_sample_rate);

#ifdef REPLACE_OUTPUT_BUFFER_WITH_CALLBACK

#else
            //ms12 output
            ms12_output_size =
                dolby_ms12_output (
                    ms12->dolby_ms12_ptr
                    , ms12->dolby_ms12_out_data
                    , ms12->dolby_ms12_out_max_size);
            if (ms12_output_size > 0) {
                audio_format_t output_format = get_output_format (stream);
                if (0 == audio_hal_data_processing (stream
                                                    , ms12->dolby_ms12_out_data
                                                    , ms12_output_size
                                                    , &output_buffer
                                                    , &output_buffer_bytes
                                                    , output_format) )
                    hw_write (stream, output_buffer, output_buffer_bytes, output_format);
            }
#endif
            if (dolby_ms12_input_bytes > 0) {
                *use_size = (adev->dual_decoder_support == true) ? (dual_decoder_used_bytes) : dolby_ms12_input_bytes;
            }
        } else {
            if (adev->dual_decoder_support == true)
                *use_size = dual_decoder_used_bytes;
            else {
                *use_size = input_bytes;
            }
        }

        return 0;
    } else
        return -1;
}


/*
 *@brief dolby ms12 system process
 *
 * input parameters
 *     stream: audio_stream_out handle
 *     buffer: data buffer address
 *     bytes: data size
 * output parameters
 *     use_size: buffer used size
 */
int dolby_ms12_system_process (
    struct audio_stream_out *stream
    , const void *buffer
    , size_t bytes
    , size_t *use_size)
{
    struct aml_stream_out *aml_out = (struct aml_stream_out *) stream;
    struct aml_audio_device *adev = aml_out->dev;
    struct dolby_ms12_desc *ms12 = & (adev->ms12);
    audio_channel_mask_t mixer_default_channelmask = AUDIO_CHANNEL_OUT_STEREO;
    int mixer_default_samplerate = 48000;
    int dolby_ms12_input_bytes = 0;
    int ms12_output_size = 0;
    pthread_mutex_lock (&ms12->lock);
    if (adev->dolby_ms12_status && ms12->dolby_ms12_enable) {
        //Dual input, here get the system data
        dolby_ms12_input_bytes =
            dolby_ms12_input_system (
                ms12->dolby_ms12_ptr
                , buffer
                , bytes
                , AUDIO_FORMAT_PCM_16_BIT
                , audio_channel_count_from_out_mask (mixer_default_channelmask)
                , mixer_default_samplerate);
        if (dolby_ms12_input_bytes > 0)
            *use_size = dolby_ms12_input_bytes;
    }
    pthread_mutex_unlock (&ms12->lock);
    return 0;
}

/*
 *@brief get dolby ms12 cleanup
 */
int get_dolby_ms12_cleanup (struct aml_audio_device *adev)
{
    ALOGI ("+%s()", __FUNCTION__);
    struct dolby_ms12_desc *ms12 = & (adev->ms12);
    pthread_mutex_lock (&ms12->lock);
    set_audio_system_format (AUDIO_FORMAT_INVALID);
    set_audio_main_format (AUDIO_FORMAT_INVALID);
    dolby_ms12_config_params_set_system_flag (false);
    aml_ms12_cleanup (ms12);
    ms12->output_format = AUDIO_FORMAT_INVALID;
    ms12->dolby_ms12_enable = false;
    adev->dolby_ms12_status = false;
    pthread_mutex_unlock (&ms12->lock);
    ALOGI ("-%s()", __FUNCTION__);
    return 0;
}

#ifdef REPLACE_OUTPUT_BUFFER_WITH_CALLBACK
int pcm_output (void *buffer, void *priv_data, size_t size)
{
    //ALOGI("+%s() size %d", __FUNCTION__, size);
    struct aml_stream_out *aml_out = (struct aml_stream_out *) priv_data;
    struct aml_audio_device *adev = aml_out->dev;
    void *output_buffer = NULL;
    size_t output_buffer_bytes = 0;
    audio_format_t output_format = AUDIO_FORMAT_PCM_16_BIT;
    int ret = 0;

    if (audio_hal_data_processing ( (struct audio_stream_out *) aml_out, buffer, size, &output_buffer, &output_buffer_bytes, output_format) == 0)
        ret = hw_write ( (struct audio_stream_out *) aml_out, output_buffer, output_buffer_bytes, output_format);
    //ALOGI("-%s() ret %d", __FUNCTION__, ret);
    return ret;
}

int bitstream_output (void *buffer, void *priv_data, size_t size)
{
    //ALOGI("+%s() size %d", __FUNCTION__, size);
    struct aml_stream_out *aml_out = (struct aml_stream_out *) priv_data;
    struct aml_audio_device *adev = aml_out->dev;
    void *output_buffer = NULL;
    size_t output_buffer_bytes = 0;
    audio_format_t output_format = AUDIO_FORMAT_AC3;
    int ret = 0;

    if (aml_out->dual_output_flag) {
        output_format = adev->optical_format;
#if 0
        int flen = 0;
        if (aml_getprop_bool ("media.audiohal.outdump") ) {
            FILE *fp1 = fopen ("/data/audio_out/optical.ac3", "a+");
            if (fp1) {
                flen = fwrite ( (char *) buffer, 1, size, fp1);
                ALOGV ("%s iec61937_buffer %p write_size %d\n", __FUNCTION__, buffer, size);
                fclose (fp1);

            }
        } else
            flen = size;
        ret = flen;
#else
        struct audio_stream_out *stream_out = (struct audio_stream_out *) aml_out;
        ret = aml_audio_spdif_output (stream_out, buffer, size);
#endif
    } else {
        output_format = adev->sink_format;
        if (audio_hal_data_processing ( (struct audio_stream_out *) aml_out, buffer, size, &output_buffer, &output_buffer_bytes, output_format) == 0)
            ret = hw_write ( (struct audio_stream_out *) aml_out, output_buffer, output_buffer_bytes, output_format);
    }
    //ALOGI("-%s() ret %d", __FUNCTION__, ret);
    return ret;
}
#endif
#endif
