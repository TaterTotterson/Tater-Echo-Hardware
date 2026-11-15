#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-2.0-only
"""Prepare a default-off, manual-start Athena SCP research trial.

Source-only patch for the exact Linux 6.18.56 mtk_scp.c. No device operation.
Other machines retain their upstream behavior. This is not codec qualification.
"""
import argparse
import hashlib
from pathlib import Path

ORIGINAL_SHA256 = '2b29e1a7c0c3de1a72bc9b0935b401146090c40b089600c0d3154691ef3e66e4'
MARKER = 'Tater Athena controlled SCP trial'
PARAMETERS = '''
/* Tater Athena controlled SCP trial: no probe without deliberate opt-in. */
static bool athena_trial;
module_param(athena_trial, bool, 0444);
MODULE_PARM_DESC(athena_trial, "Allow a manually started Athena SCP research trial");

static bool scp_is_athena(struct device *dev)
{
	return of_machine_is_compatible("amzn,athena") &&
		of_device_is_compatible(dev->of_node, "mediatek,mt8183-scp");
}
'''
HOLD_RESET = '''
	/* Quiesce any bootloader SCP before installing an IRQ handler. */
	if (scp_is_athena(dev)) {
		ret = clk_prepare_enable(scp->clk);
		if (ret)
			goto remove_subdev;
		scp->data->scp_reset_assert(scp);
		scp->data->scp_stop(scp);
		ret = scp->data->scp_before_load(scp);
		writel(MT8183_SCP_IPC_INT_BIT | MT8183_SCP_WDT_INT_BIT,
		       scp->cluster->reg_base + MT8183_SCP_TO_HOST);
		clk_disable_unprepare(scp->clk);
		if (ret)
			goto remove_subdev;
	}
'''
REPLACEMENTS = (
    ('#define SECTION_NAME_IPI_BUFFER ".ipi_buffer"\n',
     '#define SECTION_NAME_IPI_BUFFER ".ipi_buffer"\n' + PARAMETERS),
    ('\tscp = rproc->priv;\n\tscp->rproc = rproc;\n',
     '\tif (scp_is_athena(dev)) {\n'
     '\t\trproc->auto_boot = false;\n'
     '\t\trproc->recovery_disabled = true;\n'
     '\t}\n\n\tscp = rproc->priv;\n\tscp->rproc = rproc;\n'),
    ('\tscp_add_rpmsg_subdev(scp);\n\n\tret = devm_request_threaded_irq',
     '\tscp_add_rpmsg_subdev(scp);\n' + HOLD_RESET + '\n\tret = devm_request_threaded_irq'),
    ('\tscp_cluster = devm_kzalloc(dev, sizeof(*scp_cluster), GFP_KERNEL);\n',
     '\tif (scp_is_athena(dev) && !athena_trial)\n'
     '\t\treturn dev_err_probe(dev, -EPERM, "Athena SCP trial is disabled\\n");\n\n'
     '\tscp_cluster = devm_kzalloc(dev, sizeof(*scp_cluster), GFP_KERNEL);\n'),
)


def patch_text(source):
    original = source
    if MARKER in source:
        for old, new in reversed(REPLACEMENTS):
            if original.count(new) != 1:
                raise ValueError('incomplete or modified trial patch')
            original = original.replace(new, old, 1)
    if hashlib.sha256(original.encode()).hexdigest() != ORIGINAL_SHA256:
        raise ValueError('not the reviewed Linux 6.18.56 SCP source')
    result = original
    for old, new in REPLACEMENTS:
        if result.count(old) != 1:
            raise ValueError('source anchor is not unique')
        result = result.replace(old, new, 1)
    return result


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('kernel_tree', type=Path)
    parser.add_argument('--apply', action='store_true', help='modify this explicitly selected source tree')
    args = parser.parse_args()
    path = args.kernel_tree/'drivers/remoteproc/mtk_scp.c'
    try:
        original = path.read_text()
        result = patch_text(original)
        if args.apply and result != original:
            path.write_text(result)
    except (OSError, ValueError) as error:
        parser.exit(1, f'refusing: {error}\n')
    print('already patched' if result == original else ('patched; hardware remains unqualified' if args.apply else 'reviewed source; --apply required to modify'))


if __name__ == '__main__':
    main()
