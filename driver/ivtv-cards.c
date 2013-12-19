/*
    Functions to query card hardware
    Copyright (C) 2003-2004  Kevin Thayer <nufan_wfk at yahoo.com>

    Audio input/output:
    Copyright (C) 2004  Hans Verkuil <hverkuil@xs4all.nl>

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

#include "ivtv-i2c.h"

/********************** card configuration *******************************/

/* Inputs for Hauppauge cards */
static struct v4l2_input ivtv_pvr150_inputs[] = {
	{
		.index = 0,
		.name = "Tuner",
                .type = V4L2_INPUT_TYPE_TUNER,
                .audioset = 3,
                .tuner = 0,
                .status = 0,
                .std = V4L2_STD_ALL,
        }, {
                .index = 1,
                .name = "Composite 0",
                .type = V4L2_INPUT_TYPE_CAMERA,
                .audioset = 3,
                .tuner = 0,
                .status = 0,
                .std = V4L2_STD_ALL,
        }, {
                .index = 2,
                .name = "Composite 1",
                .type = V4L2_INPUT_TYPE_CAMERA,
                .audioset = 3,
                .tuner = 0,
                .status = 0,
                .std = V4L2_STD_ALL,
        }, {
                .index = 3,
                .name = "S-Video 0",
                .type = V4L2_INPUT_TYPE_CAMERA,
                .audioset = 3,
                .tuner = 0,
                .status = 0,
                .std = V4L2_STD_ALL,
        }, {
                .index = 4,
                .name = "S-Video 1",
                .type = V4L2_INPUT_TYPE_CAMERA,
                .audioset = 3,
                .tuner = 0,
                .status = 0,
                .std = V4L2_STD_ALL,
        }
};

static const int ivtv_pvr150_inputs_size =
    sizeof(ivtv_pvr150_inputs) / sizeof(ivtv_pvr150_inputs[0]);

const struct v4l2_input *ivtv_get_input(struct ivtv *itv, u16 index)
{
	switch (itv->card->type) {
	case IVTV_CARD_PVR_150:
		if (index >= ivtv_pvr150_inputs_size)
			return NULL;
		return &ivtv_pvr150_inputs[index];
	default:
		if (index >= ivtv_pvr150_inputs_size)
			return NULL;
		return &ivtv_pvr150_inputs[index];
	}
}

/* Outputs for Hauppauge cards */
static struct v4l2_output ivtv_pvr_outputs[] = {
        {
                .index = 0,
                .name = "S-Video + Composite",
                .type = V4L2_OUTPUT_TYPE_ANALOG,
                .audioset = 1,
                .std = V4L2_STD_ALL,
        }
};

static const int ivtv_pvr_outputs_size =
    sizeof(ivtv_pvr_outputs) / sizeof(ivtv_pvr_outputs[0]);

const struct v4l2_output *ivtv_get_output(struct ivtv *itv, u16 index)
{
	if (!(itv->v4l2_cap & V4L2_CAP_VIDEO_OUTPUT))
		return NULL;
	if (index >= ivtv_pvr_outputs_size)
		return NULL;
	return &ivtv_pvr_outputs[index];
}

/* Audio inputs */
static struct v4l2_audio ivtv_pvr150_audio_inputs[] = {
	{
	 .index = 0,
	 .name = "Tuner Audio In",
	 .capability = V4L2_AUDCAP_STEREO,
	}, {
	 .index = 1,
	 .name = "Audio Line 1",
	 .capability = V4L2_AUDCAP_STEREO,
	}, {
	 .index = 2,
	 .name = "Audio Line 2",
	 .capability = V4L2_AUDCAP_STEREO,
	}, {
	 .index = 3,
	 .name = "Audio Line 3",
	 .capability = V4L2_AUDCAP_STEREO,
	}, {
	 .index = 4,
	 .name = "Audio Line 4",
	 .capability = V4L2_AUDCAP_STEREO,
	}
};

