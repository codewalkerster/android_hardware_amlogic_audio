/*
 * rt5631_mixer_ctrl.h  --  RT5631 Mixer control for Android ICS
 *
 * Copyright 2012 Amlogic Corp.
 *
 */

#ifndef _RT5631_MIXER_CTRL_H_
#define _RT5631_MIXER_CTRL_H_

struct route_setting
{
    char *ctl_name;
    int intval;
    char *strval;
};

/* Mixer control names */
/* INT type */
#define MIXER_HP_PLAYBACK_VOLUME            "HP Playback Volume"
#define MIXER_SPK_PLAYBACK_VOLUME           "Speaker Playback Volume"
#define MIXER_MIC1_BOOST                    "MIC1 Boost"

/* ENUM type */
#define MIXER_JACK_FUNC_ENUM                "Jack Function"
#define MIXER_SPEAKER_FUNC_ENUM             "Speaker Function"
#define MIXER_R_HPVOL_MUX_ENUM              "Right HPVOL Mux"
#define MIXER_L_HPVOL_MUX_ENUM              "Left HPVOL Mux"
#define MIXER_R_SPKVOL_MUX_ENUM             "Right SPKVOL Mux"
#define MIXER_L_SPKVOL_MUX_ENUM             "Left SPKVOL Mux"
#define MIXER_SPKR_MUX_ENUM                 "SPOR Mux"
#define MIXER_SPKL_MUX_ENUM                 "SPOL Mux"
#define MIXER_HPL_MUX_ENUM                  "HPL Mux"
#define MIXER_HPR_MUX_ENUM                  "HPR Mux"

/* BOOL type */
#define MIXER_HP_PLAYBACK_SWITCH                "HP Playback Switch"
#define MIXER_SPK_PLAYBACK_SWITCH               "Speaker Playback Switch"
#define MIXER_RECMIXL_MIC1_CAPTURE_SWITCH       "RECMIXL Mixer MIC1_BST1 Capture Switch"
#define MIXER_OUT_DACR_PLAYBACK_SWITCH          "OUTMIXR Mixer DACR Playback Switch"
#define MIXER_OUT_DACL_PLAYBACK_SWITCH          "OUTMIXL Mixer DACL Playback Switch"
#define MIXER_SPKMIX_DACR_PLAYBACK_SWITCH       "SPKMIXR Mixer DACR Playback Switch"
#define MIXER_SPKMIX_DACL_PLAYBACK_SWITCH       "SPKMIXL Mixer DACL Playback Switch"
#define MIXER_SPKRMIX_SPKVOLR_PLAYBACK_SWITCH   "SPORMIX Mixer SPKVOLR Playback Switch"
#define MIXER_SPKLMIX_SPKVOLL_PLAYBACK_SWITCH   "SPOLMIX Mixer SPKVOLL Playback Switch"
#define MIXER_SPKRMIX_SPKVOLL_PLAYBACK_SWITCH   "SPORMIX Mixer SPKVOLL Playback Switch"
#define MIXER_SPKLMIX_SPKVOLR_PLAYBACK_SWITCH   "SPOLMIX Mixer SPKVOLR Playback Switch"

/* Mixer control value */
#define MIXER_JACK_SPEAKER                  "Speaker"
#define MIXER_JACK_HEADPHONE                "HeadPhone"
#define MIXER_SPEAKER_ON                    "On"
#define MIXER_SPEAKER_OFF                   "Off"
#define MIXER_L_HPVOL_OUTMIX                "OUTMIXL"
#define MIXER_R_HPVOL_OUTMIX                "OUTMIXR"
#define MIXER_L_SPKVOL_OUTMIX               "SPKMIXL"
#define MIXER_R_SPKVOL_OUTMIX               "SPKMIXR"
#define MIXER_SPK_MUX_SPOL                  "SPOLMIX"
#define MIXER_SPK_MUX_SPOR                  "SPORMIX"

