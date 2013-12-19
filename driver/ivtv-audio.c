/*
    Audio-related ivtv functions.
    Copyright (C) 2003-2004  Kevin Thayer <nufan_wfk at yahoo.com>

    This program is free software; you can redistribute it and/or modify
    it under the terms of the GNU General Public License as published by
    the Free Software Foundation; either version 2 of the License, or
    (at your option) any later version.

    This program is distributed in the hope that it will be useful,
    but WITHOUT ANY WARRANTY; without even the implied warranty of
    MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
    GNU General Public License for more details.

    You should have received a copy of the GNU General Public License
    along with this program; if not, write to the Free Software
    Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA  02111-1307  USA
 */

#include "ivtv-driver.h"
#include "ivtv-mailbox.h"
#include "ivtv-i2c.h"
#include "ivtv-cards.h"
#include "ivtv-audio.h"
#include "v4l2-common.h"
#include "audiochip.h"

static int ivtv_set_audio_for_cx25840(struct ivtv *itv, u32 audio_input)
{
	int cx_input;

	switch (audio_input) {
	case 0: cx_input = AUDIO_TUNER; break;
	case 1: cx_input = AUDIO_EXTERN_1; break;
	case 2: cx_input = AUDIO_EXTERN_2; break;
	case 3: cx_input = AUDIO_INTERN; break;
	case 4: cx_input = AUDIO_RADIO; break;
	default:
		IVTV_ERR("Invalid audio input, shouldn't happen!\n");
		return -EINVAL;
	}

	IVTV_DEBUG_INFO("Setting audio to input %d\n", audio_input);
	ivtv_cx25840(itv, AUDC_SET_INPUT, &cx_input);
	return 0;
}


/* switching audio input on PVR-150/500, only tested on 150 thus far */
static int ivtv_set_audio_for_pvr150(struct ivtv *itv, u32 audio_input)
{
	int wm_input;

	switch (audio_input) {
	case 0: wm_input = AUDIO_TUNER; break;
	case 1: wm_input = AUDIO_EXTERN_1; break;
	case 2: wm_input = AUDIO_EXTERN_2; break;
	case 3: wm_input = AUDIO_INTERN; break;
	case 4: wm_input = AUDIO_RADIO; break;
	default: return -EINVAL;
	}

	ivtv_wm8775(itv, AUDC_SET_INPUT, &wm_input);
	return 0;
}

/* Selects the audio input and output according to the current
   settings. Call this function after these settings change. */
int ivtv_audio_set_io(struct ivtv *itv)
{
	u32 audio_input;

	/* Determine which input to use */
	if (test_bit(IVTV_F_I_RADIO_USER, &itv->i_flags)) {
		audio_input = itv->audio_input_radio;
	} else {
		audio_input = itv->audio_input_tv;
	}

	switch (itv->card->audio_selector) {
        case USE_PVR150:
                ivtv_set_audio_for_pvr150(itv, audio_input);
                /* fall through */
	case USE_CX25840:
		return ivtv_set_audio_for_cx25840(itv, audio_input);
	}

	IVTV_ERR("Invalid card type [ivtv_set_audio]!\n");
	return -EINVAL;
}

void ivtv_audio_set_volume(struct ivtv *itv, int volume)
{
	struct video_audio va;
	struct v4l2_control ctrl;
	memset(&va, 0, sizeof(struct video_audio));

	switch (itv->card->audio_selector) {
        case USE_PVR150:
	case USE_CX25840:
		ctrl.id = V4L2_CID_AUDIO_VOLUME;
		ctrl.value = volume;
		ivtv_cx25840(itv, VIDIOC_S_CTRL, &ctrl);
		break;
	}
}

