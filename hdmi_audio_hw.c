/*
 * Copyright (C) 2011 The Android Open Source Project
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 *	  http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 */

#define LOG_TAG "audio_hw_hdmi"
//#define LOG_NDEBUG 0
//#define LOG_NDEBUG_FUNCTION
#ifdef LOG_NDEBUG_FUNCTION
#define LOGFUNC(...) ((void)0)
#else
#define LOGFUNC(...) (ALOGD(__VA_ARGS__))
#endif

#include <errno.h>
#include <pthread.h>
#include <stdint.h>
#include <sys/time.h>
#include <stdlib.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <math.h>

#include <cutils/log.h>
#include <cutils/str_parms.h>
#include <cutils/properties.h>

#include <hardware/hardware.h>
#include <system/audio.h>
#include <hardware/audio.h>

#include <tinyalsa/asoundlib.h>
#include <audio_utils/resampler.h>
#include <audio_utils/echo_reference.h>
#include <hardware/audio_effect.h>
#include <audio_effects/effect_aec.h>

/* ALSA cards for AML */
#define CARD_AMLOGIC_BOARD 0
#define CARD_AMLOGIC_USB 1
#define CARD_AMLOGIC_DEFAULT CARD_AMLOGIC_BOARD
/* ALSA ports for AML */
#define PORT_MM 1
/* number of frames per period */
#define DEFAULT_PERIOD_SIZE  1024	//(1024 * 2)
static unsigned PERIOD_SIZE = DEFAULT_PERIOD_SIZE;
/* number of periods for low power playback */
#define PLAYBACK_PERIOD_COUNT 4
/* number of periods for capture */
#define CAPTURE_PERIOD_COUNT 4

/* minimum sleep time in out_write() when write threshold is not reached */
#define MIN_WRITE_SLEEP_US 5000

#define RESAMPLER_BUFFER_FRAMES (PERIOD_SIZE * 6)
#define RESAMPLER_BUFFER_SIZE (4 * RESAMPLER_BUFFER_FRAMES)

static unsigned int DEFAULT_OUT_SAMPLING_RATE = 48000;

/* sampling rate when using MM low power port */
#define MM_LOW_POWER_SAMPLING_RATE 44100
/* sampling rate when using MM full power port */
#define MM_FULL_POWER_SAMPLING_RATE 48000
/* sampling rate when using VX port for narrow band */
#define VX_NB_SAMPLING_RATE 8000

static unsigned int first_write_status;


struct pcm_config pcm_config_out = {
  .channels = 2,
  .rate = MM_FULL_POWER_SAMPLING_RATE,
  .period_size = DEFAULT_PERIOD_SIZE,
  .period_count = PLAYBACK_PERIOD_COUNT,
  .format = PCM_FORMAT_S16_LE,
};

struct pcm_config pcm_config_in = {
  .channels = 2,
  .rate = MM_FULL_POWER_SAMPLING_RATE,
  .period_size = DEFAULT_PERIOD_SIZE,
  .period_count = PLAYBACK_PERIOD_COUNT,
  .format = PCM_FORMAT_S16_LE,
};

struct aml_audio_device
{
  struct audio_hw_device hw_device;

  pthread_mutex_t lock;		/* see note below on mutex acquisition order */
  int mode;
  audio_devices_t in_device;
  audio_devices_t out_device;
  int in_call;
  //int cur_devices;
  struct pcm *pcm_modem_dl;
  struct pcm *pcm_modem_ul;
  struct aml_stream_in *active_input;
  struct aml_stream_out *active_output;

  bool mic_mute;
  struct echo_reference_itfe *echo_reference;
  bool bluetooth_nrec;
  bool low_power;
  /* RIL */
  //struct ril_handle ril;
};

struct aml_stream_out
{
  struct audio_stream_out stream;
  pthread_mutex_t lock;		/* see note below on mutex acquisition order */
  struct pcm_config config;
  struct pcm *pcm;
  struct resampler_itfe *resampler;
  char *buffer;
  int standby;
  struct echo_reference_itfe *echo_reference;
  struct aml_audio_device *dev;
  int write_threshold;
  bool low_power;
  unsigned multich;
};

#define MAX_PREPROCESSORS 3	/* maximum one AGC + one NS + one AEC per input stream */
struct aml_stream_in
{
  struct audio_stream_in stream;
  pthread_mutex_t lock;		/* see note below on mutex acquisition order */
  struct pcm_config config;
  struct pcm *pcm;
  int device;
  struct resampler_itfe *resampler;
  struct resampler_itfe *up_resampler;	//shuai
  struct resampler_buffer_provider buf_provider;
  char *up_resampler_buffer;
  struct resampler_buffer_provider up_buf_provider;
  int16_t *buffer;
  size_t frames_in;
  unsigned int requested_rate;
  int standby;
  int source;
  struct echo_reference_itfe *echo_reference;
  bool need_echo_reference;
  effect_handle_t preprocessors[MAX_PREPROCESSORS];
  int num_preprocessors;
  int16_t *proc_buf;
  size_t proc_buf_size;
  size_t proc_frames_in;
  int16_t *ref_buf;
  size_t ref_buf_size;
  size_t ref_frames_in;
  int read_status;
  int voip_mode;
  //hdmi in volume parameters
  int volume_index;
  float last_volume;
  int indexMIn;
  int indexMax;

  struct aml_audio_device *dev;
};

static char cache_buffer_bytes[64];
static uint cached_len = 0;
static void select_output_device (struct aml_audio_device *adev);
static void select_input_device (struct aml_audio_device *adev);
static int adev_set_voice_volume (struct audio_hw_device *dev, float volume);
static int do_input_standby (struct aml_stream_in *in);
static int do_output_standby (struct aml_stream_out *out);

// add compute hdmi in volume function, volume index between 0 and 15 ,this reference AudioPolicyManagerBase

enum
{ VOLMIN = 0, VOLKNEE1 = 1, VOLKNEE2 = 2, VOLMAX = 3, VOLCNT = 4 };

struct VolumeCurvePoint
{
  int mIndex;
  float mDBAttenuation;
};

struct VolumeCurvePoint sSpeakerMediaVolumeCurve[VOLCNT] = {
  {1, -56.0f}, {20, -34.0f}, {60, -11.0f}, {100, 0.0f}
};

// stream descriptor used for volume control
struct StreamDescriptor
{
  int mIndexMin;		// min volume index
  int mIndexMax;		// max volume index
  int mIndexCur[15];		// current volume index
  struct VolumeCurvePoint *mVolumeCurve;
};


float
volIndexToAmpl (struct audio_stream_in *stream, int indexInUi)
{
  struct aml_stream_in *in = (struct aml_stream_in *) stream;
  struct VolumeCurvePoint *curve = NULL;
  struct StreamDescriptor *streamDesc = NULL;

  streamDesc = malloc (sizeof (struct StreamDescriptor));
  streamDesc->mVolumeCurve = sSpeakerMediaVolumeCurve;

  curve = streamDesc->mVolumeCurve;
  streamDesc->mIndexMin = in->indexMIn;
  streamDesc->mIndexMax = in->indexMax;
  // the volume index in the UI is relative to the min and max volume indices for this stream type
  int nbSteps = 1 + curve[VOLMAX].mIndex - curve[VOLMIN].mIndex;

  ALOGD ("volIndexToAmpl,nbSteps=%d", nbSteps);
  int volIdx = (nbSteps * (indexInUi - streamDesc->mIndexMin)) /
	(streamDesc->mIndexMax - streamDesc->mIndexMin);

  // find what part of the curve this index volume belongs to, or if it's out of bounds
  int segment = 0;
  if (volIdx < curve[VOLMIN].mIndex)
	{	// out of bounds
	  return 0.0f;
	}
  else if (volIdx < curve[VOLKNEE1].mIndex)
	{
	  segment = 0;
	}
  else if (volIdx < curve[VOLKNEE2].mIndex)
	{
	  segment = 1;
	}
  else if (volIdx <= curve[VOLMAX].mIndex)
	{
	  segment = 2;
	}
  else
	{	// out of bounds
	  return 1.0f;
	}

  // linear interpolation in the attenuation table in dB
  float decibels = curve[segment].mDBAttenuation +
	((float) (volIdx - curve[segment].mIndex)) *
	((curve[segment + 1].mDBAttenuation -
	  curve[segment].mDBAttenuation) /
	 ((float) (curve[segment + 1].mIndex - curve[segment].mIndex)));

  float amplification = exp (decibels * 0.115129f);	// exp( dB * ln(10) / 20 )

  ALOGD ("VOLUME vol index=[%d %d %d], dB=[%.1f %.1f %.1f] ampl=%.5f",
	 curve[segment].mIndex, volIdx,curve[segment + 1].mIndex,
	 curve[segment].mDBAttenuation,
	 decibels, curve[segment + 1].mDBAttenuation, amplification);

  return amplification;
}

float
computeVolume (struct audio_stream_in *stream)
{
  struct aml_stream_in *in = (struct aml_stream_in *) stream;
  float volume = 1.0f;
  char prop[PROPERTY_VALUE_MAX];

  property_get ("mbx.hdmiin.vol", prop, "15");

  int indexUI = atoi (prop);
  if (indexUI > in->indexMax)
	indexUI = in->indexMax;
  else if (indexUI < in->indexMIn)
	indexUI = in->indexMIn;

  if (in->volume_index != indexUI)
	{
	  in->volume_index = indexUI;

	  volume = volIndexToAmpl (stream, indexUI);
	  in->last_volume = volume;
	}
  else
	{
	  volume = in->last_volume;
	}
  return volume;
}

static int
getprop_bool (const char *path)
{
  char buf[PROPERTY_VALUE_MAX];
  int ret = -1;

  ret = property_get (path, buf, NULL);
  if (ret > 0)
	{
	  if (strcasecmp (buf, "true") == 0 || strcmp (buf, "1") == 0)
	return 1;
	}
  return 0;
}

static int
get_codec_type (const char *path)
{
  int val = 0;
  int fd = open ("/sys/class/audiodsp/digital_codec", O_RDONLY);
  if (fd >= 0)
	{
	  char bcmd[16];
	  read (fd, bcmd, sizeof (bcmd));
	  val = strtol (bcmd, NULL, 10);
	  close (fd);
	}
  else
	{
	  LOGFUNC ("[%s]open digital_codec node failed! return 0\n",
		__FUNCTION__);
	}
  return val;
}

static void
select_output_device (struct aml_audio_device *adev)
{
  LOGFUNC ("%s(mode=%d, out_device=%#x)", __FUNCTION__, adev->mode,
	adev->out_device);
}