struct route_setting mic_input[] = {
    {
        .ctl_name = MIXER_RECMIXL_MIC1_CAPTURE_SWITCH,
        .intval = 1
    },    
    {
    	.ctl_name = MIXER_MIC1_BOOST,
		.intval = 4
    },
    {
        .ctl_name = NULL,
    },
};

struct route_setting headset_input[] = {
    {
        .ctl_name = NULL,
    },
};

struct route_setting output_speaker[] = {
    {
        .ctl_name = MIXER_SPKMIX_DACR_PLAYBACK_SWITCH,
        .intval = 1,
    },
    {
        .ctl_name = MIXER_SPKMIX_DACL_PLAYBACK_SWITCH,
        .intval = 1,
    },
    {
        .ctl_name = MIXER_R_SPKVOL_MUX_ENUM,
        .strval = MIXER_R_SPKVOL_OUTMIX,
    },
    {
        .ctl_name = MIXER_L_SPKVOL_MUX_ENUM,
        .strval = MIXER_L_SPKVOL_OUTMIX,
    },    
    {
        .ctl_name = MIXER_SPKLMIX_SPKVOLL_PLAYBACK_SWITCH,
        .intval = 1,
    },
    {
        .ctl_name = MIXER_SPKRMIX_SPKVOLR_PLAYBACK_SWITCH,
        .intval = 1,
    },
/*    {
        .ctl_name = MIXER_SPKRMIX_SPKVOLL_PLAYBACK_SWITCH,
        .intval = 1,
    },
    {
        .ctl_name = MIXER_SPKLMIX_SPKVOLR_PLAYBACK_SWITCH,
        .intval = 1,
    },*/
    {
        .ctl_name = MIXER_SPKR_MUX_ENUM,
        .strval = MIXER_SPK_MUX_SPOR,
    },
    {
        .ctl_name = MIXER_SPKL_MUX_ENUM,
        .strval = MIXER_SPK_MUX_SPOL,
    },
/*    {
        .ctl_name = MIXER_SPK_PLAYBACK_SWITCH,
        .intval = 1,
    },*/
    {
        .ctl_name = MIXER_SPK_PLAYBACK_VOLUME,
        .intval = 33,
    },
    {
        .ctl_name = NULL,
    },
};

struct route_setting output_headphone[] = {    
	{
        .ctl_name = MIXER_HPL_MUX_ENUM,
        .intval = 1,
    },
    {
        .ctl_name = MIXER_HPR_MUX_ENUM,
        .intval = 1,
    },
/*    {
        .ctl_name = MIXER_HP_PLAYBACK_SWITCH,
        .intval = 1,
    },*/
    {
        .ctl_name = MIXER_HP_PLAYBACK_VOLUME,
        .intval = 31,
    },
    {
        .ctl_name = NULL,
    },
    {
        .ctl_name = NULL,
    },
};

struct route_setting output_headset[] = {
    {
        .ctl_name = MIXER_R_HPVOL_MUX_ENUM,
        .strval = MIXER_R_HPVOL_OUTMIX,
    },
    {
        .ctl_name = MIXER_L_HPVOL_MUX_ENUM,
        .strval = MIXER_L_HPVOL_OUTMIX,
    },
    {
        .ctl_name = MIXER_OUT_DACR_PLAYBACK_SWITCH,
        .intval = 1,
    },
    {
        .ctl_name = MIXER_OUT_DACL_PLAYBACK_SWITCH,
        .intval = 1,
    },
    {
        .ctl_name = MIXER_HP_PLAYBACK_SWITCH,
        .intval = 1,
    },
    {
        .ctl_name = MIXER_HP_PLAYBACK_VOLUME,
        .intval = 31,
    },
    {
        .ctl_name = MIXER_JACK_FUNC_ENUM,
        .strval = MIXER_JACK_HEADPHONE,
    },
    {
        .ctl_name = MIXER_SPEAKER_FUNC_ENUM,
        .strval = MIXER_SPEAKER_OFF,
    },
    {
        .ctl_name = NULL,
    },
    {
        .ctl_name = NULL,
    },
};


#endif //_RT5631_MIXER_CTRL_H_

