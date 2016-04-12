#include <errno.h>
#include <pthread.h>
#include <stdint.h>
#include <sys/time.h>
#include <stdlib.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <math.h>
#include <cutils/log.h>
#include <cutils/str_parms.h>
#include <cutils/properties.h>
#include <hardware/hardware.h>
#include <system/audio.h>
#include <hardware/audio.h>
/*
  type : 0 -> playback, 1 -> capture
*/
#define MAX_CARD_NUM    2
int get_external_card(int type)
{
    int card_num = 1;       // start num, 0 is defualt sound card.
    struct stat card_stat;
    char fpath[256];
    int ret;
    while (card_num <= MAX_CARD_NUM) {
        snprintf(fpath, sizeof(fpath), "/proc/asound/card%d", card_num);
        ret = stat(fpath, &card_stat);
        if (ret < 0) {
            ret = -1;
        } else {
            snprintf(fpath, sizeof(fpath), "/dev/snd/pcmC%uD0%c", card_num,
                     type ? 'c' : 'p');
            ret = stat(fpath, &card_stat);
            if (ret == 0) {
                return card_num;
            }
        }
        card_num++;
    }
    return ret;
}

int
get_aml_card()
{
    int card = -1, err = 0;
    int fd = -1;
    unsigned fileSize = 512;
    char *read_buf = NULL, *pd = NULL;
    static const char *const SOUND_CARDS_PATH = "/proc/asound/cards";
    fd = open(SOUND_CARDS_PATH, O_RDONLY);
    if (fd < 0) {
        ALOGE("ERROR: failed to open config file %s error: %d\n",
              SOUND_CARDS_PATH, errno);
        close(fd);
        return -EINVAL;
    }

    read_buf = (char *) malloc(fileSize);
    if (!read_buf) {
        ALOGE("Failed to malloc read_buf");
        close(fd);
        return -ENOMEM;
    }
    memset(read_buf, 0x0, fileSize);
    err = read(fd, read_buf, fileSize);
    if (fd < 0) {
        ALOGE("ERROR: failed to read config file %s error: %d\n",
              SOUND_CARDS_PATH, errno);
        close(fd);
        return -EINVAL;
    }
    pd = strstr(read_buf, "AML");
    card = *(pd - 3) - '0';
OUT:
    free(read_buf);
    close(fd);
    return card;
}

int
get_spdif_port()
{
    int port = -1, err = 0;
    int fd = -1;
    unsigned fileSize = 512;
    char *read_buf = NULL, *pd = NULL;
    static const char *const SOUND_PCM_PATH = "/proc/asound/pcm";
    fd = open(SOUND_PCM_PATH, O_RDONLY);
    if (fd < 0) {
        ALOGE("ERROR: failed to open config file %s error: %d\n",
              SOUND_PCM_PATH, errno);
        close(fd);
        return -EINVAL;
    }
    read_buf = (char *) malloc(fileSize);
    if (!read_buf) {
        ALOGE("Failed to malloc read_buf");
        close(fd);
        return -ENOMEM;
    }
    memset(read_buf, 0x0, fileSize);
    err = read(fd, read_buf, fileSize);
    if (fd < 0) {
        ALOGE("ERROR: failed to read config file %s error: %d\n",
              SOUND_PCM_PATH, errno);
        close(fd);
        return -EINVAL;
    }
    pd = strstr(read_buf, "SPDIF");
    port = *(pd - 3) - '0';
OUT:
    free(read_buf);
    close(fd);
    return port;
}


