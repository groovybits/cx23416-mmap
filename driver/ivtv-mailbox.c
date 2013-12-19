/* mailbox functions
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

#include <stdarg.h>

#include "ivtv-driver.h"
#include "ivtv-fileops.h"
#include "ivtv-mailbox.h"

static int ivtv_get_free_mailbox(struct ivtv *itv, int cmd, struct ivtv_mailbox *mbox, int maxnum,
				 int interrupt)
{
	int i = 0, y = 0;
	int retries = 100;
	if (NULL == mbox) {
		IVTV_DEBUG_WARN("Can't get mailbox from NULL\n");
		return -ENODEV;
	}

	/* find free mailbox */
	for (y = 0; y < retries; y++) {
		for (i = 0; i <= maxnum; i++) {
			/* Mailbox is uninitialized, lock mailbox */
			if (((readl((unsigned char *)&mbox[i].flags)&0x02) == 0)) {
				ivtv_write_reg(IVTV_MBOX_DRIVER_BUSY, 
					(unsigned char*)&mbox[i].flags);
			
        				IVTV_DEBUG_API(
					   "got new mailbox: %d after %d tries\n",
					   i, y + 1);
				return i;
			}

			if (readl((unsigned char *)&mbox[i].flags) & 0x04) {
				ivtv_write_reg(IVTV_MBOX_DRIVER_BUSY,
					(unsigned char*)&mbox[i].flags);
			
        			IVTV_DEBUG_API(
				   "got free mailbox: %d after %d tries\n",
					   i, y + 1);
				return i;
			}
			/* Sleep before a retry, if not atomic */
			if (!interrupt)
				ivtv_sleep_timeout(HZ / 100, 0);
		}
		/* Sleep before a retry, if not atomic */
		if (!interrupt)
			ivtv_sleep_timeout(HZ / 100, 0);
	}

	IVTV_DEBUG_WARN(
		   "No Free Mailbox for cmd 0x%08x after %d tries!\n", cmd, y);

	/* Clear all mailboxes */
	for (i = 0; i <= maxnum; i++) {
		IVTV_DEBUG_WARN("Mailbox[%d] 0x%08x flags 0x%08x\n",
			   i, mbox[i].cmd, mbox[i].flags);
		ivtv_write_reg(0x00, (unsigned char *)&mbox[i].flags);
	}
	return -ENODEV;
}

/* This one is for stuff that can't sleep.. irq handlers, etc.. */
int ivtv_api_getresult_nosleep(struct ivtv *itv, struct ivtv_mailbox *mbox, u32 *result,
			       u32 data[])
{
	int count;

	if (NULL == mbox) {
		IVTV_DEBUG_WARN("invalid api mailbox\n");
		return -ENODEV;
	}

	for (count = 0; count < 7; count++) {
		data[count] =
		    readl((unsigned char *)&mbox->data[count]);
	}
	return 0;
}

static int ivtv_api_getresult(struct ivtv *itv, struct ivtv_mailbox *mbox, u32 * result,
			      u32 data[], int api_timeout, int cmd)
{
	u32 readdata;
	int count = 0;

	if (NULL == mbox) {
		IVTV_DEBUG_WARN("invalid api mailbox\n");
		return -ENODEV;
	}

	readdata = readl((unsigned char *)&mbox->flags);

	while (!(readdata & IVTV_MBOX_FIRMWARE_DONE)) {
                if (count)
        		IVTV_DEBUG_API(
			   "result not ready, waiting 10 ms (attempt %d)\n", count + 1);

		if (count++ > api_timeout) {
			IVTV_DEBUG_WARN(
				   "%d ms time out waiting for firmware\n",
				   api_timeout);
			return -EBUSY;
		}

		/* we want to finish this api call and not break for
		   any pending signals. */
		set_current_state(TASK_UNINTERRUPTIBLE);
		schedule_timeout(HZ / 100);

		readdata = readl((unsigned char *)&mbox->flags);
	}

	*result = readl((unsigned char *)&mbox->retval);

	/* Failed, data must be bad */
	if (readl((unsigned char *)&mbox->retval) != 0x00)
		return -EBUSY;

	for (count = 0; count < IVTV_MBOX_MAX_DATA; count++)
		data[count] =
		    readl((unsigned char *)&mbox->data[count]);

	return 0;
}

#define API_ENTRY(x) { (x), #x },

