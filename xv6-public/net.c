#include "types.h"
#include "defs.h"
#include "param.h"
#include "spinlock.h"
#include "proc.h"
#include "e1000.h"
#include "net.h"

// ---- Network configuration (QEMU user-mode defaults) ----
// 10.0.2.15  stored as big-endian (network byte order)
uint32 net_ip;    // set in netinit
uint32 net_mask;
uint32 net_gw;

// ---- ARP cache ----
#define ARP_CACHE_SIZE 8
static struct {
  uint32 ip;
  uchar  mac[6];
} arp_cache[ARP_CACHE_SIZE];
static int arp_cache_n;

static struct spinlock netlock;

// ---- Socket table ----
static struct sock socks[NSOCK];

// ---- Checksum ----
static uint32
cksum_add(uint32 sum, const void *data, int len)
{
  const uchar *p = data;
  while (len >= 2) {
    sum += ((uint32)p[0] << 8) | p[1];
    p += 2;
    len -= 2;
  }
  if (len)
    sum += (uint32)p[0] << 8;
  return sum;
}

static ushort
cksum_finish(uint32 sum)
{
  while (sum >> 16)
    sum = (sum & 0xFFFF) + (sum >> 16);
  return ~(ushort)sum;
}

static ushort
ip_cksum(struct ip_hdr *ip)
{
  // htons: checksum algorithm produces a network-byte-order result
  return htons(cksum_finish(cksum_add(0, ip, sizeof(struct ip_hdr))));
}

static ushort
tcp_cksum(struct ip_hdr *ip, struct tcp_hdr *tcp, int tcp_total_len)
{
  // Pseudo-header: src, dst, zero, proto, tcp_len
  uint32 sum = 0;
  sum = cksum_add(sum, &ip->src, 4);
  sum = cksum_add(sum, &ip->dst, 4);
  uchar pseudo[4] = { 0, IP_PROTO_TCP,
                      (tcp_total_len >> 8) & 0xFF,
                      tcp_total_len & 0xFF };
  sum = cksum_add(sum, pseudo, 4);
  sum = cksum_add(sum, tcp, tcp_total_len);
  return cksum_finish(sum);
}

// ---- ARP ----
static void
arp_cache_update(uint32 ip, uchar *mac)
{
  // Check if already there
  for (int i = 0; i < arp_cache_n; i++) {
    if (arp_cache[i].ip == ip) {
      memmove(arp_cache[i].mac, mac, 6);
      return;
    }
  }
  if (arp_cache_n < ARP_CACHE_SIZE) {
    arp_cache[arp_cache_n].ip = ip;
    memmove(arp_cache[arp_cache_n].mac, mac, 6);
    arp_cache_n++;
  }
}

static int
arp_lookup(uint32 ip, uchar *mac_out)
{
  for (int i = 0; i < arp_cache_n; i++) {
    if (arp_cache[i].ip == ip) {
      memmove(mac_out, arp_cache[i].mac, 6);
      return 0;
    }
  }
  return -1;
}

static uchar broadcast_mac[6] = { 0xFF,0xFF,0xFF,0xFF,0xFF,0xFF };

// Send a raw Ethernet frame
static int
eth_send(uchar *dst_mac, ushort type, void *payload, int plen)
{
  static uchar frame[1518];
  if (plen + (int)sizeof(struct eth_hdr) > (int)sizeof(frame))
    return -1;
  struct eth_hdr *eth = (struct eth_hdr *)frame;
  memmove(eth->dst, dst_mac, 6);
  memmove(eth->src, e1000_mac, 6);
  eth->type = htons(type);
  memmove(frame + sizeof(struct eth_hdr), payload, plen);
  return e1000tx(frame, sizeof(struct eth_hdr) + plen);
}

