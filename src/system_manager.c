// Gabriel Suzana Ferreira 2021242097
// Simão Cabral Sousa 2020226115
//

#include "dados.h"

// Initialize global variables
int queue_sz, n_workers, max_keys, max_sensors, max_alerts;
int fd_sensor_pipe, fd_console_pipe;
pid_t id_alerts_watcher;
pthread_t threads[2];
char *linha = NULL;
char message[MESSAGE_SIZE];
int number_chars = 0;
worker_t (*workers); // Pointer to array of workers
input_t (*internal_queue); // Pointer to array representing the internal queue
sem_t *full, *empty, *mutex, *mutex_shm, *go_alerts, *worker_sem; // Pointers to semaphores
int iq_count = 0;
char to_log [256];
key_t shmkey;
void *shm_ptr;
fd_set read_set;

volatile sig_atomic_t sigint_received = 0;

int main_pid = 0;

// Signal handler for SIGINT
void sigint_handler() {
    sigint_received = 1;
}

// Function to clean all resources upon exit
void clean_resources() {
	
	free(internal_queue);

	// Close pipes to workers
    for(int i = 0; i < n_workers; i++){
    	close(workers[i].fd[0]);
    	close(workers[i].fd[1]);
    }

	// Detach from shared memory segments
	shmdt(info);
	shmdt(sensors);
    shmdt(keys);
    shmdt(alerts);
	shmdt(workers);

	// Remove shared memory segments
    if (shmid >= 0)
		shmctl(shmid, IPC_RMID, NULL);

	// Remove message queue
	if(mqid >= 0)
    	msgctl(mqid, IPC_RMID, NULL);

	// Close pipes to sensor and console processes
    close(fd_sensor_pipe);
    close(fd_console_pipe);
	
	// Remove named pipes for sensor and console processes
	unlink(SENSOR_PIPE_NAME);
	unlink(CONSOLE_PIPE_NAME);

	sem_destroy(empty);
	sem_destroy(full);
	sem_destroy(mutex);

	sem_destroy(mutex_shm);
	sem_destroy(sensor_sem);
	sem_destroy(console_sem);
	sem_destroy(go_alerts);
	sem_destroy(worker_sem);
	
	unlink(SHM_MUTEX);
	unlink(SEM_SENSOR_PIPE);
	unlink(SEM_CONSOLE_PIPE);
	unlink(GO_ALERTS);
	unlink(WORKER_SEM);


	// Write a log entry indicating that all resources were cleaned
	write_log(fp_log, "ALL RESOURCES CLEANED");  
	fclose(fp_log);
	
	// Destroy the file lock and semaphores
	pthread_mutex_destroy(&file_lock);
}

void erro_handler(char msg[]){
	write_log(fp_log,msg); 
	write_log(fp_log,"erro HOME_IOT SIMULATOR CLOSING");
	clean_resources(); // call to clean resources function
	exit(0);
}

/*
 * Function to shutdown all processes and clean resources.
 */
void shutdown_all(){
	// Cancel all threads
	for (int i = 0; i < 3 ; i++){
		pthread_cancel(threads[i]);
		pthread_join(threads[i],NULL);
	}
	
	// Send SIGINT signal to all worker processes
	for(int i=0;i < n_workers;i++){
		if(kill(workers[i].pid,SIGINT) == -1){
			erro_handler("CAN'T KILL WORKER PROCESS");
		}
	}

	// Send SIGKILL signal to alerts_watcher process
	if(kill(id_alerts_watcher,SIGKILL) == -1){
		erro_handler("CAN'T KILL ALERTS_WATCHER PROCESS");
	}

	// Wait for all child processes to exit
	while (wait(NULL) != -1);

	// Write closing message to log file
	write_log(fp_log,"HOME_IOT SIMULATOR CLOSING");

	// Clean resources
	clean_resources();
}




