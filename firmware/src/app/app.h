/* SPDX-License-Identifier: GPL-3.0-only */
/* dubengine app: the entry points. Included by the unity build after gfx.c (drawing helpers in scope) and by the
 * host simulator. */
#pragma once
#include <stdint.h>
#include "plat.h"
#include "../dsp/amb.h"

#ifndef EN_BUILD
#define EN_BUILD "host"
#endif
#ifndef EN_VERSION                       /* a release build passes its own (tools/build.py --release) */
#define EN_VERSION "DEV"
#endif

void ui_init(void);                      /* the last save read, to the engine; the panel started */
void ui_frame(void);                     /* input, autosave, the screen, the lights */
void ui_input_only(void);
void ui_say(const char *a, const char *b);   /* a message on the screen a moment */
#define UI_SPLASH_N 26                   /* the launch screen's frames (~1.4 s at 55 ms) */
void ui_splash(int f);                   /* frame f of the launch screen (the last: as it stays) */