static void
select_input_device (struct aml_audio_device *adev)
{
  int mic_in = adev->in_device & AUDIO_DEVICE_IN_BUILTIN_MIC;
  int headset_mic = adev->in_device & AUDIO_DEVICE_IN_WIRED_HEADSET;
  LOGFUNC ("~~~~ %s : in_device(%#x), mic_in(%#x), headset_mic(%#x)",
	__func__, adev->in_device, mic_in, headset_mic);
  return;
}

static void
force_all_standby (struct aml_audio_device *adev)
{
  struct aml_stream_in *in;
  struct aml_stream_out *out;

  LOGFUNC ("%s(%p)", __FUNCTION__, adev);

  if (adev->active_output)
	{
	  out = adev->active_output;
	  pthread_mutex_lock (&out->lock);
	  do_output_standby (out);
	  pthread_mutex_unlock (&out->lock);
	}

  if (adev->active_input)
	{
	  in = adev->active_input;
	  pthread_mutex_lock (&in->lock);
	  do_input_standby (in);
	  pthread_mutex_unlock (&in->lock);
	}
}

static void
select_mode (struct aml_audio_device *adev)
{
  LOGFUNC ("%s(out_device=%#x)", __FUNCTION__, adev->out_device);
  LOGFUNC ("%s(in_device=%#x)", __FUNCTION__, adev->in_device);
  return;
  force_all_standby (adev);
  /* force earpiece route for in call state if speaker is the
	 only currently selected route. This prevents having to tear
	 down the modem PCMs to change route from speaker to earpiece
	 after the ringtone is played, but doesn't cause a route
	 change if a headset or bt device is already connected. If
	 speaker is not the only thing active, just remove it from
	 the route. We'll assume it'll never be used initally during
	 a call. This works because we're sure that the audio policy
	 manager will update the output device after the audio mode
	 change, even if the device selection did not change. */
  if ((adev->out_device & AUDIO_DEVICE_OUT_ALL) == AUDIO_DEVICE_OUT_SPEAKER)
	adev->in_device = AUDIO_DEVICE_IN_BUILTIN_MIC & ~AUDIO_DEVICE_BIT_IN;
  else
	adev->out_device &= ~AUDIO_DEVICE_OUT_SPEAKER;

  select_output_device (adev);
  select_input_device (adev);
  return;
}

/*
  type : 0 -> playback, 1 -> capture  
*/
#define MAX_CARD_NUM	2
int
get_external_card (int type)
{
  int card_num = 1;		// start num, 0 is defualt sound card.

  struct stat card_stat;
  char fpath[256];
  int ret;


  while (card_num <= MAX_CARD_NUM)
	{
	  snprintf (fpath, sizeof (fpath), "/proc/asound/card%d", card_num);

	  ret = stat (fpath, &card_stat);
	  if (ret < 0)
	{
	  ret = -1;
	}
	  else
	{
	  snprintf (fpath, sizeof (fpath), "/dev/snd/pcmC%uD0%c", card_num,
	  type ? 'c' : 'p');
	  ret = stat (fpath, &card_stat);
	  if (ret == 0)
	 {
	   return card_num;
	 }
	}

	  card_num++;
	}

  return ret;
}

static int
check_output_stream (struct aml_stream_out *out)
{
  int ret = 0;
  unsigned int card = CARD_AMLOGIC_DEFAULT;
  unsigned int port = PORT_MM;
  int ext_card;
  ext_card = get_external_card (0);
  if (ext_card < 0)
	{
	  card = CARD_AMLOGIC_DEFAULT;
	}
  else
	{
	  card = ext_card;
	}

  out->config.start_threshold = PERIOD_SIZE * 2;
  out->config.avail_min = 0;	//SHORT_PERIOD_SIZE;
  return 0;
}

static int
get_aml_card ()
{
  int card = -1, err = 0;
  int fd = -1;
  unsigned fileSize = 512;
  char *read_buf = NULL, *pd = NULL;
  static const char *const SOUND_CARDS_PATH = "/proc/asound/cards";
  fd = open (SOUND_CARDS_PATH, O_RDONLY);
  if (fd < 0)
	{
	  ALOGE ("ERROR: failed to open config file %s error: %d\n",
	  SOUND_CARDS_PATH, errno);
	  close (fd);
	  return -EINVAL;
	}

  read_buf = (char *) malloc (fileSize);
  if (!read_buf)
	{
	  ALOGE ("Failed to malloc read_buf");
	  close (fd);
	  return -ENOMEM;
	}
  memset (read_buf, 0x0, fileSize);
  err = read (fd, read_buf, fileSize);
  if (fd < 0)
	{
	  ALOGE ("ERROR: failed to read config file %s error: %d\n",
	  SOUND_CARDS_PATH, errno);
	  close (fd);
	  return -EINVAL;
	}
  pd = strstr (read_buf, "AML");
  card = *(pd - 3) - '0';

OUT:
  free (read_buf);
  close (fd);
  return card;
}

static int
get_spdif_port ()
{
  int port = -1, err = 0;
  int fd = -1;
  unsigned fileSize = 512;
  char *read_buf = NULL, *pd = NULL;
  static const char *const SOUND_PCM_PATH = "/proc/asound/pcm";
  fd = open (SOUND_PCM_PATH, O_RDONLY);
  if (fd < 0)
	{
	  ALOGE ("ERROR: failed to open config file %s error: %d\n",
	  SOUND_PCM_PATH, errno);
	  close (fd);
	  return -EINVAL;
	}

  read_buf = (char *) malloc (fileSize);
  if (!read_buf)
	{
	  ALOGE ("Failed to malloc read_buf");
	  close (fd);
	  return -ENOMEM;
	}
  memset (read_buf, 0x0, fileSize);
  err = read (fd, read_buf, fileSize);
  if (fd < 0)
	{
	  ALOGE ("ERROR: failed to read config file %s error: %d\n",
	  SOUND_PCM_PATH, errno);
	  close (fd);
	  return -EINVAL;
	}
  pd = strstr (read_buf, "SPDIF");
  port = *(pd - 3) - '0';

OUT:
  free (read_buf);
  close (fd);
  return port;
}

/* must be called with hw device and output stream mutexes locked */
static int
start_output_stream (struct aml_stream_out *out)
{
  struct aml_audio_device *adev = out->dev;
  int card = CARD_AMLOGIC_DEFAULT;
  int port = PORT_MM;
  int ret = 0;

  adev->active_output = out;

  if (adev->mode != AUDIO_MODE_IN_CALL)
	{
	  /* FIXME: only works if only one output can be active at a time */
	  select_output_device (adev);
	}
  LOGFUNC ("%s(adev->out_device=%#x, adev->mode=%d)", __FUNCTION__,
	adev->out_device, adev->mode);

  card = get_aml_card ();
  if (card < 0)
	{
	  ALOGE ("hdmi get aml card id failed \n");
	  card = CARD_AMLOGIC_DEFAULT;
	}
  port = get_spdif_port ();
  if (port < 0)
	{
	  ALOGE ("hdmi get aml card port  failed \n");
	  card = PORT_MM;
	}
  ALOGI ("hdmi sound card id %d,device id %d \n", card, port);
  if (out->config.channels == 8)
	{
	  port = 0;
	  out->config.format = PCM_FORMAT_S32_LE;
	  adev->out_device = AUDIO_DEVICE_OUT_SPEAKER;
	  ALOGI ("[%s %d]8CH format output: set port/0 adev->out_device/%d\n",
	  __FUNCTION__, __LINE__, AUDIO_DEVICE_OUT_SPEAKER);
	}
  LOGFUNC ("------------open on board audio-------");
  if (getprop_bool ("media.libplayer.wfd"))
	{
	  out->config.period_size = PERIOD_SIZE;
	}
  /* default to low power: will be corrected in out_write if necessary before first write to
   * tinyalsa.
   */
  int codec_type = get_codec_type ("/sys/class/audiodsp/digital_codec");
  if (codec_type == 4 || codec_type == 5)
	{
	  out->config.period_size = PERIOD_SIZE * 2;
	  out->write_threshold = PLAYBACK_PERIOD_COUNT * PERIOD_SIZE * 2;
	  out->config.start_threshold = PLAYBACK_PERIOD_COUNT * PERIOD_SIZE * 2;
	}
  else if (codec_type == 7 || codec_type == 8)
	{
	  out->config.period_size = PERIOD_SIZE * 4 * 2;
	  out->write_threshold = PLAYBACK_PERIOD_COUNT * PERIOD_SIZE * 4 * 2;
	  out->config.start_threshold =
	PLAYBACK_PERIOD_COUNT * PERIOD_SIZE * 4 * 2;
	}
  else
	{
	  out->config.period_size = PERIOD_SIZE;
	  out->write_threshold = PLAYBACK_PERIOD_COUNT * PERIOD_SIZE;
	  out->config.start_threshold = PERIOD_SIZE * PLAYBACK_PERIOD_COUNT;
	}
  out->config.avail_min = 0;	//SHORT_PERIOD_SIZE

  if (out->config.rate != DEFAULT_OUT_SAMPLING_RATE)
	{

	  ret = create_resampler (DEFAULT_OUT_SAMPLING_RATE,
		 out->config.rate,
		 2,
		 RESAMPLER_QUALITY_DEFAULT,
		 NULL, &out->resampler);
	  if (ret != 0)
	{
	  ALOGE ("cannot create resampler for output");
	  return ENOMEM;
	}
	  out->buffer = malloc (RESAMPLER_BUFFER_SIZE);	/* todo: allow for reallocing */
	  if (out->buffer == NULL)
	return ENOMEM;
	}

  ALOGI
	("channels=%d---format=%d---period_count%d---period_size%d---rate=%d---",
	 out->config.channels, out->config.format, out->config.period_count,
	 out->config.period_size, out->config.rate);
  out->pcm =
	pcm_open (card, port, PCM_OUT /*| PCM_MMAP | PCM_NOIRQ */ ,
	   &(out->config));
  if (!pcm_is_ready (out->pcm))
	{
	  ALOGE ("cannot open pcm_out driver: %s", pcm_get_error (out->pcm));
	  pcm_close (out->pcm);
	  adev->active_output = NULL;
	  return -ENOMEM;
	}
  if (adev->echo_reference != NULL)
	out->echo_reference = adev->echo_reference;
  if (out->resampler)
	{
	  out->resampler->reset (out->resampler);
	}

  return 0;
}

static int
check_input_parameters (uint32_t sample_rate, int format, int channel_count)
{
  LOGFUNC ("%s(sample_rate=%d, format=%d, channel_count=%d)", __FUNCTION__,
	sample_rate, format, channel_count);

  if (format != AUDIO_FORMAT_PCM_16_BIT)
	return -EINVAL;

  if ((channel_count < 1) || (channel_count > 2))
	return -EINVAL;

  switch (sample_rate)
	{
	case 8000:
	case 11025:
	case 16000:
	case 22050:
	case 24000:
	case 32000:
	case 44100:
	case 48000:
	  break;
	default:
	  return -EINVAL;
	}

  return 0;
}

