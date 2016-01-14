#ifndef __TV_ANDROID_OUT_H__
#define __TV_ANDROID_OUT_H__

#ifdef __cplusplus
extern "C" {
#endif

int new_android_audiotrack(void);
int release_android_audiotrack(void);
int amsysfs_get_sysfs_int(const char *path);


#ifdef __cplusplus
}
#endif

#endif //__TV_ANDROID_OUT_H__
