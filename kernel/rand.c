#include "types.h"
#include "param.h"
#include "spinlock.h"
#include "defs.h"

// only this file can call lcg_rand and lcg_srand, so we don't need to worry about concurrent access to lcg_state
static struct spinlock lcg_lock; // protects lcg_state
static uint lcg_state = 0;       // the current state of the generator

// called by main() in main.c to initialize the random number generator
void
lcg_randinit(void)
{
  initlock(&lcg_lock, "lcg");
}

// sets the seed for the random number generator  - must hold the lock
void 
lcg_srand(uint seed)
{
    acquire(&lcg_lock);
    lcg_state = seed;
    release(&lcg_lock);
}

// advances the generator and returns the next random number
// formula: x_n+1 = (1664525 * x_n + 1013904223) % 2^32
// the modulus is implicit because we are using 32-bit unsigned integers
uint
lcg_rand(void)
{
    uint result;
    acquire(&lcg_lock);
    lcg_state = lcg_state * 1664525 + 1013904223;
    result = lcg_state;  // save before releasing the lock = another CPU could call lcg_srand and change lcg_state after we release the lock
    release(&lcg_lock);
    return result;
}

