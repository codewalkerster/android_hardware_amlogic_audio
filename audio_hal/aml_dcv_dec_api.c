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

#define LOG_TAG "aml_audio_dcv_dec"

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

#include "audio_hw_utils.h"
#include "aml_dcv_dec_api.h"
#include "aml_ac3_parser.h"

enum {
    EXITING_STATUS = -1001,
    NO_ENOUGH_DATA = -1002,
};

#define     DDPshort            short
#define     DDPerr              short
#define     DDPushort           unsigned short
#define     BYTESPERWRD         2
#define     BITSPERWRD          (BYTESPERWRD*8)
#define     SYNCWRD             ((DDPshort)0x0b77)
#define     MAXFSCOD            3
#define     MAXDDDATARATE       38
#define     BS_STD              8
#define     ISDD(bsid)          ((bsid) <= BS_STD)
#define     MAXCHANCFGS         8
#define     BS_AXE              16
#define     ISDDP(bsid)         ((bsid) <= BS_AXE && (bsid) > 10)
#define     BS_BITOFFSET        40
#define     PTR_HEAD_SIZE       7//20


typedef struct {
    DDPshort       *buf;
    DDPshort        bitptr;
    DDPshort        data;
} DDP_BSTRM;

const DDPshort chanary[MAXCHANCFGS] = { 2, 1, 2, 3, 3, 4, 4, 5 };
enum {
    MODE11 = 0,
    MODE_RSVD = 0,
    MODE10,
    MODE20,
    MODE30,
    MODE21,
    MODE31,
    MODE22,
    MODE32
};
const DDPushort msktab[] = { 0x0000, 0x8000, 0xc000, 0xe000, 0xf000, 0xf800,
                             0xfc00, 0xfe00, 0xff00, 0xff80, 0xffc0, 0xffe0, 0xfff0, 0xfff8, 0xfffc,
                             0xfffe, 0xffff
                           };
const DDPshort frmsizetab[MAXFSCOD][MAXDDDATARATE] = {
    /* 48kHz */
    {
        64, 64, 80, 80, 96, 96, 112, 112,
        128, 128, 160, 160, 192, 192, 224, 224,
        256, 256, 320, 320, 384, 384, 448, 448,
        512, 512, 640, 640, 768, 768, 896, 896,
        1024, 1024, 1152, 1152, 1280, 1280
    },
    /* 44.1kHz */
    {
        69, 70, 87, 88, 104, 105, 121, 122,
        139, 140, 174, 175, 208, 209, 243, 244,
        278, 279, 348, 349, 417, 418, 487, 488,
        557, 558, 696, 697, 835, 836, 975, 976,
        1114, 1115, 1253, 1254, 1393, 1394
    },
    /* 32kHz */
    {
        96, 96, 120, 120, 144, 144, 168, 168,
        192, 192, 240, 240, 288, 288, 336, 336,
        384, 384, 480, 480, 576, 576, 672, 672,
        768, 768, 960, 960, 1152, 1152, 1344, 1344,
        1536, 1536, 1728, 1728, 1920, 1920
    }
};

static int (*ddp_decoder_init)(int, int);
static int (*ddp_decoder_cleanup)();
static int (*ddp_decoder_process)(char *, int, int *, int, char *, int *, struct pcm_info *, char *, int *);
static void *gDDPDecoderLibHandler = NULL;

static DDPerr ddbs_init(DDPshort * buf, DDPshort bitptr, DDP_BSTRM *p_bstrm)
{
    p_bstrm->buf = buf;
    p_bstrm->bitptr = bitptr;
    p_bstrm->data = *buf;
    return 0;
}

static DDPerr ddbs_unprj(DDP_BSTRM *p_bstrm, DDPshort *p_data,  DDPshort numbits)
{
    DDPushort data;
    *p_data = (DDPshort)((p_bstrm->data << p_bstrm->bitptr) & msktab[numbits]);
    p_bstrm->bitptr += numbits;
    if (p_bstrm->bitptr >= BITSPERWRD) {
        p_bstrm->buf++;
        p_bstrm->data = *p_bstrm->buf;
        p_bstrm->bitptr -= BITSPERWRD;
        data = (DDPushort) p_bstrm->data;
        *p_data |= ((data >> (numbits - p_bstrm->bitptr)) & msktab[numbits]);
    }
    *p_data = (DDPshort)((DDPushort)(*p_data) >> (BITSPERWRD - numbits));
    return 0;
}

