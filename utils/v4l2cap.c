/*
 *  V4L2 video capture example
 *
 * Chris Kennedy (C) 2006
 *
 *  This program can be used and distributed without restrictions.
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <assert.h>
#include <byteswap.h>

#include <getopt.h>             /* getopt_long() */

#include <fcntl.h>              /* low-level i/o */
#include <unistd.h>
#include <errno.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <sys/time.h>
#include <sys/mman.h>
#include <sys/ioctl.h>
#include <asm/page.h>

#include <asm/types.h>          /* for videodev2.h */

#define __user
#include "videodev2.h"

#define CLEAR(x) memset (&(x), 0, sizeof (x))

static void stop_capturing (void);

typedef enum {
	IO_METHOD_READ,
	IO_METHOD_MMAP,
	IO_METHOD_USERPTR,
} io_method;

struct buffer {
        void *                  start;
        size_t                  length;
};

static char *           dev_name        = NULL;
static io_method	io		= IO_METHOD_MMAP;
static int              fd              = -1;
struct buffer *         buffers         = NULL;
static unsigned int     n_buffers       = 0;

static char *           out_dev_name    = NULL;
static int              fd_out          = -1;
static int 		numbufs		= 8;
static int 		ysize		= 0;
static int		uvsize		= 0;
static int		uvoffset	= 0;
static int		yoffset	= 0;
static int		bs		= 0;
static int		hm12		= 0;
static int		frames_read	= 0;
static int pageysize;
static int pageuvsize;
static int		nonblocking	= 1;
static int 		height		= 480;
static int		width		= 720;

static unsigned int     count           = (60*30)*5; // 5 minutes

static void
errno_exit                      (const char *           s)
{
        fprintf (stderr, "%s error %d, %s\n",
                 s, errno, strerror (errno));

        exit (EXIT_FAILURE);
}

static int
xioctl                          (int                    fd,
                                 int                    request,
                                 void *                 arg)
{
        int r;

        do r = ioctl (fd, request, arg);
        while (-1 == r && EINTR == errno);

        return r;
}

static void
process_image                   (const void *           p)
{
        fputc ('.', stdout);
        fflush (stdout);
}

