/* SPDX-License-Identifier: GPL-2.0 */
/*
 * i.MX8QXP/i.MX8QM JPEG encoder/decoder v4l2 driver
 *
 * Copyright 2018-2019 NXP
 */

#include <media/v4l2-ctrls.h>
#include <media/v4l2-device.h>
#include <media/v4l2-fh.h>

#ifndef _MXC_JPEG_CORE_H
#define _MXC_JPEG_CORE_H
#if 0
#define MXC_JPEG_NAME			"mxc-jpeg"
#define MXC_JPEG_FMT_TYPE_ENC		0
#define MXC_JPEG_FMT_TYPE_RAW		1
#define MXC_JPEG_DEFAULT_WIDTH		1280
#define MXC_JPEG_DEFAULT_HEIGHT		720
#define MXC_JPEG_DEFAULT_PFMT		V4L2_PIX_FMT_RGB24
#define MXC_JPEG_MIN_WIDTH		64
#define MXC_JPEG_MIN_HEIGHT		64
#define MXC_JPEG_MAX_WIDTH		0x2000
#define MXC_JPEG_MAX_HEIGHT		0x2000
#define MXC_JPEG_MAX_CFG_STREAM		0x1000
#define MXC_JPEG_H_ALIGN		3
#define MXC_JPEG_W_ALIGN		3
#define MXC_JPEG_MAX_SIZEIMAGE		0xFFFFFC00
#define MXC_JPEG_MAX_PLANES		2

enum mxc_jpeg_enc_state {
	MXC_JPEG_ENCODING	= 0, /* jpeg encode phase */
	MXC_JPEG_ENC_CONF	= 1, /* jpeg encoder config phase */
};

enum mxc_jpeg_mode {
	MXC_JPEG_DECODE	= 0, /* jpeg decode mode */
	MXC_JPEG_ENCODE	= 1, /* jpeg encode mode */
};

/**
 * struct mxc_jpeg_fmt - driver's internal color format data
 * @name:	format description
 * @fourcc:	fourcc code, 0 if not applicable
 * @subsampling: subsampling of jpeg components
 * @nc:		number of color components
 * @depth:	number of bits per pixel
 * @colplanes:	number of color planes (1 for packed formats)
 * @h_align:	horizontal alignment order (align to 2^h_align)
 * @v_align:	vertical alignment order (align to 2^v_align)
 * @flags:	flags describing format applicability
 */
struct mxc_jpeg_fmt {
	const char				*name;
	u32					fourcc;
	enum v4l2_jpeg_chroma_subsampling	subsampling;
	int					nc;
	int					depth;
	int					colplanes;
	int					h_align;
	int					v_align;
	u32					flags;
};

struct mxc_jpeg_desc {
	u32 next_descpt_ptr;
	u32 buf_base0;
	u32 buf_base1;
	u32 line_pitch;
	u32 stm_bufbase;
	u32 stm_bufsize;
	u32 imgsize;
	u32 stm_ctrl;
} __packed;

struct mxc_jpeg_q_data {
	const struct mxc_jpeg_fmt	*fmt;
	u32				sizeimage[MXC_JPEG_MAX_PLANES];
	u32				bytesperline[MXC_JPEG_MAX_PLANES];
	int				w;
	int				w_adjusted;
	int				h;
	int				h_adjusted;
	unsigned int			sequence;
};

struct mxc_jpeg_ctx {
	struct mxc_jpeg_dev		*mxc_jpeg;
	struct mxc_jpeg_q_data		out_q;
	struct mxc_jpeg_q_data		cap_q;
	struct v4l2_fh			fh;
	enum mxc_jpeg_enc_state		enc_state;
	unsigned int			stopping;
	unsigned int			slot;
};

struct mxc_jpeg_slot_data {
	bool used;
	struct mxc_jpeg_desc *desc; // enc/dec descriptor
	struct mxc_jpeg_desc *cfg_desc; // configuration descriptor
	void *cfg_stream_vaddr; // configuration bitstream virtual address
	unsigned int cfg_stream_size;
	dma_addr_t desc_handle;
	dma_addr_t cfg_desc_handle; // configuration descriptor dma address
	dma_addr_t cfg_stream_handle; // configuration bitstream dma address
};

