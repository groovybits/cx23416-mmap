/*
    ivtv driver initialization and card probing
    Copyright (C) 2003-2004  Kevin Thayer <nufan_wfk at yahoo.com>

    Card autodetect:
    Copyright (C) 2004  Hans Verkuil <hverkuil@xs4all.nl>

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

/* Main Driver file for the ivtv project:
 * Driver for the Conexant CX23415/CX23416 chip.
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

#include "ivtv-driver.h"
#include "ivtv-version.h"
#include "ivtv-fileops.h"
#include "ivtv-i2c.h"
#include "ivtv-firmware.h"
#include "ivtv-queue.h"
#include "ivtv-irq.h"
#include "ivtv-mailbox.h"
#include "ivtv-streams.h"
#include "ivtv-ioctl.h"
#include "ivtv-cards.h"
#include "ivtv-vbi.h"
#include "ivtv-audio.h"
#include "v4l2-common.h"

#ifdef LINUX26
#include <linux/vermagic.h>
#else
#include <linux/version.h>
#include <linux/kmod.h>
#endif /* LINUX26 */

/* Version info*/
#define IVTV_DEFINE_VERSION_INTERNAL(name, major, minor, patchlevel, comment) \
	uint32_t IVTV_VERSION_NUMBER_INTERNAL(name) = \
		((major << 16) | (minor << 8) | (patchlevel)); \
	const char * const IVTV_VERSION_STRING_INTERNAL(name) = \
		#major"."#minor"."#patchlevel;\
	const char * const IVTV_VERSION_COMMENT_INTERNAL(name) = \
		comment;

#define IVTV_DEFINE_VERSION(name, major, minor, patchlevel, comment) \
		IVTV_DEFINE_VERSION_INTERNAL(name, major, minor, patchlevel, comment)

IVTV_DEFINE_VERSION(IVTV_VERSION_INFO_NAME,
		    IVTV_DRIVER_VERSION_MAJOR,
		    IVTV_DRIVER_VERSION_MINOR,
		    IVTV_DRIVER_VERSION_PATCHLEVEL,
		    IVTV_DRIVER_VERSION_COMMENT);

/* mini header */

/* var to keep track of the number of array elements in use */
int ivtv_cards_active = 0;

/* If you have already X v4l cards, then set this to X. This way
   the device numbers stay matched. Example: you have a WinTV card
   without radio and a PVR-350 with. Normally this would give a
   video1 device together with a radio0 device for the PVR. By
   setting this to 1 you ensure that radio0 is now also radio1. */
int ivtv_first_minor = 0;

/* Master variable for all ivtv info */
struct ivtv *ivtv_cards[IVTV_MAX_CARDS];

/* Protects ivtv_cards_active */
spinlock_t ivtv_cards_lock = SPIN_LOCK_UNLOCKED;

/* add your revision and whatnot here */
static struct pci_device_id ivtv_pci_tbl[] __devinitdata = {
	{PCI_VENDOR_ID_ICOMP, PCI_DEVICE_ID_IVTV16,
	 PCI_ANY_ID, PCI_ANY_ID, 0, 0, 0},
	{0,}
};

MODULE_DEVICE_TABLE(pci,ivtv_pci_tbl);

/* Parameter declarations */
static int cardtype[IVTV_MAX_CARDS];
static int tuner[IVTV_MAX_CARDS] = { -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1 };
static int radio[IVTV_MAX_CARDS] = { -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1 };
/* tuner.h tuner type for ivtv card */
static int tda9887[IVTV_MAX_CARDS] = { -2, -2, -2, -2, -2, -2, -2, -2, -2, -2, -2, -2 };


static int cardtype_c = 1;
static int tuner_c = 1;
static int radio_c = 1;
static int tda9887_c = 1;

#ifdef MODULE
static unsigned int autoload = 1;
#else
static unsigned int autoload = 0;
#endif /* MODULE */

static int ivtv_std = 0;

/* Buffers */
static int max_mpg_buffers = IVTV_MAX_MPG_BUFFERS;
static int max_yuv_buffers = IVTV_MAX_YUV_BUFFERS;
static int max_vbi_buffers = IVTV_MAX_VBI_BUFFERS;
static int max_pcm_buffers = IVTV_MAX_PCM_BUFFERS;

char *ivtv_efw = NULL;
char *ivtv_dfw = NULL;

int ivtv_debug = IVTV_DBGFLG_WARN;

int errno;

int newi2c = 1;

module_param_array(tuner, int, &tuner_c, 0644);
module_param_array(radio, bool, &radio_c, 0644);
module_param_array(cardtype, int, &cardtype_c, 0644);
module_param_array(tda9887, int, &tda9887_c, 0644);
module_param(autoload, int, 0644);
module_param(ivtv_debug, int, 0644);
module_param(ivtv_std, int, 0644);
module_param(ivtv_efw, charp, 0644);
module_param(ivtv_dfw, charp, 0644);
module_param(ivtv_first_minor, int, 0644);
module_param(newi2c, int, 0644);

MODULE_PARM_DESC(tuner, "Tuner type selection,\n"
			"\t\t\tsee tuner.h for values");
