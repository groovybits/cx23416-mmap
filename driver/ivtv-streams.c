/*
    init/start/stop/exit stream functions
    Copyright (C) 2003-2004  Kevin Thayer <nufan_wfk at yahoo.com>

    Copyright (C) 2004  Chris Kennedy <c@groovy.org>

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

/* License: GPL
 * Author: Kevin Thayer <nufan_wfk at yahoo dot com>
 *
 * This file will hold API related functions, both internal (firmware api)
 * and external (v4l2, etc)
 *
 * -----
 * MPG600/MPG160 support by  T.Adachi <tadachi@tadachi-net.com>
 *                      and Takeru KOMORIYA<komoriya@paken.org>
 *
 * AVerMedia M179 GPIO info by Chris Pinkham <cpinkham@bc2va.org>
 *                using information provided by Jiun-Kuei Jung @ AVerMedia.
 */

#include "ivtv-driver.h"
#include "ivtv-fileops.h"
#include "ivtv-i2c.h"
#include "ivtv-queue.h"
#include "ivtv-mailbox.h"
#include "ivtv-audio.h"
#include "ivtv-video.h"
#include "ivtv-vbi.h"
#include "ivtv-ioctl.h"
#include "ivtv-irq.h"
#include "ivtv-streams.h"
#include "ivtv-cards.h"
#include "audiochip.h"
#include <linux/smp_lock.h>

#define IVTV_V4L2_MAX_MINOR 15

static struct file_operations ivtv_v4l2_enc_fops = {
      owner:THIS_MODULE,
      read:ivtv_v4l2_read,
      write:ivtv_v4l2_write,
      open:ivtv_v4l2_open,
      ioctl:ivtv_v4l2_ioctl,
      release:ivtv_v4l2_close,
      poll:ivtv_v4l2_enc_poll,
      mmap:ivtv_mmap,
};

static struct video_device ivtv_v4l2dev_template = {
	.name = "cx2341x",
	.type = VID_TYPE_CAPTURE | VID_TYPE_TUNER | VID_TYPE_TELETEXT |
	    VID_TYPE_CLIPPING | VID_TYPE_SCALES | VID_TYPE_MPEG_ENCODER,
	.fops = &ivtv_v4l2_enc_fops,
	.minor = -1,
};

static int ivtv_stream_init(struct ivtv *itv, int streamtype,
		     int buffers, int bufsize, int dma)
{
	struct ivtv_stream *s = &itv->streams[streamtype];
	u32 ysize, uvsize;
	int SGsize, SGsizeAlign;

	s->dev = itv->dev;
	s->buftype = 0;
	s->count = 0;
	s->first_read = 1;
        s->trans_id = 0;
	s->streaming = 0;
	s->state = 0;

	s->fmt = format_by_fourcc(V4L2_PIX_FMT_NV12);
	s->height = itv->height;
	s->width = itv->width;
	s->field = V4L2_FIELD_INTERLACED;

 	s->timeout.function = ivtv_timeout;
        s->timeout.data     = (unsigned long)s;
        init_timer(&s->timeout);

	/* Translate streamtype to buffers limit */
	if (streamtype == IVTV_ENC_STREAM_TYPE_MPG) {
		bufsize = 128*1024;

		s->fmt = format_by_fourcc(V4L2_PIX_FMT_MPEG);
		s->buftype = V4L2_BUF_TYPE_VIDEO_CAPTURE;
		s->field = V4L2_FIELD_INTERLACED;
	} else if (streamtype == IVTV_ENC_STREAM_TYPE_YUV) {
		// SG Array for DMA Xfers
		ysize = s->width*s->height;
		ysize = ((ysize+(PAGE_SIZE-1))&PAGE_MASK);
		uvsize = s->width*(s->height/2);
		uvsize = ((uvsize+(PAGE_SIZE-1))&PAGE_MASK);
		bufsize = ysize + uvsize;

		s->fmt = format_by_fourcc(V4L2_PIX_FMT_NV12);

		s->buftype = V4L2_BUF_TYPE_VIDEO_CAPTURE;
	} else if (streamtype == IVTV_ENC_STREAM_TYPE_VBI) {
		s->buftype = V4L2_BUF_TYPE_VBI_CAPTURE;
		//s->buftype = V4L2_BUF_TYPE_SLICED_VBI_CAPTURE;
		s->field = V4L2_FIELD_SEQ_TB;
	} else if (streamtype == IVTV_ENC_STREAM_TYPE_PCM) {
		s->buftype = V4L2_BUF_TYPE_VIDEO_CAPTURE;
	} else if (streamtype == IVTV_ENC_STREAM_TYPE_RAD) {
		s->buftype = V4L2_BUF_TYPE_VIDEO_CAPTURE;
	} else {
		IVTV_DEBUG_WARN(
			   "Stream Init: Unknown stream: %d\n", streamtype);
		return -EIO;
	}
	s->buf_min = 2;
	s->buf_max = 32;

	SGsize = ((bufsize+PAGE_SIZE-1)&PAGE_MASK) / PAGE_SIZE;

	if (streamtype == IVTV_ENC_STREAM_TYPE_YUV)
		bufsize = ((bufsize+PAGE_SIZE-1)&PAGE_MASK);

	IVTV_INFO("Create %s%s stream: "
		"%d x %d buffers of (%d/4096) pages (%dKB total)\n",
		dma != PCI_DMA_NONE ? "DMA " : "",
		ivtv_stream_name(streamtype), 
		(int)s->buf_max, (int)bufsize, SGsize, (s->buf_max * bufsize));

	// SG Array Allocation
	SGsizeAlign = (sizeof(struct ivtv_SG_element) * SGsize);

       	s->SGarray = (struct ivtv_SG_element *) kmalloc(SGsizeAlign, 
		GFP_KERNEL);

       	if (s->SGarray == NULL) {
               	IVTV_ERR("Could not allocate SGarray\n");
               	return -ENOMEM;
        }
       	/* Set SGarray to all 0's */
       	memset(s->SGarray, 0, SGsizeAlign);

	/* Make it easier to know what type it is */
	s->type = streamtype;

	/* Step 2: initialize ivtv_stream fields */
	init_waitqueue_head(&s->waitq);
	init_MUTEX(&s->mlock);

	s->slock = SPIN_LOCK_UNLOCKED;
	spin_lock_init(&s->slock);

	// Dma Hardware Status and Xfer Information
 	s->dma_info.id		= 0;
        s->dma_info.type       = 0;
        s->dma_info.status     = 0;
        s->dma_info.pts_stamp  = 0;
        s->dma_info.done       = 0;
 	s->dma_req.id		= 0;
        s->dma_req.type             = 0;
        s->dma_req.offset           = 0;
        s->dma_req.size             = 0;
        s->dma_req.UVoffset         = 0;
        s->dma_req.UVsize           = 0;
        s->dma_req.pts_stamp        = 0;
        s->dma_req.bytes_needed     = 0;
        s->dma_req.done             = 0x01;

	s->dma = dma;
	s->id = -1;
	s->SG_handle = IVTV_DMA_UNMAPPED;
	s->SG_length = 0;
	s->buffers = buffers;
	s->bufsize = bufsize;
	s->buf_total = 0;
	s->buf_fill = 0;
	s->dmatype = 0;
	atomic_set(&s->allocated_buffers, 0);

	INIT_LIST_HEAD(&s->active);
	INIT_LIST_HEAD(&s->queued);

	return 0;
}

