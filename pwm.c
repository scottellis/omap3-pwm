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

/*
 Modification summary:

 Original version by Scott Ellis

 Updated by Jack Elston to support all 4 hardware PWM signal generators
 and create a separate /dev entry for individual control.

 Updated by Curt Olson to support smaller granularity clock on PWM 10 & 11
 (based on Scott Ellis's older pulse.c code)
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

/* default frequency of 50 Hz */
//#define DEFAULT_TLDR	0xFFFFFC9E

/* default 50% duty cycle */
/* TMAR = (0xFFFFFFFF - ((0xFFFFFFFF - (DEFAULT_TLDR + 1)) / 2)) */
#define DEFAULT_TMAR	0xFFFFFFEF

/* default TCLR is off state */
#define DEFAULT_TCLR (GPT_TCLR_PT | GPT_TCLR_TRG_OVFL_MATCH | GPT_TCLR_CE | GPT_TCLR_AR) 

#define DEFAULT_PWM_FREQUENCY 50

static int frequency = DEFAULT_PWM_FREQUENCY;
module_param(frequency, int, S_IRUGO);
MODULE_PARM_DESC(frequency, "The PWM frequency, power of two, max of 16384");


#define USER_BUFF_SIZE	128

struct pwm_dev {
	dev_t devt;
	struct cdev cdev;
	struct device *device;
	struct semaphore sem;
	u32 pwm;
	u32 mux_offset;
	u32 phys_base;
	void __iomem *virt_base;
	struct clk *clk;
	u32 input_freq;
	u32 old_mux;
	u32 tldr;
	u32 tmar;
	u32 tclr;
	u32 num_freqs;
	char *user_buff;
};

// only one class
struct class *pwm_class;

#define NUM_PWM_TIMERS 4
static struct pwm_dev pwm_dev[NUM_PWM_TIMERS];


static int pwm_init_mux(struct pwm_dev *pd)
{
	void __iomem *base;

	base = ioremap(OMAP34XX_PADCONF_START, OMAP34XX_PADCONF_SIZE);
	if (!base) {
		printk(KERN_ALERT "pwm_init_mux: ioremap failed\n");
		return -1;
	}

	pd->old_mux = ioread16(base + pd->mux_offset);
	iowrite16(PWM_ENABLE_MUX, base + pd->mux_offset);
	iounmap(base);	

	return 0;
}

static int pwm_restore_mux(struct pwm_dev *pd)
{
	void __iomem *base;

	if (pd->old_mux) {
		base = ioremap(OMAP34XX_PADCONF_START, OMAP34XX_PADCONF_SIZE);
	
		if (!base) {
			printk(KERN_ALERT "pwm_restore_mux: ioremap failed\n");
			return -1; 
		}

		iowrite16(pd->old_mux, base + pd->mux_offset);
		iounmap(base);	
	}

	return 0;
}

static int pwm_enable_clock(struct pwm_dev *pd)
{
	char id[16];

	if (pd->clk)
		return 0;

	sprintf(id, "gpt%d_fck", pd->pwm);
	
	pd->clk = clk_get(NULL, id);

	if (IS_ERR(pd->clk)) {
		printk(KERN_ERR "Failed to get %s\n", id);
		return -1;
	}

	pd->input_freq = clk_get_rate(pd->clk);
		
	if (clk_enable(pd->clk)) {
		clk_put(pd->clk);
		pd->clk = NULL;
		printk(KERN_ERR "Error enabling %s\n", id);
		return -1;
	}
	
	return 0;	
}

static void pwm_free_clock(struct pwm_dev *pd)
{
	if (pd->clk) {
		clk_disable(pd->clk);
		clk_put(pd->clk);
	}
}

/* Change PWM10 and PWM11 to use CM_SYS_CLK rather then CM_32K_CLK */
static int pwm_use_sys_clk(void)
{
	void __iomem *base;
	u32 val;

	base = ioremap(CLOCK_CONTROL_REG_CM_START, CLOCK_CONTROL_REG_CM_SIZE);

	if (!base) {
		printk(KERN_ALERT "pwm_use_sys_clk: ioremap failed\n");
		return -1;
	}

	val = ioread32(base + CM_CLKSEL_CORE_OFFSET);
	val |= 0xc0;
	iowrite32(val, base + CM_CLKSEL_CORE_OFFSET);
	iounmap(base);

	pwm_dev[2].input_freq = CLK_SYS_FREQ;
	pwm_dev[3].input_freq = CLK_SYS_FREQ;
	
	return 0;
}

/* Restore PWM10 and PWM11 to using the CM_32K_CLK */
static int pwm_restore_32k_clk(void)
{
	void __iomem *base;
	u32 val;

	base = ioremap(CLOCK_CONTROL_REG_CM_START, CLOCK_CONTROL_REG_CM_SIZE);

	if (!base) {
		printk(KERN_ALERT "pwm_restore_32k_clk: ioremap failed\n");
		return -1;
	}

	val = ioread32(base + CM_CLKSEL_CORE_OFFSET);
	val &= ~0xc0;
	iowrite32(val, base + CM_CLKSEL_CORE_OFFSET);
	iounmap(base);

	pwm_dev[2].input_freq = CLK_32K_FREQ;
	pwm_dev[3].input_freq = CLK_32K_FREQ;
	
	return 0;
}

