// SPDX-License-Identifier: GPL-2.0-only
/*
 * Ilitek ILI9341 TFT LCD drm_panel driver.
 *
 * This panel can be configured to support:
 * - 16-bit parallel RGB interface
 *
 * Copyright (C) 2020 Dillon Min <dillon.minfei@gmail.com>
 * Derived from drivers/drm/gpu/panel/panel-ilitek-ili9322.c
 */

#include <linux/bitops.h>
#include <linux/gpio/consumer.h>
#include <linux/module.h>
#include <linux/of_device.h>
#include <linux/regulator/consumer.h>
#include <linux/spi/spi.h>
#include <linux/delay.h>
#include <video/mipi_display.h>
#include <video/of_videomode.h>
#include <video/videomode.h>

#include <drm/drm_modes.h>
#include <drm/drm_panel.h>
#include <drm/drm_print.h>

#define DEFAULT_SPI_SPEED	10000000

#define ILI9341_SLEEP_OUT            0x11   /* Sleep out register */
#define ILI9341_GAMMA                0x26   /* Gamma register */
#define ILI9341_DISPLAY_OFF          0x28   /* Display off register */
#define ILI9341_DISPLAY_ON           0x29   /* Display on register */
#define ILI9341_COLUMN_ADDR          0x2A   /* Colomn address register */
#define ILI9341_PAGE_ADDR            0x2B   /* Page address register */
#define ILI9341_GRAM                 0x2C   /* GRAM register */
#define ILI9341_MAC                  0x36   /* Memory Access Control register*/
#define ILI9341_PIXEL_FORMAT         0x3A   /* Pixel Format register */
#define ILI9341_WDB                  0x51   /* Write Brightness Display
					     * register
					     */
#define ILI9341_WCD                  0x53   /* Write Control Display
					     * register
					     */
#define ILI9341_RGB_INTERFACE        0xB0   /* RGB Interface Signal Control */
#define ILI9341_FRC                  0xB1   /* Frame Rate Control register */
#define ILI9341_BPC                  0xB5   /* Blanking Porch Control
					     * register
					     */
#define ILI9341_DFC                  0xB6   /* Display Function Control
					     * register
					     */
#define ILI9341_POWER1               0xC0   /* Power Control 1 register */
#define ILI9341_POWER2               0xC1   /* Power Control 2 register */
#define ILI9341_VCOM1                0xC5   /* VCOM Control 1 register */
#define ILI9341_VCOM2                0xC7   /* VCOM Control 2 register */
#define ILI9341_POWERA               0xCB   /* Power control A register */
#define ILI9341_POWERB               0xCF   /* Power control B register */
#define ILI9341_PGAMMA               0xE0   /* Positive Gamma Correction
					     * register
					     */
#define ILI9341_NGAMMA               0xE1   /* Negative Gamma Correction
					     * register
					     */
#define ILI9341_DTCA                 0xE8   /* Driver timing control A */
#define ILI9341_DTCB                 0xEA   /* Driver timing control B */
#define ILI9341_POWER_SEQ            0xED   /* Power on sequence register */
#define ILI9341_3GAMMA_EN            0xF2   /* 3 Gamma enable register */
#define ILI9341_INTERFACE            0xF6   /* Interface control register */
#define ILI9341_PRC                  0xF7   /* Pump ratio control register */

