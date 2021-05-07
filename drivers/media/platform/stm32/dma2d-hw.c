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

static inline u32 reg_read(void __iomem *base, u32 reg)
{
	return readl_relaxed(base + reg);
}

static void dump_regs(struct dma2d_dev *d)
{
	printk("reg cr \t%x\r\n", reg_read(d->regs, 0x00));
	printk("reg isr \t%x\r\n", reg_read(d->regs, 0x04));
	printk("reg ifcr \t%x\r\n", reg_read(d->regs, 0x08));
	printk("reg fgmar \t%x\r\n", reg_read(d->regs, 0x0c));
	printk("reg fgor \t%x\r\n", reg_read(d->regs, 0x10));
	printk("reg bgmar \t%x\r\n", reg_read(d->regs, 0x14));
	printk("reg bgor \t%x\r\n", reg_read(d->regs, 0x18));
	printk("reg fgpfccr \t%x\r\n", reg_read(d->regs, 0x1c));
	printk("reg fgcolr \t%x\r\n", reg_read(d->regs, 0x20));
	printk("reg bgpfccr \t%x\r\n", reg_read(d->regs, 0x24));
	printk("reg bgcolr \t%x\r\n", reg_read(d->regs, 0x28));
	printk("reg fgcmar \t%x\r\n", reg_read(d->regs, 0x2c));
	printk("reg bgcmar \t%x\r\n", reg_read(d->regs, 0x30));
	printk("reg opfccr \t%x\r\n", reg_read(d->regs, 0x34));
	printk("reg ocolr \t%x\r\n", reg_read(d->regs, 0x38));
	printk("reg omar \t%x\r\n", reg_read(d->regs, 0x3c));
	printk("reg oor \t%x\r\n", reg_read(d->regs, 0x40));
	printk("reg nlr \t%x\r\n", reg_read(d->regs, 0x44));
	printk("reg lwr \t%x\r\n", reg_read(d->regs, 0x48));
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
	dump_regs(d);
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

void dma2d_config_common(struct dma2d_dev *d, enum dma2d_op_mode op_mode,
		u16 width, u16 height)
{
	/* dma2d transfer mode */
	reg_update_bits(d->regs, DMA2D_CR_REG, CR_MODE_MASK,
		op_mode << CR_MODE_SHIFT);

	/* dma2d transfer width, height*/
	reg_write(d->regs, DMA2D_NLR_REG, (width << 16) | height);
}

void dma2d_config_out(struct dma2d_dev *d, struct dma2d_frame *frm,
		dma_addr_t o_addr)
{
	/* dma2d interrupts enable */
	reg_update_bits(d->regs, DMA2D_CR_REG, CR_CEIE, CR_CEIE);
	reg_update_bits(d->regs, DMA2D_CR_REG, CR_CTCIE, CR_CTCIE);
	reg_update_bits(d->regs, DMA2D_CR_REG, CR_CAEIE, CR_CAEIE);
	reg_update_bits(d->regs, DMA2D_CR_REG, CR_TCIE, CR_TCIE);
	reg_update_bits(d->regs, DMA2D_CR_REG, CR_TEIE, CR_TEIE);

	/* dma2d output color mode */
	if (frm->fmt->cmode < 5)
		reg_update_bits(d->regs, DMA2D_OPFCCR_REG,
			OPFCCR_CM_MASK,
			frm->fmt->cmode); 

	/* dma2d output address */
	reg_write(d->regs, DMA2D_OMAR_REG, o_addr);

	/* dma2d output alpha, rgb*/
	reg_write(d->regs, DMA2D_OCOLR_REG,
		(frm->argb[3] << 24) |
		(frm->argb[2] << 16) |
		(frm->argb[1] << 8) |
		frm->argb[0]);

	/* dma2d output offset */
	reg_update_bits(d->regs, DMA2D_OOR_REG, OOR_LO_MASK,
			frm->line_ofs & 0x3fff);
}

void dma2d_config_fg(struct dma2d_dev *d, struct dma2d_frame *frm,
		dma_addr_t f_addr)
{
	/* dma2d input address */
	reg_write(d->regs, DMA2D_FGMAR_REG, f_addr);
	reg_update_bits(d->regs, DMA2D_FGOR_REG, FGOR_LO_MASK,
			frm->line_ofs);

	/* dma2d input color mode */
	reg_update_bits(d->regs, DMA2D_FGPFCCR_REG, FGPFCCR_CM_MASK,
			frm->fmt->cmode);

	/* dma2d input alpha mode */
	reg_update_bits(d->regs, DMA2D_FGPFCCR_REG, FGPFCCR_AM_MASK,
			frm->a_mode << 16);

	reg_update_bits(d->regs, DMA2D_FGPFCCR_REG, FGPFCCR_ALPHA_MASK,
			((frm->argb[3] << 24) & 0x03));
}

void dma2d_config_bg(struct dma2d_dev *d, struct dma2d_frame *frm,
		dma_addr_t b_addr)
{
	/* dma2d input address */
	reg_write(d->regs, DMA2D_BGMAR_REG, b_addr);
	reg_update_bits(d->regs, DMA2D_BGOR_REG, BGOR_LO_MASK,
			frm->line_ofs);

	/* dma2d input color mode */
	reg_update_bits(d->regs, DMA2D_BGPFCCR_REG, BGPFCCR_CM_MASK,
			frm->fmt->cmode);

	/* dma2d input alpha mode */
	reg_update_bits(d->regs, DMA2D_BGPFCCR_REG, BGPFCCR_AM_MASK,
			frm->a_mode << 16);

	reg_update_bits(d->regs, DMA2D_BGPFCCR_REG, BGPFCCR_ALPHA_MASK,
			((frm->argb[3] << 24) & 0x03));
}