static int Get_DD_Parameters(void *buf, int *sample_rate, int *frame_size, int *ChNum)
{
    int numch = 0;
    DDP_BSTRM bstrm = {NULL, 0, 0};
    DDP_BSTRM *p_bstrm = &bstrm;
    short tmp = 0, acmod, lfeon, fscod, frmsizecod;
    ddbs_init((short*) buf, 0, p_bstrm);

    ddbs_unprj(p_bstrm, &tmp, 16);
    if (tmp != SYNCWRD) {
        ALOGI("Invalid synchronization word");
        return 0;
    }
    ddbs_unprj(p_bstrm, &tmp, 16);
    ddbs_unprj(p_bstrm, &fscod, 2);
    if (fscod == MAXFSCOD) {
        ALOGI("Invalid sampling rate code");
        return 0;
    }

    if (fscod == 0) {
        *sample_rate = 48000;
    } else if (fscod == 1) {
        *sample_rate = 44100;
    } else if (fscod == 2) {
        *sample_rate = 32000;
    }

    ddbs_unprj(p_bstrm, &frmsizecod, 6);
    if (frmsizecod >= MAXDDDATARATE) {
        ALOGI("Invalid frame size code");
        return 0;
    }

    *frame_size = 2 * frmsizetab[fscod][frmsizecod];

    ddbs_unprj(p_bstrm, &tmp, 5);
    if (!ISDD(tmp)) {
        ALOGI("Unsupported bitstream id");
        return 0;
    }

    ddbs_unprj(p_bstrm, &tmp, 3);
    ddbs_unprj(p_bstrm, &acmod, 3);

    if ((acmod != MODE10) && (acmod & 0x1)) {
        ddbs_unprj(p_bstrm, &tmp, 2);
    }
    if (acmod & 0x4) {
        ddbs_unprj(p_bstrm, &tmp, 2);
    }

    if (acmod == MODE20) {
        ddbs_unprj(p_bstrm, &tmp, 2);
    }
    ddbs_unprj(p_bstrm, &lfeon, 1);


    numch = chanary[acmod];
    if (0) {
        if (numch >= 3) {
            numch = 8;
        } else {
            numch = 2;
        }
    } else {
        numch = 2;
    }
    *ChNum = numch;
    //ALOGI("DEBUG:numch=%d sample_rate=%d %p [%s %d]",ChNum,sample_rate,this,__FUNCTION__,__LINE__);
    return numch;
}

static int Get_DDP_Parameters(void *buf, int *sample_rate, int *frame_size, int *ChNum)
{
    int numch = 0;
    DDP_BSTRM bstrm = {NULL, 0, 0};
    DDP_BSTRM *p_bstrm = &bstrm;
    short tmp = 0, acmod, lfeon, strmtyp;
    ddbs_init((short*) buf, 0, p_bstrm);
    ddbs_unprj(p_bstrm, &tmp, 16);
    if (tmp != SYNCWRD) {
        ALOGI("Invalid synchronization word");
        return -1;
    }

    ddbs_unprj(p_bstrm, &strmtyp, 2);
    ddbs_unprj(p_bstrm, &tmp, 3);
    ddbs_unprj(p_bstrm, &tmp, 11);

    *frame_size = 2 * (tmp + 1);
    if (strmtyp != 0 && strmtyp != 2) {
        return -1;
    }
    ddbs_unprj(p_bstrm, &tmp, 2);

    if (tmp == 0x3) {
        ALOGI("Half sample rate unsupported");
        return -1;
    } else {
        if (tmp == 0) {
            *sample_rate = 48000;
        } else if (tmp == 1) {
            *sample_rate = 44100;
        } else if (tmp == 2) {
            *sample_rate = 32000;
        }

        ddbs_unprj(p_bstrm, &tmp, 2);
    }
    ddbs_unprj(p_bstrm, &acmod, 3);
    ddbs_unprj(p_bstrm, &lfeon, 1);
    numch = chanary[acmod];
    numch = 2;
    *ChNum = numch;
    //ALOGI("DEBUG[%s %d]:numch=%d,sr=%d,frs=%d",__FUNCTION__,__LINE__,*ChNum,*sample_rate,*frame_size);
    return 0;
}

