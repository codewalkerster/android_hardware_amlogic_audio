#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <pthread.h>
#include <fcntl.h>
#include <errno.h>
#include <strings.h>
#include <sys/ioctl.h>
#include <cutils/log.h>
#include <cutils/properties.h>
#include <media/AudioTrack.h>

#include "audio_usb_check.h"
#include "android_out.h"
#include "aml_audio.h"
#include "DDP_media_source.h"

using namespace android;

#define LOG_TAG "android_out"

static AudioTrack *glpTracker = NULL;
static AudioTrack *glpTracker_raw = NULL;

static sp<AudioTrack> gmpAudioTracker;
static sp<AudioTrack> gmpAudioTracker_raw;

extern struct circle_buffer android_out_buffer;
extern struct circle_buffer DDP_out_buffer;
extern struct circle_buffer DD_out_buffer;
extern int output_record_enable;
extern pthread_mutex_t device_change_lock;

int I2S_state = 0;
static int raw_start_flag = 0;
static int last_raw_flag = 0;
static int amsysfs_set_sysfs_int(const char *path, int val) {
    int fd;
    int bytes;
    char bcmd[16];
    fd = open(path, O_CREAT | O_RDWR | O_TRUNC, 0644);
    if (fd >= 0) {
        sprintf(bcmd, "%d", val);
        bytes = write(fd, bcmd, strlen(bcmd));
        close(fd);
        return 0;
    } else {
        ALOGE("unable to open file %s,err: %s", path, strerror(errno));
    }
    return -1;
}

int amsysfs_get_sysfs_int(const char *path) {
    int fd;
    int val = 0;
    char bcmd[16];
    fd = open(path, O_RDONLY);
    if (fd >= 0) {
        read(fd, bcmd, sizeof(bcmd));
        val = strtol(bcmd, NULL, 10);
        close(fd);
    }else {
        ALOGE("unable to open file %s,err: %s", path, strerror(errno));
    }
    return val;
}

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
    int bytes_raw = 0;
    char raw_data[16384];
    int user_raw_enable = amsysfs_get_sysfs_int("/sys/class/audiodsp/digital_raw");
    pthread_mutex_lock(&device_change_lock);

    if (GetOutputdevice() == 1) {
        if (raw_start_flag == 1) {
            glpTracker_raw->pause();
            amsysfs_set_sysfs_int("/sys/class/audiodsp/digital_codec", 0);
            raw_start_flag = 0;
        }
        bytes = buffer_read(&android_out_buffer, (char *) buffer->raw,
                buffer->size);
        if (bytes < 0)
            buffer->size = 0;
    } else if (GetOutputdevice() == 2) {
        bytes = buffer_read(&DDP_out_buffer, (char *) buffer->raw,
                buffer->size);
        if (bytes < 0)
            buffer->size = 0;

        //here handle raw data
        if (user_raw_enable == 1) {
            if (raw_start_flag == 0) {
                ALOGI("glpTracker_raw start \n");
                amsysfs_set_sysfs_int("/sys/class/audiodsp/digital_codec", 2);
                glpTracker_raw->start();
                raw_start_flag = 1;
            }
            bytes_raw = buffer_read(&DD_out_buffer, (char *) raw_data,
                    buffer->size);
            ALOGV("raw read got %d\n",bytes_raw);
            if ( bytes_raw > 0) {
                glpTracker_raw->write(raw_data,bytes_raw);
            }
        } else {
            if (raw_start_flag == 1) {
                glpTracker_raw->pause();
                amsysfs_set_sysfs_int("/sys/class/audiodsp/digital_codec", 0);
                raw_start_flag = 0;
            }
        }
    } else {
        if (raw_start_flag == 1) {
            glpTracker_raw->pause();
            amsysfs_set_sysfs_int("/sys/class/audiodsp/digital_codec", 0);
            raw_start_flag = 0;
        }
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
    if (glpTracker != NULL ) {
        glpTracker->stop();
        glpTracker = NULL;
    }

    if (gmpAudioTracker != NULL ) {
        gmpAudioTracker.clear();
    }
    //raw here
    if (glpTracker_raw != NULL ) {
        if (raw_start_flag == 1)
            glpTracker_raw->stop();
        raw_start_flag = 0;
        glpTracker_raw = NULL;
    }
    if (gmpAudioTracker_raw != NULL ) {
        gmpAudioTracker_raw.clear();
    }
    int ret;
    ret=property_set("media.libplayer.dtsopt0", "0");
    ALOGI("property_set<media.libplayer.dtsopt0> ret/%d\n",ret);
    amsysfs_set_sysfs_int("/sys/class/audiodsp/digital_codec",0);
    // raw end
    if (last_raw_flag  == 2) {
        ALOGI("change back digital raw to 2 for hdmi pass through\n");
        amsysfs_set_sysfs_int("/sys/class/audiodsp/digital_raw",2);
        last_raw_flag = 0;
    }
    return 0;
}

static int AudioTrackInit(void) {
    status_t Status;

    ALOGD("%s, entering...\n", __FUNCTION__);

    I2S_state = 0;

    gmpAudioTracker = new AudioTrack();
    if (gmpAudioTracker == NULL) {
        ALOGE("%s, new AudioTrack failed.\n", __FUNCTION__);
        return -1;
    }
    glpTracker = gmpAudioTracker.get();
    //raw here
    gmpAudioTracker_raw = new AudioTrack();
    if (gmpAudioTracker_raw == NULL) {
        ALOGE("%s, new gmpAudioTracker_raw failed.\n", __FUNCTION__);
        return -1;
    }
    glpTracker_raw = gmpAudioTracker_raw.get();
    int ret;
    ret=property_set("media.libplayer.dtsopt0", "1");
    ALOGI("property_set<media.libplayer.dtsopt0> ret/%d\n",ret);
    // raw end

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
    // raw here
    Status = glpTracker_raw->set(AUDIO_STREAM_MUSIC, 48000, AUDIO_FORMAT_AC3,
            AUDIO_CHANNEL_OUT_STEREO, 0, AUDIO_OUTPUT_FLAG_DIRECT,
            NULL/* AudioTrackCallback_raw */, NULL, 0, 0, false, 0);
    if (Status != NO_ERROR) {
        ALOGE("%s, AudioTrack raw set failed.\n", __FUNCTION__);
        AudioTrackRelease();
        return -1;
    }
    Status = glpTracker_raw->initCheck();
    if (Status != NO_ERROR) {
        ALOGE("%s, AudioTrack raw initCheck failed.\n", __FUNCTION__);
        AudioTrackRelease();
        return -1;
    }
    //raw end

    glpTracker->start();

    Status = glpTracker->setVolume(1.0, 1.0);
    if (Status != NO_ERROR) {
        ALOGE("%s, AudioTrack setVolume failed.\n", __FUNCTION__);

        AudioTrackRelease();
        return -1;
    }
    ALOGD("%s, exit...\n", __FUNCTION__);

    int digital_raw = amsysfs_get_sysfs_int("/sys/class/audiodsp/digital_raw");
    if (digital_raw == 2) {
        ALOGI("change digital raw to 2 for spdif pass through\n");
        amsysfs_set_sysfs_int("/sys/class/audiodsp/digital_raw",1);
        last_raw_flag = 2;
    }
    return 0;
}

int new_android_audiotrack(void) {
    return AudioTrackInit();
}

int release_android_audiotrack(void) {
    return AudioTrackRelease();
}