static int ivtv_reg_dev(struct ivtv *itv, int streamtype, int minor, int reg_type)
{
	struct ivtv_stream *s = &itv->streams[streamtype];

	if (minor > -1)
		minor += ivtv_first_minor;	/* skip any other TV cards */

	/* Step 1: allocate and initialize the v4l2 video device structure */
	s->v4l2dev = video_device_alloc();
	if (NULL == s->v4l2dev) {
		IVTV_ERR("Couldn't allocate v4l2 video_device\n");
		return -ENOMEM;
	}
	memcpy(s->v4l2dev, &ivtv_v4l2dev_template, sizeof(struct video_device));
	if (itv->v4l2_cap & V4L2_CAP_VIDEO_OUTPUT) {
		s->v4l2dev->type |= VID_TYPE_MPEG_DECODER;
	}
	snprintf(s->v4l2dev->name, sizeof(s->v4l2dev->name), "ivtv%d %s",
			itv->num, ivtv_stream_name(streamtype));

	s->v4l2dev->minor = minor;
#ifdef LINUX26
	s->v4l2dev->dev = &itv->dev->dev;
#endif /* LINUX26 */
	s->v4l2dev->release = video_device_release;

	/* We're done if this is a 'hidden' stream (OSD) */
	if (minor < 0)
		return 0;

	/* Step 3: register device. First try the desired minor, 
	   then any free one. */
	if (video_register_device(s->v4l2dev, reg_type, minor) &&
	    video_register_device(s->v4l2dev, reg_type, -1)) {
		IVTV_ERR("Couldn't register v4l2 device for %s minor %d\n",
			      ivtv_stream_name(streamtype), minor);

		video_device_release(s->v4l2dev);
		s->v4l2dev = NULL;

		return -ENOMEM;
	}

	IVTV_DEBUG_INFO("Registered v4l2 device for %s minor %d\n",
		       ivtv_stream_name(streamtype), s->v4l2dev->minor);

	/* Success! All done. */

	return 0;
}

static int ivtv_dev_setup(struct ivtv *itv, int s)
{
	switch (s) {
		case IVTV_ENC_STREAM_TYPE_MPG:
			return ivtv_reg_dev(itv, s, itv->num, VFL_TYPE_GRABBER);
		case IVTV_ENC_STREAM_TYPE_YUV:
			return ivtv_reg_dev(itv, s, itv->num + IVTV_V4L2_ENC_YUV_OFFSET,
				 VFL_TYPE_GRABBER);
		case IVTV_ENC_STREAM_TYPE_VBI:
			return ivtv_reg_dev(itv, s, itv->num, VFL_TYPE_VBI);
		case IVTV_ENC_STREAM_TYPE_PCM:
			return ivtv_reg_dev(itv, s, itv->num + IVTV_V4L2_ENC_PCM_OFFSET,
				 VFL_TYPE_GRABBER);
		case IVTV_ENC_STREAM_TYPE_RAD:
			if (!(itv->v4l2_cap & V4L2_CAP_RADIO))
				return 0;
			return ivtv_reg_dev(itv, s, itv->num, VFL_TYPE_RADIO);
	}
	return 0;
}

