/* SPDX-License-Identifier: GPL-2.0 */
/*
 * STM32F7/H7 JPEG encoder/decoder v4l2 driver
 *
 * Copyright 2021 Dillon Min <dillon.minfei@gmail.com>
 */

#ifndef _STM_JPEG_HW_H
#define _STM_JPEG_HW_H
#if 0
/* JPEG Decoder/Encoder Wrapper Register Map */
#define GLB_CTRL			0x0
#define COM_STATUS			0x4
#define BUF_BASE0			0x14
#define BUF_BASE1			0x18
#define LINE_PITCH			0x1C
#define STM_BUFBASE			0x20
#define STM_BUFSIZE			0x24
#define IMGSIZE				0x28
#define STM_CTRL			0x2C

/* CAST JPEG-Decoder/Encoder Status Register Map (read-only)*/
#define CAST_STATUS0			0x100
#define CAST_STATUS1			0x104
#define CAST_STATUS2			0x108
#define CAST_STATUS3			0x10c
#define CAST_STATUS4			0x110
#define CAST_STATUS5			0x114
#define CAST_STATUS6			0x118
#define CAST_STATUS7			0x11c
#define CAST_STATUS8			0x120
#define CAST_STATUS9			0x124
#define CAST_STATUS10			0x128
#define CAST_STATUS11			0x12c
#define CAST_STATUS12			0x130
#define CAST_STATUS13			0x134
/* the following are for encoder only */
#define CAST_STATUS14		0x138
#define CAST_STATUS15		0x13c
#define CAST_STATUS16		0x140
#define CAST_STATUS17		0x144
#define CAST_STATUS18		0x148
#define CAST_STATUS19		0x14c

/* CAST JPEG-Decoder Control Register Map (write-only) */
#define CAST_CTRL			CAST_STATUS13

/* CAST JPEG-Encoder Control Register Map (write-only) */
#define CAST_MODE			CAST_STATUS0
#define CAST_CFG_MODE			CAST_STATUS1
#define CAST_QUALITY			CAST_STATUS2
#define CAST_RSVD			CAST_STATUS3
#define CAST_REC_REGS_SEL		CAST_STATUS4
#define CAST_LUMTH			CAST_STATUS5
#define CAST_CHRTH			CAST_STATUS6
#define CAST_NOMFRSIZE_LO		CAST_STATUS7
#define CAST_NOMFRSIZE_HI		CAST_STATUS8
#define CAST_OFBSIZE_LO			CAST_STATUS9
#define CAST_OFBSIZE_HI			CAST_STATUS10

#define MXC_MAX_SLOTS	1 /* TODO use all 4 slots*/
/* JPEG-Decoder Wrapper Slot Registers 0..3 */
#define SLOT_BASE			0x10000
#define SLOT_STATUS			0x0
#define SLOT_IRQ_EN			0x4
#define SLOT_BUF_PTR			0x8
#define SLOT_CUR_DESCPT_PTR		0xC
#define SLOT_NXT_DESCPT_PTR		0x10
#define MXC_SLOT_OFFSET(slot, offset)	((SLOT_BASE * ((slot) + 1)) + (offset))

/* GLB_CTRL fields */
#define GLB_CTRL_JPG_EN					0x1
#define GLB_CTRL_SFT_RST				(0x1 << 1)
#define GLB_CTRL_DEC_GO					(0x1 << 2)
#define GLB_CTRL_L_ENDIAN(le)				((le) << 3)
#define GLB_CTRL_SLOT_EN(slot)				(0x1 << ((slot) + 4))

/* COM_STAUS fields */
#define COM_STATUS_DEC_ONGOING(r)		(((r) & (1 << 31)) >> 31)
#define COM_STATUS_CUR_SLOT(r)			(((r) & (0x3 << 29)) >> 29)

/* STM_CTRL fields */
#define STM_CTRL_PIXEL_PRECISION		(0x1 << 2)
#define STM_CTRL_IMAGE_FORMAT(img_fmt)		((img_fmt) << 3)
#define STM_CTRL_IMAGE_FORMAT_MASK		(0xF << 3)
#define STM_CTRL_BITBUF_PTR_CLR(clr)		((clr) << 7)
#define STM_CTRL_AUTO_START(go)			((go) << 8)
#define STM_CTRL_CONFIG_MOD(mod)		((mod) << 9)

