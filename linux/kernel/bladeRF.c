#include <linux/module.h>
#include <linux/kernel.h>
#include <linux/init.h>
#include <linux/usb.h>
#include <linux/slab.h>
#include <linux/scatterlist.h>
#include <linux/dma-mapping.h>
#include <linux/sched.h>
#include <linux/mutex.h>
#include <linux/wait.h>
#include "../../common/bladeRF.h"

struct data_buffer {
    struct urb  *urb;
    void        *addr;
    dma_addr_t   dma;
    int          valid;
};

typedef struct {
    struct usb_device    *udev;
    struct usb_interface *interface;

    int                   intnum;

    int                   rx_en;
    spinlock_t            data_in_lock;
    unsigned int          data_in_consumer_idx;
    unsigned int          data_in_producer_idx;
    atomic_t              data_in_cnt;
    atomic_t              data_in_inflight;
    struct data_buffer    data_in_bufs[NUM_DATA_URB];
    struct usb_anchor     data_in_anchor;
    wait_queue_head_t     data_in_wait;

    int                   tx_en;
    spinlock_t            data_out_lock;
    unsigned int          data_out_consumer_idx;
    unsigned int          data_out_producer_idx;
    atomic_t              data_out_cnt;
    atomic_t              data_out_inflight;
    struct data_buffer    data_out_bufs[NUM_DATA_URB];
    struct usb_anchor     data_out_anchor;
    wait_queue_head_t     data_out_wait;

    struct semaphore      config_sem;

    int bytes;
    int debug;
} bladerf_device_t;

static struct usb_driver bladerf_driver;

// USB PID-VID table
static struct usb_device_id bladerf_table[] = {
    { USB_DEVICE(USB_NUAND_VENDOR_ID, USB_NUAND_BLADERF_PRODUCT_ID) },
    { } /* Terminate entry */
};
MODULE_DEVICE_TABLE(usb, bladerf_table);

static int __submit_rx_urb(bladerf_device_t *dev, unsigned int flags) {
    struct urb *urb;
    unsigned long irq_flags;
    int ret;

    ret = 0;
    while (atomic_read(&dev->data_in_inflight) < NUM_CONCURRENT && atomic_read(&dev->data_in_cnt) < NUM_DATA_URB) {
        spin_lock_irqsave(&dev->data_in_lock, irq_flags);
        urb = dev->data_in_bufs[dev->data_in_producer_idx].urb;

        dev->data_in_producer_idx++;
        dev->data_in_producer_idx &= (NUM_DATA_URB - 1);
        atomic_inc(&dev->data_in_inflight);

        usb_anchor_urb(urb, &dev->data_in_anchor);
        spin_unlock_irqrestore(&dev->data_in_lock, irq_flags);
        ret = usb_submit_urb(urb, GFP_ATOMIC);
    }

    return ret;
}

static void __bladeRF_write_cb(struct urb *urb);
static void __bladeRF_read_cb(struct urb *urb) {
    bladerf_device_t *dev;
    unsigned char *buf;

    buf = (unsigned char *)urb->transfer_buffer;
    dev = (bladerf_device_t *)urb->context;
    usb_unanchor_urb(urb);
    atomic_dec(&dev->data_in_inflight);
    dev->bytes += DATA_BUF_SZ;
    atomic_inc(&dev->data_in_cnt);

    if (dev->rx_en)
        __submit_rx_urb(dev, GFP_ATOMIC);
    wake_up_interruptible(&dev->data_in_wait);
}

