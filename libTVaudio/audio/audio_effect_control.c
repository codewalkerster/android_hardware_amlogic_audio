/*
 audio_effect_control.c
 */
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <stdbool.h>
#include <dlfcn.h>

#include <cutils/log.h>

#include "aml_audio.h"
#include "audio_effect_control.h"

#define LOG_TAG "effect_ctl"

//------------------------------------EQ control------------------------------------------------
int (*EQ_process)(short *in, short *out, int framecount);
int (*EQ_init)(void);
int (*EQ_setParameter)(int band1, int band2, int band3, int band4, int band5);
int (*EQ_getParameter)(int *band1, int *band2, int *band3, int *band4,
        int *band5);
int (*EQ_enable)(bool enable);
int (*EQ_release)(void);

static void *gEQLibHandler = NULL;

int unload_EQ_lib(void) {
    /*ALOGD("%s, entering...\n", __FUNCTION__);*/
    HPEQ_release();

    EQ_process = NULL;
    EQ_init = NULL;
    EQ_setParameter = NULL;
    EQ_getParameter = NULL;
    EQ_enable = NULL;
    EQ_release = NULL;

    if (gEQLibHandler != NULL) {
        dlclose(gEQLibHandler);
        gEQLibHandler = NULL;
    }

    return 0;
}

int load_EQ_lib(void) {
    char *error;

    unload_EQ_lib();

    /*ALOGD("%s, entering...\n", __FUNCTION__);*/

    gEQLibHandler = dlopen("/system/lib/soundfx/libhpeq.so", RTLD_NOW);
    if (!gEQLibHandler) {
        ALOGE("%s, failed to load EQ lib (libhpeq.so)\n", __FUNCTION__);
        goto Error;
    }

    EQ_release = (int (*)(void))dlsym(gEQLibHandler, "HPEQ_release_api");
    if (EQ_release == NULL) {
        ALOGE("%s, fail find fun HPEQ_release_api()\n", __FUNCTION__);
        goto Error;
    }
    EQ_process = (int (*)(short*, short*,
            int))dlsym(gEQLibHandler, "HPEQ_process_api");
    if (EQ_process == NULL) {
        ALOGE("%s, fail find fun HPEQ_process_api()\n", __FUNCTION__);
        goto Error;
    }
    EQ_init = (int (*)(void))dlsym(gEQLibHandler, "HPEQ_init_api");
    if (EQ_init == NULL) {
        ALOGE("%s, fail find fun HPEQ_init_api()\n", __FUNCTION__);
        goto Error;
    }
    EQ_setParameter = (int (*)(int, int, int, int,
            int))dlsym(gEQLibHandler, "HPEQ_setParameter_api");
    if (EQ_setParameter == NULL) {
        ALOGE("%s, fail find fun HPEQ_setParameter_api\n", __FUNCTION__);
        goto Error;
    }
    EQ_getParameter = (int (*)(int*, int*, int*, int*,
            int*))dlsym(gEQLibHandler, "HPEQ_getParameter_api");
    if (EQ_getParameter == NULL) {
        ALOGE("%s, fail find fun HPEQ_getParameter_api()\n", __FUNCTION__);
        goto Error;
    }
    EQ_enable = (int (*)(bool))dlsym(gEQLibHandler, "HPEQ_enable_api");
    if (EQ_enable == NULL) {
        ALOGE("%s, fail find fun HPEQ_enable_api()\n", __FUNCTION__);
        goto Error;
    }

    return 0;

    Error:
    unload_EQ_lib();
    return -1;
}

int HPEQ_process(short *in, short *out, int framecount) {
    int ret = 0;

    if (EQ_process == NULL) {
        ALOGE("%s, pls load lib first.\n", __FUNCTION__);
        return -1;
    }

    ret = (*EQ_process)(in, out, framecount);

    return ret;
}

int HPEQ_init(void) {
    int ret = 0;

    if (EQ_init == NULL) {
        ALOGE("%s, pls load lib first.\n", __FUNCTION__);
        return -1;
    }

    ret = (*EQ_init)();

    return ret;
}

