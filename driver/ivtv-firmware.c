/*
    ivtv firmware functions.
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

#include "ivtv-driver.h"
#include "ivtv-fileops.h"
#include "ivtv-mailbox.h"
#include "ivtv-streams.h"
#include "ivtv-cards.h"
#include "ivtv-firmware.h"

#define FWDEV(x) &((x)->dev)

static u32 ivtv_firm_search_id[] =
    { 0x12345678, 0x34567812, 0x56781234, 0x78123456 };

static int ivtv_check_enc_firmware(struct ivtv *itv)
{
	/* check enc firmware */
	if (ivtv_vapi(itv, IVTV_API_ENC_PING_FW, 0)) {
		IVTV_DEBUG_WARN("Encoder firmware dead!\n");
		/* WARNING this creates a race (if another process is
		   in the midst of an api command).. */
		itv->enc_mbox = NULL;
		return 1;
	}
	IVTV_DEBUG_INFO("Encoder OK\n");
	return 0;
}

int ivtv_check_firmware(struct ivtv *itv)
{
	int enc_error = 0;

	IVTV_DEBUG_INFO("Checking firmware\n");

	/* encoder */
	enc_error = ivtv_check_enc_firmware(itv);

	return enc_error;
}

#if defined(CONFIG_FW_LOADER) || defined(CONFIG_FW_LOADER_MODULE)
/* Use hotplug support */
static int load_fw_direct(const char *fn, char *mem, struct ivtv *itv, long size)
{
	const struct firmware *fw = NULL;
	struct pci_dev *pdev = itv->dev;
	int retval = -ENOMEM;

	if (request_firmware(&fw, fn, FWDEV(pdev)) == 0) {
		int i;
		u32 *dst = (u32 *)mem;
		const u32 *src = (const u32 *)fw->data;

		if (fw->size >= size) {
			retval = size;
		} else {
			retval = fw->size;
		}
		for (i = 0; i < retval; i += 4) {
			writel(*src, dst);
			dst++;
			src++;
		}
		release_firmware(fw);
		IVTV_INFO("loaded %s firmware (%d bytes)\n", fn, retval);
	} else {
		IVTV_INFO("unable to open firmware %s\n", fn);
		IVTV_INFO("did you put the firmware in the hotplug firmware directory?\n");
	}

	return retval;
}
#else
/* do it ourselves (for older 2.4 kernels) */
static int load_fw_direct(const char *fn, char *mem, struct ivtv *itv, long size) 
{ 
	kernel_filep filep; 
	loff_t file_offset = 0; 
	int retval = -EINVAL; 
	mm_segment_t fs = get_fs(); 

	set_fs(get_ds()); 

	filep = kernel_file_open(fn, 0, 0); 

	if (kernel_file_is_err(filep)) { 
		IVTV_INFO("unable to open firmware\n"); 
	} else { 
		retval = kernel_file_read(filep, mem, size, &file_offset); 
		kernel_file_close(filep); 
		IVTV_INFO("loaded %s firmware (%d bytes)\n", fn, retval);
	} 

	set_fs(fs); 

	return retval; 
} 
#endif

static int ivtv_enc_firmware_copy(struct ivtv *itv)
{
	IVTV_DEBUG_INFO("Loading encoder image\n");

	if (load_fw_direct(ivtv_efw?ivtv_efw:IVTV_FIRM_ENC_FILENAME,
			   (char *)(itv->enc_mem), itv, IVTV_FIRM_IMAGE_SIZE) !=
	    IVTV_FIRM_IMAGE_SIZE) {
		IVTV_DEBUG_WARN("failed loading encoder firmware\n");
		return -3;
	}
	return 0;
}

static int ivtv_stop_firmware_enc(struct ivtv *itv)
{
	u32 data[IVTV_MBOX_MAX_DATA], result;
	int x = 0, sleep = 0;

	IVTV_DEBUG_INFO("Stopping encoder firmware\n");

	if (NULL != itv->enc_mbox) {
		/*halt enc firmware */
		x = ivtv_api(itv, itv->enc_mbox, &itv->enc_msem,
			     IVTV_API_ENC_HALT_FW, &result, 0, &data[0]);
		if (x)
			IVTV_DEBUG_WARN("stop_fw error 3. Code %d\n",
				   x);

		sleep += 1;
	}

	return sleep;
}

