#include "types.h"
#include "param.h"
#include "memlayout.h"
#include "riscv.h"
#include "spinlock.h"
#include "proc.h"
#include "defs.h"
#include "e1000_dev.h"
#include "net.h"

#define TX_RING_SIZE 16
static struct tx_desc tx_ring[TX_RING_SIZE] __attribute__((aligned(16)));

#define RX_RING_SIZE 16
static struct rx_desc rx_ring[RX_RING_SIZE] __attribute__((aligned(16)));

// remember where the e1000's registers live.
static volatile uint32 *regs;

struct spinlock e1000_lock;

static void e1000_recv(void);


// called by pci_init().
// xregs is the memory address at which the
// e1000's registers are mapped.
void
e1000_init(uint32 *xregs)
{
//  printf("e1000_init called: xregs=%p\n", xregs);

  int i;

  initlock(&e1000_lock, "e1000");
  regs = xregs;

  // Reset device
  regs[E1000_IMS] = 0;
  regs[E1000_CTL] |= E1000_CTL_RST;
  regs[E1000_IMS] = 0;
  __sync_synchronize();

  //
  // TX init
  //
  memset(tx_ring, 0, sizeof(tx_ring));
  for (i = 0; i < TX_RING_SIZE; i++) {
    tx_ring[i].status = E1000_TXD_STAT_DD;
    tx_ring[i].addr = 0;
  }

  regs[E1000_TDBAL] = (uint64)tx_ring;

  if (sizeof(tx_ring) % 128 != 0)
    panic("e1000");

  regs[E1000_TDLEN] = sizeof(tx_ring);
  regs[E1000_TDH] = regs[E1000_TDT] = 0;

  //
  // RX init
  //
  memset(rx_ring, 0, sizeof(rx_ring));

  for (i = 0; i < RX_RING_SIZE; i++) {
    rx_ring[i].addr = (uint64)kalloc();
    if (!rx_ring[i].addr)
      panic("e1000");
  }

  regs[E1000_RDBAL] = (uint64)rx_ring;

  if (sizeof(rx_ring) % 128 != 0)
    panic("e1000");

  regs[E1000_RDH] = 0;
  regs[E1000_RDT] = RX_RING_SIZE - 1;
  regs[E1000_RDLEN] = sizeof(rx_ring);

  // filter by qemu's MAC address: 52:54:00:12:34:56
  regs[E1000_RA] = 0x12005452;
  regs[E1000_RA + 1] = 0x5634 | (1 << 31);

  // multicast table
  for (int i = 0; i < 4096 / 32; i++)
    regs[E1000_MTA + i] = 0;

  // TX control
  regs[E1000_TCTL] =
      E1000_TCTL_EN |
      E1000_TCTL_PSP |
      (0x10 << E1000_TCTL_CT_SHIFT) |
      (0x40 << E1000_TCTL_COLD_SHIFT);

  regs[E1000_TIPG] = 10 | (8 << 10) | (6 << 20);

  // RX control
  regs[E1000_RCTL] =
      E1000_RCTL_EN |
      E1000_RCTL_BAM |
      E1000_RCTL_SZ_2048 |
      E1000_RCTL_SECRC;

  // Interrupt moderation OFF
  regs[E1000_RDTR] = 0;
  regs[E1000_RADV] = 0;

     
  regs[E1000_IMS] = (1 << 0) | (1 << 7);

}

// -------------------------------------------------------------

static void
e1000_recv(void)
{
  acquire(&e1000_lock);

  int rdt = regs[E1000_RDT] % RX_RING_SIZE;
  int idx = (rdt + 1) % RX_RING_SIZE;

  while (rx_ring[idx].status & E1000_RXD_STAT_DD) {

    int pkt_len = (int)rx_ring[idx].length;
    char *pkt_buf = (char*)rx_ring[idx].addr;

    if (!pkt_buf) {
      rx_ring[idx].status = 0;
      rx_ring[idx].length = 0;
      regs[E1000_RDT] = idx;
      idx = (idx + 1) % RX_RING_SIZE;
      continue;
    }

    char *newbuf = kalloc();
    if (!newbuf) {
      rx_ring[idx].addr = 0;
      rx_ring[idx].status = 0;
      rx_ring[idx].length = 0;
      regs[E1000_RDT] = idx;
      idx = (idx + 1) % RX_RING_SIZE;
      continue;
    }

    rx_ring[idx].addr = (uint64)newbuf;
    rx_ring[idx].length = 0;
    rx_ring[idx].csum = 0;
    rx_ring[idx].status = 0;
    rx_ring[idx].errors = 0;
    rx_ring[idx].special = 0;

    __sync_synchronize();

    regs[E1000_RDT] = idx;
    idx = (idx + 1) % RX_RING_SIZE;

    release(&e1000_lock);

    net_rx(pkt_buf, pkt_len);

    acquire(&e1000_lock);
  }

  release(&e1000_lock);
}

// -------------------------------------------------------------

void
e1000_intr(void)
{
//  printf("e1000_intr: interrupt\n");

  regs[E1000_ICR] = 0xffffffff;

  e1000_recv();

}

// -------------------------------------------------------------

int
e1000_transmit(char *buf, int len)
{
  acquire(&e1000_lock);

  int tdt = regs[E1000_TDT] % TX_RING_SIZE;



  if (!(tx_ring[tdt].status & E1000_TXD_STAT_DD)) {
  //  printf("e1000_transmit: ring full at idx=%d, fail to queue\n", tdt);
    release(&e1000_lock);
    return -1;
  }

  if (tx_ring[tdt].addr) {
    kfree((char*)tx_ring[tdt].addr);
    tx_ring[tdt].addr = 0;
  }

  tx_ring[tdt].addr = (uint64)buf;
  tx_ring[tdt].length = (uint16)len;
  tx_ring[tdt].cso = 0;
  tx_ring[tdt].cmd = E1000_TXD_CMD_EOP | E1000_TXD_CMD_RS;
  tx_ring[tdt].status = 0;
  tx_ring[tdt].css = 0;
  tx_ring[tdt].special = 0;

  __sync_synchronize();

  regs[E1000_TDT] = (tdt + 1) % TX_RING_SIZE;

  release(&e1000_lock);
  return 0;
}