static DDPerr ddbs_skip(DDP_BSTRM   *p_bstrm, DDPshort    numbits)
{
    p_bstrm->bitptr += numbits;
    while (p_bstrm->bitptr >= BITSPERWRD) {
        p_bstrm->buf++;
        p_bstrm->data = *p_bstrm->buf;
        p_bstrm->bitptr -= BITSPERWRD;
    }
    return 0;
}

static DDPerr ddbs_getbsid(DDP_BSTRM *p_inbstrm,    DDPshort *p_bsid)
{
    DDP_BSTRM    bstrm;

    ddbs_init(p_inbstrm->buf, p_inbstrm->bitptr, &bstrm);
    ddbs_skip(&bstrm, BS_BITOFFSET);
    ddbs_unprj(&bstrm, p_bsid, 5);
    if (!ISDDP(*p_bsid) && !ISDD(*p_bsid)) {
        ALOGI("Unsupported bitstream id");
    }

    return 0;
}

static int Get_Parameters(void *buf, int *sample_rate, int *frame_size, int *ChNum, int *is_eac3)
{
    DDP_BSTRM bstrm = {NULL, 0, 0};
    DDP_BSTRM *p_bstrm = &bstrm;
    DDPshort    bsid;
    int chnum = 0;
    uint8_t ptr8[PTR_HEAD_SIZE];

    memcpy(ptr8, buf, PTR_HEAD_SIZE);

    //ALOGI("LZG->ptr_head:0x%x 0x%x 0x%x 0x%x 0x%x 0x%x \n",
    //     ptr8[0],ptr8[1],ptr8[2], ptr8[3],ptr8[4],ptr8[5] );
    if ((ptr8[0] == 0x0b) && (ptr8[1] == 0x77)) {
        int i;
        uint8_t tmp;
        for (i = 0; i < PTR_HEAD_SIZE; i += 2) {
            tmp = ptr8[i];
            ptr8[i] = ptr8[i + 1];
            ptr8[i + 1] = tmp;
        }
    }

    ddbs_init((short*) ptr8, 0, p_bstrm);
    int ret = ddbs_getbsid(p_bstrm, &bsid);
    if (ret < 0) {
        return -1;
    }
    if (ISDDP(bsid)) {
        Get_DDP_Parameters(ptr8, sample_rate, frame_size, ChNum);
        *is_eac3 = 1;
    } else if (ISDD(bsid)) {
        Get_DD_Parameters(ptr8, sample_rate, frame_size, ChNum);
        *is_eac3 = 0;
    }
    return 0;
}

static  int unload_ddp_decoder_lib()
{
    if (ddp_decoder_cleanup != NULL) {
        (*ddp_decoder_cleanup)();
    }
    ddp_decoder_init = NULL;
    ddp_decoder_process = NULL;
    ddp_decoder_cleanup = NULL;
    if (gDDPDecoderLibHandler != NULL) {
        dlclose(gDDPDecoderLibHandler);
        gDDPDecoderLibHandler = NULL;
    }
    return 0;
}

