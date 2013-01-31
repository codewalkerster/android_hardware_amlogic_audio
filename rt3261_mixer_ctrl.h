/*
 * rt3261_mixer_ctrl.h  --  RT3261 Mixer control for Android ICS
 *
 * Copyright 2012 Amlogic Corp.
 *
 */

#ifndef _RT3261_MIXER_CTRL_H_
#define _RT3261_MIXER_CTRL_H_

struct route_setting
{
    char *ctl_name;
    int intval;
    char *strval;
};

struct route_setting output_speaker[] = {
    {
        .ctl_name = "Mono DAC MIXL DAC L2 Switch",
        .intval = 0,
    },
    {
        .ctl_name = "Mono DAC MIXR DAC R2 Switch",
        .intval = 0,
    },
    {
        .ctl_name = "RECMIXL BST1 Switch",
        .intval = 1,
    },
    {
        .ctl_name = "RECMIXR BST1 Switch",
        .intval = 1,
    },
    {
        .ctl_name = "RECMIXL BST2 Switch",
        .intval = 1,
    },
    {
        .ctl_name = "RECMIXR BST2 Switch",
        .intval = 1,
    },
    {
        .ctl_name = "Stereo ADC L1 Mux",
        .strval = "ADC",
    },
    {
        .ctl_name = "Stereo ADC R1 Mux",
        .strval = "ADC",
    },
    {
        .ctl_name = "Stereo ADC MIXL ADC1 Switch",
        .intval = 1,
    },
    {
        .ctl_name = "Stereo ADC MIXR ADC1 Switch",
        .intval = 1,
    },
    {
        .ctl_name = "Stereo DAC MIXL DAC L1 Switch",
        .intval = 1,
    },
    {
        .ctl_name = "Stereo DAC MIXR DAC R1 Switch",
        .intval = 1,
    },
    {
        .ctl_name = "OUT MIXL DAC L1 Switch",
        .intval = 1,
    },
    {
        .ctl_name = "OUT MIXR DAC R1 Switch",
        .intval = 1,
    },
    {
        .ctl_name = "SPK MIXL OUT MIXL Switch",
        .intval = 1,
    },
    {
        .ctl_name = "SPK MIXR OUT MIXR Switch",
        .intval = 1,
    },
    {
        .ctl_name = "SPOL MIX SPKVOL L Switch",
        .intval = 1,
    },
    {
        .ctl_name = "SPOR MIX SPKVOL R Switch",
        .intval = 1,
    },
    {
        .ctl_name = "I2S2 mode Switch",
        .strval = "Disable",
    },
    {
        .ctl_name = NULL,
    },
};

struct route_setting speaker_ringtone[] = {
    {
        .ctl_name = "RECMIXL BST1 Switch",
        .intval = 1,
    },
    {
        .ctl_name = "RECMIXR BST1 Switch",
        .intval = 1,
    },
    {
        .ctl_name = "RECMIXL BST2 Switch",
        .intval = 1,
    },
    {
        .ctl_name = "RECMIXR BST2 Switch",
        .intval = 1,
    },
    {
        .ctl_name = "Mono ADC L1 Mux",
        .strval = "ADCL",
    },
    {
        .ctl_name = "Mono ADC R1 Mux",
        .strval = "ADCR",
    },
    {
        .ctl_name = "Mono ADC MIXL ADC1 Switch",
        .intval = 1,
    },
    {
        .ctl_name = "Mono ADC MIXR ADC1 Switch",
        .intval = 1,
    },   
    {
        .ctl_name = "IF2 ADC L Mux",
        .strval = "Mono ADC MIXL",
    },
    {
        .ctl_name = "IF2 ADC R Mux",
        .strval = "Mono ADC MIXR",
    },  
    {
        .ctl_name = "Stereo DAC MIXL DAC L1 Switch",
        .intval = 1,
    },
    {
        .ctl_name = "Stereo DAC MIXR DAC R1 Switch",
        .intval = 1,
    },
    {
        .ctl_name = "OUT MIXL DAC L1 Switch",
        .intval = 1,
    },
    {
        .ctl_name = "OUT MIXR DAC R1 Switch",
        .intval = 1,
    },
    {
        .ctl_name = "SPK MIXL OUT MIXL Switch",
        .intval = 1,
    },
    {
        .ctl_name = "SPK MIXR OUT MIXR Switch",
        .intval = 1,
    },
    {
        .ctl_name = "SPOL MIX SPKVOL L Switch",
        .intval = 1,
    },
    {
        .ctl_name = "SPOR MIX SPKVOL R Switch",
        .intval = 1,
    },
    {
        .ctl_name = "I2S2 mode Switch",
        .strval = "Disable",
    },
    {
        .ctl_name = NULL,
    },
};

