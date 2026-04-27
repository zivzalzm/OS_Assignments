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
  argint(0, &target_pid);
  argint(1, &value);

  struct proc *p = myproc();

  // Validate arguments: target must be a different, existing process
  // and value must be a positive integer.
  if (target_pid <= 0 || target_pid == p->pid)
    return -1;

  if (value <= 0)
    return -1;

  // Walk the global proc table to find the target.
  // We hold each entry's spinlock while inspecting it so the state we
  // read is stable — fork() and exit() modify state under that same lock.
  // If we find the target we keep its lock held on exit from the loop.
  extern struct proc proc[];
  struct proc *target = 0;
  for (struct proc *t = proc; t < &proc[NPROC]; t++) {
    acquire(&t->lock);
    if (t->pid == target_pid) {
      // Reject any process that is not safe to yield to:
      //   UNUSED / ZOMBIE / killed  — already dead or never alive.
      //   USED                      — still being initialized by allocproc().
      //   SLEEPING on foreign chan  — blocked on pipe/disk/wait; waking it
      //                               here would corrupt that protocol.
      if (t->state == UNUSED ||
          t->state == USED   ||
          t->state == ZOMBIE ||
          t->killed          ||
          (t->state == SLEEPING && t->chan != (void*)t)) {
        release(&t->lock);
        return -1;
      }
      target = t;
      break;
    }
    release(&t->lock);
  }

  if (target == 0)
    return -1;

  // target->lock is held here (noff == 1, interrupts are disabled).
  struct cpu *c = mycpu();

  if (target->state == SLEEPING && target->chan == (void*)target) {
    // -----------------------------------------------------------------
    // DIRECT SWITCH PATH
    //
    // target is already blocked inside co_yield, recognised by the
    // sentinel chan == target (a self-pointer that no other kernel path
    // ever uses as a sleep channel).  We switch straight to it without
    // going through the scheduler.
    // -----------------------------------------------------------------

    // Deposit our value into target's a0 so that when target resumes
    // and returns from its co_yield syscall, it receives this value.
    target->trapframe->a0 = (uint64)value;

    // Put ourselves to sleep before handing the CPU away.
    // We use our own process pointer as the sleep channel (same sentinel
    // convention) so a future caller scanning the proc table can identify
    // us as "waiting in co_yield".
    // Interrupts are already off because we hold target->lock (push_off),
    // and CPUS=1 means no other CPU can race on these fields, so the
    // standard acquire(p->lock) is not required here.
    p->state = SLEEPING;
    p->chan  = (void*)p;

    // Declare target as the running process on this CPU so that trap
    // handlers and mycpu() see the correct current process after the switch.
    target->state = RUNNING;
    c->proc       = target;

    // swtch() saves our callee-saved registers into p->context and loads
    // target->context.  This function resumes only when some future caller
    // switches back to us using our context.
    // target->lock (noff == 1) is transferred to target: it resumes
    // holding it, clears its chan, and then releases it.
    int intena = c->intena;
    swtch(&p->context, &target->context);
    c->intena = intena;

    // We are back.  Whoever switched to us held p->lock and transferred
    // it here, mirroring exactly what we did for target above.
    p->chan = 0;
    release(&p->lock);

  } else {
    // -----------------------------------------------------------------
    // SCHEDULER PATH
    //
    // target is RUNNABLE but has not yet called co_yield, so there is no
    // rendezvous point to deliver our value right now.  We release its
    // lock and put ourselves to sleep via the normal scheduler; when
    // target eventually calls co_yield(us, ...) it will find us sleeping
    // on our own chan and take the direct-switch path above.
    // -----------------------------------------------------------------
    release(&target->lock);

    // Standard xv6 sleep-via-scheduler protocol:
    // acquire p->lock, change state to SLEEPING, call sched().
    // sched() switches to the scheduler context; the scheduler releases
    // p->lock and later re-acquires it before switching back to us.
    acquire(&p->lock);
    p->chan  = (void*)p;
    p->state = SLEEPING;
    sched();
    p->chan = 0;
    release(&p->lock);
  }

  // If we were killed while sleeping, report the error to user space.
  if (killed(p))
    return -1;

  // Return the value that whoever woke us deposited into our trapframe a0.
  return (int)p->trapframe->a0;
}
