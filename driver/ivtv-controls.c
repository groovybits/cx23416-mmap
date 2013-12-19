/*
    ioctl control functions
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
#include "ivtv-cards.h"
#include "ivtv-audio.h"
#include "ivtv-i2c.h"
#include "ivtv-controls.h"

static int ivtv_querymenu(struct ivtv *itv, struct v4l2_querymenu *qmenu)
{
	const char * const *menu;
	u32 i = qmenu->index;

	switch (qmenu->id) {
	case V4L2_CID_IVTV_FREQ:{
		static const char * const menu_freq[] = {
			"44.1 kHz",
			"48 kHz",
			"32 kHz",
			NULL
		};
		menu = menu_freq;
		break;
	}
	case V4L2_CID_IVTV_ENC:{
		static const char * const menu_layer[] = {
			"Layer 1",
			"Layer 2",
			"Layer 3 (?)",
			NULL
		};
		menu = menu_layer;
		break;
	}
	case V4L2_CID_IVTV_BITRATE:{
		static const char * const menu_bitrate[] = {
			"[L1/L2] Free fmt",
			"[L1/L2] 32k/32k",
			"[L1/L2] 64k/48k",
			"[L1/L2] 96k/56k",
			"[L1/L2] 128k/64k",
			"[L1/L2] 160k/80k",
			"[L1/L2] 192k/96k",
			"[L1/L2] 224k/112k",
			"[L1/L2] 256k/128k",
			"[L1/L2] 288k/160k",
			"[L1/L2] 320k/192k",
			"[L1/L2] 352k/224k",
			"[L1/L2] 384k/256k",
			"[L1/L2] 416k/320k",
			"[L1/L2] 448k/384k",
			NULL
		};
		menu = menu_bitrate;
		break;
	}
	case V4L2_CID_IVTV_MONO:{
		static const char * const menu_mono[] = {
			"Stereo",
			"JointStereo",
			"Dual",
			"Mono",
			NULL
		};
		menu = menu_mono;
		break;
	}
	case V4L2_CID_IVTV_JOINT:{
		static const char * const menu_joint[] = {
			"Subbands 4-31/bound=4",
			"Subbands 8-31/bound=8",
			"Subbands 12-31/bound=12",
			"Subbands 16-31/bound=16",
			NULL
		};
		menu = menu_joint;
		break;
	}
	case V4L2_CID_IVTV_EMPHASIS:{
		static const char * const menu_emph[] = {
			"None",
			"50/15uS",
			"CCITT J.17",
			NULL
		};
		menu = menu_emph;
		break;
	}
	case V4L2_CID_IVTV_CRC:{
		static const char * const menu_crc[] = {
			"CRC off",
			"CRC on",
			NULL
		};
		menu = menu_crc;
		break;
	}
	case V4L2_CID_IVTV_COPYRIGHT:{
		static const char * const menu_copyright[] = {
			"Copyright off",
			"Copyright on",
			NULL
		};
		menu = menu_copyright;
		break;
	}
	case V4L2_CID_IVTV_GEN:{
		static const char * const menu_gen[] = {
			"Copy",
			"Original",
			NULL
		};
		menu = menu_gen;
		break;
	}
	default:
		IVTV_DEBUG_IOCTL("invalid control %x\n", qmenu->id);
		return -EINVAL;
	}
	while (i-- && *menu) {
		menu++;
	}
	if (*menu == NULL) {
		IVTV_DEBUG_IOCTL("invalid menu index %d (control %x)\n",
                                 qmenu->index, qmenu->id);
		return -EINVAL;
	}
	strncpy(qmenu->name, *menu, sizeof(qmenu->name));
	return 0;
}

static void ivtv_init_queryctrl(struct v4l2_queryctrl *qctrl,
				int type, int min, int max, int step, int def)
{
	qctrl->type = type;
	qctrl->minimum = min;
	qctrl->maximum = max;
	qctrl->step = step;
	qctrl->default_value = def;
	qctrl->flags = 0;
	qctrl->reserved[0] = 0;
	qctrl->reserved[1] = 0;
}

static int ivtv_queryctrl(struct ivtv *itv, struct v4l2_queryctrl *qctrl)
{
	const char *name;

	IVTV_DEBUG_IOCTL("VIDIOC_QUERYCTRL(%d)\n", qctrl->id);
	switch (qctrl->id) {
		/* Audio controls */
	case V4L2_CID_IVTV_FREQ:
		name = "Frequency";
		ivtv_init_queryctrl(qctrl, V4L2_CTRL_TYPE_MENU, 0, 2, 1, 1);
		break;
	case V4L2_CID_IVTV_ENC:
		name = "Encoding";
		ivtv_init_queryctrl(qctrl, V4L2_CTRL_TYPE_MENU, 0, 2, 1, 1);
		break;
	case V4L2_CID_IVTV_BITRATE:
		name = "Audio Bitrate";
		ivtv_init_queryctrl(qctrl, V4L2_CTRL_TYPE_MENU, 0, 14, 1, 14);
		break;
	case V4L2_CID_IVTV_MONO:
		name = "Mono/Stereo";
		ivtv_init_queryctrl(qctrl, V4L2_CTRL_TYPE_MENU, 0, 3, 1, 0);
		break;
	case V4L2_CID_IVTV_JOINT:
		name = "Joint extension";
		ivtv_init_queryctrl(qctrl, V4L2_CTRL_TYPE_MENU, 0, 3, 1, 0);
		break;
	case V4L2_CID_IVTV_EMPHASIS:
		name = "Emphasis";
		ivtv_init_queryctrl(qctrl, V4L2_CTRL_TYPE_MENU, 0, 2, 1, 0);
		break;
	case V4L2_CID_IVTV_CRC:
		name = "Audio CRC";
		ivtv_init_queryctrl(qctrl, V4L2_CTRL_TYPE_MENU, 0, 1, 1, 0);
		break;
	case V4L2_CID_IVTV_COPYRIGHT:
		name = "Copyright";
		ivtv_init_queryctrl(qctrl, V4L2_CTRL_TYPE_MENU, 0, 1, 1, 0);
		break;
	case V4L2_CID_IVTV_GEN:
		name = "Generation";
		ivtv_init_queryctrl(qctrl, V4L2_CTRL_TYPE_MENU, 0, 1, 1, 0);
		break;

		/* Standard V4L2 controls */
	case V4L2_CID_BRIGHTNESS:
		name = "Brightness";
		ivtv_init_queryctrl(qctrl, V4L2_CTRL_TYPE_INTEGER, 0, 255, 0, 128);
		break;
	case V4L2_CID_HUE:
		name = "Hue";
		ivtv_init_queryctrl(qctrl, V4L2_CTRL_TYPE_INTEGER, -128, 127, 0, 0);
		break;
	case V4L2_CID_SATURATION:
		name = "Saturation";
		ivtv_init_queryctrl(qctrl, V4L2_CTRL_TYPE_INTEGER, 0, 127, 0, 64);
		break;
	case V4L2_CID_CONTRAST:
		name = "Contrast";
		ivtv_init_queryctrl(qctrl, V4L2_CTRL_TYPE_INTEGER, 0, 127, 0, 64);
		break;
	case V4L2_CID_AUDIO_VOLUME:
		name = "Volume";
		ivtv_init_queryctrl(qctrl, V4L2_CTRL_TYPE_INTEGER, 0, 65535, 0, 65535);
		break;
	case V4L2_CID_AUDIO_MUTE:
		name = "Mute";
		ivtv_init_queryctrl(qctrl, V4L2_CTRL_TYPE_INTEGER, 0, 1, 0, 1);
		break;
	default:
		IVTV_DEBUG_IOCTL("invalid control %x\n", qctrl->id);
		return -EINVAL;
	}
	strncpy(qctrl->name, name, sizeof(qctrl->name) - 1);
	qctrl->name[sizeof(qctrl->name) - 1] = 0;
	return 0;
}