static void
arp_reply(struct arp_hdr *req)
{
  struct arp_hdr rep;
  rep.hwtype   = htons(ARP_HWTYPE_ETH);
  rep.protype  = htons(ARP_PROTYPE_IP);
  rep.hwlen    = 6;
  rep.prolen   = 4;
  rep.opcode   = htons(ARP_OP_REPLY);
  memmove(rep.sender_mac, e1000_mac, 6);
  rep.sender_ip = net_ip;
  memmove(rep.target_mac, req->sender_mac, 6);
  rep.target_ip = req->sender_ip;
  eth_send(req->sender_mac, ETH_TYPE_ARP, &rep, sizeof(rep));
}

static void
arp_send_request(uint32 target_ip)
{
  struct arp_hdr req;
  req.hwtype   = htons(ARP_HWTYPE_ETH);
  req.protype  = htons(ARP_PROTYPE_IP);
  req.hwlen    = 6;
  req.prolen   = 4;
  req.opcode   = htons(ARP_OP_REQUEST);
  memmove(req.sender_mac, e1000_mac, 6);
  req.sender_ip = net_ip;
  memset(req.target_mac, 0, 6);
  req.target_ip = target_ip;
  eth_send(broadcast_mac, ETH_TYPE_ARP, &req, sizeof(req));
}

// ---- IP send ----
// Sends IP packet to dst_ip.  proto=6 for TCP, etc.
// Performs ARP if needed (best-effort: drops if ARP not resolved).
int
netsend(uint32 dst_ip, uchar proto, void *data, int len)
{
  static uchar pkt[1500];  // IP header + payload (Ethernet MTU)

  if (len + (int)sizeof(struct ip_hdr) > (int)sizeof(pkt))
    return -1;

  struct ip_hdr *ip = (struct ip_hdr *)pkt;
  ip->vihl      = 0x45;
  ip->tos       = 0;
  ip->total_len = htons(sizeof(struct ip_hdr) + len);
  ip->id        = 0;
  ip->frag_off  = 0;
  ip->ttl       = 64;
  ip->proto     = proto;
  ip->checksum  = 0;
  ip->src       = net_ip;
  ip->dst       = dst_ip;
  ip->checksum  = ip_cksum(ip);

  memmove(pkt + sizeof(struct ip_hdr), data, len);

  // Resolve destination MAC
  uchar dst_mac[6];
  // If destination is on same subnet, ARP it directly; else ARP gateway
  uint32 via = ((dst_ip & net_mask) == (net_ip & net_mask)) ? dst_ip : net_gw;

  if (arp_lookup(via, dst_mac) < 0) {
    cprintf("netsend: ARP miss for %d.%d.%d.%d, sending request\n",
            (ntohl(via)>>24)&0xFF, (ntohl(via)>>16)&0xFF,
            (ntohl(via)>>8)&0xFF,  ntohl(via)&0xFF);
    arp_send_request(via);
    return -1;  // caller may retry; packet dropped for now
  }
  return eth_send(dst_mac, ETH_TYPE_IP, pkt, sizeof(struct ip_hdr) + len);
}

// ---- TCP helpers ----
static void
tcp_send_flags(struct sock *s, uchar flags, const char *data, int dlen)
{
  static uchar buf[sizeof(struct tcp_hdr) + 1460];
  int hlen = sizeof(struct tcp_hdr);
  int total = hlen + dlen;

  struct tcp_hdr *tcp = (struct tcp_hdr *)buf;
  tcp->sport    = htons(s->local_port);
  tcp->dport    = htons(s->remote_port);
  tcp->seq      = htonl(s->snd_nxt);
  tcp->ack_seq  = (flags & TCP_ACK) ? htonl(s->rcv_nxt) : 0;
  tcp->data_off = (hlen / 4) << 4;
  tcp->flags    = flags;
  tcp->window   = htons(SOCK_BUFSIZE);
  tcp->checksum = 0;
  tcp->urgent   = 0;
  if (dlen > 0)
    memmove(buf + hlen, data, dlen);

  // Build minimal IP header for checksum
  struct ip_hdr fake_ip;
  fake_ip.src   = s->local_ip;
  fake_ip.dst   = s->remote_ip;
  fake_ip.proto = IP_PROTO_TCP;
  tcp->checksum = htons(tcp_cksum(&fake_ip, tcp, total));

  netsend(s->remote_ip, IP_PROTO_TCP, buf, total);
}

