/*
 * demo.c  -  Lab7 Item 1 / Item 4
 * Character device driver that controls 4 LEDs on the Jetson TX2.
 *
 *   /dev/demo   write "LED1 on" | "LED1 off" | "LED1"   (select / control)
 *               read  -> "LED1 Status: 0\n"
 *
 * LED GPIOs are driven through GPIO sysfs using KERNEL FILE I/O
 * (filp_open + vfs_write/vfs_read with set_fs), as Lab7 Item 4 requires.
 * Target: Jetson TX2, L4T, Linux 4.9.
 */
#include <linux/module.h>
#include <linux/kernel.h>
#include <linux/init.h>
#include <linux/fs.h>
#include <linux/uaccess.h>
#include <linux/string.h>
#include <linux/mutex.h>
#include <linux/err.h>

#define DEV_NAME  "demo"
#define NUM_LEDS  4

/* LED1..LED4 Linux GPIO numbers (J21 pin 7, 15, 16, 22) */
static const int led_gpio[NUM_LEDS] = { 396, 255, 296, 298 };

static struct file *led_value[NUM_LEDS]; /* opened /sys/class/gpio/gpioN/value */
static bool led_exported[NUM_LEDS];      /* true = this module exported it   */
static int major;
static DEFINE_MUTEX(led_lock);

/* ---------- kernel file I/O helpers (Linux 4.9 style) ---------- */
static ssize_t kfile_write(struct file *fp, const char *data, size_t len)
{
	mm_segment_t old_fs = get_fs();
	loff_t pos = 0;
	ssize_t ret;

	set_fs(KERNEL_DS);
	ret = vfs_write(fp, (const char __user *)data, len, &pos);
	set_fs(old_fs);
	return ret;
}

static ssize_t kfile_read(struct file *fp, char *data, size_t len)
{
	mm_segment_t old_fs = get_fs();
	loff_t pos = 0;
	ssize_t ret;

	set_fs(KERNEL_DS);
	ret = vfs_read(fp, (char __user *)data, len, &pos);
	set_fs(old_fs);
	return ret;
}

static int sysfs_write_path(const char *path, const char *text)
{
	struct file *fp;
	ssize_t ret;

	fp = filp_open(path, O_WRONLY, 0);
	if (IS_ERR(fp))
		return PTR_ERR(fp);
	ret = kfile_write(fp, text, strlen(text));
	filp_close(fp, NULL);
	if (ret < 0)
		return ret;
	return 0;
}

/* export (if needed), set direction=out, open value file */
static int led_setup(int i)
{
	char path[64], num[8];
	struct file *fp;
	int ret;

	snprintf(path, sizeof(path), "/sys/class/gpio/gpio%d/value", led_gpio[i]);
	fp = filp_open(path, O_RDWR, 0);
	if (IS_ERR(fp)) {
		/* not exported yet -> export it ourselves */
		snprintf(num, sizeof(num), "%d", led_gpio[i]);
		ret = sysfs_write_path("/sys/class/gpio/export", num);
		if (ret) {
			pr_err("demo: export gpio%d failed: %d\n", led_gpio[i], ret);
			return ret;
		}
		led_exported[i] = true;
	} else {
		filp_close(fp, NULL);
	}

	snprintf(path, sizeof(path), "/sys/class/gpio/gpio%d/direction", led_gpio[i]);
	ret = sysfs_write_path(path, "out"); /* "out" also drives it low = LED off */
	if (ret) {
		pr_err("demo: set direction gpio%d failed: %d\n", led_gpio[i], ret);
		return ret;
	}

	snprintf(path, sizeof(path), "/sys/class/gpio/gpio%d/value", led_gpio[i]);
	fp = filp_open(path, O_RDWR, 0);
	if (IS_ERR(fp)) {
		pr_err("demo: open %s failed: %ld\n", path, PTR_ERR(fp));
		return PTR_ERR(fp);
	}
	led_value[i] = fp;
	return 0;
}

static void led_cleanup(int i)
{
	char num[8];

	if (led_value[i]) {
		kfile_write(led_value[i], "0", 1); /* turn LED off */
		filp_close(led_value[i], NULL);
		led_value[i] = NULL;
	}
	if (led_exported[i]) {
		snprintf(num, sizeof(num), "%d", led_gpio[i]);
		sysfs_write_path("/sys/class/gpio/unexport", num);
		led_exported[i] = false;
	}
}

