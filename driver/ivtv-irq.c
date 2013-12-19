/* interrupt handling
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
#include "ivtv-firmware.h"
#include "ivtv-fileops.h"
#include "ivtv-queue.h"
#include "ivtv-irq.h"
#include "ivtv-ioctl.h"
#include "ivtv-mailbox.h"
#include "ivtv-vbi.h"

typedef unsigned long uintptr_t;

static void cx23416_dma_start(struct ivtv *itv, int vbi);
static void cx23416_dma_finish(struct ivtv *itv);

IRQRETURN_T ivtv_irq_handler(int irq, void *dev_id, struct pt_regs *regs)
{
	struct ivtv *itv = (struct ivtv *)dev_id;
	u32 combo;
	u32 stat;

	spin_lock(&itv->DMA_slock);

	/* get contents of irq status register */
	stat = readl(itv->reg_mem + IVTV_REG_IRQSTATUS);

	combo = ~itv->irqmask & stat;

	/* Clear out IRQ */
	if (combo) writel(combo, (itv->reg_mem + IVTV_REG_IRQSTATUS));

	if (0 == combo) {
		/* The vsync interrupt is unusual & clears itself. If we
		 * took too long, we may have missed it. Do some checks
		 */
			/* No Vsync expected, wasn't for us */
		spin_unlock(&itv->DMA_slock);
		return IRQ_NONE;
	}

	IVTV_DEBUG_IRQ(
		   "======= valid IRQ bits: 0x%08x ======\n", combo);

	// DMA Read Done Interrupt
	if (combo & (IVTV_IRQ_DMA_READ | IVTV_IRQ_DEC_DMA_COMPLETE)) {
		IVTV_DEBUG_IRQ("IRQ: IVTV_IRQ_DEC_DMA_COMPLETE\n");
	}

	// DMA Write Done Interrupt
	if (combo & (IVTV_IRQ_ENC_DMA_COMPLETE/*|IVTV_IRQ_DMA_WRITE*/)) {
		IVTV_DEBUG_DMA("IRQ: IVTV_IRQ_ENC_DMA_MPEGDONE\n");

		/* Release DMA */
		cx23416_dma_finish(itv);
	}

	// DMA Error with Link List
	if (combo & IVTV_IRQ_DMA_ERR) {
		IVTV_DEBUG_WARN(
			   "IRQ: Link List DMA Error has occured!!!, finishing DMA Xfer...\n");

		writel(0x0, itv->reg_mem + IVTV_REG_DMAXFER);
		cx23416_dma_finish(itv);

		writel(combo&(IVTV_IRQ_DMA_ERR), 
			(itv->reg_mem + IVTV_REG_IRQSTATUS));
	}

	// Encoder DMA Request
	if (combo & IVTV_IRQ_ENC_START_CAP) {
		IVTV_DEBUG_DMA("IRQ ENC DMA\n");

		/* Setup DMA and send */
		cx23416_dma_start(itv, 0);
	}

	// VIM Reset/Refresh needed
	if (combo & IVTV_IRQ_ENC_VIM_RST) {
		IVTV_DEBUG_IRQ("VIM Restart\n");
		ivtv_vapi(itv, IVTV_API_REFRESH_INPUT, 0);
	}

	// Encoder VBI DMA Request
	if (combo & IVTV_IRQ_ENC_VBI_CAP) {
		IVTV_DEBUG_IRQ("IRQ ENC VBI DMA\n");
		set_bit(IVTV_F_T_ENC_VBI_DMA, &itv->t_flags);
		cx23416_dma_start(itv, 1);
	}

	// Encoder EOS Interrupt
	if (combo & IVTV_IRQ_ENC_EOS) {
		IVTV_DEBUG_IRQ("Encoder End Of Stream\n");
		set_bit(IVTV_F_I_EOS, &itv->i_flags);
		wake_up(&itv->cap_w);
	}

	/* If we've just handled a 'forced' vsync, it's safest to say it
	 * wasn't ours. Another device may have triggered it at just
	 * the right time.
	 */
	spin_unlock(&itv->DMA_slock);
	return IRQ_HANDLED;
}