static int ivtv_stream_setup(struct ivtv *itv, int x)
{
	int ret = 0;
	if (x == IVTV_ENC_STREAM_TYPE_MPG) {
		if (ivtv_stream_init(itv, x, 0,
				     itv->dma_cfg.enc_buf_size,
				     PCI_DMA_FROMDEVICE)) {
			return -ENOMEM;
		}
	} else if (x == IVTV_ENC_STREAM_TYPE_YUV) {
		if (ivtv_stream_init(itv, x, 0,
				     itv->dma_cfg.enc_yuv_buf_size,
				     PCI_DMA_FROMDEVICE)) {
			return -ENOMEM;
		}
	} else if (x == IVTV_ENC_STREAM_TYPE_VBI) {
		if (ivtv_stream_init(itv, x, 0,
				     itv->dma_cfg.vbi_buf_size, 
					PCI_DMA_FROMDEVICE)) {
			return -ENOMEM;
		}
	} else if (x == IVTV_ENC_STREAM_TYPE_PCM) {
		if (ivtv_stream_init(itv, x, 0,
				     itv->dma_cfg.enc_pcm_buf_size,
				     PCI_DMA_FROMDEVICE)) {
			return -ENOMEM;
		}
	} else if ((itv->v4l2_cap & V4L2_CAP_RADIO) &&
		   (x == IVTV_ENC_STREAM_TYPE_RAD)) {
		if (ivtv_stream_init(itv, x, 0, 0, PCI_DMA_NONE)) {
			return -ENOMEM;
		}
	}
	return ret;
}

/* Initialize v4l2 variables and register v4l2 device */
int ivtv_streams_setup(struct ivtv *itv)
{
	int x;

	IVTV_DEBUG_INFO("v4l2 streams setup\n");

	itv->streamcount = 4;
	if (itv->v4l2_cap & V4L2_CAP_RADIO)
		itv->streamcount++;

	/* fill in appropriate v4l2 device */
	IVTV_DEBUG_INFO("Configuring %s card with %d streams\n",
		       itv->card_name, itv->streamcount);

	/* Allocate streams */
	itv->streams = (struct ivtv_stream *)
	    kmalloc(itv->streamcount * sizeof(struct ivtv_stream), GFP_KERNEL);
	if (NULL == itv->streams) {
		IVTV_ERR("Couldn't allocate v4l2 streams\n");
		return -ENOMEM;
	}
	memset(itv->streams, 0, itv->streamcount * sizeof(struct ivtv_stream));

	/* Setup V4L2 Devices */
	for (x = 0; x < itv->streamcount; x++) {
		/* Register Device */
		if (ivtv_dev_setup(itv, x))
			break;

		/* Setup Stream */
		if (ivtv_stream_setup(itv, x))
			break;
	}
	if (x == itv->streamcount) {
		atomic_set(&itv->streams_setup, 1);
		return 0;
	}

	/* One or more streams could not be initialized. Clean 'em all up. */
	ivtv_streams_cleanup(itv);
	return -ENOMEM;
}

static struct ivtv_stream *ivtv_stream_safeget(const char *desc, 
		struct ivtv *itv, int type)
{
	if (type >= itv->streamcount) {
		IVTV_DEBUG_WARN(
			"%s accessing uninitialied %s stream\n",
			desc, ivtv_stream_name(type));
		return NULL;
	}

	return &itv->streams[type];
}

static void ivtv_unreg_dev(struct ivtv *itv, int stream)
{
	struct ivtv_stream *s = ivtv_stream_safeget(
		"ivtv_unreg_dev", itv, stream);
	if (s == NULL)
		return;

	if (s->v4l2dev == NULL)
		return;

	/* Free Device */
	if (s->v4l2dev->minor == -1) {
		// 'Hidden' never registered stream (OSD)
		video_device_release(s->v4l2dev);
	} else {
		// All others, just unregister.
		video_unregister_device(s->v4l2dev);
	}
	return;
}

void ivtv_streams_cleanup(struct ivtv *itv)
{
	int x;

	/* Teardown all streams */
	for (x = 0; x < itv->streamcount; x++) {
		ivtv_stream_free(itv, x);
		ivtv_unreg_dev(itv, x);
	}

	if (itv->streams)
		kfree(itv->streams);
	itv->streams = NULL;

	atomic_set(&itv->streams_setup, 0);
}

