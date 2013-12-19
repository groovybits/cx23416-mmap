/*
    ioctl system call
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
#include "ivtv-version.h"
#include "ivtv-mailbox.h"
#include "ivtv-i2c.h"
#include "ivtv-queue.h"
#include "ivtv-fileops.h"
#include "ivtv-vbi.h"
#include "ivtv-audio.h"
#include "ivtv-video.h"
#include "ivtv-streams.h"
#include "ivtv-ioctl.h"
#include "ivtv-controls.h"
#include "ivtv-cards.h"
#include "ivtv-irq.h"
#include "v4l2-common.h"
#include "audiochip.h"
#include "cx25840.h"
#include "ivtv-reset.h"

/* from v4l1_compat.c */
extern int
ivtv_compat_translate_ioctl(struct inode *inode,
			    struct file *file,
			    int cmd, void *arg, v4l2_kioctl drv);

u16 service2vbi(int type)
{
        switch (type) {
                case V4L2_SLICED_TELETEXT_B:
                        return IVTV_SLICED_TYPE_TELETEXT_B;
                case V4L2_SLICED_CAPTION_525:
                        return IVTV_SLICED_TYPE_CAPTION_525;
                case V4L2_SLICED_WSS_625:
                        return IVTV_SLICED_TYPE_WSS_625;
                case V4L2_SLICED_VPS:
                        return IVTV_SLICED_TYPE_VPS;
                default:
                        return 0;
        }
}

static int valid_service_line(int field, int line, int is_pal)
{
        return (is_pal && line >= 6 && (line != 23 || field == 0)) ||
               (!is_pal && line >= 10 && line < 22);
}

static u16 select_service_from_set(int field, int line, u16 set, int is_pal)
{
        u16 valid_set = (is_pal ? V4L2_SLICED_VBI_625 : V4L2_SLICED_VBI_525);
        int i;

        set = set & valid_set;
        if (set == 0 || !valid_service_line(field, line, is_pal)) {
                return 0;
        }
        if (!is_pal) {
                if (line == 21 && (set & V4L2_SLICED_CAPTION_525))
                        return V4L2_SLICED_CAPTION_525;
        }
        else {
                if (line == 16 && field == 0 && (set & V4L2_SLICED_VPS))
                        return V4L2_SLICED_VPS;
                if (line == 23 && field == 0 && (set & V4L2_SLICED_WSS_625))
                        return V4L2_SLICED_WSS_625;
                if (line == 23)
                        return 0;
        }
        for (i = 0; i < 32; i++) {
                if ((1 << i) & set)
                        return 1 << i;
        }
        return 0;
}

static void expand_service_set(struct v4l2_sliced_vbi_format *fmt, int is_pal)
{
        u16 set = fmt->service_set;
        int f, l;

        fmt->service_set = 0;
        for (f = 0; f < 2; f++) {
                for (l = 0; l < 24; l++) {
                        fmt->service_lines[f][l] = select_service_from_set(f, l, set, is_pal);
                }
        }
}

static int check_service_set(struct v4l2_sliced_vbi_format *fmt, int is_pal)
{
        int f, l;
        u16 set = 0;

        for (f = 0; f < 2; f++) {
                for (l = 0; l < 24; l++) {
                        fmt->service_lines[f][l] = select_service_from_set(f, l, fmt->service_lines[f][l], is_pal);
                        set |= fmt->service_lines[f][l];
                }
        }
        return set != 0;
}

u16 get_service_set(struct v4l2_sliced_vbi_format *fmt)
{
        int f, l;
        u16 set = 0;

        for (f = 0; f < 2; f++) {
                for (l = 0; l < 24; l++) {
                        set |= fmt->service_lines[f][l];
                }
        }
        return set;
}

static const struct v4l2_standard ivtv_stds[] = {
        {
                .index = 0,
                .id = V4L2_STD_NTSC,
                .name = "NTSC",
                .frameperiod = {.numerator = 1001,
                        .denominator = 30000},
                .framelines = 525,
                .reserved = {0, 0, 0, 0}
        }, {
                .index = 1,
                .id = V4L2_STD_PAL,
                .name = "PAL",
                .frameperiod = {.numerator = 1,
                        .denominator = 25},
                .framelines = 625,
                .reserved = {0, 0, 0, 0}
        }, {
                .index = 2,
                .id = V4L2_STD_SECAM,
                .name = "SECAM",
                .frameperiod = {.numerator = 1,
                        .denominator = 25},
                .framelines = 625,
                .reserved = {0, 0, 0, 0}
        }
};
static const int ivtv_stds_size = sizeof(ivtv_stds) / sizeof(ivtv_stds[0]);

static int ivtv_itvc(struct ivtv *itv, unsigned int cmd, void *arg)
{
	struct v4l2_register *regs = arg;
        unsigned long flags;
        unsigned char *reg_start;

        if (regs->reg >= 0x02000000 && regs->reg < IVTV_IOREMAP_SIZE) {
                reg_start = itv->reg_mem;// - 0x02000000;
                regs->reg -= 0x02000000;
        } else if (regs->reg >= 0x01000000 && regs->reg < 0x01800000)
                return -EINVAL;
        else if (regs->reg >= 0x00000000 && regs->reg < 0x00800000)
                reg_start = itv->enc_mem;
        else 
                return -EINVAL;

	spin_lock_irqsave(&ivtv_cards_lock, flags);
	if (cmd == ITVC_GET_REG) {
		regs->val = readl(regs->reg + reg_start);
	} else {
		printk("Write Register 0x%08x with value 0x%08x\n",
		       (unsigned int)regs->reg, (unsigned int)regs->val);
		writel(regs->val, regs->reg + reg_start);
	}
	spin_unlock_irqrestore(&ivtv_cards_lock, flags);
        return 0;
}