static u8 ili9341_cmd0[] = {0xca, 0xc3, 0x08, 0x50};
static u8 ili9341_powerb[] = {ILI9341_POWERB, 0x00, 0xc1, 0x30};
static u8 ili9341_power_seq[] = {ILI9341_POWER_SEQ, 0x64, 0x03, 0x12, 0x81};
static u8 ili9341_dtca[] = {ILI9341_DTCA, 0x85, 0x00, 0x78};
static u8 ili9341_powera[] = {ILI9341_POWERA, 0x39, 0x2c, 0x00, 0x34, 0x02};
static u8 ili9341_prc[] = {ILI9341_PRC, 0x20};
static u8 ili9341_dtcb[] = {ILI9341_DTCB, 0x00, 0x00};
static u8 ili9341_frc[] = {ILI9341_FRC, 0x00, 0x1b};
static u8 ili9341_dfc1[] = {ILI9341_DFC, 0x0a, 0xa2};
static u8 ili9341_power1[] = {ILI9341_POWER1, 0x10};
static u8 ili9341_power2[] = {ILI9341_POWER2, 0x10};
static u8 ili9341_vcom1[] = {ILI9341_VCOM1, 0x45, 0x15};
static u8 ili9341_vcom2[] = {ILI9341_VCOM2, 0x90};
static u8 ili9341_mac[] = {ILI9341_MAC, 0xc8};
static u8 ili9341_gamma_en[] = {ILI9341_3GAMMA_EN, 0x00};
static u8 ili9341_rgb_intr[] = {ILI9341_RGB_INTERFACE, 0xc2};
static u8 ili9341_dfc2[] = {ILI9341_DFC, 0x0a, 0xa7, 0x27, 0x04};
static u8 ili9341_column_addr[] = {ILI9341_COLUMN_ADDR, 0x00, 0x00, 0x00,
					0xef};
static u8 ili9341_page_addr[] = {ILI9341_PAGE_ADDR, 0x00, 0x00, 0x01, 0x3f};
static u8 ili9341_intr[] = {ILI9341_INTERFACE, 0x01, 0x00, 0x06};
static u8 ili9341_gamma[] = {ILI9341_GAMMA, 0x01};
static u8 ili9341_pgamma[] = {ILI9341_PGAMMA, 0x0f, 0x29, 0x24, 0x0c, 0x0e,
				0x09, 0x4e, 0x78,
				0x3c, 0x09, 0x13, 0x05,	0x17, 0x11, 0x00};
static u8 ili9341_ngamma[] = {ILI9341_NGAMMA, 0x00, 0x16, 0x1b, 0x04, 0x11,
				0x07, 0x31, 0x33,
				0x42, 0x05, 0x0c, 0x0a, 0x28, 0x2f, 0x0f};
static u8 ili9341_gram[] = {ILI9341_GRAM};
static u8 ili9341_sleep_out[] = {ILI9341_SLEEP_OUT};
static u8 ili9341_display_on[] = {ILI9341_DISPLAY_ON};
static u8 ili9341_display_off[] = {ILI9341_DISPLAY_OFF};

/**
 * enum ili9341_input - the format of the incoming signal to the panel
 *
 * The panel can be connected to various input streams and four of them can
 * be selected by electronic straps on the display. However it is possible
 * to select another mode or override the electronic default with this
 * setting.
 */
enum ili9341_input {
	ILI9341_INPUT_PRGB_THROUGH = 0x0,
	ILI9341_INPUT_PRGB_ALIGNED = 0x1,
	ILI9341_INPUT_UNKNOWN = 0xf,
};

/**
 * struct ili9341_config - the system specific ILI9341 configuration
 * @width_mm: physical panel width [mm]
 * @height_mm: physical panel height [mm]
 * @input: the input/entry type used in this system, if this is set to
 * ILI9341_INPUT_UNKNOWN the driver will try to figure it out by probing
 * the hardware
 * @dclk_active_high: data/pixel clock active high, data will be clocked
 * in on the rising edge of the DCLK (this is usually the case).
 * @de_active_high: DE (data entry) is active high
 * @hsync_active_high: HSYNC is active high
 * @vsync_active_high: VSYNC is active high
 */
struct ili9341_config {
	u32 width_mm;
	u32 height_mm;
	enum ili9341_input input;
	bool dclk_active_high;
	bool de_active_high;
	bool hsync_active_high;
	bool vsync_active_high;
};

struct ili9341 {
	struct device *dev;
	const struct ili9341_config *conf;
	struct drm_panel panel;
	struct gpio_desc *reset_gpio;
	struct gpio_desc *dc_gpio;
	enum ili9341_input input;
	struct videomode vm;
};

