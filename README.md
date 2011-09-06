  omap3 pwm driver
=======

Implements a driver to test the PWM outputs of an OMAP3 based Linux system from userspace.

Developers
-------
Scott Ellis, Jack Elston, Curtis Olson

The TI TRM is the reference for all this. 

I have some notes for the OMAP3 PWM timers here

http://www.jumpnowtek.com/index.php?option=com_content&view=article&id=56&Itemid=63

Curtis Olson has a relevant PWM/servo article here

http://gallinazo.flightgear.org/technology/gumstix-overo-rc-servos-and-pwm-signal-generation/


The code should work with any OMAP3 board, but was only tested with Gumstix Overo 
and Beagleboard.

Their are two branches of interest in the project 

The [master] branch implements a duty-cycles of 0-100% for PWM output.

The [servo] branch implements a PWM output geared toward servo control.


The rest of this README refers to the [master] branch. Checkout the [servo]
branch and the README there for details on using servo mode outputs.

There is a ${MACHINE}-source-me.txt file that will set up your environment for
the cross-compilation. It assumes you are using an OE environment. 
Adjust for the build system you are using.


Follow these steps to build.

	$ git clone git://github.com/scottellis/omap3-pwm.git
	$ cd omap3-pwm

If you want to build the [servo] branch, use git to check it out now.

	$ git checkout -b servo origin/servo

If you have your OE temp directory in a non-standard location, then export an
OETMP variable with the path before sourcing the overo-source-me.txt file. 

	$ [optional] export OETMP=/<your-oetmp-path>

Then

	$ source overo-source-me.txt
	$ make 


Copy the pwm.ko file to your board.


Once on the system, use insmod to load using the optional frequency and timers
parameters. The default frequency is 1024 Hz. Use multiples of two with a max 
of 16384.

The default behavior is for the driver to enable all four PWM timers. You
can customize this with a timers=<timer list> where timer list is a comma
separated list of the numbers 8-11.

	root@overo# ls
	pwm.ko

	root@overo# insmod pwm.ko timers=8,10

The driver implements a character device interface. When it loads, it will 
create /dev/pwmXX entries for each of the timers specified.
 
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

You can put an oscope on pin 28 of the Overo expansion board to see the signal for pwm10.
Use pin 15 for ground. Or you can measure the voltage on pin 28 and you'll see the duty 
cycle percentage of 1.8v.

Here are the expansion board pins for all the PWM timers

PWM8  (gpio_147) : pin 29
PWM9  (gpio_144) : pin 30
PWM10 (gpio_145) : pin 28
PWM11 (gpio_146) : pin 27

You have to unload and reload the module to change the frequency or the active
timers.

	root@overo:~# rmmod pwm  

	root@overo:~# insmod pwm.ko frequency=2048

	root@overo:~# cat /dev/pwm10
	PWM10 Frequency 2048 Hz Stopped

The driver takes care of muxing the output pins correctly and restores the 
original muxing when it unloads. The default muxing by Gumstix for the PWM 
pins is to be GPIO. 

The driver also switches PWM10 and PWM11 to use a 13MHz clock for the source
similar to what PWM8 and PWM9 use by default. This is currently not 
configurable.

Gumstix Note: GPIO 144 and 145, PWM 9 and 10, are used with the lcd displays.
See board-overo.c in your kernel source for details.


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