static int ivtv_get_fmt(struct ivtv *itv, int streamtype, struct v4l2_format *fmt)
{
	struct ivtv_stream *st;
	st = &itv->streams[streamtype];

        switch (fmt->type) {
        case V4L2_BUF_TYPE_VIDEO_CAPTURE:
        case V4L2_BUF_TYPE_VIDEO_OUTPUT:
                fmt->fmt.pix.width = itv->width;
                fmt->fmt.pix.height = itv->height;
                fmt->fmt.pix.colorspace = V4L2_COLORSPACE_SMPTE170M;
                fmt->fmt.pix.field = V4L2_FIELD_INTERLACED;
               	fmt->fmt.pix.sizeimage = st->bufsize;
                if (streamtype == IVTV_ENC_STREAM_TYPE_YUV) {

                        /* YUV size is (Y=(w*h) + UV=(w*(h/2))) */
                        int ysize = fmt->fmt.pix.width * fmt->fmt.pix.height;
                        int uvsize = fmt->fmt.pix.width * (fmt->fmt.pix.height / 2);

			ysize = ysize + (PAGE_SIZE-(ysize%PAGE_SIZE));
			uvsize = uvsize + (PAGE_SIZE-(uvsize%PAGE_SIZE));

                        fmt->fmt.pix.pixelformat = V4L2_PIX_FMT_NV12;
                	fmt->fmt.pix.bytesperline = 720 * 2;
                } else {
                        fmt->fmt.pix.pixelformat = V4L2_PIX_FMT_MPEG;
                	fmt->fmt.pix.bytesperline = itv->width * 2;
                }
                break;

        case V4L2_BUF_TYPE_VBI_CAPTURE:
                fmt->fmt.vbi.sampling_rate = 27000000;
                fmt->fmt.vbi.offset = 248;
                fmt->fmt.vbi.samples_per_line = itv->vbi_raw_decoder_line_size - 4;
                fmt->fmt.vbi.sample_format = V4L2_PIX_FMT_GREY;
                fmt->fmt.vbi.start[0] = itv->vbi_start[0];
                fmt->fmt.vbi.start[1] = itv->vbi_start[1];
                fmt->fmt.vbi.count[0] = fmt->fmt.vbi.count[1] = itv->vbi_count;
                break;

        case V4L2_BUF_TYPE_SLICED_VBI_OUTPUT:
        {
                struct v4l2_sliced_vbi_format *vbifmt = &fmt->fmt.sliced;

                vbifmt->io_size = sizeof(struct v4l2_sliced_vbi_data) * 36;
                memset(vbifmt->reserved, 0, sizeof(vbifmt->reserved));
                memset(vbifmt->service_lines, 0, sizeof(vbifmt->service_lines));
                if (itv->is_60hz) {
                        vbifmt->service_lines[0][21] = V4L2_SLICED_CAPTION_525;
                        vbifmt->service_lines[1][21] = V4L2_SLICED_CAPTION_525;
                } else {
                        vbifmt->service_lines[0][23] = V4L2_SLICED_WSS_625;
                        vbifmt->service_lines[0][16] = V4L2_SLICED_VPS;
                }
                vbifmt->service_set = get_service_set(vbifmt);
                break;
        }

        case V4L2_BUF_TYPE_SLICED_VBI_CAPTURE: 
        {
                struct v4l2_sliced_vbi_format *vbifmt = &fmt->fmt.sliced;

                vbifmt->io_size = sizeof(struct v4l2_sliced_vbi_data) * 36;
                memset(vbifmt->reserved, 0, sizeof(vbifmt->reserved));
                memset(vbifmt->service_lines, 0, sizeof(vbifmt->service_lines));

                itv->card->video_dec_func(itv, VIDIOC_G_FMT, fmt);
                vbifmt->service_set = get_service_set(vbifmt);
                break;
        }
        case V4L2_BUF_TYPE_VBI_OUTPUT:
        case V4L2_BUF_TYPE_VIDEO_OVERLAY:
        default:
                return -EINVAL;
        }
        return 0;
}

static int ivtv_try_or_set_fmt(struct ivtv *itv, int streamtype,
                struct v4l2_format *fmt, int set_fmt)
{
	struct ivtv_stream *st;
        struct v4l2_sliced_vbi_format *vbifmt = &fmt->fmt.sliced;
        u16 set;

        st = &itv->streams[streamtype];

        if (fmt->type == V4L2_BUF_TYPE_VIDEO_OUTPUT) 
                return ivtv_get_fmt(itv, streamtype, fmt);

        // set window size
        if (fmt->type == V4L2_BUF_TYPE_VIDEO_CAPTURE) {
                struct v4l2_format pix;

                if (!set_fmt)
                        return ivtv_get_fmt(itv, streamtype, fmt);

                if ((itv->width != fmt->fmt.pix.width ||
                     itv->height != fmt->fmt.pix.height) &&
                    atomic_read(&itv->capturing) > 0) {
                        return -EBUSY;
                }

                /* FIXME: only sets resolution for now */
		pix.type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
                pix.fmt.pix.width = itv->width = fmt->fmt.pix.width;
                pix.fmt.pix.height = itv->height = fmt->fmt.pix.height;
                itv->card->video_dec_func(itv, VIDIOC_S_FMT, &pix);

                if (itv->codec.stream_type == IVTV_STREAM_MPEG1 ||
                    itv->codec.stream_type == IVTV_STREAM_VCD) {
                        /* this is an MPEG1 stream */
                        IVTV_DEBUG_INFO("VIDIOC_S_FMT: "
                            "the current codec stream type is MPEG1 or VCD, "
                            "you have to do a S_CODEC after this ioctl\n");
                }
                return ivtv_get_fmt(itv, streamtype, fmt);
        }
       
        // set raw VBI format
        if (fmt->type == V4L2_BUF_TYPE_VBI_CAPTURE) {
                if (set_fmt && streamtype == IVTV_ENC_STREAM_TYPE_VBI &&
                    itv->vbi_sliced_in->service_set &&
                    atomic_read(&itv->capturing) > 0) {
                        return -EBUSY;
                }
		itv->vbi_sliced_in->service_set = 0;
                itv->card->video_dec_func(itv, VIDIOC_S_FMT, &itv->vbi_in);

 		//st->buftype = V4L2_BUF_TYPE_VBI_CAPTURE;

                return ivtv_get_fmt(itv, streamtype, fmt);
        }
       
        // set sliced VBI output
        // In principle the user could request that only certain
        // VBI types are output and that the others are ignored.
        // I.e., suppress CC in the even fields or only output
        // WSS and no VPS. Currently though there is no choice.
        if (fmt->type == V4L2_BUF_TYPE_SLICED_VBI_OUTPUT) 
                return ivtv_get_fmt(itv, streamtype, fmt);

        // any else but sliced VBI capture is an error
        if (fmt->type != V4L2_BUF_TYPE_SLICED_VBI_CAPTURE) 
                return -EINVAL;

        // set sliced VBI capture format
        vbifmt->io_size = sizeof(struct v4l2_sliced_vbi_data) * 36;
        memset(vbifmt->reserved, 0, sizeof(vbifmt->reserved));

        if (vbifmt->service_set)
                expand_service_set(vbifmt, itv->is_50hz);
        set = check_service_set(vbifmt, itv->is_50hz);
        vbifmt->service_set = get_service_set(vbifmt);

	// Sliced Buffers
        //st->buftype = V4L2_BUF_TYPE_SLICED_VBI_CAPTURE;

        if (!set_fmt)
                return 0;
        if (set == 0)
                return -EINVAL;
        if (atomic_read(&itv->capturing) > 0 && itv->vbi_sliced_in->service_set == 0) {
                return -EBUSY;
        }
        itv->card->video_dec_func(itv, VIDIOC_S_FMT, fmt);
	memcpy(itv->vbi_sliced_in, vbifmt, sizeof(*itv->vbi_sliced_in));
        return 0;
}


