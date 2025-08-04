// SPDX-License-Identifier: GPL-2.0+
/*
 *  (C) Copyright 2010-2013
 *  NVIDIA Corporation <www.nvidia.com>
 *
 *  (C) Copyright 2022
 *  Svyatoslav Ryhel <clamor95@gmail.com>
 */

#include <dm.h>
#include <fdt_support.h>
#include <asm/arch/clock.h>
#include <asm/arch-tegra/fuse.h>
#include <asm/arch/gpio.h>
#include <asm/gpio.h>
#include <i2c.h>
#include <linux/delay.h>

//#define RESET_GPIO	TEGRA_GPIO(BB, 5)
//#define VIO_FRONT	TEGRA_GPIO(Y, 2)
#define EN_CAM_PMIC	TEGRA_GPIO(BB, 4)

int nvidia_board_init(void)
{
	struct udevice *lp8720;
	int ret;

	ret = i2c_get_chip_for_busnum(2, 0x7d, 1, &lp8720);
	if (ret)
		debug("%s: Cannot find LP8720 I2C chip\n", __func__);

//	gpio_request(VIO_FRONT, "cam1_ldo_en");
//	gpio_direction_output(VIO_FRONT, 0);

//	gpio_request(RESET_GPIO, "rst_lo");
//	gpio_direction_output(RESET_GPIO, 1);

	gpio_request(EN_CAM_PMIC, "cam_pmic");
	gpio_direction_output(EN_CAM_PMIC, 1);
	gpio_set_value(EN_CAM_PMIC, 1);

//	gpio_set_value(RESET_GPIO, 0);
//	gpio_set_value(VIO_FRONT, 1);

	mdelay(5);

	dm_i2c_reg_write(lp8720, 0x1, 0x0c | 0xe0); /* 1.8v | ON */ // 0x11?? - 2.2v
	dm_i2c_reg_write(lp8720, 0x2, 0x19 | 0xe0); /* 2.8v | ON */
	dm_i2c_reg_write(lp8720, 0x8, 0x80 | BIT(0) | BIT(1)); /* enable ldo1 and ldo2 */

//	gpio_set_value(RESET_GPIO, 1);

	// cam_mclk pinmux is from tree, csi clock is not needed
	// PCC0 gate is irrel (cam_mclk)
	// sd3 > ldo7 (dsi avdd) is irrel to cam
	// vi_sensor and susout clocks are set in the kernel

	/* Set up panel bridge clocks */
	clock_start_periph_pll(PERIPH_ID_EXTPERIPH3, CLOCK_ID_PERIPH,
			       24 * 1000000);
	clock_external_output(3);

	return 0;
}

#if defined(CONFIG_OF_LIBFDT) && defined(CONFIG_OF_BOARD_SETUP)
int ft_board_setup(void *blob, struct bd_info *bd)
{
	/* First 3 bytes refer to LG vendor */
	u8 btmacaddr[6] = { 0x00, 0x00, 0x00, 0xD0, 0xC9, 0x88 };

	/* Generate device 3 bytes based on chip sd */
	u64 bt_device = tegra_chip_uid() >> 24ull;

	btmacaddr[0] =  (bt_device >> 1 & 0x0F) |
			(bt_device >> 5 & 0xF0);
	btmacaddr[1] =  (bt_device >> 11 & 0x0F) |
			(bt_device >> 17 & 0xF0);
	btmacaddr[2] =  (bt_device >> 23 & 0x0F) |
			(bt_device >> 29 & 0xF0);

	/* Set BT MAC address */
	fdt_find_and_setprop(blob, "/serial@70006200/bluetooth",
			     "local-bd-address", btmacaddr, 6, 1);

	/* Remove TrustZone nodes */
	fdt_del_node_and_alias(blob, "/firmware");
	fdt_del_node_and_alias(blob, "/reserved-memory/trustzone@bfe00000");

	return 0;
}
#endif
