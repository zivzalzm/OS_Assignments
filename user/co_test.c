#include "kernel/types.h"
#include "user/user.h"


void test_errors(void){
    printf("---Error condition tests ---\n");

    // (a) yield to a non-existent PID
    int ret = co_yield(9999, 1);
    printf("non-existent PID: %d (expected -1)\n", ret);

    // (b) yield to yourself
    ret = co_yield(getpid(), 1);
    printf("self-yield: %d (expected -1)\n", ret);

    // (c) yield to a killed process
    int pid = fork();
    if (pid == 0){
        exit(0);    // child exits immediately
    }
    wait(0); // wait for child to die
    ret = co_yield(pid, 1);
    printf("killed process: %d (expected -1)\n", ret);
}
void test_exchange(void){
    printf("--- Exchange test ---\n");

    int pid1 = getpid();
    int pid2 = fork();

    if (pid2 < 0){
        printf("fork failed\n");
        exit(1);
    }

    if (pid2 == 0){
        // Child
        for(int i = 0; i < 5 ; i++){
            int value = co_yield(pid1, 1);
            printf("child received: %d\n", value);
        }
        exit(0);
    } else {
        // Parent 
        for(int i = 0; i < 5 ; i++){
            int value = co_yield(pid2, 2);
            printf("parent received: %d\n", value);
        }
        kill(pid2); // wake child from co_yield sleep so it can exit 
        wait(0);
    }
}

int main(void){
    test_errors();
    test_exchange();
    exit(0);
}