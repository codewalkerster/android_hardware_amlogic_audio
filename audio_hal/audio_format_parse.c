/*
 * hardware/amlogic/audio/TvAudio/audio_format_parse.c
 *
 * Copyright (C) 2017 Amlogic, Inc. All rights reserved.
 *
 * This source code is subject to the terms and conditions defined in the
 * file 'LICENSE' which is part of this source code package.
 *
 * Alternatively, this software may be distributed under the terms of BSD
 * license.
 *
 */

#define LOG_TAG "audio_format_parse"
//#define LOG_NDEBUG 0

#include <pthread.h>
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <errno.h>
#include <pthread.h>
#include <sys/prctl.h>
#include <cutils/log.h>

#include "aml_audio_stream.h"
#include "audio_format_parse.h"
#include "aml_dump_debug.h"
#include "ac3_parser_utils.h"

#include "aml_alsa_mixer.h"
#include "audio_hw_utils.h"

#include "alsa_device_parser.h"

/*Find the position of 61937 sync word in the buffer*/
static int seek_61937_sync_word(char *buffer, int size)
{
    int i = -1;
    if (size < 4) {
        return i;
    }

    //DoDumpData(buffer, size, CC_DUMP_SRC_TYPE_INPUT_PARSE);

    for (i = 0; i < (size - 3); i++) {
        //ALOGV("%s() 61937 sync word %x %x %x %x\n", __FUNCTION__, buffer[i + 0], buffer[i + 1], buffer[i + 2], buffer[i + 3]);
        if (buffer[i + 0] == 0x72 && buffer[i + 1] == 0xf8 && buffer[i + 2] == 0x1f && buffer[i + 3] == 0x4e) {
            return i;
        }
        if (buffer[i + 0] == 0x4e && buffer[i + 1] == 0x1f && buffer[i + 2] == 0xf8 && buffer[i + 3] == 0x72) {
            return i;
        }
    }
    return -1;
}

/*DTSCD format is a special dts format without 61937 header*/
static int seek_dts_cd_sync_word(char *buffer, int size)
{
    int i = -1;
    if (size < 6) {
        return i;
    }

    for (i = 0; i < (size - 5); i++) {
        if ((buffer[i + 0] == 0xFF && buffer[i + 1] == 0x1F
             && buffer[i + 2] == 0x00 && buffer[i + 3] == 0xE8
             && buffer[i + 4] == 0xF1 && buffer[i + 5] == 0x07) ||
            (buffer[i + 0] == 0xE8 && buffer[i + 1] == 0x00
             && buffer[i + 2] == 0x1F && buffer[i + 3] == 0xFF
             && buffer[i + 6] == 0x07 && buffer[i + 7] == 0xF1)) {
            return i;
        }
    }
    return -1;
}

static audio_channel_mask_t get_dolby_channel_mask(const unsigned char *frameBuf
        , int length)
{
    int scan_frame_offset;
    int scan_frame_size;
    int scan_channel_num;
    int scan_numblks;
    int scan_timeslice_61937;
    int scan_framevalid_flag;
    int ret = 0;
    int total_channel_num  = 0;

    if ((frameBuf == NULL) || (length <= 0)) {
        ret = -1;
    } else {
        ret = parse_dolby_frame_header(frameBuf, length, &scan_frame_offset, &scan_frame_size
                                       , &scan_channel_num, &scan_numblks, &scan_timeslice_61937, &scan_framevalid_flag);
        ALOGV("%s A:scan_channel_num %d scan_numblks %d scan_timeslice_61937 %d\n",
              __FUNCTION__, scan_channel_num, scan_numblks, scan_timeslice_61937);
    }

    if (ret) {
        return AUDIO_CHANNEL_OUT_STEREO;
    } else {
        total_channel_num += scan_channel_num;
        if (length - scan_frame_offset - scan_frame_size > 0) {
            ret = parse_dolby_frame_header(frameBuf + scan_frame_offset + scan_frame_size
                                           , length - scan_frame_offset - scan_frame_size, &scan_frame_offset, &scan_frame_size
                                           , &scan_channel_num, &scan_numblks, &scan_timeslice_61937, &scan_framevalid_flag);
            ALOGV("%s B:scan_channel_num %d scan_numblks %d scan_timeslice_61937 %d\n",
                  __FUNCTION__, scan_channel_num, scan_numblks, scan_timeslice_61937);
            if ((ret == 0) && (scan_timeslice_61937 == 3)) {
                /*dolby frame contain that DEPENDENT frame, that means 7.1ch*/
                total_channel_num = 8;
            }
        }
        return audio_channel_out_mask_from_count(total_channel_num);
    }
}

