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

#define LOG_TAG "libms12"
//#define LOG_NDEBUG 0
//#define LOG_NALOGV 0

#include <utils/Log.h>
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <stdbool.h>
#include <dlfcn.h>

#include "DolbyMS12.h"
#include "DolbyMS12ConfigParams.h"

#define DOLBY_MS12_LIB_PATH_A "/system/lib/libdolbyms12.so"
#define DOLBY_MS12_LIB_PATH_B "/system/vendor/lib/libdolbyms12.so"

namespace android {

//static function pointer
int (*FuncGetMS12OutputMaxSize)(void);
void * (*FuncDolbyMS12Init)(int argc, char **pp_argv);
void (*FuncDolbyMS12Release)(void *);
int (*FuncDolbyMS12InputMain)(void *, const void *, size_t, int, int, int);
int (*FuncDolbyMS12InputAssociate)(void *, const void *, size_t, int, int, int);
int (*FuncDolbyMS12InputSystem)(void *, const void *, size_t, int, int, int);

#ifdef REPLACE_OUTPUT_BUFFER_WITH_CALLBACK
int (*FuncDolbyMS12RegisterPCMCallback)(output_callback , void *);
int (*FuncDolbyMS12RegisterBitstreamCallback)(output_callback , void *);
#else
int (*FuncDolbyMS12Output)(void *, const void *, size_t );
#endif

int (*FuncDolbyMS12UpdateRuntimeParams)(void *, int , char **);

DolbyMS12::DolbyMS12() :
mDolbyMS12LibHanle(NULL)
{
    ALOGD("%s()", __FUNCTION__);
}


DolbyMS12::~DolbyMS12()
{
    ALOGD("%s()", __FUNCTION__);
}


int DolbyMS12::GetLibHandle(void)
{
    ALOGD("+%s()", __FUNCTION__);
    ReleaseLibHandle();

    //here there are two paths, "the DOLBY_MS12_LIB_PATH_A/B", where could exit that dolby ms12 libary.
    mDolbyMS12LibHanle = dlopen(DOLBY_MS12_LIB_PATH_A, RTLD_NOW);
    if (!mDolbyMS12LibHanle) {
        mDolbyMS12LibHanle = dlopen(DOLBY_MS12_LIB_PATH_B, RTLD_NOW);
        if (!mDolbyMS12LibHanle) {
            ALOGE("%s, failed to load libdolbyms12 lib %s\n", __FUNCTION__, dlerror());
            goto ERROR;
        }
    }

    FuncGetMS12OutputMaxSize = (int (*) (void)) dlsym(mDolbyMS12LibHanle, "get_ms12_output_max_size");
    if (!FuncGetMS12OutputMaxSize) {
        ALOGE("%s, dlsym get_ms12_output_max_size fail\n", __FUNCTION__);
        goto ERROR;
    }

    FuncDolbyMS12Init = (void* (*) (int, char **)) dlsym(mDolbyMS12LibHanle, "ms12_init");
    if (!FuncDolbyMS12Init) {
        ALOGE("%s, dlsym ms12_init fail\n", __FUNCTION__);
        goto ERROR;
    }

    FuncDolbyMS12Release = (void (*) (void *)) dlsym(mDolbyMS12LibHanle, "ms12_release");
    if (!FuncDolbyMS12Release) {
        ALOGE("%s, dlsym ms12_release fail\n", __FUNCTION__);
        goto ERROR;
    }

    FuncDolbyMS12InputMain = (int (*) (void *, const void *, size_t, int, int, int)) dlsym(mDolbyMS12LibHanle, "ms12_input_main");
    if (!FuncDolbyMS12InputMain) {
        ALOGE("%s, dlsym ms12_input_main fail\n", __FUNCTION__);
        goto ERROR;
    }

    FuncDolbyMS12InputAssociate = (int (*) (void *, const void *, size_t, int, int, int)) dlsym(mDolbyMS12LibHanle, "ms12_input_associate");
    if (!FuncDolbyMS12InputAssociate) {
        ALOGE("%s, dlsym ms12_input_associate fail\n", __FUNCTION__);
        goto ERROR;
    }

    FuncDolbyMS12InputSystem = (int (*) (void *, const void *, size_t, int, int, int)) dlsym(mDolbyMS12LibHanle, "ms12_input_system");
    if (!FuncDolbyMS12InputSystem) {
        ALOGE("%s, dlsym ms12_input_system fail\n", __FUNCTION__);
        goto ERROR;
    }

#ifdef REPLACE_OUTPUT_BUFFER_WITH_CALLBACK
    FuncDolbyMS12RegisterPCMCallback = (int (*) (output_callback , void *)) dlsym(mDolbyMS12LibHanle, "ms12_output_register_pcm_callback");
    if (!FuncDolbyMS12RegisterPCMCallback) {
        ALOGE("%s, dlsym ms12_output_register_pcm_callback fail\n", __FUNCTION__);
        goto ERROR;
    }
    FuncDolbyMS12RegisterBitstreamCallback = (int (*) (output_callback , void *)) dlsym(mDolbyMS12LibHanle, "ms12_output_register_bitstream_callback");
    if (!FuncDolbyMS12RegisterBitstreamCallback) {
        ALOGE("%s, dlsym ms12_output_register_bitstream_callback fail\n", __FUNCTION__);
        goto ERROR;
    }
#else
    FuncDolbyMS12Output = (int (*) (void *, const void *, size_t))  dlsym(mDolbyMS12LibHanle, "ms12_output");
    if (!FuncDolbyMS12Output) {
        ALOGE("%s, dlsym ms12_output fail\n", __FUNCTION__);
        goto ERROR;
    }
#endif

    FuncDolbyMS12UpdateRuntimeParams = (int (*) (void *, int , char **))  dlsym(mDolbyMS12LibHanle, "ms12_update_runtime_params");
    if (!FuncDolbyMS12UpdateRuntimeParams) {
        ALOGE("%s, dlsym ms12_update_runtime_params fail\n", __FUNCTION__);
        goto ERROR;
    }

    ALOGD("-%s() line %d get libdolbyms12 success!", __FUNCTION__, __LINE__);
    return 0;

ERROR:
    ALOGD("-%s() line %d", __FUNCTION__, __LINE__);
    return -1;
}

void DolbyMS12::ReleaseLibHandle(void)
{
    ALOGD("+%s()", __FUNCTION__);

    //re-value the api as NULL
    FuncGetMS12OutputMaxSize = NULL;
    FuncDolbyMS12Init = NULL;
    FuncDolbyMS12Release = NULL;
    FuncDolbyMS12InputMain = NULL;
    FuncDolbyMS12InputAssociate = NULL;
    FuncDolbyMS12InputSystem = NULL;
#ifdef REPLACE_OUTPUT_BUFFER_WITH_CALLBACK
    FuncDolbyMS12RegisterPCMCallback = NULL;
    FuncDolbyMS12RegisterBitstreamCallback = NULL;
#else
    FuncDolbyMS12Output = NULL;
#endif
    FuncDolbyMS12UpdateRuntimeParams = NULL;

    if (mDolbyMS12LibHanle != NULL) {
        dlclose(mDolbyMS12LibHanle);
        mDolbyMS12LibHanle = NULL;
    }

    ALOGD("-%s()", __FUNCTION__);
    return ;
}

int DolbyMS12::GetMS12OutputMaxSize(void)
{
    ALOGD("+%s()", __FUNCTION__);
    int ret = 0;
    if (!FuncGetMS12OutputMaxSize) {
        ALOGE("%s(), pls load lib first.\n", __FUNCTION__);
        return NULL;
    }

    ret = (*FuncGetMS12OutputMaxSize)();
    ALOGD("-%s() ret %d", __FUNCTION__, ret);
    return ret;
}


void * DolbyMS12::DolbyMS12Init(int configNum, char **configParams)
{
    void * dolby_ms12_init_ret = NULL;
    ALOGD("+%s()", __FUNCTION__);
    if (!FuncDolbyMS12Init) {
        ALOGE("%s(), pls load lib first.\n", __FUNCTION__);
        return NULL;
    }

    dolby_ms12_init_ret = (*FuncDolbyMS12Init)(configNum, configParams);
    ALOGD("-%s() dolby_ms12_init_ret %p", __FUNCTION__, dolby_ms12_init_ret);
    return dolby_ms12_init_ret;
}

void DolbyMS12::DolbyMS12Release(void *DolbyMS12Pointer)
{
    ALOGD("+%s()", __FUNCTION__);
    if (!FuncDolbyMS12Release) {
        ALOGE("%s(), pls load lib first.\n", __FUNCTION__);
        return ;
    }

    (*FuncDolbyMS12Release)(DolbyMS12Pointer);
    ALOGD("-%s()", __FUNCTION__);
    return ;
}

int DolbyMS12::DolbyMS12InputMain(
  void *DolbyMS12Pointer
  , const void *audio_stream_out_buffer //ms12 input buffer
  , size_t audio_stream_out_buffer_size //ms12 input buffer size
  , int audio_stream_out_format
  , int audio_stream_out_channel_num
  , int audio_stream_out_sample_rate
)
{
    ALOGV("+%s()", __FUNCTION__);
    int ret = 0;

    if (!FuncDolbyMS12InputMain) {
        ALOGE("%s(), pls load lib first.\n", __FUNCTION__);
        return -1;
    }

    ret = (*FuncDolbyMS12InputMain)(DolbyMS12Pointer
                , audio_stream_out_buffer //ms12 input buffer
                , audio_stream_out_buffer_size //ms12 input buffer size
                , audio_stream_out_format
                , audio_stream_out_channel_num
                , audio_stream_out_sample_rate);
    ALOGV("-%s() ret %d", __FUNCTION__, ret);
    return ret;
}

int DolbyMS12::DolbyMS12InputAssociate(
  void *DolbyMS12Pointer
  , const void *audio_stream_out_buffer //ms12 input buffer
  , size_t audio_stream_out_buffer_size //ms12 input buffer size
  , int audio_stream_out_format
  , int audio_stream_out_channel_num
  , int audio_stream_out_sample_rate
)
{
    ALOGV("+%s()", __FUNCTION__);
    int ret = 0;

    if (!FuncDolbyMS12InputAssociate) {
        ALOGE("%s(), pls load lib first.\n", __FUNCTION__);
        return -1;
    }

    ret = (*FuncDolbyMS12InputAssociate)(DolbyMS12Pointer
                , audio_stream_out_buffer //ms12 input buffer
                , audio_stream_out_buffer_size //ms12 input buffer size
                , audio_stream_out_format
                , audio_stream_out_channel_num
                , audio_stream_out_sample_rate);
    ALOGV("-%s() ret %d", __FUNCTION__, ret);
    return ret;
}

int DolbyMS12::DolbyMS12InputSystem(
  void *DolbyMS12Pointer
  , const void *audio_stream_out_buffer //ms12 input buffer
  , size_t audio_stream_out_buffer_size //ms12 input buffer size
  , int audio_stream_out_format
  , int audio_stream_out_channel_num
  , int audio_stream_out_sample_rate
)
{
    ALOGV("+%s()", __FUNCTION__);
    int ret = 0;

    if (!FuncDolbyMS12InputSystem) {
        ALOGE("%s(), pls load lib first.\n", __FUNCTION__);
        return -1;
    }

    ret = (*FuncDolbyMS12InputSystem)(DolbyMS12Pointer
                , audio_stream_out_buffer //ms12 input buffer
                , audio_stream_out_buffer_size //ms12 input buffer size
                , audio_stream_out_format
                , audio_stream_out_channel_num
                , audio_stream_out_sample_rate);
    ALOGV("-%s() ret %d", __FUNCTION__, ret);
    return ret;
}

#ifdef REPLACE_OUTPUT_BUFFER_WITH_CALLBACK
int DolbyMS12::DolbyMS12RegisterPCMCallback(output_callback callback, void *priv_data)
{
    int ret = 0;
    ALOGV("+%s()", __FUNCTION__);
    if (!FuncDolbyMS12RegisterPCMCallback) {
        ALOGE("%s(), pls load lib first.\n", __FUNCTION__);
        return -1;
    }

    ret = (*FuncDolbyMS12RegisterPCMCallback)(callback, priv_data);
    ALOGV("-%s() ret %d", __FUNCTION__, ret);
    return ret;
}

int DolbyMS12::DolbyMS12RegisterBitstreamCallback(output_callback callback, void *priv_data)
{
    int ret = 0;
    ALOGV("+%s()", __FUNCTION__);
    if (!FuncDolbyMS12RegisterBitstreamCallback) {
        ALOGE("%s(), pls load lib first.\n", __FUNCTION__);
        return -1;
    }

    ret = (*FuncDolbyMS12RegisterBitstreamCallback)(callback , priv_data);
    ALOGV("-%s() ret %d", __FUNCTION__, ret);
    return ret;
}
#else
int DolbyMS12::DolbyMS12Output(
  void *DolbyMS12Pointer
  , const void *ms12_out_buffer //ms12 output buffer
  , size_t request_out_buffer_size //ms12 output buffer size
)
{
    int ret = 0;
    ALOGV("+%s()", __FUNCTION__);
    if (!FuncDolbyMS12Output) {
        ALOGE("%s(), pls load lib first.\n", __FUNCTION__);
        return -1;
    }

    ret = (*FuncDolbyMS12Output)(DolbyMS12Pointer
                , ms12_out_buffer //ms12 output buffer
                , request_out_buffer_size //ms12 output buffer size
                );
    ALOGV("-%s() ret %d", __FUNCTION__, ret);
    return ret;
}
#endif

int DolbyMS12::DolbyMS12UpdateRuntimeParams(void *DolbyMS12Pointer, int configNum, char **configParams)
{
    int ret = 0;
    ALOGV("+%s()", __FUNCTION__);
    if (!FuncDolbyMS12UpdateRuntimeParams) {
        ALOGE("%s(), pls load lib first.\n", __FUNCTION__);
        return -1;
    }

    ret = (*FuncDolbyMS12UpdateRuntimeParams)(DolbyMS12Pointer, configNum, configParams);
    ALOGV("-%s() ret %d", __FUNCTION__, ret);
    return ret;
}


/*--------------------------------------------------------------------------*/
}   // namespace android