/* SLOT_STATUS fields for slots 0..3 */
#define SLOT_STATUS_FRMDONE			(0x1 << 3)
#define SLOT_STATUS_ENC_CONFIG_ERR		(0x1 << 8)

/* SLOT_IRQ_EN fields TBD */

#define MXC_NXT_DESCPT_EN			0x1
#define MXC_DEC_EXIT_IDLE_MODE			0x4

/* JPEG-Decoder Wrapper - STM_CTRL Register Fields */
#define MXC_PIXEL_PRECISION(precision) ((precision) / 8 << 2)
enum mxc_jpeg_image_format {
	MXC_JPEG_INVALID = -1,
	MXC_JPEG_YUV420 = 0x0, /* 2 Plannar, Y=1st plane UV=2nd plane */
	MXC_JPEG_YUV422 = 0x1, /* 1 Plannar, YUYV sequence */
	MXC_JPEG_RGB	= 0x2, /* RGBRGB packed format */
	MXC_JPEG_YUV444	= 0x3, /* 1 Plannar, YUVYUV sequence */
	MXC_JPEG_GRAY = 0x4, /* Y8 or Y12 or Single Component */
	MXC_JPEG_RESERVED = 0x5,
	MXC_JPEG_ARGB	= 0x6,
};

#include "mxc-jpeg.h"
void print_descriptor_info(struct device *dev, struct mxc_jpeg_desc *desc);
void print_cast_status(struct device *dev, void __iomem *reg,
		       unsigned int mode);
void print_wrapper_info(struct device *dev, void __iomem *reg);
void mxc_jpeg_sw_reset(void __iomem *reg);
int mxc_jpeg_enable(void __iomem *reg);
void wait_frmdone(struct device *dev, void __iomem *reg);
void mxc_jpeg_enc_mode_conf(struct device *dev, void __iomem *reg);
void mxc_jpeg_enc_mode_go(struct device *dev, void __iomem *reg);
void mxc_jpeg_dec_mode_go(struct device *dev, void __iomem *reg);
int mxc_jpeg_get_slot(void __iomem *reg);
u32 mxc_jpeg_get_offset(void __iomem *reg, int slot);
void mxc_jpeg_enable_slot(void __iomem *reg, int slot);
void mxc_jpeg_set_l_endian(void __iomem *reg, int le);
void mxc_jpeg_enable_irq(void __iomem *reg, int slot);
int mxc_jpeg_set_input(void __iomem *reg, u32 in_buf, u32 bufsize);
int mxc_jpeg_set_output(void __iomem *reg, u16 out_pitch, u32 out_buf,
			u16 w, u16 h);
void mxc_jpeg_set_config_mode(void __iomem *reg, int config_mode);
int mxc_jpeg_set_params(struct mxc_jpeg_desc *desc,  u32 bufsize, u16
			out_pitch, u32 format);
void mxc_jpeg_set_bufsize(struct mxc_jpeg_desc *desc,  u32 bufsize);
void mxc_jpeg_set_res(struct mxc_jpeg_desc *desc, u16 w, u16 h);
void mxc_jpeg_set_line_pitch(struct mxc_jpeg_desc *desc, u32 line_pitch);
void mxc_jpeg_set_desc(u32 desc, void __iomem *reg, int slot);
void mxc_jpeg_set_regs_from_desc(struct mxc_jpeg_desc *desc,
				 void __iomem *reg);
