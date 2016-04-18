#ifndef __TV_AUDIO_AMAUDIO_H__
#define __TV_AUDIO_AMAUDIO_H__

#include "audio/aml_audio.h"

int amSetAudioDelay(int delay_ms);
int amGetAudioDelay(void);
int amAudioOpen(unsigned int sr, int input_device, int output_device);
int amAudioClose(void);
int amAudioSetInputSr(unsigned int sr, int input_device, int output_device);
int amAudioSetOutputMode(int mode);
int amAudioSetMusicGain(int gain);
int amAudioSetLeftGain(int gain);
int amAudioSetRightGain(int gain);

int amAudioSetEQGain(int gain_val_buf[], int buf_item_cnt);
int amAudioGetEQGain(int gain_val_buf[], int buf_item_cnt);
int amAudioSetEQEnable(int en_val);
int amAudioGetEQEnable();

int amAudioSetSRSSurroundSwitch(int switch_val);
int amAudioSetSRSSurroundGain(int gain_val);
int amAudioSetSRSTrubassSwitch(int switch_val);
int amAudioSetSRSTrubassGain(int gain_val);
int amAudioSetSRSDialogClaritySwitch(int switch_val);
int amAudioSetSRSDialogClarityGain(int gain_val);
int amAudioSetSRSDefinitionGain(int gain_val);
int amAudioSetSRSTrubassSpeakerSize(int tmp_val);
int amAudioSetSRSGain(int input_gain, int output_gain);

int amAudioSetDumpDataFlag(int tmp_flag);
int amAudioGetDumpDataFlag();

// gain is in dB float format
int amAudioSetPreGain(float gain);
int amAudioGetPreGain(float *gain);
int amAudioSetPreMute(uint mute);
int amAudioGetPreMute(uint *mute);
#endif //__TV_AUDIO_AMAUDIO_H__
