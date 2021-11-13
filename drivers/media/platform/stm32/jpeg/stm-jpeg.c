// SPDX-License-Identifier: GPL-2.0
/*
 * V4L2 driver for the JPEG encoder/decoder from stm32f7/h7 application
 * processors.
 *
 * A module parameter is available for debug purpose (jpeg_tracing), to enable
 * it, enable dynamic debug for this module and:
 * echo 1 > /sys/module/mxc_jpeg_encdec/parameters/jpeg_tracing
 *
 * This is inspired by the drivers/media/platform/s5p-jpeg driver
 *
 * Copyright 2018-2019 NXP
 */

#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/io.h>
#include <linux/clk.h>
#include <linux/of_platform.h>
#include <linux/platform_device.h>
#include <linux/slab.h>
#include <linux/irqreturn.h>
#include <linux/interrupt.h>
#include <linux/pm_domain.h>
#include <linux/string.h>

#include <media/v4l2-jpeg.h>
#include <media/v4l2-mem2mem.h>
#include <media/v4l2-ioctl.h>
#include <media/v4l2-common.h>
#include <media/v4l2-event.h>
#include <media/videobuf2-dma-contig.h>

#include "stm-jpeg-hw.h"
#include "stm-jpeg.h"

static void *jpeg_get_drv_data(struct device *dev);

static const struct stm_jpeg_fmt stm_formats[] = {
	{
		.name		= "JPEG",
		.fourcc		= V4L2_PIX_FMT_JPEG,
		.subsampling	= -1,
		.colplanes	= 1,
		.flags		= STM_JPEG_FMT_FLAG_ENC_CAPTURE |
						STM_JPEG_FMT_FLAG_DEC_OUTPUT |
						STM_JPEG_FMT_FLAG_STM32H7 |
						STM_JPEG_FMT_FLAG_STM32F7,
	},
	{
		.name		= "YUV420",
		.fourcc		= V4L2_PIX_FMT_YUV420,
		.subsampling	= V4L2_JPEG_CHROMA_SUBSAMPLING_420,
		.h_align	= 1,
		.v_align	= 1,
		.depth		= 12,
		.colplanes	= 3,
		.flags		= STM_JPEG_FMT_FLAG_ENC_OUTPUT |
						STM_JPEG_FMT_FLAG_DEC_CAPTURE |
						STM_JPEG_FMT_FLAG_STM32H7 |
						STM_JPEG_FMT_FLAG_STM32F7,
	},
	{
		.name		= "YUV422",
		.fourcc		= V4L2_PIX_FMT_YUYV,
		.subsampling	= V4L2_JPEG_CHROMA_SUBSAMPLING_422,
		.h_align	= 2,
		.v_align	= 0,
		.depth		= 16,
		.colplanes	= 1,
		.flags		= STM_JPEG_FMT_FLAG_ENC_OUTPUT |
						STM_JPEG_FMT_FLAG_DEC_CAPTURE |
						STM_JPEG_FMT_FLAG_STM32H7 |
						STM_JPEG_FMT_FLAG_STM32F7,
	},
	{
		.name		= "YUV444",
		.fourcc		= V4L2_PIX_FMT_YUV24,
		.subsampling	= V4L2_JPEG_CHROMA_SUBSAMPLING_444,
		.h_align	= 0,
		.v_align	= 0,
		.depth		= 24,
		.colplanes	= 1,
		.flags		= STM_JPEG_FMT_FLAG_ENC_OUTPUT |
						STM_JPEG_FMT_FLAG_DEC_CAPTURE |
						STM_JPEG_FMT_FLAG_STM32H7 |
						STM_JPEG_FMT_FLAG_STM32F7,
	},
	{
		.name		= "Gray",
		.fourcc		= V4L2_PIX_FMT_GREY,
		.subsampling	= V4L2_JPEG_CHROMA_SUBSAMPLING_GRAY,
		.depth		= 8,
		.colplanes	= 1,
		.flags		= STM_JPEG_FMT_FLAG_ENC_OUTPUT |
						STM_JPEG_FMT_FLAG_DEC_CAPTURE |
						STM_JPEG_FMT_FLAG_STM32H7 |
						STM_JPEG_FMT_FLAG_STM32F7,
	},
};

#define STM_JPEG_NUM_FORMATS ARRAY_SIZE(stm_formats)
static unsigned int debug;
module_param(debug, int, 0644);
MODULE_PARM_DESC(debug, "Debug level (0-3)");

static void print_stm_buf(struct stm_jpeg_dev *jpeg, struct vb2_buffer *buf,
						  unsigned long len)
{
	unsigned int plane_no;
	u32 dma_addr;
	void *vaddr;
	unsigned long payload;

	if (debug < 3)
		return;

	for (plane_no = 0; plane_no < buf->num_planes; plane_no++) {
		payload = vb2_get_plane_payload(buf, plane_no);
		if (len == 0)
			len = payload;

		dma_addr = vb2_dma_contig_plane_dma_addr(buf, plane_no);
		vaddr = vb2_plane_vaddr(buf, plane_no);

		v4l2_dbg(3, debug, &jpeg->v4l2_dev,
			 "plane %d (vaddr=%p dma_addr=%x payload=%ld):",
			  plane_no, vaddr, dma_addr, payload);

		print_hex_dump(KERN_DEBUG, "", DUMP_PREFIX_OFFSET, 32, 1,
			       vaddr, len, false);
	}
}

static inline struct stm_jpeg_ctx *ctrl_to_ctx(struct v4l2_ctrl *c)
{
	return container_of(c->handler, struct stm_jpeg_ctx, ctrl_handler);
}

static inline struct stm_jpeg_ctx *fh_to_ctx(struct v4l2_fh *fh)
{
	return container_of(fh, struct stm_jpeg_ctx, fh);
}

static int stm_jpeg_querycap(struct file *file, void *priv,
							 struct v4l2_capability *cap)
{
	struct stm_jpeg_ctx *ctx = fh_to_ctx(priv);

	if (ctx->mode == STM_JPEG_ENCODE) {
		strscpy(cap->driver, STM_JPEG_NAME,
			sizeof(cap->driver));
		strscpy(cap->card, STM_JPEG_NAME " encoder",
			sizeof(cap->card));
	} else {
		strscpy(cap->driver, STM_JPEG_NAME,
			sizeof(cap->driver));
		strscpy(cap->card, STM_JPEG_NAME " decoder",
			sizeof(cap->card));
	}

	snprintf(cap->bus_info, sizeof(cap->bus_info), "platform:%s",
		 dev_name(ctx->jpeg->dev));

