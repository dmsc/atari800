#ifndef UI_BASIC_H_
#define UI_BASIC_H_

#include "ui.h"

/* "Basic" UI driver. Always present and guaranteed to work. Override
   driver may call into this one to implement filtering */
extern UI_tDriver UI_BASIC_driver;

/* for Windows CE and Dreamcast */
#ifdef USE_UI_BASIC_ONSCREEN_KEYBOARD
extern int UI_BASIC_in_kbui;
extern int UI_BASIC_OnScreenKeyboard(const char *title, int layout);
/* layout must be one of MACHINE_* values (see atari.h)
   or -1 for base keyboard with no function keys */
#endif

/* used only in the Dreamcast port */
extern const unsigned char UI_BASIC_key_to_ascii[256];

#endif /* UI_BASIC_H_ */
