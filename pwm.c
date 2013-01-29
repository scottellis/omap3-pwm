/*
 Copyright (c) 2010, Scott Ellis
 All rights reserved.

 Redistribution and use in source and binary forms, with or without
 modification, are permitted provided that the following conditions are met:
	* Redistributions of source code must retain the above copyright
	  notice, this list of conditions and the following disclaimer.
	* Redistributions in binary form must reproduce the above copyright
	  notice, this list of conditions and the following disclaimer in the
	  documentation and/or other materials provided with the distribution.
	* Neither the name of the <organization> nor the
	  names of its contributors may be used to endorse or promote products
	  derived from this software without specific prior written permission.

 THIS SOFTWARE IS PROVIDED BY Scott Ellis ''AS IS'' AND ANY
 EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED
 WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE
 DISCLAIMED. IN NO EVENT SHALL Scott Ellis BE LIABLE FOR ANY
 DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES
 (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES;
 LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND
 ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
 (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF THIS
 SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.

 Authors: Scott Ellis, Jack Elston, Curt Olson
*/

#include <linux/init.h>
#include <linux/module.h>
#include <linux/device.h>
#include <linux/semaphore.h>
#include <linux/cdev.h>
#include <linux/fs.h>
#include <linux/errno.h>
#include <asm/io.h>
#include <asm/uaccess.h>
#include <linux/moduleparam.h>
#include <linux/slab.h>
#include <linux/clk.h>
#include <linux/interrupt.h>
#include <plat/dmtimer.h>

#include "pwm.h"

static int nomux;
module_param(nomux, int, S_IRUGO);
MODULE_PARM_DESC(nomux, "Do not mux the PWM pins");

static int frequency;
module_param(frequency, int, S_IRUGO);
MODULE_PARM_DESC(frequency, "PWM frequency");

#define MAX_TIMERS 4
static int timers[MAX_TIMERS] = { 8, 9, 10, 11 };
static int num_timers;
module_param_array(timers, int, &num_timers, 0000);
MODULE_PARM_DESC(timers, "List of PWM timers to control");

static int servo;
module_param(servo, int, S_IRUGO);
MODULE_PARM_DESC(servo, "Enable servo mode operation");

#define SERVO_ABSOLUTE_MIN 5000
#define SERVO_ABSOLUTE_MAX 25000
#define SERVO_DEFAULT_MIN 10000
#define SERVO_DEFAULT_MAX 20000
#define SERVO_CENTER 15000

static int servo_min = SERVO_DEFAULT_MIN;
module_param(servo_min, int, S_IRUGO);
MODULE_PARM_DESC(servo_min, "Servo min value in tenths of usec," \
		" default 10000");

static int servo_max = SERVO_DEFAULT_MAX;
module_param(servo_max, int, S_IRUGO);
MODULE_PARM_DESC(servo_max, "Servo max value in tenths of usec," \
		" default 20000");

static int servo_start = SERVO_CENTER;
module_param(servo_start, int, S_IRUGO);
MODULE_PARM_DESC(servo_start, "Servo value on startup in tenths of usec," \
		" default 15000");

static int irq_mode = 0;
module_param(irq_mode, int, S_IRUGO);
MODULE_PARM_DESC(irq_mode, "enable interrupt mode," \
		" default 0");


struct pwm_dev {
	dev_t devt;
	struct cdev cdev;
	struct device *device;
	struct semaphore sem;
	int id;
	u32 mux_offset;
	struct omap_dm_timer *timer;
	u32 input_freq;
	u32 old_mux;
	u32 tldr;
	u32 tmar;
	u32 num_settings;
	u32 current_val;
	spinlock_t lock;
	int irq;
};

// only one class
struct class *pwm_class;

static struct pwm_dev pwm_dev[MAX_TIMERS];


static int pwm_init_mux(struct pwm_dev *pd)
{
	void __iomem *base;

	if (nomux)
		return 0;

	base = ioremap(OMAP34XX_PADCONF_START, OMAP34XX_PADCONF_SIZE);

	if (!base)
		return -ENOMEM;

	pd->old_mux = ioread16(base + pd->mux_offset);
	iowrite16(PWM_ENABLE_MUX, base + pd->mux_offset);
	iounmap(base);

	return 0;
}

