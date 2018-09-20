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

/**
 ** aml_alsa_mixer.c
 **
 ** This program is APIs for read/write mixers of alsa.
 ** author: shen pengru
 **
 */
#include <stdlib.h>
#include <stdint.h>
#include <unistd.h>
#include <errno.h>
#include <cutils/log.h>
#include <fcntl.h>
#include <tinyalsa/asoundlib.h>
#include <aml_hw_profile.h>
#include <aml_alsa_mixer.h>

#undef  LOG_TAG
#define LOG_TAG "audio_alsa_mixer"

static struct aml_mixer_list gAmlMixerList[] = {
    /* for i2s out status */
    {AML_MIXER_ID_I2S_MUTE,             "Audio i2s mute"},
    /* for spdif out status */
    {AML_MIXER_ID_SPDIF_MUTE,           "Audio spdif mute"},
    /* for HDMI TX status */
    {AML_MIXER_ID_HDMI_OUT_AUDIO_MUTE,  "Audio hdmi-out mute"},
    /* for HDMI ARC status */
    {AML_MIXER_ID_HDMI_ARC_AUDIO_ENABLE, "HDMI ARC Switch"},
    {AML_MIXER_ID_AUDIO_IN_SRC,         "Audio In Source"},
    {AML_MIXER_ID_I2SIN_AUDIO_TYPE,     "I2SIN Audio Type"},
    {AML_MIXER_ID_SPDIFIN_AUDIO_TYPE,   "SPDIFIN Audio Type"},
    {AML_MIXER_ID_HW_RESAMPLE_ENABLE,   "Hardware resample enable"},
    {AML_MIXER_ID_OUTPUT_SWAP,          "Output Swap"},
    /* for HDMI RX status */
    {AML_MIXER_ID_HDMI_IN_AUDIO_STABLE, "HDMIIN audio stable"},
    {AML_MIXER_ID_HDMI_IN_SAMPLERATE,   "HDMIIN audio samplerate"},
    {AML_MIXER_ID_HDMI_IN_CHANNELS,     "HDMIIN audio channels"},
    {AML_MIXER_ID_HDMI_IN_FORMATS,      "HDMIIN audio format"},
    {AML_MIXER_ID_HDMI_ATMOS_EDID,      "HDMI ATMOS EDID Switch"},
    /* for ATV status */
    {AML_MIXER_ID_ATV_IN_AUDIO_STABLE,  "ATV audio stable"},
    {AML_MIXER_ID_SPDIF_FORMAT,         "Audio spdif format"},
    /* for AV status */
    {AML_MIXER_ID_AV_IN_AUDIO_STABLE,   "AV audio stable"},
    /* for Speaker master volume */
    {AML_MIXER_ID_EQ_MASTER_VOLUME,     "EQ master volume"},
    /* ARCIN and SPDIFIN switch*/
    {AML_MIXER_ID_SPDIFIN_ARCIN_SWITCH, "AudioIn Switch"}
};

static char *_get_mixer_name_by_id(int mixer_id)
{
    int i;
    int cnt_mixer = sizeof(gAmlMixerList) / sizeof(struct aml_mixer_list);

    for (i = 0; i < cnt_mixer; i++) {
        if (gAmlMixerList[i].id == mixer_id) {
            return gAmlMixerList[i].mixer_name;
        }
    }

    return NULL;
}

static struct mixer *_open_mixer_handle(void)
{
    int card = 0;
    struct mixer *pmixer = NULL;

    card = aml_get_sound_card_main();
    if (card < 0) {
        ALOGE("[%s:%d] Failed to get sound card\n", __FUNCTION__, __LINE__);
        return NULL;
    }

    pmixer = mixer_open(card);
    if (NULL == pmixer) {
        ALOGE("[%s:%d] Failed to open mixer\n", __FUNCTION__, __LINE__);
        return NULL;
    }

    return pmixer;
}

static int _close_mixer_handle(struct mixer *pmixer)
{
    if (NULL != pmixer) {
        mixer_close(pmixer);
    }

    return 0;
}

static struct mixer_ctl *_get_mixer_ctl_handle(struct mixer *pmixer, int mixer_id)
{
    struct mixer_ctl *pCtrl = NULL;

    if (_get_mixer_name_by_id(mixer_id) != NULL) {
        pCtrl = mixer_get_ctl_by_name(pmixer,
                                      _get_mixer_name_by_id(mixer_id));
    }

    return pCtrl;
}