// ---- Find a socket by 4-tuple ----
// Prefers established/active sockets over stale SYN_RCVD ones so that
// duplicate SYN retransmits don't shadow the real connection.
static struct sock *
sock_find(uint32 local_ip, ushort local_port, uint32 remote_ip, ushort remote_port)
{
  struct sock *best = 0;
  for (int i = 0; i < NSOCK; i++) {
    struct sock *s = &socks[i];
    if (!s->used) continue;
    if (s->local_port != local_port || s->remote_port != remote_port ||
        s->local_ip != local_ip    || s->remote_ip != remote_ip)
      continue;
    // Prefer any state beyond SYN_RCVD
    if (s->state != TCP_SYN_RCVD)
      return s;
    if (!best)
      best = s;
  }
  return best;
}

// Find listening socket for a port
static struct sock *
sock_find_listener(ushort port)
{
  for (int i = 0; i < NSOCK; i++) {
    struct sock *s = &socks[i];
    if (s->used && s->state == TCP_LISTEN && s->local_port == port)
      return s;
  }
  return 0;
}

// ---- TCP input ----
static void
tcp_in(struct ip_hdr *ip, int ip_len)
{
  int ip_hlen = (ip->vihl & 0xF) * 4;
  int tcp_total = ip_len - ip_hlen;
  if (tcp_total < (int)sizeof(struct tcp_hdr))
    return;

  struct tcp_hdr *tcp = (struct tcp_hdr *)((uchar *)ip + ip_hlen);
  ushort dport = ntohs(tcp->dport);
  ushort sport = ntohs(tcp->sport);
  uint32 seq   = ntohl(tcp->seq);
  uint32 ack   = ntohl(tcp->ack_seq);
  uchar flags  = tcp->flags;
  int hlen     = ((tcp->data_off >> 4) & 0xF) * 4;
  int dlen     = tcp_total - hlen;
  char *data   = (char *)tcp + hlen;

  struct sock *s = sock_find(ip->dst, dport, ip->src, sport);

  if (flags & TCP_SYN) {
    // Retransmit SYN-ACK if we already have a socket for this 4-tuple.
    if (s && s->state == TCP_SYN_RCVD) {
      acquire(&s->lock);
      tcp_send_flags(s, TCP_SYN | TCP_ACK, 0, 0);
      release(&s->lock);
      return;
    }

    // New connection attempt
    struct sock *listener = sock_find_listener(dport);
    if (!listener) {
      cprintf("tcp: no listener on port %d\n", dport);
      return;
    }

    struct sock *ns = sockalloc();
    if (!ns) return;
    ns->local_ip    = ip->dst;
    ns->local_port  = dport;
    ns->remote_ip   = ip->src;
    ns->remote_port = sport;
    ns->rcv_nxt     = seq + 1;
    ns->snd_nxt     = ticks * 1000 + 1;
    ns->snd_una     = ns->snd_nxt;
    ns->state       = TCP_SYN_RCVD;
    ns->parent      = listener;

    tcp_send_flags(ns, TCP_SYN | TCP_ACK, 0, 0);
    ns->snd_nxt++;
    return;
  }

  if (!s) return;

  acquire(&s->lock);

  if (flags & TCP_RST) {
    s->eof = 1;
    s->state = TCP_CLOSED;
    wakeup(s);
    release(&s->lock);
    return;
  }

  if (s->state == TCP_SYN_RCVD && (flags & TCP_ACK)) {
    if (ack == s->snd_nxt) {
      s->state = TCP_ESTABLISHED;
      // Enqueue into parent's accept queue
      struct sock *parent = s->parent;
      if (parent) {
        acquire(&parent->lock);
        int next = (parent->accept_tail + 1) % SOCK_BACKLOG;
        if (next != parent->accept_head) {
          parent->accept_q[parent->accept_tail] = s;
          parent->accept_tail = next;
          wakeup(parent);
        }
        release(&parent->lock);
      }
    }
    release(&s->lock);
    return;
  }

  if (s->state == TCP_ESTABLISHED || s->state == TCP_CLOSE_WAIT) {
    // Process incoming data
    if (dlen > 0 && seq == s->rcv_nxt) {
      // Copy into RX ring
      for (int i = 0; i < dlen; i++) {
        int next = (s->rxtail + 1) % SOCK_BUFSIZE;
        if (next == s->rxhead) break;  // buffer full; drop
        s->rxbuf[s->rxtail] = data[i];
        s->rxtail = next;
      }
      s->rcv_nxt += dlen;
      wakeup(s);  // wake up sockread
    }
    // Send ACK for data (or just the FIN below)
    if (dlen > 0)
      tcp_send_flags(s, TCP_ACK, 0, 0);
  }

  if ((flags & TCP_FIN) && s->state == TCP_ESTABLISHED) {
    s->rcv_nxt++;
    s->eof = 1;
    s->state = TCP_CLOSE_WAIT;
    tcp_send_flags(s, TCP_ACK, 0, 0);
    wakeup(s);
  } else if ((flags & TCP_FIN) && s->state == TCP_FIN_WAIT1) {
    s->rcv_nxt++;
    s->state = TCP_FIN_WAIT2;
    tcp_send_flags(s, TCP_ACK, 0, 0);
  }

  if ((flags & TCP_ACK) && s->state == TCP_LAST_ACK) {
    if (ack == s->snd_nxt) {
      s->state = TCP_CLOSED;
      s->used  = 0;
    }
  }

  release(&s->lock);
}

