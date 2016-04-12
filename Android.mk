# Copyright (C) 2011 The Android Open Source Project
#
# Licensed under the Apache License, Version 2.0 (the "License");
# you may not use this file except in compliance with the License.
# You may obtain a copy of the License at
#
#      http://www.apache.org/licenses/LICENSE-2.0
#
# Unless required by applicable law or agreed to in writing, software
# distributed under the License is distributed on an "AS IS" BASIS,
# WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
# See the License for the specific language governing permissions and
# limitations under the License.

ifeq ($(strip $(BOARD_ALSA_AUDIO)),tiny)

	LOCAL_PATH := $(call my-dir)

# The default audio HAL module, which is a stub, that is loaded if no other
# device specific modules are present. The exact load order can be seen in
# libhardware/hardware.c
#
# The format of the name is audio.<type>.<hardware/etc>.so where the only
# required type is 'primary'. Other possibilites are 'a2dp', 'usb', etc.
	include $(CLEAR_VARS)

	LOCAL_MODULE := audio.primary.amlogic
	LOCAL_MODULE_RELATIVE_PATH := hw
	LOCAL_SRC_FILES := \
		audio_hw.c \
		libTVaudio/audio/audio_effect_control.c
	LOCAL_C_INCLUDES += \
		external/tinyalsa/include \
		system/media/audio_utils/include \
		system/media/audio_effects/include \
		system/media/audio_route/include \
		libTVaudio/audio

	LOCAL_SHARED_LIBRARIES := \
		liblog libcutils libtinyalsa \
		libaudioutils libdl libaudioroute libutils
	LOCAL_MODULE_TAGS := optional

	include $(BUILD_SHARED_LIBRARY)
#build for USB audio
#BOARD_USE_USB_AUDIO := 1
	ifeq ($(strip $(BOARD_USE_USB_AUDIO)),true)
		include $(CLEAR_VARS)
		
		LOCAL_MODULE := audio.usb.amlogic
		LOCAL_MODULE_RELATIVE_PATH := hw
		LOCAL_SRC_FILES := \
			usb_audio_hw.c \
			audio_resampler.c
		LOCAL_C_INCLUDES += \
			external/tinyalsa/include \
			system/media/audio_utils/include 
		LOCAL_SHARED_LIBRARIES := liblog libcutils libtinyalsa libaudioutils libutils
		LOCAL_MODULE_TAGS := optional
		
		include $(BUILD_SHARED_LIBRARY)
	endif
#build for hdmi audio HAL
		include $(CLEAR_VARS)
		
		LOCAL_MODULE := audio.hdmi.amlogic
		LOCAL_MODULE_RELATIVE_PATH := hw
		LOCAL_SRC_FILES := \
			hdmi_audio_hw.c
		LOCAL_C_INCLUDES += \
			external/tinyalsa/include \
			system/media/audio_effects/include \
			system/media/audio_utils/include 
			
		LOCAL_SHARED_LIBRARIES := liblog libcutils libtinyalsa libaudioutils libutils
#ifdef DOLBY_UDC_PASSTHROUGH_HDMI_PACK
LOCAL_SRC_FILES += spdifenc_wrap.cpp
LOCAL_C_INCLUDES += \
    $(call include-path-for, audio-utils)
LOCAL_SHARED_LIBRARIES += \
    libaudiospdif
#endif # DOLBY_UDC_PASSTHROUGH_HDMI_PACK
ifdef DOLBY_EAC3_TO_AC3_CONVERTER
LOCAL_SHARED_LIBRARIES += \
    libdlb_converter
endif
LOCAL_SRC_FILES += audio_hw_profile.c
LOCAL_SRC_FILES += audio_hw_utils.c
LOCAL_SRC_FILES += audio_hwsync.c
LOCAL_MODULE_TAGS := optional
include $(BUILD_SHARED_LIBRARY)

#########################################################



#########################################################
# Audio Policy Manager
ifeq ($(USE_CUSTOM_AUDIO_POLICY),1)
include $(CLEAR_VARS)

LOCAL_SRC_FILES := \
	DLGAudioPolicyManager.cpp

LOCAL_SHARED_LIBRARIES := \
	libcutils \
	liblog \
	libutils \
	libmedia \
	libbinder \
	libaudiopolicymanagerdefault \
	libutils

LOCAL_C_INCLUDES := \
	external/tinyalsa/include \
	$(TOPDIR)frameworks/av/services/audiopolicy \
	$(TOPDIR)frameworks/av/services/audiopolicy/managerdefault \
	$(TOPDIR)frameworks/av/services/audiopolicy/engine/interface \
	$(TOPDIR)frameworks/av/services/audiopolicy/common/managerdefinitions/include \
	$(TOPDIR)frameworks/av/services/audiopolicy/common/include


LOCAL_MODULE := libaudiopolicymanager
LOCAL_MODULE_TAGS := optional

include $(BUILD_SHARED_LIBRARY)
endif # USE_CUSTOM_AUDIO_POLICY

endif # BOARD_ALSA_AUDIO
include $(call all-makefiles-under,$(LOCAL_PATH))
