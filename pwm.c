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
	struct class *class;
	struct semaphore sem;
	u32 pwm;
	u32 mux_offset;
	u32 gpt_base;
	u32 input_freq;
	u32 old_mux;
	u32 tldr;
	u32 tmar;
	u32 tclr;
	u32 num_freqs;
	char *user_buff;
	u8 number;
};

#define NUM_PWM_TIMERS 4
static struct pwm_dev pwm_dev[NUM_PWM_TIMERS];


static int init_mux(int channel)
{
	void __iomem *base;

	base = ioremap(OMAP34XX_PADCONF_START, OMAP34XX_PADCONF_SIZE);
	if (!base) {
		printk(KERN_ALERT "init_mux(): ioremap() failed\n");
		return -1;
	}

	pwm_dev[channel].old_mux = ioread16(base + pwm_dev[channel].mux_offset);
	iowrite16(PWM_ENABLE_MUX, base + pwm_dev[channel].mux_offset);
	iounmap(base);	

	return 0;
}

static int restore_mux(int channel)
{
	void __iomem *base;

	if (pwm_dev[channel].old_mux) {
		base = ioremap(OMAP34XX_PADCONF_START, OMAP34XX_PADCONF_SIZE);
	
		if (!base) {
			printk(KERN_ALERT "restore_mux(): ioremap() failed\n");
			return -1;
		}

		iowrite16(pwm_dev[channel].old_mux, base + pwm_dev[channel].mux_offset);
		iounmap(base);	
	}

	return 0;
}

/* Change PWM10 and PWM11 to use CM_SYS_CLK rather then CM_32K_CLK */
static int use_sys_clk(void)
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
static int restore_32k_clk(void)
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

static int set_pwm_frequency(int channel)
{
	void __iomem *base;

	base = ioremap(pwm_dev[channel].gpt_base, GPT_REGS_PAGE_SIZE);
	if (!base) {
		printk(KERN_ALERT "set_pwm_frequency(): ioremap failed\n");
		return -1;
	}

	if (frequency < 0) {
		frequency = DEFAULT_PWM_FREQUENCY;
	} else {
		/* only powers of two, for simplicity */
		frequency &= ~0x01;

		if (frequency > (pwm_dev[channel].input_freq / 2)) 
			frequency = pwm_dev[channel].input_freq / 2;
		else if (frequency == 0)
			frequency = DEFAULT_PWM_FREQUENCY;
	}

	/* PWM_FREQ = 32768 / ((0xFFFF FFFF - TLDR) + 1) */
	pwm_dev[channel].tldr = 0xFFFFFFFF - ((pwm_dev[channel].input_freq / frequency) - 1);

	/* just for convenience */	
	pwm_dev[channel].num_freqs = 0xFFFFFFFE - pwm_dev[channel].tldr;	

	iowrite32(pwm_dev[channel].tldr, base + GPT_TLDR);

	/* initialize TCRR to TLDR, have to start somewhere */
	iowrite32(pwm_dev[channel].tldr, base + GPT_TCRR);

	iounmap(base);

	return 0;
}

static int pwm_off(int channel)
{
	void __iomem *base;

	base = ioremap(pwm_dev[channel].gpt_base, GPT_REGS_PAGE_SIZE);
	if (!base) {
		printk(KERN_ALERT "pwm_off(): ioremap failed\n");
		return -1;
	}

	pwm_dev[channel].tclr &= ~GPT_TCLR_ST;
	iowrite32(pwm_dev[channel].tclr, base + GPT_TCLR); 
	iounmap(base);

	return 0;
}

static int pwm_on(int channel)
{
	void __iomem *base;

	base = ioremap(pwm_dev[channel].gpt_base, GPT_REGS_PAGE_SIZE);

	if (!base) {
		printk(KERN_ALERT "pwm_on(): ioremap failed\n");
		return -1;
	}

	/* set the duty cycle */
	iowrite32(pwm_dev[channel].tmar, base + GPT_TMAR);
	
	/* now turn it on */
	pwm_dev[channel].tclr = ioread32(base + GPT_TCLR);
	pwm_dev[channel].tclr |= GPT_TCLR_ST;
	iowrite32(pwm_dev[channel].tclr, base + GPT_TCLR); 
	iounmap(base);

	return 0;
}