void ivtv_setup_v4l2_encode_stream(struct ivtv *itv, int type)
{
        int dnr_temporal;
	struct ivtv_stream *s = ivtv_stream_safeget(
		"ivtv_setup_v4l2_encode_stream", itv, type);

	if (s == NULL)
		return;

	/* assign stream type */
	ivtv_vapi(itv, IVTV_API_ASSIGN_STREAM_TYPE, 1, itv->codec.stream_type);

	/* assign output port */
	ivtv_vapi(itv, IVTV_API_ASSIGN_OUTPUT_PORT, 1, 0);	/* 0 = Memory */

	/* assign framerate */
	ivtv_vapi(itv, IVTV_API_ASSIGN_FRAMERATE, 1,
		  (itv->std & V4L2_STD_625_50) ? 1 : 0);

	/* assign frame size */
	ivtv_vapi(itv, IVTV_API_ASSIGN_FRAME_SIZE, 2, itv->height, itv->width);

	/* assign aspect ratio */
	ivtv_vapi(itv, IVTV_API_ASSIGN_ASPECT_RATIO, 1, itv->codec.aspect);

	/* automatically calculate bitrate on the fly */
	if (itv->codec.bitrate > 15000000) {
		IVTV_DEBUG_WARN(
			   "Bitrate too high, adjusted 15mbit, see mpeg specs\n");
		itv->codec.bitrate = 15000000;
	}

	/* assign bitrates */
	ivtv_vapi(itv, IVTV_API_ASSIGN_BITRATES, 5, itv->codec.bitrate_mode,
		  itv->codec.bitrate,	/* bps */
		  itv->codec.bitrate_peak / 400,	/* peak/400 */
		  0x00, 0x00, 0x70);	/* encoding buffer, ckennedy */

	/* assign gop properties */
	ivtv_vapi(itv, IVTV_API_ASSIGN_GOP_PROPERTIES, 2,
		  itv->codec.framespergop, itv->codec.bframes);

	/* assign 3 2 pulldown */
	ivtv_vapi(itv, IVTV_API_ASSIGN_3_2_PULLDOWN, 1, itv->codec.pulldown);

	/* assign gop closure */
	ivtv_vapi(itv, IVTV_API_ASSIGN_GOP_CLOSURE, 1, itv->codec.gop_closure);

	/* assign audio properties */
	ivtv_vapi(itv, IVTV_API_ASSIGN_AUDIO_PROPERTIES, 1,
		  itv->codec.audio_bitmask);

	/* assign dnr filter mode */
	ivtv_vapi(itv, IVTV_API_ASSIGN_DNR_FILTER_MODE, 2,
		  itv->codec.dnr_mode, itv->codec.dnr_type);

	/* assign dnr filter props */
        dnr_temporal = itv->codec.dnr_temporal;
        if (itv->width != 720 || 
            (itv->is_50hz && itv->height != 576) ||
            (itv->is_60hz && itv->height != 480)) {
                // dnr_temporal != 0 for scaled images gives ghosting effect.
                // Always set to 0 for scaled images.
                dnr_temporal = 0;
        }
	ivtv_vapi(itv, IVTV_API_ASSIGN_DNR_FILTER_PROPS, 2,
		  itv->codec.dnr_spatial, dnr_temporal);

	/* assign coring levels (luma_h, luma_l, chroma_h, chroma_l) */
	ivtv_vapi(itv, IVTV_API_ASSIGN_CORING_LEVELS, 4, 0, 255, 0, 255);

	/* assign spatial filter type: luma_t: 1 = horiz_only, chroma_t: 1 = horiz_only */
	ivtv_vapi(itv, IVTV_API_ASSIGN_SPATIAL_FILTER_TYPE, 2, 1, 1);

	/* assign frame drop rate */
	ivtv_vapi(itv, IVTV_API_ASSIGN_FRAME_DROP_RATE, 1, 0);
}

