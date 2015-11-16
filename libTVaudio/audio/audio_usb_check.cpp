#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <errno.h>
#include <cutils/log.h>
#include <media/AudioSystem.h>

#include "audio_usb_check.h"

#define LOG_TAG "aml_audio"

namespace android {

extern "C" int GetUsbAudioCheckFlag(void) {
    int device = (int)AudioSystem::getDevicesForStream(AUDIO_STREAM_MUSIC);
    return device;
}

static float last_vol = 1.0;
extern "C" float get_android_stream_volume() {
    float vol = last_vol;
    unsigned int sr = 0;
    AudioSystem::getOutputSamplingRate(&sr, AUDIO_STREAM_MUSIC);
    if (sr > 0) {
        audio_io_handle_t handle = -1;
        handle = AudioSystem::getOutput(AUDIO_STREAM_MUSIC,
                                    48000,
                                    AUDIO_FORMAT_PCM_16_BIT,
                                    AUDIO_CHANNEL_OUT_STEREO,
                                    AUDIO_OUTPUT_FLAG_PRIMARY
        );
        if (handle > 0) {
            if (AudioSystem::getStreamVolume(AUDIO_STREAM_MUSIC,&vol,handle) == NO_ERROR) {
                last_vol = vol;
            } else
                ALOGI("get stream volume failed\n");
        }
        else
            ALOGI("get output handle failed\n");
    }
    return vol;
 }

}

