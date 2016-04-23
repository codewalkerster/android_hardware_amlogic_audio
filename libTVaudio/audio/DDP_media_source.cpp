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

#include "DDP_media_source.h"
#include "aml_audio.h"
extern struct circle_buffer android_out_buffer;
extern struct circle_buffer DDP_out_buffer;
extern struct circle_buffer DD_out_buffer;
extern int spdif_audio_type;
namespace android {

#define LOG_TAG "DDP_Media_Source"

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

const sp<ISystemControlService>& getSystemWriteService()
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
void amSystemWriteSetProperty(const char* key, const char* value)
{
    const sp<ISystemControlService>& sws = getSystemWriteService();
    if (sws != 0) {
        sws->setProperty(String16(key), String16(value));
    }
}
//code end for system write service
#endif
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
    if (p_bstrm->bitptr >= BITSPERWRD)
    {
        p_bstrm->buf++;
        p_bstrm->data = *p_bstrm->buf;
        p_bstrm->bitptr -= BITSPERWRD;
        data = (DDPushort)p_bstrm->data;
        *p_data |= ((data >> (numbits - p_bstrm->bitptr)) & msktab[numbits]);
    }
    *p_data = (DDPshort)((DDPushort)(*p_data) >> (BITSPERWRD - numbits));
    return 0;
}


static int Get_DD_Parameters(void *buf, int *sample_rate, int *frame_size, int *ChNum)
{
    int numch=0;
    DDP_BSTRM bstrm={0};
    DDP_BSTRM *p_bstrm=&bstrm;
    short tmp=0,acmod,lfeon,fscod,frmsizecod;
    ddbs_init((short*)buf,0,p_bstrm);

    ddbs_unprj(p_bstrm, &tmp, 16);
    if (tmp!= SYNCWRD)
    {
        ALOGI("Invalid synchronization word");
        return 0;
    }
    ddbs_unprj(p_bstrm, &tmp, 16);
    ddbs_unprj(p_bstrm, &fscod, 2);
    if (fscod == MAXFSCOD)
    {
        ALOGI("Invalid sampling rate code");
        return 0;
    }

    if (fscod == 0)      *sample_rate = 48000;
    else if (fscod == 1) *sample_rate = 44100;
    else if (fscod == 2) *sample_rate = 32000;

    ddbs_unprj(p_bstrm, &frmsizecod, 6);
    if (frmsizecod >= MAXDDDATARATE)
    {
        ALOGI("Invalid frame size code");
        return 0;
    }

    *frame_size=2*frmsizetab[fscod][frmsizecod];

    ddbs_unprj(p_bstrm, &tmp, 5);
    if (!ISDD(tmp))
    {
        ALOGI("Unsupported bitstream id");
        return 0;
    }

    ddbs_unprj(p_bstrm, &tmp, 3);
    ddbs_unprj(p_bstrm, &acmod, 3);

    if ((acmod!= MODE10) && (acmod& 0x1))
    {
        ddbs_unprj(p_bstrm, &tmp, 2);
    }
    if (acmod& 0x4)
    {
        ddbs_unprj(p_bstrm, &tmp, 2);
    }

    if (acmod == MODE20)
    {
        ddbs_unprj(p_bstrm,&tmp, 2);
    }
    ddbs_unprj(p_bstrm, &lfeon, 1);


    numch = chanary[acmod];
    if (0)
    {
        if (numch >= 3)
            numch = 8;
        else
            numch = 2;
    }else{
        numch = 2;
    }
    *ChNum=numch;
    //ALOGI("DEBUG:numch=%d sample_rate=%d %p [%s %d]",ChNum,sample_rate,this,__FUNCTION__,__LINE__);
    return numch;
}