	return 0;
}

static int enum_fmt(struct stm_jpeg_ctx *ctx,
					struct stm_jpeg_fmt *jpeg_formats, int n,
					struct v4l2_fmtdesc *f, u32 type)
{
	int i, num = 0;
	unsigned int fmt_ver_flag = ctx->jpeg->variant->fmt_ver_flag;

	for (i = 0; i < n; ++i) {
		if (jpeg_formats[i].flags & type &&
		    jpeg_formats[i].flags & fmt_ver_flag) {
			/* index-th format of type type found ? */
			if (num == f->index)
				break;
			/* Correct type but haven't reached our index yet,
			 * just increment per-type index
			 */
			++num;
		}
	}

	/* Format not found */
	if (i >= n)
		return -EINVAL;

	f->pixelformat = jpeg_formats[i].fourcc;

	return 0;
}

static int stm_jpeg_enum_fmt_vid_cap(struct file *file, void *priv,
									 struct v4l2_fmtdesc *f)
{
	struct stm_jpeg_ctx *ctx = fh_to_ctx(priv);

	if (ctx->mode == STM_JPEG_ENCODE)
		return enum_fmt(ctx, stm_jpeg_formats, STM_JPEG_NUM_FORMATS, f,
				STM_JPEG_FMT_FLAG_ENC_CAPTURE);

	return enum_fmt(ctx, stm_jpeg_formats, STM_JPEG_NUM_FORMATS, f,
			STM_JPEG_FMT_FLAG_DEC_CAPTURE);
}

static int stm_jpeg_enum_fmt_vid_out(struct file *file, void *priv,
									 struct v4l2_fmtdesc *f)
{
	struct stm_jpeg_ctx *ctx = fh_to_ctx(priv);

	if (ctx->mode == STM_JPEG_ENCODE)
		return enum_fmt(ctx, stm_jpeg_formats, STM_JPEG_NUM_FORMATS, f,
				STM_JPEG_FMT_FLAG_ENC_OUTPUT);

	return enum_fmt(ctx, stm_jpeg_formats, STM_JPEG_NUM_FORMATS, f,
			STM_JPEG_FMT_FLAG_DEC_OUTPUT);
}

static struct stm_jpeg_q_data *get_q_data(struct stm_jpeg_ctx *ctx,
										  enum v4l2_buf_type type)
{
	if (type == V4L2_BUF_TYPE_VIDEO_OUTPUT)
		return &ctx->out_q;

	if (type == V4L2_BUF_TYPE_VIDEO_CAPTURE)
		return &ctx->cap_q;

	return NULL;
}

static int stm_jpeg_g_fmt(struct file *file, void *priv, struct v4l2_format *f)
{
	struct vb2_queue *vq;
	struct stm_jpeg_q_data *q_data = NULL;
	struct v4l2_pix_format *pix = &f->fmt.pix;
	struct stm_jpeg_ctx *ctx = fh_to_ctx(priv);

	vq = v4l2_m2m_get_vq(ctx->fh.m2m_ctx, f->type);
	if (!vq)
		return -EINVAL;

	if (f->type == V4L2_BUF_TYPE_VIDEO_CAPTURE &&
	    ctx->mode == STM_JPEG_DECODE && !ctx->hdr_parsed)
		return -EINVAL;

	q_data = get_q_data(ctx, f->type);
	BUG_ON(q_data == NULL);

	pix->width = q_data->w;
	pix->height = q_data->h;
	pix->field = V4L2_FIELD_NONE;
	pix->pixelformat = q_data->fmt->fourcc;
	pix->bytesperline = 0;

	if (q_data->fmt->fourcc != V4L2_PIX_FMT_JPEG) {
		u32 bpl = q_data->w;

		if (q_data->fmt->colplanes == 1)
			bpl = (bpl * q_data->fmt->depth) >> 3;
		pix->bytesperline = bpl;
	}

	pix->sizeimage = q_data->size;

	return 0;
}

static struct stm_jpeg_fmt *stm_jpeg_find_format(struct stm_jpeg_ctx *ctx,
												 u32 pixelformat,
												 unsigned int fmt_type)
{
	unsigned int k, fmt_flag;

	if (ctx->mode == STM_JPEG_ENCODE)
		fmt_flag = (fmt_type == FMT_TYPE_OUTPUT) ?
					STM_JPEG_FMT_FLAG_ENC_OUTPUT :
					STM_JPEG_FMT_FLAG_ENC_CAPTURE;
	else
		fmt_flag = (fmt_type == FMT_TYPE_OUTPUT) ?
					STM_JPEG_FMT_FLAG_DEC_OUTPUT :
					STM_JPEG_FMT_FLAG_DEC_CAPTURE;

	for (k = 0; k < ARRAY_SIZE(stm_jpeg_formats); k++) {
		struct stm_jpeg_fmt *fmt = &stm_jpeg_formats[k];

		if (fmt->fourcc == pixelformat &&
		    fmt->flags & fmt_flag &&
		    fmt->flags & ctx->jpeg->variant->fmt_ver_flag) {
			return fmt;
		}
	}

	return NULL;
}

static void jpeg_bound_align_image(struct stm_jpeg_ctx *ctx,
								   u32 *w, unsigned int wmin, unsigned int wmax,
								   unsigned int walign,
								   u32 *h, unsigned int hmin, unsigned int hmax,
								   unsigned int halign)
{
	int width, height, w_step, h_step;

	width = *w;
	height = *h;

	w_step = 1 << walign;
	h_step = 1 << halign;

	v4l_bound_align_image(w, wmin, wmax, walign, h, hmin, hmax, halign, 0);

	if (*w < width && (*w + w_step) < wmax)
		*w += w_step;
	if (*h < height && (*h + h_step) < hmax)
		*h += h_step;
}

static int vidioc_try_fmt(struct v4l2_format *f, struct stm_jpeg_fmt *fmt,
						  struct stm_jpeg_ctx *ctx, int q_type)
{
	struct v4l2_pix_format *pix = &f->fmt.pix;

	if (pix->field == V4L2_FIELD_ANY)
		pix->field = V4L2_FIELD_NONE;
	else if (pix->field != V4L2_FIELD_NONE)
		return -EINVAL;

