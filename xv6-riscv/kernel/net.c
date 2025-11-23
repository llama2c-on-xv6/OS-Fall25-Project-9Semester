/* kernel/net.c */
#include "types.h"
#include "param.h"
#include "memlayout.h"
#include "riscv.h"
#include "spinlock.h"
#include "proc.h"
#include "defs.h"
#include "fs.h"
#include "sleeplock.h"
#include "file.h"
#include "net.h"

/* xv6's ethernet and IP addresses */
static uint8 local_mac[ETHADDR_LEN] = { 0x52, 0x54, 0x00, 0x12, 0x34, 0x56 };
static uint32 local_ip = MAKE_IP_ADDR(10, 0, 2, 15);

/* qemu host's ethernet address. */
static uint8 host_mac[ETHADDR_LEN] = { 0x52, 0x55, 0x0a, 0x00, 0x02, 0x02 };

static struct spinlock netlock;

/* Forward declaration (avoid implicit-declaration errors) */
static void udp_ports_init_once(void);

void
netinit(void)
{
  initlock(&netlock, "netlock");
  udp_ports_init_once();
}

#define UDP_MAX_QUEUED 16

/* Packet node stored in kernel for queued UDP payloads. */
struct udp_pkt {
  struct udp_pkt *next;
  int src_ip;        /* host byte order */
  short src_port;    /* host byte order */
  int len;           /* payload length in bytes */
  char *data;        /* kernel buffer holding payload (kalloc() pointer) */
};

/* Per-bound-port queue */
struct udp_port {
  short port;                /* local port (host order) */
  struct spinlock lock;      /* protects this port's queue & fields */
  struct udp_pkt *head;
  struct udp_pkt *tail;
  int count;                 /* number of queued packets */
  struct proc *owner;        /* process that called bind() */
  struct udp_port *next;     /* linked list of ports */
};

/* Global list of bound ports and lock to protect list */
static struct udp_port *udp_ports = 0;
static struct spinlock udp_ports_lock;

/* Initialize udp_ports_lock if not yet initialized. Called from netinit or lazily. */
static void
udp_ports_init_once(void) {
  static int inited = 0;
  if (!inited) {
    initlock(&udp_ports_lock, "udp_ports");
    inited = 1;
  }
}

/* Helper: find port entry (returns pointer or 0). No port lock held on return. */
static struct udp_port *
udp_port_find(short port) {
  struct udp_port *p;
  acquire(&udp_ports_lock);
  for(p = udp_ports; p; p = p->next) {
    if(p->port == port) {
      release(&udp_ports_lock);
      return p;
    }
  }
  release(&udp_ports_lock);
  return 0;
}

/* Helper: allocate & add a new port entry. Caller ensures not already present.
 * Returns 0 on failure (OOM), pointer on success.
 */
static struct udp_port *
udp_port_create(short port, struct proc *owner) {
  struct udp_port *p = (struct udp_port *)kalloc();
  if(!p) return 0;

  p->port = port;
  initlock(&p->lock, "udp_port");
  p->head = p->tail = 0;
  p->count = 0;
  p->owner = owner;
  p->next = 0;

  acquire(&udp_ports_lock);
  p->next = udp_ports;
  udp_ports = p;
  release(&udp_ports_lock);

  return p;
}

/* Helper: remove & free a port entry. Caller may be any process; only owner removal is allowed in sys_unbind.
 * Returns 0 on success, -1 if not found.
 */
static int
udp_port_remove(short port) {
  struct udp_port *p, *prev = 0;
  acquire(&udp_ports_lock);
  for(p = udp_ports; p; prev = p, p = p->next) {
    if(p->port == port) {
      /* unlink */
      if(prev) prev->next = p->next;
      else udp_ports = p->next;
      release(&udp_ports_lock);

      /* free queued packets and the port struct */
      acquire(&p->lock);
      struct udp_pkt *pkt = p->head;
      while(pkt) {
        struct udp_pkt *n = pkt->next;
        if(pkt->data) kfree(pkt->data);
        kfree((char *)pkt);
        pkt = n;
      }
      p->head = p->tail = 0;
      p->count = 0;
      release(&p->lock);

      kfree((char *)p);
      return 0;
    }
  }
  release(&udp_ports_lock);
  return -1;
}

