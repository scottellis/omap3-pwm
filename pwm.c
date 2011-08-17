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

#include "pwm.h"

/* default frequency of 1 kHz */
#define DEFAULT_TLDR	0xFFFFFFE0

/* default 50% duty cycle */
/* TMAR = (0xFFFFFFFF - ((0xFFFFFFFF - (DEFAULT_TLDR + 1)) / 2)) */
#define DEFAULT_TMAR	0xFFFFFFEF

/* default TCLR is off state */
#define DEFAULT_TCLR (GPT_TCLR_PT | GPT_TCLR_TRG_OVFL_MATCH | GPT_TCLR_CE | GPT_TCLR_AR) 

#define DEFAULT_PWM_FREQUENCY 1024

static int frequency = DEFAULT_PWM_FREQUENCY;
module_param(frequency, int, S_IRUGO);
MODULE_PARM_DESC(frequency, "the pwm frequency");


#define USER_BUFF_SIZE	128

struct gpt {
	u32 timer_num;
	u32 mux_offset;
	u32 gpt_base;
	struct clk *clk;
	u32 input_freq;
	u32 old_mux;
	u32 tldr;
	u32 tmar;
	u32 tclr;
	u32 num_freqs;
};

struct pwm_dev {
	dev_t devt;
	struct cdev cdev;
	struct class *class;
	struct semaphore sem;
	struct gpt gpt;
	char *user_buff;
};

static struct pwm_dev pwm_dev;


static int init_mux(void)
{
	void __iomem *base;

	base = ioremap(OMAP34XX_PADCONF_START, OMAP34XX_PADCONF_SIZE);
	if (!base) {
		printk(KERN_ALERT "init_mux(): ioremap() failed\n");
		return -1;
	}

	pwm_dev.gpt.old_mux = ioread16(base + pwm_dev.gpt.mux_offset);
	iowrite16(PWM_ENABLE_MUX, base + pwm_dev.gpt.mux_offset);
	iounmap(base);	

	return 0;
}

static int restore_mux(void)
{
	void __iomem *base;

	if (pwm_dev.gpt.old_mux) {
		base = ioremap(OMAP34XX_PADCONF_START, OMAP34XX_PADCONF_SIZE);
	
		if (!base) {
			printk(KERN_ALERT "restore_mux(): ioremap() failed\n");
			return -1;
		}

		iowrite16(pwm_dev.gpt.old_mux, base + pwm_dev.gpt.mux_offset);
		iounmap(base);	
	}

	return 0;
}

static int set_pwm_frequency(void)
{
	void __iomem *base;

	base = ioremap(pwm_dev.gpt.gpt_base, GPT_REGS_PAGE_SIZE);
	if (!base) {
		printk(KERN_ALERT "set_pwm_frequency(): ioremap failed\n");
		return -1;
	}

	if (frequency <= 0)
		frequency = DEFAULT_PWM_FREQUENCY;
	else if (frequency > (pwm_dev.gpt.input_freq / 2)) 
		frequency = pwm_dev.gpt.input_freq / 2;

	/* PWM_FREQ = 32768 / ((0xFFFF FFFF - TLDR) + 1) */
	pwm_dev.gpt.tldr = 0xFFFFFFFF - ((pwm_dev.gpt.input_freq / frequency) - 1);

	/* just for convenience */	
	pwm_dev.gpt.num_freqs = 0xFFFFFFFE - pwm_dev.gpt.tldr;	

	iowrite32(pwm_dev.gpt.tldr, base + GPT_TLDR);

	// initialize TCRR to TLDR, have to start somewhere
	iowrite32(pwm_dev.gpt.tldr, base + GPT_TCRR);

	iounmap(base);

	return 0;
}

static int pwm_off(void)
{
	void __iomem *base;

	base = ioremap(pwm_dev.gpt.gpt_base, GPT_REGS_PAGE_SIZE);
	if (!base) {
		printk(KERN_ALERT "pwm_off(): ioremap failed\n");
		return -1;
	}

	pwm_dev.gpt.tclr &= ~GPT_TCLR_ST;
	iowrite32(pwm_dev.gpt.tclr, base + GPT_TCLR); 
	iounmap(base);

	return 0;
}

