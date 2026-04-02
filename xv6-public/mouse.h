// PS/2 Mouse driver header
#pragma once

#define MOUSE_PORT      0x60
#define MOUSE_STATUS    0x64
#define MOUSE_CMD       0x64

#define MOUSE_ABIT      0x02
#define MOUSE_BBIT      0x01
#define MOUSE_WRITE     0xD4
#define MOUSE_F_BIT     0x20
#define MOUSE_V_BIT     0x08

#define IRQ_MOUSE       12

// Mouse button bits
#define MOUSE_LEFT      0x01
#define MOUSE_RIGHT     0x02
#define MOUSE_MIDDLE    0x04