static const int ivtv_pvr150_audio_inputs_size =
    sizeof(ivtv_pvr150_audio_inputs) / sizeof(ivtv_pvr150_audio_inputs[0]);

const struct v4l2_audio *ivtv_get_audio_input(struct ivtv *itv, u16 index)
{
	switch (itv->card->type) {
	case IVTV_CARD_PVR_150:
		if (index >= ivtv_pvr150_audio_inputs_size)
			return NULL;
		return &ivtv_pvr150_audio_inputs[index];

	default:
		if (index >= ivtv_pvr150_audio_inputs_size)
			return NULL;
		return &ivtv_pvr150_audio_inputs[index];
	}
}

/* Audio outputs */
static struct v4l2_audioout ivtv_pvr_audio_outputs[] = {
	{
	 .index = 0,
	 .name = "A/V Audio Out",
	 }
};

const struct v4l2_audioout *ivtv_get_audio_output(struct ivtv *itv, u16 index)
{
	if (!(itv->v4l2_cap & V4L2_CAP_VIDEO_OUTPUT))
		return NULL;
	if (index != 0)
		return NULL;
	return &ivtv_pvr_audio_outputs[index];
}

struct v4l2_fmtdesc ivtv_formats[] = {
        {
                .index       = 0,
                .type        = V4L2_BUF_TYPE_VIDEO_CAPTURE,
                .flags       = V4L2_FMT_FLAG_COMPRESSED,
                .description = "MPEG2 Compressed Video",
                .pixelformat = V4L2_PIX_FMT_MPEG,
                .reserved    = {0, 0, 0, 0}
        }, {
                .index       = 1,
                .type        = V4L2_BUF_TYPE_VIDEO_CAPTURE,
                .flags       = 0,
                .description = "YUV 420 Raw Video",
                .pixelformat = V4L2_PIX_FMT_NV12,
                .reserved    = {0, 0, 0, 0}
        }
};

static struct ivtv_fmt formats[] = {
        {
                .name     = "MPEG Compressed Video",
                .fourcc   = V4L2_PIX_FMT_MPEG,
                .cxformat = 0x04,
                .depth    = 12,
                .flags    = V4L2_FMT_FLAG_COMPRESSED
        },{
                .name     = "NV12 420 Raw Video",
                .fourcc   = V4L2_PIX_FMT_NV12,
                .cxformat = 0x00,
                .depth    = 12,
                .flags    = FORMAT_FLAGS_PLANAR
        },{
                .name     = "YUV 420 Raw Video",
                .fourcc   = V4L2_PIX_FMT_YUV420,
                .cxformat = 0x00,
                .depth    = 12,
                .flags    = FORMAT_FLAGS_PLANAR
	}
};

struct ivtv_fmt* format_by_fourcc(unsigned int fourcc)
{
        unsigned int i;

        for (i = 0; i < ARRAY_SIZE(formats); i++)
                if (formats[i].fourcc == fourcc)
                        return formats+i;
        return NULL;
}

const unsigned int IVTV_FORMATS = ARRAY_SIZE(ivtv_formats);

#define V4L2_CAP_ENCODER (V4L2_CAP_VIDEO_CAPTURE | V4L2_CAP_TUNER | \
		          V4L2_CAP_AUDIO | V4L2_CAP_READWRITE | V4L2_CAP_VBI_CAPTURE | \
                          V4L2_CAP_SLICED_VBI_CAPTURE | \
			  V4L2_CAP_STREAMING)
#define V4L2_CAP_DECODER (V4L2_CAP_VBI_OUTPUT | V4L2_CAP_VIDEO_OUTPUT | \
                          V4L2_CAP_SLICED_VBI_OUTPUT)

