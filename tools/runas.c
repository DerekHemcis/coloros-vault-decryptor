#include <unistd.h>
#include <stdio.h>
#include <stdlib.h>
#include <grp.h>

int main(int argc, char **argv) {
    if (argc < 3) {
        fprintf(stderr, "usage: runas <uid> <cmd> [args...]\n");
        return 2;
    }
    uid_t uid = (uid_t)atoi(argv[1]);
    if (setgroups(0, NULL) != 0) perror("setgroups");
    if (setgid(uid) != 0) perror("setgid");
    if (setuid(uid) != 0) perror("setuid");
    fprintf(stderr, "[runas] now uid=%d euid=%d\n", getuid(), geteuid());
    execv(argv[2], &argv[2]);
    perror("execv");
    return 1;
}
