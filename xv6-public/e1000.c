#include "types.h"
#include "defs.h"
#include "memlayout.h"
#include "pci.h"
#include "e1000.h"
#include "net.h"

// MAC address read from hardware
uchar e1000_mac[6];
// IRQ line read from PCI config space (set in e1000init)
uchar e1000_irq;

// MMIO base (kernel virtual address)
static volatile uint32 *regs;

// TX ring
static struct e1000_tx_desc tx_ring[E1000_TX_RING] __attribute__((aligned(16)));
static uchar tx_bufs[E1000_TX_RING][E1000_PKT_SIZE];
static int tx_tail;

// RX ring
static struct e1000_rx_desc rx_ring[E1000_RX_RING] __attribute__((aligned(16)));
static uchar rx_bufs[E1000_RX_RING][E1000_PKT_SIZE];
static int rx_tail;

static inline uint32
e1000r(int reg)
{
  return regs[reg / 4];
}

static inline void
e1000w(int reg, uint32 val)
{
  regs[reg / 4] = val;
}

void
e1000init(void)
{
  struct pci_dev dev;

  if (pci_find(PCI_VENDOR_INTEL, PCI_DEV_E1000, &dev) < 0) {
    cprintf("e1000: not found\n");
    return;
  }
  cprintf("e1000: found at bus=%d slot=%d irq=%d bar0=0x%x\n",
          dev.bus, dev.slot, dev.irq_line, dev.bar0);

  pci_enable_master(&dev);

  // Map BAR0 into kernel virtual address space.
  // kvmalloc() maps the 4th physical GB (0xC0000000-0xFFFFFFFF) at
  // KERNBASE+3GB, so P2V works for any device in that range.
  regs = (volatile uint32 *) p2v(dev.bar0);

  // Reset device
  e1000w(E1000_CTRL, e1000r(E1000_CTRL) | E1000_CTRL_RST);
  // Small delay to let reset complete
  for (volatile int i = 0; i < 100000; i++);

  // Re-read status, then set link up + full duplex
  e1000w(E1000_CTRL, E1000_CTRL_SLU | E1000_CTRL_ASDE | E1000_CTRL_FD);

  // Read MAC from Receive Address registers
  uint32 ral = e1000r(E1000_RAL);
  uint32 rah = e1000r(E1000_RAH);
  e1000_mac[0] = (ral >>  0) & 0xFF;
  e1000_mac[1] = (ral >>  8) & 0xFF;
  e1000_mac[2] = (ral >> 16) & 0xFF;
  e1000_mac[3] = (ral >> 24) & 0xFF;
  e1000_mac[4] = (rah >>  0) & 0xFF;
  e1000_mac[5] = (rah >>  8) & 0xFF;
  cprintf("e1000: MAC %x:%x:%x:%x:%x:%x\n",
          e1000_mac[0], e1000_mac[1], e1000_mac[2],
          e1000_mac[3], e1000_mac[4], e1000_mac[5]);

  // Clear multicast table
  for (int i = 0; i < 128; i++)
    e1000w(E1000_MTA + i * 4, 0);

  // ---- TX setup ----
  for (int i = 0; i < E1000_TX_RING; i++) {
    tx_ring[i].addr   = V2P(&tx_bufs[i]);
    tx_ring[i].status = E1000_TXD_STAT_DD;  // mark all as done initially
  }
  tx_tail = 0;

  e1000w(E1000_TDBAL, (uint32) V2P(tx_ring));
  e1000w(E1000_TDBAH, 0);
  e1000w(E1000_TDLEN, E1000_TX_RING * sizeof(struct e1000_tx_desc));
  e1000w(E1000_TDH, 0);
  e1000w(E1000_TDT, 0);
  e1000w(E1000_TCTL,
         E1000_TCTL_EN | E1000_TCTL_PSP | E1000_TCTL_CT | E1000_TCTL_COLD);
  // TIPG for 82540EM copper: IPGT=10, IPGR1=8, IPGR2=6
  e1000w(E1000_TIPG, (6 << 20) | (8 << 10) | 10);

  // ---- RX setup ----
  for (int i = 0; i < E1000_RX_RING; i++) {
    rx_ring[i].addr   = V2P(&rx_bufs[i]);
    rx_ring[i].status = 0;
  }
  rx_tail = E1000_RX_RING - 1;

  // Program MAC into receive address register 0
  e1000w(E1000_RAL, ral);
  e1000w(E1000_RAH, rah | E1000_RAH_AV);

  e1000w(E1000_RDBAL, (uint32) V2P(rx_ring));
  e1000w(E1000_RDBAH, 0);
  e1000w(E1000_RDLEN, E1000_RX_RING * sizeof(struct e1000_rx_desc));
  e1000w(E1000_RDH, 0);
  e1000w(E1000_RDT, rx_tail);
  e1000w(E1000_RCTL,
         E1000_RCTL_EN | E1000_RCTL_BAM | E1000_RCTL_SECRC);

  // Enable RX interrupts
  e1000w(E1000_IMS, E1000_ICR_RXT0 | E1000_ICR_RXO);

  // Save and enable the e1000's actual IRQ line
  e1000_irq = dev.irq_line;
  cprintf("e1000: irq=%d\n", e1000_irq);
  ioapicenable(e1000_irq, 0);

  // Print link status so we know whether the NIC came up
  uint32 status = e1000r(E1000_STATUS);
  cprintf("e1000: STATUS=0x%x link=%s\n", status,
          (status & 2) ? "UP" : "DOWN");
  cprintf("e1000: RDBAL=0x%x TDBAL=0x%x\n",
          e1000r(E1000_RDBAL), e1000r(E1000_TDBAL));
  cprintf("e1000: init done\n");
}

