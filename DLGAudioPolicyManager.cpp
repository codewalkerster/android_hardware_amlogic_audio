/*
 * Copyright (C) 2014 The Android Open Source Project
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

#define LOG_TAG "DLGAudioPolicyManager"
//#define LOG_NDEBUG 0
#include <media/AudioParameter.h>
#include <media/mediarecorder.h>
#include <utils/Log.h>
#include <utils/String16.h>
#include <utils/String8.h>
#include <utils/StrongPointer.h>

#include "DLGAudioPolicyManager.h"
#include <stdlib.h>
#include <sys/stat.h>
#include <fcntl.h>


namespace android {
// ----------------------------------------------------------------------------
// Common audio policy manager code is implemented in AudioPolicyManager class
// ----------------------------------------------------------------------------

// ---  class factory


extern "C" AudioPolicyInterface* createAudioPolicyManager(
        AudioPolicyClientInterface *clientInterface)
{
    return new DLGAudioPolicyManager(clientInterface);
}

extern "C" void destroyAudioPolicyManager(AudioPolicyInterface *interface)
{
    delete interface;
}

DLGAudioPolicyManager::DLGAudioPolicyManager(
        AudioPolicyClientInterface *clientInterface)
    : AudioPolicyManager(clientInterface), mForceSubmixInputSelection(false)
{
}

float DLGAudioPolicyManager::computeVolume(audio_stream_type_t stream,
                                           int index,
                                           audio_io_handle_t output,
                                           audio_devices_t device)
{
    // We only use master volume, so all audio flinger streams
    // should be set to maximum
    (void)stream;
    (void)index;
    (void)output;
    (void)device;
    return AudioPolicyManager::computeVolume(stream,index,output,device);
}

status_t DLGAudioPolicyManager::setDeviceConnectionState(audio_devices_t device,
                                                         audio_policy_dev_state_t state,
                                                         const char *device_address)
{
    audio_devices_t tmp = AUDIO_DEVICE_NONE;;
    ALOGV("setDeviceConnectionState %08x %x %s", device, state,
          device_address ? device_address : "(null)");

    // If the input device is the remote submix and an address starting with "force=" was
    // specified, enable "force=1" / disable "force=0" the forced selection of the remote submix
    // input device over hardware input devices (e.g RemoteControl).
    if (device == AUDIO_DEVICE_IN_REMOTE_SUBMIX && device_address) {
        AudioParameter parameters = AudioParameter(String8(device_address));
        int forceValue;
        if (parameters.getInt(String8("force"), forceValue) == OK) {
            mForceSubmixInputSelection = forceValue != 0;
        }
    }

    status_t ret = 0;
    if (device != AUDIO_DEVICE_IN_REMOTE_SUBMIX) {
      ret = AudioPolicyManager::setDeviceConnectionState(
                    device, state, device_address);
    }

    return ret;
}

audio_devices_t DLGAudioPolicyManager::getDeviceForInputSource(audio_source_t inputSource)
{
    uint32_t device = AUDIO_DEVICE_NONE;
    bool usePhysRemote = true;

    if (inputSource == AUDIO_SOURCE_VOICE_RECOGNITION) {
      ALOGV("getDeviceForInputSource %s", mForceSubmixInputSelection ? "use virtual" : "");
      audio_devices_t availableDeviceTypes = mAvailableInputDevices.types() &
                                                ~AUDIO_DEVICE_BIT_IN;
      if (availableDeviceTypes & AUDIO_DEVICE_IN_REMOTE_SUBMIX &&
            mForceSubmixInputSelection) {
          // REMOTE_SUBMIX should always be avaible, let's make sure it's being forced at the moment
          ALOGV("Virtual remote available");
          device = AUDIO_DEVICE_IN_REMOTE_SUBMIX;
      }
    }
    else
        device = AudioPolicyManager::getDeviceForInputSource(inputSource);

    ALOGV("getDeviceForInputSource() input source %d, device %08x", inputSource, device);
    return device;
}

audio_devices_t DLGAudioPolicyManager::getDevicesForStream(audio_stream_type_t stream) {
    // By checking the range of stream before calling getStrategy, we avoid
    // getStrategy's behavior for invalid streams.  getStrategy would do a ALOGE
    // and then return STRATEGY_MEDIA, but we want to return the empty set.
    if (stream < (audio_stream_type_t) 0 || stream >= AUDIO_STREAM_CNT) {
        return AUDIO_DEVICE_NONE;
    }
    audio_devices_t devices;
    AudioPolicyManager::routing_strategy strategy = getStrategy(stream);
    devices = getDeviceForStrategy(strategy, true /*fromCache*/);

    if (stream == AUDIO_STREAM_MUSIC && devices == AUDIO_DEVICE_OUT_HDMI) {
         ALOGV("[%s %d]force change device from 0x/%x to 0x%x for AUDIO_STREAM_MUSIC\n",__FUNCTION__,__LINE__,AUDIO_DEVICE_OUT_HDMI,AUDIO_STREAM_MUSIC);
         devices = AUDIO_DEVICE_OUT_SPEAKER;
    }

    SortedVector<audio_io_handle_t> outputs = getOutputsForDevice(devices, mOutputs);
    for (size_t i = 0; i < outputs.size(); i++) {
        sp<AudioOutputDescriptor> outputDesc = mOutputs.valueFor(outputs[i]);
        if (outputDesc->isStrategyActive(strategy)) {
            devices = outputDesc->device();
            break;
        }
    }

    /*Filter SPEAKER_SAFE out of results, as AudioService doesn't know about it
      and doesn't really need to.*/
    if (devices & AUDIO_DEVICE_OUT_SPEAKER_SAFE) {
        devices |= AUDIO_DEVICE_OUT_SPEAKER;
        devices &= ~AUDIO_DEVICE_OUT_SPEAKER_SAFE;
    }

    return devices;
}