// ---- ICMP (ping reply) ----
static void
icmp_in(struct ip_hdr *ip, int ip_len)
{
  int ip_hlen = (ip->vihl & 0xF) * 4;
  int icmp_len = ip_len - ip_hlen;
  if (icmp_len < (int)sizeof(struct icmp_hdr)) return;

  struct icmp_hdr *icmp = (struct icmp_hdr *)((uchar *)ip + ip_hlen);
  if (icmp->type != ICMP_ECHO_REQUEST) return;

  // Reply: reuse the same buffer
  struct icmp_hdr *rep = icmp;
  rep->type     = ICMP_ECHO_REPLY;
  rep->checksum = 0;
  rep->checksum = htons(cksum_finish(cksum_add(0, rep, icmp_len)));

  netsend(ip->src, IP_PROTO_ICMP, rep, icmp_len);
}

// ---- ARP input ----
static void
arp_in(struct arp_hdr *arp)
{
  arp_cache_update(arp->sender_ip, arp->sender_mac);
  if (ntohs(arp->opcode) == ARP_OP_REQUEST && arp->target_ip == net_ip)
    arp_reply(arp);
}

// ---- Main receive entry point (called by e1000intr) ----
void
netin(void *pkt, int len)
{
  if (len < (int)sizeof(struct eth_hdr)) return;

  struct eth_hdr *eth = (struct eth_hdr *)pkt;
  ushort type = ntohs(eth->type);
  void *payload = (uchar *)pkt + sizeof(struct eth_hdr);
  int   plen   = len - sizeof(struct eth_hdr);

  if (type == ETH_TYPE_ARP) {
    if (plen >= (int)sizeof(struct arp_hdr))
      arp_in(payload);
  } else if (type == ETH_TYPE_IP) {
    if (plen < (int)sizeof(struct ip_hdr)) return;
    struct ip_hdr *ip = (struct ip_hdr *)payload;
    int ip_len = ntohs(ip->total_len);
    if (ip->proto == IP_PROTO_TCP)
      tcp_in(ip, ip_len);
    else if (ip->proto == IP_PROTO_ICMP)
      icmp_in(ip, ip_len);
  }
}

