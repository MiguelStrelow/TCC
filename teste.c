#include <stdio.h>
#include <stdlib.h>
#include <stdbool.h>
#include <string.h>
#include <cudd.h>

typedef struct {
    char **function;
    int order;
    int size;
} Bucket;

// Protótipos para funções úteis

// Função para adicionar um novo bucket
Bucket* addBucket(Bucket *buckets, int *numBuckets);
// Imprimir o bucket para fins de debug
void printBucket(Bucket bucket);
// Função para liberar memória alocada
void freeAll(Bucket *buckets, int numBuckets);
// Função para combinar dois buckets
void combineBuckets(Bucket b1, Bucket b2, Bucket *result);
//Função para criar um novo bucket de ordem l realizando todas as combinações possíveis entre todos os buckets de ordem n + m = l
void createCombinedBucket(Bucket *buckets, int numBuckets, int targetOrder);
//Parse a string de entrada
void parseInput(char *input, Bucket *bucket);

int main(int argc, char *argv[]) {
    if (argc < 2) {
        fprintf(stderr, "Uso: %s <expressão>\n", argv[0]);
        return EXIT_FAILURE;
    }
    int numBuckets = 1; // começa com um bucket, de ordem 1
    Bucket *buckets = (Bucket *)malloc(numBuckets * sizeof(Bucket));
    if (buckets == NULL) {
        fprintf(stderr, "Erro ao alocar memória para buckets.\n");
        exit(EXIT_FAILURE);
    }
        parseInput(argv[1], &buckets[0]);
        printBucket(buckets[0]);
       /* buckets[0].order = 1;
        buckets[0].function = (char **)malloc(4 * sizeof(char *));
        buckets[0].function[0] = strdup("A");
        buckets[0].function[1] = strdup("B");
        buckets[0].function[2] = strdup("C");
        buckets[0].function[3] = NULL;
        buckets[0].size = 3;
        printBucket(buckets[0]);

    buckets = addBucket(buckets, &numBuckets);
    
    // Cria um novo bucket combinando o de ordem 1 com ele mesmo
    combineBuckets(buckets[0], buckets[0], &buckets[1]);

    printBucket(buckets[1]);
    buckets = addBucket(buckets, &numBuckets);
    combineBuckets(buckets[1], buckets[0], &buckets[2]);
    printBucket(buckets[2]);
    createCombinedBucket(buckets, numBuckets, 4);
    printBucket(buckets[3]);
    freeAll(buckets, numBuckets);*/
    return 0;
}

void combineBuckets(Bucket b1, Bucket b2, Bucket *result) {
    int combinations = b1.size * b2.size;
    int total_functions = combinations * 2;

    result->order = b1.order + b2.order;
    result->size = total_functions;
    result->function = (char **)malloc((total_functions + 1) * sizeof(char *));
    if (result->function == NULL) {
        fprintf(stderr, "Erro ao alocar memória para funções do bucket.\n");
        exit(EXIT_FAILURE);
    }

    int currentIndex = 0;
    for (int i = 0; i < b1.size; i++) {
        for (int j = 0; j < b2.size; j++) {
            char *func1 = b1.function[i];
            char *func2 = b2.function[j];
            int len1 = strlen(func1);
            int len2 = strlen(func2);
            
            // Aloca memória para as novas strings (+ e *)
            char *plus_combination = (char *)malloc(len1 + len2 + 2);
            char *mult_combination = (char *)malloc(len1 + len2 + 2);

            if (plus_combination == NULL || mult_combination == NULL) {
                fprintf(stderr, "Erro ao alocar memória para combinação de strings.\n");
                exit(EXIT_FAILURE);
            }

            // Cria as novas strings
            sprintf(plus_combination, "%s+%s", func1, func2);
            sprintf(mult_combination, "%s*%s", func1, func2);

            // Adiciona ao bucket de resultado
            result->function[currentIndex] = plus_combination;
            result->function[currentIndex + combinations] = mult_combination;
            currentIndex++;
        }
    }
    result->function[total_functions] = NULL; // Termina a lista
}


Bucket* addBucket(Bucket *buckets, int *numBuckets) {
    (*numBuckets)++;
    buckets = (Bucket *)realloc(buckets, (*numBuckets) * sizeof(Bucket));
    if (buckets == NULL) {
        fprintf(stderr, "Erro ao realocar memória para buckets.\n");
        exit(EXIT_FAILURE);
    }
    buckets[(*numBuckets) - 1].order = (*numBuckets) - 1;
    buckets[(*numBuckets) - 1].function = NULL;
    return buckets;
}
void printBucket(Bucket bucket) {
    printf("Bucket Order: %d\n", bucket.order);
    if (bucket.function != NULL) {
        for (int i = 0; bucket.function[i] != NULL; i++) {
            printf("Function[%d]: %s\n", i, bucket.function[i]);
        }
    } else {
        printf("No functions in this bucket.\n");
    }
}

void freeAll(Bucket *buckets, int numBuckets){
    for (int i = 0; i < numBuckets; i++) {
        if (buckets[i].function != NULL) {
            for (int j = 0; buckets[i].function[j] != NULL; j++) {
                free(buckets[i].function[j]);
            }
            free(buckets[i].function);
        }
    }
    free(buckets);
}

void createCombinedBucket(Bucket *buckets, int numBuckets, int targetOrder){
    for(int i = numBuckets; i < targetOrder; i++){    
        buckets = addBucket(buckets, &numBuckets);
        createCombinedBucket(buckets, numBuckets, i);
    }

    for(int i = 0; i < numBuckets; i++){
        for(int j = 0; j < numBuckets; j++){
            if(buckets[i].order + buckets[j].order == targetOrder){
                combineBuckets(buckets[i], buckets[j], &buckets[targetOrder - 1]);
            }
        }
    }
}

void parseInput(char *input, Bucket *bucket) {
    //normaliza a string de entrada p/ maiúsculas e remove espaços
    for(int i = 0; input[i]; i++){
        if(input[i] >= 'a' && input[i] <= 'z'){
            input[i] = input[i] - ('a' - 'A');
        }
        if(input[i] == ' '){
            for(int j = i; input[j]; j++){
                input[j] = input[j + 1];
            }
            i--;
        }
    }
    char *inputCpy = strdup(input);
    int count = 0;
    char *token = strtok(inputCpy, "+*&|");
    while (token != NULL) {
        token = strtok(NULL, "+*&|");
        count++;
    }
    free(inputCpy);
    bucket->function = (char **)malloc((count + 1) * sizeof(char *));
    if (bucket->function == NULL) {
        fprintf(stderr, "Erro ao alocar memória para funções do bucket.\n");
        exit(EXIT_FAILURE);
    }
    bucket->size = count;
    bucket->order = 1; // Supondo que a entrada inicial é sempre de ordem 1
    inputCpy = strdup(input);
    int index = 0;
    token = strtok(inputCpy, "+*&|");
    while (token != NULL) {
        bucket->function[index++] = strdup(token);
        token = strtok(NULL, "+*&|");
    }  
    bucket->function[index] = NULL; // Termina a lista
    free(inputCpy);
}