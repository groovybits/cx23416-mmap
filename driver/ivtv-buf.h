/*
 *
 * generic helper functions for video4linux capture buffers, to handle
 * memory management and PCI DMA.  Right now bttv + saa7134 use it.
 *
 * The functions expect the hardware being able to scatter gatter
 * (i.e. the buffers are not linear in physical memory, but fragmented
 * into PAGE_SIZE chunks).  They also assume the driver does not need
 * to touch the video data (thus it is probably not useful for USB as
 * data often must be uncompressed by the drivers).
 *
 * (c) 2001,02 Gerd Knorr <kraxel@bytesex.org>
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 */

#include <linux/videodev2.h>
#include <linux/poll.h>

#define UNSET (-1U)

#ifndef VIDEO_MAX_FRAME
#define VIDEO_MAX_FRAME 32
#endif

/* --------------------------------------------------------------------- */

/*
 * Return a scatterlist for some page-aligned vmalloc()'ed memory
 * block (NULL on errors).  Memory for the scatterlist is allocated
 * using kmalloc.  The caller must free the memory.
 */
struct scatterlist* ivtvbuf_vmalloc_to_sg(unsigned char *virt, int nr_pages);

/*
 * Return a scatterlist for a an array of userpages (NULL on errors).
 * Memory for the scatterlist is allocated using kmalloc.  The caller
 * must free the memory.
 */
struct scatterlist* ivtvbuf_pages_to_sg(struct page **pages, int nr_pages,
					 int offset);

/* --------------------------------------------------------------------- */

/*
 * A small set of helper functions to manage buffers (both userland
 * and kernel) for DMA.
 *
 * ivtvbuf_dma_init_*()
 *	creates a buffer.  The userland version takes a userspace
 *	pointer + length.  The kernel version just wants the size and
 *	does memory allocation too using vmalloc_32().
 *
 * ivtvbuf_dma_pci_*()
 *	see Documentation/DMA-mapping.txt, these functions to
 *	basically the same.  The map function does also build a
 *	scatterlist for the buffer (and unmap frees it ...)
 *
 * ivtvbuf_dma_free()
 *	no comment ...
 *
 */

struct ivtvbuf_dmabuf {
	u32                 magic;

	/* for userland buffer */
	int                 offset;
	struct page         **pages;

	/* for kernel buffers */
	void                *vmalloc;

	/* for overlay buffers (pci-pci dma) */
	dma_addr_t          bus_addr;

	/* common */
	struct scatterlist  *sglist;
	int                 sglen;
	int                 nr_pages;
	int                 direction;
};

void ivtvbuf_dma_init(struct ivtvbuf_dmabuf *dma);
int ivtvbuf_dma_init_user(struct ivtvbuf_dmabuf *dma, int direction,
			   unsigned long data, unsigned long size);
int ivtvbuf_dma_init_kernel(struct ivtvbuf_dmabuf *dma, int direction,
			     int nr_pages);
int ivtvbuf_dma_init_overlay(struct ivtvbuf_dmabuf *dma, int direction,
			      dma_addr_t addr, int nr_pages);
int ivtvbuf_dma_pci_map(struct pci_dev *dev, struct ivtvbuf_dmabuf *dma);
int ivtvbuf_dma_pci_sync(struct pci_dev *dev,
			  struct ivtvbuf_dmabuf *dma);
int ivtvbuf_dma_pci_unmap(struct pci_dev *dev, struct ivtvbuf_dmabuf *dma);
int ivtvbuf_dma_free(struct ivtvbuf_dmabuf *dma);

/* --------------------------------------------------------------------- */

/*
 * A small set of helper functions to manage video4linux buffers.
 *
 * struct ivtvbuf_buffer holds the data structures used by the helper
 * functions, additionally some commonly used fields for v4l buffers
 * (width, height, lists, waitqueue) are in there.  That struct should
 * be used as first element in the drivers buffer struct.
 *
 * about the mmap helpers (ivtvbuf_mmap_*):
 *
 * The mmaper function allows to map any subset of contingous buffers.
 * This includes one mmap() call for all buffers (which the original
 * video4linux API uses) as well as one mmap() for every single buffer
 * (which v4l2 uses).
 *
 * If there is a valid mapping for a buffer, buffer->baddr/bsize holds
 * userspace address + size which can be feeded into the
 * ivtvbuf_dma_init_user function listed above.
 *
 */

struct ivtvbuf_buffer;
struct ivtvbuf_queue;

struct ivtvbuf_mapping {
	unsigned int count;
	unsigned long start;
	unsigned long end;
	struct ivtvbuf_queue *q;
};

enum ivtvbuf_state {
	STATE_NEEDS_INIT = 0,
	STATE_PREPARED   = 1,
	STATE_QUEUED     = 2,
	STATE_ACTIVE     = 3,
	STATE_DONE       = 4,
	STATE_ERROR      = 5,
	STATE_IDLE       = 6,
};

struct ivtvbuf_buffer {
	unsigned int            i;
	u32                     magic;