MODULE_PARM_DESC(tda9887,
		 "Configure or disable tda9887 module. Use only if autodetection\n"
		 "\t\t\tfails. -1 = disable, 0 = normal, 1 = no QSS");
MODULE_PARM_DESC(radio,
		 "Enable or disable the radio. Use only if autodetection\n"
		 "\t\t\tfails. 0 = disable, 1 = enable");
MODULE_PARM_DESC(autoload,
		 "automatically load i2c modules like tuner.o,\n"
		 "\t\t\tdefault is 1 (yes)");
MODULE_PARM_DESC(cardtype,
		 "Only use this option if your card is not detected properly.\n"
		 "\t\tSpecify card type:\n"
		 "\t\t\t6 = WinTV PVR-150 or PVR-500\n"
		 "\t\t\tDefault: Autodetect");
MODULE_PARM_DESC(ivtv_debug,
		 "Debug level (bitmask). Default: errors only\n"
		 "\t\t\t(ivtv_debug = 511 gives full debugging)");
MODULE_PARM_DESC(ivtv_std,
		 "Specify video standard: 1 = NTSC, 2 = PAL, 3 = SECAM,\n"
		 "\t\t\tDefault: Autodetect");
MODULE_PARM_DESC(ivtv_efw,
		 "Encoder firmware image\n"
		 "\t\t\tDefault: " IVTV_FIRM_ENC_FILENAME);

MODULE_PARM_DESC(newi2c,
		 "Use new I2C implementation\n"
		 "\t\t\t default is 1 (yes)");

MODULE_PARM_DESC(ivtv_first_minor, "Set minor assigned to first card");

MODULE_AUTHOR("Chris Kennedy, Kevin Thayer, Hans Verkuil");
MODULE_DESCRIPTION("CX23416 driver");
MODULE_SUPPORTED_DEVICE
    ("CX23416 MPEG2 encoder (WinTV PVR-150/500,\n");
MODULE_LICENSE("GPL");

#if defined(LINUX26)
MODULE_VERSION(IVTV_VERSION);
#endif /* defined(LINUX26) */

void ivtv_clear_irq_mask(struct ivtv *itv, unsigned long mask)
{
	itv->irqmask &= ~mask;
	writel(itv->irqmask, (itv->reg_mem + IVTV_REG_IRQMASK));

	/* pci posting */
	readl(itv->reg_mem + IVTV_REG_IRQMASK);
}

void ivtv_set_irq_mask(struct ivtv *itv, unsigned long mask)
{
	itv->irqmask |= mask;
	writel(itv->irqmask, (itv->reg_mem + IVTV_REG_IRQMASK));

	/* pci posting */
	readl(itv->reg_mem + IVTV_REG_IRQMASK);
}

/* Release ioremapped memory */
static void ivtv_iounmap(struct ivtv *itv)
{
	if (itv == NULL)
		return;

	/* Release registers memory */
	if (itv->reg_mem != NULL) {
		IVTV_DEBUG_INFO("releasing reg_mem\n");
		iounmap(itv->reg_mem);
		itv->reg_mem = NULL;
	}

	/* Release io memory */
	if (itv->enc_mem != NULL) {
		IVTV_DEBUG_INFO("releasing enc_mem\n");
		iounmap(itv->enc_mem);
		itv->enc_mem = NULL;
        }

}

