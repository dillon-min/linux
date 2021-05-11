// SPDX-License-Identifier: GPL-2.0-or-later
/*
 * STM32 DMA2D - 2D Graphics Accelerator Driver
 *
 * Copyright (c) 2021 Dillon Min
 * Dillon Min, <dillon.minfei@gmail.com>
 *
 * based on s5p-g2d
 *
 * Copyright (c) 2011 Samsung Electronics Co., Ltd.
 * Kamil Debski, <k.debski@samsung.com>
 */

#include <linux/module.h>
#include <linux/fs.h>
#include <linux/timer.h>
#include <linux/sched.h>
#include <linux/slab.h>
#include <linux/clk.h>
#include <linux/interrupt.h>
#include <linux/of.h>

#include <linux/platform_device.h>
#include <media/v4l2-mem2mem.h>
#include <media/v4l2-device.h>
#include <media/v4l2-ioctl.h>
#include <media/v4l2-event.h>
#include <media/videobuf2-v4l2.h>
#include <media/videobuf2-dma-contig.h>

#include "dma2d.h"
#include "dma2d-regs.h"

#define fh2ctx(__fh) container_of(__fh, struct dma2d_ctx, fh)

static struct dma2d_fmt formats[] = {
	{
		.fourcc	= V4L2_PIX_FMT_ARGB32,
		.cmode = DMA2D_CMODE_ARGB8888,
		.depth = 32,
	},
	{
		.fourcc	= V4L2_PIX_FMT_RGB24,
		.cmode = DMA2D_CMODE_RGB888,
		.depth = 24,
	},
	{
		.fourcc	= V4L2_PIX_FMT_RGB565,
		.cmode = DMA2D_CMODE_RGB565,
		.depth = 16,
	},
	{
		.fourcc	= V4L2_PIX_FMT_ARGB555,
		.cmode = DMA2D_CMODE_ARGB1555,
		.depth = 16,
	},
	{
		.fourcc	= V4L2_PIX_FMT_ARGB444,
		.cmode = DMA2D_CMODE_ARGB4444,
		.depth = 16,
	},
	/*
	 * TODO: Add support for A4, A8, L4, AL88, AL44, L8
	 */
};
#define NUM_FORMATS ARRAY_SIZE(formats)

static struct dma2d_frame def_frame = {
	.width		= DEFAULT_WIDTH,
	.height		= DEFAULT_HEIGHT,
	.line_ofs	= 0,
	.a_rgb		= {0x00, 0x00, 0x00, 0xff},
	.a_mode		= DMA2D_ALPHA_MODE_NO_MODIF,
	.fmt		= &formats[0],
	.size		= DEFAULT_SIZE,
};

static struct dma2d_fmt *find_fmt(int pixelformat)
{
	unsigned int i;

	for (i = 0; i < NUM_FORMATS; i++) {
		if (formats[i].fourcc == pixelformat)
			return &formats[i];
	}

	return NULL;
}

static struct dma2d_frame *get_frame(struct dma2d_ctx *ctx,
				   enum v4l2_buf_type type)
{
	switch (type) {
	case V4L2_BUF_TYPE_VIDEO_OUTPUT:
		return &ctx->fg;
	case V4L2_BUF_TYPE_VIDEO_CAPTURE:
		return &ctx->out;
	default:
		return ERR_PTR(-EINVAL);
	}
}

static int dma2d_queue_setup(struct vb2_queue *vq,
			   unsigned int *nbuffers, unsigned int *nplanes,
			   unsigned int sizes[], struct device *alloc_devs[])
{
	struct dma2d_ctx *ctx = vb2_get_drv_priv(vq);
	struct dma2d_frame *f = get_frame(ctx, vq->type);

	if (IS_ERR(f))
		return PTR_ERR(f);

	sizes[0] = f->size;
	*nplanes = 1;

	if (*nbuffers == 0)
		*nbuffers = 1;

	return 0;
}

static int dma2d_buf_prepare(struct vb2_buffer *vb)
{
	struct dma2d_ctx *ctx = vb2_get_drv_priv(vb->vb2_queue);
	struct dma2d_frame *f = get_frame(ctx, vb->vb2_queue->type);

	if (IS_ERR(f))
		return PTR_ERR(f);
	
	vb2_set_plane_payload(vb, 0, f->size);

	return 0;
}

