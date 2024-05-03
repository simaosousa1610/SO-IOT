#include <stdio.h>
#include <stdlib.h>
#include <string.h>
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
#include <semaphore.h>
#include <sys/msg.h>
#include <sys/select.h>


#define SENSOR_PIPE_NAME "SENSOR_PIPE"
#define CONSOLE_PIPE_NAME "CONSOLE_PIPE"
#define INTERNAL_QUEUE_NAME "INTERNAL_QUEUE"
#define SEM_SENSOR_PIPE "SEM_SENSOR"
#define SEM_CONSOLE_PIPE "SEM_CONSOLE"
#define SHM_MUTEX "SHM_MUTEX"
#define GO_ALERTS "GO_ALERTS"
#define WORKER_SEM "WORKER_SEM"
#define GO_WORKERS "GO_WORKERS"
#define MESSAGE_SIZE 100
#define mq_key 12345

char msg[MESSAGE_SIZE];
int shmid;
int mqid;

struct sigaction sigint_action;
struct sigaction sigtstp_action;

FILE *fp_config;
FILE *fp_log;

pthread_mutex_t file_lock;

typedef struct _msgbuf {
    long mtype;      
    char mtext[MESSAGE_SIZE];
}msgbuf;

typedef struct{
	int sensor_count;
	int key_count;
	int alert_count;
	int max_sensors;
    int max_keys;
    int max_alerts;
    int workers_in_use;
} Info;

Info *info;

typedef struct {
	char key[100];
    int last_value;
    int min_value;
    int max_value;
    int used_count;
    double average_value;
    int sum;
}Key_Data;

Key_Data* keys;

typedef struct{
    int pid;
	char id[100];
	char key[100];
	int valor_min;
	int valor_max;
}Alert;

Alert* alerts;

typedef struct{
	char id[100];
}Sensor;

Sensor* sensors;

typedef struct{
    char message[100];
    int priority;
}input_t;

typedef struct {
    int available;
    int fd[2];
    pid_t pid;
} worker_t;

worker_t (*workers);

sem_t *console_sem,*sensor_sem;


void write_log(FILE* fp_log, char msg[]);
void worker(key_t shmkey, int index);
void alerts_watcher();
int input_validation(char id[10], char key[20]);


