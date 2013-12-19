/*
    buffer queues.
    Copyright (C) 2003-2004  Kevin Thayer <nufan_wfk at yahoo.com>
    Copyright (C) 2006  Chris Kennedy <c@groovy.org>

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

#define IVTV_DMA_UNMAPPED	((u32) -1)

/* moves all items in queue 'src' to queue 'dst' */
void ivtv_free_v4lbuf(struct pci_dev *dev,
		      struct ivtv_buffer *item, struct ivtv_stream *stream);
void ivtv_init_v4l2buf(struct pci_dev *dev,
				     struct ivtv_stream *stream,
					struct scatterlist *sglist,
					struct ivtv_buffer *buf);
int ivtv_sleep_timeout(int timeout, int intr);
void ivtv_stream_free(struct ivtv *itv, int stream);
const char *ivtv_stream_name(int streamtype);
