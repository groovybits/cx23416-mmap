/*
    Vertical Blank Interval support functions
    Copyright (C) 2004  Hans Verkuil <hverkuil@xs4all.nl>

    Copyright (C) 2006 Chris Kennedy <c@groovy.org>


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

void ivtv_disable_vbi(struct ivtv *itv);
void vbi_setup_lcr(struct ivtv *itv, int set, int is_pal, struct v4l2_sliced_vbi_format *fmt);