static int Get_DDP_Parameters(void *buf, int *sample_rate, int *frame_size,int *ChNum)
{
    int numch = 0;
    DDP_BSTRM bstrm={0};
    DDP_BSTRM *p_bstrm=&bstrm;
    short tmp=0,acmod,lfeon,strmtyp;
    ddbs_init((short*)buf,0,p_bstrm);
    ddbs_unprj(p_bstrm, &tmp, 16);
    if (tmp!= SYNCWRD) {
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
        if (tmp == 0)
            *sample_rate = 48000;
        else if (tmp == 1)
            *sample_rate = 44100;
        else if (tmp == 2)
            *sample_rate = 32000;

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
    while (p_bstrm->bitptr >= BITSPERWRD)
    {
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
    if (!ISDDP(*p_bsid) && !ISDD(*p_bsid))
    {
        ALOGI("Unsupported bitstream id");
    }

    return 0;
}

static int Get_Parameters(void *buf, int *sample_rate, int *frame_size,int *ChNum)
 {
    DDP_BSTRM bstrm={0};
    DDP_BSTRM *p_bstrm=&bstrm;
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

    ddbs_init((short*)ptr8,0,p_bstrm);
    int ret = ddbs_getbsid(p_bstrm, &bsid);
    if (ret < 0) {
        return -1;
    }

    if (ISDDP(bsid)) {
        Get_DDP_Parameters(ptr8, sample_rate, frame_size, ChNum);
    }else if (ISDD(bsid)){
        Get_DD_Parameters(ptr8, sample_rate, frame_size, ChNum);
    }

    return 0;
}

//----------------------------DDP Media Source----------------------------------------------

static pthread_mutex_t decode_dev_op_mutex = PTHREAD_MUTEX_INITIALIZER;
static int decode_ThreadExitFlag = 0; //0:exit from thread; 1:thread looping
static int decode_ThreadStopFlag = 1; //0:start; 1: stop
static pthread_t decode_ThreadID = 0;

DDP_Media_Source::DDP_Media_Source(void) {
    ALOGI("[%s: %d]\n", __FUNCTION__, __LINE__);
    mStarted = false;
    mMeta = new MetaData;
    mGroup = NULL;
    mSample_rate = 0;
    mChNum = 0;
    mFrame_size = 0;
    mStop_ReadBuf_Flag = 0; //0:start 1:stop
}

DDP_Media_Source::~DDP_Media_Source() {
}

int DDP_Media_Source::GetSampleRate() {
    ALOGI("[DDP_Media_Source::%s: %d]\n", __FUNCTION__, __LINE__);
    return mSample_rate;
}
int DDP_Media_Source::GetChNum() {
    ALOGI("[DDP_Media_Source::%s: %d]\n", __FUNCTION__, __LINE__);
    return mChNum;
}

int DDP_Media_Source::Get_Stop_ReadBuf_Flag() {
    return mStop_ReadBuf_Flag;
}

int DDP_Media_Source::Set_Stop_ReadBuf_Flag(int Stop) {
    //ALOGI("[DDP_Media_Source::%s: %d]\n",__FUNCTION__,__LINE__);
    mStop_ReadBuf_Flag = Stop;
    return 0;
}

sp<MetaData> DDP_Media_Source::getFormat() {
    ALOGI("[DDP_Media_Source::%s: %d]\n", __FUNCTION__, __LINE__);
    return mMeta;
}

status_t DDP_Media_Source::start(MetaData *params) {
    ALOGI("[DDP_Media_Source::%s: %d]\n", __FUNCTION__, __LINE__);
    mGroup = new MediaBufferGroup;
    mGroup->add_buffer(new MediaBuffer(4096));
    mStarted = true;
    return OK;
}

status_t DDP_Media_Source::stop() {
    ALOGI("[DDP_Media_Source::%s: %d]\n", __FUNCTION__, __LINE__);
    delete mGroup;
    mGroup = NULL;
    mStarted = false;
    return OK;
}

int DDP_Media_Source::MediaSourceRead_buffer(unsigned char *buffer, int size) {
    int readcnt = -1;
    int sleep_time = 0;
    if ((mStop_ReadBuf_Flag == 1) || (decode_ThreadStopFlag == 1)) {
        ALOGI("[DDP_Media_Source::%s] ddp mediasource stop!\n ", __FUNCTION__);
        return -1;
    }
    while ((readcnt < size) && (mStop_ReadBuf_Flag == 0)
            && (decode_ThreadStopFlag == 0)) {
        readcnt = buffer_read(&android_out_buffer, (char*) buffer, size);
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

status_t DDP_Media_Source::read(MediaBuffer **out, const ReadOptions *options) {
    *out = NULL;
    unsigned char ptr_head[PTR_HEAD_SIZE] = { 0 };
    int readedbytes;
    int SyncFlag = 0;

    resync: mFrame_size = 0;
    SyncFlag = 0;

    if ((mStop_ReadBuf_Flag == 1) || (decode_ThreadStopFlag == 1)) {
        ALOGI("Stop Flag is set, stop read_buf [%s %d]", __FUNCTION__,
                __LINE__);
        return ERROR_END_OF_STREAM;
    }

    if (MediaSourceRead_buffer(&ptr_head[0], 6) < 6) {
        readedbytes = 6;
        ALOGI("WARNING:read %d bytes failed [%s %d]!\n", readedbytes,
                __FUNCTION__, __LINE__);
        return ERROR_END_OF_STREAM;
    }

    if ((mStop_ReadBuf_Flag == 1) || (decode_ThreadStopFlag == 1)) {
        ALOGI("Stop Flag is set, stop read_buf [%s %d]", __FUNCTION__,
                __LINE__);
        return ERROR_END_OF_STREAM;
    }

    while (!SyncFlag) {
        int i;
        for (i = 0; i <= 4; i++) {
            if ((ptr_head[i] == 0x0b && ptr_head[i + 1] == 0x77)
                    || (ptr_head[i] == 0x77 && ptr_head[i + 1] == 0x0b)) {
                memcpy(&ptr_head[0], &ptr_head[i], 6 - i);
                if (MediaSourceRead_buffer(&ptr_head[6 - i], i) < i) {
                    readedbytes = i;
                    ALOGI("WARNING: read %d bytes failed [%s %d]!\n",
                            readedbytes, __FUNCTION__, __LINE__);
                    return ERROR_END_OF_STREAM;
                }

                if ((mStop_ReadBuf_Flag == 1) || (decode_ThreadStopFlag == 1)) {
                    ALOGI("Stop Flag is set, stop read_buf [%s %d]",
                            __FUNCTION__, __LINE__);
                    return ERROR_END_OF_STREAM;
                }
                SyncFlag = 1;
                break;
            }
        }

        if (SyncFlag == 0) {
            ptr_head[0] = ptr_head[5];
            if (MediaSourceRead_buffer(&ptr_head[1], 5) < 5) {
                readedbytes = 5;
                ALOGI("WARNING: fpread_buffer read %d bytes failed [%s %d]!\n",
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
    if (MediaSourceRead_buffer(&ptr_head[6], PTR_HEAD_SIZE - 6)
            < (PTR_HEAD_SIZE - 6)) {
        readedbytes = PTR_HEAD_SIZE - 6;
        ALOGI("WARNING: fpread_buffer read %d bytes failed [%s %d]!\n",
                readedbytes, __FUNCTION__, __LINE__);
        return ERROR_END_OF_STREAM;
    }

    Get_Parameters(ptr_head, &mSample_rate, &mFrame_size, &mChNum);
    if ((mFrame_size == 0) || (mFrame_size < PTR_HEAD_SIZE) || (mChNum == 0)
            || (mSample_rate == 0)) {
        goto resync;
    }

    MediaBuffer *buffer;
    status_t err = mGroup->acquire_buffer(&buffer);
    if (err != OK) {
        return err;
    }
    memcpy((unsigned char*) (buffer->data()), ptr_head, PTR_HEAD_SIZE);
    if (MediaSourceRead_buffer(
            (unsigned char*) (buffer->data()) + PTR_HEAD_SIZE,
            mFrame_size - PTR_HEAD_SIZE) != (mFrame_size - PTR_HEAD_SIZE)) {
        ALOGI("[%s %d]stream read failed:bytes_req/%d\n", __FUNCTION__,
                __LINE__, (mFrame_size - PTR_HEAD_SIZE));
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

const char *MEDIA_MIMETYPE_AUDIO_AC3 = "audio/ac3";
const char *MEDIA_MIMETYPE_AUDIO_EAC3 = "audio/eac3";
Aml_OMX_Codec::Aml_OMX_Codec(void) {
    ALOGI("[Aml_OMX_Codec::%s: %d],atype %d\n", __FUNCTION__, __LINE__,spdif_audio_type);
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
        if (spdif_audio_type == EAC3)
            mine_type = MEDIA_MIMETYPE_AUDIO_EAC3;
        else
            mine_type = MEDIA_MIMETYPE_AUDIO_AC3;
        m_OMXMediaSource = new DDP_Media_Source();
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

Aml_OMX_Codec::~Aml_OMX_Codec() {
}

status_t Aml_OMX_Codec::read(unsigned char *buf, unsigned *size, int *exit) {
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

status_t Aml_OMX_Codec::start() {
    ALOGI("[Aml_OMX_Codec::%s %d] enter!\n", __FUNCTION__, __LINE__);
    status_t status = m_codec->start();
    if (status != OK) {
        ALOGE("Err:OMX client can't start OMX decoder! status=%d (0x%08x)\n",
                (int) status, (int) status);
        m_codec.clear();
    }
    return status;
}

void Aml_OMX_Codec::stop() {
    ALOGI("[Aml_OMX_Codec::%s %d] enter!\n", __FUNCTION__, __LINE__);
    if (m_codec != NULL) {
        if (m_OMXMediaSource->Get_Stop_ReadBuf_Flag())
            m_OMXMediaSource->Set_Stop_ReadBuf_Flag(1);
        m_codec->pause();
        m_codec->stop();
        wp < MediaSource > tmp = m_codec;
        m_codec.clear();
        while (tmp.promote() != NULL) {
            ALOGI("[Aml_OMX_Codec::%s %d]wait m_codec free OK!\n", __FUNCTION__,
                    __LINE__);
            usleep(1000);
        }
        m_OMXClient.disconnect();
        m_OMXMediaSource.clear();
    } else
        ALOGE("m_codec==NULL, m_codec->stop() failed! [%s %d] \n", __FUNCTION__,
                __LINE__);
}

void Aml_OMX_Codec::pause() {
    ALOGI("[Aml_OMX_Codec::%s %d] \n", __FUNCTION__, __LINE__);
    if (m_codec != NULL)
        m_codec->pause();
    else
        ALOGE("m_codec==NULL, m_codec->pause() failed! [%s %d] \n",
                __FUNCTION__, __LINE__);
}

int Aml_OMX_Codec::GetDecBytes() {
    int used_len = 0;
    used_len = buf_decode_offset - buf_decode_offset_pre;
    buf_decode_offset_pre = buf_decode_offset;
    return used_len;
}

void Aml_OMX_Codec::lock_init() {
    pthread_mutexattr_t attr;
    pthread_mutexattr_init(&attr);
    pthread_mutexattr_setpshared(&attr, PTHREAD_PROCESS_SHARED);
    pthread_mutex_init(&lock, &attr);
    pthread_mutexattr_destroy(&attr);
}
void Aml_OMX_Codec::locked() {
    pthread_mutex_lock (&lock);
}
void Aml_OMX_Codec::unlocked() {
    pthread_mutex_unlock (&lock);
}

//--------------------------------OMX_Codec_local API-------------------------------------------------

Aml_OMX_Codec *arm_omx_codec = NULL;

void omx_codec_pause() {
    if (arm_omx_codec != NULL) {
        arm_omx_codec->locked();
        arm_omx_codec->pause();
        arm_omx_codec->unlocked();
    } else
        ALOGE("arm_omx_codec==NULL  arm_omx_codec->pause failed! %s %d \n",
                __FUNCTION__, __LINE__);
}

void omx_codec_read(unsigned char *buf, unsigned *size, int *exit) {
    if (arm_omx_codec != NULL) {
        arm_omx_codec->locked();
        arm_omx_codec->read(buf, size, exit);
        arm_omx_codec->unlocked();
    } else
        ALOGE("arm_omx_codec==NULL  arm_omx_codec->read failed! %s %d \n",
                __FUNCTION__, __LINE__);
}

int omx_codec_get_declen() {
    int declen = 0;
    if (arm_omx_codec != NULL) {
        arm_omx_codec->locked();
        declen = arm_omx_codec->GetDecBytes();
        arm_omx_codec->unlocked();
    } else {
        ALOGI(
                "NOTE:arm_omx_codec==NULL arm_omx_codec_get_declen() return 0! %s %d \n",
                __FUNCTION__, __LINE__);
    }
    return declen;
}

int omx_codec_get_FS() {
    if (arm_omx_codec != NULL) {
        return arm_omx_codec->m_OMXMediaSource->GetSampleRate();

    } else {
        ALOGI(
                "NOTE:arm_omx_codec==NULL arm_omx_codec_get_FS() return 0! %s %d \n",
                __FUNCTION__, __LINE__);
        return 0;
    }
}

int omx_codec_get_Nch() {
    if (arm_omx_codec != NULL) {
        return arm_omx_codec->m_OMXMediaSource->GetChNum();
    } else {
        ALOGI(
                "NOTE:arm_omx_codec==NULL arm_omx_codec_get_Nch() return 0! %s %d \n",
                __FUNCTION__, __LINE__);
        return 0;
    }
}

//--------------------------------------Decoder ThreadLoop--------------------------------------------

void *decode_threadloop(void *args) {
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
            omx_codec_read((unsigned char*)tmp, &outlen, &(decode_ThreadStopFlag));
        }
        if (decode_ThreadStopFlag == 1) {
            ALOGD("%s, exit threadloop! \n", __FUNCTION__);
            break;
        }
        if (outlen > 8) {
            memcpy(&outlen_pcm,tmp,4);
            memcpy(&outlen_raw,tmp+4+outlen_pcm,4);
            if (outlen_pcm > 0) {
                //ALOGI("pcm data size %d\n",outlen_pcm);
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
                    //write_sucessed = 0;
                    //ALOGI("raw data write failed\n");
                    usleep(10 * 1000); //10ms
                } else {
                    //write_sucessed = 1;
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
    ret = pthread_create(&decode_ThreadID, &attr, &decode_threadloop, NULL);
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
int omx_codec_init(void) {
    int ret = 0;
    ALOGI("omx_codec_init!\n");
#ifndef USE_SYS_WRITE_SERVICE
    ret=property_set("media.libplayer.dtsopt0", "1");
    ALOGI("property_set<media.libplayer.dtsopt0> ret/%d\n",ret);
#else
    amSystemWriteSetProperty("media.libplayer.dtsopt0", "1");
#endif
    arm_omx_codec = new android::Aml_OMX_Codec();
    if (arm_omx_codec == NULL) {
        ALOGE("Err:arm_omx_codec_init failed!\n");
        return -1;
    }

    arm_omx_codec->locked();
    ret = arm_omx_codec->start();
    arm_omx_codec->unlocked();
    if (ret < 0) {
        goto Exit;
    }

    ret = start_decode_thread_omx();
    if (ret == 0)
        return 0;
    Exit: arm_omx_codec->stop();
    delete arm_omx_codec;
    arm_omx_codec = NULL;
    return -1;
}

void omx_codec_close(void) {
    int ret = 0;
#ifndef USE_SYS_WRITE_SERVICE
    ret=property_set("media.libplayer.dtsopt0", "0");
    ALOGI("property_set<media.libplayer.dtsopt0> ret/%d\n",ret);
#else
    amSystemWriteSetProperty("media.libplayer.dtsopt0", "0");
#endif
    if (arm_omx_codec == NULL) {
        ALOGI(
                "NOTE:arm_omx_codec==NULL arm_omx_codec_close() do nothing! %s %d \n",
                __FUNCTION__, __LINE__);
        return;
    }
    ALOGI("omx_codec_close!\n");
    stop_decode_thread_omx();
    arm_omx_codec->locked();
    arm_omx_codec->stop();
    arm_omx_codec->unlocked();
    delete arm_omx_codec;
    arm_omx_codec = NULL;
    return;
}

}
}