/* Hauppauge card? get values from tveeprom */
static int ivtv_read_eeprom(struct ivtv *itv)
{
	const struct ivtv_card *card = itv->card;
	u32 eepromdata[5];
	int ret;

	ret = ivtv_hauppauge(itv, 0, eepromdata);

	if (ret) {
		IVTV_ERR("Error %d reading Hauppauge eeprom.\n", ret);
		IVTV_ERR
		    ("Possible causes: the tveeprom module was not loaded, or\n");
		IVTV_ERR
		    ("the eeprom kernel module was loaded before the tveeprom module.\n");
		return ret;
	}

        /* if the model is a 23xxx (PVR500), and this is the 'PVR150' on
           that card (subsystem device ID has bit 4 set), then the eeprom
           reported incorrectly that a radio is present. A radio is only
           available for the first 'PVR150'. 
         
           Many thanks to Hauppauge for providing this data. */
        if (eepromdata[2] / 1000 == 23) {
                int is_first = (itv->dev->subsystem_device & 0x0010) == 0;

                IVTV_INFO("This is the %s unit of a PVR500\n",
                                is_first ? "first" : "second");
                itv->card_name = is_first ? "WinTV PVR 500 (unit #1)" :
                                            "WinTV PVR 500 (unit #2)";
                if (!is_first) {
                        IVTV_INFO("Correcting tveeprom data: no radio present on second unit\n");
                        eepromdata[4] = 0;
                }
        }

        switch (eepromdata[2]) {
                // In a few cases the PCI subsystem IDs do not correctly
		// identify the card. A better method is to check the
		// model number from the eeprom instead.
                case 26000 ... 26199:
                        itv->card = ivtv_get_card(IVTV_CARD_PVR_150);
                        break;
		case 32000 ... 32099:
                        itv->card = ivtv_get_card(IVTV_CARD_PVR_250);
			break;
		/* Old style PVR350 (with an saa7114) uses this input for
		   the tuner. */
		case 48254:
			itv->msp34xx_tuner_input = 7;
			break;
                default:
                        break;
        }
	if (card != itv->card) {
        	itv->card_name = itv->card->name;
		IVTV_INFO("Corrected autodetection from %s to %s\n",
				card->name, itv->card_name);
	}

	if (itv->options.tuner == -1)
		itv->options.tuner = eepromdata[0];
	itv->pvr150_workaround = itv->options.tuner == TUNER_TCL_2002N;
	if (itv->options.radio == -1)
		itv->options.radio = (eepromdata[4] != 0);
	if (itv->std != 0) {
                // user specified tuner standard
                return ret;
        }

        /* autodetect tuner standard */
	itv->height = 576;
	itv->width = 720;
        if (eepromdata[1] & V4L2_STD_PAL) {
                IVTV_DEBUG_INFO("PAL tuner detected\n");
                itv->std |= V4L2_STD_PAL;
                itv->codec.framespergop = 12;
                ivtv_std = 2;
        } else if (eepromdata[1] & V4L2_STD_NTSC) {
                IVTV_DEBUG_INFO("NTSC tuner detected\n");
                itv->std |= V4L2_STD_NTSC;
                itv->codec.framespergop = 15;
                ivtv_std = 1;
		itv->height = 480;
		itv->width = 720;
        } else if (eepromdata[1] & V4L2_STD_SECAM) {
                IVTV_DEBUG_INFO("SECAM tuner detected\n");
                itv->std |= V4L2_STD_SECAM;
                itv->codec.framespergop = 12;
                ivtv_std = 3;
        } else if (ivtv_std == 0) {
                IVTV_INFO("No tuner detected, default to NTSC\n");
                itv->std |= V4L2_STD_NTSC;
                itv->codec.framespergop = 15;
                ivtv_std = 1;
        } else {
                IVTV_INFO("No tuner detected, and std forced to %d\n", 
                                ivtv_std);
        }
	return ret;
}

static void ivtv_process_options(struct ivtv *itv)
{
        const char *chipname;
	int i, j;

	/* Buffers are now dynamically allocated, could specify limit though */
	itv->options.cardtype = cardtype[itv->num];
	itv->options.tuner = tuner[itv->num];
	itv->options.radio = radio[itv->num];
	itv->options.tda9887 = tda9887[itv->num];
	itv->options.newi2c = newi2c;

        chipname = "cx23416";
	if ((itv->card = ivtv_get_card(itv->options.cardtype - 1))) {
		IVTV_INFO("User specified %s card (detected %s based chip)\n",
                                itv->card->name, chipname);
	} else {
                if (itv->options.cardtype != 0) {
		        IVTV_ERR("Unknown user specified type, trying to autodetect card\n");
                }
		for (i = 0; (itv->card = ivtv_get_card(i)); i++) {
			for (j = 0; itv->card->pci_list[j].device; j++) {
				if (itv->dev->device !=
				    itv->card->pci_list[j].device)
					continue;
				if (itv->dev->subsystem_vendor !=
				    itv->card->pci_list[j].subsystem_vendor)
					continue;
				if (itv->dev->subsystem_device !=
				    itv->card->pci_list[j].subsystem_device)
					continue;
				IVTV_INFO("Autodetected %s card (%s based)\n",
                                                itv->card->name, chipname);
				goto done;
			}
		}

	      done:
		if (itv->card == NULL) {
			itv->card = ivtv_get_card(IVTV_CARD_PVR_150);
			IVTV_ERR("Unknown card: vendor/device: %04x/%04x\n",
			     itv->dev->vendor, itv->dev->device);
                        IVTV_ERR("              subsystem vendor/device: %04x/%04x\n",
			     itv->dev->subsystem_vendor, itv->dev->subsystem_device);
                        IVTV_ERR("              %s based\n", chipname);
			IVTV_ERR("Defaulting to %s card\n", itv->card->name);
			IVTV_ERR("Please mail the vendor/device and subsystem vendor/device IDs and what kind of\n");
			IVTV_ERR("card you have to the ivtv-devel mailinglist (www.ivtvdriver.org)\n");
			IVTV_ERR("Prefix your subject line with [UNKNOWN CARD].\n");
		}
	}
	itv->v4l2_cap = itv->card->v4l2_capabilities;
        itv->card_name = itv->card->name;

	/* set the standard */
	itv->height = 576;
	itv->width = 720;
	switch (ivtv_std) {
	case 1:
		itv->std = V4L2_STD_NTSC;
		itv->codec.framespergop = 15;
		itv->height = 480;
		itv->width = 720;
		break;
	case 2:
		itv->std = V4L2_STD_PAL;
		itv->codec.framespergop = 12;
		break;
	case 3:
		itv->std = V4L2_STD_SECAM;
		itv->codec.framespergop = 12;
		break;
	default:
		break;
	}
}

/* Precondition: the ivtv structure has been memset to 0. Only
   the dev and num fields have been filled in.
   No assumptions on the card type may be made here (see ivtv_init_struct2
   for that).
 */