void ivtv_vbi_setup(struct ivtv *itv, int mode)
{
	int raw = itv->vbi_sliced_in->service_set == 0;
	u32 data[IVTV_MBOX_MAX_DATA], result;
	int lines;
	int x;
	int i;
	int h = (itv->std & V4L2_STD_NTSC) ? 480 : 576;

	/* Reset VBI */
	ivtv_vapi(itv, IVTV_API_SELECT_VBI_LINE, 5, 0x0fffffff , 0, 0, 0, 0);

	/* Don't use VBI if scaling */
	if (raw && itv->height != h) {
		itv->card->video_dec_func(itv, VIDIOC_S_FMT, &itv->vbi_in);
		return;
	}

        if (itv->std & V4L2_STD_NTSC) {
                itv->vbi_count = 12;
                itv->vbi_start[0] = 10;
                itv->vbi_start[1] = 273;
        } else {        /* PAL/SECAM */
                itv->vbi_count = 18;
                itv->vbi_start[0] = 6;
                itv->vbi_start[1] = 318;
        }

	// setup VBI registers
	itv->card->video_dec_func(itv, VIDIOC_S_FMT, &itv->vbi_in);

	// determine number of lines and total number of VBI bytes.
	// A raw line takes 1443 bytes: 2 * 720 + 4 byte frame header - 1
	// The '- 1' byte is probably an unused U or V byte. Or something...
	// A sliced line takes 51 bytes: 4 byte frame header, 4 byte internal
	// header, 42 data bytes + checksum (to be confirmed) 
	if (raw) {
		lines = itv->vbi_count * 2;
	} else {
		if (mode == 0x00)
			lines = itv->is_60hz ? 2 : 38;
		else // Insertion
			lines = itv->is_60hz ? 24 : 38;
	}

        ivtv_vapi(itv, IVTV_API_ASSIGN_NUM_VSYNC_LINES,
        	2, itv->digitizer, itv->digitizer);

	itv->vbi_enc_size =
	    (lines * (raw ? itv->vbi_raw_size : itv->vbi_sliced_size));

	// Type of VBI packets and Encoder-Insertion
	if (mode == 0x00) {
		if (itv->codec.stream_type > 0x00) // DVD
			data[0] = 0x08 | (0xbd << 8); // User pkt: DVD
		else // Program Stream
			data[0] = 0x0A | (0xbd << 8); // Private pkt: Corruption
	} else // Separate Stream
		data[0] = raw | mode | (0xbd << 8);

	// Every X number of frames a VBI interrupt arrives (frames as in 25 or 30 fps)
	data[1] = 1; 	
	// The VBI frames are stored in a ringbuffer with this size (with a VBI frame as unit)
	if (raw) {
		data[2] = 4;
	} else {
		if (mode == 0x00) { // Insertion
			data[1] = 1;
			data[2] = 1;
		} else {
			data[1] = 4; 	
			data[2] = 40;
		}
	}
	// The start/stop codes determine which VBI lines end up in the raw VBI data area.
	// The codes are from table 24 in the saa7115 datasheet. Each raw/sliced/video line
	// is framed with codes FF0000XX where XX is the SAV/EAV (Start/End of Active Video)
	// code. These values for raw VBI are obtained from a driver disassembly. The sliced
	// start/stop codes was deduced from this, but they do not appear in the driver.
	// Other code pairs that I found are: 0x250E6249/0x13545454 and 0x25256262/0x38137F54.
	// However, I have no idea what these values are for.
        if (itv->card->type == IVTV_CARD_PVR_150)
	{
        	/* Setup VBI for the cx25840 digitizer */
        	if (raw) {
			data[3] = 0x20602060;
			data[4] = 0x30703070;
        	} else {
                        data[3] = 0xB0F0B0F0;
                        data[4] = 0xA0E0A0E0;
        	}
		// Lines per frame
		data[5] = lines;
		// bytes per line
		data[6] = (raw ? itv->vbi_raw_size : itv->vbi_sliced_size);
	}
	data[7] = 0;    // VBI Qualification offset
	data[8] = 0x01; // VBI Type (CC)

	IVTV_DEBUG_INFO(
		"Setup VBI API header 0x%08x pkts %d buffs %d ln %d sz %d\n", 
			data[0], data[1], data[2], data[5], data[6]);

	x = ivtv_api(itv, itv->enc_mbox, &itv->enc_msem, IVTV_API_CONFIG_VBI,
		     &result, 9, &data[0]);
	if (x)
		IVTV_DEBUG_WARN("init error 21. Code %d\n", x);

	// returns the VBI encoder memory area.
	itv->vbi_enc_start = data[2];
	itv->vbi_total_frames = data[1];
	itv->vbi_fpi = data[0];
	if (!itv->vbi_fpi)
		itv->vbi_fpi = 1;
	itv->vbi_index = 0;

	IVTV_DEBUG_INFO("Setup VBI start 0x%08x frames %d fpi %d lines 0x%08x\n",
		itv->vbi_enc_start, itv->vbi_total_frames, itv->vbi_fpi, itv->digitizer); 

	// select VBI lines.
	// Note that the sliced argument seems to have no effect.
	for (i = 2; i <= 24; i++) {
		int valid;

                if (itv->std & V4L2_STD_NTSC) {
			if (mode == 0x00)
		        	valid = i >= 21 && i < 22;
			else
		        	valid = i >= 10 && i < 22;
                } else {
		        valid = i >= 6 && i < 24;
                }
		ivtv_vapi(itv, IVTV_API_SELECT_VBI_LINE, 5, i - 1,
                                valid, 0 , 0, 0);
		ivtv_vapi(itv, IVTV_API_SELECT_VBI_LINE, 5, (i - 1) | 0x80000000,
                                valid, 0, 0, 0);
	}

	// Remaining VBI questions:
	// - Is it possible to select particular VBI lines only for inclusion in the MPEG
	// stream? Currently you can only get the first X lines.
	// - Is mixed raw and sliced VBI possible? 
	// - What's the meaning of the raw/sliced flag?
	// - What's the meaning of params 2, 3 & 4 of the Select VBI command?
}

static void unknown_setup_api(struct ivtv *itv)
{
	ivtv_vapi(itv, IVTV_API_ENC_UNKNOWN, 1, 0); // Turn off Scene Change Detection
	ivtv_vapi(itv, IVTV_API_ENC_MISC, 2, 7, 0);
	ivtv_vapi(itv, IVTV_API_ENC_MISC, 2, 8, 0);
	ivtv_vapi(itv, IVTV_API_ENC_MISC, 2, 4, 1);
	ivtv_vapi(itv, IVTV_API_ENC_MISC, 2, 12, 0);
}

