/*
 * Copyright (C) 2010 Amlogic Corporation.
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 *      http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 */



#ifndef  _AUDIO_HW_UTILS_H_
#define _AUDIO_HW_UTILS_H_
int get_sysfs_uint(const char *path, uint *value);
int sysfs_set_sysfs_str(const char *path, const char *val);
int get_sysfs_int (const char *path);
int mystrstr(char *mystr,char *substr) ;
void set_codec_type(int type);
int get_codec_type(int format);
int getprop_bool (const char *path);
unsigned char codec_type_is_raw_data(int type);
int mystrstr(char *mystr,char *substr);
void *convert_audio_sample_for_output(int input_frames,int input_format,int input_ch,void *input_buf,int *out_size/*,float lvol*/);
int  aml_audio_start_trigger(void *stream);
#endif
