  omap3 pwm driver
=======

Implements a driver to easily test the PWM outputs of an OMAP3 based Linux
system from userspace.

The TI TRM is the reference for all this. I did put a few notes I collected
for the OMAP3 PWM timers over here:

http://www.jumpnowtek.com/index.php?option=com_content&view=article&id=56&Itemid=63

The code should work with any OMAP3 board, but I only tested with Gumstix Overo 
and Beagleboard.

The [master] branch of this project only implements one PWM channel. The default
is PWM10. 

The [four-channel] branch of the project implements all for PWM channels with
their own char dev node. 

There is a ${MACHINE}-source-me.txt file that will set up your environment for
the cross-compilation. It assumes you are using an OE environment and it tries 
to be generic enough for both userland and kernel/module stuff. 

You should modify or create a similar script for pointing to the build system 
you are using.

If you modified your OE temp directory, then also update the OETMP variable in 
the appropriate ${MACHINE}-source-me.txt. I kind of tested overo and beagleboard, 
but I don't normally use the defaults.

Follow these steps to build. Using an overo for the example.

	$ git clone git://github.com/scottellis/omap3-pwm.git
	$ cd omap3-pwm

If you want to build the [four-channel] branch use git to check it out now.

	$ git checkout -b four-channel origin/four-channel

Then

	$ <edit> overo-source-me.txt
	$ source overo-source-me.txt
	$ make 

Next copy the pwm.ko file to your board.

These final instructions apply to the [master] branch version of the driver.

The [four-channel] instruction are coming soon.

Once on the system, use insmod to load using the optional frequency parameter.
The default frequency is 1024 Hz. Use multiples of two with a max of 16384.

	root@overo# ls
	pwm.ko

	root@overo# insmod pwm.ko

The driver implements a character device interface. When it loads, it will 
create a /dev/pwm10 entry.
 
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

For now, if you want to change which timer is being used, look at pwm_init().



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


