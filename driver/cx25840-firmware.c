/* cx25840 firmware functions
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License
 * as published by the Free Software Foundation; either version 2
 * of the License, or (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA  02110-1301, USA.
 */

#include "ivtv-compat.h"

#include <linux/module.h>
#include <linux/i2c.h>
#include <linux/i2c-algo-bit.h>
#include <linux/slab.h>
#include <linux/mm.h>
#include <asm/uaccess.h>
#include "v4l2-common.h"

#include "cx25840.h"

#define FWSEND 1024

#if LINUX_VERSION_CODE > KERNEL_VERSION(2,5,0)
#define FWDEV(x) &((x)->adapter->dev)
#else
#define FWDEV(x) (x)->name
#endif

static char *firmware = FWFILE;

#if LINUX_VERSION_CODE > KERNEL_VERSION(2,5,0)
module_param(firmware, charp, 0444);
#else
MODULE_PARM(firmware, "s");
#endif

MODULE_PARM_DESC(firmware, "Firmware image [default: " FWFILE "]");

static inline void start_fw_load(struct i2c_client *client)
{
	/* DL_ADDR_LB=0 DL_ADDR_HB=0 */
	cx25840_write(client, 0x800, 0x00);
	cx25840_write(client, 0x801, 0x00);
	// DL_MAP=3 DL_AUTO_INC=0 DL_ENABLE=1
	cx25840_write(client, 0x803, 0x0b);
	/* AUTO_INC_DIS=1 */
	cx25840_write(client, 0x000, 0x20);
}

static inline void end_fw_load(struct i2c_client *client)
{
	/* AUTO_INC_DIS=0 */
	cx25840_write(client, 0x000, 0x00);
	/* DL_ENABLE=0 */
	cx25840_write(client, 0x803, 0x03);
}

static inline int check_fw_load(struct i2c_client *client, int size)
{
	/* DL_ADDR_HB DL_ADDR_LB */
	int s = cx25840_read(client, 0x801) << 8;
	s |= cx25840_read(client, 0x800);

	if (size != s) {
		cx25840_err("firmware %s load failed\n", firmware);
		return -EINVAL;
	}

	cx25840_info("loaded %s firmware (%d bytes)\n", firmware, size);
	return 0;
}

static inline int fw_write(struct i2c_client *client, u8 * data, int size)
{
	int sent;
	
	if ((sent = i2c_master_send(client, data, size)) < size) {
		cx25840_err("firmware load i2c failure\n");
		return -ENOSYS;
	}

	return 0;
}

int cx25840_loadfw_hp(struct i2c_client *client)
{
	const struct firmware *fw = NULL;
	u8 buffer[4], *ptr;
	int size, send, retval;

	if (request_firmware(&fw, firmware, FWDEV(client)) != 0) {
		cx25840_err("unable to open firmware %s\n", firmware);
		return -EINVAL;
	}

	start_fw_load(client);

	buffer[0] = 0x08;
	buffer[1] = 0x02;
	buffer[2] = fw->data[0];
	buffer[3] = fw->data[1];
	retval = fw_write(client, buffer, 4);

	if (retval < 0) {
		release_firmware(fw);
		return retval;
	}

	size = fw->size - 2;
	ptr = fw->data;
	while (size > 0) {
		ptr[0] = 0x08;
		ptr[1] = 0x02;
		send = size > (FWSEND - 2) ? FWSEND : size + 2;
		retval = fw_write(client, ptr, send);

		if (retval < 0) {
			release_firmware(fw);
			return retval;
		}

		size -= FWSEND - 2;
		ptr += FWSEND - 2;
	}

	end_fw_load(client);

	size = fw->size;
	release_firmware(fw);

	return check_fw_load(client, size);
}

int cx25840_loadfw_nohp(struct i2c_client *client)
{
	loff_t file_offset = 0;
	mm_segment_t fs;
	kernel_filep filep;
	int size, tsize, retval;
	u8 *buffer;

	buffer = kmalloc(FWSEND, GFP_KERNEL);
	if (buffer == 0) {
		cx25840_err("firmware load failed (out of memory)\n");
		return -ENOMEM;
	}

	buffer[0] = 0x08;
	buffer[1] = 0x02;

	fs = get_fs();
	set_fs(get_ds());

	filep = kernel_file_open(firmware, 0, 0);
	if (kernel_file_is_err(filep)) {
		cx25840_err("unable to open firmware %s\n", firmware);
		kfree(buffer);
		set_fs(fs);
		return -EINVAL;
	}

	start_fw_load(client);

	tsize = 0;
	do {
		size =
		    kernel_file_read(filep, &buffer[2], FWSEND - 2,
 				     &file_offset);
		if (size < 0) {
			cx25840_err("firmware %s read failed\n", firmware);
			kfree(buffer);
			kernel_file_close(filep);
			set_fs(fs);
			return -EINVAL;
		}

		tsize += size;

		if (size) {
			retval = fw_write(client, buffer, size + 2);

			if (retval < 0) {
				kfree(buffer);
				kernel_file_close(filep);
				set_fs(fs);
				return retval;
			}
		}

	} while (size == FWSEND - 2);

	end_fw_load(client);

	kfree(buffer);
	kernel_file_close(filep);
	set_fs(fs);

	return check_fw_load(client, tsize);
}
