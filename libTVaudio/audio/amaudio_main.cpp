/*
 This file is used for testing audio fuction.
 */

#include <unistd.h>

#include <binder/IPCThreadState.h>
#include <binder/ProcessState.h>
#include <binder/IServiceManager.h>

#include "../audio_amaudio.h"
#include "audio_usb_check.h"

using namespace android;

int main(int argc, char** argv) {
    sp < ProcessState > proc(ProcessState::self());
    sp < IServiceManager > sm = defaultServiceManager();

    //test for audio volume control
    amAudioSetAndroidVolumeEnable(1);
    amAudioSetAndroidVolume(60, 60);
    sleep(10);

    //test for S805 HDMI_IN BOX
    amAudioOpen(48000, CC_IN_USE_SPDIF_DEVICE, CC_OUT_USE_AMAUDIO);
    amAudioOpen(48000, CC_IN_USE_SPDIF_DEVICE, CC_OUT_USE_ANDROID);
    sleep(100);

    //test for TV
    amAudioOpen(48000, CC_IN_USE_I2S_DEVICE, CC_OUT_USE_AMAUDIO);
    amAudioOpen(48000, CC_IN_USE_I2S_DEVICE, CC_OUT_USE_ANDROID);
    sleep(100);

    //test for data mix mode and mix volume
    sleep(10);
    amAudioSetOutputMode(2);
    amAudioSetMusicGain(60);
    sleep(10);
    amAudioSetMusicGain(256);
    sleep(10);
    amAudioSetAndroidVolumeEnable(0);
    sleep(10);
    amAudioSetOutputMode(0);
    amAudioSetLeftGain(256);
    amAudioSetRightGain(256);

    //test for audio effect
    sleep(30);
    //EQ parameters
    int gain_val_buf[5] = { 3, 2, 0, 2, 3 };
    amAudioSetEQGain(gain_val_buf, sizeof(gain_val_buf));
    amAudioSetEQEnable(1);
    sleep(30);

    //srs parameters
    amAudioSetSRSTrubassSpeakerSize(4);
    amAudioSetSRSTrubassGain(0.3);
    amAudioSetSRSDialogClarityGain(0.2);
    amAudioSetSRSDefinitionGain(0.5);
    amAudioSetSRSSurroundGain(0.6);
    amAudioSetSRSDialogClaritySwitch(1);
    amAudioSetSRSSurroundSwitch(1);
    amAudioSetSRSTrubassSwitch(1);

    sleep(500);

    amAudioClose();

    ProcessState::self()->startThreadPool();
    IPCThreadState::self()->joinThreadPool();

    return 0;
}
