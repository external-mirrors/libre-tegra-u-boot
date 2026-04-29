/* SPDX-License-Identifier: GPL-2.0+ */

#ifndef _TEGRA124_BCT_H_
#define _TEGRA124_BCT_H_

/*
 * Defines the BCT parametres for T124
 */
#define UBCT_LENGTH		0x6b0  /* bytes */
#define SBCT_LENGTH		0x1950 /* bytes */

struct nv_bootloader_info {
	u32 version;
	u32 start_blk;
	u32 start_page;
	u32 length;
	u32 load_addr;
	u32 entry_point;
	u32 attribute;

	/* Specifies the AES-CMAC MAC or RSASSA-PSS signature of the BL. */
	u32 crypto_hash[NVBOOT_CMAC_AES_HASH_LENGTH];
	u32 bl_rsa_sig[NVBOOT_SE_RSA_MODULUS_LENGTH];
};

struct nvboot_config_table {
	u32 ubct_unused1[196];
	u32 crypto_hash[NVBOOT_CMAC_AES_HASH_LENGTH];
	u32 ubct_unused2[228];

	u32 sbct_unused1[2];
	u32 boot_data_version;
	u32 block_size_log2;
	u32 page_size_log2;
	u32 partition_size;
	u32 sbct_unused2[1312];
	u32 bootloader_used;
	struct nv_bootloader_info bootloader[NVBOOT_MAX_BOOTLOADERS];
	u32 sbct_unused3;
};

struct tegra_boot_info_table {
	u8 unused1[12];
	u32 boot_type;
	u32 primary_device;
	u32 secondary_device;
	u8 unused2[26];
	u8 bct_valid;
	u8 unused3[13];
	u32 bct_block;
	u32 bct_page;
	u32 bct_size;
	u32 bct_ptr;
};

#endif /* _TEGRA124_BCT_H_ */