static int hw_audio_format_detection(struct aml_mixer_handle *mixer_handle)
{
    int type = 0;
    type = aml_mixer_ctrl_get_int(mixer_handle,AML_MIXER_ID_SPDIFIN_AUDIO_TYPE);
    if (type >= LPCM && type <= DTS) {
        return type;
    } else {
        return LPCM;
    }

}

int audio_type_parse(void *buffer, size_t bytes, int *package_size, audio_channel_mask_t *cur_ch_mask)
{
    int pos_sync_word = -1, pos_dtscd_sync_word = -1;
    char *temp_buffer = (char*)buffer;
    int AudioType = LPCM;
    uint32_t *tmp_pc;
    uint32_t pc = 0;
    uint32_t tmp = 0;

    pos_sync_word = seek_61937_sync_word((char*)temp_buffer, bytes);
    pos_dtscd_sync_word = seek_dts_cd_sync_word((char*)temp_buffer, bytes);

    DoDumpData(temp_buffer, bytes, CC_DUMP_SRC_TYPE_INPUT_PARSE);

    if (pos_dtscd_sync_word >= 0) {
        AudioType = DTSCD;
        *package_size = DTSHD_PERIOD_SIZE * 2;
        ALOGV("%s() %d data format: %d *package_size %d\n", __FUNCTION__, __LINE__, AudioType, *package_size);
        return AudioType;
    }

    if (pos_sync_word >= 0) {
        tmp_pc = (uint32_t*)(temp_buffer + pos_sync_word + 4);
        pc = *tmp_pc;
        *cur_ch_mask = AUDIO_CHANNEL_OUT_STEREO;
        /*Value of 0-4bit is data type*/
        switch (pc & 0x1f) {
        case IEC61937_NULL:
            AudioType = MUTE;
            // not defined, suggestion is 4096 samples must have one
            *package_size = 4096 * 4;
            break;
        case IEC61937_AC3:
            AudioType = AC3;
            /* *cur_ch_mask = get_dolby_channel_mask((const unsigned char *)(temp_buffer + pos_sync_word + 8),
                    (bytes - pos_sync_word - 8));
            ALOGV("%d channel mask %#x", __LINE__, *cur_ch_mask);*/
            *package_size = AC3_PERIOD_SIZE;
            break;
        case IEC61937_EAC3:
            AudioType = EAC3;
            /* *cur_ch_mask = get_dolby_channel_mask((const unsigned char *)(temp_buffer + pos_sync_word + 8),
                    (bytes - pos_sync_word - 8));
            ALOGV("%d channel mask %#x", __LINE__, *cur_ch_mask);*/
            *package_size = EAC3_PERIOD_SIZE;
            break;
        case IEC61937_DTS1:
            AudioType = DTS;
            *package_size = DTS1_PERIOD_SIZE;
            break;
        case IEC61937_DTS2:
            AudioType = DTS;
            *package_size = DTS2_PERIOD_SIZE;
            break;
        case IEC61937_DTS3:
            AudioType = DTS;
            *package_size = DTS3_PERIOD_SIZE;
            break;
        case IEC61937_DTSHD:
            /*Value of 8-12bit is framesize*/
            tmp = (pc & 0x7ff) >> 8;
            AudioType = DTSHD;
            /*refer to IEC 61937-5 pdf, table 6*/
            *package_size = DTSHD_PERIOD_SIZE << tmp ;
            break;
        case IEC61937_TRUEHD:
            AudioType = TRUEHD;
            *package_size = THD_PERIOD_SIZE;
            break;
        case IEC61937_PAUSE:
            AudioType = PAUSE;
            // Not defined, set it as 1024*4
            *package_size = 1024 * 4;
            break;
        default:
            AudioType = LPCM;
            break;
        }
        ALOGV("%s() data format: %d, *package_size %d, input size %zu\n",
              __FUNCTION__, AudioType, *package_size, bytes);
    }
    return AudioType;
}

