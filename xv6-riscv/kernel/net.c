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
#include "e1000.c"


// xv6's ethernet and IP addresses
static uint8 local_mac[ETHADDR_LEN] = { 0x52, 0x54, 0x00, 0x12, 0x34, 0x56 };
static uint32 local_ip = MAKE_IP_ADDR(10, 0, 2, 15);

// qemu host's ethernet address.
static uint8 host_mac[ETHADDR_LEN] = { 0x52, 0x55, 0x0a, 0x00, 0x02, 0x02 };

//manage "Sockets" (bound ports) and a queue to hold packets for those sockets
struct rx_packet {
  char *buf;            // The 4KB buffer containing the packet
  int len;              // Length of valid data
  struct rx_packet *next;
};

struct sock {
  int port;             // Local port we are listening on
  struct rx_packet *head; // Queue head
  struct rx_packet *tail; // Queue tail
  struct spinlock lock; // Protects this socket's queue
  struct sock *next;    // Linked list of all sockets
};

struct sock *sockets = 0; // Head of the global socket list

static struct spinlock netlock;

void
netinit(void)
{
  initlock(&netlock, "netlock");
}


//
// bind(int port)
// prepare to receive UDP packets address to the port,
// i.e. allocate any queues &c needed.

// This function checks if a port is taken. If not, 
// it allocates a new socket structure and adds it to the
// list.
uint64
sys_bind(void)
{
  int port;
  struct sock *s;

  if(argint(0, &port) < 0)
    return -1;

  acquire(&netlock);
  // 1. Check if port is already in use
  for(s = sockets; s; s = s->next){
    if(s->port == port){
      release(&netlock);
      return -1;
    }
  }

  // 2. Allocate new socket
  // We use kalloc() to get a page, then cast it to struct sock.
  // It's a bit wasteful (uses 4096 bytes for a small struct), but standard xv6 
  // doesn't have a general purpose malloc.
  s = (struct sock*)kalloc();
  if(s == 0){
    release(&netlock);
    return -1;
  }
  
  memset(s, 0, PGSIZE);
  s->port = port;
  initlock(&s->lock, "sock");
  s->head = 0;
  s->tail = 0;

  // 3. Add to global list
  s->next = sockets;
  sockets = s;

  release(&netlock);
  return 0;
}

//
// unbind(int port)
// release any resources previously created by bind(port);
// from now on UDP packets addressed to port should be dropped.
//
uint64
sys_unbind(void)
{
  //
  // Optional: Your code here.
  //

  return 0;
}

//
// recv(int dport, int *src, short *sport, char *buf, int maxlen)
// if there's a received UDP packet already queued that was
// addressed to dport, then return it.
// otherwise wait for such a packet.
//
// sets *src to the IP source address.
// sets *sport to the UDP source port.
// copies up to maxlen bytes of UDP payload to buf.
// returns the number of bytes copied,
// and -1 if there was an error.
//
// dport, *src, and *sport are host byte order.
// bind(dport) must previously have been called.
//
uint64
sys_recv(void)
{
  int dport;
  uint64 src_addr, sport_addr, buf_addr;
  int maxlen;

  if(argint(0, &dport) < 0 ||
     argaddr(1, &src_addr) < 0 ||
     argaddr(2, &sport_addr) < 0 ||
     argaddr(3, &buf_addr) < 0 ||
     argint(4, &maxlen) < 0)
    return -1;

  // 1. Find the socket
  acquire(&netlock);
  struct sock *s;
  for(s = sockets; s; s = s->next){
    if(s->port == dport) break;
  }
  release(&netlock);

  if(s == 0) return -1; // Not bound

  acquire(&s->lock);

  // 2. Wait for a packet
  while(s->head == 0){
    if(myproc()->killed){
      release(&s->lock);
      return -1;
    }
    sleep(s, &s->lock);
  }

  // 3. Dequeue packet
  struct rx_packet *pkt = s->head;
  s->head = pkt->next;
  if(s->head == 0) s->tail = 0;
  
  release(&s->lock);

  // 4. Extract Header Info
  // We need to recalculate pointers because 'buf' is the raw start of the frame
  struct eth *eth = (struct eth *)pkt->buf;
  struct ip *ip = (struct ip *)(eth + 1);
  struct udp *udp = (struct udp *)(ip + 1);
  char *payload = (char *)(udp + 1);

  uint32 src_ip = ntohl(ip->ip_src);
  uint16 src_port = ntohs(udp->sport);
  
  // Calculate actual payload length
  // UDP header length field includes the 8-byte UDP header itself
  int payload_len = ntohs(udp->ulen) - sizeof(struct udp);
  if(payload_len > maxlen) payload_len = maxlen;

  // 5. Copy out to user
  if(copyout(myproc()->pagetable, src_addr, (char*)&src_ip, sizeof(src_ip)) < 0 ||
     copyout(myproc()->pagetable, sport_addr, (char*)&src_port, sizeof(src_port)) < 0 ||
     copyout(myproc()->pagetable, buf_addr, payload, payload_len) < 0)
  {
    kfree(pkt->buf);
    kfree((char*)pkt);
    return -1;
  }

  // 6. Cleanup
  kfree(pkt->buf);      // Free the packet data buffer
  kfree((char*)pkt);    // Free the queue node
  
  return payload_len;
}

