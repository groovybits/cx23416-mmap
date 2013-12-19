/*
    Reset firmware function
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

#include "ivtv-driver.h"
#include "ivtv-firmware.h"
#include "ivtv-fileops.h"
#include "ivtv-i2c.h"
#include "ivtv-mailbox.h"
#include "ivtv-audio.h"
#include "ivtv-video.h"
#include "ivtv-streams.h"
#include "ivtv-reset.h"

int ivtv_firmware_flags(struct ivtv *itv, int type)
{
	/* Reload Firmware if Dead before schedule */
	if (test_and_clear_bit(FW_RESET_NEEDED, &itv->r_flags)) {
		IVTV_DEBUG_WARN(
			   "Reloading FW at %lu jiffies\n", jiffies);
		return ivtv_reset_card(itv, 1, IVTV_CMD_FULL_RESET);
	} else if (test_and_clear_bit(FW_SOFT_RESET_NEEDED, &itv->r_flags)) {
		IVTV_DEBUG_WARN(
			   "Checking FW at %lu jiffies\n", jiffies);
		return ivtv_reset_card(itv, 0, IVTV_CMD_FULL_RESET);
	}

	return 0;
}

static int ivtv_reset_firmware(struct ivtv *itv, int cmd)
{
	int ret = 0;

	switch (cmd) {
	case 0:
	case 1:
	case 2:
		break;
	default:
		IVTV_DEBUG_WARN("Invalid command %d\n", cmd);
		return -EINVAL;
	};

	ivtv_halt_firmware(itv, 3);

	/* (re)upload firmware */
	if (cmd == IVTV_CMD_FULL_RESET) {
		ret = ivtv_firmware_copy(itv);
		if (ret) {
			IVTV_DEBUG_WARN(
				   "%d Error loading firmware!\n", ret);
			return ret;
		}
	}

	/* re-init firmware */
	ivtv_start_firmware(itv);

	/* make sure firmware is OK */
	ret = ivtv_check_firmware(itv);

	/* Encoder Bad */
	if (ret /*== 1 || ret == 3*/ ) {
		int ret2;
		IVTV_DEBUG_WARN(
			   "Firmware not responding, trying mailboxes again\n");

		/* re-find mailboxes */
		ret2 = ivtv_find_enc_firmware_mailbox(itv);
		if (ret2) {
			IVTV_DEBUG_WARN(
				   "Error finding enc mailboxes!\n");
			return ret2;
		}
	}

	/* Make sure it works now */
	if (ret) {
		/* make sure firmware is OK */
		ret = ivtv_check_firmware(itv);
		if (ret) {
			IVTV_DEBUG_WARN(
				   "Firmware not responding, giving up!\n");
			return ret;
		}
	}

	return 0;
}