static const struct {
        int cmd;
        const char *name;
} cmd_names[] = {
	/* MPEG encoder API */
	API_ENTRY(IVTV_API_ENC_PING_FW)
	API_ENTRY(IVTV_API_BEGIN_CAPTURE)
	API_ENTRY(IVTV_API_END_CAPTURE)
	API_ENTRY(IVTV_API_ASSIGN_AUDIO_ID)
	API_ENTRY(IVTV_API_ASSIGN_VIDEO_ID)
	API_ENTRY(IVTV_API_ASSIGN_PCR_ID)
	API_ENTRY(IVTV_API_ASSIGN_FRAMERATE)
	API_ENTRY(IVTV_API_ASSIGN_FRAME_SIZE)
	API_ENTRY(IVTV_API_ASSIGN_BITRATES)
	API_ENTRY(IVTV_API_ASSIGN_GOP_PROPERTIES)
	API_ENTRY(IVTV_API_ASSIGN_ASPECT_RATIO)
	API_ENTRY(IVTV_API_ASSIGN_DNR_FILTER_MODE)
	API_ENTRY(IVTV_API_ASSIGN_DNR_FILTER_PROPS)
	API_ENTRY(IVTV_API_ASSIGN_CORING_LEVELS)
	API_ENTRY(IVTV_API_ASSIGN_SPATIAL_FILTER_TYPE)
	API_ENTRY(IVTV_API_ASSIGN_3_2_PULLDOWN)
	API_ENTRY(IVTV_API_SELECT_VBI_LINE)
	API_ENTRY(IVTV_API_ASSIGN_STREAM_TYPE)
	API_ENTRY(IVTV_API_ASSIGN_OUTPUT_PORT)
	API_ENTRY(IVTV_API_ASSIGN_AUDIO_PROPERTIES)
	API_ENTRY(IVTV_API_ENC_HALT_FW)
	API_ENTRY(IVTV_API_ENC_GETVER)
	API_ENTRY(IVTV_API_ASSIGN_GOP_CLOSURE)
	API_ENTRY(IVTV_API_ENC_GET_SEQ_END)
	API_ENTRY(IVTV_API_ASSIGN_PGM_INDEX_INFO)
	API_ENTRY(IVTV_API_CONFIG_VBI)
	API_ENTRY(IVTV_API_ASSIGN_DMA_BLOCKLEN)
	API_ENTRY(IVTV_API_PREV_DMA_INFO_MB_10)
	API_ENTRY(IVTV_API_PREV_DMA_INFO_MB_9)
	API_ENTRY(IVTV_API_SCHED_DMA_TO_HOST)
	API_ENTRY(IVTV_API_INITIALIZE_INPUT)
	API_ENTRY(IVTV_API_ASSIGN_FRAME_DROP_RATE)
	API_ENTRY(IVTV_API_PAUSE_ENCODER)
	API_ENTRY(IVTV_API_REFRESH_INPUT)
	API_ENTRY(IVTV_API_ASSIGN_COPYRIGHT)
	API_ENTRY(IVTV_API_EVENT_NOTIFICATION)
	API_ENTRY(IVTV_API_ASSIGN_NUM_VSYNC_LINES)
	API_ENTRY(IVTV_API_ASSIGN_PLACEHOLDER)
	API_ENTRY(IVTV_API_MUTE_VIDEO)
	API_ENTRY(IVTV_API_MUTE_AUDIO)
	API_ENTRY(IVTV_API_ENC_UNKNOWN)
	API_ENTRY(IVTV_API_ENC_MISC)

};

static const char *get_cmd_name(int cmd)
{
        int i;

        for (i = 0; i < sizeof(cmd_names) / sizeof(cmd_names[0]); i++) {
                if (cmd_names[i].cmd == cmd)
                        return cmd_names[i].name;
        }
        return "Unknown";
}

