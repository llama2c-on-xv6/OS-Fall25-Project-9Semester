#include "types.h"
#include "riscv.h"
#include "defs.h"
#include "param.h"
#include "memlayout.h"
#include "spinlock.h"
#include "proc.h"
#include "vm.h"
#include "mutex.h"

uint64
sys_thread_create(void)
{
  uint64 startptr;
  uint64 argptr;

  if (argaddr(0, &startptr) < 0)
    return -1;
  if (argaddr(1, &argptr) < 0)
    return -1;

  void (*start_routine)(void *) = (void(*)(void *))startptr;
  void *arg = (void*)argptr;

  return (uint64)create_thread(myproc(), start_routine, arg);
}

uint64
sys_thread_exit(void)
{
  // no args
  thread_exit();
  return 0; // not reached
}

uint64
sys_thread_join(void)
{
  int tid;
  if (argint(0, &tid) < 0)
    return -1;
  return (uint64)thread_join(myproc(), tid);
}

// mutex syscalls
uint64
sys_mutex_init(void)
{
  uint64 user_addr;
  if (argaddr(0, &user_addr) < 0)
    return -1;
  int idx = kmutex_find_or_alloc(user_addr);
  return (uint64)idx; // return index >=0 or -1
}

uint64
sys_mutex_lock(void)
{
  uint64 user_addr;
  if (argaddr(0, &user_addr) < 0)
    return -1;
  int idx = kmutex_find_or_alloc(user_addr);
  if (idx < 0) return -1;
  struct kmutex *km = kmutex_get(idx);
  acquire(&km->lk);
  while (km->locked) {
    // sleep on kmutex pointer; this releases km->lk and swings back when woken
    sleep(km, &km->lk);
  }
  km->locked = 1;
  release(&km->lk);
  return 0;
}

uint64
sys_mutex_unlock(void)
{
  uint64 user_addr;
  if (argaddr(0, &user_addr) < 0)
    return -1;
  int idx = kmutex_find_or_alloc(user_addr);
  if (idx < 0) return -1;
  struct kmutex *km = kmutex_get(idx);
  acquire(&km->lk);
  km->locked = 0;
  wakeup(km);
  release(&km->lk);
  return 0;
}

uint64
sys_exit(void)
{
  int n;
  argint(0, &n);
  kexit(n);
  return 0;  // not reached
}

uint64
sys_getpid(void)
{
  return myproc()->pid;
}

uint64
sys_fork(void)
{
  return kfork();
}

uint64
sys_wait(void)
{
  uint64 p;
  argaddr(0, &p);
  return kwait(p);
}

uint64
sys_sbrk(void)
{
  uint64 addr;
  int t;
  int n;

  argint(0, &n);
  argint(1, &t);
  addr = myproc()->sz;

  if(t == SBRK_EAGER || n < 0) {
    if(growproc(n) < 0) {
      return -1;
    }
  } else {
    // Lazily allocate memory for this process: increase its memory
    // size but don't allocate memory. If the processes uses the
    // memory, vmfault() will allocate it.
    if(addr + n < addr)
      return -1;
    if(addr + n > TRAPFRAME)
      return -1;
    myproc()->sz += n;
  }
  return addr;
}

uint64
sys_pause(void)
{
  int n;
  uint ticks0;

  argint(0, &n);
  if(n < 0)
    n = 0;
  acquire(&tickslock);
  ticks0 = ticks;
  while(ticks - ticks0 < n){
    if(killed(myproc())){
      release(&tickslock);
      return -1;
    }
    sleep(&ticks, &tickslock);
  }
  release(&tickslock);
  return 0;
}

uint64
sys_kill(void)
{
  int pid;

  argint(0, &pid);
  return kkill(pid);
}

// return how many clock tick interrupts have occurred
// since start.
uint64
sys_uptime(void)
{
  uint xticks;

  acquire(&tickslock);
  xticks = ticks;
  release(&tickslock);
  return xticks;
}

// milestone-4 metrics
uint64
sys_rdtime(void) {
  return rdtime_csr();
}
