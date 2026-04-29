// SPDX-License-Identifier: GPL-2.0+
/*
 * Copyright (c) 2026, Ion Agorria <ion@agorria.com>
 * Copyright (c) 2026, Svyatoslav Ryhel <clamor95@gmail.com>
 */

#include <dm.h>
#include <blk.h>
#include <command.h>
#include <fastboot.h>
#include <fs.h>
#include <log.h>
#include <mmc.h>
#include <spi.h>
#include <spi_flash.h>
#include <stdlib.h>
#include <string.h>
#include <linux/delay.h>
#include <linux/printk.h>

#include <asm/arch-tegra/crypto.h>
#include <asm/arch-tegra/fuse.h>
#include <asm/arch-tegra/pmc.h>

#include "bct.h"

int board_fastboot_flash(char *cmd_parameter, void *download_buffer,
			 u32 download_bytes, char *response)
{
	struct tegra_boot_update_context ctx;
	bool is_bct = !strcmp(cmd_parameter, "bct");
	bool is_bootloader = !strcmp(cmd_parameter, "ebt") ||
			     !strcmp(cmd_parameter, "bootloader");
	int ret = 0;

	if (!is_bct && !is_bootloader)
		/* Continue normal fastboot code */
		return 0;

	if (is_bct)
		ret = tegra_boot_flash_bct(&ctx, (struct nvboot_config_table *)download_buffer,
					   download_bytes);
	else
		ret = tegra_boot_flash_bootloader(&ctx, download_buffer,
						  download_bytes);

	if (ret) {
		if (strlen(ctx.error))
			fastboot_fail(ctx.error, response);
		else
			fastboot_fail("unknown error flashing", response);

		return ret;
	}

	fastboot_okay(NULL, response);

	return 1;
}

int board_fastboot_get_part_info(const char *part_name, char *response,
				 size_t *size)
{
	if (!strcmp(part_name, "bct")) {
		if (size)
			*size = BCT_LENGTH;
		return 0;
	} else if (!strcmp(part_name, "ebt") ||
		   !strcmp(part_name, "bootloader")) {
		if (size)
			*size = EBT_MAX_LENGTH;
		return 0;
	}

	return 1;
}
