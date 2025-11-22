#include "types.h"
#include "param.h"
#include "memlayout.h"
#include "riscv.h"
#include "spinlock.h"
#include "proc.h"
#include "defs.h"
#include "e1000_dev.h"

#define TX_RING_SIZE 16
static struct tx_desc tx_ring[TX_RING_SIZE] __attribute__((aligned(16)));

#define RX_RING_SIZE 16
static struct rx_desc rx_ring[RX_RING_SIZE] __attribute__((aligned(16)));

// remember where the e1000's registers live.
static volatile uint32 *regs;

struct spinlock e1000_lock;

// called by pci_init().
// xregs is the memory address at which the
// e1000's registers are mapped.
// this code loosely follows the initialization directions
// in Chapter 14 of Intel's Software Developer's Manual.
void
e1000_init(uint32 *xregs)
{
  int i;

  initlock(&e1000_lock, "e1000");

  regs = xregs;

  // Reset the device
  regs[E1000_IMS] = 0; // disable interrupts
  regs[E1000_CTL] |= E1000_CTL_RST;
  regs[E1000_IMS] = 0; // redisable interrupts
  __sync_synchronize();

  // [E1000 14.5] Transmit initialization
  memset(tx_ring, 0, sizeof(tx_ring));
  for (i = 0; i < TX_RING_SIZE; i++) {
    tx_ring[i].status = E1000_TXD_STAT_DD;
    tx_ring[i].addr = 0;
  }
  regs[E1000_TDBAL] = (uint64) tx_ring;
  if(sizeof(tx_ring) % 128 != 0)
    panic("e1000");
  regs[E1000_TDLEN] = sizeof(tx_ring);
  regs[E1000_TDH] = regs[E1000_TDT] = 0;
  
  // [E1000 14.4] Receive initialization
  memset(rx_ring, 0, sizeof(rx_ring));
  for (i = 0; i < RX_RING_SIZE; i++) {
    rx_ring[i].addr = (uint64) kalloc();
    if (!rx_ring[i].addr)
      panic("e1000");
  }
  regs[E1000_RDBAL] = (uint64) rx_ring;
  if(sizeof(rx_ring) % 128 != 0)
    panic("e1000");
  regs[E1000_RDH] = 0;
  regs[E1000_RDT] = RX_RING_SIZE - 1;
  regs[E1000_RDLEN] = sizeof(rx_ring);

  // filter by qemu's MAC address, 52:54:00:12:34:56
  regs[E1000_RA] = 0x12005452;
  regs[E1000_RA+1] = 0x5634 | (1<<31);
  // multicast table
  for (int i = 0; i < 4096/32; i++)
    regs[E1000_MTA + i] = 0;

  // transmitter control bits.
  regs[E1000_TCTL] = E1000_TCTL_EN |  // enable
    E1000_TCTL_PSP |                  // pad short packets
    (0x10 << E1000_TCTL_CT_SHIFT) |   // collision stuff
    (0x40 << E1000_TCTL_COLD_SHIFT);
  regs[E1000_TIPG] = 10 | (8<<10) | (6<<20); // inter-pkt gap

  // receiver control bits.
  regs[E1000_RCTL] = E1000_RCTL_EN | // enable receiver
    E1000_RCTL_BAM |                 // enable broadcast
    E1000_RCTL_SZ_2048 |             // 2048-byte rx buffers
    E1000_RCTL_SECRC;                // strip CRC
  
  // ask e1000 for receive interrupts.
  regs[E1000_RDTR] = 0; // interrupt after every received packet (no timer)
  regs[E1000_RADV] = 0; // interrupt after every packet (no timer)
  regs[E1000_IMS] = (1 << 7); // RXDW -- Receiver Descriptor Write Back
}

int
e1000_transmit(char *buf, int len)
{
  //
  acquire(&e1000_lock);

  // 1. Get the current transmit ring index (TDT)
  uint32 idx = regs[E1000_TDT];

  // 2. Check if the ring is overflowing
  // If the DD (Descriptor Done) bit is NOT set, the E1000 is still using this descriptor.
  if((tx_ring[idx].status & E1000_TXD_STAT_DD) == 0){
    release(&e1000_lock);
    return -1;
  }

  // 3. Free the old mbuf at this slot if it exists
  if(tx_mbufs[idx]){
    mbuffree(tx_mbufs[idx]);
  }

  // 4. Stash the new mbuf pointer for later freeing
  tx_mbufs[idx] = m;

  // 5. Fill in the descriptor
  tx_ring[idx].addr = (uint64)m->head;
  tx_ring[idx].length = m->len;
  // CMD_EOP: End of Packet, CMD_RS: Report Status (sets DD bit when done)
  tx_ring[idx].cmd = E1000_TXD_CMD_EOP | E1000_TXD_CMD_RS; 

  // 6. Update TDT to the next index modulo ring size
  regs[E1000_TDT] = (idx + 1) % TX_RING_SIZE;

  release(&e1000_lock);
  //
  // buf contains an ethernet frame; program it into
  // the TX descriptor ring so that the e1000 sends it. Stash
  // a pointer so that it can be freed after send completes.
  //
  // return 0 on success.
  // return -1 on failure (e.g., there is no descriptor available)
  // so that the caller knows to free buf.
  //

  
  return 0;
}

static void
e1000_recv(void)
{
  //
  // Loop to handle multiple packets in one interrupt
  while(1){
    // 1. Get the next ring index (RDT + 1)
    // RDT points to the last descriptor given TO the hardware, so RDT+1 is the next one to check.
    uint32 idx = (regs[E1000_RDT] + 1) % RX_RING_SIZE;

    // 2. Check if a new packet is available (DD bit set)
    if((rx_ring[idx].status & E1000_RXD_STAT_DD) == 0){
      break; // No more packets processed by hardware
    }

    // 3. Get the mbuf and update length
    struct mbuf *m = rx_mbufs[idx];
    m->len = rx_ring[idx].length;

    // 4. Deliver to network stack
    // We must NOT hold e1000_lock here because net_rx might call into other locking code
    net_rx(m);

    // 5. Allocate a new mbuf to replace the one sent up
    struct mbuf *new_m = mbufalloc(0);
    if(!new_m){
      // If allocation fails, we can't replenish the ring right now.
      // In a robust driver, we might drop the packet and reuse the old mbuf.
      // For this lab, panic or handling it gracefully is acceptable, but ensuring replacement is key.
      panic("e1000_recv: mbufalloc failed");
    }
    rx_mbufs[idx] = new_m;

    // 6. Reset descriptor for next use
    rx_ring[idx].addr = (uint64)new_m->head;
    rx_ring[idx].status = 0;

    // 7. Advance RDT
    regs[E1000_RDT] = idx;
  } 
  //
  // Check for packets that have arrived from the e1000
  // Create and deliver a buf for each packet (using net_rx()).
  //

}

void
e1000_intr(void)
{
  // tell the e1000 we've seen this interrupt;
  // without this the e1000 won't raise any
  // further interrupts.
  regs[E1000_ICR] = 0xffffffff;

  e1000_recv();
}
