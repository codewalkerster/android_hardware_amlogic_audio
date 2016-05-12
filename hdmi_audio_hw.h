#ifndef _HDMI_AUDIO_HW_H_
#define _HDMI_AUDIO_HW_H_


// add compute hdmi in volume function, volume index between 0 and 15 ,this reference AudioPolicyManagerBase

enum
{ VOLMIN = 0, VOLKNEE1 = 1, VOLKNEE2 = 2, VOLMAX = 3, VOLCNT = 4 };

struct VolumeCurvePoint {
    int mIndex;
    float mDBAttenuation;
};

static struct VolumeCurvePoint sSpeakerMediaVolumeCurve[VOLCNT] = {
    {1, -56.0f}, {20, -34.0f}, {60, -11.0f}, {100, 0.0f}
};

// stream descriptor used for volume control
struct StreamDescriptor {
    int mIndexMin;        // min volume index
    int mIndexMax;        // max volume index
    int mIndexCur[15];        // current volume index
    struct VolumeCurvePoint *mVolumeCurve;
};
enum {
    TYPE_PCM = 0,
    TYPE_AC3 = 2,
    TYPE_DTS = 3,
    TYPE_EAC3 = 4,
    TYPE_DTS_HD = 5 ,
    TYPE_TRUE_HD = 7,
};

struct aml_audio_device {
    struct audio_hw_device hw_device;
    pthread_mutex_t lock;       /* see note below on mutex acquisition order */
    int mode;
    audio_devices_t in_device;
    audio_devices_t out_device;
    int in_call;
    struct aml_stream_in *active_input;
    struct aml_stream_out *active_output;
    bool mic_mute;
    struct echo_reference_itfe *echo_reference;
    bool hw_sync_mode;
    audio_hwsync_t  hwsync;
};

struct aml_stream_out {
    struct audio_stream_out stream;
    pthread_mutex_t lock;       /* see note below on mutex acquisition order */
    struct pcm_config config;
    struct pcm *pcm;
    char *buffer;
    int standby;
    struct aml_audio_device *dev;
    int write_threshold;
    unsigned multich;
    int codec_type;
    int last_codec_type;
    int format;
    uint64_t frame_write_sum;
    uint64_t frame_skip_sum;	
    audio_output_flags_t flags;
    uint64_t spdif_enc_init_frame_write_sum;
    uint64_t last_frames_postion;
    uint64_t bytes_write_total;
    unsigned char pause_status;
    int skip_frame;
};

struct aml_stream_in {
    struct audio_stream_in stream;
    pthread_mutex_t lock;       /* see note below on mutex acquisition order */
    struct pcm_config config;
    struct pcm *pcm;
    int device;
    int16_t *buffer;
    size_t frames_in;
    int standby;
    int source;
    bool need_echo_reference;
    int requested_rate;
    struct aml_audio_device *dev;
};
#endif
