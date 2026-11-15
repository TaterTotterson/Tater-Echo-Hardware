// SPDX-License-Identifier: GPL-2.0-only
/* Original temporary Athena-only encoder adapter. No SCP or security writes. */
#include <linux/anon_inodes.h>
#include <linux/capability.h>
#include <linux/clk.h>
#include <linux/dma-mapping.h>
#include <linux/file.h>
#include <linux/interrupt.h>
#include <linux/io.h>
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

static bool allow_ap_trial, allow_encoder_mmio;
module_param(allow_ap_trial, bool, 0444);
module_param(allow_encoder_mmio, bool, 0444);
#define MAX_BUFFER (8UL * 1024 * 1024)
#define MAX_TOTAL (64UL * 1024 * 1024)
struct ap_device {
    struct device *dev;
    struct miscdevice misc;
    struct clk *clk;
    void __iomem *regs;
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
    u32 status = readl(ap->regs + 0x5c) & 0x3f;
    unsigned int bit;
    if (!status)
        return IRQ_NONE;
    for (bit = 0; bit < 6; bit++)
        if (status & BIT(bit))
            writel(BIT(bit), ap->regs + 0x60);
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
        /* A mapped encoder may have an outstanding job after cancellation.
         * GPL stock venc_break defines this exact hard-reset register/polarity.
         * Reset before any DMA mappings are freed, never change protection. */
        if (allow_encoder_mmio) {
            writel(0, ap->regs + 0xa8);
            readl(ap->regs + 0xa8);
            writel(1, ap->regs + 0xa8);
            readl(ap->regs + 0xa8);
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
        struct ap_info info = {1, 0, allow_encoder_mmio, ap->powered};
        ret = copy_to_user(user, &info, sizeof info) ? -EFAULT : 0;
    } else if (command == AP_POWER) {
        u32 enabled;
        if (copy_from_user(&enabled, user, sizeof enabled)) ret = -EFAULT;
        else if (enabled > 1) ret = -EINVAL;
        else ret = ap_power(ap, enabled);
    } else if (command == AP_CLEAR) {
        if (!allow_encoder_mmio || !ap->powered) ret = -EPERM;
        else atomic_set(&ap->irq_status, 0);
    } else if (command == AP_WAIT) {
        struct ap_wait arg;
        if (copy_from_user(&arg, user, sizeof arg)) ret = -EFAULT;
        else if (!allow_encoder_mmio || !ap->powered || !arg.timeout_ms ||
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
    if ((vma->vm_flags & VM_EXEC) || !allow_encoder_mmio || !capable(CAP_SYS_RAWIO) || !ap->powered ||
        vma->vm_pgoff != (0x17020000 >> PAGE_SHIFT) ||
        vma->vm_end - vma->vm_start != PAGE_SIZE || !(vma->vm_flags & VM_SHARED))
        return -EPERM;
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
    if (!res || res->start != 0x17020000 || resource_size(res) != 0x1000)
        return -EINVAL;
    ap = devm_kzalloc(&pdev->dev, sizeof(*ap), GFP_KERNEL);
    if (!ap) return -ENOMEM;
    ap->dev = &pdev->dev;
    mutex_init(&ap->mutex);
    init_waitqueue_head(&ap->wait);
    INIT_LIST_HEAD(&ap->retired);
    ap->regs = devm_ioremap_resource(ap->dev, res);
    if (IS_ERR(ap->regs)) return PTR_ERR(ap->regs);
    ap->clk = devm_clk_get(ap->dev, "venc_sel");
    if (IS_ERR(ap->clk)) return PTR_ERR(ap->clk);
    ret = dma_set_mask_and_coherent(ap->dev, DMA_BIT_MASK(32));
    if (ret) return ret;
    ap->irq = platform_get_irq(pdev, 0);
    if (ap->irq < 0) return ap->irq;
    ret = devm_request_irq(ap->dev, ap->irq, ap_irq, IRQF_NO_AUTOEN,
                           "tater-athena-ap-venc", ap);
    if (ret) return ret;
    ap->misc = (struct miscdevice){.minor=MISC_DYNAMIC_MINOR,
        .name="tater-athena-ap-venc", .fops=&ap_ops, .mode=0600};
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
    {.compatible="mediatek,mt8183-vcodec-enc"}, {}
};
static struct platform_driver ap_driver = {
    .probe=ap_probe, .remove=ap_remove,
    .driver={.name="tater-athena-ap-trial", .of_match_table=ap_ids,
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
MODULE_DESCRIPTION("Opt-in exact-Athena bounded AP encoder experiment");
