/*
 * wm8960_mixer_ctrl.h  --  Wm8960 Mixer control for Android ICS
 *
 * Copyright 2012 Amlogic Corp.
 *
 */

#ifndef _WM8960_MIXER_CTRL_H_
#define _WM8960_MIXER_CTRL_H_

struct route_setting
{
    char *ctl_name;
    int intval;
    char *strval;
};



#define MIXER_LEFT_BOOST_MIXER_LINPUT1_SWITCH "Left Boost Mixer LINPUT1 Switch"
#define MIXER_LEFT_INPUT_MIXER_BOOST_SWITCH "Left Input Mixer Boost Switch"
#define MIXER_LEFT_BOOST_MIXER_LINPUT3_SWITCH "Left Boost Mixer LINPUT3 Switch"
#define MIXER_LEFT_BOOST_MIXER_LINPUT2_SWITCH "Left Boost Mixer LINPUT2 Switch"  
#define MIXER_CAPTURE_SWITCH "Capture Switch"
#define MIXER_CAPTURE_VOLUME "Capture Volume"
#define MIXER_ADC_HIGH_PASS_FILTER_SWITCH "ADC High Pass Filter Switch"
#define MIXER_CAPTURE_VOLUME_ZC_SWITCH "Capture Volume ZC Switch"
#define MIXER_ADC_OUTPUT_SELECT "ADC Output Select"
#define MIXER_ADC_PCM_CAPTURE_VOLUME "ADC PCM Capture Volume"
#define MIXER_LEFT_OUTPUT_MIXER_PCM_PLAYBACK_SWITCH "Left Output Mixer PCM Playback Switch"
#define MIXER_RIGHT_OUTPUT_MIXER_PCM_PLAYBACK_SWITCH "Right Output Mixer PCM Playback Switch"
#define MIXER_HEADPHONE_PLAYBACK_VOLUME "Headphone Playback Volume"
#define MIXER_SPEAKER_PLAYBACK_VOLUME "Speaker Playback Volume"
#define MIXER_SPEAK_DC_VOLUME "Speaker DC Volume"
#define MIXER_SPEAKER_AC_VOLUME "Speaker AC Volume"
#define MIXER_DAC_MONO_MIX "DAC Mono Mix"
#define MIXER_MONO_OUTPUT_MIXER_LEFT_SWITCH "Mono Output Mixer Left Switch"
#define MIXER_MONO_OUTPUT_MIXER_RIGHT_SWITCH "Mono Output Mixer Right Switch"


struct route_setting mic_input[] = {
    {
        .ctl_name = MIXER_LEFT_BOOST_MIXER_LINPUT1_SWITCH,
        .intval = 1,
    },
    {
        .ctl_name = MIXER_LEFT_INPUT_MIXER_BOOST_SWITCH,
        .intval = 1,
    },
    {
        .ctl_name = MIXER_LEFT_BOOST_MIXER_LINPUT3_SWITCH,
        .intval = 0,
    },
    {
        .ctl_name = MIXER_LEFT_BOOST_MIXER_LINPUT2_SWITCH,
        .intval = 1,
    },    
    {
        .ctl_name = MIXER_CAPTURE_SWITCH,
        .intval = 0,
    },
    {
        .ctl_name = MIXER_CAPTURE_VOLUME,
        .intval = 40,
    },
    {
        .ctl_name = MIXER_ADC_HIGH_PASS_FILTER_SWITCH,
        .intval = 1,
    },
    {
        .ctl_name = MIXER_CAPTURE_VOLUME_ZC_SWITCH,
        .intval = 1,
    },
    {
        .ctl_name = MIXER_ADC_OUTPUT_SELECT,
        .intval = 1,
    },
    {
        .ctl_name = MIXER_ADC_PCM_CAPTURE_VOLUME,
        .intval = 2,
    },
    {
        .ctl_name = MIXER_LEFT_OUTPUT_MIXER_PCM_PLAYBACK_SWITCH,
        .intval = 1,
    },
    {
        .ctl_name = MIXER_RIGHT_OUTPUT_MIXER_PCM_PLAYBACK_SWITCH,
        .intval = 1,
    },
    {
        .ctl_name = MIXER_HEADPHONE_PLAYBACK_VOLUME,
        .intval = 0,
    },
    {
        .ctl_name = MIXER_SPEAKER_PLAYBACK_VOLUME,
        .intval = 125,
    },
    {
        .ctl_name = MIXER_SPEAK_DC_VOLUME,
        .intval = 3,
    },
    {
        .ctl_name = MIXER_SPEAKER_AC_VOLUME,
        .intval = 3,
    },
    {
        .ctl_name = MIXER_DAC_MONO_MIX,
        .intval = 1,
    },
    {
        .ctl_name = MIXER_MONO_OUTPUT_MIXER_LEFT_SWITCH,
        .intval = 1,
    },
    {
        .ctl_name = MIXER_MONO_OUTPUT_MIXER_RIGHT_SWITCH,
        .intval = 1,
    },
    {
        .ctl_name = NULL,
    },
};