static inline struct ili9341 *panel_to_ili9341(struct drm_panel *panel)
{
	return container_of(panel, struct ili9341, panel);
}

int ili9341_spi_transfer(struct spi_device *spi, u32 speed_hz,
			  u8 bpw, const void *buf, size_t len)
{
	size_t max_chunk = spi_max_transfer_size(spi);
	struct spi_transfer tr = {
		.bits_per_word = bpw,
		.speed_hz = speed_hz,
		.len = len,
	};
	struct spi_message m;
	size_t chunk;
	int ret;

	spi_message_init_with_transfers(&m, &tr, 1);

	while (len) {
		chunk = min(len, max_chunk);

		tr.tx_buf = buf;
		tr.len = chunk;
		buf += chunk;
		len -= chunk;

		ret = spi_sync(spi, &m);
		if (ret)
			return ret;
	}
	return 0;
}
static int ili9341_spi_write(struct ili9341 *ili, const void *data,
				    size_t count)
{
	struct spi_device *spi = to_spi_device(ili->dev);
	int ret = 0;

	gpiod_set_value_cansleep(ili->dc_gpio, 0);

	ret = ili9341_spi_transfer(spi, DEFAULT_SPI_SPEED, 8, data+0, 1);
	if (ret || count == 1)
		return ret;

	gpiod_set_value_cansleep(ili->dc_gpio, 1);

	return ili9341_spi_transfer(spi, DEFAULT_SPI_SPEED, 8,
		data+1, count-1);
}

static int ili9341_init(struct ili9341 *ili)
{
	ili9341_spi_write(ili, ili9341_cmd0,
				sizeof(ili9341_cmd0));
	ili9341_spi_write(ili, ili9341_powerb,
				sizeof(ili9341_powerb));
	ili9341_spi_write(ili, ili9341_power_seq,
				sizeof(ili9341_power_seq));
	ili9341_spi_write(ili, ili9341_dtca,
				sizeof(ili9341_dtca));
	ili9341_spi_write(ili, ili9341_powera,
				sizeof(ili9341_powera));
	ili9341_spi_write(ili, ili9341_prc,
				sizeof(ili9341_prc));
	ili9341_spi_write(ili, ili9341_dtcb,
				sizeof(ili9341_dtcb));
	ili9341_spi_write(ili, ili9341_frc,
				sizeof(ili9341_frc));
	ili9341_spi_write(ili, ili9341_dfc1,
				sizeof(ili9341_dfc1));
	ili9341_spi_write(ili, ili9341_power1,
				sizeof(ili9341_power1));
	ili9341_spi_write(ili, ili9341_power2,
				sizeof(ili9341_power2));
	ili9341_spi_write(ili, ili9341_vcom1,
				sizeof(ili9341_vcom1));
	ili9341_spi_write(ili, ili9341_vcom2,
				sizeof(ili9341_vcom2));
	ili9341_spi_write(ili, ili9341_mac,
				sizeof(ili9341_mac));
	ili9341_spi_write(ili, ili9341_gamma_en,
				sizeof(ili9341_gamma_en));
	ili9341_spi_write(ili, ili9341_rgb_intr,
				sizeof(ili9341_rgb_intr));
	ili9341_spi_write(ili, ili9341_dfc2,
				sizeof(ili9341_dfc2));
	ili9341_spi_write(ili, ili9341_column_addr,
				sizeof(ili9341_column_addr));
	ili9341_spi_write(ili, ili9341_page_addr,
				sizeof(ili9341_page_addr));
	ili9341_spi_write(ili, ili9341_intr,
				sizeof(ili9341_intr));
	ili9341_spi_write(ili, ili9341_gram,
				sizeof(ili9341_gram));
	msleep(200);
	ili9341_spi_write(ili, ili9341_gamma,
				sizeof(ili9341_gamma));
	ili9341_spi_write(ili, ili9341_pgamma,
				sizeof(ili9341_pgamma));
	ili9341_spi_write(ili, ili9341_ngamma,
				sizeof(ili9341_ngamma));
	ili9341_spi_write(ili, ili9341_sleep_out,
				sizeof(ili9341_sleep_out));
	msleep(200);
	ili9341_spi_write(ili, ili9341_display_on,
				sizeof(ili9341_display_on));
	ili9341_spi_write(ili, ili9341_gram,
				sizeof(ili9341_gram));

	dev_info(ili->dev, "initialized display\n");

	return 0;
}