static int
read_frame			(void)
{
	struct v4l2_buffer buf;
	unsigned int i;

	switch (io) {
	case IO_METHOD_READ:
    		if (-1 == read (fd, buffers[0].start, buffers[0].length)) {
            		switch (errno) {
            		case EAGAIN:
                    		return 0;

			case EIO:
				/* Could ignore EIO, see spec. */

				/* fall through */

			default:
				errno_exit ("read");
			}
		}

    		process_image (buffers[0].start);
		if (-1 == write (fd_out, (void *)buffers[0].start, buffers[0].length))
                       	errno_exit ("write");

		break;

	case IO_METHOD_MMAP:
		CLEAR (buf);

            	buf.type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
            	buf.memory = V4L2_MEMORY_MMAP;

    		if (-1 == xioctl (fd, VIDIOC_DQBUF, &buf)) {
            		switch (errno) {
            		case EAGAIN:
                    		return 0;

			case EIO:
				/* Could ignore EIO, see spec. */

				/* fall through */
				return 0;

			default:
				errno_exit ("VIDIOC_DQBUF");
			}
		}

                assert (buf.index < n_buffers);

	        process_image (buffers[buf.index].start);

		// Write out Data
		if (hm12) {
			char *src=buffers[buf.index].start;
			char *dst;
			dst = (char*)malloc(ysize+uvsize);
			int dstride = 720;
			int y, j, x, i;
			int total = 0;

			fprintf(stderr, "\nGot buffer with %d bytes of yuv data (actual=%d, expected=%d)", 
				buf.bytesused, buffers[buf.index].length, (ysize+uvoffset+uvsize));

			if (buf.bytesused/*buffers[buf.index].length*/ >= (ysize+uvoffset+uvsize))
			{
#if 0
				if (-1 == write (fd_out, (void *)buffers[buf.index].start, 4))
                        		errno_exit ("write");
			}
#else
				if (-1 == write (fd_out, (void *)buffers[buf.index].start, ysize))
                        		errno_exit ("write");
				if (-1 == write (fd_out, (void *)buffers[buf.index].start+(ysize+uvoffset), uvsize))
                        		errno_exit ("write");
			}
#endif

#if 0
				for(y=0;y<height;y+=16) {
					for (x=0;x<width;x+=16) {
						for(i=0;i<16;i++) {
							if (total < ysize) {
								// Each Y Pixel set per macroblock
								memcpy(dst + x + (y+i)*dstride, src, 16);
                						src+=16;
								total+=16;
							}
						}
					}
				}
				src+=uvoffset;
				char *dstu = dst;
				char *dstv = dst+(width*(height/4));
				for(y=0;y<height;y+=16) {
					for(x=0; x<width; x+=8) {
							// Each UV Pixel set per macroblock
						for(i=0;i<16;i++) {
							if (total < (ysize+uvsize)) {
							 	int idx=x + (y+i)*dstride;
                						dstu[idx+0]=src[0]; dstv[idx+0]=src[1];
                						dstu[idx+1]=src[2]; dstv[idx+1]=src[3];
                						dstu[idx+2]=src[4]; dstv[idx+2]=src[5];
                						dstu[idx+3]=src[6]; dstv[idx+3]=src[7];
                						dstu[idx+4]=src[8]; dstv[idx+4]=src[9];
                						dstu[idx+5]=src[10]; dstv[idx+5]=src[11];
                						dstu[idx+6]=src[12]; dstv[idx+6]=src[13];
                						dstu[idx+7]=src[14]; dstv[idx+7]=src[15];
                						src+=16;
								total+=16;
							}
						}
					}
				}
			}
			free(dst);
#endif
#if 0
		} else if (buf.bytesused != 0x1200 /*Skip PCM*/) {
			fprintf(stderr, "\nGot buffer with %d bytes of data (max=%d)", buf.bytesused, buffers[buf.index].length);
			if (buf.bytesused <= buffers[buf.index].length && 
				buffers[buf.index].length > 0)
			{
				if (-1 == write (fd_out,
                        		(void *)buffers[buf.index].start, buf.bytesused/*buffers[buf.index].length*/))
                		{
                        		errno_exit ("write");
                		}
			}
#endif
		} else {
			fprintf(stderr, 
				"\nGot buffer with %d bytes of data (max=%d)", buf.bytesused, buffers[buf.index].length);
			if (-1 == write (fd_out, (void *)buffers[buf.index].start, 4))
                       		errno_exit ("write");
		}

		if (-1 == xioctl (fd, VIDIOC_QBUF, &buf)) {
			errno_exit ("VIDIOC_QBUF");
			}

		break;

	case IO_METHOD_USERPTR:
		CLEAR (buf);

    		buf.type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
    		buf.memory = V4L2_MEMORY_USERPTR;

		if (-1 == xioctl (fd, VIDIOC_DQBUF, &buf)) {
			switch (errno) {
			case EAGAIN:
				return 0;

			case EIO:
				/* Could ignore EIO, see spec. */

				/* fall through */

			default: 
				errno_exit ("VIDIOC_DQBUF");
			}
		}

		for (i = 0; i < n_buffers; ++i)
			if (buf.m.userptr == (unsigned long) buffers[i].start
			    && buf.length == buffers[i].length)
				break;

		if (i > n_buffers) {
			errno_exit ("VIDIOC_QBUF");
		}

    		process_image ((void *) buf.m.userptr);

 		if (-1 == write (fd_out, (void *) buf.m.userptr, buf.bytesused))
                        errno_exit ("write");

		if (-1 == xioctl (fd, VIDIOC_QBUF, &buf)) {
			errno_exit ("VIDIOC_QBUF");
		}

		break;
	}

	return 1;
}

