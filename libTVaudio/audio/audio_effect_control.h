#ifndef __TV_AUDIO_EFFECT_CONTROL_H__
#define __TV_AUDIO_EFFECT_CONTROL_H__

#ifdef __cplusplus
extern "C" {
#endif

/*
 When aml audio system is open, EQ is init, but not enable. Set EQ band value first, then enable it.
 EQ has 5 bands, the gains are from -12dB to 12dB. When EQ is enabled, the main volume is 6 dB lowered.
 */

int unload_EQ_lib(void);
int load_EQ_lib(void);
int HPEQ_process(short *in, short *out, int framecount);
int HPEQ_init(void);
int HPEQ_setParameter(int band1, int band2, int band3, int band4, int band5);
int HPEQ_getParameter(int EQ_user_config[]);
int HPEQ_enable(bool enable);
int HPEQ_release(void);

/*
 When aml audio system is open, SRS is init, but not enable. Set SRS parameters as default value.
 truebass_spker_size = 2    (40, 60, 100, 150, 200, 250, 300, 400)Hz
 truebass_gain = 0.3        (0.0~1.0)
 dialogclarity_gain = 0.2   (0.0~1.0)
 definition_gain = 0.3      (0.0~1.0)
 surround_gain = 0.5        (0.0~1.0)
 When set srs_truebass_enable(1), srs works in default value. You can set other values before enable.
 SRS works only when framecount is aligned by 64, else it will make noise.
 */

int unload_SRS_lib(void);
int load_SRS_lib(void);
int srs_init(int sample_rate);
int srs_release(void);
int srs_setParameter(int SRS_user_config[]);
int srs_getParameter(int SRS_user_config[]);
int srs_set_gain(int input_gain, int output_gain);
int srs_truebass_enable(int enable);
int srs_dialogclarity_enable(int enable);
int srs_surround_enable(int enable);
int srs_process(short *in, short *out, int framecount);

#ifdef __cplusplus
}
#endif

#endif //__TV_AUDIO_EFFECT_CONTROL_H__