static size_t
get_input_buffer_size (uint32_t sample_rate, int format, int channel_count)
{
  size_t size;
  size_t device_rate;

  LOGFUNC ("%s(sample_rate=%d, format=%d, channel_count=%d)", __FUNCTION__,
	sample_rate, format, channel_count);

  if (check_input_parameters (sample_rate, format, channel_count) != 0)
	return 0;

  /* take resampling into account and return the closest majoring
	 multiple of 16 frames, as audioflinger expects audio buffers to
	 be a multiple of 16 frames */
  size = (pcm_config_in.period_size * sample_rate) / pcm_config_in.rate;
  size = ((size + 15) / 16) * 16;

  return size * channel_count * sizeof (short);
}

static void
add_echo_reference (struct aml_stream_out *out,
	  struct echo_reference_itfe *reference)
{
  pthread_mutex_lock (&out->lock);
  out->echo_reference = reference;
  pthread_mutex_unlock (&out->lock);
}

static void
remove_echo_reference (struct aml_stream_out *out,
		 struct echo_reference_itfe *reference)
{
  pthread_mutex_lock (&out->lock);
  if (out->echo_reference == reference)
	{
	  /* stop writing to echo reference */
	  reference->write (reference, NULL);
	  out->echo_reference = NULL;
	}
  pthread_mutex_unlock (&out->lock);
}

static void
put_echo_reference (struct aml_audio_device *adev,
	  struct echo_reference_itfe *reference)
{
  if (adev->echo_reference != NULL && reference == adev->echo_reference)
	{
	  if (adev->active_output != NULL)
	remove_echo_reference (adev->active_output, reference);
	  release_echo_reference (reference);
	  adev->echo_reference = NULL;
	}
}

static struct echo_reference_itfe *
get_echo_reference (struct aml_audio_device *adev,
	  audio_format_t format,
	  uint32_t channel_count, uint32_t sampling_rate)
{
  put_echo_reference (adev, adev->echo_reference);
  if (adev->active_output != NULL)
	{
	  struct audio_stream *stream = &adev->active_output->stream.common;
	  uint32_t wr_channel_count = popcount (stream->get_channels (stream));
	  uint32_t wr_sampling_rate = stream->get_sample_rate (stream);

	  int status = create_echo_reference (AUDIO_FORMAT_PCM_16_BIT,
		  channel_count,
		  sampling_rate,
		  AUDIO_FORMAT_PCM_16_BIT,
		  wr_channel_count,
		  wr_sampling_rate,
		  &adev->echo_reference);
	  if (status == 0)
	add_echo_reference (adev->active_output, adev->echo_reference);
	}
  return adev->echo_reference;
}

static int
get_playback_delay (struct aml_stream_out *out,
	  size_t frames, struct echo_reference_buffer *buffer)
{

  size_t kernel_frames;
  int status;
  status = pcm_get_htimestamp (out->pcm, &kernel_frames, &buffer->time_stamp);
  if (status < 0)
	{
	  buffer->time_stamp.tv_sec = 0;
	  buffer->time_stamp.tv_nsec = 0;
	  buffer->delay_ns = 0;
	  ALOGV ("get_playback_delay(): pcm_get_htimestamp error,"
	  "setting playbackTimestamp to 0");
	  return status;
	}
  kernel_frames = pcm_get_buffer_size (out->pcm) - kernel_frames;
  ALOGV ("~~pcm_get_buffer_size(out->pcm)=%d",
	 pcm_get_buffer_size (out->pcm));
  /* adjust render time stamp with delay added by current driver buffer.
   * Add the duration of current frame as we want the render time of the last
   * sample being written. */
  buffer->delay_ns =
	(long) (((int64_t) (kernel_frames + frames) * 1000000000) /
	 DEFAULT_OUT_SAMPLING_RATE);

  ALOGV ("get_playback_delay time_stamp = [%ld].[%ld], delay_ns: [%d],"
	 "kernel_frames:[%d]",
	 buffer->time_stamp.tv_sec, buffer->time_stamp.tv_nsec,
	 buffer->delay_ns, kernel_frames);
  return 0;
}

static uint32_t
out_get_sample_rate (const struct audio_stream *stream)
{
  return DEFAULT_OUT_SAMPLING_RATE;
}

static int
out_set_sample_rate (struct audio_stream *stream, uint32_t rate)
{
  LOGFUNC ("%s(%p, %d)", __FUNCTION__, stream, rate);

  return 0;
}

static size_t
out_get_buffer_size (const struct audio_stream *stream)
{
  struct aml_stream_out *out = (struct aml_stream_out *) stream;

  LOGFUNC ("%s(out->config.rate=%d)", __FUNCTION__, out->config.rate);

  /* take resampling into account and return the closest majoring
   * multiple of 16 frames, as audioflinger expects audio buffers to
   * be a multiple of 16 frames
   */
  size_t size;
  int codec_type = get_codec_type ("/sys/class/audiodsp/digital_codec");
  if (codec_type == 4 || codec_type == 5)	//dd+
	size =
	  (PERIOD_SIZE * 2 * PLAYBACK_PERIOD_COUNT *
	   (int64_t) DEFAULT_OUT_SAMPLING_RATE) / out->config.rate;
  else if (codec_type == 7)
	size =
	  (PERIOD_SIZE * 4 * 4 * PLAYBACK_PERIOD_COUNT *
	   (int64_t) DEFAULT_OUT_SAMPLING_RATE) / out->config.rate;
  else if (codec_type > 0 && codec_type < 4)	//dd/dts
	size =
	  (PERIOD_SIZE * 4 * (int64_t) DEFAULT_OUT_SAMPLING_RATE) /
	  out->config.rate;
  else	//pcm
	size =
	  (PERIOD_SIZE * (int64_t) DEFAULT_OUT_SAMPLING_RATE) / out->config.rate;

  size = ((size + 15) / 16) * 16;
  return size * audio_stream_out_frame_size (&out->stream);
}

static audio_channel_mask_t
out_get_channels (const struct audio_stream *stream)
{
  struct aml_stream_out *out = (struct aml_stream_out *) stream;
  if (out->multich > 2)
	{
	  if (out->multich == 6)
	return AUDIO_CHANNEL_OUT_5POINT1;
	  else if (out->multich == 8)
	return AUDIO_CHANNEL_OUT_7POINT1;
	}
  if (out->config.channels == 1)
	{
	  return AUDIO_CHANNEL_OUT_MONO;
	}
  else
	{
	  return AUDIO_CHANNEL_OUT_STEREO;
	}
}

static audio_format_t
out_get_format (const struct audio_stream *stream)
{
  return AUDIO_FORMAT_PCM_16_BIT;
}

static int
out_set_format (struct audio_stream *stream, int format)
{
  LOGFUNC ("%s(%p)", __FUNCTION__, stream);

  return 0;
}

/* must be called with hw device and output stream mutexes locked */
static int
do_output_standby (struct aml_stream_out *out)
{
  struct aml_audio_device *adev = out->dev;
  if (!out->standby)
	{
	  pcm_close (out->pcm);
	  out->pcm = NULL;

	  adev->active_output = 0;

	  /* if in call, don't turn off the output stage. This will
		 be done when the call is ended */
	  if (adev->mode != AUDIO_MODE_IN_CALL)
	{
	  /* FIXME: only works if only one output can be active at a time */

	  //reset_mixer_state(adev->ar);
	}

	  /* stop writing to echo reference */
	  if (out->echo_reference != NULL)
	{
	  out->echo_reference->write (out->echo_reference, NULL);
	  out->echo_reference = NULL;
	}

	  out->standby = 1;
	  first_write_status = 0;
	}

  return 0;
}

static int
out_standby (struct audio_stream *stream)
{
  struct aml_stream_out *out = (struct aml_stream_out *) stream;
  int status;

  LOGFUNC ("%s(%p)", __FUNCTION__, stream);

  pthread_mutex_lock (&out->dev->lock);
  pthread_mutex_lock (&out->lock);
  status = do_output_standby (out);
  pthread_mutex_unlock (&out->lock);
  pthread_mutex_unlock (&out->dev->lock);
  return status;
}

static int
out_dump (const struct audio_stream *stream, int fd)
{
  LOGFUNC ("%s(%p, %d)", __FUNCTION__, stream, fd);
  return 0;
}

static int
out_set_parameters (struct audio_stream *stream, const char *kvpairs)
{
  struct aml_stream_out *out = (struct aml_stream_out *) stream;
  struct aml_audio_device *adev = out->dev;
  struct aml_stream_in *in;
  struct str_parms *parms;
  char *str;
  char value[32];
  int ret, val = 0;
  bool force_input_standby = false;

  LOGFUNC ("%s(kvpairs(%s), out_device=%#x)", __FUNCTION__, kvpairs,
	adev->out_device);
  parms = str_parms_create_str (kvpairs);

  ret =
	str_parms_get_str (parms, AUDIO_PARAMETER_STREAM_ROUTING, value,
		 sizeof (value));
  if (ret >= 0)
	{
	  val = atoi (value);
	  pthread_mutex_lock (&adev->lock);
	  pthread_mutex_lock (&out->lock);
	  if (((adev->out_device & AUDIO_DEVICE_OUT_ALL) != val) && (val != 0))
	{
	  if (out == adev->active_output)
	 {
	   do_output_standby (out);
	   /* a change in output device may change the microphone selection */
	   if (adev->active_input &&
	adev->active_input->source ==
	AUDIO_SOURCE_VOICE_COMMUNICATION)
		{
	force_input_standby = true;
		}
	   /* force standby if moving to/from HDMI */
	   if (((val & AUDIO_DEVICE_OUT_AUX_DIGITAL) ^
	 (adev->out_device & AUDIO_DEVICE_OUT_AUX_DIGITAL)) ||
	((val & AUDIO_DEVICE_OUT_DGTL_DOCK_HEADSET) ^
	 (adev->out_device & AUDIO_DEVICE_OUT_DGTL_DOCK_HEADSET)))
		do_output_standby (out);
	 }
	  adev->out_device &= ~AUDIO_DEVICE_OUT_ALL;
	  adev->out_device |= val;
	  select_output_device (adev);
	}
	  pthread_mutex_unlock (&out->lock);
	  if (force_input_standby)
	{
	  in = adev->active_input;
	  pthread_mutex_lock (&in->lock);
	  do_input_standby (in);
	  pthread_mutex_unlock (&in->lock);
	}
	  pthread_mutex_unlock (&adev->lock);
	  goto exit;
	}
  int sr = 0;
  ret = str_parms_get_int (parms, AUDIO_PARAMETER_STREAM_SAMPLING_RATE, &sr);
  if (ret >= 0)
	{
	  if (sr > 0)
	{
	  ALOGI ("audio hw sampling_rate change from %d to %d \n",
		 DEFAULT_OUT_SAMPLING_RATE, sr);
	  DEFAULT_OUT_SAMPLING_RATE = sr;
	  pcm_config_out.rate = DEFAULT_OUT_SAMPLING_RATE;
	  out->config.rate = DEFAULT_OUT_SAMPLING_RATE;
	  pthread_mutex_lock (&adev->lock);
	  pthread_mutex_lock (&out->lock);
	  if (!out->standby && (out == adev->active_output))
	 {
	   //do_output_standby (out);
	   //start_output_stream (out);
	   //out->standby = 0;
	 }
	  pthread_mutex_unlock (&adev->lock);
	  pthread_mutex_unlock (&out->lock);

	}
	  goto exit;
	}
  int frame_size = 0;
  ret =
	str_parms_get_int (parms, AUDIO_PARAMETER_STREAM_FRAME_COUNT,
		 &frame_size);
  if (ret >= 0)
	{
	  if (frame_size > 0)
	{
	  ALOGI ("audio hw frame size change from %d to %d \n", PERIOD_SIZE,
		 frame_size);
	  PERIOD_SIZE = frame_size;
	  pcm_config_out.period_size = PERIOD_SIZE;
	  out->config.period_size = PERIOD_SIZE;
	  pthread_mutex_lock (&adev->lock);
	  pthread_mutex_lock (&out->lock);
	  if (!out->standby && (out == adev->active_output))
	 {
	   //do_output_standby (out);
	   //start_output_stream (out);
	   //out->standby = 0;
	 }
	  pthread_mutex_unlock (&adev->lock);
	  pthread_mutex_unlock (&out->lock);

	}
	}
exit:
  str_parms_destroy (parms);
  return ret;
}