static void
mainloop                        (void)
{
        while (count-- > 0) {
                for (;;) {
                        fd_set fds;
                        struct timeval tv;
                        int r = 0;

                        FD_ZERO (&fds);
                        FD_SET (fd, &fds);

                       	/* Timeout. */
                       	tv.tv_sec = 10;
                       	tv.tv_usec = 0;

			if (nonblocking) {
                        	r = select (fd + 1, &fds, NULL, NULL, &tv);
			}

			if (errno == EAGAIN) {
				continue;
			}

                        if (-1 == r) {
                                if (nonblocking && EINTR == errno) {
					return;
                                        //continue;
				}
  
                                errno_exit ("select");
                        }

                        if (nonblocking && 0 == r) {
                                fprintf (stderr, "select timeout\n");
                                exit (EXIT_FAILURE);
                        }
			if (!nonblocking && r <=0)
				return;

			frames_read++;
			if (read_frame ()) {
				break;
			}
	
			/* EAGAIN - continue select loop. */
                }
        }
}

static void
stop_capturing                  (void)
{
        enum v4l2_buf_type type;

	switch (io) {
	case IO_METHOD_READ:
		/* Nothing to do. */
		break;

	case IO_METHOD_MMAP:
	case IO_METHOD_USERPTR:
		type = V4L2_BUF_TYPE_VIDEO_CAPTURE;

		if (-1 == xioctl (fd, VIDIOC_STREAMOFF, &type))
			errno_exit ("VIDIOC_STREAMOFF");

		break;
	}
        fprintf (stderr, "\nRead %d Frames\n", frames_read);
}

static void
start_capturing                 (void)
{
        unsigned int i;
        enum v4l2_buf_type type;

	switch (io) {
	case IO_METHOD_READ:
		/* Nothing to do. */
		break;

	case IO_METHOD_MMAP:
		for (i = 0; i < n_buffers; ++i) {
            		struct v4l2_buffer buf;

        		CLEAR (buf);

        		buf.type        = V4L2_BUF_TYPE_VIDEO_CAPTURE;
        		buf.memory      = V4L2_MEMORY_MMAP;
        		buf.index       = i;

        		if (-1 == xioctl (fd, VIDIOC_QBUF, &buf))
                    		errno_exit ("VIDIOC_QBUF");
		}
		
		type = V4L2_BUF_TYPE_VIDEO_CAPTURE;

		if (-1 == xioctl (fd, VIDIOC_STREAMON, &type))
			errno_exit ("VIDIOC_STREAMON");

		break;

	case IO_METHOD_USERPTR:
		for (i = 0; i < n_buffers; ++i) {
            		struct v4l2_buffer buf;

        		CLEAR (buf);

        		buf.type        = V4L2_BUF_TYPE_VIDEO_CAPTURE;
        		buf.memory      = V4L2_MEMORY_USERPTR;
        		buf.m.userptr	= (unsigned long) buffers[i].start;
			buf.length      = buffers[i].length;

        		if (-1 == xioctl (fd, VIDIOC_QBUF, &buf))
                    		errno_exit ("VIDIOC_QBUF");
		}


		type = V4L2_BUF_TYPE_VIDEO_CAPTURE;

		if (-1 == xioctl (fd, VIDIOC_STREAMON, &type))
			errno_exit ("VIDIOC_STREAMON");

		break;
	}
}

static void
uninit_device                   (void)
{
        unsigned int i;

	switch (io) {
	case IO_METHOD_READ:
		free (buffers[0].start);
		break;

	case IO_METHOD_MMAP:
		for (i = 0; i < n_buffers; ++i)
			if (-1 == munmap (buffers[i].start, buffers[i].length))
				errno_exit ("munmap");
		break;

	case IO_METHOD_USERPTR:
		for (i = 0; i < n_buffers; ++i)
			free (buffers[i].start);
		break;
	}

	free (buffers);
}

static void
init_read			(unsigned int		buffer_size)
{
        buffers = calloc (1, sizeof (*buffers));

        if (!buffers) {
                fprintf (stderr, "Out of memory\n");
                exit (EXIT_FAILURE);
        }

	buffers[0].length = buffer_size;
	buffers[0].start = malloc (buffer_size);

	if (!buffers[0].start) {
    		fprintf (stderr, "Out of memory\n");
            	exit (EXIT_FAILURE);
	}
}