audio_io_handle_t DLGAudioPolicyManager::getOutput(audio_stream_type_t stream,
                                    uint32_t samplingRate,
                                    audio_format_t format,
                                    audio_channel_mask_t channelMask,
                                    audio_output_flags_t flags,
                                    const audio_offload_info_t *offloadInfo)
{

    routing_strategy strategy = getStrategy(stream);
    audio_devices_t device = getDeviceForStrategy(strategy, false /*fromCache*/);
    ALOGV("[%s %d] device %d, stream %d, samplingRate %d, format %x, channelMask %x, flags %x",__FUNCTION__,__LINE__,
          device, stream, samplingRate, format, channelMask, flags);
   return getOutputForDevice(device, stream, samplingRate,format, channelMask, flags,
            offloadInfo);
}

audio_io_handle_t DLGAudioPolicyManager::getOutputForAttr(const audio_attributes_t *attr,
                                    uint32_t samplingRate,
                                    audio_format_t format,
                                    audio_channel_mask_t channelMask,
                                    audio_output_flags_t flags,
                                    const audio_offload_info_t *offloadInfo)
{
    if (attr == NULL) {
        ALOGE("getOutputForAttr() called with NULL audio attributes");
        return 0;
    }
    ALOGV("getOutputForAttr() usage=%d, content=%d, tag=%s flags=%08x",
            attr->usage, attr->content_type, attr->tags, attr->flags);

    // TODO this is where filtering for custom policies (rerouting, dynamic sources) will go
    routing_strategy strategy = (routing_strategy) getStrategyForAttr(attr);
    audio_devices_t device = getDeviceForStrategy(strategy, false /*fromCache*/);

    if ((attr->flags & AUDIO_FLAG_HW_AV_SYNC) != 0) {
        flags = (audio_output_flags_t)(flags | AUDIO_OUTPUT_FLAG_HW_AV_SYNC);
    }

    ALOGV("getOutputForAttr() device %d, samplingRate %d, format %x, channelMask %x, flags %x",
          device, samplingRate, format, channelMask, flags);

    audio_stream_type_t stream = streamTypefromAttributesInt(attr);
    return getOutputForDevice(device, stream, samplingRate, format, channelMask, flags,
                offloadInfo);
}