static int audio_type_parse_init(audio_type_parse_t *status)
{
    audio_type_parse_t *audio_type_status = status;
    struct aml_mixer_handle *mixer_handle = audio_type_status->mixer_handle;
    struct pcm_config *config_in = &(audio_type_status->config_in);
    struct pcm *in;
    int ret, bytes;

    audio_type_status->card = (unsigned int)alsa_device_get_card_index();
    audio_type_status->device = (unsigned int)alsa_device_update_pcm_index(PORT_I2S, CAPTURE);
    audio_type_status->flags = PCM_IN;

    if (config_in->channels == 0) {
        config_in->channels = 2;
    }
    if (config_in->rate == 0) {
        config_in->rate = 48000;
    }
    if (config_in->period_size == 0) {
        config_in->period_size = PARSER_DEFAULT_PERIOD_SIZE;
    }
    if (config_in->period_count == 0) {
        config_in->period_count = 4;
    }
    config_in->format = PCM_FORMAT_S16_LE;

    bytes = config_in->period_size * config_in->channels * 2;
    audio_type_status->period_bytes = bytes;

    /*malloc max audio type size, save last 3 byte*/
    audio_type_status->parse_buffer = (char*) malloc(sizeof(char) * (bytes + 16));
    if (NULL == audio_type_status->parse_buffer) {
        ALOGE("%s, no memory\n", __FUNCTION__);
        return -1;
    }

    in = pcm_open(audio_type_status->card, audio_type_status->device,
                  PCM_IN, &audio_type_status->config_in);
    if (!pcm_is_ready(in)) {
        ALOGE("open device failed: %s\n", pcm_get_error(in));
        pcm_close(in);
        goto error;
    }

    audio_type_status->in = in;
    enable_HW_resample(mixer_handle, HW_RESAMPLE_ENABLE);

    ALOGD("init parser success: (%d), (%d), (%p)",
          audio_type_status->card, audio_type_status->device, audio_type_status->in);
    return 0;
error:
    free(audio_type_status->parse_buffer);
    return -1;
}

static int audio_type_parse_release(audio_type_parse_t *status)
{
    audio_type_parse_t *audio_type_status = status;

    pcm_close(audio_type_status->in);
    audio_type_status->in = NULL;
    free(audio_type_status->parse_buffer);

    return 0;
}


static int update_audio_type(audio_type_parse_t *status, int update_bytes)
{
    audio_type_parse_t *audio_type_status = status;
    struct aml_mixer_handle *mixer_handle = audio_type_status->mixer_handle;
    struct pcm_config *config_in = &(audio_type_status->config_in);

    if (audio_type_status->audio_type == audio_type_status->cur_audio_type) {
        audio_type_status->read_bytes = 0;
        return 0;
    }
    if (audio_type_status->audio_type != LPCM && audio_type_status->cur_audio_type == LPCM) {
        /* check 2 period size of IEC61937 burst data to find syncword*/
        if (audio_type_status->read_bytes > (audio_type_status->package_size * 2)) {
            audio_type_status->audio_type = audio_type_status->cur_audio_type;
            audio_type_status->read_bytes = 0;
            ALOGI("no IEC61937 header found, PCM data!\n");
            enable_HW_resample(mixer_handle, HW_RESAMPLE_ENABLE);
        }
        audio_type_status->read_bytes += update_bytes;
    } else {
        /* if find 61937 syncword or raw audio type changed,
        immediately update audio type*/
        audio_type_status->audio_type = audio_type_status->cur_audio_type;
        audio_type_status->read_bytes = 0;
        ALOGI("Raw data found: type(%d)\n", audio_type_status->audio_type);
        enable_HW_resample(mixer_handle, HW_RESAMPLE_DISABLE);
    }
    return 0;
}

