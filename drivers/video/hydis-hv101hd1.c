// SPDX-License-Identifier: GPL-2.0+
/*
 * Hydis HV101HD1 panel driver
 *
 * Copyright (c) 2023 Svyatoslav Ryhel <clamor95@gmail.com>
 */

#define LOG_DEBUG

#include <backlight.h>
#include <dm.h>
#include <panel.h>
#include <log.h>
#include <mipi_dsi.h>
#include <asm/gpio.h>
#include <linux/delay.h>
#include <power/regulator.h>

static const char * const hv101hd1_supplies[] = {
	"vdd-supply", "vio-supply"
};

struct hv101hd1_priv {
	struct udevice *backlight;
	struct udevice *supplies[ARRAY_SIZE(hv101hd1_supplies)];
};

static struct display_timing default_timing = {
	.pixelclock.typ		= 72000000,
	.hactive.typ		= 1366,
	.hfront_porch.typ	= 74,
	.hback_porch.typ	= 24,
	.hsync_len.typ		= 36,
	.vactive.typ		= 768,
	.vfront_porch.typ	= 21,
	.vback_porch.typ	= 4,
	.vsync_len.typ		= 7,
};

static int hv101hd1_hw_init(struct udevice *dev)
{
	struct hv101hd1_priv *priv = dev_get_priv(dev);
	int i, ret;

	for (i = 0; i < ARRAY_SIZE(hv101hd1_supplies); i++) {
		ret = regulator_set_enable_if_allowed(priv->supplies[i], 1);
		if (ret) {
			log_debug("%s: cannot enable %s %d\n", __func__,
				  hv101hd1_supplies[i], ret);
			return ret;
		}
	}
	mdelay(5);

	return 0;
}

static int hv101hd1_enable_backlight(struct udevice *dev)
{
	return hv101hd1_hw_init(dev);
}

static int hv101hd1_set_backlight(struct udevice *dev, int percent)
{
	struct hv101hd1_priv *priv = dev_get_priv(dev);
	struct mipi_dsi_panel_plat *plat = dev_get_plat(dev);
	struct mipi_dsi_device *dsi = plat->device;
	int ret;

	ret = mipi_dsi_dcs_exit_sleep_mode(dsi);
	if (ret) {
		log_debug("%s: failed to exit sleep mode: %d\n", __func__, ret);
		return ret;
	}
	mdelay(20);

	ret = mipi_dsi_dcs_set_display_on(dsi);
	if (ret) {
		log_debug("%s: failed to set display on: %d\n", __func__, ret);
		return ret;
	}
	mdelay(20);

	ret = backlight_enable(priv->backlight);
	if (ret)
		return ret;

	return backlight_set_brightness(priv->backlight, percent);
}

static int hv101hd1_timings(struct udevice *dev, struct display_timing *timing)
{
	memcpy(timing, &default_timing, sizeof(*timing));
	return 0;
}

static int hv101hd1_of_to_plat(struct udevice *dev)
{
	struct hv101hd1_priv *priv = dev_get_priv(dev);
	int i, ret;

	ret = uclass_get_device_by_phandle(UCLASS_PANEL_BACKLIGHT, dev,
					   "backlight", &priv->backlight);
	if (ret) {
		log_debug("%s: cannot get backlight: ret = %d\n", __func__, ret);
		return ret;
	}

	for (i = 0; i < ARRAY_SIZE(hv101hd1_supplies); i++) {
		ret = device_get_supply_regulator(dev, hv101hd1_supplies[i],
						  &priv->supplies[i]);
		if (ret) {
			log_debug("%s: cannot get %s %d\n", __func__,
				  hv101hd1_supplies[i], ret);
			if (ret != -ENOENT)
				return log_ret(ret);
		}
	}

	return 0;
}

static int hv101hd1_probe(struct udevice *dev)
{
	struct mipi_dsi_panel_plat *plat = dev_get_plat(dev);

	/* fill characteristics of DSI data link */
	plat->lanes = 2;
	plat->format = MIPI_DSI_FMT_RGB888;
	plat->mode_flags = MIPI_DSI_MODE_VIDEO | MIPI_DSI_MODE_LPM;

	return 0;//hv101hd1_hw_init(dev);
}

static const struct panel_ops hv101hd1_ops = {
	.enable_backlight	= hv101hd1_enable_backlight,
	.set_backlight		= hv101hd1_set_backlight,
	.get_display_timing	= hv101hd1_timings,
};

static const struct udevice_id hv101hd1_ids[] = {
	{ .compatible = "hydis,hv101hd1" },
	{ }
};

U_BOOT_DRIVER(hydis_hv101hd1) = {
	.name		= "hydis_hv101hd1",
	.id		= UCLASS_PANEL,
	.of_match	= hv101hd1_ids,
	.ops		= &hv101hd1_ops,
	.of_to_plat	= hv101hd1_of_to_plat,
	.probe		= hv101hd1_probe,
	.plat_auto	= sizeof(struct mipi_dsi_panel_plat),
	.priv_auto	= sizeof(struct hv101hd1_priv),
};