static int __devinit ivtv_init_struct1(struct ivtv *itv)
{
	itv->base_addr = pci_resource_start(itv->dev, 0);
	itv->enc_mem = NULL;
	itv->reg_mem = NULL;

	init_MUTEX(&itv->enc_msem);
	init_MUTEX(&itv->streams_lock);
	init_MUTEX(&itv->i2c_lock);
	init_MUTEX(&itv->DMA_lock);
	init_MUTEX(&itv->i2c_bus_lock);

	itv->DMAP = 0;

	itv->DMA_slock = SPIN_LOCK_UNLOCKED;
	spin_lock_init(&itv->DMA_slock);
	spin_lock_init(&ivtv_cards_lock);

	/* start counting open_id at 1 */
	itv->open_id = 1;

	/* Initial settings */
	itv->codec.bitrate_mode = 0;
	itv->codec.bitrate = 8000000;
	itv->codec.bitrate_peak = 9600000;
	itv->codec.stream_type = IVTV_STREAM_DVD_S2;
	itv->codec.bframes = 3;
	itv->codec.gop_closure = 1;
	itv->codec.dnr_mode = 0;
	itv->codec.dnr_type = 0;
	itv->codec.dnr_spatial = 0;
	itv->codec.dnr_temporal = 8;
	itv->codec.aspect = 2;
	itv->codec.pulldown = 0;
	/* Freq 48 kHz, Layer II, 384 kbit/s */
	itv->codec.audio_bitmask = 1 | (2 << 2) | (14 << 4);

	init_waitqueue_head(&itv->cap_w);

	/* DMA Settings */
	itv->dma_cfg.enc_buf_size = IVTV_DMA_ENC_BUF_SIZE;
        itv->dma_cfg.enc_yuv_buf_size = IVTV_DMA_ENC_YUV_BUF_SIZE;
	itv->dma_cfg.enc_pcm_buf_size = IVTV_DMA_ENC_PCM_BUF_SIZE;

	itv->dma_cfg.max_mpg_buf = max_mpg_buffers;
	itv->dma_cfg.max_yuv_buf = max_yuv_buffers;
	itv->dma_cfg.max_vbi_buf = max_vbi_buffers;
	itv->dma_cfg.max_pcm_buf = max_pcm_buffers;

	//itv->dma_cfg.fw_enc_dma_xfer = FW_ENC_DMA_XFER;
	//itv->dma_cfg.fw_enc_dma_type = FW_ENC_DMA_TYPE;
	itv->dma_cfg.vbi_pio = IVTV_VBI_PIO;
	itv->dma_cfg.enc_pio = IVTV_ENC_PIO;

	/* Encoder Options */
	itv->end_gop = 0;
	itv->idx_sdf_offset = 0;
	itv->idx_sdf_num = 400;
	itv->idx_sdf_mask = 7;

	itv->dmaboxnum = 5;

	// vbi
	itv->vbi_in.type = V4L2_BUF_TYPE_SLICED_VBI_CAPTURE;
	itv->vbi_sliced_in = &itv->vbi_in.fmt.sliced;
	itv->vbi_sliced_in->service_set = 0;	// marks raw VBI
	itv->vbi_service_set_out = 0;
	itv->vbi_frame = 0;
	itv->vbi_inserted_frame = 0;
	itv->vbi_passthrough = 0;
	itv->vbi_insert_mpeg = 0;

	itv->msp34xx_tuner_input = 3;   /* tuner input for msp34xx based cards */
	itv->msp34xx_audio_output = 1;	/* SCART1 output for msp34xx based cards */
	itv->audio_input_tv = 0;	/* TV tuner */
	itv->audmode_tv = itv->audmode_radio = V4L2_TUNER_MODE_STEREO;

	return 0;
}

// Second initialization part. Here the card type has been
// autodetected.
static void __devinit ivtv_init_struct2(struct ivtv *itv)
{
	const struct v4l2_input *input;
        int i;

	itv->audio_input_radio = 2;	/* Radio tuner */

        /* 0x00EF = saa7114(239) 0x00F0 = saa7115(240) 0x0106 = micro */
        switch (itv->card->type) {
        case IVTV_CARD_PVR_150:
                itv->digitizer = 0x140;
		itv->audio_input_radio = 4;	/* Radio tuner */
                break;
        default:
                itv->digitizer = 0x140;
		itv->audio_input_radio = 4;	/* Radio tuner */
                break;
        }

        itv->vbi_sliced_size = 288;  // multiple of 16, real size = 284

	/* Find tuner input */
	for (i = 0; (input = ivtv_get_input(itv, i)); i++) {
		if (input->type == V4L2_INPUT_TYPE_TUNER)
			break;
	}
	if (input == NULL)
		i = 0;
	itv->active_input = i;
}

