// SPDX-License-Identifier: GPL-2.0+
/*
 * Sony Tablet P DSI panel driver
 *
 * Copyright (c) 2025 Svyatoslav Ryhel <clamor95@gmail.com>
 */

#include <backlight.h>
#include <dm.h>
#include <i2c.h>
#include <panel.h>
#include <log.h>
#include <mipi_dsi.h>
#include <linux/delay.h>
#include <power/regulator.h>
#include <asm/gpio.h>

#define DEBUG

struct sony_tablet_p_priv {
	struct udevice *panel_link;

	struct udevice *backlight;

	struct udevice *vdd;
	struct udevice *vio;

	struct gpio_desc reset_gpio;
};

static struct display_timing sony_tablet_p_timing = {
	.pixelclock.typ		= 48000000,
	.hactive.typ		= 1024,
	.hfront_porch.typ	= 23,
	.hback_porch.typ	= 16,
	.hsync_len.typ		= 8,
	.vactive.typ		= 960,
	.vfront_porch.typ	= 11,
	.vback_porch.typ	= 0,
	.vsync_len.typ		= 1,
	.flags			= DISPLAY_FLAGS_HSYNC_LOW | DISPLAY_FLAGS_VSYNC_LOW,
};

static int sony_tablet_p_enable_backlight(struct udevice *dev)
{
	struct sony_tablet_p_priv *priv = dev_get_priv(dev);

	if (!priv->panel_link)
		return 0;

	struct sony_tablet_p_priv *link_priv = dev_get_priv(priv->panel_link);
	u8 buff[4] = { 0x10, 0x14, 0x53, 0xe3 };
	int ret;

	ret = dm_gpio_set_value(&priv->reset_gpio, 0);
	if (ret) {
		log_debug("reset-gpio link0 disable failed (%d)\n", ret);
		return ret;
	}

	ret = dm_gpio_set_value(&link_priv->reset_gpio, 0);
	if (ret) {
		log_debug("reset-gpio link1 disable failed (%d)\n", ret);
		return ret;
	}

	mdelay(20);

#if 0
	// panel on
	dm_i2c_reg_write(dev, 0x36, 0x00);
	dm_i2c_reg_write(priv->panel_link, 0x36, 0x00);

	dm_i2c_reg_write(dev, 0x3a, 0x60);
	dm_i2c_reg_write(priv->panel_link, 0x3a, 0x60);

	dm_i2c_reg_write(dev, 0xc6, MIPI_DCS_EXIT_SLEEP_MODE);
	dm_i2c_reg_write(priv->panel_link, 0xc6, MIPI_DCS_ENTER_INVERT_MODE);
#endif

	// panel resume

	dm_i2c_reg_write(dev, 0xb0, 0x04);
	dm_i2c_reg_write(priv->panel_link, 0xb0, 0x04);

	dm_i2c_write(dev, 0xd1, buff, 4);
	dm_i2c_write(priv->panel_link, 0xd1, buff, 4);

	dm_i2c_reg_write(dev, 0xb0, 0x03);
	dm_i2c_reg_write(priv->panel_link, 0xb0, 0x03);

	dm_i2c_reg_write(dev, 0x36, 0x00);
	dm_i2c_reg_write(priv->panel_link, 0x36, 0x00);

	dm_i2c_reg_write(dev, 0x3a, 0x60);
	dm_i2c_reg_write(priv->panel_link, 0x3a, 0x60);

	dm_i2c_reg_write(dev, 0xc6, MIPI_DCS_EXIT_SLEEP_MODE);
	dm_i2c_reg_write(priv->panel_link, 0xc6, MIPI_DCS_ENTER_INVERT_MODE);

	dm_i2c_reg_write(dev, MIPI_DCS_SET_DISPLAY_ON, 0x00);
	dm_i2c_reg_write(priv->panel_link, MIPI_DCS_SET_DISPLAY_ON, 0x00);

	dm_i2c_reg_write(dev, MIPI_DCS_EXIT_SLEEP_MODE, 0x00);
	dm_i2c_reg_write(priv->panel_link, MIPI_DCS_EXIT_SLEEP_MODE, 0x00);

	return 0;
}

