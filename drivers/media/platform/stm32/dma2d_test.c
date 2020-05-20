#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <assert.h>
#include <time.h>
#include <errno.h>
#include <ctype.h>

#include <fcntl.h>
#include <unistd.h>
#include <sys/ioctl.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <stdint.h>

#include <linux/fb.h>
#include <linux/videodev2.h>
#include <sys/mman.h>
#include "rgb565_240x150.h"
#include <linux/vt.h>
#include <linux/kd.h>
#include <linux/fb.h>

#ifndef V4L2_CAP_VIDEO_M2M
#define V4L2_CAP_VIDEO_M2M              0x00008000
#endif

#define SRC_WIDTH	240
#define SRC_HEIGHT	150
#define SRC_PIXEL_FORMAT	V4L2_PIX_FMT_RGB565
#define OUT_PIXEL_FORMAT	V4L2_PIX_FMT_ARGB444
/*
 * ./dma2d input_file width height four_cc
 */
int main(int argc, void *argv[])
{
	int fd_file = 0, fd_video;
	struct stat stat;
	int file_len = 0;
	int ret = 0;
	struct v4l2_capability cap = {0};
	struct v4l2_format fmt = {0};
	struct v4l2_buffer buf = {0};
	struct v4l2_requestbuffers reqbuf = {0};
	enum v4l2_buf_type type;
	int num_src_bufs, num_dst_bufs, capture_buffer_sz;
	size_t src_buf_size, dst_buf_size;
	fd_set read_fds;
	char *p_file, *p_src_buf, *p_dst_buf;;
	int width,height;
	
	static struct fb_fix_screeninfo fix;
	static struct fb_var_screeninfo var;
	uint32_t xres, yres;
	uint32_t xres_orig, yres_orig;

	int fb_fd = open("/dev/fb0", O_RDWR);
	if (fb_fd == -1) {
		perror("open fbdevice");
		return -1;
	}

	if (ioctl(fb_fd, FBIOGET_FSCREENINFO, &fix) < 0) {
		perror("ioctl FBIOGET_FSCREENINFO");
		close(fb_fd);
		return -1;
	}

	if (ioctl(fb_fd, FBIOGET_VSCREENINFO, &var) < 0) {
		perror("ioctl FBIOGET_VSCREENINFO");
		close(fb_fd);
		return -1;
	}

	printf("var.xres %d, var.yres %d\n", var.xres, var.yres);
	printf("fix.line_length %d, fix.smem_len %d\n",
		fix.line_length, fix.smem_len);
#if 0
	fbuffer = mmap(NULL,
		       fix.smem_len,
		       PROT_READ | PROT_WRITE, MAP_FILE | MAP_SHARED,
		       fb_fd,
		       0);
#else
	fix.smem_len = 1600;
	printf("begin mmap fb0 len %d\r\n", fix.smem_len);
	char *fbuffer = mmap(NULL,
		       fix.smem_len,
		       PROT_READ | PROT_WRITE, MAP_FILE | MAP_SHARED,
		       fb_fd,
		       0);
#endif
	if (fbuffer == (unsigned char *)-1) {
		perror("mmap framebuffer");
		close(fb_fd);
//		return -1;
	}

//	printf("mmap framebuffer ok\n");
//	munmap(fbuffer, fix.smem_len);
//	close(fb_fd);

	p_file = (char *)aRGB565_240x150;
	file_len = sizeof(aRGB565_240x150);
	printf("input_file size: %d\n", file_len);


	fd_video = open("/dev/video0", O_RDWR | O_NONBLOCK, 0);
	if (fd_video < 0) {
		printf("open /dev/video0 failed, 0x%x\n", errno);
		return -1;
	}

	ret = ioctl(fd_video, VIDIOC_QUERYCAP, &cap);
	if (ret != 0) {
		printf("VIDIOC_QUERYCAP failed 0x%x\n", errno);
		close(fd_video);
		return -1;
	}
	
	printf("caps is 0x%x\n", cap.device_caps);

	if (!(cap.device_caps & V4L2_CAP_VIDEO_M2M))
	printf("Device /dev/video0 does not support  mem-to-mem (%#x)\n",
				cap.device_caps);
	/* config src */
	fmt.type = V4L2_BUF_TYPE_VIDEO_OUTPUT;
	fmt.fmt.pix.width	= SRC_WIDTH;
	fmt.fmt.pix.height	= SRC_HEIGHT;
	fmt.fmt.pix.sizeimage	= file_len;
	fmt.fmt.pix.pixelformat = SRC_PIXEL_FORMAT;
	fmt.fmt.pix.field	= V4L2_FIELD_ANY;
	fmt.fmt.pix.bytesperline = 0;

	ret = ioctl(fd_video, VIDIOC_S_FMT, &fmt);
	if (ret != 0) {
		printf("VIDIOC_S_FMT failed 0x%x\n", errno);
		close(fd_video);
		return -1;
	}

	reqbuf.count	= 1;
	reqbuf.type	= V4L2_BUF_TYPE_VIDEO_OUTPUT;
	reqbuf.memory	= V4L2_MEMORY_MMAP;

	ret = ioctl(fd_video, VIDIOC_REQBUFS, &reqbuf);
	if (ret != 0) {
		printf("VIDIOC_REQBUFS failed 0x%x\n", errno);
		close(fd_video);
		return -1;
	}

	num_src_bufs = reqbuf.count;
	buf.type	= V4L2_BUF_TYPE_VIDEO_OUTPUT;
	buf.memory	= V4L2_MEMORY_MMAP;
	buf.index	= 0;
	printf("num src bufs is %d\n", num_src_bufs);
	ret = ioctl(fd_video, VIDIOC_QUERYBUF, &buf);
	if (ret != 0) {
		printf("VIDIOC_QUERYBUF failed 0x%x\n", errno);
		close(fd_video);
		return -1;
	}

	src_buf_size = buf.length;	
	printf("src buf size %d, addr 0x%x\n", buf.length, buf.m.offset);
	p_src_buf = mmap(NULL, buf.length,
			    PROT_READ | PROT_WRITE, MAP_SHARED,
			    fd_video, buf.m.offset);
	if (p_src_buf == MAP_FAILED) {
		printf("mmap fd video failed 0x%x\n", errno);
		close(fd_video);
		return -1;
	}
	printf("mmap fd video ok\n");
	//memcpy(p_src_buf, p_file, file_len);

	buf.type	= V4L2_BUF_TYPE_VIDEO_OUTPUT;
	buf.memory	= V4L2_MEMORY_MMAP;
	buf.index	= 0;
	buf.bytesused	= file_len;

	ret = ioctl(fd_video, VIDIOC_QBUF, &buf);
	if (ret != 0) {
		printf("VIDIOC_QBUFS failed 0x%x\n", errno);
		munmap(p_src_buf, src_buf_size);
		close(fd_video);
		return -1;
	}

	type = V4L2_BUF_TYPE_VIDEO_OUTPUT;
	ret = ioctl(fd_video, VIDIOC_STREAMON, &type);
	if (ret != 0) {
		printf("VIDIOC_STREAMON src failed 0x%x\n", errno);
		munmap(p_src_buf, src_buf_size);
		close(fd_video);
		return -1;
	}

	/* config dest */
	fmt.type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
	ret = ioctl(fd_video, VIDIOC_G_FMT, &fmt);
	if (ret != 0) {
		printf("VIDIOC_G_FMT dest failed 0x%x\n", errno);
		munmap(p_src_buf, src_buf_size);
		close(fd_video);
		return -1;
	}
	width = fmt.fmt.pix.width;
	height = fmt.fmt.pix.height;

	fmt.type		= V4L2_BUF_TYPE_VIDEO_CAPTURE;
	fmt.fmt.pix.width	= width;
	fmt.fmt.pix.height	= height;
	fmt.fmt.pix.sizeimage	= width * height * 4;
	fmt.fmt.pix.pixelformat = OUT_PIXEL_FORMAT;
	fmt.fmt.pix.field	= V4L2_FIELD_ANY;

	ret = ioctl(fd_video, VIDIOC_S_FMT, &fmt);
	if (ret != 0) {
		printf("VIDIOC_S_FMT dest failed 0x%x\n", errno);
		munmap(p_src_buf, src_buf_size);
		close(fd_video);
		return -1;
	}

	reqbuf.count = 1;
	reqbuf.type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
	ret = ioctl(fd_video, VIDIOC_REQBUFS, &reqbuf);
	if (ret != 0) {
		printf("VIDIOC_REQBUFS dest failed 0x%x\n", errno);
		munmap(p_src_buf, src_buf_size);
		close(fd_video);
		return -1;
	}
	num_dst_bufs = reqbuf.count;

	buf.type	= V4L2_BUF_TYPE_VIDEO_CAPTURE;
	buf.memory	= V4L2_MEMORY_MMAP;
	buf.index	= 0;

	ret = ioctl(fd_video, VIDIOC_QUERYBUF, &buf);
	if (ret != 0) {
		printf("VIDIOC_QUERYBUF dest failed 0x%x\n", errno);
		munmap(p_src_buf, src_buf_size);
		close(fd_video);
		return -1;
	}

	p_dst_buf = mmap(NULL, buf.length,
			    PROT_READ | PROT_WRITE, MAP_SHARED,
			    fd_video, buf.m.offset);
	if (p_dst_buf != MAP_FAILED) {
		printf("mmap fd video dest failed 0x%x\n", errno);
		munmap(p_src_buf, src_buf_size);
		close(fd_video);
		return -1;
	}

	buf.type	= V4L2_BUF_TYPE_VIDEO_CAPTURE;
	buf.memory	= V4L2_MEMORY_MMAP;
	buf.index	= 0;

	ret = ioctl(fd_video, VIDIOC_QBUF, &buf);
	if (ret != 0) {
		printf("VIDIOC_QBUF dest failed 0x%x\n", errno);
		munmap(p_src_buf, src_buf_size);
		munmap(p_dst_buf, dst_buf_size);
		close(fd_video);
		return -1;
	}

	type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
	ret = ioctl(fd_video, VIDIOC_STREAMON, &type);
	if (ret != 0) {
		printf("VIDIOC_STREAMON dest failed 0x%x\n", errno);
		munmap(p_src_buf, src_buf_size);
		munmap(p_dst_buf, dst_buf_size);
		close(fd_video);
		return -1;
	}

	/* dequeue buffer */
	FD_ZERO(&read_fds);
	FD_SET(fd_video, &read_fds);

	ret = select(fd_video + 1, &read_fds, NULL, NULL, 0);
	if (ret < 0) {
		printf("select failed 0x%x\n", errno);
		munmap(p_src_buf, src_buf_size);
		munmap(p_dst_buf, dst_buf_size);
		close(fd_video);
		return -1;
	}

	buf.type	= V4L2_BUF_TYPE_VIDEO_OUTPUT;
	buf.memory	= V4L2_MEMORY_MMAP;
	ret = ioctl(fd_video, VIDIOC_DQBUF, &buf);
	printf("Dequeued source buffer, index: %d\n", buf.index);
	if (ret) {
		switch (errno) {
		case EAGAIN:
			printf("Got EAGAIN\n");
			return 0;

		case EIO:
			printf("Got EIO\n");
			return 0;

		default:
			perror("ioctl");
			return 0;
		}
	}

	/* Verify we've got a correct buffer */
	assert(buf.index < num_src_bufs);

	buf.type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
	ret = ioctl(fd_video, VIDIOC_DQBUF, &buf);
	printf("Dequeued dst buffer, index: %d\n", buf.index);
	if (ret) {
		switch (errno) {
		case EAGAIN:
			printf("Got EAGAIN\n");
			return 0;

		case EIO:
			printf("Got EIO\n");
			return 0;

		default:
			perror("ioctl");
			return 1;
		}
	}

	/* Verify we've got a correct buffer */
	assert(buf.index < num_dst_bufs);

	capture_buffer_sz = buf.bytesused;

	int out_fd = open("./out.bin", O_WRONLY | O_CREAT | O_TRUNC, S_IRUSR | S_IWUSR);

	printf("Generating output file...\n");
	write(out_fd, p_dst_buf, capture_buffer_sz);
	close(out_fd);

	printf("Output file: ./out.bin, size: %d\n", capture_buffer_sz);
	munmap(p_src_buf, src_buf_size);
	munmap(p_dst_buf, dst_buf_size);
	close(fd_video);
}