static int set_us_pulse(unsigned int us_pulse, int channel) 
{
	u32 new_tmar;
	u32 factor;

	pwm_off(channel);

	if (us_pulse == 0)
		return 0;

	printk("us pulse rx=%d frequency=%d num_freq=%d ", us_pulse, frequency, pwm_dev[channel].num_freqs);
 
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
	new_tmar = (us_pulse * (pwm_dev[channel].num_freqs / 2)) / factor;

	printk("new tmar: %d\n", new_tmar);

	if (new_tmar < 1) 
		new_tmar = 1;
	else if (new_tmar > pwm_dev[channel].num_freqs)
		new_tmar = pwm_dev[channel].num_freqs;
		
	pwm_dev[channel].tmar = pwm_dev[channel].tldr + new_tmar;
	
	return pwm_on(channel);
}

static ssize_t pwm_read(struct file *filp, char __user *buff, size_t count,
			loff_t *offp)
{
	size_t len;
	unsigned int duty_cycle;
	ssize_t error = 0;

	struct pwm_dev * pwm_devp = filp->private_data;

	if (!buff) 
		return -EFAULT;

	/* tell the user there is no more */
	if (*offp > 0) 
		return 0;

	if (down_interruptible(&pwm_devp->sem)) 
		return -ERESTARTSYS;

	if (pwm_devp->tclr & GPT_TCLR_ST) {
		duty_cycle = (100 * (pwm_devp->tmar - pwm_devp->tldr)) 
				/ pwm_devp->num_freqs;

		snprintf(pwm_devp->user_buff, USER_BUFF_SIZE,
				"PWM%d Frequency %u Hz Duty Cycle %u%%\n",
				pwm_devp->pwm, frequency, duty_cycle);
	}
	else {
		snprintf(pwm_devp->user_buff, USER_BUFF_SIZE,
				"PWM%d Frequency %u Hz Stopped\n",
				pwm_devp->pwm, frequency);
	}

	len = strlen(pwm_devp->user_buff);
 
	if (len + 1 < count) 
		count = len + 1;

	if (copy_to_user(buff, pwm_devp->user_buff, count))  {
		printk(KERN_ALERT "pwm_read(): copy_to_user() failed\n");
		error = -EFAULT;
	}
	else {
		*offp += count;
		error = count;
	}

	up(&pwm_devp->sem);

	return error;	
}

static ssize_t pwm_write(struct file *filp, const char __user *buff, 
			size_t count, loff_t *offp)
{
	size_t len;
	unsigned int us_pulse;
	ssize_t error = 0;

	struct pwm_dev * pwm_devp = filp->private_data;
	
	if (down_interruptible(&pwm_devp->sem)) 
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
		
	memset(pwm_devp->user_buff, 0, 16);

	if (copy_from_user(pwm_devp->user_buff, buff, len)) {
		printk(KERN_ALERT "pwm_write(): copy_from_user() failed\n"); 
		error = -EFAULT; 	
		goto pwm_write_done;
	}

	us_pulse = simple_strtoul(pwm_devp->user_buff, NULL, 0);

	set_us_pulse(us_pulse,pwm_devp->number);

	/* pretend we ate it all */
	*offp += count;

	error = count;

pwm_write_done:

	up(&pwm_devp->sem);
	
	return error;
}

static int pwm_open(struct inode *inode, struct file *filp)
{
	int error = 0;

	struct pwm_dev * pwm_devp;
	pwm_devp = container_of(inode->i_cdev, struct pwm_dev, cdev);
	filp->private_data = pwm_devp;

	if (down_interruptible(&pwm_devp->sem)) 
		return -ERESTARTSYS;

	if (pwm_devp->old_mux == 0) {
		if (init_mux(pwm_devp->number))  
			error = -EIO;
		else if (set_pwm_frequency(pwm_devp->number)) 
			error = -EIO;
	}

	if (!pwm_devp->user_buff) {
		pwm_devp->user_buff = kmalloc(USER_BUFF_SIZE, GFP_KERNEL);
		if (!pwm_devp->user_buff)
			error = -ENOMEM;
	}

	up(&pwm_devp->sem);

	return error;	
}

static struct file_operations pwm_fops = {
	.owner = THIS_MODULE,
	.read = pwm_read,
	.write = pwm_write,
	.open = pwm_open,
};

static int __init pwm_init_cdev(int channel)
{
	int error;

	error = alloc_chrdev_region(&pwm_dev[channel].devt, pwm_dev[channel].pwm, 
					1, "pwm");

	if (error < 0) {
		printk(KERN_ALERT "alloc_chrdev_region() failed: %d \n", 
			error);
		return -1;
	}

	cdev_init(&pwm_dev[channel].cdev, &pwm_fops);
	pwm_dev[channel].cdev.owner = THIS_MODULE;
	
	error = cdev_add(&pwm_dev[channel].cdev, pwm_dev[channel].devt, 1);
	if (error) {
		printk(KERN_ALERT "cdev_add() failed: %d\n", error);
		unregister_chrdev_region(pwm_dev[channel].devt, 1);
		return -1;
	}	

	return 0;
}

