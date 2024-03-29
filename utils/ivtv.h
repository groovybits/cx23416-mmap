/*
    Public ivtv API header
    Copyright (C) 2003-2004  Kevin Thayer <nufan_wfk at yahoo.com>

    VBI portions:
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

#ifndef _LINUX_IVTV_H
#define _LINUX_IVTV_H

/* Stream types */
#define IVTV_STREAM_PS		0
#define IVTV_STREAM_TS		1
#define IVTV_STREAM_MPEG1	2
#define IVTV_STREAM_PES_AV	3
#define IVTV_STREAM_PES_V	5
#define IVTV_STREAM_PES_A	7
#define IVTV_STREAM_DVD		10
#define IVTV_STREAM_VCD		11
#define IVTV_STREAM_SVCD	12
#define IVTV_STREAM_DVD_S1	13
#define IVTV_STREAM_DVD_S2	14

#define IVTV_YUV_TYPE_HME12		0	/* Hauppauge macro block format */
#define IVTV_YUV_TYPE_FOURCC_YV12	1

/* device ioctls should use the range 29-199 */
#define IVTV_IOC_START_DECODE      _IOW ('@', 29, struct ivtv_cfg_start_decode)
#define IVTV_IOC_STOP_DECODE       _IOW ('@', 30, struct ivtv_cfg_stop_decode)
#define IVTV_IOC_G_SPEED           _IOR ('@', 31, struct ivtv_speed)
#define IVTV_IOC_S_SPEED           _IOW ('@', 32, struct ivtv_speed)
#define IVTV_IOC_DEC_STEP          _IOW ('@', 33, int)
#define IVTV_IOC_DEC_FLUSH         _IOW ('@', 34, int)
#define IVTV_IOC_PLAY     	   _IO  ('@', 37)
#define IVTV_IOC_PAUSE    	   _IO  ('@', 38)
#define IVTV_IOC_FRAMESYNC	   _IOR ('@', 39, struct ivtv_ioctl_framesync)
#define IVTV_IOC_GET_TIMING	   _IOR ('@', 40, struct ivtv_ioctl_framesync)
#define IVTV_IOC_S_SLOW_FAST       _IOW ('@', 41, struct ivtv_slow_fast)
#define IVTV_IOC_S_START_DECODE    _IOW ('@', 42, struct ivtv_cfg_start_decode)
#define IVTV_IOC_S_STOP_DECODE     _IOW ('@', 43, struct ivtv_cfg_stop_decode)
#define IVTV_IOC_GET_FB            _IOR ('@', 44, int)
#define IVTV_IOC_G_CODEC           _IOR ('@', 48, struct ivtv_ioctl_codec)
#define IVTV_IOC_S_CODEC           _IOW ('@', 49, struct ivtv_ioctl_codec)
#define IVTV_IOC_S_GOP_END         _IOWR('@', 50, int)
#define IVTV_IOC_S_VBI_PASSTHROUGH _IOW ('@', 51, int)
#define IVTV_IOC_G_VBI_PASSTHROUGH _IOR ('@', 52, int)
#define IVTV_IOC_PASSTHROUGH       _IOW ('@', 53, int)
#define IVTV_IOC_S_VBI_EMBED       _IOW ('@', 54, int)
#define IVTV_IOC_G_VBI_EMBED       _IOR ('@', 55, int)
#define IVTV_IOC_PAUSE_ENCODE      _IO  ('@', 56)
#define IVTV_IOC_RESUME_ENCODE     _IO  ('@', 57)
#define IVTV_IOC_DEC_SPLICE        _IOW ('@', 58, int)
#define IVTV_IOC_DEC_FAST_STOP     _IOW ('@', 59, int)
#define IVTV_IOC_PREP_FRAME_YUV    _IOW ('@', 60, struct ivtvyuv_ioctl_dma_host_to_ivtv_args)
#define IVTV_IOC_G_YUV_INTERLACE   _IOR ('@', 61, struct ivtv_ioctl_yuv_interlace)
#define IVTV_IOC_S_YUV_INTERLACE   _IOW ('@', 62, struct ivtv_ioctl_yuv_interlace)

// Note: You only append to this structure, you never reorder the members,
// you never play tricks with its alignment, you never change the size of
// anything.
#define IVTV_DRIVER_INFO_MAX_COMMENT_LENGTH 100
struct ivtv_driver_info {
	uint32_t size;		// size of this structure
	uint32_t version;	// version bits 31-16 = major, 15-8 = minor,
	// 7-0 = patchlevel
	char comment[IVTV_DRIVER_INFO_MAX_COMMENT_LENGTH];
        uint32_t cardnr;        // the ivtv card number (0-based)
        uint32_t hw_flags;      // hardware flags: which chips are used?
} __attribute__((packed));

