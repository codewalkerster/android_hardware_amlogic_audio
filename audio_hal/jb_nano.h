/*
 * Copyright (C) 2018 Amlogic Corporation.
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

#ifndef __JB_NANO_H__
#define __JB_NANO_H__

#ifdef __cplusplus
extern "C"{
#endif

#include <tinyalsa/asoundlib.h>
#include <system/audio.h>
#include <hardware/audio.h>

typedef enum{
	RECORDING_DEVICE_NANO,
	RECORDING_DEVICE_OTHER,

}RECORDING_DEVICE;

void nano_init(void);
int nano_open(struct pcm_config *config_pcm, struct audio_config *config,int source,struct audio_stream_in* stream_in);
void nano_close(struct audio_stream_in* stream_in);
int nano_get_recorde_device(void);
uint32_t nano_get_sample_rate(const struct audio_stream *stream);
size_t nano_get_buffer_size(const struct audio_stream *stream);
uint32_t nano_get_channels(const struct audio_stream *stream);
audio_format_t nano_get_format(const struct audio_stream *stream);
ssize_t nano_read(struct audio_stream_in *stream, void *buffer, size_t bytes);

#ifdef __cplusplus
}
#endif
#endif