/* tuner I2C addresses */
#define IVTV_TUNER_I2C_ADDR 		0x61
#define IVTV_MPG600_TUNER_I2C_ADDR 	0x60
#define IVTV_MPG160_TUNER_I2C_ADDR 	0x60
#define IVTV_M179_TUNER_I2C_ADDR 	0x60
#define IVTV_PG600_TUNER_I2C_ADDR 	0x61
#define IVTV_AVC2410_TUNER_I2C_ADDR	0x60

/* Please add new PCI IDs to: http://pci-ids.ucw.cz/iii
   This keeps the PCI ID database up to date. Note that the entries
    must be added under vendor 0x4444 (Conexant) as subsystem IDs.
    New vendor IDs should still be added to the vendor ID list. */
static const struct ivtv_card_pci_info ivtv_pci_pvr150[] = {
	{PCI_DEVICE_ID_IVTV16, IVTV_PCI_ID_HAUPPAUGE, 0x0009},
	{PCI_DEVICE_ID_IVTV16, IVTV_PCI_ID_HAUPPAUGE, 0x0801},
	{PCI_DEVICE_ID_IVTV16, IVTV_PCI_ID_HAUPPAUGE, 0x0807},
	{PCI_DEVICE_ID_IVTV16, IVTV_PCI_ID_HAUPPAUGE, 0x17e7},
	{PCI_DEVICE_ID_IVTV16, IVTV_PCI_ID_HAUPPAUGE, 0x17f7},
	{PCI_DEVICE_ID_IVTV16, IVTV_PCI_ID_HAUPPAUGE, 0x37f1},
	{PCI_DEVICE_ID_IVTV16, IVTV_PCI_ID_HAUPPAUGE, 0x37f3},
	{PCI_DEVICE_ID_IVTV16, IVTV_PCI_ID_HAUPPAUGE, 0x8001},
	{PCI_DEVICE_ID_IVTV16, IVTV_PCI_ID_HAUPPAUGE, 0x8003},
	{PCI_DEVICE_ID_IVTV16, IVTV_PCI_ID_HAUPPAUGE, 0x8801},
	{PCI_DEVICE_ID_IVTV16, IVTV_PCI_ID_HAUPPAUGE, 0xc801},
	{PCI_DEVICE_ID_IVTV16, IVTV_PCI_ID_HAUPPAUGE, 0xe807},
	{PCI_DEVICE_ID_IVTV16, 0x470, 0xe807},
	{PCI_DEVICE_ID_IVTV16, IVTV_PCI_ID_HAUPPAUGE, 0xe817},
	{PCI_DEVICE_ID_IVTV16, 0x470, 0xe817},
        // Alternate Hauppauge vendor IDs 
	{PCI_DEVICE_ID_IVTV16, 0x0270,                0x0801},
	{PCI_DEVICE_ID_IVTV16, 0x4070,                0x8801},
	{0, 0, 0}
};

static const struct ivtv_card ivtv_card_list[] = {
	{
                .type = IVTV_CARD_PVR_150,
                .name = "WinTV PVR 150",
                .v4l2_capabilities = V4L2_CAP_ENCODER,
                .audio_selector = USE_PVR150,

                .video_dec_func = ivtv_cx25840,
                .init_ex_func = NULL,
                .chg_channel_ex_func = NULL,
                .chg_input_ex_func = NULL,

                .pci_list = ivtv_pci_pvr150,
        }, 
	{
                .type = IVTV_CARD_PVR_150,
                .name = "WinTV PVR 150",
                .v4l2_capabilities = V4L2_CAP_ENCODER,
                .audio_selector = USE_PVR150,

                .video_dec_func = ivtv_cx25840,
                .init_ex_func = NULL,
                .chg_channel_ex_func = NULL,
                .chg_input_ex_func = NULL,

                .pci_list = ivtv_pci_pvr150,
        }
};

static const int ivtv_cards_size =
    sizeof(ivtv_card_list) / sizeof(ivtv_card_list[0]);

const struct ivtv_card *ivtv_get_card(u16 index)
{
	if (index >= ivtv_cards_size)
		return NULL;
	return &ivtv_card_list[index];
}