void ivtv_halt_firmware(struct ivtv *itv, int mode)
{
	int x = 0;

	/* check that we're not RE-loading firmware */
	/*  a sucessful load will have detected HW  */
	/*  mailboxes. */

	IVTV_DEBUG_INFO("Preparing for firmware halt.\n");
	if (mode == 1 || mode == 3)
		x += ivtv_stop_firmware_enc(itv);

	if (x > 0) {
		IVTV_DEBUG_INFO("Sleeping for 10ms\n");
		ivtv_sleep_timeout(HZ / 100, 0);
	}

	IVTV_DEBUG_INFO("Stopping VDM\n");
	writel(IVTV_CMD_VDM_STOP, (IVTV_REG_VDM + itv->reg_mem));

	IVTV_DEBUG_INFO("Stopping AO\n");
	writel(IVTV_CMD_AO_STOP, (IVTV_REG_AO + itv->reg_mem));

	IVTV_DEBUG_INFO("pinging (?) APU\n");
	writel(IVTV_CMD_APU_PING, (IVTV_REG_APU + itv->reg_mem));

	IVTV_DEBUG_INFO("Stopping VPU\n");
	writel(IVTV_CMD_VPU_STOP16, (IVTV_REG_VPU + itv->reg_mem));
	//writel(IVTV_CMD_VPU_STOP15, (IVTV_REG_VPU + itv->reg_mem));

	IVTV_DEBUG_INFO("Resetting Hw Blocks\n");
	writel(IVTV_CMD_HW_BLOCKS_RST, (IVTV_REG_HW_BLOCKS + itv->reg_mem));

	IVTV_DEBUG_INFO("Stopping SPU\n");
	writel(IVTV_CMD_SPU_STOP, (IVTV_REG_SPU + itv->reg_mem));

	IVTV_DEBUG_INFO("Sleeping for 10ms\n");
	ivtv_sleep_timeout(HZ / 100, 0);
}

void ivtv_start_firmware(struct ivtv *itv)
{
	/* I guess this is read-modify-write :) */
	writel((readl(itv->reg_mem + IVTV_REG_SPU) & IVTV_MASK_SPU_ENABLE),
	       (IVTV_REG_SPU + itv->reg_mem));

	IVTV_DEBUG_WARN("Sleeping for 100 ms\n");
	ivtv_sleep_timeout(HZ / 10, 0);

	writel((readl(itv->reg_mem + IVTV_REG_VPU) &
		IVTV_MASK_VPU_ENABLE16), (IVTV_REG_VPU + itv->reg_mem));

	IVTV_DEBUG_WARN("Sleeping for 100 ms\n");
	ivtv_sleep_timeout(HZ / 10, 0);
}

int ivtv_find_enc_firmware_mailbox(struct ivtv *itv)
{
	u32 *searchptr, *result;
	int match = 0;

	searchptr = NULL;
	result = NULL;

	IVTV_DEBUG_INFO("Searching for encoder mailbox\n");
	searchptr = (u32 *) (IVTV_FIRM_SEARCH_ENCODER_START + itv->enc_mem);

	while (searchptr <
	       (u32 *) (IVTV_FIRM_SEARCH_ENCODER_END + itv->enc_mem)) {
		if (ivtv_firm_search_id[match] == readl(searchptr)) {
			result = searchptr + 1;	/* pointer arithmetic */
			match++;
			while ((match > 0) && (match < 4)) {
				IVTV_DEBUG_INFO("match: 0x%08x at "
					   "0x%08lx. match: %d\n", *result,
					   (unsigned long)result, match);
				if (ivtv_firm_search_id[match] == readl(result)) {
					match++;
					result++;	/* pointer arithmetic */
				} else
					match = 0;
			}
		} else {
			if ((IVTV_DBGFLG_INFO) & ivtv_debug)
				printk(".");

		}
		if (4 == match) {
			IVTV_DEBUG_INFO("found encoder mailbox!\n");
			itv->enc_mbox = (struct ivtv_mailbox *)result;
			break;
		}
		searchptr += IVTV_FIRM_SEARCH_STEP;
	}

	if (itv->enc_mbox == NULL) {
		IVTV_DEBUG_WARN("Encoder mailbox not found\n");
		return -ENODEV;
	}

	return 0;
}

int ivtv_firmware_versions(struct ivtv *itv)
{
	u32 data[IVTV_MBOX_MAX_DATA], result;
	int x;

	/* Encoder */
	IVTV_DEBUG_INFO("Getting encoder firmware rev.\n");
	x = ivtv_api(itv, itv->enc_mbox, &itv->enc_msem, IVTV_API_ENC_GETVER,
		     &result, 0, &data[0]);
	if (x) {
		IVTV_ERR("error getting Encoder firmware version\n");
		return x;
	}
	IVTV_INFO("Encoder revision: 0x%08x\n", data[0]);

	return 0;
}

int ivtv_firmware_copy(struct ivtv *itv)
{
	int ret = 0;

	ret = ivtv_enc_firmware_copy(itv);
	if (ret)
		return ret;

	return 0;
}