#define IVTV_DRIVER_INFO_V1_SIZE 108
#define IVTV_DRIVER_INFO_V2_SIZE 112
#define IVTV_DRIVER_INFO_V3_SIZE 116

#define IVTV_IOC_G_DRIVER_INFO _IOWR('@', 100, struct ivtv_driver_info *)

#define IVTV_HW_CX25840  (1 << 0)
#define IVTV_HW_SAA7115  (1 << 1)
#define IVTV_HW_SAA7127  (1 << 2)
#define IVTV_HW_MSP34XX  (1 << 3)
#define IVTV_HW_TDA9887  (1 << 4)
#define IVTV_HW_WM8775   (1 << 5)
#define IVTV_HW_CS53132A (1 << 6)
#define IVTV_HW_TVEEPROM (1 << 7)
#define IVTV_HW_SAA7114  (1 << 8)

// Version info
// Note: never use the _INTERNAL versions of these macros

// Internal version macros, don't use these
#define IVTV_VERSION_NUMBER_INTERNAL(name) name##_version_int
#define IVTV_VERSION_STRING_INTERNAL(name) name##_version_string
#define IVTV_VERSION_COMMENT_INTERNAL(name) name##_comment_string

#define IVTV_VERSION_EXTERN_NUMBER_INTERNAL(name) \
	extern uint32_t IVTV_VERSION_NUMBER_INTERNAL(name)
#define IVTV_VERSION_EXTERN_STRING_INTERNAL(name) \
	extern const char * const IVTV_VERSION_STRING_INTERNAL(name)
#define IVTV_VERSION_EXTERN_COMMENT_INTERNAL(name) \
	extern const char * const IVTV_VERSION_COMMENT_INTERNAL(name)

#define IVTV_VERSION_MAJOR_INTERNAL(name) \
	(0xFF & (IVTV_VERSION_NUMBER_INTERNAL(name) >> 16))
#define IVTV_VERSION_MINOR_INTERNAL(name) \
	(0xFF & (IVTV_VERSION_NUMBER_INTERNAL(name) >> 8))
#define IVTV_VERSION_PATCHLEVEL_INTERNAL(name) \
	(0xFF & (IVTV_VERSION_NUMBER_INTERNAL(name)))

// External version macros
#define IVTV_VERSION_NUMBER(name) IVTV_VERSION_NUMBER_INTERNAL(name)
#define IVTV_VERSION_STRING(name) IVTV_VERSION_STRING_INTERNAL(name)
#define IVTV_VERSION_COMMENT(name) IVTV_VERSION_COMMENT_INTERNAL(name)
#define IVTV_VERSION_EXTERN_NUMBER(name) \
	IVTV_VERSION_EXTERN_NUMBER_INTERNAL(name)
#define IVTV_VERSION_EXTERN_STRING(name) \
	IVTV_VERSION_EXTERN_STRING_INTERNAL(name)
#define IVTV_VERSION_EXTERN_COMMENT(name) \
	IVTV_VERSION_EXTERN_COMMENT_INTERNAL(name)

#define IVTV_VERSION_INFO_NAME ivtv_rev

IVTV_VERSION_EXTERN_NUMBER(IVTV_VERSION_INFO_NAME);
IVTV_VERSION_EXTERN_STRING(IVTV_VERSION_INFO_NAME);
IVTV_VERSION_EXTERN_COMMENT(IVTV_VERSION_INFO_NAME);

/* Custom v4l controls */
#ifndef V4L2_CID_PRIVATE_BASE
#define V4L2_CID_PRIVATE_BASE			0x08000000
#endif /* V4L2_CID_PRIVATE_BASE */

#define V4L2_CID_IVTV_FREQ      	(V4L2_CID_PRIVATE_BASE)
#define V4L2_CID_IVTV_ENC       	(V4L2_CID_PRIVATE_BASE + 1)
#define V4L2_CID_IVTV_BITRATE   	(V4L2_CID_PRIVATE_BASE + 2)
#define V4L2_CID_IVTV_MONO      	(V4L2_CID_PRIVATE_BASE + 3)
#define V4L2_CID_IVTV_JOINT     	(V4L2_CID_PRIVATE_BASE + 4)
#define V4L2_CID_IVTV_EMPHASIS  	(V4L2_CID_PRIVATE_BASE + 5)
#define V4L2_CID_IVTV_CRC       	(V4L2_CID_PRIVATE_BASE + 6)
#define V4L2_CID_IVTV_COPYRIGHT 	(V4L2_CID_PRIVATE_BASE + 7)
#define V4L2_CID_IVTV_GEN       	(V4L2_CID_PRIVATE_BASE + 8)