struct mxc_jpeg_dev {
	spinlock_t			hw_lock; /* hardware access lock */
	unsigned int			mode;
	struct mutex			lock; /* v4l2 ioctls serialization */
	struct platform_device		*pdev;
	struct device			*dev;
	void __iomem			*base_reg;
	struct v4l2_device		v4l2_dev;
	struct v4l2_m2m_dev		*m2m_dev;
	struct video_device		*dec_vdev;
	struct mxc_jpeg_slot_data	slot_data[MXC_MAX_SLOTS];
	int				num_domains;
	struct device			**pd_dev;
	struct device_link		**pd_link;
};

/**
 * struct mxc_jpeg_sof_comp - JPEG Start Of Frame component fields
 * @id:				component id
 * @v:				vertical sampling
 * @h:				horizontal sampling
 * @quantization_table_no:	id of quantization table
 */
struct mxc_jpeg_sof_comp {
	u8 id;
	u8 v :4;
	u8 h :4;
	u8 quantization_table_no;
} __packed;

#define MXC_JPEG_MAX_COMPONENTS 4
/**
 * struct mxc_jpeg_sof - JPEG Start Of Frame marker fields
 * @length:		Start of Frame length
 * @precision:		precision (bits per pixel per color component)
 * @height:		image height
 * @width:		image width
 * @components_no:	number of color components
 * @comp:		component fields for each color component
 */
struct mxc_jpeg_sof {
	u16 length;
	u8 precision;
	u16 height, width;
	u8 components_no;
	struct mxc_jpeg_sof_comp comp[MXC_JPEG_MAX_COMPONENTS];
} __packed;

/**
 * struct mxc_jpeg_sos_comp - JPEG Start Of Scan component fields
 * @id:			component id
 * @huffman_table_no:	id of the Huffman table
 */
struct mxc_jpeg_sos_comp {
	u8 id; /*component id*/
	u8 huffman_table_no;
} __packed;

/**
 * struct mxc_jpeg_sos - JPEG Start Of Scan marker fields
 * @length:		Start of Frame length
 * @components_no:	number of color components
 * @comp:		SOS component fields for each color component
 * @ignorable_bytes:	ignorable bytes
 */
struct mxc_jpeg_sos {
	u16 length;
	u8 components_no;
	struct mxc_jpeg_sos_comp comp[MXC_JPEG_MAX_COMPONENTS];
	u8 ignorable_bytes[3];
} __packed;
#else
#define STM_JPEG_NAME			"stm32-jpeg"
/* Flags that indicate a format can be used for capture/output */
#define STM_JPEG_FMT_FLAG_ENC_CAPTURE	(1 << 0)
#define STM_JPEG_FMT_FLAG_ENC_OUTPUT	(1 << 1)
#define STM_JPEG_FMT_FLAG_DEC_CAPTURE	(1 << 2)
#define STM_JPEG_FMT_FLAG_DEC_OUTPUT	(1 << 3)
#define STM_JPEG_FMT_FLAG_STM32H7		(1 << 4)
#define STM_JPEG_FMT_FLAG_STM32F7	(1 << 5)
#define STM_JPEG_FMT_RGB			(1 << 7)
#define STM_JPEG_FMT_NON_RGB		(1 << 8)

#define STM_JPEG_ENCODE		0
#define STM_JPEG_DECODE		1
#define STM_JPEG_DISABLE	-1

#define FMT_TYPE_OUTPUT		0
#define FMT_TYPE_CAPTURE	1

#define STM_JPEG_MAX_WIDTH 2592
#define STM_JPEG_MAX_HEIGHT 2592
#define STM_JPEG_MIN_WIDTH 32
#define STM_JPEG_MIN_HEIGHT 32
/* Version numbers */
enum stm_jpeg_version {
	STM_JPEG_F7,
	STM_JPEG_H7,
};

enum stm_jpeg_ctx_state {
	JPEGCTX_RUNNING = 0,
	JPEGCTX_RESOLUTION_CHANGE,
};

struct stm_jpeg_variant {
	unsigned int		version;
	unsigned int		fmt_ver_flag;
	struct v4l2_m2m_ops	*m2m_ops;
	irqreturn_t		(*jpeg_irq)(int irq, void *priv);
	const char		*clk_names[JPEG_MAX_CLOCKS];
	int			num_clocks;
};

/**
 * struct stm_jpeg - JPEG IP abstraction
 * @lock:		the mutex protecting this structure
 * @slock:		spinlock protecting the device contexts
 * @v4l2_dev:		v4l2 device for mem2mem mode
 * @vfd_encoder:	video device node for encoder mem2mem mode
 * @vfd_decoder:	video device node for decoder mem2mem mode
 * @m2m_dev:		v4l2 mem2mem device data
 * @regs:		JPEG IP registers mapping
 * @irq:		JPEG IP irq
 * @irq_ret:		JPEG IP irq result value
 * @clocks:		JPEG IP clock(s)
 * @dev:		JPEG IP struct device
 * @variant:		driver variant to be used
 * @irq_status:		interrupt flags set during single encode/decode
 *			operation
 */