static int dcv_decoder_init(int digital_raw)
{
    //int digital_raw = 1;
    gDDPDecoderLibHandler = dlopen("/vendor/lib/libHwAudio_dcvdec.so", RTLD_NOW);
    if (!gDDPDecoderLibHandler) {
        ALOGE("%s, failed to open (libstagefright_soft_dcvdec.so), %s\n", __FUNCTION__, dlerror());
        goto Error;
    } else {
        ALOGV("<%s::%d>--[gDDPDecoderLibHandler]", __FUNCTION__, __LINE__);
    }

    ddp_decoder_init = (int (*)(int, int)) dlsym(gDDPDecoderLibHandler, "ddp_decoder_init");
    if (ddp_decoder_init == NULL) {
        ALOGE("%s,cant find decoder lib,%s\n", __FUNCTION__, dlerror());
        goto Error;
    } else {
        ALOGV("<%s::%d>--[ddp_decoder_init:]", __FUNCTION__, __LINE__);
    }

    ddp_decoder_process = (int (*)(char * , int , int *, int , char *, int *, struct pcm_info *, char *, int *))
                          dlsym(gDDPDecoderLibHandler, "ddp_decoder_process");
    if (ddp_decoder_process == NULL) {
        ALOGE("%s,cant find decoder lib,%s\n", __FUNCTION__, dlerror());
        goto Error;
    } else {
        ALOGV("<%s::%d>--[ddp_decoder_process:]", __FUNCTION__, __LINE__);
    }

    ddp_decoder_cleanup = (int (*)()) dlsym(gDDPDecoderLibHandler, "ddp_decoder_cleanup");
    if (ddp_decoder_cleanup == NULL) {
        ALOGE("%s,cant find decoder lib,%s\n", __FUNCTION__, dlerror());
        goto Error;
    } else {
        ALOGV("<%s::%d>--[ddp_decoder_cleanup:]", __FUNCTION__, __LINE__);
    }

    /*TODO: always decode*/
    (*ddp_decoder_init)(1, digital_raw);
    return 0;
Error:
    unload_ddp_decoder_lib();
    return -1;
}

static int dcv_decode_process(unsigned char*input, int input_size, unsigned char *outbuf,
                              int *out_size, char *spdif_buf, int *raw_size, int nIsEc3,
                              struct pcm_info *pcm_out_info)
{
    int outputFrameSize = 0;
    int used_size = 0;
    int decoded_pcm_size = 0;
    int ret = -1;

    if (ddp_decoder_process == NULL) {
        return ret;
    }

    ret = (*ddp_decoder_process)((char *) input
                                 , input_size
                                 , &used_size
                                 , nIsEc3
                                 , (char *) outbuf
                                 , out_size
                                 , pcm_out_info
                                 , (char *) spdif_buf
                                 , (int *) raw_size);
    //ALOGI("used_size %d,lpcm out_size %d,raw out size %d",used_size,*out_size,*raw_size);
    return used_size;
}

int Write_buffer(struct aml_audio_parser *parser, unsigned char *buffer, int size)
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

#define MAX_DECODER_FRAME_LENGTH 6144
#define READ_PERIOD_LENGTH 2048
#define MAX_DDP_FRAME_LENGTH 2560
#define MAX_DDP_BUFFER_SIZE (MAX_DECODER_FRAME_LENGTH * 4 + MAX_DECODER_FRAME_LENGTH + 8)

