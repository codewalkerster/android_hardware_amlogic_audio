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

#ifndef _AUDIO_HW_H_
#define _AUDIO_HW_H_

#include <audio_utils/resampler.h>
#include <hardware/audio.h>
#include <cutils/list.h>

/* ALSA cards for AML */
#define CARD_AMLOGIC_BOARD 0
#define CARD_AMLOGIC_DEFAULT CARD_AMLOGIC_BOARD
/* ALSA ports for AML */
#if (ENABLE_HUITONG == 0)
#define PORT_MM 1
#endif

#include "audio_hwsync.h"
#include "audio_post_process.h"
#include "aml_hw_profile.h"
#include "aml_hw_mixer.h"
#include "audio_eq_drc_compensation.h"
#include "aml_dcv_dec_api.h"
#include "audio_format_parse.h"
#include "aml_alsa_mixer.h"

/* number of frames per period */
/*
 * change DEFAULT_PERIOD_SIZE from 1024 to 512 for passing CTS
 * test case test4_1MeasurePeakRms(android.media.cts.VisualizerTest)
 */
#if defined(IS_ATOM_PROJECT)
#define DEFAULT_PLAYBACK_PERIOD_SIZE 1024
#else
#define DEFAULT_PLAYBACK_PERIOD_SIZE 512
#endif
#define DEFAULT_CAPTURE_PERIOD_SIZE  1024

/* number of ICE61937 format frames per period */
#define DEFAULT_IEC_SIZE 6144

/* number of periods for low power playback */
#define PLAYBACK_PERIOD_COUNT 4
/* number of periods for capture */
#define CAPTURE_PERIOD_COUNT 4

#ifdef DOLBY_MS12_ENABLE
#include <aml_audio_ms12.h>
#endif

/* minimum sleep time in out_write() when write threshold is not reached */
#define MIN_WRITE_SLEEP_US 5000

#define RESAMPLER_BUFFER_FRAMES (DEFAULT_PLAYBACK_PERIOD_SIZE * 6)
#define RESAMPLER_BUFFER_SIZE (4 * RESAMPLER_BUFFER_FRAMES)

static unsigned int DEFAULT_OUT_SAMPLING_RATE = 48000;

/* sampling rate when using MM low power port */
#define MM_LOW_POWER_SAMPLING_RATE 44100
/* sampling rate when using MM full power port */
#define MM_FULL_POWER_SAMPLING_RATE 48000
/* sampling rate when using VX port for narrow band */
#define VX_NB_SAMPLING_RATE 8000

#define AUDIO_PARAMETER_STREAM_EQ "audioeffect_eq"
#define AUDIO_PARAMETER_STREAM_SRS "audioeffect_srs_param"
#define AUDIO_PARAMETER_STREAM_SRS_GAIN "audioeffect_srs_gain"
#define AUDIO_PARAMETER_STREAM_SRS_SWITCH "audioeffect_srs_switch"

/* Get a new HW synchronization source identifier.
 * Return a valid source (positive integer) or AUDIO_HW_SYNC_INVALID if an error occurs
 * or no HW sync is available. */
#define AUDIO_PARAMETER_HW_AV_SYNC "hw_av_sync"

#define AUDIO_PARAMETER_HW_AV_EAC3_SYNC "HwAvSyncEAC3Supported"

/*
enum {
    TYPE_PCM = 0,
    TYPE_AC3 = 2,
    TYPE_DTS = 3,
    TYPE_EAC3 = 4,
    TYPE_DTS_HD = 5 ,
    TYPE_MULTI_PCM = 6,
    TYPE_TRUE_HD = 7,
    TYPE_DTS_HD_MA = 8,//should not used after we unify DTS-HD&DTS-HD MA
    TYPE_PCM_HIGH_SR = 9,
};
*/

#define AML_HAL_MIXER_BUF_SIZE  64*1024
struct aml_hal_mixer {
    unsigned char start_buf[AML_HAL_MIXER_BUF_SIZE];
    unsigned int wp;
    unsigned int rp;
    unsigned int buf_size;
    /* flag to check if need cache some data before write to mix */
    unsigned char need_cache_flag;
    pthread_mutex_t lock;
};

