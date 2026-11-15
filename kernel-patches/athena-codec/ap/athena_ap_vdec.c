// SPDX-License-Identifier: GPL-2.0-only
/* Original temporary Athena-only decoder adapter. No SCP or security writes. */
#include <linux/anon_inodes.h>
#include <linux/capability.h>
#include <linux/clk.h>
#include <linux/dma-mapping.h>
#include <linux/file.h>
#include <linux/interrupt.h>
#include <linux/io.h>
#include <linux/iopoll.h>
#include <linux/of_address.h>
#include <linux/mfd/syscon.h>
#include <linux/regmap.h>
#include <linux/list.h>
#include <linux/miscdevice.h>
#include <linux/mm.h>
#include <linux/module.h>
#include <linux/mutex.h>
#include <linux/of.h>
#include <linux/platform_device.h>
#include <linux/pm_runtime.h>
#include <linux/uaccess.h>
#include <linux/utsname.h>
#include <linux/wait.h>
#include "athena_ap.h"

static bool allow_ap_trial, allow_decoder_mmio;
module_param(allow_ap_trial, bool, 0444);
module_param(allow_decoder_mmio, bool, 0444);
#define MAX_BUFFER (8UL * 1024 * 1024)
#define MAX_TOTAL (64UL * 1024 * 1024)
struct ap_device {
    struct device *dev;
    struct miscdevice misc;
    struct clk *clk;
    void __iomem *regs;
    void __iomem *ld;
    struct regmap *vdecsys;
    bool mapped_regs;
    bool dcm_changed;
    u32 dcm_original;
    int irq;
    bool powered;
    atomic_t opened;
    atomic_long_t allocated;
    atomic_t irq_status;
    wait_queue_head_t wait;
    struct mutex mutex;
    struct list_head retired;
};
struct ap_buffer {
    struct ap_device *ap;
    void *cpu;
    dma_addr_t dma;
    size_t bytes;
    struct list_head node;
};
static void free_buffer(struct ap_buffer *buffer)
{
    dma_free_coherent(buffer->ap->dev, buffer->bytes, buffer->cpu, buffer->dma);
    atomic_long_sub(buffer->bytes, &buffer->ap->allocated);
    kfree(buffer);
    module_put(THIS_MODULE);
}
static void free_retired(struct ap_device *ap)
{
    struct ap_buffer *buffer, *next;
    list_for_each_entry_safe(buffer, next, &ap->retired, node) {
        list_del(&buffer->node);
        free_buffer(buffer);
    }
}
static irqreturn_t ap_irq(int irq, void *opaque)
{
    struct ap_device *ap = opaque;
    u32 active, status;
    if (regmap_read(ap->vdecsys, 0, &active) || (active & BIT(4)))
        return IRQ_NONE;
    status = readl(ap->regs + 0xa4);
    if (!(status & BIT(16)))
        return IRQ_NONE;
    /* Exact MT8183 stock/mainline decoder interrupt acknowledge sequence. */
    writel(readl(ap->regs + 0xa4) | 0x11, ap->regs + 0xa4);
    writel(readl(ap->regs + 0xa4) & ~0x10, ap->regs + 0xa4);
    atomic_or(status, &ap->irq_status);
    wake_up_interruptible(&ap->wait);
    return IRQ_HANDLED;
}
static int ap_power(struct ap_device *ap, bool enabled)
{
    int ret;
    if (ap->powered == enabled)
        return 0;
    if (enabled) {
        ret = pm_runtime_resume_and_get(ap->dev);
        if (ret < 0)
            return ret;
        ret = clk_prepare_enable(ap->clk);
        if (ret) {
            pm_runtime_put_sync(ap->dev);
            return ret;
        }
        atomic_set(&ap->irq_status, 0);
        ap->powered = true;
        enable_irq(ap->irq);
    } else {
        disable_irq(ap->irq);
        /* Source-established MT8183 vdec_break, bounded unlike the stock
         * busy loop. Reset and stop the engine before retiring its DMA. */
        if (allow_decoder_mmio && ap->mapped_regs) {
            u32 status;
            writel(1, ap->regs + 0x100);
            ret = readl_poll_timeout(ap->regs + 0x104, status,
                                      (status & 0x11) == 0x11, 10, 10000);
            if (ret)
                dev_warn(ap->dev, "bounded decoder break timed out\n");
            writel(1, ap->ld + 0x108);
            readl(ap->ld + 0x108);
            writel(0, ap->ld + 0x108);
            readl(ap->ld + 0x108);
        }
        ap->mapped_regs = false;
        if (ap->dcm_changed) {
            ret = regmap_update_bits(ap->vdecsys, 0x18, BIT(0), ap->dcm_original & BIT(0));
            if (ret) dev_err(ap->dev, "decoder DCM restore failed: %d\n", ret);
            ap->dcm_changed = false;
        }
        ap->powered = false;
        clk_disable_unprepare(ap->clk);
        pm_runtime_put_sync(ap->dev);
        free_retired(ap);
    }
    return 0;
}
static int buffer_mmap(struct file *file, struct vm_area_struct *vma)
{
    struct ap_buffer *buffer = file->private_data;
    if ((vma->vm_flags & VM_EXEC) || vma->vm_pgoff || vma->vm_end - vma->vm_start != buffer->bytes ||
        !(vma->vm_flags & VM_SHARED))
        return -EINVAL;
    vm_flags_clear(vma, VM_MAYEXEC);
    return dma_mmap_coherent(buffer->ap->dev, vma, buffer->cpu,
                             buffer->dma, buffer->bytes);
}
static int buffer_release(struct inode *inode, struct file *file)
{
    struct ap_buffer *buffer = file->private_data;
    struct ap_device *ap = buffer->ap;
    /* A close during a powered session cannot revoke in-flight DMA. Defer
     * reclamation until the exclusive control file resets/powers off. This
     * also handles process exit closing buffer and control fds in any order. */
    mutex_lock(&ap->mutex);
    if (ap->powered)
        list_add_tail(&buffer->node, &ap->retired);
    else
        free_buffer(buffer);
    mutex_unlock(&ap->mutex);
    return 0;
}
static const struct file_operations buffer_ops = {
    .mmap = buffer_mmap, .release = buffer_release,
};
static long allocate_buffer(struct ap_device *ap, void __user *user)
{
    struct ap_alloc arg;
    struct ap_buffer *buffer;
    struct file *file;
    int fd, ret;
    if (copy_from_user(&arg, user, sizeof arg))
        return -EFAULT;
    if (!arg.bytes || arg.bytes > MAX_BUFFER || arg.reserved || arg.dma ||
        arg.fd != -1)
        return -EINVAL;
    arg.bytes = PAGE_ALIGN(arg.bytes);
    if (atomic_long_add_return(arg.bytes, &ap->allocated) > MAX_TOTAL) {
        atomic_long_sub(arg.bytes, &ap->allocated);
        return -ENOMEM;
    }
    buffer = kzalloc(sizeof(*buffer), GFP_KERNEL);
    if (!buffer) { ret = -ENOMEM; goto uncharge; }
    buffer->ap = ap;
    buffer->bytes = arg.bytes;
    buffer->cpu = dma_alloc_coherent(ap->dev, buffer->bytes, &buffer->dma, GFP_KERNEL);
    if (!buffer->cpu) { ret = -ENOMEM; goto free_object; }
    if (upper_32_bits(buffer->dma) ||
        (u64)buffer->dma + buffer->bytes > (1ULL << 32)) {
        ret = -ERANGE; goto free_dma;
    }
    memset(buffer->cpu, 0, buffer->bytes);
    fd = get_unused_fd_flags(O_CLOEXEC);
    if (fd < 0) { ret = fd; goto free_dma; }
    __module_get(THIS_MODULE);
    file = anon_inode_getfile("tater-athena-owned-dma", &buffer_ops, buffer, O_RDWR);
    if (IS_ERR(file)) {
        ret = PTR_ERR(file); module_put(THIS_MODULE);
        put_unused_fd(fd); goto free_dma;
    }
    arg.dma = buffer->dma;
    arg.fd = fd;
    if (copy_to_user(user, &arg, sizeof arg)) {
        put_unused_fd(fd); fput(file); return -EFAULT;
    }
    fd_install(fd, file);
    return 0;
free_dma:
    dma_free_coherent(ap->dev, buffer->bytes, buffer->cpu, buffer->dma);
free_object:
    kfree(buffer);
uncharge:
    atomic_long_sub(arg.bytes, &ap->allocated);
    return ret;
}
static long ap_ioctl(struct file *file, unsigned int command, unsigned long value)
{
    struct ap_device *ap = file->private_data;
    void __user *user = (void __user *)value;
    int ret = 0;
    if (!capable(CAP_SYS_RAWIO))
        return -EPERM;
    if (command == AP_ALLOC)
        return allocate_buffer(ap, user);
    mutex_lock(&ap->mutex);
    if (command == AP_INFO) {
        struct ap_info info = {1, 1, allow_decoder_mmio, ap->powered};
        ret = copy_to_user(user, &info, sizeof info) ? -EFAULT : 0;
    } else if (command == AP_POWER) {
        u32 enabled;
        if (copy_from_user(&enabled, user, sizeof enabled)) ret = -EFAULT;
        else if (enabled > 1) ret = -EINVAL;
        else ret = ap_power(ap, enabled);
    } else if (command == AP_DCM) {
        u32 previous, actual;
        if (!allow_decoder_mmio || !ap->powered) ret = -EPERM;
        else {
            ret = regmap_read(ap->vdecsys, 0x18, &previous);
            /* This exact board also reports bit4 at idle. The public golden
             * setting specifies ONLY bit0; preserve bit4, never whole-write1. */
            if (!ret && (previous & ~(BIT(0) | BIT(4)))) ret = -EPERM;
            if (!ret) {
                if (!ap->dcm_changed) ap->dcm_original = previous;
                ap->dcm_changed = true;
                ret = regmap_update_bits(ap->vdecsys, 0x18, BIT(0), BIT(0));
                if (!ret) ret = regmap_read(ap->vdecsys, 0x18, &actual);
                if (!ret && !(actual & BIT(0))) ret = -EIO;
            }
        }
    } else if (command == AP_CLEAR) {
        if (!allow_decoder_mmio || !ap->powered) ret = -EPERM;
        else atomic_set(&ap->irq_status, 0);
    } else if (command == AP_WAIT) {
        struct ap_wait arg;
        if (copy_from_user(&arg, user, sizeof arg)) ret = -EFAULT;
        else if (!allow_decoder_mmio || !ap->powered || !arg.timeout_ms ||
                 arg.timeout_ms > 1000 || arg.status) ret = -EINVAL;
        else {
            ret = wait_event_interruptible_timeout(ap->wait,
                atomic_read(&ap->irq_status), msecs_to_jiffies(arg.timeout_ms));
            if (!ret) ret = -ETIMEDOUT;
            else if (ret > 0) {
                arg.status = atomic_xchg(&ap->irq_status, 0);
                ret = copy_to_user(user, &arg, sizeof arg) ? -EFAULT : 0;
            }
        }
    } else ret = -ENOTTY;
    mutex_unlock(&ap->mutex);
    return ret;
}
static int ap_open(struct inode *inode, struct file *file)
{
    struct miscdevice *misc = file->private_data;
    struct ap_device *ap = container_of(misc, struct ap_device, misc);
    if (!capable(CAP_SYS_RAWIO)) return -EPERM;
    if (atomic_cmpxchg(&ap->opened, 0, 1)) return -EBUSY;
    if (atomic_long_read(&ap->allocated)) {
        atomic_set(&ap->opened, 0);
        return -EBUSY;
    }
    file->private_data = ap;
    return 0;
}
static int ap_release(struct inode *inode, struct file *file)
{
    struct ap_device *ap = file->private_data;
    mutex_lock(&ap->mutex);
    ap_power(ap, false);
    mutex_unlock(&ap->mutex);
    atomic_set(&ap->opened, 0);
    return 0;
}
static int ap_mmap(struct file *file, struct vm_area_struct *vma)
{
    struct ap_device *ap = file->private_data;
    unsigned long physical = vma->vm_pgoff << PAGE_SHIFT;
    if ((physical >> PAGE_SHIFT) != vma->vm_pgoff || (vma->vm_flags & VM_EXEC) ||
        !allow_decoder_mmio || !capable(CAP_SYS_RAWIO) || !ap->powered ||
        vma->vm_end - vma->vm_start != PAGE_SIZE || !(vma->vm_flags & VM_SHARED))
        return -EPERM;
    if (physical == 0x16000000) {
        /* Shared clock/status syscon: read-only; mprotect cannot add writes. */
        if (vma->vm_flags & VM_WRITE)
            return -EPERM;
        vm_flags_clear(vma, VM_MAYWRITE);
    } else {
        /* Only complete, validated DT-owned pages. Partial VP8/VP9 pages
         * 16026000 and 16028000 deliberately have NO physical mapping. */
        if (!((physical >= 0x16020000 && physical <= 0x16025000) ||
              physical == 0x16027000))
            return -EPERM;
        ap->mapped_regs = true;
    }
    vm_flags_set(vma, VM_DONTDUMP | VM_DONTEXPAND | VM_IO | VM_PFNMAP);
    vm_flags_clear(vma, VM_MAYEXEC);
    vma->vm_page_prot = pgprot_noncached(vma->vm_page_prot);
    return io_remap_pfn_range(vma, vma->vm_start, vma->vm_pgoff, PAGE_SIZE, vma->vm_page_prot);
}
static const struct file_operations ap_ops = {
    .owner = THIS_MODULE, .open = ap_open, .release = ap_release,
    .unlocked_ioctl = ap_ioctl, .compat_ioctl = ap_ioctl,
    .mmap = ap_mmap,
};
static int ap_probe(struct platform_device *pdev)
{
    struct ap_device *ap;
    struct resource *res;
    int ret;
    res = platform_get_resource(pdev, IORESOURCE_MEM, 0);
    if (!res || res->start != 0x16020000 || resource_size(res) != 0x1000)
        return -EINVAL;
    ap = devm_kzalloc(&pdev->dev, sizeof(*ap), GFP_KERNEL);
    if (!ap) return -ENOMEM;
    ap->dev = &pdev->dev;
    mutex_init(&ap->mutex);
    init_waitqueue_head(&ap->wait);
    INIT_LIST_HEAD(&ap->retired);
    {
        static const resource_size_t starts[] = {0x16020000, 0x16021000,
            0x16021800, 0x16022000, 0x16023000, 0x16024000, 0x16025000,
            0x16026800, 0x16027000, 0x16027800, 0x16028400};
        static const resource_size_t sizes[] = {0x1000, 0x800, 0x800,
            0x1000, 0x1000, 0x1000, 0x1000, 0x800, 0x800, 0x800, 0x400};
        unsigned i;
        struct device_node *syscon;
        struct resource sysres;
        for (i = 0; i < ARRAY_SIZE(starts); i++) {
            void __iomem *mapped;
            res = platform_get_resource(pdev, IORESOURCE_MEM, i);
            if (!res || res->start != starts[i] || resource_size(res) != sizes[i])
                return -EINVAL;
            mapped = devm_ioremap_resource(ap->dev, res);
            if (IS_ERR(mapped)) return PTR_ERR(mapped);
            if (!i) ap->regs = mapped;
            if (i == 1) ap->ld = mapped;
        }
        if (platform_get_resource(pdev, IORESOURCE_MEM, ARRAY_SIZE(starts)))
            return -EINVAL;
        syscon = of_parse_phandle(pdev->dev.of_node, "mediatek,vdecsys", 0);
        if (!syscon) return -EINVAL;
        ret = of_address_to_resource(syscon, 0, &sysres);
        of_node_put(syscon);
        if (ret || sysres.start != 0x16000000 || resource_size(&sysres) != 0x1000)
            return -EINVAL;
        ap->vdecsys = syscon_regmap_lookup_by_phandle(pdev->dev.of_node, "mediatek,vdecsys");
        if (IS_ERR(ap->vdecsys)) return PTR_ERR(ap->vdecsys);
    }
    ap->clk = devm_clk_get(ap->dev, "vdec");
    if (IS_ERR(ap->clk)) return PTR_ERR(ap->clk);
    ret = dma_set_mask_and_coherent(ap->dev, DMA_BIT_MASK(32));
    if (ret) return ret;
    ap->irq = platform_get_irq(pdev, 0);
    if (ap->irq < 0) return ap->irq;
    ret = devm_request_irq(ap->dev, ap->irq, ap_irq, IRQF_NO_AUTOEN,
                           "tater-athena-ap-vdec", ap);
    if (ret) return ret;
    ap->misc = (struct miscdevice){.minor=MISC_DYNAMIC_MINOR,
        .name="tater-athena-ap-vdec", .fops=&ap_ops, .mode=0600};
    platform_set_drvdata(pdev, ap);
    pm_runtime_enable(ap->dev);
    ret = misc_register(&ap->misc);
    if (ret) pm_runtime_disable(ap->dev);
    return ret;
}
static void ap_remove(struct platform_device *pdev)
{
    struct ap_device *ap = platform_get_drvdata(pdev);
    misc_deregister(&ap->misc);
    mutex_lock(&ap->mutex);
    ap_power(ap, false);
    mutex_unlock(&ap->mutex);
    pm_runtime_disable(ap->dev);
}
static const struct of_device_id ap_ids[] = {
    {.compatible="mediatek,mt8183-vcodec-dec"}, {}
};
static struct platform_driver ap_driver = {
    .probe=ap_probe, .remove=ap_remove,
    .driver={.name="tater-athena-ap-decoder-trial", .of_match_table=ap_ids,
             .suppress_bind_attrs=true},
};
static int __init ap_init(void)
{
    if (!allow_ap_trial || !of_machine_is_compatible("amzn,athena") ||
        !of_machine_is_compatible("mediatek,mt8183") ||
        strcmp(init_utsname()->version, "#1 SMP PREEMPT 2026-11-14 10:00:00 UTC"))
        return -EPERM;
    return platform_driver_register(&ap_driver);
}
static void __exit ap_exit(void) { platform_driver_unregister(&ap_driver); }
module_init(ap_init);
module_exit(ap_exit);
MODULE_LICENSE("GPL");
MODULE_DESCRIPTION("Opt-in exact-Athena bounded AP decoder experiment");
