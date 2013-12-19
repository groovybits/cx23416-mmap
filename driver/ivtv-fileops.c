/* file operation functions
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
#include "ivtv-i2c.h"
#include "ivtv-queue.h"
#include "ivtv-irq.h"
#include "ivtv-vbi.h"
#include "ivtv-mailbox.h"
#include "ivtv-audio.h"
#include "ivtv-video.h"
#include "ivtv-streams.h"
#include "ivtv-cards.h"
#include "audiochip.h"
#include "cx25840.h"
#include "ivtv-ioctl.h"

typedef unsigned long uintptr_t;

/* This function tries to claim the stream for a specific file descriptor.
   If no one else is using this stream then the stream is claimed, any
   memory is allocated and associated VBI streams are also automatically
   claimed. Possible error returns: -EBUSY if someone else has claimed
   the stream, -ENOMEM when out of memory or 0 on success. */
int ivtv_claim_stream(struct ivtv_open_id *id, int type)
{
	struct ivtv *itv = id->itv;
	struct ivtv_stream *stream = &itv->streams[type];
	struct ivtv_stream *vbi_stream;
        int vbi_type;

	if (test_and_set_bit(IVTV_F_S_IN_USE, &stream->s_flags)) {
                // someone already claimed this stream
                if (stream->id == id->open_id) {
                        // yes, this file descriptor did. So that's OK.
                        return 0;
                }
                if (stream->id == -1 && (type == IVTV_ENC_STREAM_TYPE_VBI)) {
                        /* VBI is handled already internally, now also assign
                           the file descriptor to this stream for external
                           reading of the stream. */
                        stream->id = id->open_id;
                        IVTV_DEBUG_INFO("Start Read VBI\n");
                        return 0;
                }
                /* someone else is using this stream already */
                IVTV_DEBUG_WARN("Stream %d is busy\n", type);
                return -EBUSY;
        }
	stream->id = id->open_id;

	/* IVTV_DEC_STREAM_TYPE_MPG needs to claim IVTV_DEC_STREAM_TYPE_VBI,
	   IVTV_ENC_STREAM_TYPE_MPG needs to claim IVTV_ENC_STREAM_TYPE_VBI
           (provided VBI insertion is on and sliced VBI is selected), for all
           other streams we're done */	 
        if (type == IVTV_ENC_STREAM_TYPE_MPG &&
                   itv->vbi_insert_mpeg && itv->vbi_sliced_in->service_set) {
                vbi_type = IVTV_ENC_STREAM_TYPE_VBI;
        } else {
                return 0;
        }
	vbi_stream = &itv->streams[vbi_type];

        if (!test_and_set_bit(IVTV_F_S_IN_USE, &vbi_stream->s_flags)) {
        }
        // mark that it is used internally
        set_bit(IVTV_F_S_INTERNAL_USE, &vbi_stream->s_flags);
	return 0;
}

/* This function releases a previously claimed stream. It will take into
   account associated VBI streams. */
void ivtv_release_stream(struct ivtv *itv, int type)
{
	struct ivtv_stream *stream = &itv->streams[type];
        struct ivtv_stream *vbi_stream;
        int vbi_type;

	stream->id = -1;
        if ((type == IVTV_ENC_STREAM_TYPE_VBI) &&
            test_bit(IVTV_F_S_INTERNAL_USE, &stream->s_flags)) {
                // this stream is still in use internally
                return;
        }
	if (!test_and_clear_bit(IVTV_F_S_IN_USE, &stream->s_flags)) {
		IVTV_DEBUG_WARN("Release stream %d not in use!\n", type);
                return;
        }

	/* IVTV_ENC_STREAM_TYPE_MPG needs to release IVTV_ENC_STREAM_TYPE_VBI,
           for all other streams we're done */	 
        if (type == IVTV_ENC_STREAM_TYPE_MPG) {
                vbi_type = IVTV_ENC_STREAM_TYPE_VBI;
        } else {
                return;
        }
        vbi_stream = &itv->streams[vbi_type];

        // clear internal use flag
        if (!test_and_clear_bit(IVTV_F_S_INTERNAL_USE, &vbi_stream->s_flags)) {
                // was already cleared
                return;
        }
        if (vbi_stream->id != -1) {
                // VBI stream still claimed by a file descriptor
                return;
        }
        clear_bit(IVTV_F_S_IN_USE, &vbi_stream->s_flags);
}