/*
 * This power-on sequence if from the datasheet, page 57.
 */
static int ili9341_power_on(struct ili9341 *ili)
{
	/* Assert RESET */
	gpiod_set_value(ili->reset_gpio, 1);

	msleep(20);

	/* De-assert RESET */
	gpiod_set_value(ili->reset_gpio, 0);

	msleep(10);

	return 0;
}

static int ili9341_power_off(struct ili9341 *ili)
{
	return 0;
}

static int ili9341_disable(struct drm_panel *panel)
{
	struct ili9341 *ili = panel_to_ili9341(panel);

	ili9341_spi_write(ili, ili9341_display_off,
				sizeof(ili9341_display_off));

	return 0;
}

static int ili9341_unprepare(struct drm_panel *panel)
{
	struct ili9341 *ili = panel_to_ili9341(panel);

	return ili9341_power_off(ili);
}

static int ili9341_prepare(struct drm_panel *panel)
{
	struct ili9341 *ili = panel_to_ili9341(panel);
	int ret;

	ret = ili9341_power_on(ili);
	if (ret < 0)
		return ret;

	ret = ili9341_init(ili);
	if (ret < 0)
		ili9341_unprepare(panel);

	return ret;
}

static int ili9341_enable(struct drm_panel *panel)
{
	struct ili9341 *ili = panel_to_ili9341(panel);

	ili9341_spi_write(ili, ili9341_display_on,
				sizeof(ili9341_display_on));

	return 0;
}

/* This is the only mode listed for parallel RGB in the datasheet */
static const struct drm_display_mode prgb_320x240_mode = {
	.clock = 6100,
	.hdisplay = 240,
	.hsync_start = 240 + 10,
	.hsync_end = 240 + 10 + 10,
	.htotal = 280,
	.vdisplay = 320,
	.vsync_start = 320 + 4,
	.vsync_end = 320 + 4 + 2,
	.vtotal = 328,
	.vrefresh = 60,
	.flags = 0,
};

static int ili9341_get_modes(struct drm_panel *panel,
				struct drm_connector *connector)
{
	struct ili9341 *ili = panel_to_ili9341(panel);
	struct drm_device *drm = connector->dev;
	struct drm_display_mode *mode;
	struct drm_display_info *info;

	info = &connector->display_info;
	info->width_mm = ili->conf->width_mm;
	info->height_mm = ili->conf->height_mm;
	if (ili->conf->dclk_active_high)
		info->bus_flags |= DRM_BUS_FLAG_PIXDATA_DRIVE_POSEDGE;
	else
		info->bus_flags |= DRM_BUS_FLAG_PIXDATA_DRIVE_NEGEDGE;

	if (ili->conf->de_active_high)
		info->bus_flags |= DRM_BUS_FLAG_DE_HIGH;
	else
		info->bus_flags |= DRM_BUS_FLAG_DE_LOW;

	switch (ili->input) {
	case ILI9341_INPUT_PRGB_THROUGH:
	case ILI9341_INPUT_PRGB_ALIGNED:
		mode = drm_mode_duplicate(drm, &prgb_320x240_mode);
		break;
	default:
		mode = NULL;
		break;
	}
	if (!mode) {
		DRM_ERROR("bad mode or failed to add mode\n");
		return -EINVAL;
	}
	drm_mode_set_name(mode);
	/*
	 * This is the preferred mode because most people are going
	 * to want to use the display with VGA type graphics.
	 */
	mode->type = DRM_MODE_TYPE_DRIVER | DRM_MODE_TYPE_PREFERRED;