void* sensor_reader(/*void *t*/){
	// Define signal handler for SIGINT signal
	struct sigaction sigint_action;
    sigint_action.sa_handler = sigint_handler;
    sigemptyset(&sigint_action.sa_mask);
    sigint_action.sa_flags = 0;
    if (sigaction(SIGINT, &sigint_action, NULL) == -1) {
        perror("sigaction");
        exit(EXIT_FAILURE);
    }

	input_t *temp = (input_t *)malloc(sizeof(input_t));

    // Continuously read sensor messages until SIGINT signal is received
    while(!sigint_received){

        // Set up read set for select system call
        FD_ZERO(&read_set);
        FD_SET(fd_sensor_pipe,&read_set);

        // Wait for input on sensor pipe
        if (select(fd_sensor_pipe+1,&read_set,NULL,NULL,NULL)>0){
            if(FD_ISSET(fd_sensor_pipe,&read_set)){

                // Read sensor message from pipe
                number_chars = read(fd_sensor_pipe,message,sizeof(message));
                message[number_chars - 1] = '\0';

                // Check if internal queue has space to store the message
                if(queue_sz>iq_count+1){
                    
                    // Acquire empty and mutex semaphores before adding message to internal queue
                    sem_wait(empty);
                    sem_wait(mutex);

                    // Create a temporary input struct to store the message and priority
                    strcpy(temp->message, message);
                    temp->priority = 2;
                    
                    // Add the message to the internal queue and free the temporary input struct
                    internal_queue[iq_count] = *temp;                    
                    iq_count++;
                    
                    // Release the mutex and full semaphores after adding message to internal queue
                    sem_post(mutex);
                    sem_post(full);  
                }             	 	
                else{
                    // Log a message if the internal queue is full
                    strcpy(to_log,"INTERNAL QUEUE IS FULL, MESSAGE: ");
                    strcat(to_log,message);
                    write_log(fp_log,to_log);
                }         
            }
        }   
    }
	free(temp);
    // Clean up resources and exit the thread
    close(fd_sensor_pipe);
    return NULL;
}


void* console_reader(/*void *t*/){

    // Set up signal handler for SIGINT
    struct sigaction sigint_action;
    sigint_action.sa_handler = sigint_handler;
    sigemptyset(&sigint_action.sa_mask);
    sigint_action.sa_flags = 0;
    if (sigaction(SIGINT, &sigint_action, NULL) == -1) {
        perror("sigaction");
        exit(EXIT_FAILURE);
    }

    // Set up file descriptor set for select()
    fd_set read_set;
	input_t *temp = (input_t *)malloc(sizeof(input_t));

    // Continue reading from console until SIGINT is received
    while(!sigint_received){

        // Clear file descriptor set and add console pipe file descriptor
        FD_ZERO(&read_set);
        FD_SET(fd_console_pipe,&read_set);

        // Wait for input from console pipe
        if (select(fd_console_pipe+1,&read_set,NULL,NULL,NULL)>0){
            if(FD_ISSET(fd_console_pipe,&read_set)){

                // Read input from console pipe
                number_chars = read(fd_console_pipe,message,sizeof(message));
                message[number_chars - 1] = '\0';

                // Add input to internal queue
                sem_wait(empty);
                sem_wait(mutex);
                strcpy(temp->message, message);
                temp->priority = 1;
                internal_queue[iq_count] = *temp;    
                iq_count++;
                sem_post(mutex);
                sem_post(full);  
            }
        }   
    }
    close(fd_console_pipe);
    return NULL;
}


