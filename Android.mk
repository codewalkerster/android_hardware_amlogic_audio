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
	LOCAL_MODULE_PATH := $(TARGET_OUT_SHARED_LIBRARIES)/hw
	LOCAL_SRC_FILES := audio_hw.c
	LOCAL_C_INCLUDES += \
		external/tinyalsa/include \
		system/media/audio_utils/include \
		system/media/audio_effects/include
	LOCAL_SHARED_LIBRARIES := liblog libcutils libtinyalsa libaudioutils libdl
	LOCAL_MODULE_TAGS := optional

#CONFIG_AML_CODEC
	ifeq ($(BOARD_AUDIO_CODEC),rt5631)
		LOCAL_CFLAGS += -DAML_AUDIO_RT5631
	endif
	
	ifeq ($(BOARD_AUDIO_CODEC),wm8960)
		LOCAL_CFLAGS += -DAML_AUDIO_WM8960
	endif
	
	ifeq ($(BOARD_AUDIO_CODEC),rt3261)
		LOCAL_CFLAGS += -DAML_AUDIO_RT3261
	endif

	include $(BUILD_SHARED_LIBRARY)

# The stub audio policy HAL module that can be used as a skeleton for
# new implementations.
#include $(CLEAR_VARS)

#LOCAL_MODULE := audio_policy.amlogic
#LOCAL_MODULE_PATH := $(TARGET_OUT_SHARED_LIBRARIES)/hw
#LOCAL_SRC_FILES := audio_policy.c
#LOCAL_SHARED_LIBRARIES := liblog libcutils
#LOCAL_MODULE_TAGS := optional

#include $(BUILD_SHARED_LIBRARY)

endif