	/* V4L2 specification suggests the driver corrects the format struct
	 * if any of the dimensions is unsupported
	 */
	if (q_type == FMT_TYPE_OUTPUT)
		jpeg_bound_align_image(ctx, &pix->width, STM_JPEG_MIN_WIDTH,
				       STM_JPEG_MAX_WIDTH, 0,
				       &pix->height, STM_JPEG_MIN_HEIGHT,
				       STM_JPEG_MAX_HEIGHT, 0);
	else
		jpeg_bound_align_image(ctx, &pix->width, STM_JPEG_MIN_WIDTH,
				       STM_JPEG_MAX_WIDTH, fmt->h_align,
				       &pix->height, STM_JPEG_MIN_HEIGHT,
				       STM_JPEG_MAX_HEIGHT, fmt->v_align);

	if (fmt->fourcc == V4L2_PIX_FMT_JPEG) {
		if (pix->sizeimage <= 0)
			pix->sizeimage = PAGE_SIZE;
		pix->bytesperline = 0;
	} else {
		u32 bpl = pix->bytesperline;

		if (fmt->colplanes > 1 && bpl < pix->width)
			bpl = pix->width; /* planar */

		if (fmt->colplanes == 1 && /* packed */
		    (bpl << 3) / fmt->depth < pix->width)
			bpl = (pix->width * fmt->depth) >> 3;

		pix->bytesperline = bpl;
		pix->sizeimage = (pix->width * pix->height * fmt->depth) >> 3;
	}

	return 0;
}

static int stm_jpeg_try_fmt_vid_cap(struct file *file, void *priv,
									struct v4l2_format *f)
{
	struct stm_jpeg_ctx *ctx = fh_to_ctx(priv);
	struct v4l2_pix_format *pix = &f->fmt.pix;
	struct stm_jpeg_fmt *fmt;
	int ret;

	fmt = stm_jpeg_find_format(ctx, f->fmt.pix.pixelformat,
						FMT_TYPE_CAPTURE);
	if (!fmt) {
		v4l2_err(&ctx->jpeg->v4l2_dev,
			 "Fourcc format (0x%08x) invalid.\n",
			 f->fmt.pix.pixelformat);
		return -EINVAL;
	}

	return vidioc_try_fmt(f, fmt, ctx, FMT_TYPE_CAPTURE);
}

static int stm_jpeg_try_fmt_vid_out(struct file *file, void *priv,
									struct v4l2_format *f)
{
	struct stm_jpeg_ctx *ctx = fh_to_ctx(priv);
	struct stm_jpeg_fmt *fmt;

	fmt = stm_jpeg_find_format(ctx, f->fmt.pix.pixelformat,
						FMT_TYPE_OUTPUT);
	if (!fmt) {
		v4l2_err(&ctx->jpeg->v4l2_dev,
			 "Fourcc format (0x%08x) invalid.\n",
			 f->fmt.pix.pixelformat);
		return -EINVAL;
	}

	return vidioc_try_fmt(f, fmt, ctx, FMT_TYPE_OUTPUT);
}

static int stm_jpeg_s_fmt(struct stm_jpeg_ctx *ct, struct v4l2_format *f)
{
	struct vb2_queue *vq;
	struct stm_jpeg_q_data *q_data = NULL;
	struct v4l2_pix_format *pix = &f->fmt.pix;
	struct v4l2_ctrl *ctrl_subs;
	struct v4l2_rect scale_rect;
	unsigned int f_type;

	vq = v4l2_m2m_get_vq(ct->fh.m2m_ctx, f->type);
	if (!vq)
		return -EINVAL;

	q_data = get_q_data(ct, f->type);
	BUG_ON(q_data == NULL);

	if (vb2_is_busy(vq)) {
		v4l2_err(&ct->jpeg->v4l2_dev, "%s queue busy\n", __func__);
		return -EBUSY;
	}

	f_type = V4L2_TYPE_IS_OUTPUT(f->type) ? FMT_TYPE_OUTPUT : FMT_TYPE_CAPTURE;

	q_data->fmt = stm_jpeg_find_format(ct, pix->pixelformat, f_type);
	if (ct->mode == STM_JPEG_ENCODE ||
		(ct->mode == STM_JPEG_DECODE &&
		q_data->fmt->fourcc != V4L2_PIX_FMT_JPEG)) {
		q_data->w = pix->width;
		q_data->h = pix->height;
	}

	if (q_data->fmt->fourcc != V4L2_PIX_FMT_JPEG)
		q_data->size = q_data->w * q_data->h * q_data->fmt->depth >> 3;
	else
		q_data->size = pix->sizeimage;

	return 0;
}

static int stm_jpeg_s_fmt_vid_cap(struct file *file, void *priv,
								  struct v4l2_format *f)
{
	int ret;

	ret = stm_jpeg_try_fmt_vid_cap(file, priv, f);
	if (ret)
		return ret;

	return stm_jpeg_s_fmt(fh_to_ctx(priv), f);
}

static int stm_jpeg_s_fmt_vid_out(struct file *file, void *priv,
								  struct v4l2_format *f)
{
	int ret;

	ret = stm_jpeg_try_fmt_vid_out(file, priv, f);
	if (ret)
		return ret;

	return stm_jpeg_s_fmt(fh_to_ctx(priv), f);
}

static int stm_jpeg_subscribe_event(struct v4l2_fh *fh,
									const struct v4l2_event_subscription *sub)
{
	if (sub->type == V4L2_EVENT_SOURCE_CHANGE)
		return v4l2_src_change_event_subscribe(fh, sub);

	return -EINVAL;
}

static int stm_jpeg_encoder_s_ctrl(struct v4l2_ctrl *ctrl)
{
	struct stm_jpeg_ctx *ctx = ctrl_to_ctx(ctrl);
	unsigned long flags;

	spin_lock_irqsave(&ctx->jpeg->slock, flags);

	switch (ctrl->id) {
	case V4L2_CID_JPEG_COMPRESSION_QUALITY:
		ctx->compr_quality = ctrl->val;
		break;
	default:
		break;
	}

	spin_unlock_irqrestore(&ctx->jpeg->slock, flags);
	return 0;
}

static const struct v4l2_ctrl_ops stm_jpeg_encoder_ctrl_ops = {
	.s_ctrl			= stm_jpeg_encoder_s_ctrl,
};

static int stm_jpeg_encoder_controls_create(struct stm_jpeg_ctx *ctx)
{
	struct v4l2_ctrl *ctrl;
	int ret;

	v4l2_ctrl_handler_init(&ctx->ctrl_handler, 1);

	v4l2_ctrl_new_std(&ctx->ctrl_handler, &stm_jpeg_encoder_ctrl_ops,
					  V4L2_CID_JPEG_COMPRESSION_QUALITY,
					  1, 100, 1, STM_JPEG_COMPR_QUAL_WORST);

	if (ctx->ctrl_handler.error) {
		ret = ctx->ctrl_handler.error;
		goto error_free;
	}

	ret = v4l2_ctrl_handler_setup(&ctx->ctrl_handler);
	if (ret < 0)
		goto error_free;

	return ret;

error_free:
	v4l2_ctrl_handler_free(&ctx->ctrl_handler);
	return ret;
}