void* dispatcher(/*void *t*/){

	// Register SIGINT signal handler
	struct sigaction sigint_action;
    sigint_action.sa_handler = sigint_handler;
    sigemptyset(&sigint_action.sa_mask);
    sigint_action.sa_flags = 0;
    if (sigaction(SIGINT, &sigint_action, NULL) == -1) {
        perror("sigaction");
        exit(EXIT_FAILURE);
    }

	while(!sigint_received){
		sem_wait(full);
		int my_worker=-1;
		int dispatch_index = -1;
		int found_priority_1 = 0;
		
		// Allocate memory for the temporary input message
		input_t *temp = (input_t *)malloc(sizeof(input_t));
		
		printf("IN QUEUE");
		for(int i = 0; i<iq_count; i++){
			printf("message: %s\n",internal_queue[i].message);
		}

		// Iterate over the internal queue to find the highest priority message
		for(int i = iq_count-1 ; i >= 0; i--) {
    		if(internal_queue[i].priority == 1) {
        		// Found a message with priority 1
        		found_priority_1 = 1;
        		*temp = internal_queue[i];
        		dispatch_index = i;
        		break;
    		}
    		else if(internal_queue[i].priority == 2 && dispatch_index == -1) {
        		// Found a message with priority 2 and no message with priority 1 yet
        		*temp = internal_queue[i];
        		dispatch_index = i;
    		}
		}
		
		// Check if a message was found and dispatch it
		if(found_priority_1 || dispatch_index != -1) {
			message[0] = '\0';
    		strcpy(message,temp->message);

			// Shift all messages in the queue to fill the empty space left by the dispatched message
			for(int i = dispatch_index + 1; i < iq_count; i++) {
            	internal_queue[i-1] = internal_queue[i];
        	}
			iq_count--;
			sem_post(empty);
			
			// Acquire a worker and dispatch the message to it
			sem_wait(worker_sem);
			sem_wait(mutex);
			for(int i = 0; i < n_workers; i++){
				if(workers[i].available == 1){
					my_worker = i;
					workers[my_worker].available = 0;
					if(strlen(message) > 0) {
						snprintf(to_log,sizeof(to_log),"SENDING %s to worker %d",message,my_worker);
						write_log(fp_log,to_log);
						int message_len = strlen(message);
						message[message_len] = '\0';
						write(workers[my_worker].fd[1], message, message_len +1);
						message[0] = '\0';
					}
					break;
				}
			}	
			my_worker = -1;
			sem_post(mutex);	
		}
	}
    return NULL;
}


void alerts_watcher(){
	struct sigaction sigint_action;
	sigint_action.sa_handler = sigint_handler;
	sigemptyset(&sigint_action.sa_mask);
	sigint_action.sa_flags = 0;
	if (sigaction(SIGINT, &sigint_action, NULL) == -1) {
		perror("sigaction");
		exit(EXIT_FAILURE);
	}

	msgbuf to_send_alert;

	// Open the mutex semaphore for synchronization with other processes
	mutex_shm=sem_open(SHM_MUTEX,0);
	if(mutex_shm==SEM_FAILED){
		erro_handler("Failure opening the semaphore FULL");
	}

	// Open the semaphore that signals this process to start checking for alerts
	go_alerts=sem_open(GO_ALERTS,0);
	if(go_alerts==SEM_FAILED){
		erro_handler("Failure opening the semaphore GO_WATCHER");
	}

	// Attach to the shared memory segment
	shmid = shmget(shmkey,0,0700);
	if (shmid < 1){
		perror("Error opening shm memory!\n");
		exit(1);
	}
	shm_ptr = shmat(shmid, NULL, 0);

	// Cast the pointer to the appropriate structs
	info = (Info*) shmat(shmid, NULL, 0);
	sensors = (Sensor*) (info);
	keys = (Key_Data*) (sensors + max_sensors);
	alerts = (Alert*) (keys + max_keys);
	workers = (worker_t*) (alerts + max_alerts);

	// Continuously check for alerts until a SIGINT signal is received
	while(!sigint_received){
		sem_wait(go_alerts);
		sem_wait(mutex_shm);

		// Iterate over all alerts and sensor values to check for alerts
		for(int i = 0 ; i< info->alert_count; i++){
			for (int j = 0 ; j< info->key_count;j++){
				if(strcmp(alerts[i].key,keys[j].key)==0){
					if(alerts[i].valor_max < keys[j].last_value || alerts[i].valor_min > keys[j].last_value){
						// Log the alert
						write_log(fp_log,"ACTIVATED ALERT !!!");
						// Send a message to the worker process corresponding to the alert
						to_send_alert.mtype = alerts[i].pid+99999;
						strcpy(to_send_alert.mtext , "ACTIVATED ALERT !!!");
						if ( (msgsnd(mqid, &to_send_alert, sizeof(to_send_alert), 0)) < 0) {
							erro_handler("msgsnd error");
						}
					}
				}
			}
		}

		sem_post(mutex_shm);
	}
}

