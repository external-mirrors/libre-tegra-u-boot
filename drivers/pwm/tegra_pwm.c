// SPDX-License-Identifier: GPL-2.0+
/*
 * Tegra pulse-width-modulation controller driver
 *
 * Copyright 2016 Google Inc.
 * Copyright (c) 2010-2020, NVIDIA Corporation.
 * Based on linux's drivers/pwm/pwm-tegra.c
 *
 * Overview of Tegra Pulse Width Modulator Register:
 * 1. 13-bit: Frequency division (SCALE)
 * 2. 8-bit : Pulse division (DUTY)
 * 3. 1-bit : Enable bit
 *
 * The PWM clock frequency is divided by 256 before subdividing it based
 * on the programmable frequency division value to generate the required
 * frequency for PWM output. The maximum output frequency that can be
 * achieved is (max rate of source clock) / 256.
 * e.g. if source clock rate is 408 MHz, maximum output frequency can be:
 * 408 MHz/256 = 1.6 MHz.
 * This 1.6 MHz frequency can further be divided using SCALE value in PWM.
 *
 * PWM pulse width: 8 bits are usable [23:16] for varying pulse width.
 * To achieve 100% duty cycle, program Bit [24] of this register to
 * 1’b1. In which case the other bits [23:16] are set to don't care.
 */

#include <dm.h>
#include <log.h>
#include <div64.h>
#include <pwm.h>
#include <asm/io.h>
#include <asm/arch/clock.h>
#include <asm/arch/pwm.h>

#define NSEC_PER_SEC			1000000000L

struct tegra_pwm_priv {
	struct pwm_ctlr *regs;
	u64 clk_rate;
	u32 min_period_ns;
	u8 polarity;
};

static int tegra_pwm_set_invert(struct udevice *dev, uint channel,
	bool polarity)
{
	struct tegra_pwm_priv *priv = dev_get_priv(dev);

	if (channel >= 4)
		return -EINVAL;

	clrsetbits_8(&priv->polarity, (1 << channel), (polarity << channel));

	return 0;
}

static int tegra_pwm_set_config(struct udevice *dev, uint channel,
				uint period_ns, uint duty_ns)
{
	struct tegra_pwm_priv *priv = dev_get_priv(dev);
	struct pwm_ctlr *regs = priv->regs;
	u64 c = duty_ns;
	u32 val;
	u32 rate;

	if (channel >= 4)
		return -EINVAL;
	debug("%s: Configure '%s' channel %u period_ns %u duty_ns %u\n", __func__, dev->name, channel, period_ns, duty_ns);

	/*
     * Convert from duty_ns / period_ns to a fixed number of duty ticks
     * per (1 << PWM_DUTY_WIDTH) cycles and make sure to round to the
     * nearest integer during division.
     */
    c *= (1 << PWM_DUTY_WIDTH);
    c = DIV_ROUND_CLOSEST_ULL(c, period_ns);

    if (priv->polarity & (1<<channel)) {
        c = 0x100-c;
    }
    if (c > 0x100){
        debug("%s: Channel %u duty too high %llu\n", __func__, channel, c);
        return -EINVAL;
    }

    val = (u32)c << PWM_DUTY_SHIFT;

    /*
     *  min period = max clock limit >> PWM_DUTY_WIDTH
     */
    if (period_ns < priv->min_period_ns) {
        debug("%s: Too low period channel %u period_ns %u minimum %u\n", __func__, channel, period_ns, priv->min_period_ns);
        return -EINVAL;
    }

    //TODO those with only 1 channel (T186 and T194 ?) should be handled differently according to linux driver

    /* Consider precision in PWM_SCALE_WIDTH rate calculation */
    rate = (priv->clk_rate * period_ns) / ((u64)NSEC_PER_SEC << PWM_DUTY_WIDTH);

    /*
     * Since the actual PWM divider is the register's frequency divider
     * field plus 1, we need to decrement to get the correct value to
     * write to the register.
     */
    if (rate > 0)
        rate--;
    else {
        debug("%s: Channel %u rate is not positive\n", __func__, channel);
        return -EINVAL;
    }

    /*
     * Make sure that the rate will fit in the register's frequency
     * divider field.
     */
    if (rate >> PWM_SCALE_WIDTH){
        debug("%s: Channel %u rate too high %u\n", __func__, channel, rate);
        return -EINVAL;
    }

    val |= rate << PWM_SCALE_SHIFT;

	debug("%s: duty = %llu rate = %u\n", __func__, c, rate);
	val |= PWM_ENABLE_MASK;
	writel(val, &regs[channel].control);

	return 0;
}

static int tegra_pwm_set_enable(struct udevice *dev, uint channel, bool enable)
{
	struct tegra_pwm_priv *priv = dev_get_priv(dev);
	struct pwm_ctlr *regs = priv->regs;

	if (channel >= 4)
		return -EINVAL;
	debug("%s: Enable '%s' channel %u\n", __func__, dev->name, channel);
	clrsetbits_le32(&regs[channel].control, PWM_ENABLE_MASK,
			enable ? PWM_ENABLE_MASK : 0);

	return 0;
}

static int tegra_pwm_of_to_plat(struct udevice *dev)
{
	struct tegra_pwm_priv *priv = dev_get_priv(dev);

	priv->regs = dev_read_addr_ptr(dev);

	return 0;
}

static int tegra_pwm_probe(struct udevice *dev) {
	const u32 pwm_max_freq = dev_get_driver_data(dev);
	struct tegra_pwm_priv *priv = dev_get_priv(dev);

	priv->clk_rate = clock_start_periph_pll(PERIPH_ID_PWM, CLOCK_ID_PERIPH, pwm_max_freq);
	priv->min_period_ns = (NSEC_PER_SEC / (pwm_max_freq >> PWM_DUTY_WIDTH)) + 1;
	debug("%s: clk_rate = %llu min_period_ns = %u\n", __func__, priv->clk_rate, priv->min_period_ns);

	return 0;
}

static const struct pwm_ops tegra_pwm_ops = {
	.set_config	= tegra_pwm_set_config,
	.set_enable	= tegra_pwm_set_enable,
	.set_invert	= tegra_pwm_set_invert,
};

static const struct udevice_id tegra_pwm_ids[] = {
	{ .compatible = "nvidia,tegra20-pwm", .data = 48 * 1000000 },
	{ .compatible = "nvidia,tegra114-pwm", .data = 408 * 1000000 },
	{ }
};

U_BOOT_DRIVER(tegra_pwm) = {
	.name	= "tegra_pwm",
	.id	= UCLASS_PWM,
	.of_match = tegra_pwm_ids,
	.ops	= &tegra_pwm_ops,
	.of_to_plat	= tegra_pwm_of_to_plat,
	.probe		= tegra_pwm_probe,
	.priv_auto	= sizeof(struct tegra_pwm_priv),
};
