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
	u32 gpt_base;
	struct clk *clk;
	u32 input_freq;
	u32 old_mux;
	u32 tldr;
	u32 tmar;
	u32 tclr;
	u32 num_freqs;
	char *user_buff;
};

struct class *pwm_class;

#define NUM_PWM_TIMERS 4
static struct pwm_dev pwm_dev[NUM_PWM_TIMERS];


static int pwm_init_mux(struct pwm_dev *pd)
{
	void __iomem *base;

	base = ioremap(OMAP34XX_PADCONF_START, OMAP34XX_PADCONF_SIZE);
	if (!base) {
		printk(KERN_ALERT "init_mux(): ioremap() failed\n");
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
			printk(KERN_ALERT "restore_mux(): ioremap() failed\n");
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
		printk(KERN_ALERT "use_sys_clk(): ioremap() failed\n");
		return -1;
	}

	val = ioread32(base + CM_CLKSEL_CORE_OFFSET);
	val |= 0xc0;
	iowrite32(val, base + CM_CLKSEL_CORE_OFFSET);
	iounmap(base);

	return 0;
}

/* Restore PWM10 and PWM11 to using the CM_32K_CLK */
static int pwm_restore_32k_clk(void)
{
	void __iomem *base;
	u32 val;

	base = ioremap(CLOCK_CONTROL_REG_CM_START, CLOCK_CONTROL_REG_CM_SIZE);

	if (!base) {
		printk(KERN_ALERT "restore_32k_clk(): ioremap() failed\n");
		return -1;
	}

	val = ioread32(base + CM_CLKSEL_CORE_OFFSET);
	val &= ~0xc0;
	iowrite32(val, base + CM_CLKSEL_CORE_OFFSET);
	iounmap(base);

	return 0;
}

static int pwm_set_frequency(struct pwm_dev *pd)
{
	void __iomem *base;

	base = ioremap(pd->gpt_base, GPT_REGS_PAGE_SIZE);
	if (!base) {
		printk(KERN_ALERT "set_pwm_frequency(): ioremap failed\n");
		return -1;
	}

	if (frequency < 0) {
		frequency = DEFAULT_PWM_FREQUENCY;
	} else {
		/* only powers of two, for simplicity */
		frequency &= ~0x01;

		if (frequency > (pd->input_freq / 2)) 
			frequency = pd->input_freq / 2;
		else if (frequency == 0)
			frequency = DEFAULT_PWM_FREQUENCY;
	}

	/* PWM_FREQ = 32768 / ((0xFFFF FFFF - TLDR) + 1) */
	pd->tldr = 0xFFFFFFFF - ((pd->input_freq / frequency) - 1);

	/* just for convenience */	
	pd->num_freqs = 0xFFFFFFFE - pd->tldr;	

	iowrite32(pd->tldr, base + GPT_TLDR);

	/* initialize TCRR to TLDR, have to start somewhere */
	iowrite32(pd->tldr, base + GPT_TCRR);

	iounmap(base);

	return 0;
}

static int pwm_off(struct pwm_dev *pd)
{
	void __iomem *base;

	base = ioremap(pd->gpt_base, GPT_REGS_PAGE_SIZE);
	if (!base) {
		printk(KERN_ALERT "pwm_off(): ioremap failed\n");
		return -1;
	}

	pd->tclr &= ~GPT_TCLR_ST;
	iowrite32(pd->tclr, base + GPT_TCLR); 
	iounmap(base);

	return 0;
}

static int pwm_on(struct pwm_dev *pd)
{
	void __iomem *base;

	base = ioremap(pd->gpt_base, GPT_REGS_PAGE_SIZE);

	if (!base) {
		printk(KERN_ALERT "pwm_on(): ioremap failed\n");
		return -1;
	}

	/* set the duty cycle */
	iowrite32(pd->tmar, base + GPT_TMAR);
	
	/* now turn it on */
	pd->tclr = ioread32(base + GPT_TCLR);
	pd->tclr |= GPT_TCLR_ST;
	iowrite32(pd->tclr, base + GPT_TCLR); 
	iounmap(base);

	return 0;
}