int buffer_prepare(struct ivtvbuf_queue *q, 
	struct ivtvbuf_buffer *vb, enum v4l2_field field) 
{
	struct ivtv_buffer *buf = container_of(vb, struct ivtv_buffer, vb);
	struct ivtv_open_id *id = q->priv_data;
	struct ivtv *itv = id->itv;
	int type = id->type;
	struct ivtv_stream *st = &itv->streams[type];
	int rc, init_buffer = 0;
	
	if (!buf || !st || !itv)
		return -EINVAL;

 	if (NULL == st->fmt) {
		st->fmt = format_by_fourcc(V4L2_PIX_FMT_NV12);
        	st->height = itv->height;
        	st->width = itv->width;
        	st->field = V4L2_FIELD_INTERLACED;
	}

	if (buf->fmt != st->fmt || buf->vb.width  != st->width  ||
            	 buf->vb.height != st->height ||
            	 buf->vb.field  != field) 
	{
		buf->fmt = st->fmt;
		buf->type = st->type;

 		buf->vb.width  = st->width;
                buf->vb.height = st->height;
                buf->vb.field  = field;

		init_buffer = 1;
	}
	buf->vb.size = st->bufsize;
 	if (0 != buf->vb.baddr  &&  buf->vb.bsize < buf->vb.size)
               return -EINVAL;

	if (STATE_NEEDS_INIT == buf->vb.state) {
		init_buffer = 1;
		//if (0 != (rc = ivtv_iolock(itv, itv->dev, &buf->vb, st)))
		if (0 != (rc = ivtvbuf_iolock(itv->dev, &buf->vb, NULL)))
			goto fail;
	}

	if (init_buffer) {
		switch(buf->vb.field) {
			case V4L2_FIELD_TOP:
			case V4L2_FIELD_BOTTOM:
			case V4L2_FIELD_INTERLACED:
				// Setup IVTV V4L2 Buffer
				ivtv_init_v4l2buf(itv->dev, st,
					buf->vb.dma.sglist, buf);
				break;
			case V4L2_FIELD_SEQ_BT:
			case V4L2_FIELD_SEQ_TB:
				// Setup IVTV V4L2 Buffer
				ivtv_init_v4l2buf(itv->dev, st,
					buf->vb.dma.sglist, buf);
				break;
			default:
				 BUG();
		}
	}

	buf->vb.state = STATE_PREPARED;

 	IVTV_DEBUG_INFO("[%p/%d] buffer_prepare - %dx%d %dbpp \"%s\" fill size %d length %d\n",
                buf, buf->vb.i, st->width, st->height, st->fmt->depth, st->fmt->name,
		(int)buf->vb.bsize, (int)buf->vb.size);

	return 0;
fail:
	ivtv_free_v4lbuf(itv->dev, buf, st);
	return rc;
}

int buffer_setup(struct ivtvbuf_queue *q, unsigned int *count,
	unsigned int *size) 
{
	struct ivtv_open_id *id = q->priv_data;
	struct ivtv *itv = id->itv;
	int type = id->type;
	struct ivtv_stream *st = &itv->streams[type];

 	*size = st->bufsize;
        if (0 == *count)
                *count = st->buf_max;
        while (*size * *count > st->buf_max * st->bufsize)
                (*count)--;
        return 0;
}

static void do_cancel_buffers(struct ivtv_stream *st)
{
        struct ivtv_buffer *buf;

	// Queue
        while (!list_empty(&st->queued)) {
                buf = list_entry(st->queued.next, struct ivtv_buffer, vb.queue);
                list_del(&buf->vb.queue);
                buf->vb.state = STATE_ERROR;
                wake_up(&buf->vb.done);

		break;
        }
	// Active
        while (!list_empty(&st->active)) {
                buf = list_entry(st->active.next, struct ivtv_buffer, vb.queue);
                list_del(&buf->vb.queue);
                buf->vb.state = STATE_ERROR;
                wake_up(&buf->vb.done);

		break;
        }
}

