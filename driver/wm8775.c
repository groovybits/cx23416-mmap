/*
 * wm8775 - driver version 0.0.1
 *
 * Copyright (C) 2004 Ulf Eklund <ivtv at eklund.to>
 *
 * Based on saa7115 driver
 *
 * Copyright (C) 2005 Hans Verkuil <hverkuil@xs4all.nl>
 * - Cleanup
 * - V4L2 API update
 * - sound fixes
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 675 Mass Ave, Cambridge, MA 02139, USA.
 */

#include "ivtv-compat.h"

#include <linux/version.h>
#include <linux/module.h>
#include <linux/types.h>
#include <linux/ioctl.h>
#include <asm/uaccess.h>
#include <linux/i2c.h>
#include <linux/i2c-id.h>
#include "audiochip.h"

MODULE_DESCRIPTION("wm8775 driver");
MODULE_AUTHOR("Ulf Eklund, Hans Verkuil");
MODULE_LICENSE("GPL");

#define wm8775_err(fmt, arg...) do { \
	printk(KERN_ERR "%s %d-%04x: " fmt, client->driver->name, \
	       i2c_adapter_id(client->adapter), client->addr , ## arg); } while (0)
#define wm8775_info(fmt, arg...) do { \
	printk(KERN_INFO "%s %d-%04x: " fmt, client->driver->name, \
	       i2c_adapter_id(client->adapter), client->addr , ## arg); } while (0)


static unsigned short normal_i2c[] = { 0x36 >> 1, I2C_CLIENT_END };

#if LINUX_VERSION_CODE < KERNEL_VERSION(2,6,13)
static unsigned short normal_i2c_range[] = { I2C_CLIENT_END };
#endif

I2C_CLIENT_INSMOD;

/* ----------------------------------------------------------------------- */

enum {
	R7 = 7, R11 = 11,
	R12, R13, R14, R15, R16, R17, R18, R19, R20, R21, R23 = 23,
	TOT_REGS
};

struct wm8775_state {
	u8 input;		/* Last selected input (0-0xf) */
	u8 muted;
};

static int wm8775_write(struct i2c_client *client, int reg, u16 val)
{
	int i;

	if (reg < 0 || reg >= TOT_REGS) {
		wm8775_err("Invalid register R%d\n", reg);
		return -1;
	}

	for (i = 0; i < 3; i++) {
		if (i2c_smbus_write_byte_data(client, (reg << 1) |
					(val >> 8), val & 0xff) == 0) {
			return 0;
		}
	}
	wm8775_err("I2C: cannot write %03x to register R%d\n", val, reg);
	return -1;
}

static int wm8775_command(struct i2c_client *client, unsigned int cmd,
			  void *arg)
{
	struct wm8775_state *state = i2c_get_clientdata(client);
	int *input = arg;

	switch (cmd) {
	case AUDC_SET_INPUT:
		wm8775_write(client, R21, 0x0c0);
		wm8775_write(client, R14, 0x1d4);
		wm8775_write(client, R15, 0x1d4);

		if (*input == AUDIO_RADIO) {
			wm8775_write(client, R21, 0x108);
			state->input = 8;
			state->muted = 0;
			break;
		}
		if (*input == AUDIO_MUTE) {
			state->muted = 1;
			break;
		}
		if (*input == AUDIO_UNMUTE) {
			wm8775_write(client, R21, 0x100 + state->input);
			state->muted = 0;
			break;
		}
		/* All other inputs... */
		wm8775_write(client, R21, 0x102);
		state->input = 2;
		state->muted = 0;
		break;

	case VIDIOC_LOG_STATUS:
		wm8775_info("Input: %s%s\n",
			    state->input == 8 ? "radio" : "default",
			    state->muted ? " (muted)" : "");
		break;

	case VIDIOC_S_FREQUENCY:
		/* If I remove this, then it can happen that I have no
		   sound the first time I tune from static to a valid channel.
		   It's difficult to reproduce and is almost certainly related
		   to the zero cross detect circuit. */
		wm8775_write(client, R21, 0x0c0);
		wm8775_write(client, R14, 0x1d4);
		wm8775_write(client, R15, 0x1d4);
		wm8775_write(client, R21, 0x100 + state->input);
		break;

	default:
		return -EINVAL;
	}
	return 0;
}

/* ----------------------------------------------------------------------- */

/* i2c implementation */

/*
 * Generic i2c probe
 * concerning the addresses: i2c wants 7 bit (without the r/w bit), so '>>1'
 */

static struct i2c_driver i2c_driver;

#if LINUX_VERSION_CODE >= KERNEL_VERSION(2,6,0)
static int wm8775_attach(struct i2c_adapter *adapter, int address, int kind)
#else
static int wm8775_attach(struct i2c_adapter *adapter, int address,
			 unsigned short flags, int kind)
#endif
{
	struct i2c_client *client;
	struct wm8775_state *state;

	/* Check if the adapter supports the needed features */
	if (!i2c_check_functionality(adapter, I2C_FUNC_SMBUS_BYTE_DATA))
		return 0;

	client = kmalloc(sizeof(struct i2c_client), GFP_KERNEL);
	if (client == 0)
		return -ENOMEM;

	memset(client, 0, sizeof(struct i2c_client));
	client->addr = address;
	client->adapter = adapter;
	client->driver = &i2c_driver;
	client->flags = I2C_CLIENT_ALLOW_USE;
	snprintf(client->name, sizeof(client->name) - 1, "wm8775");

	wm8775_info("ivtv driver\n");
	wm8775_info("chip found @ 0x%x (%s)\n", address << 1, adapter->name);

	state = kmalloc(sizeof(struct wm8775_state), GFP_KERNEL);
	if (state == NULL) {
		kfree(client);
		return -ENOMEM;
	}
	state->input = 2;
	state->muted = 0;
	i2c_set_clientdata(client, state);

	/* initialize wm8775 */
	wm8775_write(client, R23, 0x000);	/* RESET */
	wm8775_write(client, R7, 0x000);	/* Disable zero cross detect timeout */
	wm8775_write(client, R11, 0x021);	/* Left justified, 24-bit mode */
	wm8775_write(client, R12, 0x102);	/* Master mode, clock ratio 256fs */
	wm8775_write(client, R13, 0x000);	/* Powered up */
	wm8775_write(client, R14, 0x1d4);	/* ADC gain +2.5dB, enable zero cross */
	wm8775_write(client, R15, 0x1d4);	/* ADC gain +2.5dB, enable zero cross */
	wm8775_write(client, R16, 0x1bf);	/* ALC Stereo, ALC target level -1dB FS */
	/* max gain +8dB */
	wm8775_write(client, R17, 0x185);	/* Enable gain control, use zero cross */
	/* detection, ALC hold time 42.6 ms */
	wm8775_write(client, R18, 0x0a2);	/* ALC gain ramp up delay 34 s, */
	/* ALC gain ramp down delay 33 ms */
	wm8775_write(client, R19, 0x005);	/* Enable noise gate, threshold -72dBfs */
	wm8775_write(client, R20, 0x07a);	/* Transient window 4ms, lower PGA gain */
	/* limit -1dB */
	wm8775_write(client, R21, 0x102);	/* LRBOTH = 1, use input 2. */
	i2c_attach_client(client);

#if LINUX_VERSION_CODE < KERNEL_VERSION(2,5,0)
	MOD_INC_USE_COUNT;
#endif
	return 0;
}

static int wm8775_probe(struct i2c_adapter *adapter)
{
#ifdef I2C_CLASS_TV_ANALOG
	if (adapter->class & I2C_CLASS_TV_ANALOG)
#else
	if (adapter->id == (I2C_HW_B_BT848 | I2C_ALGO_BIT))
#endif
		return i2c_probe(adapter, &addr_data, wm8775_attach);
	return 0;
}

static int wm8775_detach(struct i2c_client *client)
{
	int err;

	err = i2c_detach_client(client);
	if (err) {
		return err;
	}
	kfree(client);

#if LINUX_VERSION_CODE < KERNEL_VERSION(2,5,0)
	MOD_DEC_USE_COUNT;
#endif
	return 0;
}

/* ----------------------------------------------------------------------- */

/* i2c implementation */
static struct i2c_driver i2c_driver = {
	.name = "wm8775",

	.id = I2C_DRIVERID_WM8775,
	.flags = I2C_DF_NOTIFY,

	.attach_adapter = wm8775_probe,
	.detach_client = wm8775_detach,
	.command = wm8775_command,
#if LINUX_VERSION_CODE >= KERNEL_VERSION(2,6,0)
	.owner = THIS_MODULE,
#endif
};

#if LINUX_VERSION_CODE < KERNEL_VERSION(2,5,0)
EXPORT_NO_SYMBOLS;
#endif

static int __init wm8775_init_module(void)
{
	return i2c_add_driver(&i2c_driver);
}

static void __exit wm8775_cleanup_module(void)
{
	i2c_del_driver(&i2c_driver);
}

module_init(wm8775_init_module);
module_exit(wm8775_cleanup_module);
