/*
    ivtv driver internal defines and structures
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

#ifndef IVTV_DRIVER_H
#define IVTV_DRIVER_H

/* Internal header for ivtv project:
 * Driver for the cx23415/6 chip.
 * Author: Kevin Thayer (nufan_wfk at yahoo.com)
 * License: GPL
 * http://www.ivtvdriver.org
 * 
 * -----
 * MPG600/MPG160 support by  T.Adachi <tadachi@tadachi-net.com>
 *                      and Takeru KOMORIYA<komoriya@paken.org>
 *
 * AVerMedia M179 GPIO info by Chris Pinkham <cpinkham@bc2va.org>
 *                using information provided by Jiun-Kuei Jung @ AVerMedia.
 */
#ifndef MODULE
#define MODULE
#endif /* MODULE */

#include "ivtv-compat.h"

#include <linux/module.h>
#include <linux/moduleparam.h>
#include <linux/init.h>
#include <linux/config.h>
#include <linux/delay.h>
#include <linux/sched.h>
#include <linux/fs.h>
#include <linux/pci.h>
#include <linux/interrupt.h>
#include <linux/spinlock.h>
#include <linux/i2c.h>
#include <linux/i2c-algo-bit.h>
#include <linux/list.h>
#include <linux/unistd.h>
#include <linux/pagemap.h>
#include <asm/uaccess.h>
#include <asm/semaphore.h>
#include <asm/system.h>

#include "ivtv-buf.h"

#define IVTV_INTERNAL
#include "ivtv.h"

#include "tuner.h"

#ifdef CONFIG_LIRC_I2C
#  error "This driver is not compatible with the LIRC I2C kernel configuration option."
#endif /* CONFIG_LIRC_I2C */

#ifndef CONFIG_PCI
#  error "This driver requires kernel PCI support."
#endif /* CONFIG_PCI */

#define IVTV_ENCODER_OFFSET	0x00000000
#define IVTV_ENCODER_SIZE	0x00800000	/* Last half isn't needed 0x01000000 */

#define IVTV_DECODER_OFFSET	0x01000000
#define IVTV_DECODER_SIZE	0x00800000	/* Last half isn't needed 0x01000000 */

#define IVTV_REG_OFFSET 	0x02000000
#define IVTV_REG_SIZE		0x00010000

#define IVTV_ENCDEC_SIZE	IVTV_REG_OFFSET
#define IVTV_IOREMAP_SIZE	0x04000000

// Based on 6 PVR500s each with two PVR15s...
#define IVTV_MAX_CARDS 12

#define IVTV_CARD_PVR_250 0	/* wintv pvr 250 */
#define IVTV_CARD_PVR_150 5	/* wintv pvr 150 */

#define NO_AUDIO    0		/* the card can't handle audio */
#define USE_GPIO    2		/* switch audio by GPIO */
#define USE_CX25840 3		/* switch audio by CX25840 */
#define USE_PVR150  4		/* switch audio with WM8775 and CX25840 */

#define IVTV_AUDIO_32000 0x2	/* 32 kHz audio */
#define IVTV_AUDIO_44100 0x0	/* 44.1 kHz audio */
#define IVTV_AUDIO_48000 0x1	/* 48 kHz audio */

#define IVTV_V4L2_ENC_PCM_OFFSET 24	/* offset from 0 to register pcm v4l2 minors on */
#define IVTV_V4L2_ENC_YUV_OFFSET 32	/* offset from 0 to register yuv v4l2 minors on */

#define IVTV_ENC_MEM_START 0x00000000
#define IVTV_DEC_MEM_START 0x01000000

#define PCI_VENDOR_ID_ICOMP  0x4444
#define PCI_DEVICE_ID_IVTV15 0x0803
#define PCI_DEVICE_ID_IVTV16 0x0016
#define IVTV_PCI_ID_HAUPPAUGE 0x0070	/* subsystem vendor id */

#define IVTV_MBOX_MAX_BOXES 20
#define IVTV_MBOX_API_BOXES 6
#define IVTV_MBOX_DMA_START 6
#define IVTV_MBOX_DMA_END 8
#define IVTV_MBOX_MAX_DATA 16
#define IVTV_MBOX_DMA 9
#define IVTV_MBOX_FIELD_DISPLAYED 8
#define IVTV_MBOX_SIZE 80

/* ======================================================================== */
/* ========================== START USER SETTABLE DMA VARIABLES =========== */
/* ======================================================================== */
#ifdef __powerpc__
#define DYNAMIC_MEMORY_ALLOC	0 /* PowerPC doesn't work with DMA currently */
#else
#define DYNAMIC_MEMORY_ALLOC 	1 /* Allocate memory each stream use */
#endif

/* DMA Buffers Sizes */
#define IVTV_DMA_ENC_BUF_SIZE     0x00020000
#define IVTV_DMA_ENC_YUV_BUF_SIZE 0x0007e900 // NTSC
#define IVTV_DMA_ENC_PCM_BUF_SIZE 0x00001200