#define V4L2_CID_IVTV_DEC_SMOOTH_FF	(V4L2_CID_PRIVATE_BASE + 9)
#define V4L2_CID_IVTV_DEC_FR_MASK	(V4L2_CID_PRIVATE_BASE + 10)
#define V4L2_CID_IVTV_DEC_SP_MUTE	(V4L2_CID_PRIVATE_BASE + 11)
#define V4L2_CID_IVTV_DEC_FR_FIELD	(V4L2_CID_PRIVATE_BASE + 12)
#define V4L2_CID_IVTV_DEC_AUD_SKIP	(V4L2_CID_PRIVATE_BASE + 13)
#define V4L2_CID_IVTV_DEC_NUM_BUFFERS	(V4L2_CID_PRIVATE_BASE + 14)
#define V4L2_CID_IVTV_DEC_PREBUFFER	(V4L2_CID_PRIVATE_BASE + 15)

struct ivtv_ioctl_framesync {
	uint32_t frame;
	uint64_t pts;
	uint64_t scr;
};

struct ivtv_speed {
	int scale;		/* 1-?? (50 for now) */
	int smooth;		/* Smooth mode when in slow/fast mode */
	int speed;		/* 0 = slow, 1 = fast */
	int direction;		/* 0 = forward, 1 = reverse (not supportd */
	int fr_mask;		/* 0 = I, 1 = I,P, 2 = I,P,B    2 = default! */
	int b_per_gop;		/* frames per GOP (reverse only) */
	int aud_mute;		/* Mute audio while in slow/fast mode */
	int fr_field;		/* 1 = show every field, 0 = show every frame */
	int mute;		/* # of audio frames to mute on playback resume */
};

struct ivtv_slow_fast {
	int speed;		/* 0 = slow, 1 = fast */
	int scale;		/* 1-?? (50 for now) */
};

struct ivtv_cfg_start_decode {
	uint32_t gop_offset;	/*Frames in GOP to skip before starting */
	uint32_t muted_audio_frames;	/* #of audio frames to mute */
};

struct ivtv_cfg_stop_decode {
	int hide_last;		/* 1 = show black after stop,
				   0 = show last frame */
	uint64_t pts_stop;	/* PTS to stop at */
};

/* For use with IVTV_IOC_G_CODEC and IVTV_IOC_S_CODEC */
struct ivtv_ioctl_codec {
	uint32_t aspect;
	uint32_t audio_bitmask;
	uint32_t bframes;
	uint32_t bitrate_mode;
	uint32_t bitrate;
	uint32_t bitrate_peak;
	uint32_t dnr_mode;
	uint32_t dnr_spatial;
	uint32_t dnr_temporal;
	uint32_t dnr_type;
	uint32_t framerate;	/* read only, ignored on write */
	uint32_t framespergop;	/* read only, ignored on write */
	uint32_t gop_closure;
	uint32_t pulldown;
	uint32_t stream_type;
};
struct ivtv_ioctl_yuv_interlace{
	int interlace_mode; /* Takes one of IVTV_YUV_MODE_xxxxxx values */
	int threshold; /* If mode is auto then if src_height <= this value treat as progressive otherwise treat as interlaced */
};
#define IVTV_YUV_MODE_INTERLACED	0
#define IVTV_YUV_MODE_PROGRESSIVE	1
#define IVTV_YUV_MODE_AUTO		2

/* Framebuffer external API */

struct ivtvfb_ioctl_state_info {
	unsigned long status;
	unsigned long alpha;
};

struct ivtvfb_ioctl_colorkey {
    int state;
    uint32_t colorKey;
};

struct ivtvfb_ioctl_blt_copy_args {
	int x, y, width, height, source_offset, source_stride;
};

struct ivtvfb_ioctl_blt_fill_args {
	int rasterop, alpha_mode, alpha_mask, width, height, x, y;
	unsigned int destPixelMask, colour;

};

struct ivtvfb_ioctl_dma_host_to_ivtv_args {
	void *source;
	unsigned long dest_offset;
	int count;
};

