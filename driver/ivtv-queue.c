/*
    buffer queues.
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
#include "ivtv-streams.h"
#include "ivtv-queue.h"
#include "ivtv-mailbox.h"

typedef unsigned long uintptr_t;

/* Generic utility functions */
int ivtv_sleep_timeout(int timeout, int intr)
{
	int sleep = timeout;
	int ret = 0;

	do {
		if (intr)
			set_current_state(TASK_INTERRUPTIBLE);
		else
			set_current_state(TASK_UNINTERRUPTIBLE);

		sleep = schedule_timeout(sleep);

		if (intr)
			if ((ret = signal_pending(current)))
				break;
	} while (sleep);
	return ret;
}

void ivtv_free_v4lbuf(struct pci_dev *dev, struct ivtv_buffer *buf,
		      struct ivtv_stream *st)
{
	if (in_interrupt())
		BUG();

	// Video 4 Linux Buffers
	ivtvbuf_waiton(&buf->vb, 0, 0);
	ivtvbuf_dma_pci_unmap(dev, &buf->vb.dma);
	ivtvbuf_dma_free(&buf->vb.dma);

	// Re-Initialize variables
	buf->buffer.length = st->bufsize;
	buf->buffer.bytesused = 0;
	buf->count = 0;
	buf->type = 0;
	buf->pts_stamp = 0;

	/* setup buffer */
	buf->buffer.index = 0;

	/* Type */
	buf->buffer.type = st->buftype;

	buf->buffer.field =  0;
	buf->buffer.memory = 0;

	// Put in NEED INIT State for V4l2
	buf->vb.state = STATE_NEEDS_INIT;
}

void ivtv_init_v4l2buf(struct pci_dev *dev, struct ivtv_stream *st, struct scatterlist *sglist, struct ivtv_buffer *buf)
{
	buf->buffer.length = st->bufsize;
	buf->buffer.bytesused = 0;
	buf->count = 0;
	buf->type = 0;
	buf->pts_stamp = 0;

	/* setup buffer */
	buf->buffer.index = buf->vb.i;

	/* Type */
	buf->buffer.type = st->buftype;

	buf->buffer.field =  0;
	buf->buffer.memory = 0;

	return;
}

void ivtv_stream_free(struct ivtv *itv, int stream)
{
	struct ivtv_stream *s = &itv->streams[stream];

	del_timer_sync(&s->timeout);

	/* Free SG Array/Lists */
	if (s->SGarray != NULL) {
               	int SGsize = s->SG_length;

		if (s->SG_handle != IVTV_DMA_UNMAPPED && SGsize > 0) {
			pci_unmap_single(itv->dev, s->SG_handle,
					 (sizeof(struct ivtv_SG_element) *
					  SGsize), s->dma);
			s->SG_handle = IVTV_DMA_UNMAPPED;
		}
		s->SG_length = 0;

		kfree(s->SGarray);
		s->SGarray = NULL;
	}

	return;
}

const char *ivtv_stream_name(int streamtype)
{
	switch (streamtype)  {
		case IVTV_ENC_STREAM_TYPE_MPG:
			return "encoder MPEG";
		case IVTV_ENC_STREAM_TYPE_YUV:
			return "encoder YUV";
		case IVTV_ENC_STREAM_TYPE_VBI:
			return "encoder VBI";
		case IVTV_ENC_STREAM_TYPE_PCM:
			return "encoder PCM audio";
		case IVTV_ENC_STREAM_TYPE_RAD:
			return "encoder radio";
		default:
			return "unknown"; 
	}
}