// Send a packet. Called with interrupts possibly off (from net stack).
// Returns 0 on success, -1 if ring is full.
int
e1000tx(void *data, int len)
{
  if (len > E1000_PKT_SIZE)
    return -1;

  // Check if current tail descriptor is free
  uchar dd = tx_ring[tx_tail].status & E1000_TXD_STAT_DD;
  if (!dd)
    return -1;  // ring full

  memmove(tx_bufs[tx_tail], data, len);

  tx_ring[tx_tail].length = (ushort) len;
  tx_ring[tx_tail].cmd    = E1000_TXD_CMD_EOP | E1000_TXD_CMD_IFCS | E1000_TXD_CMD_RS;
  tx_ring[tx_tail].status = 0;
  tx_ring[tx_tail].cso    = 0;
  tx_ring[tx_tail].css    = 0;
  tx_ring[tx_tail].special = 0;

  tx_tail = (tx_tail + 1) % E1000_TX_RING;
  e1000w(E1000_TDT, tx_tail);
  return 0;
}

// Drain all ready RX descriptors.
// Called from both the interrupt handler and the timer-based poll path.
static void
e1000_drain_rx(void)
{
  for (;;) {
    int i = (rx_tail + 1) % E1000_RX_RING;
    if (!(rx_ring[i].status & E1000_RXD_STAT_DD))
      break;  // no more packets

    int len = rx_ring[i].length;
    if (len > 0 && (rx_ring[i].status & E1000_RXD_STAT_EOP))
      netin(rx_bufs[i], len);

    // Give descriptor back to hardware
    rx_ring[i].status = 0;
    rx_tail = i;
    e1000w(E1000_RDT, rx_tail);
  }
}

// Interrupt handler — called from trap.c when the e1000 IRQ fires
void
e1000intr(void)
{
  // Read and clear interrupt cause register
  uint32 cause = e1000r(E1000_ICR);
  if (cause & (E1000_ICR_RXT0 | E1000_ICR_RXO))
    e1000_drain_rx();
}

// Polling fallback — called from the timer ISR so packets are never
// lost even if the PCI IRQ routing is misconfigured.
void
e1000poll(void)
{
  if (!regs)
    return;
  e1000_drain_rx();
}
