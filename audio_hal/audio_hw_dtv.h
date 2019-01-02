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

#ifndef _AUDIO_HW_DTV_H_
#define _AUDIO_HW_DTV_H_

enum {
    AUDIO_DTV_PATCH_DECODER_STATE_INIT,
    AUDIO_DTV_PATCH_DECODER_STATE_START,
    AUDIO_DTV_PATCH_DECODER_STATE_RUNING,
    AUDIO_DTV_PATCH_DECODER_STATE_PAUSE,
    AUDIO_DTV_PATCH_DECODER_STATE_RESUME,
    AUDIO_DTV_PATCH_DECODER_RELEASE,
};

enum {
    AUDIO_DTV_PATCH_CMD_NULL,
    AUDIO_DTV_PATCH_CMD_START,
    AUDIO_DTV_PATCH_CMD_STOP,
    AUDIO_DTV_PATCH_CMD_PAUSE,
    AUDIO_DTV_PATCH_CMD_RESUME,
    AUDIO_DTV_PATCH_CMD_NUM,
};

int create_dtv_patch(struct audio_hw_device *dev, audio_devices_t input, audio_devices_t output __unused);
int release_dtv_patch(struct aml_audio_device *dev);
int dtv_patch_add_cmd(int cmd);
int dtv_in_read(struct audio_stream_in *stream, void* buffer, size_t bytes);
void dtv_in_write(struct audio_stream_out *stream, const void* buffer, size_t bytes);
void save_latest_dtv_aformat(int afmt);
#endif