static int ivtv_setup_pci(struct ivtv *itv, struct pci_dev *dev,
			  const struct pci_device_id *pci_id)
{
	u16 cmd;
	unsigned char pci_latency;

	IVTV_DEBUG_INFO("Enabling pci device\n");

	if (pci_enable_device(dev)) {
		IVTV_ERR("Can't enable device %d!\n", itv->num);
		return -EIO;
	}
	if (pci_set_dma_mask(dev, 0xffffffff)) {
		IVTV_ERR("No suitable DMA available on card %d.\n",
			      itv->num);
		return -EIO;
	}
	if (!request_mem_region(pci_resource_start(dev, 0),
				IVTV_IOREMAP_SIZE, itv->name)) {
		IVTV_ERR("Cannot request memory region on card %d.\n",
			      itv->num);
		return -EIO;
	}

	/* Check for bus mastering */
	pci_read_config_word(dev, PCI_COMMAND, &cmd);
	if (!(cmd & PCI_COMMAND_MASTER)) {
		IVTV_DEBUG_INFO("Attempting to enable Bus Mastering\n");
		pci_set_master(dev);
		pci_read_config_word(dev, PCI_COMMAND, &cmd);
		if (!(cmd & PCI_COMMAND_MASTER)) {
			IVTV_ERR("Bus Mastering is not enabled\n");
			return -ENXIO;
		}
	} 
	IVTV_DEBUG_INFO("Bus Mastering Enabled.\n");

	pci_read_config_byte(dev, PCI_CLASS_REVISION, &itv->card_rev);
	pci_read_config_byte(dev, PCI_LATENCY_TIMER, &pci_latency);

	if (pci_latency < 64) {
		IVTV_INFO("Unreasonably low latency timer, "
			       "setting to 64 (was %d)\n", pci_latency);
		pci_write_config_byte(dev, PCI_LATENCY_TIMER, 64);
		pci_read_config_byte(dev, PCI_LATENCY_TIMER, &pci_latency);
	}
	pci_write_config_dword(dev, 0x40, 0xffff);

	IVTV_DEBUG_INFO("%d (rev %d) at %02x:%02x.%x, "
		   "irq: %d, latency: %d, memory: 0x%lx\n",
		   itv->dev->device, itv->card_rev, dev->bus->number,
		   PCI_SLOT(dev->devfn), PCI_FUNC(dev->devfn),
		   itv->dev->irq, pci_latency, (unsigned long)itv->base_addr);

	return 0;
}

static inline void ivtv_request_module(struct ivtv *itv, const char *name)
{
	if (request_module(name) != 0) {
		IVTV_ERR("Failed to load module %s\n", name);
	} else {
		IVTV_DEBUG_INFO("Loaded module %s\n", name);
	}
}

