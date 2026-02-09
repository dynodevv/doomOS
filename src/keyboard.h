/*
 * keyboard.h — PS/2 Scancode Set 1 definitions and Doom key mappings
 *
 * Maps raw scancodes from port 0x60 to Doom's internal DOOMKEY_* codes.
 * Only key-down events (top bit clear) are used; key-up events (top bit set)
 * are detected by masking with 0x80.
 */

#ifndef KEYBOARD_H
#define KEYBOARD_H

#include <stdint.h>

/* PS/2 controller I/O ports */
#define KB_DATA_PORT   0x60   /* Read scancode byte                     */
#define KB_STATUS_PORT 0x64   /* Bit 0 = output buffer full (data ready)*/

/* Scancode Set 1 — make codes (key-down) */
#define SC_ESCAPE      0x01
#define SC_1           0x02
#define SC_2           0x03
#define SC_3           0x04
#define SC_4           0x05
#define SC_5           0x06
#define SC_6           0x07
#define SC_7           0x08
#define SC_8           0x09
#define SC_9           0x0A
#define SC_0           0x0B
#define SC_MINUS       0x0C
#define SC_EQUALS      0x0D
#define SC_BACKSPACE   0x0E
#define SC_TAB         0x0F
#define SC_Q           0x10
#define SC_W           0x11
#define SC_E           0x12
#define SC_R           0x13
#define SC_T           0x14
#define SC_Y           0x15
#define SC_U           0x16
#define SC_I           0x17
#define SC_O           0x18
#define SC_P           0x19
#define SC_A           0x1E
#define SC_S           0x1F
#define SC_D           0x20
#define SC_F           0x21
#define SC_G           0x22
#define SC_H           0x23
#define SC_J           0x24
#define SC_K           0x25
#define SC_L           0x26
#define SC_ENTER       0x1C
#define SC_LCTRL       0x1D
#define SC_Z           0x2C
#define SC_X           0x2D
#define SC_C           0x2E
#define SC_V           0x2F
#define SC_SPACE       0x39
#define SC_LSHIFT      0x2A
#define SC_RSHIFT      0x36
#define SC_LALT        0x38
#define SC_UP          0x48
#define SC_DOWN        0x50
#define SC_LEFT        0x4B
#define SC_RIGHT       0x4D

/* Doom internal key codes (from doomkeys.h in doomgeneric) */
#define DOOMKEY_ESCAPE      27
#define DOOMKEY_ENTER       13
#define DOOMKEY_TAB         9
#define DOOMKEY_BACKSPACE   127
#define DOOMKEY_UPARROW     0xAD
#define DOOMKEY_DOWNARROW   0xAF
#define DOOMKEY_LEFTARROW   0xAC
#define DOOMKEY_RIGHTARROW  0xAE
#define DOOMKEY_FIRE        0x80 + 0x32   /* Ctrl = fire  */
#define DOOMKEY_USE         ' '           /* Space = use  */
#define DOOMKEY_RSHIFT      0x80 + 0x36
#define DOOMKEY_LALT        0x80 + 0x38
#define DOOMKEY_SPACE       ' '

/* ------------------------------------------------------------------ */
/* Public API                                                         */
/* ------------------------------------------------------------------ */

/* Initialise the PS/2 keyboard (flush buffer, etc.) */
void keyboard_init(void);

/*
 * Poll the keyboard controller.
 * Returns 1 if a key event occurred and fills *doom_key and *pressed.
 * Returns 0 if no data is available.
 */
int keyboard_poll(int *doom_key, int *pressed);

#endif /* KEYBOARD_H */
