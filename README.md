  omap3 pwm driver
=======

A driver for the PWM timers of an OMAP3 based Linux system exposing a simple
char dev interface to userland.

Jack Elston and Curtis Olson are coathors of this code. They are responsible for
the [four-channel] branch of this project.

The TI TRM is the primary reference for the code. 

I put up some of my notes for the OMAP3 PWM timers here:

http://www.jumpnowtek.com/index.php?option=com_content&view=article&id=56&Itemid=63

And Curtis Olson has a more general PWM article here:

http://gallinazo.flightgear.org/technology/gumstix-overo-rc-servos-and-pwm-signal-generation/


The code should work with any OMAP3 board, but I only tested with Gumstix Overo. 

The [master] branch of this project only implements one PWM channel at a time.
The default is PWM10, but you can change that with a module parameter.

The [four-channel] branch of the project implements all 4 PWM channels each with
their own char dev node. 

There is a ${MACHINE}-source-me.txt file that will set up your environment for
the cross-compilation. It assumes you are using an OE environment and it tries 
to be generic enough for both userland and kernel/module stuff. Export an OETMP
before sourcing this file if you have a non-standard OETMP location. 

Follow these steps to build. Using an overo for the example.

	$ git clone git://github.com/scottellis/omap3-pwm.git
	$ cd omap3-pwm

If you want to build the [four-channel] branch use git to check it out now.

	$ git checkout -b four-channel origin/four-channel

Then

	<optional> $ export OETMP=/<some-non-standard-location>

	$ source overo-source-me.txt
	$ make 

Next copy the pwm.ko file to your board.

These final instructions apply to the [master] branch version of the driver.

The [four-channel] instruction are coming soon.

Once on the system, use insmod to load the driver.
There are several parameters you can pass to the module on load.

pwm=<channel> choices are 8,9,10 or 11

use_sys_clock=1 only applicable for PWM 10 or 11. PWM 8 and 9 already use
		the 13 MHz sys clock for input. The default for PWM 10 or 11
		is to use a 32768 Hz clock. Gives you better granularity.

frequency=<n> 	Some multiple of two (because I'm a lazy coder) up to the input
		clock frequency / 2. The input clock is either 13 MHz 
		or 32768 Hz depending on the PWM channel and whether the
		use_sys_clock is set. The default frequency is 1024.

The driver implements a character device interface. When it loads, it will 
create a /dev/pwm<channel> entry. If you are on the console or watching the log
the driver will show you the source clock frequency.

	root@overo# ls
	pwm.ko

	root@overo# insmod pwm.ko
	[17311.489135] source clock rate 32768

	root@overo:~# rmmod pwm

	root@overo:~# insmod pwm.ko pwm=8
	[17373.138122] source clock rate 13000000

	root@overo:~# rmmod pwm

	root@overo:~# insmod pwm.ko pwm=10 use_sys_clock=1
	[17445.638397] source clock rate 13000000


Then to issue commands you can use any program that can do file I/O. 
cat and echo will work. 

	root@overo# cat /dev/pwm10
	PWM10 Frequency 1024 Hz Stopped

	root@overo# echo 50 > /dev/pwm10

	root@overo:~# cat /dev/pwm10
	PWM10 Frequency 1024 Hz Duty Cycle 50%

	root@overo:~# echo 80 > /dev/pwm10

	root@overo:~# cat /dev/pwm10
	PWM10 Frequency 1024 Hz Duty Cycle 80%

You can put an oscope on pin 28 of the expansion board to see the signal.
Use pin 15 for ground. Or you can measure the voltage on pin 28 and you'll
see the duty cycle percentage of 1.8v.

You have to unload and reload the module to change the frequency.

	root@overo:~# rmmod pwm  

	root@overo:~# insmod pwm.ko frequency=2048

	root@overo:~# cat /dev/pwm10
	PWM10 Frequency 2048 Hz Stopped

The driver takes care of muxing the output pin correctly and restores the original
muxing when it unloads. The default muxing by Gumstix for the PWM pins is to be
GPIO. 


The driver now also starts the PWM input clock so the following note is probably
not necessary. I didn't test with a beagleboard though, so I'll leave it here.

BEAGLEBOARD Note: The kernel config option CONFIG_OMAP_RESET_CLOCKS is enabled
in the default beagleboard defconfigs. You'll get an oops using pwm.ko with
this enabled. This is a kernel power saving feature. You'll need to disable this 
config option to use this driver. Below is a sample patch for linux-omap-2.6.32's
defconfig. Adjust for the kernel you are using. Gumstix users already have this
turned off in default kernels.

	diff --git a/recipes/linux/linux-omap-2.6.32/beagleboard/defconfig b/recipes/linux/linux-omap-2.6.32/beagleboard/defconfig
	index cebe1f5..2dad30c 100644
	--- a/recipes/linux/linux-omap-2.6.32/beagleboard/defconfig
	+++ b/recipes/linux/linux-omap-2.6.32/beagleboard/defconfig
	@@ -241,7 +241,7 @@ CONFIG_ARCH_OMAP3=y
	 #
	 # CONFIG_OMAP_DEBUG_POWERDOMAIN is not set
	 # CONFIG_OMAP_DEBUG_CLOCKDOMAIN is not set
	-CONFIG_OMAP_RESET_CLOCKS=y
	+# CONFIG_OMAP_RESET_CLOCKS is not set
	 # CONFIG_OMAP_MUX is not set
	 CONFIG_OMAP_MCBSP=y
	 CONFIG_OMAP_MBOX_FWK=m


