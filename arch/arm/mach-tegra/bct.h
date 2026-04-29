/* SPDX-License-Identifier: GPL-2.0+ */

#ifndef _BCT_H_
#define _BCT_H_

#define EBT_ALIGNMENT		0x10

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

/*
 * Updates the hash of bootloader entry and the BCT itself
 * It will encrypt BCT in place if the device requires it
 *
 * @param  bct		boot config table start in RAM
 * @param  ebt		bootloader start in RAM
 * @param  ebt_size	bootloader file size in bytes
 * Return: 0, or 1 if failed
 */
int bct_patch(u8 *bct, u8 *ebt, u32 ebt_size);

#endif /* _BCT_H_ */
