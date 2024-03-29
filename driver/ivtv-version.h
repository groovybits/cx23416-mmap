/*
    ivtv driver version information
    Copyright (C) 2005  Hans Verkuil <hverkuil at xs4all.nl>

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

/* Obtain IVTV_DRIVER_VERSION_COMMENT from here */
#include "ivtv-svnversion.h"

#define IVTV_DRIVER_NAME "ivtv"
#define IVTV_DRIVER_VERSION_MAJOR 1
#define IVTV_DRIVER_VERSION_MINOR 70
#define IVTV_DRIVER_VERSION_PATCHLEVEL 1
#define IVTV_VERSION __stringify(IVTV_DRIVER_VERSION_MAJOR) "." __stringify(IVTV_DRIVER_VERSION_MINOR) "." __stringify(IVTV_DRIVER_VERSION_PATCHLEVEL) " " IVTV_DRIVER_VERSION_COMMENT

#define IVTV_DRIVER_VERSION KERNEL_VERSION(IVTV_DRIVER_VERSION_MAJOR,IVTV_DRIVER_VERSION_MINOR,IVTV_DRIVER_VERSION_PATCHLEVEL)