static int pwm_restore_mux(struct pwm_dev *pd)
{
	void __iomem *base;

	if (nomux)
		return 0;

	if (pd->old_mux) {
		base = ioremap(OMAP34XX_PADCONF_START, OMAP34XX_PADCONF_SIZE);

		if (!base)
			return -ENOMEM;

		iowrite16(pd->old_mux, base + pd->mux_offset);
		iounmap(base);
	}

	return 0;
}

static void pwm_set_frequency(struct pwm_dev *pd)
{
	if (frequency > (pd->input_freq / 2))
		frequency = pd->input_freq / 2;

	pd->tldr = 0xFFFFFFFF - ((pd->input_freq / frequency) - 1);

	omap_dm_timer_set_load(pd->timer, 1, pd->tldr);

	pd->num_settings = 0xFFFFFFFE - pd->tldr;
}

static void pwm_off(struct pwm_dev *pd)
{
	if (pd->current_val != 0) {
		if (irq_mode)
			omap_dm_timer_set_int_enable(pd->timer, 0);

		omap_dm_timer_stop(pd->timer);
		pd->current_val = 0;
	}
}

static void pwm_on(struct pwm_dev *pd)
{
	if (!irq_mode)
		omap_dm_timer_set_match(pd->timer, 1, pd->tmar);

	if (pd->current_val == 0) {
		if (irq_mode)
			omap_dm_timer_set_match(pd->timer, 1, pd->tmar);

		omap_dm_timer_start(pd->timer);
	}

	if (irq_mode)
		omap_dm_timer_set_int_enable(pd->timer, OMAP_TIMER_INT_MATCH);
}

static int pwm_set_duty_cycle(struct pwm_dev *pd, u32 duty_cycle)
{
	u32 new_tmar;

	if (duty_cycle > 100)
		return -EINVAL;

	if (duty_cycle == 0) {
		pwm_off(pd);
		return 0;
	}

	new_tmar = (duty_cycle * pd->num_settings) / 100;

	if (new_tmar < 1)
		new_tmar = 1;
	else if (new_tmar > pd->num_settings)
		new_tmar = pd->num_settings;

	spin_lock(&pd->lock);
	pd->tmar = pd->tldr + new_tmar;
	spin_unlock(&pd->lock);

	pwm_on(pd);

	pd->current_val = duty_cycle;

	return 0;
}

#define TENTHS_OF_MICROSEC_PER_SEC	10000000
static int pwm_set_servo_pulse(struct pwm_dev *pd, u32 tenths_us)
{
	u32 new_tmar, factor;

	if (tenths_us < servo_min || tenths_us > servo_max)
		return -EINVAL;

	factor = TENTHS_OF_MICROSEC_PER_SEC / (frequency * 2);
	new_tmar = (tenths_us * (pd->num_settings / 2)) / factor;

	if (new_tmar < 1)
		new_tmar = 1;
	else if (new_tmar > pd->num_settings)
		new_tmar = pd->num_settings;

	spin_lock(&pd->lock);
	pd->tmar = pd->tldr + new_tmar;
	spin_unlock(&pd->lock);

	pwm_on(pd);

	pd->current_val = tenths_us;

	return 0;
}

static void pwm_timer_cleanup(void)
{
	int i;

	for (i = 0; i < num_timers; i++) {
		pwm_off(&pwm_dev[i]);

		if (pwm_dev[i].timer) {
			omap_dm_timer_free(pwm_dev[i].timer);
			pwm_dev[i].timer = NULL;
			if (irq_mode)
				free_irq(pwm_dev[i].irq, &pwm_dev[i]);
		}

		pwm_restore_mux(&pwm_dev[i]);
	}
}

static irqreturn_t match_handler(int irq, void *ptr)
{
	/* this handler executes code right after a match event,
	   thus we are synchronized to the timer domain */
	u32 val;
	struct pwm_dev *pd = (struct pwm_dev *)ptr;

	/* read new tmar value: */
	spin_lock(&pd->lock);
	val = pd->tmar;
	spin_unlock(&pd->lock);

	/* set a new tmar value: */
	omap_dm_timer_set_match(pd->timer, 1, val);

	/* indicate that match interrupt has been handled: */
	val = omap_dm_timer_read_status(pd->timer);
	val |= OMAP_TIMER_INT_MATCH;
	omap_dm_timer_write_status(pd->timer, val);

	return IRQ_HANDLED;
}

