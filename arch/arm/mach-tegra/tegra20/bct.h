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
	u32 random_aes_blk[4];
	u32 boot_data_version;
	u32 block_size_log2;
	u32 page_size_log2;
	u32 partition_size;
	u32 num_param_sets;
	u32 dev_type[NVBOOT_MAX_PARAM_SETS];
	u32 dev_params[NVBOOT_MAX_PARAM_SETS * 4];
	u32 num_sdram_sets;
	u32 sdram[NVBOOT_MAX_SDRAM_SETS * 128];
	u32 badblock_table[130];
	u32 bootloader_used;
	struct nv_bootloader_info bootloader[NVBOOT_MAX_BOOTLOADERS];
	u8 customer_data[1184];
	u32 odm_data;
	u32 reserved1;
	u8 enable_fail_back;
	u8 reserved[3];
};

struct tegra_boot_info_table {
	u8 unused1[12];
	u32 boot_type;
	u32 primary_device;
	u32 secondary_device;
	u8 unused2[9];
	u8 bct_valid;
	u8 unused3[13];
	u32 bct_block;
	u32 bct_page;
	u32 bct_size;
	u32 bct_ptr;
};

#endif /* _TEGRA20_BCT_H_ */