/* Decoder DMA or PIO, 1=PIO, 0=DMA */
/* PowerPC does not work with DMA currently */
#ifdef __powerpc__
#define IVTV_VBI_PIO		1
#define IVTV_ENC_PIO		1
#else
#define IVTV_VBI_PIO		0
#define IVTV_ENC_PIO		0
#endif
/* This sometimes times out, seems to  kill
					   encoding sometimes */

/* What we tell the firmware to expect or return, 131072 is best so far */
#define FW_ENC_DMA_XFER_SIZE  	131072	/* 524288, 262144, 131072 */
#define FW_ENC_DMA_XFER_TYPE 	0	/* 1=frame, 0=block */
#define FW_ENC_DMA_XFER_COUNT	0	/* Queue size in block (max=5) or number of frames */

/* DMA Buffers, Default size in MEGS allocated */
#define IVTV_DEFAULT_MPG_BUFFERS 1
#define IVTV_DEFAULT_YUV_BUFFERS 1
#define IVTV_DEFAULT_VBI_BUFFERS 1
#define IVTV_DEFAULT_PCM_BUFFERS 1

/* DMA Buffers MAX Limit in MEGS allocated */
#define IVTV_MAX_MPG_BUFFERS 32
#define IVTV_MAX_YUV_BUFFERS 32
#define IVTV_MAX_VBI_BUFFERS 32
#define IVTV_MAX_PCM_BUFFERS 32

/* ======================================================================== */
/* ========================== END USER SETTABLE DMA VARIABLES ============= */
/* ======================================================================== */

/* Decoder DMA Settings */
struct ivtv_dma_settings {
	/* Buffer sizes */
	int enc_buf_size;
	int enc_yuv_buf_size;
	int enc_pcm_buf_size;
	int vbi_buf_size;

	/* Max growable amount of buffers */
	int max_yuv_buf;
	int max_pcm_buf;
	int max_mpg_buf;
	int max_vbi_buf;

	/* Chip DMA Xfer Settings */
	int fw_enc_dma_xfer;
	int fw_enc_dma_type;

	/* Processor IO */
	int vbi_pio;
	int enc_pio;
	int dec_pio;
	int osd_pio;
};

/* Status Register */
#define IVTV_DMA_ERR_LIST 0x00000010
#define IVTV_DMA_ERR_WRITE 0x00000008
#define IVTV_DMA_ERR_READ 0x00000004
#define IVTV_DMA_SUCCESS_WRITE 0x00000002
#define IVTV_DMA_SUCCESS_READ 0x00000001
#define IVTV_DMA_READ_ERR (IVTV_DMA_ERR_LIST | IVTV_DMA_ERR_READ)
#define IVTV_DMA_WRITE_ERR (IVTV_DMA_ERR_LIST | IVTV_DMA_ERR_WRITE)
#define IVTV_DMA_ERR (IVTV_DMA_ERR_LIST | IVTV_DMA_ERR_WRITE | IVTV_DMA_ERR_READ)

#define ITVC_GET_REG		0x1
#define ITVC_SET_REG		0x2

/* video related */
#define IVTV_MAX_INPUTS 9

/* DMA Registers */
#define IVTV_REG_DMAXFER 	(0x0000 /*| IVTV_REG_OFFSET*/)
#define IVTV_REG_DMASTATUS 	(0x0004 /*| IVTV_REG_OFFSET*/)
#define IVTV_REG_DECDMAADDR 	(0x0008 /*| IVTV_REG_OFFSET*/)
#define IVTV_REG_ENCDMAADDR 	(0x000c /*| IVTV_REG_OFFSET*/)
#define IVTV_REG_DMACONTROL 	(0x0010 /*| IVTV_REG_OFFSET*/)
#define IVTV_REG_DMABITS 	(0x001c /*| IVTV_REG_OFFSET*/)
#define IVTV_REG_IRQSTATUS 	(0x0040 /*| IVTV_REG_OFFSET*/)
#define IVTV_REG_IRQMASK 	(0x0048 /*| IVTV_REG_OFFSET*/)

