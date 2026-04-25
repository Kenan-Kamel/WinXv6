#pragma once
#include "types.h"

// E1000 MMIO register offsets
#define E1000_CTRL   0x0000  // Device Control
#define E1000_STATUS 0x0008  // Device Status
#define E1000_ICR    0x00C0  // Interrupt Cause Read (clears on read)
#define E1000_IMS    0x00D0  // Interrupt Mask Set
#define E1000_IMC    0x00D8  // Interrupt Mask Clear
#define E1000_RCTL   0x0100  // Receive Control
#define E1000_TCTL   0x0400  // Transmit Control
#define E1000_TIPG   0x0410  // Transmit IPG
#define E1000_RDBAL  0x2800  // RX Descriptor Base Low
#define E1000_RDBAH  0x2804  // RX Descriptor Base High
#define E1000_RDLEN  0x2808  // RX Descriptor Ring Length
#define E1000_RDH    0x2810  // RX Descriptor Head
#define E1000_RDT    0x2818  // RX Descriptor Tail
#define E1000_TDBAL  0x3800  // TX Descriptor Base Low
#define E1000_TDBAH  0x3804  // TX Descriptor Base High
#define E1000_TDLEN  0x3808  // TX Descriptor Ring Length
#define E1000_TDH    0x3810  // TX Descriptor Head
#define E1000_TDT    0x3818  // TX Descriptor Tail
#define E1000_MTA    0x5200  // Multicast Table Array (128 x 4 bytes)
#define E1000_RAL    0x5400  // Receive Address Low
#define E1000_RAH    0x5404  // Receive Address High

// CTRL bits
#define E1000_CTRL_FD    (1 << 0)   // Full Duplex
#define E1000_CTRL_ASDE  (1 << 5)   // Auto-Speed Detection Enable
#define E1000_CTRL_SLU   (1 << 6)   // Set Link Up
#define E1000_CTRL_RST   (1 << 26)  // Reset

// RCTL bits
#define E1000_RCTL_EN    (1 << 1)   // Receiver Enable
#define E1000_RCTL_BAM   (1 << 15)  // Broadcast Accept Mode
#define E1000_RCTL_SECRC (1 << 26)  // Strip Ethernet CRC

// TCTL bits
#define E1000_TCTL_EN    (1 << 1)   // Transmit Enable
#define E1000_TCTL_PSP   (1 << 3)   // Pad Short Packets
#define E1000_TCTL_CT    (0x10 << 4)  // Collision Threshold
#define E1000_TCTL_COLD  (0x40 << 12) // Collision Distance

// TX descriptor CMD bits
#define E1000_TXD_CMD_EOP  0x01  // End of Packet
#define E1000_TXD_CMD_IFCS 0x02  // Insert FCS (CRC)
#define E1000_TXD_CMD_RS   0x08  // Report Status (sets DD when done)

// TX/RX descriptor status bits
#define E1000_TXD_STAT_DD  0x01  // Descriptor Done
#define E1000_RXD_STAT_DD  0x01  // Descriptor Done
#define E1000_RXD_STAT_EOP 0x02  // End of Packet

// Interrupt bits (ICR/IMS)
#define E1000_ICR_TXDW   (1 << 0)  // TX descriptor written back
#define E1000_ICR_RXT0   (1 << 7)  // RX timer (packet received)
#define E1000_ICR_RXO    (1 << 6)  // RX overrun

// RAH bits
#define E1000_RAH_AV     (1u << 31) // Address Valid

#define E1000_TX_RING  8
#define E1000_RX_RING  8
#define E1000_PKT_SIZE 2048

// TX descriptor (legacy format)
struct e1000_tx_desc {
  uint64 addr;
  ushort length;
  uchar  cso;
  uchar  cmd;
  uchar  status;
  uchar  css;
  ushort special;
} __attribute__((packed));

// RX descriptor (legacy format)
struct e1000_rx_desc {
  uint64 addr;
  ushort length;
  ushort checksum;
  uchar  status;
  uchar  errors;
  ushort special;
} __attribute__((packed));

void  e1000init(void);
int   e1000tx(void *data, int len);
void  e1000intr(void);
void  e1000poll(void);       // poll RX ring (called from timer ISR as fallback)
extern uchar e1000_mac[6];
extern uchar e1000_irq;      // actual IRQ line read from PCI config space