static int get_codec_type(const char * path)
{
    int val = 0;
    int fd = open("/sys/class/audiodsp/digital_codec", O_RDONLY);
    if (fd >= 0) {
        char  bcmd[16];
        read(fd, bcmd, sizeof(bcmd));
        val = strtol(bcmd, NULL, 10);
        close(fd);
    }else{
         ALOGI("[%s]open digital_codec node failed! return 0\n", __FUNCTION__);
    }
    return val;
}

#define MAX_MIXER_SAMPLING_RATE 192000

audio_io_handle_t DLGAudioPolicyManager::getOutputForDevice(
        audio_devices_t device,
        audio_stream_type_t stream,
        uint32_t samplingRate,
        audio_format_t format,
        audio_channel_mask_t channelMask,
        audio_output_flags_t flags,
        const audio_offload_info_t *offloadInfo)
{
    audio_io_handle_t output = AUDIO_IO_HANDLE_NONE;
    uint32_t latency = 0;
    status_t status;

#ifdef AUDIO_POLICY_TEST
    if (mCurOutput != 0) {
        ALOGV("getOutput() test output mCurOutput %d, samplingRate %d, format %d, channelMask %x, mDirectOutput %d",
                mCurOutput, mTestSamplingRate, mTestFormat, mTestChannels, mDirectOutput);

        if (mTestOutputs[mCurOutput] == 0) {
            ALOGV("getOutput() opening test output");
            sp<AudioOutputDescriptor> outputDesc = new AudioOutputDescriptor(NULL);
            outputDesc->mDevice = mTestDevice;
            outputDesc->mLatency = mTestLatencyMs;
            outputDesc->mFlags =
                    (audio_output_flags_t)(mDirectOutput ? AUDIO_OUTPUT_FLAG_DIRECT : 0);
            outputDesc->mRefCount[stream] = 0;
            audio_config_t config = AUDIO_CONFIG_INITIALIZER;
            config.sample_rate = mTestSamplingRate;
            config.channel_mask = mTestChannels;
            config.format = mTestFormat;
            if (offloadInfo != NULL) {
                config.offload_info = *offloadInfo;
            }
            status = mpClientInterface->openOutput(0,
                                                  &mTestOutputs[mCurOutput],
                                                  &config,
                                                  &outputDesc->mDevice,
                                                  String8(""),
                                                  &outputDesc->mLatency,
                                                  outputDesc->mFlags);
            if (status == NO_ERROR) {
                outputDesc->mSamplingRate = config.sample_rate;
                outputDesc->mFormat = config.format;
                outputDesc->mChannelMask = config.channel_mask;
                AudioParameter outputCmd = AudioParameter();
                outputCmd.addInt(String8("set_id"),mCurOutput);
                mpClientInterface->setParameters(mTestOutputs[mCurOutput],outputCmd.toString());
                addOutput(mTestOutputs[mCurOutput], outputDesc);
            }
        }
        return mTestOutputs[mCurOutput];
    }
#endif //AUDIO_POLICY_TEST

    // open a direct output if required by specified parameters
    //force direct flag if offload flag is set: offloading implies a direct output stream
    // and all common behaviors are driven by checking only the direct flag
    // this should normally be set appropriately in the policy configuration file
    if ((flags & AUDIO_OUTPUT_FLAG_COMPRESS_OFFLOAD) != 0) {
        flags = (audio_output_flags_t)(flags | AUDIO_OUTPUT_FLAG_DIRECT);
    }
    if ((flags & AUDIO_OUTPUT_FLAG_HW_AV_SYNC) != 0) {
        flags = (audio_output_flags_t)(flags | AUDIO_OUTPUT_FLAG_DIRECT);
    }

    sp<IOProfile> profile;

    // skip direct output selection if the request can obviously be attached to a mixed output
    // and not explicitly requested
    if (((flags & AUDIO_OUTPUT_FLAG_DIRECT) == 0) &&
            audio_is_linear_pcm(format) && samplingRate <= MAX_MIXER_SAMPLING_RATE &&
            audio_channel_count_from_out_mask(channelMask) <= 2) {
        goto non_direct_output;
    }

    // Do not allow offloading if one non offloadable effect is enabled. This prevents from
    // creating an offloaded track and tearing it down immediately after start when audioflinger
    // detects there is an active non offloadable effect.
    // FIXME: We should check the audio session here but we do not have it in this context.
    // This may prevent offloading in rare situations where effects are left active by apps
    // in the background.

    if (((flags & AUDIO_OUTPUT_FLAG_COMPRESS_OFFLOAD) == 0) ||
            !isNonOffloadableEffectEnabled()) {
        profile = getProfileForDirectOutput(device,
                                           samplingRate,
                                           format,
                                           channelMask,
                                           (audio_output_flags_t)flags);
    }

    if (profile != 0) {
        sp<AudioOutputDescriptor> outputDesc = NULL;

        for (size_t i = 0; i < mOutputs.size(); i++) {
            sp<AudioOutputDescriptor> desc = mOutputs.valueAt(i);
            if (!desc->isDuplicated() && (profile == desc->mProfile)) {
                outputDesc = desc;
                // reuse direct output if currently open and configured with same parameters
                if ((samplingRate == outputDesc->mSamplingRate) &&
                        (format == outputDesc->mFormat) &&
                        (channelMask == outputDesc->mChannelMask)) {
                    outputDesc->mDirectOpenCount++;
                    ALOGI("getOutput() reusing direct output %d", mOutputs.keyAt(i));
                    return mOutputs.keyAt(i);
                }
            }
        }
        // close direct output if currently open and configured with different parameters
        if (outputDesc != NULL) {
            closeOutput(outputDesc->mIoHandle);
        }
        outputDesc = new AudioOutputDescriptor(profile);
        outputDesc->mDevice = device;
        outputDesc->mLatency = 0;
        outputDesc->mFlags =(audio_output_flags_t) (outputDesc->mFlags | flags);
        audio_config_t config = AUDIO_CONFIG_INITIALIZER;
        config.sample_rate = samplingRate;
        config.channel_mask = channelMask;
        config.format = format;
        if (offloadInfo != NULL) {
            config.offload_info = *offloadInfo;
        }
        status = mpClientInterface->openOutput(profile->mModule->mHandle,
                                               &output,
                                               &config,
                                               &outputDesc->mDevice,
                                               String8(""),
                                               &outputDesc->mLatency,
                                               outputDesc->mFlags);

        // only accept an output with the requested parameters
        if (status != NO_ERROR ||
            (samplingRate != 0 && samplingRate != config.sample_rate) ||
            (format != AUDIO_FORMAT_DEFAULT && format != config.format) ||
            (channelMask != 0 && channelMask != config.channel_mask)) {
            ALOGV("getOutput() failed opening direct output: output %d samplingRate %d %d,"
                    "format %d %d, channelMask %04x %04x", output, samplingRate,
                    outputDesc->mSamplingRate, format, outputDesc->mFormat, channelMask,
                    outputDesc->mChannelMask);
            if (output != AUDIO_IO_HANDLE_NONE) {
                mpClientInterface->closeOutput(output);
            }
            return AUDIO_IO_HANDLE_NONE;
        }
        outputDesc->mSamplingRate = config.sample_rate;
        outputDesc->mChannelMask = config.channel_mask;
        outputDesc->mFormat = config.format;
        outputDesc->mRefCount[stream] = 0;
        outputDesc->mStopTime[stream] = 0;
        outputDesc->mDirectOpenCount = 1;

        audio_io_handle_t srcOutput = getOutputForEffect();
        addOutput(output, outputDesc);
        audio_io_handle_t dstOutput = getOutputForEffect();
        if (dstOutput == output) {
            mpClientInterface->moveEffects(AUDIO_SESSION_OUTPUT_MIX, srcOutput, dstOutput);
        }
        mPreviousOutputs = mOutputs;
        ALOGV("getOutput() returns new direct output %d", output);
        mpClientInterface->onAudioPortListUpdate();
        return output;
    }

non_direct_output:

    // ignoring channel mask due to downmix capability in mixer

    // open a non direct output

    // for non direct outputs, only PCM is supported
    int direct_in_use=0;
    for (size_t i = 0; i < mOutputs.size(); i++) {
        sp<AudioOutputDescriptor> desc = mOutputs.valueAt(i);
        if (desc->mFlags & AUDIO_OUTPUT_FLAG_DIRECT)
            direct_in_use=1;
    }

    audio_devices_t availableOutputDeviceTypes = mAvailableOutputDevices.types();
    if ( (device == AUDIO_DEVICE_OUT_AUX_DIGITAL && !get_codec_type("/sys/class/audiodsp/digital_codec")) || direct_in_use)
    {
        device = availableOutputDeviceTypes & AUDIO_DEVICE_OUT_SPEAKER;
        if (device == AUDIO_DEVICE_NONE)
            device = AUDIO_DEVICE_OUT_AUX_DIGITAL;
        if (device == AUDIO_DEVICE_OUT_SPEAKER)
        {
           ALOGV("[%s %d]direct_in_use/%d force change device from %x to %x\n",__FUNCTION__,__LINE__,
           direct_in_use,AUDIO_DEVICE_OUT_AUX_DIGITAL,AUDIO_DEVICE_OUT_SPEAKER);
        }
    }

    if (audio_is_linear_pcm(format)) {
        // get which output is suitable for the specified stream. The actual
        // routing change will happen when startOutput() will be called
        SortedVector<audio_io_handle_t> outputs = getOutputsForDevice(device, mOutputs);

        // at this stage we should ignore the DIRECT flag as no direct output could be found earlier
        flags = (audio_output_flags_t)(flags & ~AUDIO_OUTPUT_FLAG_DIRECT);
        output = selectOutput(outputs, flags, format);
    }
    ALOGW_IF((output == 0), "getOutput() could not find output for stream %d, samplingRate %d,"
            "format %d, channels %x, flags %x", stream, samplingRate, format, channelMask, flags);

    ALOGV("getOutput() returns output %d", output);

    return output;
}

