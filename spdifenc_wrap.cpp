//#define LOG_NDEBUG 0
#define LOG_TAG "AudioSPDIF-wrap"
#include <stdint.h>
#include <utils/Log.h>
#include <audio_utils/spdif/SPDIFEncoder.h>
#include <tinyalsa/asoundlib.h>
#include <cutils/properties.h>
static int
getprop_bool(const char *path)
{
    char buf[PROPERTY_VALUE_MAX];
    int ret = -1;
    ret = property_get(path, buf, NULL);
    if (ret > 0) {
        if (strcasecmp(buf, "true") == 0 || strcmp(buf, "1") == 0) {
            return 1;
        }
    }
    return 0;
}
namespace android
{
class MySPDIFEncoder : public SPDIFEncoder
{
public:
    MySPDIFEncoder(struct pcm *mypcm)
        : pcm_handle(mypcm), mTotalBytes(0), eac3_frame(0)
    {};
    virtual ssize_t writeOutput(const void* buffer, size_t bytes)
    {
        ALOGV("write size %d \n", bytes);
#if 1
        if (getprop_bool("media.spdif.outdump")) {
            FILE *fp1 = fopen("/data/tmp/hdmi_audio_out.spdif", "a+");
            if (fp1) {
                int flen = fwrite((char *)buffer, 1, bytes, fp1);
                //LOGFUNC("flen = %d---outlen=%d ", flen, out_frames * frame_size);
                fclose(fp1);
            } else {
                //LOGFUNC("could not open file:/data/hdmi_audio_out.pcm");
            }
        }
#endif
        mTotalBytes += bytes;
        return pcm_write(pcm_handle, buffer, bytes);
    }
    virtual uint64_t total_bytes()
    {
        return mTotalBytes;
    }
protected:
    struct pcm *pcm_handle;
private:
    uint64_t mTotalBytes;
    uint64_t eac3_frame;
};
static MySPDIFEncoder *myencoder = NULL;
extern "C" int spdifenc_init(struct pcm *mypcm)
{
    if (myencoder) {
        delete myencoder;
        myencoder = NULL;
    }
    myencoder = new MySPDIFEncoder(mypcm);
    if (myencoder == NULL) {
        ALOGE("init SPDIFEncoder failed \n");
        return  -1;
    }
    ALOGI("init SPDIFEncoder done\n");
    return 0;
}
extern "C" int  spdifenc_write(const void *buffer, size_t numBytes)
{
    return myencoder->write(buffer, numBytes);
}
extern "C" uint64_t  spdifenc_get_total()
{
    return myencoder->total_bytes();
}
}