// This code is lifted from FreeBSD's ping.c, and is copyright by the Regents
// of the University of California.
static unsigned short
in_cksum(const unsigned char *addr, int len)
{
  int nleft = len;
  const unsigned short *w = (const unsigned short *)addr;
  unsigned int sum = 0;
  unsigned short answer = 0;

  /*
   * Our algorithm is simple, using a 32 bit accumulator (sum), we add
   * sequential 16 bit words to it, and at the end, fold back all the
   * carry bits from the top 16 bits into the lower 16 bits.
   */
  while (nleft > 1)  {
    sum += *w++;
    nleft -= 2;
  }

  /* mop up an odd byte, if necessary */
  if (nleft == 1) {
    *(unsigned char *)(&answer) = *(const unsigned char *)w;
    sum += answer;
  }

  /* add back carry outs from top 16 bits to low 16 bits */
  sum = (sum & 0xffff) + (sum >> 16);
  sum += (sum >> 16);
  /* guaranteed now that the lower 16 bits of sum are correct */

  answer = ~sum; /* truncate to 16 bits */
  return answer;
}

//
// send(int sport, int dst, int dport, char *buf, int len)
//
uint64
sys_send(void)
{
  struct proc *p = myproc();
  int sport;
  int dst;
  int dport;
  uint64 bufaddr;
  int len;

  argint(0, &sport);
  argint(1, &dst);
  argint(2, &dport);
  argaddr(3, &bufaddr);
  argint(4, &len);

  int total = len + sizeof(struct eth) + sizeof(struct ip) + sizeof(struct udp);
  if(total > PGSIZE)
    return -1;

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
  ip->ip_vhl = 0x45; // version 4, header length 4*5
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

  e1000_transmit(buf, total);

  return 0;
}

//is UDP? & is passed to bind()?
void
ip_rx(char *buf, int len)
{
  // don't delete this printf; make grade depends on it.
  static int seen_ip = 0;
  if(seen_ip == 0)
    printf("ip_rx: received an IP packet\n");
  seen_ip = 1;

  struct eth *eth = (struct eth *)buf;
  struct ip *ip = (struct ip *)(eth + 1);
  struct udp *udp = (struct udp *)(ip + 1);

  // 1. Check if protocol is UDP
  if(ip->ip_p != IPPROTO_UDP){
    kfree(buf); // Not UDP, we are done with it
    return;
  }

  // 2. Get Destination Port (Network to Host order)
  uint16 dport = ntohs(udp->dport);

  acquire(&netlock);
  struct sock *s;
  for(s = sockets; s; s = s->next){
    if(s->port == dport) break;
  }
  release(&netlock);

  // 3. If socket found, try to enqueue
  if(s){
    acquire(&s->lock);

    // Count queue size to prevent DoS (limit 16)
    int qlen = 0;
    struct rx_packet *p = s->head;
    while(p){ qlen++; p = p->next; }

    if(qlen < 16){
      // Create a queue node
      struct rx_packet *node = (struct rx_packet*)kalloc();
      if(node){
        node->buf = buf;
        node->len = len;
        node->next = 0;

        // Add to tail
        if(s->tail){
          s->tail->next = node;
          s->tail = node;
        } else {
          s->head = node;
          s->tail = node;
        }
        
        wakeup(s); // Wake up sys_recv
        release(&s->lock);
        return; // PACKET CONSUMED, do NOT kfree(buf)
      }
    }
    release(&s->lock);
  }

  // If we get here: either no socket found, queue full, or alloc failed.
  kfree(buf);
}

//
// send an ARP reply packet to tell qemu to map
// xv6's ip address to its ethernet address.
// this is the bare minimum needed to persuade
// qemu to send IP packets to xv6; the real ARP
// protocol is more complex.
//
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
  if(buf == 0)
    panic("send_arp_reply");
  
  struct eth *eth = (struct eth *) buf;
  memmove(eth->dhost, ineth->shost, ETHADDR_LEN); // ethernet destination = query source
  memmove(eth->shost, local_mac, ETHADDR_LEN); // ethernet source = xv6's ethernet address
  eth->type = htons(ETHTYPE_ARP);

  struct arp *arp = (struct arp *)(eth + 1);
  arp->hrd = htons(ARP_HRD_ETHER);
  arp->pro = htons(ETHTYPE_IP);
  arp->hln = ETHADDR_LEN;
  arp->pln = sizeof(uint32);
  arp->op = htons(ARP_OP_REPLY);

  memmove(arp->sha, local_mac, ETHADDR_LEN);
  arp->sip = htonl(local_ip);
  memmove(arp->tha, ineth->shost, ETHADDR_LEN);
  arp->tip = inarp->sip;

  e1000_transmit(buf, sizeof(*eth) + sizeof(*arp));

  kfree(inbuf);
}

void
net_rx(char *buf, int len)
{
  struct eth *eth = (struct eth *) buf;

  if(len >= sizeof(struct eth) + sizeof(struct arp) &&
     ntohs(eth->type) == ETHTYPE_ARP){
    arp_rx(buf);
  } else if(len >= sizeof(struct eth) + sizeof(struct ip) &&
     ntohs(eth->type) == ETHTYPE_IP){
    ip_rx(buf, len);
  } else {
    kfree(buf);
  }
}
