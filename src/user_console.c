//
// Gabriel Suzana Ferreira 2021242097
// Simão Cabral Sousa 2020226115
//

#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include "dados.h"

int fd_console_pipe;

// Vetor de alertas
int n_alerts = 0;
pthread_t thread_id;
char message[100];

volatile sig_atomic_t sigint_received = 0;


void signal_handler_sigint() {
    printf("\nSIGINT RECEIVED\n");
    sigint_received = 1;
}

void clean_resources(){
    close(fd_console_pipe);
}

void* alert_mgs_reader(){
    msgbuf alert_rcv;

    while(!sigint_received){
        if((msgrcv(mqid,&alert_rcv,sizeof(alert_rcv),getpid()+99999,0)) < 0){
            printf("\n\nHOME_IOT TERMINATED\n");
                return NULL;
        }
        printf("\n\n%s\n",alert_rcv.mtext);
    }
    return NULL;
}

int main(int argc, char** argv) {
    // Verifica se o identificador da consola foi fornecido
    if (argc < 2) {
        printf("Erro: Identificador da consola não fornecido.\n");
        return 1;
    }

    struct sigaction sigint_action;
	sigint_action.sa_handler = signal_handler_sigint;
	sigemptyset(&sigint_action.sa_mask);
	sigint_action.sa_flags = 0;
	if (sigaction(SIGINT, &sigint_action, NULL) == -1) {
		perror("sigaction");
		exit(EXIT_FAILURE);
	}
    
    if ((mqid = msgget(mq_key,0)) == -1){
		perror("ERROR IN MSGGET MESSAGE QUEUE");
		exit(0);
    }
	console_sem=sem_open(SEM_CONSOLE_PIPE,O_RDWR); // create semaphore
	if(console_sem==SEM_FAILED){
		perror("Failure creating the semaphore FULL");
		exit(0);
	}
    
  	if ((fd_console_pipe = open(CONSOLE_PIPE_NAME,O_RDWR))<0){
		perror("ERROR OPENING CONSOLE NAMED PIPE"); 
		exit(0);
  	}  

    msgbuf to_receive;


    long id=0;
    pthread_create(&thread_id,NULL,alert_mgs_reader,(void*)&id);
   
    char *console_id = argv[1];
    printf("Bem-vindo ao User Console %s.\n", console_id);

    // Loop principal do menu
    while(!sigint_received) {
        printf("\n");
        printf("MENU COMANDOS DISPONÍVEIS\n");
        printf("exit -> Sai do User Console.\n");
        printf("stats -> Apresenta estatísticas referentes aos dados enviados pelos sensores.\n");
        printf("reset -> Limpa todas as estatísticas calculadas até ao momento pelo sistema (relativa a todos os sensores, criados por qualquer User Console).\n");
        printf("sensors -> Lista todos os Sensors que enviaram dados ao sistema.\n");
        printf("add_alert [id] [chave] [min] [max] -> Adiciona uma nova regra de alerta ao sistema.\n");
        printf("remove_alert [id] -> Remove uma regra de alerta do sistema.\n");
        printf("list_alerts -> Lista todas as regras de alerta que existem no sistema.\n");

        // Lê o input do usuário
        char input[100];
        printf("Digite um comando: ");
        fgets(input, 100, stdin);
        //flush unread input buffer
        while( !strchr( input, '\n' ) )
            if( !fgets(input,(sizeof input),stdin) )
                break;

        // Remove o caractêr '\n' do final da string
        int len = strlen(input);
        if (input[len-1] == '\n') {
            input[len-1] = '\0';
        }

        if(strcmp(input,"exit")==0){
            sigint_received=1;
            break;
        }

        if(!sigint_received){
            sem_wait(console_sem);
            sprintf(message,"%d#",getpid());
            strcat(message,input);
            printf("%s\n",message);
            // escreve a mensagem no named pipe
            write(fd_console_pipe, message, strlen(message)+1);
            sem_post(console_sem);
            
            msgrcv(mqid,&to_receive,sizeof(to_receive),getpid(),0);
            
            printf("\n\n%s\n",to_receive.mtext); 
        }
    }
    pthread_cancel(thread_id);
    pthread_join(thread_id,NULL);
    clean_resources();
    printf("EXITING CONSOLE\n");
    exit(0);
}