void worker(key_t shmkey, int index){
    // Initializes variables and semaphores
    int key_found = -1;	
    int id_found = -1;
    int alert_found = -1;
    int i = 0;
    char id[20];
    char key[20];
    int value=0;
    char message[MESSAGE_SIZE];
    char temp[1024];
    char chave[20];
    int min = 0;
    int max = 0;
    msgbuf to_send;
    struct sigaction sigint_action;

    // Configures a signal handler for SIGINT
    sigint_action.sa_handler = sigint_handler;
    sigemptyset(&sigint_action.sa_mask);
    sigint_action.sa_flags = 0;
    if (sigaction(SIGINT, &sigint_action, NULL) == -1) {
        perror("sigaction");
        exit(EXIT_FAILURE);
    }

    // Initializes and checks three semaphores
    mutex_shm=sem_open(SHM_MUTEX,0);
    if(mutex_shm==SEM_FAILED){
        erro_handler("Failure opening the semaphore FULL");
    }
    go_alerts=sem_open(GO_ALERTS,0);
    if(go_alerts==SEM_FAILED){
        erro_handler("Failure opening the semaphore GO_WATCHER");
    }
    worker_sem=sem_open(WORKER_SEM,0);
    if(worker_sem==SEM_FAILED){
        erro_handler("Failure opening the semaphore WORKER_SEM");
    }

    // Accesses shared memory and attaches to the structs stored there
    shmid = shmget(shmkey,0,0700);
    if (shmid < 1){
        perror("Error opening shm memory!\n");
        exit(1);
    }
    shm_ptr = shmat(shmid, NULL, 0);
    info = (Info*) shmat(shmid, NULL, 0);
    sensors = (Sensor*) (info);
    keys = (Key_Data*) (sensors + max_sensors);
    alerts = (Alert*) (keys + max_keys);
    workers = (worker_t*) (alerts + max_alerts);

    // Closes the writing end of the pipe
    close(workers[index].fd[1]);

    // Repeatedly listens to data from sensors via pipe
    while(!sigint_received){
        FD_ZERO(&read_set);
        FD_SET(workers[index].fd[0],&read_set);

        // Waits for data to arrive from the pipe
        if (select(workers[index].fd[0]+1,&read_set,NULL,NULL,NULL)>0){
            if(FD_ISSET(workers[index].fd[0],&read_set)){
                sem_wait(mutex_shm);
                id[0] = '\0';
                key[0] = '\0';
                message[0] = '\0';
                temp[0] = '\0';
                value = 0;
                key_found = -1;	
                id_found = -1;
                alert_found = -1;
                i = 0;
                chave[0] = '\0';
                min = 0;
                max = 0;

                // Reads the incoming message and parses it
                number_chars = read(workers[index].fd[0],message,sizeof(message)-1);
                message[number_chars] = '\0';
                if(sscanf(message, "%[^#]#%[^#]#%d", id, key, &value) == 3){

                    // Searches for the key in the shared memory
					for (i = 0; i<info->key_count;i++){
						if(strcmp(keys[i].key,key) == 0){
							key_found = i;
							break;
						}
					}

					// Didn't find the key, checks if theres still space
					if (key_found == -1) {
						if(info->max_keys >= info->key_count+1){
							strcpy(keys[info->key_count].key, key);
							keys[info->key_count].last_value = value;
							keys[info->key_count].min_value = value;
							keys[info->key_count].max_value = value;
							keys[info->key_count].used_count = 1;
							keys[info->key_count].average_value = value;
							keys[info->key_count].sum = value;
							info->key_count++;
						}
						else{
							write_log(fp_log,"MAX KEYS REACHED, KEY NOT ADDED");
						}
						
					}

					// Found the key, updates values
					else if (key_found != -1){
						keys[key_found].last_value = value;
						if(value < keys[key_found].min_value) keys[key_found].min_value = value;
						if(value > keys[key_found].max_value) keys[key_found].max_value = value;
						keys[key_found].used_count++;
						keys[key_found].sum+=value;
						keys[key_found].average_value = keys[key_found].sum / keys[key_found].used_count;
					}
					
					// Searches for the sensor in the shared memory
					for (i = 1; i<info->sensor_count+1;i++){
						if(strcmp(sensors[i].id,id) == 0){
							id_found = i;
							break;
						}
					}

					// Didn't find the key, if theres space add the sensor
					if(id_found == -1){
						if(info->max_sensors >= info->sensor_count+1){
							info->sensor_count++;
							snprintf(sensors[info->sensor_count].id,20,"%s",id);	
						}
						else{
							write_log(fp_log,"MAX SENSORS REACHED, SENSOR NOT ADDED");
						}
					}

				// command comes from an user console
				}else{
					int pid=0;
					char comando[100];
					sscanf(message,"%d#%[^\n]",&pid,comando);
					message[0] = '\0';

					if(strcmp(comando,"stats")==0){ 
						strcat(message,"\nKey Last Min Max Avg Count\n");	

						// iterates through all the keys	
						for(int i = 0; i <info->key_count; i++){
							snprintf(temp, sizeof(temp), "%s %d %d %d %f %d\n", keys[i].key, keys[i].last_value, keys[i].min_value, keys[i].max_value, keys[i].average_value, keys[i].used_count);
							strcat(message,temp);
						}

						// sends the response message back to the user console who made the request
						strcpy(to_send.mtext,message);
						to_send.mtype = pid;
						if(msgsnd(mqid, &to_send, sizeof(to_send), 0) == -1) {
							perror("msgsnd failed");
						}
						write_log(fp_log,"KEY'S STATS LISTED");


					}else if(strcmp(comando,"sensors")==0) {

						strcat(message,"\nID\n");	

						// iterates through all the sensors
						for(int i = 1; i <info->sensor_count+1; i++){
							snprintf(temp, sizeof(temp), "%s\n", sensors[i].id);
							strcat(message,temp);
						}

						// sends response message back to user console who made the request
						strcpy(to_send.mtext,message);
						to_send.mtype = pid;
						if(msgsnd(mqid, &to_send, sizeof(to_send), 0) == -1) {
							perror("msgsnd failed");
						}
						write_log(fp_log,"SENSORS LISTED");


					}else if(strcmp(comando, "reset")==0){

						// erses all the information stored about  the sensors' readings

						for(int i = 1; i<info->key_count+1; i++){
							keys[i].last_value = 0;
							keys[i].min_value = 0;
							keys[i].max_value = 0;
							keys[i].used_count = 0;
							keys[i].average_value = 0;
							keys[i].sum = 0;
							info->key_count = 0;
						}

						// sends response back to user console
						strcpy(to_send.mtext,"OK");
						to_send.mtype = pid;

						if(msgsnd(mqid, &to_send, sizeof(to_send), 0) == -1) {
							perror("msgsnd failed");
						}
						write_log(fp_log,"RESET DONE");

					}else if(sscanf(comando, "add_alert %s %s %d %d", id, chave, &min ,&max) == 4){
						

						// checks if alert to insert already exists
						for(int i = 0; i<info->alert_count; i++){
							if(strcmp(alerts[i].id,id) == 0){
								alert_found = i;
								break;
							}
						}

						// alert doesn't exist, adds it if theres still space
						if(alert_found == -1){
							if(info->max_alerts > info->alert_count+1){
								strcpy(alerts[info->alert_count].id, id);
								strcpy(alerts[info->alert_count].key, chave);
								alerts[info->alert_count].valor_max = max;
								alerts[info->alert_count].valor_min = min;
								alerts[info->alert_count].pid = pid;
								//send to console result							
								info->alert_count++;
								strcpy(message,"OK");
								write_log(fp_log,"ADDED ALERT");					


							}
							else{
								strcpy(message,"ERRO");
								write_log(fp_log,"NO SPACE FOR NEW ALERT");					

							}
						}
						else if (alert_found != -1){
							strcpy(message,"ERRO");
							write_log(fp_log,"ALERT ALREADY EXISTS");					

						}

						// sends message back to user console
						strcpy(to_send.mtext,message);
						to_send.mtype = pid;

						if(msgsnd(mqid, &to_send, sizeof(to_send), 0) == -1) {
							perror("msgsnd failed");
						}


					}else if(strcmp(comando, "list_alerts")==0) {

						// lists all the existing alerts
						strcat(message,"PID ID KEY MIN MAX\n");		
						for (int i =0; i<info->alert_count; i++){
							snprintf(temp, sizeof(temp), "%d %s %s %d %d\n", alerts[i].pid, alerts[i].id, alerts[i].key, alerts[i].valor_min, alerts[i].valor_max);
							strcat(message,temp);
						}

						// sends response to user console
						strcpy(to_send.mtext,message);
						to_send.mtype = pid;

						if(msgsnd(mqid, &to_send, sizeof(to_send), 0) == -1) {
							perror("msgsnd failed");
						}
						write_log(fp_log,"LISTED ALERTS");						


					}else if (sscanf(comando, "remove_alert %s", id) == 1) {

						// removes an alert and sends response depending on result

						int alert_found = -1;
						for (int i = 0; i < info->alert_count; i++) {
							if (strcmp(alerts[i].id, id) == 0) {
								alert_found = i;
								break;
							}
						}

						if (alert_found > -1) {
							for (int i = alert_found + 1; i < info->alert_count; i++) {
								alerts[i - 1] = alerts[i];
							}
							info->alert_count--;

							strcpy(alerts[info->alert_count].id, "");
							strcpy(alerts[info->alert_count].key, "");
							alerts[info->alert_count].valor_max = 0;
							alerts[info->alert_count].valor_min = 0;
							alerts[info->alert_count].pid = 0;

							strcpy(to_send.mtext, "OK");
							to_send.mtype = pid;

							if (msgsnd(mqid, &to_send, sizeof(to_send), 0) == -1) {
								perror("msgsnd failed");
							}
							write_log(fp_log, "ALERT REMOVED");
						} else {
							strcpy(to_send.mtext, "ERRO");
							to_send.mtype = pid;

							if (msgsnd(mqid, &to_send, sizeof(to_send), 0) == -1) {
								perror("msgsnd failed");
							}
							write_log(fp_log, "ALERT DOESNT EXIST");
						}

					}else if(strcmp(comando, "exit")==0) {
						

					}
					else{

						// command received is invalid
						strcpy(to_send.mtext,"INVALID COMMAND");
						to_send.mtype = pid;

						if(msgsnd(mqid, &to_send, sizeof(to_send), 0) == -1) {
							perror("msgsnd failed");
						}
						write_log(fp_log,"INVALID COMMAND");

					}
				}
			
				workers[index].available = 1; // sets worker back to available
				sem_post(mutex_shm);	// posts the mutex that controlls acess to shared memory
				sem_post(go_alerts);	// signals to alerts that changes to shm may have been made
				sem_post(worker_sem);	// represents setting worker to available
			}
		}			
	}
}

