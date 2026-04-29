// SPDX-License-Identifier: GPL-2.0-or-later
/*
 * Copyright (c) 2022, Ramin <raminterex@yahoo.com>
 * Copyright (c) 2022, Svyatoslav Ryhel <clamor95@gmail.com>
 * Copyright (c) 2026, Ion Agorria <ion@agorria.com>
 */

#include <dm.h>
#include <blk.h>
#include <command.h>
#include <fs.h>
#include <log.h>
#include <mmc.h>
#include <spi.h>
#include <spi_flash.h>
#include <stdlib.h>
#include <string.h>
#include <vsprintf.h>
#include <linux/string.h>
#include <linux/delay.h>
#include <linux/printk.h>

#include <asm/arch-tegra/ap.h>
#include <asm/arch-tegra/crypto.h>
#include <asm/arch-tegra/fuse.h>
#include <asm/arch-tegra/pmc.h>

#include "bct.h"
#include "uboot_aes.h"

#define NV_PA_IRAM_BASE			0x40000000

static int convert_to_blocks(struct blk_desc *dev_desc, int len)
{
	len = ((len + (dev_desc->blksz - 1)) & ~(dev_desc->blksz - 1));
	return lldiv(len, dev_desc->blksz);
}

static int get_mmc_hwpart_from_offset(struct mmc *mmc, u32 *data_offset,
				      u32 data_size)
{
	/*
	 * Get which hwpart the offset is located, assuming the offset follows
	 * a linear layout as: eMMC start | [boot0] - [boot1] - [user] | eMMC end
	 */
	if (*data_offset < mmc->capacity_boot) {
		if (*data_offset + data_size > mmc->capacity_boot) {
			log_err("Data doesn't fit within the boundaries of boot0 partition\n");
			return -1;
		}

		return 1;
	} else if (*data_offset < mmc->capacity_boot * 2) {
		*data_offset -= mmc->capacity_boot;
		if (*data_offset + data_size > mmc->capacity_boot) {
			log_err("Data doesn't fit within the boundaries of boot1 partition\n");
			return -1;
		}

		return 2;
	} else {
		*data_offset -= mmc->capacity_boot * 2;
		if (*data_offset + data_size > mmc->capacity_user) {
			log_err("Data doesn't fit within the boundaries of user partition\n");
			return -1;
		}

		return 0;
	}

	return -1;
}

static bool is_bct_valid(struct nvboot_config_table *bct)
{
	int soc_expected = tegra_get_chip();
	int soc_val = (bct->boot_data_version >> 16) & 0xff;

	/* Make 0x2 and 0x3 into 0x20 and 0x30 for Tegra 2/3 */
	if (soc_val <= 0x3)
		soc_val <<= 4;

	log_debug("Checking BCT\n");

	if (!soc_expected || soc_expected != soc_val) {
		log_debug("SoC doesn't match: soc %d bct %d\n",
			  soc_expected, soc_val);
		return false;
	}

	if ((bct->boot_data_version & 0xf) != 1) {
		log_debug("Boot data version not 1\n");
		return false;
	}

	if (bct->block_size_log2 < 8 || bct->block_size_log2 > 23) {
		log_debug("Block size out of bounds\n");
		return false;
	}

	if (bct->page_size_log2 < 8 || bct->page_size_log2 > 14) {
		log_debug("Page size out of bounds\n");
		return false;
	}

	if (bct->num_param_sets > NVBOOT_MAX_PARAM_SETS) {
		log_debug("Number of param sets out of bounds\n");
		return false;
	}

	if (bct->num_sdram_sets > NVBOOT_MAX_SDRAM_SETS) {
		log_debug("Number of sdram sets out of bounds\n");
		return false;
	}

	if (bct->bootloader_used > NVBOOT_MAX_BOOTLOADERS) {
		log_debug("Bootloader used out of bounds\n");
		return false;
	}

	return true;
}