static int bladerf_start(bladerf_device_t *dev) {
    int i;
    void *buf;
    struct urb *urb;

    dev->rx_en = 0;
    atomic_set(&dev->data_in_cnt, 0);
    dev->data_in_consumer_idx = 0;
    dev->data_in_producer_idx = 0;

    for (i = 0; i < NUM_DATA_URB; i++) {
        buf = usb_alloc_coherent(dev->udev, DATA_BUF_SZ,
                GFP_KERNEL, &dev->data_in_bufs[i].dma);
        memset(buf, 0, DATA_BUF_SZ);
        if (!buf) {
            dev_err(&dev->interface->dev, "Could not allocate data IN buffer\n");
            return -1;
        }

        dev->data_in_bufs[i].addr = buf;

        urb = usb_alloc_urb(0, GFP_KERNEL);
        if (!buf) {
            dev_err(&dev->interface->dev, "Could not allocate data IN URB\n");
            return -1;
        }

        dev->data_in_bufs[i].urb = urb;

        usb_fill_bulk_urb(urb, dev->udev, usb_rcvbulkpipe(dev->udev, 1),
                dev->data_in_bufs[i].addr, DATA_BUF_SZ, __bladeRF_read_cb, dev);

        urb->transfer_flags |= URB_NO_TRANSFER_DMA_MAP;
        urb->transfer_dma = dev->data_in_bufs[i].dma;
    }

    dev->tx_en = 0;
    atomic_set(&dev->data_out_cnt, 0);
    dev->data_out_consumer_idx = 0;
    dev->data_out_producer_idx = 0;

    for (i = 0; i < NUM_DATA_URB; i++) {
        buf = usb_alloc_coherent(dev->udev, DATA_BUF_SZ,
                GFP_KERNEL, &dev->data_out_bufs[i].dma);
        memset(buf, 0, DATA_BUF_SZ);
        if (!buf) {
            dev_err(&dev->interface->dev, "Could not allocate data OUT buffer\n");
            return -1;
        }

        dev->data_out_bufs[i].addr = buf;

        urb = usb_alloc_urb(0, GFP_KERNEL);
        if (!buf) {
            dev_err(&dev->interface->dev, "Could not allocate data OUT URB\n");
            return -1;
        }

        dev->data_out_bufs[i].urb = urb;
        dev->data_out_bufs[i].valid = 0;

        usb_fill_bulk_urb(urb, dev->udev, usb_sndbulkpipe(dev->udev, 1),
                dev->data_out_bufs[i].addr, DATA_BUF_SZ, __bladeRF_write_cb, dev);

        urb->transfer_flags |= URB_NO_TRANSFER_DMA_MAP;
        urb->transfer_dma = dev->data_out_bufs[i].dma;
    }


    return 0;
}

static void bladerf_stop(bladerf_device_t *dev) {
    int i;
    for (i = 0; i < NUM_DATA_URB; i++) {
        usb_free_coherent(dev->udev, DATA_BUF_SZ, dev->data_in_bufs[i].addr, dev->data_in_bufs[i].dma);
        usb_free_urb(dev->data_in_bufs[i].urb);
        usb_free_coherent(dev->udev, DATA_BUF_SZ, dev->data_out_bufs[i].addr, dev->data_out_bufs[i].dma);
        usb_free_urb(dev->data_out_bufs[i].urb);
    }
}

int __bladerf_snd_cmd(bladerf_device_t *dev, int cmd, void *ptr, __u16 len);
static int disable_tx(bladerf_device_t *dev) {
    int ret;
    unsigned int val;
    val = 0;

    if (dev->intnum != 1)
        return -1;

    dev->tx_en = 0;

    usb_kill_anchored_urbs(&dev->data_out_anchor);

    ret = __bladerf_snd_cmd(dev, BLADE_USB_CMD_RF_TX, &val, sizeof(val));
    if (ret < 0)
        goto err_out;

    ret = 0;

err_out:
    return ret;
}

static int enable_tx(bladerf_device_t *dev) {
    int ret;
    unsigned int val;
    val = 1;

    if (dev->intnum != 1)
        return -1;

    ret = __bladerf_snd_cmd(dev, BLADE_USB_CMD_RF_TX, &val, sizeof(val));
    if (ret < 0)
        goto err_out;

    ret = 0;
    dev->tx_en = 1;

err_out:
    return ret;
}

static int disable_rx(bladerf_device_t *dev) {
    int ret;
    unsigned int val;
    val = 0;

    if (dev->intnum != 1)
        return -1;

    dev->rx_en = 0;

    usb_kill_anchored_urbs(&dev->data_in_anchor);

    ret = __bladerf_snd_cmd(dev, BLADE_USB_CMD_RF_RX, &val, sizeof(val));
    if (ret < 0)
        goto err_out;

    ret = 0;
    atomic_set(&dev->data_in_cnt, 0);
    dev->data_in_consumer_idx = 0;
    dev->data_in_producer_idx = 0;

err_out:
    return ret;
}

