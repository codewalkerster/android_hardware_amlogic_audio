#include <stdio.h>
#include <stdarg.h>
#include <string.h>
#include <android/log.h>
#include <cutils/properties.h>
#include <pthread.h>

//code here for sys write service
#ifdef USE_SYS_WRITE_SERVICE
#include <binder/Binder.h>
#include <binder/IServiceManager.h>
#include <utils/Atomic.h>
#include <utils/Log.h>
#include <utils/RefBase.h>
#include <utils/String8.h>
#include <utils/String16.h>
#include <utils/threads.h>
#include <unistd.h>
#if ANDROID_PLATFORM_SDK_VERSION >= 21 //5.0
#include <systemcontrol/ISystemControlService.h>
#else
#include <systemwrite/ISystemWriteService.h>
#endif
// code end
#endif

#include "DTSHD_media_source.h"
#include "aml_audio.h"
extern struct circle_buffer android_out_buffer;
extern struct circle_buffer DDP_out_buffer;
extern struct circle_buffer DD_out_buffer;
extern int spdif_audio_type;
namespace android {

#define LOG_TAG "DTSHD_Media_Source"

#ifdef USE_SYS_WRITE_SERVICE
//code here for system write service
class DeathNotifier: public IBinder::DeathRecipient
{
    public:
        DeathNotifier() {
        }