int ivtv_internal_ioctls(struct ivtv *itv, int streamtype, unsigned int cmd,
			 void *arg)
{
	struct v4l2_register *reg = arg;

	switch (cmd) {
	case IVTV_IOC_FWAPI:{
		struct ivtv_ioctl_fwapi *fwapi = arg;

		IVTV_DEBUG_IOCTL("IVTV_IOC_FWAPI\n");

		/* Encoder */
		if (fwapi->cmd >= 128)
			return ivtv_api(itv, itv->enc_mbox, &itv->enc_msem,
					fwapi->cmd, &fwapi->result, fwapi->args,
					fwapi->data);

		return -1;
	}

	case IVTV_IOC_RELOAD_FW:{
		int ret = 0;

		IVTV_DEBUG_IOCTL("IVTV_IOC_RELOAD_FW\n");

		/* Force Full Reload */
		ret = ivtv_reset_card(itv, 1, IVTV_CMD_FULL_RESET);

		return ret;
	}

	/* ioctls to allow direct access to the ITVC registers for testing */
	case IVTV_IOC_G_ITVC_REG:
		IVTV_DEBUG_IOCTL("IVTV_IOC_G_ITVC_REG\n");
		return ivtv_itvc(itv, ITVC_GET_REG, arg);

	case IVTV_IOC_S_ITVC_REG:
		IVTV_DEBUG_IOCTL("IVTV_IOC_S_ITVC_REG\n");
		return ivtv_itvc(itv, ITVC_SET_REG, arg);

	/* ioctls to allow direct access to the decoder registers for testing */
	case IVTV_IOC_G_DECODER_REG:
		IVTV_DEBUG_IOCTL("IVTV_IOC_G_DECODER_REG\n");
		reg->i2c_id = (itv->hw_flags & IVTV_HW_CX25840) ?
			I2C_DRIVERID_CX25840 : I2C_DRIVERID_SAA711X;
		return itv->card->video_dec_func(itv, VIDIOC_INT_G_REGISTER, reg);

	case IVTV_IOC_S_DECODER_REG:
		IVTV_DEBUG_IOCTL("IVTV_IOC_S_DECODER_REG\n");
		reg->i2c_id = (itv->hw_flags & IVTV_HW_CX25840) ?
			I2C_DRIVERID_CX25840 : I2C_DRIVERID_SAA711X;
		return itv->card->video_dec_func(itv, VIDIOC_INT_S_REGISTER, reg);

	case IVTV_IOC_ZCOUNT:{
		/* Zeroes out usage count so it can be unloaded in case of
		 * drastic error */

		IVTV_DEBUG_IOCTL("ZCOUNT\n");

#ifndef LINUX26
		while (MOD_IN_USE)
			MOD_DEC_USE_COUNT;

		MOD_INC_USE_COUNT;
#endif /* LINUX26 */
		break;
	}

	case IVTV_IOC_S_MSP_MATRIX:{
		struct ivtv_msp_matrix *matrix = arg;

		IVTV_DEBUG_IOCTL("IVTV_IOC_S_MSP_MATRIX\n");
		return ivtv_audio_set_matrix(itv, matrix);
	}

	case IVTV_IOC_G_DEBUG_LEVEL:
		IVTV_DEBUG_IOCTL("IVTV_IOC_G_DEBUG_LEVEL\n");
		*(int *)arg = ivtv_debug;
		break;

	case IVTV_IOC_S_DEBUG_LEVEL:
		IVTV_DEBUG_IOCTL("IVTV_IOC_S_DEBUG_LEVEL\n");
		ivtv_debug = (*(int *)arg) | IVTV_DBGFLG_WARN;
		*(int *)arg = ivtv_debug;
		break;

	default:
		IVTV_DEBUG_IOCTL( "Unknown internal IVTV command %08x\n", cmd);
		return -EINVAL;
	}
	return 0;
}

