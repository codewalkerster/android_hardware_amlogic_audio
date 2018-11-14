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

#define LOG_TAG "aml_audio_dca_dec"

#include <unistd.h>
#include <math.h>
#include <fcntl.h>
#include <errno.h>
#include <pthread.h>
#include <fcntl.h>
#include <dlfcn.h>
#include <string.h>
#include <sys/mman.h>
#include <sys/ioctl.h>
#include <sys/stat.h>
#include <sys/prctl.h>
#include <sys/time.h>
#include <sound/asound.h>
#include <cutils/log.h>
#include <tinyalsa/asoundlib.h>

#include "aml_dca_dec_api.h"
//#include "aml_ac3_parser.h"


enum {
    EXITING_STATUS = -1001,
    NO_ENOUGH_DATA = -1002,
};
#define MAX_DECODER_FRAME_LENGTH 32768
#define READ_PERIOD_LENGTH 2048 * 4

//#define MAX_DDP_FRAME_LENGTH 2048

static struct pcm_info pcm_out_info;
/*dts decoder lib function*/
int (*dts_decoder_init)(int, int);
int (*dts_decoder_cleanup)();
int (*dts_decoder_process)(char * , int , int *, char *, int *, struct pcm_info *, char *, int *);
void *gDtsDecoderLibHandler = NULL;


static  int unload_dts_decoder_lib()
{
    if (dts_decoder_cleanup != NULL) {
        (*dts_decoder_cleanup)();
    }
    dts_decoder_init = NULL;
    dts_decoder_process = NULL;
    dts_decoder_cleanup = NULL;
    if (gDtsDecoderLibHandler != NULL) {
        dlclose(gDtsDecoderLibHandler);
        gDtsDecoderLibHandler = NULL;
    }
    return 0;
}

static int dca_decoder_init(int digital_raw)
{
    //int digital_raw = 1;
    gDtsDecoderLibHandler = dlopen("/vendor/lib/libHwAudio_dtshd.so", RTLD_NOW);
    if (!gDtsDecoderLibHandler) {
        ALOGE("%s, failed to open (libstagefright_soft_dtshd.so), %s\n", __FUNCTION__, dlerror());
        goto Error;
    } else {
        ALOGV("<%s::%d>--[gDtsDecoderLibHandler]", __FUNCTION__, __LINE__);
    }

    dts_decoder_init = (int (*)(int, int)) dlsym(gDtsDecoderLibHandler, "dca_decoder_init");
    if (dts_decoder_init == NULL) {
        ALOGE("%s,cant find decoder lib,%s\n", __FUNCTION__, dlerror());
        goto Error;
    } else {
        ALOGV("<%s::%d>--[dts_decoder_init:]", __FUNCTION__, __LINE__);
    }

    dts_decoder_process = (int (*)(char * , int , int *, char *, int *, struct pcm_info *, char *, int *))
                          dlsym(gDtsDecoderLibHandler, "dca_decoder_process");
    if (dts_decoder_process == NULL) {
        ALOGE("%s,cant find decoder lib,%s\n", __FUNCTION__, dlerror());
        goto Error;
    } else {
        ALOGV("<%s::%d>--[dts_decoder_process:]", __FUNCTION__, __LINE__);
    }

    dts_decoder_cleanup = (int (*)()) dlsym(gDtsDecoderLibHandler, "dca_decoder_deinit");
    if (dts_decoder_cleanup == NULL) {
        ALOGE("%s,cant find decoder lib,%s\n", __FUNCTION__, dlerror());
        goto Error;
    } else {
        ALOGV("<%s::%d>--[dts_decoder_cleanup:]", __FUNCTION__, __LINE__);
    }

    /*TODO: always decode*/
    (*dts_decoder_init)(1, digital_raw);
    return 0;
Error:
    unload_dts_decoder_lib();
    return -1;
}

