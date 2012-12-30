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

#ifndef PWM_H
#define PWM_H

#define OMAP34XX_PADCONF_START  0x48002030
#define OMAP34XX_PADCONF_SIZE   0x05cc

#define GPT8_MUX_OFFSET		(0x4800217A - OMAP34XX_PADCONF_START)
#define GPT9_MUX_OFFSET		(0x48002174 - OMAP34XX_PADCONF_START)
#define GPT10_MUX_OFFSET	(0x48002176 - OMAP34XX_PADCONF_START)
#define GPT11_MUX_OFFSET	(0x48002178 - OMAP34XX_PADCONF_START)

#define PWM_ENABLE_MUX		0x0002	/* IDIS | PTD | DIS | M2 */


#endif /* ifndef PWM_H */