int HPEQ_setParameter(int band1, int band2, int band3, int band4, int band5) {
    int ret = 0;

    if (EQ_setParameter == NULL) {
        ALOGE("%s, pls load lib first.\n", __FUNCTION__);
        return -1;
    }

    ret = (*EQ_setParameter)(band1, band2, band3, band4, band5);

    return ret;
}
int HPEQ_getParameter(int EQ_user_config[]) {
    int ret = 0;

    if (EQ_getParameter == NULL) {
        ALOGE("%s, pls load lib first.\n", __FUNCTION__);
        return -1;
    }

    (*EQ_getParameter)(&EQ_user_config[0], &EQ_user_config[1],
            &EQ_user_config[2], &EQ_user_config[3], &EQ_user_config[4]);

    return 0;
}

int HPEQ_enable(bool enable) {
    int ret = 0;

    if (EQ_enable == NULL) {
        ALOGE("%s, pls load lib first.\n", __FUNCTION__);
        return -1;
    }

    ret = (*EQ_enable)(enable);

    return ret;
}

int HPEQ_release(void) {
    int ret = 0;

    if (EQ_release == NULL) {
        ALOGE("%s, pls load lib first.\n", __FUNCTION__);
        return -1;
    }

    ret = (*EQ_release)();

    return ret;
}

//-------------------------SRS control---------------------------------------------------------
int (*SRS_init)(int sample_rate);
int (*SRS_release)(void);
int (*SRS_setParameter)(int truebass_spker_size, float truebass_gain,
        float dialogclarity_gain, float definition_gain, float surround_gain);
int (*SRS_getParameter)(int *truebass_spker_size, float *truebass_gain,
        float *dialogclarity_gain, float *definition_gain,
        float *surround_gain);
int (*SRS_TRUEBASS_ENABLE)(int value);
int (*SRS_DIALOGCLARITY_ENABLE)(int value);
int (*SRS_SURROUND_ENABLE)(int value);
int (*SRS_process)(short *in, short *out, int framecount);
int (*SRS_set_gain)(float input_gain, float output_gain);
static void *gSRSLibHandler = NULL;

int unload_SRS_lib(void) {
    /*ALOGD("%s, entering...\n", __FUNCTION__);*/

    srs_release();

    SRS_release = NULL;
    SRS_process = NULL;
    SRS_init = NULL;
    SRS_setParameter = NULL;
    SRS_getParameter = NULL;
    SRS_TRUEBASS_ENABLE = NULL;
    SRS_DIALOGCLARITY_ENABLE = NULL;
    SRS_SURROUND_ENABLE = NULL;
    SRS_set_gain = NULL;
    if (gSRSLibHandler != NULL) {
        dlclose(gSRSLibHandler);
        gSRSLibHandler = NULL;
    }

    return 0;
}