static void
init_mmap			(void)
{
	struct v4l2_requestbuffers req;

        CLEAR (req);

        req.count               = numbufs;
        req.type                = V4L2_BUF_TYPE_VIDEO_CAPTURE;
        req.memory              = V4L2_MEMORY_MMAP;

	if (-1 == xioctl (fd, VIDIOC_REQBUFS, &req)) {
                if (EINVAL == errno) {
                        fprintf (stderr, "%s does not support "
                                 "memory mapping\n", dev_name);
                        exit (EXIT_FAILURE);
                } else {
                        errno_exit ("VIDIOC_REQBUFS");
                }
        }

        if (req.count < 1) {
                fprintf (stderr, 
			"Insufficient buffer memory on %s for %d buffers, got %d\n",
                         dev_name, 1, req.count);
                exit (EXIT_FAILURE);
        } else
		fprintf (stderr, "Got %d buffers\n", req.count);

        buffers = calloc (req.count, sizeof (*buffers));

        if (!buffers) {
                fprintf (stderr, "Out of memory\n");
                exit (EXIT_FAILURE);
        }

        for (n_buffers = 0; n_buffers < req.count; ++n_buffers) {
                struct v4l2_buffer buf;

                CLEAR (buf);

                buf.type        = V4L2_BUF_TYPE_VIDEO_CAPTURE;
                buf.memory      = V4L2_MEMORY_MMAP;
                buf.index       = n_buffers;

                if (-1 == xioctl (fd, VIDIOC_QUERYBUF, &buf))
                        errno_exit ("VIDIOC_QUERYBUF");

                buffers[n_buffers].length = buf.length;
                buffers[n_buffers].start =
                        mmap (NULL /* start anywhere */,
                              buf.length,
                              PROT_READ | PROT_WRITE /* required */,
                              MAP_SHARED /* recommended */,
                              fd, buf.m.offset);

                if (MAP_FAILED == buffers[n_buffers].start)
                        errno_exit ("mmap");
        }
}

static void
init_userp			(unsigned int		buffer_size)
{
	struct v4l2_requestbuffers req;

        CLEAR (req);

        req.count               = numbufs;
        req.type                = V4L2_BUF_TYPE_VIDEO_CAPTURE;
        req.memory              = V4L2_MEMORY_USERPTR;

        if (-1 == xioctl (fd, VIDIOC_REQBUFS, &req)) {
                if (EINVAL == errno) {
                        fprintf (stderr, "%s does not support "
                                 "user pointer i/o\n", dev_name);
                        exit (EXIT_FAILURE);
                } else {
                        errno_exit ("VIDIOC_REQBUFS");
                }
        }

        buffers = calloc (req.count, sizeof (*buffers));

        if (!buffers) {
                fprintf (stderr, "Out of memory\n");
                exit (EXIT_FAILURE);
        }

        for (n_buffers = 0; n_buffers < req.count; ++n_buffers) {
                buffers[n_buffers].length = buffer_size;
                buffers[n_buffers].start = malloc (buffer_size);

                if (!buffers[n_buffers].start) {
    			fprintf (stderr, "Out of memory\n");
            		exit (EXIT_FAILURE);
		}
        }
}

