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

#include "audio_effect_control.h"
#include "../audio_amaudio.h"

#define LOG_TAG "LibAudioCtl"

int amAudioOpen(unsigned int sr, int input_device, int output_device) {
    return aml_audio_open(sr, input_device, 2);
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

int amAudioSetEQGain(int gain_val_buf[], int buf_item_cnt) {
    int i = 0;
    int tmp_buf[6] = { 0, 0, 0, 0, 0, 0 };

    if (buf_item_cnt > 5) {
        buf_item_cnt = 5;
    }

    for (i = 0; i < buf_item_cnt; i++) {
        tmp_buf[i] = gain_val_buf[i];
    }

    HPEQ_setParameter(tmp_buf[0], tmp_buf[1], tmp_buf[2], tmp_buf[3],
            tmp_buf[4]);

    return 0;
}

int amAudioGetEQGain(int gain_val_buf[], int buf_item_cnt) {
    int i = 0, tmp_cnt = 0;
    int tmp_buf[6] = { 0, 0, 0, 0, 0, 0 };

    HPEQ_getParameter(tmp_buf);

    tmp_cnt = buf_item_cnt;

    if (buf_item_cnt > 5) {
        tmp_cnt = 5;
    }

    for (i = 0; i < buf_item_cnt; i++) {
        if (i < tmp_cnt) {
            gain_val_buf[i] = tmp_buf[i];
        } else {
            gain_val_buf[i] = 0;
        }
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

static int amAudioSetSRSParameter(int set_type, int gain_val) {
    int tmp_buf[5] = { 0, 0, 0, 0, 0};

    if (srs_getParameter(tmp_buf) < 0) {
        ALOGE("%s, srs_getParameter error.\n", __FUNCTION__);
        return -1;
    }
    tmp_buf[set_type] = gain_val;
    return srs_setParameter(tmp_buf);
}

int amAudioSetSRSGain(int input_gain, int output_gain) {
    return srs_set_gain(input_gain, output_gain);
}

int amAudioSetSRSSurroundSwitch(int switch_val) {
    return srs_surround_enable(switch_val);
}

int amAudioSetSRSSurroundGain(int gain_val) {
    return amAudioSetSRSParameter(CC_SET_TYPE_SURROUND_GAIN, gain_val);
}

int amAudioSetSRSTrubassSwitch(int switch_val) {
    return srs_truebass_enable(switch_val);
}

int amAudioSetSRSTrubassGain(int gain_val) {
    return amAudioSetSRSParameter(CC_SET_TYPE_TRUBASS_GAIN, gain_val);
}

int amAudioSetSRSDialogClaritySwitch(int switch_val) {
    return srs_dialogclarity_enable(switch_val);
}

int amAudioSetSRSDialogClarityGain(int gain_val) {
    return amAudioSetSRSParameter(CC_SET_TYPE_DIALOGCLARITY_GAIN, gain_val);
}

int amAudioSetSRSDefinitionGain(int gain_val) {
    return amAudioSetSRSParameter(CC_SET_TYPE_DEFINITION_GAIN, gain_val);
}

int amAudioSetSRSTrubassSpeakerSize(int tmp_val) {
    int tmp_buf[8] = { 40, 60, 100, 150, 200, 250, 300, 400 };
    int gain_val = 40;

    if (tmp_val >= 0 && tmp_val < (int) sizeof(tmp_buf)) {
        gain_val = tmp_buf[tmp_val];
    }

    return amAudioSetSRSParameter(CC_SET_TYPE_TRUBASS_SPEAKERSIZE, gain_val);
}

