//
// Gabriel Suzana Ferreira 2021242097
// Simão Cabral Sousa 2020226115
//

#include <ctype.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "dados.h"



int input_validation(char id[10], char key[20]){
    int error = 0;
    // verificar se o identificador do sensor é válido
    if (strlen(id) < 3 || strlen(id) > 32) {
        printf("Erro: Identificador de sensor inválido.\n");
        error = 1;
    }

    // verificar se a chave é válida
    
    for (size_t i = 0; i < strlen(key); i++) {
        if (!isdigit(key[i]) && !isalpha(key[i]) && key[i] != '_') {
            printf("Erro: Chave inválida.\n");
            error = 1;
            break;
        }
    }

    if (strlen(key) < 3 || strlen(key) > 32) {
        printf("Erro: Chave deve ter tamanho entre 3 e 32 caracteres.\n");
        error = 1;
    }

    return error;

}