static void
init_device                     (void)
{
        struct v4l2_capability cap;
        struct v4l2_cropcap cropcap;
        struct v4l2_crop crop;
        struct v4l2_format fmt;
	unsigned int min;

        if (-1 == xioctl (fd, VIDIOC_QUERYCAP, &cap)) {
                if (EINVAL == errno) {
                        fprintf (stderr, "%s is no V4L2 device\n",
                                 dev_name);
                        exit (EXIT_FAILURE);
                } else {
                        errno_exit ("VIDIOC_QUERYCAP");
                }
        }

        if (!(cap.capabilities & V4L2_CAP_VIDEO_CAPTURE)) {
                fprintf (stderr, "%s is no video capture device\n",
                         dev_name);
                exit (EXIT_FAILURE);
        }

	switch (io) {
	case IO_METHOD_READ:
		if (!(cap.capabilities & V4L2_CAP_READWRITE)) {
			fprintf (stderr, "%s does not support read i/o\n",
				 dev_name);
			exit (EXIT_FAILURE);
		}

		break;

	case IO_METHOD_MMAP:
	case IO_METHOD_USERPTR:
		if (!(cap.capabilities & V4L2_CAP_STREAMING)) {
			fprintf (stderr, "%s does not support streaming i/o\n",
				 dev_name);
			exit (EXIT_FAILURE);
		}

		break;
	}


        /* Select video input, video standard and tune here. */


	CLEAR (cropcap);

        cropcap.type = V4L2_BUF_TYPE_VIDEO_CAPTURE;

        if (0 == xioctl (fd, VIDIOC_CROPCAP, &cropcap)) {
                crop.type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
                crop.c = cropcap.defrect; /* reset to default */

                if (-1 == xioctl (fd, VIDIOC_S_CROP, &crop)) {
                        switch (errno) {
                        case EINVAL:
                                /* Cropping not supported. */
                                break;
                        default:
                                /* Errors ignored. */
                                break;
                        }
                }
        } else {	
                /* Errors ignored. */
        }


        CLEAR (fmt);

        fmt.type                = V4L2_BUF_TYPE_VIDEO_CAPTURE;
        fmt.fmt.pix.width       = 720; 
        fmt.fmt.pix.height      = 480;
        fmt.fmt.pix.pixelformat = V4L2_PIX_FMT_NV12;
        fmt.fmt.pix.field       = V4L2_FIELD_INTERLACED;

        if (-1 == xioctl (fd, VIDIOC_S_FMT, &fmt))
                errno_exit ("VIDIOC_S_FMT");

	height = fmt.fmt.pix.height;
	width = fmt.fmt.pix.width;

	if (fmt.fmt.pix.pixelformat == V4L2_PIX_FMT_NV12) {
		bs = 0;
		hm12 = 1;

		ysize = fmt.fmt.pix.width*fmt.fmt.pix.height;
		pageysize = ((ysize+(PAGE_SIZE-1))&PAGE_MASK);
		uvsize = fmt.fmt.pix.width*(fmt.fmt.pix.height/2);
		pageuvsize = ((uvsize+(PAGE_SIZE-1))&PAGE_MASK);
		uvoffset = (pageysize-ysize);
		yoffset = 0;

		//min = pageysize + pageuvsize;
		//fmt.fmt.pix.sizeimage = min;

		fprintf(stderr, "ysize %d uvsize %d yoffset %d uvoffset %d total %d\n",
			ysize, uvsize, yoffset, uvoffset, min);
	} else if (fmt.fmt.pix.pixelformat == V4L2_PIX_FMT_MPEG) {
		bs = 0;
		hm12 = 0;
		//min = 128 * 1024;
		//fmt.fmt.pix.sizeimage = min;
	}

        /* Note VIDIOC_S_FMT may change width and height. */

	/* Buggy driver paranoia. */
	min = fmt.fmt.pix.width * 2;
	if (fmt.fmt.pix.bytesperline < min)
		fmt.fmt.pix.bytesperline = min;

	//min = fmt.fmt.pix.bytesperline * fmt.fmt.pix.height;
	//if (fmt.fmt.pix.sizeimage < min)
	//	fmt.fmt.pix.sizeimage = min;

	fprintf(stderr, "Capture starting with Image size %dx%d of %d bytes total\n",
		fmt.fmt.pix.width, fmt.fmt.pix.height, fmt.fmt.pix.sizeimage);

	switch (io) {
	case IO_METHOD_READ:
		init_read (fmt.fmt.pix.sizeimage);
		break;

	case IO_METHOD_MMAP:
		init_mmap ();
		break;

	case IO_METHOD_USERPTR:
		init_userp (fmt.fmt.pix.sizeimage);
		break;
	}
}

static void
close_device                    (void)
{
        if (-1 == close (fd))
	        errno_exit ("close");

        fd = -1;

 	if (-1 == close (fd_out))
                errno_exit ("close");
	fd_out = -1;
}

