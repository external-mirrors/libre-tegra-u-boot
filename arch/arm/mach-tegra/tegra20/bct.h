/* SPDX-License-Identifier: GPL-2.0+ */

#ifndef _TEGRA20_BCT_H_
#define _TEGRA20_BCT_H_

/*
 * Defines the BCT parametres for T20
 */
#define UBCT_LENGTH		0x10
#define SBCT_LENGTH		0xFE0

struct nv_bootloader_info {
	u32 version;
	u32 start_blk;
	u32 start_page;
	u32 length;
	u32 load_addr;
	u32 entry_point;
	u32 attribute;
	u32 crypto_hash[NVBOOT_CMAC_AES_HASH_LENGTH];
};

struct nvboot_config_table {
	u32 crypto_hash[NVBOOT_CMAC_AES_HASH_LENGTH];
	u32 unused0[4];
	u32 boot_data_version;
	u32 unused1[668];
	struct nv_bootloader_info bootloader[NVBOOT_MAX_BOOTLOADERS];
	u32 unused2[508];
};

#endif /* _TEGRA20_BCT_H_ */