static int enable_rx(bladerf_device_t *dev) {
    int ret;
    int i;
    unsigned int val;
    val = 1;

    if (dev->intnum != 1)
        return -1;

    ret = __bladerf_snd_cmd(dev, BLADE_USB_CMD_RF_RX, &val, sizeof(val));
    if (ret < 0)
        goto err_out;

    ret = 0;
    dev->rx_en = 1;

    for (i = 0; i < NUM_CONCURRENT; i++) {
        if ((ret = __submit_rx_urb(dev, 0)) < 0) {
            dev_err(&dev->interface->dev, "Error submitting initial RX URBs (%d/%d), error=%d\n", i, NUM_CONCURRENT, ret);
            break;
        }
    }

err_out:
    return ret;
}

static ssize_t bladerf_read(struct file *file, char __user *buf, size_t count, loff_t *ppos)
{
    ssize_t ret = 0;
    bladerf_device_t *dev;
    unsigned long flags;
    int read;

    dev = (bladerf_device_t *)file->private_data;
    if (dev->intnum != 1) {
        return -1;
    }

    if (!dev->rx_en) {
        if (enable_rx(dev)) {
            return -EINVAL;
        }
    }
    read = 0;

    while (!read) {
        int reread;
        reread = atomic_read(&dev->data_in_cnt);

        if (reread) {
            unsigned int idx;

            spin_lock_irqsave(&dev->data_in_lock, flags);
            atomic_dec(&dev->data_in_cnt);
            idx = dev->data_in_consumer_idx++;
            dev->data_in_consumer_idx &= (NUM_DATA_URB - 1);

            spin_unlock_irqrestore(&dev->data_in_lock, flags);

            ret = copy_to_user(buf, dev->data_in_bufs[idx].addr, DATA_BUF_SZ);

            if (!ret)
                ret = DATA_BUF_SZ;

            break;

        } else {
            ret = wait_event_interruptible(dev->data_in_wait, atomic_read(&dev->data_in_cnt));
            if (ret < 0)
                break;

        }
    }

    return ret;
}

static int __submit_tx_urb(bladerf_device_t *dev) {
    struct urb *urb;
    struct data_buffer *db;
    unsigned long flags;

    int ret = 0;

    while (atomic_read(&dev->data_out_inflight) < NUM_CONCURRENT && atomic_read(&dev->data_out_cnt)) {
        spin_lock_irqsave(&dev->data_out_lock, flags);
        db = &dev->data_out_bufs[dev->data_out_consumer_idx];
        urb = db->urb;

        if (!db->valid)
            break;

        dev->data_out_consumer_idx++;
        dev->data_out_consumer_idx &= (NUM_DATA_URB - 1);

        atomic_dec(&dev->data_out_cnt);

        usb_anchor_urb(urb, &dev->data_out_anchor);
        spin_unlock_irqrestore(&dev->data_out_lock, flags);
        ret = usb_submit_urb(urb, GFP_ATOMIC);
        if (!ret)
            atomic_inc(&dev->data_out_inflight);
        else
            break;
    }

    return ret;
}

static void __bladeRF_write_cb(struct urb *urb)
{
    bladerf_device_t *dev;

    dev = (bladerf_device_t *)urb->context;

    usb_unanchor_urb(urb);

    atomic_dec(&dev->data_out_inflight);
    __submit_tx_urb(dev);
    dev->bytes += DATA_BUF_SZ;
    wake_up_interruptible(&dev->data_out_wait);
}

