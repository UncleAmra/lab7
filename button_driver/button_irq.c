/*
 * button_irq.c  -  Lab7 Item 2
 * Push button on J21 pin 18 (Linux GPIO481) -> hardware interrupt -> ISR
 * prints "Button Pressed! Interrupt Triggered." to dmesg.
 *
 *   sudo insmod button_irq.ko gpio=481 active_low=1 debounce_ms=30
 *
 * Wiring assumed: released = 1 (pull-up), pressed = 0  -> falling edge.
 */
#include <linux/module.h>
#include <linux/kernel.h>
#include <linux/init.h>
#include <linux/gpio.h>
#include <linux/interrupt.h>
#include <linux/jiffies.h>

static int gpio = 481;
module_param(gpio, int, 0444);
MODULE_PARM_DESC(gpio, "Linux GPIO number of the button (default 481 = J21 pin 18)");

static int active_low = 1;
module_param(active_low, int, 0444);
MODULE_PARM_DESC(active_low, "1 = pressed is LOW (falling edge), 0 = pressed is HIGH");

static int debounce_ms = 30;
module_param(debounce_ms, int, 0444);
MODULE_PARM_DESC(debounce_ms, "ignore edges within this many ms after a press");

struct button_device {
	int gpio;
	int irq;
	bool have_last_press;
	unsigned long last_press;
	unsigned long press_count;
	unsigned long debounce_ticks;
};

static struct button_device button;

/* ISR: keep it short, no sleeping, no file I/O */
static irqreturn_t button_isr(int irq, void *dev_id)
{
	struct button_device *b = dev_id;
	unsigned long now = jiffies;

	if (b->debounce_ticks && b->have_last_press &&
	    time_before(now, b->last_press + b->debounce_ticks))
		return IRQ_HANDLED; /* bounce -> ignore */

	b->last_press = now;
	b->have_last_press = true;
	b->press_count++;
	pr_info("button_irq: Button Pressed! Interrupt Triggered. count=%lu\n",
		b->press_count);
	return IRQ_HANDLED;
}

static int __init button_init(void)
{
	unsigned long flags;
	int ret;

	button.gpio = gpio;
	if (!gpio_is_valid(button.gpio))
		return -EINVAL;

	ret = gpio_request(button.gpio, "lab_button_irq");
	if (ret) {
		pr_err("button_irq: gpio_request(%d) failed: %d\n", button.gpio, ret);
		return ret;
	}

	ret = gpio_direction_input(button.gpio);
	if (ret)
		goto free_gpio;

	ret = gpio_to_irq(button.gpio);
	if (ret < 0)
		goto free_gpio;
	button.irq = ret;

	/* everything the ISR uses must be ready BEFORE request_irq */
	button.have_last_press = false;
	button.press_count = 0;
	button.debounce_ticks = debounce_ms > 0 ? msecs_to_jiffies(debounce_ms) : 0;

	flags = active_low ? IRQF_TRIGGER_FALLING : IRQF_TRIGGER_RISING;
	ret = request_irq(button.irq, button_isr, flags, "lab_button_irq", &button);
	if (ret) {
		pr_err("button_irq: request_irq(%d) failed: %d\n", button.irq, ret);
		goto free_gpio;
	}

	pr_info("button_irq: loaded: gpio=%d irq=%d edge=%s debounce_ms=%d\n",
		button.gpio, button.irq, active_low ? "falling" : "rising", debounce_ms);
	return 0;

free_gpio:
	gpio_free(button.gpio);
	return ret;
}

static void __exit button_exit(void)
{
	free_irq(button.irq, &button); /* first IRQ ... */
	gpio_free(button.gpio);        /* ... then GPIO */
	pr_info("button_irq: unloaded: accepted_presses=%lu\n", button.press_count);
}

module_init(button_init);
module_exit(button_exit);
MODULE_LICENSE("GPL");
MODULE_DESCRIPTION("Lab7 push-button interrupt driver");
