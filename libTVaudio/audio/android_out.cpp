/*
 android_out.cpp
 New an audio track in android, but no data in this track. Only push FIFO pointer.
 */

#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <pthread.h>
#include <cutils/log.h>

#include <media/AudioTrack.h>

#include "audio_usb_check.h"
#include "android_out.h"
#include "aml_audio.h"
#include "DDP_media_source.h"

using namespace android;

#define LOG_TAG "android_out"

static AudioTrack *glpTracker = NULL;

#if ANDROID_PLATFORM_SDK_VERSION >= 19
static sp<AudioTrack> gmpAudioTracker;
#endif

extern struct circle_buffer android_out_buffer;
extern struct circle_buffer DDP_out_buffer;
extern int output_record_enable;
extern pthread_mutex_t device_change_lock;

int I2S_state = 0;

static void AudioTrackCallback(int event, void* user, void *info) {
    AudioTrack::Buffer *buffer = static_cast<AudioTrack::Buffer *>(info);

    if (event != AudioTrack::EVENT_MORE_DATA) {
        ALOGD("%s, audio track envent = %d!\n", __FUNCTION__, event);
        return;
    }
    if (buffer == NULL || buffer->size == 0) {
        return;
    }

    int bytes = 0;

    pthread_mutex_lock(&device_change_lock);

    if (GetOutputdevice() == 1) {
        bytes = buffer_read(&android_out_buffer, (char *) buffer->raw,
                buffer->size);
        if (bytes < 0)
            buffer->size = 0;
    } else if (GetOutputdevice() == 2) {
        bytes = buffer_read(&DDP_out_buffer, (char *) buffer->raw,
                buffer->size);
        if (bytes < 0)
            buffer->size = 0;
    } else {
        if (output_record_enable == 1) {
            bytes = buffer_read(&android_out_buffer, (char *) buffer->raw,
                    buffer->size);
            if (bytes < 0)
                buffer->size = 0;
        } else {
            memset(buffer->i16, 0, buffer->size);
        }
    }

    pthread_mutex_unlock(&device_change_lock);
    I2S_state += 1;
    return;
}

static int AudioTrackRelease(void) {
#if ANDROID_PLATFORM_SDK_VERSION < 19
    if (glpTracker != NULL) {
        glpTracker->stop();
        delete glpTracker;
        glpTracker = NULL;
    }
#else
    if (glpTracker != NULL ) {
        glpTracker->stop();
        glpTracker = NULL;
    }

    if (gmpAudioTracker != NULL ) {
        gmpAudioTracker.clear();
    }
#endif
    return 0;
}

static int AudioTrackInit(void) {
    status_t Status;

    ALOGD("%s, entering...\n", __FUNCTION__);

    I2S_state = 0;

#if ANDROID_PLATFORM_SDK_VERSION < 19
    glpTracker = new AudioTrack();
    if (glpTracker == NULL) {
        ALOGE("%s, new AudioTrack failed.\n", __FUNCTION__);
        return -1;
    }
#else
    gmpAudioTracker = new AudioTrack();
    if (gmpAudioTracker == NULL) {
        ALOGE("%s, new AudioTrack failed.\n", __FUNCTION__);
        return -1;
    }
    glpTracker = gmpAudioTracker.get();
#endif

    Status = glpTracker->set(AUDIO_STREAM_MUSIC, 48000, AUDIO_FORMAT_PCM_16_BIT,
            AUDIO_CHANNEL_OUT_STEREO, 0, AUDIO_OUTPUT_FLAG_NONE,
            AudioTrackCallback, NULL, 0, 0, false, 0);

    if (Status != NO_ERROR) {
        ALOGE("%s, AudioTrack set failed.\n", __FUNCTION__);

        AudioTrackRelease();
        return -1;
    }

    Status = glpTracker->initCheck();
    if (Status != NO_ERROR) {
        ALOGE("%s, AudioTrack initCheck failed.\n", __FUNCTION__);

        AudioTrackRelease();
        return -1;
    }

    glpTracker->start();

    Status = glpTracker->setVolume(1.0, 1.0);
    if (Status != NO_ERROR) {
        ALOGE("%s, AudioTrack setVolume failed.\n", __FUNCTION__);

        AudioTrackRelease();
        return -1;
    }

    ALOGD("%s, exit...\n", __FUNCTION__);

    return 0;
}

int new_android_audiotrack(void) {
    return AudioTrackInit();
}

int release_android_audiotrack(void) {
    return AudioTrackRelease();
}

