#ifndef __DDP_MEDIA_SOURCE_H__
#define __DDP_MEDIA_SOURCE_H__

#include  "MediaSource.h"
#include  "DataSource.h"
#include  "MediaBufferGroup.h"
#include  "MetaData.h"
#include  "OMXCodec.h"
#include  "OMX_Index.h"
#include  "OMX_Core.h"
#include  "OMXClient.h"

namespace android {

#define     DDPshort short
#define     DDPerr   short
#define     DDPushort  unsigned short
#define     BYTESPERWRD         2
#define     BITSPERWRD          (BYTESPERWRD*8)
#define     SYNCWRD             ((DDPshort)0x0b77)
#define     MAXFSCOD            3
#define     MAXDDDATARATE       38
#define     BS_STD            8
#define     ISDD(bsid)          ((bsid) <= BS_STD)
#define     MAXCHANCFGS         8
#define     BS_AXE            16
#define     ISDDP(bsid)         ((bsid) <= BS_AXE && (bsid) > 10)
#define     BS_BITOFFSET      40
#define     PTR_HEAD_SIZE 7//20


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
        0xfffe, 0xffff };
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

//--------------------------------------------------------------------------------
class DDP_Media_Source: public MediaSource {
public:
    DDP_Media_Source(void);

    virtual status_t start(MetaData *params = NULL);
    virtual status_t stop();
    virtual sp<MetaData> getFormat();
    virtual status_t read(MediaBuffer **buffer, const ReadOptions *options =
            NULL);

    virtual int GetSampleRate();
    virtual int GetChNum();

    virtual int Get_Stop_ReadBuf_Flag();
    virtual int Set_Stop_ReadBuf_Flag(int StopFlag);
    virtual int MediaSourceRead_buffer(unsigned char *buffer, int size);

protected:
    virtual ~DDP_Media_Source();

private:

    bool mStarted;
    sp<MetaData> mMeta;
    MediaBufferGroup *mGroup;
    int mSample_rate;
    int mChNum;
    int mFrame_size;
    int mStop_ReadBuf_Flag;

    DDP_Media_Source(const DDP_Media_Source &);
    DDP_Media_Source &operator=(const DDP_Media_Source &);
};

//----------------------------------------------------------------------------------
class Aml_OMX_Codec {
public:

    Aml_OMX_Codec(void);

    OMXClient m_OMXClient;
    sp<DDP_Media_Source> m_OMXMediaSource;
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

    ~Aml_OMX_Codec();
};

}

#endif
