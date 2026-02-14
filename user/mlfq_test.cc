#include "user/user.h"

int main(void) {
    // Create 9 children + parent = 10 CPU-bound processes total.
    for (int i = 0; i < 9; i++) {
        int pid = fork();
        if (pid < 0) {
            printf("fork failed at i=%d\n", i);
            exit(1);
        }
        if (pid == 0) {
            // child: spin forever
            for (;;)
                ;
        }
        // parent continues the loop to fork more children
    }

    // parent also spins forever
    for (;;)
        ;

    // unreachable, but keeps compiler happy
    exit(0);
}
