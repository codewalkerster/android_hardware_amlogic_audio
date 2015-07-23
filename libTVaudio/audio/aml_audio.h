#ifndef __TV_AML_AUDIO_H__
#define __TV_AML_AUDIO_H__

#include "audio_effect_control.h"
#include "audio_usb_check.h"

/*
 In this system, input device is always ALSA_in, but There are 3 devices outpurt.
 For TV application, AMAUDIO OUT is suggested. This mode has low latency.
 IF HMDI/SPDIF has raw data input,  ANDROID OUT is suggested.
 */
#define CC_OUT_USE_AMAUDIO       (0)
#define CC_OUT_USE_ALSA          (1)
#define CC_OUT_USE_ANDROID       (2)

/*
 mix mode:
 DIRECT, input is direct to output, cover the local sound.
 INTER_MIX, left and right input channels mix first as one channel, and then mix with local sound.
 DIRECT_MIX,left input channel mix with local left channel, right input channel mix with local right channel
 */
#define CC_OUT_MODE_DIRECT       (0)
#define CC_OUT_MODE_INTER_MIX    (1)
#define CC_OUT_MODE_DIRECT_MIX   (2)

/*
 There are two input device, spdif or i2s.
 */
#define CC_IN_USE_I2S_DEVICE     (0)
#define CC_IN_USE_SPDIF_DEVICE   (1)

#define CC_SINGLE_OUTPUT     (0)
#define CC_DOUBLE_OUTPUT     (1)

#ifdef __cplusplus
extern "C" {
#endif

struct circle_buffer {
    pthread_mutex_t lock;
    char *start_add;
    char *rd;
    char *wr;
    int size;
};

/*
 In this system, input stream sample rate can be set from 8K-48K, and output sample rate is fixed 48K.
 When input stream sample rate is different from output, inlined reample is in operation. If input stream sr is not set,
 the input stream is default 48K.
 */
int GetOutputdevice(void);
int aml_audio_open(unsigned int sr, int input_device, int output_device);
int aml_audio_close(void);
int check_input_stream_sr(unsigned int sr);
int SetDumpDataFlag(int tmp_flag);
int GetDumpDataFlag();
int set_output_mode(int mode);
int set_music_gain(int gain);
int set_left_gain(int left_gain);
int set_right_gain(int right_gain);
int buffer_read(struct circle_buffer *tmp, char* buffer, size_t bytes);
int buffer_write(struct circle_buffer *tmp, char* buffer, size_t bytes);
int getUSBCheckFlag();
int set_android_volume_enable(int enable);
int set_android_volume(int left, int right);
int set_output_record_enable(int enable);

#ifdef __cplusplus
}
#endif

#endif //__TV_AML_AUDIO_H__
