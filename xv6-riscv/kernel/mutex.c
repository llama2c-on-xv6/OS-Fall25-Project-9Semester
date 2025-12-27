// kernel/mutex.c
#include "types.h"
#include "riscv.h"
#include "defs.h"
#include "param.h"
#include "memlayout.h"
#include "spinlock.h"
#include "proc.h"
#include "mutex.h"

static struct kmutex kmutex_table[KMUTEX_TABLE_SIZE];
static struct spinlock kmutex_globallock;

void
kmutex_init_table(void)
{
  initlock(&kmutex_globallock, "kmutex_globallock");
  for (int i = 0; i < KMUTEX_TABLE_SIZE; i++) {
    kmutex_table[i].inuse = 0;
    kmutex_table[i].user_addr = 0;
    kmutex_table[i].locked = 0;
    initlock(&kmutex_table[i].lk, "kmutex_entry");
  }
}

// find index for user_addr; if not present, allocate a slot; return -1 if none
int
kmutex_find_or_alloc(uint64 user_addr)
{
  int freeidx = -1;
  acquire(&kmutex_globallock);
  for (int i = 0; i < KMUTEX_TABLE_SIZE; i++) {
    if (kmutex_table[i].inuse && kmutex_table[i].user_addr == user_addr) {
      release(&kmutex_globallock);
      return i;
    }
    if (!kmutex_table[i].inuse && freeidx < 0) {
      freeidx = i;
    }
  }
  if (freeidx >= 0) {
    kmutex_table[freeidx].inuse = 1;
    kmutex_table[freeidx].user_addr = user_addr;
    kmutex_table[freeidx].locked = 0;
    // its per-entry lock already init'd in init_table
    release(&kmutex_globallock);
    return freeidx;
  }
  release(&kmutex_globallock);
  return -1;
}

struct kmutex*
kmutex_get(int idx)
{
  if (idx < 0 || idx >= KMUTEX_TABLE_SIZE) return 0;
  return &kmutex_table[idx];
}