static void dma2d_buf_queue(struct vb2_buffer *vb)
{
	struct vb2_v4l2_buffer *vbuf = to_vb2_v4l2_buffer(vb);
	struct dma2d_ctx *ctx = vb2_get_drv_priv(vb->vb2_queue);

	v4l2_m2m_buf_queue(ctx->fh.m2m_ctx, vbuf);
}

static const struct vb2_ops dma2d_qops = {
	.queue_setup	= dma2d_queue_setup,
	.buf_prepare	= dma2d_buf_prepare,
	.buf_queue	= dma2d_buf_queue,
	.wait_prepare	= vb2_ops_wait_prepare,
	.wait_finish	= vb2_ops_wait_finish,
};

static int queue_init(void *priv, struct vb2_queue *src_vq,
						struct vb2_queue *dst_vq)
{
	struct dma2d_ctx *ctx = priv;
	int ret;
	
	src_vq->type = V4L2_BUF_TYPE_VIDEO_OUTPUT;
	src_vq->io_modes = VB2_MMAP | VB2_USERPTR;
	src_vq->drv_priv = ctx;
	src_vq->ops = &dma2d_qops;
	src_vq->mem_ops = &vb2_dma_contig_memops;
	src_vq->buf_struct_size = sizeof(struct v4l2_m2m_buffer);
	src_vq->timestamp_flags = V4L2_BUF_FLAG_TIMESTAMP_COPY;
	src_vq->lock = &ctx->dev->mutex;
	src_vq->dev = ctx->dev->v4l2_dev.dev;

	ret = vb2_queue_init(src_vq);
	if (ret)
		return ret;

	dst_vq->type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
	dst_vq->io_modes = VB2_MMAP | VB2_USERPTR;
	dst_vq->drv_priv = ctx;
	dst_vq->ops = &dma2d_qops;
	dst_vq->mem_ops = &vb2_dma_contig_memops;
	dst_vq->buf_struct_size = sizeof(struct v4l2_m2m_buffer);
	dst_vq->timestamp_flags = V4L2_BUF_FLAG_TIMESTAMP_COPY;
	dst_vq->lock = &ctx->dev->mutex;
	dst_vq->dev = ctx->dev->v4l2_dev.dev;

	return vb2_queue_init(dst_vq);
}

#define V4L2_CID_DMA2D_R2M_COLOR (V4L2_CID_USER_BASE | 0x1200)
#define V4L2_CID_DMA2D_R2M_MODE (V4L2_CID_USER_BASE | 0x1201)
static int dma2d_s_ctrl(struct v4l2_ctrl *ctrl)
{
	struct dma2d_frame *frm;
	struct dma2d_ctx *ctx = container_of(ctrl->handler, struct dma2d_ctx,
								ctrl_handler);
	unsigned long flags;

	spin_lock_irqsave(&ctx->dev->ctrl_lock, flags);
	switch (ctrl->id) {
	case V4L2_CID_ALPHA_COMPONENT:
		/* set the background alpha value*/
		ctx->alpha_component = (u8) ctrl->val;
		printk("%s: set to alpha mode %d\r\n", __func__, ctrl->val);
		break;
	case V4L2_CID_DMA2D_R2M_COLOR:
		printk("%s %d\r\n", __func__, __LINE__);
		frm = get_frame(ctx, V4L2_BUF_TYPE_VIDEO_CAPTURE);
		frm->a_rgb[3] = (ctrl->val >> 24) & 0xff;
		frm->a_rgb[2] = (ctrl->val >> 16) & 0xff;
		frm->a_rgb[1] = (ctrl->val >>  8) & 0xff;
		frm->a_rgb[0] = ctrl->val & 0xff;
		break;
	case V4L2_CID_DMA2D_R2M_MODE:
		if (ctrl->val)
			ctx->op_mode = DMA2D_MODE_R2M;
		printk("%s %d\r\n", __func__, __LINE__);
		break;
	default:
		v4l2_err(&ctx->dev->v4l2_dev, "Invalid control\n");
		spin_unlock_irqrestore(&ctx->dev->ctrl_lock, flags);
		return -EINVAL;

	}
	spin_unlock_irqrestore(&ctx->dev->ctrl_lock, flags);

	return 0;
}

static const struct v4l2_ctrl_ops dma2d_ctrl_ops = {
	.s_ctrl	= dma2d_s_ctrl,
};