static char *
out_get_parameters (const struct audio_stream *stream, const char *keys)
{
  return strdup ("");
}

#if 0
static uint32_t
out_get_latency (const struct audio_stream_out *stream)
{
  struct aml_stream_out *out = (struct aml_stream_out *) stream;
  uint32_t whole_latency;
  uint32_t ret;
  whole_latency =
	(out->config.period_size * out->config.period_count * 1000) /
	out->config.rate;
  if (!out->pcm || !pcm_is_ready (out->pcm))
	return whole_latency;
  ret = pcm_get_latency (out->pcm);
  if (ret == -1)
	{
	  return whole_latency;
	}
  return ret;
}
#else
static uint32_t
out_get_latency (const struct audio_stream_out *stream)
{
  struct aml_stream_out *out = (struct aml_stream_out *) stream;
  return (out->config.period_size * out->config.period_count * 1000) /
	out->config.rate;
}
#endif
static int
out_set_volume (struct audio_stream_out *stream, float left, float right)
{
  return -ENOSYS;
}

static int
get_sysfs_int (const char *path)
{
  int val = 0;
  int fd = open (path, O_RDONLY);
  if (fd >= 0)
	{
	  char bcmd[16];
	  read (fd, bcmd, sizeof (bcmd));
	  val = strtol (bcmd, NULL, 10);
	  close (fd);
	}
  else
	{
	  LOGFUNC ("[%s]open %s node failed! return 0\n", path, __FUNCTION__);
	}
  return val;
}

static int last_codec_type = -1;
static ssize_t
out_write (struct audio_stream_out *stream, const void *buffer, size_t bytes)
{

  int ret = 0;
  struct aml_stream_out *out = (struct aml_stream_out *) stream;
  struct aml_audio_device *adev = out->dev;
  size_t frame_size = audio_stream_out_frame_size (stream);
  size_t in_frames = bytes / frame_size;
  size_t out_frames = RESAMPLER_BUFFER_SIZE / frame_size;
  bool force_input_standby = false;
  struct aml_stream_in *in;
  bool low_power;
  int kernel_frames;
  void *buf;
  char output_buffer_bytes[RESAMPLER_BUFFER_SIZE + 128];
  uint ouput_len;
  char *data, *data_dst;
  volatile char *data_src;
  short *dataprint;
  uint i, total_len;
  char prop[PROPERTY_VALUE_MAX];
  int codec_type = 0;
  int samesource_flag = 0;
  /* acquiring hw device mutex systematically is useful if a low priority thread is waiting
   * on the output stream mutex - e.g. executing select_mode() while holding the hw device
   * mutex
   */


  pthread_mutex_lock (&adev->lock);
  pthread_mutex_lock (&out->lock);
  if (out->standby)
	{
	  ret = start_output_stream (out);
	  if (ret != 0)
	{
	  pthread_mutex_unlock (&adev->lock);
	  goto exit;
	}
	  out->standby = 0;
	  first_write_status = 0;
	  /* a change in output device may change the microphone selection */
	  if (adev->active_input &&
	  adev->active_input->source == AUDIO_SOURCE_VOICE_COMMUNICATION)
	force_input_standby = true;
	}
  pthread_mutex_unlock (&adev->lock);

  /* only use resampler if required */
  if (out->config.rate != DEFAULT_OUT_SAMPLING_RATE)
	{
	  if (!out->resampler)
	{

	  ret = create_resampler (DEFAULT_OUT_SAMPLING_RATE,
	  out->config.rate,
	  2,
	  RESAMPLER_QUALITY_DEFAULT,
	  NULL, &out->resampler);
	  if (ret != 0)
	 goto exit;
	  out->buffer = malloc (RESAMPLER_BUFFER_SIZE);	/* todo: allow for reallocing */
	  if (!out->buffer)
	 {
	   ret = -ENOMEM;
	   goto exit;
	 }
	}
	  out->resampler->resample_from_input (out->resampler,
		(int16_t *) buffer,
		&in_frames,
		(int16_t *) out->buffer,
		&out_frames);
	  buf = out->buffer;
	}
  else
	{
	  out_frames = in_frames;
	  buf = (void *) buffer;
	}
  if (out->echo_reference != NULL)
	{
	  struct echo_reference_buffer b;
	  b.raw = (void *) buffer;
	  b.frame_count = in_frames;
	  get_playback_delay (out, out_frames, &b);
	  out->echo_reference->write (out->echo_reference, &b);
	}

#if 0
  FILE *fp = fopen ("/data/audio_out.pcm", "a+");
  if (fp)
	{
	  int flen = fwrite ((char *) buffer, 1, out_frames * frame_size, fp);
	  LOGFUNC ("flen = %d---outlen=%d ", flen, out_frames * frame_size);
	  fclose (fp);
	}
  else
	{
	  LOGFUNC ("could not open file:audio_out");
	}
#endif

  if (out->config.rate != DEFAULT_OUT_SAMPLING_RATE)
	{
	  total_len = out_frames * frame_size + cached_len;


	  LOGFUNC ("total_len(%d) = resampler_out_len(%d) + cached_len111(%d)",
		total_len, out_frames * frame_size, cached_len);

	  data_src = (char *) cache_buffer_bytes;
	  data_dst = (char *) output_buffer_bytes;



	  /*write_back data from cached_buffer */
	  if (cached_len)
	{
	  memcpy ((void *) data_dst, (void *) data_src, cached_len);
	  data_dst += cached_len;
	}

	  ouput_len = total_len & (~0x3f);
	  data = (char *) buf;

	  memcpy ((void *) data_dst, (void *) data, ouput_len - cached_len);
	  data += (ouput_len - cached_len);
	  cached_len = total_len & 0x3f;
	  data_src = (char *) cache_buffer_bytes;

	  /*save data to cached_buffer */
	  if (cached_len)
	{
	  memcpy ((void *) data_src, (void *) data, cached_len);
	}
	  if (!out->standby)
	ret = pcm_write (out->pcm, (void *) output_buffer_bytes, ouput_len);
	}
  else
	{
	  if (!out->standby)
	{
	  property_get ("sys.hdmiIn.Capture", prop, "false");
	  // ALOGD("****first_write_status=%d***",first_write_status);
	  if (!strcmp (prop, "true"))
	 {
	   if (first_write_status < 20)
		{
	first_write_status = first_write_status + 1;
	memset ((char *) buf, 0, bytes);
		}
	 }
	  else
	 {
	   first_write_status = 0;
	 }
	  if (out->config.channels == 8)
	 {
	   int *p32 = NULL;
	   short *p16 = (short *) buf;
	   int i, NumSamps;
	   NumSamps = out_frames * frame_size / sizeof (short);
	   p32 = malloc (NumSamps * sizeof (int));
	   if (p32 != NULL)
		{
	for (i = 0; i < NumSamps; i++)	//suppose 16bit/8ch PCM
	  {
		p32[i] = p16[i] << 16;
	  }
	ret = pcm_write (out->pcm, (void *) p32, NumSamps * 4);
	free (p32);
		}
	 }
	  else
	 {
	   codec_type =
		get_sysfs_int ("/sys/class/audiodsp/digital_codec");
	   samesource_flag =
		get_sysfs_int ("/sys/class/audiodsp/audio_samesource");
	   if (last_codec_type > 0 && codec_type != last_codec_type)
		{
	samesource_flag = 1;
		}
	   if (samesource_flag == 1 && codec_type)
		{
	ALOGI
	  ("to disable same source,need reset alsa,last %d,type %d,same source flag %d ,\n",
	   last_codec_type, codec_type, samesource_flag);
	last_codec_type = codec_type;
	pcm_stop (out->pcm);
		}
	   ret =
		pcm_write (out->pcm, (void *) buf, out_frames * frame_size);
	 }
	  //ret = pcm_mmap_write(out->pcm, (void *)buf, out_frames * frame_size);
	}
	}


exit:
  pthread_mutex_unlock (&out->lock);
  if (ret != 0)
	{
	  usleep (bytes * 1000000 / audio_stream_out_frame_size (stream) /
	   out_get_sample_rate (&stream->common));
	}

  if (force_input_standby)
	{
	  pthread_mutex_lock (&adev->lock);
	  if (adev->active_input)
	{
	  in = adev->active_input;
	  pthread_mutex_lock (&in->lock);
	  do_input_standby (in);
	  pthread_mutex_unlock (&in->lock);
	}
	  pthread_mutex_unlock (&adev->lock);
	}


  return bytes;
}

static int
out_get_render_position (const struct audio_stream_out *stream,
	uint32_t * dsp_frames)
{
  LOGFUNC ("%s(%p, %p)", __FUNCTION__, stream, dsp_frames);
  return -EINVAL;
}

static int
out_add_audio_effect (const struct audio_stream *stream,
		effect_handle_t effect)
{
  LOGFUNC ("%s(%p, %p)", __FUNCTION__, stream, effect);
  return 0;
}

static int
out_remove_audio_effect (const struct audio_stream *stream,
	effect_handle_t effect)
{
  return 0;
}

static int
out_get_next_write_timestamp (const struct audio_stream_out *stream,
		 int64_t * timestamp)
{
  return -EINVAL;
}