	/* info about the buffer */
	unsigned int            width;
	unsigned int            height;
	unsigned int            bytesperline; /* use only if != 0 */
	unsigned long           size;
	unsigned int            input;
	enum v4l2_field         field;
	enum ivtvbuf_state     state;
	struct ivtvbuf_dmabuf  dma;
	struct list_head        stream;  /* QBUF/DQBUF list */

	/* for mmap'ed buffers */
	enum v4l2_memory        memory;
	size_t                  boff;    /* buffer offset (mmap + overlay) */
	size_t                  bsize;   /* buffer size */
	unsigned long           baddr;   /* buffer addr (userland ptr!) */
	struct ivtvbuf_mapping *map;

	/* touched by irq handler */
	struct list_head        queue;
	wait_queue_head_t       done;
	unsigned int            field_count;
	struct timeval          ts;
};

struct ivtvbuf_queue_ops {
	int (*buf_setup)(struct ivtvbuf_queue *q,
			 unsigned int *count, unsigned int *size);
	int (*buf_prepare)(struct ivtvbuf_queue *q,
			   struct ivtvbuf_buffer *vb,
			   enum v4l2_field field);
	void (*buf_queue)(struct ivtvbuf_queue *q,
			  struct ivtvbuf_buffer *vb);
	void (*buf_release)(struct ivtvbuf_queue *q,
			    struct ivtvbuf_buffer *vb);
};

struct ivtvbuf_queue {
	struct semaphore           lock;
	spinlock_t                 *irqlock;
	struct pci_dev             *pci;

	enum v4l2_buf_type         type;
	unsigned int               inputs; /* for V4L2_BUF_FLAG_INPUT */
	unsigned int               msize;
	enum v4l2_field            field;
	enum v4l2_field            last;   /* for field=V4L2_FIELD_ALTERNATE */
	struct ivtvbuf_buffer     *bufs[VIDEO_MAX_FRAME];
	struct ivtvbuf_queue_ops  *ops;

	/* capture via mmap() + ioctl(QBUF/DQBUF) */
	unsigned int               streaming;
	struct list_head           stream;

	/* capture via read() */
	unsigned int               reading;
	unsigned int               read_off;
	struct ivtvbuf_buffer     *read_buf;

	/* driver private data */
	void                       *priv_data;
};

void* ivtvbuf_alloc(unsigned int size);
int ivtvbuf_waiton(struct ivtvbuf_buffer *vb, int non_blocking, int intr);
int ivtvbuf_iolock(struct pci_dev *pci, struct ivtvbuf_buffer *vb,
		    struct v4l2_framebuffer *fbuf);

void ivtvbuf_queue_init(struct ivtvbuf_queue *q,
			 struct ivtvbuf_queue_ops *ops,
			 struct pci_dev *pci,
			 spinlock_t *irqlock,
			 enum v4l2_buf_type type,
			 enum v4l2_field field,
			 unsigned int msize,
			 void *priv);
int  ivtvbuf_queue_is_busy(struct ivtvbuf_queue *q);
void ivtvbuf_queue_cancel(struct ivtvbuf_queue *q);

enum v4l2_field ivtvbuf_next_field(struct ivtvbuf_queue *q);
void ivtvbuf_status(struct v4l2_buffer *b, struct ivtvbuf_buffer *vb,
		     enum v4l2_buf_type type);
int ivtvbuf_reqbufs(struct ivtvbuf_queue *q,
		     struct v4l2_requestbuffers *req);
int ivtvbuf_querybuf(struct ivtvbuf_queue *q, struct v4l2_buffer *b);
int ivtvbuf_qbuf(struct ivtvbuf_queue *q,
		  struct v4l2_buffer *b);
int ivtvbuf_dqbuf(struct ivtvbuf_queue *q,
		   struct v4l2_buffer *b, int nonblocking);
int ivtvbuf_streamon(struct ivtvbuf_queue *q);
int ivtvbuf_streamoff(struct ivtvbuf_queue *q);

int ivtvbuf_read_start(struct ivtvbuf_queue *q);
void ivtvbuf_read_stop(struct ivtvbuf_queue *q);
ssize_t ivtvbuf_read_stream(struct ivtvbuf_queue *q,
			     char __user *data, size_t count, loff_t *ppos,
			     int vbihack, int nonblocking);
ssize_t ivtvbuf_read_one(struct ivtvbuf_queue *q,
			  char __user *data, size_t count, loff_t *ppos,
			  int nonblocking);
unsigned int ivtvbuf_poll_stream(struct file *file,
				  struct ivtvbuf_queue *q,
				  poll_table *wait);

int ivtvbuf_mmap_setup(struct ivtvbuf_queue *q,
			unsigned int bcount, unsigned int bsize,
			enum v4l2_memory memory);
int ivtvbuf_mmap_free(struct ivtvbuf_queue *q);
int ivtvbuf_mmap_mapper(struct ivtvbuf_queue *q,
			 struct vm_area_struct *vma);

/* --------------------------------------------------------------------- */

/*
 * Local variables:
 * c-basic-offset: 8
 * End:
 */