static int pwm_timer_init(void)
{
	int i;
	struct clk *fclk;

	for (i = 0; i < num_timers; i++) {
		if (pwm_init_mux(&pwm_dev[i]))
			goto timer_init_fail;
	}

	for (i = 0; i < num_timers; i++) {
		pwm_dev[i].timer
			= omap_dm_timer_request_specific(pwm_dev[i].id);

		if (!pwm_dev[i].timer)
			goto timer_init_fail;

		omap_dm_timer_set_pwm(pwm_dev[i].timer,
				0,	// ~SCPWM low when off
				1,	// PT pulse toggle modulation
				OMAP_TIMER_TRIGGER_OVERFLOW_AND_COMPARE);

		if (omap_dm_timer_set_source(pwm_dev[i].timer,
						OMAP_TIMER_SRC_SYS_CLK))
			goto timer_init_fail;

		// make sure we know the source clock frequency
		fclk = omap_dm_timer_get_fclk(pwm_dev[i].timer);
		pwm_dev[i].input_freq = clk_get_rate(fclk);

		pwm_set_frequency(&pwm_dev[i]);

		if (irq_mode) {
			pwm_dev[i].irq = omap_dm_timer_get_irq(pwm_dev[i].timer);

			if (request_irq(pwm_dev[i].irq, match_handler,
					IRQF_DISABLED | IRQF_SHARED,
					"pwm-match",
					&pwm_dev[i])) {
				printk(KERN_ERR
					"request_irq failed (on irq %d)\n",
					pwm_dev[i].irq);

				goto timer_init_fail;
			}
		}

	}

	if (servo) {
		for (i = 0; i < num_timers; i++)
			pwm_set_servo_pulse(&pwm_dev[i], servo_start);
	}

	return 0;

timer_init_fail:

	pwm_timer_cleanup();

	return -1;
}

static ssize_t pwm_read(struct file *filp, char __user *buff, size_t count,
			loff_t *offp)
{
	size_t len;
	ssize_t status;
	struct pwm_dev *pd = filp->private_data;
	char temp[16];

	if (!buff)
		return -EFAULT;

	// for user progs like cat that will keep asking forever
	if (*offp > 0)
		return 0;

	if (down_interruptible(&pd->sem))
		return -ERESTARTSYS;

	len = sprintf(temp, "%u\n", pd->current_val);

	if (len + 1 < count)
		count = len + 1;

	if (copy_to_user(buff, temp, count))  {
		status = -EFAULT;
	}
	else {
		*offp += count;
		status = count;
	}

	up(&pd->sem);

	return status;
}

static ssize_t pwm_write(struct file *filp, const char __user *buff,
			size_t count, loff_t *offp)
{
	size_t len;
	u32 val;
	ssize_t status = 0;
	char temp[16];

	struct pwm_dev *pd = filp->private_data;

	if (down_interruptible(&pd->sem))
		return -ERESTARTSYS;

	if (!buff || count < 1) {
		status = -EINVAL;
		goto pwm_write_done;
	}

	if (count > 8)
		len = 8;
	else
		len = count;

	memset(temp, 0, 16);

	if (copy_from_user(temp, buff, len)) {
		status = -EFAULT;
		goto pwm_write_done;
	}

	val = simple_strtoul(temp, NULL, 0);

	if (servo)
		status = pwm_set_servo_pulse(pd, val);
	else
		status = pwm_set_duty_cycle(pd, val);

	*offp += count;

	if (!status)
		status = count;

pwm_write_done:

	up(&pd->sem);

	return status;
}

static int pwm_open(struct inode *inode, struct file *filp)
{
	struct pwm_dev *pd = container_of(inode->i_cdev, struct pwm_dev, cdev);
	filp->private_data = pd;

	return 0;
}

static struct file_operations pwm_fops = {
	.owner = THIS_MODULE,
	.read = pwm_read,
	.write = pwm_write,
	.open = pwm_open,
};

static int __init pwm_init_cdev(struct pwm_dev *pd)
{
	int error;

	error = alloc_chrdev_region(&pd->devt, pd->id, 1, "pwm");

	if (error) {
		pd->devt = 0;
		return error;
	}

	cdev_init(&pd->cdev, &pwm_fops);
	pd->cdev.owner = THIS_MODULE;

	error = cdev_add(&pd->cdev, pd->devt, 1);

	if (error) {
		unregister_chrdev_region(pd->devt, 1);
		pd->devt = 0;
		return error;
	}

	return 0;
}