struct route_setting speaker_incall[] = {
	  {
        .ctl_name = "I2S2 mode Switch",
        .strval = "3G",
    },
    {
        .ctl_name = "Stereo DAC MIXL DAC L1 Switch",
        .intval = 0,
    },
    {
        .ctl_name = "Stereo DAC MIXR DAC R1 Switch",
        .intval = 0,
    },
    {
        .ctl_name = "RECMIXL BST1 Switch",
        .intval = 1,
    },
    {
        .ctl_name = "RECMIXR BST1 Switch",
        .intval = 1,
    },
    {
        .ctl_name = "RECMIXL BST2 Switch",
        .intval = 1,
    },
    {
        .ctl_name = "RECMIXR BST2 Switch",
        .intval = 1,
    },
    {
        .ctl_name = "Mono ADC L1 Mux",
        .strval = "ADCL",
    },
    {
        .ctl_name = "Mono ADC R1 Mux",
        .strval = "ADCR",
    },
    {
        .ctl_name = "Mono ADC MIXL ADC1 Switch",
        .intval = 1,
    },
    {
        .ctl_name = "Mono ADC MIXR ADC1 Switch",
        .intval = 1,
    },   
    {
        .ctl_name = "IF2 ADC L Mux",
        .strval = "Mono ADC MIXL",
    },
    {
        .ctl_name = "IF2 ADC R Mux",
        .strval = "Mono ADC MIXR",
    },
    {
        .ctl_name = "DAC L2 Mux",
        .strval = "IF2",
    },
    {
        .ctl_name = "DAC R2 Mux",
        .strval = "IF2",
    },   
    {
        .ctl_name = "Mono DAC MIXL DAC L2 Switch",
        .intval = 1,
    },
    {
        .ctl_name = "Mono DAC MIXR DAC R2 Switch",
        .intval = 1,
    },
    {
        .ctl_name = "OUT MIXL DAC L2 Switch",
        .intval = 1,
    },
    {
        .ctl_name = "OUT MIXR DAC R2 Switch",
        .intval = 1,
    },
    {
        .ctl_name = "SPK MIXL OUT MIXL Switch",
        .intval = 1,
    },
    {
        .ctl_name = "SPK MIXR OUT MIXR Switch",
        .intval = 1,
    },
    {
        .ctl_name = "SPOL MIX SPKVOL L Switch",
        .intval = 1,
    },
    {
        .ctl_name = "SPOR MIX SPKVOL R Switch",
        .intval = 1,
    },
    {
        .ctl_name = NULL,
    },
};

struct route_setting output_headphone[] = {
    {
        .ctl_name = "Mono DAC MIXL DAC L2 Switch",
        .intval = 0,
    },
    {
        .ctl_name = "Mono DAC MIXR DAC R2 Switch",
        .intval = 0,
    },
    {
        .ctl_name = "RECMIXL BST1 Switch",
        .intval = 0,
    },
    {
        .ctl_name = "RECMIXR BST1 Switch",
        .intval = 0,
    },
    {
        .ctl_name = "RECMIXL BST2 Switch",
        .intval = 1,
    },
    {
        .ctl_name = "RECMIXR BST2 Switch",
        .intval = 1,
    },
    {
        .ctl_name = "Stereo ADC L1 Mux",
        .strval = "ADC",
    },
    {
        .ctl_name = "Stereo ADC R1 Mux",
        .strval = "ADC",
    },
    {
        .ctl_name = "Stereo ADC MIXL ADC1 Switch",
        .intval = 1,
    },
    {
        .ctl_name = "Stereo ADC MIXR ADC1 Switch",
        .intval = 1,
    },
    {
        .ctl_name = "Stereo DAC MIXL DAC L1 Switch",
        .intval = 1,
    },
    {
        .ctl_name = "Stereo DAC MIXR DAC R1 Switch",
        .intval = 1,
    },
    {
        .ctl_name = "OUT MIXL DAC L1 Switch",
        .intval = 1,
    },
    {
        .ctl_name = "OUT MIXR DAC R1 Switch",
        .intval = 1,
    },
    {
        .ctl_name = "SPK MIXL DAC L1 Switch",
        .intval = 0,
    },
    {
        .ctl_name = "SPK MIXR DAC R1 Switch",
        .intval = 0,
    },
    {
        .ctl_name = "SPK MIXL OUT MIXL Switch",
        .intval = 0,
    },
    {
        .ctl_name = "SPK MIXR OUT MIXR Switch",
        .intval = 0,
    },
    {
        .ctl_name = "SPOL MIX SPKVOL L Switch",
        .intval = 0,
    },
    {
        .ctl_name = "SPOR MIX SPKVOL R Switch",
        .intval = 0,
    },
    {
        .ctl_name = "HPOL MIX HPVOL Switch",
        .intval = 1,
    },
    {
        .ctl_name = "HPOR MIX HPVOL Switch",
        .intval = 1,
    },
    {
        .ctl_name = "I2S2 mode Switch",
        .strval = "Disable",
    },
    {
        .ctl_name = NULL,
    },
};