static int __devinit ivtv_probe(struct pci_dev *dev,
				const struct pci_device_id *pci_id)
{
	int retval = 0;
	int cfg;
	struct ivtv *itv;
        struct v4l2_frequency vf;

	spin_lock(&ivtv_cards_lock);

	/* Make sure we've got a place for this card */
	if (ivtv_cards_active == IVTV_MAX_CARDS) {
		printk(KERN_ERR "ivtv:  Maximum number of cards detected (%d).\n",
			      ivtv_cards_active);
		spin_unlock(&ivtv_cards_lock);
		return -ENOMEM;
	}

	itv = kmalloc(sizeof(struct ivtv), GFP_ATOMIC);
	if (itv == 0) {
		spin_unlock(&ivtv_cards_lock);
		return -ENOMEM;
        }
	ivtv_cards[ivtv_cards_active] = itv;
	memset(itv, 0, sizeof(*itv));
	itv->dev = dev;
	itv->num = ivtv_cards_active++;
	snprintf(itv->name, sizeof(itv->name) - 1, "ivtv%d", itv->num);
        if (itv->num) {
                printk(KERN_INFO "ivtv:  ======================  NEXT CARD  ======================\n");
        }

	spin_unlock(&ivtv_cards_lock);

	ivtv_process_options(itv);
	if (ivtv_init_struct1(itv)) {
		retval = -ENOMEM;
		goto err;
	}

	//init_waitqueue_head(&itv->w_intr_wq);
	//init_waitqueue_head(&itv->r_intr_wq);

	IVTV_DEBUG_INFO("base addr: 0x%08x\n", itv->base_addr);

	/* PCI Device Setup */
	if ((retval = ivtv_setup_pci(itv, dev, pci_id)) != 0) {
		//if (retval == -EIO)
		//	goto free_workqueue;
		/*else*/ if (retval == -ENXIO)
			goto free_mem;
	}
	/* save itv in the pci struct for later use */
	pci_set_drvdata(dev, itv);

	/* map io memory */
	IVTV_DEBUG_INFO("attempting ioremap at 0x%08x len 0x%08x\n",
		   itv->base_addr + IVTV_ENCODER_OFFSET, IVTV_ENCODER_SIZE);
	itv->enc_mem = ioremap_nocache(itv->base_addr + IVTV_ENCODER_OFFSET,
				       IVTV_ENCODER_SIZE);

	if (!itv->enc_mem) {
		IVTV_ERR("ioremap failed, perhaps increasing "
		         "__VMALLOC_RESERVE in page.h\n");
		IVTV_ERR("or disabling CONFIG_HIMEM4G "
		         "into the kernel would help\n");
		retval = -ENOMEM;
		goto free_mem;
	}

	/* map registers memory */
	IVTV_DEBUG_INFO(
		   "attempting ioremap at 0x%08x len 0x%08x\n",
		   itv->base_addr + IVTV_REG_OFFSET, IVTV_REG_SIZE);
	itv->reg_mem =
	    ioremap_nocache(itv->base_addr + IVTV_REG_OFFSET, IVTV_REG_SIZE);
	if (!itv->reg_mem) {
		IVTV_ERR("ioremap failed, perhaps increasing "
		         "__VMALLOC_RESERVE in page.h\n");
		IVTV_ERR("or disabling CONFIG_HIMEM4G "
		         "into the kernel would help\n");
		retval = -ENOMEM;
		goto free_io;
	}

	/* active i2c  */
	IVTV_DEBUG_INFO("activating i2c...\n");
	if (init_ivtv_i2c(itv)) {
		IVTV_ERR("Could not initialize i2c\n");
		goto free_irq;
	}

	IVTV_DEBUG_INFO(
		   "Active card count: %d.\n", ivtv_cards_active);

	switch (itv->card->type) {
	/* Hauppauge models */
	case IVTV_CARD_PVR_150:
#ifndef CONFIG_VIDEO_TVEEPROM
		if (autoload)
			ivtv_request_module(itv, "tveeprom");
#endif /* CONFIG_VIDEO_TVEEPROM */
                itv->hw_flags |= IVTV_HW_TVEEPROM;
                // Based on the model number the cardtype may be changed.
                // The PCI IDs are not always reliable.
		ivtv_read_eeprom(itv);
		break;
	default:
		break;
	}

        // The card is now fully identified, continue with card-specific
        // initialization.
	ivtv_init_struct2(itv);

	if (autoload) {
#ifndef CONFIG_VIDEO_TUNER
		ivtv_request_module(itv, "tuner");
#endif /* CONFIG_VIDEO_TUNER */
		if (itv->card->type == IVTV_CARD_PVR_150) {
			struct v4l2_control ctrl;

			ctrl.id = V4L2_CID_PRIVATE_BASE; /* CX25840_CID_CARDTYPE */
			ctrl.value = itv->pvr150_workaround ? 2 : 0;

#ifndef CONFIG_VIDEO_DECODER
			ivtv_request_module(itv, "cx25840");
#endif /* CONFIG_VIDEO_DECODER */
		        itv->card->video_dec_func(itv, VIDIOC_S_CTRL, &ctrl);
                        itv->has_cx25840 = 1;
                        itv->hw_flags |= IVTV_HW_CX25840;
                        itv->vbi_raw_decoder_line_size = 1444;
                        itv->vbi_raw_decoder_sav_odd_field = 0x20;
                        itv->vbi_raw_decoder_sav_even_field = 0x60;
                        itv->vbi_sliced_decoder_line_size = 272;
                        itv->vbi_sliced_decoder_sav_odd_field = 0xB0;
                        itv->vbi_sliced_decoder_sav_even_field = 0xF0;
			if (itv->card->type == IVTV_CARD_PVR_150) {
#ifndef CONFIG_VIDEO_AUDIO_DECODER
				ivtv_request_module(itv, "wm8775");
#endif /* CONFIG_VIDEO_AUDIO_DECODER */
                                itv->hw_flags |= IVTV_HW_WM8775;
			}
		}
                // -2 == autodetect, -1 == no tda9887
		if (itv->options.tda9887 != -1) {
#ifndef CONFIG_VIDEO_TUNER
			ivtv_request_module(itv, "tda9887");
#endif /* CONFIG_VIDEO_TUNER */
		}
                /* when tda9887 is loaded and detects the chip it will
                   call attach_inform() in ivtv-i2c.c which will set the
                   itv->has_tda9887 variable to 1. */
                if (itv->has_tda9887) {
                        itv->hw_flags |= IVTV_HW_TDA9887;
                }
                if (itv->options.tda9887 == -2 && itv->has_tda9887)
                        itv->options.tda9887 = 0;
	}

	if (itv->std <= 0) {
		IVTV_ERR("Could not detect tuner standard, defaulting to NTSC.\n");
		itv->std = V4L2_STD_NTSC;
		ivtv_std = 1;
	}

        if (itv->std & V4L2_STD_525_60) {
                itv->is_60hz = 1;
        } else {
                itv->is_50hz = 1;
        }

        /* Setup YUV Encoding/Decoding Buffers */
        itv->dma_cfg.enc_yuv_buf_size = IVTV_DMA_ENC_YUV_BUF_SIZE;

        /* Setup VBI Raw Size. Should be big enough to hold PAL.
           It is possible to switch between PAL and NTSC, so we need to
           take the largest size here. */
        // 1456 is multiple of 16, real size = 1444
        itv->vbi_raw_size = 1456;
        // We use a buffer size of 1/2 of the total size needed for a
        // frame. This is actually very useful, since we now receive
        // a field at a time and that makes 'compressing' the raw data
        // down to size by stripping off the SAV codes a lot easier.
        if (itv->is_60hz) {
                // Note: having two different buffer sizes prevents standard
                // switching on the fly. We need to find a better solution...
                //itv->dma_cfg.vbi_buf_size = itv->vbi_raw_size * 24 / 2;
		// Raw
                itv->dma_cfg.vbi_buf_size = itv->vbi_raw_size * 24;
		// Sliced (crap)
                //itv->dma_cfg.vbi_buf_size = (((288*24)+16) * 4)-16;
        } else {
                itv->dma_cfg.vbi_buf_size = itv->vbi_raw_size * 36 / 2;
        }

        if (itv->i2c_radio_tuner_addr) {
                IVTV_INFO("Detected a TEA5767 radio tuner. Enabling radio support.\n");
                itv->options.radio = 1;
        }
	if (itv->options.radio == -1)
		itv->options.radio = 0;
	if (itv->options.radio)
		itv->v4l2_cap |= V4L2_CAP_RADIO;

	/* write firmware */
	retval = ivtv_firmware_init(itv);
	if (retval) {
		IVTV_ERR("Error %d initializing firmware.\n", retval);
		retval = -ENOMEM;
		goto free_i2c;
	}

	/* search for encoder/decoder mailboxes */
	IVTV_DEBUG_INFO("About to search for mailboxes\n");
	ivtv_find_enc_firmware_mailbox(itv);

	if (itv->enc_mbox == NULL) {
		IVTV_ERR("Error locating firmware.\n");
		retval = -ENOMEM;
		goto free_i2c;
	}

	if (ivtv_check_firmware(itv)) {
		IVTV_ERR("Error initializing firmware\n");
		retval = -EINVAL;
		goto free_i2c;
	}

	/* Try and get firmware versions */
	IVTV_DEBUG_INFO("Getting firmware version..\n");
	retval = ivtv_firmware_versions(itv);
	if (retval) {
		IVTV_ERR("Error %d getting firmware version!\n", retval);
		goto free_i2c;
	}

	retval = ivtv_streams_setup(itv);
	if (retval) {
		IVTV_ERR("Error %d setting up streams\n", retval);
		goto free_i2c;
	}

	/* clear interrupt mask, effectively disabling interrupts */
	ivtv_set_irq_mask(itv, 0xffffffff);

	/* Register IRQ */
	retval = request_irq(itv->dev->irq, ivtv_irq_handler,
			     SA_SHIRQ | SA_INTERRUPT, itv->name, (void *)itv);
	if (retval) {
		IVTV_ERR("Failed to register irq %d\n", retval);
		goto free_streams;
	}

	/* Default interrupts enabled. For the PVR350 this includes the
           decoder VSYNC interrupt, which is always on. It is not only used
           during decoding but also by the OSD.
           Some old PVR250 cards had a cx23415, so testing for that is too
           general. Instead test if the card has video output capability. */

	ivtv_clear_irq_mask(itv, IVTV_IRQ_MASK_INIT);

	if (itv->options.tuner > -1) {
#if LINUX_VERSION_CODE >= KERNEL_VERSION(2, 6, 13)
                {
                        struct tuner_setup setup;

                        setup.addr = ADDR_UNSET;
                        setup.type = itv->options.tuner;
                        setup.mode_mask = T_ANALOG_TV;
                        if (itv->options.radio && itv->i2c_radio_tuner_addr == 0)
                                setup.mode_mask |= T_RADIO;
        		ivtv_tv_tuner(itv, TUNER_SET_TYPE_ADDR, &setup);
                }
#else
		ivtv_tv_tuner(itv, TUNER_SET_TYPE, &(itv->options.tuner));
#endif /* LINUX_VERSION_CODE >= KERNEL_VERSION(2, 6, 13) */
	}

        vf.tuner = 0;
	vf.type = V4L2_TUNER_ANALOG_TV;
        vf.frequency = 6400; /* the tuner 'baseline' frequency */
	if (itv->std & V4L2_STD_NTSC_M) {
		/* Why on earth? */
		vf.frequency = 1076;	/* ch. 4 67250*16/1000 */
	}

	/* The tuner is fixed to the standard. The other inputs (e.g. S-Video)
	   are not. */
	itv->tuner_std = itv->std;

        /* Digitizer */
	itv->card->video_dec_func(itv, VIDIOC_S_STD, &itv->std);

	/* Change Input to active, 150 needs this, seems right */
	itv->card->video_dec_func(itv, VIDIOC_S_INPUT, &itv->active_input);

	/* Init new audio input/output */
	ivtv_audio_set_io(itv);

	/* Let the VIDIOC_S_STD ioctl do all the work, keeps the code
	   in one place. */
	itv->std++;		// Force full standard initialization
	ivtv_v4l2_ioctls(NULL, itv, NULL, 0, VIDIOC_S_STD, &itv->tuner_std);

	switch (itv->options.tuner) {
	case TUNER_PHILIPS_FM1216ME_MK3:
	case TUNER_PHILIPS_FM1236_MK3:
                cfg = TDA9887_PORT1_ACTIVE|TDA9887_PORT2_ACTIVE;
                ivtv_tda9887(itv, TDA9887_SET_CONFIG, &cfg);
		itv->set_fm_high_sensitivity = 1;
		break;
	
	case TUNER_PHILIPS_FQ1216AME_MK4:
	case TUNER_PHILIPS_FQ1236A_MK4:
                cfg = TDA9887_PORT1_ACTIVE|TDA9887_PORT2_INACTIVE;
                ivtv_tda9887(itv, TDA9887_SET_CONFIG, &cfg);
		itv->set_fm_high_sensitivity = 1;
		break;
	}
	ivtv_v4l2_ioctls(NULL, itv, NULL, 0, VIDIOC_S_FREQUENCY, &vf);

	IVTV_INFO("Initialized %s, card #%d\n", itv->card_name, itv->num);

	return 0;

      free_irq:
	free_irq(itv->dev->irq, (void *)itv);
      free_streams:
	ivtv_streams_cleanup(itv);
      free_i2c:
	exit_ivtv_i2c(itv);
      free_io:
	ivtv_iounmap(itv);
      free_mem:
	release_mem_region(pci_resource_start(itv->dev, 0), IVTV_IOREMAP_SIZE);
      err:
        if (retval == 0)
		retval = -ENODEV;

	IVTV_ERR("Error %d on initialization\n", retval);

	spin_lock(&ivtv_cards_lock);
	ivtv_cards_active--;
	spin_unlock(&ivtv_cards_lock);

	pci_disable_device(itv->dev);

	return retval;
}

