// SPDX-License-Identifier: GPL-2.0-or-later
/*
 * Copyright (c) 2022, Ramin <raminterex@yahoo.com>
 * Copyright (c) 2022, Svyatoslav Ryhel <clamor95@gmail.com>
 */

#include <command.h>
#include <log.h>
#include <vsprintf.h>
#include <linux/string.h>
#include <asm/arch-tegra/crypto.h>
#include <asm/arch-tegra/fuse.h>
#include "bct.h"
#include "uboot_aes.h"

int bct_patch(u8 *bct, u8 *ebt, u32 ebt_size)
{
	struct nvboot_config_table *bct_tbl = (struct nvboot_config_table *)bct;
	bool encrypted;
	int ret;

	bct += UBCT_LENGTH;

	ebt_size = roundup(ebt_size, EBT_ALIGNMENT);

	ret = sign_data_block(ebt, ebt_size, (u8 *)bct_tbl->bootloader[0].crypto_hash);
	if (ret)
		return 1;

	bct_tbl->bootloader[0].entry_point = CONFIG_SPL_TEXT_BASE;
	bct_tbl->bootloader[0].load_addr = CONFIG_SPL_TEXT_BASE;
	bct_tbl->bootloader[0].length = ebt_size;

	encrypted = tegra_fuse_get_operation_mode() == MODE_ODM_PRODUCTION_SECURE;
	if (encrypted) {
		ret = encrypt_data_block(bct, bct, SBCT_LENGTH);
		if (ret)
			return 1;
	}

	ret = sign_data_block(bct, SBCT_LENGTH, (u8 *)bct_tbl->crypto_hash);
	if (ret)
		return 1;

	return 0;

}

#ifdef CONFIG_CMD_EBTUPDATE
static int do_ebtupdate(struct cmd_tbl *cmdtp, int flag, int argc,
			char *const argv[])
{
	u8 *bct = (u8 *)hextoul(argv[1], NULL);
	u8 *ebt = (u8 *)hextoul(argv[2], NULL);
	u32 ebt_size = hextoul(argv[3], NULL);
	bool encrypted;
	int ret;

	ebt_size = roundup(ebt_size, EBT_ALIGNMENT);
	encrypted = tegra_fuse_get_operation_mode() == MODE_ODM_PRODUCTION_SECURE;

	if (encrypted) {
		ret = decrypt_data_block(bct, bct, SBCT_LENGTH);
		if (ret)
			return 1;

		ret = encrypt_data_block(ebt, ebt, ebt_size);
		if (ret)
			return 1;
	}

	return bct_patch(bct, ebt, ebt_size);
}

U_BOOT_CMD(ebtupdate,	4,	0,	do_ebtupdate,
	   "update bootloader on re-crypted Tegra devices",
	   ""
);
#endif
