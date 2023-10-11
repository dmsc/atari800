#ifndef A8PICO_H_
#define A8OPICO_H_

#include "atari.h"
#include <stdio.h>

int A8Pico_Initialise(int *argc, char *argv[]);
void A8Pico_InsertCart(void);
void A8Pico_Exit(void);
int A8Pico_D5GetByte(UWORD addr, int no_side_effects);
void A8Pico_D5PutByte(UWORD addr, UBYTE byte);
extern int A8Pico_enabled;

#endif /* A8PICO_H_ */