int ivtv_v4l2_ioctls(struct file *filp, struct ivtv *itv, struct ivtv_open_id *id,
		     int streamtype, unsigned int cmd, void *arg)
{
	struct ivtv_stream *stream = &itv->streams[streamtype];

	switch (cmd) {
	case VIDIOC_QUERYCAP:{
		struct v4l2_capability *vcap = arg;
		memset(vcap, 0, sizeof(*vcap));

		IVTV_DEBUG_IOCTL("VIDIOC_QUERYCAP\n");

		/* driver name */
		strcpy(vcap->driver, IVTV_DRIVER_NAME);

		/* card type */
		strcpy(vcap->card, itv->card_name);

		/* bus info... */
                strcpy(vcap->bus_info, pci_name(itv->dev));

		/* version */
		vcap->version = IVTV_DRIVER_VERSION;

		/* capabilities */
		vcap->capabilities = itv->v4l2_cap;

		/* reserved.. must set to 0! */
		vcap->reserved[0] = vcap->reserved[1] = vcap->reserved[2] =
		    vcap->reserved[3] = 0;
		break;
	}

	case VIDIOC_ENUMAUDIO:{
		struct v4l2_audio *vin = arg;

		IVTV_DEBUG_IOCTL("VIDIOC_ENUMAUDIO\n");

		if (ivtv_get_audio_input(itv, vin->index) == NULL) {
			return -EINVAL;
		}

		/* set it to defaults from our table */
		*vin = *ivtv_get_audio_input(itv, vin->index);
		break;
	}

	case VIDIOC_G_AUDIO:{
		struct v4l2_audio *vin = arg;

		IVTV_DEBUG_IOCTL("VIDIOC_G_AUDIO\n");
		if (test_bit(IVTV_F_I_RADIO_USER, &itv->i_flags)) {
			vin->index = itv->audio_input_radio;
			/* msp3400 input 2 maps to audio input 0 (tuner) */
			if (vin->index == 2)
				vin->index = 0;
		} else {
			vin->index = itv->audio_input_tv;
		}

		if (ivtv_get_audio_input(itv, vin->index) == NULL)
			return -EINVAL;
		*vin = *ivtv_get_audio_input(itv, vin->index);
		break;
	}

	case VIDIOC_S_AUDIO:{
		struct v4l2_audio *vout = arg;

		IVTV_DEBUG_IOCTL("VIDIOC_S_AUDIO\n");

		if (ivtv_get_audio_input(itv, vout->index) == NULL)
			return -EINVAL;
		if (test_bit(IVTV_F_I_RADIO_USER, &itv->i_flags)) {
			/* audio input 0 (tuner) maps to msp3400 input 2 for the radio */
			if (vout->index == 0)
				vout->index = 2;
			itv->audio_input_radio = vout->index;
		} else {
			itv->audio_input_tv = vout->index;
		}
		ivtv_audio_set_io(itv);
		break;
	}

	case VIDIOC_ENUMAUDOUT:{
		struct v4l2_audioout *vin = arg;

		IVTV_DEBUG_IOCTL("VIDIOC_ENUMAUDOUT\n");

		if (ivtv_get_audio_output(itv, vin->index) == NULL) {
			return -EINVAL;
		}

		/* set it to defaults from our table */
		*vin = *ivtv_get_audio_output(itv, vin->index);
		break;
	}

	case VIDIOC_G_AUDOUT:{
		struct v4l2_audioout *vin = arg;

		IVTV_DEBUG_IOCTL("VIDIOC_G_AUDOUT\n");
		vin->index = 0;
		if (ivtv_get_audio_output(itv, vin->index) == NULL)
			return -EINVAL;
		*vin = *ivtv_get_audio_output(itv, vin->index);
		break;
	}

	case VIDIOC_S_AUDOUT:{
		struct v4l2_audioout *vout = arg;

		IVTV_DEBUG_IOCTL("VIDIOC_S_AUDOUT\n");

		if (ivtv_get_audio_output(itv, vout->index) == NULL)
			return -EINVAL;
		/* Do nothing. */
		break;
	}

	case VIDIOC_ENUMINPUT:{
		struct v4l2_input *vin = arg;

		IVTV_DEBUG_IOCTL("VIDIOC_ENUMINPUT\n");

		if (ivtv_get_input(itv, vin->index) == NULL) {
			return -EINVAL;
		}

		/* set it to defaults from our table */
		*vin = *ivtv_get_input(itv, vin->index);

		/* The tuner is a special case */
		if (vin->type == V4L2_INPUT_TYPE_TUNER)
			vin->std = itv->tuner_std;
		break;
	}

	case VIDIOC_ENUMOUTPUT:{
		struct v4l2_output *vout = arg;

		IVTV_DEBUG_IOCTL("VIDIOC_ENUMOUTPUT\n");

		if (ivtv_get_output(itv, vout->index) == NULL) {
			return -EINVAL;
		}

		/* set it to defaults from our table */
		*vout = *ivtv_get_output(itv, vout->index);

		break;
	}

 	case VIDIOC_STREAMOFF:{
                if (id->open_id != stream->id) {
                	IVTV_DEBUG_WARN("VIDEOC_STREAMOFF - Was not the right ID\n");
                        return -EBUSY;
		}

                // V4L Streaming Interface
		// Only if capturing
 		if (atomic_read(&itv->capturing) == 0 && (stream->id == -1)) {
               		IVTV_DEBUG_WARN( "VIDEOC_STREAMOFF - Stream not initialized\n");
               		return -EIO;
		}

		// If not capturing return
               	if (!test_bit(IVTV_F_S_CAPTURING,
                       	&stream->s_flags)) {
               		IVTV_DEBUG_WARN( "VIDEOC_STREAMOFF - Not capturing\n");
                       	return -EINVAL;
               	}

               	IVTV_DEBUG_INFO("VIDIOC_STREAMOFF - with %d buffers allocated\n",
			stream->buf_total);

		// See if this is safe?
               	//ivtv_stream_off(itv, stream); // MPEG Signal
		ivtv_stop_capture(itv, id->type);
		clear_bit(IVTV_F_S_CAPTURING, &stream->s_flags);

		// Stop V4L2 Streaming so it's safe
		ivtvbuf_streamoff(&stream->vidq);	

                break;
        }

	case VIDIOC_STREAMON: {
		//int blocking = !(filp->f_flags & O_NONBLOCK);

		// Make sure we own this stream
                if (id->open_id != stream->id) {
                        IVTV_DEBUG_WARN("VIDEOC_STREAMON - Was not the right ID\n");
                        return -EBUSY;
                }

                IVTV_DEBUG_INFO("VIDEOC_STREAMON\n");
		ivtvbuf_streamon(&stream->vidq);	

 		set_bit(IVTV_F_S_CAPTURING, &stream->s_flags);
                if (ivtv_start_v4l2_encode_stream(itv, stream->type)) {
                                        IVTV_DEBUG_WARN(
                                                   "Failed to start capturing "
                                                   "for stream %d "
                                                   "\n",
                                                   stream->type);

                                        clear_bit(IVTV_F_S_CAPTURING,
                                                  &stream->s_flags);
                                        return -EIO;
                }

		break;
	}

	case VIDIOCGMBUF:
        {
                struct video_mbuf *mbuf = arg;
                struct ivtvbuf_queue *q;
                struct v4l2_requestbuffers req;
                unsigned int i;
		int err = 0;

                q = &stream->vidq;
                memset(&req,0,sizeof(req));
                req.type   = q->type;
                req.count  = 8;
                req.memory = V4L2_MEMORY_MMAP;
                err = ivtvbuf_reqbufs(q,&req);
                if (err < 0)
                        return err;
                memset(mbuf,0,sizeof(*mbuf));
                mbuf->frames = req.count;
                mbuf->size   = 0;
                for (i = 0; i < mbuf->frames; i++) {
                        mbuf->offsets[i]  = q->bufs[i]->boff;
                        mbuf->size       += q->bufs[i]->bsize;
                }

		// Turn on Streaming
                return 0;
        }

        case VIDIOC_REQBUFS: {
                struct v4l2_requestbuffers *req = arg;
		int buffers = stream->buf_total;
		int bufmin = stream->buf_min;
		int bufmax = stream->buf_max;

		// Check if able to stream
                if (stream->buftype == 0)
			return -EINVAL;

		// Check if capture is in progress
        	if (test_bit(IVTV_F_S_CAPTURING, &stream->s_flags)) {
                	return -EBUSY;
		}

		// Check if same type of buffer
                if (stream->buftype != req->type) {
                	IVTV_DEBUG_WARN(
				"VIDEOC_REQBUF: got 0x%08x as stream buftype, req is for 0x%08x\n",
				stream->buftype, req->type);
			return -EINVAL;
		}

		// Claim the stream
                if (ivtv_claim_stream(id, stream->type))
                	return -EBUSY;

		// Double check buffers requested
		if (req->count > bufmax || req->count < bufmin) 
		{
			// Change to fit our min/max buffers
			if (req->count < bufmin)
				req->count = bufmin;
			else if (req->count > bufmax)
				req->count = bufmax;
		}
		// Recalculate all buffers
		buffers = req->count;

		// See if already allocated
                IVTV_DEBUG_INFO(
	 		"VIDIOC_REQBUF - %d buffers alloced %d req.\n",
                        buffers, req->count);

		// Failed to get Buffers
		if (!buffers) {
                        IVTV_DEBUG_WARN(
			 "VIDIOC_REQBUF - Error %d buffers alloced %d req.\n",
                                buffers, req->count);
			req->count = 0;
			return -ENOMEM;
		}
		stream->buf_total = req->count;
		//st->memory = req->memory;

                IVTV_DEBUG_INFO("VIDEOC_REQBUF\n");
		return ivtvbuf_reqbufs(&stream->vidq, req);
	}

	case VIDIOC_QUERYBUF: {
		struct v4l2_buffer *buf = arg;

		// Claim the stream
                if (ivtv_claim_stream(id, stream->type)) {
                        IVTV_DEBUG_WARN("VIDEOC_QEURYBUF - Device cannot be claimed!!!\n");
                	return -EBUSY;
		}

		// Check if right stream id
                if (id->open_id != stream->id) {
                        IVTV_DEBUG_WARN("VIDEOC_QEURYBUF - Was not the right ID\n");
                        return -EBUSY;
                }

                IVTV_DEBUG_INFO("VIDEOC_QUERYBUF - %d\n", buf->index);
		return ivtvbuf_querybuf(&stream->vidq, buf);
	}

	case VIDIOC_QBUF: {
		struct v4l2_buffer *buf = arg;
		int ret = 0;
		//int blocking = !(filp->f_flags & O_NONBLOCK);

		// Claim the stream
                if (ivtv_claim_stream(id, stream->type))
                	return -EBUSY;
		// Check if right stream id
                if (id->open_id != stream->id) {
                        IVTV_DEBUG_WARN("VIDEOC_QBUF - Was not the right ID\n");
                        return -EBUSY;
                }

                IVTV_DEBUG_INFO("VIDEOC_QBUF\n");
		if ((ret = ivtvbuf_qbuf(&stream->vidq, buf)))
			return ret;

		if (test_bit(IVTV_F_T_ENC_DMA_QUEUED, &itv->t_flags) &&
			!list_empty(&stream->queued) && list_empty(&stream->active)) 
		{
			ivtv_sched_DMA(itv, stream->type);
		}

		return ret;
	}

	case VIDIOC_DQBUF: {
		struct v4l2_buffer *buf = arg;
		//int blocking = !(filp->f_flags & O_NONBLOCK);
		int ret = 0;

		// Only if capturing
 		if (atomic_read(&itv->capturing) == 0 && (stream->id == -1)) {
                	IVTV_DEBUG_WARN( "Stream not initialized\n");
                	return -EIO;
		}

		// Claim the stream
                if (ivtv_claim_stream(id, stream->type))
                	return -EBUSY;
		// Make sure we own this stream
                if (id->open_id != stream->id) {
                        IVTV_DEBUG_WARN("VIDEOC_DQBUF - Was not the right ID\n");
                        return -EBUSY;
                }

		if (test_bit(IVTV_F_T_ENC_DMA_QUEUED, &itv->t_flags) && 
				!list_empty(&stream->queued) && list_empty(&stream->active)) 
		{
			ivtv_sched_DMA(itv, stream->type);
		}

		// Get a full buffer from Queue
		if ((ret = ivtvbuf_dqbuf(&stream->vidq, 
			buf, filp->f_flags & O_NONBLOCK))) {
			return ret;
		}

                IVTV_DEBUG_INFO("VIDEOC_DQBUF\n");
		return ret;
	}
       	case VIDIOC_ENUM_FMT: { 
 		                static struct v4l2_fmtdesc formats[] = { 
 		                        { 0, 0, 0, 
 		                          //"HM12 (YUV 4:1:1)", V4L2_PIX_FMT_HM12, 
 		                          "NV12 (YUV 4:1:1)", V4L2_PIX_FMT_NV12, 
 		                          { 0, 0, 0, 0 } 
 		                        }, 
 		                        { 1, 0, V4L2_FMT_FLAG_COMPRESSED, 
 		                          "MPEG", V4L2_PIX_FMT_MPEG, 
 		                          { 0, 0, 0, 0 } 
 		                        } 
 		                }; 
 		                struct v4l2_fmtdesc *fmt = arg; 
 		                enum v4l2_buf_type type = fmt->type; 
 		 
 		                switch (type) { 
 		                case V4L2_BUF_TYPE_VIDEO_CAPTURE: 
 		                        break; 
 		                default: 
 		                        return -EINVAL; 
 		                } 
 		                if (fmt->index > 1) 
 		                        return -EINVAL; 
 		                *fmt = formats[fmt->index]; 
 		                fmt->type = type; 
 		                return 0; 
 	} 

	case VIDIOC_TRY_FMT:
	case VIDIOC_S_FMT: {
		struct v4l2_format *fmt = arg;
		int ret = 0;

                if (cmd == VIDIOC_S_FMT) {
                        IVTV_DEBUG_IOCTL("VIDIOC_S_FMT\n");
                } else {
                        IVTV_DEBUG_IOCTL("VIDIOC_TRY_FMT\n");
                }
                ret = ivtv_try_or_set_fmt(itv, streamtype, fmt, cmd == VIDIOC_S_FMT);
		if (ret == 0) {
			stream->fmt = format_by_fourcc(fmt->fmt.pix.pixelformat);	
			stream->height = fmt->fmt.pix.height;
			stream->width = fmt->fmt.pix.width;
		}
		return ret;
	}

	case VIDIOC_G_FMT: {
		struct v4l2_format *fmt = arg;
		int type = fmt->type;

		IVTV_DEBUG_IOCTL("VIDIOC_G_FMT\n");
		memset(fmt, 0, sizeof(*fmt));
		fmt->type = type;
                return ivtv_get_fmt(itv, streamtype, fmt);
        }

	case VIDIOC_G_INPUT:{
		IVTV_DEBUG_IOCTL("VIDIOC_G_INPUT\n");

		*(int *)arg = itv->active_input;
		break;
	}

	case VIDIOC_S_INPUT:{
		int inp = *(int *)arg;

		IVTV_DEBUG_IOCTL("VIDIOC_S_INPUT\n");

		if (ivtv_get_input(itv, inp) == NULL)
			return -EINVAL;

		if (inp == itv->active_input) {
			IVTV_DEBUG_INFO("Input unchanged\n");
		} else {
			IVTV_DEBUG_INFO(
				   "Changing input from %d to %d\n",
				   itv->active_input, inp);

			itv->active_input = inp;
			/* Set the audio input to whatever is appropriate for the
			   input type. */
			if (ivtv_get_input(itv, inp)->type ==
			    V4L2_INPUT_TYPE_TUNER) {
				itv->audio_input_tv = 0;	/* TV tuner */
                        } else {
                                if (itv->card->type == IVTV_CARD_PVR_150) {
                                        itv->audio_input_tv = 2; /* Line in */
                                } else {
                                        itv->audio_input_tv = 1; /* Line in */
                                }
                        }

			/* prevent others from messing with the streams until
			   we're finished changing inputs. */
			ivtv_mute(itv);

			itv->card->video_dec_func(itv, VIDIOC_S_INPUT, &inp);

			/* Select new audio input */
			ivtv_audio_set_io(itv);
			ivtv_unmute(itv);
		}
		break;
	}

	case VIDIOC_G_OUTPUT:{
		IVTV_DEBUG_IOCTL("VIDIOC_G_OUTPUT\n");

		if (!(itv->v4l2_cap & V4L2_CAP_VIDEO_OUTPUT))
			return -EINVAL;
		*(int *)arg = itv->active_output;
		break;
	}

	case VIDIOC_S_OUTPUT:{
		int outp = *(int *)arg;

		IVTV_DEBUG_IOCTL("VIDIOC_S_OUTPUT\n");

		if (ivtv_get_output(itv, outp) == NULL)
			return -EINVAL;

		if (outp == itv->active_output) {
			IVTV_DEBUG_INFO("Output unchanged\n");
			break;
		}
		IVTV_DEBUG_INFO("Changing output from %d to %d\n",
			   itv->active_output, outp);

		itv->active_output = outp;

		break;
	}

	case VIDIOC_G_FREQUENCY:{
		struct v4l2_frequency *vf = arg;

		IVTV_DEBUG_IOCTL("VIDIOC_G_FREQUENCY\n");

		if (vf->tuner != 0)
			return -EINVAL;
		if (test_bit(IVTV_F_I_RADIO_USER, &itv->i_flags)) {
			vf->type = V4L2_TUNER_RADIO;
#if LINUX_VERSION_CODE >= KERNEL_VERSION(2, 6, 13)
			vf->frequency = itv->freq_radio / 1000;
#else
			vf->frequency = itv->freq_radio;
#endif /* LINUX_VERSION_CODE >= KERNEL_VERSION(2, 6, 13) */
		} else {
			vf->type = V4L2_TUNER_ANALOG_TV;
			vf->frequency = itv->freq_tv;
		}
		memset(vf->reserved, 0, sizeof(vf->reserved));
		break;
	}

	case VIDIOC_S_FREQUENCY:{
		struct v4l2_frequency vf = *(struct v4l2_frequency *)arg;

		IVTV_DEBUG_IOCTL("VIDIOC_S_FREQUENCY\n");

		if (vf.tuner != 0)
			return -EINVAL;

		if (test_bit(IVTV_F_I_RADIO_USER, &itv->i_flags)) {
#if LINUX_VERSION_CODE >= KERNEL_VERSION(2, 6, 13)
                        vf.frequency *= 1000;
#endif /* LINUX_VERSION_CODE >= KERNEL_VERSION(2, 6, 13) */
			itv->freq_radio = vf.frequency;
			vf.type = V4L2_TUNER_RADIO; /* not always set, and needed by IF demod */
		} else {
			itv->freq_tv = vf.frequency;
			vf.type = V4L2_TUNER_ANALOG_TV;
		}

		ivtv_mute(itv);
		IVTV_DEBUG_INFO("v4l2 ioctl: set frequency %d\n", vf.frequency);
                if (vf.type == V4L2_TUNER_ANALOG_TV) {
		        ivtv_tv_tuner(itv, VIDIOC_S_FREQUENCY, &vf);
                }
                else {
		        ivtv_radio_tuner(itv, VIDIOC_S_FREQUENCY, &vf);
                }
		if (itv->options.tda9887 == 0) ivtv_tda9887(itv, VIDIOC_S_FREQUENCY, &vf);
		ivtv_audio_freq_changed(itv);
		ivtv_unmute(itv);
		break;
	}

	case VIDIOC_ENUMSTD:{
		struct v4l2_standard *vs = arg;

		IVTV_DEBUG_IOCTL("VIDIOC_ENUMSTD\n");

		if (vs->index < 0 || vs->index >= ivtv_stds_size)
			return -EINVAL;

		*vs = ivtv_stds[vs->index];
		break;
	}

	case VIDIOC_G_STD:{
		IVTV_DEBUG_IOCTL("VIDIOC_G_STD\n");
		*(v4l2_std_id *) arg = itv->std;
		break;
	}

	case VIDIOC_S_STD:{
		v4l2_std_id std = *(v4l2_std_id *) arg;
		int x;

		IVTV_DEBUG_IOCTL("VIDIOC_S_STD\n");

		for (x = 0; x < ivtv_stds_size; x++) {
			if (ivtv_stds[x].id & std)
				break;
		}
		if (x == ivtv_stds_size)
			return -EINVAL;

		if (ivtv_stds[x].id == itv->std)
			break;

		/* Prevent others from messing around with streams while
		   we change standard. */
		down(&stream->mlock);
		if (test_bit(IVTV_F_I_RADIO_USER, &itv->i_flags) ||
		    atomic_read(&itv->capturing) > 0) {
			/* Switching standard would turn off the radio or mess
			   with already running streams, prevent that by
			   returning EBUSY. */
			up(&stream->mlock);
			return -EBUSY;
		}

		IVTV_DEBUG_INFO(
			   "Switching standard to %s.\n", ivtv_stds[x].name);
		itv->std = ivtv_stds[x].id;
		itv->width = 720;
		if (itv->std & V4L2_STD_525_60) {
                        itv->is_50hz = 0;
                        itv->is_60hz = 1;
			itv->height = 480;
			itv->vbi_count = 12;
			itv->vbi_start[0] = 10;
			itv->vbi_start[1] = 273;
		} else {	/* PAL/SECAM */
                        itv->is_50hz = 1;
                        itv->is_60hz = 0;
			itv->height = 576;
			itv->vbi_count = 18;
			itv->vbi_start[0] = 6;
			itv->vbi_start[1] = 318;
		}
		if (itv->hw_flags & IVTV_HW_CX25840) {
			itv->vbi_sliced_decoder_line_size = itv->is_60hz ? 272 : 284;
		}

		/* Tuner */
		ivtv_tv_tuner(itv, VIDIOC_S_STD, &itv->std);

		/* Tuner Audio */
		ivtv_audio_set_std(itv);

		/* Microtune support */
		if (itv->options.tda9887 > 0) {
			unsigned int c = TDA9887_INTERCARRIER;

			ivtv_tda9887(itv, TDA9887_SET_CONFIG, &c);
		} else if (itv->options.tda9887 == 0) {
			ivtv_tda9887(itv, VIDIOC_S_STD, &itv->std);
		}

		/* Digitizer */
		itv->card->video_dec_func(itv, VIDIOC_S_STD, &itv->std);

		up(&stream->mlock);
		break;
	}

	case VIDIOC_S_TUNER:{	/* Setting tuner can only set audio mode */
		struct v4l2_tuner *vt = arg;

		IVTV_DEBUG_IOCTL("VIDIOC_S_TUNER\n");

		if (vt->index != 0)
			return -EINVAL;

		if (test_bit(IVTV_F_I_RADIO_USER, &itv->i_flags)) {
			itv->audmode_radio = vt->audmode;
		} else {
			itv->audmode_tv = vt->audmode;
		}
		ivtv_audio_set_audmode(itv, vt->audmode);
		break;
	}

	case VIDIOC_G_TUNER:{
		struct v4l2_tuner *vt = arg;

		IVTV_DEBUG_IOCTL("VIDIOC_G_TUNER\n");

		if (vt->index != 0)
			return -EINVAL;

		vt->afc = 0;
		memset(vt->reserved, 0, sizeof(vt->reserved));

		if (test_bit(IVTV_F_I_RADIO_USER, &itv->i_flags)) {
			struct v4l2_tuner sig;

			strcpy(vt->name, "ivtv Radio Tuner");
			vt->type = V4L2_TUNER_RADIO;
			vt->capability = V4L2_TUNER_CAP_STEREO;
			/* Japan:          76.0 MHz -  89.9 MHz
			   Western Europe: 87.5 MHz - 108.0 MHz
			   Russia:         65.0 MHz - 108.0 MHz */
			vt->rangelow = 65 * 16;
			vt->rangehigh = 108 * 16;
			ivtv_radio_tuner(itv, VIDIOC_G_TUNER, &sig);
			vt->signal = sig.signal;
			vt->audmode = itv->audmode_radio;
		} else {
			struct v4l2_tuner sig;

			strcpy(vt->name, "ivtv TV Tuner");
			vt->type = V4L2_TUNER_ANALOG_TV;
			vt->capability =
			    V4L2_TUNER_CAP_NORM | V4L2_TUNER_CAP_STEREO |
			    V4L2_TUNER_CAP_LANG1 | V4L2_TUNER_CAP_LANG2;
			vt->rangelow = 0;
			vt->rangehigh = 0xffffffffUL;

			itv->card->video_dec_func(itv, VIDIOC_G_TUNER, &sig);

			vt->signal = sig.signal;
			vt->audmode = itv->audmode_tv;
		}
		vt->rxsubchans = ivtv_audio_get_rxsubchans(itv, vt);
		break;
	}

        case VIDIOC_G_SLICED_VBI_CAP: {
		struct v4l2_sliced_vbi_cap *cap = arg;
                int set = itv->is_50hz ? V4L2_SLICED_VBI_625 : V4L2_SLICED_VBI_525;
                int f, l;

		IVTV_DEBUG_IOCTL("VIDIOC_G_SLICED_VBI_CAP\n");
                memset(cap, 0, sizeof(*cap));
		if (streamtype == IVTV_ENC_STREAM_TYPE_VBI) {
                        for (f = 0; f < 2; f++) {
                                for (l = 0; l < 24; l++) {
                                        if (valid_service_line(f, l, itv->is_50hz)) {
                                                cap->service_lines[f][l] = set;
                                        }
                                }
                        }
		}
                for (f = 0; f < 2; f++) {
                        for (l = 0; l < 24; l++) {
                                cap->service_set |= cap->service_lines[f][l];
                        }
                }
                break;
        }

        case VIDIOC_G_FBUF:
        {
		struct v4l2_framebuffer *fb = arg;

		IVTV_DEBUG_IOCTL("VIDIOC_G_FBUF\n");
		memset(fb, 0, sizeof(*fb));
                break;
        }

        case VIDIOC_LOG_STATUS:
                IVTV_INFO("=================  START STATUS CARD #%d  =================\n", itv->num);
                if (itv->hw_flags & IVTV_HW_TVEEPROM) {
//			struct tveeprom tv;

//			ivtv_read_eeprom(itv, &tv);
                }
                if (itv->hw_flags & IVTV_HW_CX25840) {
                        itv->card->video_dec_func(itv, VIDIOC_LOG_STATUS, 0);
                }
                if (itv->hw_flags & IVTV_HW_WM8775) {
                        ivtv_wm8775(itv, VIDIOC_LOG_STATUS, 0);
                }
                if (itv->hw_flags & IVTV_HW_TDA9887) {
                        ivtv_tda9887(itv, VIDIOC_LOG_STATUS, 0);
                }
                IVTV_INFO("==================  END STATUS CARD #%d  ==================\n", itv->num);
                break;

	default:
		IVTV_DEBUG_WARN("unknown VIDIOC command %08x\n", cmd);
		return -EINVAL;
	}
	return 0;
}