static ssize_t bladerf_write(struct file *file, const char *user_buf, size_t count, loff_t *ppos)
{
    bladerf_device_t *dev;
    unsigned long flags;
    char *buf = NULL;
    struct data_buffer *db = NULL;
    unsigned int idx;
    int reread;

    dev = (bladerf_device_t *)file->private_data;

    if (dev->intnum == 0) {
        int ret, llen;
        buf = (char *)kmalloc(count, GFP_KERNEL);
        if (copy_from_user(buf, user_buf, count)) {
            ret = -EFAULT;
            goto err_out;
        }
        ret = usb_bulk_msg(dev->udev, usb_sndbulkpipe(dev->udev, 2), buf, count, &llen, BLADE_USB_TIMEOUT_MS);
err_out:
        kfree(buf);

        if (ret < 0)
            return ret;
        else
            return llen;
    }

    reread = atomic_read(&dev->data_out_cnt);
    if (reread >= NUM_DATA_URB) {
         wait_event_interruptible(dev->data_out_wait, atomic_read(&dev->data_out_cnt) < NUM_DATA_URB);
	    reread = atomic_read(&dev->data_out_cnt);
    }

    spin_lock_irqsave(&dev->data_out_lock, flags);

    idx = dev->data_out_producer_idx++;
    dev->data_out_producer_idx &= (NUM_DATA_URB - 1);
    db = &dev->data_out_bufs[idx];
    atomic_inc(&dev->data_out_cnt);

    spin_unlock_irqrestore(&dev->data_out_lock, flags);

    if (copy_from_user(db->addr, user_buf, count)) {
        return -EFAULT;
    }

    db->valid = 1;

    __submit_tx_urb(dev);
    if (!dev->tx_en)
        enable_tx(dev);

    return count;
}


int __bladerf_rcv_cmd(bladerf_device_t *dev, int cmd, void *ptr, __u16 len) {
    int tries = 3;
    int retval;

    do {
        retval = usb_control_msg(dev->udev, usb_rcvctrlpipe(dev->udev, 0),
                cmd, BLADE_USB_TYPE_IN, 0, 0,
                ptr, len, BLADE_USB_TIMEOUT_MS);
        if (retval < 0) {
            dev_err(&dev->interface->dev, "Error in %s calling usb_control_msg()"
                    " with error %d, %d tries left\n", __func__, retval, tries);
        }
    } while ((retval < 0) && --tries);

    return retval;
}

int __bladerf_rcv_one_word(bladerf_device_t *dev, int cmd, void __user *arg) {
    unsigned int buf;
    int retval = -EINVAL;

    if (!arg) {
        retval = -EFAULT;
        goto err_out;
    }

    retval = __bladerf_rcv_cmd(dev, cmd, &buf, sizeof(buf));

    if (retval >= 0) {
        buf = le32_to_cpu(buf);
        retval = copy_to_user(arg, &buf, sizeof(buf));
    }

    if (retval >= 0) {
        retval = 0;
    }

err_out:
    return retval;
}

int __bladerf_snd_cmd(bladerf_device_t *dev, int cmd, void *ptr, __u16 len) {
    int tries = 3;
    int retval;

    do {
        printk("usb_control_msg(ptr=%p) len=%d\n", ptr, len);
        retval = usb_control_msg(dev->udev, usb_sndctrlpipe(dev->udev, 0),
                cmd, BLADE_USB_TYPE_OUT, 0, 0,
                ptr, len, BLADE_USB_TIMEOUT_MS);
        printk("done usb_control_msg() = %d\n", retval);
        if (retval < 0) {
            dev_err(&dev->interface->dev, "Error in %s calling usb_control_msg()"
                    " with error %d, %d tries left\n", __func__, retval, tries);
        }
    } while ((retval < 0) && --tries);

    return retval;
}

int __bladerf_snd_one_word(bladerf_device_t *dev, int cmd, void __user *arg) {
    unsigned int buf;
    int retval = -EINVAL;

    if (!arg) {
        retval = -EFAULT;
        goto err_out;
    }

    if ((retval = copy_from_user(&buf, arg, sizeof(buf))))
        goto err_out;
    buf = cpu_to_le32(buf);

    retval = __bladerf_snd_cmd(dev, cmd, &buf, sizeof(buf));

err_out:
    return retval;
}