void cx23416_dma_start(struct ivtv *itv, int vbi) {
	u32 reQdata[IVTV_MBOX_MAX_DATA], reQresult;
	u32 type, status;
	u64 pts_stamp = 0;
	u32 reQtype, size, offset;
	u32 UVsize = 0, UVoffset = 0;
	u64 reQpts_stamp = 0;
	struct ivtv_stream *st = NULL;
	struct ivtv_stream *reQst = NULL;
	int x = 0;
	int streamtype = -1;
	u32 bufptr = 0;
	//int queueCount, rdPtr, wrPtr;

	if (vbi == 0) {
		/* Get DMA destination and size arguments from card */
		x = ivtv_api_getresult_nosleep(itv, &itv->enc_mbox[9], &reQresult, &reQdata[0]);
		if (x) {
			IVTV_DEBUG_WARN("error:%d getting DMA Request info\n", x);
			return;
		}
		reQtype = reQdata[0];
	} else
		reQtype = 0x03;

	switch (reQtype) {
	case 0:		/* MPEG */
		offset = reQdata[1];
		size = reQdata[2];
		reQpts_stamp = ((u64) reQdata[5] << 32) | reQdata[6];
		IVTV_DEBUG_INFO(
			   "DMA/MPG type 0x%08x,size 0x%08x,offset 0x%08x\n",
			   reQtype, size, offset);
		streamtype = IVTV_ENC_STREAM_TYPE_MPG;
		break;

	case 1:		/* YUV */
		offset = reQdata[1];
		size = reQdata[2];
		UVoffset = reQdata[3];
		UVsize = reQdata[4];
		reQpts_stamp = ((u64) reQdata[5] << 32) | reQdata[6];

		IVTV_DEBUG_INFO(
			   "DMA/YUV type 0x%08x,Ysize 0x%08x,Yoffset 0x%08x,"
			   "UVsize 0x%08x,UVoffset 0x%08x,PTS 0x%09llx\n",
			   reQtype, size, offset, UVsize, UVoffset, reQpts_stamp);

		streamtype = IVTV_ENC_STREAM_TYPE_YUV;
		break;

	case 2:		/* PCM (audio) */
		offset = reQdata[1] + 12;
		size = reQdata[2] - 12;
		reQpts_stamp = ((u64) reQdata[5] << 32) | reQdata[6];
#if 0
		reQpts_stamp = *(u32 *) (itv->enc_mem + offset - 8);
		reQpts_stamp |=
		    (u64) (*(u32 *) (itv->enc_mem + offset - 12)) << 32;
#endif
		IVTV_DEBUG_INFO(
			   "DMA/PCM type 0x%08x,size 0x%08x,offset 0x%08x "
			   "PTS 0x%09llx\n", reQtype, size, offset, pts_stamp);
		streamtype = IVTV_ENC_STREAM_TYPE_PCM;

		break;
	case 3:
 		bufptr = readl(itv->enc_mem + (itv->vbi_enc_start-4))+12;

                if (!test_bit(IVTV_F_T_ENC_VBI_STARTED, &itv->t_flags)) {
                        IVTV_DEBUG_INFO(
                                "VBI: Capture not started\n");
                        return;
                }

                itv->vbi_frame += itv->vbi_fpi;

		// Trim off header and time stamp
		offset = reQdata[1]+12;

		// Size isn't right yet
		//size = reQdata[2];

                //offset = bufptr;
                size = ((itv->vbi_enc_size+16) * (itv->vbi_fpi))-16;


                reQpts_stamp = *(u32 *) (itv->enc_mem + offset - 4);
                reQpts_stamp |=
                        (u64) (*(u32 *) (itv->enc_mem + offset - 8))
                        << 32;

		// Trim off start code
		//offset = reQdata[1]+16;

                IVTV_DEBUG_DMA(
                        "schedDMA: VBI(%d) BufPTR: 0x%08x FWsize: 0x%08x PTS: 0x%09llx\n",
                        size, bufptr, size, reQpts_stamp);

		streamtype = IVTV_ENC_STREAM_TYPE_VBI;

		break;
	default:
		offset = reQdata[1];
		size = reQdata[2];
		UVoffset = reQdata[3];
		UVsize = reQdata[4];
		reQpts_stamp = ((u64) reQdata[5] << 32) | reQdata[6];
		IVTV_DEBUG_WARN(
			   "DMA/UNKNOWN NOT SUPPORTED Request 0x%08x,Ysize 0x%08x,Yoffset 0x%08x,"
			   "UVsize 0x%08x,UVoffset 0x%08x,PTS 0x%09llx\n",
			   reQtype, size, offset, UVsize, UVoffset, reQpts_stamp);
		return;
	} 
	if (streamtype >= itv->streamcount) {
		IVTV_DEBUG_WARN(
			   "DMA Request: No stream handler for type %d\n", streamtype);
		return;
	}
	// Get Stream
	reQst = &itv->streams[streamtype];

	// DMA Request Storage
	if (reQst) {
		int bytes_needed = 0;
		int newsize = 0;

		reQst->pts = reQpts_stamp;
		reQst->dmatype = reQtype;

		// Previous DMA Status
		type =  st->dma_info.type;
		status =  st->dma_info.status;
		pts_stamp =  st->dma_info.pts_stamp;

		// Check if DMA is already Pending
		if (test_and_set_bit(IVTV_F_S_DMAP, &reQst->s_flags)) {
			IVTV_DEBUG_WARN(
			   "DMA Request: Already a DMA Pending for %d\n", streamtype);

			IVTV_DEBUG_WARN(
			   "DMA/Pending Request 0x%08x,Ysize 0x%08x,Yoffset 0x%08x,"
			   "UVsize 0x%08x,UVoffset 0x%08x,PTS 0x%09llx\n",
			   reQtype, size, offset, UVsize, UVoffset, reQpts_stamp);

			IVTV_DEBUG_WARN("DMA/Last Request 0x%08x,Ysize 0x%08x,Yoffset 0x%08x,"
			   "UVsize 0x%08x,UVoffset 0x%08x,PTS 0x%09llx,BytesNeeded %d,"
			   "ReqDone %d,InfoDone %d\n",
				reQst->dma_req.type, reQst->dma_req.size,
				reQst->dma_req.offset, reQst->dma_req.UVoffset,
				reQst->dma_req.UVsize,
				reQst->dma_req.pts_stamp,
				(int)reQst->dma_req.bytes_needed,
				reQst->dma_req.done, reQst->dma_info.done);

			// Try to Finish Pending DMA
			cx23416_dma_finish(itv);

			// If still locked then turn on debugging and fail
			if (test_and_set_bit(IVTV_F_S_DMAP, &reQst->s_flags)) {
				IVTV_DEBUG_WARN(
			   		"DMA Request: Still a DMA Pending for %d\n", streamtype);

				//set_bit(IVTV_F_T_ENC_DMA_QUEUED, &itv->t_flags);

				// Failed
				return;
			}
		}

		// See how much data is needed
		newsize = ((size+(PAGE_SIZE-1))&PAGE_MASK);
		if (UVsize)
			size = newsize;
		bytes_needed = size + UVsize;

		reQst->dma_req.id++;
		reQst->dma_req.type 		= reQtype;
		reQst->dma_req.offset 		= offset;
		reQst->dma_req.size 		= size;
		reQst->dma_req.UVoffset 	= UVoffset;
		reQst->dma_req.UVsize 		= UVsize;
		reQst->dma_req.pts_stamp 	= reQpts_stamp;
		reQst->dma_req.bytes_needed 	= bytes_needed;
		reQst->dma_req.done 		= 0x00;
		reQst->dma_info.done 		= 0x00;

		// Check to see if no other DMA is in progress, else we are probably not
		// supposed to run at all.
		if (test_and_set_bit(IVTV_F_S_DMAP, &itv->DMAP)) {
			IVTV_DEBUG_WARN(
		   	"DMA Request: Already a DMA In Progress for HW Type %d\n", 
				reQtype);

			// Mark that the DMA has been ignored for now...
			//set_bit(IVTV_F_S_NEEDS_DATA, &itv->DMAP);
			set_bit(IVTV_F_T_ENC_DMA_QUEUED, &itv->t_flags);

			return;
		}

		// Check Firmware
		if ((readl((unsigned char *)
			   itv->reg_mem + IVTV_REG_DMASTATUS) & 0x02) &&
	    		!(readl((unsigned char *)
			    itv->reg_mem + IVTV_REG_DMASTATUS) & 0x18))
		{
			// DMA Xfer Able to be done
                       	IVTV_DEBUG_DMA(
                            "DMA Request: stream %d Ready for Xfer.\n", 
				reQst->type);
		} else {
                	IVTV_DEBUG_WARN(
                       		"DMA Request: stream %d Firmware is busy!!!.\n", reQst->type);

			clear_bit(IVTV_F_S_DMAP, &reQst->s_flags);

			set_bit(IVTV_F_T_ENC_DMA_QUEUED, &itv->t_flags);
			return;
		}
	}

 	// Set off request
        if (!ivtv_sched_DMA(itv, streamtype)) {
		clear_bit(IVTV_F_S_DMAP, &reQst->s_flags);
		set_bit(IVTV_F_T_ENC_DMA_QUEUED, &itv->t_flags);
        	atomic_set(&itv->r_intr, 1);
	}
}

