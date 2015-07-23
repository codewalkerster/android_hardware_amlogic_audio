/*
 audio_usb_check.cpp
 check usb audio device.
 */

#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <errno.h>
#include <cutils/log.h>
#include <media/AudioSystem.h>

#include "audio_usb_check.h"

#define LOG_TAG "audio_usb_check"

namespace android {

extern "C" int GetUsbAudioCheckFlag() {
    int device = (int)AudioSystem::getDevicesForStream(AUDIO_STREAM_MUSIC);
    return device;
}

}

int createMonitorUsbHostBusThread() {
    return 0;
}

