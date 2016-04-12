#define LOG_TAG "audio_hwsync"
#include <errno.h>
#include <pthread.h>
#include <stdint.h>
#include <sys/time.h>
#include <stdlib.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <time.h>
#include <utils/Timers.h>
#include <cutils/log.h>
#include <cutils/str_parms.h>
#include <cutils/properties.h>
#include <linux/ioctl.h>
#include <hardware/hardware.h>
#include <system/audio.h>
#include <hardware/audio.h>
#include <sound/asound.h>
#include <tinyalsa/asoundlib.h>

#include "audio_hwsync.h"
#include "audio_hw_utils.h"
#include "audio_hw.h"
#include "hdmi_audio_hw.h"
static uint32_t aml_hwsync_out_get_latency(const struct audio_stream_out *stream)
{
    struct aml_stream_out *out = (struct aml_stream_out *)stream;
    uint32_t whole_latency;
    uint32_t ret;
    snd_pcm_sframes_t frames = 0;
    whole_latency = (out->config.period_size * out->config.period_count * 1000) / out->config.rate;
    if (!out->pcm || !pcm_is_ready(out->pcm)) {
        return whole_latency;
    }
    ret = pcm_ioctl(out->pcm, SNDRV_PCM_IOCTL_DELAY, &frames);
    if (ret < 0) {
        return whole_latency;
    }
    if (out->format == AUDIO_FORMAT_E_AC3) {
        frames /= 4;
    }
    return (uint32_t)((frames * 1000) / out->config.rate);
}

void aml_audio_hwsync_clear_status(struct aml_stream_out *out)
{
    struct aml_audio_device *adev = out->dev;
    audio_hwsync_t  *p_hwsync = &adev->hwsync;
    p_hwsync->first_apts_flag = 0;
    p_hwsync->hw_sync_state = HW_SYNC_STATE_HEADER;
    p_hwsync->hw_sync_header_cnt = 0;
    return;
}
//return bytes cost from input,
int  aml_audio_hwsync_find_frame(struct aml_stream_out *out, const void *in_buffer, size_t in_bytes, uint64_t *cur_pts, int *outsize)
{
    int remain = in_bytes;
    uint8_t *p = in_buffer;
    struct aml_audio_device *adev = out->dev;
    audio_hwsync_t  *p_hwsync = &adev->hwsync;
    int time_diff = 0;
    //ALOGI(" --- out_write %d, cache cnt = %d, body = %d, hw_sync_state = %d", out_frames * frame_size, out->body_align_cnt, out->hw_sync_body_cnt, out->hw_sync_state);
    while (remain > 0) {
        //if (p_hwsync->hw_sync_state == HW_SYNC_STATE_RESYNC) {
        //}
        if (p_hwsync->hw_sync_state == HW_SYNC_STATE_HEADER) {
            //ALOGI("Add to header buffer [%d], 0x%x", out->hw_sync_header_cnt, *p);
            p_hwsync->hw_sync_header[p_hwsync->hw_sync_header_cnt++] = *p++;
            remain--;
            if (p_hwsync->hw_sync_header_cnt == 16) {
                uint64_t pts;
                if (!hwsync_header_valid(&p_hwsync->hw_sync_header[0])) {
                    ALOGE("!!!!!!hwsync header out of sync! Resync.should not happen????");
                    p_hwsync->hw_sync_state = HW_SYNC_STATE_HEADER;
                    memcpy(p_hwsync->hw_sync_header, p_hwsync->hw_sync_header + 1, 15);
                    p_hwsync->hw_sync_header_cnt--;
                    continue;
                }
                p_hwsync->hw_sync_state = HW_SYNC_STATE_BODY;
                p_hwsync->hw_sync_body_cnt = hwsync_header_get_size(&p_hwsync->hw_sync_header[0]);
                p_hwsync->hw_sync_frame_size = p_hwsync->hw_sync_body_cnt;
                p_hwsync->body_align_cnt = 0;
                pts = hwsync_header_get_pts(&p_hwsync->hw_sync_header[0]);
                //memcpy(write_buf+write_pos,&p_hwsync->hw_sync_header[0],16);
                //write_pos += 16;
                pts = pts * 90 / 1000000;
                time_diff = (abs(pts - p_hwsync->last_apts_from_header)) / 90;
                ALOGV("pts %llx,frame len %d\n", pts, p_hwsync->hw_sync_body_cnt);
                ALOGV("last pts %llx,diff %d ms\n", p_hwsync->last_apts_from_header, time_diff);

                if (time_diff > 32) {
                    ALOGI("pts  time gap %d ms,last %llx,cur %llx\n", time_diff,
                          p_hwsync->last_apts_from_header, pts);
                }
                p_hwsync->last_apts_from_header = pts;
                *cur_pts = pts;
                //ALOGI("get header body_cnt = %d, pts = %lld", out->hw_sync_body_cnt, pts);
            }
            continue;
        } else if (p_hwsync->hw_sync_state == HW_SYNC_STATE_BODY) {
            int m = (p_hwsync->hw_sync_body_cnt < remain) ? p_hwsync->hw_sync_body_cnt : remain;
            //ALOGI("m = %d", m);
            // process m bytes body with an empty fragment for alignment
            if (m  > 0) {
                //ret = pcm_write(out->pcm, p, m - align);
                memcpy(p_hwsync->hw_sync_body_buf + p_hwsync->hw_sync_frame_size - p_hwsync->hw_sync_body_cnt, p, m);
                p += m;
                remain -= m;
                //ALOGI("pcm_write %d, remain %d", m - align, remain);
                p_hwsync->hw_sync_body_cnt -= m;
                if (p_hwsync->hw_sync_body_cnt == 0) {
                    p_hwsync->hw_sync_state = HW_SYNC_STATE_HEADER;
                    p_hwsync->hw_sync_header_cnt = 0;
                    *outsize = p_hwsync->hw_sync_frame_size;
                    ALOGV("we found the frame total body,yeah\n");
                    break;//continue;
                }
            }
        }
    }
    return in_bytes - remain;
}