struct route_setting line_input[] = {
    {
        .ctl_name = NULL,
    },
};

struct route_setting output_speaker[] = {
	/*
    {
        .ctl_name = MIXER_LEFT_BOOST_MIXER_LINPUT1_SWITCH,
        .intval = 1,
    },
    {
        .ctl_name = MIXER_LEFT_INPUT_MIXER_BOOST_SWITCH,
        .intval = 1,
    },
    {
        .ctl_name = MIXER_LEFT_BOOST_MIXER_LINPUT3_SWITCH,
        .intval = 0,
    },
    {
        .ctl_name = MIXER_LEFT_BOOST_MIXER_LINPUT2_SWITCH,
        .intval = 1,
    },    
    {
        .ctl_name = MIXER_CAPTURE_SWITCH,
        .intval = 0,
    },
    {
        .ctl_name = MIXER_CAPTURE_VOLUME,
        .intval = 40,
    },
    {
        .ctl_name = MIXER_ADC_HIGH_PASS_FILTER_SWITCH,
        .intval = 1,
    },
    {
        .ctl_name = MIXER_CAPTURE_VOLUME_ZC_SWITCH,
        .intval = 1,
    },
    {
        .ctl_name = MIXER_ADC_OUTPUT_SELECT,
        .intval = 1,
    },
    {
        .ctl_name = MIXER_ADC_PCM_CAPTURE_VOLUME,
        .intval = 2,
    },
    */
    {
        .ctl_name = MIXER_LEFT_OUTPUT_MIXER_PCM_PLAYBACK_SWITCH,
        .intval = 1,
    },
    {
        .ctl_name = MIXER_RIGHT_OUTPUT_MIXER_PCM_PLAYBACK_SWITCH,
        .intval = 1,
    },
    {
        .ctl_name = MIXER_HEADPHONE_PLAYBACK_VOLUME,
        .intval = 0,
    },
    {
        .ctl_name = MIXER_SPEAKER_PLAYBACK_VOLUME,
        .intval = 125,
    },
    {
        .ctl_name = MIXER_SPEAK_DC_VOLUME,
        .intval = 3,
    },
    {
        .ctl_name = MIXER_SPEAKER_AC_VOLUME,
        .intval = 3,
    },
    {
        .ctl_name = MIXER_DAC_MONO_MIX,
        .intval = 1,
    },
    {
        .ctl_name = MIXER_MONO_OUTPUT_MIXER_LEFT_SWITCH,
        .intval = 1,
    },
    {
        .ctl_name = MIXER_MONO_OUTPUT_MIXER_RIGHT_SWITCH,
        .intval = 1,
    },
    {
        .ctl_name = NULL,
    },
};

