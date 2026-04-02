// VGA/VBE graphics driver using Bochs Graphics Adapter (BGA)
// Programs VBE registers via I/O ports to set up 1024x768x32bpp framebuffer

#include "types.h"
#include "defs.h"
#include "x86.h"
#include "memlayout.h"
#include "vga.h"

static uint *framebuffer = 0;
static addr_t fb_phys_addr = 0;
int gui_active = 0;

static void
vbe_write(ushort index, ushort value)
{
  outw(VBE_DISPI_IOPORT_INDEX, index);
  outw(VBE_DISPI_IOPORT_DATA, value);
}

static ushort
vbe_read(ushort index)
{
  outw(VBE_DISPI_IOPORT_INDEX, index);
  return inw(VBE_DISPI_IOPORT_DATA);
}

// Read PCI config space
static uint
pci_read32(int bus, int dev, int func, int reg)
{
  uint addr = 0x80000000 | (bus << 16) | (dev << 11) | (func << 8) | (reg & 0xFC);
  outl(PCI_CONFIG_ADDR, addr);
  return inl(PCI_CONFIG_DATA);
}

// Find VGA framebuffer physical address via PCI
static addr_t
find_fb_addr(void)
{
  // Try standard VGA at PCI 00:02.0
  uint bar0 = pci_read32(0, 2, 0, 0x10);
  if(bar0 != 0 && bar0 != 0xFFFFFFFF && (bar0 & 1) == 0){
    addr_t addr = bar0 & 0xFFFFFFF0;
    if(addr >= 0xC0000000 && addr < 0xFFFFFFFF)
      return addr;
  }

  // Try PCI 00:01.0
  bar0 = pci_read32(0, 1, 0, 0x10);
  if(bar0 != 0 && bar0 != 0xFFFFFFFF && (bar0 & 1) == 0){
    addr_t addr = bar0 & 0xFFFFFFF0;
    if(addr >= 0xC0000000 && addr < 0xFFFFFFFF)
      return addr;
  }

  return FB_PHYS_DEFAULT;
}

int
vga_init(void)
{
  ushort id;

  // Check for Bochs VGA
  id = vbe_read(VBE_DISPI_INDEX_ID);
  if(id < 0xB0C0 || id > 0xB0CF){
    cprintf("vga: Bochs VGA not found (id=0x%x)\n", id);
    return -1;
  }

  cprintf("vga: Bochs VGA detected (id=0x%x)\n", id);

  // Find framebuffer physical address
  fb_phys_addr = find_fb_addr();
  cprintf("vga: framebuffer at physical 0x%p\n", fb_phys_addr);

  // Set up VBE mode: 1024x768x32bpp with LFB
  vbe_write(VBE_DISPI_INDEX_ENABLE, VBE_DISPI_DISABLED);
  vbe_write(VBE_DISPI_INDEX_XRES, SCREEN_WIDTH);
  vbe_write(VBE_DISPI_INDEX_YRES, SCREEN_HEIGHT);
  vbe_write(VBE_DISPI_INDEX_BPP, SCREEN_BPP);
  vbe_write(VBE_DISPI_INDEX_VIRT_W, SCREEN_WIDTH);
  vbe_write(VBE_DISPI_INDEX_VIRT_H, SCREEN_HEIGHT);
  vbe_write(VBE_DISPI_INDEX_X_OFF, 0);
  vbe_write(VBE_DISPI_INDEX_Y_OFF, 0);
  vbe_write(VBE_DISPI_INDEX_ENABLE, VBE_DISPI_ENABLED | VBE_DISPI_LFB_ENABLED);

  // Map framebuffer via kernel's direct mapping of 4th GB
  framebuffer = (uint*)P2V(fb_phys_addr);

  // Clear screen to dark blue
  uint color = 0xFF1a1a2e;
  for(int i = 0; i < SCREEN_WIDTH * SCREEN_HEIGHT; i++)
    framebuffer[i] = color;

  gui_active = 1;
  cprintf("vga: mode set to %dx%dx%d\n", SCREEN_WIDTH, SCREEN_HEIGHT, SCREEN_BPP);

  return 0;
}

uint*
vga_get_fb(void)
{
  return framebuffer;
}

// Copy user back buffer to hardware framebuffer
void
vga_flush(uint *src, int size)
{
  if(framebuffer && src){
    memmove(framebuffer, src, size);
  }
}

int
vga_is_active(void)
{
  return gui_active;
}