static const struct v4l2_ctrl_config dma2d_r2m_control[] = {
	{
	.ops = &dma2d_ctrl_ops,
	.id = V4L2_CID_DMA2D_R2M_COLOR,
	.name = "R2M Alpha/Color Value",
	.type = V4L2_CTRL_TYPE_INTEGER,
	.min = 0xffffffff80000000ULL,
	.max = 0x7fffffff,
	.def = 0,
	.step = 1,
	},
	{
	.ops = &dma2d_ctrl_ops,
	.id = V4L2_CID_DMA2D_R2M_MODE,
	.name = "Set to r2m mode",
	.type = V4L2_CTRL_TYPE_BOOLEAN,
	.min = 0,
	.max = 1,
	.def = 0,
	.step = 1,
	}
};

static int dma2d_setup_ctrls(struct dma2d_ctx *ctx)
{
	v4l2_ctrl_handler_init(&ctx->ctrl_handler, 3);

	v4l2_ctrl_new_std(&ctx->ctrl_handler, &dma2d_ctrl_ops,
			V4L2_CID_ALPHA_COMPONENT, 0, 255, 1, 255);
	v4l2_ctrl_new_custom(&ctx->ctrl_handler, &dma2d_r2m_control[0], NULL);
	v4l2_ctrl_new_custom(&ctx->ctrl_handler, &dma2d_r2m_control[1], NULL);

	return 0;
}

static int dma2d_open(struct file *file)
{
	struct dma2d_dev *dev = video_drvdata(file);
	struct dma2d_ctx *ctx = NULL;
	int ret = 0;

	ctx = kzalloc(sizeof(*ctx), GFP_KERNEL);
	if (!ctx)
		return -ENOMEM;
	ctx->dev = dev;
	/* Set default formats */
	ctx->fg		= def_frame;
	ctx->bg		= def_frame;
	ctx->out	= def_frame;
	ctx->op_mode	= DMA2D_MODE_M2M_FPC; 
	ctx->colorspace = V4L2_COLORSPACE_REC709;
	ctx->alpha_component = 0x00;
	if (mutex_lock_interruptible(&dev->mutex)) {
		kfree(ctx);
		return -ERESTARTSYS;
	}

	ctx->fh.m2m_ctx = v4l2_m2m_ctx_init(dev->m2m_dev, ctx, &queue_init);
	if (IS_ERR(ctx->fh.m2m_ctx)) {
		ret = PTR_ERR(ctx->fh.m2m_ctx);
		mutex_unlock(&dev->mutex);
		kfree(ctx);
		return ret;
	}

	v4l2_fh_init(&ctx->fh, video_devdata(file));
	file->private_data = &ctx->fh;
	v4l2_fh_add(&ctx->fh);

	dma2d_setup_ctrls(ctx);

	/* Write the default values to the ctx struct */
	v4l2_ctrl_handler_setup(&ctx->ctrl_handler);

	ctx->fh.ctrl_handler = &ctx->ctrl_handler;
	mutex_unlock(&dev->mutex);

	return 0;
}

static int dma2d_release(struct file *file)
{
	struct dma2d_dev *dev = video_drvdata(file);
	struct dma2d_ctx *ctx = fh2ctx(file->private_data);

	v4l2_ctrl_handler_free(&ctx->ctrl_handler);
	v4l2_fh_del(&ctx->fh);
	v4l2_fh_exit(&ctx->fh);
	mutex_lock(&dev->mutex);
	v4l2_m2m_ctx_release(ctx->fh.m2m_ctx);
	mutex_unlock(&dev->mutex);
	kfree(ctx);

	return 0;
}

static int vidioc_querycap(struct file *file, void *priv,
				struct v4l2_capability *cap)
{
	strscpy(cap->driver, DMA2D_NAME, sizeof(cap->driver));
	strscpy(cap->card, DMA2D_NAME, sizeof(cap->card));
	strscpy(cap->bus_info, BUS_INFO, sizeof(cap->bus_info));

	return 0;
}

static int vidioc_enum_fmt(struct file *file, void *prv, struct v4l2_fmtdesc *f)
{
	if (f->index >= NUM_FORMATS)
		return -EINVAL;
	f->pixelformat = formats[f->index].fourcc;

	return 0;
}