struct route_setting output_headphone[] = { 
	/*   
    {
        .ctl_name = MIXER_LEFT_BOOST_MIXER_LINPUT1_SWITCH,
        .intval = 1,
    },
    {
        .ctl_name = MIXER_LEFT_INPUT_MIXER_BOOST_SWITCH,
        .intval = 0,
    },
    {
        .ctl_name = MIXER_LEFT_BOOST_MIXER_LINPUT3_SWITCH,
        .intval = 0,
    },
    {
        .ctl_name = MIXER_LEFT_BOOST_MIXER_LINPUT2_SWITCH,
        .intval = 1,
    },    
    {
        .ctl_name = MIXER_CAPTURE_SWITCH,
        .intval = 0,
    },
    {
        .ctl_name = MIXER_CAPTURE_VOLUME,
        .intval = 40,
    },
    {
        .ctl_name = MIXER_ADC_HIGH_PASS_FILTER_SWITCH,
        .intval = 1,
    },
    {
        .ctl_name = MIXER_CAPTURE_VOLUME_ZC_SWITCH,
        .intval = 1,
    },
    {
        .ctl_name = MIXER_ADC_OUTPUT_SELECT,
        .intval = 1,
    },
    {
        .ctl_name = MIXER_ADC_PCM_CAPTURE_VOLUME,
        .intval = 2,
    },
    */
    {
        .ctl_name = MIXER_LEFT_OUTPUT_MIXER_PCM_PLAYBACK_SWITCH,
        .intval = 1,
    },
    {
        .ctl_name = MIXER_RIGHT_OUTPUT_MIXER_PCM_PLAYBACK_SWITCH,
        .intval = 1,
    },
    {
        .ctl_name = MIXER_SPEAKER_PLAYBACK_VOLUME,
        .intval = 0,
    },
    {
        .ctl_name = MIXER_DAC_MONO_MIX,
        .intval = 0,
    },
    {
        .ctl_name = MIXER_MONO_OUTPUT_MIXER_LEFT_SWITCH,
        .intval = 0,
    },
    {
        .ctl_name = MIXER_MONO_OUTPUT_MIXER_RIGHT_SWITCH,
        .intval = 0,
    },
    {
        .ctl_name = MIXER_HEADPHONE_PLAYBACK_VOLUME,
        .intval = 112,
    },
    {
        .ctl_name = NULL,
    },
};

struct route_setting output_headset[] = {
	/*
    {
        .ctl_name = MIXER_LEFT_BOOST_MIXER_LINPUT1_SWITCH,
        .intval = 1,
    },
    {
        .ctl_name = MIXER_LEFT_INPUT_MIXER_BOOST_SWITCH,
        .intval = 0,
    },
    {
        .ctl_name = MIXER_LEFT_BOOST_MIXER_LINPUT3_SWITCH,
        .intval = 0,
    },
    {
        .ctl_name = MIXER_LEFT_BOOST_MIXER_LINPUT2_SWITCH,
        .intval = 1,
    },    
    {
        .ctl_name = MIXER_CAPTURE_SWITCH,
        .intval = 0,
    },
    {
        .ctl_name = MIXER_CAPTURE_VOLUME,
        .intval = 40,
    },
    {
        .ctl_name = MIXER_ADC_HIGH_PASS_FILTER_SWITCH,
        .intval = 1,
    },
    {
        .ctl_name = MIXER_CAPTURE_VOLUME_ZC_SWITCH,
        .intval = 1,
    },
    {
        .ctl_name = MIXER_ADC_OUTPUT_SELECT,
        .intval = 1,
    },
    {
        .ctl_name = MIXER_ADC_PCM_CAPTURE_VOLUME,
        .intval = 2,
    },
    */
    {
        .ctl_name = MIXER_LEFT_OUTPUT_MIXER_PCM_PLAYBACK_SWITCH,
        .intval = 1,
    },
    {
        .ctl_name = MIXER_RIGHT_OUTPUT_MIXER_PCM_PLAYBACK_SWITCH,
        .intval = 1,
    },

    {
        .ctl_name = MIXER_SPEAKER_PLAYBACK_VOLUME,
        .intval = 0,
    },
    {
        .ctl_name = MIXER_DAC_MONO_MIX,
        .intval = 0,
    },
    {
        .ctl_name = MIXER_MONO_OUTPUT_MIXER_LEFT_SWITCH,
        .intval = 0,
    },
    {
        .ctl_name = MIXER_MONO_OUTPUT_MIXER_RIGHT_SWITCH,
        .intval = 0,
    },
    {
        .ctl_name = MIXER_HEADPHONE_PLAYBACK_VOLUME,
        .intval = 112,
    },
    {
        .ctl_name = NULL,
    },
};


#endif //_WM8960_MIXER_CTRL_H_

