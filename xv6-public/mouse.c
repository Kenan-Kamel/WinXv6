// PS/2 Mouse driver

#include "types.h"
#include "defs.h"
#include "x86.h"
#include "traps.h"
#include "spinlock.h"
#include "vga.h"
#include "mouse.h"

static struct {
  struct spinlock lock;
  int x, y;
  int buttons;
  int cycle;
  uchar bytes[3];
} mouse;

static void
mouse_wait_write(void)
{
  int timeout = 100000;
  while(timeout--){
    if(!(inb(MOUSE_STATUS) & MOUSE_ABIT))
      return;
  }
}

static void
mouse_wait_read(void)
{
  int timeout = 100000;
  while(timeout--){
    if(inb(MOUSE_STATUS) & MOUSE_BBIT)
      return;
  }
}

static void
mouse_write(uchar data)
{
  mouse_wait_write();
  outb(MOUSE_CMD, MOUSE_WRITE);
  mouse_wait_write();
  outb(MOUSE_PORT, data);
}

static uchar
mouse_read(void)
{
  mouse_wait_read();
  return inb(MOUSE_PORT);
}

void
mouseinit(void)
{
  uchar status;

  initlock(&mouse.lock, "mouse");

  mouse.x = SCREEN_WIDTH / 2;
  mouse.y = SCREEN_HEIGHT / 2;
  mouse.buttons = 0;
  mouse.cycle = 0;

  // Enable auxiliary mouse device
  mouse_wait_write();
  outb(MOUSE_CMD, 0xA8);

  // Enable interrupts
  mouse_wait_write();
  outb(MOUSE_CMD, 0x20);
  mouse_wait_read();
  status = inb(MOUSE_PORT);
  status |= 0x02;   // Enable IRQ12
  status &= ~0x20;  // Enable mouse clock
  mouse_wait_write();
  outb(MOUSE_CMD, 0x60);
  mouse_wait_write();
  outb(MOUSE_PORT, status);

  // Set defaults
  mouse_write(0xF6);
  mouse_read(); // ACK

  // Set maximum sample rate (200 samples/sec)
  mouse_write(0xF3);
  mouse_read(); // ACK
  mouse_write(200);
  mouse_read(); // ACK

  // Enable data reporting
  mouse_write(0xF4);
  mouse_read(); // ACK

  // Enable IRQ12 in IOAPIC
  ioapicenable(IRQ_MOUSE, 0);

  cprintf("mouse: PS/2 mouse initialized\n");
}

void
mouseintr(void)
{
  uchar data;
  int dx, dy;

  data = inb(MOUSE_PORT);

  acquire(&mouse.lock);

  switch(mouse.cycle){
  case 0:
    // First byte: status
    if(data & MOUSE_V_BIT){
      // Valid first byte (bit 3 always set)
      mouse.bytes[0] = data;
      mouse.cycle = 1;
    }
    break;

  case 1:
    // Second byte: X movement
    mouse.bytes[1] = data;
    mouse.cycle = 2;
    break;

  case 2:
    // Third byte: Y movement
    mouse.bytes[2] = data;
    mouse.cycle = 0;

    // Process complete packet
    mouse.buttons = mouse.bytes[0] & 0x07;

    // X movement (signed)
    dx = (int)mouse.bytes[1];
    if(mouse.bytes[0] & 0x10)
      dx |= 0xFFFFFF00; // sign extend

    // Y movement (signed, inverted for screen coords)
    dy = (int)mouse.bytes[2];
    if(mouse.bytes[0] & 0x20)
      dy |= 0xFFFFFF00; // sign extend

    // Check for overflow
    if(!(mouse.bytes[0] & 0x40) && !(mouse.bytes[0] & 0x80)){
      mouse.x += dx * 3;
      mouse.y -= dy * 3; // Y is inverted, 3x sensitivity

      // Clamp to screen bounds
      if(mouse.x < 0) mouse.x = 0;
      if(mouse.y < 0) mouse.y = 0;
      if(mouse.x >= SCREEN_WIDTH) mouse.x = SCREEN_WIDTH - 1;
      if(mouse.y >= SCREEN_HEIGHT) mouse.y = SCREEN_HEIGHT - 1;
    }
    break;
  }

  release(&mouse.lock);
}

void
mouse_getstate(int *x, int *y, int *buttons)
{
  acquire(&mouse.lock);
  *x = mouse.x;
  *y = mouse.y;
  *buttons = mouse.buttons;
  release(&mouse.lock);
}