int aml_mixer_ctrl_get_int(int mixer_id)
{
    struct mixer     *pMixer;
    struct mixer_ctl *pCtrl;
    int value = -1;

    pMixer = _open_mixer_handle();
    if (pMixer == NULL) {
        ALOGE("[%s:%d] Failed to open mixer\n", __FUNCTION__, __LINE__);
        return -1;
    }

    pCtrl = _get_mixer_ctl_handle(pMixer, mixer_id);
    if (pCtrl == NULL) {
        ALOGE("[%s:%d] Failed to open mixer %s\n", __FUNCTION__, __LINE__,
              _get_mixer_name_by_id(mixer_id));
        _close_mixer_handle(pMixer);
        return -1;
    }

    value = mixer_ctl_get_value(pCtrl, 0);

    _close_mixer_handle(pMixer);
    return value;
}

int aml_mixer_ctrl_get_enum_str_to_int(int mixer_id, int *ret)
{
    struct mixer     *pMixer;
    struct mixer_ctl *pCtrl;
    const char *string = NULL;
    int value = -1;

    pMixer = _open_mixer_handle();
    if (pMixer == NULL) {
        ALOGE("[%s:%d] Failed to open mixer\n", __FUNCTION__, __LINE__);
        return -1;
    }

    pCtrl = _get_mixer_ctl_handle(pMixer, mixer_id);
    if (pCtrl == NULL) {
        ALOGE("[%s:%d] Failed to open mixer %s\n", __FUNCTION__, __LINE__,
              _get_mixer_name_by_id(mixer_id));
        _close_mixer_handle(pMixer);
        return -1;
    }

    value = mixer_ctl_get_value(pCtrl, 0);
    string = mixer_ctl_get_enum_string(pCtrl, value);
    _close_mixer_handle(pMixer);
    if (string) {
        *ret = atoi(string);
        return 0;
    } else {
        return -1;
    }
}

#if 0
int aml_mixer_ctrl_get_str(int mixer_id, char *value)
{
    struct mixer     *pMixer;
    struct mixer_ctl *pCtrl;

    pMixer = _open_mixer_handle();
    if (pMixer == NULL) {
        ALOGE("[%s:%d] Failed to open mixer\n", __FUNCTION__, __LINE__);
        return -1;
    }

    pCtrl = _get_mixer_ctl_handle(pMixer, mixer_id);
    if (pCtrl == NULL) {
        ALOGE("[%s:%d] Failed to open mixer %s\n", __FUNCTION__, __LINE__,
              _get_mixer_name_by_id(mixer_id));
        _close_mixer_handle(pMixer);
        return -1;
    }

    strcpy(value, mixer_ctl_get_value(pctl, 0));

    _close_mixer_handle(pMixer);
    return 0;
}
#endif

int aml_mixer_ctrl_set_int(int mixer_id, int value)
{
    struct mixer     *pMixer;
    struct mixer_ctl *pCtrl;

    pMixer = _open_mixer_handle();
    if (pMixer == NULL) {
        ALOGE("[%s:%d] Failed to open mixer\n", __FUNCTION__, __LINE__);
        return -1;
    }

    pCtrl = _get_mixer_ctl_handle(pMixer, mixer_id);
    if (pCtrl == NULL) {
        ALOGE("[%s:%d] Failed to open mixer %s\n", __FUNCTION__, __LINE__,
              _get_mixer_name_by_id(mixer_id));
        _close_mixer_handle(pMixer);
        return -1;
    }

    mixer_ctl_set_value(pCtrl, 0, value);

    _close_mixer_handle(pMixer);
    return 0;
}

int aml_mixer_ctrl_set_str(int mixer_id, char *value)
{
    struct mixer     *pMixer;
    struct mixer_ctl *pCtrl;

    pMixer = _open_mixer_handle();
    if (pMixer == NULL) {
        ALOGE("[%s:%d] Failed to open mixer\n", __FUNCTION__, __LINE__);
        return -1;
    }

    pCtrl = _get_mixer_ctl_handle(pMixer, mixer_id);
    if (pCtrl == NULL) {
        ALOGE("[%s:%d] Failed to open mixer %s\n", __FUNCTION__, __LINE__,
              _get_mixer_name_by_id(mixer_id));
        _close_mixer_handle(pMixer);
        return -1;
    }

    mixer_ctl_set_enum_by_string(pCtrl, value);

    _close_mixer_handle(pMixer);
    return 0;
}