int main(int argc, char *argv[]) {

    if(argc!=2) {
		printf("Wrong number of parameters\n");
		return 1;
	}

	struct sigaction sigint_action;
	sigint_action.sa_handler = sigint_handler;
	sigemptyset(&sigint_action.sa_mask);
	sigint_action.sa_flags = 0;
	if (sigaction(SIGINT, &sigint_action, NULL) == -1) {
		perror("sigaction");
		exit(EXIT_FAILURE);
	}

	// Read configuration file
    size_t len = 0;
    
    fp_log = fopen("log","w");
	if (fp_log == NULL) {
		erro_handler("FAILED TO OPEN LOG FILE");  
    	exit(EXIT_FAILURE);
	}
	
    fp_config = fopen(argv[1], "r");
    if (fp_config == NULL) {
		erro_handler("FAILED TO OPEN CONFIGURATION FILE");  
        exit(EXIT_FAILURE);
    }write_log(fp_log,"HOME_IOT SIMULATOR STARTING");  
	
	getline(&linha, &len, fp_config);
	if((queue_sz = atoi(linha)) == 0 || queue_sz <1 ){
    	write_log(fp_log,"INVALID QUEUE SIZE");
    	fclose(fp_config);
    	exit(EXIT_FAILURE);}

	getline(&linha, &len, fp_config);
	if((n_workers = atoi(linha)) == 0 || n_workers <1){
    	write_log(fp_log,"INVALID NUMBER OF WORKERS");
    	fclose(fp_config);
    	exit(EXIT_FAILURE);} 

	getline(&linha, &len, fp_config);
	if((max_keys = atoi(linha)) == 0 || max_keys <1){
    	write_log(fp_log,"INVALID NUMBER OF MAX KEYS");
    	fclose(fp_config);
    	exit(EXIT_FAILURE);}

	getline(&linha, &len, fp_config);
	if((max_sensors = atoi(linha)) == 0 || max_sensors <1){
    	write_log(fp_log,"INVALID NUMBER OF MAX SENSORS");
    	fclose(fp_config);
    	exit(EXIT_FAILURE);}

	getline(&linha, &len, fp_config);
	if((max_alerts = atoi(linha)) == 0 || max_alerts<0){
    	write_log(fp_log,"INVALID NUMBER OF MAX ALERTS");
    	fclose(fp_config);
    	exit(EXIT_FAILURE);
    } write_log(fp_log,"CONFIG FILE READ AND VERIFIED");
    	
	fclose(fp_config);
	
	//itinialize sems
	sem_unlink("MUTEX");
	mutex=sem_open("MUTEX",O_CREAT|O_EXCL,0700,1); // create semaphore
	if(mutex==SEM_FAILED){
		erro_handler("Failure creating the semaphore MUTEX");
	}
	sem_unlink("EMPTY");
	empty=sem_open("EMPTY",O_CREAT|O_EXCL,0700,queue_sz); // create semaphore
	if(empty==SEM_FAILED){
		erro_handler("Failure creating the semaphore EMPTY");
	}
  	sem_unlink("FULL");
	full=sem_open("FULL",O_CREAT|O_EXCL,0700,0); // create semaphore
	if(full==SEM_FAILED){
		erro_handler("Failure creating the semaphore FULL");
	} 
	sem_unlink(SHM_MUTEX);
	mutex_shm=sem_open(SHM_MUTEX,O_CREAT|O_EXCL,0700,1); // create semaphore
	if(mutex_shm==SEM_FAILED){
		erro_handler("Failure creating the semaphore SHM_MUTEX");
	}
	sem_unlink(SEM_SENSOR_PIPE);
	sensor_sem=sem_open(SEM_SENSOR_PIPE,O_CREAT|O_EXCL,0700,1); // create semaphore
	if(sensor_sem==SEM_FAILED){
		erro_handler("Failure creating the semaphore SEM_SENSOR");
	}
	sem_unlink(SEM_CONSOLE_PIPE);
	console_sem=sem_open(SEM_CONSOLE_PIPE,O_CREAT|O_EXCL,0700,1); // create semaphore
	if(console_sem==SEM_FAILED){
		erro_handler("Failure creating the semaphore CONSOLE_SEM");
	}
	sem_unlink(GO_ALERTS);
	go_alerts=sem_open(GO_ALERTS,O_CREAT|O_EXCL,0700,1); // create semaphore
	if(go_alerts==SEM_FAILED){
		erro_handler("Failure creating the semaphore GO_WATCHER");
	}
	sem_unlink(WORKER_SEM);
	worker_sem=sem_open(WORKER_SEM,O_CREAT|O_EXCL,0700,n_workers); // create semaphore
	if(worker_sem==SEM_FAILED){
		erro_handler("Failure creating the semaphore WORKER_SEM");
	}

	if((shmkey = ftok(".", getpid())) == (key_t) -1){
    	perror("IPC error: ftok");
    	exit(1);
  	}
  
    // Initialize shared memory
    if ((shmid = shmget(shmkey,sizeof(int) * n_workers + sizeof(Info) + sizeof(Sensor) * max_sensors+1 + sizeof(Key_Data) * max_keys + sizeof(Alert) * max_alerts + sizeof(worker_t) * n_workers, IPC_CREAT | 0766)) < 0) {
		erro_handler("ERROR IN SHMGET WITH IPC_CREAT");
		exit(0);
	}

	info = (Info*) shmat(shmid,NULL,0);
	sensors = (Sensor*) (info);
    keys = (Key_Data*) (sensors + max_sensors);
    alerts = (Alert*) (keys + max_keys);
	workers = (worker_t*) (alerts + max_alerts);
    
    info->key_count = 0;
    info->sensor_count = 0;
    info->alert_count = 0;
    info->max_sensors = max_sensors;
    info->max_keys = max_keys;
    info->max_alerts = max_alerts;

	for (int i = 0; i< n_workers; i++){
		workers[i].available = 1;
	}
	
	write_log(fp_log,"CREATED AND INITIALIZED SHARED MEMORY");
	
	// Initialize queues
	msgctl(mqid,IPC_RMID,NULL);
	if ((mqid = msgget(mq_key,IPC_CREAT|0777)) == -1){
		erro_handler("ERROR IN MSGGET MESSAGE QUEUE");
		exit(0);
	}write_log(fp_log,"CREATED MESSAGE QUEUE");
	
	
	internal_queue = malloc (queue_sz * sizeof(input_t)) ;
	 
	// Create named pipes
    if((mkfifo(SENSOR_PIPE_NAME,O_CREAT|O_EXCL|0600)<0) && (errno != EEXIST)){
		erro_handler("ERROR CREATING SENSOR NAMED PIPE"); 
		exit(0);
	}
	if((mkfifo(CONSOLE_PIPE_NAME,O_CREAT|O_EXCL|0600)<0) && (errno != EEXIST)){
		erro_handler("ERROR CREATING CONSOLE NAMED PIPE"); 
		exit(0);
	}

	// Open named pipes
  	if ((fd_sensor_pipe = open(SENSOR_PIPE_NAME,O_RDWR))<0){
		erro_handler("ERROR OPENING SENSOR NAMED PIPE"); 
		exit(0);
  	}
  	if ((fd_console_pipe = open(CONSOLE_PIPE_NAME,O_RDWR))<0){
		erro_handler("ERROR OPENING CONSOLE NAMED PIPE"); 
		exit(0);
  	}  
  	

    for (int i = 0; i < n_workers; i++) {
    	pipe(workers[i].fd);
    	if ((workers[i].pid = fork()) == 0) {
        	sprintf(message,"PROCESS WORKER %d CREATED",i);
        	write_log(fp_log,message);
        	worker(shmkey,i);
        	exit(0);
		}
	}
  	
  	// Create alerts_watcher process
	if((id_alerts_watcher = fork()) == 0){
    	write_log(fp_log,"PROCESS ALERTS WATCHER CREATED");    	
		alerts_watcher();
		exit(0);
	}
	
	long id[3];
  	id[0]=0;
  	id[1]=1;
  	id[2]=2;
  	
  	pthread_create(&threads[0],NULL,sensor_reader,(void*)&id[0]);
    write_log(fp_log,"SENSOR_READER THREAD CREATED");
	pthread_create(&threads[1], NULL, console_reader,(void*)&id[1]);
    write_log(fp_log,"CONSOLE_READER THREAD CREATED");
	pthread_create(&threads[2], NULL, dispatcher,(void*)&id[2]);
    write_log(fp_log,"DISPATCHER THREAD CREATED");
	
	while(!sigint_received){
		pause();
	}

	write_log(fp_log,"SIGINT RECEIVED");

	if(iq_count > 0){
		write_log(fp_log,"LEFT IN INTERNAL QUEUE: ");
		for (int i = 0; i<iq_count;i++){
			write_log(fp_log,internal_queue[i].message);
		}
	}
	else{
		write_log(fp_log,"INTERNAL QUEUE IS EMPTY");
	}
	
	shutdown_all();

	return 0;
}
