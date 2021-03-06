
#define BLADERF_IOCTL_BASE      'N'
#define BLADE_QUERY_VERSION     _IOR(BLADERF_IOCTL_BASE, 0, struct bladeRF_version)
#define BLADE_QUERY_FPGA_STATUS     _IOR(BLADERF_IOCTL_BASE, 1, unsigned int)
#define BLADE_BEGIN_PROG        _IOR(BLADERF_IOCTL_BASE, 2, unsigned int)
#define BLADE_END_PROG          _IOR(BLADERF_IOCTL_BASE, 3, unsigned int)
#define BLADE_CHECK_PROG        _IOR(BLADERF_IOCTL_BASE, 4, unsigned int)
#define BLADE_RF_RX             _IOR(BLADERF_IOCTL_BASE, 5, unsigned int)
#define BLADE_RF_TX             _IOR(BLADERF_IOCTL_BASE, 6, unsigned int)
#define BLADE_LMS_WRITE         _IOR(BLADERF_IOCTL_BASE, 20, unsigned int)
#define BLADE_LMS_READ          _IOR(BLADERF_IOCTL_BASE, 21, unsigned int)
#define BLADE_SI5338_WRITE      _IOR(BLADERF_IOCTL_BASE, 22, unsigned int)
#define BLADE_SI5338_READ       _IOR(BLADERF_IOCTL_BASE, 23, unsigned int)
#define BLADE_VCTCXO_WRITE      _IOR(BLADERF_IOCTL_BASE, 24, unsigned int)
#define BLADE_GPIO_WRITE        _IOR(BLADERF_IOCTL_BASE, 25, unsigned int)
#define BLADE_GPIO_READ         _IOR(BLADERF_IOCTL_BASE, 26, unsigned int)

#define BLADE_UPGRADE_FW        _IOR(BLADERF_IOCTL_BASE, 50, unsigned int)

#define BLADE_USB_CMD_QUERY_VERSION             0
#define BLADE_USB_CMD_QUERY_FPGA_STATUS         1
#define BLADE_USB_CMD_BEGIN_PROG                2
#define BLADE_USB_CMD_END_PROG                  3
#define BLADE_USB_CMD_RF_RX                     4
#define BLADE_USB_CMD_RF_TX                     5
#define BLADE_USB_CMD_FLASH_READ              100
#define BLADE_USB_CMD_FLASH_WRITE             101
#define BLADE_USB_CMD_FLASH_ERASE             102

#define BLADE_USB_CMD_QUERY_VERSION      0
#define BLADE_USB_CMD_QUERY_FPGA_STATUS  1
#define BLADE_USB_CMD_BEGIN_PROG         2
#define BLADE_USB_CMD_END_PROG           3
#define BLADE_USB_CMD_RF_RX              4

struct bladeRF_version {
    unsigned short major;
    unsigned short minor;
};

struct bladeRF_firmware {
    unsigned int len;
    unsigned char *ptr;
};

#define BLADE_USB_TYPE_OUT      0x40
#define BLADE_USB_TYPE_IN       0xC0
#define BLADE_USB_TIMEOUT_MS    1000

#define USB_NUAND_VENDOR_ID     0x1d50
#define USB_NUAND_BLADERF_PRODUCT_ID    0x6066
#define NUM_CONCURRENT  8
#define NUM_DATA_URB    (1024)
#define DATA_BUF_SZ     (1024*4)

struct uart_pkt {
    unsigned char magic;
#define UART_PKT_MAGIC          'N'

    //  | 7 | 6 | 5 | 4 | 3 | 2 | 1 | 0 |
    //  |  dir  |  dev  |     cnt       |
    unsigned char mode;
#define UART_PKT_MODE_CNT_MASK   0xF
#define UART_PKT_MODE_CNT_SHIFT  0

#define UART_PKT_MODE_DEV_MASK   0x30
#define UART_PKT_MODE_DEV_SHIFT  4
#define UART_PKT_DEV_GPIO        (0<<UART_PKT_MODE_DEV_SHIFT)
#define UART_PKT_DEV_LMS         (1<<UART_PKT_MODE_DEV_SHIFT)
#define UART_PKT_DEV_VCTCXO      (2<<UART_PKT_MODE_DEV_SHIFT)
#define UART_PKT_DEV_SI5338      (3<<UART_PKT_MODE_DEV_SHIFT)

#define UART_PKT_MODE_DIR_MASK   0xC0
#define UART_PKT_MODE_DIR_SHIFT  6
#define UART_PKT_MODE_DIR_READ   (2<<UART_PKT_MODE_DIR_SHIFT)
#define UART_PKT_MODE_DIR_WRITE  (1<<UART_PKT_MODE_DIR_SHIFT)
};

struct uart_cmd {
    unsigned char addr;
    unsigned char data;
};
