#include "kernel/types.h"
#include "kernel/stat.h"
#include "user/user.h"

// Test 1: error cases
void
test_errors(void)
{
    printf("=== error cases ===\n");

    int ret;

    ret = co_yield(-1,5);
    printf("co_yield(-1,5)      = %d (supposed -1)\n", ret);

    ret = co_yield(0,5);
    printf("co_yield(0,5)      = %d (supposed -1)\n", ret);

    ret = co_yield(9999,5);
    printf("co_yield(9999,5)      = %d (supposed -1)\n", ret);

    ret = co_yield(getpid(),5);
    printf("co_yield(self,5)      = %d (supposed -1)\n", ret);

    // co_yield to a zombie
    int cpid = fork();
    if(cpid == 0){
        exit(0);
    }
    //give child time to become zombie before parent calls wait()
    sleep(5);
    ret = co_yield(cpid,5);
    printf("co_yield(zombie,5)      = %d (supposed -1)\n", ret);
    wait(0);

    printf("=== error cases done ===\n");
}

// Test 2: co_yield between parent and child
// parent sends 2, child sends 1; each runs 5 rounds
void
test_co_yield(void)
{
    printf("=== co_yield ===\n");

    int parent = getpid();
    int child = fork();

    if(child == 0){
        for(int i = 0; i < 5; i++){
            int val = co_yield(parent, 1);
            printf("child received: %d (supposed 2)\n", val);
        }
        exit(0);
    } else {
        for(int i = 0; i < 5; i++){
            int val = co_yield(child, 2);
            printf("parent received: %d (supposed 1)\n", val);
        }
        wait(0);
    }
    printf(" === co_yield done ===");
}

int 
main (void)
{
    test_errors();
    test_co_yield();
    exit(0);
}