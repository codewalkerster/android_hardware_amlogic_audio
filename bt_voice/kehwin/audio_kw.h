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

#ifndef _AUDIO_KW_H_
#define _AUDIO_KW_H_

#include <hardware/hardware.h>
#include <system/audio.h>
#include <hardware/audio.h>

#define KEHWIN_AUDIO 1

#ifdef __cplusplus
extern "C"{
#endif
/**isUseGoogleVoice Indicates whether the recognition engine is google voice*/
void initAudio(int isUseGoogleVoice);
int remoteDeviceOnline();
ssize_t kehwin_in_read(struct audio_stream_in *stream, void* buffer,size_t bytes);
int kehwin_in_standby(struct audio_stream *stream);
void kehwin_adev_close_input_stream(struct audio_hw_device *dev,struct audio_stream_in *stream);
size_t kehwin_in_get_buffer_size(const struct audio_stream *stream);
uint32_t kehwin_in_get_sample_rate(const struct audio_stream *stream);
int kehwin_in_set_sample_rate(struct audio_stream *stream, uint32_t rate);
audio_channel_mask_t kehwin_in_get_channels(const struct audio_stream *stream);
int kehwin_in_set_format(struct audio_stream *stream, audio_format_t format);
audio_format_t kehwin_in_get_format(const struct audio_stream *stream);
int kehwin_in_set_parameters(struct audio_stream *stream, const char *kvpairs);
char * kehwin_in_get_parameters(const struct audio_stream *stream,const char *keys);
int kehwin_in_dump(const struct audio_stream *stream, int fd);
int kehwin_in_add_audio_effect(const struct audio_stream *stream, effect_handle_t effect);
int kehwin_in_remove_audio_effect(const struct audio_stream *stream, effect_handle_t effect);
int kehwin_in_set_gain(struct audio_stream_in *stream, float gain);
uint32_t kehwin_in_get_input_frames_lost(struct audio_stream_in *stream);
int kehwin_in_get_capture_position(const struct audio_stream_in *stream,int64_t *frames, int64_t *time);
#ifdef __cplusplus
}
#endif

#endif