struct route_setting headphone_ringtone[] = {
    {
        .ctl_name = "RECMIXL BST1 Switch",
        .intval = 0,
    },
    {
        .ctl_name = "RECMIXR BST1 Switch",
        .intval = 0,
    },
    {
        .ctl_name = "RECMIXL BST2 Switch",
        .intval = 1,
    },
    {
        .ctl_name = "RECMIXR BST2 Switch",
        .intval = 1,
    },
    {
        .ctl_name = "Mono ADC L1 Mux",
        .strval = "ADCL",
    },
    {
        .ctl_name = "Mono ADC R1 Mux",
        .strval = "ADCR",
    },
    {
        .ctl_name = "Mono ADC MIXL ADC1 Switch",
        .intval = 1,
    },
    {
        .ctl_name = "Mono ADC MIXR ADC1 Switch",
        .intval = 1,
    },   
    {
        .ctl_name = "IF2 ADC L Mux",
        .strval = "Mono ADC MIXL",
    },
    {
        .ctl_name = "IF2 ADC R Mux",
        .strval = "Mono ADC MIXR",
    },  
    {
        .ctl_name = "Stereo DAC MIXL DAC L1 Switch",
        .intval = 1,
    },
    {
        .ctl_name = "Stereo DAC MIXR DAC R1 Switch",
        .intval = 1,
    },
    {
        .ctl_name = "OUT MIXL DAC L1 Switch",
        .intval = 1,
    },
    {
        .ctl_name = "OUT MIXR DAC R1 Switch",
        .intval = 1,
    },
    {
        .ctl_name = "SPK MIXL OUT MIXL Switch",
        .intval = 1,
    },
    {
        .ctl_name = "SPK MIXR OUT MIXR Switch",
        .intval = 1,
    },
    {
        .ctl_name = "SPOL MIX SPKVOL L Switch",
        .intval = 1,
    },
    {
        .ctl_name = "SPOR MIX SPKVOL R Switch",
        .intval = 1,
    },
    {
        .ctl_name = "HPOL MIX HPVOL Switch",
        .intval = 1,
    },
    {
        .ctl_name = "HPOR MIX HPVOL Switch",
        .intval = 1,
    },
    {
        .ctl_name = "I2S2 mode Switch",
        .strval = "Disable",
    },
    {
        .ctl_name = NULL,
    },
};

struct route_setting headphone_incall[] = {
	 	{
        .ctl_name = "I2S2 mode Switch",
        .strval = "3G",
    },
    {
        .ctl_name = "Stereo DAC MIXL DAC L1 Switch",
        .intval = 0,
    },
    {
        .ctl_name = "Stereo DAC MIXR DAC R1 Switch",
        .intval = 0,
    },
    {
        .ctl_name = "RECMIXL BST1 Switch",
        .intval = 0,
    },
    {
        .ctl_name = "RECMIXR BST1 Switch",
        .intval = 0,
    },
    {
        .ctl_name = "RECMIXL BST2 Switch",
        .intval = 1,
    },
    {
        .ctl_name = "RECMIXR BST2 Switch",
        .intval = 1,
    },
    {
        .ctl_name = "Mono ADC L1 Mux",
        .strval = "ADCL",
    },
    {
        .ctl_name = "Mono ADC R1 Mux",
        .strval = "ADCR",
    },
    {
        .ctl_name = "Mono ADC MIXL ADC1 Switch",
        .intval = 1,
    },
    {
        .ctl_name = "Mono ADC MIXR ADC1 Switch",
        .intval = 1,
    },   
    {
        .ctl_name = "IF2 ADC L Mux",
        .strval = "Mono ADC MIXL",
    },
    {
        .ctl_name = "IF2 ADC R Mux",
        .strval = "Mono ADC MIXR",
    },
    {
        .ctl_name = "DAC L2 Mux",
        .strval = "IF2",
    },
    {
        .ctl_name = "DAC R2 Mux",
        .strval = "IF2",
    },   
    {
        .ctl_name = "Mono DAC MIXL DAC L2 Switch",
        .intval = 1,
    },
    {
        .ctl_name = "Mono DAC MIXR DAC R2 Switch",
        .intval = 1,
    },
    {
        .ctl_name = "OUT MIXL DAC L2 Switch",
        .intval = 1,
    },
    {
        .ctl_name = "OUT MIXR DAC R2 Switch",
        .intval = 1,
    },
    {
        .ctl_name = "SPK MIXL OUT MIXL Switch",
        .intval = 0,
    },
    {
        .ctl_name = "SPK MIXR OUT MIXR Switch",
        .intval = 0,
    },
    {
        .ctl_name = "SPOL MIX SPKVOL L Switch",
        .intval = 0,
    },
    {
        .ctl_name = "SPOR MIX SPKVOL R Switch",
        .intval = 0,
    },
    {
        .ctl_name = "HPOL MIX HPVOL Switch",
        .intval = 1,
    },
    {
        .ctl_name = "HPOR MIX HPVOL Switch",
        .intval = 1,
    },
    {
        .ctl_name = NULL,
    },
};