long bladerf_ioctl(struct file *file, unsigned int cmd, unsigned long arg)
{
    bladerf_device_t *dev;
    void __user *data;
    struct bladeRF_version ver;
    int ret;
    int retval = -EINVAL;
    int sz, nread, nwrite;
    struct uart_cmd spi_reg;
    int sectors_to_wipe, sector_idx;
    int pages_to_write, page_idx;
    int pages_to_read;
    int check_idx, check_error;
    int count, tries;
    int targetdev;

    unsigned char buf[1024];
    struct bladeRF_firmware brf_fw;
    unsigned char *fw_buf;

    dev = file->private_data;
    data = (void __user *)arg;


    switch (cmd) {
        case BLADE_QUERY_VERSION:
            retval = __bladerf_rcv_cmd(dev, BLADE_USB_CMD_QUERY_VERSION, &ver, sizeof(ver));
            if (retval >= 0) {
                ver.major = le16_to_cpu(ver.major);
                ver.minor = le16_to_cpu(ver.minor);
                retval = copy_to_user(data, &ver, sizeof(struct bladeRF_version));
            }
            break;

        case BLADE_QUERY_FPGA_STATUS:
            retval = __bladerf_rcv_one_word(dev, BLADE_USB_CMD_QUERY_FPGA_STATUS, data);
            break;

        case BLADE_BEGIN_PROG:
            if (dev->intnum != 0) {
                ret = usb_set_interface(dev->udev, 0,0);
                dev->intnum = 0;
            }

            retval = __bladerf_rcv_one_word(dev, BLADE_USB_CMD_BEGIN_PROG, data);
            break;

        case BLADE_END_PROG:
            // TODO: send another 2 DCLK cycles to ensure compliance with C4's boot procedure
            retval = __bladerf_rcv_one_word(dev, BLADE_USB_CMD_QUERY_FPGA_STATUS, data);

            if (!retval) {
                ret = usb_set_interface(dev->udev, 1,0);
                dev->intnum = 1;
            }
            break;

        case BLADE_UPGRADE_FW:

            if (dev->intnum != 2) {
                retval = usb_set_interface(dev->udev, 2,0);

                if (retval)
                    break;

                dev->intnum = 2;
            }

            if (copy_from_user(&brf_fw, data, sizeof(struct bladeRF_firmware))) {
                return -EFAULT;
            }

            brf_fw.len = ((brf_fw.len + 255) / 256) * 256;

            fw_buf = kzalloc(brf_fw.len, GFP_KERNEL);
            if (!fw_buf)
                goto leave_fw;

            if (copy_from_user(fw_buf, brf_fw.ptr, brf_fw.len)) {
                retval = -EFAULT;
                goto leave_fw;
            }

            retval = -ENODEV;

            sectors_to_wipe = (brf_fw.len + 0xffff) / 0x10000;
            printk("Going to wipe %d sectors\n", sectors_to_wipe);
            for (sector_idx = 0; sector_idx < sectors_to_wipe; sector_idx++) {
                printk("Erasing sector %d... ", sector_idx);
                retval = usb_control_msg(dev->udev, usb_rcvctrlpipe(dev->udev, 0),
                        BLADE_USB_CMD_FLASH_ERASE, BLADE_USB_TYPE_IN, 0x0000, sector_idx,
                        &ret, 4, BLADE_USB_TIMEOUT_MS * 100);
                printk("- erased\n");

                if (retval != 4) {
                    goto leave_fw;
                }
                ret = le32_to_cpu(ret);
                if (ret != 1) {
                    printk("Unable to erase previous sector, quitting\n");
                    goto leave_fw;
                }
            }

            sz = 0;
            if (dev->udev->speed == USB_SPEED_HIGH) {
                sz = 64;
            } else if (dev->udev->speed == USB_SPEED_SUPER) {
                sz = 256;
            }

            pages_to_write = (brf_fw.len + 255) / 0x100;
            for (page_idx = pages_to_write - 1; page_idx >= 0; page_idx--) {
                nwrite = 0;
                do {
                    retval = usb_control_msg(dev->udev, usb_rcvctrlpipe(dev->udev, 0),
                            BLADE_USB_CMD_FLASH_WRITE, BLADE_USB_TYPE_OUT, 0x0000, page_idx,
                            &fw_buf[page_idx * 256 + nwrite], sz, BLADE_USB_TIMEOUT_MS);
                    nwrite += sz;
                } while (nwrite != 256);
            }

            pages_to_read = (brf_fw.len + 255) / 0x100;

            check_error = 0;
            for (page_idx = 0; page_idx < pages_to_read; page_idx++) {
                nread = 0;
                do {
                    retval = usb_control_msg(dev->udev, usb_rcvctrlpipe(dev->udev, 0),
                            BLADE_USB_CMD_FLASH_READ, BLADE_USB_TYPE_IN, 0x0000, page_idx,
                            &buf[nread], sz, BLADE_USB_TIMEOUT_MS);
                    nread += sz;
                } while (nread != 256);

                for (check_idx = 0; check_idx < 256; check_idx++) {
                    if (buf[check_idx] != fw_buf[page_idx * 256 + check_idx]) {
                        printk("ERROR: bladeRF firmware verification detected a mismatch at byte offset 0x%.8x\n", page_idx * 256 + check_idx);
                        printk("ERROR: expected byte 0x%.2X, got 0x%.2X\n", fw_buf[page_idx * 256 + check_idx], buf[check_idx]);
                        check_error = 1;
                        goto leave_fw;
                    }
                }
            }
            retval = 0;
            printk("SUCCESSFULLY VERIFIED\n");

leave_fw:
            kfree(fw_buf);
            break;

        case BLADE_CHECK_PROG:
            retval = 0;
            printk("ok %d\n", dev->intnum);
            if (dev->intnum == 0) {
                retval = __bladerf_rcv_cmd(dev, BLADE_USB_CMD_QUERY_FPGA_STATUS, &ret, sizeof(ret));
                printk("retval =%d     ret=%d\n", retval, ret);
                if (retval >= 0 && ret) {
                    retval = 0;
                    ret = usb_set_interface(dev->udev, 1,0);
                    dev->intnum = 1;
                    retval = copy_to_user((void __user *)arg, &ret, sizeof(ret));
                    printk("ok changed intf\n");
                }
            }
            break;

        case BLADE_RF_RX:
            if (dev->intnum != 1) {
                dev_err(&dev->interface->dev, "Cannot enable RX from config mode\n");
                retval = -1;
                break;
            }

            printk("RF_RX!\n");
            retval = __bladerf_snd_one_word(dev, BLADE_USB_CMD_RF_RX, data);
            break;

        case BLADE_RF_TX:
            if (dev->intnum != 1) {
                dev_err(&dev->interface->dev, "Cannot enable TX from config mode\n");
                retval = -1;
                break;
            }

            printk("RF_TX!\n");
            retval = __bladerf_snd_one_word(dev, BLADE_USB_CMD_RF_TX, data);
            break;

        case BLADE_LMS_WRITE:
        case BLADE_LMS_READ:
        case BLADE_SI5338_WRITE:
        case BLADE_SI5338_READ:
        case BLADE_GPIO_WRITE:
        case BLADE_GPIO_READ:
        case BLADE_VCTCXO_WRITE:

            if (copy_from_user(&spi_reg, (void __user *)arg, sizeof(struct uart_cmd))) {
                retval = -EFAULT;
                break;
            }

            nread = count = 16;
            memset(buf, 0, 20);
            buf[0] = 'N';

            targetdev = UART_PKT_DEV_SI5338;
            if (cmd == BLADE_GPIO_WRITE || cmd == BLADE_GPIO_READ)
                targetdev = UART_PKT_DEV_GPIO;
            if (cmd == BLADE_LMS_WRITE || cmd == BLADE_LMS_READ)
                targetdev = UART_PKT_DEV_LMS;
            if (cmd == BLADE_VCTCXO_WRITE)
                targetdev = UART_PKT_MODE_DIR_WRITE;

            if (cmd == BLADE_LMS_WRITE || cmd == BLADE_GPIO_WRITE || cmd == BLADE_SI5338_WRITE || cmd == BLADE_VCTCXO_WRITE) {
                buf[1] = UART_PKT_MODE_DIR_WRITE | targetdev | 0x01;
                buf[2] = spi_reg.addr;
                buf[3] = spi_reg.data;
            } else if (cmd == BLADE_LMS_READ || cmd == BLADE_GPIO_READ || cmd == BLADE_SI5338_READ) {
                buf[1] = UART_PKT_MODE_DIR_READ | targetdev | 0x01;
                buf[2] = spi_reg.addr;
                buf[3] = 0xff;
            }

            retval = usb_bulk_msg(dev->udev, usb_sndbulkpipe(dev->udev, 2), buf, count, &nread, BLADE_USB_TIMEOUT_MS);
            memset(buf, 0, 20);

            tries = 3;
            do {
                retval = usb_bulk_msg(dev->udev, usb_rcvbulkpipe(dev->udev, 0x82), buf, count, &nread, 1);
            } while(retval == -ETIMEDOUT && tries--);

            spi_reg.addr = buf[2];
            spi_reg.data = buf[3];

            retval = copy_to_user((void __user *)arg, &spi_reg, count);
            break;

    }

    return retval;
}