int ivtv_stop_firmware(struct ivtv *itv)
{
	u32 data[IVTV_MBOX_MAX_DATA], result;
	int x = 0;

	IVTV_DEBUG_INFO("Stopping firmware\n");

	if (atomic_read(&itv->capturing)) {
		x = ivtv_stop_all_captures(itv);
		if (x)
			IVTV_DEBUG_WARN("stop_fw error 1. Code %d\n",
				   x);
	}

	/*halt enc firmware */
	x = ivtv_api(itv, itv->enc_mbox, &itv->enc_msem, IVTV_API_ENC_HALT_FW,
		     &result, 0, &data[0]);
	if (x)
		IVTV_DEBUG_WARN("stop_fw error 3. Code %d\n", x);

	IVTV_DEBUG_INFO("Sleeping for 10ms\n");
	ivtv_sleep_timeout(HZ / 100, 0);

	return 0;
}

int ivtv_firmware_init(struct ivtv *itv)
{
	int x;

	/* check that we're not RE-loading firmware */
	/*  a sucessful load will have detected HW  */
	/*  mailboxes. */

	// Stop all Hardware units and Java CPU's
	IVTV_DEBUG_INFO("Stopping VDM\n");
	writel(IVTV_CMD_VDM_STOP, (IVTV_REG_VDM + itv->reg_mem));

	IVTV_DEBUG_INFO("Stopping AO\n");
	writel(IVTV_CMD_AO_STOP, (IVTV_REG_AO + itv->reg_mem));

	IVTV_DEBUG_INFO("pinging (?) APU\n");
	writel(IVTV_CMD_APU_PING, (IVTV_REG_APU + itv->reg_mem));

	IVTV_DEBUG_INFO("Stopping VPU\n");
	writel(IVTV_CMD_VPU_STOP16, (IVTV_REG_VPU + itv->reg_mem));
	//writel(IVTV_CMD_VPU_STOP15, (IVTV_REG_VPU + itv->reg_mem));

	IVTV_DEBUG_INFO("Resetting Hw Blocks\n");
	writel(IVTV_CMD_HW_BLOCKS_RST, (IVTV_REG_HW_BLOCKS + itv->reg_mem));

	IVTV_DEBUG_INFO("Stopping SPU\n");
	writel(IVTV_CMD_SPU_STOP, (IVTV_REG_SPU + itv->reg_mem));

	IVTV_DEBUG_INFO("Sleeping for 10ms\n");
	ivtv_sleep_timeout(HZ / 100, 0);

	// Setup SDRAM Memory

	IVTV_DEBUG_INFO("init Encoder SDRAM pre-charge\n");
	writel(IVTV_CMD_SDRAM_PRECHARGE_INIT,
	       (IVTV_REG_ENC_SDRAM_PRECHARGE + itv->reg_mem));

	IVTV_DEBUG_INFO("init Encoder SDRAM refresh to 1us\n");
	writel(IVTV_CMD_SDRAM_REFRESH_INIT,
	       (IVTV_REG_ENC_SDRAM_REFRESH + itv->reg_mem));

	IVTV_DEBUG_INFO("Sleeping for %dms (600 recommended)\n",
		   (int)IVTV_SDRAM_SLEEPTIME);
	ivtv_sleep_timeout(IVTV_SDRAM_SLEEPTIME, 0);

	// Load Firmware

	IVTV_DEBUG_INFO("Card ready for firmware!\n");
	x = ivtv_firmware_copy(itv);
	if (x) {
		IVTV_DEBUG_WARN("Error loading firmware %d!\n", x);
		return x;
	}

	// Startup Java Processors and Hardware Units

	/*I guess this is read-modify-write :) */
	writel((readl(itv->reg_mem + IVTV_REG_SPU) & IVTV_MASK_SPU_ENABLE),
	       (IVTV_REG_SPU + itv->reg_mem));

	IVTV_DEBUG_INFO("Sleeping for 100 ms\n");
	ivtv_sleep_timeout(HZ / 10, 0);

	/*I guess this is read-modify-write :) */
	writel((readl(itv->reg_mem + IVTV_REG_VPU) &
		IVTV_MASK_VPU_ENABLE16), (IVTV_REG_VPU + itv->reg_mem));
	//writel((readl(itv->reg_mem + IVTV_REG_VPU) &
	//	IVTV_MASK_VPU_ENABLE15), (IVTV_REG_VPU + itv->reg_mem));

	IVTV_DEBUG_INFO("Sleeping for 100 ms\n");
	ivtv_sleep_timeout(HZ / 10, 0);

	return 0;
}

