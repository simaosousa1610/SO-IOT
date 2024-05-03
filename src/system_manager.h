#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <string.h>
#include <errno.h>
#include <signal.h>
#include <sys/mman.h>
#include <sys/fcntl.h>
#include <sys/shm.h>
#include <sys/ipc.h>
#include <fcntl.h>
#include <sys/stat.h>
#include <pthread.h>
#include <time.h>

#define SENSOR_PIPE_NAME "SENSOR_PIPE"
#define CONSOLE_PIPE_NAME "CONSOLE_PIPE"
#define MESSAGE_SIZE 100

struct sigaction sa;

typedef struct {
    int sensor_id;
    int value;
} mem_struct;

void write_log(FILE* fp_log, char message[]);