void ivtv_timeout(unsigned long data)
{
        struct ivtv_stream *st = (struct ivtv_stream *)&data;

	printk(KERN_ERR "Timeout for Stream\n");

	if (st) {
		unsigned long flags;
        	printk(KERN_ERR "[%llu/%u] DMA Timeout Error stream %d, DMA Timed Out!!!\n",
                	 st->SG_handle, st->SG_length, st->type);

        	spin_lock_irqsave(&st->slock,flags);
		del_timer_sync(&st->timeout);

                /* Unmap SG Array */
                if (st->SG_handle != IVTV_DMA_UNMAPPED && st->SG_length > 0) {
                        int alignSize = (sizeof(struct ivtv_SG_element) * st->SG_length);

                        pci_unmap_single(st->dev, st->SG_handle, alignSize, st->dma);
                        memset(st->SGarray, 0, alignSize);

                        // Clear DMA
                        st->SG_handle = IVTV_DMA_UNMAPPED;
                        st->SG_length = 0;
                } else {
                        printk(KERN_ERR "[%llu/%u] DMA Timeout Error stream %d, DMA is not mapped!!!\n",
                                st->SG_handle, st->SG_length, st->type);
                }

        	do_cancel_buffers(st);
        	spin_unlock_irqrestore(&st->slock,flags);
	}
}

void buffer_queue(struct ivtvbuf_queue *vq, 
	struct ivtvbuf_buffer *vb) 
{
	struct ivtv_buffer *buf = container_of(vb, struct ivtv_buffer, vb);
	struct ivtv_open_id *id = vq->priv_data;
	struct ivtv *itv = id->itv;
	int type = id->type;
	struct ivtv_stream *st = &itv->streams[type];

        IVTV_DEBUG_INFO("Adding a buffer to the Queue\n" );
        list_add_tail(&buf->vb.queue,&st->queued);
        buf->vb.state = STATE_QUEUED;
        buf->count    = 1;
        IVTV_DEBUG_INFO("[%p/%d] %s - append to queue in state 0x%0x\n",
               buf, buf->vb.i, __FUNCTION__, buf->vb.state);
}

void buffer_release(struct ivtvbuf_queue *vq, 
	struct ivtvbuf_buffer *vb)
{ 
	struct ivtv_buffer *buf = container_of(vb, struct ivtv_buffer, vb);
	struct ivtv_open_id *id = vq->priv_data;
	struct ivtv *itv = id->itv;
	int type = id->type;
	struct ivtv_stream *st = &itv->streams[type];

	ivtv_free_v4lbuf(itv->dev, buf, st);
}

static struct ivtvbuf_queue_ops ivtv_video_qops = {
	.buf_setup   = buffer_setup,
	.buf_prepare = buffer_prepare,
	.buf_queue   = buffer_queue,
	.buf_release = buffer_release,
};

int ivtv_mmap(struct file *filp, struct vm_area_struct *vma)
{
	struct ivtv_open_id *id = filp->private_data;
	struct ivtv *itv = id->itv;
	int type = id->type;
	struct ivtv_stream *st = &itv->streams[type];

	return ivtvbuf_mmap_mapper(&st->vidq, vma);
}

static int ivtv_read(struct file *filp, char *ubuf, size_t count, loff_t * pos)
{
	struct ivtv_open_id *id = filp->private_data;
	struct ivtv *itv = id->itv;
	int type = id->type;
	struct ivtv_stream *st = &itv->streams[type];

	IVTV_DEBUG_INFO("ivtv_read: stream %d.. \n", type);

	if (atomic_read(&itv->capturing) == 0 && (st->id == -1)) {
		IVTV_DEBUG_WARN(
			   "Stream not initialized before read (shouldn't happen)\n");
		return -EIO;
	}

        if (test_bit(IVTV_F_T_ENC_DMA_QUEUED, &itv->t_flags) &&
                        !list_empty(&st->queued) && list_empty(&st->active))
        {
                ivtv_sched_DMA(itv, st->type);
        }

	st->first_read = 0;

 	switch (st->buftype) {
        	case V4L2_BUF_TYPE_VIDEO_CAPTURE:
                	return ivtvbuf_read_one(&st->vidq, ubuf, count, pos,
                                         filp->f_flags & O_NONBLOCK);
        	case V4L2_BUF_TYPE_VBI_CAPTURE:
                //	return ivtvbuf_read_stream(&st->vidq, ubuf, count, pos, 1,
                //                            filp->f_flags & O_NONBLOCK);
        	case V4L2_BUF_TYPE_SLICED_VBI_CAPTURE:
                	return ivtvbuf_read_one(&st->vidq, ubuf, count, pos,
                                            filp->f_flags & O_NONBLOCK);
        	default:
                	BUG();
                	return 0;
       	}

	return -EINVAL;
}

