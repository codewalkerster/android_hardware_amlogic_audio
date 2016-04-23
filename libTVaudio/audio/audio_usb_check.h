#ifndef __TV_USB_AUDIO_CHECK_H__
#define __TV_USB_AUDIO_CHECK_H__

#ifdef __cplusplus
extern "C" {
#endif

unsigned int GetUsbAudioCheckFlag(void);
float get_android_stream_volume(void);
int set_parameters(char parameters[], char parm_key[]);
int creat_pthread_for_android_check
                    (pthread_t *android_check_ThreadID);
int exit_pthread_for_android_check
                    (pthread_t android_check_ThreadID);

#ifdef __cplusplus
}
#endif

#endif //__TV_USB_AUDIO_CHECK_H__
