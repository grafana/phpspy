#include "pyroscope_api.h"
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <string.h>
#include <rte_eal.h>

int main(int argc, char **argv)
{
    const int err_len = 200;
    const int data_len = 2000;
    char err_buffer[err_len];
    char data_buffer[data_len];

    memset(&err_buffer[0], 0, err_len);
    memset(&data_buffer[0], 0, data_len);
    pid_t pid = (pid_t)atoi(argv[1]);
    printf("Application pid is %d\n", pid);

    rte_eal_init(argc, argv);

    for(int k = 0; k < 10; k++)
    {
        phpspy_init(pid, (void*)&err_buffer[0], err_len);

        int snapshot_len = phpspy_snapshot(pid, (void*)&data_buffer[0], data_len, (void*)&err_buffer[0], err_len);
        if(snapshot_len > 0)
        {
            printf("Data buffer: %s\n", data_buffer);
        }
        if(err_buffer[0] != '\0')
        {
            printf("Error buffer: %s\n", err_buffer);
        }
        phpspy_cleanup(pid, (void*)&err_buffer[0], err_len);
    }
}
