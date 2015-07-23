#ifndef __TV_USB_AUDIO_CHECK_H__
#define __TV_USB_AUDIO_CHECK_H__

#define CC_USB_AUDIO_PLAYBACK_BIT_IND    (0)
#define CC_USB_AUDIO_CAPTURE_BIT_IND     (1)

#ifdef __cplusplus
extern "C" {
#endif

int createMonitorUsbHostBusThread();
int GetUsbAudioCheckFlag();

#ifdef __cplusplus
}
#endif

#endif //__TV_USB_AUDIO_CHECK_H__
