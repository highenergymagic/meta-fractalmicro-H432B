// SPDX-License-Identifier: GPL-2.0-only
/*
 * S5PV210 frequency scaling with an unchanged APLL, MSYS bus and supply.
 *
 * The firmware must provide an 800 MHz operating point. Only ARM and
 * HCLK_MSYS divider fields change, together, as in s5pv210-cpufreq.c.
 * No PLL, regulator or DRAM register is written by this driver.
 */
#include <linux/bitfield.h>
#include <linux/clk.h>
#include <linux/cpufreq.h>
#include <linux/io.h>
#include <linux/iopoll.h>
#include <linux/mutex.h>
#include <linux/of_address.h>
#include <linux/platform_device.h>
#include <linux/reboot.h>

#define APLL_CON		0x0100
#define CLK_SRC0		0x0200
#define CLK_DIV0		0x0300
#define CLKDIV_STAT0	0x1000
#define ARM_MCS_CON	0x6100
#define ARM_DIV		GENMASK(2, 0)
#define HCLK_DIV		GENMASK(10, 8)
#define DIV_MASK		(ARM_DIV | HCLK_DIV)
#define DIV_800		0x300
#define DIV_400		0x101
#define DIV_BUSY		(BIT(0) | BIT(2))
#define APLL_800		0xa0640301
#define APLL_MASK		0xa3ff3f07
#define SOURCE_MASK	(BIT(0) | BIT(16))
#define RESTORE_KHZ	800000

static void __iomem *base;
static struct clk *armclk;
static struct device *cpu_dev;
static u32 fixed_dividers;
static DEFINE_MUTEX(transition_lock);
static bool blocked;
static bool failed;

static struct cpufreq_frequency_table frequencies[] = {
	{ .driver_data = DIV_400, .frequency = 400000 },
	{ .driver_data = DIV_800, .frequency = RESTORE_KHZ },
	{ .frequency = CPUFREQ_TABLE_END },
};

static bool clock_configuration_valid(void)
{
	u32 div = readl(base + CLK_DIV0);

	return (readl(base + APLL_CON) & APLL_MASK) == APLL_800 &&
	       (readl(base + CLK_SRC0) & SOURCE_MASK) == BIT(0) &&
	       (readl(base + ARM_MCS_CON) & 3) == 1 &&
	       (div & ~DIV_MASK) == fixed_dividers &&
	       ((div & DIV_MASK) == DIV_800 ||
		(div & DIV_MASK) == DIV_400);
}

/* Caller holds transition_lock; CPUFreq owns the transition notifications. */
static int change_dividers(u32 fields)
{
	unsigned long flags;
	u32 old, status;
	int ret, rollback;

	if (failed || !clock_configuration_valid())
		return -EIO;

	old = readl(base + CLK_DIV0);
	if ((old & DIV_MASK) == fields)
		return 0;

	/*
	 * One register update keeps ARM/HCLK at 800/200 or 400/200 MHz.
	 * Do not allow an interrupt handler to run between the write and
	 * completion. Both waits are bounded, including the rollback path.
	 */
	local_irq_save(flags);
	writel((old & ~DIV_MASK) | fields, base + CLK_DIV0);
	ret = readl_poll_timeout_atomic(base + CLKDIV_STAT0, status,
					!(status & DIV_BUSY), 1, 500);
	if (!ret && (readl(base + CLK_DIV0) & DIV_MASK) != fields)
		ret = -EIO;
	if (ret) {
		writel(old, base + CLK_DIV0);
		rollback = readl_poll_timeout_atomic(base + CLKDIV_STAT0, status,
						     !(status & DIV_BUSY), 1, 500);
		failed = true;
	} else {
		rollback = 0;
	}
	local_irq_restore(flags);

	if (ret)
		dev_err(cpu_dev, "CPU divider transition failed: %d\n", ret);
	if (rollback || (ret && readl(base + CLK_DIV0) != old))
		dev_crit(cpu_dev, "CPU divider rollback failed; restart required\n");
	return ret;
}