static const struct v4l2_ioctl_ops stm_jpeg_ioctl_ops = {
	.vidioc_querycap			= stm_jpeg_querycap,

	.vidioc_enum_fmt_vid_cap	= stm_jpeg_enum_fmt_vid_cap,
	.vidioc_enum_fmt_vid_out	= stm_jpeg_enum_fmt_vid_out,

	.vidioc_g_fmt_vid_cap		= stm_jpeg_g_fmt,
	.vidioc_g_fmt_vid_out		= stm_jpeg_g_fmt,

	.vidioc_try_fmt_vid_cap		= stm_jpeg_try_fmt_vid_cap,
	.vidioc_try_fmt_vid_out		= stm_jpeg_try_fmt_vid_out,

	.vidioc_s_fmt_vid_cap		= stm_jpeg_s_fmt_vid_cap,
	.vidioc_s_fmt_vid_out		= stm_jpeg_s_fmt_vid_out,

	.vidioc_reqbufs				= v4l2_m2m_ioctl_reqbufs,
	.vidioc_querybuf			= v4l2_m2m_ioctl_querybuf,
	.vidioc_qbuf				= v4l2_m2m_ioctl_qbuf,
	.vidioc_dqbuf				= v4l2_m2m_ioctl_dqbuf,

	.vidioc_streamon			= v4l2_m2m_ioctl_streamon,
	.vidioc_streamoff			= v4l2_m2m_ioctl_streamoff,

	.vidioc_subscribe_event		= stm_jpeg_subscribe_event,
	.vidioc_unsubscribe_event	= v4l2_event_unsubscribe,
};

static void stm_jpeg_device_run(void *priv)
{
	struct stm_jpeg_ctx *ctx = priv;
	struct stm_jpeg *jpeg = ctx->jpeg;
	struct vb2_v4l2_buffer *src_buf, *dst_buf;
	unsigned long src_addr, dst_addr, flags;

	spin_lock_irqsave(&ctx->jpeg->slock, flags);

	src_buf = v4l2_m2m_next_src_buf(ctx->fh.m2m_ctx);
	dst_buf = v4l2_m2m_next_dst_buf(ctx->fh.m2m_ctx);
	src_addr = vb2_dma_contig_plane_dma_addr(&src_buf->vb2_buf, 0);
	dst_addr = vb2_dma_contig_plane_dma_addr(&dst_buf->vb2_buf, 0);
	stm_jpeg_disable(jpeg->regs);
	stm_jpeg_disable_int(jpeg->regs);
	stm_jpeg_flush_in_fifo(jpeg->regs);
	stm_jpeg_flush_out_fifo(jpeg->regs);
	stm_jpeg_clear_flags(jpeg->regs);
//	s5p_jpeg_reset(jpeg->regs);
//	s5p_jpeg_poweron(jpeg->regs);
//	s5p_jpeg_proc_mode(jpeg->regs, ctx->mode);
	if (ctx->mode == STM_JPEG_ENCODE) {
#if 0
		if (ctx->out_q.fmt->fourcc == V4L2_PIX_FMT_RGB565)
			s5p_jpeg_input_raw_mode(jpeg->regs,
							S5P_JPEG_RAW_IN_565);
		else
			s5p_jpeg_input_raw_mode(jpeg->regs,
							S5P_JPEG_RAW_IN_422);
		s5p_jpeg_subsampling_mode(jpeg->regs, ctx->subsampling);
		s5p_jpeg_dri(jpeg->regs, ctx->restart_interval);
		s5p_jpeg_x(jpeg->regs, ctx->out_q.w);
		s5p_jpeg_y(jpeg->regs, ctx->out_q.h);
		s5p_jpeg_imgadr(jpeg->regs, src_addr);
		s5p_jpeg_jpgadr(jpeg->regs, dst_addr);

		/* ultimately comes from sizeimage from userspace */
		s5p_jpeg_enc_stream_int(jpeg->regs, ctx->cap_q.size);

		/* JPEG RGB to YCbCr conversion matrix */
		s5p_jpeg_coef(jpeg->regs, 1, 1, S5P_JPEG_COEF11);
		s5p_jpeg_coef(jpeg->regs, 1, 2, S5P_JPEG_COEF12);
		s5p_jpeg_coef(jpeg->regs, 1, 3, S5P_JPEG_COEF13);
		s5p_jpeg_coef(jpeg->regs, 2, 1, S5P_JPEG_COEF21);
		s5p_jpeg_coef(jpeg->regs, 2, 2, S5P_JPEG_COEF22);
		s5p_jpeg_coef(jpeg->regs, 2, 3, S5P_JPEG_COEF23);
		s5p_jpeg_coef(jpeg->regs, 3, 1, S5P_JPEG_COEF31);
		s5p_jpeg_coef(jpeg->regs, 3, 2, S5P_JPEG_COEF32);
		s5p_jpeg_coef(jpeg->regs, 3, 3, S5P_JPEG_COEF33);

		/*
		 * JPEG IP allows storing 4 quantization tables
		 * We fill table 0 for luma and table 1 for chroma
		 */
		s5p_jpeg_set_qtbl_lum(jpeg->regs, ctx->compr_quality);
		s5p_jpeg_set_qtbl_chr(jpeg->regs, ctx->compr_quality);
		/* use table 0 for Y */
		s5p_jpeg_qtbl(jpeg->regs, 1, 0);
		/* use table 1 for Cb and Cr*/
		s5p_jpeg_qtbl(jpeg->regs, 2, 1);
		s5p_jpeg_qtbl(jpeg->regs, 3, 1);

		/* Y, Cb, Cr use Huffman table 0 */
		s5p_jpeg_htbl_ac(jpeg->regs, 1);
		s5p_jpeg_htbl_dc(jpeg->regs, 1);
		s5p_jpeg_htbl_ac(jpeg->regs, 2);
		s5p_jpeg_htbl_dc(jpeg->regs, 2);
		s5p_jpeg_htbl_ac(jpeg->regs, 3);
		s5p_jpeg_htbl_dc(jpeg->regs, 3);
#else
#endif
	} else { /* S5P_JPEG_DECODE */
		stm_jpeg_config_decode(jpeg->regs);
#if 0
		s5p_jpeg_rst_int_enable(jpeg->regs, true);
		s5p_jpeg_data_num_int_enable(jpeg->regs, true);
		s5p_jpeg_final_mcu_num_int_enable(jpeg->regs, true);
		if (ctx->cap_q.fmt->fourcc == V4L2_PIX_FMT_YUYV)
			s5p_jpeg_outform_raw(jpeg->regs, S5P_JPEG_RAW_OUT_422);
		else
			s5p_jpeg_outform_raw(jpeg->regs, S5P_JPEG_RAW_OUT_420);
		s5p_jpeg_jpgadr(jpeg->regs, src_addr);
		s5p_jpeg_imgadr(jpeg->regs, dst_addr);
#else
#endif
	}
	stm_jpeg_enable_int(jpeg->regs);
	stm_jpeg_enable(jpeg->regs);
	//s5p_jpeg_start(jpeg->regs);

	spin_unlock_irqrestore(&ctx->jpeg->slock, flags);
}

