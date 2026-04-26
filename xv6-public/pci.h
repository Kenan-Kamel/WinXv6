#pragma once
#include "types.h"

// PCI config space I/O ports
#define PCI_CONF_ADDR  0xCF8
#define PCI_CONF_DATA  0xCFC

// PCI config space offsets
#define PCI_VENDOR_ID   0x00
#define PCI_DEVICE_ID   0x02
#define PCI_COMMAND     0x04
#define PCI_STATUS      0x06
#define PCI_BAR0        0x10
#define PCI_INT_LINE    0x3C
#define PCI_INT_PIN     0x3D

// PCI command register bits
#define PCI_CMD_IO      0x0001  // I/O space enable
#define PCI_CMD_MEM     0x0002  // Memory space enable
#define PCI_CMD_MASTER  0x0004  // Bus master enable

// Known device IDs
#define PCI_VENDOR_INTEL  0x8086
#define PCI_DEV_E1000     0x100E  // Intel 82540EM (QEMU default e1000)

struct pci_dev {
  int bus, slot, func;
  ushort vendor, device;
  uint32 bar0;        // physical base address (MMIO)
  uchar irq_line;
};

int  pci_find(ushort vendor, ushort device, struct pci_dev *out);
void pci_enable_master(struct pci_dev *dev);