/*
CodingType MaxChannels SamplingFreq SampleSize
PCM, 2 ch, 32/44.1/48/88.2/96/176.4/192 kHz, 16/20/24 bit
PCM, 8 ch, 32/44.1/48/88.2/96/176.4/192 kHz, 16/20/24 bit
AC-3, 8 ch, 32/44.1/48 kHz,  bit
DTS, 8 ch, 44.1/48 kHz,  bit
OneBitAudio, 2 ch, 44.1 kHz,  bit
Dobly_Digital+, 8 ch, 44.1/48 kHz, 16 bit
DTS-HD, 8 ch, 44.1/48/88.2/96/176.4/192 kHz, 16 bit
MAT, 8 ch, 32/44.1/48/88.2/96/176.4/192 kHz, 16 bit
*/
char*  get_hdmi_sink_cap(const char *keys)
{
    int i = 0;
    char * infobuf = NULL;
    int channel = 0;
    int dgraw = 0;
    int fd = -1;
    int size = 0;
    char *aud_cap = NULL;
    infobuf = (char *)malloc(1024 * sizeof(char));
    if (infobuf == NULL) {
        ALOGE("malloc buffer failed\n");
        goto fail;
    }
    aud_cap = (char*)malloc(1024);
    if (aud_cap == NULL) {
        ALOGE("malloc buffer failed\n");
        goto fail;
    }
    memset(aud_cap, 0, 1024);
    memset(infobuf, 0, 1024);
    fd = open("/sys/class/amhdmitx/amhdmitx0/aud_cap", O_RDONLY);
    if (fd >= 0) {
        int nread = read(fd, infobuf, 1024);
        /* check the format cap */
        if (strstr(keys, AUDIO_PARAMETER_STREAM_SUP_FORMATS)) {
            size += sprintf(aud_cap, "=");
            if (mystrstr(infobuf, "Dobly_Digital+")) {
                size += sprintf(aud_cap + size, "%s|", "AUDIO_FORMAT_E_AC3");
            }
            if (mystrstr(infobuf, "AC-3")) {
                size += sprintf(aud_cap + size, "%s|", "AUDIO_FORMAT_AC3");
            }
            if (mystrstr(infobuf, "DTS-HD")) {
                size += sprintf(aud_cap + size, "%s|", "AUDIO_FORMAT_DTS|AUDIO_FORMAT_DTSHD");
            } else if (mystrstr(infobuf, "DTS")) {
                size += sprintf(aud_cap + size, "%s|", "AUDIO_FORMAT_DTS");
            }
            if (mystrstr(infobuf, "MAT")) {
                size += sprintf(aud_cap + size, "%s|", "AUDIO_FORMAT_TRUEHD");
            }
        }
        /*check the channel cap */
        else if (strstr(keys, AUDIO_PARAMETER_STREAM_SUP_CHANNELS)) {
            /* take the 2ch suppported as default */
            size += sprintf(aud_cap, "=%s|", "AUDIO_CHANNEL_OUT_STEREO");
            if (mystrstr(infobuf, "PCM, 8 ch")) {
                size += sprintf(aud_cap + size, "%s|", "AUDIO_CHANNEL_OUT_5POINT1|AUDIO_CHANNEL_OUT_7POINT1");
            } else if (mystrstr(infobuf, "PCM, 6 ch")) {
                size += sprintf(aud_cap + size, "%s|", "AUDIO_CHANNEL_OUT_5POINT1");
            }
        } else if (strstr(keys, AUDIO_PARAMETER_STREAM_SUP_SAMPLING_RATES)) {
            /* take the 32/44.1/48 khz suppported as default */
            size += sprintf(aud_cap, "=%s|", "32000|44100|48000");
            if (mystrstr(infobuf, "88.2")) {
                size += sprintf(aud_cap + size, "%s|", "88200");
            }
            if (mystrstr(infobuf, "96")) {
                size += sprintf(aud_cap + size, "%s|", "96000");
            }
            if (mystrstr(infobuf, "176.4")) {
                size += sprintf(aud_cap + size, "%s|", "176400");
            }
            if (mystrstr(infobuf, "192")) {
                size += sprintf(aud_cap + size, "%s|", "192000");
            }
        }
    }
    if (infobuf) {
        free(infobuf);
    }
    if (fd >= 0) {
        close(fd);
    }
    return aud_cap;
fail:
    if (aud_cap) {
        free(aud_cap);
    }
    if (infobuf) {
        free(infobuf);
    }
    return NULL;
}