int load_SRS_lib(void) {
    char *error;

    unload_SRS_lib();

    /*ALOGD("%s, entering...\n", __FUNCTION__);*/

    gSRSLibHandler = dlopen("/system/lib/soundfx/libsrs.so", RTLD_NOW);
    if (!gSRSLibHandler) {
        ALOGE("%s, failed to load SRS lib (libsrs.so)\n", __FUNCTION__);
        goto Error;
    }

    SRS_release = (int (*)(void))dlsym(gSRSLibHandler, "SRS_release_api");
    if (SRS_release == NULL) {
        ALOGE("%s, fail find fun SRS_release_api()\n", __FUNCTION__);
        goto Error;
    }
    SRS_process = (int (*)(short*, short*,
            int))dlsym(gSRSLibHandler, "SRS_process_api");
    if (SRS_process == NULL) {
        ALOGE("%s, fail find fun SRS_process_api()\n", __FUNCTION__);
        goto Error;
    }
    SRS_init = (int (*)(int))dlsym(gSRSLibHandler, "SRS_init_api");
    if (SRS_init == NULL) {
        ALOGE("%s, fail find fun SRS_init_api()\n", __FUNCTION__);
        goto Error;
    }
    SRS_setParameter = (int (*)(int, float, float, float,
            float))dlsym(gSRSLibHandler, "SRS_setParameter_api");
    if (SRS_setParameter == NULL) {
        ALOGE("%s, fail find fun SRS_setParameter_api()\n", __FUNCTION__);
        goto Error;
    }
    SRS_getParameter = (int (*)(int*, float*, float*, float*,
            float*))dlsym(gSRSLibHandler, "SRS_getParameter_api");
    if (SRS_getParameter == NULL) {
        ALOGE("%s, fail find fun SRS_getParameter_api()\n", __FUNCTION__);
        goto Error;
    }
    SRS_TRUEBASS_ENABLE = (int (*)(
            int))dlsym(gSRSLibHandler, "SRS_TRUEBASS_ENABLE_api");
    if (SRS_TRUEBASS_ENABLE == NULL) {
        ALOGE("%s, fail find fun SRS_TRUEBASS_ENABLE_api()\n", __FUNCTION__);
        goto Error;
    }
    SRS_DIALOGCLARITY_ENABLE = (int (*)(
            int))dlsym(gSRSLibHandler, "SRS_DIALOGCLARITY_ENABLE_api");
    if (SRS_DIALOGCLARITY_ENABLE == NULL) {
        ALOGE("%s, fail find fun SRS_DIALOGCLARITY_ENABLE_api\n", __FUNCTION__);
        goto Error;
    }
    SRS_SURROUND_ENABLE = (int (*)(
            int))dlsym(gSRSLibHandler, "SRS_SURROUND_ENABLE_api");
    if (SRS_SURROUND_ENABLE == NULL) {
        ALOGE("%s, fail find fun SRS_SURROUND_ENABLE_api\n", __FUNCTION__);
        goto Error;
    }
    SRS_set_gain = (int (*)(float,float))dlsym(gSRSLibHandler, "SRS_set_gain_api");
    if (SRS_set_gain == NULL) {
        ALOGE("%s, fail find fun SRS_set_gain_api\n", __FUNCTION__);
        goto Error;
    }
    return 0;
Error:
    unload_SRS_lib();
    return -1;
}

int srs_init(int sample_rate) {
    int ret = 0;

    if (SRS_init == NULL) {
        ALOGE("%s, pls load lib first.\n", __FUNCTION__);
        return -1;
    }

    ret = (*SRS_init)(sample_rate);

    return ret;
}

int srs_release(void) {
    int ret = 0;

    if (SRS_release == NULL) {
        ALOGE("%s, pls load lib first.\n", __FUNCTION__);
        return -1;
    }

    ret = (*SRS_release)();

    return ret;
}

int srs_setParameter(int SRS_user_config[]) {
    int ret = 0;
    int truebass_spker_size;
    float truebass_gain;
    float dialogclarity_gain;
    float definition_gain;
    float surround_gain;

    if (SRS_setParameter == NULL) {
        ALOGE("%s, pls load lib first.\n", __FUNCTION__);
        return -1;
    }

    if (SRS_user_config == NULL) {
        ALOGE("%s, SRS_user_config buf pointer is NULL.\n", __FUNCTION__);
        return -1;
    }

    truebass_spker_size = SRS_user_config[0];
    truebass_gain = (float) SRS_user_config[1] / (float) 100;
    dialogclarity_gain = (float) SRS_user_config[2] / (float) 100;
    definition_gain = (float) SRS_user_config[3] / (float) 100;
    surround_gain = (float) SRS_user_config[4] / (float) 100;

    ret = (*SRS_setParameter)(truebass_spker_size, truebass_gain,
            dialogclarity_gain, definition_gain, surround_gain);

    return ret;
}

int srs_getParameter(int SRS_user_config[]) {
    int ret = 0;
    int truebass_spker_size;
    float truebass_gain;
    float dialogclarity_gain;
    float definition_gain;
    float surround_gain;

    if (SRS_getParameter == NULL) {
        ALOGE("%s, pls load lib first.\n", __FUNCTION__);
        return -1;
    }

    if (SRS_user_config == NULL) {
        ALOGE("%s, SRS_user_config buf pointer is NULL.\n", __FUNCTION__);
        return -1;
    }

    ret = (*SRS_getParameter)(&truebass_spker_size, &truebass_gain,
            &dialogclarity_gain, &definition_gain, &surround_gain);

    SRS_user_config[0] = truebass_spker_size;
    SRS_user_config[1] = (int) (truebass_gain * 100);
    SRS_user_config[2] = (int) (dialogclarity_gain * 100);
    SRS_user_config[3] = (int) (definition_gain * 100);
    SRS_user_config[4] = (int) (surround_gain * 100);

    return 0;
}