ssize_t ivtv_v4l2_read(struct file * filp, char *buf, size_t count,
		       loff_t * pos)
{
	struct ivtv_open_id *id = filp->private_data;
	struct ivtv_stream *stream;
       	struct ivtv_stream *vbi_stream;
	struct ivtv *itv = id->itv;
	int type = id->type;

	IVTV_DEBUG_INFO("v4l2 read\n");

	if (type == IVTV_ENC_STREAM_TYPE_RAD) {
		/* you cannot read from these stream types. */
		return -EPERM;
	}

	stream = &itv->streams[type];

	/* Try to claim this stream. */
	if (ivtv_claim_stream(id, type))
		return -EBUSY;

	/* If capture is already in progress, then we also have to
	   do nothing extra. */
	if (test_and_set_bit(IVTV_F_S_CAPTURING, &stream->s_flags)) {
		return ivtv_read(filp, buf, count, pos);
	}

	/* Start VBI capture if required */
       	vbi_stream = &itv->streams[IVTV_ENC_STREAM_TYPE_VBI];
        if (type == IVTV_ENC_STREAM_TYPE_MPG &&
            test_bit(IVTV_F_S_INTERNAL_USE, &vbi_stream->s_flags) &&
            !test_and_set_bit(IVTV_F_S_CAPTURING, &vbi_stream->s_flags)) {
                /* Note: the IVTV_ENC_STREAM_TYPE_VBI is claimed
                   automatically when the MPG stream is claimed. 
                   We only need to start the VBI capturing. */

		// Video 4 Linux 2 VBI Setup CHRISKENNEDY
		ivtvbuf_queue_init(&vbi_stream->vidq, &ivtv_video_qops, 	
			itv->dev, &vbi_stream->slock, vbi_stream->buftype, 
			vbi_stream->field, sizeof(struct ivtv_buffer), 
			itv);

		//IVTV_DEBUG_INFO("VIDEOC_STREAMON\n");
        	ivtvbuf_streamon(&vbi_stream->vidq);

        	if (ivtv_start_v4l2_encode_stream(itv, IVTV_ENC_STREAM_TYPE_VBI)) {
               		IVTV_DEBUG_WARN("VBI capture start failed\n");

                	/* Failure, clean up and return an error */
        		clear_bit(IVTV_F_S_CAPTURING, &vbi_stream->s_flags);
			clear_bit(IVTV_F_S_CAPTURING, &stream->s_flags);
                        /* also releases the associated VBI stream */
			ivtv_release_stream(itv, type);
                	return -EIO;
                }
               	IVTV_DEBUG_INFO("VBI insertion started\n");
	}

	//IVTV_DEBUG_INFO("VIDEOC_STREAMON\n");
        ivtvbuf_streamon(&stream->vidq);
	//ivtvbuf_read_start(&stream->vidq);

	/* Tell the card to start capturing */
	if (!ivtv_start_v4l2_encode_stream(itv, type)) {
        	/* We're done */
	        return ivtv_read(filp, buf, count, pos);
        }

        /* failure, clean up */
        IVTV_DEBUG_WARN("Failed to start capturing for stream %d\n", type);

        /* Note: the IVTV_ENC_STREAM_TYPE_VBI is released
           automatically when the MPG stream is released. 
           We only need to stop the VBI capturing. */
        if (type == IVTV_ENC_STREAM_TYPE_MPG &&
            test_bit(IVTV_F_S_CAPTURING, &vbi_stream->s_flags)) {
                ivtv_stop_capture(itv, IVTV_ENC_STREAM_TYPE_VBI);
                clear_bit(IVTV_F_S_CAPTURING, &vbi_stream->s_flags);
        }
        clear_bit(IVTV_F_S_CAPTURING, &stream->s_flags);
        ivtv_release_stream(itv, type);
        return -EIO;
}

ssize_t ivtv_v4l2_write(struct file * filp, const char *buf, size_t count,
			loff_t * pos)
{
	return -EIO;
}

unsigned int ivtv_v4l2_dec_poll(struct file *filp, poll_table * wait)
{
	return -EIO;
}