static int __init pwm_init_class(struct pwm_dev *pd)
{
	int ret;

	if (!pwm_class) {
		pwm_class = class_create(THIS_MODULE, "pwm");

		if (IS_ERR(pwm_class)) {
			ret = PTR_ERR(pwm_class);
			pwm_class = 0;
			return ret;
		}
	}

	pd->device = device_create(pwm_class, NULL, pd->devt, NULL, "pwm%d",
				MINOR(pd->devt));

	if (IS_ERR(pd->device)) {
		ret = PTR_ERR(pd->device);
		pd->device = 0;
		return ret;
	}

	return 0;
}

static void pwm_dev_cleanup(void)
{
	int i;

	for (i = 0; i < num_timers; i++) {
		if (pwm_dev[i].device)
			device_destroy(pwm_class, pwm_dev[i].devt);
	}

	if (pwm_class)
		class_destroy(pwm_class);

	for (i = 0; i < num_timers; i++) {
		cdev_del(&pwm_dev[i].cdev);
		unregister_chrdev_region(pwm_dev[i].devt, 1);
	}
}

struct timer_init {
	int id;
	u32 mux_offset;
	u32 used;
};

static struct timer_init timer_init[MAX_TIMERS] = {
	{ 8, GPT8_MUX_OFFSET, 0 },
	{ 9, GPT9_MUX_OFFSET, 0 },
	{ 10, GPT10_MUX_OFFSET, 0 },
	{ 11, GPT11_MUX_OFFSET, 0 }
};

static int pwm_init_timer_list(void)
{
	int i, j;

	if (num_timers == 0)
		num_timers = 4;

	for (i = 0; i < num_timers; i++) {
		for (j = 0; j < MAX_TIMERS; j++) {
			if (timers[i] == timer_init[j].id)
				break;
		}

		if (j == MAX_TIMERS) {
			printk(KERN_ERR "Invalid timer requested: %d\n",
				timers[i]);
			return -1;
		}

		if (timer_init[j].used) {
			printk(KERN_ERR "Timer %d specified more then once\n",
				timers[i]);
			return -1;
		}

		timer_init[j].used = 1;
		pwm_dev[i].id = timer_init[j].id;
		pwm_dev[i].mux_offset = timer_init[j].mux_offset;
	}

	return 0;
}

static int __init pwm_init(void)
{
	int i;

	if (pwm_init_timer_list())
		return -1;

	for (i = 0; i < num_timers; i++) {
		spin_lock_init(&pwm_dev[i].lock);
		sema_init(&pwm_dev[i].sem, 1);

		if (pwm_init_cdev(&pwm_dev[i]))
			goto init_fail;

		if (pwm_init_class(&pwm_dev[i]))
			goto init_fail;
	}

	if (servo)
		frequency = 50;
	else if (frequency <= 0)
		frequency = 1024;

	if (servo) {
		if (servo_min < SERVO_ABSOLUTE_MIN)
			servo_min = SERVO_ABSOLUTE_MIN;

		if (servo_max > SERVO_ABSOLUTE_MAX)
			servo_max = SERVO_ABSOLUTE_MAX;

		if (servo_min >= servo_max) {
			servo_min = SERVO_ABSOLUTE_MIN;
			servo_max = SERVO_ABSOLUTE_MAX;
		}

		if (servo_start < servo_min)
			servo_start = servo_min;
		else if (servo_start > servo_max)
			servo_start = servo_max;
	}

	if (pwm_timer_init())
		goto init_fail_2;

	if (servo) {
		printk(KERN_INFO
			"pwm: frequency=%d Hz servo=%d " \
			"servo_min = %d servo_max = %d\n",
			frequency, servo, servo_min, servo_max);
	}
	else {
		printk(KERN_INFO "pwm: frequency=%d Hz  servo=%d\n",
			frequency, servo);
	}

	return 0;

init_fail_2:
	pwm_timer_cleanup();

init_fail:
	pwm_dev_cleanup();

	return -1;
}
module_init(pwm_init);

static void __exit pwm_exit(void)
{
	pwm_dev_cleanup();
	pwm_timer_cleanup();
}
module_exit(pwm_exit);

MODULE_LICENSE("GPL");
MODULE_AUTHOR("Scott Ellis - Jumpnow");
MODULE_DESCRIPTION("PWM example for Gumstix Overo");