void cx23416_dma_finish(struct ivtv *itv) {
	u32 data[IVTV_MBOX_MAX_DATA], result;
	u32 type, status;
	u64 pts_stamp = 0;
	struct ivtv_stream *st = NULL;
	int x = 0;
	int stmtype = -1;


	/* Get DMA destination and size arguments from card */
	x = ivtv_api_getresult_nosleep(itv, &itv->enc_mbox[8], &result, &data[0]);
	if (x) {
		IVTV_DEBUG_WARN("error:%d getting Done DMA info\n", x);
		return;
	}
	status = data[0];
	type = data[1];
	pts_stamp = ((u64) data[2] << 32) | data[3];

	IVTV_DEBUG_DMA(
		   "DMA Done type 0x%08x,status 0x%08x PTS 0x%09llx\n",
		   type, status, pts_stamp);

	// Check Type
	if (type == 0) {
		clear_bit(IVTV_F_T_ENC_MPEGDONE, &itv->t_flags);
		stmtype = IVTV_ENC_STREAM_TYPE_MPG;
	} else if (type == 1) {
		clear_bit(IVTV_F_T_ENC_YUVDONE, &itv->t_flags);
		stmtype = IVTV_ENC_STREAM_TYPE_YUV;
	} else if (type == 2) {
		clear_bit(IVTV_F_T_ENC_PCMDONE, &itv->t_flags);
		stmtype = IVTV_ENC_STREAM_TYPE_PCM;
	} else if (type == 3) {
		clear_bit(IVTV_F_T_ENC_VBIDONE, &itv->t_flags);
		stmtype = IVTV_ENC_STREAM_TYPE_VBI;
	} else {
		IVTV_DEBUG_WARN(
	   		"DMA Done type 0x%08x,status 0x%08x PTS 0x%09llx is not VALID!!!\n",
	   		type, status, pts_stamp);
		return; // Invalid Type
	}

	st = &itv->streams[stmtype];

	// No DMA Pending?
	if (!test_bit(IVTV_F_S_DMAP, &st->s_flags)) {
		IVTV_DEBUG_WARN(
	   		"DMA Done type 0x%08x,status 0x%08x PTS 0x%09llx Has No DMA Pending!!!\n",
	   		type, status, pts_stamp);
		return;
	}

    	if (!test_and_clear_bit(IVTV_F_S_DMAP, &itv->DMAP)) {
                IVTV_DEBUG_WARN(
                   "DMA Done: NO DMA In Progress for HW Type %d\n",
                        type);

                // Mark that the DMA is free for the taking...
                clear_bit(IVTV_F_S_NEEDS_DATA, &itv->DMAP);

                return;
        }

	// Streaming DMA Done is simpler
	if (st) {
		IVTV_DEBUG_DMA("ENC: DMA Done for stream %d\n", stmtype);
		st->dma_info.id++;
		st->dma_info.type 	= type;
		st->dma_info.status 	= status;
		st->dma_info.pts_stamp 	= pts_stamp;
	} else
		return;

	if (!st || !test_bit(IVTV_F_S_DMAP, &st->s_flags) ||
		st->dma_info.done 	!= 0x00 ||
		st->dma_req.done 	!= 0x00 ||
		st->SG_length <= 0)
	{
		IVTV_DEBUG_DMA("ENC: DMA Done, no xfer - DMAP=%d, info.done=%d, req.don=%d, SG_len=%d\n",
			(test_bit(IVTV_F_S_DMAP, &st->s_flags)), 
			st->dma_info.done, st->dma_req.done, st->SG_length);
		// Nothing to finish?
		if (st->SG_length == 0) {
			clear_bit(IVTV_F_S_DMAP, &st->s_flags);
			set_bit(IVTV_F_T_ENC_DMA_QUEUED, &itv->t_flags);
			st->dma_info.done 	= 0x01;
			st->dma_req.done 	= 0x01;
			return;
		} else {
			// Fix up since seems to be wrong?
			st->dma_info.done 	= 0x00;
			st->dma_req.done 	= 0x00;
			set_bit(IVTV_F_S_DMAP, &st->s_flags);
		}
	}

	IVTV_DEBUG_DMA("ENC: DMA Done for stream %d\n", stmtype);
	if (!(status & 0x02)) {
		IVTV_DEBUG_WARN(
		   "Busy DMA Done type 0x%08x,status 0x%08x PTS 0x%09llx\n",
		   type, status, pts_stamp);

		return; // Not done yet
	} else if ((status & 0x18)) {
		IVTV_DEBUG_WARN(
		   "Error with DMA Done type 0x%08x,status 0x%08x PTS 0x%09llx\n",
		   type, status, pts_stamp);

		clear_bit(IVTV_F_S_DMAP, &st->s_flags);
		set_bit(IVTV_F_T_ENC_DMA_QUEUED, &itv->t_flags);
		st->dma_info.done 	= 0x01;
		st->dma_req.done 	= 0x01;

		return; // DMA Error
	}
 	// Wait till buffer is written to
        if (!ivtv_FROM_DMA_done(itv, stmtype)) {
       		atomic_set((&itv->w_intr), 1);
	}
}