u32 ivtv_api_sendDMA(struct ivtv *itv, struct ivtv_mailbox *dma_box, int type, u32 address, u32 size) 
{
	int cmd = IVTV_API_SCHED_DMA_TO_HOST;
	u32 before = 0;

	// Force Lock Mailbox
	ivtv_write_reg(0x00, (unsigned char *)&dma_box->flags);
	ivtv_write_reg(IVTV_MBOX_DRIVER_BUSY, (unsigned char *)&dma_box->flags);

	/* Setup DMA Transfer Command and timeout */
	ivtv_write_reg(cmd, (unsigned char *)&dma_box->cmd);
	ivtv_write_reg(0x50000, (unsigned char *)&dma_box->timeout);
	ivtv_write_reg(0x00, (unsigned char *)&dma_box->retval);

	// Setup Args for DMA Transfer
	ivtv_write_reg(address, 
		(unsigned char *)&dma_box->data[0]);
	ivtv_write_reg(size, 
		(unsigned char *)&dma_box->data[1]);
	ivtv_write_reg(type, 
		(unsigned char *)&dma_box->data[2]);

	IVTV_DEBUG_DMA("Host DMA xfer setup in Mailbox %d with sg addr 0x%0x size %d and type %d\n",
		2, address, size, type);	

	// Send the DMA Request
	ivtv_write_reg((IVTV_MBOX_DRIVER_DONE | IVTV_MBOX_DRIVER_BUSY),
		       (unsigned char *)&dma_box->flags);

        before = readl((unsigned char *)&dma_box->flags);

	if (readl((unsigned char*)&dma_box->flags) & IVTV_MBOX_FIRMWARE_DONE)
		ivtv_write_reg(0x00, (unsigned char *)&dma_box->flags);

	IVTV_DEBUG_DMA("DMA Mailbox (before 0x%08x, after 0x%08x)\n",
		   before, readl((unsigned char *)&dma_box->flags));

	return readl((unsigned char *)&dma_box->retval);
}