enum arc_hdmi_format {
    _LPCM = 1,
    _AC3,
    _MPEG1,
    _MP3,
    _MPEG2,
    _AAC,
    _DTS,
    _ATRAC,
    _ONE_BIT_AUDIO,
    _DDP,
    _DTSHD,
    _MAT,
    _DST,
    _WMAPRO
};

struct format_desc {
    enum arc_hdmi_format fmt;
    bool is_support;
    unsigned int max_channels;
    /*
     * bit:    6     5     4    3    2    1    0
     * rate: 192  176.4   96  88.2  48  44.1   32
     */
    unsigned int sample_rate_mask;
    unsigned int max_bit_rate;
    /* only used by dd+ format */
    bool   atmos_supported;
};

struct aml_arc_hdmi_desc {
    unsigned int avr_port;
    struct format_desc pcm_fmt;
    struct format_desc dd_fmt;
    struct format_desc ddp_fmt;
};

enum patch_src_assortion {
    SRC_DTV,
    SRC_ATV,
    SRC_LINEIN,
    SRC_HDMIIN,
    SRC_SPDIFIN,
    SRC_OTHER,
    SRC_INVAL
};

enum OUT_PORT {
    OUTPORT_SPEAKER = 0,
    OUTPORT_HDMI_ARC,
    OUTPORT_HDMI,
    OUTPORT_SPDIF,
    OUTPORT_AUX_LINE,
    OUTPORT_HEADPHONE,
    OUTPORT_MAX
};

enum IN_PORT {
    INPORT_TUNER = 0,
    INPORT_HDMIIN,
    INPORT_SPDIF,
    INPORT_LINEIN,
    INPORT_MAX
};

struct audio_patch_set {
    struct listnode list;
    struct audio_patch audio_patch;
};

typedef enum stream_usecase {
    STREAM_PCM_NORMAL = 0,
    STREAM_PCM_DIRECT,
    STREAM_PCM_HWSYNC,
    STREAM_RAW_DIRECT,
    STREAM_RAW_HWSYNC,
    STREAM_PCM_PATCH,
    STREAM_RAW_PATCH,
    STREAM_USECASE_MAX,
    STREAM_USECASE_INVAL = -1
} stream_usecase_t;

typedef enum alsa_device {
    I2S_DEVICE = 0,
    DIGITAL_DEVICE,
    TDM_DEVICE,
    ALSA_DEVICE_CNT
} alsa_device_t;

enum stream_status {
    STREAM_STANDBY = 0,
    STREAM_HW_WRITING,
    STREAM_MIXING
};

#if defined(IS_ATOM_PROJECT)
typedef enum atom_stream_type {
    STREAM_ANDROID = 0,
    STREAM_HDMI,
    STREAM_OPTAUX
} atom_stream_type_t;
#endif

typedef union {
    unsigned long long timeStamp;
    unsigned char tsB[8];
} aec_timestamp;

#define MAX_STREAM_NUM   5
#define HDMI_ARC_MAX_FORMAT  20
struct aml_audio_device {
    struct audio_hw_device hw_device;
    /* see note below on mutex acquisition order */
    pthread_mutex_t lock;
    pthread_mutex_t pcm_write_lock;
    int mode;
    audio_devices_t in_device;
    audio_devices_t out_device;
    int in_call;
    struct aml_stream_in *active_input;
    struct aml_stream_out *active_output[MAX_STREAM_NUM];
    unsigned char active_output_count;
    bool mic_mute;
    bool speaker_mute;
    unsigned int card;
    struct audio_route *ar;
    struct echo_reference_itfe *echo_reference;
    bool low_power;
    struct aml_stream_out *hwsync_output;
    struct aml_hal_mixer hal_mixer;
    struct pcm *pcm;
    bool pcm_paused;
    unsigned hdmi_arc_ad[HDMI_ARC_MAX_FORMAT];
    bool hi_pcm_mode;
    bool audio_patching;
    /* audio configuration for dolby HDMI/SPDIF output */
    int hdmi_format;
    int spdif_format;
    int hdmi_is_pth_active;
    int disable_pcm_mixing;
    /* mute/unmute for vchip  lock control */
    bool parental_control_av_mute;
    int routing;
    struct audio_config output_config;
    struct aml_arc_hdmi_desc hdmi_descs;
    int arc_hdmi_updated;
    struct aml_native_postprocess native_postprocess;
    /* to classify audio patch sources */
    enum patch_src_assortion patch_src;
    /* for port config infos */
    float sink_gain[OUTPORT_MAX];
    enum OUT_PORT active_outport;
    float src_gain[INPORT_MAX];
    enum IN_PORT active_inport;
    /* message to handle usecase changes */
    bool usecase_changed;
    uint32_t usecase_masks;
    struct aml_stream_out *active_outputs[STREAM_USECASE_MAX];
    struct aml_audio_patch *audio_patch;
    /* Now only two pcm handle supported: I2S, SPDIF */
    pthread_mutex_t alsa_pcm_lock;
    struct pcm *pcm_handle[ALSA_DEVICE_CNT];
    int pcm_refs[ALSA_DEVICE_CNT];
    bool is_paused[ALSA_DEVICE_CNT];
    struct aml_hw_mixer hw_mixer;
    audio_format_t sink_format;
    audio_format_t optical_format;
    volatile int32_t next_unique_ID;
    /* list head for audio_patch */
    struct listnode patch_list;

