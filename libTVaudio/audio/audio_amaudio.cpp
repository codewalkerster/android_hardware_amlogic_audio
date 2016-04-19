#include <stdbool.h>
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <stdint.h>
#include <signal.h>
#include <pthread.h>
#include <unistd.h>
#include <math.h>
#include <fcntl.h>
#include <sys/mman.h>
#include <sys/ioctl.h>
#include <sys/stat.h>
#include <dlfcn.h>

#include <cutils/log.h>
#include <cutils/properties.h>
#include <tinyalsa/asoundlib.h>
#include <hardware/audio.h>

#include "audio_usb_check.h"
#include "audio_effect_control.h"
#include "../audio_amaudio.h"

#define LOG_TAG "LibAudioCtl"

int amSetAudioDelay(int delay_ms) {
    return set_audio_delay(delay_ms);
}

int amGetAudioDelay(void) {
    return get_audio_delay();
}

int amAudioOpen(unsigned int sr, int input_device, int output_device) {
    int ret;
    ret = aml_audio_open(sr, input_device, output_device);
    return ret;
}

int amAudioClose(void) {
    return aml_audio_close();
}

int amAudioSetInputSr(unsigned int sr, int input_device, int output_device) {
    int tmpRet = 0;

    if (check_input_stream_sr(sr) < 0) {
        ALOGE("%s, The sample rate (%u) is invalid!\n", __FUNCTION__, sr);
        return -1;
    }

    tmpRet |= amAudioClose();
    tmpRet |= amAudioOpen(sr, input_device, output_device);

    return tmpRet;
}

int amAudioSetDumpDataFlag(int tmp_flag) {
    return SetDumpDataFlag(tmp_flag);
}

int amAudioGetDumpDataFlag() {
    return GetDumpDataFlag();
}

int amAudioSetOutputMode(int mode) {
    return set_output_mode(mode);
}

int amAudioSetMusicGain(int gain) {
    return set_music_gain(gain);
}

int amAudioSetLeftGain(int gain) {
    return set_left_gain(gain);
}

int amAudioSetRightGain(int gain) {
    return set_right_gain(gain);
}

int amAudioSetEQGain(int gain_val_buf[], int buf_item_cnt __unused) {
    int i = 0, ret = 0;
    int tmp_buf[5] = { 0, 0, 0, 0, 0};

    for (i = 0; i < 5; i++) {
        tmp_buf[i] = gain_val_buf[i];
    }

    HPEQ_setParameter(tmp_buf[0], tmp_buf[1], tmp_buf[2], tmp_buf[3],
            tmp_buf[4]);

    char param[15];
    char parm_key[] = AUDIO_PARAMETER_STREAM_EQ;
    sprintf(param, "%02d%02d%02d%02d%02d", tmp_buf[0]+10, tmp_buf[1]+10,
                            tmp_buf[2]+10, tmp_buf[3]+10, tmp_buf[4]+10);

    ret = set_parameters(param, parm_key);
    //ALOGE("EQ param : %s\n", param);
    return ret;
}

int amAudioGetEQGain(int gain_val_buf[], int buf_item_cnt __unused) {
    int i = 0;
    int tmp_buf[5] = { 0, 0, 0, 0, 0};

    HPEQ_getParameter(tmp_buf);

    for (i = 0; i < 5; i++) {
        gain_val_buf[i] = tmp_buf[i];
    }
    return 0;
}

int amAudioSetEQEnable(int en_val) {
    return HPEQ_enable(en_val);
}

int amAudioGetEQEnable() {
    return 0;
}

#define CC_SET_TYPE_TRUBASS_SPEAKERSIZE     (0)
#define CC_SET_TYPE_TRUBASS_GAIN            (1)
#define CC_SET_TYPE_DIALOGCLARITY_GAIN      (2)
#define CC_SET_TYPE_DEFINITION_GAIN         (3)
#define CC_SET_TYPE_SURROUND_GAIN           (4)
#define CC_SET_TYPE_MAX                     (5)