static int pwm_set_frequency(struct pwm_dev *pd)
{
	if (frequency <= 0)
		frequency = DEFAULT_PWM_FREQUENCY;
	else if (frequency > (pd->input_freq / 2)) 
		frequency = pd->input_freq / 2;

	pd->tldr = 0xFFFFFFFF - ((pd->input_freq / frequency) - 1);

	pd->num_freqs = 0xFFFFFFFE - pd->tldr;	

	iowrite32(pd->tldr, pd->virt_base + GPT_TLDR);

	// initialize TCRR to TLDR, have to start somewhere
	iowrite32(pd->tldr, pd->virt_base + GPT_TCRR);

	return 0;
}

static int pwm_off(struct pwm_dev *pd)
{
	pd->tclr = ioread32(pd->virt_base + GPT_TCLR);
	pd->tclr &= ~GPT_TCLR_ST;
	iowrite32(pd->tclr, pd->virt_base + GPT_TCLR); 
	
	return 0;
}

static int pwm_on(struct pwm_dev *pd)
{
	/* set the duty cycle */
	iowrite32(pd->tmar, pd->virt_base + GPT_TMAR);
	
	/* now turn it on */
	pd->tclr = ioread32(pd->virt_base + GPT_TCLR);
	pd->tclr |= GPT_TCLR_ST;
	iowrite32(pd->tclr, pd->virt_base + GPT_TCLR); 
	
	return 0;
}

static int pwm_set_duty_cycle(struct pwm_dev *pd, unsigned int duty_cycle) 
{
	unsigned int new_tmar;

	pwm_off(pd);

	if (duty_cycle == 0)
		return 0;
 
	new_tmar = (duty_cycle * pd->num_freqs) / 100;

	if (new_tmar < 1) 
		new_tmar = 1;
	else if (new_tmar > pd->num_freqs)
		new_tmar = pd->num_freqs;
		
	pd->tmar = pd->tldr + new_tmar;
	
	return pwm_on(pd);
}

static void pwm_timer_cleanup(void)
{
	int i;
	
	// since this is called by error handling code, only call this 
	// function if PWM10 or PWM11 fck is enabled or you might oops
	if (pwm_dev[2].clk || pwm_dev[3].clk)
		pwm_restore_32k_clk();
	
	for (i = 0; i < NUM_PWM_TIMERS; i++) {
		pwm_free_clock(&pwm_dev[i]);
		pwm_restore_mux(&pwm_dev[i]);	
		
		if (pwm_dev[i].virt_base) {
			iounmap(pwm_dev[i].virt_base);
			pwm_dev[i].virt_base = NULL;
		}
	}
}

