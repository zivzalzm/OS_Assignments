#include "types.h"
#include "riscv.h"
#include "defs.h"
#include "param.h"
#include "memlayout.h"
#include "spinlock.h"
#include "proc.h"

uint64
sys_exit(void)
{
  int n;
  argint(0, &n);
  exit(n);
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
  return fork();
}

uint64
sys_wait(void)
{
  uint64 p;
  argaddr(0, &p);
  return wait(p);
}

uint64
sys_sbrk(void)
{
  uint64 addr;
  int n;

  argint(0, &n);
  addr = myproc()->sz;
  if(growproc(n) < 0)
    return -1;
  return addr;
}

uint64
sys_sleep(void)
{
  int n;
  uint ticks0;

  argint(0, &n);
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
  return kill(pid);
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

uint64
sys_co_yield(void)
{
  int target_pid, value;
  argint (0, &target_pid);
  argint (1, &value);

  struct proc *p = myproc();

  // error: invalid pid or self-yield
  if (target_pid <= 0 || target_pid == p->pid){
    return -1;
  }

  // find target process in the proc table
  extern struct proc proc[];
  struct  proc *target = 0;
  for (struct proc *t = proc; t < &proc[NPROC]; t++){
    acquire(&t->lock);
    if (t->pid == target_pid){
      // errors
      if (t->state == UNUSED || t->state == ZOMBIE || t->killed){
        release (&t->lock);
        return -1;
      }

      target = t;
      break;
    }
    release(&t->lock);
  }
  
  if (target == 0){
    return -1;
  }
  //target->lock is held here

  //if target is already waiting in co_yield (chan == target is our sentinel)
  if (target->state == SLEEPING && target->chan == (void*)target) {
    target->trapframe->a0 = (uint64)value; // give target our value
    target->state = RUNNABLE;
  }
  release(&target->lock);

  //sleep ourselves, waiting for target to co_yield back to us
  acquire(&p->lock);
  p->chan = (void*)p; // sentinel: I`m sleeping in co_yield
  p->state = SLEEPING;
  sched();            // give up the CPU
  p->chan = 0;
  release(&p->lock);

  // if we were killed while sleeping, return error
  if (killed(p)){
    return -1;
  }

  return (int)p->trapframe->a0; // value set by whoever woke us

}