int ivtv_ivtv_ioctls(struct ivtv *itv, struct ivtv_open_id *id,
		     int streamtype, unsigned int cmd, void *arg)
{
	//struct ivtv_stream *stream = &itv->streams[streamtype];

	switch (cmd) {

	case IVTV_IOC_S_VBI_EMBED:{
		int is_ntsc = (itv->std & V4L2_STD_NTSC) ? 1 : 0;
                int embed = *(int *)arg;

		IVTV_DEBUG_IOCTL("IVTV_IOC_S_VBI_EMBED\n");
		if (!(itv->v4l2_cap & V4L2_CAP_SLICED_VBI_CAPTURE))
			return -EINVAL;
                if (embed != 0 && embed != 1)
                        return -EINVAL;
		if (atomic_read(&itv->capturing) > 0)
			return -EBUSY;

		itv->vbi_insert_mpeg = embed;

		if (itv->vbi_insert_mpeg == 0) {
                        break;
                }
		/* Need sliced data for mpeg insertion */
                if (get_service_set(itv->vbi_sliced_in) == 0) {
                        if (is_ntsc)
        			itv->vbi_sliced_in->service_set = V4L2_SLICED_CAPTION_525;
                        else
        			itv->vbi_sliced_in->service_set = V4L2_SLICED_WSS_625;
        		expand_service_set(itv->vbi_sliced_in, !is_ntsc);
                }
		break;
	}

	case IVTV_IOC_G_VBI_EMBED:{
		IVTV_DEBUG_IOCTL("IVTV_IOC_G_VBI_EMBED\n");
		*(int *)arg = itv->vbi_insert_mpeg;
		break;
	}

	case IVTV_IOC_G_CODEC:{
		struct ivtv_ioctl_codec *codec = arg;

		IVTV_DEBUG_IOCTL("IVTV_IOC_G_CODEC\n");
		*codec = itv->codec;
		codec->framerate = itv->codec.framerate;
		codec->framespergop = itv->codec.framespergop;
		break;
	}

	case IVTV_IOC_S_CODEC:{
		struct ivtv_ioctl_codec *codec = arg;
		struct v4l2_format fmt;

		IVTV_DEBUG_IOCTL("IVTV_IOC_S_CODEC\n");
		/* FIXME: insert abundant parameter validation here */
		if ((codec->bitrate == 0) || (codec->bitrate_peak == 0) ||
		    (codec->bitrate > codec->bitrate_peak)) {
			IVTV_DEBUG_WARN("ivtv ioctl: set "
				   "bitrate=%u < peak=%u: failed\n",
				   codec->bitrate, codec->bitrate_peak);
			return -EINVAL;
		}

                /* check if it is an MPEG1 stream */
                if ((codec->stream_type == IVTV_STREAM_MPEG1) ||
                    (codec->stream_type == IVTV_STREAM_VCD)) {
                        /* this is an MPEG1 stream */
                        int tmp_height =
                            (itv->std & V4L2_STD_NTSC) ? 480 : 576;

                        /* set vertical mpeg encoder resolution */
                        if (itv->height != tmp_height / 2) {
                                itv->height = tmp_height / 2;
                        }
                        /* mpeg1 is cbr */
                        codec->bitrate_mode = 1;

                        IVTV_DEBUG_INFO(
                                   "ivtv ioctl: set codec: "
                                   "stream_type is MPEG1 or VCD. resolution %dx%d.\n",
                                   itv->width, itv->height);

                        /* fix videodecoder resolution */
			fmt.type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
                        fmt.fmt.pix.width = itv->width;
                        fmt.fmt.pix.height = tmp_height;
                        if (atomic_read(&itv->capturing) > 0) {
                                IVTV_DEBUG_WARN(
                                        "v4l2 ioctl: set size failed, "
                                        "capture in progress\n");
                        } else {
                                itv->card->video_dec_func(itv, VIDIOC_S_FMT, &fmt);
                        }
                }

                /* Passed the garbage check */
                itv->codec = *codec;

		ivtv_audio_set_audio_clock_freq(itv, codec->audio_bitmask & 0x03);
		break;
	}

	case IVTV_IOC_S_GOP_END:{
		IVTV_DEBUG_IOCTL("IVTV_IOC_S_GOP_END\n");
		itv->end_gop = *(int *)arg;
		*(int *)arg = itv->end_gop;
		break;
	}

	case IVTV_IOC_PAUSE_ENCODE:{
		IVTV_DEBUG_IOCTL("IVTV_IOC_PAUSE_ENCODE\n");
		if (!atomic_read(&itv->capturing))
			return 0;
		ivtv_vapi(itv, IVTV_API_PAUSE_ENCODER, 1);
		ivtv_mute(itv);
		break;
	}

	case IVTV_IOC_RESUME_ENCODE:{
		IVTV_DEBUG_IOCTL("IVTV_IOC_RESUME_ENCODE\n");
		if (!atomic_read(&itv->capturing))
			return 0;
		ivtv_unmute(itv);
		ivtv_vapi(itv, IVTV_API_PAUSE_ENCODER, 0);
		break;
	}
	default:
		IVTV_DEBUG_WARN("unknown IVTV command %08x\n", cmd);
		return -EINVAL;
	}
	return 0;
}

