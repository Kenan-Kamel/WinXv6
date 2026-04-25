#pragma once
#include "types.h"
#include "spinlock.h"

// ---- Byte-order helpers ----
static inline ushort htons(ushort x) { return __builtin_bswap16(x); }
static inline ushort ntohs(ushort x) { return __builtin_bswap16(x); }
static inline uint32 htonl(uint32 x) { return __builtin_bswap32(x); }
static inline uint32 ntohl(uint32 x) { return __builtin_bswap32(x); }

// ---- Ethernet ----
#define ETH_TYPE_ARP  0x0806
#define ETH_TYPE_IP   0x0800

struct eth_hdr {
  uchar  dst[6];
  uchar  src[6];
  ushort type;
} __attribute__((packed));

// ---- ARP ----
#define ARP_HWTYPE_ETH  1
#define ARP_PROTYPE_IP  0x0800
#define ARP_OP_REQUEST  1
#define ARP_OP_REPLY    2

struct arp_hdr {
  ushort hwtype;
  ushort protype;
  uchar  hwlen;
  uchar  prolen;
  ushort opcode;
  uchar  sender_mac[6];
  uint32 sender_ip;
  uchar  target_mac[6];
  uint32 target_ip;
} __attribute__((packed));

// ---- IP ----
#define IP_PROTO_ICMP  1
#define IP_PROTO_TCP   6

struct ip_hdr {
  uchar  vihl;       // version (4) + IHL (5 dwords = 20 bytes)
  uchar  tos;
  ushort total_len;  // including header
  ushort id;
  ushort frag_off;
  uchar  ttl;
  uchar  proto;
  ushort checksum;
  uint32 src;
  uint32 dst;
} __attribute__((packed));

// ---- ICMP ----
#define ICMP_ECHO_REQUEST 8
#define ICMP_ECHO_REPLY   0

struct icmp_hdr {
  uchar  type;
  uchar  code;
  ushort checksum;
  ushort id;
  ushort seq;
} __attribute__((packed));

// ---- TCP ----
#define TCP_FIN 0x01
#define TCP_SYN 0x02
#define TCP_RST 0x04
#define TCP_PSH 0x08
#define TCP_ACK 0x10

struct tcp_hdr {
  ushort sport;
  ushort dport;
  uint32 seq;
  uint32 ack_seq;
  uchar  data_off;   // upper 4 bits = header length in 32-bit words
  uchar  flags;
  ushort window;
  ushort checksum;
  ushort urgent;
} __attribute__((packed));

// ---- Socket ----
#define NSOCK         16
#define SOCK_BUFSIZE  4096
#define SOCK_BACKLOG  4

// TCP states
#define TCP_CLOSED      0
#define TCP_LISTEN      1
#define TCP_SYN_RCVD    2
#define TCP_ESTABLISHED 3
#define TCP_FIN_WAIT1   4
#define TCP_FIN_WAIT2   5
#define TCP_CLOSE_WAIT  6
#define TCP_LAST_ACK    7

struct sock {
  int    state;
  int    used;
  uint32 local_ip;
  ushort local_port;
  uint32 remote_ip;
  ushort remote_port;

  uint32 snd_nxt;   // next byte to send
  uint32 snd_una;   // oldest unacked byte
  uint32 rcv_nxt;   // next byte expected from remote

  struct spinlock lock;

  // Receive ring buffer
  char rxbuf[SOCK_BUFSIZE];
  int  rxhead;   // consumer index
  int  rxtail;   // producer index

  // Accept queue (for listening sockets)
  struct sock *accept_q[SOCK_BACKLOG];
  int accept_head;
  int accept_tail;

  struct sock *parent;   // listening socket (for accepted socks)
  int eof;               // FIN received, no more data coming
};

// ---- Net stack API ----
void    netinit(void);
void    netin(void *pkt, int len);              // called by e1000 driver
int     netsend(uint32 dst_ip, uchar proto, void *data, int len);

// Socket API (used by sysnet.c)
struct sock *sockalloc(void);
void         sockfree(struct sock *s);
int          sockbind(struct sock *s, ushort port);
int          sockaccept(struct sock *listener, struct sock **out);
int          sockread(struct sock *s, char *buf, int n);
int          sockwrite(struct sock *s, const char *buf, int n);
void         sockclose(struct sock *s);

// Config (set by netinit)
extern uint32 net_ip;     // our IP in network byte order
extern uint32 net_mask;   // subnet mask in network byte order
extern uint32 net_gw;     // gateway IP in network byte order
