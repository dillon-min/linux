/* SPDX-License-Identifier: GPL-2.0-or-later */
/*
 * ST stm32 DMA2D - 2D Graphics Accelerator Driver
 *
 * Copyright (c) 2020 Dillon Min
 * based on s5p-g2d 
 * dillon.minfei@gmail.com
 */

#include <linux/platform_device.h>
#include <media/v4l2-device.h>
#include <media/v4l2-ctrls.h>

#define DMA2D_NAME "stm-dma2d"

enum dma2d_op_mode {
	DMA2D_MODE_M2M = 0x00,
	DMA2D_MODE_M2M_FPC = 0x01,
	DMA2D_MODE_M2M_BLEND = 0x02,
	DMA2D_MODE_R2M = 0x03,
};

enum dma2d_cmode {
	/* output pfc cmode from ARGB888 to ARGB4444 */
	DMA2D_CMODE_ARGB8888 = 0x00,
	DMA2D_CMODE_RGB888 = 0x01,
	DMA2D_CMODE_RGB565 = 0x02,
	DMA2D_CMODE_ARGB1555 = 0x03,
	DMA2D_CMODE_ARGB4444 = 0x04,
	/* bg or fg pfc cmode from L8 to A4 */
	DMA2D_CMODE_L8 = 0x05,
	DMA2D_CMODE_AL44 = 0x06,
	DMA2D_CMODE_AL88 = 0x07,
	DMA2D_CMODE_L4 = 0x08,
	DMA2D_CMODE_A8 = 0x09,
	DMA2D_CMODE_A4 = 0x0a,
};

enum dma2d_in_alpha_mode {
	DMA2D_ALPHA_MODE_NO_MODIF = 0x00,
	DMA2D_ALPHA_MODE_REPLACE = 0x01,
	DMA2D_ALPHA_MODE_COMBINE = 0x02,
};

struct dma2d_dev {
	struct v4l2_device	v4l2_dev;
	struct v4l2_m2m_dev	*m2m_dev;
	struct video_device	*vfd;
	struct mutex		mutex;
	spinlock_t		ctrl_lock;
	atomic_t		num_inst;
	void __iomem		*regs;
	struct clk		*gate;
	struct dma2d_ctx	*curr;
	int irq;
};

struct dma2d_frame {
	/*
	 * pixel per line
	 * PL[13:0] of DMA2D_NLR register
	 */
	u16	width;
	/*
	 * number lines 
	 * NL[15:0] of DMA2D_NLR register
	 */
	u16	height;
	/*
	 * bg offset address
	 * DMA2D_BGOR register
	 */
	u16	bg_ofs;
	/*
	 * fg offset address
	 * DMA2D_FGOR register
	 */
	u16	fg_ofs;
	/*
	 * output address offset
	 * DMA2D_OOR register
	 */
	u16	o_ofs;
	/* Image format */
	struct dma2d_fmt *fmt;
	/*
	 * rgba for bg, 0:R, 1:G, 2:B, 3:Alpha
	 * ALPHA[7:0] of DMA2D_BGPFCCR
	 * DMA2D_BGCOLR
	 */
	u8	bg_rgba[4];
	/*
	 * rgba for fg, 0:R, 1:G, 2:B, 3:Alpha
	 * ALPHA[7:0] of DMA2D_FGPFCCR
	 * DMA2D_FGCOLR
	 */
	u8	fg_rgba[4];
	/*
	 * rgba for output, 0:B, 1:G, 2:R, 3:Alpha
	 * DMA2D_OCOLR
	 */
	u8	o_rgba[4];
	/*
	 * AM[1:0] of DMA2D_BGPFCCR
	 */
	enum dma2d_in_alpha_mode bg_a_mode;
	/*
	 * AM[1:0] of DMA2D_FGPFCCR
	 */
	enum dma2d_in_alpha_mode fg_a_mode;
	/*
	 * MODE[17:16] of DMA2D_CR
	 */
	enum dma2d_op_mode op_mode;
	u32 size;
};

struct dma2d_ctx {
	struct v4l2_fh fh;
	struct dma2d_dev	*dev;
	struct dma2d_frame	in;
	struct dma2d_frame	out;
	u8			alpha_component;
	struct v4l2_ctrl_handler ctrl_handler;
};

struct dma2d_fmt {
	u32	fourcc;
	int	depth;
	enum dma2d_cmode cmode;
};

void dma2d_start(struct dma2d_dev *d);
u32 dma2d_get_int(struct dma2d_dev *d);
void dma2d_clear_int(struct dma2d_dev *d);
void dma2d_config_out(struct dma2d_dev *d, struct dma2d_frame *frm,
			dma_addr_t o_addr);
void dma2d_config_in(struct dma2d_dev *d, struct dma2d_frame *frm,
		dma_addr_t i_addr);
