// SPDX-License-Identifier: GPL-2.0-or-later
/*
 * Samsung S5P G2D - 2D Graphics Accelerator Driver
 *
 * Copyright (c) 2011 Samsung Electronics Co., Ltd.
 * Kamil Debski, <k.debski@samsung.com>
 */

#include <linux/io.h>

#include "dma2d.h"
#include "dma2d-regs.h"

#define w(x, a)	writel((x), d->regs + (a))
#define r(a)	readl(d->regs + (a))
#if 0
/* dma2d_reset clears all dma2d registers */
void dma2d_reset(struct dma2d_dev *d)
{
	w(1, SOFT_RESET_REG);
}

void dma2d_set_src_size(struct dma2d_dev *d, struct dma2d_frame *f)
{
	u32 n;

	w(0, SRC_SELECT_REG);
	w(f->stride & 0xFFFF, SRC_STRIDE_REG);

	n = f->o_height & 0xFFF;
	n <<= 16;
	n |= f->o_width & 0xFFF;
	w(n, SRC_LEFT_TOP_REG);

	n = f->bottom & 0xFFF;
	n <<= 16;
	n |= f->right & 0xFFF;
	w(n, SRC_RIGHT_BOTTOM_REG);

	w(f->fmt->hw, SRC_COLOR_MODE_REG);
}

void dma2d_set_src_addr(struct dma2d_dev *d, dma_addr_t a)
{
	w(a, SRC_BASE_ADDR_REG);
}

void dma2d_set_dst_size(struct dma2d_dev *d, struct dma2d_frame *f)
{
	u32 n;

	w(0, DST_SELECT_REG);
	w(f->stride & 0xFFFF, DST_STRIDE_REG);

	n = f->o_height & 0xFFF;
	n <<= 16;
	n |= f->o_width & 0xFFF;
	w(n, DST_LEFT_TOP_REG);

	n = f->bottom & 0xFFF;
	n <<= 16;
	n |= f->right & 0xFFF;
	w(n, DST_RIGHT_BOTTOM_REG);

	w(f->fmt->hw, DST_COLOR_MODE_REG);
}

void dma2d_set_dst_addr(struct dma2d_dev *d, dma_addr_t a)
{
	w(a, DST_BASE_ADDR_REG);
}

void dma2d_set_rop4(struct dma2d_dev *d, u32 r)
{
	w(r, ROP4_REG);
}

void dma2d_set_flip(struct dma2d_dev *d, u32 r)
{
	w(r, SRC_MSK_DIRECT_REG);
}

void dma2d_set_v41_stretch(struct dma2d_dev *d, struct dma2d_frame *src,
					struct dma2d_frame *dst)
{
	w(DEFAULT_SCALE_MODE, SRC_SCALE_CTRL_REG);

	/* inversed scaling factor: src is numerator */
	w((src->c_width << 16) / dst->c_width, SRC_XSCALE_REG);
	w((src->c_height << 16) / dst->c_height, SRC_YSCALE_REG);
}

void dma2d_set_cmd(struct dma2d_dev *d, u32 c)
{
	w(c, BITBLT_COMMAND_REG);
}

void dma2d_start(struct dma2d_dev *d)
{
	/* Clear cache */
	if (d->variant->hw_rev == TYPE_G2D_3X)
		w(0x7, CACHECTL_REG);

	/* Enable interrupt */
	w(1, INTEN_REG);
	/* Start G2D engine */
	w(1, BITBLT_START_REG);
}

void dma2d_clear_int(struct dma2d_dev *d)
{
	w(1, INTC_PEND_REG);
}
#else
static inline u32 reg_read(void __iomem *base, u32 reg)
{
	return readl_relaxed(base + reg);
}

static inline void reg_write(void __iomem *base, u32 reg, u32 val)
{
	writel_relaxed(val, base + reg);
}

static inline void reg_set(void __iomem *base, u32 reg, u32 mask)
{
	reg_write(base, reg, reg_read(base, reg) | mask);
}

static inline void reg_clear(void __iomem *base, u32 reg, u32 mask)
{
	reg_write(base, reg, reg_read(base, reg) & ~mask);
}

static inline void reg_update_bits(void __iomem *base, u32 reg, u32 mask,
				   u32 val)
{
	reg_write(base, reg, (reg_read(base, reg) & ~mask) | val);
}

void dma2d_start(struct dma2d_dev *d)
{
    reg_update_bits(d->regs, DMA2D_CR_REG, CR_START, CR_START);
}

u32 dma2d_get_int(struct dma2d_dev *d)
{
    return reg_read(d->regs, DMA2D_ISR_REG);
}

void dma2d_clear_int(struct dma2d_dev *d)
{
    u32 isr_val = reg_read(d->regs, DMA2D_ISR_REG);
    reg_write(d->regs, DMA2D_IFCR_REG, isr_val & 0x003f);
}

void dma2d_config_out(struct dma2d_dev *d, struct dma2d_frame *frm,
			dma_addr_t o_addr)
{
    /* dma2d transfer mode */
	reg_update_bits(d->regs, DMA2D_CR_REG, CR_MODE_MASK,
			frm->op_mode << CR_MODE_SHIFT |
			CR_CEIE |
			CR_CTCIE |
			CR_CAEIE |
			CR_TCIE |
			CR_TEIE); 
    /* dma2d output color mode */
    	if (frm->fmt->cmode < 5)
		reg_update_bits(d->regs, DMA2D_OPFCCR_REG, OPFCCR_CM_MASK,
			frm->fmt->cmode); 
    /* dma2d output address */
	reg_write(d->regs, DMA2D_OMAR_REG, o_addr);
    /* dma2d output alpha, rgb*/
    	reg_write(d->regs, DMA2D_OCOLR_REG, (frm->o_rgba[3] << 24) |
    					(frm->o_rgba[2] << 16) |
    					(frm->o_rgba[1] << 8) |
    					frm->o_rgba[0]);
    /* dma2d output offset */
	reg_update_bits(d->regs, DMA2D_OOR_REG, OOR_LO_MASK,
					frm->o_ofs & 0x3fff);
    /* dma2d transfer width, height*/
	reg_write(d->regs, DMA2D_NLR_REG, ((frm->width << 16) & 0x3fff) |
					frm->height);
}

void dma2d_config_in(struct dma2d_dev *d, struct dma2d_frame *frm,
		dma_addr_t i_addr)
{
    /* dma2d input address */
	reg_write(d->regs, DMA2D_FGMAR_REG, i_addr);
    /* dma2d input color mode */
	reg_update_bits(d->regs, DMA2D_FGPFCCR_REG, FGPFCCR_CM_MASK,
					frm->fg_a_mode);
    /* dma2d input alpha mode */
	reg_update_bits(d->regs, DMA2D_FGPFCCR_REG, FGPFCCR_AM_MASK,
					frm->fg_a_mode << 16 |
					((frm->fg_rgba[3] << 24) & 0x03) |
					frm->fmt->cmode);
}
#endif