/* SG Buffers */
#define IVTV_REG_ENCSG1SRC 	(0x0080 /*| IVTV_REG_OFFSET*/)
#define IVTV_REG_ENCSG1DST 	(0x0084 /*| IVTV_REG_OFFSET*/)
#define IVTV_REG_ENCSG1LEN 	(0x0088 /*| IVTV_REG_OFFSET*/)
#define IVTV_REG_ENCSG2SRC 	(0x008c /*| IVTV_REG_OFFSET*/)
#define IVTV_REG_ENCSG2DST 	(0x0090 /*| IVTV_REG_OFFSET*/)
#define IVTV_REG_ENCSG2LEN 	(0x0094 /*| IVTV_REG_OFFSET*/)
#define IVTV_REG_ENCSG3SRC 	(0x0098 /*| IVTV_REG_OFFSET*/)
#define IVTV_REG_ENCSG3DST 	(0x009c /*| IVTV_REG_OFFSET*/)
#define IVTV_REG_ENCSG3LEN 	(0x00a0 /*| IVTV_REG_OFFSET*/)
#define IVTV_REG_ENCSG4SRC 	(0x00a4 /*| IVTV_REG_OFFSET*/)
#define IVTV_REG_ENCSG4DST 	(0x00a8 /*| IVTV_REG_OFFSET*/)
#define IVTV_REG_ENCSG4LEN 	(0x00ac /*| IVTV_REG_OFFSET*/)
#define IVTV_REG_ENCSG5SRC 	(0x00b0 /*| IVTV_REG_OFFSET*/)
#define IVTV_REG_ENCSG5DST 	(0x00b4 /*| IVTV_REG_OFFSET*/)
#define IVTV_REG_ENCSG5LEN 	(0x00b8 /*| IVTV_REG_OFFSET*/)
#define IVTV_REG_ENCSG6SRC 	(0x00bc /*| IVTV_REG_OFFSET*/)
#define IVTV_REG_ENCSG6DST 	(0x00c0 /*| IVTV_REG_OFFSET*/)
#define IVTV_REG_ENCSG6LEN 	(0x00c4 /*| IVTV_REG_OFFSET*/)
#define IVTV_REG_ENCSG7SRC 	(0x00c8 /*| IVTV_REG_OFFSET*/)
#define IVTV_REG_ENCSG7DST 	(0x00cc /*| IVTV_REG_OFFSET*/)
#define IVTV_REG_ENCSG7LEN 	(0x00d0 /*| IVTV_REG_OFFSET*/)
#define IVTV_REG_ENCSG8SRC 	(0x00d4 /*| IVTV_REG_OFFSET*/)
#define IVTV_REG_ENCSG8DST 	(0x00d8 /*| IVTV_REG_OFFSET*/)
#define IVTV_REG_ENCSG8LEN 	(0x00dc /*| IVTV_REG_OFFSET*/)

#define IVTV_REG_DECSG1SRC 	(0x00e0 /*| IVTV_REG_OFFSET*/)
#define IVTV_REG_DECSG1DST 	(0x00e4 /*| IVTV_REG_OFFSET*/)
#define IVTV_REG_DECSG1LEN 	(0x00e8 /*| IVTV_REG_OFFSET*/)

/* Setup? Registers */
#define IVTV_REG_ENC_SDRAM_REFRESH (0x07F8 /*| IVTV_REG_OFFSET*/)
#define IVTV_REG_ENC_SDRAM_PRECHARGE (0x07FC /*| IVTV_REG_OFFSET*/)
#define IVTV_REG_DEC_SDRAM_REFRESH (0x08F8 /*| IVTV_REG_OFFSET*/)
#define IVTV_REG_DEC_SDRAM_PRECHARGE (0x08FC /*| IVTV_REG_OFFSET*/)
#define IVTV_REG_VDM (0x2800 /*| IVTV_REG_OFFSET*/)
#define IVTV_REG_AO (0x2D00 /*| IVTV_REG_OFFSET*/)
#define IVTV_REG_BYTEFLUSH (0x2D24 /*| IVTV_REG_OFFSET*/)
#define IVTV_REG_SPU (0x9050 /*| IVTV_REG_OFFSET*/)
#define IVTV_REG_HW_BLOCKS (0x9054 /*| IVTV_REG_OFFSET*/)
#define IVTV_REG_VPU (0x9058 /*| IVTV_REG_OFFSET*/)
#define IVTV_REG_APU (0xA064 /*| IVTV_REG_OFFSET*/)

/* commands */
#define IVTV_MASK_SPU_ENABLE 0xFFFFFFFE
#define IVTV_MASK_VPU_ENABLE15 0xFFFFFFF6
#define IVTV_MASK_VPU_ENABLE16 0xFFFFFFFB
#define IVTV_CMD_VDM_STOP 0x00000000
#define IVTV_CMD_AO_STOP 0x00000005
#define IVTV_CMD_APU_PING 0x00000000
#define IVTV_CMD_VPU_STOP15 0xFFFFFFFE
#define IVTV_CMD_VPU_STOP16 0xFFFFFFEE
#define IVTV_CMD_HW_BLOCKS_RST 0xFFFFFFFF
#define IVTV_CMD_SPU_STOP 0x00000001
#define IVTV_CMD_SDRAM_PRECHARGE_INIT 0x0000001A
#define IVTV_CMD_SDRAM_REFRESH_INIT 0x80000640
#define IVTV_SDRAM_SLEEPTIME (60 * HZ / 100)	/* 600ms */

