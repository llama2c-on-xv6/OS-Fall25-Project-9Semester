// kernel/mutex.h
#ifndef KERNEL_MUTEX_H
#define KERNEL_MUTEX_H

#include "types.h"
#include "spinlock.h"
#include "proc.h"

#define KMUTEX_TABLE_SIZE 64

struct kmutex {
  int inuse;          // non-zero if entry used
  uint64 user_addr;   // user-space pointer value passed to mutex_init
  int locked;         // 0 or 1
  struct spinlock lk; // protect this entry
};

void kmutex_init_table(void);
int kmutex_find_or_alloc(uint64 user_addr);
struct kmutex* kmutex_get(int idx);

#endif