#else
#define	JPEG_REG_CONF0		0x0000
#define	JPEG_REG_CONF1		0x0004
#define	JPEG_REG_CONF2		0x0008
#define	JPEG_REG_CONF3		0x000c
#define	JPEG_REG_CONF4		0x0010
#define	JPEG_REG_CONF5		0x0014
#define	JPEG_REG_CONF6		0x0018
#define	JPEG_REG_CONF7		0x001c
#define	JPEG_REG_CR			0x0030
#define	JPEG_REG_SR			0x0034
#define	JPEG_REG_CFR		0x0038
#define	JPEG_REG_DIR		0x0040
#define	JPEG_REG_DOR		0x0044
#define JPEG_REG_QMEM0		0x0050
#define JPEG_REG_QMEM1		0x0090
#define JPEG_REG_QMEM2		0x00d0
#define JPEG_REG_QMEM3		0x0110
#define JPEG_REG_HUFF_MIN	0x0150
#define JPEG_REG_HUFF_BASE	0x0190
#define JPEG_REG_HUFF_SYMB	0x0210
#define JPEG_REG_DHT_MEM	0x0360
#define JPEG_REG_HUFF_AC0	0x0500
#define JPEG_REG_HUFF_AC1	0x0660
#define JPEG_REG_HUFF_DC0	0x07c0
#define JPEG_REG_HUFF_DC1	0x07e0

#define CONFR0_START		BIT(0)
#define CONFR1_YSIZE_MASK	GENMASK(31, 16)
#define CONFR1_YSIZE_SHIFT	16
#define CONFR1_HDR_EN		BIT(8)
#define CONFR1_NS_MASK		GENMASK(7, 6)
#define CONFR1_NS_SHIFT		6
#define CONFR1_CS_MASK		GENMASK(5, 4)
#define CONFR1_CS_SHIFT		4
#define CONFR1_DECODE		BIT(3)
#define CONFR1_NF_MASK		GENMASK(1, 0)
#define CONFR1_NF_SHIFT		0
#define CONFR2_NMCU_MASK	GENMASK(25, 0)
#define CONFR2_NMCU_SHIFT	0
#define CONFR3_XSIZE_MASK	GENMASK(31, 16)
#define CONFR3_XSIZE_SHIFT	16
#define CONFRx_HSF_MASK		GENMASK(15, 12)
#define CONFRx_HSF_SHIFT	12
#define CONFRx_VSF_MASK		GENMASK(11, 8)
#define CONFRx_VSF_SHIFT	8
#define CONFRx_NB_MASK		GENMASK(7, 4)
#define CONFRx_NB_SHIFT		4
#define CONFRx_QT_MASK		GENMASK(3, 2)
#define CONFRx_QT_SHIFT		2
#define CR_IE_MASK			GENMASK(6, 1)
#define CR_IE_SHIFT			1
#define CONFRx_QT_0			0x00
#define CONFRx_QT_1			0x01
#define CONFRx_QT_2			0x02
#define CONFRx_QT_3			0x03
#define CONFRx_HA_1			BIT(1)
#define CONFRx_HD_1			BIT(0)
#define CR_OFF				BIT(14)
#define CR_IFF				BIT(13)
#define CR_HPDIE			BIT(6)
#define CR_EOCIE			BIT(5)
#define CR_OFNEIE			BIT(4)
#define CR_OFTIE			BIT(3)
#define CR_IFNFIE			BIT(2)
#define CR_IFTIE			BIT(1)
#define CR_JCEN				BIT(0)
#define SR_COF				BIT(7)
#define SR_HPDF				BIT(6)
#define SR_EOCF				BIT(5)
#define SR_OFNEF			BIT(4)
#define SR_OFTF				BIT(3)
#define SR_IFNFF			BIT(2)
#define SR_IFTF				BIT(1)
#define CFR_CHPDF			BIT(6)
#define CFR_CEOCF			BIT(5)

/* number of quantization tables minus 1 to insert in the output stream */
enum stm_jpeg_color_space {
	STM_JPEG_CS_GRAY = 0x00, /* 1 quantization table */
	STM_JPEG_CS_YCBCR = 0x01, /* 2 quantization table */
	STM_JPEG_CS_RGB = 0x02, /* 3 quantization table */
	STM_JPEG_CS_CMYK = 0x03, /* 4 quantization table */
};

/* number of color components minus 1 */
enum stm_jpeg_num_comp {
	STM_JPEG_NF_1 = 0x00, /* 1 color component */
	STM_JPEG_NF_2 = 0x01, /* 2 color component */
	STM_JPEG_NF_3 = 0x02, /* 3 color component */
	STM_JPEG_NF_4 = 0x03, /* 4 color component */
};

#endif
#endif