int ivtv_init_digitizer(struct ivtv *itv)
{
        /* initialize or refresh input */
        if (atomic_read(&itv->capturing) == 0)
                ivtv_vapi(itv, IVTV_API_INITIALIZE_INPUT, 0);
        else
                ivtv_vapi(itv, IVTV_API_REFRESH_INPUT, 0);

	// Helps the digitizer sleep some and get ready for the day
	ivtv_sleep_timeout(100/20, 0);
	return 0;
}

int ivtv_start_v4l2_encode_stream(struct ivtv *itv, int type)
{
	u32 data[IVTV_MBOX_MAX_DATA], result;
	int captype = 0, subtype = 0;
	struct ivtv_stream *st = ivtv_stream_safeget(
		"ivtv_start_v4l2_encode_stream", itv, type);

	IVTV_DEBUG_INFO("ivtv start v4l2 stream\n");

	if (st == NULL)
		return -EINVAL;

	down(&itv->streams_lock);
	st->state = 1;

	captype = type;

	switch (type) {
	case IVTV_ENC_STREAM_TYPE_MPG:
		captype = 0;
		subtype = 3;//|0x04;

		set_bit(IVTV_F_T_ENC_VID_STARTED, &itv->t_flags);

		break;

	case IVTV_ENC_STREAM_TYPE_YUV:
		captype = 1;
		subtype = 1|0x10;
		set_bit(IVTV_F_T_ENC_RAWVID_STARTED, &itv->t_flags);

		break;
	case IVTV_ENC_STREAM_TYPE_PCM:
		captype = 1;
		subtype = 2|0x10;
		set_bit(IVTV_F_T_ENC_RAWAUD_STARTED, &itv->t_flags);

		break;
	case IVTV_ENC_STREAM_TYPE_VBI:
		captype = 1;
		subtype = 4|0x10;

                itv->vbi_frame = 0;

		set_bit(IVTV_F_T_ENC_VBI_STARTED, &itv->t_flags);

		break;
	default:
		break;
	}
	st->subtype = subtype;
	st->first_read = 1;
	st->trans_id = 0;
 	st->dma_info.done = 0;
        st->dma_req.done  = 0;
	st->count = 0;


	/* mute/unmute video */
	ivtv_vapi(itv, IVTV_API_MUTE_VIDEO, 1,
		  test_bit(IVTV_F_I_RADIO_USER, &itv->i_flags) ? 1 : 0);

	/* Clear Streamoff flags in case left from last capture */
	clear_bit(IVTV_F_S_STREAMOFF, &st->s_flags);
	clear_bit(IVTV_F_S_OVERFLOW, &st->s_flags);
	clear_bit(IVTV_F_S_DMAP, &st->s_flags);

        if (atomic_read(&itv->capturing) == 0) {
		/*assign dma block size */
		data[0] = FW_ENC_DMA_XFER_SIZE;	
		data[1] = FW_ENC_DMA_XFER_COUNT;	/* 0=bytes, 1=frames */
		ivtv_api(itv, itv->enc_mbox, &itv->enc_msem,
	     		IVTV_API_ASSIGN_DMA_BLOCKLEN, &result, 2, &data[0]);

		if (FW_ENC_DMA_XFER_TYPE == 1) {
			data[0] = FW_ENC_DMA_XFER_TYPE;	
			data[1] = FW_ENC_DMA_XFER_COUNT;	/* 0=bytes, 1=frames */
			ivtv_api(itv, itv->enc_mbox, &itv->enc_msem,
	     			IVTV_API_ASSIGN_DMA_BLOCKLEN, &result, 2, &data[0]);
		}

		/* Stuff from Windows, we don't know what it is */
		unknown_setup_api(itv);

		/* Setup VBI */
        	if ((itv->v4l2_cap & V4L2_CAP_VBI_CAPTURE)) {
                	int pkt_type = 0x02;

       			/* assign placeholder */
       			ivtv_vapi(itv, IVTV_API_ASSIGN_PLACEHOLDER, 12, 
				0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0);

                	/* Raw mode, no private packet */
                	if (itv->vbi_sliced_in->service_set == 0)
                        	pkt_type = 0x00;

                	if (test_bit(IVTV_F_T_ENC_VBI_STARTED, &itv->t_flags) &&
                                !itv->vbi_insert_mpeg) {
                        	ivtv_vbi_setup(itv, 0x04|pkt_type); // Private/User Separate
                	} else if (itv->vbi_insert_mpeg) {
                        	ivtv_vbi_setup(itv, (pkt_type)); // Private Embed

				st->subtype = subtype |= 4;
                	} else {
                        	ivtv_vbi_setup(itv, (0x02|0x04)); // Private Separate
                	}
        	} else {
       			/* assign placeholder */
       			ivtv_vapi(itv, IVTV_API_ASSIGN_PLACEHOLDER, 12, 
				0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0);

			ivtv_vapi(itv, IVTV_API_ASSIGN_NUM_VSYNC_LINES,
	  			2, itv->digitizer, itv->digitizer);
		}

		/*assign program index info. Mask 0: Disable, Num_req: 400 max*/
		data[0] = 0;//itv->idx_sdf_mask;
		data[1] = 0;//itv->idx_sdf_num;
		ivtv_api(itv, itv->enc_mbox, &itv->enc_msem,
	 		IVTV_API_ASSIGN_PGM_INDEX_INFO, &result, 2, &data[0]);
		itv->idx_sdf_offset = data[0];
		itv->idx_sdf_num = data[1];

		IVTV_DEBUG_INFO(
	   		"ENC: PGM Index at 0x%08x with 0x%08x elements\n",
	   		itv->idx_sdf_offset, itv->idx_sdf_num);

		/* Setup API for Stream */
		ivtv_setup_v4l2_encode_stream(itv, type);
	}

	if (atomic_read(&itv->capturing) == 0)
		ivtv_vapi(itv, IVTV_API_ENC_MISC, 2, 3, itv->has_cx25840);

	/* Check if capturing already and trying VBI capture */
	if (type == IVTV_ENC_STREAM_TYPE_VBI && atomic_read(&itv->capturing)) {
		IVTV_DEBUG_WARN(
			"Starting VBI after starting an encoding, seems to not work.\n"); 

		/*if (test_bit(IVTV_F_T_ENC_VID_STARTED, &itv->t_flags)
			subtype |= 0x03;

		if (test_bit(IVTV_F_T_ENC_RAWVID_STARTED, &itv->t_flags)
			subtype |= 0x01|0x10;

		if (test_bit(IVTV_F_T_ENC_RAWAUD_STARTED, &itv->t_flags)
			subtype |= 0x02|0x10;*/
	}

	// Start VBI if hasn't been started and MPEG is chosen
	if (type == IVTV_ENC_STREAM_TYPE_MPG && !test_bit(IVTV_F_T_ENC_VBI_STARTED, &itv->t_flags))
		subtype |= 0x04;
	else if (type == IVTV_ENC_STREAM_TYPE_YUV && !test_bit(IVTV_F_T_ENC_VBI_STARTED, &itv->t_flags))
		subtype |= 0x04;

        if (atomic_read(&itv->capturing) == 0) {
        	/* Clear all Pending Interrupts */
        	ivtv_set_irq_mask(itv, IVTV_IRQ_MASK_CAPTURE);

        	clear_bit(IVTV_F_I_EOS, &itv->i_flags);

		/* Initialize Digitizer for Capture */
		ivtv_init_digitizer(itv);	

        	/*IVTV_DEBUG_INFO("Sleeping for 100ms\n"); */
        	ivtv_sleep_timeout(HZ / 10, 0);

   		atomic_set(&itv->r_intr, 0);
        	atomic_set(&itv->w_intr, 0);

		clear_bit(IVTV_F_T_ENC_DMA_QUEUED, &itv->t_flags);
        }

	/* begin_capture */
	if (ivtv_vapi(itv, IVTV_API_BEGIN_CAPTURE, 2, captype, subtype)) 
	{
		IVTV_DEBUG_WARN( "Error starting capture!\n");
		up(&itv->streams_lock);
		return -EINVAL;
	}

	// IRQ mask set
	ivtv_clear_irq_mask(itv, IVTV_IRQ_MASK_CAPTURE);

	/* you're live! sit back and await interrupts :) */
	atomic_inc(&itv->capturing);
	up(&itv->streams_lock);

	return 0;
}