static int stm_jpeg_job_ready(void *priv)
{
	struct stm_jpeg_ctx *ctx = priv;

	if (ctx->mode == STM_JPEG_DECODE) {
		/*
		 * We have only one input buffer and one output buffer. If there
		 * is a resolution change event, no need to continue decoding.
		 */
		if (ctx->state == JPEGCTX_RESOLUTION_CHANGE)
			return 0;

		return ctx->hdr_parsed;
	}

	return 1;
}

static struct v4l2_m2m_ops stm_jpeg_m2m_ops = {
	.device_run	= stm_jpeg_device_run,
	.job_ready	= stm_jpeg_job_ready,
};

static int stm_jpeg_queue_setup(struct vb2_queue *vq,
								unsigned int *nbuffers,
								unsigned int *nplanes,
								unsigned int sizes[],
								struct device *alloc_devs[])
{
	struct stm_jpeg_ctx *ctx = vb2_get_drv_priv(vq);
	struct stm_jpeg_q_data *q_data = NULL;
	unsigned int size, count = *nbuffers;

	q_data = get_q_data(ctx, vq->type);
	BUG_ON(q_data == NULL);

	size = q_data->size;

	/*
	 * header is parsed during decoding and parsed information stored
	 * in the context so we do not allow another buffer to overwrite it
	 */
	if (ctx->mode == STM_JPEG_DECODE)
		count = 1;

	*nbuffers = count;
	*nplanes = 1;
	sizes[0] = size;

	return 0;
}

static int stm_jpeg_buf_prepare(struct vb2_buffer *vb)
{
	struct stm_jpeg_ctx *ctx = vb2_get_drv_priv(vb->vb2_queue);
	struct stm_jpeg_q_data *q_data = NULL;

	q_data = get_q_data(ctx, vb->vb2_queue->type);
	BUG_ON(q_data == NULL);

	if (vb2_plane_size(vb, 0) < q_data->size) {
		pr_err("%s data will not fit into plane (%lu < %lu)\n",
				__func__, vb2_plane_size(vb, 0),
				(long)q_data->size);
		return -EINVAL;
	}

	vb2_set_plane_payload(vb, 0, q_data->size);

	return 0;
}

static void stm_jpeg_set_capture_queue_data(struct stm_jpeg_ctx *ctx)
{
	struct stm_jpeg_q_data *q_data = &ctx->cap_q;

	q_data->w = ctx->out_q.w;
	q_data->h = ctx->out_q.h;

	/*
	 * This call to jpeg_bound_align_image() takes care of width and
	 * height values alignment when user space calls the QBUF of
	 * OUTPUT buffer after the S_FMT of CAPTURE buffer.
	 * Please note that on Exynos4x12 SoCs, resigning from executing
	 * S_FMT on capture buffer for each JPEG image can result in a
	 * hardware hangup if subsampling is lower than the one of input
	 * JPEG.
	 */
	jpeg_bound_align_image(ctx, &q_data->w, STM_JPEG_MIN_WIDTH,
			       STM_JPEG_MAX_WIDTH, q_data->fmt->h_align,
			       &q_data->h, STM_JPEG_MIN_HEIGHT,
			       STM_JPEG_MAX_HEIGHT, q_data->fmt->v_align);

	q_data->size = q_data->w * q_data->h * q_data->fmt->depth >> 3;
}

static void stm_jpeg_buf_queue(struct vb2_buffer *vb)
{
	struct vb2_v4l2_buffer *vbuf = to_vb2_v4l2_buffer(vb);
	struct stm_jpeg_ctx *ctx = vb2_get_drv_priv(vb->vb2_queue);

	if (ctx->mode == STM_JPEG_DECODE &&
	    vb->vb2_queue->type == V4L2_BUF_TYPE_VIDEO_OUTPUT) {
		static const struct v4l2_event ev_src_ch = {
			.type = V4L2_EVENT_SOURCE_CHANGE,
			.u.src_change.changes = V4L2_EVENT_SRC_CH_RESOLUTION,
		};
		struct vb2_queue *dst_vq;
		u32 ori_w;
		u32 ori_h;

		dst_vq = v4l2_m2m_get_vq(ctx->fh.m2m_ctx,
					 V4L2_BUF_TYPE_VIDEO_CAPTURE);
		ori_w = ctx->out_q.w;
		ori_h = ctx->out_q.h;

		ctx->hdr_parsed = stm_jpeg_parse_hdr(&ctx->out_q,
		     (unsigned long)vb2_plane_vaddr(vb, 0),
		     min((unsigned long)ctx->out_q.size,
			 vb2_get_plane_payload(vb, 0)), ctx);
		if (!ctx->hdr_parsed) {
			vb2_buffer_done(vb, VB2_BUF_STATE_ERROR);
			return;
		}

		/*
		 * If there is a resolution change event, only update capture
		 * queue when it is not streaming. Otherwise, update it in
		 * STREAMOFF. See stm_jpeg_stop_streaming for detail.
		 */
		if (ctx->out_q.w != ori_w || ctx->out_q.h != ori_h) {
			v4l2_event_queue_fh(&ctx->fh, &ev_src_ch);
			if (vb2_is_streaming(dst_vq))
				ctx->state = JPEGCTX_RESOLUTION_CHANGE;
			else
				stm_jpeg_set_capture_queue_data(ctx);
		}
	}

	v4l2_m2m_buf_queue(ctx->fh.m2m_ctx, vbuf);
}

static int stm_jpeg_start_streaming(struct vb2_queue *q, unsigned int count)
{
	struct stm_jpeg_ctx *ctx = vb2_get_drv_priv(q);

	return pm_runtime_resume_and_get(ctx->jpeg->dev);
}