#define IVTV_IRQ_ENC_START_CAP		(0x1 << 31)
#define IVTV_IRQ_ENC_EOS		(0x1 << 30)
#define IVTV_IRQ_ENC_VBI_CAP		(0x1 << 29)
#define IVTV_IRQ_ENC_VIM_RST		(0x1 << 28)
#define IVTV_IRQ_ENC_DMA_COMPLETE	(0x1 << 27)
#define IVTV_IRQ_DEC_VBI_RE_INSERT2	(0x1 << 26)
#define IVTV_IRQ_ENC_DMA_YUVDONE	(0x1 << 25)
#define IVTV_IRQ_ENC_DMA_PCMDONE	(0x1 << 24)
#define IVTV_IRQ_ENC_DMA_VBIDONE	(0x1 << 23)
#define IVTV_IRQ_DEC_DATA_REQ		(0x1 << 22)
#define IVTV_IRQ_DEC_IFRAME_DONE	(0x1 << 21)
#define IVTV_IRQ_DEC_DMA_COMPLETE	(0x1 << 20)
#define IVTV_IRQ_DEC_VBI_RE_INSERT	(0x1 << 19)
#define IVTV_IRQ_DMA_ERR		(0x1 << 18)
#define IVTV_IRQ_DMA_WRITE		(0x1 << 17)
#define IVTV_IRQ_DMA_READ		(0x1 << 16)
#define IVTV_IRQ_DEC_DMA_15		(0x1 << 15)
#define IVTV_IRQ_DEC_DMA_14		(0x1 << 14)
#define IVTV_IRQ_DEC_DMA_13		(0x1 << 13)
#define IVTV_IRQ_DEC_DMA_12		(0x1 << 12)
#define IVTV_IRQ_DEC_DMA_11		(0x1 << 11)
#define IVTV_IRQ_DEC_VSYNC		(0x1 << 10)
#define IVTV_IRQ_DEC_DMA_9		(0x1 << 9)
#define IVTV_IRQ_DEC_DMA_8		(0x1 << 8)
#define IVTV_IRQ_DEC_DMA_7		(0x1 << 7)
#define IVTV_IRQ_DEC_DMA_6		(0x1 << 6)
#define IVTV_IRQ_DEC_DMA_5		(0x1 << 5)
#define IVTV_IRQ_DEC_DMA_4		(0x1 << 4)
#define IVTV_IRQ_DEC_DMA_3		(0x1 << 3)
#define IVTV_IRQ_DEC_DMA_2		(0x1 << 2)
#define IVTV_IRQ_DEC_DMA_1		(0x1 << 1)
#define IVTV_IRQ_DEC_DMA_0		(0x1 << 0)

/* IRQ Masks */
#define IVTV_IRQ_MASK_INIT (IVTV_IRQ_DMA_ERR|IVTV_IRQ_DMA_READ|IVTV_IRQ_ENC_DMA_COMPLETE)

#define IVTV_IRQ_MASK_CAPTURE (IVTV_IRQ_ENC_START_CAP|IVTV_IRQ_ENC_EOS)

/*Used for locating the firmware mailboxes*/
#if defined(CONFIG_FW_LOADER) || defined(CONFIG_FW_LOADER_MODULE)
#include <linux/firmware.h>
#define IVTV_FIRM_ENC_FILENAME "v4l-cx2341x-enc.fw"
#else
#define IVTV_FIRM_ENC_FILENAME "/lib/modules/v4l-cx2341x-enc.fw"
#endif

#define IVTV_FIRM_IMAGE_SIZE 256*1024
#define IVTV_FIRM_SEARCH_ENCODER_START 0x00000000
#define IVTV_FIRM_SEARCH_DECODER_START 0x00000000
#define IVTV_FIRM_SEARCH_ENCODER_END (IVTV_ENCODER_OFFSET + IVTV_ENCODER_SIZE - 1)
#define IVTV_FIRM_SEARCH_DECODER_END (IVTV_DECODER_OFFSET + IVTV_DECODER_SIZE - 1)
#define IVTV_FIRM_SEARCH_STEP (0x00000100 / sizeof(u32))


/* Firmware mailbox flags*/
#define IVTV_MBOX_FIRMWARE_DONE 0x00000004
#define IVTV_MBOX_DRIVER_DONE 0x00000002
#define IVTV_MBOX_DRIVER_BUSY 0x00000001
#define IVTV_MBOX_FREE 0x00000000

#define IVTV_API_STD_TIMEOUT 0x02000000	/*units?? */

/* fw reset commands */
#define IVTV_CMD_QUICK_RESET 0	/* stop then start firmware, don't re-upload */
#define IVTV_CMD_SOFT_RESET  1	/* quick + find mboxes */
#define IVTV_CMD_FULL_RESET  2	/* full stop/upload/start/find mboxes */

/* Firmware API commands */

