#include <unistd.h>
#include <stdlib.h>
#include <stdio.h>
#include <memory.h>
#include "pyroscope_api.h"

char err[0x1000];
char buf[0x1000];
int main(int argc, char **argv) {
    pid_t p;
    int res;

    if (argc != 2) {
        puts("usage: playground ${pid}");
        return -1;
    }

    p = atoi(argv[1]);
    res = phpspy_init(p, err, sizeof(err));
    printf("phpspy_init: %d\n", res);
    if (res != 0) {
        printf("phpspy_init: %d %s\n", res, err);
        return res;
    }
    while (1) {
        memset(buf, 0, sizeof(buf));
        memset(err, 0, sizeof(err));
        res = phpspy_snapshot(p, buf, sizeof(buf), err, sizeof(err));
        if (res > 0) {
            printf("phpspy_snapshot : %d %s\n", res, buf);
            fflush(stdout);
        }
        if (res < 0) {
            printf("phpspy_snapshot : %d %s\n", res, err);
            break;
        }
    }
    memset(err, 0, sizeof(err));
    res = phpspy_cleanup(p, err, sizeof(err));
    if (res < 0) {
        printf("phpspy_cleanup : %d %s\n", res, err);
    }
    return 0;
}