int ivtv_FROM_DMA_done(struct ivtv *itv, int stmtype)
{
	struct ivtv_stream *stream = NULL;
	struct ivtv_buffer *buf;
	unsigned long flags;
	int retval = 1;

	stream = &itv->streams[stmtype];

	if (!stream || !test_bit(IVTV_F_S_DMAP, &stream->s_flags) ||
		stream->dma_info.done 	!= 0x00 ||
		stream->dma_req.done 	!= 0x00 ||
		stream->SG_length <= 0)
	{
		IVTV_DEBUG_DMA("ENC: DMA Done, no xfer - DMAP=%d, info.done=%d, req.don=%d, SG_len=%d\n",
			(test_bit(IVTV_F_S_DMAP, &stream->s_flags)), 
			stream->dma_info.done, stream->dma_req.done, stream->SG_length);
		// Nothing to finish?
		return 0;
	}
	IVTV_DEBUG_DMA("ENC: DMA Done for stream %d\n", stmtype);

	// Streaming DMA Done is simpler
	if (stream) {

        	/* Unmap SG Array */
        	if (stream->SG_handle != IVTV_DMA_UNMAPPED && stream->SG_length > 0) {
			int alignSize = (sizeof(struct ivtv_SG_element) * stream->SG_length);

                	pci_unmap_single(itv->dev, stream->SG_handle,
                                 alignSize, stream->dma);
			memset(stream->SGarray, 0, alignSize);

			// Clear DMA
                	stream->SG_handle = IVTV_DMA_UNMAPPED;
			stream->SG_length = 0;
        	} else {
                       	IVTV_DEBUG_WARN("[%llu/%u] DMA Done Error stream %d, DMA is not mapped!!!\n",
                               	stream->SG_handle, stream->SG_length, stream->type);

			goto all_dma_done; // Something is terribly wrong here
		}

		del_timer(&stream->timeout);

		// Get buffer and free it
		spin_lock_irqsave(&stream->slock, flags);
        	if (!list_empty(&stream->active)) {
                	buf = list_entry(stream->active.next, struct ivtv_buffer, vb.queue);

			do_gettimeofday(&buf->vb.ts);

			// Mark it Done and remove from queue
                	buf->vb.state = STATE_DONE;
               		list_del(&buf->vb.queue);
               		wake_up(&buf->vb.done);

                       	IVTV_DEBUG_DMA("[%llu/%u] DMA Done DMA for stream %d buffer %d in state 0x%0x\n",
                               	stream->SG_handle, stream->SG_length, stream->type, buf->vb.i, buf->vb.state);
        	} else {
                       	IVTV_DEBUG_WARN("ERROR [%llu/%u] DMA Done for stream %d Doesn't have any buffers!!!!\n",
                               	stream->SG_handle, stream->SG_length, stream->type);
		}
		spin_unlock_irqrestore(&stream->slock, flags);
	} else {
		stream->dma_info.done 	= 0x01;
		stream->dma_req.done 	= 0x01;
		clear_bit(IVTV_F_S_DMAP, &stream->s_flags);
                IVTV_DEBUG_WARN("ERROR DMA Done for stream is NULL!!!!\n");
		return 0;
	}

all_dma_done:

	stream->dma_info.done 	= 0x01;
	stream->dma_req.done 	= 0x01;
	clear_bit(IVTV_F_S_DMAP, &stream->s_flags);

	del_timer(&stream->timeout);
	wake_up(&stream->waitq);

	return retval;
}