static int dca_decode_process(unsigned char*input, int input_size, unsigned char *outbuf,
                              int *out_size, char *spdif_buf, int *raw_size)
{
    int outputFrameSize = 0;
    int used_size = 0;
    int decoded_pcm_size = 0;
    int ret = -1;

    if (dts_decoder_process == NULL) {
        return ret;
    }

    ret = (*dts_decoder_process)((char *) input
                                 , input_size
                                 , &used_size
                                 , (char *) outbuf
                                 , out_size
                                 , (struct pcm_info *) &pcm_out_info
                                 , (char *) spdif_buf
                                 , (int *) raw_size);
    if (ret == 0) {
        ALOGI("decode ok");
    }
    ALOGV("used_size %d,lpcm out_size %d,raw out size %d", used_size, *out_size, *raw_size);

    return used_size;
}


int dca_decoder_init_patch(struct dca_dts_dec *dts_dec)
{
    dts_dec->status = dca_decoder_init(dts_dec->digital_raw);
    if (dts_dec->status < 0) {
        return -1;
    }
    dts_dec->status = 1;
    dts_dec->remain_size = 0;
    dts_dec->outlen_pcm = 0;
    dts_dec->outlen_raw = 0;
    dts_dec->is_dtscd = 0;
    dts_dec->inbuf = NULL;
    dts_dec->outbuf = NULL;
    dts_dec->outbuf_raw = NULL;
    dts_dec->inbuf = (unsigned char*) malloc(MAX_DECODER_FRAME_LENGTH * 4 * 2);
    if (!dts_dec->inbuf) {
        ALOGE("malloc buffer failed\n");
        return -1;
    }
    dts_dec->outbuf = (unsigned char*) malloc(MAX_DECODER_FRAME_LENGTH * 3 + MAX_DECODER_FRAME_LENGTH + 8);
    if (!dts_dec->outbuf) {
        ALOGE("malloc buffer failed\n");
        return -1;
    }
    dts_dec->outbuf_raw = dts_dec->outbuf + MAX_DECODER_FRAME_LENGTH * 3;
    dts_dec->decoder_process = dca_decode_process;
    //dts_dec->get_parameters = Get_Parameters;
    return 1;
}

int dca_decoder_release_patch(struct dca_dts_dec *dts_dec)
{
    ALOGI("+++%s", __func__);
    if (dts_decoder_cleanup != NULL) {
        (*dts_decoder_cleanup)();
    }
    if (dts_dec->status == 1) {
        dts_dec->status = 0;
        dts_dec->remain_size = 0;
        dts_dec->outlen_pcm = 0;
        dts_dec->outlen_raw = 0;
        //dts_dec->nIsEc3 = 0;
        free(dts_dec->inbuf);
        free(dts_dec->outbuf);
        dts_dec->inbuf = NULL;
        dts_dec->outbuf = NULL;
        dts_dec->outbuf_raw = NULL;
        //dts_dec->get_parameters = NULL;
        dts_dec->decoder_process = NULL;
    }
    ALOGI("---%s", __func__);
    return 1;
}