static int pwm_timer_init(void)
{
	int i;

	for (i = 0; i < NUM_PWM_TIMERS; i++) {
		if (pwm_init_mux(&pwm_dev[i]))
			goto timer_init_fail;
		
		if (pwm_enable_clock(&pwm_dev[i]))
			goto timer_init_fail;
	}

	if (pwm_use_sys_clk()) 
		goto timer_init_fail;
	
	for (i = 0; i < NUM_PWM_TIMERS; i++) {
		pwm_dev[i].virt_base = ioremap(pwm_dev[i].phys_base, 
						GPT_REGS_PAGE_SIZE);
						
		if (!pwm_dev[i].virt_base)
			goto timer_init_fail;

		pwm_off(&pwm_dev[i]);
		
		// frequency is a global module param
		if (pwm_set_frequency(&pwm_dev[i]))
			goto timer_init_fail;
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
	unsigned int duty_cycle;
	ssize_t error;
	struct pwm_dev *pd = filp->private_data;

	if (!buff) 
		return -EFAULT;

	// for user progs like cat that will keep asking forever
	if (*offp > 0) 
		return 0;

	if (down_interruptible(&pd->sem)) 
		return -ERESTARTSYS;

	if (pd->tclr & GPT_TCLR_ST) {
		duty_cycle = (100 * (pd->tmar - pd->tldr)) / pd->num_freqs;

		len = snprintf(pd->user_buff, USER_BUFF_SIZE,
				"PWM%d Frequency %u Hz Duty Cycle %u%%\n",
				pd->pwm, frequency, duty_cycle);
	}
	else {
		len = snprintf(pd->user_buff, USER_BUFF_SIZE,
				"PWM%d Frequency %u Hz Stopped\n",
				pd->pwm, frequency);
	}

	if (len + 1 < count) 
		count = len + 1;

	if (copy_to_user(buff, pd->user_buff, count))  {
		printk(KERN_ALERT "pwm_read: copy_to_user failed\n");
		error = -EFAULT;
	}
	else {
		*offp += count;
		error = count;
	}

	up(&pd->sem);

	return error;	
}

static ssize_t pwm_write(struct file *filp, const char __user *buff, 
			size_t count, loff_t *offp)
{
	size_t len;
	unsigned int duty_cycle;
	ssize_t error = 0;

	struct pwm_dev *pd = filp->private_data;
	
	if (down_interruptible(&pd->sem)) 
		return -ERESTARTSYS;

	if (!buff || count < 1) {
		printk(KERN_ALERT "pwm_write: input check failed\n");
		error = -EFAULT; 
		goto pwm_write_done;
	}
	
	if (count > 8)
		len = 8;
	else
		len = count;
		
	memset(pd->user_buff, 0, 16);

	if (copy_from_user(pd->user_buff, buff, len)) {
		printk(KERN_ALERT "pwm_write: copy_from_user failed\n"); 
		error = -EFAULT; 	
		goto pwm_write_done;
	}

	duty_cycle = simple_strtoul(pd->user_buff, NULL, 0);

	pwm_set_duty_cycle(pd, duty_cycle);

	*offp += count;

	error = count;

pwm_write_done:

	up(&pd->sem);
	
	return error;
}

static int pwm_open(struct inode *inode, struct file *filp)
{
	int error = 0;
	struct pwm_dev *pd = container_of(inode->i_cdev, struct pwm_dev, cdev);
	filp->private_data = pd;

	if (down_interruptible(&pd->sem)) 
		return -ERESTARTSYS;

	if (!pd->user_buff) {
		pd->user_buff = kmalloc(USER_BUFF_SIZE, GFP_KERNEL);
		if (!pd->user_buff)
			error = -ENOMEM;
	}

	up(&pd->sem);

	return error;	
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

	error = alloc_chrdev_region(&pd->devt, pd->pwm, 1, "pwm");

	if (error < 0) {
		printk(KERN_ALERT "alloc_chrdev_region fail: %d \n", error);
		pd->devt = 0;
		return -1;
	}

	cdev_init(&pd->cdev, &pwm_fops);
	pd->cdev.owner = THIS_MODULE;
	
	error = cdev_add(&pd->cdev, pd->devt, 1);
	if (error) {
		printk(KERN_ALERT "cdev_add failed: %d\n", error);
		unregister_chrdev_region(pd->devt, 1);
		pd->devt = 0;
		return -1;
	}	

	return 0;
}

static int __init pwm_init_class(struct pwm_dev *pd)
{
	if (!pwm_class) {
		pwm_class = class_create(THIS_MODULE, "pwm");

		if (!pwm_class) {
			printk(KERN_ALERT "class_create failed\n");
			return -1;
		}
	}
	
	pd->device = device_create(pwm_class, NULL, pd->devt, NULL, "pwm%d", 
				MINOR(pd->devt));
					
	if (!pd->device) {				
		printk(KERN_ALERT "device_create(..., pwm) failed\n");					
		return -1;
	}

	return 0;
}

static void pwm_dev_cleanup(void)
{
	int i;
	
	for (i = 0; i < NUM_PWM_TIMERS; i++) {
		if (pwm_dev[i].device)
			device_destroy(pwm_class, pwm_dev[i].devt);
	}
	
	class_destroy(pwm_class);
	
	for (i = 0; i < NUM_PWM_TIMERS; i++) {
		cdev_del(&pwm_dev[i].cdev);
		unregister_chrdev_region(pwm_dev[i].devt, 1);
	}			
}

static int __init pwm_init(void)
{
	int i;

	pwm_dev[0].pwm = 8;
	pwm_dev[0].mux_offset = GPT8_MUX_OFFSET;
	pwm_dev[0].phys_base = PWM8_CTL_BASE;

	pwm_dev[1].pwm = 9;
	pwm_dev[1].mux_offset = GPT9_MUX_OFFSET;
	pwm_dev[1].phys_base = PWM9_CTL_BASE;

	pwm_dev[2].pwm = 10;
	pwm_dev[2].mux_offset = GPT10_MUX_OFFSET;
	pwm_dev[2].phys_base = PWM10_CTL_BASE;

	pwm_dev[3].pwm = 11;
	pwm_dev[3].mux_offset = GPT11_MUX_OFFSET;
	pwm_dev[3].phys_base = PWM11_CTL_BASE;

	for (i = 0; i < NUM_PWM_TIMERS; i++) {
		pwm_dev[i].tldr = DEFAULT_TLDR;
		pwm_dev[i].tmar = DEFAULT_TMAR;
		pwm_dev[i].tclr = DEFAULT_TCLR;

		sema_init(&pwm_dev[i].sem, 1);

		if (pwm_init_cdev(&pwm_dev[i]))
			goto init_fail;

		if (pwm_init_class(&pwm_dev[i]))
			goto init_fail;
	}

	if (pwm_timer_init())
		goto init_fail_2;

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
	int i;
	
	pwm_dev_cleanup();
	pwm_timer_cleanup();
	
	for (i = 0; i < NUM_PWM_TIMERS; i++) {
		if (pwm_dev[i].user_buff)
			kfree(pwm_dev[i].user_buff);
	}
}
module_exit(pwm_exit);

MODULE_LICENSE("GPL");
MODULE_AUTHOR("Scott Ellis - Jumpnow");
MODULE_DESCRIPTION("PWM example for Gumstix Overo"); 

