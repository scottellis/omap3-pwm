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
  

 Some defs for the PADCONF pin muxing.
 TODO: Find where these are defined in the kernel source.
*/

#ifndef PWM_H
#define PWM_H

#define OMAP34XX_PADCONF_START  0x48002030
#define OMAP34XX_PADCONF_SIZE   0x05cc

#define GPT8_MUX_OFFSET_A               (0x4800217A - OMAP34XX_PADCONF_START) // Mode 2
#define GPT8_MUX_OFFSET_B               (0x480021DE - OMAP34XX_PADCONF_START) // Mode 1

#define GPT9_MUX_OFFSET_A               (0x48002174 - OMAP34XX_PADCONF_START) // Mode 2
#define GPT9_MUX_OFFSET_B               (0x480021D8 - OMAP34XX_PADCONF_START) // Mode 1

#define GPT10_MUX_OFFSET_A      	(0x48002176 - OMAP34XX_PADCONF_START) // Mode 2
#define GPT10_MUX_OFFSET_B      	(0x480021DA - OMAP34XX_PADCONF_START) // Mode 1

#define GPT11_MUX_OFFSET_A      	(0x48002178 - OMAP34XX_PADCONF_START) // Mode 2
#define GPT11_MUX_OFFSET_B      	(0x480021DC - OMAP34XX_PADCONF_START) // Mode 1


#define PWM_ENABLE_MUX_A		0x0002	/* IDIS | PTD | DIS | M2 */
#define PWM_ENABLE_MUX_B                0x0001  /* IDIS | PTD | DIS | M1 */

#define MUX_SET_INDEX_LEN	1
#define MUX_SET_MIN		'A'
#define MUX_SET_MAX		'B'
#define MUX_SET_DEFAULT		0 // Set A
#define MAX_MUX_OFFSETS		2


#endif /* ifndef PWM_H */