/* Enqueue a payload copy into port queue. Returns 0 on success, -1 if queue full or OOM. */
static int
udp_enqueue(struct udp_port *port, int src_ip, short src_port, char *payload, int plen) {
  struct udp_pkt *pkt;

  acquire(&port->lock);
  if(port->count >= UDP_MAX_QUEUED) {
  //  printf("udp_enqueue: port %d queue full\n", port->port);
    release(&port->lock);
    return -1; /* queue full */
  }

  /* allocate pkt node */
  pkt = (struct udp_pkt *)kalloc();
  if(!pkt) {
    release(&port->lock);
    return -1;
  }

  /* allocate buffer for payload: use kalloc() (one page). Tests use small packets. */
  if(plen > PGSIZE) {
    kfree((char *)pkt);
    release(&port->lock);
    return -1;
  }
  pkt->data = kalloc();
  if(!pkt->data) {
    kfree((char *)pkt);
    release(&port->lock);
    return -1;
  }

  /* copy payload into kernel buffer */
  memmove(pkt->data, payload, plen);

  pkt->src_ip = src_ip;
  pkt->src_port = src_port;
  pkt->len = plen;
  pkt->next = 0;

  if(port->tail) {
    port->tail->next = pkt;
    port->tail = pkt;
  } else {
    port->head = port->tail = pkt;
  }
  port->count++;
  release(&port->lock);

  /* wake any waiter(s) sleeping on this port */
  wakeup(port);

//  printf("udp_enqueue: enqueued port=%d src=%x:%d len=%d count=%d\n", port->port, src_ip, src_port, plen, port->count);
  return 0;
}

/* Caller must hold port->lock to dequeue. Returns pkt pointer (caller frees pkt->data and pkt). */
static struct udp_pkt *
udp_dequeue_locked(struct udp_port *port) {
  struct udp_pkt *pkt = port->head;
  if(!pkt) return 0;

  port->head = pkt->next;
  if(!port->head) port->tail = 0;
  port->count--;
  pkt->next = 0;
  return pkt;
}

/* bind(int port) - prepare to receive UDP packets addressed to the port */
uint64
sys_bind(void)
{
  int port;
  argint(0, &port);

 // printf("sys_bind: pid=%d port=%d\n", myproc()->pid, port); /* debug */

  if(port <= 0 || port > 65535) return -1;

  udp_ports_init_once();

  /* disallow double-binding */
  acquire(&udp_ports_lock);
  for(struct udp_port *p = udp_ports; p; p = p->next) {
    if(p->port == (short)port) {
      release(&udp_ports_lock);
      return -1; /* already bound */
    }
  }
  release(&udp_ports_lock);

  if(!udp_port_create((short)port, myproc())) return -1;
  return 0;
}

/* unbind(int port) */
uint64
sys_unbind(void)
{
  int port;
  argint(0, &port);

  if(port <= 0 || port > 65535) return -1;

  struct udp_port *p = udp_port_find((short)port);
  if(!p) return -1;
  if(p->owner != myproc()) return -1;

  if(udp_port_remove((short)port) < 0) return -1;
  return 0;
}