struct stm_jpeg {
	struct mutex		lock;
	spinlock_t		slock;

	struct v4l2_device	v4l2_dev;
	struct video_device	*vfd_encoder;
	struct video_device	*vfd_decoder;
	struct v4l2_m2m_dev	*m2m_dev;

	void __iomem		*regs;
	unsigned int		irq;
	enum exynos4_jpeg_result irq_ret;
	struct clk		*clocks[JPEG_MAX_CLOCKS];
	struct device		*dev;
	struct stm_jpeg_variant *variant;
	u32			irq_status;
};

/**
 * struct stm_jpeg_fmt - driver's internal color format data
 * @name: format description
 * @fourcc: fourcc code, 0 if not applicable
 * @subsampling: subsampling of jpeg components
 * @depth:	number of bits per pixel
 * @colplanes:	number of color planes (1 for packed formats)
 * @ns: number of components minus 1 for scan header marker segment.
 * @color_space: number of quantization tables minus 1.
 * @nf: number of color components minus 1.
 * @h_factor:
 * @v_factor:
 * @hsf: horizontal sampling factor for component i.
 * @vsf: vertical sampling factor for component i.
 * @nb: number of data units minus 1 that belong to a particular color in the MCU.
 * @qt: selects quantization table used for component i.
 * @ha: selects the Huffman table for encoding AC coefficients.
 * @hd: selects the Huffman table for encoding DC coefficients.
 * @flags:	flags describing format applicability
 */
struct stm_jpeg_fmt {
	const char	*name;
	u32			fourcc;
	enum v4l2_jpeg_chroma_subsampling	subsampling;
	int			h_align;
	int			v_align;
	int			depth;
	int			colplanes;
	u32			flags;
};

/**
 * struct stm_jpeg_q_data - parameters of one queue
 * @fmt:	driver-specific format of this queue
 * @w:		image width
 * @h:		image height
 * @size:	image buffer size in bytes
 */
struct stm_jpeg_q_data {
	struct stm_jpeg_fmt	*fmt;
	u32			w;
	u32			h;
	u32			size;
};

/**
 * struct stm_jpeg_ctx - the device context data
 * @jpeg:		JPEG IP device for this context
 * @mode:		compression (encode) operation or decompression (decode)
 * @compr_quality:	destination image quality in compression (encode) mode
 * @restart_interval:	JPEG restart interval for JPEG encoding
 * @subsampling:	subsampling of a raw format or a JPEG
 * @out_q:		source (output) queue information
 * @cap_q:		destination (capture) queue queue information
 * @scale_factor:	scale factor for JPEG decoding
 * @crop_rect:		a rectangle representing crop area of the output buffer
 * @fh:			V4L2 file handle
 * @hdr_parsed:		set if header has been parsed during decompression
 * @crop_altered:	set if crop rectangle has been altered by the user space
 * @ctrl_handler:	controls handler
 * @state:		state of the context
 */
struct stm_jpeg_ctx {
	struct stm_jpeg		*jpeg;
	unsigned int		mode;
	unsigned short		compr_quality;
	unsigned short		restart_interval;
	unsigned short		subsampling;
	struct s5p_jpeg_q_data	out_q;
	struct s5p_jpeg_q_data	cap_q;
	unsigned int		scale_factor;
	struct v4l2_rect	crop_rect;
	struct v4l2_fh		fh;
	bool			hdr_parsed;
	bool			crop_altered;
	struct v4l2_ctrl_handler ctrl_handler;
	enum stm_jpeg_ctx_state	state;
};

/**
 * struct stm_jpeg_buffer - description of memory containing input JPEG data
 * @size:	buffer size
 * @curr:	current position in the buffer
 * @data:	pointer to the data
 */
struct stm_jpeg_buffer {
	unsigned long size;
	unsigned long curr;
	unsigned long data;
};

/**
 * struct stm_jpeg_addr - JPEG converter physical address set for DMA
 * @y:   luminance plane physical address
 * @cb:  Cb plane physical address
 * @cr:  Cr plane physical address
 */
struct stm_jpeg_addr {
	u32     y;
	u32     cb;
	u32     cr;
};
#endif
#endif