static int ivtv_s_ctrl(struct ivtv *itv, struct v4l2_control *vctrl)
{
	s32 v = vctrl->value;

	IVTV_DEBUG_IOCTL("VIDIOC_S_CTRL(%d, %x)\n", vctrl->id, v);
	switch (vctrl->id) {
		/* Audio controls */
	case V4L2_CID_IVTV_FREQ:
		if ((v < 0) || (v > 2))
			return -ERANGE;
		itv->codec.audio_bitmask &= ~3;
		itv->codec.audio_bitmask |= v;
		ivtv_audio_set_audio_clock_freq(itv, v);
		break;
	case V4L2_CID_IVTV_ENC:
		if ((v < 0) || (v > 2))
			return -ERANGE;
		itv->codec.audio_bitmask &= ~(3 << 2);
		itv->codec.audio_bitmask |= (v + 1) << 2;
		break;
	case V4L2_CID_IVTV_BITRATE:
		if ((v < 0) || (v > 14))
			return -ERANGE;
		itv->codec.audio_bitmask &= ~(15 << 4);
		itv->codec.audio_bitmask |= v << 4;
		break;
	case V4L2_CID_IVTV_MONO:
		if ((v < 0) || (v > 3))
			return -ERANGE;
		itv->codec.audio_bitmask &= ~(3 << 8);
		itv->codec.audio_bitmask |= v << 8;
		break;
	case V4L2_CID_IVTV_JOINT:
		if ((v < 0) || (v > 3))
			return -ERANGE;
		itv->codec.audio_bitmask &= ~(3 << 10);
		itv->codec.audio_bitmask |= v << 10;
		break;
	case V4L2_CID_IVTV_EMPHASIS:
		if ((v < 0) || (v > 2))
			return -ERANGE;
		itv->codec.audio_bitmask &= ~(3 << 12);
		itv->codec.audio_bitmask |= v << 12;
		break;
	case V4L2_CID_IVTV_CRC:
		if ((v < 0) || (v > 1))
			return -ERANGE;
		itv->codec.audio_bitmask &= ~(1 << 14);
		itv->codec.audio_bitmask |= v << 14;
		break;
	case V4L2_CID_IVTV_COPYRIGHT:
		if ((v < 0) || (v > 1))
			return -ERANGE;
		itv->codec.audio_bitmask &= ~(1 << 15);
		itv->codec.audio_bitmask |= v << 15;
		break;
	case V4L2_CID_IVTV_GEN:
		if ((v < 0) || (v > 1))
			return -ERANGE;
		itv->codec.audio_bitmask &= ~(1 << 16);
		itv->codec.audio_bitmask |= v << 16;
		break;

		/* Standard V4L2 controls */
	case V4L2_CID_BRIGHTNESS:
	case V4L2_CID_HUE:
	case V4L2_CID_SATURATION:
	case V4L2_CID_CONTRAST:
		return itv->card->video_dec_func(itv, VIDIOC_S_CTRL, vctrl);
	case V4L2_CID_AUDIO_VOLUME:
		if (v > 65535 || v < 0)
			return -ERANGE;
		ivtv_audio_set_volume(itv, v);
		break;
	case V4L2_CID_AUDIO_MUTE:
		if ((v < 0) || (v > 1))
			return -ERANGE;
		ivtv_audio_set_mute(itv, v);
		break;
	default:
		IVTV_DEBUG_IOCTL("invalid control %x\n", vctrl->id);
		return -EINVAL;
	}
	return 0;
}

