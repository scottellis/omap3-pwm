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


The code should work with any OMAP3 board, but primarily tested with Gumstix Overos, a little
testing with Beagleboards.

The driver has two modes.

Duty-cycle mode (default) - generates PWM signals with outputs in the range 0-100 duty cycle.

Servo mode - runs at 50 Hz, generating pulses from 1-2 ms in duration by default


Build
-------

There is a ${MACHINE}-source-me.txt file that will set up your environment for
the cross-compilation. It assumes you are using an OE environment. 
Adjust for the build system you are using.


Follow these steps to build.

	$ git clone git://github.com/scottellis/omap3-pwm.git
	$ cd omap3-pwm


If you have your OE temp directory in a non-standard location, then export an
OETMP variable with the path before sourcing the overo-source-me.txt file. 

	$ [optional] export OETMP=/<your-oetmp-path>

Then

	$ source overo-source-me.txt
	$ make 


Copy the pwm.ko file to your board.


Once on the system, use insmod to load using the optional parameters.

Driver parameters

*timers* - A comma separated list of the timers to use, 8-11. The driver will create
/dev/pwmXX devices for each timer you enable. The default is all 4 timers.

Example: timers=8,9,10,11

*frequency* - non-servo mode only, specify the frequency of the pwm pulse, the default
is 1024, the max is 13Mhz / 2.

Example: frequency=1024

*servo* - Whether to enable servo mode. Values 0 or 1, default is 0.

Example: servo=1

*servo_min* - Minimum value for servo pulse in tenths of microseconds. The default is 10000 representing 1 ms. The absolute min is 5000 or 0.5 ms.

Example: servo_min=12000

*servo_max* - Maximum value for servo pulse in tenths of microseconds. The default is 20000 representing 2 ms. The absolute max is 25000 or 2.5 ms.

Example: servo_max=18000

*servo_start* - Start value for servo pulse in tenths of microseconds. The default is 15000 representing 1.5 ms.
Electronic speed controllers might prefer a 1 ms pulse. The range is servo_min to servo_max.

Example: servo_start=10000


*nomux* - Do not mux the pins for PWM usage. The driver only knows about mux'ing PWM8-11 out the GPIO144-147
(UART2_CTS, UART2_TX) pads, what the Gumstix uses. If you want to use PWM coming off different pads, then 
either modify the driver or set this flag and make sure the pads are mux'd in another place, like the 
bootloader or the Linux board init code. Refer to table 7.4 in the OMAP3 TRM and your board documentation.
The default is nomux=0 which tells the driver to mux the pins.

Example: nomux=1


Here is an example session with the driver.

	root@overo# ls
	pwm.ko

	root@overo# insmod pwm.ko timers=8,10

The driver will create /dev/pwm8 and /dev/pwm10 in duty-cycle mode.

To issue commands you can use any program that can do file I/O. 
The standard utilities cat and echo will work. 

	root@overo# cat /dev/pwm10
	0

	root@overo# echo 50 > /dev/pwm10

	root@overo:~# cat /dev/pwm10
	50

	root@overo:~# echo 80 > /dev/pwm10

	root@overo:~# cat /dev/pwm10
	80

In duty-cycle mode, valid settings are in the range 0-100.

You can put an oscope on pin 28 of the Overo expansion board to see the signal for pwm10.
Use pin 15 for ground. Or you can measure the voltage on pin 28 and you'll see the duty 
cycle percentage of 1.8v.

Here are the expansion board pins for all the PWM timers

	PWM8  (gpio_147) : pin 29
	PWM9  (gpio_144) : pin 30
	PWM10 (gpio_145) : pin 28
	PWM11 (gpio_146) : pin 27

You have to unload and reload the module to change any of the module parameters.

	root@overo:~# rmmod pwm  

	root@overo:~# insmod pwm.ko servo=1

This module load tells the driver to create all 4 PWM devices, /dev/pwm8-11,
in servo mode. 

	root@overo:~# echo 12500 > /dev/pwm9
	
	root@overo:~# cat /dev/pwm9
	12500

In servo mode the driver starts all PWM at 15000 or 1.5 ms pulse duration which is
the standard zero position for servos.

	root@overo:~# cat /dev/pwm8
	15000

In servo mode the driver wants settings in tenths of microseconds with the default
range of 10000 to 20000.


The driver takes care of muxing the output pins correctly for the Overo
boards and restores the original muxing when it unloads. The default muxing
by Gumstix for the PWM pins is to be GPIO. 

The driver also switches PWM10 and PWM11 to use a 13MHz clock for the source
similar to what PWM8 and PWM9 use by default. This is currently not 
configurable.

Gumstix Note: GPIO 144 and 145, PWM 9 and 10, are used with the lcd touchscreen
displays. See board-overo.c in your kernel source for details. You probably don't
want to use PWM 9 and 10 at the same time you are using an lcd. I haven't tried
it.


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