void* audio_type_parse_threadloop(void *data)
{
    audio_type_parse_t *audio_type_status = (audio_type_parse_t *)data;
    int bytes, ret = -1;

    int txlx_chip = is_txlx_chip();

    ret = audio_type_parse_init(audio_type_status);
    if (ret < 0) {
        ALOGE("fail to init parser\n");
        return ((void *) 0);
    }

    prctl(PR_SET_NAME, (unsigned long)"audio_type_parse");

    bytes = audio_type_status->period_bytes;

    ALOGV("Start thread loop for android audio data parse! data = %p, bytes = %d, in = %p\n",
          data, bytes, audio_type_status->in);

    while (audio_type_status->running_flag && audio_type_status->in != NULL) {
        ret = pcm_read(audio_type_status->in, audio_type_status->parse_buffer + 3, bytes);
        if (ret >= 0) {
#if 0
            if (getprop_bool("media.audiohal.parsedump")) {
                FILE *dump_fp = NULL;
                dump_fp = fopen("/data/audio_hal/audio_parse.dump", "a+");
                if (dump_fp != NULL) {
                    fwrite(audio_type_status->parse_buffer + 3, bytes, 1, dump_fp);
                    fclose(dump_fp);
                } else {
                    ALOGW("[Error] Can't write to /data/audio_hal/audio_parse.dump");
                }
            }
#endif
            if (alsa_device_is_auge()) {
                audio_type_status->cur_audio_type = LPCM;
                continue;
            }

            audio_type_status->cur_audio_type = audio_type_parse(audio_type_status->parse_buffer,
                                                bytes, &(audio_type_status->package_size), &(audio_type_status->audio_ch_mask));
            // ALOGD("cur_audio_type=%d\n", audio_type_status->cur_audio_type);
            //for txl chip,the PAO sw audio format detection is not ready yet.
            //we use the hw audio format detection.
            //TODO
            if (!txlx_chip && audio_type_status->cur_audio_type == LPCM) {
                audio_type_status->cur_audio_type = hw_audio_format_detection(audio_type_status->mixer_handle);
            }

            memcpy(audio_type_status->parse_buffer, audio_type_status->parse_buffer + bytes, 3);
            update_audio_type(audio_type_status, bytes);
        } else {
            usleep(10 * 1000);
            //ALOGE("fail to read bytes = %d\n", bytes);
        }
    }

    audio_type_parse_release(audio_type_status);

    ALOGI("Exit thread loop for audio type parse!\n");
    return ((void *) 0);
}

