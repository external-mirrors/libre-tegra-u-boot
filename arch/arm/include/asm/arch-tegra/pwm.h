/* SPDX-License-Identifier: GPL-2.0+ */
/*
 * Tegra pulse width frequency modulator definitions
 *
 * Copyright (c) 2011 The Chromium OS Authors.
 */

#ifndef __ASM_ARCH_TEGRA_PWM_H
#define __ASM_ARCH_TEGRA_PWM_H

/* This is a single PWM channel */
struct pwm_ctlr {
	uint control;		/* Control register */
	uint reserved[3];	/* Space space */
};

#define PWM_NUM_CHANNELS	4

/* PWM_CONTROLLER_PWM_CSR_0/1/2/3_0 */

#define PWM_DUTY_WIDTH  8
#define PWM_DUTY_SHIFT  16
#define PWM_SCALE_WIDTH 13
#define PWM_SCALE_SHIFT 0

#define PWM_ENABLE_SHIFT	31
#define PWM_ENABLE_MASK	(0x1 << PWM_ENABLE_SHIFT)

#endif	/* __ASM_ARCH_TEGRA_PWM_H */