static void ivtv_remove(struct pci_dev *pci_dev)
{
	struct ivtv *itv = pci_get_drvdata(pci_dev);

	/* Lock firmware reloads */
	itv->fw_reset_counter = 99;
	set_bit(FW_RESET_SHUTDOWN, &itv->r_flags);

	IVTV_DEBUG_INFO("Removing Card #%d.\n", itv->num);

	/* Stop all captures */
	IVTV_DEBUG_INFO(" Stopping all streams.\n");
	itv->end_gop = 0;
	if (atomic_read(&itv->capturing) > 0) {
		ivtv_stop_all_captures(itv);
	}

	/* Interrupts */
	IVTV_DEBUG_INFO(" Disabling interrupts.\n");
	ivtv_set_irq_mask(itv, 0xffffffff);

	atomic_set(&itv->r_intr, 1);
	atomic_set(&itv->w_intr, 1);
	itv->t_flags = 0;

	IVTV_DEBUG_INFO(" Stopping Firmware.\n");
	ivtv_halt_firmware(itv, 3);

	IVTV_DEBUG_INFO(" Freeing dma resources.\n");
	IVTV_DEBUG_INFO(" Unregistering v4l devices.\n");
	ivtv_streams_cleanup(itv);

	exit_ivtv_i2c(itv);

	IVTV_DEBUG_INFO(" Releasing irq.\n");
	free_irq(itv->dev->irq, (void *)itv);

	if (itv->dev) {
		ivtv_iounmap(itv);
	}

	IVTV_DEBUG_INFO(" Releasing mem.\n");
	release_mem_region(pci_resource_start(itv->dev, 0), IVTV_IOREMAP_SIZE);

	pci_disable_device(itv->dev);

	IVTV_INFO("Removed %s, card #%d\n", itv->card_name, itv->num);
}

