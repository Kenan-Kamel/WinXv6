// GUI-related system calls

#include "types.h"
#include "defs.h"
#include "param.h"
#include "memlayout.h"
#include "mmu.h"
#include "proc.h"
#include "x86.h"
#include "vga.h"
#include "spinlock.h"

// GUI keyboard buffer
#define GUI_KEYBUF_SIZE 256
static struct {
  struct spinlock lock;
  int buf[GUI_KEYBUF_SIZE];
  uint r;
  uint w;
} gui_keys;

void
gui_keys_init(void)
{
  initlock(&gui_keys.lock, "guikeys");
  gui_keys.r = 0;
  gui_keys.w = 0;
}

// Called from kbdintr when GUI is active
void
gui_key_put(int c)
{
  acquire(&gui_keys.lock);
  if(gui_keys.w - gui_keys.r < GUI_KEYBUF_SIZE){
    gui_keys.buf[gui_keys.w % GUI_KEYBUF_SIZE] = c;
    gui_keys.w++;
  }
  release(&gui_keys.lock);
}

static int
gui_key_get(void)
{
  int c = -1;
  acquire(&gui_keys.lock);
  if(gui_keys.r != gui_keys.w){
    c = gui_keys.buf[gui_keys.r % GUI_KEYBUF_SIZE];
    gui_keys.r++;
  }
  release(&gui_keys.lock);
  return c;
}

// SYS_screen_init: Initialize graphics mode
// arg0: pointer to struct screen_info (filled on return)
addr_t
sys_screen_init(void)
{
  char *ptr;
  struct screen_info *info;

  if(argptr(0, &ptr, sizeof(struct screen_info)) < 0)
    return -1;

  info = (struct screen_info*)ptr;

  if(vga_init() < 0)
    return -1;

  info->width = SCREEN_WIDTH;
  info->height = SCREEN_HEIGHT;
  info->bpp = SCREEN_BPP;
  info->pitch = SCREEN_PITCH;

  return 0;
}

// SYS_flush_screen: Copy user back buffer to hardware framebuffer
// arg0: pointer to user pixel buffer (uint[SCREEN_WIDTH * SCREEN_HEIGHT])
addr_t
sys_flush_screen(void)
{
  addr_t addr;
  argaddr(0, &addr);

  // Validate: buffer must be within process address space
  if(addr < PGSIZE || addr + FB_SIZE > proc->sz)
    return -1;

  vga_flush((uint*)addr, FB_SIZE);
  return 0;
}

// SYS_getmouse: Get current mouse state
// arg0: pointer to struct mouse_info (filled on return)
addr_t
sys_getmouse(void)
{
  char *ptr;
  struct mouse_info *info;

  if(argptr(0, &ptr, sizeof(struct mouse_info)) < 0)
    return -1;

  info = (struct mouse_info*)ptr;
  mouse_getstate(&info->x, &info->y, &info->buttons);
  return 0;
}

// SYS_getkey_async: Non-blocking keyboard read for GUI
// Returns key code or -1 if no key available
addr_t
sys_getkey_async(void)
{
  return gui_key_get();
}