unsigned int ivtv_v4l2_enc_poll(struct file *filp, poll_table * wait)
{
	struct ivtv_open_id *id = filp->private_data;
	struct ivtv_stream *st;
	unsigned int mask = 0;
	struct ivtv *itv = id->itv;
	struct ivtv_buffer *buf;

	st = &itv->streams[id->type];
	if (!st || st->state == 0) {
		mask |= POLLERR;
		goto poll_exit;
	}

	/* Are we capturing */
	if (!test_bit(IVTV_F_S_CAPTURING, &st->s_flags)) {
		mask |= POLLERR;
		goto poll_exit;
	}

	/* Not Capturing?, invalid stream id */
	if (atomic_read(&itv->capturing) == 0 || (st->id == -1)) {
		mask |= POLLERR;
		goto poll_exit;
	}

	// V4L2 Streaming 
	/*if (V4L2_BUF_TYPE_VBI_CAPTURE == st->buftype) {
               	if (st->type != IVTV_ENC_STREAM_TYPE_VBI)
                       	return POLLERR;
               	return ivtvbuf_poll_stream(filp, &st->vidq, wait);
       	}*/

        if (test_bit(IVTV_F_T_ENC_DMA_QUEUED, &itv->t_flags) &&
                !list_empty(&st->queued) && list_empty(&st->active))
        {
                ivtv_sched_DMA(itv, st->type);
        }

       	if (st->vidq.streaming) {
               	/* streaming capture */

		/* OVERRIDE */
               	//return ivtvbuf_poll_stream(filp, &st->vidq, wait);

               	if (list_empty(&st->vidq.stream))
                       	return POLLERR;
               	buf = list_entry(st->vidq.stream.next,struct ivtv_buffer,vb.stream);
       	} else if (st->vidq.reading) {
                /* read() capture */
                down(&st->vidq.lock);
                if (NULL == st->vidq.read_buf) {
                        /* need to capture a new frame */
                        if (st->state == 0) {
                                up(&st->vidq.lock);
                                return POLLERR;
                        }
                        st->vidq.read_buf = ivtvbuf_alloc(st->vidq.msize);
                        if (NULL == st->vidq.read_buf) {
                                up(&st->vidq.lock);
                                return POLLERR;
                        }
                        st->vidq.read_buf->memory = V4L2_MEMORY_USERPTR;
                        if (0 != st->vidq.ops->buf_prepare(&st->vidq,st->vidq.read_buf,st->field)) {
                                kfree (st->vidq.read_buf);
                                st->vidq.read_buf = NULL;
                                up(&st->vidq.lock);
                                return POLLERR;
                        }
                        st->vidq.ops->buf_queue(&st->vidq,st->vidq.read_buf);
                        st->vidq.read_off = 0;
                }
                up(&st->vidq.lock);
                buf = (struct ivtv_buffer*)st->vidq.read_buf;

               	/* read() capture */
               	if (NULL == buf)
                      	return POLLERR;
       	} else
		return POLLERR;

       	if (buf->vb.state == STATE_DONE ||
           		buf->vb.state == STATE_ERROR)
               		return POLLIN|POLLRDNORM;

       	poll_wait(filp, &buf->vb.done, wait);

       	if (buf->vb.state == STATE_DONE ||
           		buf->vb.state == STATE_ERROR)
               		return POLLIN|POLLRDNORM;
       	return 0;

      poll_exit:
	return mask;
}

int ivtv_v4l2_release(struct ivtv *itv, struct ivtv_stream *st) {
	struct ivtv_buffer *buf;

 	/* stop video capture */
        if (st->vidq.streaming) {
		unsigned long flags;

		spin_lock_irqsave(&st->slock, flags);
		while(!list_empty(&st->active)) {
 			buf = list_entry(st->active.next, struct ivtv_buffer, vb.queue);

                	// Mark it Done and remove from queue
                	list_del(&buf->vb.queue);
                	buf->vb.state = STATE_ERROR;
                	wake_up(&buf->vb.done);

                	IVTV_DEBUG_INFO(
				"[%llu/%u] Wakeup close for Active stream %d buffer %d in state 0x%0x\n",
                       		st->SG_handle, st->SG_length, st->type, buf->vb.i, buf->vb.state);
		}
		while(!list_empty(&st->queued)) {
 			buf = list_entry(st->queued.next, struct ivtv_buffer, vb.queue);

                	// Mark it Done and remove from queue
                	list_del(&buf->vb.queue);
                	buf->vb.state = STATE_ERROR;
                	wake_up(&buf->vb.done);

                	IVTV_DEBUG_INFO(
				"[%llu/%u] Wakeup close for Queued stream %d buffer %d in state 0x%0x\n",
                       		st->SG_handle, st->SG_length, st->type, buf->vb.i, buf->vb.state);
		}
		spin_unlock_irqrestore(&st->slock, flags);
	}

        if (st->vidq.streaming || st->vidq.reading)
        	ivtvbuf_queue_cancel(&st->vidq);

        if (st->vidq.read_buf) {
                buffer_release(&st->vidq,st->vidq.read_buf);
                kfree(st->vidq.read_buf);
        }

        /* stop capture */
        if (st->vidq.streaming)
                ivtvbuf_streamoff(&st->vidq);
        if (st->vidq.reading)
                ivtvbuf_read_stop(&st->vidq);

	st->buf_total = 0;

        ivtvbuf_mmap_free(&st->vidq);

	st->vidq.priv_data = NULL;

	return 0;
}

