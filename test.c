#include "pyroscope_api.h"
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>

int main(int argc, char **argv)
{
    pid_t pid = (pid_t)atoi(argv[1]);
    printf("Application pid is %d\n", pid);

    const int err_len = 200;
    const int data_len = 1000;
    char err_buffer[err_len];
    char data_buffer[data_len];

    phpspy_init(pid, (void*)&err_buffer[0], err_len);
    sleep(1);

    for(int i=0; i < 20; i++)
    {
        int snapshot_len = phpspy_snapshot(pid, (void*)&data_buffer[0], data_len, (void*)&err_buffer[0], err_len);
        if(snapshot_len > 0)
        {
            printf("Data buffer: %s\n", data_buffer);
        }
        if(snapshot_len < 0)
        {
            printf("Error buffer: %s\n", err_buffer);
        }
    }
    phpspy_cleanup(pid, (void*)&err_buffer[0], err_len);
}