/* recv(int dport, int *src, short *sport, char *buf, int maxlen) */
uint64
sys_recv(void)
{
  int dport;
  uint64 src_ptr, sport_ptr, buf_ptr;
  int maxlen;

  argint(0, &dport);
  argaddr(1, &src_ptr);
  argaddr(2, &sport_ptr);
  argaddr(3, &buf_ptr);
  argint(4, &maxlen);

  udp_ports_init_once();

  struct udp_port *port = udp_port_find((short)dport);
  if(!port) return -1;

  if(port->owner != myproc()) return -1;

  acquire(&port->lock);
  //printf("sys_recv: pid=%d dport=%d owner=%p count=%d\n", myproc()->pid, dport, port->owner, port->count); /*debug*/

  while(port->count == 0) {
    sleep(port, &port->lock);
  //  printf("sys_recv: woke pid=%d dport=%d\n", myproc()->pid, dport); /*debug*/
  }

  struct udp_pkt *pkt = udp_dequeue_locked(port);
  release(&port->lock);

  if(!pkt) return -1;

  int tocopy = pkt->len;
  if(tocopy > maxlen) tocopy = maxlen;

  if(copyout(myproc()->pagetable, buf_ptr, pkt->data, tocopy) < 0) {
    kfree(pkt->data);
    kfree((char *)pkt);
    return -1;
  }

  if(copyout(myproc()->pagetable, src_ptr, (char *)&pkt->src_ip, sizeof(pkt->src_ip)) < 0) {
    kfree(pkt->data);
    kfree((char *)pkt);
    return -1;
  }

  short sp = pkt->src_port;
  if(copyout(myproc()->pagetable, sport_ptr, (char *)&sp, sizeof(sp)) < 0) {
    kfree(pkt->data);
    kfree((char *)pkt);
    return -1;
  }

  kfree(pkt->data);
  kfree((char *)pkt);

  return tocopy;
}

static unsigned short
in_cksum(const unsigned char *addr, int len)
{
  int nleft = len;
  const unsigned short *w = (const unsigned short *)addr;
  unsigned int sum = 0;
  unsigned short answer = 0;

  while (nleft > 1)  {
    sum += *w++;
    nleft -= 2;
  }

  if (nleft == 1) {
    *(unsigned char *)(&answer) = *(const unsigned char *)w;
    sum += answer;
  }

  sum = (sum & 0xffff) + (sum >> 16);
  sum += (sum >> 16);
  answer = ~sum;
  return answer;
}

/* send(int sport, int dst, int dport, char *buf, int len) */
uint64
sys_send(void)
{
  struct proc *p = myproc();
  int sport; int dst; int dport;
  uint64 bufaddr;
  int len;

  argint(0, &sport);
  argint(1, &dst);
  argint(2, &dport);
  argaddr(3, &bufaddr);
  argint(4, &len);

  int total = len + sizeof(struct eth) + sizeof(struct ip) + sizeof(struct udp);
  if(total > PGSIZE) return -1;

  char *buf = kalloc();
  if(buf == 0){
    printf("sys_send: kalloc failed\n");
    return -1;
  }
  memset(buf, 0, PGSIZE);

  struct eth *eth = (struct eth *) buf;
  memmove(eth->dhost, host_mac, ETHADDR_LEN);
  memmove(eth->shost, local_mac, ETHADDR_LEN);
  eth->type = htons(ETHTYPE_IP);

  struct ip *ip = (struct ip *)(eth + 1);
  ip->ip_vhl = 0x45; /* version 4, header length 4*5 */
  ip->ip_tos = 0;
  ip->ip_len = htons(sizeof(struct ip) + sizeof(struct udp) + len);
  ip->ip_id = 0;
  ip->ip_off = 0;
  ip->ip_ttl = 100;
  ip->ip_p = IPPROTO_UDP;
  ip->ip_src = htonl(local_ip);
  ip->ip_dst = htonl(dst);
  ip->ip_sum = in_cksum((unsigned char *)ip, sizeof(*ip));

  struct udp *udp = (struct udp *)(ip + 1);
  udp->sport = htons(sport);
  udp->dport = htons(dport);
  udp->ulen = htons(len + sizeof(struct udp));

  char *payload = (char *)(udp + 1);
  if(copyin(p->pagetable, payload, bufaddr, len) < 0){
    kfree(buf);
    printf("send: copyin failed\n");
    return -1;
  }

  if (e1000_transmit(buf, total) < 0) {
    kfree(buf);
    return -1;
  }

  return 0;
}