static int ivtv_v4l2_do_ioctl(struct inode *inode, struct file *filp,
			      unsigned int cmd, void *arg)
{
	struct ivtv_open_id *id = (struct ivtv_open_id *)filp->private_data;
	struct ivtv *itv = id->itv;
	int streamtype = id->type;

	IVTV_DEBUG_IOCTL("v4l2 ioctl 0x%08x\n", cmd);

	switch (cmd) {
	case IVTV_IOC_RELOAD_FW:
	case IVTV_IOC_G_ITVC_REG:
	case IVTV_IOC_S_ITVC_REG:
	case IVTV_IOC_G_DECODER_REG:
	case IVTV_IOC_S_DECODER_REG:
	case IVTV_IOC_ZCOUNT:
	case IVTV_IOC_S_MSP_MATRIX:
	case IVTV_IOC_G_DEBUG_LEVEL:
	case IVTV_IOC_S_DEBUG_LEVEL:
	case IVTV_IOC_FWAPI:
		return ivtv_internal_ioctls(itv, streamtype, cmd, arg);

	case VIDIOC_QUERYCAP:
	case VIDIOC_ENUMINPUT:
	case VIDIOC_G_INPUT:
	case VIDIOC_S_INPUT:
	case VIDIOC_ENUMOUTPUT:
	case VIDIOC_QUERYBUF:
	case VIDIOC_REQBUFS:
	case VIDIOC_QBUF:
	case VIDIOC_DQBUF:
	case VIDIOC_STREAMON:
	case VIDIOC_STREAMOFF:
	case VIDIOC_G_OUTPUT:
	case VIDIOC_S_OUTPUT:
	case VIDIOC_G_FMT:
	case VIDIOC_S_FMT:
	case VIDIOC_TRY_FMT:
	case VIDIOC_ENUM_FMT:
	case VIDIOC_G_FREQUENCY:
	case VIDIOC_S_FREQUENCY:
	case VIDIOC_ENUMSTD:
	case VIDIOC_G_STD:
	case VIDIOC_S_STD:
	case VIDIOC_S_TUNER:
	case VIDIOC_G_TUNER:
	case VIDIOC_ENUMAUDIO:
	case VIDIOC_S_AUDIO:
	case VIDIOC_G_AUDIO:
	case VIDIOC_ENUMAUDOUT:
	case VIDIOC_S_AUDOUT:
	case VIDIOC_G_AUDOUT:
        case VIDIOC_G_SLICED_VBI_CAP:
        case VIDIOC_G_FBUF:
	case VIDIOC_LOG_STATUS:
		return ivtv_v4l2_ioctls(filp, itv, id, streamtype, cmd, arg);

	case VIDIOC_QUERYMENU:
	case VIDIOC_QUERYCTRL:
	case VIDIOC_S_CTRL:
	case VIDIOC_G_CTRL:
		return ivtv_control_ioctls(itv, cmd, arg);

	case IVTV_IOC_S_VBI_EMBED:
	case IVTV_IOC_G_VBI_EMBED:
	case IVTV_IOC_G_CODEC:
	case IVTV_IOC_S_CODEC:
	case IVTV_IOC_S_GOP_END:
	case IVTV_IOC_PAUSE_ENCODE:
	case IVTV_IOC_RESUME_ENCODE:
                return ivtv_ivtv_ioctls(itv, id, streamtype, cmd, arg);

	case 0x00005401:	/* Handle isatty() calls */
		return -EINVAL;
	default:
		return ivtv_compat_translate_ioctl(inode, filp, cmd, arg,
						   ivtv_v4l2_do_ioctl);
	}
	return 0;
}

