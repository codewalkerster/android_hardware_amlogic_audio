#include <errno.h>
#include <pthread.h>
#include <stdint.h>
#include <sys/time.h>
#include <stdlib.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <time.h>
#include <utils/Timers.h>
#include <cutils/log.h>
#include <cutils/str_parms.h>
#include <cutils/properties.h>
#include <linux/ioctl.h>
#include <hardware/hardware.h>
#include <system/audio.h>
#include <hardware/audio.h>
#include <sound/asound.h>
#include <tinyalsa/asoundlib.h>

#include "audio_hw_utils.h"

#include "audio_hwsync.h"
#include "hdmi_audio_hw.h"

#define LOG_TAG "audio_hw_utils"
#define LOG_NDEBUG 0
#ifdef LOG_NDEBUG_FUNCTION
#define LOGFUNC(...) ((void)0)
#else
#define LOGFUNC(...) (ALOGD(__VA_ARGS__))
#endif
int get_sysfs_int16(const char *path,unsigned *value)
{
    int fd;
    char valstr[64];
    unsigned  val = 0;
    fd = open(path, O_RDONLY);
    if (fd >= 0) {
        memset(valstr, 0, 64);
        read(fd, valstr, 64 - 1);
        valstr[strlen(valstr)] = '\0';
        close(fd);
    } else {
        ALOGE("unable to open file %s\n", path);
        return -1;
    }
    if (sscanf(valstr, "0x%lx", &val) < 1) {
        ALOGE("unable to get pts from: %s", valstr);
        return -1;
    }
    *value = val;
    return 0;
}

int sysfs_set_sysfs_str(const char *path, const char *val)
{
    int fd;
    int bytes;
    fd = open(path, O_CREAT | O_RDWR | O_TRUNC, 0644);
    if (fd >= 0) {
        bytes = write(fd, val, strlen(val));
        close(fd);
        return 0;
    } else {
        ALOGE("unable to open file %s,err: %s", path, strerror(errno));
    }
    return -1;
}

int get_sysfs_int (const char *path)
{
  int val = 0;
  int fd = open (path, O_RDONLY);
  if (fd >= 0)
	{
	  char bcmd[16];
	  read (fd, bcmd, sizeof (bcmd));
	  val = strtol (bcmd, NULL, 10);
	  close (fd);
	}
  else
	{
	  LOGFUNC ("[%s]open %s node failed! return 0\n", path, __FUNCTION__);
	}
  return val;
}
int mystrstr(char *mystr,char *substr) {
    int i=0;
    int j=0;
    int score = 0;
    int substrlen = strlen(substr);
    int ok = 0;
    for (i =0;i < 1024 - substrlen;i++) {
		for (j = 0;j < substrlen;j++) {
			score += (substr[j] == mystr[i+j])?1:0;
		}
		if (score == substrlen) {
		   ok = 1;
                   break;
		}
		score = 0;
        }
	return ok;
}
void set_codec_type(int type)
{
    char buf[16];
    int fd = open ("/sys/class/audiodsp/digital_codec", O_WRONLY);

    if (fd >= 0) {
        memset(buf, 0, sizeof(buf));
        snprintf(buf, sizeof(buf), "%d", type);

        write(fd, buf, sizeof(buf));
        close(fd);
    }
}

int get_codec_type(int format)
{
    switch (format) {
        case AUDIO_FORMAT_AC3:
        return TYPE_AC3;
        case AUDIO_FORMAT_E_AC3:
        return TYPE_EAC3;
        case AUDIO_FORMAT_DTS:
        return TYPE_DTS;
	 case AUDIO_FORMAT_DTS_HD:
	 return TYPE_DTS_HD;
	 case AUDIO_FORMAT_TRUEHD:
	 return TYPE_TRUE_HD;
        case AUDIO_FORMAT_PCM:
        return 0;
        default:
        return 0;
    }
}
int getprop_bool (const char *path)
{
  char buf[PROPERTY_VALUE_MAX];
  int ret = -1;

  ret = property_get (path, buf, NULL);
  if (ret > 0)
	{
	  if (strcasecmp (buf, "true") == 0 || strcmp (buf, "1") == 0)
	return 1;
	}
  return 0;
}

