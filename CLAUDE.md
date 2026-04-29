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
Implement `int co_yield(int pid, int value)` — a system call that hands the CPU
**directly** to another process by PID, passing an integer value.
The target receives that value as the return of its own `co_yield` call.

---

## ⚠️ Lecturer & TA Clarifications

### 1. Think from the CPU's perspective, not the process's
There is only 1 CPU. Ask: "what is the CPU executing at each moment?"

`acquire(&lock)` disables interrupts. `release(&lock)` re-enables them.
Use this to protect the handoff from being interrupted by the timer.

### 2. Do NOT call sched() inside co_yield — this is the core constraint
In xv6, the normal preemptive path is:
```
process → yield() → sched() → swtch(&p->context, &cpu->scheduler) → scheduler loop
```
`sched()` always returns to the scheduler. Calling it inside `co_yield`
contradicts "bypass the scheduler". The correct approach:

```
co_yield: swtch(&current->context, &target->context)  // direct, no scheduler involved
```

Skip RUNNABLE entirely. Set the target to RUNNING, switch directly to it.

### 3. Accepted solution tiers (from email)
| Tier | Description | Uses sched()? |
|------|-------------|---------------|
| ✅ Best | direct swtch, no scheduler involvement, works indefinitely | No |
| ✅ Acceptable | uses sched(), crashes after N iterations (panic: release) | Yes |
| ✅ Also acceptable | modifies the scheduler to return to the right process | Yes + scheduler change |

**The provided test code must work in all cases.**

### 4. Design is heavily weighted
- Define precisely in code what "ready for handoff" means
- Decide which edge cases to handle or ignore — be consistent
- Document every decision — you must justify it at the grading session

---

## Hard Constraints
- ❌ Do NOT modify `struct proc` or add new process states/fields
- ❌ Do NOT add global or process-level kernel data structures
- ❌ Do NOT call `sched()` inside `co_yield` (for the ideal solution)
- ✅ `CPUS := 1` in Makefile
- ✅ Direct `swtch()` from current process to target, skipping RUNNABLE

## Return Values
- **Success:** the `value` passed by the partner (always positive)
- **Error:** `-1` — invalid/zero/negative PID, nonexistent, killed, self-yield

---

## Agreed Design (finalized — do not re-derive)

### Value Passing — two borrowed fields, no struct proc modification

**`p->xstate` — outgoing value slot**
When a process goes to sleep inside `co_yield`, it writes the value it is
offering into its own `xstate` field. `xstate` is documented as "exit status
for parent's wait()" and is only meaningful when a process is ZOMBIE. A process
sleeping in co_yield is not a zombie, so the field is safely idle.

**`target->trapframe->a0` — incoming value slot**
`syscall.c` line 141 does: `p->trapframe->a0 = syscalls[num]();`
This means whatever `sys_co_yield` returns becomes the user-visible return value.
The waker writes `sleeper->trapframe->a0 = val` before switching to the sleeper.
The sleeper's `sys_co_yield` then reads back `myproc()->trapframe->a0` and returns
it. `syscall()` writes the same value back — no corruption. Net effect: the
sleeper sees the waker's value.

**Local stack variable for Case 1**
When A wakes B and then immediately sleeps (Case 1), A already knows the return
value (`B->xstate`). A captures it in a local `int ret` on its kernel stack
before sleeping. `swtch()` saves/restores `sp`, so the stack frame is frozen and
`ret` is intact on resume. A returns `ret`, not `trapframe->a0`, in this path.

### Readiness — the "co_yield channel" convention

A process is ready for co_yield handoff if and only if:
```c
target->state == SLEEPING  &&  target->chan == (void *)target
```
We use the process's own address (`&proc`) as the sleep channel. No other sleep
in xv6 uses this channel: `sys_sleep` uses `&ticks`, `wait()` uses `p` (parent),
pipes use `&pipe->fields`. Our channel is unambiguous and requires no new fields.

### State machine

```
Normal running:  state == RUNNING
Waiting in co_yield: state == SLEEPING, chan == &proc, xstate == offered value
Being handed the CPU: state set to RUNNING by the waker, then swtch to it
```
RUNNABLE is never used. The waker sets target directly to RUNNING before swtch.

### Case 1 — target is already sleeping (the steady-state path)

```
Precondition: target->state==SLEEPING && target->chan==&target && !target->killed

1.  acquire(&target->lock)
2.  verify precondition (return -1 if not met)
3.  int ret = (int)(uint64)target->xstate    // capture B's value on our stack
4.  target->trapframe->a0 = (uint64)val      // our value → B's return register
5.  target->chan  = 0
6.  target->state = RUNNING                  // skip RUNNABLE, direct to RUNNING
7.  release(&target->lock)                   // MUST release before swtch (noff=0)

8.  acquire(&myproc()->lock)                 // now noff=1, exactly one lock
9.  myproc()->xstate = val                   // store our value for next partner
10. myproc()->chan   = (void *)myproc()       // mark us as waiting for co_yield
11. myproc()->state = SLEEPING
12. mycpu()->proc   = target                 // update CPU's current-process pointer
13. intena = mycpu()->intena                 // save interrupt-enable state
14. swtch(&myproc()->context, &target->context)  // direct handoff, noff=1 ✓
    ← we resume here when target calls swtch back to us
15. mycpu()->intena = intena
16. myproc()->chan  = 0
17. release(&myproc()->lock)
18. return ret                               // local variable, not trapframe->a0
```