static int pwm_on(void)
{
	void __iomem *base;

	base = ioremap(pwm_dev.gpt.gpt_base, GPT_REGS_PAGE_SIZE);

	if (!base) {
		printk(KERN_ALERT "pwm_on(): ioremap failed\n");
		return -1;
	}

	/* set the duty cycle */
	iowrite32(pwm_dev.gpt.tmar, base + GPT_TMAR);
	
	/* now turn it on */
	pwm_dev.gpt.tclr = ioread32(base + GPT_TCLR);
	pwm_dev.gpt.tclr |= GPT_TCLR_ST;
	iowrite32(pwm_dev.gpt.tclr, base + GPT_TCLR); 
	iounmap(base);

	return 0;
}

static int set_duty_cycle(unsigned int duty_cycle) 
{
	unsigned int new_tmar;

	pwm_off();

	if (duty_cycle == 0)
		return 0;
 
	new_tmar = (duty_cycle * pwm_dev.gpt.num_freqs) / 100;

	if (new_tmar < 1) 
		new_tmar = 1;
	else if (new_tmar > pwm_dev.gpt.num_freqs)
		new_tmar = pwm_dev.gpt.num_freqs;
		
	pwm_dev.gpt.tmar = pwm_dev.gpt.tldr + new_tmar;
	
	return pwm_on();
}

static ssize_t pwm_read(struct file *filp, char __user *buff, size_t count,
			loff_t *offp)
{
	size_t len;
	unsigned int duty_cycle;
	ssize_t error = 0;

	if (!buff) 
		return -EFAULT;

	/* tell the user there is no more */
	if (*offp > 0) 
		return 0;

	if (down_interruptible(&pwm_dev.sem)) 
		return -ERESTARTSYS;

	if (pwm_dev.gpt.tclr & GPT_TCLR_ST) {
		duty_cycle = (100 * (pwm_dev.gpt.tmar - pwm_dev.gpt.tldr)) 
				/ pwm_dev.gpt.num_freqs;

		snprintf(pwm_dev.user_buff, USER_BUFF_SIZE,
				"PWM%d Frequency %u Hz Duty Cycle %u%%\n",
				pwm_dev.gpt.timer_num, frequency, duty_cycle);
	}
	else {
		snprintf(pwm_dev.user_buff, USER_BUFF_SIZE,
				"PWM%d Frequency %u Hz Stopped\n",
				pwm_dev.gpt.timer_num, frequency);
	}

	len = strlen(pwm_dev.user_buff);
 
	if (len + 1 < count) 
		count = len + 1;

	if (copy_to_user(buff, pwm_dev.user_buff, count))  {
		printk(KERN_ALERT "pwm_read(): copy_to_user() failed\n");
		error = -EFAULT;
	}
	else {
		*offp += count;
		error = count;
	}

	up(&pwm_dev.sem);

	return error;	
}

static ssize_t pwm_write(struct file *filp, const char __user *buff, 
			size_t count, loff_t *offp)
{
	size_t len;
	unsigned int duty_cycle;
	ssize_t error = 0;
	
	if (down_interruptible(&pwm_dev.sem)) 
		return -ERESTARTSYS;

	if (!buff || count < 1) {
		printk(KERN_ALERT "pwm_write(): input check failed\n");
		error = -EFAULT; 
		goto pwm_write_done;
	}
	
	/* we are only expecting a small integer, ignore anything else */
	if (count > 8)
		len = 8;
	else
		len = count;
		
	memset(pwm_dev.user_buff, 0, 16);

	if (copy_from_user(pwm_dev.user_buff, buff, len)) {
		printk(KERN_ALERT "pwm_write(): copy_from_user() failed\n"); 
		error = -EFAULT; 	
		goto pwm_write_done;
	}


	duty_cycle = simple_strtoul(pwm_dev.user_buff, NULL, 0);

	set_duty_cycle(duty_cycle);

	/* pretend we ate it all */
	*offp += count;

	error = count;

pwm_write_done:

	up(&pwm_dev.sem);
	
	return error;
}

static int pwm_enable_clock(void)
{
	char id[16];

	if (pwm_dev.gpt.clk)
		return 0;

	sprintf(id, "gpt%d_fck", pwm_dev.gpt.timer_num);
	
	pwm_dev.gpt.clk = clk_get(NULL, id);

	if (IS_ERR(pwm_dev.gpt.clk)) {
		printk(KERN_ERR "Failed to get %s\n", id);
		return -1;
	}

	pwm_dev.gpt.input_freq = clk_get_rate(pwm_dev.gpt.clk);

	printk(KERN_INFO "%s clock rate %u\n", id, pwm_dev.gpt.input_freq);

	if (clk_enable(pwm_dev.gpt.clk)) {
		clk_put(pwm_dev.gpt.clk);
		pwm_dev.gpt.clk = NULL;
		printk(KERN_ERR "Error enabling %s\n", id);
		return -1;
	}
	
	return 0;	
}

