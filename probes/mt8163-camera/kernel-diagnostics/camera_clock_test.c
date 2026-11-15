// SPDX-License-Identifier: GPL-2.0-only
/* Bounded lab-only proof of the missing Checkers camera clock selection.
 * No sensor, GPIO, ISP register, microphone, framebuffer or image is touched.
 * Load/unload only while every camera owner is idle. See the research record.
 */
#include <linux/clk.h>
#include <linux/err.h>
#include <linux/module.h>
#include <linux/of.h>
#include <linux/of_platform.h>

static bool acknowledge_idle_checkers;
module_param(acknowledge_idle_checkers, bool, 0400);
static struct platform_device *camera;
static struct clk *clock, *parent, *previous;
static bool enabled, reparented;

static void restore(void)
{
	if (enabled)
		clk_disable_unprepare(clock);
	if (reparented && clk_set_parent(clock, previous))
		pr_err("tater camera clock: original parent restore failed\n");
	if (!IS_ERR_OR_NULL(parent))
		clk_put(parent);
	if (!IS_ERR_OR_NULL(clock))
		clk_put(clock);
	if (camera)
		put_device(&camera->dev);
}

static int __init camera_clock_test_init(void)
{
	struct device_node *node, *chosen;
	const char *args;
	int error;
	if (!acknowledge_idle_checkers || !of_machine_is_compatible("mediatek,mt8163"))
		return -EPERM;
	chosen = of_find_node_by_path("/chosen");
	if (!chosen)
		return -ENODEV;
	error = of_property_read_string(chosen, "bootargs", &args);
	if (!error && !strstr(args, "androidboot.product=checkers "))
		error = -ENODEV;
	of_node_put(chosen);
	if (error)
		return error;
	node = of_find_compatible_node(NULL, NULL, "mediatek,mt8163-camera_hw");
	if (!node)
		return -ENODEV;
	camera = of_find_device_by_node(node);
	of_node_put(node);
	if (!camera)
		return -ENODEV;
	clock = clk_get(&camera->dev, "TOP_CAMTG_SEL");
	parent = clk_get(&camera->dev, "TOP_UNIVPLL_D26");
	if (IS_ERR(clock) || IS_ERR(parent)) {
		error = -ENODEV;
		goto failed;
	}
	previous = clk_get_parent(clock);
	if (IS_ERR_OR_NULL(previous) || clk_get_rate(parent) != 48000000) {
		error = -EINVAL;
		goto failed;
	}
	error = clk_set_parent(clock, parent);
	if (error)
		goto failed;
	reparented = true;
	error = clk_prepare_enable(clock);
	if (error)
		goto failed;
	enabled = true;
	if (clk_get_rate(clock) != 48000000) {
		error = -EINVAL;
		goto failed;
	}
	pr_info("tater camera clock: test CAMTG 48 MHz; sensor remains off\n");
	return 0;
failed:
	restore();
	return error;
}

static void __exit camera_clock_test_exit(void)
{
	restore();
	pr_info("tater camera clock: test released and previous parent restored\n");
}
module_init(camera_clock_test_init);
module_exit(camera_clock_test_exit);
MODULE_LICENSE("GPL");
MODULE_DESCRIPTION("Checkers idle-camera CCF selection proof; not a production driver");