**Why exactly one lock at swtch:** when the target resumes from its own swtch,
the CPU's `noff` counter reflects however many locks were held at the time of
switch. If we hold two locks (ours + target's), noff=2 when target resumes.
Target releases its own lock (noff→1), but our lock is now "orphaned" on the CPU.
Interrupts stay disabled in target's user space. Eventually noff becomes corrupt
and causes "panic: release". Releasing target->lock before acquiring our own
ensures noff=1 at the moment of swtch.

### Case 2 — target is not sleeping yet (startup only, happens once)

Case 2 only occurs once: the first co_yield call before the partner has ever
entered co_yield. After the first successful handshake, one partner is always
SLEEPING when the other calls co_yield, so Case 1 handles all subsequent calls.

For Case 2, A cannot switch directly to B because B's kernel context is at an
unknown location (user space or fresh forkret). A sleeps and waits for B to
call co_yield(A,...), which will be a Case 1 call from B's perspective.

A sleeps by switching to the scheduler context WITHOUT calling sched():
```
8.  acquire(&myproc()->lock)                // noff=1
9.  myproc()->xstate = val
10. myproc()->chan   = (void *)myproc()
11. myproc()->state = SLEEPING
12. intena = mycpu()->intena
13. swtch(&myproc()->context, &mycpu()->context)  // to scheduler, not sched()
    ← scheduler will release our lock (normal scheduler cleanup)
    ← scheduler picks B (RUNNABLE), B eventually calls co_yield(A) → Case 1
    ← when scheduler picks us again, we resume here with our lock re-held
14. mycpu()->intena = intena
15. myproc()->chan  = 0
16. release(&myproc()->lock)
17. return (int)myproc()->trapframe->a0     // written by whoever woke us
```

### Edge cases

**Return -1 immediately (before any sleeping):**
- `pid <= 0`
- `pid == myproc()->pid` (self-yield)
- No live process with that pid (UNUSED slot or not found)
- `target->state == ZOMBIE`
- `target->killed == 1`

**Explicitly not handled (documented limitation):**
- Target is killed WHILE caller is sleeping → caller hangs. Fixing this requires
  storing "who is waiting for me" which needs a struct proc field. Out of scope.
- Three-way co_yield chains → undefined behavior.

---

## Implementation Status

### ✅ Done
| File | Change | Commit |
|------|--------|--------|
| `kernel/syscall.h` | `#define SYS_co_yield 22` | register co_yield syscall number and dispatch entry |
| `kernel/syscall.c` | `extern uint64 sys_co_yield(void);` + dispatch table entry | register co_yield syscall number and dispatch entry |
| `user/usys.pl` | `entry("co_yield");` | add co_yield userspace stub and declaration |
| `user/user.h` | `int co_yield(int, int);` | add co_yield userspace stub and declaration |

### 🔲 Next — in order
1. `kernel/defs.h` — add `int do_co_yield(int, int);` under the proc.c section
2. `kernel/proc.c` — add `do_co_yield()` function (the full logic above)
3. `kernel/sysproc.c` — add `sys_co_yield()` (argument parsing, calls do_co_yield)
4. `user/co_test.c` — create test program (new file)
5. `Makefile` — add `$U/_co_test` to UPROGS

---

## Working Preferences (for Claude — follow exactly)

1. **Tell me the exact file and line number** to edit. Never make changes yourself.
2. **Show exactly what to write**, including professional English comments in the code.
3. **Explain in 2-3 lines WHY the code works** — not just what it does.
4. **Wait for my confirmation** before moving to the next change.
5. **Before each commit**: write the commit message suggestion in English,
   then also write a short summary in Hebrew of what changed and why.
6. **One logical unit at a time**: group related changes (e.g. all syscall
   registration files) into one commit, then pause before the next unit.

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

// Error cases (must return -1):
co_yield(-1, 5);         // invalid PID
co_yield(9999, 5);       // nonexistent PID
co_yield(getpid(), 5);   // self-yield
// also test: yield to a killed process
```

---

## Key Files Reference
```
kernel/proc.h       — struct proc, process states, struct context
kernel/proc.c       — scheduler(), sleep(), wakeup(), swtch(), sched(), yield()
kernel/syscall.h    — syscall numbers (SYS_co_yield = 22)
kernel/syscall.c    — syscall dispatch table
kernel/sysproc.c    — add sys_co_yield here (argument parsing)
kernel/defs.h       — kernel-internal function declarations (add do_co_yield)
kernel/spinlock.c   — acquire()/release() — disables/enables interrupts
user/user.h         — userspace syscall declarations
user/usys.pl        — generates syscall stubs
Makefile            — CPUS := 1, add _co_test to UPROGS
```

## Process States
```c
enum procstate { UNUSED, USED, SLEEPING, RUNNABLE, RUNNING, ZOMBIE };
// co_yield skips RUNNABLE — goes directly SLEEPING → RUNNING
```

---

## Submission Checklist
- [ ] `CPUS := 1` in Makefile
- [ ] `make clean` before packing
- [ ] `co_test.c` included and works
- [ ] All modified kernel files included
- [ ] Code is commented — explain every design decision
- [ ] Single `.tar.gz` or `.zip`, submitted via Moodle in pairs