static int get_next_buffer (struct resampler_buffer_provider *buffer_provider,
	   struct resampler_buffer *buffer);
static void release_buffer (struct resampler_buffer_provider *buffer_provider,
	   struct resampler_buffer *buffer);


/** audio_stream_in implementation **/

/* must be called with hw device and input stream mutexes locked */
static int
start_input_stream (struct aml_stream_in *in)
{
  int ret = 0;
  unsigned int card = CARD_AMLOGIC_DEFAULT;
  unsigned int port = PORT_MM;

  struct aml_audio_device *adev = in->dev;
  LOGFUNC
	("%s(need_echo_reference=%d, channels=%d, rate=%d, requested_rate=%d, mode= %d)",
	 __FUNCTION__, in->need_echo_reference, in->config.channels,
	 in->config.rate, in->requested_rate, adev->mode);
  adev->active_input = in;

  if (adev->mode != AUDIO_MODE_IN_CALL)
	{
	  adev->in_device &= ~AUDIO_DEVICE_IN_ALL;
	  adev->in_device |= in->device;
	  select_input_device (adev);
	}

  ALOGV ("%s(in->requested_rate=%d, in->config.rate=%d)",
	 __FUNCTION__, in->requested_rate, in->config.rate);

  if (getprop_bool ("media.libplayer.wfd"))
	{
	  PERIOD_SIZE = DEFAULT_PERIOD_SIZE / 2;
	  in->config.period_size = PERIOD_SIZE;
	}
  else
	{
	  PERIOD_SIZE = DEFAULT_PERIOD_SIZE;
	  in->config.period_size = PERIOD_SIZE;
	}
  if (in->need_echo_reference && in->echo_reference == NULL)
	{
	  in->echo_reference = get_echo_reference (adev,
			AUDIO_FORMAT_PCM_16_BIT,
			in->config.channels,
			VX_NB_SAMPLING_RATE);
	  //in->requested_rate);
	  LOGFUNC ("%s(after get_echo_ref.... now in->echo_reference = %p)",
		__FUNCTION__, in->echo_reference);

	  if (in->echo_reference != NULL)
	{
	  in->buf_provider.get_next_buffer = get_next_buffer;
	  in->buf_provider.release_buffer = release_buffer;
	  ret = create_resampler (in->config.rate,
	  VX_NB_SAMPLING_RATE,
	  in->config.channels,
	  RESAMPLER_QUALITY_DEFAULT,
	  &in->buf_provider, &in->resampler);

	  if (ret != 0)
	 {
	   ret = -EINVAL;
	   return ret;
	 }
	  ret = create_resampler (VX_NB_SAMPLING_RATE, in->config.rate, in->config.channels, RESAMPLER_QUALITY_DEFAULT, NULL,	//&in->up_buf_provider,
	  &in->up_resampler);

	  if (ret != 0)
	 {
	   ret = -EINVAL;
	   return ret;
	 }
	  in->up_resampler_buffer = malloc (RESAMPLER_BUFFER_SIZE);	/* todo: allow for reallocing */
	  if (in->up_resampler_buffer == NULL)
	 return ENOMEM;
	}
	}
  /* this assumes routing is done previously */
  in->pcm = pcm_open (card, port, PCM_IN, &in->config);
  if (!pcm_is_ready (in->pcm))
	{
	  ALOGE ("cannot open pcm_in driver: %s", pcm_get_error (in->pcm));
	  pcm_close (in->pcm);
	  adev->active_input = NULL;
	  return -ENOMEM;
	}
  ALOGD ("pcm_open in: card(%d), port(%d)", card, port);

  /* if no supported sample rate is available, use the resampler */
  if (in->resampler)
	{
	  in->resampler->reset (in->resampler);
	  in->frames_in = 0;
	}
  return 0;
}

static int
check_input_stream (struct aml_stream_in *in)
{
  int ret = 0;
  unsigned int card = CARD_AMLOGIC_BOARD;
  unsigned int port = 0;
  int ext_card;

  ext_card = get_external_card (1);
  if (ext_card < 0)
	{
	  card = CARD_AMLOGIC_BOARD;
	}
  else
	{
	  card = ext_card;
	}

  /* this assumes routing is done previously */
  in->pcm = pcm_open (card, port, PCM_IN, &in->config);
  if (!pcm_is_ready (in->pcm))
	{
	  ALOGE ("check_input_stream:cannot open pcm_in driver: %s",
	  pcm_get_error (in->pcm));
	  pcm_close (in->pcm);
	  return -ENOMEM;
	}

  pcm_close (in->pcm);

  return 0;
}

static uint32_t
in_get_sample_rate (const struct audio_stream *stream)
{
  struct aml_stream_in *in = (struct aml_stream_in *) stream;

  LOGFUNC ("%s(%p)", __FUNCTION__, stream);
  return in->requested_rate;
}

static int
in_set_sample_rate (struct audio_stream *stream, uint32_t rate)
{
  LOGFUNC ("%s(%p, %d)", __FUNCTION__, stream, rate);
  return 0;
}

static size_t
in_get_buffer_size (const struct audio_stream *stream)
{
  struct aml_stream_in *in = (struct aml_stream_in *) stream;

  LOGFUNC ("%s(%p)", __FUNCTION__, stream);
  return get_input_buffer_size (in->config.rate,
	AUDIO_FORMAT_PCM_16_BIT, in->config.channels);
}

static audio_channel_mask_t
in_get_channels (const struct audio_stream *stream)
{
  struct aml_stream_in *in = (struct aml_stream_in *) stream;
  if (in->config.channels == 1)
	{
	  return AUDIO_CHANNEL_IN_MONO;
	}
  else
	{
	  return AUDIO_CHANNEL_IN_STEREO;
	}
}

static audio_format_t
in_get_format (const struct audio_stream *stream)
{
  return AUDIO_FORMAT_PCM_16_BIT;
}

static int
in_set_format (struct audio_stream *stream, audio_format_t format)
{
  LOGFUNC ("%s(%p, %d)", __FUNCTION__, stream, format);
  return 0;
}

/* must be called with hw device and input stream mutexes locked */
static int
do_input_standby (struct aml_stream_in *in)
{
  struct aml_audio_device *adev = in->dev;

  LOGFUNC ("%s(%p)", __FUNCTION__, in);
  if (!in->standby)
	{
	  pcm_close (in->pcm);
	  in->pcm = NULL;

	  adev->active_input = 0;
	  if (adev->mode != AUDIO_MODE_IN_CALL)
	{
	  adev->in_device &= ~AUDIO_DEVICE_IN_ALL;
	  select_input_device (adev);
	}

	  if (in->echo_reference != NULL)
	{
	  /* stop reading from echo reference */
	  in->echo_reference->read (in->echo_reference, NULL);
	  put_echo_reference (adev, in->echo_reference);
	  in->echo_reference = NULL;
	}

	  in->standby = 1;
	}
  return 0;
}

static int
in_standby (struct audio_stream *stream)
{
  struct aml_stream_in *in = (struct aml_stream_in *) stream;
  int status;
  LOGFUNC ("%s(%p)", __FUNCTION__, stream);

  pthread_mutex_lock (&in->dev->lock);
  pthread_mutex_lock (&in->lock);
  status = do_input_standby (in);
  pthread_mutex_unlock (&in->lock);
  pthread_mutex_unlock (&in->dev->lock);
  return status;
}

static int
in_dump (const struct audio_stream *stream, int fd)
{
  LOGFUNC ("%s(%p, %d)", __FUNCTION__, stream, fd);
  return 0;
}

static int
in_set_parameters (struct audio_stream *stream, const char *kvpairs)
{
  struct aml_stream_in *in = (struct aml_stream_in *) stream;
  struct aml_audio_device *adev = in->dev;
  struct str_parms *parms;
  char *str;
  char value[32];
  int ret, val = 0;
  bool do_standby = false;

  LOGFUNC ("%s(%p, %s)", __FUNCTION__, stream, kvpairs);
  parms = str_parms_create_str (kvpairs);

  ret =
	str_parms_get_str (parms, AUDIO_PARAMETER_STREAM_INPUT_SOURCE, value,
		 sizeof (value));

  pthread_mutex_lock (&adev->lock);
  pthread_mutex_lock (&in->lock);
  if (ret >= 0)
	{
	  val = atoi (value);
	  /* no audio source uses val == 0 */
	  if ((in->source != val) && (val != 0))
	{
	  in->source = val;
	  do_standby = true;
	}
	}

  ret =
	str_parms_get_str (parms, AUDIO_PARAMETER_STREAM_ROUTING, value,
		 sizeof (value));
  if (ret >= 0)
	{
	  val = atoi (value) & ~AUDIO_DEVICE_BIT_IN;
	  if ((in->device != val) && (val != 0))
	{
	  in->device = val;
	  do_standby = true;
	}
	}

  if (do_standby)
	do_input_standby (in);
  pthread_mutex_unlock (&in->lock);
  pthread_mutex_unlock (&adev->lock);

  str_parms_destroy (parms);
  return ret;
}

static char *
in_get_parameters (const struct audio_stream *stream, const char *keys)
{
  return strdup ("");
}

static int
in_set_gain (struct audio_stream_in *stream, float gain)
{
  LOGFUNC ("%s(%p, %f)", __FUNCTION__, stream, gain);
  return 0;
}

static void
get_capture_delay (struct aml_stream_in *in,
	 size_t frames, struct echo_reference_buffer *buffer)
{
  /* read frames available in kernel driver buffer */
  size_t kernel_frames;
  struct timespec tstamp;
  long buf_delay;
  long rsmp_delay;
  long kernel_delay;
  long delay_ns;
  int rsmp_mul = in->config.rate / VX_NB_SAMPLING_RATE;
  if (pcm_get_htimestamp (in->pcm, &kernel_frames, &tstamp) < 0)
	{
	  buffer->time_stamp.tv_sec = 0;
	  buffer->time_stamp.tv_nsec = 0;
	  buffer->delay_ns = 0;
	  ALOGW ("read get_capture_delay(): pcm_htimestamp error");
	  return;
	}

  /* read frames available in audio HAL input buffer
   * add number of frames being read as we want the capture time of first sample
   * in current buffer */
  buf_delay =
	(long) (((int64_t) (in->frames_in + in->proc_frames_in * rsmp_mul) *
	  1000000000) / in->config.rate);
  /* add delay introduced by resampler */
  rsmp_delay = 0;
  if (in->resampler)
	{
	  rsmp_delay = in->resampler->delay_ns (in->resampler);
	}

  kernel_delay =
	(long) (((int64_t) kernel_frames * 1000000000) / in->config.rate);

  delay_ns = kernel_delay + buf_delay + rsmp_delay;

  buffer->time_stamp = tstamp;
  buffer->delay_ns = delay_ns;
  ALOGV ("get_capture_delay time_stamp = [%ld].[%ld], delay_ns: [%d],"
	 " kernel_delay:[%ld], buf_delay:[%ld], rsmp_delay:[%ld], kernel_frames:[%d], "
	 "in->frames_in:[%d], in->proc_frames_in:[%d], frames:[%d]",
	 buffer->time_stamp.tv_sec, buffer->time_stamp.tv_nsec,
	 buffer->delay_ns, kernel_delay, buf_delay, rsmp_delay, kernel_frames,
	 in->frames_in, in->proc_frames_in, frames);

}