static int vidioc_g_fmt(struct file *file, void *prv, struct v4l2_format *f)
{
	struct dma2d_ctx *ctx = prv;
	struct vb2_queue *vq;
	struct dma2d_frame *frm;

	vq = v4l2_m2m_get_vq(ctx->fh.m2m_ctx, f->type);
	if (!vq)
		return -EINVAL;
	
	frm = get_frame(ctx, f->type);
	if (IS_ERR(frm))
		return PTR_ERR(frm);

	f->fmt.pix.width		= frm->width;
	f->fmt.pix.height		= frm->height;
	f->fmt.pix.field		= V4L2_FIELD_NONE;
	f->fmt.pix.pixelformat		= frm->fmt->fourcc;
	f->fmt.pix.bytesperline		= (frm->width * frm->fmt->depth) >> 3;
	f->fmt.pix.sizeimage		= frm->size;
	f->fmt.pix.colorspace		= ctx->colorspace;
	f->fmt.pix.xfer_func		= ctx->xfer_func;
	f->fmt.pix.ycbcr_enc		= ctx->ycbcr_enc;
	f->fmt.pix.quantization		= ctx->quant;

	return 0;
}

static int vidioc_try_fmt(struct file *file, void *prv, struct v4l2_format *f)
{
	struct dma2d_ctx *ctx = prv;
	struct dma2d_fmt *fmt;
	enum v4l2_field *field;

	fmt = find_fmt(f->fmt.pix.pixelformat);
	if (!fmt)
		return -EINVAL;

	field = &f->fmt.pix.field;
	if (*field == V4L2_FIELD_ANY)
		*field = V4L2_FIELD_NONE;
	else if (*field != V4L2_FIELD_NONE)
		return -EINVAL;

	if (f->fmt.pix.width > MAX_WIDTH)
		f->fmt.pix.width = MAX_WIDTH;
	if (f->fmt.pix.height > MAX_HEIGHT)
		f->fmt.pix.height = MAX_HEIGHT;

	if (f->fmt.pix.width < 1)
		f->fmt.pix.width = 1;
	if (f->fmt.pix.height < 1)
		f->fmt.pix.height = 1;

	if (f->type == V4L2_BUF_TYPE_VIDEO_OUTPUT && !f->fmt.pix.colorspace)
		f->fmt.pix.colorspace = V4L2_COLORSPACE_REC709;
	else if (f->type == V4L2_BUF_TYPE_VIDEO_CAPTURE) {
		f->fmt.pix.colorspace	= ctx->colorspace;
		f->fmt.pix.xfer_func = ctx->xfer_func;
		f->fmt.pix.ycbcr_enc = ctx->ycbcr_enc;
		f->fmt.pix.quantization = ctx->quant;
	}
	f->fmt.pix.bytesperline = (f->fmt.pix.width * fmt->depth) >> 3;
	f->fmt.pix.sizeimage = f->fmt.pix.height * f->fmt.pix.bytesperline;

	return 0;
}

static int vidioc_s_fmt(struct file *file, void *prv, struct v4l2_format *f)
{
	struct dma2d_ctx *ctx = prv;
	struct dma2d_dev *dev = ctx->dev;
	struct vb2_queue *vq;
	struct dma2d_frame *frm;
	struct dma2d_fmt *fmt;
	int ret = 0;

	/* Adjust all values accordingly to the hardware capabilities
	 * and chosen format. */
	ret = vidioc_try_fmt(file, prv, f);
	if (ret)
		return ret;
	vq = v4l2_m2m_get_vq(ctx->fh.m2m_ctx, f->type);
	if (vb2_is_busy(vq)) {
		v4l2_err(&dev->v4l2_dev, "queue (%d) bust\n", f->type);
		return -EBUSY;
	}
	
	frm = get_frame(ctx, f->type);
	if (IS_ERR(frm))
		return PTR_ERR(frm);
	
	fmt = find_fmt(f->fmt.pix.pixelformat);
	if (!fmt)
		return -EINVAL;
	
	if (f->type == V4L2_BUF_TYPE_VIDEO_OUTPUT) {
		ctx->colorspace = f->fmt.pix.colorspace;
		ctx->xfer_func = f->fmt.pix.xfer_func;
		ctx->ycbcr_enc = f->fmt.pix.ycbcr_enc;
		ctx->quant = f->fmt.pix.quantization;
	}

	frm->width	= f->fmt.pix.width;
	frm->height	= f->fmt.pix.height;
	frm->size	= f->fmt.pix.sizeimage;
	/* Reset crop settings */
	frm->o_width	= 0;
	frm->o_height	= 0;
	frm->c_width	= frm->width;
	frm->c_height	= frm->height;
	frm->right	= frm->width;
	frm->bottom	= frm->height;
	frm->fmt	= fmt;
	frm->line_ofs	= 0;
	if (f->fmt.win.global_alpha != 0) {
		frm->a_rgb[3] = f->fmt.win.global_alpha;
		frm->a_mode = DMA2D_ALPHA_MODE_REPLACE;
	}

	return 0;
}