struct ivtvyuv_ioctl_dma_host_to_ivtv_args {
	void *y_source;
	void *uv_source;
	unsigned int yuv_type;
	int src_x;
	int src_y;
	unsigned int src_w;
	unsigned int src_h;
	int dst_x;
	int dst_y;
	unsigned int dst_w;
	unsigned int dst_h;
	int srcBuf_width;
	int srcBuf_height;
};

struct ivtvfb_ioctl_get_frame_buffer {
	void *mem;
	int size;
	int sizex;
	int sizey;
};

struct ivtv_osd_coords {
	unsigned long offset;
	unsigned long max_offset;
	int pixel_stride;
	int lines;
	int x;
	int y;
};

struct rectangle {
	int x0;
	int y0;
	int x1;
	int y1;
};

struct ivtvfb_ioctl_set_window {
	int width;
	int height;
	int left;
	int top;
};



/* Framebuffer ioctls should use the range 1 - 28 */
#define IVTVFB_IOCTL_GET_STATE          _IOR('@', 1, struct ivtvfb_ioctl_state_info)
#define IVTVFB_IOCTL_SET_STATE          _IOW('@', 2, struct ivtvfb_ioctl_state_info)
#define IVTVFB_IOCTL_PREP_FRAME         _IOW('@', 3, struct ivtvfb_ioctl_dma_host_to_ivtv_args)
#define IVTVFB_IOCTL_BLT_COPY           _IOW('@', 4, struct ivtvfb_ioctl_blt_copy_args)
#define IVTVFB_IOCTL_GET_ACTIVE_BUFFER  _IOR('@', 5, struct ivtv_osd_coords)
#define IVTVFB_IOCTL_SET_ACTIVE_BUFFER  _IOW('@', 6, struct ivtv_osd_coords)
#define IVTVFB_IOCTL_GET_FRAME_BUFFER   _IOR('@', 7, struct ivtvfb_ioctl_get_frame_buffer)
#define IVTVFB_IOCTL_BLT_FILL           _IOW('@', 8, struct ivtvfb_ioctl_blt_fill_args)
#define IVTVFB_IOCTL_PREP_FRAME_BUF     _IOW('@', 9, struct ivtvfb_ioctl_dma_host_to_ivtv_args)
#define IVTVFB_IOCTL_SET_WINDOW         _IOW('@', 11, struct ivtvfb_ioctl_set_window)
#define IVTVFB_IOCTL_GET_COLORKEY       _IOW('@', 12, struct ivtvfb_ioctl_colorkey)
#define IVTVFB_IOCTL_SET_COLORKEY       _IOW('@', 13, struct ivtvfb_ioctl_colorkey)

#define IVTVFB_STATUS_ENABLED           (1 << 0)
#define IVTVFB_STATUS_GLOBAL_ALPHA      (1 << 1)
#define IVTVFB_STATUS_LOCAL_ALPHA       (1 << 2)
#define IVTVFB_STATUS_FLICKER_REDUCTION (1 << 3)

#ifdef IVTV_INTERNAL
/* Do not use these structures and ioctls in code that you want to release.
   Only to be used for testing and by the utilities ivtvctl, ivtvfbctl and fwapi. */

/* These are the VBI types as they appear in the embedded VBI private packets.
   It is very likely that this will disappear and be replaced by the DVB standard. */
#define IVTV_SLICED_TYPE_TELETEXT_B     (1)
#define IVTV_SLICED_TYPE_CAPTION_525    (4)
#define IVTV_SLICED_TYPE_WSS_625        (5)
#define IVTV_SLICED_TYPE_VPS            (7)

#define IVTV_ENC_STREAM_TYPE_MPG 0
#define IVTV_ENC_STREAM_TYPE_YUV 1
#define IVTV_ENC_STREAM_TYPE_VBI 2
#define IVTV_ENC_STREAM_TYPE_PCM 3
#define IVTV_ENC_STREAM_TYPE_RAD 4
#define IVTV_DEC_STREAM_TYPE_MPG 5
#define IVTV_DEC_STREAM_TYPE_VBI 6
#define IVTV_DEC_STREAM_TYPE_VOUT 7
#define IVTV_DEC_STREAM_TYPE_YUV 8
#define IVTV_DEC_STREAM_TYPE_OSD 9

struct ivtv_stream_info {
        uint32_t size;
        uint32_t type;
} __attribute__((packed));

#define IVTV_STREAM_INFO_V1_SIZE 8
#define IVTV_IOC_G_STREAM_INFO _IOWR('@', 101, struct ivtv_stream_info *)