static int fixed_target(struct cpufreq_policy *policy, unsigned int index)
{
	int ret;

	mutex_lock(&transition_lock);
	ret = blocked ? -EBUSY : change_dividers(frequencies[index].driver_data);
	mutex_unlock(&transition_lock);
	return ret;
}

/*
 * Suspend/reboot must restore the firmware operating point, independently
 * of userspace's scaling_max_freq. Transition notifications also maintain
 * delay-loop calibration. begin/end serialize against governor transitions.
 */
static int restore_firmware_clock(struct cpufreq_policy *policy)
{
	struct cpufreq_freqs freqs = {
		.old = policy->cur,
		.new = RESTORE_KHZ,
	};
	int ret;

	cpufreq_freq_transition_begin(policy, &freqs);
	mutex_lock(&transition_lock);
	ret = change_dividers(DIV_800);
	if (!ret)
		blocked = true;
	mutex_unlock(&transition_lock);
	cpufreq_freq_transition_end(policy, &freqs, ret);
	return ret;
}

static int fixed_resume(struct cpufreq_policy *policy)
{
	int ret = 0;

	mutex_lock(&transition_lock);
	if (!clock_configuration_valid() ||
	    (readl(base + CLK_DIV0) & DIV_MASK) != DIV_800)
		ret = -EIO;
	else
		blocked = false;
	mutex_unlock(&transition_lock);
	return ret;
}

static int fixed_init(struct cpufreq_policy *policy)
{
	if (policy->cpu)
		return -EINVAL;

	policy->clk = armclk;
	policy->suspend_freq = RESTORE_KHZ;
	cpufreq_generic_init(policy, frequencies, 40000);
	return 0;
}

static struct cpufreq_driver fixed_driver = {
	.name = "s5pv210-fixed",
	.flags = CPUFREQ_NEED_INITIAL_FREQ_CHECK,
	.init = fixed_init,
	.verify = cpufreq_generic_frequency_table_verify,
	.target_index = fixed_target,
	.get = cpufreq_generic_get,
	.suspend = restore_firmware_clock,
	.resume = fixed_resume,
	.attr = cpufreq_generic_attr,
};

static int fixed_reboot(struct notifier_block *nb, unsigned long event, void *arg)
{
	struct cpufreq_policy *policy = cpufreq_cpu_get(0);
	int ret;

	if (!policy)
		return NOTIFY_DONE;
	ret = restore_firmware_clock(policy);
	cpufreq_cpu_put(policy);
	return ret ? NOTIFY_BAD : NOTIFY_DONE;
}

static struct notifier_block reboot_notifier = {
	.notifier_call = fixed_reboot,
};

static int fixed_probe(struct platform_device *pdev)
{
	struct device_node *np;
	int ret;

	cpu_dev = &pdev->dev;
	armclk = devm_clk_get(cpu_dev, "armclk");
	if (IS_ERR(armclk))
		return dev_err_probe(cpu_dev, PTR_ERR(armclk), "Missing ARM clock\n");

	np = of_find_compatible_node(NULL, NULL, "samsung,s5pv210-clock");
	if (!np)
		return -ENODEV;
	base = of_iomap(np, 0);
	of_node_put(np);
	if (!base)
		return -ENOMEM;

	fixed_dividers = readl(base + CLK_DIV0) & ~DIV_MASK;
	if (!clock_configuration_valid() ||
	    (readl(base + CLK_DIV0) & DIV_MASK) != DIV_800 ||
	    clk_get_rate(armclk) != 800000000) {
		dev_err(cpu_dev, "Requires firmware 800 MHz ARM / 200 MHz MSYS and MCS=1\n");
		ret = -EINVAL;
		goto unmap;
	}

	ret = cpufreq_register_driver(&fixed_driver);
	if (ret)
		goto unmap;
	ret = register_reboot_notifier(&reboot_notifier);
	if (ret) {
		cpufreq_unregister_driver(&fixed_driver);
		goto unmap;
	}
	return 0;

unmap:
	iounmap(base);
	return ret;
}

static struct platform_driver fixed_platform_driver = {
	.probe = fixed_probe,
	.driver = {
		.name = "s5pv210-cpufreq",
		.suppress_bind_attrs = true,
	},
};
builtin_platform_driver(fixed_platform_driver);