int creat_pthread_for_audio_type_parse(
                     pthread_t *audio_type_parse_ThreadID,
                     void **status,
                     struct aml_mixer_handle *mixer)
 {
    pthread_attr_t attr;
    struct sched_param param;
    audio_type_parse_t *audio_type_status = NULL;
    int ret;

    if (*status) {
        ALOGE("Aml TV audio format check is exist!");
        return -1;
    }

    audio_type_status = (audio_type_parse_t*) malloc(sizeof(audio_type_parse_t));
    if (NULL == audio_type_status) {
        ALOGE("%s, no memory\n", __FUNCTION__);
        return -1;
    }

    memset(audio_type_status, 0, sizeof(audio_type_parse_t));
    audio_type_status->running_flag = 1;
    audio_type_status->audio_type = LPCM;
    audio_type_status->audio_ch_mask = AUDIO_CHANNEL_OUT_STEREO;
    audio_type_status->mixer_handle = mixer;

    pthread_attr_init(&attr);
    pthread_attr_setschedpolicy(&attr, SCHED_RR);
    param.sched_priority = 50;//sched_get_priority_max(SCHED_RR);
    pthread_attr_setschedparam(&attr, &param);
    ret = pthread_create(audio_type_parse_ThreadID, &attr,
                         &audio_type_parse_threadloop, audio_type_status);
    pthread_attr_destroy(&attr);
    if (ret != 0) {
        ALOGE("%s, Create thread fail!\n", __FUNCTION__);
        free(audio_type_status);
        return -1;
    }

    ALOGI("Creat thread ID: %lu! audio_type_status: %p\n", *audio_type_parse_ThreadID, audio_type_status);
    *status = audio_type_status;
    return 0;
}

void exit_pthread_for_audio_type_parse(
    pthread_t audio_type_parse_ThreadID,
    void **status)
{
    audio_type_parse_t *audio_type_status = (audio_type_parse_t *)(*status);
    audio_type_status->running_flag = 0;
    pthread_join(audio_type_parse_ThreadID, NULL);
    free(audio_type_status);
    *status = NULL;
    ALOGI("Exit parse thread,thread ID: %ld!\n", audio_type_parse_ThreadID);
    return;
}

/*
 *@brief convert the audio type to android audio format
 */
audio_format_t andio_type_convert_to_android_audio_format_t(int codec_type)
{
    switch (codec_type) {
    case AC3:
        return AUDIO_FORMAT_AC3;
    case EAC3:
        return AUDIO_FORMAT_E_AC3;
    case DTS:
    case DTSCD:
        return AUDIO_FORMAT_DTS;
    case DTSHD:
        return AUDIO_FORMAT_DTS_HD;
    case TRUEHD:
        return AUDIO_FORMAT_DOLBY_TRUEHD;
    case LPCM:
#if defined(IS_ATOM_PROJECT)
        return AUDIO_FORMAT_PCM_32_BIT;
#else
        return AUDIO_FORMAT_PCM_16_BIT;
#endif
    default:
        return AUDIO_FORMAT_PCM_16_BIT;
    }
}

/*
 *@brief convert android audio format to the audio type
 */
int android_audio_format_t_convert_to_andio_type(audio_format_t format)
{
    switch (format) {
    case AUDIO_FORMAT_AC3:
        return AC3;
    case AUDIO_FORMAT_E_AC3:
        return EAC3;
    case AUDIO_FORMAT_DTS:
        return  DTS;//DTSCD;
    case AUDIO_FORMAT_DTS_HD:
        return DTSHD;
    case AUDIO_FORMAT_DOLBY_TRUEHD:
        return TRUEHD;
    case AUDIO_FORMAT_PCM:
        return LPCM;
    default:
        return LPCM;
    }
}

int audio_parse_get_audio_type_direct(audio_type_parse_t *status)
{
    if (!status) {
        ALOGE("NULL pointer of audio_type_parse_t\n");
        return -1;
    }
    return status->audio_type;
}

audio_format_t audio_parse_get_audio_type(audio_type_parse_t *status)
{
    if (!status) {
        ALOGE("NULL pointer of audio_type_parse_t\n");
        return AUDIO_FORMAT_INVALID;
    }
    return andio_type_convert_to_android_audio_format_t(status->audio_type);
}

audio_channel_mask_t audio_parse_get_audio_channel_mask(audio_type_parse_t *status)
{
    if (!status) {
        ALOGE("NULL pointer of audio_type_parse_t, return AUDIO_CHANNEL_OUT_STEREO\n");
        return AUDIO_CHANNEL_OUT_STEREO;
    }
    return status->audio_ch_mask;
}
