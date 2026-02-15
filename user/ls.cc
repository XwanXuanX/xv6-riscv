#include "kernel/stats.h"
#include "user/user.h"
#include "kernel/fs.h"
#include "kernel/fcntl.h"
#include <array>
#include <span>

const char *fmtname(const char *path) {
    static std::array<char, DIRSIZ + 1> buf{};
    const char *p;

    // Find first character after last slash.
    for (p = path + strlen(path); p >= path && *p != '/'; p--)
        ;
    p++;

    // Return blank-padded name.
    if (strlen(p) >= DIRSIZ) {
        return p;
    }
    memmove(buf.data(), p, static_cast<int>(strlen(p)));
    memset(buf.data() + strlen(p), ' ', DIRSIZ - strlen(p));
    buf[sizeof(buf) - 1] = '\0';
    return buf.data();
}

void ls(const char *path) {
    std::array<char, 512> buf{};
    char *p;
    int fd;
    xv6::dirent de{};
    stats st{};

    if ((fd = open(path, O_RDONLY)) < 0) {
        fprintf(2, "ls: cannot open %s\n", path);
        return;
    }

    if (fstat(fd, &st) < 0) {
        fprintf(2, "ls: cannot stat %s\n", path);
        close(fd);
        return;
    }

    switch (st.type) {
    case T_DEVICE:
    case T_FILE:
        printf("%s %d %d %d\n", fmtname(path), st.type, st.ino,
               static_cast<int>(st.size));
        break;

    case T_DIR:
        if (strlen(path) + 1 + DIRSIZ + 1 > sizeof buf) {
            printf("ls: path too long\n");
            break;
        }
        strcpy(buf.data(), path);
        p = buf.data() + strlen(buf.data());
        *p++ = '/';
        while (read(fd, &de, sizeof(de)) == sizeof(de)) {
            if (de.inum == 0) {
                continue;
            }
            memmove(p, de.name.data(), DIRSIZ);
            p[DIRSIZ] = 0;
            if (stat(buf.data(), &st) < 0) {
                printf("ls: cannot stat %s\n", buf.data());
                continue;
            }
            printf("%s %d %d %d\n", fmtname(buf.data()), st.type, st.ino,
                   static_cast<int>(st.size));
        }
        break;
    default:;
    }
    close(fd);
}

int main(const int argc, const std::span<char *> argv) {
    if (argc < 2) {
        ls(".");
        exit(0);
    }
    for (int i = 1; i < argc; i++) {
        ls(argv[i]);
    }
    exit(0);
}