int srs_param_buf[5] = { 0, 0, 0, 0, 0};
static int amAudioSetSRSParameter(int set_type, int gain_val) {
    int ret = 0;

    srs_param_buf[set_type] = gain_val;
    ret = srs_setParameter(srs_param_buf);

    char param[20];
    char parm_key[] = AUDIO_PARAMETER_STREAM_SRS;

    sprintf(param, "%03d%03d%03d%03d%03d", srs_param_buf[0],
                        srs_param_buf[1], srs_param_buf[2],
                        srs_param_buf[3], srs_param_buf[4]);
    ret = set_parameters(param, parm_key);
    return ret;
}

int amAudioSetSRSGain(int input_gain, int output_gain) {
    int ret = 0;
    ret = srs_set_gain(input_gain, output_gain);

    char param[10];
    char parm_key[] = AUDIO_PARAMETER_STREAM_SRS_GAIN;
    sprintf(param, "%03d%03d", input_gain, output_gain);
    ret = set_parameters(param, parm_key);

    return ret;
}

#define CC_SET_SWITCH_SURROUND              (0)
#define CC_SET_SWITCH_DIALOGCLARITY         (1)
#define CC_SET_SWITCH_TRUBASS               (2)
int srs_switch[3]= {0, 0, 0};

int amAudioSetSRSSurroundSwitch(int switch_val) {
    int ret = 0;
    ret = srs_surround_enable(switch_val);

    srs_switch[CC_SET_SWITCH_SURROUND] = switch_val;
    char param[10];
    char parm_key[] = AUDIO_PARAMETER_STREAM_SRS_SWITCH;
    sprintf(param, "%02d%02d%02d", srs_switch[0], srs_switch[1], srs_switch[2]);
    ret = set_parameters(param, parm_key);

    return ret;
}

int amAudioSetSRSDialogClaritySwitch(int switch_val) {
    int ret = 0;
    ret = srs_dialogclarity_enable(switch_val);

    srs_switch[CC_SET_SWITCH_DIALOGCLARITY] = switch_val;
    char param[10];
    char parm_key[] = AUDIO_PARAMETER_STREAM_SRS_SWITCH;
    sprintf(param, "%02d%02d%02d", srs_switch[0], srs_switch[1], srs_switch[2]);
    ret = set_parameters(param, parm_key);

    return ret;
}

int amAudioSetSRSTrubassSwitch(int switch_val) {
    int ret = 0;
    ret = srs_truebass_enable(switch_val);

    srs_switch[CC_SET_SWITCH_TRUBASS] = switch_val;
    char param[10];
    char parm_key[] = AUDIO_PARAMETER_STREAM_SRS_SWITCH;
    sprintf(param, "%02d%02d%02d", srs_switch[0], srs_switch[1], srs_switch[2]);
    ret = set_parameters(param, parm_key);

    return ret;
}

int amAudioSetSRSSurroundGain(int gain_val) {
    return amAudioSetSRSParameter(CC_SET_TYPE_SURROUND_GAIN, gain_val);
}

int amAudioSetSRSTrubassGain(int gain_val) {
    return amAudioSetSRSParameter(CC_SET_TYPE_TRUBASS_GAIN, gain_val);
}

int amAudioSetSRSDialogClarityGain(int gain_val) {
    return amAudioSetSRSParameter(CC_SET_TYPE_DIALOGCLARITY_GAIN, gain_val);
}

int amAudioSetSRSDefinitionGain(int gain_val) {
    return amAudioSetSRSParameter(CC_SET_TYPE_DEFINITION_GAIN, gain_val);
}

int amAudioSetSRSTrubassSpeakerSize(int tmp_val) {
    return amAudioSetSRSParameter(CC_SET_TYPE_TRUBASS_SPEAKERSIZE, tmp_val);
}