int ivtv_v4l2_ioctl(struct inode *inode, struct file *filp, unsigned int cmd,
		    unsigned long arg)
{
        struct ivtv_open_id *id = (struct ivtv_open_id *)filp->private_data;
        struct ivtv *itv = id->itv;
	int streamtype = id->type;

	/* Converts old (0.1.9) non-conforming ioctls that were using
	   'just some values I picked for now'. I hoped this would not be
	   necessary, but too many people were already using existing apps
	   (MythTV) written for this version of the driver. */
	switch (cmd) {
	case 0xFFEE7703:
		cmd = IVTV_IOC_G_CODEC;
		break;
	case 0xFFEE7704:
		cmd = IVTV_IOC_S_CODEC;
		break;
	case IVTV_IOC_G_DRIVER_INFO:{
		// AEW - put here becuase we can't do this after video_usercopy
		// has done its work of copying a tiny portion of what we really
		// need.
		struct ivtv_driver_info driver_info;
		uint32_t *driver_info_size_ptr = (uint32_t *) arg;
		uint32_t driver_info_size = 0;

		IVTV_DEBUG_IOCTL("IVTV_IOC_G_DRIVER_INFO\n");
		if (driver_info_size_ptr == NULL) {
			IVTV_DEBUG_WARN(
				   "Error: IVTV_IOC_G_DRIVER_INFO called with "
				   "invalid ivtv_driver_info address.\n");
			return -EINVAL;
		}

                memset(&driver_info, 0,
                       sizeof(struct ivtv_driver_info));
                get_user(driver_info_size, driver_info_size_ptr);
                if (driver_info_size > sizeof(struct ivtv_driver_info))
                        driver_info_size = sizeof(struct ivtv_driver_info);

                switch (driver_info_size) {
                case IVTV_DRIVER_INFO_V3_SIZE:
                        driver_info.hw_flags = itv->hw_flags;
                        /* fall through */

                case IVTV_DRIVER_INFO_V2_SIZE:
                        driver_info.cardnr = itv->num;
                        /* fall through */

                case IVTV_DRIVER_INFO_V1_SIZE:
                        driver_info.size = driver_info_size;
                        driver_info.version =
                            IVTV_VERSION_NUMBER(IVTV_VERSION_INFO_NAME);
                        strncpy(driver_info.comment,
                                IVTV_VERSION_COMMENT
                                (IVTV_VERSION_INFO_NAME),
                                IVTV_DRIVER_INFO_MAX_COMMENT_LENGTH);
                        driver_info.
                            comment[IVTV_DRIVER_INFO_MAX_COMMENT_LENGTH
                                    - 1] = '\0';
                        break;
                
                default:
                        IVTV_DEBUG_WARN(
                                   "Error: Unknown size "
                                   "(0x%08x) passed to IVTV_IOC_G_DRIVER_INFO.\n",
                                   driver_info_size);
                        return -EINVAL;
                }

                if (copy_to_user((struct ivtv_driver_info *)arg,
                                 &driver_info,
                                 driver_info_size)) {
                        IVTV_DEBUG_WARN(
                                   "Error: IVTV_IOC_G_DRIVER_INFO Unable "
                                   "to copy data to user space.\n");
                        return -EINVAL;
                }
		return 0;
	}

	case IVTV_IOC_G_STREAM_INFO:{
		// AEW - put here because we can't do this after video_usercopy
		// has done its work of copying a tiny portion of what we really
		// need.
		struct ivtv_stream_info stream_info;
		uint32_t *stream_info_size_ptr = (uint32_t *) arg;
		uint32_t stream_info_size = 0;

		IVTV_DEBUG_IOCTL("IVTV_IOC_G_STREAM_INFO\n");
		if (stream_info_size_ptr == NULL) {
			IVTV_DEBUG_WARN(
				   "Error: IVTV_IOC_G_STREAM_INFO called with "
				   "invalid ivtv_stream_info address.\n");
			return -EINVAL;
		}

                memset(&stream_info, 0,
                       sizeof(struct ivtv_stream_info));
                get_user(stream_info_size, stream_info_size_ptr);
                if (stream_info_size > sizeof(struct ivtv_stream_info))
                        stream_info_size = sizeof(struct ivtv_stream_info);

                switch (stream_info_size) {
                case IVTV_STREAM_INFO_V1_SIZE:
                        stream_info.size = stream_info_size;
                        stream_info.type = streamtype;
                        break;
                
                default:
                        IVTV_DEBUG_WARN(
                                   "Error: Unknown size "
                                   "(0x%08x) passed to IVTV_IOC_G_STREAM_INFO.\n",
                                   stream_info_size);
                        return -EINVAL;
                }

                if (copy_to_user((struct ivtv_stream_info *)arg,
                                 &stream_info,
                                 stream_info_size)) {
                        IVTV_DEBUG_WARN(
                                   "Error: IVTV_IOC_G_STREAM_INFO Unable "
                                   "to copy data to user space.\n");
                        return -EINVAL;
                }
		return 0;
	}
	}
	return video_usercopy(inode, filp, cmd, arg, ivtv_v4l2_do_ioctl);
}