    void *temp_buf;
    int temp_buf_size;
    int temp_buf_pos;
    bool need_reset_for_dual_decoder;
    bool dual_spdifenc_inited;
    bool dual_decoder_support;
#ifdef DOLBY_MS12_ENABLE
    struct dolby_ms12_desc ms12;
    bool dolby_ms12_status;
    struct pcm_config ms12_config;
    int mixing_level;
    bool associate_audio_mixing_enable;
#endif
    /*used for ac3 eac3 decoder*/
    struct dolby_ddp_dec ddp;
    /**
     * buffer pointer whose data output to headphone
     * buffer size equal to efect_buf_size
     */
    void *hp_output_buf;
    void *effect_buf;
    size_t effect_buf_size;
    bool mix_init_flag;
    struct eq_drc_data eq_data;
    /*used for high pricision A/V from amlogic amadec decoder*/
    unsigned first_apts;
    /*
    first apts flag for alsa hardware prepare,true,need set apts to hw.
    by default it is false as we do not need set the first apts in normal use case.
    */
    bool first_apts_flag;
    struct aml_audio_parser *aml_parser;
    float dts_post_gain;
    bool spdif_encoder_init_flag;
    struct timespec mute_start_ts;
    int spdif_fmt_hw;

#if defined(IS_ATOM_PROJECT)
    struct aml_stream_in *aux_mic_in;
    int mic_running;
    int spk_running;
    ring_buffer_t spk_ring_buf;
    void *spk_buf;
    size_t spk_buf_size;
    size_t spk_write_bytes;
    size_t extra_write_bytes;
    void *output_tmp_buf;
    unsigned int output_tmp_buf_size;

    // spk_buf mgmt
    atom_stream_type_t atom_stream_type_val;
    unsigned long long spk_buf_last_write_time;
    unsigned long long spk_buf_write_count;
    unsigned long long spk_buf_read_count;
    unsigned long long spk_buf_very_first_write_time;

    unsigned long long debug_spk_buf_time_last;
    unsigned long long debug_spk_buf_time_curr;

    bool has_dsp_lib;
    void *aec_buf;
    void *dsp_in_buf;
    size_t dsp_frames;
    void *pstFir_mic;
    void *pstFir_spk;

    pthread_mutex_t aec_spk_mic_lock;
    pthread_mutex_t aec_spk_buf_lock;
    pthread_mutex_t dsp_processing_lock;
#endif

};