// ---- Socket allocator ----
struct sock *
sockalloc(void)
{
  for (int i = 0; i < NSOCK; i++) {
    struct sock *s = &socks[i];
    if (!s->used) {
      memset(s, 0, sizeof(*s));
      initlock(&s->lock, "sock");
      s->used  = 1;
      s->state = TCP_CLOSED;
      return s;
    }
  }
  return 0;
}

void
sockfree(struct sock *s)
{
  s->used  = 0;
  s->state = TCP_CLOSED;
}

int
sockbind(struct sock *s, ushort port)
{
  s->local_ip   = net_ip;
  s->local_port = port;
  s->state      = TCP_LISTEN;
  s->accept_head = s->accept_tail = 0;
  return 0;
}

// Block until a connection is ready in the accept queue.
// Returns 0 and sets *out on success, -1 on error.
int
sockaccept(struct sock *listener, struct sock **out)
{
  acquire(&listener->lock);
  while (listener->accept_head == listener->accept_tail) {
    if (!listener->used) {
      release(&listener->lock);
      return -1;
    }
    sleep(listener, &listener->lock);
  }
  *out = listener->accept_q[listener->accept_head];
  listener->accept_head = (listener->accept_head + 1) % SOCK_BACKLOG;
  release(&listener->lock);
  return 0;
}

// Read up to n bytes from socket.  Blocks until data available or EOF.
int
sockread(struct sock *s, char *buf, int n)
{
  acquire(&s->lock);
  while (s->rxhead == s->rxtail && !s->eof &&
         s->state != TCP_CLOSED) {
    sleep(s, &s->lock);
  }
  int i = 0;
  while (i < n && s->rxhead != s->rxtail) {
    buf[i++] = s->rxbuf[s->rxhead];
    s->rxhead = (s->rxhead + 1) % SOCK_BUFSIZE;
  }
  release(&s->lock);
  return i;  // 0 means EOF
}

// Write n bytes to socket.  Best-effort (TCP window not tracked).
int
sockwrite(struct sock *s, const char *buf, int n)
{
  if (s->state != TCP_ESTABLISHED && s->state != TCP_CLOSE_WAIT)
    return -1;

  // Send in chunks that fit within a single TCP segment
  int sent = 0;
  while (sent < n) {
    int chunk = n - sent;
    if (chunk > 1460) chunk = 1460;
    acquire(&s->lock);
    tcp_send_flags(s, TCP_ACK | TCP_PSH, buf + sent, chunk);
    s->snd_nxt += chunk;
    release(&s->lock);
    sent += chunk;
  }
  return sent;
}

// Initiate TCP close (send FIN), or free slot if already closed (RST path).
void
sockclose(struct sock *s)
{
  acquire(&s->lock);
  if (s->state == TCP_ESTABLISHED || s->state == TCP_CLOSE_WAIT) {
    tcp_send_flags(s, TCP_FIN | TCP_ACK, 0, 0);
    s->snd_nxt++;
    s->state = (s->state == TCP_ESTABLISHED) ? TCP_FIN_WAIT1 : TCP_LAST_ACK;
  } else if (s->state == TCP_CLOSED) {
    sockfree(s);
  }
  release(&s->lock);
}

void
netinit(void)
{
  // QEMU user-mode networking: guest is always 10.0.2.15
  // These are stored in network byte order
  net_ip   = htonl(0x0A00020F);   // 10.0.2.15
  net_mask = htonl(0xFFFFFF00);   // /24
  net_gw   = htonl(0x0A000202);   // 10.0.2.2

  initlock(&netlock, "net");
  memset(socks, 0, sizeof(socks));
  for (int i = 0; i < NSOCK; i++)
    initlock(&socks[i].lock, "sock");

  e1000init();
  cprintf("net: 10.0.2.15/24 gw 10.0.2.2\n");
}
