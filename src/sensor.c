#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <time.h>
#include <signal.h>
#include <ctype.h>
#include <semaphore.h>
#include "dados.h"


int num_messages_sent = 0;
int sleep_time = 0;
volatile sig_atomic_t sigtstp_received = 0;
int fd_sensor_pipe;



void clean_resources(){
    close(fd_sensor_pipe);
}

void signal_handler_sigtstp() {
    printf("NUMBER OF MESSAGES SENT: %d\n", num_messages_sent);
    fflush(stdout);

    printf("PRESS ENTER TO CONTINUE...");
    while(getchar() != '\n');
}

void signal_handler_sigint() {
    printf("SENSOR PROCESS TERMINATED\n");
    clean_resources();
    exit(0);
}

int main(int argc, char *argv[]) {
    char id[10];
    char key[20];
    int min_value, max_value;
    float interval;

    if (argc != 6) {
        printf("Uso: sensor {identificador do sensor} {intervalo entre envios em segundos (>=0)} {chave} {valor inteiro mínimo a ser enviado} {valor inteiro máximo a ser enviado}\n");
        exit(1);
    }

    // set up the signals handlers
    sigint_action.sa_handler = signal_handler_sigint;
    sigemptyset(&sigint_action.sa_mask);
    sigint_action.sa_flags = SA_SIGINFO;

    sigtstp_action.sa_handler = signal_handler_sigtstp;
    sigemptyset(&sigtstp_action.sa_mask);
    sigtstp_action.sa_flags = SA_SIGINFO;

    sigaction(SIGINT, &sigint_action, NULL);
    sigaction(SIGTSTP, &sigtstp_action,NULL);
	
	sensor_sem=sem_open(SEM_SENSOR_PIPE,0);
	if(sensor_sem==SEM_FAILED){
		perror("Failure opening the semaphore SENSOR SEM");
		exit(0);
	}

	if ((fd_sensor_pipe = open(SENSOR_PIPE_NAME,O_WRONLY))<0){
        perror("ERROR OPENING SENSOR NAMED PIPE TO WRITE");
        exit(0);
    }

    strcpy(id, argv[1]);
    interval = atof(argv[2]);
    strcpy(key, argv[3]);
    min_value = atoi(argv[4]);
    max_value = atoi(argv[5]);

    int error_gate = input_validation(id,key);

    if (error_gate == 1){
        perror("O programa encerrou por erro referente a inputs");
        exit(1);
    }

    char message[50];
    srand(time(NULL)); // inicializa o gerador de números aleatórios

    while(1) {
        // gera um valor aleatório dentro do intervalo especificado
        int value = rand() % (max_value - min_value + 1) + min_value;

        sem_wait(sensor_sem);
        // monta a mensagem a ser enviada para o named pipe
        snprintf(message, sizeof(message), "%s#%s#%d", id, key, value);
		printf("%s\n",message);
        // escreve a mensagem no named pipe

        write(fd_sensor_pipe, message, strlen(message)+1);

        //incremento da variavél global para ser printado aquando o sinal SIGTSTP é recebido
        num_messages_sent += 1;

        sem_post(sensor_sem);

        struct timespec req, rem;
        req.tv_sec = (time_t)interval;  // integer part of seconds
        req.tv_nsec = (long)((interval - (double)req.tv_sec) * 1e9);  // fractional part in nanoseconds

    if (nanosleep(&req, &rem) != 0) {
        // handle error
    }
        // espera o intervalo especificado antes de enviar o próximo valor
        sleep(interval);
    }
    pause();
    return 0;
}
