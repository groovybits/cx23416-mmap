/*
    I2C functions
    Copyright (C) 2003-2004  Kevin Thayer <nufan_wfk at yahoo.com>

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

int ivtv_wm8775(struct ivtv *itv, unsigned int cmd, void *arg);
int ivtv_cx25840(struct ivtv *itv, unsigned int cmd, void *arg);
int ivtv_radio_tuner(struct ivtv *itv, unsigned int cmd, void *arg);
int ivtv_tv_tuner(struct ivtv *itv, unsigned int cmd, void *arg);
int ivtv_tda9887(struct ivtv *itv, unsigned int cmd, void *arg);
int ivtv_hauppauge(struct ivtv *itv, unsigned int cmd, void *arg);
int ivtv_wm8775(struct ivtv *itv, unsigned int cmd, void *arg);

/* init + register i2c algo-bit adapter */
int __devinit init_ivtv_i2c(struct ivtv *itv);
void __devexit exit_ivtv_i2c(struct ivtv *itv);