static int ivtv_g_ctrl(struct ivtv *itv, struct v4l2_control *vctrl)
{
	IVTV_DEBUG_IOCTL("VIDIOC_G_CTRL(%d)\n", vctrl->id);

	switch (vctrl->id) {
		/* Audio controls */
	case V4L2_CID_IVTV_FREQ:
		vctrl->value = itv->codec.audio_bitmask & 3;
		break;
	case V4L2_CID_IVTV_ENC:
		vctrl->value = ((itv->codec.audio_bitmask >> 2) & 3) - 1;
		break;
	case V4L2_CID_IVTV_BITRATE:
		vctrl->value = (itv->codec.audio_bitmask >> 4) & 15;
		break;
	case V4L2_CID_IVTV_MONO:
		vctrl->value = (itv->codec.audio_bitmask >> 8) & 3;
		break;
	case V4L2_CID_IVTV_JOINT:
		vctrl->value = (itv->codec.audio_bitmask >> 10) & 3;
		break;
	case V4L2_CID_IVTV_EMPHASIS:
		vctrl->value = (itv->codec.audio_bitmask >> 12) & 3;
		break;
	case V4L2_CID_IVTV_CRC:
		vctrl->value = (itv->codec.audio_bitmask >> 14) & 1;
		break;
	case V4L2_CID_IVTV_COPYRIGHT:
		vctrl->value = (itv->codec.audio_bitmask >> 15) & 1;
		break;
	case V4L2_CID_IVTV_GEN:
		vctrl->value = (itv->codec.audio_bitmask >> 16) & 1;
		break;

		/* Standard V4L2 controls */
	case V4L2_CID_BRIGHTNESS:
	case V4L2_CID_HUE:
	case V4L2_CID_SATURATION:
	case V4L2_CID_CONTRAST:
		return itv->card->video_dec_func(itv, VIDIOC_G_CTRL, vctrl);
	case V4L2_CID_AUDIO_VOLUME:
		vctrl->value = ivtv_audio_get_volume(itv);
		break;
	case V4L2_CID_AUDIO_MUTE:
		vctrl->value = ivtv_audio_get_mute(itv);
		break;
	default:
		IVTV_DEBUG_IOCTL("invalid control %x\n", vctrl->id);
		return -EINVAL;
	}
	return 0;
}

int ivtv_control_ioctls(struct ivtv *itv, unsigned int cmd, void *arg)
{
	switch (cmd) {
	case VIDIOC_QUERYMENU:
	        IVTV_DEBUG_IOCTL("VIDIOC_QUERYMENU\n");
		return ivtv_querymenu(itv, arg);

	case VIDIOC_QUERYCTRL:
		return ivtv_queryctrl(itv, arg);

	case VIDIOC_S_CTRL:
		return ivtv_s_ctrl(itv, arg);

	case VIDIOC_G_CTRL:
		return ivtv_g_ctrl(itv, arg);

	default:
		return -EINVAL;
	}
	return 0;
}
