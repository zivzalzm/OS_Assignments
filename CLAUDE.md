# OS Assignment 1 — Task 3: co_yield System Call
# BGU Course 202.1.3031, Spring 2026 | xv6-riscv

## Project Context
Kernel-level C project based on **xv6-riscv** (MIT's teaching OS).
Runs inside a **Docker dev container**, emulated with **QEMU** on RISC-V.
Tasks 0, 1, 2 are already done. This file focuses entirely on Task 3.

```bash
make qemu       # build and run (exit with Ctrl-A then X)
make clean      # required before submission
```

---

## Goal
Implement `int co_yield(int pid, int value)` — a system call that lets a process
hand off the CPU **directly** to another process by PID, passing an integer value.
The target process receives that value as the return of its own `co_yield` call.

---

## ⚠️ Lecturer Clarifications

### Think from the CPU's perspective, not the process's
There is only 1 CPU. Think about what **the CPU** is doing at each moment.

This is tied to `acquire`/`release`:
- `acquire(&lock)` — **disables interrupts** on this CPU
- `release(&lock)` — **re-enables interrupts**

Use this to prevent the scheduler timer from preempting you mid-handoff.

### Design is the main emphasis
- Define precisely in code what "ready for handoff" means
- Decide which edge cases to handle and which to ignore
- Be consistent — you must justify every decision at the grading session

### Accepted solution tiers
1. ✅ Works correctly indefinitely — best
2. ✅ Works but crashes after several iterations (e.g., `panic: release`) — acceptable
3. ✅ Modifies the scheduler to return to the relevant process — also acceptable

**The provided test code must work.**

---

## Hard Constraints
- ❌ Do NOT modify `struct proc` or add new process states/fields
- ❌ Do NOT add global or process-level kernel data structures
- ✅ `CPUS := 1` in Makefile (already set)
- ✅ Direct process-to-process switching via `swtch()`, bypassing the scheduler

## Return Values
- **Success:** the `value` passed by the partner (always a positive integer)
- **Error:** `-1` for: invalid/zero/negative PID, nonexistent process, killed process, self-yield

## Files to Modify
- `kernel/syscall.h` — add syscall number for `co_yield`
- `kernel/syscall.c` — add dispatch entry
- `kernel/sysproc.c` — implement `sys_co_yield`
- `kernel/proc.c` — core logic: state changes, value passing, direct `swtch()`
- `user/usys.pl` — add userspace stub
- `user/user.h` — add declaration
- `user/co_test.c` — test program (new file — **this is the submission**)

---

## Implementation Order
1. Read `kernel/proc.h` — `struct proc`, process states, `struct context`
2. Read `kernel/proc.c` — focus on `sleep()`, `wakeup()`, `scheduler()`, `swtch()`
3. Read `kernel/spinlock.c` — understand interrupt-disable behavior of `acquire`/`release`
4. **Phase 1:** implement with normal sleep/wakeup (let scheduler manage switching)
   - Focus on: state machine, value passing, defining "readiness"
   - Test with provided test code
5. Verify locking — check sleep/wakeup protocol is followed correctly
6. **Phase 2:** replace with direct `swtch()` to target process, bypassing scheduler
7. Test all edge cases

---

## Test Code (must work)
```c
// co_test.c — normal operation
int pid1 = getpid();
int pid2 = fork();
if (pid2 == 0) {         // Child
    for (;;) {
        int value = co_yield(pid1, 1);
        printf("Child received: %d\n", value); // must print 2
    }
} else {                 // Parent
    for (;;) {
        int value = co_yield(pid2, 2);
        printf("parent received: %d\n", value); // must print 1
    }
}

// Error cases
co_yield(-1, 5);         // invalid PID
co_yield(9999, 5);       // nonexistent PID
co_yield(getpid(), 5);   // self-yield
```

## Key Design Questions (decide and document each)
- What if A calls `co_yield(B, val)` but B hasn't called `co_yield` yet? → A sleeps and waits
- What if the target process is killed while A is sleeping?
- After the handoff, is the yielding process SLEEPING or RUNNABLE?
- Who is responsible for waking up the sleeping partner?

---

## Key Files Reference
```
kernel/proc.h       — struct proc, process states, struct context
kernel/proc.c       — scheduler(), sleep(), wakeup(), swtch()
kernel/syscall.h    — syscall numbers
kernel/syscall.c    — syscall dispatch table
kernel/sysproc.c    — process syscall implementations (add sys_co_yield here)
kernel/spinlock.c   — acquire()/release() — disables/enables interrupts
user/user.h         — userspace syscall declarations
user/usys.pl        — generates syscall stubs
Makefile            — CPUS := 1, add _co_test to UPROGS
```

## Process States
```c
enum procstate { UNUSED, USED, SLEEPING, RUNNABLE, RUNNING, ZOMBIE };
```

## Critical Patterns
```c
struct proc *p = myproc();          // current process

acquire(&p->lock);                  // DISABLES interrupts
release(&p->lock);                  // re-enables interrupts

sleep(chan, &lock);                 // sleep on channel, atomically releases lock
wakeup(chan);                       // wake all sleeping on this channel

swtch(&c->context, &p->context);   // scheduler → process
swtch(&p->context, &c->context);   // process → scheduler (normal yield)
swtch(&current->context, &target->context); // direct process-to-process
```

## How swtch() Works
`swtch(old, new)` saves callee-saved registers into `old`, loads from `new`.
The switched-away process resumes from exactly where it called `swtch()` when
someone switches back to it. The scheduler uses this to enter/exit processes.

---

## Submission Checklist
- [ ] `CPUS := 1` in Makefile
- [ ] `make clean` before packing
- [ ] `co_test.c` included
- [ ] All modified kernel files included
- [ ] Single `.tar.gz` or `.zip`, submitted via Moodle in pairs