int dca_decoder_process_patch(struct dca_dts_dec *dts_dec, unsigned char*buffer, int bytes)
{

    int mFrame_size = 0;
    unsigned char *read_pointer = NULL;
    memcpy((char *)dts_dec->inbuf + dts_dec->remain_size, (char *)buffer, bytes);
    dts_dec->remain_size += bytes;
    read_pointer = dts_dec->inbuf;
    int decoder_used_bytes = 0;
    void *main_frame_buffer = NULL;
    int main_frame_size = 0;
    bool SyncFlag = false;
    bool  little_end = false;
    if (dts_dec->is_dtscd && dts_dec->remain_size >= MAX_DECODER_FRAME_LENGTH) {
        main_frame_buffer = read_pointer;
        main_frame_size = mFrame_size = dts_dec->remain_size;

    } else if (dts_dec->remain_size >= MAX_DECODER_FRAME_LENGTH) {
        while (!SyncFlag && dts_dec->remain_size > 8) {
            //DTS_SYNCWORD_IEC61937 : 0xF8724E1F
            if (read_pointer[0] == 0x72 && read_pointer[ 1] == 0xf8
                && read_pointer[2] == 0x1f && read_pointer[3] == 0x4e) {
                SyncFlag = true;
            } else if (read_pointer[0] == 0xf8 && read_pointer[1] == 0x72
                       && read_pointer[2] == 0x4e && read_pointer[3] == 0x1f) {
                SyncFlag = true;
                little_end = true;
            }
            if (SyncFlag == 0) {
                read_pointer++;
                dts_dec->remain_size--;
            }
        }
        if (SyncFlag) {
            read_pointer = read_pointer + 6;
            //ALOGI("read_pointer[0]:%d read_pointer[1]:%d",read_pointer[0],read_pointer[1]);
            if (!little_end) {
                mFrame_size = (read_pointer[0] | read_pointer[1] << 8) / 8;
            } else {
                mFrame_size = (read_pointer[1] | read_pointer[0] << 8) / 8;
            }
            //to do know why
            if (mFrame_size == 2013) {
                mFrame_size = 2012;
            }
            read_pointer = read_pointer + 2;
            main_frame_buffer = read_pointer;
            main_frame_size = mFrame_size;
            ALOGV("mFrame_size:%d dts_dec->remain_size:%d little_end:%d", mFrame_size, dts_dec->remain_size, little_end);
            if (dts_dec->remain_size < mFrame_size) {
                memcpy(dts_dec->inbuf, read_pointer - 8, dts_dec->remain_size);
                mFrame_size = 0;
            } else {
                dts_dec->remain_size = dts_dec->remain_size - 8;
            }
        } else {
            mFrame_size = 0;
        }

    }
    if (mFrame_size <= 0) {
        return -1;
    }
    int used_size = dts_dec->decoder_process((unsigned char*)main_frame_buffer,
                    main_frame_size,
                    (unsigned char *)dts_dec->outbuf,
                    &dts_dec->outlen_pcm,
                    (char *)dts_dec->outbuf_raw,
                    &dts_dec->outlen_raw);

    ALOGV("mFrame_size:%d, outlen_pcm:%d, ret = %d\n", main_frame_size, dts_dec->outlen_pcm, used_size);
    if (used_size > 0) {
        dts_dec->remain_size -= used_size;
        memcpy(dts_dec->inbuf, read_pointer + used_size, dts_dec->remain_size);
    }

    return 1;
}

static int Write_buffer(struct aml_audio_parser *parser, unsigned char *buffer, int size)
{
    int writecnt = -1;
    int sleep_time = 0;

    if (parser->decode_ThreadExitFlag == 1) {
        ALOGI("decoder exiting status %s\n", __func__);
        return EXITING_STATUS;
    }
    while (parser->decode_ThreadExitFlag == 0) {
        if (get_buffer_write_space(&parser->aml_ringbuffer) < size) {
            sleep_time++;
            usleep(3000);
        } else {
            writecnt = ring_buffer_write(&parser->aml_ringbuffer,
                                         (unsigned char*) buffer, size, UNCOVER_WRITE);
            break;
        }
        if (sleep_time > 1000) { //wait for max 1s to get audio data
            ALOGW("[%s] time out to write audio buffer data! wait for 1s\n", __func__);
            return (parser->decode_ThreadExitFlag == 1) ? EXITING_STATUS : NO_ENOUGH_DATA;
        }
    }
    return writecnt;
}