int ivtv_sched_DMA(struct ivtv *itv, int streamtype)
{
	u32 result = 0;
	u32 type, size, offset, pad = 0;
	u32 UVsize = 0, UVoffset = 0;
	u64 pts_stamp = 0;
	struct ivtv_stream *st;
	int x = 0;
	int uvflag = 0;
	long sequence;
	u32 bytes_needed = 0, bytes_read = 0, bytes_received = 0;
	struct ivtv_buffer *buf = NULL;
	int xfer_pad;
	int pio_mode = 0;
	/* Set these as you wish */
	unsigned long flags;
	int page_count = 0;
	int y_page_count = 0;
	int uv_page_count = 0;
	int fwoffset;
	int retval = 1;

	/* Get DMA destination and size arguments from card */
	st = &itv->streams[streamtype];

	// Make sure it's active
	if (!st || st->id == -1) {
		if (st) {
	    		clear_bit(IVTV_F_S_DMAP, &st->s_flags);
			clear_bit(IVTV_F_T_ENC_DMA_QUEUED, &st->s_flags);
		}
		IVTV_DEBUG_WARN("error: DMA Schedule called and Stream is not in use!!!\n");
		return retval;
	}

	
	if (!(test_bit(IVTV_F_T_ENC_DMA_QUEUED, &st->s_flags)|test_bit(IVTV_F_S_DMAP, &st->s_flags)) ||
                st->dma_info.done   != 0x00 ||
                st->dma_req.done    != 0x00 ||
                st->SG_length > 0)
        {
		IVTV_DEBUG_DMA("ENC: Sched DMA, nothing to transfer - DMAP=%d, info.done=%d, req.don=%d, SG_len=%d\n",
			(test_bit(IVTV_F_S_DMAP, &st->s_flags)), st->dma_info.done, st->dma_req.done, st->SG_length);
                // Nothing to finish?
                return 0;
        }
	set_bit(IVTV_F_S_DMAP, &st->s_flags);

	IVTV_DEBUG_DMA("ENC: Sched DMA\n");

	// Stream information filled in HERE...
	offset =	st->dma_req.offset;
	size =		st->dma_req.size;
	UVoffset =	st->dma_req.UVoffset;
	UVsize =	st->dma_req.UVsize;
	pts_stamp =	st->dma_req.pts_stamp;
	bytes_needed =	st->dma_req.bytes_needed;

	type = st->dmatype;
	st->pts = st->dma_req.pts_stamp;

	// Check Firmware
	if (st->dma_req.done == 0 && ((readl((unsigned char *)
			   itv->reg_mem + IVTV_REG_DMASTATUS) & 0x02) &&
	    		!(readl((unsigned char *)
			    itv->reg_mem + IVTV_REG_DMASTATUS) & 0x18)))
	{
		// Do a DMA Xfer
                       IVTV_DEBUG_DMA(
                            "DMA Request: stream %d Ready for Xfer.\n", 
			st->type);
	} else {
                IVTV_DEBUG_WARN(
                       "DMA Request: stream %d Firmware is busy!!!.\n", st->type);

		goto requeueDMA;
	}

	// CKENNEDY FIXME 
       IVTV_DEBUG_DMA(
            "DMA Request: stream %d Ready for Xfer of buffer index %d bytes.\n", 
		st->type, bytes_needed);

	// If we failed
	if (!st->state) {
                IVTV_DEBUG_WARN(
                       "DMA Request: stream %d Xfer failed since state = 0.\n", st->type);
		clear_bit(IVTV_F_S_DMAP, &st->s_flags);
		clear_bit(IVTV_F_T_ENC_DMA_QUEUED, &st->s_flags);
		return 1;
	}

	// Get Free Buffer
	spin_lock_irqsave(&st->slock, flags);
	if (!list_empty(&st->queued)) {
       		buf = list_entry(st->queued.next,struct ivtv_buffer,vb.queue);
	} else {
                IVTV_DEBUG_DMA(
                       "DMA Request: stream %d Xfer failed in since stream empty.\n", st->type);

		spin_unlock_irqrestore(&st->slock, flags);
		goto requeueDMA;
	}
	spin_unlock_irqrestore(&st->slock, flags);

	sequence = ++st->seq;

	/* increment the sequence # */
	IVTV_DEBUG_DMA(
	   "ENC: stream %d sequence %d Starting in state %x and streaming=%x\n", 
		st->type, (int)sequence, st->state, st->streaming);

	buf->buffer.bytesused = 0;

	// Time Code
	buf->buffer.timecode.type = 0x00; // 0x01 = drop frame
	buf->buffer.timecode.flags = V4L2_TC_TYPE_30FPS;
	buf->buffer.timecode.frames = st->seq;
	buf->buffer.timecode.seconds = do_div(pts_stamp,1000);
	buf->buffer.timecode.minutes = 
		do_div(buf->buffer.timecode.seconds,60);
	buf->buffer.timecode.hours = 
		do_div(buf->buffer.timecode.minutes,60);
	buf->pts_stamp = pts_stamp;

	page_count = buf->vb.dma.nr_pages;
	y_page_count = 0;
	uv_page_count = 0;
	fwoffset = offset;
	y_page_count = ((size)+(PAGE_SIZE-1)/PAGE_SIZE)/PAGE_SIZE;
	if (UVsize) {
		uv_page_count = ((UVsize)+(PAGE_SIZE-1)/PAGE_SIZE)/PAGE_SIZE;
	}
       	IVTV_DEBUG_DMA("Building SG Array: with %d pages for Y and %d pages for UV\n", 
		y_page_count, uv_page_count);

	if (type == 3)
		buf->vb.field_count = itv->vbi_frame * 2;
	else
		buf->vb.field_count = sequence * 2;	

	for (x = 0; x < page_count && bytes_read < bytes_needed; x++) {
                /* extract the buffers we procured earlier */

                buf->buffer.index = x;
                buf->buffer.sequence = sequence;

                bytes_read += sg_dma_len(&buf->vb.dma.sglist[x]);

                if (size < sg_dma_len(&buf->vb.dma.sglist[x])) {
                        xfer_pad = 256; // Java processor requirement 256 byte align reads
                        pad = size;
                        buf->buffer.bytesused += size;
			//if (size < PAGE_SIZE) // at least 4096k transfer
			//	size = PAGE_SIZE;
                        if (size > xfer_pad && size % xfer_pad) /* Align */
                               	size = ((size+(xfer_pad-1))/xfer_pad)*xfer_pad;
                        if (size < xfer_pad)    /* Too small */
                               	size = xfer_pad;
                        st->SGarray[x].size = size;
                        size = 0;

                } else {
                        pad = 0;
                        buf->buffer.bytesused += sg_dma_len(&buf->vb.dma.sglist[x]);;
                        st->SGarray[x].size = sg_dma_len(&buf->vb.dma.sglist[x]);
                        size -= st->SGarray[x].size;
                }
                st->SGarray[x].src = offset;    /* Encoder Addr */
                st->SGarray[x].dst = sg_dma_address(&buf->vb.dma.sglist[x]);   /* Encoder Addr */

                /* PIO Mode */
                if (pio_mode) {
                        memcpy_fromio((void *)buf->buffer.m.userptr,
                                      (void *)(itv->enc_mem + offset),
                         st->SGarray[x].size);
                }
                offset += st->SGarray[x].size;  /* Increment Enc Addr */

                if ((size == 0) && (type == 1) && (uvflag == 0)) {      /* YUV */
                        /* process the UV section */
                        offset = UVoffset;
                        size = UVsize;
                        uvflag = 1;
                }
        }
	buf->buffer.length = buf->vb.size;
	buf->vb.size = bytes_needed;

       	IVTV_DEBUG_DMA("Built SG Array: with %d bytes for Y and %d bytes for UV or %d total, x=%d page_count=%d\n", 
		(int)(y_page_count*PAGE_SIZE), (int)(uv_page_count*PAGE_SIZE), bytes_read, x, page_count);

	/* This should wrap gracefully */
	st->trans_id++;

	/* Mark last buffer size for Interrupt flag */
	if (x <=0) {
		// No SG Array ???
		IVTV_DEBUG_WARN("Error Encoder DMA SG Array has nothing to allocate = 0\n");
		set_bit(IVTV_F_T_ENC_DMA_QUEUED, &itv->t_flags);

		// Failed
		goto requeueDMA;
	}
	st->SG_length = x;
	st->SGarray[st->SG_length - 1].size |= 0x80000000;

       	IVTV_DEBUG_DMA(
    		"[0x%08llx/%d] Setup DMA Buffer 0x%08x Bytes, %d pages, %d Stream, Buf Index %d, State 0x%0x\n",
       		(u64)st->SG_handle, st->SG_length, bytes_needed, buf->vb.dma.nr_pages, 
		st->type, buf->vb.i, buf->vb.state);

	/* Sync Hardware SG List of buffers */
	if (st->SGarray) {
		int alignSize = (sizeof(struct ivtv_SG_element) * st->SG_length);
       		st->SG_handle = pci_map_single(itv->dev, (void *)st->SGarray,
                	alignSize, st->dma);
	} else {
		// No SG Array ???
		IVTV_DEBUG_WARN("Error Encoder DMA SG Array not allocated!!!\n");
		set_bit(IVTV_F_T_ENC_DMA_QUEUED, &itv->t_flags);

		// Failed
		goto requeueDMA;
	}

        IVTV_DEBUG_DMA(
	    "[0x%08llx/%d] DMA Sched for 0x%08x Bytes, 0x%08x SG Size, %d Stream\n",
           (u64)st->SG_handle, st->SG_length, bytes_read, bytes_received, st->type);

        // Put Buffer into the active queue to send
	spin_lock_irqsave(&st->slock, flags);
        list_del(&buf->vb.queue);
        buf->vb.state = STATE_ACTIVE;
        buf->count = st->count++;
	list_add_tail(&buf->vb.queue, &st->active);

	// Send DMA Xfer (rotate mailbox)
	if (itv->dmaboxnum == 5)
		itv->dmaboxnum = 6;
	else
		itv->dmaboxnum = 5;
#if 1 // Either API or Do it Manually with the registers (dangerous)
	result = ivtv_api_sendDMA(itv, &itv->enc_mbox[itv->dmaboxnum], 
		type, st->SG_handle, bytes_read);
#else
	/* put SG Handle into register 0x0c */
	ivtv_write_reg(st->SG_handle, itv->reg_mem + IVTV_REG_ENCDMAADDR);
	if (readl(itv->reg_mem + IVTV_REG_ENCDMAADDR) != st->SG_handle)
		ivtv_write_reg(st->SG_handle,
			       itv->reg_mem + IVTV_REG_ENCDMAADDR);

	/* Send DMA with register 0x00, using the enc DMA bit 0x02 */
	if (readl(itv->reg_mem + IVTV_REG_ENCDMAADDR) == st->SG_handle) {
		ivtv_write_reg(readl(itv->reg_mem + IVTV_REG_DMAXFER) |
			       0x02, itv->reg_mem + IVTV_REG_DMAXFER);
		result = 0;
	} else {
		// Try API if we must
		result = ivtv_api_sendDMA(itv, &itv->enc_mbox[itv->dmaboxnum], 
			type, st->SG_handle, bytes_read);
	}
#endif	
	// Set results of done, usually 0x00
	if (result == 0) {
		clear_bit(IVTV_F_T_ENC_DMA_QUEUED, &itv->t_flags);
		st->dma_info.done 	= 0x00;
		st->dma_req.done 	= 0x00;
	} else {
    		/* Unmap SG Array */
                if (st->SG_handle != IVTV_DMA_UNMAPPED && st->SG_length > 0) {
                        int alignSize = (sizeof(struct ivtv_SG_element) * st->SG_length);

                        pci_unmap_single(itv->dev, st->SG_handle,
                                 alignSize, st->dma);
                        memset(st->SGarray, 0, alignSize);

                        // Clear DMA
                        st->SG_handle = IVTV_DMA_UNMAPPED;
                        st->SG_length = 0;
                } else {
                        IVTV_DEBUG_WARN("[%llu/%u] DMA Req Error stream %d, DMA is not mapped!!!\n",
                                st->SG_handle, st->SG_length, st->type);
                }

		// If failed, put back into queue
        	list_del(&buf->vb.queue);
        	buf->vb.state = STATE_QUEUED;
        	buf->count = 1;
		list_add_tail(&buf->vb.queue, &st->queued);
        	wake_up(&buf->vb.done);
		spin_unlock_irqrestore(&st->slock, flags);

		IVTV_DEBUG_WARN("Error Encoder DMA API returned 0x%08x!!!\n", result);

		goto requeueDMA;
	}
	spin_unlock_irqrestore(&st->slock, flags);
	return retval;

requeueDMA:
	set_bit(IVTV_F_T_ENC_DMA_QUEUED, &itv->t_flags);
    	clear_bit(IVTV_F_S_DMAP, &st->s_flags);
	clear_bit(IVTV_F_S_DMAP, &itv->DMAP);

	retval = 0;
	return retval;
}