/* MPEG encoder API */
#define IVTV_API_ENC_PING_FW 			0x00000080
#define IVTV_API_BEGIN_CAPTURE 			0x00000081
#define IVTV_API_END_CAPTURE 			0x00000082
#define IVTV_API_ASSIGN_AUDIO_ID 		0x00000089
#define IVTV_API_ASSIGN_VIDEO_ID 		0x0000008b
#define IVTV_API_ASSIGN_PCR_ID 			0x0000008d
#define IVTV_API_ASSIGN_FRAMERATE 		0x0000008f
#define IVTV_API_ASSIGN_FRAME_SIZE 		0x00000091
#define IVTV_API_ASSIGN_BITRATES 		0x00000095
#define IVTV_API_ASSIGN_GOP_PROPERTIES 		0x00000097
#define IVTV_API_ASSIGN_ASPECT_RATIO 		0x00000099
#define IVTV_API_ASSIGN_DNR_FILTER_MODE 	0x0000009b
#define IVTV_API_ASSIGN_DNR_FILTER_PROPS 	0x0000009d
#define IVTV_API_ASSIGN_CORING_LEVELS 		0x0000009f
#define IVTV_API_ASSIGN_SPATIAL_FILTER_TYPE 	0x000000a1
#define IVTV_API_ASSIGN_3_2_PULLDOWN 		0x000000b1
#define IVTV_API_SELECT_VBI_LINE 		0x000000b7
#define IVTV_API_ASSIGN_STREAM_TYPE 		0x000000b9
#define IVTV_API_ASSIGN_OUTPUT_PORT 		0x000000bb
#define IVTV_API_ASSIGN_AUDIO_PROPERTIES 	0x000000bd
#define IVTV_API_ENC_HALT_FW 			0x000000c3
#define IVTV_API_ENC_GETVER 			0x000000c4
#define IVTV_API_ASSIGN_GOP_CLOSURE 		0x000000c5
#define IVTV_API_ENC_GET_SEQ_END 		0x000000c6
#define IVTV_API_ASSIGN_PGM_INDEX_INFO 		0x000000c7
#define IVTV_API_CONFIG_VBI 			0x000000c8
#define IVTV_API_ASSIGN_DMA_BLOCKLEN 		0x000000c9
#define IVTV_API_PREV_DMA_INFO_MB_10		0x000000ca
#define IVTV_API_PREV_DMA_INFO_MB_9		0x000000cb
#define IVTV_API_SCHED_DMA_TO_HOST 		0x000000cc
#define IVTV_API_INITIALIZE_INPUT 		0x000000cd
#define IVTV_API_ASSIGN_FRAME_DROP_RATE 	0x000000d0
#define IVTV_API_PAUSE_ENCODER 			0x000000d2
#define IVTV_API_REFRESH_INPUT 			0x000000d3
#define IVTV_API_ASSIGN_COPYRIGHT		0x000000d4
#define IVTV_API_EVENT_NOTIFICATION 		0x000000d5
#define IVTV_API_ASSIGN_NUM_VSYNC_LINES 	0x000000d6
#define IVTV_API_ASSIGN_PLACEHOLDER 		0x000000d7
#define IVTV_API_MUTE_VIDEO 			0x000000d9
#define IVTV_API_MUTE_AUDIO 			0x000000da
#define IVTV_API_ENC_UNKNOWN			0x000000db
#define IVTV_API_ENC_MISC 			0x000000dc

/* i2c stuff */
#define I2C_CLIENTS_MAX 16
#define I2C_TIMING (0x7<<4)
#define IVTV_REG_I2C_SETSCL_OFFSET (0x7000 /*| IVTV_REG_OFFSET*/)
#define IVTV_REG_I2C_SETSDA_OFFSET (0x7004 /*| IVTV_REG_OFFSET*/)
#define IVTV_REG_I2C_GETSCL_OFFSET (0x7008 /*| IVTV_REG_OFFSET*/)
#define IVTV_REG_I2C_GETSDA_OFFSET (0x700c /*| IVTV_REG_OFFSET*/)

/* debugging */
/* NOTE: extra space before comma in 'itv->num , ## args' is required for
   gcc-2.95, otherwise it won't compile. */