#define DTS_DECODER_ENABLE
static void *decode_threadloop(void *data)
{
    struct aml_audio_parser *parser = (struct aml_audio_parser *) data;
    unsigned char *outbuf = NULL;
    unsigned char *inbuf = NULL;
    unsigned char *outbuf_raw = NULL;
    unsigned char *read_pointer = NULL;
    int valid_lib = 1;
    int outlen = 0;
    int outlen_raw = 0;
    int outlen_pcm = 0;
    int remain_size = 0;
    bool get_frame_size_ok = 0;
    bool  little_end = false;
    bool SyncFlag = false;
    int used_size = 0;
    int read_size = READ_PERIOD_LENGTH;
    int ret = 0;
    int mSample_rate = 0;
    int mFrame_size = 0;
    int mChNum = 0;
    unsigned char temp;
    int i, j;
    int digital_raw = 0;

    ALOGI("++ %s, in_sr = %d, out_sr = %d\n", __func__, parser->in_sample_rate, parser->out_sample_rate);
    outbuf = (unsigned char*) malloc(MAX_DECODER_FRAME_LENGTH * 4 + MAX_DECODER_FRAME_LENGTH + 8);
    if (!outbuf) {
        ALOGE("malloc buffer failed\n");
        return NULL;
    }
    outbuf_raw = outbuf + MAX_DECODER_FRAME_LENGTH;
    inbuf = (unsigned char*) malloc(MAX_DECODER_FRAME_LENGTH * 4 * 2);

    if (!inbuf) {
        ALOGE("malloc inbuf failed\n");
        free(outbuf);
        return NULL;
    }
#ifdef DTS_DECODER_ENABLE
    if (dts_decoder_init != NULL) {
        unload_dts_decoder_lib();
    }
    ret = dca_decoder_init(1);
    if (ret) {
        ALOGW("dec init failed, maybe no lisensed dts decoder.\n");
        valid_lib = 0;
    }
    //parser->decode_enabled = 1;
#endif
    if (parser->in_sample_rate != parser->out_sample_rate) {
        parser->aml_resample.input_sr = parser->in_sample_rate;
        parser->aml_resample.output_sr = parser->out_sample_rate;
        parser->aml_resample.channels = 2;
        resampler_init(&parser->aml_resample);
    }

    prctl(PR_SET_NAME, (unsigned long)"audio_dca_dec");
    while (parser->decode_ThreadExitFlag == 0) {
        outlen = 0;
        outlen_raw = 0;
        outlen_pcm = 0;
        SyncFlag = 0;
        mFrame_size = 0;
        if (parser->decode_ThreadExitFlag == 1) {
            ALOGI("%s, exit threadloop! \n", __func__);
            break;
        }

        //here we call decode api to decode audio frame here
        if (remain_size + READ_PERIOD_LENGTH <= (MAX_DECODER_FRAME_LENGTH * 4 * 2)) { //input buffer size
            ret = pcm_read(parser->aml_pcm, inbuf + remain_size, read_size);
            //ret = pcm_read(parser->aml_pcm, inbuf, read_size);

            //pthread_mutex_unlock(parser->decode_dev_op_mutex);
            if (ret < 0) {
                usleep(1000);  //1ms
                continue;
            } else {
#if 0
                FILE *dump_origin = NULL;
                dump_origin = fopen("/data/tmp/pcm_read.raw", "a+");
                if (dump_origin != NULL) {
                    //fwrite(inbuf + remain_size, read_size, 1, dump_origin);
                    fwrite(inbuf + remain_size, read_size, 1, dump_origin);
                    fclose(dump_origin);
                } else {
                    ALOGW("[Error] Can't write to /data/tmp/pcm_read.raw");
                }
#endif
            }
            remain_size += read_size;
        }

#ifdef DTS_DECODER_ENABLE
        if (valid_lib == 0) {
            continue;
        }
#endif
        //find header and get paramters
        read_pointer = inbuf;
        while (parser->decode_ThreadExitFlag == 0 && remain_size > 8) {
            //DTS_SYNCWORD_IEC61937 : 0xF8724E1F
            if (read_pointer[0] == 0x72 && read_pointer[ 1] == 0xf8
                && read_pointer[2] == 0x1f && read_pointer[3] == 0x4e) {
                SyncFlag = true;
                little_end = false;
                mFrame_size = (read_pointer[6] | read_pointer[7] << 8) / 8;
                if (mFrame_size == 2013) {
                    mFrame_size = 2012;
                }
                //ALOGI("mFrame_size:%d dts_dec->remain_size:%d little_end:%d", mFrame_size, remain_size, little_end);
                break;
            }
            read_pointer++;
            remain_size--;
        }

        if (remain_size < (mFrame_size + 8) || SyncFlag == 0) {
            ALOGI("remain %d,frame size %d, read more\n", remain_size, mFrame_size);
            memcpy(inbuf, read_pointer, remain_size);
            continue;
        }

        read_pointer += 8;   //pa pb pc pd
#if 0
        FILE *dump_fp = NULL;
        dump_fp = fopen("/data/tmp/decoder.raw", "a+");
        if (dump_fp != NULL) {
            fwrite(read_pointer, mFrame_size, 1, dump_fp);
            fclose(dump_fp);
        } else {
            ALOGW("[Error] Can't write to /data/tmp/decoder.raw");
        }
#endif

#ifdef DTS_DECODER_ENABLE
        used_size = dca_decode_process(read_pointer, mFrame_size, outbuf,
                                       &outlen_pcm, (char *) outbuf_raw, &outlen_raw);
#else
        used_size = mFrame_size;
#endif
        if (used_size > 0) {
            remain_size -= 8;    //pa pb pc pd
            remain_size -= used_size;
            //ALOGI("%s, %d used size %d, remain_size %d\n", __func__, __LINE__, used_size, remain_size);
            memcpy(inbuf, read_pointer + used_size, remain_size);
        }

#ifdef DTS_DECODER_ENABLE
        //only need pcm data
        if (outlen_pcm > 0) {
            //ALOGI("outlen_pcm: %d,outlen_raw: %d\n", outlen_pcm, outlen_raw);
            // here only downresample, so no need to malloc more buffer
            if (parser->in_sample_rate != parser->out_sample_rate) {
                int out_frame = outlen_pcm >> 2;
                out_frame = resample_process(&parser->aml_resample, out_frame, (short*) outbuf, (short*) outbuf);
                outlen_pcm = out_frame << 2;
            }
            parser->data_ready = 1;
            Write_buffer(parser, outbuf, outlen_pcm);
        }
#endif
    }

    parser->decode_enabled = 0;
    if (inbuf) {
        free(inbuf);
    }
    if (outbuf) {
        free(outbuf);
    }
#ifdef DTS_DECODER_ENABLE
    unload_dts_decoder_lib();
#endif
    ALOGI("-- %s\n", __func__);
    return NULL;
}


static int start_decode_thread(struct aml_audio_parser *parser)
{
    int ret = 0;

    ALOGI("++ %s\n", __func__);
    parser->decode_enabled = 1;
    parser->decode_ThreadExitFlag = 0;
    ret = pthread_create(&parser->decode_ThreadID, NULL, &decode_threadloop, parser);
    if (ret != 0) {
        ALOGE("%s, Create thread fail!\n", __FUNCTION__);
        return -1;
    }
    ALOGI("-- %s\n", __func__);
    return 0;
}

static int stop_decode_thread(struct aml_audio_parser *parser)
{
    ALOGI("++ %s \n", __func__);
    parser->decode_ThreadExitFlag = 1;
    pthread_join(parser->decode_ThreadID, NULL);
    parser->decode_ThreadID = 0;
    ALOGI("-- %s \n", __func__);
    return 0;
}

int dca_decode_init(struct aml_audio_parser *parser)
{
    return start_decode_thread(parser);
}

int dca_decode_release(struct aml_audio_parser *parser)
{
    return stop_decode_thread(parser);
}