#define IVTV_MBOX_MAX_DATA 16

struct ivtv_ioctl_fwapi {
	uint32_t cmd;
	uint32_t result;
	int32_t args;
	uint32_t data[IVTV_MBOX_MAX_DATA];
};

struct ivtv_ioctl_event {
	uint32_t type;
	uint32_t mbox;
	struct ivtv_ioctl_fwapi api;
};

struct ivtv_ioctl_register {
	uint32_t i2c_id; /* I2C ID of the I2C chip. 0 for the I2C adapter. */
	unsigned long reg;
	uint32_t val;
};

struct ivtv_msp_matrix {
	int input;
	int output;
};

/* Debug flags */
#define IVTV_DBGFLG_WARN  (1 << 0)
#define IVTV_DBGFLG_INFO  (1 << 1)
#define IVTV_DBGFLG_API   (1 << 2)
#define IVTV_DBGFLG_DMA   (1 << 3)
#define IVTV_DBGFLG_IOCTL (1 << 4)
#define IVTV_DBGFLG_I2C   (1 << 5)
#define IVTV_DBGFLG_IRQ   (1 << 6)
#define IVTV_DBGFLG_DEC   (1 << 7)
#define IVTV_DBGFLG_YUV   (1 << 8)

/* BLT RasterOps */
#define IVTV_BLT_RASTER_ZERO		0
#define IVTV_BLT_RASTER_NOTDEST_AND_NOTSRC	1
#define IVTV_BLT_RASTER_NOTDEST_AND_SRC	2
#define IVTV_BLT_RASTER_NOTDEST		3
#define IVTV_BLT_RASTER_DEST_AND_NOTSRC	4
#define IVTV_BLT_RASTER_NOTSRC		5
#define IVTV_BLT_RASTER_DEST_XOR_SRC	6
#define IVTV_BLT_RASTER_NOTDEST_OR_NOTSRC	7
													    /* #define IVTV_BLT_RASTER_NOTDEST_AND_NOTSRC      8 *//* Same as 1 */
#define IVTV_BLT_RASTER_DEST_XNOR_SRC	9
#define IVTV_BLT_RASTER_SRC			10
#define IVTV_BLT_RASTER_NOTDEST_OR_SRC	11
#define IVTV_BLT_RASTER_DEST		12
#define IVTV_BLT_RASTER_DEST_OR_NOTSRC	13
#define IVTV_BLT_RASTER_DEST_OR_SRC		14
#define IVTV_BLT_RASTER_ONE			15

/* BLT Alpha blending */

#define IVTV_BLT_ALPHABLEND_SRC		0x01
#define IVTV_BLT_ALPHABLEND_DEST	0x10
#define IVTV_BLT_ALPHABLEND_DEST_X_SRC	0x11	/* dest x src +1 , = zero if both zero */

/* Internal ioctls should use the range 200-255 */
#define IVTV_IOC_S_DEBUG_LEVEL     _IOWR('@', 200, int)
#define IVTV_IOC_G_DEBUG_LEVEL     _IOR ('@', 201, int)
#define IVTV_IOC_RELOAD_FW         _IO  ('@', 202)
#define IVTV_IOC_ZCOUNT            _IO  ('@', 203)
#define IVTV_IOC_FWAPI             _IOWR('@', 204, struct ivtv_ioctl_fwapi)
#define IVTV_IOC_EVENT_SETUP       _IOWR('@', 205, struct ivtv_ioctl_event)
#define IVTV_IOC_G_DECODER_REG     _IOWR('@', 206, struct ivtv_ioctl_register)
#define IVTV_IOC_S_DECODER_REG     _IOW ('@', 207, struct ivtv_ioctl_register)
#define IVTV_IOC_G_ENCODER_REG     _IOWR('@', 208, struct ivtv_ioctl_register)
#define IVTV_IOC_S_ENCODER_REG     _IOW ('@', 209, struct ivtv_ioctl_register)
#define IVTV_IOC_S_MSP_MATRIX      _IOW ('@', 210, struct ivtv_msp_matrix)
#define IVTV_IOC_G_ITVC_REG        _IOWR('@', 211, struct ivtv_ioctl_register)
#define IVTV_IOC_S_ITVC_REG        _IOW ('@', 212, struct ivtv_ioctl_register)
#define IVTV_IOC_RESET_IR          _IO  ('@', 213)

#endif /* IVTV_INTERNAL */

#endif /* _LINUX_IVTV_H */