static void stm_jpeg_stop_streaming(struct vb2_queue *q)
{
	struct stm_jpeg_ctx *ctx = vb2_get_drv_priv(q);

	/*
	 * STREAMOFF is an acknowledgment for resolution change event.
	 * Before STREAMOFF, we still have to return the old resolution and
	 * subsampling. Update capture queue when the stream is off.
	 */
	if (ctx->state == JPEGCTX_RESOLUTION_CHANGE &&
	    q->type == V4L2_BUF_TYPE_VIDEO_CAPTURE) {
		stm_jpeg_set_capture_queue_data(ctx);
		ctx->state = JPEGCTX_RUNNING;
	}

	pm_runtime_put(ctx->jpeg->dev);
}

static const struct vb2_ops stm_jpeg_qops = {
	.queue_setup		= stm_jpeg_queue_setup,
	.buf_prepare		= stm_jpeg_buf_prepare,
	.buf_queue		= stm_jpeg_buf_queue,
	.wait_prepare		= vb2_ops_wait_prepare,
	.wait_finish		= vb2_ops_wait_finish,
	.start_streaming	= stm_jpeg_start_streaming,
	.stop_streaming		= stm_jpeg_stop_streaming,
};

static int queue_init(void *priv, struct vb2_queue *src_vq,
					  struct vb2_queue *dst_vq)
{
	struct stm_jpeg_ctx *ctx = priv;
	int ret;

	src_vq->type = V4L2_BUF_TYPE_VIDEO_OUTPUT;
	src_vq->io_modes = VB2_MMAP | VB2_USERPTR;
	src_vq->drv_priv = ctx;
	src_vq->buf_struct_size = sizeof(struct v4l2_m2m_buffer);
	src_vq->ops = &stm_jpeg_qops;
	src_vq->mem_ops = &vb2_dma_contig_memops;
	src_vq->timestamp_flags = V4L2_BUF_FLAG_TIMESTAMP_COPY;
	src_vq->lock = &ctx->jpeg->lock;
	src_vq->dev = ctx->jpeg->dev;

	ret = vb2_queue_init(src_vq);
	if (ret)
		return ret;

	dst_vq->type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
	dst_vq->io_modes = VB2_MMAP | VB2_USERPTR;
	dst_vq->drv_priv = ctx;
	dst_vq->buf_struct_size = sizeof(struct v4l2_m2m_buffer);
	dst_vq->ops = &stm_jpeg_qops;
	dst_vq->mem_ops = &vb2_dma_contig_memops;
	dst_vq->timestamp_flags = V4L2_BUF_FLAG_TIMESTAMP_COPY;
	dst_vq->lock = &ctx->jpeg->lock;
	dst_vq->dev = ctx->jpeg->dev;

	return vb2_queue_init(dst_vq);
}

static irqreturn_t stm_jpeg_irq(int irq, void *dev_id)
{
	struct stm_jpeg *jpeg = dev_id;
	struct stm_jpeg_ctx *curr_ctx;
	struct vb2_v4l2_buffer *src_buf, *dst_buf;
	unsigned long payload_size = 0;
	enum vb2_buffer_state state = VB2_BUF_STATE_DONE;
	bool enc_jpeg_too_large = false;
	bool timer_elapsed = false;
	bool op_completed = false;

	spin_lock(&jpeg->slock);

	curr_ctx = v4l2_m2m_get_curr_priv(jpeg->m2m_dev);

	src_buf = v4l2_m2m_src_buf_remove(curr_ctx->fh.m2m_ctx);
	dst_buf = v4l2_m2m_dst_buf_remove(curr_ctx->fh.m2m_ctx);

	if (curr_ctx->mode == STM_JPEG_ENCODE)
		enc_jpeg_too_large = stm_jpeg_enc_stream_stat(jpeg->regs);
	timer_elapsed = stm_jpeg_timer_stat(jpeg->regs);
	op_completed = stm_jpeg_result_stat_ok(jpeg->regs);
	if (curr_ctx->mode == STM_JPEG_DECODE)
		op_completed = op_completed &&
					stm_jpeg_stream_stat_ok(jpeg->regs);

	if (enc_jpeg_too_large) {
		state = VB2_BUF_STATE_ERROR;
		stm_jpeg_clear_enc_stream_stat(jpeg->regs);
	} else if (timer_elapsed) {
		state = VB2_BUF_STATE_ERROR;
		stm_jpeg_clear_timer_stat(jpeg->regs);
	} else if (!op_completed) {
		state = VB2_BUF_STATE_ERROR;
	} else {
		payload_size = stm_jpeg_compressed_size(jpeg->regs);
	}

	dst_buf->timecode = src_buf->timecode;
	dst_buf->vb2_buf.timestamp = src_buf->vb2_buf.timestamp;
	dst_buf->flags &= ~V4L2_BUF_FLAG_TSTAMP_SRC_MASK;
	dst_buf->flags |=
		src_buf->flags & V4L2_BUF_FLAG_TSTAMP_SRC_MASK;

	v4l2_m2m_buf_done(src_buf, state);
	if (curr_ctx->mode == STM_JPEG_ENCODE)
		vb2_set_plane_payload(&dst_buf->vb2_buf, 0, payload_size);
	v4l2_m2m_buf_done(dst_buf, state);

	curr_ctx->subsampling = stm_jpeg_get_subsampling_mode(jpeg->regs);
	spin_unlock(&jpeg->slock);

	stm_jpeg_clear_int(jpeg->regs);

	v4l2_m2m_job_finish(jpeg->m2m_dev, curr_ctx->fh.m2m_ctx);
	return IRQ_HANDLED;
}