	/* Set up the polarity */
	if (ili->conf->hsync_active_high)
		mode->flags |= DRM_MODE_FLAG_PHSYNC;
	else
		mode->flags |= DRM_MODE_FLAG_NHSYNC;
	if (ili->conf->vsync_active_high)
		mode->flags |= DRM_MODE_FLAG_PVSYNC;
	else
		mode->flags |= DRM_MODE_FLAG_NVSYNC;

	mode->width_mm = ili->conf->width_mm;
	mode->height_mm = ili->conf->height_mm;
	drm_mode_probed_add(connector, mode);

	return 1; /* Number of modes */
}

static const struct drm_panel_funcs ili9341_drm_funcs = {
	.disable = ili9341_disable,
	.unprepare = ili9341_unprepare,
	.prepare = ili9341_prepare,
	.enable = ili9341_enable,
	.get_modes = ili9341_get_modes,
};

static int ili9341_probe(struct spi_device *spi)
{
	struct device *dev = &spi->dev;
	struct ili9341 *ili;
	int ret;

	ili = devm_kzalloc(dev, sizeof(struct ili9341), GFP_KERNEL);
	if (!ili)
		return -ENOMEM;

	spi_set_drvdata(spi, ili);

	ili->dev = dev;
	/*
	 * Every new incarnation of this display must have a unique
	 * data entry for the system in this driver.
	 */
	ili->conf = of_device_get_match_data(dev);
	if (!ili->conf) {
		dev_err(dev, "missing device configuration\n");
		return -ENODEV;
	}

	ili->reset_gpio = devm_gpiod_get_optional(dev, "reset",
		GPIOD_OUT_HIGH);
	if (IS_ERR(ili->reset_gpio)) {
		dev_err(dev, "failed to get RESET GPIO\n");
		return PTR_ERR(ili->reset_gpio);
	}

	ili->dc_gpio = devm_gpiod_get_optional(dev, "dc", GPIOD_OUT_LOW);
	if (IS_ERR(ili->dc_gpio)) {
		dev_err(dev, "failed to get DC GPIO\n");
		return PTR_ERR(ili->dc_gpio);
	}

	spi->bits_per_word = 8;
	ret = spi_setup(spi);
	if (ret < 0) {
		dev_err(dev, "spi setup failed.\n");
		return ret;
	}

	ili->input = ili->conf->input;

	drm_panel_init(&ili->panel, dev, &ili9341_drm_funcs,
		       DRM_MODE_CONNECTOR_DPI);

	return drm_panel_add(&ili->panel);
}

static int ili9341_remove(struct spi_device *spi)
{
	struct ili9341 *ili = spi_get_drvdata(spi);

	ili9341_power_off(ili);
	drm_panel_remove(&ili->panel);

	return 0;
}

/*
 * The Stm32f429-disco board has a panel ili9341 connected to ltdc controller
 */
static const struct ili9341_config ili9341_data = {
	.width_mm = 65,
	.height_mm = 50,
	.input = ILI9341_INPUT_PRGB_THROUGH,
	.dclk_active_high = true,
	.de_active_high = false,
	.hsync_active_high = false,
	.vsync_active_high = false,
};

static const struct of_device_id ili9341_of_match[] = {
	{
		.compatible = "stm32f429-disco,ltdc-panel",
		.data = &ili9341_data,
	},
	{
		.compatible = "ilitek,ili9341",
		.data = NULL,
	},
	{ }
};
MODULE_DEVICE_TABLE(of, ili9341_of_match);

static struct spi_driver ili9341_driver = {
	.probe = ili9341_probe,
	.remove = ili9341_remove,
	.driver = {
		.name = "panel-ilitek-ili9341",
		.of_match_table = ili9341_of_match,
	},
};
module_spi_driver(ili9341_driver);

MODULE_AUTHOR("Dillon Min <dillon.minfei@gmail.com>");
MODULE_DESCRIPTION("ILI9341 LCD panel driver");
MODULE_LICENSE("GPL v2");
