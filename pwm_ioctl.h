#ifndef PWM_IOCTL_H
#define PWM_IOCTL_H

#include <linux/ioctl.h>

/*
 * Ioctl definitions
 */

/* Use 'P' as magic number */
#define PWM_IOC_MAGIC	'P'

#define PWM_PULSE_RESET	_IO(PWM_IOC_MAGIC, 0)
#define PWM_PULSE_SET	_IOW(PWM_IOC_MAGIC, 1, int)
#define PWM_FREQ_SET	_IOW(PWM_IOC_MAGIC, 2, int)
#define PWM_FREQ_GET	_IOR(PWM_IOC_MAGIC, 3, int)


#define PWM_IOC_MAXNR   3

#endif