void
ip_rx(char *buf, int len)
{
  /* don't delete this printf; make grade depends on it. */
  static int seen_ip = 0;
  if(seen_ip == 0)
    printf("ip_rx: received an IP packet\n");
  seen_ip = 1;

  udp_ports_init_once();

  if(len < sizeof(struct eth) + sizeof(struct ip) + sizeof(struct udp)) {
    kfree(buf);
    return;
  }

  struct ip *ip = (struct ip *)(buf + sizeof(struct eth));

  if(ip->ip_p != IPPROTO_UDP){
    kfree(buf);
    return;
  }

  struct udp *uh = (struct udp *)((char *)ip + sizeof(struct ip));
  int udp_len = ntohs(uh->ulen);
  int payload_len = udp_len - sizeof(struct udp);
  if(payload_len < 0){
    kfree(buf);
    return;
  }

  if(len < (int)(sizeof(struct eth) + sizeof(struct ip) + sizeof(struct udp) + payload_len)) {
    kfree(buf);
    return;
  }

  int src_ip = ntohl(ip->ip_src);
  short dst_port = ntohs(uh->dport);
  short src_port = ntohs(uh->sport);

  /* DEBUG: show packet meta so we can diagnose why it's not delivered */
  //printf("DEBUG ip_rx: src_ip=%x dst_port=%d src_port=%d payload_len=%d len_total=%d\n",
  //       src_ip, dst_port, src_port, payload_len, len);

  struct udp_port *port = udp_port_find(dst_port);
  if(!port) {
    kfree(buf);
    return;
  }

  char *payload = (char *)uh + sizeof(struct udp);

  if(udp_enqueue(port, src_ip, src_port, payload, payload_len) < 0) {
    kfree(buf);
    return;
  }

  kfree(buf);
}

/* send an ARP reply packet to tell qemu to map xv6's ip address to its ethernet address. */
void
arp_rx(char *inbuf)
{
  static int seen_arp = 0;

  if(seen_arp){
    kfree(inbuf);
    return;
  }
  printf("arp_rx: received an ARP packet\n");
  seen_arp = 1;

  struct eth *ineth = (struct eth *) inbuf;
  struct arp *inarp = (struct arp *) (ineth + 1);

  char *buf = kalloc();
  if(buf == 0) panic("send_arp_reply");

  struct eth *eth = (struct eth *) buf;
  memmove(eth->dhost, ineth->shost, ETHADDR_LEN); /* ethernet destination = query source */
  memmove(eth->shost, local_mac, ETHADDR_LEN);   /* ethernet source = xv6's ethernet address */
  eth->type = htons(ETHTYPE_ARP);

  struct arp *arp = (struct arp *)(eth + 1);
  arp->hrd = htons(ARP_HRD_ETHER);
  arp->pro = htons(ETHTYPE_IP);
  arp->hln = ETHADDR_LEN;
  arp->pln = sizeof(uint32);
  arp->op  = htons(ARP_OP_REPLY);

  memmove(arp->sha, local_mac, ETHADDR_LEN);
  arp->sip = htonl(local_ip);
  memmove(arp->tha, ineth->shost, ETHADDR_LEN);
  arp->tip = inarp->sip;

  if (e1000_transmit(buf, sizeof(*eth) + sizeof(*arp)) < 0) {
    kfree(buf);
    kfree(inbuf);
    return;
  }

  kfree(inbuf);
}

void
net_rx(char *buf, int len)
{
  struct eth *eth = (struct eth *) buf;
//  printf("net_rx: eth type=0x%x len=%d\n", ntohs(eth->type), len); /*debug*/

  if(len >= sizeof(struct eth) + sizeof(struct arp) && ntohs(eth->type) == ETHTYPE_ARP){
    arp_rx(buf);
  } else if(len >= sizeof(struct eth) + sizeof(struct ip) && ntohs(eth->type) == ETHTYPE_IP){
    ip_rx(buf, len);
  } else {
    kfree(buf);
  }
}