struct aml_stream_out {
    struct audio_stream_out stream;
    /* see note below on mutex acquisition order */
    pthread_mutex_t lock;
    /* config which set to ALSA device */
    struct pcm_config config;
    /* channel mask exposed to AudioFlinger. */
    audio_channel_mask_t hal_channel_mask;
    /* format mask exposed to AudioFlinger. */
    audio_format_t hal_format;
    /* samplerate exposed to AudioFlinger. */
    unsigned int hal_rate;
    audio_output_flags_t flags;
    audio_devices_t out_device;
    struct pcm *pcm;
    struct resampler_itfe *resampler;
    char *buffer;
    size_t buffer_frames;
    bool standby;
    struct aml_audio_device *dev;
    int write_threshold;
    bool low_power;
    unsigned multich;
    int codec_type;
    uint64_t frame_write_sum;
    uint64_t frame_skip_sum;
    uint64_t last_frames_postion;
    uint64_t spdif_enc_init_frame_write_sum;
    int skip_frame;
    int32_t *tmp_buffer_8ch;
    size_t tmp_buffer_8ch_size;
    int is_tv_platform;
    void *audioeffect_tmp_buffer;
    bool pause_status;
    bool hw_sync_mode;
    float volume_l;
    float volume_r;
    int last_codec_type;
    /**
     * as raw audio framesize  is 1 computed by audio_stream_out_frame_size
     * we need divide more when we got 61937 audio package
     */
    int raw_61937_frame_size;
    /* recorded for wraparound print info */
    unsigned last_dsp_frame;
    audio_hwsync_t hwsync;
    struct timespec timestamp;
    stream_usecase_t usecase;
    uint32_t dev_usecase_masks;
    /**
     * flag indicates that this stream need do mixing
     * int is_in_mixing: 1;
     * Normal pcm may not hold alsa pcm device.
     */
    int is_normal_pcm;
    unsigned int card;
    alsa_device_t device;
    ssize_t (*write)(struct audio_stream_out *stream, const void *buffer, size_t bytes);
    enum stream_status status;
    audio_format_t hal_internal_format;
    bool dual_output_flag;
    uint64_t input_bytes_size;
    uint64_t continuous_audio_offset;
    bool hwsync_pcm_config;
    bool hwsync_raw_config;
    bool direct_raw_config;
    bool is_device_differ_with_ms12;
    uint64_t total_write_size;
    int  ddp_frame_size;
    int dropped_size;
    unsigned long long mute_bytes;
    bool is_get_mute_bytes;
    size_t frame_deficiency;
};

typedef ssize_t (*write_func)(struct audio_stream_out *stream, const void *buffer, size_t bytes);

#define MAX_PREPROCESSORS 3 /* maximum one AGC + one NS + one AEC per input stream */
struct aml_stream_in {
    struct audio_stream_in stream;
    pthread_mutex_t lock;       /* see note below on mutex acquisition order */
    struct pcm_config config;
    struct pcm *pcm;
    int device;
    audio_channel_mask_t hal_channel_mask;
    audio_format_t hal_format;
    struct resampler_itfe *resampler;
    struct resampler_buffer_provider buf_provider;
    int16_t *buffer;
    size_t frames_in;
    unsigned int requested_rate;
    bool standby;
    int source;
    struct echo_reference_itfe *echo_reference;
    bool need_echo_reference;
    effect_handle_t preprocessors[MAX_PREPROCESSORS];
    int num_preprocessors;
    int read_status;
    /* HW parser audio format */
    int spdif_fmt_hw;
    /* SW parser audio format */
    audio_format_t spdif_fmt_sw;
    struct timespec mute_start_ts;
    int mute_flag;
    int mute_log_cntr;
    struct aml_audio_device *dev;

#if defined(IS_ATOM_PROJECT)
    int ref_count;
    void *aux_buf;
    size_t aux_buf_size;
    size_t aux_buf_write_bytes;
    void *mic_buf;
    size_t mic_buf_size;
    void *tmp_buffer_8ch;
    size_t tmp_buffer_8ch_size;
    pthread_mutex_t aux_mic_mutex;
    pthread_cond_t aux_mic_cond;
#endif
};
typedef  int (*do_standby_func)(struct aml_stream_out *out);
typedef  int (*do_startup_func)(struct aml_stream_out *out);

/*
 *@brief get_output_format get the output format always return the "sink_format" of adev
 */
audio_format_t get_output_format(struct audio_stream_out *stream);

ssize_t aml_audio_spdif_output(struct audio_stream_out *stream,
                const void *buffer, size_t bytes);

/*
 *@brief audio_hal_data_processing
 * format:
 *    if pcm-16bits-stereo, add audio effect process, and mapping to 8ch
 *    if raw data, packet it to IEC61937 format with spdif encoder
 *    if IEC61937 format, write them to hardware
 * return
 *    0, success
 *    -1, fail
 */
ssize_t audio_hal_data_processing(struct audio_stream_out *stream
    , const void *input_buffer
    , size_t input_buffer_bytes
    , void **output_buffer
    , size_t *output_buffer_bytes
    , audio_format_t output_format);

/*
 *@brief hw_write the api to write the data to audio hardware
 */
ssize_t hw_write(struct audio_stream_out *stream
    , const void *buffer
    , size_t bytes
    , audio_format_t output_format);
#endif