#define IVTV_DEBUG(x, type, fmt, args...) \
	do { \
		if ((x) & ivtv_debug) \
               		printk(KERN_INFO "ivtv%d " type ": " fmt, itv->num , ## args); \
	} while (0)
#define IVTV_DEBUG_WARN(fmt, args...)  IVTV_DEBUG(IVTV_DBGFLG_WARN, "warning", fmt , ## args)
#define IVTV_DEBUG_INFO(fmt, args...)  IVTV_DEBUG(IVTV_DBGFLG_INFO, "info",fmt , ## args)
#define IVTV_DEBUG_API(fmt, args...)   IVTV_DEBUG(IVTV_DBGFLG_API, "api", fmt , ## args)
#define IVTV_DEBUG_DMA(fmt, args...)   IVTV_DEBUG(IVTV_DBGFLG_DMA, "dma", fmt , ## args)
#define IVTV_DEBUG_IOCTL(fmt, args...) IVTV_DEBUG(IVTV_DBGFLG_IOCTL, "ioctl", fmt , ## args)
#define IVTV_DEBUG_I2C(fmt, args...)   IVTV_DEBUG(IVTV_DBGFLG_I2C, "i2c", fmt , ## args)
#define IVTV_DEBUG_IRQ(fmt, args...)   IVTV_DEBUG(IVTV_DBGFLG_IRQ, "irq", fmt , ## args)
#define IVTV_DEBUG_DEC(fmt, args...)   IVTV_DEBUG(IVTV_DBGFLG_DEC, "dec", fmt , ## args)
#define IVTV_DEBUG_YUV(fmt, args...)   IVTV_DEBUG(IVTV_DBGFLG_YUV, "yuv", fmt , ## args)

/* Standard kernel messages */
#define IVTV_ERR(fmt, args...)      printk(KERN_ERR  "ivtv%d: " fmt, itv->num , ## args)
#define IVTV_WARN(fmt, args...)     printk(KERN_WARNING "ivtv%d: " fmt, itv->num , ## args)
#define IVTV_INFO(fmt, args...)     printk(KERN_INFO "ivtv%d: " fmt, itv->num , ## args)
#define IVTV_OSD_ERR(fmt, args...)  printk(KERN_ERR  "ivtv%d-osd: " fmt, itv->num , ## args)
#define IVTV_OSD_INFO(fmt, args...) printk(KERN_INFO "ivtv%d-osd: " fmt, itv->num , ## args)

extern int ivtv_debug;


struct ivtv_options {
	int cardtype;		/* force card type on load */
	int tuner;		/* set tuner on load */
	int radio;		/* enable/disable radio */
        int tda9887;
	int newi2c;		/* New I2C algorithm */
};

/* ivtv-specific mailbox template */
struct ivtv_mailbox {
	u32 flags;
	u32 cmd;
	u32 retval;
	u32 timeout;
	u32 data[IVTV_MBOX_MAX_DATA];
};

/* per-stream, s_flags */
#define IVTV_F_S_DMAP		0
#define IVTV_F_S_OVERFLOW	1
#define IVTV_F_S_INTERNAL_USE	2	/* this stream won't be read from */
#define IVTV_F_S_NO_DMA		3	/* this stream doesn't use DMA */
#define IVTV_F_S_NEEDS_DATA	4
#define IVTV_F_S_CAPTURING	6	/* this stream is capturing */
#define IVTV_F_S_IN_USE 	7	/* this stream is in use, no one else can
					   try to use it. */
#define IVTV_F_S_STREAMOFF	8	/* signal end of stream EOS */
#define IVTV_F_S_RESET		9

/* per-ivtv, i_flags */
#define IVTV_F_I_EOS		1
#define IVTV_F_I_RADIO_USER	2
#define IVTV_F_I_DIG_PAUSE	4
#define IVTV_F_I_DIG_RST	5
#define IVTV_F_I_MUTE_PAUSE	6

/* dma-tasklet, dma-thread, t_flags */
/* tasklets */
#define IVTV_F_T_ENC_DMA_DONE 	3
#define IVTV_F_T_ENC_VBI_DMA 	4
#define IVTV_F_T_ENC_RUNNING    9 /* Encoder has been started */
#define IVTV_F_T_ENC_VID_STARTED    10/* Encoder has been started */
#define IVTV_F_T_ENC_VBI_STARTED    11/* Encoder VBI has been started */
#define IVTV_F_T_ENC_VBI_RUNNING    13 /* Encoder VBI is running */
#define IVTV_F_T_ENC_RAWVID_STARTED    15/* Encoder has been started */
#define IVTV_F_T_ENC_RAWAUD_STARTED    16/* Encoder has been started */
#define IVTV_F_T_ENC_DMA_QUEUED		17/* DMA needs to be queued */
#define IVTV_F_T_ENC_VBI_DMA_QUEUED		18/* VBI DMA needs to be queued */
#define IVTV_F_T_ENC_YUVDONE		19/* VBI DMA needs to be queued */
#define IVTV_F_T_ENC_PCMDONE		20/* VBI DMA needs to be queued */
#define IVTV_F_T_ENC_VBIDONE		21/* VBI DMA needs to be queued */
#define IVTV_F_T_ENC_MPEGDONE		22/* VBI DMA needs to be queued */

/* firmware-reset, r_flags */
#define FW_RESET_NEEDED        	0
#define DEC_DMA_ERR		3
#define FW_SOFT_RESET_NEEDED  	4
#define FW_RESET_LOCK	  	5
#define FW_RESET_SHUTDOWN  	6

/* Scatter-Gather array element, used in DMA transfers */
struct ivtv_SG_element {
	u32 src;
	u32 dst;
	u32 size;
};

#define FORMAT_FLAGS_PLANAR 0x04

struct ivtv_fmt {
        char  *name;
        u32   fourcc;          /* v4l2 format id */
        int   depth;
        int   flags;
        u32   cxformat;
};

struct ivtv_buffer {
	struct ivtvbuf_buffer 	vb;
	struct v4l2_buffer 	buffer;
	struct ivtv_fmt      	*fmt;
	u32 			count;
	int 			type;
	u64 			pts_stamp;
};

struct cx23416_dma_request {
	long id;
	u32  type;
	u32  size;
	u32  UVsize;
	u32  offset;
	u32  UVoffset; 
	u64  pts_stamp;
	long bytes_needed;
	int done;
};

struct cx23416_dma_info {
	long id;
	u32 type;
	u32 status;
	u32 pts_stamp;
	int done;	
};

struct ivtv_stream {
	long id;
	long seq;
	unsigned long s_flags;	/* status flags, see above */
	int dma;		/* can be PCI_DMA_TODEVICE, 
				   PCI_DMA_FROMDEVICE or
				   PCI_DMA_NONE */

	// DMA Buffer in high mem above 796 meg
	//volatile unsigned char *DMABptr; // DMA Memory start address
	//long DMABbase; // DMA Memory start address
	//u32 DMABoff; // Offset from main DMA Buffer
	//u32 DMABlen; // total DMA Buffer
	//u32 DMABpos; // Current DMA position

	unsigned long trans_id;

	int subtype;
	int type;
	int dmatype;
	u64 pts;		/* last seen Program Time Stamp (PTS) */

	wait_queue_head_t waitq;

	struct pci_dev *dev;
	struct video_device *v4l2dev;

	// DMA Transfer Information
	struct cx23416_dma_request dma_req;
	struct cx23416_dma_info dma_info;

	// V4L2 Stuff
	struct ivtvbuf_queue 	vidq;
	spinlock_t 		slock;
	u32 			count;
	struct list_head 	active;
	struct list_head 	queued;
	struct timer_list      	timeout;
	struct ivtv_fmt      	*fmt;

	int 			height;
	int 			width;

	u32 buftype;
	u32 streaming;
	u32 state;
	enum v4l2_field field;

	/* Buffer Stats */
	atomic_t allocated_buffers;
	int buffers;
	u32 buf_min;
	u32 buf_max;
	int bufsize;
	u32 buf_total;
	u32 buf_fill;

	/* Base Dev SG Array for cx23415/6 */
	struct ivtv_SG_element *SGarray;
	dma_addr_t SG_handle;
	int SG_length;

	/* Locking */
	struct semaphore mlock;

	int first_read;		/* used to clean up stream */
};

struct ivtv_open_id {
	int open_id;
	int type;
	struct ivtv *itv;
};

/* dualwatch thread and flags */
/* audio_bitmask: 
 * bits 0-15 follow iso11172, mostly, see doc/fw-encoder-api.txt. 
 */
#define IVTV_CODEC_AUDIO_MPEG_STEREO_MASK        (0x03 << 8)
#define IVTV_CODEC_AUDIO_MPEG_STEREO_STEREO      (0x00 << 8)
#define IVTV_CODEC_AUDIO_MPEG_STEREO_JOINT       (0x01 << 8)
#define IVTV_CODEC_AUDIO_MPEG_STEREO_DUAL        (0x02 << 8)
#define IVTV_CODEC_AUDIO_MPEG_STEREO_MONO        (0x03 << 8)

/* Save API commands in structure */
#define ENCODER_API_OFFSET 255	/* else encoder */

#define MBOX_TIMEOUT	(HZ*10)	/* seconds */

struct api_cmd {
	int marked;		/* is this used */
	unsigned long jiffies;		/* last command issued */

	u32 s_data[IVTV_MBOX_MAX_DATA];	/* send api data */
	u32 r_data[IVTV_MBOX_MAX_DATA];	/* returned api data */
};

/* forward declaration of struct defined in ivtv-cards.h */
struct ivtv_card;

#define IVTV_VBI_FRAMES 50

/* Struct to hold info about ivtv cards */
struct ivtv {
	const struct ivtv_card *card;	/* card information */
        const char *card_name;
        u8 has_cx25840; 
        u8 has_tda9887; 
        u8 is_50hz; 
        u8 is_60hz;
	u32 v4l2_cap;		/* V4L2 capabilities of card */
        u32 i2c_tv_tuner_addr;
        u32 i2c_radio_tuner_addr;
        u32 hw_flags;
	u8 pvr150_workaround;
	u8 set_fm_high_sensitivity; /* the tuner requires PORT1=1 for radio */

	struct pci_dev *dev;
	struct ivtv_options options;
	int num;		/* invalidate during init! */
	char name[8];		/* name for printk and interupts ala bttv */
	unsigned long i_flags;
	atomic_t capturing;	/* count number of active capture streams */

	// DMA Lock
	unsigned long DMAP;	/* DMA is pending */

	// DMA Buffer
	//unsigned char 	*DMABremap;	/* DMA Buffer, High Memory */
	//long 		DMABbase;	/* DMA Base BUS Address */
	//u32		DMABpos;

	/* Semaphore to ensure stream changes do not happen concurrently. To be
	   precise: while this lock is held, no other process can start/stop,
	   pause/resume or change codecs/formats for any stream.
	 */
	struct semaphore streams_lock;
	struct semaphore i2c_lock;
	struct semaphore DMA_lock;
	spinlock_t DMA_slock;

	int open_id;		/* incremented each time an open occurs, used as unique ID.
				   starts at 1, so 0 can be used as uninitialized value
				   in the stream->id. */

	u32 enc_fw_ver, base_addr;	/*is base_addr needed? */
	u32 irqmask;

	/* VBI data */
	u32 vbi_dec_start, vbi_dec_size;
	u32 vbi_enc_start, vbi_enc_size;
	int vbi_index;
	int vbi_offset;
	int vbi_total_frames;
	int vbi_fpi;
	unsigned long vbi_frame;
	int vbi_dec_mode;
	int vbi_dec_io_size;
	int vbi_passthrough;
	u8 vbi_cc_data_odd[256];
	u8 vbi_cc_data_even[256];
	int vbi_cc_pos;
	u8 vbi_cc_no_update;
	u8 vbi_vps[5];
	u8 vbi_vps_found;
	int vbi_wss;
	u8 vbi_wss_found;
	u8 vbi_wss_no_update;
        u32 vbi_raw_decoder_line_size;
        u8 vbi_raw_decoder_sav_odd_field;
        u8 vbi_raw_decoder_sav_even_field;
        u32 vbi_sliced_decoder_line_size;
        u8 vbi_sliced_decoder_sav_odd_field;
        u8 vbi_sliced_decoder_sav_even_field;
        struct v4l2_format vbi_in;
       	/* convenience pointer to sliced struct in vbi_in union */
        struct v4l2_sliced_vbi_format *vbi_sliced_in;
	u32 vbi_service_set_in;
	u32 vbi_service_set_out;
	int vbi_insert_mpeg;

	/* Buffer for the maximum of 2 * 18 * packet_size sliced VBI lines.
	   One for /dev/vbi0 and one for /dev/vbi4 */

        struct v4l2_sliced_vbi_data vbi_sliced_data[36];
        struct v4l2_sliced_vbi_data vbi_sliced_dec_data[36];

	/* Buffer for VBI data inserted into MPEG stream.
	   The first byte is a dummy byte that's never used.
	   The next 16 bytes contain the MPEG header for the VBI data,
	   the remainder is the actual VBI data.
	   The max size accepted by the MPEG VBI reinsertion turns out
	   to be 1552 bytes, which happens to be 4 + (1 + 42) * (2 * 18) bytes,
	   where 4 is a four byte header, 42 is the max sliced VBI payload, 1 is
	   a single line header byte and 2 * 18 is the number of VBI lines per frame.

	   However, it seems that the data must be 1K aligned, so we have to
	   pad the data until the 1 or 2 K boundary.
        
           This pointer array will allocate 2049 bytes to store each VBI frame. */

	unsigned long vbi_inserted_frame;
	struct timer_list vbi_passthrough_timer;

	struct ivtv_mailbox *enc_mbox;
	struct semaphore enc_msem;

	int dmaboxnum;

	unsigned char card_rev, *enc_mem, *reg_mem;

	u32 idx_sdf_offset;
	u32 idx_sdf_num;
	u32 idx_sdf_mask;

	wait_queue_head_t cap_w;

	/* i2c */
	struct i2c_adapter i2c_adap;
	struct i2c_algo_bit_data i2c_algo;
	struct i2c_client i2c_client;
	struct semaphore i2c_bus_lock;
	int i2c_state;
	struct i2c_client *i2c_clients[I2C_CLIENTS_MAX];

	/* v4l2 and User settings */

	/* codec settings */
	struct ivtv_ioctl_codec codec;
	u32 audmode_tv;
	u32 audmode_radio;
	unsigned long freq_tv;
	unsigned long freq_radio;
	u32 audio_input_tv;
	u32 audio_input_radio;
	u32 msp34xx_tuner_input;
	u32 msp34xx_audio_output;
	u32 active_input;
	u32 active_output;
	v4l2_std_id std;
	v4l2_std_id tuner_std;	/* The norm of the tuner (fixed) */
	u32 width, height;
	u32 vbi_start[2], vbi_count;
	int streamcount;	/* Number of elements in streams */
	struct ivtv_stream *streams;

	/* Firmware Reset */
	int fw_reset_counter;
	unsigned long r_flags;

	/* Tasklet Flags */
	unsigned long t_flags;

	int end_gop;

	/* Digitizer type */
	int digitizer;		/* 0x00EF = saa7114 0x00FO = saa7115 0x0106 = mic */

	/* API Commands */
	struct api_cmd api[256];

	atomic_t streams_setup;

	struct ivtv_dma_settings dma_cfg;

	atomic_t r_intr;
	atomic_t w_intr;
	wait_queue_head_t r_intr_wq;
	wait_queue_head_t w_intr_wq;

	atomic_t yuv_num;

	u32 vbi_raw_size;
	u32 vbi_sliced_size;
};

/* Globals */
extern struct ivtv *ivtv_cards[];
extern int ivtv_cards_active;
extern int ivtv_first_minor;
extern spinlock_t ivtv_cards_lock;
extern char *ivtv_efw;
extern char *ivtv_dfw;
 /*SECAM*/
/*==============Prototypes==================*/

/* Hardware/IRQ */
void ivtv_set_irq_mask(struct ivtv *itv, unsigned long mask);
void ivtv_clear_irq_mask(struct ivtv *itv, unsigned long mask);

/* Return non-zero if a signal is pending */
int ivtv_sleep_timeout(int timeout, int intr);

/* debug stuff, to get the locking right */
#ifndef WARN_ON
#define WARN_ON(condition) do { \
	if (unlikely((condition)!=0)) { \
		printk("Badness in %s at %s:%d\n", __FUNCTION__, __FILE__, __LINE__); \
		dump_stack(); \
	} \
} while (0)
#endif /* WARN_ON */

#define IVTV_ASSERT(x)	WARN_ON(!(x))

/* This is a PCI post thing, where if the pci register is not read, then
   the write doesn't always take effect right away. It also seems to add
   in a little latency with that.
   Basically without this writel/readl construct some motherboards seem
   to kill the firmware and get into the broken state until computer is
   rebooted. */
static inline void ivtv_write_reg(u32 val, void *reg)
{
	writel(val, reg);
	readl(reg);
}

static inline int ivtv_sem_count(struct semaphore *sem)
{
	return atomic_read(&sem->count);
}

#endif /* IVTV_DRIVER_H */