/* ---------- file_operations callbacks ---------- */
static int drv_open(struct inode *inode, struct file *filp)
{
	pr_info("Enter Open function\n");
	filp->private_data = NULL; /* no LED selected yet */
	return 0;
}

static int drv_release(struct inode *inode, struct file *filp)
{
	pr_info("Enter Release function\n");
	return 0;
}

static long drv_ioctl(struct file *filp, unsigned int cmd, unsigned long arg)
{
	pr_info("Enter I/O Control function\n");
	return cmd == 0 ? 0 : -ENOTTY;
}

/* accepts: "LEDn on", "LEDn off", or "LEDn" (query only) */
static ssize_t drv_write(struct file *filp, const char __user *buf,
			 size_t count, loff_t *ppos)
{
	char cmd[32];
	char name[8] = "", action[8] = "";
	int n, idx;
	ssize_t ret;
	char v;

	pr_info("Enter Write function\n");
	if (count == 0)
		return 0;
	if (count >= sizeof(cmd))
		return -EINVAL;
	if (copy_from_user(cmd, buf, count)) /* user space -> kernel space */
		return -EFAULT;
	cmd[count] = '\0';

	n = sscanf(cmd, "%7s %7s", name, action);
	if (n < 1 || strlen(name) != 4 || strncmp(name, "LED", 3) ||
	    name[3] < '1' || name[3] > '4')
		return -EINVAL;
	idx = name[3] - '1';

	if (n == 2) {
		if (!strcmp(action, "on"))
			v = '1';
		else if (!strcmp(action, "off"))
			v = '0';
		else
			return -EINVAL;

		mutex_lock(&led_lock);
		ret = kfile_write(led_value[idx], &v, 1);
		mutex_unlock(&led_lock);
		if (ret != 1)
			return ret < 0 ? ret : -EIO;
		pr_info("demo: LED%d (gpio%d) -> %s\n", idx + 1, led_gpio[idx], action);
	}

	/* remember which LED this fd talks about, for the next read() */
	filp->private_data = (void *)(long)(idx + 1);
	return count;
}

static ssize_t drv_read(struct file *filp, char __user *buf,
			size_t count, loff_t *ppos)
{
	long sel = (long)filp->private_data;
	char val[4] = { 0 };
	char reply[32];
	ssize_t ret;
	int idx, len;

	pr_info("Enter Read function\n");
	if (sel < 1 || sel > NUM_LEDS)
		return -EINVAL; /* write "LEDn" first */
	idx = sel - 1;

	mutex_lock(&led_lock);
	ret = kfile_read(led_value[idx], val, 2);
	mutex_unlock(&led_lock);
	if (ret < 1 || (val[0] != '0' && val[0] != '1'))
		return ret < 0 ? ret : -EIO;

	len = scnprintf(reply, sizeof(reply), "LED%d Status: %c\n", idx + 1, val[0]);
	return simple_read_from_buffer(buf, count, ppos, reply, len);
}

static const struct file_operations drv_fops = {
	.owner          = THIS_MODULE,
	.open           = drv_open,
	.read           = drv_read,
	.write          = drv_write,
	.unlocked_ioctl = drv_ioctl,
	.release        = drv_release,
};

/* ---------- module init / exit ---------- */
static int __init demo_init(void)
{
	int i, ret;

	for (i = 0; i < NUM_LEDS; i++) {
		ret = led_setup(i);
		if (ret)
			goto fail;
	}

	major = register_chrdev(0, DEV_NAME, &drv_fops); /* 0 = dynamic major */
	if (major < 0) {
		ret = major;
		pr_err("demo: register_chrdev failed: %d\n", ret);
		goto fail;
	}
	pr_info("demo: started, major=%d\n", major);
	return 0;

fail:
	for (i = 0; i < NUM_LEDS; i++)
		led_cleanup(i);
	return ret;
}

static void __exit demo_exit(void)
{
	int i;

	unregister_chrdev(major, DEV_NAME);
	for (i = 0; i < NUM_LEDS; i++)
		led_cleanup(i);
	pr_info("demo: removed\n");
}

module_init(demo_init);
module_exit(demo_exit);
MODULE_LICENSE("GPL");
MODULE_DESCRIPTION("Lab7 4-LED character device driver");