int ivtv_v4l2_close(struct inode *inode, struct file *filp)
{
	struct ivtv_open_id *id = filp->private_data;
	struct ivtv_stream *stream, *vbi_stream;
	struct ivtv *itv;

	if (NULL == id) {
		printk(KERN_WARNING "ivtv:  invalid id on v4l2 close\n");
		return -ENODEV;
	}

	itv = id->itv;
	
	IVTV_DEBUG_INFO("v4l2 close\n");

	stream = &itv->streams[id->type];

	/* Easy case first: this stream was never claimed by us */
	if (stream->id != id->open_id) {
                kfree(id);
		return 0;
	}

	/* Stop radio */
	if (id->type == IVTV_ENC_STREAM_TYPE_RAD) {
		struct v4l2_frequency vf;

		// Closing radio device, return to TV mode
		ivtv_mute(itv);
		/* Switch tuner to TV */
		ivtv_tv_tuner(itv, VIDIOC_S_STD, &itv->std);
		/* Mark that the radio is no longer in use */
		clear_bit(IVTV_F_I_RADIO_USER, &itv->i_flags);
		/* Select TV frequency */
		vf.frequency = itv->freq_tv;
		vf.type = V4L2_TUNER_ANALOG_TV;
		ivtv_tv_tuner(itv, VIDIOC_S_FREQUENCY, &vf);
		/* Make sure IF demodulator is updated properly, or we'll get static */
		if (itv->options.tda9887 >= 0) {
                	int cfg = TDA9887_PORT1_ACTIVE;
			ivtv_tda9887(itv, VIDIOC_S_STD, &itv->std);
			if (itv->set_fm_high_sensitivity)
                		ivtv_tda9887(itv, TDA9887_SET_CONFIG, &cfg);
		}
		/* Select correct audio input (i.e. TV tuner or Line in) */
		ivtv_audio_set_io(itv);
		/* Start automatic sound detection (if supported) */
		ivtv_audio_freq_changed(itv);
		/* Done! Unmute and continue. */
		ivtv_unmute(itv);
		ivtv_release_stream(itv, id->type);
		kfree(id);
		return 0;
	}

	/* Stop capturing */
	if (test_bit(IVTV_F_S_CAPTURING, &stream->s_flags)) {
		vbi_stream = &itv->streams[IVTV_ENC_STREAM_TYPE_VBI];


		IVTV_DEBUG_INFO("close stopping capture\n");
		/* Special case: a running VBI capture for VBI insertion
		   in the mpeg stream. Need to stop that too. */
		if (id->type == IVTV_ENC_STREAM_TYPE_MPG &&
	            test_bit(IVTV_F_S_CAPTURING, &vbi_stream->s_flags) &&
                    vbi_stream->id == -1) {
			IVTV_DEBUG_INFO(
				"close stopping embedded VBI capture\n");

			ivtv_stop_capture(itv, IVTV_ENC_STREAM_TYPE_VBI);

			/* 'Unclaim' this stream */
			ivtv_v4l2_release(itv, vbi_stream);
		}

		ivtv_stop_capture(itv, id->type);
	}
	/* 'Unclaim' this stream */
	ivtv_v4l2_release(itv, stream);

	IVTV_DEBUG_INFO(
		"Close for stream %d freeing V4L2 Resources\n", id->type);
	ivtv_release_stream(itv, id->type);

	kfree(id);
	return 0;
}