int ivtv_api(struct ivtv *itv, struct ivtv_mailbox *mbox, struct semaphore *sem,
	     int cmd, u32 * result, int args, u32 data[])
{
	int x = 0, gotsem = 0, needsresult = 1;
	int api_timeout = 1000;
	struct ivtv_mailbox *local_box;
	struct ivtv_mailbox *dma_box = &mbox[2];
	int mbox_num = 1;
	int mbox_max = 1;
	int no_reset = 0;
	int type = 0;		/* 0 = dec, 1 = osd, 2 = enc */
	int i, intr = 0;

	if (NULL == mbox) {
		IVTV_DEBUG_WARN("invalid api mailbox\n");
		return -ENODEV;
	}

	/* Check command type and validity */
	if (cmd <= ENCODER_API_OFFSET)
		type = 2;
	else			/* Bad Command */
		return -ENODEV;

	/* encoder api mailboxes */
	mbox_max = 2;	/* Encoder, use all three, we need them */

	IVTV_DEBUG_API("API Call: 0x%08x (%s)\n", cmd, get_cmd_name(cmd));

	local_box = &mbox[0];	/* normal commands use mbox 1 */

	/* check args */
	if (args > IVTV_MBOX_MAX_DATA)
		return -EINVAL;

	switch (cmd) {
	case IVTV_API_SCHED_DMA_TO_HOST:
		// This is untested, use it directly instead
		return ivtv_api_sendDMA(itv, dma_box, data[2], data[0], data[1]);
	case IVTV_API_BEGIN_CAPTURE:
	case IVTV_API_END_CAPTURE:
	case IVTV_API_EVENT_NOTIFICATION:
	case IVTV_API_ENC_PING_FW:
		down(sem);
		gotsem = 1;

		api_timeout = 1000;
		no_reset = 0;

		/* find free mailbox */
		mbox_num = ivtv_get_free_mailbox(itv, cmd, mbox, mbox_max, intr);
		if (mbox_num >= 0 && mbox_num <= mbox_max)
			local_box = &mbox[mbox_num];
		else
			goto ivtv_api_fail;

		ivtv_write_reg(cmd, (unsigned char *)&local_box->cmd);
		ivtv_write_reg(IVTV_API_STD_TIMEOUT,
			       (unsigned char *)&local_box->timeout);

		for (i = 0; i < IVTV_MBOX_MAX_DATA; i++) {
			if (i < args) {
				ivtv_write_reg(data[i],
					       (unsigned char *)&local_box->
					       data[i]);
			} else
				ivtv_write_reg(0x00,
					       (unsigned char *)&local_box->
					       data[i]);
		}

		ivtv_write_reg((IVTV_MBOX_DRIVER_DONE | IVTV_MBOX_DRIVER_BUSY),
			       (unsigned char *)&local_box->flags);

		break;
		/* These commands are not stored */
		/* These don't need a result */
	case IVTV_API_ENC_UNKNOWN:
	case IVTV_API_ENC_MISC:
	case IVTV_API_ASSIGN_DMA_BLOCKLEN:
	case IVTV_API_INITIALIZE_INPUT:
	case IVTV_API_REFRESH_INPUT:
		if (cmd == IVTV_API_REFRESH_INPUT)
			intr = 1;
	case IVTV_API_ASSIGN_NUM_VSYNC_LINES:
	case IVTV_API_MUTE_VIDEO:
	case IVTV_API_MUTE_AUDIO:
		needsresult = 0;

	/* These commands need a result and are not stored */
	case IVTV_API_PAUSE_ENCODER:
	case IVTV_API_ENC_HALT_FW:
		if (cmd == IVTV_API_ENC_HALT_FW) {
			api_timeout = 100;
			no_reset = 1;
		}
	case IVTV_API_ENC_GETVER:
	case IVTV_API_CONFIG_VBI:
	case IVTV_API_SELECT_VBI_LINE:
		if (!intr) {
			down(sem);
			gotsem = 1;
		}

		/* find free mailbox */
		mbox_num = ivtv_get_free_mailbox(itv, cmd, mbox, mbox_max, intr);
		if (mbox_num >= 0 && mbox_num <= mbox_max)
			local_box = &mbox[mbox_num];
		else
			goto ivtv_api_fail;

		ivtv_write_reg(cmd, (unsigned char *)&local_box->cmd);
		ivtv_write_reg(IVTV_API_STD_TIMEOUT,
			       (unsigned char *)&local_box->timeout);

		for (i = 0; i < IVTV_MBOX_MAX_DATA; i++) {
			if (i < args) {
				ivtv_write_reg(data[i],
					       (unsigned char *)&local_box->
					       data[i]);
			} else
				ivtv_write_reg(0x00,
					       (unsigned char *)&local_box->
					       data[i]);
		}

		ivtv_write_reg((IVTV_MBOX_DRIVER_DONE | IVTV_MBOX_DRIVER_BUSY),
			       (unsigned char *)&local_box->flags);

		break;
		/* These don't need a result */
	case IVTV_API_ASSIGN_PLACEHOLDER:
	case IVTV_API_ASSIGN_FRAME_DROP_RATE:
	case IVTV_API_ASSIGN_SPATIAL_FILTER_TYPE:
	case IVTV_API_ASSIGN_CORING_LEVELS:
	case IVTV_API_ASSIGN_DNR_FILTER_PROPS:
	case IVTV_API_ASSIGN_DNR_FILTER_MODE:
	case IVTV_API_ASSIGN_AUDIO_PROPERTIES:
	case IVTV_API_ASSIGN_GOP_CLOSURE:
	case IVTV_API_ASSIGN_3_2_PULLDOWN:
	case IVTV_API_ASSIGN_GOP_PROPERTIES:
	case IVTV_API_ASSIGN_BITRATES:
	case IVTV_API_ASSIGN_ASPECT_RATIO:
	case IVTV_API_ASSIGN_FRAME_SIZE:
	case IVTV_API_ASSIGN_FRAMERATE:
	case IVTV_API_ASSIGN_OUTPUT_PORT:
	case IVTV_API_ASSIGN_STREAM_TYPE:
		needsresult = 0;
	default:		/* Stored Commands */
		if (!intr) {
			down(sem);
			gotsem = 1;
		}

		/* Store command send/return data */
		if (itv->api[cmd].marked &&
		    ((jiffies - itv->api[cmd].jiffies) < MBOX_TIMEOUT)) {
			int stored = 1;
			/* check if same args given within timeout */
			/* if ok, return old args, or run again */
			for (i = 0; i < args; i++) {
				if (itv->api[cmd].s_data[i] == data[i]) {
					/* Data is same as before */
				} else {
					IVTV_DEBUG_API(
						   "cmd: 0x%08x arg %d "
						   "stored as 0x%08x "
						   "but changed to "
						   "0x%08x\n",
						   cmd, i,
						   itv->api[cmd].s_data[i],
						   data[i]);
					stored = 0;	/* different */
					break;
				}
			}

			if (stored) {
				if (i > 0)
					IVTV_DEBUG_API(
					   "cmd: 0x%08x arg %d stored as 0x%08x\n",
                                           cmd, i,
					   itv->api[cmd].s_data[(i - 1)]);
                                else
                                        IVTV_DEBUG_API(
                                                   "cmd: 0x%08x arg %d stored\n",
                                                   cmd, i);
				for (i = 0; i < IVTV_MBOX_MAX_DATA; i++) {
					data[i] = itv->api[cmd].r_data[i];
				}

				result = 0;
				goto ivtv_api_done;
			}

			itv->api[cmd].marked = 1;	/* dirty mbox */
			itv->api[cmd].jiffies = jiffies;	/* timestamp */

			for (i = 0; i < IVTV_MBOX_MAX_DATA; i++) {
				if (i < args) {
					itv->api[cmd].s_data[i] = data[i];
				} else {
					itv->api[cmd].s_data[i] = 0;
				}
			}
		} else {	/* Not Stored */
			IVTV_DEBUG_API(
				   "cmd: 0x%08x not stored\n", cmd);
			itv->api[cmd].marked = 1;	/* dirty mbox */
			itv->api[cmd].jiffies = jiffies;	/* timestamp */

			for (i = 0; i < IVTV_MBOX_MAX_DATA; i++) {
				if (i < args) {
					itv->api[cmd].s_data[i] = data[i];
				} else {
					itv->api[cmd].s_data[i] = 0;
				}
			}
		}

		/* find free mailbox */
		mbox_num = ivtv_get_free_mailbox(itv, cmd, mbox, mbox_max, intr);
		if (mbox_num >= 0 && mbox_num <= mbox_max)
			local_box = &mbox[mbox_num];
		else
			goto ivtv_api_fail;

		ivtv_write_reg(cmd, (unsigned char *)&local_box->cmd);
		ivtv_write_reg(IVTV_API_STD_TIMEOUT,
			       (unsigned char *)&local_box->timeout);

		for (i = 0; i < IVTV_MBOX_MAX_DATA; i++) {
			if (i < args) {
				ivtv_write_reg(data[i],
					       (unsigned char *)&local_box->
					       data[i]);
			} else
				ivtv_write_reg(0x00,
					       (unsigned char *)&local_box->
					       data[i]);
		}

		ivtv_write_reg((IVTV_MBOX_DRIVER_DONE | IVTV_MBOX_DRIVER_BUSY),
			       (unsigned char *)&local_box->flags);
		break;
	}

	/* Get results if needed */
	if (needsresult) {
		int count;
                u32 before;

		x = ivtv_api_getresult(itv, local_box,
				       result, &data[0], api_timeout, cmd);

		if (x == -EBUSY) {
			IVTV_DEBUG_WARN(
				   "Failed api call 0x%08x with result 0x%08x\n",
				   cmd, x);

			/* make sure cmd is not stored */
			itv->api[cmd].marked = 0;
		} else {
			/* Store results */
			for (count = 0; count < IVTV_MBOX_MAX_DATA; count++)
				itv->api[cmd].r_data[count] = data[count];
		}

                before = readl((unsigned char *)&local_box->flags);

		/* reinit results and flags */
		ivtv_write_reg(0x00, (unsigned char *)&local_box->retval);
		ivtv_write_reg(0x00, (unsigned char *)&local_box->flags);

		IVTV_DEBUG_API(
			   "Releasing mailbox (before 0x%08x, after 0x%08x)\n",
			   before, readl((unsigned char *)&local_box->flags));
	} else {
		x = 0;
	}

      ivtv_api_done:
	if (gotsem) {
		up(sem);
	}

	if (x)
		itv->api[cmd].marked = 0;	/* make sure cmd is not stored */

	return x;
      ivtv_api_fail:
	IVTV_DEBUG_WARN(
		   "Firmware UNRESPONSIVE when trying cmd 0x%08x!!!\n", cmd);
	if (gotsem) {
		up(sem);
	}

	itv->api[cmd].marked = 0;	/* make sure cmd is not stored */

	return -EBUSY;
}

int ivtv_api_result(struct ivtv *itv, int cmd, int args, u32 * data)
{
	u32 result;

	if (cmd >= 128) {
		return ivtv_api(itv, itv->enc_mbox, &itv->enc_msem,
				cmd, &result, args, data);
	} else
		return -1;
}

int ivtv_vapi(struct ivtv *itv, int cmd, int args, ...)
{
	u32 data[IVTV_MBOX_MAX_DATA];
	va_list ap;
	int i;

	va_start(ap, args);
	for (i = 0; i < args; i++) {
		data[i] = va_arg(ap, u32);
	}
	va_end(ap);
	return ivtv_api_result(itv, cmd, args, data);
}
