/* SPDX-License-Identifier: GPL-3.0-only */
/* FM-1 (AC79) clock controller, read only: the system clock's source and PLL1's two control words (addresses
 * from felucca-bsp's chip/fm1_clock.h, MPL-2.0; its notes: M-VAVE's bootloader leaves source 6, PLL1 x 2/3 =
 * 360 MHz, PLL1 0x0940022B / 0x0750310C; m-bootloader 1.2 leaves 320 MHz). Nothing here writes. */
#pragma once
#include <stdint.h>

#define FM1_CLK_SYS_SRC (*(volatile uint32_t *)0x10014u)
#define FM1_CLK_PLL1_0  (*(volatile uint32_t *)0x119A8u)
#define FM1_CLK_PLL1_1  (*(volatile uint32_t *)0x119ACu)

static inline void fm1_clock_regs(uint32_t r[3])
{
    r[0] = FM1_CLK_SYS_SRC;
    r[1] = FM1_CLK_PLL1_0;
    r[2] = FM1_CLK_PLL1_1;
}