int ivtv_v4l2_open(struct inode *inode, struct file *filp)
{
	int x, y = 0;
	struct ivtv_open_id *item;
	struct ivtv *itv = NULL;
	struct ivtv_stream *stream = NULL;
	int minor = MINOR(inode->i_rdev);

	/* Find which card this open was on */
	spin_lock(&ivtv_cards_lock);
	for (x = 0; itv == NULL && x < ivtv_cards_active; x++) {
		/* find out which stream this open was on */
		for (y = 0; y < ivtv_cards[x]->streamcount; y++) {
			stream = &ivtv_cards[x]->streams[y];
			if (stream->v4l2dev->minor == minor) {
				itv = ivtv_cards[x];
				break;
			}
		}
	}
	spin_unlock(&ivtv_cards_lock);

	if (itv == NULL) {
		/* Couldn't find a device registered 
		   on that minor, shouldn't happen! */
		printk(KERN_WARNING "ivtv:  no ivtv device found on minor %d\n", minor);
		return -ENXIO;
	}

	// Allocate memory
	item = kmalloc(sizeof(struct ivtv_open_id), GFP_KERNEL);
	if (NULL == item) {
		IVTV_DEBUG_WARN("nomem on v4l2 open\n");
		return -ENOMEM;
	}
	item->itv = itv;
	item->type = y;

	item->open_id = itv->open_id++;
	filp->private_data = item;

	if (item->type == IVTV_ENC_STREAM_TYPE_RAD) {
		struct v4l2_frequency vf;

		/* Try to claim this stream */
		if (ivtv_claim_stream(item, item->type)) {
			/* No, it's already in use */
			kfree(item);
			return -EBUSY;
		}

		/* We have the radio */
		ivtv_mute(itv);
		/* Switch tuner to radio */
		ivtv_radio_tuner(itv, AUDC_SET_RADIO, 0);
		/* Switch IF demodulator to radio */
		if (itv->options.tda9887 >= 0) {
                	int cfg = TDA9887_PORT1_INACTIVE;
			ivtv_tda9887(itv, AUDC_SET_RADIO, 0);
			if (itv->set_fm_high_sensitivity)
                		ivtv_tda9887(itv, TDA9887_SET_CONFIG, &cfg);
		}
		/* Mark that the radio is being used. */
		set_bit(IVTV_F_I_RADIO_USER, &itv->i_flags);
		/* Select radio frequency */
		vf.type = V4L2_TUNER_RADIO;
		vf.frequency = itv->freq_radio;
		ivtv_radio_tuner(itv, VIDIOC_S_FREQUENCY, &vf);
		/* Select the correct audio input (i.e. radio tuner) */
		ivtv_audio_set_io(itv);
		/* Start automatic sound detection (if supported) */
		ivtv_audio_freq_changed(itv);
		/* Done! Unmute and continue. */
		ivtv_unmute(itv);
	}

	// Video 4 Linux 2 Setup CHRISKENNEDY
	if (!test_bit(IVTV_F_S_CAPTURING, &stream->s_flags)) {
		ivtvbuf_queue_init(&stream->vidq, &ivtv_video_qops, 	
			itv->dev, &stream->slock, stream->buftype, 
			stream->field, sizeof(struct ivtv_buffer), 
			item);	
	}

	return 0;
}

void ivtv_mute(struct ivtv *itv)
{
	IVTV_DEBUG_INFO("Mute\n");
	/* Mute sound to avoid pop */
	ivtv_audio_set_mute(itv, 1);

	if (atomic_read(&itv->capturing))
        	ivtv_vapi(itv, IVTV_API_MUTE_AUDIO, 1);
}

void ivtv_unmute(struct ivtv *itv)
{
        /* initialize or refresh input */
        if (atomic_read(&itv->capturing) == 0)
                ivtv_vapi(itv, IVTV_API_INITIALIZE_INPUT, 0);

	if (atomic_read(&itv->capturing)) {
		ivtv_sleep_timeout(HZ / 10, 0);
		ivtv_vapi(itv, IVTV_API_ENC_MISC, 12);
		ivtv_vapi(itv, IVTV_API_MUTE_AUDIO, 0);
	}

	/* Unmute */
	ivtv_audio_set_mute(itv, 0);
	IVTV_DEBUG_INFO("Unmute\n");
}
