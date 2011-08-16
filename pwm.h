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
  

 Register definitions used by the pwm driver. 
 Some for the PADCONF pin muxing, the rest for the PWM timer control.
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


#define GPTIMER8		0x4903E000
#define GPTIMER9		0x49040000
#define GPTIMER10 		0x48086000
#define GPTIMER11		0x48088000

#define GPT_REGS_PAGE_SIZE      4096

#define PWM8_CTL_BASE		GPTIMER8
#define PWM9_CTL_BASE		GPTIMER9
#define PWM10_CTL_BASE		GPTIMER10
#define PWM11_CTL_BASE		GPTIMER11


/* GPT register offsets */
#define GPT_TIOCP_CFG 0x010
#define GPT_TISTAT    0x014
#define GPT_TISR      0x018
#define GPT_TIER      0x01C
#define GPT_TWER      0x020
#define GPT_TCLR      0x024
#define GPT_TCRR      0x028
#define GPT_TLDR      0x02C
#define GPT_TTGR      0x030
#define GPT_TWPS      0x034
#define GPT_TMAR      0x038
#define GPT_TCAR1     0x03C
#define GPT_TSICR     0x040
#define GPT_TCAR2     0x044
#define GPT_TPIR      0x048
#define GPT_TNIR      0x04C
#define GPT_TCVR      0x050
#define GPT_TOCR      0x054
#define GPT_TOWR      0x058   

/* TCLR bits for PWM */
#define GPT_TCLR_ST     	(1 << 0)	/* stop/start */
#define GPT_TCLR_AR     	(1 << 1)	/* one shot/auto-reload */
#define GPT_TCLR_PTV_MASK    	(7 << 2)	/* prescaler value 2^(PTV + 1) */
#define GPT_TCLR_PRE    	(1 << 5)	/* disable/enable prescaler */
#define GPT_TCLR_CE     	(1 << 6)	/* disable/enable compare */
#define GPT_TCLR_SCPWM  	(1 << 7)	/* PWM value when off */
#define GPT_TCLR_TCM_MASK    	(3 << 8)	/* transition capture mode */

#define GPT_TCLR_TRG_MASK 	(3 << 10)	/* trigger output mode */
#define GPT_TCLR_TRG_OVFL	(1 << 10)	/* trigger on overflow */
#define GPT_TCLR_TRG_OVFL_MATCH	(2 << 10)	/* trigger on overflow and match */	

#define GPT_TCLR_PT     	(1 << 12)	/* pulse/toggle modulation */
#define GPT_TCLR_CAPT_MODE      (1 << 13)	/* capture mode config */
#define GPT_TCLR_GPO_CFG        (1 << 14)	/* pwm or capture mode */


#endif /* ifndef PWM_H */

