#include <stdio.h>
#include <stdlib.h>
#include <stdbool.h>
#include <string.h>
#include <cudd.h>

typedef struct {
    DdNode **functions;
    int order;
    int size;
} Bucket;

typedef struct {
    char name;
    DdNode *bdd;
} VarMap;

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
//Função para precedência de operadores
int getPrecedence(char op);
//Função para verificar se é operador
bool isOperator(char c);
//Avalia uma expressão em pós-fixada usando BDDs
DdNode* evaluatePostfix(DdManager *manager, char **postfix, int count, VarMap *varMap, int varCount); 
//Analisa a expressão infixada de entrada, converte para pós-fixada, e retorna o BDD resultante
DdNode* parseInputExpression(DdManager *manager, const char *input, VarMap **outVarMap, int *outVarCount);


int main(int argc, char *argv[]) {
    if (argc < 2) {
        fprintf(stderr, "Uso: %s <expressão>\n", argv[0]);
        return EXIT_FAILURE;
    }
    DdManager *manager = Cudd_Init(0,0,CUDD_UNIQUE_SLOTS,CUDD_CACHE_SLOTS,0);
        if (manager == NULL) {
        fprintf(stderr, "Erro ao inicializar o CUDD.\n");
        return EXIT_FAILURE;
    }

    VarMap *varMap = NULL;
    int varCount = 0;

    // Parseia a expressão de entrada e obtém o BDD resultante
    DdNode *objectiveExp = parseInputExpression(manager, argv[1], &varMap, &varCount);
    if (objectiveExp == NULL) {
        fprintf(stderr, "Erro ao parsear a expressão.\n");
        Cudd_Quit(manager);
        return EXIT_FAILURE;
    }
    Cudd_Ref(objectiveExp); // Referencia o BDD resultante
    Cudd_PrintDebug(manager, objectiveExp, varCount, 2);


    /*
    int numBuckets = 1; // começa com um bucket, de ordem 1
    Bucket *buckets = (Bucket *)malloc(numBuckets * sizeof(Bucket));
    if (buckets == NULL) {
        fprintf(stderr, "Erro ao alocar memória para buckets.\n");
        exit(EXIT_FAILURE);
    }
        parseInput(manager, argv[1], &buckets[0]);
        printBucket(buckets[0]);
        buckets[0].order = 1;
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
    Cudd_Quit(manager);
    return 0;
}

/*void combineBuckets(Bucket b1, Bucket b2, Bucket *result) {
    int combinations = b1.size * b2.size;
    int total_functions = combinations * 2;

    result->order = b1.order + b2.order;
    result->size = total_functions;
    result->functions = (char **)malloc((total_functions + 1) * sizeof(char *));
    if (result->functions == NULL) {
        fprintf(stderr, "Erro ao alocar memória para funções do bucket.\n");
        exit(EXIT_FAILURE);
    }

    int currentIndex = 0;
    for (int i = 0; i < b1.size; i++) {
        for (int j = 0; j < b2.size; j++) {
            char *func1 = b1.functions[i];
            char *func2 = b2.functions[j];
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
            result->functions[currentIndex] = plus_combination;
            result->functions[currentIndex + combinations] = mult_combination;
            currentIndex++;
        }
    }
    result->functions[total_functions] = NULL; // Termina a lista
}


Bucket* addBucket(Bucket *buckets, int *numBuckets) {
    (*numBuckets)++;
    buckets = (Bucket *)realloc(buckets, (*numBuckets) * sizeof(Bucket));
    if (buckets == NULL) {
        fprintf(stderr, "Erro ao realocar memória para buckets.\n");
        exit(EXIT_FAILURE);
    }
    buckets[(*numBuckets) - 1].order = (*numBuckets) - 1;
    buckets[(*numBuckets) - 1].functions = NULL;
    return buckets;
}
void printBucket(Bucket bucket) {
    printf("Bucket Order: %d\n", bucket.order);
    if (bucket.functions != NULL) {
        for (int i = 0; bucket.functions[i] != NULL; i++) {
            printf("functions[%d]: %s\n", i, bucket.functions[i]);
        }
    } else {
        printf("No functions in this bucket.\n");
    }
}
*/
void freeAll(Bucket *buckets, int numBuckets){
    for (int i = 0; i < numBuckets; i++) {
        if (buckets[i].functions != NULL) {
            for (int j = 0; buckets[i].functions[j] != NULL; j++) {
                free(buckets[i].functions[j]);
            }
            free(buckets[i].functions);
        }
    }
    free(buckets);
}
/*
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
*/
int getPrecedence(char op) {
    switch (op) {
        case '!': return 3; // Negação (unário)
        case '*': return 2; // AND
        case '+': return 1; // OR
        default:  return 0;
    }
}

bool isOperator(char c) {

    return c == '+' || c == '*' || c == '!';
}

DdNode* evaluatePostfix(DdManager *manager, char **postfix, int count, VarMap *varMap, int varCount){
    DdNode *pilha[256]; // Pilha para avaliação
    int top = -1; // Índice do topo da pilha
    DdNode *result = NULL;

    for (int i = 0; i < count; i++) {
        char *token = postfix[i];
        if (token[0] >= 'A' && token[0] <= 'Z' && token[1] == '\0') {
            // Se for uma variável, descobre se já existe no varMap
            DdNode *varBdd = NULL;
            for (int j = 0; j < varCount; j++) {
                if (varMap[j].name == token[0]) {
                    varBdd = varMap[j].bdd;
                    break;
                }
            }
            if (varBdd) {
                pilha[++top] = varBdd;
                Cudd_Ref(varBdd); // Referencia o BDD ao colocá-lo na pilha
            }
        } else if (isOperator(token[0]) && token[1] == '\0'){ 
            if (token[0] == '!') { //Se for NOT, realiza a operação, consome e reempilha o resultado
                if (top < 0) {
                    fprintf(stderr, "Erro: Pilha vazia para operador '!'\n");
                    return NULL;
                }
                DdNode *operand = pilha[top--];
                result = Cudd_Not(operand);
                Cudd_Ref(result);
                Cudd_RecursiveDeref(manager, operand);
                pilha[++top] = result;

            } else if (token[0] == '*' || token[0] == '+') { //Se for AND ou OR, realiza a operação, consome os operandos e reempilha o resultado
                if (top < 1) {
                    fprintf(stderr, "Erro: Pilha com operandos insuficientes para operador '%c'\n", token[0]);
                    return NULL;
                }
                DdNode *op1 = pilha[top--];
                DdNode *op2 = pilha[top--];
                DdNode *result;
                if (token[0] == '*') {
                    result = Cudd_bddAnd(manager, op2, op1);
                } else { // token[0] == '+'
                    result = Cudd_bddOr(manager, op2, op1);
            }
                Cudd_Ref(result);
                Cudd_RecursiveDeref(manager, op2);
                Cudd_RecursiveDeref(manager, op1);
                pilha[++top] = result;
        }
        }
    }
    DdNode *finalBdd = pilha[top--];
    // No fim, retorna o BDD encontrado
    return finalBdd;
}

DdNode* parseInputExpression(DdManager *manager, const char *input, VarMap **outVarMap, int *outVarCount){
    char *postfix[256]; // Para montar a expressão em pós-fixada
    int postfix_count = 0;
    
    char op_stack[256]; // Pilha de operadores
    int op_top = -1;

    VarMap local_var_map[52];
    int var_count = 0;
    
    //Tokenização e Shunting-yard
    for (int i = 0; input[i] != '\0'; i++) {
        char c = input[i];

        //Normaliza o caracter caso seja minúsculo
        if (c >= 'a' && c <= 'z') {
            c = c - ('a' - 'A');
        }

        if (c == ' ') continue; // Ignora espaços

        if (c >= 'A' && c <= 'Z') { // Se é uma variável
            // Adiciona a variável ao nosso mapa se for nova
            bool found = false;
            for(int j=0; j<var_count; j++) {
                if(local_var_map[j].name == c) found = true;
            }
            if (!found) {
                local_var_map[var_count].name = c;
                local_var_map[var_count].bdd = Cudd_bddIthVar(manager, var_count);
                printf("Criada variável BDD %d para '%c'\n", var_count, c);
                var_count++;
            }
            
            // Adiciona à fila de saída
            char *var_token = malloc(2);
            var_token[0] = c; var_token[1] = '\0';
            postfix[postfix_count++] = var_token;

        } else if (isOperator(c)) { // Se é um operador
            while (op_top > -1 && getPrecedence(op_stack[op_top]) >= getPrecedence(c)) {
                char *op_token = malloc(2);
                op_token[0] = op_stack[op_top--]; op_token[1] = '\0';
                postfix[postfix_count++] = op_token;
            }
            op_stack[++op_top] = c;
        } else if (c == '(') {
            op_stack[++op_top] = c;
        } else if (c == ')') {
            while (op_top > -1 && op_stack[op_top] != '(') {
                char *op_token = malloc(2);
                op_token[0] = op_stack[op_top--]; op_token[1] = '\0';
                postfix[postfix_count++] = op_token;
            }
            op_top--; // Descarta o '('
        }
    }

    //Pop dos operadores restantes da pilha
    while (op_top > -1) {
        char *op_token = malloc(2);
        op_token[0] = op_stack[op_top--]; op_token[1] = '\0';
        postfix[postfix_count++] = op_token;
    }

    //Avalia a expressão
    *outVarMap = malloc(var_count * sizeof(VarMap));
    memcpy(*outVarMap, local_var_map, var_count * sizeof(VarMap));
    *outVarCount = var_count;

    DdNode* finalBdd = evaluatePostfix(manager, postfix, postfix_count, *outVarMap, var_count);

    //Limpa a memória dos tokens
    for (int i = 0; i < postfix_count; i++) {
        free(postfix[i]);
    }
    
    return finalBdd;
}