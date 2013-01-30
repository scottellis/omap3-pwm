/*
 Copyright (c) 2010-2012, Scott Ellis
 All rights reserved.

 This program is free software; you can redistribute it and/or modify
 it under the terms of the GNU General Public License as published by
 the Free Software Foundation; either version 2 of the License, or
 (at your option) any later version.

 This program is distributed in the hope that it will be useful,
 but WITHOUT ANY WARRANTY; without even the implied warranty of
 MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the
 GNU General Public License for more details.

 You should have received a copy of the GNU General Public License
 along with this program; if not, write to the Free Software
 Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA 02111-1307 USA
*/

#ifndef PWM_IOCTL_H
#define PWM_IOCTL_H

#include <linux/ioctl.h>

#define PWM_IOC_MAGIC 'P'

#define PWM_PULSE_RESET _IO(PWM_IOC_MAGIC, 0)
#define PWM_PULSE_SET _IOW(PWM_IOC_MAGIC, 1, int)

#define PWM_IOC_MAXNR 1

#endif
