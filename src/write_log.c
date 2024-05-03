//
// Gabriel Suzana Ferreira 2021242097
// Simão Cabral Sousa 2020226115
//

#include "dados.h"


void write_log(FILE* fp_log, char message[]){
    pthread_mutex_lock(&file_lock);
    time_t t = time(NULL);
    char time_string[9];
    struct tm *local_time = localtime(&t);
    strftime(time_string, 9, "%H:%M:%S", local_time);
    printf("%s %s\n", time_string, message);
    fprintf(fp_log, "\n%s %s\n", time_string, message);
    fflush(fp_log);
    pthread_mutex_unlock(&file_lock);
}