static int32_t
update_echo_reference (struct aml_stream_in *in, size_t frames)
{
  struct echo_reference_buffer b;
  b.delay_ns = 0;

  ALOGV ("update_echo_reference, frames = [%d], in->ref_frames_in = [%d],  "
	 "b.frame_count = [%d]",
	 frames, in->ref_frames_in, frames - in->ref_frames_in);
  if (in->ref_frames_in < frames)
	{
	  if (in->ref_buf_size < frames)
	{
	  in->ref_buf_size = frames;
	  in->ref_buf = (int16_t *) realloc (in->ref_buf,
		  in->ref_buf_size *
		  in->config.channels *
		  sizeof (int16_t));
	}

	  b.frame_count = frames - in->ref_frames_in;
	  b.raw =
	(void *) (in->ref_buf + in->ref_frames_in * in->config.channels);

	  get_capture_delay (in, frames, &b);
	  LOGFUNC ("update_echo_reference  return ::b.delay_ns=%d", b.delay_ns);

	  if (in->echo_reference->read (in->echo_reference, &b) == 0)
	{
	  in->ref_frames_in += b.frame_count;
	  ALOGV ("update_echo_reference: in->ref_frames_in:[%d], "
		 "in->ref_buf_size:[%d], frames:[%d], b.frame_count:[%d]",
		 in->ref_frames_in, in->ref_buf_size, frames, b.frame_count);
	}
	}
  else
	ALOGW ("update_echo_reference: NOT enough frames to read ref buffer");
  return b.delay_ns;
}

static int
set_preprocessor_param (effect_handle_t handle, effect_param_t * param)
{
  uint32_t size = sizeof (int);
  uint32_t psize = ((param->psize - 1) / sizeof (int) + 1) * sizeof (int) +
	param->vsize;

  int status = (*handle)->command (handle,
	   EFFECT_CMD_SET_PARAM,
	   sizeof (effect_param_t) + psize,
	   param,
	   &size,
	   &param->status);
  if (status == 0)
	status = param->status;

  return status;
}

static int
set_preprocessor_echo_delay (effect_handle_t handle, int32_t delay_us)
{
  uint32_t buf[sizeof (effect_param_t) / sizeof (uint32_t) + 2];
  effect_param_t *param = (effect_param_t *) buf;
  LOGFUNC ("%s(%p, %d)", __FUNCTION__, handle, delay_us);

  param->psize = sizeof (uint32_t);
  param->vsize = sizeof (uint32_t);
  *(uint32_t *) param->data = AEC_PARAM_ECHO_DELAY;
  *((int32_t *) param->data + 1) = delay_us;

  return set_preprocessor_param (handle, param);
}

static void
push_echo_reference (struct aml_stream_in *in, size_t frames)
{
  /* read frames from echo reference buffer and update echo delay
   * in->ref_frames_in is updated with frames available in in->ref_buf */
  int32_t delay_us = update_echo_reference (in, frames) / 1000;
  int i;
  audio_buffer_t buf;

  if (in->ref_frames_in < frames)
	frames = in->ref_frames_in;

  buf.frameCount = frames;
  buf.raw = in->ref_buf;

  for (i = 0; i < in->num_preprocessors; i++)
	{
	  if ((*in->preprocessors[i])->process_reverse == NULL)
	continue;

	  (*in->preprocessors[i])->process_reverse (in->preprocessors[i],
			&buf, NULL);
	  set_preprocessor_echo_delay (in->preprocessors[i], delay_us);
	}

  in->ref_frames_in -= buf.frameCount;
  if (in->ref_frames_in)
	{
	  memcpy (in->ref_buf,
	   in->ref_buf + buf.frameCount * in->config.channels,
	   in->ref_frames_in * in->config.channels * sizeof (int16_t));
	}
}

static int
get_next_buffer (struct resampler_buffer_provider *buffer_provider,
		 struct resampler_buffer *buffer)
{
  struct aml_stream_in *in;

  if (buffer_provider == NULL || buffer == NULL)
	return -EINVAL;

  in = (struct aml_stream_in *) ((char *) buffer_provider -
	 offsetof (struct aml_stream_in,
		buf_provider));

  if (in->pcm == NULL)
	{
	  buffer->raw = NULL;
	  buffer->frame_count = 0;
	  in->read_status = -ENODEV;
	  return -ENODEV;
	}

  if (in->frames_in == 0)
	{
	  in->read_status = pcm_read (in->pcm,
	  (void *) in->buffer,
	  in->config.period_size *
	  audio_stream_in_frame_size (&in->stream));
	  if (in->read_status != 0)
	{
	  ALOGE ("get_next_buffer() pcm_read error %d", in->read_status);
	  buffer->raw = NULL;
	  buffer->frame_count = 0;
	  return in->read_status;
	}
	  in->frames_in = in->config.period_size;
	}

  buffer->frame_count = (buffer->frame_count > in->frames_in) ?
	in->frames_in : buffer->frame_count;
  buffer->i16 = in->buffer + (in->config.period_size - in->frames_in) *
	in->config.channels;

  return in->read_status;

}

static void
release_buffer (struct resampler_buffer_provider *buffer_provider,
		struct resampler_buffer *buffer)
{
  struct aml_stream_in *in;
  if (buffer_provider == NULL || buffer == NULL)
	return;

  in = (struct aml_stream_in *) ((char *) buffer_provider -
	 offsetof (struct aml_stream_in,
		buf_provider));

  in->frames_in -= buffer->frame_count;
}

/* read_frames() reads frames from kernel driver, down samples to capture rate
 * if necessary and output the number of frames requested to the buffer specified */
static ssize_t
read_frames (struct aml_stream_in *in, void *buffer, ssize_t frames)
{
  ssize_t frames_wr = 0;
  while (frames_wr < frames)
	{
	  size_t frames_rd = frames - frames_wr;
	  if (in->resampler != NULL)
	{
	  in->resampler->resample_from_provider (in->resampler,
			 (int16_t *) ((char *) buffer
			 +
			 frames_wr *
			 audio_stream_in_frame_size
			 (&in->stream)),
			 &frames_rd);
	}
	  else
	{
	  struct resampler_buffer buf = {
	  {raw:NULL,},
	  frame_count:frames_rd,
	  };
	  get_next_buffer (&in->buf_provider, &buf);
	  if (buf.raw != NULL)
	 {
	   memcpy ((char *) buffer +
		frames_wr * audio_stream_in_frame_size (&in->stream),
		buf.raw,
		buf.frame_count *
		audio_stream_in_frame_size (&in->stream));
	   frames_rd = buf.frame_count;
	 }
	  release_buffer (&in->buf_provider, &buf);
	}
	  /* in->read_status is updated by getNextBuffer() also called by
	   * in->resampler->resample_from_provider() */
	  if (in->read_status != 0)
	return in->read_status;

	  frames_wr += frames_rd;
	}
  return frames_wr;
}

/* process_frames() reads frames from kernel driver (via read_frames()),
 * calls the active audio pre processings and output the number of frames requested
 * to the buffer specified */
static ssize_t
process_frames (struct aml_stream_in *in, void *buffer, ssize_t frames)
{
  ssize_t frames_wr = 0;
  ssize_t proc_frames;
  audio_buffer_t in_buf;
  audio_buffer_t proc_buf;
  audio_buffer_t out_buf;
  int i, resp_mul;
  resp_mul = in->requested_rate / VX_NB_SAMPLING_RATE;

  proc_frames = frames / resp_mul;

  while (frames_wr < proc_frames)
	{
	  /* first reload enough frames at the end of process input buffer */
	  if (in->proc_frames_in < (size_t) proc_frames)
	{
	  ssize_t frames_rd;

	  if (in->proc_buf_size < (size_t) proc_frames)
	 {
	   in->proc_buf_size = (size_t) proc_frames;
	   in->proc_buf = (int16_t *) realloc (in->proc_buf,
		in->proc_buf_size *
		in->config.channels *
		sizeof (int16_t));
	   ALOGV
		("process_frames(): in->proc_buf %p size extended to %d frames",
		 in->proc_buf, in->proc_buf_size);
	 }
	  frames_rd = read_frames (in,
	   in->proc_buf +
	   in->proc_frames_in * in->config.channels,
	   proc_frames - in->proc_frames_in);
	  if (frames_rd < 0)
	 {
	   frames_wr = frames_rd;
	   break;
	 }
	  in->proc_frames_in += frames_rd;
	}

	  if (in->echo_reference != NULL)
	push_echo_reference (in, in->proc_frames_in);

	  /* in_buf.frameCount and out_buf.frameCount indicate respectively
	   * the maximum number of frames to be consumed and produced by process() */
	  in_buf.frameCount = in->proc_frames_in;
	  in_buf.s16 = in->proc_buf;
	  proc_buf.frameCount = proc_frames - frames_wr;
	  proc_buf.s16 =
	(int16_t *) in->up_resampler_buffer + frames_wr * in->config.channels;
	  out_buf.frameCount = (proc_frames - frames_wr) * resp_mul;
	  out_buf.s16 =
	(int16_t *) buffer + resp_mul * frames_wr * in->config.channels;
	  ALOGV
	("first**in_buf.frameCount=%d***\n**proc_buf.frameCount=%d\n**out_buf.frameCount=%d***",
	 in_buf.frameCount, proc_buf.frameCount, out_buf.frameCount);

	  for (i = 0; i < in->num_preprocessors; i++)
	(*in->preprocessors[i])->process (in->preprocessors[i], &in_buf, &proc_buf);	//&out_buf);
	  if (in->up_resampler != NULL)
	{
	  in->up_resampler->resample_from_input (in->up_resampler,
			 proc_buf.s16,
			 &(proc_buf.frameCount),
			 out_buf.s16,
			 &(out_buf.frameCount));
	}
	  ALOGV
	("after**in_buf.frameCount=%d***\n**proc_buf.frameCount=%d\n**out_buf.frameCount=%d***",
	 in_buf.frameCount, proc_buf.frameCount, out_buf.frameCount);

	  /* process() has updated the number of frames consumed and produced in
	   * in_buf.frameCount and out_buf.frameCount respectively
	   * move remaining frames to the beginning of in->proc_buf */
	  in->proc_frames_in -= in_buf.frameCount;
	  if (in->proc_frames_in)
	{
	  memcpy (in->proc_buf,
	in->proc_buf + in_buf.frameCount * in->config.channels,
	in->proc_frames_in * in->config.channels *
	sizeof (int16_t));
	}

	  /* if not enough frames were passed to process(), read more and retry. */
	  if (out_buf.frameCount == 0)
	continue;

	  frames_wr += out_buf.frameCount / resp_mul;	//out_buf.frameCount * VX_NB_SAMPLING_RATE/in->requested_rate;
	  ALOGV ("***last*****frames_wr=%ld***********", frames_wr);
	}
  return frames_wr;
}