audio_stream_type_t DLGAudioPolicyManager::streamTypefromAttributesInt(const audio_attributes_t *attr)
{
    // flags to stream type mapping
    if ((attr->flags & AUDIO_FLAG_AUDIBILITY_ENFORCED) == AUDIO_FLAG_AUDIBILITY_ENFORCED) {
        return AUDIO_STREAM_ENFORCED_AUDIBLE;
    }
    if ((attr->flags & AUDIO_FLAG_SCO) == AUDIO_FLAG_SCO) {
        return AUDIO_STREAM_BLUETOOTH_SCO;
    }

    // usage to stream type mapping
    switch (attr->usage) {
    case AUDIO_USAGE_MEDIA:
    case AUDIO_USAGE_GAME:
    case AUDIO_USAGE_ASSISTANCE_ACCESSIBILITY:
    case AUDIO_USAGE_ASSISTANCE_NAVIGATION_GUIDANCE:
        return AUDIO_STREAM_MUSIC;
    case AUDIO_USAGE_ASSISTANCE_SONIFICATION:
        return AUDIO_STREAM_SYSTEM;
    case AUDIO_USAGE_VOICE_COMMUNICATION:
        return AUDIO_STREAM_VOICE_CALL;

    case AUDIO_USAGE_VOICE_COMMUNICATION_SIGNALLING:
        return AUDIO_STREAM_DTMF;

    case AUDIO_USAGE_ALARM:
        return AUDIO_STREAM_ALARM;
    case AUDIO_USAGE_NOTIFICATION_TELEPHONY_RINGTONE:
        return AUDIO_STREAM_RING;

    case AUDIO_USAGE_NOTIFICATION:
    case AUDIO_USAGE_NOTIFICATION_COMMUNICATION_REQUEST:
    case AUDIO_USAGE_NOTIFICATION_COMMUNICATION_INSTANT:
    case AUDIO_USAGE_NOTIFICATION_COMMUNICATION_DELAYED:
    case AUDIO_USAGE_NOTIFICATION_EVENT:
        return AUDIO_STREAM_NOTIFICATION;

    case AUDIO_USAGE_UNKNOWN:
    default:
        return AUDIO_STREAM_MUSIC;
    }
}
audio_devices_t DLGAudioPolicyManager::getNewOutputDevice(audio_io_handle_t output, bool fromCache)
{
    audio_devices_t device = AUDIO_DEVICE_NONE;

    sp<AudioOutputDescriptor> outputDesc = mOutputs.valueFor(output);

    ssize_t index = mAudioPatches.indexOfKey(outputDesc->mPatchHandle);
    if (index >= 0) {
        sp<AudioPatch> patchDesc = mAudioPatches.valueAt(index);
        if (patchDesc->mUid != mUidCached) {
            ALOGV("getNewOutputDevice() device %08x forced by patch %d",
                  outputDesc->device(), outputDesc->mPatchHandle);
            return outputDesc->device();
        }
    }

    // check the following by order of priority to request a routing change if necessary:
    // 1: the strategy enforced audible is active and enforced on the output:
    //      use device for strategy enforced audible
    // 2: we are in call or the strategy phone is active on the output:
    //      use device for strategy phone
    // 3: the strategy for enforced audible is active but not enforced on the output:
    //      use the device for strategy enforced audible
    // 4: the strategy sonification is active on the output:
    //      use device for strategy sonification
    // 5: the strategy "respectful" sonification is active on the output:
    //      use device for strategy "respectful" sonification
    // 6: the strategy media is active on the output:
    //      use device for strategy media
    // 7: the strategy DTMF is active on the output:
    //      use device for strategy DTMF
    if (outputDesc->isStrategyActive(STRATEGY_ENFORCED_AUDIBLE) &&
        mForceUse[AUDIO_POLICY_FORCE_FOR_SYSTEM] == AUDIO_POLICY_FORCE_SYSTEM_ENFORCED) {
        device = getDeviceForStrategy(STRATEGY_ENFORCED_AUDIBLE, fromCache);
    } else if (isInCall() ||
                    outputDesc->isStrategyActive(STRATEGY_PHONE)) {
        device = getDeviceForStrategy(STRATEGY_PHONE, fromCache);
    } else if (outputDesc->isStrategyActive(STRATEGY_ENFORCED_AUDIBLE)) {
        device = getDeviceForStrategy(STRATEGY_ENFORCED_AUDIBLE, fromCache);
    } else if (outputDesc->isStrategyActive(STRATEGY_SONIFICATION)) {
        device = getDeviceForStrategy(STRATEGY_SONIFICATION, fromCache);
    } else if (outputDesc->isStrategyActive(STRATEGY_SONIFICATION_RESPECTFUL)) {
        device = getDeviceForStrategy(STRATEGY_SONIFICATION_RESPECTFUL, fromCache);
    } else if (outputDesc->isStrategyActive(STRATEGY_MEDIA)) {
        device = getDeviceForStrategy(STRATEGY_MEDIA, fromCache);
    } else if (outputDesc->isStrategyActive(STRATEGY_DTMF)) {
        device = getDeviceForStrategy(STRATEGY_DTMF, fromCache);
    }

    int direct_in_use=0;
    for (size_t i = 0; i < mOutputs.size(); i++) {
        sp<AudioOutputDescriptor> desc = mOutputs.valueAt(i);
        if (desc->mFlags & AUDIO_OUTPUT_FLAG_DIRECT)
            direct_in_use=1;
    }

    audio_devices_t availableOutputDeviceTypes = mAvailableOutputDevices.types();
    ALOGV("[%s %d]outputDesc->mDevice/0x%x device/%x direct_in_use/%d availableOutputDeviceTypes/0x%x isStrategyActive(STRATEGY_MEDIA)/%d fromCache/%d\n", __FUNCTION__,__LINE__,
           outputDesc->mDevice,device,direct_in_use,availableOutputDeviceTypes,outputDesc->isStrategyActive(STRATEGY_MEDIA),fromCache);
    if (outputDesc->mDevice == AUDIO_DEVICE_OUT_SPEAKER && outputDesc->isStrategyActive(STRATEGY_MEDIA) && (device == AUDIO_DEVICE_OUT_AUX_DIGITAL && !get_codec_type("/sys/class/audiodsp/digital_codec")) || direct_in_use)
    {
        device = availableOutputDeviceTypes & AUDIO_DEVICE_OUT_SPEAKER;
        if (device == AUDIO_DEVICE_NONE)
            device = AUDIO_DEVICE_OUT_AUX_DIGITAL;
        if (device == AUDIO_DEVICE_OUT_SPEAKER)
        {
           ALOGV("[%s %d]direct_in_use/%d force change device from %x to %x\n",__FUNCTION__,__LINE__,
           direct_in_use,AUDIO_DEVICE_OUT_AUX_DIGITAL,AUDIO_DEVICE_OUT_SPEAKER);
        }
    }
    ALOGV("[%s %d]selected device %x", __FUNCTION__,__LINE__,device);
    return device;
}

