LOCAL_PATH:= $(call my-dir)

include $(CLEAR_VARS)

LOCAL_MODULE := aml_audio_test

LOCAL_SHARED_LIBRARIES := libcutils libutils libtinyalsa libdl \
    libmedia libbinder libusbhost libstagefright

LOCAL_C_INCLUDES := \
    external/tinyalsa/include \
    frameworks/av/include/media/stagefright \
    frameworks/native/include/media/openmax \
    $(LOCAL_PATH)/ \

LOCAL_SRC_FILES := \
    aml_audio.c \
    audio_effect_control.c \
    android_out.cpp \
    audio_amaudio.cpp \
    audio_usb_check.cpp \
    amaudio_main.cpp \
    DDP_media_source.cpp \
    DTSHD_media_source.cpp \
LOCAL_CFLAGS := -DANDROID_PLATFORM_SDK_VERSION=$(PLATFORM_SDK_VERSION)

LOCAL_MODULE_TAGS := optional
LOCAL_PRELINK_MODULE := false

include $(BUILD_EXECUTABLE)