/* define a pci_driver for card detection */
static struct pci_driver ivtv_pci_driver = {
      name: 	"ivtv",
      id_table: ivtv_pci_tbl,
      probe:    ivtv_probe,
      remove:   ivtv_remove,
};

static int module_start(void)
{
	printk(KERN_INFO "ivtv:  ==================== START INIT IVTV ====================\n");
	printk(KERN_INFO "ivtv:  version %s %s By Chris Kennedy loading\n",
		       IVTV_VERSION_STRING(IVTV_VERSION_INFO_NAME),
		       IVTV_VERSION_COMMENT(IVTV_VERSION_INFO_NAME));
#ifdef LINUX26
	printk(KERN_INFO "ivtv:  Linux version: " VERMAGIC_STRING "\n");
#else
	printk(KERN_INFO "ivtv:  Linux version: " UTS_RELEASE "\n");
#endif /* LINUX26 */

	memset(ivtv_cards, 0, sizeof(ivtv_cards));

	/* Validate parameters */
	if (ivtv_first_minor < 0 || ivtv_first_minor >= IVTV_MAX_CARDS) {
		printk(KERN_ERR "ivtv:  ivtv_first_minor must be between 0 and %d. Exiting...\n",
		     IVTV_MAX_CARDS - 1);
		return -1;
	}

	if (ivtv_debug < 0 || ivtv_debug > 511) {
		ivtv_debug = 1;
		printk(KERN_INFO "ivtv:  debug value must be >= 0 and <= 511!\n");
	}

	if (pci_module_init(&ivtv_pci_driver)) {
		printk(KERN_ERR "ivtv:  Error detecting PCI card\n");
		return -ENODEV;
	}
	printk(KERN_INFO "ivtv:  ====================  END INIT IVTV  ====================\n");
	return 0;
}

static void module_cleanup(void)
{
        int i;

        for (i = 0; i < ivtv_cards_active; i++) {
                kfree(ivtv_cards[i]);
        }
	pci_unregister_driver(&ivtv_pci_driver);
}

EXPORT_SYMBOL(ivtv_set_irq_mask);
EXPORT_SYMBOL(ivtv_cards_active);
EXPORT_SYMBOL(ivtv_cards);
EXPORT_SYMBOL(ivtv_api);
EXPORT_SYMBOL(ivtv_vapi);
EXPORT_SYMBOL(ivtv_clear_irq_mask);
EXPORT_SYMBOL(ivtv_debug);
module_init(module_start);
module_exit(module_cleanup);