struct route_setting headset[] = {
    {
        .ctl_name = "Mono DAC MIXL DAC L2 Switch",
        .intval = 0,
    },
    {
        .ctl_name = "Mono DAC MIXR DAC R2 Switch",
        .intval = 0,
    },
    {
        .ctl_name = "RECMIXL BST1 Switch",
        .intval = 0,
    },
    {
        .ctl_name = "RECMIXR BST1 Switch",
        .intval = 0,
    },
    {
        .ctl_name = "RECMIXL BST2 Switch",
        .intval = 1,
    },
    {
        .ctl_name = "RECMIXR BST2 Switch",
        .intval = 1,
    },
    {
        .ctl_name = "Stereo ADC L1 Mux",
        .strval = "ADC",
    },
    {
        .ctl_name = "Stereo ADC R1 Mux",
        .strval = "ADC",
    },
    {
        .ctl_name = "Stereo ADC MIXL ADC1 Switch",
        .intval = 1,
    },
    {
        .ctl_name = "Stereo ADC MIXR ADC1 Switch",
        .intval = 1,
    },
    {
        .ctl_name = "Stereo DAC MIXL DAC L1 Switch",
        .intval = 1,
    },
    {
        .ctl_name = "Stereo DAC MIXR DAC R1 Switch",
        .intval = 1,
    },
    {
        .ctl_name = "OUT MIXL DAC L1 Switch",
        .intval = 1,
    },
    {
        .ctl_name = "OUT MIXR DAC R1 Switch",
        .intval = 1,
    },
    {
        .ctl_name = "SPK MIXL DAC L1 Switch",
        .intval = 0,
    },
    {
        .ctl_name = "SPK MIXR DAC R1 Switch",
        .intval = 0,
    },
    {
        .ctl_name = "SPK MIXL OUT MIXL Switch",
        .intval = 0,
    },
    {
        .ctl_name = "SPK MIXR OUT MIXR Switch",
        .intval = 0,
    },
    {
        .ctl_name = "SPOL MIX SPKVOL L Switch",
        .intval = 0,
    },
    {
        .ctl_name = "SPOR MIX SPKVOL R Switch",
        .intval = 0,
    },
    {
        .ctl_name = "HPOL MIX HPVOL Switch",
        .intval = 1,
    },
    {
        .ctl_name = "HPOR MIX HPVOL Switch",
        .intval = 1,
    },
    {
        .ctl_name = "I2S2 mode Switch",
        .strval = "Disable",
    },
    {
        .ctl_name = NULL,
    },
};