static ssize_t
in_read (struct audio_stream_in *stream, void *buffer, size_t bytes)
{
  int ret = 0;
  int i = 0;
  struct aml_stream_in *in = (struct aml_stream_in *) stream;
  struct aml_audio_device *adev = in->dev;
  size_t frames_rq = bytes / audio_stream_in_frame_size (stream);
  if (in->voip_mode)
	{
	  int sleepTime;

	  sleepTime = 1000 * bytes / (in->config.channels * in->config.rate * 2);
	  usleep (sleepTime * 1000);
	  return bytes;
	}
  /* acquiring hw device mutex systematically is useful if a low priority thread is waiting
   * on the input stream mutex - e.g. executing select_mode() while holding the hw device
   * mutex
   */
  pthread_mutex_lock (&adev->lock);
  pthread_mutex_lock (&in->lock);
  if (in->standby)
	{
	  ret = start_input_stream (in);
	  if (ret == 0)
	in->standby = 0;
	}
  pthread_mutex_unlock (&adev->lock);

  if (ret < 0)
	goto exit;
  if (in->need_echo_reference && in->echo_reference == NULL)
	{
	 in->echo_reference = get_echo_reference (adev,
			AUDIO_FORMAT_PCM_16_BIT,
			in->config.channels,
			VX_NB_SAMPLING_RATE);
	  //in->requested_rate);
	  LOGFUNC ("%s(after get_echo_ref.... now in->echo_reference = %p)",
		__FUNCTION__, in->echo_reference);
#if 1
	  if (in->echo_reference != NULL)
	{
	  in->buf_provider.get_next_buffer = get_next_buffer;
	  in->buf_provider.release_buffer = release_buffer;
	  ret = create_resampler (in->config.rate,
	  VX_NB_SAMPLING_RATE,
	  in->config.channels,
	  RESAMPLER_QUALITY_DEFAULT,
	  &in->buf_provider, &in->resampler);

	  if (ret != 0)
	 {
	   ret = -EINVAL;
	   return ret;
	 }
	  ret = create_resampler (VX_NB_SAMPLING_RATE, in->config.rate, in->config.channels, RESAMPLER_QUALITY_DEFAULT, NULL,	//&in->up_buf_provider,
	  &in->up_resampler);

	  if (ret != 0)
	 {
	   ret = -EINVAL;
	   return ret;
	 }
	  in->up_resampler_buffer = malloc (RESAMPLER_BUFFER_SIZE);	/* todo: allow for reallocing */
	  if (in->up_resampler_buffer == NULL)
	 return ENOMEM;
	}
#endif
	}

/////////////////////////////////////////////////////////////////////
  if (in->num_preprocessors != 0 && in->echo_reference != NULL)
	//if (in->num_preprocessors != 0)
	ret = process_frames (in, buffer, frames_rq);
  else if (in->resampler != NULL)
	ret = read_frames (in, buffer, frames_rq);
  else
	ret = pcm_read (in->pcm, buffer, bytes);

  float volume = computeVolume (stream);
  int16_t *hdmi_buf = (int16_t *) buffer;
  for (i = 0; i < bytes / 2; i++)
	{
	  hdmi_buf[i] = (int16_t) (((float) hdmi_buf[i]) * volume);
	}

  if (ret > 0)
	ret = 0;

  if (ret == 0 && adev->mic_mute)
	{
	  LOGFUNC ("%s(adev->mic_mute = %d)", __FUNCTION__, adev->mic_mute);
	  memset (buffer, 0, bytes);
	}
exit:
  if (ret < 0)
	usleep (bytes * 1000000 / audio_stream_in_frame_size (stream) /
	 in_get_sample_rate (&stream->common));

  pthread_mutex_unlock (&in->lock);
  return bytes;

}

static uint32_t
in_get_input_frames_lost (struct audio_stream_in *stream)
{
  return 0;
}

static int
in_add_audio_effect (const struct audio_stream *stream,
	   effect_handle_t effect)
{
  struct aml_stream_in *in = (struct aml_stream_in *) stream;
  int status;
  effect_descriptor_t desc;
  LOGFUNC ("*********%s", __FUNCTION__);

  pthread_mutex_lock (&in->dev->lock);
  pthread_mutex_lock (&in->lock);
  if (in->num_preprocessors >= MAX_PREPROCESSORS)
	{
	  status = -ENOSYS;
	  goto exit;
	}

  status = (*effect)->get_descriptor (effect, &desc);
  if (status != 0)
	goto exit;

  in->preprocessors[in->num_preprocessors++] = effect;

  if (memcmp (&desc.type, FX_IID_AEC, sizeof (effect_uuid_t)) == 0)
	{
	  in->need_echo_reference = true;
	  do_input_standby (in);
	}

exit:

  pthread_mutex_unlock (&in->lock);
  pthread_mutex_unlock (&in->dev->lock);
  return status;
}

static int
in_remove_audio_effect (const struct audio_stream *stream,
			effect_handle_t effect)
{
  struct aml_stream_in *in = (struct aml_stream_in *) stream;
  int i;
  int status = -EINVAL;
  bool found = false;
  effect_descriptor_t desc;

  pthread_mutex_lock (&in->dev->lock);
  pthread_mutex_lock (&in->lock);
  if (in->num_preprocessors <= 0)
	{
	  status = -ENOSYS;
	  goto exit;
	}

  for (i = 0; i < in->num_preprocessors; i++)
	{
	  if (found)
	{
	  in->preprocessors[i - 1] = in->preprocessors[i];
	  continue;
	}
	  if (in->preprocessors[i] == effect)
	{
	  in->preprocessors[i] = NULL;
	  status = 0;
	  found = true;
	}
	}

  if (status != 0)
	goto exit;

  in->num_preprocessors--;

  status = (*effect)->get_descriptor (effect, &desc);
  if (status != 0)
	goto exit;
  if (memcmp (&desc.type, FX_IID_AEC, sizeof (effect_uuid_t)) == 0)
	{
	  in->need_echo_reference = false;
	  do_input_standby (in);
	}

exit:

  pthread_mutex_unlock (&in->lock);
  pthread_mutex_unlock (&in->dev->lock);
  return status;
}

static int
adev_open_output_stream (struct audio_hw_device *dev,
	audio_io_handle_t handle,
	audio_devices_t devices,
	audio_output_flags_t flags,
	struct audio_config *config,
	struct audio_stream_out **stream_out)
{
  struct aml_audio_device *ladev = (struct aml_audio_device *) dev;
  struct aml_stream_out *out;
  int channel_count = popcount (config->channel_mask);
  int ret;
  int dc;			//digital_codec

  LOGFUNC ("%s(devices=0x%04x,format=%d, chnum=0x%04x, SR=%d,io handle %d )",
	__FUNCTION__, devices, config->format, channel_count,
	config->sample_rate, handle);

  out = (struct aml_stream_out *) calloc (1, sizeof (struct aml_stream_out));
  if (!out)
	return -ENOMEM;

  out->stream.common.get_sample_rate = out_get_sample_rate;
  out->stream.common.set_sample_rate = out_set_sample_rate;
  out->stream.common.get_buffer_size = out_get_buffer_size;
  out->stream.common.get_channels = out_get_channels;
  out->stream.common.get_format = out_get_format;
  out->stream.common.set_format = out_set_format;
  out->stream.common.standby = out_standby;
  out->stream.common.dump = out_dump;
  out->stream.common.set_parameters = out_set_parameters;
  out->stream.common.get_parameters = out_get_parameters;
  out->stream.common.add_audio_effect = out_add_audio_effect;
  out->stream.common.remove_audio_effect = out_remove_audio_effect;
  //out->stream.common.set_voip_mode = out_set_voip_mode;
  out->stream.get_latency = out_get_latency;
  out->stream.set_volume = out_set_volume;
  out->stream.write = out_write;
  out->stream.get_render_position = out_get_render_position;
  out->stream.get_next_write_timestamp = out_get_next_write_timestamp;
  out->config = pcm_config_out;
  dc = get_codec_type ("/sys/class/audiodsp/digital_codec");
  if (dc == 4 || dc == 5)
	out->config.period_size = pcm_config_out.period_size * 2;
  else if (dc == 7)
	out->config.period_size = pcm_config_out.period_size * 4 * 2;
  if (channel_count > 2)
	{
	  ALOGI ("[adev_open_output_stream]: out/%p channel/%d\n", out,
	  channel_count);
	  out->multich = channel_count;
	  out->config.channels = channel_count;
	}
  out->dev = ladev;
  out->standby = 1;

  /* FIXME: when we support multiple output devices, we will want to
   * do the following:
   * adev->devices &= ~AUDIO_DEVICE_OUT_ALL;
   * adev->devices |= out->device;
   * select_output_device(adev);
   * This is because out_set_parameters() with a route is not
   * guaranteed to be called after an output stream is opened.
   */

  //config->format = out_get_format(&out->stream.common);
  //config->channel_mask = out_get_channels(&out->stream.common);
  //config->sample_rate = out_get_sample_rate(&out->stream.common);
  LOGFUNC ("%s(devices=0x%04x,format=0x%x, chmask=0x%04x, SR=%d)",
	__FUNCTION__, devices, config->format, config->channel_mask,
	config->sample_rate);



  *stream_out = &out->stream;
  return 0;

err_open:
  free (out);
  *stream_out = NULL;
  return ret;
}

static void
adev_close_output_stream (struct audio_hw_device *dev,
	 struct audio_stream_out *stream)
{
  struct aml_stream_out *out = (struct aml_stream_out *) stream;
  LOGFUNC ("%s(%p, %p)", __FUNCTION__, dev, stream);
  out_standby (&stream->common);
  if (out->buffer)
	free (out->buffer);
  if (out->resampler)
	release_resampler (out->resampler);

  free (stream);
}

static int
adev_set_parameters (struct audio_hw_device *dev, const char *kvpairs)
{
  LOGFUNC ("%s(%p, %s)", __FUNCTION__, dev, kvpairs);

  struct aml_audio_device *adev = (struct aml_audio_device *) dev;
  struct str_parms *parms;
  char *str;
  char value[32];
  int ret;

  parms = str_parms_create_str (kvpairs);
  ret = str_parms_get_str (parms, "screen_state", value, sizeof (value));
  if (ret >= 0)
	{
	  if (strcmp (value, AUDIO_PARAMETER_VALUE_ON) == 0)
	adev->low_power = false;
	  else
	adev->low_power = true;
	}

  str_parms_destroy (parms);
  return ret;
}