void *decode_threadloop(void *data)
{
    struct aml_audio_parser *parser = (struct aml_audio_parser *) data;
    unsigned char *outbuf = NULL;
    unsigned char *inbuf = NULL;
    unsigned char *outbuf_raw = NULL;
    unsigned char *read_pointer = NULL;
    int valid_lib = 1;
    int nIsEc3  = 0;
    int outlen = 0;
    int outlen_raw = 0;
    int outlen_pcm = 0;
    int remain_size = 0;
    int used_size = 0;
    int read_size = READ_PERIOD_LENGTH;
    int ret = 0;
    int mSample_rate = 0;
    int mFrame_size = 0;
    int is_eac3 = 0;
    int mChNum = 0;
    int in_sync = 0;
    unsigned char temp;
    int i;
    int digital_raw = 0;

    ALOGI ("++ %s, in_sr = %d, out_sr = %d\n", __func__, parser->in_sample_rate, parser->out_sample_rate);
    outbuf = (unsigned char*) malloc (MAX_DDP_BUFFER_SIZE);

    if (!outbuf) {
        ALOGE("malloc buffer failed\n");
        return NULL;
    }
    outbuf_raw = outbuf + MAX_DECODER_FRAME_LENGTH;

    inbuf = (unsigned char *) malloc(MAX_DDP_FRAME_LENGTH + read_size);
    if (!inbuf) {
        ALOGE("malloc inbuf failed\n");
        free(outbuf);
        return NULL;
    }

    ret = dcv_decoder_init(digital_raw);
    if (ret) {
        ALOGW("dec init failed, maybe no lisensed ddp decoder.\n");
        valid_lib = 0;
    }
    //parser->decode_enabled = 1;

    if (parser->in_sample_rate != parser->out_sample_rate) {
        parser->aml_resample.input_sr = parser->in_sample_rate;
        parser->aml_resample.output_sr = parser->out_sample_rate;
        parser->aml_resample.channels = 2;
        resampler_init(&parser->aml_resample);
    }

    prctl(PR_SET_NAME, (unsigned long)"audio_dcv_dec");
    while (parser->decode_ThreadExitFlag == 0) {
        outlen = 0;
        outlen_raw = 0;
        outlen_pcm = 0;
        in_sync = 0;

        if (parser->decode_ThreadExitFlag == 1) {
            ALOGI("%s, exit threadloop! \n", __func__);
            break;
        }

        if (parser->aformat == AUDIO_FORMAT_E_AC3) {
            nIsEc3 = 1;
        } else {
            nIsEc3 = 0;
        }

        //here we call decode api to decode audio frame here
        if (remain_size < MAX_DDP_FRAME_LENGTH) {
            //pthread_mutex_lock(parser->decode_dev_op_mutex);
            ret = pcm_read(parser->aml_pcm, inbuf + remain_size, read_size);
            //pthread_mutex_unlock(parser->decode_dev_op_mutex);
            if (ret < 0) {
                usleep(1000);  //1ms
                continue;
            }
            remain_size += read_size;
        }

        if (valid_lib == 0) {
            continue;
        }

        //find header and get paramters
        read_pointer = inbuf;
        while (parser->decode_ThreadExitFlag == 0 && remain_size > 16) {
            if ((read_pointer[0] == 0x0b && read_pointer[1] == 0x77) || \
                (read_pointer[0] == 0x77 && read_pointer[1] == 0x0b)) {
                Get_Parameters(read_pointer, &mSample_rate, &mFrame_size, &mChNum, &is_eac3);
                if ((mFrame_size == 0) || (mFrame_size < PTR_HEAD_SIZE) || \
                    (mChNum == 0) || (mSample_rate == 0)) {
                } else {
                    in_sync = 1;
                    break;
                }
            }
            remain_size--;
            read_pointer++;
        }
        //ALOGI("remain %d, frame size %d\n", remain_size, mFrame_size);

        if (remain_size < mFrame_size || in_sync == 0) {
            //if (in_sync == 1)
            //    ALOGI("remain %d,frame size %d, read more\n",remain_size,mFrame_size);
            memcpy(inbuf, read_pointer, remain_size);
            continue;
        }

        //ALOGI("frame size %d,remain %d\n",mFrame_size,remain_size);
        //do the endian conversion
        if (read_pointer[0] == 0x77 && read_pointer[1] == 0x0b) {
            for (i = 0; i < mFrame_size / 2; i++) {
                temp = read_pointer[2 * i + 1];
                read_pointer[2 * i + 1] = read_pointer[2 * i];
                read_pointer[2 * i] = temp;
            }
        }

        used_size = dcv_decode_process(read_pointer, mFrame_size, outbuf,
                                       &outlen_pcm, (char *) outbuf_raw, &outlen_raw, nIsEc3, &parser->pcm_out_info);
        if (used_size > 0) {
            remain_size -= used_size;
            memcpy(inbuf, read_pointer + used_size, remain_size);
        }

        //ALOGI("outlen_pcm: %d,outlen_raw: %d\n", outlen_pcm, outlen_raw);

        //only need pcm data
        if (outlen_pcm > 0) {
            // here only downresample, so no need to malloc more buffer
            if (parser->in_sample_rate != parser->out_sample_rate) {
                int out_frame = outlen_pcm >> 2;
                out_frame = resample_process (&parser->aml_resample, out_frame, (int16_t*) outbuf, (int16_t*) outbuf);
                outlen_pcm = out_frame << 2;
            }
            parser->data_ready = 1;
            Write_buffer(parser, outbuf, outlen_pcm);
        }
    }

    parser->decode_enabled = 0;
    if (inbuf) {
        free(inbuf);
    }
    if (outbuf) {
        free(outbuf);
    }

    unload_ddp_decoder_lib();
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

int dcv_decode_init(struct aml_audio_parser *parser)
{
    return start_decode_thread(parser);
}

int dcv_decode_release(struct aml_audio_parser *parser)
{
    return stop_decode_thread(parser);
}

int dcv_decoder_init_patch(struct dolby_ddp_dec *ddp_dec)
{
    ddp_dec->status = dcv_decoder_init(ddp_dec->digital_raw);
    if (ddp_dec->status < 0) {
        return -1;
    }
    pthread_mutex_init(&ddp_dec->lock, NULL);
    pthread_mutex_lock(&ddp_dec->lock);
    ddp_dec->status = 1;
    ddp_dec->remain_size = 0;
    ddp_dec->outlen_pcm = 0;
    ddp_dec->outlen_raw = 0;
    ddp_dec->nIsEc3 = 0;
    ddp_dec->inbuf = NULL;
    ddp_dec->outbuf = NULL;
    ddp_dec->outbuf_raw = NULL;

    memset(&ddp_dec->pcm_out_info, 0, sizeof(struct pcm_info));
    memset(&ddp_dec->aml_resample, 0, sizeof(struct resample_para));
    ddp_dec->inbuf = (unsigned char*) malloc(MAX_DECODER_FRAME_LENGTH * 4 * 4);

    if (!ddp_dec->inbuf) {
        ALOGE("malloc buffer failed\n");
        pthread_mutex_unlock(&ddp_dec->lock);
        return -1;
    }
    ddp_dec->outbuf = (unsigned char*) malloc(MAX_DDP_BUFFER_SIZE);
    if (!ddp_dec->outbuf) {
        ALOGE("malloc buffer failed\n");
        pthread_mutex_unlock(&ddp_dec->lock);
        return -1;
    }
    ring_buffer_init(&ddp_dec->output_ring_buf, 6 * MAX_DECODER_FRAME_LENGTH);
    ddp_dec->outbuf_raw = ddp_dec->outbuf + MAX_DECODER_FRAME_LENGTH;
    ddp_dec->decoder_process = dcv_decode_process;
    ddp_dec->get_parameters = Get_Parameters;

    pthread_mutex_unlock(&ddp_dec->lock);
    return 0;
}

int dcv_decoder_release_patch(struct dolby_ddp_dec *ddp_dec)
{
    pthread_mutex_lock(&ddp_dec->lock);
    (*ddp_decoder_cleanup)();
    if (ddp_dec->status == 1) {
        ddp_dec->status = 0;
        ddp_dec->remain_size = 0;
        ddp_dec->outlen_pcm = 0;
        ddp_dec->outlen_raw = 0;
        ddp_dec->nIsEc3 = 0;
        free(ddp_dec->inbuf);
        free(ddp_dec->outbuf);
        ddp_dec->inbuf = NULL;
        ddp_dec->outbuf = NULL;
        ddp_dec->outbuf_raw = NULL;
        ddp_dec->get_parameters = NULL;
        ddp_dec->decoder_process = NULL;
        memset(&ddp_dec->pcm_out_info, 0, sizeof(struct pcm_info));
        memset(&ddp_dec->aml_resample, 0, sizeof(struct resample_para));
        ring_buffer_release(&ddp_dec->output_ring_buf);
        if (!ddp_dec->resample_outbuf) {
            free(ddp_dec->resample_outbuf);
            ddp_dec->resample_outbuf = NULL;
        }
    }
    pthread_mutex_unlock(&ddp_dec->lock);
    return 0;
}
#define IEC61937_HEADER_SIZE 8
int dcv_decoder_process_patch(struct dolby_ddp_dec *ddp_dec, unsigned char*buffer, int bytes)
{
    int mSample_rate = 0;
    int mFrame_size = 0;
    int mChNum = 0;
    int is_eac3 = 0;
    int in_sync = 0;
    int used_size;
    int i = 0;
    unsigned char *read_pointer = NULL;
    size_t main_frame_deficiency = 0;
    int ret = 0;
    unsigned char temp;
    int outPCMLen = 0;
    int outRAWLen = 0;
    int total_size = 0;
    int read_offset = 0;
#if 0
    if (getprop_bool("media.audio_hal.ddp.outdump")) {
        FILE *fp1 = fopen("/data/tmp/audio_decode_61937.raw", "a+");
        if (fp1) {
            int flen = fwrite((char *)buffer, 1, bytes, fp1);
            fclose(fp1);
        } else {
            ALOGD("could not open files!");
        }
    }
#endif

    //TODO should we need check the if the buffer overflow ???
    memcpy((char *)ddp_dec->inbuf + ddp_dec->remain_size, (char *)buffer, bytes);
    ddp_dec->remain_size += bytes;
    total_size = ddp_dec->remain_size;
    read_pointer = ddp_dec->inbuf;
    ddp_dec->outlen_pcm = 0;
    pthread_mutex_lock(&ddp_dec->lock);

    //check the sync word of dolby frames
    if (ddp_dec->is_iec61937 == false) {
        while (ddp_dec->remain_size > 16) {
            if ((read_pointer[0] == 0x0b && read_pointer[1] == 0x77) || \
                (read_pointer[0] == 0x77 && read_pointer[1] == 0x0b)) {
                Get_Parameters(read_pointer, &mSample_rate, &mFrame_size, &mChNum,&is_eac3);
                if ((mFrame_size == 0) || (mFrame_size < PTR_HEAD_SIZE) || \
                    (mChNum == 0) || (mSample_rate == 0)) {
                } else {
                    in_sync = 1;
                    break;
                }
            }
            ddp_dec->remain_size--;
            read_pointer++;
        }
    } else {
        //if the dolby audio is contained in 61937 format. for perfermance issue,
        //re-check the PaPbPcPd
        //TODO, we need improve the perfermance issue from the whole pipeline,such
        //as read/write burst size optimization(DD/6144,DD+ 24576..) as some

    while (ddp_dec->remain_size > 16) {
        if ((read_pointer[i + 0] == 0x72 && read_pointer[i + 1] == 0xf8 && read_pointer[i + 2] == 0x1f && read_pointer[i + 3] == 0x4e)||
               (read_pointer[i + 0] == 0x4e && read_pointer[i + 1] == 0x1f && read_pointer[i + 2] == 0xf8 && read_pointer[i + 3] == 0x72)) {
                    unsigned int pcpd = *(uint32_t*)(read_pointer  + 4);
                    int pc = (pcpd & 0x1f);
                    int  payload_size ;
                    if (pc == 0x15) {
                        mFrame_size = payload_size = (pcpd >> 16);
                        in_sync = 1;
                        break;
                    } else if (pc == 0x1) {
                        mFrame_size = payload_size = (pcpd >> 16) / 8;
                        in_sync = 1;
                        break;
                    }
            }
            ddp_dec->remain_size--;
            read_pointer++;
        }
        read_offset = 8;
    }

    ALOGV("remain %d, frame size %d,in sync %d\n", ddp_dec->remain_size, mFrame_size, in_sync);

    //we do not have one complete dolby frames.we need cache the
    //data and combine with the next input data.
    if (ddp_dec->remain_size < mFrame_size || in_sync == 0) {
        //ALOGI("remain %d,frame size %d, read more\n",remain_size,mFrame_size);
        memcpy(ddp_dec->inbuf, read_pointer, ddp_dec->remain_size);
        goto EXIT;

    }

    read_pointer += read_offset;
    ddp_dec->remain_size -= read_offset;
    //ALOGI("frame size %d,remain %d\n",mFrame_size,remain_size);
    //do the endian conversion
    if (read_pointer[0] == 0x77 && read_pointer[1] == 0x0b) {
        for (i = 0; i < mFrame_size / 2; i++) {
            temp = read_pointer[2 * i + 1];
            read_pointer[2 * i + 1] = read_pointer[2 * i];
            read_pointer[2 * i] = temp;
        }
    }

    ddp_dec->outlen_pcm =  0;
    ddp_dec->outlen_raw = 0;
    used_size = 0;

    while (mFrame_size > 0) {
        outPCMLen = 0;
        outRAWLen = 0;
        int current_size = 0;
        current_size = dcv_decode_process((unsigned char*)read_pointer + used_size,
                                             mFrame_size,
                                             (unsigned char *)ddp_dec->outbuf + ddp_dec->outlen_pcm,
                                             &outPCMLen,
                                             (char *)ddp_dec->outbuf_raw + ddp_dec->outlen_raw,
                                             &outRAWLen,
                                             ddp_dec->nIsEc3,
                                             &ddp_dec->pcm_out_info);
        used_size += current_size;
        ddp_dec->outlen_pcm += outPCMLen;
        ddp_dec->outlen_raw += outRAWLen;
        if (used_size > 0)
            mFrame_size -= current_size;
    }
    if (used_size > 0) {
        ddp_dec->remain_size -= used_size;
        memcpy(ddp_dec->inbuf, read_pointer + used_size, ddp_dec->remain_size);
    }

#if 0
    if (getprop_bool("media.audio_hal.ddp.outdump")) {
        FILE *fp1 = fopen("/data/tmp/audio_decode.raw", "a+");
        if (fp1) {
            int flen = fwrite((char *)ddp_dec->outbuf, 1, ddp_dec->outlen_pcm, fp1);
            fclose(fp1);
        } else {
            ALOGD("could not open files!");
        }
    }
#endif

    if (ddp_dec->outlen_pcm > 0 && ddp_dec->pcm_out_info.sample_rate > 0 && ddp_dec->pcm_out_info.sample_rate != 48000) {
        if ((int)ddp_dec->aml_resample.input_sr != ddp_dec->pcm_out_info.sample_rate) {
            ALOGI("init resampler from %d to 48000!\n", ddp_dec->pcm_out_info.sample_rate);
            ddp_dec->aml_resample.input_sr = ddp_dec->pcm_out_info.sample_rate;
            ddp_dec->aml_resample.output_sr = 48000;
            ddp_dec->aml_resample.channels = ddp_dec->pcm_out_info.channel_num;
            resampler_init (&ddp_dec->aml_resample);
            /*max buffer from 32K to 48K*/
            if (!ddp_dec->resample_outbuf) {
                ddp_dec->resample_outbuf = (unsigned char*) malloc (MAX_DDP_BUFFER_SIZE *3/2);
                if (!ddp_dec->resample_outbuf) {
                    ALOGE ("malloc buffer failed\n");
                    ret = -1;
                    goto EXIT;
                }
            }
        }
        int out_frame = ddp_dec->outlen_pcm >> 2;
        out_frame = resample_process (&ddp_dec->aml_resample, out_frame,
                (int16_t *) ddp_dec->outbuf, (int16_t *) ddp_dec->resample_outbuf);
        ddp_dec->outlen_pcm = out_frame << 2;

        if (get_buffer_write_space(&ddp_dec->output_ring_buf) > ddp_dec->outlen_pcm) {
            ring_buffer_write(&ddp_dec->output_ring_buf, ddp_dec->resample_outbuf,
                    ddp_dec->outlen_pcm, UNCOVER_WRITE);
            ALOGV("mFrame_size:%d, outlen_pcm:%d, ret = %d\n", mFrame_size , ddp_dec->outlen_pcm, used_size);
        } else {
            ALOGI("Lost data, outlen_pcm:%d\n", ddp_dec->outlen_pcm);
        }

    } else if (ddp_dec->outlen_pcm > 0) {
        if (get_buffer_write_space(&ddp_dec->output_ring_buf) > ddp_dec->outlen_pcm) {
            ring_buffer_write(&ddp_dec->output_ring_buf, ddp_dec->outbuf,
                    ddp_dec->outlen_pcm, UNCOVER_WRITE);
            ALOGV("mFrame_size:%d, outlen_pcm:%d, ret = %d\n", mFrame_size, ddp_dec->outlen_pcm, used_size);
        }
    }
    pthread_mutex_unlock(&ddp_dec->lock);
    return 0;
EXIT:
    pthread_mutex_unlock(&ddp_dec->lock);
    return -1;
}