struct route_setting headset_ringtone[] = {
    {
        .ctl_name = "RECMIXL BST1 Switch",
        .intval = 0,
    },
    {
        .ctl_name = "RECMIXR BST1 Switch",
        .intval = 0,
    },
    {
        .ctl_name = "RECMIXL BST2 Switch",
        .intval = 1,
    },
    {
        .ctl_name = "RECMIXR BST2 Switch",
        .intval = 1,
    },
    {
        .ctl_name = "Mono ADC L1 Mux",
        .strval = "ADCL",
    },
    {
        .ctl_name = "Mono ADC R1 Mux",
        .strval = "ADCR",
    },
    {
        .ctl_name = "Mono ADC MIXL ADC1 Switch",
        .intval = 1,
    },
    {
        .ctl_name = "Mono ADC MIXR ADC1 Switch",
        .intval = 1,
    },   
    {
        .ctl_name = "IF2 ADC L Mux",
        .strval = "Mono ADC MIXL",
    },
    {
        .ctl_name = "IF2 ADC R Mux",
        .strval = "Mono ADC MIXR",
    },  
    {
        .ctl_name = "Stereo DAC MIXL DAC L1 Switch",
        .intval = 1,
    },
    {
        .ctl_name = "Stereo DAC MIXR DAC R1 Switch",
        .intval = 1,
    },
    {
        .ctl_name = "OUT MIXL DAC L1 Switch",
        .intval = 1,
    },
    {
        .ctl_name = "OUT MIXR DAC R1 Switch",
        .intval = 1,
    },
    {
        .ctl_name = "SPK MIXL OUT MIXL Switch",
        .intval = 1,
    },
    {
        .ctl_name = "SPK MIXR OUT MIXR Switch",
        .intval = 1,
    },
    {
        .ctl_name = "SPOL MIX SPKVOL L Switch",
        .intval = 1,
    },
    {
        .ctl_name = "SPOR MIX SPKVOL R Switch",
        .intval = 1,
    },
    {
        .ctl_name = "HPOL MIX HPVOL Switch",
        .intval = 1,
    },
    {
        .ctl_name = "HPOR MIX HPVOL Switch",
        .intval = 1,
    },
    {
        .ctl_name = "I2S2 mode Switch",
        .strval = "Disable",
    },
    {
        .ctl_name = NULL,
    },
};

struct route_setting headset_incall[] = {
	 	{
        .ctl_name = "I2S2 mode Switch",
        .strval = "3G",
    },
    {
        .ctl_name = "Stereo DAC MIXL DAC L1 Switch",
        .intval = 0,
    },
    {
        .ctl_name = "Stereo DAC MIXR DAC R1 Switch",
        .intval = 0,
    },
    {
        .ctl_name = "RECMIXL BST1 Switch",
        .intval = 0,
    },
    {
        .ctl_name = "RECMIXR BST1 Switch",
        .intval = 0,
    },
    {
        .ctl_name = "RECMIXL BST2 Switch",
        .intval = 1,
    },
    {
        .ctl_name = "RECMIXR BST2 Switch",
        .intval = 1,
    },
    {
        .ctl_name = "Mono ADC L1 Mux",
        .strval = "ADCL",
    },
    {
        .ctl_name = "Mono ADC R1 Mux",
        .strval = "ADCR",
    },
    {
        .ctl_name = "Mono ADC MIXL ADC1 Switch",
        .intval = 1,
    },
    {
        .ctl_name = "Mono ADC MIXR ADC1 Switch",
        .intval = 1,
    },   
    {
        .ctl_name = "IF2 ADC L Mux",
        .strval = "Mono ADC MIXL",
    },
    {
        .ctl_name = "IF2 ADC R Mux",
        .strval = "Mono ADC MIXR",
    },
    {
        .ctl_name = "DAC L2 Mux",
        .strval = "IF2",
    },
    {
        .ctl_name = "DAC R2 Mux",
        .strval = "IF2",
    },   
    {
        .ctl_name = "Mono DAC MIXL DAC L2 Switch",
        .intval = 1,
    },
    {
        .ctl_name = "Mono DAC MIXR DAC R2 Switch",
        .intval = 1,
    },
    {
        .ctl_name = "OUT MIXL DAC L2 Switch",
        .intval = 1,
    },
    {
        .ctl_name = "OUT MIXR DAC R2 Switch",
        .intval = 1,
    },
    {
        .ctl_name = "SPK MIXL OUT MIXL Switch",
        .intval = 0,
    },
    {
        .ctl_name = "SPK MIXR OUT MIXR Switch",
        .intval = 0,
    },
    {
        .ctl_name = "SPOL MIX SPKVOL L Switch",
        .intval = 0,
    },
    {
        .ctl_name = "SPOR MIX SPKVOL R Switch",
        .intval = 0,
    },
    {
        .ctl_name = "HPOL MIX HPVOL Switch",
        .intval = 1,
    },
    {
        .ctl_name = "HPOR MIX HPVOL Switch",
        .intval = 1,
    },
    {
        .ctl_name = NULL,
    },
};

struct route_setting mic_input[] = {
	 	{
        .ctl_name = "RECMIXL Mixer MIC1_BST1 Capture Switch",
        .intval = 1,
    },
    {
        .ctl_name = "MIC1 Boost",
        .intval = 3,
    },
    {
        .ctl_name = NULL,
    },
};

struct route_setting headset_mic[] = {
	 	{
        .ctl_name = "RECMIXL Mixer MIC2_BST1 Capture Switch",
        .intval = 1,
    },
    {
        .ctl_name = "MIC2 Boost",
        .intval = 3,
    },
    {
        .ctl_name = NULL,
    },
};

#endif //_RT3261_MIXER_CTRL_H_