static int bladerf_open(struct inode *inode, struct file *file)
{
    bladerf_device_t *dev;
    struct usb_interface *interface;
    int subminor;

    subminor = iminor(inode);

    interface = usb_find_interface(&bladerf_driver, subminor);
    if (interface == NULL) {
        pr_err("%s - error, cannot find device for minor %d\n", __func__, subminor);
        return -ENODEV;
    }

    dev = usb_get_intfdata(interface);
    if (dev == NULL) {
        return -ENODEV;
    }

    file->private_data = dev;

    return 0;
}

static int bladerf_release(struct inode *inode, struct file *file)
{
    bladerf_device_t *dev;

    dev = (bladerf_device_t *)file->private_data;
    if (dev->debug) {
        dev->debug--;
        return 0;
    }

    if (dev->tx_en) {
        disable_tx(dev);
    }

    if (dev->rx_en) {
        disable_rx(dev);
    }

    return 0;
}

static struct file_operations bladerf_fops = {
    .owner    =  THIS_MODULE,
    .read     =  bladerf_read,
    .write    =  bladerf_write,
    .unlocked_ioctl = bladerf_ioctl,
    .open     =  bladerf_open,
    .release  =  bladerf_release,
};


#define USB_NUAND_BLADERF_MINOR_BASE 193