static int vidioc_g_selection(struct file *file, void *prv,
			      struct v4l2_selection *s)
{
	struct dma2d_ctx *ctx = prv;
	struct dma2d_frame *frm;
	
	frm = get_frame(ctx, s->type);
	if (IS_ERR(frm))
		return PTR_ERR(frm);

	switch (s->target) {
	case V4L2_SEL_TGT_CROP:
	case V4L2_SEL_TGT_CROP_DEFAULT:
	case V4L2_SEL_TGT_CROP_BOUNDS:
		if (s->type != V4L2_BUF_TYPE_VIDEO_OUTPUT)
			return -EINVAL;
		break;
	case V4L2_SEL_TGT_COMPOSE:
	case V4L2_SEL_TGT_COMPOSE_DEFAULT:
	case V4L2_SEL_TGT_COMPOSE_BOUNDS:
		if (s->type != V4L2_BUF_TYPE_VIDEO_CAPTURE)
			return -EINVAL;
		break;
	default:
		return -EINVAL;
	}

	switch (s->target) {
	case V4L2_SEL_TGT_CROP:
	case V4L2_SEL_TGT_COMPOSE:
		s->r.left = frm->o_height;
		s->r.top = frm->o_width;
		s->r.width = frm->c_width;
		s->r.height = frm->c_height;
		break;
	case V4L2_SEL_TGT_CROP_DEFAULT:
	case V4L2_SEL_TGT_CROP_BOUNDS:
	case V4L2_SEL_TGT_COMPOSE_DEFAULT:
	case V4L2_SEL_TGT_COMPOSE_BOUNDS:
		s->r.left = 0;
		s->r.top = 0;
		s->r.width = frm->width;
		s->r.height = frm->height;
		break;
	default:
		return -EINVAL;
	}

	return 0;
}

static int vidioc_try_selection(struct file *file, void *prv,
				const struct v4l2_selection *s)
{
	struct dma2d_ctx *ctx = prv;
	struct dma2d_dev *dev = ctx->dev;
	struct dma2d_frame *frm;

	frm = get_frame(ctx, s->type);
	if (IS_ERR(frm))
		return PTR_ERR(frm);

	if (s->type == V4L2_BUF_TYPE_VIDEO_CAPTURE) {
		if (s->target != V4L2_SEL_TGT_COMPOSE)
			return -EINVAL;
	} else if (s->type == V4L2_BUF_TYPE_VIDEO_OUTPUT) {
		if (s->target != V4L2_SEL_TGT_CROP)
			return -EINVAL;
	}

	if (s->r.top < 0 || s->r.left < 0) {
		v4l2_err(&dev->v4l2_dev,
			"doesn't support negative values for top & left\n");
		return -EINVAL;
	}

	return 0;
}

static int vidioc_s_selection(struct file *file, void *prv,
			      struct v4l2_selection *s)
{
	struct dma2d_ctx *ctx = prv;
	struct dma2d_frame *frm;
	int ret;

	ret = vidioc_try_selection(file, prv, s);
	if (ret)
		return ret;
	frm = get_frame(ctx, s->type);
	if (IS_ERR(frm))
		return PTR_ERR(frm);

	frm->c_width	= s->r.width;
	frm->c_height	= s->r.height;
	frm->o_width	= s->r.left;
	frm->o_height	= s->r.top;
	frm->bottom	= frm->o_height + frm->c_height;
	frm->right	= frm->o_width + frm->c_width;
	frm->line_ofs	= frm->o_width * frm->o_height;
	frm->width	= frm->c_width;
	frm->height	= frm->c_height;

	return 0;
}

static int vidioc_g_fbuf(struct file *file, void *priv, struct v4l2_framebuffer *arg)
{
	struct dma2d_ctx  *ctx = priv;
	struct v4l2_framebuffer *fb = arg;

	*fb = ctx->fb_buf;
	fb->capability = V4L2_FBUF_CAP_LIST_CLIPPING;

	return 0;
}