int srs_set_gain(int input_gain, int output_gain) {
    int ret = 0;
    if (SRS_set_gain == NULL) {
        ALOGE("%s, pls load lib first.\n", __FUNCTION__);
        return -1;
    }
    ret = (*SRS_set_gain)((float)input_gain/(float)100, (float)output_gain/(float)100);
    return ret;
}

int srs_truebass_enable(int enable) {
    int ret = 0;

    if (SRS_TRUEBASS_ENABLE == NULL) {
        ALOGE("%s, pls load lib first.\n", __FUNCTION__);
        return -1;
    }

    ret = (*SRS_TRUEBASS_ENABLE)(enable);

    return ret;
}

int srs_dialogclarity_enable(int enable) {
    int ret = 0;

    if (SRS_DIALOGCLARITY_ENABLE == NULL) {
        ALOGE("%s, pls load lib first.\n", __FUNCTION__);
        return -1;
    }

    ret = (*SRS_DIALOGCLARITY_ENABLE)(enable);

    return ret;
}

int srs_surround_enable(int enable) {
    int ret = 0;

    if (SRS_SURROUND_ENABLE == NULL) {
        ALOGE("%s, pls load lib first.\n", __FUNCTION__);
        return -1;
    }

    ret = (*SRS_SURROUND_ENABLE)(enable);

    return ret;
}

int srs_process(short *in, short *out, int framecount) {
    int output_framecount = 0;

    //In SRS prosess, framecount must be aligned by 64.
    int input_framecount = framecount >> 6 << 6;

    if (SRS_process == NULL) {
        ALOGE("%s, pls load lib first.\n", __FUNCTION__);
        return -1;
    }

    output_framecount = (*SRS_process)(in, out, input_framecount);

    return output_framecount << 2;
}

//----------------------aml_IIR-------------------------------------------------
int (*audio_IIR_process_api)(int input, int channel);
void (*audio_IIR_init_api)(int param_index);
static void *gAML_IIR_LibHandler = NULL;

int aml_IIR_process(int input, int channel) {
    int ret = 0;
    if (audio_IIR_process_api == NULL) {
        return input;
    }
    ret = (*audio_IIR_process_api)(input, channel);
    return ret;
}

void aml_IIR_init(int param_index) {
    if (audio_IIR_init_api == NULL) {
        return;
    }
    (*audio_IIR_init_api)(param_index);
    return;
}

int unload_aml_IIR_lib(void) {
    audio_IIR_process_api = NULL;
    audio_IIR_init_api = NULL;
    if (gAML_IIR_LibHandler != NULL) {
        dlclose(gAML_IIR_LibHandler);
        gAML_IIR_LibHandler = NULL;
    }
    return 0;
}

int load_aml_IIR_lib(void) {
    char *error;

    unload_aml_IIR_lib();

    gAML_IIR_LibHandler = dlopen("/system/lib/soundfx/libaml_IIR.so", RTLD_NOW);
    if (!gAML_IIR_LibHandler) {
        ALOGE("%s, failed to load aml_IIR lib (libaml_IIR.so)\n", __FUNCTION__);
        goto Error;
    }

    audio_IIR_init_api = (void (*)(int))dlsym(gAML_IIR_LibHandler, "audio_IIR_init");
    if (audio_IIR_init_api == NULL) {
        ALOGE("%s, fail find func audio_IIR_init()\n", __FUNCTION__);
        goto Error;
    }

    audio_IIR_process_api = (int (*)(int, int))
                dlsym(gAML_IIR_LibHandler, "audio_IIR_process");
    if (audio_IIR_process_api == NULL) {
        ALOGE("%s, fail find func audio_IIR_process()\n", __FUNCTION__);
        goto Error;
    }
    return 0;
Error:
    unload_aml_IIR_lib();
    return -1;
}