static int sony_tablet_p_set_backlight(struct udevice *dev, int percent)
{
	struct sony_tablet_p_priv *priv = dev_get_priv(dev);
	int ret;

	ret = backlight_enable(priv->backlight);
	if (ret)
		return ret;

	ret = backlight_set_brightness(priv->backlight, percent);
	if (ret)
		return ret;

	if (priv->panel_link)
		return sony_tablet_p_set_backlight(priv->panel_link, percent);
	
	return 0;
}

static int sony_tablet_p_timings(struct udevice *dev, struct display_timing *timing)
{
	memcpy(timing, &sony_tablet_p_timing, sizeof(*timing));

	return 0;
}

static int sony_tablet_p_of_to_plat(struct udevice *dev)
{
	struct sony_tablet_p_priv *priv = dev_get_priv(dev);
	int ret;

	ret = uclass_get_device_by_phandle(UCLASS_PANEL_BACKLIGHT, dev,
					   "backlight", &priv->backlight);
	if (ret) {
		log_debug("%s: cannot get backlight: ret = %d\n",
			  __func__, ret);
		return ret;
	}

	ret = uclass_get_device_by_phandle(UCLASS_REGULATOR, dev,
					   "vdd-supply", &priv->vdd);
	if (ret) {
		log_debug("%s: cannot get vdd-supply: ret = %d\n",
			  __func__, ret);
		return ret;
	}

	ret = uclass_get_device_by_phandle(UCLASS_REGULATOR, dev,
					   "vio-supply", &priv->vio);
	if (ret) {
		log_debug("%s: cannot get vio-supply: ret = %d\n",
			  __func__, ret);
		return ret;
	}

	ret = gpio_request_by_name(dev, "reset-gpios", 0,
				   &priv->reset_gpio, GPIOD_IS_OUT);
	if (ret) {
		log_debug("could not decode reset-gpios (%d)\n", ret);
		return ret;
	}

	ret = dm_gpio_set_value(&priv->reset_gpio, 1);
	if (ret) {
		log_debug("reset-gpio enable failed (%d)\n", ret);
		return ret;
	}

	/* If node has no link2 it is secondary panel */
	if (!dev_read_bool(dev, "link2"))
		return 0;

	ret = uclass_get_device_by_phandle(UCLASS_PANEL, dev,
					   "link2", &priv->panel_link);
	if (ret) {
		log_debug("%s: cannot get secondary panel: ret = %d\n",
			  __func__, ret);
		return ret;
	}

	return 0;
}

static int sony_tablet_p_hw_init(struct udevice *dev)
{
	struct sony_tablet_p_priv *priv = dev_get_priv(dev);
	int ret;

	ret = regulator_set_enable_if_allowed(priv->vio, 1);
	if (ret) {
		log_debug("%s: enabling vio-supply failed (%d)\n",
			  __func__, ret);
		return ret;
	}

	ret = regulator_set_enable_if_allowed(priv->vdd, 1);
	if (ret) {
		log_debug("%s: enabling vdd-supply failed (%d)\n",
			  __func__, ret);
		return ret;
	}

	mdelay(10);

	return 0;
}

static int sony_tablet_p_probe(struct udevice *dev)
{
	struct mipi_dsi_panel_plat *plat = dev_get_plat(dev);

	/* fill characteristics of DSI data link */
	plat->lanes = 2; // arbitrary, exact amount is not known
	plat->format = MIPI_DSI_FMT_RGB888;
	plat->mode_flags = MIPI_DSI_MODE_LPM;

	return sony_tablet_p_hw_init(dev);
}

static const struct panel_ops sony_tablet_p_ops = {
	.enable_backlight	= sony_tablet_p_enable_backlight,
	.set_backlight		= sony_tablet_p_set_backlight,
	.get_display_timing	= sony_tablet_p_timings,
};

static const struct udevice_id sony_tablet_p_ids[] = {
	{ .compatible = "sony,tablet-p-panel" },
	{ }
};

U_BOOT_DRIVER(sony_tablet_p) = {
	.name		= "sony_tablet_p",
	.id		= UCLASS_PANEL,
	.of_match	= sony_tablet_p_ids,
	.ops		= &sony_tablet_p_ops,
	.of_to_plat	= sony_tablet_p_of_to_plat,
	.probe		= sony_tablet_p_probe,
	.plat_auto	= sizeof(struct mipi_dsi_panel_plat),
	.priv_auto	= sizeof(struct sony_tablet_p_priv),
};
