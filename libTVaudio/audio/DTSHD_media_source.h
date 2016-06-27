#ifndef MEDIA_DTSHD_MEDIASOURCE_H_
#define MEDIA_DTSHD_MEDIASOURCE_H_

#include  "MediaSource.h"
#include  "DataSource.h"
#include  "MediaBufferGroup.h"
#include  "MetaData.h"
#include  "OMXCodec.h"
#include  "OMX_Index.h"
#include  "OMX_Core.h"
#include  "OMXClient.h"
namespace android
{

#define AML_DCA_OMX_DECODER_NUMBUF    2
#define AML_DCA_SW_CORE_16M          0x7ffe8001
#define AML_DCA_SW_CORE_14M          0x1fffe800
#define AML_DCA_SW_CORE_24M          0xfe80007f
#define AML_DCA_SW_CORE_16             0xfe7f0180
#define AML_DCA_SW_CORE_14             0xff1f00e8
#define AML_DCA_SW_CORE_24             0x80fe7f01
#define AML_DCA_SW_SUBSTREAM_M    0x64582025
#define AML_DCA_SW_SUBSTREAM         0x58642520
#define IEC61937_DTS_HEAD_PTR 20


class Dtshd_Media_Source : public MediaSource{
public:
    Dtshd_Media_Source(void);

    virtual status_t start(MetaData *params = NULL);
    virtual status_t stop();
    virtual sp<MetaData> getFormat();
    virtual status_t read(MediaBuffer **buffer, const ReadOptions *options = NULL);

    //virtual int  GetReadedBytes();
    virtual int GetSampleRate();
    virtual int GetChNum();
    virtual int Get_Stop_ReadBuf_Flag();
    virtual int Set_Stop_ReadBuf_Flag(int pStop);
    virtual int MediaSourceRead_buffer(unsigned char *buffer, int size);

    int mSample_rate;
    int mChNum;
    int mFrame_size;
    int FrameSizeDetectFlag;
    int mStop_ReadBuf_Flag;
    int64_t bytes_readed_sum_pre;
    int64_t bytes_readed_sum;
protected:
    virtual ~Dtshd_Media_Source();

private:
    bool mStarted;
    sp<DataSource> mDataSource;
    sp<MetaData>   mMeta;
    MediaBufferGroup *mGroup;
    int64_t mCurrentTimeUs;
    int     mBytesReaded;
    int     block_align;
    Dtshd_Media_Source(const Dtshd_Media_Source &);
    Dtshd_Media_Source &operator=(const Dtshd_Media_Source &);
};

class Aml_OMX_DtsCodec {
public:

    Aml_OMX_DtsCodec(void);

    OMXClient m_OMXClient;
    sp<Dtshd_Media_Source> m_OMXMediaSource;
    sp<MediaSource> m_codec;

    int read(unsigned char *buf, unsigned *size, int* exit);
    status_t start();
    void pause();
    void stop();
    void lock_init();
    void locked();
    void unlocked();
    int GetDecBytes();

    int64_t buf_decode_offset;
    int64_t buf_decode_offset_pre;
    pthread_mutex_t lock;

    ~Aml_OMX_DtsCodec();
};

}

#endif