static void
open_device                     (void)
{
        struct stat st; 

        if (-1 == stat (dev_name, &st)) {
                fprintf (stderr, "Cannot identify '%s': %d, %s\n",
                         dev_name, errno, strerror (errno));
                exit (EXIT_FAILURE);
        }

        if (!S_ISCHR (st.st_mode)) {
                fprintf (stderr, "%s is no device\n", dev_name);
                exit (EXIT_FAILURE);
        }

	if (nonblocking)
        	fd = open (dev_name, O_RDWR /* required */ | O_NONBLOCK, 0);
	else
        	fd = open (dev_name, O_RDWR /* required */, 0);

        if (-1 == fd) {
                fprintf (stderr, "Cannot open '%s': %d, %s\n",
                         dev_name, errno, strerror (errno));
                exit (EXIT_FAILURE);
        }

	fprintf (stderr, "Writing to '%s'\n", out_dev_name);
        fd_out = open (out_dev_name,
                O_RDWR | O_CREAT| O_APPEND |O_TRUNC, S_IRWXU|S_IRGRP|S_IROTH);

        if (-1 == fd_out) {
                fprintf (stderr, "Cannot open '%s': %d, %s\n",
                         out_dev_name, errno, strerror (errno));
                exit (EXIT_FAILURE);
        }

}

static void
usage                           (FILE *                 fp,
                                 int                    argc,
                                 char **                argv)
{
        fprintf (fp,
                 "Usage: %s [options]\n\n"
                 "Options:\n"
                 "-d | --device  name   Video device name [/dev/video]\n"
                 "-o | --output  name   output file name [/tmp/test.mpg]\n"
                 "-c | --count   number of reads  [r=50,m=1000]\n"
                 "-b | --numbufs number of buffers  [1-32]\n"
                 "-h | --help          Print this message\n"
                 "-m | --mmap          Use memory mapped buffers\n"
                 "-r | --read          Use read() calls\n"
                 "-u | --userp         Use application allocated buffers\n"
                 "",
		 argv[0]);
}

static const char short_options [] = "d:c:o:b:hmru";

static const struct option
long_options [] = {
        { "device",     required_argument,      NULL,           'd' },
 	{ "output",     required_argument,      NULL,           'o' },
	{ "count",      required_argument,      NULL,           'c' },
	{ "numbufs",    required_argument,      NULL,           'b' },
        { "help",       no_argument,            NULL,           'h' },
        { "mmap",       no_argument,            NULL,           'm' },
        { "read",       no_argument,            NULL,           'r' },
        { "userp",      no_argument,            NULL,           'u' },
        { 0, 0, 0, 0 }
};

int
main                            (int                    argc,
                                 char **                argv)
{
        dev_name = "/dev/video";
	out_dev_name = "/tmp/test.yuv";

        for (;;) {
                int index;
                int c;
                
                c = getopt_long (argc, argv,
                                 short_options, long_options,
                                 &index);

                if (-1 == c)
                        break;

                switch (c) {
                case 0: /* getopt_long() flag */
                        break;

                case 'd':
                        dev_name = optarg;
                        break;

		case 'c':
                        count = (int)atoi(optarg);
                        break;

		case 'b':
                        numbufs = (int)atoi(optarg);
                        break;
                case 'o':
                        out_dev_name = optarg;
                        break;

                case 'h':
                        usage (stdout, argc, argv);
                        exit (EXIT_SUCCESS);

                case 'm':
                        io = IO_METHOD_MMAP;
			break;

                case 'r':
                        io = IO_METHOD_READ;
			break;

                case 'u':
                        io = IO_METHOD_USERPTR;
			break;
                default:
                        usage (stderr, argc, argv);
                        exit (EXIT_FAILURE);
                }
        }

        open_device ();

        init_device ();

        start_capturing ();

        mainloop ();

        stop_capturing ();

        uninit_device ();

        close_device ();

	fprintf(stderr, "Stopped Capture Successfully\n");
        exit (EXIT_SUCCESS);
	

        return 0;
}
