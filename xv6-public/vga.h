// VGA/VBE (Bochs Graphics Adapter) driver header
#pragma once

#define VBE_DISPI_IOPORT_INDEX  0x01CE
#define VBE_DISPI_IOPORT_DATA   0x01CF

#define VBE_DISPI_INDEX_ID      0x0
#define VBE_DISPI_INDEX_XRES    0x1
#define VBE_DISPI_INDEX_YRES    0x2
#define VBE_DISPI_INDEX_BPP     0x3
#define VBE_DISPI_INDEX_ENABLE  0x4
#define VBE_DISPI_INDEX_BANK    0x5
#define VBE_DISPI_INDEX_VIRT_W  0x6
#define VBE_DISPI_INDEX_VIRT_H  0x7
#define VBE_DISPI_INDEX_X_OFF   0x8
#define VBE_DISPI_INDEX_Y_OFF   0x9

#define VBE_DISPI_DISABLED      0x00
#define VBE_DISPI_ENABLED       0x01
#define VBE_DISPI_LFB_ENABLED   0x40

#define SCREEN_WIDTH   1024
#define SCREEN_HEIGHT  768
#define SCREEN_BPP     32
#define SCREEN_PITCH   (SCREEN_WIDTH * 4)
#define FB_SIZE        (SCREEN_WIDTH * SCREEN_HEIGHT * 4)

// PCI configuration for finding VGA framebuffer
#define PCI_CONFIG_ADDR 0xCF8
#define PCI_CONFIG_DATA 0xCFC

// Fallback framebuffer address
#define FB_PHYS_DEFAULT 0xFD000000

// Shared structures between kernel and user space
struct screen_info {
  int width;
  int height;
  int bpp;
  int pitch;
};

struct mouse_info {
  int x;
  int y;
  int buttons;
};