static int stm_jpeg_open(struct file *file)
{
	struct stm_jpeg *jpeg = video_drvdata(file);
	struct video_device *vfd = video_devdata(file);
	struct stm_jpeg_ctx *ctx;
	struct stm_jpeg_fmt *out_fmt, *cap_fmt;
	int ret = 0;

	ctx = kzalloc(sizeof(*ctx), GFP_KERNEL);
	if (!ctx)
		return -ENOMEM;

	if (mutex_lock_interruptible(&jpeg->lock)) {
		ret = -ERESTARTSYS;
		goto free;
	}

	v4l2_fh_init(&ctx->fh, vfd);
	/* Use separate control handler per file handle */
	ctx->fh.ctrl_handler = &ctx->ctrl_handler;
	file->private_data = &ctx->fh;
	v4l2_fh_add(&ctx->fh);

	ctx->jpeg = jpeg;
	if (vfd == jpeg->vfd_encoder) {
		ctx->mode = STM_JPEG_ENCODE;
		out_fmt = stm_jpeg_find_format(ctx, V4L2_PIX_FMT_RGB565,
							FMT_TYPE_OUTPUT);
		cap_fmt = stm_jpeg_find_format(ctx, V4L2_PIX_FMT_JPEG,
							FMT_TYPE_CAPTURE);
	} else {
		ctx->mode = STM_JPEG_DECODE;
		out_fmt = stm_jpeg_find_format(ctx, V4L2_PIX_FMT_JPEG,
							FMT_TYPE_OUTPUT);
		cap_fmt = stm_jpeg_find_format(ctx, V4L2_PIX_FMT_YUYV,
							FMT_TYPE_CAPTURE);
	}

	ctx->fh.m2m_ctx = v4l2_m2m_ctx_init(jpeg->m2m_dev, ctx, queue_init);
	if (IS_ERR(ctx->fh.m2m_ctx)) {
		ret = PTR_ERR(ctx->fh.m2m_ctx);
		goto error;
	}

	ctx->out_q.fmt = out_fmt;
	ctx->cap_q.fmt = cap_fmt;

	if (vfd == jpeg->vfd_encoder) {
		ret = stm_jpeg_encoder_controls_create(ctx);
		if (ret < 0)
			goto error;
	}
	mutex_unlock(&jpeg->lock);
	return 0;

error:
	v4l2_fh_del(&ctx->fh);
	v4l2_fh_exit(&ctx->fh);
	mutex_unlock(&jpeg->lock);
free:
	kfree(ctx);
	return ret;
}

static int stm_jpeg_release(struct file *file)
{
	struct stm_jpeg *jpeg = video_drvdata(file);
	struct stm_jpeg_ctx *ctx = fh_to_ctx(file->private_data);

	mutex_lock(&jpeg->lock);
	v4l2_m2m_ctx_release(ctx->fh.m2m_ctx);
	v4l2_ctrl_handler_free(&ctx->ctrl_handler);
	v4l2_fh_del(&ctx->fh);
	v4l2_fh_exit(&ctx->fh);
	kfree(ctx);
	mutex_unlock(&jpeg->lock);

	return 0;
}

static const struct v4l2_file_operations stm_jpeg_fops = {
	.owner		= THIS_MODULE,
	.open		= stm_jpeg_open,
	.release	= stm_jpeg_release,
	.poll		= v4l2_m2m_fop_poll,
	.unlocked_ioctl	= video_ioctl2,
	.mmap		= v4l2_m2m_fop_mmap,
};

static int stm_jpeg_probe(struct platform_device *pdev)
{
	struct stm_jpeg *jpeg;
	int i, ret;

	/* JPEG IP abstraction struct */
	jpeg = devm_kzalloc(&pdev->dev, sizeof(struct stm_jpeg), GFP_KERNEL);
	if (!jpeg)
		return -ENOMEM;

	jpeg->variant = jpeg_get_drv_data(&pdev->dev);
	if (!jpeg->variant)
		return -ENODEV;

	mutex_init(&jpeg->lock);
	spin_lock_init(&jpeg->slock);
	jpeg->dev = &pdev->dev;

	/* memory-mapped registers */
	jpeg->regs = devm_platform_ioremap_resource(pdev, 0);
	if (IS_ERR(jpeg->regs))
		return PTR_ERR(jpeg->regs);

	/* interrupt service routine registration */
	jpeg->irq = ret = platform_get_irq(pdev, 0);
	if (ret < 0) {
		dev_err(&pdev->dev, "cannot find IRQ\n");
		return ret;
	}

	ret = devm_request_irq(&pdev->dev, jpeg->irq, jpeg->variant->jpeg_irq,
				0, dev_name(&pdev->dev), jpeg);
	if (ret) {
		dev_err(&pdev->dev, "cannot claim IRQ %d\n", jpeg->irq);
		return ret;
	}

	/* clocks */
	for (i = 0; i < jpeg->variant->num_clocks; i++) {
		jpeg->clocks[i] = devm_clk_get(&pdev->dev,
					      jpeg->variant->clk_names[i]);
		if (IS_ERR(jpeg->clocks[i])) {
			dev_err(&pdev->dev, "failed to get clock: %s\n",
				jpeg->variant->clk_names[i]);
			return PTR_ERR(jpeg->clocks[i]);
		}
	}

	/* v4l2 device */
	ret = v4l2_device_register(&pdev->dev, &jpeg->v4l2_dev);
	if (ret) {
		dev_err(&pdev->dev, "Failed to register v4l2 device\n");
		return ret;
	}

	/* mem2mem device */
	jpeg->m2m_dev = v4l2_m2m_init(jpeg->variant->m2m_ops);
	if (IS_ERR(jpeg->m2m_dev)) {
		v4l2_err(&jpeg->v4l2_dev, "Failed to init mem2mem device\n");
		ret = PTR_ERR(jpeg->m2m_dev);
		goto device_register_rollback;
	}

	vb2_dma_contig_set_max_seg_size(&pdev->dev, DMA_BIT_MASK(32));
#if 0
	/* JPEG encoder /dev/videoX node */
	jpeg->vfd_encoder = video_device_alloc();
	if (!jpeg->vfd_encoder) {
		v4l2_err(&jpeg->v4l2_dev, "Failed to allocate video device\n");
		ret = -ENOMEM;
		goto m2m_init_rollback;
	}
	snprintf(jpeg->vfd_encoder->name, sizeof(jpeg->vfd_encoder->name),
				"%s-enc", STM_JPEG_NAME);
	jpeg->vfd_encoder->fops		= &stm_jpeg_fops;
	jpeg->vfd_encoder->ioctl_ops	= &stm_jpeg_ioctl_ops;
	jpeg->vfd_encoder->minor	= -1;
	jpeg->vfd_encoder->release	= video_device_release;
	jpeg->vfd_encoder->lock		= &jpeg->lock;
	jpeg->vfd_encoder->v4l2_dev	= &jpeg->v4l2_dev;
	jpeg->vfd_encoder->vfl_dir	= VFL_DIR_M2M;
	jpeg->vfd_encoder->device_caps	= V4L2_CAP_STREAMING | V4L2_CAP_VIDEO_M2M;

	ret = video_register_device(jpeg->vfd_encoder, VFL_TYPE_VIDEO, -1);
	if (ret) {
		v4l2_err(&jpeg->v4l2_dev, "Failed to register video device\n");
		video_device_release(jpeg->vfd_encoder);
		goto m2m_init_rollback;
	}

	video_set_drvdata(jpeg->vfd_encoder, jpeg);
	v4l2_info(&jpeg->v4l2_dev,
		  "encoder device registered as /dev/video%d\n",
		  jpeg->vfd_encoder->num);
#endif
	/* JPEG decoder /dev/videoX node */
	jpeg->vfd_decoder = video_device_alloc();
	if (!jpeg->vfd_decoder) {
		v4l2_err(&jpeg->v4l2_dev, "Failed to allocate video device\n");
		ret = -ENOMEM;
		goto enc_vdev_register_rollback;
	}
	snprintf(jpeg->vfd_decoder->name, sizeof(jpeg->vfd_decoder->name),
				"%s-dec", STM_JPEG_NAME);
	jpeg->vfd_decoder->fops		= &stm_jpeg_fops;
	jpeg->vfd_decoder->ioctl_ops	= &stm_jpeg_ioctl_ops;
	jpeg->vfd_decoder->minor	= -1;
	jpeg->vfd_decoder->release	= video_device_release;
	jpeg->vfd_decoder->lock		= &jpeg->lock;
	jpeg->vfd_decoder->v4l2_dev	= &jpeg->v4l2_dev;
	jpeg->vfd_decoder->vfl_dir	= VFL_DIR_M2M;
	jpeg->vfd_decoder->device_caps	= V4L2_CAP_STREAMING | V4L2_CAP_VIDEO_M2M;

	ret = video_register_device(jpeg->vfd_decoder, VFL_TYPE_VIDEO, -1);
	if (ret) {
		v4l2_err(&jpeg->v4l2_dev, "Failed to register video device\n");
		video_device_release(jpeg->vfd_decoder);
		goto enc_vdev_register_rollback;
	}

	video_set_drvdata(jpeg->vfd_decoder, jpeg);
	v4l2_info(&jpeg->v4l2_dev,
		  "decoder device registered as /dev/video%d\n",
		  jpeg->vfd_decoder->num);

	/* final statements & power management */
	platform_set_drvdata(pdev, jpeg);

	pm_runtime_enable(&pdev->dev);

	v4l2_info(&jpeg->v4l2_dev, "STM32 JPEG codec\n");

	return 0;

enc_vdev_register_rollback:
	video_unregister_device(jpeg->vfd_encoder);

m2m_init_rollback:
	v4l2_m2m_release(jpeg->m2m_dev);

device_register_rollback:
	v4l2_device_unregister(&jpeg->v4l2_dev);

	return ret;
}