static int bct_patch(u8 *bct, u8 *ebt, u32 ebt_size)
{
	struct nvboot_config_table *bct_tbl = (struct nvboot_config_table *)bct;
	bool encrypted;
	int ret;

	bct += UBCT_LENGTH;

	ebt_size = roundup(ebt_size, AES_BLOCK_LENGTH);

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

static enum tegra_boot_device get_boot_device(void)
{
	void *fdt = (void *)gd->fdt_blob;
	static const char * const spi_nodes[] = {
		"/spi@7000c380", /* Tegra20 */
		"/spi@7000d400", "/spi@7000d600", "/spi@7000d800",
		"/spi@7000da00", "/spi@7000dc00", "/spi@7000de00"
	};
	int spi_node, subnode, i, ret;

	for (i = 0; i < ARRAY_SIZE(spi_nodes); i++) {
		spi_node = fdt_path_offset(fdt, spi_nodes[i]);
		if (spi_node < 0)
			continue;

		fdt_for_each_subnode(subnode, fdt, spi_node) {
			ret = fdt_node_check_compatible(fdt, subnode,
							"jedec,spi-nor");
			if (!ret)
				return TEGRA_BOOT_DEVICE_SPI;
		}
	}

	if (find_mmc_device(0))
		return TEGRA_BOOT_DEVICE_MMC;

	return TEGRA_BOOT_DEVICE_UNKNOWN;
}

static int access_boot_data_mmc(struct tegra_boot_update_context *ctx,
				u8 *data_ptr, u32 data_offset, u32 data_size)
{
	int ret, hwpart, data_offset_blk, data_size_blk;
	struct blk_desc *dev_desc;

	/* Switch to hw partition where data will be read in MMC */
	hwpart = get_mmc_hwpart_from_offset(ctx->mmc, &data_offset, data_size);
	if (hwpart < 0)
		return hwpart;

	dev_desc = mmc_get_blk_desc(ctx->mmc);
	ret = blk_dselect_hwpart(dev_desc, hwpart);
	if (ret) {
		log_err("Failed to select MMC hwpart %d (%d)\n", hwpart, ret);
		return ret;
	}

	data_offset_blk = convert_to_blocks(dev_desc, data_offset);
	data_size_blk = convert_to_blocks(dev_desc, data_size);

	log_debug("hwpart: %d offset blk: 0x%x size blk: 0x%x\n",
		  hwpart, data_offset_blk, data_size_blk);

	if (data_ptr) {
		/* Writing */
		ret = blk_dwrite(dev_desc, data_offset_blk, data_size_blk,
				 data_ptr);
	} else {
		/* Reading */
		ret = blk_dread(dev_desc, data_offset_blk, data_size_blk,
				(u8 *)CONFIG_SYS_LOAD_ADDR);
	}

	if (ret != data_size_blk) {
		log_err("Failed to %s data at MMC (%d)\n",
			data_ptr ? "write" : "read", ret);
		return -1;
	}

	return 0;
}

static int access_boot_data_spi(struct tegra_boot_update_context *ctx,
				u8 *data_ptr, u32 data_offset, u32 data_size)
{
	int ret;

	if (!data_ptr) {
		/* Reading */
		ret = spi_flash_read(ctx->spi_flash, data_offset, data_size,
				     (u8 *)CONFIG_SYS_LOAD_ADDR);
		if (ret) {
			log_err("Failed to read data at SF (%d)\n", ret);
			return ret;
		}
	}

	/* Writing */

	/* Check if fits */
	if (data_offset + data_size > ctx->spi_flash->size) {
		log_err("Data to write doesn't fit in SF\n");
		return ret;
	}

	/* Erase and write */
	ret = spi_flash_erase(ctx->spi_flash, data_offset,
			      ROUND(data_size, ctx->spi_flash->erase_size));
	if (ret) {
		log_err("Failed to erase data at SF (%d)\n", ret);
		return ret;
	}

	ret = spi_flash_write(ctx->spi_flash, data_offset, data_size, data_ptr);
	if (ret) {
		log_err("Failed to write data at SF (%d)\n", ret);
		return ret;
	}

	return 0;
}

static int read_boot_data(struct tegra_boot_update_context *ctx,
			  u32 data_offset, u32 data_size)
{
	log_debug("Read boot data offset: 0x%x size: %d\n",
		  data_offset, data_size);

	if (ctx->dev == TEGRA_BOOT_DEVICE_MMC)
		return access_boot_data_mmc(ctx, NULL, data_offset, data_size);
	else if (ctx->dev == TEGRA_BOOT_DEVICE_SPI)
		return access_boot_data_spi(ctx, NULL, data_offset, data_size);

	log_err("No boot device to read\n");
	return -1;
}

static int write_boot_data(struct tegra_boot_update_context *ctx,
			   u8 *data_ptr, u32 data_offset, u32 data_size)
{
	int ret;

	/* Check if data already matches to avoid writing same thing */
	ret = read_boot_data(ctx, data_offset, data_size);
	if (ret) {
		log_err("Failed to read data to check (%d)\n", ret);
		return ret;
	}

	log_debug("Write boot data ptr: %p offset: 0x%x size: %d\n",
		  data_ptr, data_offset, data_size);

	if (memcmp(data_ptr, (u8 *)CONFIG_SYS_LOAD_ADDR, data_size) == 0) {
		log_debug("Skipping write, data already matches\n");
		return 0;
	}

	/* Write the data */
	if (ctx->dev == TEGRA_BOOT_DEVICE_MMC) {
		ret = access_boot_data_mmc(ctx, data_ptr, data_offset, data_size);
	} else if (ctx->dev == TEGRA_BOOT_DEVICE_SPI) {
		ret = access_boot_data_spi(ctx, data_ptr, data_offset, data_size);
	} else {
		log_err("No boot device to write\n");
		return -1;
	}
	if (ret)
		return ret;

	/* Verify written data matches */
	ret = read_boot_data(ctx, data_offset, data_size);
	if (ret) {
		log_err("Failed to read data to verify (%d)\n", ret);
		return ret;
	}

	if (memcmp(data_ptr, (u8 *)CONFIG_SYS_LOAD_ADDR, data_size) != 0) {
		log_err("Written data mismatches\n");
		return -1;
	}

	return 0;
}

/*
 * Attempt to read a copy of BCT from boot device
 */
static int read_boot_device_bct(struct tegra_boot_update_context *ctx)
{
	int ret;
	bool encrypted;

	ret = read_boot_data(ctx, 0, BCT_LENGTH);
	if (ret)
		return ret;

	/* Check and decrypt */
	encrypted = tegra_fuse_get_operation_mode() == MODE_ODM_PRODUCTION_SECURE;
	if (encrypted) {
		u8 *bct = ((u8 *)CONFIG_SYS_LOAD_ADDR) + UBCT_LENGTH;

		ret = decrypt_data_block(bct, bct, SBCT_LENGTH);
		if (ret)
			return ret;
	}

	if (is_bct_valid((struct nvboot_config_table *)CONFIG_SYS_LOAD_ADDR)) {
		if (!ctx->bct)
			ctx->bct = (struct nvboot_config_table *)malloc(BCT_LENGTH);
		memcpy(ctx->bct, (u8 *)CONFIG_SYS_LOAD_ADDR, BCT_LENGTH);
	}

	return 0;
}

/*
 * Attempt to read a copy of BCT from IRAM
 */
static int read_iram_bct(struct tegra_boot_update_context *ctx)
{
	struct tegra_boot_info_table *bit =
			(struct tegra_boot_info_table *)NV_PA_IRAM_BASE;
	struct nvboot_config_table *bit_bct =
			(struct nvboot_config_table *)bit->bct_ptr;

	/* Make sure BIT and BCT are valid */
	if ((bit->boot_type != 1 && bit->boot_type != 2) ||
	    bit->primary_device != 5) {
		log_err("BIT is not valid - boot type: %d primary device: %d\n",
			bit->boot_type, bit->primary_device);
		return -1;
	}

	/* Couldn't get a valid one, use the one in IRAM */
	if (!bit->bct_valid || bit->bct_size != BCT_LENGTH || !bit_bct) {
		log_err("BIT BCT data error - valid: %d size: 0x%x ptr: 0x%p\n",
			bit->bct_valid, bit->bct_size, bit_bct);
		return -1;
	}

	if (is_bct_valid(bit_bct)) {
		if (!ctx->bct)
			ctx->bct = (struct nvboot_config_table *)malloc(BCT_LENGTH);
		memcpy(ctx->bct, bit_bct, BCT_LENGTH);
	}

	return 0;
}

/**
 * Applies some basic BCT adjusts loads some data from it
 */
static int adjust_bct(struct tegra_boot_update_context *ctx,
		      u32 *ebt_offset, u32 *ebt_size)
{
	struct nv_bootloader_info *info = &ctx->bct->bootloader[0];
	int ret;

	/* Get block and page size if any */
	ctx->block_size = (1 << ctx->bct->block_size_log2);
	ctx->page_size = (1 << ctx->bct->page_size_log2);

	log_debug("BCT block: 0x%x page: 0x%x\n",
		  ctx->block_size, ctx->page_size);

	if (!ctx->bct->num_sdram_sets) {
		pr_err("BCT has no SDRAM timing, aborting\n");
		return -1;
	}

	/* If BCT has no bootloaders defined, make a entry */
	if (!ctx->bct->bootloader_used) {
		log_debug("BCT has 0 bootloaders used, creating entry\n");

		ctx->bct->bootloader_used = 1;
		memset(info, 0, sizeof(struct nv_bootloader_info));

		info->version = 1;

		/*
		 * MMC is handled later automatically to accommodate
		 * boot1 and partition
		 */

		/* Devices with SPI boot device have 1MB set by vendors*/
		if (ctx->dev == TEGRA_BOOT_DEVICE_SPI)
			info->start_blk = DIV_ROUND_UP(1024 * 1024,
						       ctx->block_size);
	}

	info->length = *ebt_size;

	/*
	 * Copy the first/two entry into last as last resort backup
	 * in case BCT is written but not EBT, the old BL hash should
	 * still be valid
	 */

	if (ctx->bct->bootloader_used <= 2) {
		ctx->bct->bootloader_used = 2;
		memcpy(&ctx->bct->bootloader[1], ctx->bct->bootloader,
		       sizeof(struct nv_bootloader_info));
	} else {
		ctx->bct->bootloader_used = 4;
		memcpy(&ctx->bct->bootloader[2], ctx->bct->bootloader,
		       sizeof(struct nv_bootloader_info) * 2);
	}

	if (ctx->dev == TEGRA_BOOT_DEVICE_MMC) {
		struct disk_partition part;

		/*
		 * Search partition in user hwpartition if exists to use
		 * it as target instead
		 */
		ret = blk_dselect_hwpart(mmc_get_blk_desc(ctx->mmc), 0);
		if (ret) {
			log_err("Failed to select MMC hwpart 0 (%d)\n", ret);
			return ret;
		}

		ret = part_get_info_by_name(mmc_get_blk_desc(ctx->mmc),
					    "ebt", &part);
		if (ret < 0 || (part.size * part.blksz) <= 0)
			ret = part_get_info_by_name(mmc_get_blk_desc(ctx->mmc),
						    "bootloader", &part);

		if (ret >= 0) {
			log_debug("Bootloader partition found! num: %d\n", ret);

			if ((part.size * part.blksz) < info->length) {
				strlcpy(ctx->error, "EBT partition is too small",
					sizeof(ctx->error));
				return ret;
			}

			info->start_blk = DIV_ROUND_UP((ctx->mmc->capacity_boot * 2 +
							part.start * part.blksz),
						       ctx->block_size);
			if (info->length == 0)
				info->length = (part.size * part.blksz);
		} else if (info->length <= ctx->mmc->capacity_boot) {
			/* On boot1 start */
			info->start_blk = DIV_ROUND_UP(ctx->mmc->capacity_boot,
						       ctx->block_size);
		} else {
			strlcpy(ctx->error, "Doesn't fit on boot1 and no EBT partition found",
				sizeof(ctx->error));
			return -1;
		}
	}

	if (info->length == 0)
		info->length = EBT_MAX_LENGTH;

	if (info->start_blk == 0) {
		strlcpy(ctx->error, "No suitable location found for bootloader!",
			sizeof(ctx->error));
		return -1;
	}

	/* Provide calculations for EBT */
	*ebt_offset = info->start_blk * ctx->block_size +
		      info->start_page * ctx->page_size;

	log_debug("EBT offset: 0x%x length: %d\n", *ebt_offset, info->length);

	*ebt_size = info->length;

	/* Last check to see if actually fits into the boot device */
	if (ctx->dev == TEGRA_BOOT_DEVICE_MMC) {
		u32 tmp = *ebt_offset;

		ret = get_mmc_hwpart_from_offset(ctx->mmc, &tmp, *ebt_size);
		if (ret < 0)
			return ret;
	} else if (ctx->dev == TEGRA_BOOT_DEVICE_SPI) {
		if (*ebt_offset + *ebt_size > ctx->spi_flash->size) {
			log_err("Bootloader doesn't fit in SF\n");
			return ret;
		}
	}

	return 0;
}

static int tegra_boot_backup_to_file(struct tegra_boot_update_context *ctx,
				     const char *ifname, const char *dev_part,
				     const char *backup_file,
				     u32 data_offset, u32 data_size)
{
	int ret;
	loff_t len;

	ret = fs_set_blk_dev(ifname, dev_part, FS_TYPE_ANY);
	if (ret) {
		snprintf(ctx->error, sizeof(ctx->error),
			 "Error opening %s %s for storing backup\n",
			 ifname, dev_part);
		return ret;
	}

	if (fs_exists(backup_file))
		return 0;

	ret = read_boot_data(ctx, data_offset, data_size);
	if (ret) {
		snprintf(ctx->error, sizeof(ctx->error),
			 "Read boot dev err %d for %s\n", ret, backup_file);
		return ret;
	}

	/* Again as fs_close was called on fs_exists */
	ret = fs_set_blk_dev(ifname, dev_part, FS_TYPE_ANY);
	if (ret) {
		snprintf(ctx->error, sizeof(ctx->error),
			 "Error opening %s %s for storing backup\n",
			 ifname, dev_part);
		return ret;
	}

	ret = fs_write(backup_file, CONFIG_SYS_LOAD_ADDR, 0, data_size, &len);
	if (ret < 0) {
		snprintf(ctx->error, sizeof(ctx->error),
			 "fs write error %d for %s\n", ret, backup_file);
		return ret;
	}

	/* One more time as fs_close was called on fs_write */
	ret = fs_set_blk_dev(ifname, dev_part, FS_TYPE_ANY);
	if (ret) {
		snprintf(ctx->error, sizeof(ctx->error),
			 "Error opening %s %s for storing backup\n",
			 ifname, dev_part);
		return ret;
	}

	if (!fs_exists(backup_file)) {
		snprintf(ctx->error, sizeof(ctx->error),
			 "%s was not created, unsafe to proceed\n", backup_file);
		return -1;
	}

	return 0;
}

static int tegra_boot_backup(struct tegra_boot_update_context *ctx)
{
	int ret;
	const char *ifname;
	const char *dev_part;
	struct mmc *bak_mmc = NULL;

	bak_mmc = find_mmc_device(1);
	if (bak_mmc) {
		/* Check sdcard slot */
		ret = mmc_init(bak_mmc);
		if (ret) {
			snprintf(ctx->error, sizeof(ctx->error),
				 "sdcard init error: %d\n", ret);
			return ret;
		}

		if (!IS_SD(bak_mmc)) {
			snprintf(ctx->error, sizeof(ctx->error),
				 "mmc1 is not sdcard: %d\n", ret);
			return ret;
		}

		ifname = "mmc";
		dev_part = "1:1";
	} else if (ctx->dev == TEGRA_BOOT_DEVICE_SPI) {
		/* Not supposed to happen as all SPI devices we know have sdcard */
		snprintf(ctx->error, sizeof(ctx->error),
			 "spi device but no sdcard slot\n");
		return -1;
	} else {
		/* No sdcard present */
		log_debug("No sdcard present");
		return 0;
	}

	if (ctx->dev == TEGRA_BOOT_DEVICE_MMC) {
		ret = tegra_boot_backup_to_file(ctx, ifname, dev_part,
						"mmc-boot0.bak", 0,
						ctx->mmc->capacity_boot);
		if (ret)
			goto err;

		ret = tegra_boot_backup_to_file(ctx, ifname, dev_part,
						"mmc-boot1.bak",
						ctx->mmc->capacity_boot,
						ctx->mmc->capacity_boot);
		if (ret)
			goto err;

		ret = tegra_boot_backup_to_file(ctx, ifname, dev_part,
						"mmc-boot-user.bak",
						ctx->mmc->capacity_boot * 2,
						1024 * 1024 * 32);
		if (ret)
			goto err;
	} else if (ctx->dev == TEGRA_BOOT_DEVICE_SPI) {
		ret = tegra_boot_backup_to_file(ctx, ifname, dev_part,
						"spi-boot.bak", 0,
						ctx->spi_flash->size);
		if (ret)
			goto err;
	} else {
		log_err("Error: boot device not set\n");
		return -1;
	}

err:
	fs_close();

	return ret;
}

/**
 * Setups the tegra boot update context struct and opens any required device
 */
static int tegra_boot_setup_context(struct tegra_boot_update_context *ctx)
{
	int ret;

	memset(ctx, 0, sizeof(struct tegra_boot_update_context));

	ctx->dev = get_boot_device();

	if (ctx->dev == TEGRA_BOOT_DEVICE_MMC) {
		/* Find emmc device (we assume to be mmc0) */
		ctx->mmc = find_mmc_device(0);
		if (!ctx->mmc) {
			log_err("Error getting mmc to write\n");
			return -1;
		}

		ret = mmc_init(ctx->mmc);
		if (ret) {
			log_err("mmc init failed with error: %d\n", ret);
			return ret;
		}
	} else if (ctx->dev == TEGRA_BOOT_DEVICE_SPI) {
		struct udevice *new;

		/* Probe spi flash */
		ret = spi_flash_probe_bus_cs(0, 1, &new);
		if (ret) {
			log_err("Error probing SF\n");
			return ret;
		}

		ctx->spi_flash = dev_get_uclass_priv(new);
	} else {
		log_err("Error: boot device not set\n");
		return -1;
	}

	return tegra_boot_backup(ctx);
}

int tegra_boot_flash_bct(struct tegra_boot_update_context *ctx,
			 struct nvboot_config_table *bct_buffer,
			 u32 bct_size)
{
	u32 ebt_offset, ebt_size;
	int ret;

	ret = tegra_boot_setup_context(ctx);
	if (ret)
		goto err;

	/* Use BCT that was sent over fastboot */
	if (bct_size != BCT_LENGTH) {
		strlcpy(ctx->error, "BCT size too big or small", sizeof(ctx->error));
		ret = -1;
		goto err;
	}

	if (!is_bct_valid(bct_buffer)) {
		strlcpy(ctx->error, "BCT is not valid", sizeof(ctx->error));
		ret = -1;
		goto err;
	}

	/* Check if BCT has bootloaders defined, take from boot device if valid */
	if (bct_buffer->bootloader_used == 0) {
		read_boot_device_bct(ctx);
		if (ctx->bct && 0 < ctx->bct->bootloader_used) {
			log_debug("Provided BCT doesn't have any bootloader entry\n");
			log_debug("Using boot device's bootloaders\n");

			bct_buffer->bootloader_used = ctx->bct->bootloader_used;
			memcpy(bct_buffer->bootloader, ctx->bct->bootloader,
			       sizeof(struct nv_bootloader_info) *
			       NVBOOT_MAX_BOOTLOADERS);
		}
	}

	if (!ctx->bct)
		ctx->bct = (struct nvboot_config_table *)malloc(BCT_LENGTH);

	memcpy(ctx->bct, (u8 *)bct_buffer, BCT_LENGTH);

	if (ctx->bct->bootloader_used == 0) {
		log_debug("BCT doesn't have any bootloader entry\n");
		ebt_size = 0;
	} else {
		ebt_size = ctx->bct->bootloader[0].length;
	}

	ret = adjust_bct(ctx, &ebt_offset, &ebt_size);
	if (ret)
		goto err;

	log_debug("Loading EBT\n");
	ret = read_boot_data(ctx, ebt_offset, ebt_size);
	if (ret)
		goto err;

	log_debug("Rehashing BCT\n");
	ret = bct_patch((u8 *)ctx->bct, (u8 *)CONFIG_SYS_LOAD_ADDR, ebt_size);
	if (ret)
		goto err;

	log_debug("Flashing BCT\n");
	ret = write_boot_data(ctx, (u8 *)ctx->bct, 0, BCT_LENGTH);

err:
	if (ctx->bct)
		free(ctx->bct);

	if (ret) {
		if (*ctx->error == 0)
			strlcpy(ctx->error, "Error writing BCT",
				sizeof(ctx->error));
	} else {
		puts("Flashing BCT successful!\n");
	}

	return ret;
}

int tegra_boot_flash_bootloader(struct tegra_boot_update_context *ctx,
				u8 *ebt_buffer, u32 ebt_size)
{
	int ret;
	u32 ebt_offset;
	u32 ebt_padding;
	bool encrypted;

	ret = tegra_boot_setup_context(ctx);
	if (ret)
		goto err;

	log_debug("Retrieving BCT\n");

	ret = read_boot_device_bct(ctx);
	if (ret) {
		ret = -1;
		goto err;
	}

	if (!ctx->bct) {
		pr_err("No valid BCT was found in boot device\n");

		ret = read_iram_bct(ctx);
		if (ret || !ctx->bct) {
			pr_err("No valid BCT was found in IRAM, aborting\n");
			ret = -1;
			goto err;
		}
	}

	/* We need to align it and add a extra padding block or it will get stuck */
	ebt_padding = (roundup(ebt_size, AES_BLOCK_LENGTH) + AES_BLOCK_LENGTH) - ebt_size;
	memset(ebt_buffer + ebt_size, 0, ebt_padding);
	ebt_size += ebt_padding;

	ret = adjust_bct(ctx, &ebt_offset, &ebt_size);
	if (ret)
		goto err;

	encrypted = tegra_fuse_get_operation_mode() == MODE_ODM_PRODUCTION_SECURE;
	if (encrypted) {
		log_debug("Encrypting bootloader\n");
		ret = encrypt_data_block(ebt_buffer, ebt_buffer, ebt_size);
		if (ret)
			return ret;
	}

	log_debug("Rehashing BCT and encrypting bootloader\n");
	ret = bct_patch((u8 *)ctx->bct, ebt_buffer, ebt_size);
	if (ret)
		goto err;

	log_debug("Flashing BCT\n");
	ret = write_boot_data(ctx, (u8 *)ctx->bct, 0, BCT_LENGTH);
	if (ret)
		goto err;

	log_debug("Flashing EBT\n");
	ret = write_boot_data(ctx, ebt_buffer, ebt_offset, ebt_size);

err:
	if (ctx->bct)
		free(ctx->bct);

	if (ret) {
		if (*ctx->error == 0)
			strlcpy(ctx->error, "Error writing bootloader",
				sizeof(ctx->error));
	} else {
		puts("Flashing bootloader successful!\n");
	}

	return ret;
}

#ifdef CONFIG_CMD_EBTUPDATE
static int do_ebtupdate(struct cmd_tbl *cmdtp, int flag, int argc,
			char *const argv[])
{
	struct tegra_boot_update_context ctx;
	u8 *ebt = (u8 *)hextoul(argv[1], NULL);
	u32 ebt_size = hextoul(argv[2], NULL);
	int ret;

	ret = tegra_boot_flash_bootloader(&ctx, ebt, ebt_size);
	if (strlen(ctx.error))
		log_err("Error: %s\n", ctx.error);

	return ret;
}

U_BOOT_CMD(ebtupdate,	3,	0,	do_ebtupdate,
	   "updates the bootloader on Tegra devices from a copy in RAM",
	   "ebtupdate <addr> <size>\n"
	   ""
);

static int do_bctupdate(struct cmd_tbl *cmdtp, int flag, int argc,
			char *const argv[])
{
	int ret;
	struct tegra_boot_update_context ctx;
	u8 *bct = (u8 *)hextoul(argv[1], NULL);
	u32 bct_size = hextoul(argv[2], NULL);

	ret = tegra_boot_flash_bct(&ctx, (struct nvboot_config_table *)bct,
				   bct_size);
	if (strlen(ctx.error))
		log_err("Error: %s\n", ctx.error);

	return ret;
}

U_BOOT_CMD(bctupdate,	3,	0,	do_bctupdate,
	   "updates the BCT on Tegra devices from a copy in RAM",
	   "bctupdate <addr> <size>\n"
	   ""
);
#endif