int ivtv_reset_card(struct ivtv *itv, int force, int reset_mode)
{
	int ret = 0, status = 0;
	int i;
	int was_encoding = 0;
	int y, captype = 0;
	int dma_err_reload = 0;
	u32 data[IVTV_MBOX_MAX_DATA], result;

	if (test_bit(FW_RESET_SHUTDOWN, &itv->r_flags)) {
		itv->fw_reset_counter = 99;
		return 0;
	}

	/* Check for pending firmware reload */
	if (test_bit(FW_RESET_LOCK, &itv->r_flags)) {
		int resetcounter = 0;
		IVTV_INFO("Already a firmware reload in progress\n");
		while (test_bit(FW_RESET_LOCK, &itv->r_flags)) {
			if (++resetcounter > 3)	/* 3 second wait */
				break;
			ivtv_sleep_timeout(HZ, 0);
		}

		/* still going */
		if (resetcounter > 3) {
			IVTV_INFO("Still reloading firmware...\n");
			return -EBUSY;
		}
		IVTV_INFO("Done reloading firmware.\n");
		return 0;
	}

	/* Make sure we really need to */
	if (!force) {
		IVTV_DEBUG_INFO("Checking Card Firmware\n");
		ret = ivtv_check_firmware(itv);
		if (!ret) {
			IVTV_DEBUG_INFO("Firmware is fine\n");
			if (!test_bit(DEC_DMA_ERR, &itv->r_flags))
				goto firmware_exit;
			else
				dma_err_reload = 1;
		}

		/* Tell exactly what is dead */
		if (ret == 1 || ret == 3)
			IVTV_DEBUG_WARN("Encoder ");
		if (ret == 3)
			IVTV_DEBUG_WARN("and ");
		if (ret == 2 || ret == 3)
			IVTV_DEBUG_WARN("Decoder ");

		IVTV_DEBUG_WARN("Firmware is Dead, RELOADING...\n");
	}

	/* Counter */
	itv->fw_reset_counter++;

	if (itv->fw_reset_counter > 3) {
		/* Leave locked, if fails 3 times, else will crash things */
		/*clear_bit(FW_RESET_LOCK, &itv->r_flags); */
		return -EIO;
	}

	/* Lock every stream w/DMAP */
	for (i = 0; i < itv->streamcount; i++) {
		wake_up(&itv->streams[i].waitq);

		if (itv->streams[i].dma == PCI_DMA_TODEVICE &&
		    itv->streams[i].id > 0) {
			IVTV_DEBUG_WARN(
				   "FW Reload: Locking DMAP on stream %d\n", i);

			if (test_and_set_bit(IVTV_F_S_DMAP,
					     &itv->streams[i].s_flags)) {
				/* Do Nothing */
			}
			set_bit(IVTV_F_S_RESET, &itv->streams[i].s_flags);
		} else if (itv->streams[i].id > 0) {
			/* wait and set DMAP for stream */
			set_bit(IVTV_F_S_RESET, &itv->streams[i].s_flags);
		}

	}
	/* Stop Encoder Interrupts */
	if (atomic_read(&itv->capturing) > 0) {
		was_encoding = 1;
		ivtv_mute(itv);
		ivtv_vapi(itv, IVTV_API_PAUSE_ENCODER, 1);

		ivtv_set_irq_mask(itv, IVTV_IRQ_MASK_CAPTURE);
		IVTV_DEBUG_WARN("FW Reload: Set ENC IRQ Mask!\n");

	}

	/* Sleep a little till DMA is ready */
	/* check DMA status register */
	/* FIXME is this needed, taken out for now */

	/* Mark all API commands clean */
	for (i = 0; i < 256; i++)
		itv->api[i].marked = 0;

	/* Check firmware only if reloading it */
	ret = 0;
	if (!test_and_clear_bit(DEC_DMA_ERR, &itv->r_flags)) {
		IVTV_INFO("Resetting Firmware!\n");

		/* Disable output, this makes the decoder not go green */
		ret = ivtv_reset_firmware(itv, reset_mode);
		ret = ivtv_check_firmware(itv);
	} else {
		if (dma_err_reload) {
			IVTV_INFO("Resetting Firmware from Dec DMA error!\n");
			ret = ivtv_reset_firmware(itv, reset_mode);
			ret = ivtv_check_firmware(itv);
		}
	}

	/* printout firmware version */
	if (ret < 0) {
		IVTV_DEBUG_WARN("Problem starting card.\n");
	} else if (ret == 0) {
		IVTV_DEBUG_WARN("Success Reloading FW!\n");
		itv->fw_reset_counter = 0;
	} else			/* reschedule */
		set_bit(FW_RESET_NEEDED, &itv->r_flags);

	if (was_encoding) {
		int x;
		IVTV_DEBUG_INFO(
			   "FW Reload: Wake up Dec Streams waitq!\n");

		/*assign dma block len */
		data[0] = itv->dma_cfg.fw_enc_dma_xfer;;	/* 2^9(512), 2^8(256), 2^7(128)
								   bytes ((2^9)1024) or num frames */
		/* 131072, 262144, 524288. 0 defaults to 131072 */
		data[1] = itv->dma_cfg.fw_enc_dma_type;;	/* 0=bytes, 1=frames */
		x = ivtv_api(itv, itv->enc_mbox, &itv->enc_msem,
			     IVTV_API_ASSIGN_DMA_BLOCKLEN, &result, 2,
			     &data[0]);
		if (x)
			IVTV_DEBUG_WARN(
				   "Error setting encoder DMA size %d\n", x);

		/* Setup Streams */
		for (i = 0; i < itv->streamcount; i++) {
			if (itv->streams[i].dma != PCI_DMA_FROMDEVICE)
				continue;

			/* Reset Stream */
			if (test_and_clear_bit(IVTV_F_S_RESET,
					       &itv->streams[i].s_flags)) {
				down(&itv->streams[i].mlock);
				ivtv_setup_v4l2_encode_stream(itv, i);
				up(&itv->streams[i].mlock);
				wake_up(&itv->streams[i].waitq);
			}
		}

		/* Enable DMA, IRQ */
		ivtv_vapi(itv, IVTV_API_PAUSE_ENCODER, 0);
		ivtv_unmute(itv);

		ivtv_clear_irq_mask(itv, IVTV_IRQ_MASK_CAPTURE);
		wake_up(&itv->cap_w);

		/* Restart All Captures */
		for (y = 0; y < itv->streamcount; y++) {
			if (itv->streams[y].dma != PCI_DMA_FROMDEVICE)
				continue;

			if (itv->streams[y].id < 1)
				continue;

			switch (y) {
			case IVTV_ENC_STREAM_TYPE_MPG:
				captype = 0;
				ivtv_vapi(itv,
					  IVTV_API_BEGIN_CAPTURE, 2,
					  captype, itv->streams[y].subtype);
				break;
			case IVTV_ENC_STREAM_TYPE_YUV:
				captype = 1;
				ivtv_vapi(itv,
					  IVTV_API_BEGIN_CAPTURE, 2,
					  captype, itv->streams[y].subtype);
				break;
			case IVTV_ENC_STREAM_TYPE_PCM:
				captype = 1;
				ivtv_vapi(itv,
					  IVTV_API_BEGIN_CAPTURE, 2,
					  captype, itv->streams[y].subtype);
				break;
			case IVTV_ENC_STREAM_TYPE_VBI:
				captype = 1;
				ivtv_vapi(itv,
					  IVTV_API_BEGIN_CAPTURE, 2,
					  captype, itv->streams[y].subtype);
				break;
			default:
				break;
			}
		}
	}

      firmware_exit:
	if (test_and_clear_bit(FW_RESET_NEEDED, &itv->i_flags))
		IVTV_INFO("firmware type FW_RESET_NEEDED was set!\n");
	if (test_and_clear_bit(DEC_DMA_ERR, &itv->r_flags)) {
		IVTV_INFO("firmware type DEC_DMA_ERR was set!\n");
	}
	if (test_and_clear_bit(FW_SOFT_RESET_NEEDED, &itv->i_flags))
		IVTV_INFO("firmware type FW_SOFT_RESET_NEEDED was set!\n");

	/* Unlock */
	clear_bit(FW_RESET_LOCK, &itv->r_flags);

	return status;
}