static int pwm_open(struct inode *inode, struct file *filp)
{
	int error = 0;

	if (down_interruptible(&pwm_dev.sem)) 
		return -ERESTARTSYS;

	if (pwm_dev.gpt.old_mux == 0) {
		if (init_mux())  
			error = -EIO;
		else if (set_pwm_frequency()) 
			error = -EIO;		
	}

	if (!pwm_dev.user_buff) {
		pwm_dev.user_buff = kmalloc(USER_BUFF_SIZE, GFP_KERNEL);
		if (!pwm_dev.user_buff)
			error = -ENOMEM;
	}

	up(&pwm_dev.sem);

	return error;	
}

static struct file_operations pwm_fops = {
	.owner = THIS_MODULE,
	.read = pwm_read,
	.write = pwm_write,
	.open = pwm_open,
};

static int __init pwm_init_cdev(void)
{
	int error;

	error = alloc_chrdev_region(&pwm_dev.devt, pwm_dev.gpt.timer_num, 
					1, "pwm");

	if (error < 0) {
		printk(KERN_ALERT "alloc_chrdev_region() failed: %d \n", 
			error);
		return -1;
	}

	cdev_init(&pwm_dev.cdev, &pwm_fops);
	pwm_dev.cdev.owner = THIS_MODULE;
	
	error = cdev_add(&pwm_dev.cdev, pwm_dev.devt, 1);
	if (error) {
		printk(KERN_ALERT "cdev_add() failed: %d\n", error);
		unregister_chrdev_region(pwm_dev.devt, 1);
		return -1;
	}	

	return 0;
}

static int __init pwm_init_class(void)
{
	pwm_dev.class = class_create(THIS_MODULE, "pwm");

	if (!pwm_dev.class) {
		printk(KERN_ALERT "class_create() failed\n");
		return -1;
	}

	if (!device_create(pwm_dev.class, NULL, pwm_dev.devt, NULL, "pwm%d", 
				MINOR(pwm_dev.devt))) {
		printk(KERN_ALERT "device_create(..., pwm) failed\n");
		class_destroy(pwm_dev.class);
		return -1;
	}

	return 0;
}
 
static int __init pwm_init(void)
{
	int error;

	/* change these 3 values to use a different PWM */
	pwm_dev.gpt.timer_num = 10;
	pwm_dev.gpt.mux_offset = GPT10_MUX_OFFSET;
	pwm_dev.gpt.gpt_base = PWM10_CTL_BASE;
		
	pwm_dev.gpt.tldr = DEFAULT_TLDR;
	pwm_dev.gpt.tmar = DEFAULT_TMAR;
	pwm_dev.gpt.tclr = DEFAULT_TCLR;

	sema_init(&pwm_dev.sem, 1);

	error = pwm_init_cdev();
	if (error)
		goto init_fail;

	error = pwm_init_class();
	if (error)
		goto init_fail_2;

	error = pwm_enable_clock();
	if (error)
		goto init_fail_3;
		
	return 0;

init_fail_3:
	device_destroy(pwm_dev.class, pwm_dev.devt);
	class_destroy(pwm_dev.class);
	
init_fail_2:
	cdev_del(&pwm_dev.cdev);
	unregister_chrdev_region(pwm_dev.devt, 1);

init_fail:
	return error;
}
module_init(pwm_init);

static void __exit pwm_exit(void)
{
	device_destroy(pwm_dev.class, pwm_dev.devt);
	class_destroy(pwm_dev.class);

	cdev_del(&pwm_dev.cdev);
	unregister_chrdev_region(pwm_dev.devt, 1);

	pwm_off();

	if (pwm_dev.gpt.clk) {
		clk_disable(pwm_dev.gpt.clk);
		clk_put(pwm_dev.gpt.clk);
	}

	restore_mux();

	if (pwm_dev.user_buff)
		kfree(pwm_dev.user_buff);
}
module_exit(pwm_exit);

MODULE_LICENSE("GPL");
MODULE_AUTHOR("Scott Ellis - Jumpnow");
MODULE_DESCRIPTION("PWM example for Gumstix Overo"); 

