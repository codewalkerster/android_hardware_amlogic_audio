/*
 * m3codec_mixer_ctl.h  --  AML_M3 Mixer control for Android ICS
 *
 * Copyright 2012 Amlogic Corp.
 *
 */

#ifndef _M3CODEC_MIXER_CTL_H_
#define _M3CODEC_MIXER_CTL_H_

//Todo: add route controls
struct route_setting
{
	char *ctl_name;
	int intval;
	char *strval;
};
struct route_setting mic_input[] = 
{
	{
		.ctl_name = NULL,
	},
};
struct route_setting line_input[] = 
{
	{
		.ctl_name = NULL,
	},
};
static struct route_setting output_headphone[] = 
{
	{
		.ctl_name = NULL,
	},
};static struct route_setting output_speaker[] = 
{
	{
		.ctl_name = NULL,
	},
};

#endif //_M3CODEC_MIXER_CTL_H_