static int pwm_set_us_pulse(struct pwm_dev *pd, unsigned int us_pulse) 
{
	u32 new_tmar, factor;

	pwm_off(pd);

	if (us_pulse == 0)
		return 0;

	printk(KERN_INFO "us pulse rx=%d frequency=%d num_freq=%d ", 
		us_pulse, frequency, pd->num_freqs);
 
	/* new_tmar is the duty cycle, basically 0 - num_freqs maps to
	   0% - 100% duty cycle.

	   So (us_pulse/1000000) * frequency * num_freq = tmar but we
	   have to worry about integer overflow (INT_MAX=4294967295u)
	   so I precompute 1000000 / freq and then divide by that
	   number to stay inside uint32 limits.

	   Hack: gpt.num_freqs is 259998 for the 13Mhz timer.  This
	   can be divided evenly by 2, 3, 17, and 2549.  If I
	   predivide by 259998 by 2, I think I can specify the input
	   pulse length in us*10 and still stay inside u32 integer
	   bounds.
	*/

	/* original code that overflows a uint32 with us input:
	   new_tmar = (us_pulse * frequency * pwm_dev[channel].num_freqs)
	              / 1000000;
	*/

	/* refactored formula to not overflow with us input:
	   factor = 1000000 / frequency;
	   new_tmar = (us_pulse * pwm_dev[channel].num_freqs) / factor;
	*/

	/* further observation that we can divide both gpt.num_freq
	   and 1000000 by 2 which makes the intermediate math even
	   smaller.  This allows us to specify input in us*10 for
	   1/10us accuracy ... woot woot!  Note, forumula is updated
	   to expect us*10 input*/
	factor = 10000000 / (frequency * 2);
	new_tmar = (us_pulse * (pd->num_freqs / 2)) / factor;

	printk("new tmar: %d\n", new_tmar);

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
		if (pwm_set_frequency(&pwm_dev[i]))
			goto timer_init_fail;
			
		if (pwm_set_us_pulse(&pwm_dev[i], 1500))
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

	/* tell the user there is no more */
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
		printk(KERN_ALERT "pwm_read(): copy_to_user() failed\n");
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
	unsigned int us_pulse;
	ssize_t error = 0;

	struct pwm_dev *pd = filp->private_data;
	
	if (down_interruptible(&pd->sem)) 
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
		
	memset(pd->user_buff, 0, 16);

	if (copy_from_user(pd->user_buff, buff, len)) {
		printk(KERN_ALERT "pwm_write(): copy_from_user() failed\n"); 
		error = -EFAULT; 	
		goto pwm_write_done;
	}

	us_pulse = simple_strtoul(pd->user_buff, NULL, 0);

	pwm_set_us_pulse(pd, us_pulse);

	/* pretend we ate it all */
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
		printk(KERN_ALERT "alloc_chrdev_region() fail: %d \n", error);
		pd->devt = 0;
		return -1;
	}

	cdev_init(&pd->cdev, &pwm_fops);
	pd->cdev.owner = THIS_MODULE;
	
	error = cdev_add(&pd->cdev, pd->devt, 1);
	if (error) {
		printk(KERN_ALERT "cdev_add() failed: %d\n", error);
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
			printk(KERN_ALERT "class_create() failed\n");
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
	pwm_dev[0].gpt_base = PWM8_CTL_BASE;

	pwm_dev[1].pwm = 9;
	pwm_dev[1].mux_offset = GPT9_MUX_OFFSET;
	pwm_dev[1].gpt_base = PWM9_CTL_BASE;

	pwm_dev[2].pwm = 10;
	pwm_dev[2].mux_offset = GPT10_MUX_OFFSET;
	pwm_dev[2].gpt_base = PWM10_CTL_BASE;

	pwm_dev[3].pwm = 11;
	pwm_dev[3].mux_offset = GPT11_MUX_OFFSET;
	pwm_dev[3].gpt_base = PWM11_CTL_BASE;

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

