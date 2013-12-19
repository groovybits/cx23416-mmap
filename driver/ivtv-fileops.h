/*
    file operation functions
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

/* Testing/Debugging */
int ivtv_v4l2_open(struct inode *inode, struct file *filp);
ssize_t ivtv_v4l2_read(struct file *filp, char *buf, size_t count,
		       loff_t * pos);
ssize_t ivtv_v4l2_write(struct file *filp, const char *buf, size_t count,
			loff_t * pos);
int ivtv_v4l2_release(struct ivtv *itv, struct ivtv_stream *st);
int ivtv_v4l2_close(struct inode *inode, struct file *filp);
unsigned int ivtv_v4l2_enc_poll(struct file *filp, poll_table * wait);
unsigned int ivtv_v4l2_dec_poll(struct file *filp, poll_table * wait);
void ivtv_mute(struct ivtv *itv);
void ivtv_unmute(struct ivtv *itv);

/* Utilities */

/* Try to claim a stream for the filehandle. Return 0 on success,
   -EBUSY if stream already claimed. Once a stream is claimed, it
   remains claimed until the associated filehandle is closed. */
int ivtv_claim_stream(struct ivtv_open_id *id, int type);

/* Release a previously claimed stream. */
void ivtv_release_stream(struct ivtv *itv, int type);

int ivtv_mmap(struct file *flip, struct vm_area_struct *vma);

void ivtv_timeout(unsigned long data);

int buffer_setup(struct ivtvbuf_queue *q, unsigned int *count, unsigned int *size);
int buffer_prepare(struct ivtvbuf_queue *q,
        struct ivtvbuf_buffer *vb, enum v4l2_field field);
void buffer_release(struct ivtvbuf_queue *vq,
        struct ivtvbuf_buffer *vb);
void buffer_queue(struct ivtvbuf_queue *vq,
        struct ivtvbuf_buffer *vb);
