/* SPDX-License-Identifier: GPL-2.0+ */

#ifndef _BCT_H_
#define _BCT_H_

#define EBT_ALIGNMENT		0x10
#define EBT_MAX_LENGTH		(1024 * 1024 * 2)	/* 2 MB */

enum tegra_boot_device {
	TEGRA_BOOT_DEVICE_UNKNOWN = 0,
	TEGRA_BOOT_DEVICE_MMC,
	TEGRA_BOOT_DEVICE_SPI,
};

struct tegra_boot_update_context {
	enum tegra_boot_device dev;
	struct nvboot_config_table *bct;
	char error[60];
	u32 block_size;
	u32 page_size;
	u8 *ebt_ptr;
	u32 ebt_size;

	struct mmc *mmc;
	struct spi_flash *spi_flash;
};

/*
 * Defines the CMAC-AES-128 hash length in 32 bit words. (128 bits = 4 words)
 */
#define NVBOOT_CMAC_AES_HASH_LENGTH		4

/*
 * Defines the RSA modulus length in 32 bit words used for PKC secure boot.
 */
#define NVBOOT_SE_RSA_MODULUS_LENGTH		64

/*
 * Defines the maximum number of bootloader descriptions in the BCT.
 */
#define NVBOOT_MAX_BOOTLOADERS			4

#ifdef CONFIG_TEGRA20
#include "tegra20/bct.h"
#elif CONFIG_TEGRA30
#include "tegra30/bct.h"
#elif CONFIG_TEGRA124
#include "tegra124/bct.h"
#endif

#define BCT_LENGTH	(UBCT_LENGTH + SBCT_LENGTH)

/**
 * tegra_boot_flash_bct - Flashes the provided BCT into the boot device
 * @ctx:	Pointer to the Tegra boot update context structure
 * @bct_buffer  Pointer to the BCT structure
 * @bct_size	BCT size in bytes
 *
 * Return: 0, or -1 if failed
 */
int tegra_boot_flash_bct(struct tegra_boot_update_context *ctx,
			 struct nvboot_config_table *bct_buffer,
			 u32 bct_size);

/**
 * tegra_boot_flash_bootloader - Flashes the provided bootloader into
 *				 the boot device
 * @ctx:	Pointer to the Tegra boot update context structure
 * @ebt_buffer  Pointer to the buffer containing bootloader
 * @ebt_size	Bootloader size in bytes
 *
 * Return: 0, or -1 if failed
 */
int tegra_boot_flash_bootloader(struct tegra_boot_update_context *ctx,
				u8 *ebt_buffer, u32 ebt_size);

#endif /* _BCT_H_ */