        void binderDied(const wp<IBinder>& who) {
            ALOGW("system_write died!");
        }
};


#if ANDROID_PLATFORM_SDK_VERSION >= 21 //5.0
//used ISystemControlService
#define SYST_SERVICES_NAME "system_control"
#else
//used amSystemWriteService
#define ISystemControlService ISystemWriteService
#define SYST_SERVICES_NAME "system_write"
#endif

static sp<ISystemControlService> amSystemWriteService;
static sp<DeathNotifier> amDeathNotifier;
static  Mutex            amLock;
static  Mutex            amgLock;

const sp<ISystemControlService>& getSystemWriteServiceDts()
{
    Mutex::Autolock _l(amgLock);
    if (amSystemWriteService.get() == 0) {
        sp<IServiceManager> sm = defaultServiceManager();
#if 0
        sp<IBinder> binder;
        do {
            binder = sm->getService(String16("system_write"));
            if (binder != 0)
                break;
            ALOGW("SystemWriteService not published, waiting...");
            usleep(500000); // 0.5 s
        } while(true);
        if (amDeathNotifier == NULL) {
            amDeathNotifier = new DeathNotifier();
        }
        binder->linkToDeath(amDeathNotifier);
        amSystemWriteService = interface_cast<ISystemWriteService>(binder);
#endif


        amSystemWriteService = interface_cast<ISystemControlService>(sm->getService(String16(SYST_SERVICES_NAME)));

    }
    ALOGE_IF(amSystemWriteService==0, "no SystemWrite Service!?");

    return amSystemWriteService;
}
void amSystemWriteSetPropertyDts(const char* key, const char* value)
{
    const sp<ISystemControlService>& sws = getSystemWriteServiceDts();
    if (sws != 0) {
        sws->setProperty(String16(key), String16(value));
    }
}
//code end for system write service
#endif

//----------------------------DTS Media Source----------------------------------------------

static pthread_mutex_t decode_dev_op_mutex = PTHREAD_MUTEX_INITIALIZER;
static int decode_ThreadExitFlag = 0; //0:exit from thread; 1:thread looping
static int decode_ThreadStopFlag = 1; //0:start; 1: stop
static pthread_t decode_ThreadID = 0;

Dtshd_Media_Source::Dtshd_Media_Source(void) {
    ALOGI("[%s: %d]\n", __FUNCTION__, __LINE__);
    mStarted = false;
    mMeta = new MetaData;
    mGroup = NULL;
    mSample_rate = 0;
    mChNum = 0;
    mFrame_size = 0;
    mStop_ReadBuf_Flag = 0; //0:start 1:stop

    mDataSource=NULL;
    mBytesReaded=0;
    mCurrentTimeUs=0;
    bytes_readed_sum_pre=0;
    bytes_readed_sum=0;
}

Dtshd_Media_Source::~Dtshd_Media_Source() {
    ALOGI("%s %d \n",__FUNCTION__,__LINE__);
    if (mStarted) {
        stop();
    }
}

int Dtshd_Media_Source::GetSampleRate() {
    ALOGI("[Dtshd_Media_Source::%s: %d]\n", __FUNCTION__, __LINE__);
    return mSample_rate;
}
int Dtshd_Media_Source::GetChNum() {
    ALOGI("[Dtshd_Media_Source::%s: %d]\n", __FUNCTION__, __LINE__);
    return mChNum;
}

int Dtshd_Media_Source::Get_Stop_ReadBuf_Flag() {
    return mStop_ReadBuf_Flag;
}

int Dtshd_Media_Source::Set_Stop_ReadBuf_Flag(int Stop) {
    mStop_ReadBuf_Flag = Stop;
    return 0;
}

sp<MetaData> Dtshd_Media_Source::getFormat() {
    ALOGI("[Dtshd_Media_Source::%s: %d]\n", __FUNCTION__, __LINE__);
    return mMeta;
}

status_t Dtshd_Media_Source::start(MetaData *params) {
    ALOGI("[Dtshd_Media_Source::%s: %d]\n", __FUNCTION__, __LINE__);
    mGroup = new MediaBufferGroup;
    mGroup->add_buffer(new MediaBuffer(4096*2));
    mStarted = true;
    return OK;
}

status_t Dtshd_Media_Source::stop() {
    ALOGI("[Dtshd_Media_Source::%s: %d]\n", __FUNCTION__, __LINE__);
    delete mGroup;
    mGroup = NULL;
    mStarted = false;
    return OK;
}

int Dtshd_Media_Source::MediaSourceRead_buffer(unsigned char *buffer, int size) {
    int readcnt = -1;
    int sleep_time = 0;
    if ((mStop_ReadBuf_Flag == 1) || (decode_ThreadStopFlag == 1)) {
        ALOGI("[Dtshd_Media_Source::%s] dtshd mediasource stop!\n ", __FUNCTION__);
        return -1;
    }
    while ((readcnt < size) && (mStop_ReadBuf_Flag == 0)
            && (decode_ThreadStopFlag == 0)) {
        readcnt = buffer_read(&android_out_buffer, (char*) buffer, size);
        //ALOGI("readcnt:%d,sleep_time:%d,size:%d",readcnt,sleep_time,size);
        if (readcnt < 0) {
            sleep_time++;
            usleep(1000); //1ms
        }
        if (sleep_time > 2000) { //wait for max 1s to get audio data
            ALOGE("[%s] time out to read audio buffer data! wait for 2s\n ",
                    __FUNCTION__);
            return -1;
        }
    }
    return readcnt;
}
status_t Dtshd_Media_Source::read(MediaBuffer **out, const ReadOptions *options) {
    *out = NULL;
    unsigned char ptr_head[4] = { 0 };
    unsigned char ptr_head2[IEC61937_DTS_HEAD_PTR -4] = { 0 };
    int SyncFlag = 0;
    int readedbytes = 0;
   if (MediaSourceRead_buffer(&ptr_head[0], 4) < 4) {
        readedbytes = 4;
        ALOGI("WARNING:read %d bytes failed [%s %d]!\n", readedbytes,
            __FUNCTION__, __LINE__);
        return ERROR_END_OF_STREAM;
    }

    mFrame_size = 0;
    SyncFlag = 0;

    if ((mStop_ReadBuf_Flag == 1) || (decode_ThreadStopFlag == 1)) {
        ALOGI("Stop Flag is set, stop read_buf [%s %d]", __FUNCTION__,
                __LINE__);
        return ERROR_END_OF_STREAM;
    }
    if ((mStop_ReadBuf_Flag == 1) || (decode_ThreadStopFlag == 1)) {
        ALOGI("Stop Flag is set, stop read_buf [%s %d]", __FUNCTION__,
                __LINE__);
        return ERROR_END_OF_STREAM;
    }
    while (!SyncFlag) {
        int i =0;
        //DTS_SYNCWORD_IEC61937 : 0xF8724E1F
        if ((ptr_head[0] == 0x72 && ptr_head[ 1] == 0xf8
                    &&ptr_head[2] == 0x1f && ptr_head[3] == 0x4e)||
                (ptr_head[0] == 0xf8 && ptr_head[1] == 0x72
                &&ptr_head[2] == 0x4e && ptr_head[3] == 0x1f)) {

            if ((mStop_ReadBuf_Flag == 1) || (decode_ThreadStopFlag == 1)) {
                ALOGI("Stop Flag is set, stop read_buf [%s %d]",
                        __FUNCTION__, __LINE__);
                return ERROR_END_OF_STREAM;
            }
            SyncFlag = 1;
        }
        if (SyncFlag == 0) {
            ptr_head[0] =ptr_head[1];
            ptr_head[1] =ptr_head[2];
            ptr_head[2] =ptr_head[3];
            if (MediaSourceRead_buffer(&ptr_head[3], 1) < 1) {
                readedbytes = 1;
                ALOGI("WARNING: read %d bytes failed [%s %d]!\n",
                    readedbytes, __FUNCTION__, __LINE__);
                return ERROR_END_OF_STREAM;
            }

            if ((mStop_ReadBuf_Flag == 1) || (decode_ThreadStopFlag == 1)) {
                ALOGI("Stop Flag is set, stop read_buf [%s %d]", __FUNCTION__,
                        __LINE__);
                return ERROR_END_OF_STREAM;
            }
        }
    }
    if (MediaSourceRead_buffer(&ptr_head2[0], 4) < 4) {
        readedbytes = 4;
        ALOGI("WARNING:read %d bytes failed [%s %d]!\n", readedbytes,
                __FUNCTION__, __LINE__);
        return ERROR_END_OF_STREAM;
    }
    //memcpy(&mFrame_size,ptr_head2+IEC61937_DTS_HEAD_PTR-6,2);
    //ALOGI("mFrame_size:%d",mFrame_size);
    //memcpy(&mFrame_size,ptr_head2+IEC61937_DTS_HEAD_PTR-6,2);
    //ado-no lib only pack dts core data
    mFrame_size = 2040;
    MediaBuffer *buffer;
    status_t err = mGroup->acquire_buffer(&buffer);
    if (err != OK) {
        return err;
    }
    if (MediaSourceRead_buffer(
            (unsigned char*) (buffer->data()),
            mFrame_size) != mFrame_size ) {
        ALOGI("[%s %d]stream read failed:bytes_req/%d\n", __FUNCTION__,
                __LINE__, mFrame_size);
        buffer->release();
        buffer = NULL;
        return ERROR_END_OF_STREAM;
    }

    buffer->set_range(0, mFrame_size);
    buffer->meta_data()->setInt64(kKeyTime, 0);
    buffer->meta_data()->setInt32(kKeyIsSyncFrame, 1);

    *out = buffer;
    return OK;
}


//-------------------------------OMX codec------------------------------------------------

const char *MEDIA_MIMETYPE_AUDIO_DTS = "audio/dtshd";
Aml_OMX_DtsCodec::Aml_OMX_DtsCodec(void) {
    ALOGI("[Aml_OMX_DtsCodec::%s: %d],atype %d\n", __FUNCTION__, __LINE__,spdif_audio_type);
    m_codec = NULL;
    status_t m_OMXClientConnectStatus = m_OMXClient.connect();
    lock_init();
    locked();
    buf_decode_offset = 0;
    buf_decode_offset_pre = 0;
    if (m_OMXClientConnectStatus != OK) {
        ALOGE("Err:omx client connect error\n");
    } else {
        const char *mine_type = NULL;
        if (spdif_audio_type == DTSHD || spdif_audio_type == DTS)
            mine_type = MEDIA_MIMETYPE_AUDIO_DTS;

        m_OMXMediaSource = new Dtshd_Media_Source();
        sp < MetaData > metadata = m_OMXMediaSource->getFormat();
        metadata->setCString(kKeyMIMEType, mine_type);
        m_codec = OMXCodec::Create(m_OMXClient.interface(), metadata, false, // createEncoder
                m_OMXMediaSource, 0, 0);

        if (m_codec != NULL) {
            ALOGI("OMXCodec::Create success %s %d \n", __FUNCTION__, __LINE__);
        } else {
            ALOGE("Err: OMXCodec::Create failed %s %d \n", __FUNCTION__,
                    __LINE__);
        }
    }
    unlocked();
}

Aml_OMX_DtsCodec::~Aml_OMX_DtsCodec() {
}

status_t Aml_OMX_DtsCodec::read(unsigned char *buf, unsigned *size, int *exit) {
    MediaBuffer *srcBuffer;
    status_t status;
    m_OMXMediaSource->Set_Stop_ReadBuf_Flag(*exit);

    if (*exit) {
        ALOGI("NOTE:exit flag enabled! [%s %d] \n", __FUNCTION__, __LINE__);
        *size = 0;
        return OK;
    }

    status = m_codec->read(&srcBuffer, NULL);

    if (srcBuffer == NULL) {
        *size = 0;
        return OK;
    }

    *size = srcBuffer->range_length();

    if (status == OK && (*size != 0)) {
        memcpy((unsigned char *) buf,
                (unsigned char *) srcBuffer->data() + srcBuffer->range_offset(),
                *size);
        srcBuffer->set_range(srcBuffer->range_offset() + (*size),
                srcBuffer->range_length() - (*size));
        srcBuffer->meta_data()->findInt64(kKeyTime, &buf_decode_offset);
    }

    if (srcBuffer->range_length() == 0) {
        srcBuffer->release();
        srcBuffer = NULL;
    }

    return OK;
}

status_t Aml_OMX_DtsCodec::start() {
    ALOGI("[Aml_OMX_DtsCodec::%s %d] enter!\n", __FUNCTION__, __LINE__);
    status_t status = m_codec->start();
    if (status != OK) {
        ALOGE("Err:OMX client can't start OMX decoder! status=%d (0x%08x)\n",
                (int) status, (int) status);
        m_codec.clear();
    }
    return status;
}

void Aml_OMX_DtsCodec::stop() {
    ALOGI("[Aml_OMX_DtsCodec::%s %d] enter!\n", __FUNCTION__, __LINE__);
    if (m_codec != NULL) {
        if (m_OMXMediaSource->Get_Stop_ReadBuf_Flag())
            m_OMXMediaSource->Set_Stop_ReadBuf_Flag(1);
        m_codec->pause();
        m_codec->stop();
        wp < MediaSource > tmp = m_codec;
        m_codec.clear();
        while (tmp.promote() != NULL) {
            ALOGI("[Aml_OMX_DtsCodec::%s %d]wait m_codec free OK!\n", __FUNCTION__,
                    __LINE__);
            usleep(1000);
        }
        m_OMXClient.disconnect();
        m_OMXMediaSource.clear();
    } else
        ALOGE("m_codec==NULL, m_codec->stop() failed! [%s %d] \n", __FUNCTION__,
                __LINE__);
}

void Aml_OMX_DtsCodec::pause() {
    ALOGI("[Aml_OMX_DtsCodec::%s %d] \n", __FUNCTION__, __LINE__);
    if (m_codec != NULL)
        m_codec->pause();
    else
        ALOGE("m_codec==NULL, m_codec->pause() failed! [%s %d] \n",
                __FUNCTION__, __LINE__);
}

int Aml_OMX_DtsCodec::GetDecBytes() {
    int used_len = 0;
    used_len = buf_decode_offset - buf_decode_offset_pre;
    buf_decode_offset_pre = buf_decode_offset;
    return used_len;
}

void Aml_OMX_DtsCodec::lock_init() {
    pthread_mutexattr_t attr;
    pthread_mutexattr_init(&attr);
    pthread_mutexattr_setpshared(&attr, PTHREAD_PROCESS_SHARED);
    pthread_mutex_init(&lock, &attr);
    pthread_mutexattr_destroy(&attr);
}
void Aml_OMX_DtsCodec::locked() {
    pthread_mutex_lock (&lock);
}
void Aml_OMX_DtsCodec::unlocked() {
    pthread_mutex_unlock (&lock);
}

//--------------------------------OMX_Codec_local API-------------------------------------------------

Aml_OMX_DtsCodec *arm_dts_omx_codec = NULL;

void omx_dts_codec_pause() {
    if (arm_dts_omx_codec != NULL) {
        arm_dts_omx_codec->locked();
        arm_dts_omx_codec->pause();
        arm_dts_omx_codec->unlocked();
    } else
        ALOGE("arm_dts_omx_codec==NULL  arm_dts_omx_codec->pause failed! %s %d \n",
                __FUNCTION__, __LINE__);
}

void omx_dts_codec_read(unsigned char *buf, unsigned *size, int *exit) {
    if (arm_dts_omx_codec != NULL) {
        arm_dts_omx_codec->locked();
        arm_dts_omx_codec->read(buf, size, exit);
        arm_dts_omx_codec->unlocked();
    } else
        ALOGE("arm_dts_omx_codec==NULL  arm_dts_omx_codec->read failed! %s %d \n",
                __FUNCTION__, __LINE__);
}

int omx_dts_codec_get_declen() {
    int declen = 0;
    if (arm_dts_omx_codec != NULL) {
        arm_dts_omx_codec->locked();
        declen = arm_dts_omx_codec->GetDecBytes();
        arm_dts_omx_codec->unlocked();
    } else {
        ALOGI(
                "NOTE:arm_dts_omx_codec==NULL arm_dts_omx_codec_get_declen() return 0! %s %d \n",
                __FUNCTION__, __LINE__);
    }
    return declen;
}

int omx_dts_codec_get_FS() {
    if (arm_dts_omx_codec != NULL) {
        return arm_dts_omx_codec->m_OMXMediaSource->GetSampleRate();

    } else {
        ALOGI(
                "NOTE:arm_dts_omx_codec==NULL arm_dts_omx_codec_get_FS() return 0! %s %d \n",
                __FUNCTION__, __LINE__);
        return 0;
    }
}

int omx_dts_codec_get_Nch() {
    if (arm_dts_omx_codec != NULL) {
        return arm_dts_omx_codec->m_OMXMediaSource->GetChNum();
    } else {
        ALOGI(
                "NOTE:arm_dts_omx_codec==NULL arm_dts_omx_codec_get_Nch() return 0! %s %d \n",
                __FUNCTION__, __LINE__);
        return 0;
    }
}

//--------------------------------------Decoder ThreadLoop--------------------------------------------

void *dts_decode_threadloop(void *args) {
    unsigned int outlen = 0;
    unsigned int outlen_raw = 0;
    unsigned int outlen_pcm = 0;
    int write_sucessed = 1;
    int ret = 0;
    char *tmp = NULL;
    tmp = (char*)malloc(6144*4+6144+8);
    if (tmp == NULL) {
        ALOGE("malloc buffer failed\n");
        return NULL;
    }
    ALOGI("[%s %d] enter!\n", __FUNCTION__, __LINE__);
    while (decode_ThreadStopFlag == 0) {
        if (write_sucessed == 1) {
            outlen = 0;
            outlen_raw = 0;
            outlen_pcm = 0;
            omx_dts_codec_read((unsigned char*)tmp, &outlen, &(decode_ThreadStopFlag));
        }
        if (decode_ThreadStopFlag == 1) {
            ALOGD("%s, exit threadloop! \n", __FUNCTION__);
            break;
        }
        if (outlen > 8) {
            memcpy(&outlen_pcm,tmp,4);
            memcpy(&outlen_raw,tmp+4+outlen_pcm,4);
            if (outlen_pcm > 0) {
                ret = buffer_write(&DDP_out_buffer, tmp+4, outlen_pcm);
                if (ret < 0) {
                    write_sucessed = 0;
                    usleep(10 * 1000); //10ms
                } else {
                    write_sucessed = 1;
                }
            }
            if (outlen_raw > 0) {
                //ALOGI("raw data size %d\n",outlen_raw);
                ret = buffer_write(&DD_out_buffer, tmp+4+outlen_pcm+4, outlen_raw);
                if (ret < 0) {
                    write_sucessed = 0;
                    ALOGI("raw data write failed\n");
                    usleep(10 * 1000); //10ms
                } else {
#if 0
        FILE *fp1=fopen("/data/audio_raw.wav","a+");
        if (fp1) {
            int flen=fwrite((char *)tmp+4+outlen_pcm+4,1,outlen_raw,fp1);
            ALOGI("flen = %d---bytes_raw=%d ", flen, outlen_raw);
            fclose(fp1);
        }else{
            ALOGI("could not open file:audio_out.pcm");
        }
#endif
                    write_sucessed = 1;
                }
            }
        }
    }
    decode_ThreadExitFlag = 0;
    if (tmp) {
        free(tmp);
    }
    ALOGD("%s, exiting...\n", __FUNCTION__);
    return NULL;
}

static int start_decode_thread_omx(void) {
    pthread_attr_t attr;
    struct sched_param param;
    int ret = 0;

    ALOGI("[%s %d] enter!\n", __FUNCTION__, __LINE__);
    pthread_mutex_lock(&decode_dev_op_mutex);
    pthread_attr_init(&attr);
    pthread_attr_setschedpolicy(&attr, SCHED_RR);
    param.sched_priority = sched_get_priority_max(SCHED_RR);
    pthread_attr_setschedparam(&attr, &param);
    decode_ThreadStopFlag = 0;
    decode_ThreadExitFlag = 1;
    ret = pthread_create(&decode_ThreadID, &attr, &dts_decode_threadloop, NULL);
    pthread_attr_destroy(&attr);
    if (ret != 0) {
        ALOGE("%s, Create thread fail!\n", __FUNCTION__);
        pthread_mutex_unlock(&decode_dev_op_mutex);
        return -1;
    }
    pthread_mutex_unlock(&decode_dev_op_mutex);
    ALOGD("[%s] exiting...\n", __FUNCTION__);
    return 0;
}

static int stop_decode_thread_omx(void) {
    int i = 0, tmp_timeout_count = 1000;

    ALOGI("[%s %d] enter!\n", __FUNCTION__, __LINE__);
    pthread_mutex_lock(&decode_dev_op_mutex);

    decode_ThreadStopFlag = 1;
    while (1) {
        if (decode_ThreadExitFlag == 0)
            break;
        if (i >= tmp_timeout_count)
            break;
        i++;
        usleep(1000);
    }
    if (i >= tmp_timeout_count) {
        ALOGE(
                "%s, Timeout: we have try %d ms, but the aml audio thread's exec flag is still(%d)!!!\n",
                __FUNCTION__, tmp_timeout_count, decode_ThreadExitFlag);
    } else {
        ALOGD("%s, kill decode thread success after try %d ms.\n", __FUNCTION__,
                i);
    }

    pthread_join(decode_ThreadID, NULL);
    decode_ThreadID = 0;
    ALOGD("%s, aml audio close success.\n", __FUNCTION__);
    pthread_mutex_unlock(&decode_dev_op_mutex);
    return 0;
}

//-------------------------------------external OMX_codec_api-----------------------------------------
extern "C" {
int omx_codec_dts_init(void) {
    int ret = 0;
    ALOGI("omx_codec_init!\n");
#ifndef USE_SYS_WRITE_SERVICE
    ret=property_set("media.libplayer.dtsopt0", "1");
    ALOGI("property_set<media.libplayer.dtsopt0> ret/%d\n",ret);
#else
    amSystemWriteSetPropertyDts("media.libplayer.dtsopt0", "1");
#endif
    arm_dts_omx_codec = new android::Aml_OMX_DtsCodec();
    if (arm_dts_omx_codec == NULL) {
        ALOGE("Err:arm_dts_omx_codec_init failed!\n");
        return -1;
    }

    arm_dts_omx_codec->locked();
    ret = arm_dts_omx_codec->start();
    arm_dts_omx_codec->unlocked();
    if (ret < 0) {
        goto Exit;
    }

    ret = start_decode_thread_omx();
    if (ret == 0)
        return 0;
    Exit: arm_dts_omx_codec->stop();
    delete arm_dts_omx_codec;
    arm_dts_omx_codec = NULL;
    return -1;
}

void omx_codec_dts_close(void) {
    int ret = 0;
#ifndef USE_SYS_WRITE_SERVICE
    ret=property_set("media.libplayer.dtsopt0", "0");
    ALOGI("property_set<media.libplayer.dtsopt0> ret/%d\n",ret);
#else
    amSystemWriteSetPropertyDts("media.libplayer.dtsopt0", "0");
#endif
    if (arm_dts_omx_codec == NULL) {
        ALOGI(
                "NOTE:arm_dts_omx_codec==NULL arm_dts_omx_codec_close() do nothing! %s %d \n",
                __FUNCTION__, __LINE__);
        return;
    }
    ALOGI("omx_codec_close!\n");
    stop_decode_thread_omx();
    arm_dts_omx_codec->locked();
    arm_dts_omx_codec->stop();
    arm_dts_omx_codec->unlocked();
    delete arm_dts_omx_codec;
    arm_dts_omx_codec = NULL;
    return;
}

}
}

