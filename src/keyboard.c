/*
 * keyboard.c — PS/2 Keyboard Driver for doomOS
 *
 * Reads Scancode Set 1 from the PS/2 data port (0x60) and translates
 * them into Doom's internal key codes.  Polling-based (no IRQ handler).
 */

#include "keyboard.h"

/* ------------------------------------------------------------------ */
/* Low-level I/O helpers (inline x86_64 port access)                  */
/* ------------------------------------------------------------------ */

static inline uint8_t inb(uint16_t port)
{
    uint8_t val;
    __asm__ volatile ("inb %1, %0" : "=a"(val) : "Nd"(port));
    return val;
}

static inline void outb(uint16_t port, uint8_t val)
{
    __asm__ volatile ("outb %0, %1" : : "a"(val), "Nd"(port));
}

/* ------------------------------------------------------------------ */
/* Scancode-to-Doom-key translation table                             */
/* ------------------------------------------------------------------ */

/*
 * Convert a Scancode Set 1 make code (0x00–0x7F) to a Doom key code.
 * Returns 0 for unmapped scancodes.
 */
static int scancode_to_doomkey(uint8_t sc)
{
    switch (sc) {
    case SC_ESCAPE:    return DOOMKEY_ESCAPE;
    case SC_ENTER:     return DOOMKEY_ENTER;
    case SC_TAB:       return DOOMKEY_TAB;
    case SC_BACKSPACE: return DOOMKEY_BACKSPACE;

    /* Arrow keys */
    case SC_UP:        return DOOMKEY_UPARROW;
    case SC_DOWN:      return DOOMKEY_DOWNARROW;
    case SC_LEFT:      return DOOMKEY_LEFTARROW;
    case SC_RIGHT:     return DOOMKEY_RIGHTARROW;

    /* Modifiers */
    case SC_LCTRL:     return DOOMKEY_FIRE;
    case SC_LSHIFT:    return DOOMKEY_RSHIFT;
    case SC_RSHIFT:    return DOOMKEY_RSHIFT;
    case SC_LALT:      return DOOMKEY_LALT;
    case SC_SPACE:     return DOOMKEY_SPACE;

    /* Letter keys — Doom expects lowercase ASCII */
    case SC_A: return 'a';  case SC_B: return 'b';
    case SC_C: return 'c';  case SC_D: return 'd';
    case SC_E: return 'e';  case SC_F: return 'f';
    case SC_G: return 'g';  case SC_H: return 'h';
    case SC_I: return 'i';  case SC_J: return 'j';
    case SC_K: return 'k';  case SC_L: return 'l';
    case SC_M: return 'm';  case SC_N: return 'n';
    case SC_O: return 'o';  case SC_P: return 'p';
    case SC_Q: return 'q';  case SC_R: return 'r';
    case SC_S: return 's';  case SC_T: return 't';
    case SC_U: return 'u';  case SC_V: return 'v';
    case SC_W: return 'w';  case SC_X: return 'x';
    case SC_Y: return 'y';  case SC_Z: return 'z';

    /* Number keys */
    case SC_1: return '1';  case SC_2: return '2';
    case SC_3: return '3';  case SC_4: return '4';
    case SC_5: return '5';  case SC_6: return '6';
    case SC_7: return '7';  case SC_8: return '8';
    case SC_9: return '9';  case SC_0: return '0';
    case SC_MINUS:  return '-';
    case SC_EQUALS: return '=';

    default: return 0;
    }
}

/* ------------------------------------------------------------------ */
/* Public API                                                         */
/* ------------------------------------------------------------------ */

void keyboard_init(void)
{
    /* Flush any stale bytes sitting in the PS/2 output buffer */
    while (inb(KB_STATUS_PORT) & 0x01) {
        (void)inb(KB_DATA_PORT);
    }
}

int keyboard_poll(int *doom_key, int *pressed)
{
    /* Check if the controller has data ready (bit 0 of status port) */
    if (!(inb(KB_STATUS_PORT) & 0x01)) {
        return 0;
    }

    uint8_t scancode = inb(KB_DATA_PORT);

    /* Bit 7 set = key release, clear = key press */
    int is_release = scancode & 0x80;
    uint8_t make_code = scancode & 0x7F;

    int key = scancode_to_doomkey(make_code);
    if (key == 0) {
        return 0;  /* Unmapped scancode — ignore */
    }

    *doom_key = key;
    *pressed  = !is_release;
    return 1;
}
