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



#define LOG_TAG "audio_hwsync"

#include <stdint.h>
#include <inttypes.h>
#include <cutils/log.h>
#include <string.h>

#include "audio_hw_utils.h"
#include "audio_hwsync.h"

void aml_audio_hwsync_init(audio_hwsync_t *p_hwsync)
{
    if (p_hwsync == NULL)
        return;

    p_hwsync->first_apts_flag = false;
    p_hwsync->hw_sync_state = HW_SYNC_STATE_HEADER;
    p_hwsync->hw_sync_header_cnt = 0;
    return;
}
//return bytes cost from input,
int aml_audio_hwsync_find_frame(audio_hwsync_t *p_hwsync,
        const void *in_buffer, size_t in_bytes, uint64_t *cur_pts, int *outsize)
{
    size_t remain = in_bytes;
    uint8_t *p = (uint8_t *)in_buffer;
    uint64_t time_diff = 0;
    int pts_found = 0;
    if (p_hwsync == NULL || in_buffer == NULL)
        return 0;

    //ALOGI(" --- out_write %d, cache cnt = %d, body = %d, hw_sync_state = %d", out_frames * frame_size, out->body_align_cnt, out->hw_sync_body_cnt, out->hw_sync_state);
    while (remain > 0) {
        //if (p_hwsync->hw_sync_state == HW_SYNC_STATE_RESYNC) {
        //}
        if (p_hwsync->hw_sync_state == HW_SYNC_STATE_HEADER) {
            //ALOGI("Add to header buffer [%d], 0x%x", p_hwsync->hw_sync_header_cnt, *p);
            p_hwsync->hw_sync_header[p_hwsync->hw_sync_header_cnt++] = *p++;
            remain--;
            if (p_hwsync->hw_sync_header_cnt == HW_SYNC_HEADER_CNT) {
                uint64_t pts;
                if (!hwsync_header_valid(&p_hwsync->hw_sync_header[0])) {
                    //ALOGE("!!!!!!hwsync header out of sync! Resync.should not happen????");
                    p_hwsync->hw_sync_state = HW_SYNC_STATE_HEADER;
                    memcpy(p_hwsync->hw_sync_header, p_hwsync->hw_sync_header + 1, HW_SYNC_HEADER_CNT - 1);
                    p_hwsync->hw_sync_header_cnt--;
                    continue;
                }
                if ((in_bytes-remain) > HW_SYNC_HEADER_CNT)
                    ALOGI("got the frame sync header cost %zu",in_bytes-remain);
                p_hwsync->hw_sync_state = HW_SYNC_STATE_BODY;
                p_hwsync->hw_sync_body_cnt = hwsync_header_get_size(&p_hwsync->hw_sync_header[0]);
                p_hwsync->hw_sync_frame_size = p_hwsync->hw_sync_body_cnt;
                p_hwsync->hw_sync_header_cnt = 0;
                pts = hwsync_header_get_pts(&p_hwsync->hw_sync_header[0]);
                pts = pts * 90 / 1000000;
                time_diff = get_pts_gap(pts, p_hwsync->last_apts_from_header) / 90;
                ALOGV("pts %"PRIx64",frame len %u\n", pts, p_hwsync->hw_sync_body_cnt);
                ALOGV("last pts %"PRIx64",diff %"PRIx64" ms\n", p_hwsync->last_apts_from_header, time_diff);

                if (time_diff > 32) {
                    ALOGI("pts  time gap %"PRIx64" ms,last %"PRIx64",cur %"PRIx64"\n", time_diff,
                          p_hwsync->last_apts_from_header, pts);
                }
                p_hwsync->last_apts_from_header = pts;
                *cur_pts = pts;
                pts_found = 1;
                //ALOGI("get header body_cnt = %d, pts = %lld", out->hw_sync_body_cnt, pts);
            }
            continue;
        } else if (p_hwsync->hw_sync_state == HW_SYNC_STATE_BODY) {
            int m = (p_hwsync->hw_sync_body_cnt < remain) ? p_hwsync->hw_sync_body_cnt : remain;
            // process m bytes body with an empty fragment for alignment
            if (m  > 0) {
                //ret = pcm_write(out->pcm, p, m - align);
#if 0
                FILE *fp1 = fopen("/data/hwsync_body.pcm", "a+");
                if (fp1) {
                    int flen = fwrite((char *)p, 1, m, fp1);
                    //ALOGD("flen = %d---outlen=%d ", flen, out_frames * frame_size);
                    fclose(fp1);
                } else {
                    ALOGE("could not open file:/data/hwsync_body.pcm");
                }
#endif
                memcpy(p_hwsync->hw_sync_body_buf + p_hwsync->hw_sync_frame_size - p_hwsync->hw_sync_body_cnt, p, m);
                p += m;
                remain -= m;
                p_hwsync->hw_sync_body_cnt -= m;
                if (p_hwsync->hw_sync_body_cnt == 0) {
                    p_hwsync->hw_sync_state = HW_SYNC_STATE_HEADER;
                    p_hwsync->hw_sync_header_cnt = 0;
                    *outsize = p_hwsync->hw_sync_frame_size;
                    /*
                    sometimes the audioflinger burst size is smaller than hwsync payload
                    we need use the last found pts when got a complete hwsync payload
                    */
                    if (!pts_found) {
                        *cur_pts = p_hwsync->last_apts_from_header;
                    }
                    ALOGV("we found the frame total body,yeah\n");
                    break;//continue;
                }
            }
        }
    }
    return in_bytes - remain;
}

int aml_audio_hwsync_set_first_pts(audio_hwsync_t *p_hwsync, uint64_t pts)
{
    uint32_t pts32;
    char tempbuf[128];

    if (p_hwsync == NULL)
        return -1;

    if (pts > 0xffffffff) {
        ALOGE("APTS exeed the 32bit range!");
        return -1;
    }

    pts32 = (uint32_t)pts;
    p_hwsync->first_apts_flag = true;
    p_hwsync->first_apts = pts;
    sprintf(tempbuf, "AUDIO_START:0x%x", pts32);
    ALOGI("hwsync set tsync -> %s", tempbuf);
    if (sysfs_set_sysfs_str(TSYNC_EVENT, tempbuf) == -1) {
        ALOGE("set AUDIO_START failed \n");
        return -1;
    }

    return 0;
}