static int vidioc_s_fbuf(struct file *file, void *priv, const struct v4l2_framebuffer *arg)
{
	struct dma2d_ctx  *ctx = priv;
	const struct v4l2_framebuffer *fb = arg;
	struct dma2d_frame *frm;
	struct dma2d_fmt *fmt;

	if (!capable(CAP_SYS_ADMIN) && !capable(CAP_SYS_RAWIO))
		return -EPERM;

	/* check args */
	fmt = find_fmt(fb->fmt.pixelformat);
	if (fmt == NULL)
		return -EINVAL;
	/* use fbuf as background layer*/
	frm = &ctx->bg;
	
	frm->c_width	= fb->fmt.width;
	frm->c_height	= fb->fmt.height;
	frm->right	= frm->c_width;
	frm->bottom	= frm->c_height;
	frm->o_width	= 0;
	frm->o_height	= 0;
	frm->fmt	= fmt;
	ctx->op_mode	= DMA2D_MODE_M2M_BLEND;	
	ctx->fb_buf	= *fb;
	frm->width	= frm->c_width;
	frm->height	= frm->c_height;

	if (ctx->fb_buf.fmt.bytesperline == 0) {
		ctx->fb_buf.fmt.bytesperline =
			ctx->fb_buf.fmt.width * fmt->depth / 8;
	}

	return 0;
}

static void device_run(void *prv)
{
	struct dma2d_ctx *ctx = prv;
	struct dma2d_dev *dev = ctx->dev;
	struct dma2d_frame *frm = &ctx->out;
	struct vb2_v4l2_buffer *src, *dst;
	unsigned long flags;

	spin_lock_irqsave(&dev->ctrl_lock, flags);
	dev->curr = ctx;

	src = v4l2_m2m_next_src_buf(ctx->fh.m2m_ctx);
	dst = v4l2_m2m_next_dst_buf(ctx->fh.m2m_ctx);
	if (dst == NULL || src == NULL) {
		spin_unlock_irqrestore(&dev->ctrl_lock, flags);
		return;
	}

	clk_enable(dev->gate);
	printk("ctx->opmode %d\r\n", ctx->op_mode);
	
	dma2d_config_fg(dev, &ctx->fg,
		vb2_dma_contig_plane_dma_addr(&src->vb2_buf, 0));
	
	if (ctx->op_mode == DMA2D_MODE_M2M_BLEND) {
		if (ctx->alpha_component != 0) {
			ctx->bg.a_rgb[3] = ctx->alpha_component;
			ctx->bg.a_mode = DMA2D_ALPHA_MODE_REPLACE; 
		}
		dma2d_config_bg(dev, &ctx->bg,
				(dma_addr_t)ctx->fb_buf.base);
	} else if (ctx->op_mode != DMA2D_MODE_R2M){
		if (ctx->fg.fmt->fourcc == ctx->out.fmt->fourcc)
			ctx->op_mode = DMA2D_MODE_M2M;
		else
			ctx->op_mode = DMA2D_MODE_M2M_FPC;
	}

	dma2d_config_out(dev, &ctx->out,
			vb2_dma_contig_plane_dma_addr(&dst->vb2_buf, 0));
	dma2d_config_common(dev, ctx->op_mode, frm->width, frm->height);

	dma2d_start(dev);

	spin_unlock_irqrestore(&dev->ctrl_lock, flags);
}

static irqreturn_t dma2d_isr(int irq, void *prv)
{
	struct dma2d_dev *dev = prv;
	struct dma2d_ctx *ctx = dev->curr;
	struct vb2_v4l2_buffer *src, *dst;
	uint32_t s = dma2d_get_int(dev);
	v4l2_info(&dev->v4l2_dev, "dma2d isr %x\n",
			s);
	if (s & ISR_TCIF || s == 0) {
		dma2d_clear_int(dev);
		clk_disable(dev->gate);

		BUG_ON(ctx == NULL);

		src = v4l2_m2m_src_buf_remove(ctx->fh.m2m_ctx);
		dst = v4l2_m2m_dst_buf_remove(ctx->fh.m2m_ctx);
		
		BUG_ON(dst == NULL);
		BUG_ON(src == NULL);

		//dst->timecode = src->timecode;
		//dst->vb2_buf.timestamp = src->vb2_buf.timestamp;
		//dst->flags &= ~V4L2_BUF_FLAG_TSTAMP_SRC_MASK;
		//dst->flags |= src->flags & V4L2_BUF_FLAG_TSTAMP_SRC_MASK;

		v4l2_m2m_buf_done(src, VB2_BUF_STATE_DONE);
		v4l2_m2m_buf_done(dst, VB2_BUF_STATE_DONE);
		v4l2_m2m_job_finish(dev->m2m_dev, ctx->fh.m2m_ctx);

		dev->curr = NULL;
	} else {
//		v4l2_info(&dev->v4l2_dev, "dma2d isr %x\n",
//				dma2d_get_int(dev));
		dma2d_clear_int(dev);
	}

	return IRQ_HANDLED;
}

