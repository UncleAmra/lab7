/*
 * spi_demo.c  -  Lab7 Item 3
 * MCP3008 ADC read through bit-banged (software) SPI on TX2 GPIOs,
 * exposed as character device /dev/spi_demo.
 *
 *   sudo insmod spi_demo.ko channel=0
 *   cat /dev/spi_demo   -> "512\n" per read
 *
 * Signal  J21 pin  Linux GPIO  MCP3008 pin
 *  CLK      23        427       13 CLK
 *  MISO     21        428       12 DOUT
 *  MOSI     19        429       11 DIN
 *  CS       24        430       10 CS/SHDN
 *
 * This is the Lab5 Python readadc() rewritten in the kernel, using the
 * linux/gpio.h API to toggle each bit.
 */
#include <linux/module.h>
#include <linux/kernel.h>
#include <linux/init.h>
#include <linux/fs.h>
#include <linux/gpio.h>
#include <linux/delay.h>
#include <linux/mutex.h>
#include <linux/slab.h>
#include <linux/uaccess.h>

#define DEV_NAME "spi_demo"
#define HALF_US  2

#define GPIO_CLK   427
#define GPIO_MISO  428
#define GPIO_MOSI  429
#define GPIO_CS    430

static int channel = 0;
module_param(channel, int, 0444);
MODULE_PARM_DESC(channel, "MCP3008 channel 0..7 (default 0)");

static struct gpio spi_pins[] = {
	{ GPIO_CLK,  GPIOF_OUT_INIT_LOW,  "mcp3008_clk"  }, /* idle low  */
	{ GPIO_MISO, GPIOF_IN,            "mcp3008_miso" },
	{ GPIO_MOSI, GPIOF_OUT_INIT_LOW,  "mcp3008_mosi" },
	{ GPIO_CS,   GPIOF_OUT_INIT_HIGH, "mcp3008_cs"   }, /* idle high */
};

static int major;
static DEFINE_MUTEX(adc_lock);

struct adc_ctx {           /* one per open() */
	char reply[16];
	size_t len;
	size_t off;
};

/* Software SPI transaction, same algorithm as Lab5 readadc() */
static unsigned int readadc(int adcnum)
{
	unsigned int commandout, adcout = 0;
	int i;

	gpio_set_value(GPIO_CS, 1);
	gpio_set_value(GPIO_CLK, 0);
	gpio_set_value(GPIO_CS, 0);          /* start transaction */

	commandout = (adcnum | 0x18) << 3;   /* start bit + single-ended + channel */
	for (i = 0; i < 5; i++) {
		gpio_set_value(GPIO_MOSI, (commandout & 0x80) ? 1 : 0);
		commandout <<= 1;
		udelay(HALF_US);
		gpio_set_value(GPIO_CLK, 1);
		udelay(HALF_US);
		gpio_set_value(GPIO_CLK, 0);
	}

	/* 12 clocks: empty bit, null bit, 10 data bits (sampled while CLK high) */
	for (i = 0; i < 12; i++) {
		udelay(HALF_US);
		gpio_set_value(GPIO_CLK, 1);
		udelay(HALF_US);
		adcout = (adcout << 1) | (gpio_get_value(GPIO_MISO) ? 1 : 0);
		gpio_set_value(GPIO_CLK, 0);
	}

	gpio_set_value(GPIO_CS, 1);          /* end transaction */
	return adcout & 0x3ff;               /* 10-bit result 0..1023 */
}

static int demo_open(struct inode *inode, struct file *filp)
{
	struct adc_ctx *ctx = kzalloc(sizeof(*ctx), GFP_KERNEL);

	if (!ctx)
		return -ENOMEM;
	filp->private_data = ctx;
	pr_info("spi_demo: open\n");
	return 0;
}

static int demo_release(struct inode *inode, struct file *filp)
{
	kfree(filp->private_data);
	pr_info("spi_demo: release\n");
	return 0;
}

static ssize_t demo_read(struct file *filp, char __user *buf,
			 size_t count, loff_t *ppos)
{
	struct adc_ctx *ctx = filp->private_data;
	unsigned int value;
	size_t bytes;

	if (count == 0)
		return 0;

	/* previous line fully read -> take a new ADC sample */
	if (ctx->off >= ctx->len) {
		mutex_lock(&adc_lock);
		value = readadc(channel);
		mutex_unlock(&adc_lock);
		ctx->len = scnprintf(ctx->reply, sizeof(ctx->reply), "%u\n", value);
		ctx->off = 0;
	}

	bytes = min(count, ctx->len - ctx->off);
	if (copy_to_user(buf, ctx->reply + ctx->off, bytes))
		return -EFAULT;
	ctx->off += bytes;
	return bytes;
}

static const struct file_operations spi_fops = {
	.owner   = THIS_MODULE,
	.open    = demo_open,
	.read    = demo_read,
	.release = demo_release,
};

static int __init spi_demo_init(void)
{
	int ret;

	if (channel < 0 || channel > 7)
		return -EINVAL;

	ret = gpio_request_array(spi_pins, ARRAY_SIZE(spi_pins));
	if (ret) {
		pr_err("spi_demo: gpio_request_array failed: %d (pins busy?)\n", ret);
		return ret;
	}

	major = register_chrdev(0, DEV_NAME, &spi_fops);
	if (major < 0) {
		ret = major;
		pr_err("spi_demo: register_chrdev failed: %d\n", ret);
		gpio_free_array(spi_pins, ARRAY_SIZE(spi_pins));
		return ret;
	}

	pr_info("spi_demo: loaded, major=%d channel=%d\n", major, channel);
	return 0;
}

static void __exit spi_demo_exit(void)
{
	unregister_chrdev(major, DEV_NAME);
	gpio_set_value(GPIO_CS, 1);
	gpio_free_array(spi_pins, ARRAY_SIZE(spi_pins));
	pr_info("spi_demo: unloaded\n");
}

module_init(spi_demo_init);
module_exit(spi_demo_exit);
MODULE_LICENSE("GPL");
MODULE_DESCRIPTION("Lab7 MCP3008 software-SPI ADC char driver");