static int stm_jpeg_remove(struct platform_device *pdev)
{
	struct stm_jpeg *jpeg = platform_get_drvdata(pdev);
	int i;

	pm_runtime_disable(jpeg->dev);

	video_unregister_device(jpeg->vfd_decoder);
	video_unregister_device(jpeg->vfd_encoder);
	vb2_dma_contig_clear_max_seg_size(&pdev->dev);
	v4l2_m2m_release(jpeg->m2m_dev);
	v4l2_device_unregister(&jpeg->v4l2_dev);

	if (!pm_runtime_status_suspended(&pdev->dev)) {
		for (i = jpeg->variant->num_clocks - 1; i >= 0; i--)
			clk_disable_unprepare(jpeg->clocks[i]);
	}

	return 0;
}

#ifdef CONFIG_PM
static int stm_jpeg_runtime_suspend(struct device *dev)
{
	struct stm_jpeg *jpeg = dev_get_drvdata(dev);
	int i;

	for (i = jpeg->variant->num_clocks - 1; i >= 0; i--)
		clk_disable_unprepare(jpeg->clocks[i]);

	return 0;
}

static int stm_jpeg_runtime_resume(struct device *dev)
{
	struct stm_jpeg *jpeg = dev_get_drvdata(dev);
	unsigned long flags;
	int i, ret;

	for (i = 0; i < jpeg->variant->num_clocks; i++) {
		ret = clk_prepare_enable(jpeg->clocks[i]);
		if (ret) {
			while (--i >= 0)
				clk_disable_unprepare(jpeg->clocks[i]);
			return ret;
		}
	}

	return 0;
}
#endif /* CONFIG_PM */

static const struct dev_pm_ops stm_jpeg_pm_ops = {
	SET_SYSTEM_SLEEP_PM_OPS(pm_runtime_force_suspend,
				pm_runtime_force_resume)
	SET_RUNTIME_PM_OPS(stm_jpeg_runtime_suspend, stm_jpeg_runtime_resume,
			   NULL)
};

static struct stm_jpeg_variant stm32f7_jpeg_drvdata = {
	.version	= STM_JPEG_F7,
	.jpeg_irq	= stm_jpeg_irq,
	.m2m_ops	= &stm_jpeg_m2m_ops,
	.fmt_ver_flag	= STM_JPEG_FMT_FLAG_STM32F7,
	.clk_names	= {"jpgdec"},
	.num_clocks	= 1,
};

static struct stm_jpeg_variant stm32h7_jpeg_drvdata = {
	.version	= STM_JPEG_H7,
	.jpeg_irq	= stm_jpeg_irq,
	.m2m_ops	= &stm_jpeg_m2m_ops,
	.fmt_ver_flag	= STM_JPEG_FMT_FLAG_STM32H7,
	.clk_names	= {"jpgdec"},
	.num_clocks	= 1,
};

static const struct of_device_id stm_jpeg_match[] = {
	{
		.compatible = "st,stm32f7-jpeg",
		.data = &f7_jpeg_drvdata,
	}, {
		.compatible = "st,stm32h7-jpeg",
		.data = &h7_jpeg_drvdata,
	},
	{ },
};

MODULE_DEVICE_TABLE(of, stm_jpeg_match);

static void *jpeg_get_drv_data(struct device *dev)
{
	struct stm_jpeg_variant *driver_data = NULL;
	const struct of_device_id *match;

	if (!IS_ENABLED(CONFIG_OF) || !dev->of_node)
		return &stm32f7_jpeg_drvdata;

	match = of_match_node(stm_jpeg_match, dev->of_node);

	if (match)
		driver_data = (struct stm_jpeg_variant *)match->data;

	return driver_data;
}

static struct platform_driver mxc_jpeg_driver = {
	.probe = stm_jpeg_probe,
	.remove = stm_jpeg_remove,
	.driver = {
		.name = STM_JPEG_NAME,
		.of_match_table = stm_jpeg_match,
	},
};
module_platform_driver(stm_jpeg_driver);

MODULE_AUTHOR("Dillon Min <dillon.minfei@gmail.com>");
MODULE_DESCRIPTION("V4L2 driver for STM32 F7/H7 JPEG encoder/decoder");
MODULE_LICENSE("GPL v2");