static const struct v4l2_file_operations dma2d_fops = {
	.owner		= THIS_MODULE,
	.open		= dma2d_open,
	.release	= dma2d_release,
	.poll		= v4l2_m2m_fop_poll,
	.unlocked_ioctl	= video_ioctl2,
	.mmap		= v4l2_m2m_fop_mmap,
#ifndef CONFIG_MMU
	.get_unmapped_area = v4l2_m2m_get_unmapped_area,
#endif
};

static const struct v4l2_ioctl_ops dma2d_ioctl_ops = {
	.vidioc_querycap	= vidioc_querycap,

	.vidioc_enum_fmt_vid_cap	= vidioc_enum_fmt,
	.vidioc_g_fmt_vid_cap		= vidioc_g_fmt,
	.vidioc_try_fmt_vid_cap		= vidioc_try_fmt,
	.vidioc_s_fmt_vid_cap		= vidioc_s_fmt,

	.vidioc_enum_fmt_vid_out	= vidioc_enum_fmt,
	.vidioc_g_fmt_vid_out		= vidioc_g_fmt,
	.vidioc_try_fmt_vid_out		= vidioc_try_fmt,
	.vidioc_s_fmt_vid_out		= vidioc_s_fmt,

	.vidioc_reqbufs			= v4l2_m2m_ioctl_reqbufs,
	.vidioc_querybuf		= v4l2_m2m_ioctl_querybuf,
	.vidioc_qbuf			= v4l2_m2m_ioctl_qbuf,
	.vidioc_dqbuf			= v4l2_m2m_ioctl_dqbuf,
	.vidioc_prepare_buf		= v4l2_m2m_ioctl_prepare_buf,
	.vidioc_create_bufs		= v4l2_m2m_ioctl_create_bufs,
	.vidioc_expbuf			= v4l2_m2m_ioctl_expbuf,

	.vidioc_streamon		= v4l2_m2m_ioctl_streamon,
	.vidioc_streamoff		= v4l2_m2m_ioctl_streamoff,

	.vidioc_g_selection		= vidioc_g_selection,
	.vidioc_s_selection		= vidioc_s_selection,
	
	//.vidioc_g_fbuf			= vidioc_g_fbuf,
	//.vidioc_s_fbuf			= vidioc_s_fbuf,

	.vidioc_subscribe_event = v4l2_ctrl_subscribe_event,
	.vidioc_unsubscribe_event = v4l2_event_unsubscribe,
};

static const struct video_device dma2d_videodev = {
	.name		= DMA2D_NAME,
	.fops		= &dma2d_fops,
	.ioctl_ops	= &dma2d_ioctl_ops,
	.minor		= -1,
	.release	= video_device_release,
	.vfl_dir	= VFL_DIR_M2M,
};

static const struct v4l2_m2m_ops dma2d_m2m_ops = {
	.device_run	= device_run,
};

static const struct of_device_id stm32_dma2d_match[];