int ivtv_audio_get_volume(struct ivtv *itv)
{
	struct video_audio va;
	struct v4l2_control ctrl;
	memset(&va, 0, sizeof(struct video_audio));

	switch (itv->card->audio_selector) {
        case USE_PVR150:
	case USE_CX25840:
		ctrl.id = V4L2_CID_AUDIO_VOLUME;
		ivtv_cx25840(itv, VIDIOC_G_CTRL, &ctrl);
		return ctrl.value;
	}
	return va.volume;
}

void ivtv_audio_set_mute(struct ivtv *itv, int mute)
{
	struct v4l2_control ctrl;

	switch (itv->card->audio_selector) {
        case USE_PVR150:
	case USE_CX25840:
		ctrl.id = V4L2_CID_AUDIO_MUTE;
		ctrl.value = mute;
		ivtv_cx25840(itv, VIDIOC_S_CTRL, &ctrl);
		break;
	}
}

int ivtv_audio_get_mute(struct ivtv *itv)
{
	struct video_audio va;
	struct v4l2_control ctrl;

	memset(&va, 0, sizeof(struct video_audio));
	switch (itv->card->audio_selector) {
        case USE_PVR150:
	case USE_CX25840:
		ctrl.id = V4L2_CID_AUDIO_MUTE;
		ivtv_cx25840(itv, VIDIOC_G_CTRL, &ctrl);
		return ctrl.value;
	}
	return va.flags & VIDEO_AUDIO_MUTE;
}

void ivtv_audio_set_std(struct ivtv *itv)
{
	switch (itv->card->audio_selector) {
        case USE_PVR150:
	case USE_CX25840:
                // Already handled by VIDIOC_S_STD
		break;
	}
}

void ivtv_audio_set_audmode(struct ivtv *itv, u32 audmode)
{
	struct v4l2_tuner vt;

	switch (itv->card->audio_selector) {
        case USE_PVR150:
	case USE_CX25840:
		vt.index = 0;
		vt.audmode = audmode;
		ivtv_cx25840(itv, VIDIOC_S_TUNER, &vt);
                break;
	}
}

u32 ivtv_audio_get_rxsubchans(struct ivtv *itv, struct v4l2_tuner *vt)
{
	if (itv->card->audio_selector == USE_CX25840 ||
            itv->card->audio_selector == USE_PVR150) {
		struct v4l2_tuner vt;

		vt.index = 0;
		ivtv_cx25840(itv, VIDIOC_G_TUNER, &vt);
		return vt.rxsubchans;
        }
	/* Unknown, so anything goes. */
	return vt->capability;
}

/* This starts the automatic sound detection of the msp34xx that detects
   mono/stereo/multilang. */
void ivtv_audio_freq_changed(struct ivtv *itv)
{
	struct v4l2_frequency vf = { 0, 0 }; /* Value is ignored by msp34xx */

	switch (itv->card->audio_selector) {
        case USE_PVR150:
		ivtv_wm8775(itv, VIDIOC_S_FREQUENCY, &vf);
		/* fall through */
	case USE_CX25840:
		ivtv_cx25840(itv, VIDIOC_S_FREQUENCY, &vf);
                break;
	}
}

int ivtv_audio_set_matrix(struct ivtv *itv, struct ivtv_msp_matrix *matrix)
{
	switch (itv->card->audio_selector) {
        case USE_PVR150:
	case USE_CX25840:
		/* do nothing, msp34xx specific function */
		break;
	}
	return 0;
}

void ivtv_audio_set_audio_clock_freq(struct ivtv *itv, u8 freq)
{
	static enum v4l2_audio_clock_freq freqs[3] = {
		V4L2_AUDCLK_441_KHZ,
		V4L2_AUDCLK_48_KHZ,
		V4L2_AUDCLK_32_KHZ,
	};

	/* Also upgrade the digitizer setting. The audio clock of the
	   digitizer must match the codec sample rate otherwise you get
	   some very strange effects. */
	if (freq > 2)
		return;
	itv->card->video_dec_func(itv, VIDIOC_INT_AUDIO_CLOCK_FREQ, &freqs[freq]);
}