static struct usb_class_driver bladerf_class = {
    .name       = "bladerf%d",
    .fops       = &bladerf_fops,
    .minor_base = USB_NUAND_BLADERF_MINOR_BASE,
};

static int bladerf_probe(struct usb_interface *interface,
        const struct usb_device_id *id)
{
    bladerf_device_t *dev;
    int retval;

    dev = kzalloc(sizeof(bladerf_device_t), GFP_KERNEL);
    if (dev == NULL) {
        dev_err(&interface->dev, "Out of memory\n");
        goto error_oom;
    }


    spin_lock_init(&dev->data_in_lock);
    spin_lock_init(&dev->data_out_lock);
    dev->udev = usb_get_dev(interface_to_usbdev(interface));
    dev->interface = interface;
    dev->intnum = 0;
    dev->bytes = 0;
    dev->debug = 0;

    atomic_set(&dev->data_in_inflight, 0);
    atomic_set(&dev->data_out_inflight, 0);

    init_usb_anchor(&dev->data_in_anchor);
    init_waitqueue_head(&dev->data_in_wait);

    init_usb_anchor(&dev->data_out_anchor);
    init_waitqueue_head(&dev->data_out_wait);

    bladerf_start(dev);

    usb_set_intfdata(interface, dev);

    retval = usb_register_dev(interface, &bladerf_class);
    if (retval) {
        dev_err(&interface->dev, "Unable to get a minor device number for bladeRF device\n");
        usb_set_intfdata(interface, NULL);
        return retval;
    }

    dev_info(&interface->dev, "Nuand bladeRF device is now attached\n");
    return 0;

error_oom:
    return -ENOMEM;
}

static void bladerf_disconnect(struct usb_interface *interface)
{
    bladerf_device_t *dev;

    dev = usb_get_intfdata(interface);

    bladerf_stop(dev);

    usb_deregister_dev(interface, &bladerf_class);

    usb_set_intfdata(interface, NULL);

    usb_put_dev(dev->udev);

    dev_info(&interface->dev, "Nuand bladeRF device has been disconnected\n");

    kfree(dev);
}

static struct usb_driver bladerf_driver = {
    .name = "nuand_bladerf",
    .probe = bladerf_probe,
    .disconnect = bladerf_disconnect,
    .id_table = bladerf_table,
};

module_usb_driver(bladerf_driver);

MODULE_AUTHOR("Robert Ghilduta <robert.ghilduta@gmail.com>");
MODULE_DESCRIPTION("bladeRF USB driver");
MODULE_VERSION("v0.1");
MODULE_LICENSE("GPL");