uint32_t DLGAudioPolicyManager::AudioPort::pickSamplingRate() const
{
    // special case for uninitialized dynamic profile
    if (mSamplingRates.size() == 1 && mSamplingRates[0] == 0) {
        return 0;
    }

    // For direct outputs, pick minimum sampling rate: this helps ensuring that the
    // channel count / sampling rate combination chosen will be supported by the connected
    // sink
    if ((mType == AUDIO_PORT_TYPE_MIX) && (mRole == AUDIO_PORT_ROLE_SOURCE) &&
            (mFlags & (AUDIO_OUTPUT_FLAG_DIRECT | AUDIO_OUTPUT_FLAG_COMPRESS_OFFLOAD))) {
        uint32_t samplingRate = UINT_MAX;
        for (size_t i = 0; i < mSamplingRates.size(); i ++) {
            if ((mSamplingRates[i] < samplingRate) && (mSamplingRates[i] > 0)) {
                samplingRate = mSamplingRates[i];
            }
        }
        return (samplingRate == UINT_MAX) ? 0 : samplingRate;
    }

    uint32_t samplingRate = 0;
    uint32_t maxRate = MAX_MIXER_SAMPLING_RATE;

    // For mixed output and inputs, use max mixer sampling rates. Do not
    // limit sampling rate otherwise
    if (mType != AUDIO_PORT_TYPE_MIX) {
        maxRate = UINT_MAX;
    }
    for (size_t i = 0; i < mSamplingRates.size(); i ++) {
        if ((mSamplingRates[i] > samplingRate) && (mSamplingRates[i] <= maxRate)) {
            samplingRate = mSamplingRates[i];
        }
    }
    return samplingRate;
}


}  // namespace android