static int dma2d_probe(struct platform_device *pdev)
{
	struct dma2d_dev *dev;
	struct video_device *vfd;
	struct resource *res;
	int ret = 0;

	dev = devm_kzalloc(&pdev->dev, sizeof(*dev), GFP_KERNEL);
	if (!dev)
		return -ENOMEM;

	spin_lock_init(&dev->ctrl_lock);
	mutex_init(&dev->mutex);
	atomic_set(&dev->num_inst, 0);

	res = platform_get_resource(pdev, IORESOURCE_MEM, 0);

	dev->regs = devm_ioremap_resource(&pdev->dev, res);
	if (IS_ERR(dev->regs))
		return PTR_ERR(dev->regs);

	dev->gate = clk_get(&pdev->dev, "dma2d");
	if (IS_ERR(dev->gate)) {
		dev_err(&pdev->dev, "failed to get dma2d clock gate\n");
		ret = -ENXIO;
		return ret;
	}

	ret = clk_prepare(dev->gate);
	if (ret) {
		dev_err(&pdev->dev, "failed to prepare dma2d clock gate\n");
		goto put_clk_gate;
	}

	res = platform_get_resource(pdev, IORESOURCE_IRQ, 0);
	if (!res) {
		dev_err(&pdev->dev, "failed to find IRQ\n");
		ret = -ENXIO;
		goto unprep_clk_gate;
	}

	dev->irq = res->start;

	ret = devm_request_irq(&pdev->dev, dev->irq, dma2d_isr,
						0, pdev->name, dev);
	if (ret) {
		dev_err(&pdev->dev, "failed to install IRQ\n");
		goto unprep_clk_gate;
	}

	//vb2_dma_contig_set_max_seg_size(&pdev->dev, DMA_BIT_MASK(32));

	ret = v4l2_device_register(&pdev->dev, &dev->v4l2_dev);
	if (ret)
		goto unprep_clk_gate;

	vfd = video_device_alloc();
	if (!vfd) {
		v4l2_err(&dev->v4l2_dev, "Failed to allocate video device\n");
		ret = -ENOMEM;
		goto unreg_v4l2_dev;
	}

	*vfd = dma2d_videodev;
	set_bit(V4L2_FL_QUIRK_INVERTED_CROP, &vfd->flags);
	vfd->lock = &dev->mutex;
	vfd->v4l2_dev = &dev->v4l2_dev;
	vfd->device_caps = V4L2_CAP_VIDEO_M2M | V4L2_CAP_STREAMING;
	ret = video_register_device(vfd, VFL_TYPE_VIDEO, 0);
	if (ret) {
		v4l2_err(&dev->v4l2_dev, "Failed to register video device\n");
		goto rel_vdev;
	}

	video_set_drvdata(vfd, dev);
	dev->vfd = vfd;
	v4l2_info(&dev->v4l2_dev, "device registered as /dev/video%d\n",
								vfd->num);
	platform_set_drvdata(pdev, dev);
	dev->m2m_dev = v4l2_m2m_init(&dma2d_m2m_ops);
	if (IS_ERR(dev->m2m_dev)) {
		v4l2_err(&dev->v4l2_dev, "Failed to init mem2mem device\n");
		ret = PTR_ERR(dev->m2m_dev);
		goto unreg_video_dev;
	}

	v4l2_info(&dev->v4l2_dev, "stm32 dma2d initialized\n");
	return 0;

unreg_video_dev:
	video_unregister_device(dev->vfd);
rel_vdev:
	video_device_release(vfd);
unreg_v4l2_dev:
	v4l2_device_unregister(&dev->v4l2_dev);
unprep_clk_gate:
	clk_unprepare(dev->gate);
put_clk_gate:
	clk_put(dev->gate);

	return ret;
}

static int dma2d_remove(struct platform_device *pdev)
{
	struct dma2d_dev *dev = platform_get_drvdata(pdev);

	v4l2_info(&dev->v4l2_dev, "Removing " DMA2D_NAME);
	v4l2_m2m_release(dev->m2m_dev);
	video_unregister_device(dev->vfd);
	v4l2_device_unregister(&dev->v4l2_dev);
	vb2_dma_contig_clear_max_seg_size(&pdev->dev);
	clk_unprepare(dev->gate);
	clk_put(dev->gate);

	return 0;
}

static const struct of_device_id stm32_dma2d_match[] = {
	{
		.compatible = "st,stm32-dma2d",
		.data = NULL,
	},
	{},
};
MODULE_DEVICE_TABLE(of, stm32_dma2d_match);

static struct platform_driver dma2d_pdrv = {
	.probe		= dma2d_probe,
	.remove		= dma2d_remove,
	.driver		= {
		.name = DMA2D_NAME,
		.of_match_table = stm32_dma2d_match,
	},
};

module_platform_driver(dma2d_pdrv);

MODULE_AUTHOR("Dillon Min <dillon.minfei@gmail.com>");
MODULE_DESCRIPTION("STM32 Chrom-Art Accelerator DMA2D driver");
MODULE_LICENSE("GPL");
