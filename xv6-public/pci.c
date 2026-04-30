#include "types.h"
#include "x86.h"
#include "pci.h"

static uint32
pci_read32(int bus, int slot, int func, int off)
{
  uint32 addr = (1u << 31) | (bus << 16) | (slot << 11) | (func << 8) | (off & 0xFC);
  outl(PCI_CONF_ADDR, addr);
  return inl(PCI_CONF_DATA);
}

static ushort
pci_read16(int bus, int slot, int func, int off)
{
  uint32 v = pci_read32(bus, slot, func, off & ~2);
  return (v >> ((off & 2) * 8)) & 0xFFFF;
}

static uchar
pci_read8(int bus, int slot, int func, int off)
{
  uint32 v = pci_read32(bus, slot, func, off & ~3);
  return (v >> ((off & 3) * 8)) & 0xFF;
}

static void
pci_write16(int bus, int slot, int func, int off, ushort val)
{
  uint32 addr = (1u << 31) | (bus << 16) | (slot << 11) | (func << 8) | (off & 0xFC);
  uint32 cur = pci_read32(bus, slot, func, off & ~2);
  int shift = (off & 2) * 8;
  cur = (cur & ~(0xFFFF << shift)) | ((uint32)val << shift);
  outl(PCI_CONF_ADDR, addr);
  outl(PCI_CONF_DATA, cur);
}

// Scan all PCI buses/slots/funcs for a device matching vendor:device.
// Fills *out and returns 0 on success, -1 if not found.
int
pci_find(ushort vendor, ushort device, struct pci_dev *out)
{
  for (int bus = 0; bus < 8; bus++) {
    for (int slot = 0; slot < 32; slot++) {
      uint32 id = pci_read32(bus, slot, 0, PCI_VENDOR_ID);
      if ((id & 0xFFFF) == 0xFFFF)
        continue;  // no device
      for (int func = 0; func < 8; func++) {
        id = pci_read32(bus, slot, func, PCI_VENDOR_ID);
        ushort v = id & 0xFFFF;
        ushort d = id >> 16;
        if (v == vendor && d == device) {
          out->bus    = bus;
          out->slot   = slot;
          out->func   = func;
          out->vendor = v;
          out->device = d;
          // BAR0: strip type bits (lower 4 bits for MMIO)
          out->bar0    = pci_read32(bus, slot, func, PCI_BAR0) & ~0xF;
          out->irq_line = pci_read8(bus, slot, func, PCI_INT_LINE);
          return 0;
        }
      }
    }
  }
  return -1;
}

void
pci_enable_master(struct pci_dev *dev)
{
  ushort cmd = pci_read16(dev->bus, dev->slot, dev->func, PCI_COMMAND);
  cmd |= PCI_CMD_MEM | PCI_CMD_MASTER;
  pci_write16(dev->bus, dev->slot, dev->func, PCI_COMMAND, cmd);
}