static int __init pwm_init_class(int channel)
{
	if(channel == 0) //FIXME this is a hack
		pwm_dev[channel].class = class_create(THIS_MODULE, "pwm");
	else
		pwm_dev[channel].class = pwm_dev[0].class;

	if (!pwm_dev[channel].class) {
		printk(KERN_ALERT "class_create() failed\n");
		return -1;
	}

	if (!device_create(pwm_dev[channel].class, NULL, pwm_dev[channel].devt, NULL, "pwm%d", 
				MINOR(pwm_dev[channel].devt))) {
		printk(KERN_ALERT "device_create(..., pwm) failed\n");
		class_destroy(pwm_dev[channel].class);
		return -1;
	}

	return 0;
}
 
static int pulse_init_timers(void)
{
	int i;

	if (use_sys_clk()) 
	  return -1;
	
	for (i = 0; i < NUM_PWM_TIMERS; i++) 
	  set_us_pulse(1500, i);

	return 0;
}

static int __init pwm_init(void)
{
	int i,error = 0;

	memset(&pwm_dev, 0, sizeof(struct pwm_dev));

	/* change these 4 values to use a different PWM */
	pwm_dev[0].pwm = 8;
	pwm_dev[1].pwm = 9;
	pwm_dev[2].pwm = 10;
	pwm_dev[3].pwm = 11;

	pwm_dev[0].number = 0;
	pwm_dev[1].number = 1;
	pwm_dev[2].number = 2;
	pwm_dev[3].number = 3;

	pwm_dev[0].mux_offset = GPT8_MUX_OFFSET;
	pwm_dev[0].gpt_base = PWM8_CTL_BASE;
	pwm_dev[0].input_freq = CLK_SYS_FREQ;

	pwm_dev[1].mux_offset = GPT9_MUX_OFFSET;
	pwm_dev[1].gpt_base = PWM9_CTL_BASE;
	pwm_dev[1].input_freq = CLK_SYS_FREQ;

	pwm_dev[2].mux_offset = GPT10_MUX_OFFSET;
	pwm_dev[2].gpt_base = PWM10_CTL_BASE;
	pwm_dev[2].input_freq = CLK_SYS_FREQ;

	pwm_dev[3].mux_offset = GPT11_MUX_OFFSET;
	pwm_dev[3].gpt_base = PWM11_CTL_BASE;
	pwm_dev[3].input_freq = CLK_SYS_FREQ;

	for(i=0;i<NUM_PWM_TIMERS;i++) {
		pwm_dev[i].tldr = DEFAULT_TLDR;
		pwm_dev[i].tmar = DEFAULT_TMAR;
		pwm_dev[i].tclr = DEFAULT_TCLR;

		sema_init(&pwm_dev[i].sem, 1);

		if (pwm_init_cdev(i))
			return error;

		if (pwm_init_class(i)) {
			cdev_del(&pwm_dev[i].cdev);
			unregister_chrdev_region(pwm_dev[i].devt, 1);
			return error;
		}
	}

	if (pulse_init_timers()) {
	  for(i=0;i<NUM_PWM_TIMERS;i++) {
	    device_destroy(pwm_dev[i].class, pwm_dev[i].devt);
	    class_destroy(pwm_dev[i].class);
	  }
	  return error;
	}

	return 0;
}
module_init(pwm_init);

static void __exit pwm_exit(void)
{
	int i;
	for(i=0;i<NUM_PWM_TIMERS;i++) {
		device_destroy(pwm_dev[i].class, pwm_dev[i].devt);

		cdev_del(&pwm_dev[i].cdev);
		unregister_chrdev_region(pwm_dev[i].devt, 1);

		pwm_off(i);
		restore_mux(i);

		restore_32k_clk();

		if (pwm_dev[i].user_buff)
			kfree(pwm_dev[i].user_buff);
	}
	class_destroy(pwm_dev[0].class);
}
module_exit(pwm_exit);

MODULE_LICENSE("GPL");
MODULE_AUTHOR("Scott Ellis - Jumpnow");
MODULE_DESCRIPTION("PWM example for Gumstix Overo"); 

