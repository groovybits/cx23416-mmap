Chris Kennedy (C) 2006

Custom cx23416 driver (only works with pvr150/500 cards)
Custom cx23416 firmware (doesn't work on cx23415 or public driver)

This uses v4l2 mmap buffers, DMA for VBI properly in the firmware,
and has buffer overflow fixes and other misc fixes to the firmware.


Notes:

With the Conexant hardwares method of byteswapping the MPEG and VBI,
it needs to be done in the application to capture MPEG2 or VBI data (captures only Raw VBI).

The YUV format needs a fixup, it's in utils/v4lcap.c, and the Y data has extra padding
so you have to do the following calculation...

                ysize = fmt.fmt.pix.width*fmt.fmt.pix.height;
                pageysize = ((ysize+(PAGE_SIZE-1))&PAGE_MASK);
                uvsize = fmt.fmt.pix.width*(fmt.fmt.pix.height/2);
                pageuvsize = ((uvsize+(PAGE_SIZE-1))&PAGE_MASK);
                uvoffset = (pageysize-ysize);
                yoffset = 0;

The Y data is of course the first 'ysize' amount of the mmap buffer, 
then you have to skip ahead to the 'uvoffset' to get the UV data.
It's all in 4096 byte pages, so the UV offset is the Y size rounded
up by the extra remainder of data (720*480)=345600 + 2560 so UV offset
is basically 348160 bytes into the buffer, removing the 2560 bytes of
padding for the last 4096 byte buffer for the Y data before the UV data
starts.


-----

