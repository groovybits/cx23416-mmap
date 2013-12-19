/*
    Vertical Blank Interval support functions
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

#include "ivtv-driver.h"
#include "ivtv-cards.h"
#include "ivtv-video.h"
#include "ivtv-i2c.h"
#include "ivtv-vbi.h"
#include "ivtv-ioctl.h"
#include "v4l2-common.h"

typedef unsigned long uintptr_t;

void vbi_setup_lcr(struct ivtv *itv, int set, int is_pal, struct v4l2_sliced_vbi_format *fmt)
{
        int i;

	memset(fmt, 0, sizeof(*fmt));
        if (set == 0) {
                return;
        }
        if (set & V4L2_SLICED_TELETEXT_B) {
                if (is_pal)
                        for (i = 6; i <= 22; i++) {
				fmt->service_lines[0][i] = V4L2_SLICED_TELETEXT_B;
				fmt->service_lines[1][i] = V4L2_SLICED_TELETEXT_B;
			}
                else
                        IVTV_ERR("Teletext not supported for NTSC\n");
        }
        if (set & V4L2_SLICED_CAPTION_525) {
                if (!is_pal) {
			fmt->service_lines[0][22] = V4L2_SLICED_CAPTION_525;
			fmt->service_lines[1][22] = V4L2_SLICED_CAPTION_525;
                } else
                        IVTV_ERR("NTSC Closed Caption not supported for PAL\n");
        }
        if (set & V4L2_SLICED_WSS_625) {
                if (is_pal) {
			fmt->service_lines[0][23] = V4L2_SLICED_WSS_625;
                }
                else
                        IVTV_ERR("WSS not supported for NTSC\n");
        }
        if (set & V4L2_SLICED_VPS) {
                if (is_pal) {
			fmt->service_lines[0][16] = V4L2_SLICED_VPS;
                }
                else
                        IVTV_ERR("VPS not supported for NTSC\n");
        }
	fmt->service_set = get_service_set(fmt);
}

void ivtv_disable_vbi(struct ivtv *itv)
{
	itv->vbi_vps_found = itv->vbi_wss_found = 0;
	itv->vbi_wss = 0;
	itv->vbi_cc_pos = 0;
}

