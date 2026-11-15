// SPDX-License-Identifier: GPL-2.0
/* Read-only MT8163 VENC/JPEG register snapshot for bounded bring-up. */

#include <linux/io.h>
#include <linux/module.h>
#include <linux/proc_fs.h>
#include <linux/seq_file.h>

#define DRIVER_NAME "mt8163_jpeg_regs"
#define VENC_CLOCK_BASE 0x17000000U
#define VENC_CLOCK_SIZE 0x10U
#define JPEG_ENCODER_BASE 0x17003000U
#define JPEG_ENCODER_SIZE 0x400U
#define JPEG_DECODER_BASE 0x17004000U
#define JPEG_DECODER_SIZE 0x400U

struct named_register {
	const char *name;
	u32 offset;
};

static const struct named_register decoder_registers[] = {
	{"reset", 0x090},
	{"debug_mode", 0x0c8},
	{"brz_factor", 0x0f8},
	{"du_sample", 0x0fc},
	{"dest_y", 0x140},
	{"dest_u", 0x144},
	{"dest_v", 0x148},
	{"stride_y", 0x158},
	{"stride_uv", 0x15c},
	{"image_stride_y", 0x160},
	{"image_stride_uv", 0x164},
	{"wdma_control", 0x16c},
	{"pause_mcu", 0x170},
	{"operation_mode", 0x17c},
	{"debug0", 0x180},
	{"debug1", 0x184},
	{"file_address", 0x200},
	{"component_id", 0x20c},
	{"total_mcu", 0x210},
	{"component0_du", 0x224},
	{"du_control", 0x23c},
	{"trigger", 0x240},
	{"file_write_pointer", 0x248},
	{"file_total_size", 0x24c},
	{"quant_table_id", 0x270},
	{"interrupt_status", 0x274},
	{"status", 0x278},
	{"mcu_count", 0x294},
	{"smi_debug0", 0x304},
	{"smi_debug1", 0x308},
	{"smi_debug2", 0x30c},
	{"smi_debug3", 0x310},
	{"ultra_threshold", 0x318},
	{"irq_enable", 0x31c},
};

static const struct named_register clock_registers[] = {
	{"clock_gate_status", 0x000},
};

static const struct named_register encoder_registers[] = {
	{"reset", 0x100},
	{"control", 0x104},
	{"quality", 0x108},
	{"block_number", 0x10c},
	{"block_count", 0x110},
	{"interrupt_status", 0x11c},
	{"destination", 0x120},
	{"dma_address", 0x124},
	{"stall_address", 0x128},
	{"offset_address", 0x138},
	{"current_dma_address", 0x13c},
	{"restart_mcu_number", 0x150},
	{"image_size", 0x154},
	{"global_ultra_threshold", 0x158},
	{"debug0", 0x160},
	{"debug1", 0x164},
	{"total_cycle", 0x168},
	{"byte_offset_mask", 0x16c},
	{"source_luma", 0x170},
	{"source_chroma", 0x174},
	{"stride", 0x178},
	{"image_stride", 0x17c},
	{"memory_cycle", 0x208},
	{"smi_debug0", 0x304},
	{"smi_debug1", 0x308},
	{"smi_debug2", 0x30c},
	{"smi_debug3", 0x310},
	{"codec_select", 0x314},
	{"ultra_threshold", 0x318},
};

static void __iomem *clock_base;
static void __iomem *encoder_base;
static void __iomem *decoder_base;
static struct proc_dir_entry *proc_entry;

static void registers_emit(struct seq_file *stream, void __iomem *base,
			   const struct named_register *registers,
			   size_t register_count)
{
	size_t index;

	for (index = 0; index < register_count; ++index) {
		const struct named_register *reg = &registers[index];

		if (index)
			seq_putc(stream, ',');
		seq_printf(stream, "\"%s\":\"0x%08x\"", reg->name,
			   readl(base + reg->offset));
	}
}

static int registers_show(struct seq_file *stream, void *unused)
{
	seq_puts(stream, "{\"venc_clock\":{\"base\":\"0x17000000\",\"registers\":{");
	registers_emit(stream, clock_base, clock_registers,
		       ARRAY_SIZE(clock_registers));
	seq_puts(stream, "}},\"encoder\":{\"base\":\"0x17003000\",\"registers\":{");
	registers_emit(stream, encoder_base, encoder_registers,
		       ARRAY_SIZE(encoder_registers));
	seq_puts(stream, "}},\"decoder\":{\"base\":\"0x17004000\",\"registers\":{");
	registers_emit(stream, decoder_base, decoder_registers,
		       ARRAY_SIZE(decoder_registers));
	seq_puts(stream, "}}}\n");
	return 0;
}

static int registers_open(struct inode *inode, struct file *file)
{
	return single_open(file, registers_show, NULL);
}

static const struct file_operations registers_operations = {
	.owner = THIS_MODULE,
	.open = registers_open,
	.read = seq_read,
	.llseek = seq_lseek,
	.release = single_release,
};

static int __init registers_init(void)
{
	clock_base = ioremap(VENC_CLOCK_BASE, VENC_CLOCK_SIZE);
	if (!clock_base)
		return -ENOMEM;
	encoder_base = ioremap(JPEG_ENCODER_BASE, JPEG_ENCODER_SIZE);
	if (!encoder_base) {
		iounmap(clock_base);
		return -ENOMEM;
	}
	decoder_base = ioremap(JPEG_DECODER_BASE, JPEG_DECODER_SIZE);
	if (!decoder_base) {
		iounmap(encoder_base);
		iounmap(clock_base);
		return -ENOMEM;
	}
	proc_entry = proc_create(DRIVER_NAME, 0444, NULL,
				 &registers_operations);
	if (!proc_entry) {
		iounmap(decoder_base);
		iounmap(encoder_base);
		iounmap(clock_base);
		return -ENOMEM;
	}
	return 0;
}

static void __exit registers_exit(void)
{
	proc_remove(proc_entry);
	iounmap(decoder_base);
	iounmap(encoder_base);
	iounmap(clock_base);
}

module_init(registers_init);
module_exit(registers_exit);

MODULE_DESCRIPTION("Read-only MT8163 VENC/JPEG register snapshot");
MODULE_AUTHOR("Tater Hardware Research");
MODULE_LICENSE("GPL");