static char *
adev_get_parameters (const struct audio_hw_device *dev, const char *keys)
{
  LOGFUNC ("%s(%p, %s)", __FUNCTION__, dev, keys);
  return strdup ("");
}

static int
adev_init_check (const struct audio_hw_device *dev)
{
  LOGFUNC ("%s(%p)", __FUNCTION__, dev);
  return 0;
}

static int
adev_set_voice_volume (struct audio_hw_device *dev, float volume)
{
  struct aml_audio_device *adev = (struct aml_audio_device *) dev;
  LOGFUNC ("%s(%p, %f)", __FUNCTION__, dev, volume);
  return 0;
}

static int
adev_set_master_volume (struct audio_hw_device *dev, float volume)
{
  LOGFUNC ("%s(%p, %f)", __FUNCTION__, dev, volume);
  return -ENOSYS;
}

static int
adev_get_master_volume (struct audio_hw_device *dev, float *volume)
{
  return -ENOSYS;
}

static int
adev_set_master_mute (struct audio_hw_device *dev, bool muted)
{
  return -ENOSYS;
}

static int
adev_get_master_mute (struct audio_hw_device *dev, bool * muted)
{
  return -ENOSYS;
}

static int
adev_set_mode (struct audio_hw_device *dev, audio_mode_t mode)
{
  struct aml_audio_device *adev = (struct aml_audio_device *) dev;
  LOGFUNC ("%s(%p, %d)", __FUNCTION__, dev, mode);

  pthread_mutex_lock (&adev->lock);
  if (adev->mode != mode)
	{
	  adev->mode = mode;
	  select_mode (adev);
	}
  pthread_mutex_unlock (&adev->lock);

  return 0;
}

static int
adev_set_mic_mute (struct audio_hw_device *dev, bool state)
{
  struct aml_audio_device *adev = (struct aml_audio_device *) dev;

  LOGFUNC ("%s(%p, %d)", __FUNCTION__, dev, state);
  adev->mic_mute = state;

  return 0;
}

static int
adev_get_mic_mute (const struct audio_hw_device *dev, bool * state)
{
  struct aml_audio_device *adev = (struct aml_audio_device *) dev;

  LOGFUNC ("%s(%p, %p)", __FUNCTION__, dev, state);
  *state = adev->mic_mute;

  return 0;

}

static size_t
adev_get_input_buffer_size (const struct audio_hw_device *dev,
	   const struct audio_config *config)
{
  size_t size;
  int channel_count = popcount (config->channel_mask);

  LOGFUNC ("%s(%p, %d, %d, %d)", __FUNCTION__, dev, config->sample_rate,
	config->format, channel_count);
  if (check_input_parameters
	  (config->sample_rate, config->format, channel_count) != 0)
	return 0;

  return get_input_buffer_size (config->sample_rate,
	config->format, channel_count);

}

static int
adev_open_input_stream (struct audio_hw_device *dev,
			audio_io_handle_t handle,
			audio_devices_t devices,
			struct audio_config *config,
			struct audio_stream_in **stream_in)
{
  struct aml_audio_device *ladev = (struct aml_audio_device *) dev;
  struct aml_stream_in *in;
  int ret;
  int channel_count = popcount (config->channel_mask);
  LOGFUNC ("**********%s(%#x, %d, 0x%04x, %d)", __FUNCTION__,
	devices, config->format, config->channel_mask,
	config->sample_rate);
  if (check_input_parameters
	  (config->sample_rate, config->format, channel_count) != 0)
	return -EINVAL;

  in = (struct aml_stream_in *) calloc (1, sizeof (struct aml_stream_in));
  if (!in)
	return -ENOMEM;

  in->stream.common.get_sample_rate = in_get_sample_rate;
  in->stream.common.set_sample_rate = in_set_sample_rate;
  in->stream.common.get_buffer_size = in_get_buffer_size;
  in->stream.common.get_channels = in_get_channels;
  in->stream.common.get_format = in_get_format;
  in->stream.common.set_format = in_set_format;
  in->stream.common.standby = in_standby;
  in->stream.common.dump = in_dump;
  in->stream.common.set_parameters = in_set_parameters;
  in->stream.common.get_parameters = in_get_parameters;
  in->stream.common.add_audio_effect = in_add_audio_effect;
  in->stream.common.remove_audio_effect = in_remove_audio_effect;
  //in->stream.common.set_voip_mode = in_set_voip_mode;
  in->stream.set_gain = in_set_gain;
  in->stream.read = in_read;
  in->stream.get_input_frames_lost = in_get_input_frames_lost;

  in->requested_rate = config->sample_rate;

  memcpy (&in->config, &pcm_config_in, sizeof (pcm_config_in));
  ret = check_input_stream (in);
  if (ret < 0)
	{
	  ALOGE ("fail to open input stream, change channel count from %d to %d",
	  in->config.channels, channel_count);
	  in->config.channels = channel_count;
	}

  if (in->config.channels == 1)
	{
	  config->channel_mask = AUDIO_CHANNEL_IN_MONO;
	}
  else if (in->config.channels == 2)
	{
	  config->channel_mask = AUDIO_CHANNEL_IN_STEREO;
	}
  else
	{
	  ALOGE ("Bad value of channel count : %d", in->config.channels);
	}
  in->buffer = malloc (in->config.period_size *
		 audio_stream_in_frame_size (&in->stream));
  if (!in->buffer)
	{
	  ret = -ENOMEM;
	  goto err_open;
	}

  if (in->requested_rate != in->config.rate)
	{
	  LOGFUNC ("%s(in->requested_rate=%d, in->config.rate=%d)",
		__FUNCTION__, in->requested_rate, in->config.rate);
	  in->buf_provider.get_next_buffer = get_next_buffer;
	  in->buf_provider.release_buffer = release_buffer;
	  ret = create_resampler (in->config.rate,
		 in->requested_rate,
		 in->config.channels,
		 RESAMPLER_QUALITY_DEFAULT,
		 &in->buf_provider, &in->resampler);

	  if (ret != 0)
	{
	  ret = -EINVAL;
	  goto err_open;
	}
	}

  in->dev = ladev;
  in->standby = 1;
  in->device = devices & ~AUDIO_DEVICE_BIT_IN;

  // init hdmi in volume parameters
  in->volume_index = 0;
  in->last_volume = 0.0f;
  in->indexMIn = 0;
  in->indexMax = 15;

  *stream_in = &in->stream;
  return 0;

err_open:
  if (in->resampler)
	release_resampler (in->resampler);

  free (in);
  *stream_in = NULL;
  return ret;
}

static void
adev_close_input_stream (struct audio_hw_device *dev,
	struct audio_stream_in *stream)
{
  struct aml_stream_in *in = (struct aml_stream_in *) stream;

  LOGFUNC ("%s(%p, %p)", __FUNCTION__, dev, stream);
  in_standby (&stream->common);

  if (in->resampler)
	{
	  free (in->buffer);
	  release_resampler (in->resampler);
	}
  if (in->up_resampler)
	{
	  free (in->up_resampler_buffer);
	  release_resampler (in->up_resampler);
	}
  if (in->proc_buf)
	free (in->proc_buf);
  if (in->ref_buf)
	free (in->ref_buf);

  free (stream);

  return;
}

static int
adev_dump (const audio_hw_device_t * device, int fd)
{
  LOGFUNC ("%s(%p, %d)", __FUNCTION__, device, fd);
  return 0;
}

static int
adev_close (hw_device_t * device)
{
  struct aml_audio_device *adev = (struct aml_audio_device *) device;

  LOGFUNC ("%s(%p)", __FUNCTION__, device);
  /* RIL */
  //ril_close(&adev->ril);
  //audio_route_free(adev->ar);
  free (device);
  return 0;
}


static int
adev_open (const hw_module_t * module, const char *name,
	hw_device_t ** device)
{
  struct aml_audio_device *adev;
  int ret;
  LOGFUNC ("%s(%p, %s, %p)", __FUNCTION__, module, name, device);
  if (strcmp (name, AUDIO_HARDWARE_INTERFACE) != 0)
	return -EINVAL;

  adev = calloc (1, sizeof (struct aml_audio_device));
  if (!adev)
	return -ENOMEM;

  adev->hw_device.common.tag = HARDWARE_DEVICE_TAG;
  adev->hw_device.common.version = AUDIO_DEVICE_API_VERSION_2_0;
  adev->hw_device.common.module = (struct hw_module_t *) module;
  adev->hw_device.common.close = adev_close;

  //adev->hw_device.get_supported_devices = adev_get_supported_devices;
  adev->hw_device.init_check = adev_init_check;
  adev->hw_device.set_voice_volume = adev_set_voice_volume;
  adev->hw_device.set_master_volume = adev_set_master_volume;
  adev->hw_device.get_master_volume = adev_get_master_volume;
  adev->hw_device.set_master_mute = adev_set_master_mute;
  adev->hw_device.get_master_mute = adev_get_master_mute;
  adev->hw_device.set_mode = adev_set_mode;
  adev->hw_device.set_mic_mute = adev_set_mic_mute;
  adev->hw_device.get_mic_mute = adev_get_mic_mute;
  adev->hw_device.set_parameters = adev_set_parameters;
  adev->hw_device.get_parameters = adev_get_parameters;
  adev->hw_device.get_input_buffer_size = adev_get_input_buffer_size;
  adev->hw_device.open_output_stream = adev_open_output_stream;
  adev->hw_device.close_output_stream = adev_close_output_stream;
  adev->hw_device.open_input_stream = adev_open_input_stream;
  adev->hw_device.close_input_stream = adev_close_input_stream;
  adev->hw_device.dump = adev_dump;

  /* Set the default route before the PCM stream is opened */
  adev->mode = AUDIO_MODE_NORMAL;
  adev->out_device = AUDIO_DEVICE_OUT_AUX_DIGITAL;
  adev->in_device = AUDIO_DEVICE_IN_BUILTIN_MIC & ~AUDIO_DEVICE_BIT_IN;

  select_output_device (adev);

  *device = &adev->hw_device.common;
  return 0;
}

static struct hw_module_methods_t hal_module_methods = {
  .open = adev_open,
};

struct audio_module HAL_MODULE_INFO_SYM = {
  .common = {
	  .tag = HARDWARE_MODULE_TAG,
	  .module_api_version = AUDIO_MODULE_API_VERSION_0_1,
	  .hal_api_version = HARDWARE_HAL_API_VERSION,
	  .id = AUDIO_HARDWARE_MODULE_ID,
	  .name = "aml HDMI audio HW HAL",
	  .author = "amlogic, Corp.",
	  .methods = &hal_module_methods,
	  },
};