int ivtv_stop_all_captures(struct ivtv *itv)
{
	int x;

	for (x = itv->streamcount-1; x >= 0; x--) {
 		/* stop streaming capture */
		if (test_bit(IVTV_F_S_CAPTURING, &itv->streams[x].s_flags)) {
 			/* stop video capture */
			ivtv_stop_capture(itv, x);
		}
	}

	return 0;
}

int ivtv_stop_capture(struct ivtv *itv, int type)
{
	DECLARE_WAITQUEUE(wait, current);
	struct ivtv_stream *st = ivtv_stream_safeget(
		"ivtv_stop_capture", itv, type);
	int cap_type;
	unsigned long then;
	int x;
	int stopmode;

	if (st == NULL)
		return -EINVAL;

	IVTV_DEBUG_INFO("Stop Capture\n");

	if (atomic_read(&itv->capturing) == 0)
		return 0;

	down(&itv->streams_lock);

	switch (type) {
	case IVTV_ENC_STREAM_TYPE_YUV:
		cap_type = 1;
		break;
	case IVTV_ENC_STREAM_TYPE_PCM:
		cap_type = 1;
		break;
	case IVTV_ENC_STREAM_TYPE_VBI:
		cap_type = 1;
		break;
	case IVTV_ENC_STREAM_TYPE_MPG:
	default:
		cap_type = 0;
		break;
	}

	/* Stop Capture Mode */
	stopmode = 1;// Always Stop Fast

	/* end_capture */
	x = ivtv_vapi(itv, IVTV_API_END_CAPTURE, 3, stopmode,	
		/* when: 0 =  end of GOP  1 = NOW! */
		      cap_type,	/* type: 0 = mpeg */
		      st->subtype);	/* subtype: 3 = video+audio */
	if (x)
		IVTV_DEBUG_WARN("ENC: Failed stopping capture %d\n",
			   x);

 	/* only run these if we're shutting down the last cap */
        if (atomic_read(&itv->capturing) - 1 == 0) {
                ivtv_vapi(itv, IVTV_API_ENC_MISC, 2, 3, 0);
        }

	// Make sure all DMA xfers are Done
	then = jiffies;
	if (!test_bit(IVTV_F_S_NO_DMA, &st->s_flags)) {
		then = jiffies;
		/* Make sure DMA is complete */
		add_wait_queue(&st->waitq, &wait);
		do {
			set_current_state(TASK_INTERRUPTIBLE);

			schedule_timeout(HZ / 100);

			/* check if DMA is pending */
			if (!test_bit(IVTV_F_S_DMAP, &st->s_flags)) {
				break;
			}

		} while (((then + (HZ * 1)) > jiffies));
		set_current_state(TASK_RUNNING);
		remove_wait_queue(&st->waitq, &wait);

		if (test_bit(IVTV_F_S_DMAP, &st->s_flags)) {
			u32 stat;
			u32 done_intr = (IVTV_IRQ_DMA_WRITE|IVTV_IRQ_ENC_DMA_COMPLETE);

			// Read Interrupts Pending, Clear DMA Done ones
 			stat = readl(itv->reg_mem + IVTV_REG_IRQSTATUS);

			IVTV_DEBUG_WARN(
			   "ENC: DMA (0x%08x) still Pending as 0x%08x stopcap for stream %d with SG Size %d dma_info.done=%d dma_reg.done=%d!\n",
				done_intr, stat, st->type, st->SG_length, st->dma_info.done, st->dma_req.done);
			// Try to stop stream nicely
                	if (st->SG_length > 0) {
                		unsigned long flags;

				IVTV_DEBUG_WARN(
				   "ENC: DMA Attempting to stop DMA for stream %d\n", st->type);

                		st->dma_info.done = 0x00; 
                		st->dma_req.done  = 0x00;

				// Run DMA Done 
                		spin_lock_irqsave(&itv->DMA_slock, flags);
                        	ivtv_FROM_DMA_done(itv, st->type);
                		spin_unlock_irqrestore(&itv->DMA_slock, flags);
			}

			// Read Interrupts Pending, Clear DMA Done ones
 			stat = readl(itv->reg_mem + IVTV_REG_IRQSTATUS);

			IVTV_DEBUG_WARN(
			   "ENC: DMA (0x%08x) now 0x%08x stopcap for stream %d with SG Size %d dma_info.done=%d dma_reg.done=%d!\n",
				done_intr, stat, st->type, st->SG_length, st->dma_info.done, st->dma_req.done);
		}
	}
	atomic_dec(&itv->capturing);

	// Set State to 0
	st->state = 0;

	/* Clear capture and no-read bits */
	clear_bit(IVTV_F_S_CAPTURING, &st->s_flags);
	/* clear Overflow */
        clear_bit(IVTV_F_S_OVERFLOW, &st->s_flags);
	/* Clear DMA */
	clear_bit(IVTV_F_S_DMAP, &st->s_flags);

	/* Clear capture started bit */
        if (type == IVTV_ENC_STREAM_TYPE_VBI) {
                clear_bit(IVTV_F_T_ENC_VBI_STARTED, &itv->t_flags);
        } else if ((atomic_read(&itv->capturing) == 0) || 
		(atomic_read(&itv->capturing) == 1 && 
			test_bit(IVTV_F_T_ENC_VBI_STARTED, &itv->t_flags))) 
	{
                clear_bit(IVTV_F_T_ENC_VID_STARTED, &itv->t_flags);
	} else if (test_bit(IVTV_F_T_ENC_RAWVID_STARTED, &itv->t_flags)) {
		clear_bit(IVTV_F_T_ENC_RAWVID_STARTED, &itv->t_flags);
	} else if (test_bit(IVTV_F_T_ENC_RAWAUD_STARTED, &itv->t_flags)) {
		clear_bit(IVTV_F_T_ENC_RAWAUD_STARTED, &itv->t_flags);
	} else
                clear_bit(IVTV_F_T_ENC_VID_STARTED, &itv->t_flags);
	
	// Still other captures running
	if (atomic_read(&itv->capturing) > 0) {
		up(&itv->streams_lock);
		return 0;
	}

	/*Set the following Interrupt mask bits for capture */
	ivtv_set_irq_mask(itv, IVTV_IRQ_MASK_CAPTURE);
	IVTV_DEBUG_IRQ(
		   "ENC: on Stop IRQ Mask is now: 0x%08x for stream %d\n", 
			itv->irqmask, st->type);

	// All DMA Should now be Idle and Done
	itv->DMAP = 0;

	up(&itv->streams_lock);

	return 0;
}

