// Demonstrate that moving the "acquire" in iderw after the loop that
// appends to the idequeue results in a race.

// For this to work, you should also add a spin within iderw's
// idequeue traversal loop.  Adding the following demonstrated a panic
// after about 5 runs of stressfs in QEMU on a 2.1GHz CPU:
//    for (i = 0; i < 40000; i++)
//      asm volatile("");

#include "user/user.h"
#include "kernel/fcntl.h"

#include <array>

int main() {
    int i;
    std::array<char, 10> path = {"stressfs0"};
    std::array<char, 512> data{};

    printf("stressfs starting\n");
    memset(data.data(), 'a', sizeof(data));

    for (i = 0; i < 4; i++) {
        if (fork() > 0) {
            break;
        }
    }

    printf("write %d\n", i);

    path[8] += i;
    int fd = open(path.data(), O_CREATE | O_RDWR);
    for (i = 0; i < 20; i++) {
        //    printf(fd, "%d\n", i);
        write(fd, data.data(), sizeof(data));
    }
    close(fd);

    printf("read\n");

    fd = open(path.data(), O_RDONLY);
    for (i = 0; i < 20; i++) {
        read(fd, data.data(), sizeof(data));
    }
    close(fd);

    wait(nullptr);

    exit(0);
}
