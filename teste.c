#include <stdio.h>
#include <stdlib.h>
#include <stdbool.h>
#include <string.h>
#include <cudd.h>
#include <st.h>
#include <time.h> 

typedef enum{
    VAR,
    NOT,
    AND,
    OR
} OpType;

typedef struct Implementation
{
    OpType operador; // 0 p/ var, 1 p/ not, 2 p/ and, 3 p/ or -> Substituido por enum pra ficar mais claro
    char varName; //Apenas se operador == 0
    struct Implementation *left;
    struct Implementation *right;
} Implementation;

typedef struct
{
    DdNode *bdd;
    Implementation *impRoot;
} Function;

typedef struct
{
    Function **functions;
    int order;
    int size;
} Bucket;




//Adicionar apenas os literais no objetivo, remover as negações desnecessárias

// Protótipos para funções úteis

// Função para adicionar um novo bucket
Bucket *addBucket(Bucket *buckets, int *numBuckets);
// Imprimir o bucket para fins de debug
void printBucket(DdManager *manager, Bucket bucket, int varCount);
// Função para liberar memória alocada
// void freeAll(Bucket *buckets, int numBuckets);
void freeAllBuckets(DdManager *manager, Bucket *buckets, int numBuckets);
// Função para combinar dois buckets
// void combineBuckets(Bucket b1, Bucket b2, Bucket *result);
// Função para criar um novo bucket de ordem l realizando todas as combinações possíveis entre todos os buckets de ordem n + m = l
// void createCombinedBucket(Bucket *buckets, int numBuckets, int targetOrder);
// Função para precedência de operadores
int getPrecedence(char op);
// Função para verificar se é operador
bool isOperator(char c);
// Avalia uma expressão em pós-fixada usando BDDs
DdNode *evaluatePostfix(DdManager *manager, char **postfix, int count, Function *varMap, int varCount);
// Analisa a expressão infixada de entrada, converte para pós-fixada, e retorna o BDD resultante
DdNode *parseInputExpression(DdManager *manager, const char *input, Function **outVarMap, int *outVarCount, int *literalCount);
// Gera o bucket 1 com base no varMap retornado por parseInputExpression
DdNode *initializeFirstBucket(DdManager *manager, Function *varMap, int varCount, Bucket *bucket, DdNode *objectiveExp, bool *found, st_table *uniqueCheck);
// Combina dois BDDs com AND, OR ou NOT
DdNode *combineBdds(DdManager *manager, DdNode *bdd1, DdNode *bdd2, char operator);
// Função para criar um novo bucket de ordem l realizando todas as combinações possíveis entre todos os buckets de ordem n + m = l
bool createCombinedBucket(DdManager *manager, Bucket *buckets, int numBuckets, int targetOrder, DdNode *objectiveExp, st_table *uniqueCheck, char choice);
// Criar um novo nó caso seja variável
Implementation* varNode(char varName);
//Criar um novo nó caso seja operador
Implementation* opNode(OpType operador, Implementation* left, Implementation* right);
//Printar a implementação
void printImplementation(Implementation* node);

int main(int argc, char *argv[])
{
    time_t start = time(NULL);
    if (argc < 3)
    {
        fprintf(stderr, "Uso: %s <expressão> <modo>\n", argv[0]);
        fprintf(stderr, "Modos disponíveis:\n e - parar ao encontrar equivalência\n c - completar o bucket final\n");
        return EXIT_FAILURE;
    }
    DdManager *manager = Cudd_Init(0, 0, CUDD_UNIQUE_SLOTS, CUDD_CACHE_SLOTS, 0);
    if (manager == NULL)
    {
        fprintf(stderr, "Erro ao inicializar o CUDD.\n");
        return EXIT_FAILURE;
    }

    //tabela hash para verificar duplicatas, migrada para cá pra permitir verificação entre buckets
    st_table *uniqueCheck = st_init_table(st_ptrcmp, st_ptrhash);
    if (uniqueCheck == NULL)
    {
        fprintf(stderr, "Erro ao criar tabela hash ST\n");
        Cudd_Quit(manager);
        return EXIT_FAILURE;
    }
    char choice = argv[2][0];

    if (choice != 'e' && choice != 'c') {
        fprintf(stderr, "Erro: Modo inválido '%c'. Use 'e' ou 'c'.\n", choice);
        return EXIT_FAILURE;
    }

    int numBuckets = 0;
    bool found = false;
    //Declarar localmente na firstBucket
    Function *varMap = NULL;
    int varCount = 0;
    int literalCount = 0;
    Bucket *buckets = NULL;
    // Parseia a expressão de entrada e obtém o BDD resultante
    DdNode *objectiveExp = parseInputExpression(manager, argv[1], &varMap, &varCount, &literalCount);
    if (objectiveExp == NULL)
    {
        fprintf(stderr, "Erro ao parsear a expressão.\n");
        st_free_table(uniqueCheck);
        Cudd_Quit(manager);
        return EXIT_FAILURE;
    }
    //Esqueci que tautologias e contradições existem, então adicionei só agora kkkkk
    if(objectiveExp == Cudd_ReadLogicZero(manager)) {
        printf("A expressão é uma contradição (Sempre falsa).\n");
        st_free_table(uniqueCheck);
        if (varMap) free(varMap);
        Cudd_RecursiveDeref(manager, objectiveExp);
        Cudd_Quit(manager);
        return EXIT_SUCCESS;
    }
    if(objectiveExp == Cudd_ReadOne(manager)) {
        printf("A expressão é uma tautologia (Sempre verdadeira).\n");
        st_free_table(uniqueCheck);
        if (varMap) free(varMap);
        Cudd_RecursiveDeref(manager, objectiveExp);
        Cudd_Quit(manager);
        return EXIT_SUCCESS;
    }
    Cudd_PrintDebug(manager, objectiveExp, varCount, 2);

    // Inicializa o bucket 1
    buckets = addBucket(buckets, &numBuckets);
    initializeFirstBucket(manager, varMap, varCount, &buckets[0], objectiveExp, &found, uniqueCheck);
    if (!found) {

    printf("--- Bucket 1 (Ordem %d, Tamanho %d) ---\n", buckets[0].order, buckets[0].size);
    printBucket(manager, buckets[0], varCount);

    // Inicializa todos os buckets que poderão ser usados nesta execução do programa
    while (numBuckets < varCount)
    {
        buckets = addBucket(buckets, &numBuckets);
    }
    for (int order = 2; order <= varCount+1; order++)
    {
  
        found = createCombinedBucket(manager, buckets, numBuckets, order, objectiveExp, uniqueCheck, choice);
        if (found) break; // Sai do loop se encontrou a equivalência
        printf("--- Bucket %d (Ordem %d, Tamanho %d) ---\n", order, buckets[order - 1].order, buckets[order - 1].size);
        //printBucket(manager, buckets[order - 1], varCount);
        
    }
    }
    if (!found) {
        printf("Nenhuma equivalência encontrada até a ordem %d.\n", varCount);
    }

    // Após o uso, libera a hash
    st_free_table(uniqueCheck);

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
    Cudd_RecursiveDeref(manager, objectiveExp);
    freeAllBuckets(manager, buckets, numBuckets);
    free(varMap);
    Cudd_Quit(manager);
    time_t end = time(NULL);
    printf("Tempo total de execução: %ld segundos\n", end - start);
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

*/
Bucket *addBucket(Bucket *buckets, int *numBuckets)
{
    (*numBuckets)++;
    buckets = (Bucket *)realloc(buckets, (*numBuckets) * sizeof(Bucket));
    if (buckets == NULL)
    {
        fprintf(stderr, "Erro ao realocar memória para buckets.\n");
        exit(EXIT_FAILURE);
    }
    buckets[(*numBuckets) - 1].order = 0;
    buckets[(*numBuckets) - 1].functions = NULL;
    buckets[(*numBuckets) - 1].size = 0;
    return buckets;
}
/*
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

void printBucket(DdManager *manager, Bucket bucket, int varCount)
{
    printf("Bucket Order: %d | Size: %d\n", bucket.order, bucket.size);
    if (bucket.functions != NULL)
    {
        for (int i = 0; i < bucket.size; i++)
        {
            printf("  Function[%d]:", i);
            printImplementation(bucket.functions[i]->impRoot);
            printf("\n");
            //Cudd_PrintDebug(manager, bucket.functions[i], varCount, 2);
        }
    }
    else
    {
        printf("  No functions in this bucket.\n");
    }
}

/*void freeAll(Bucket *buckets, int numBuckets){
    for (int i = 0; i < numBuckets; i++) {
        if (buckets[i].functions != NULL) {
            for (int j = 0; buckets[i].functions[j] != NULL; j++) {
                free(buckets[i].functions[j]);
            }
            free(buckets[i].functions);
        }
    }
    free(buckets);
}*/
void freeAllBuckets(DdManager *manager, Bucket *buckets, int numBuckets)
{
    if (buckets == NULL)
        return;
    for (int i = 0; i < numBuckets; i++)
    {
        if (buckets[i].functions != NULL)
        {
            // Dereferencia todas as funções no bucket
            for (int j = 0; j < buckets[i].size; j++)
            {
                if (buckets[i].functions[j] != NULL)
                {
                    //Deferecia o BDD
                    Cudd_RecursiveDeref(manager, buckets[i].functions[j]->bdd);
                    
                    //Libera o nó de implementação
                    //Primeiro verifica se é NOT. Se sim, libera o filho esquerdo antes
                    if(buckets[i].functions[j]->impRoot->operador == NOT) {
                        free(buckets[i].functions[j]->impRoot->left);
                    }
                    free(buckets[i].functions[j]->impRoot);

                    //Libera a struct
                    free(buckets[i].functions[j]);
                }
            }
            free(buckets[i].functions); // Libera o array de ponteiros
        }
    }
    free(buckets); // Libera o array de buckets
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
int getPrecedence(char op)
{
    switch (op)
    {
    case '!':
        return 3; // Negação (unário)
    case '*':
        return 2; // AND
    case '+':
        return 1; // OR
    default:
        return 0;
    }
}

bool isOperator(char c)
{

    return c == '+' || c == '*' || c == '!';
}

DdNode *evaluatePostfix(DdManager *manager, char **postfix, int count, Function *varMap, int varCount)
{
    DdNode *pilha[256]; // Pilha para avaliação
    int top = -1;       // Índice do topo da pilha
    DdNode *result = NULL;

    for (int i = 0; i < count; i++)
    {
        char *token = postfix[i];
        if (token[0] >= 'A' && token[0] <= 'Z' && token[1] == '\0')
        {
            // Se for uma variável, descobre se já existe no varMap
            DdNode *varBdd = NULL;
            for (int j = 0; j < varCount; j++)
            {
                if (varMap[j].impRoot->varName == token[0])
                {
                    varBdd = varMap[j].bdd;
                    break;
                }
            }
            if (varBdd)
            {
                pilha[++top] = varBdd;
                Cudd_Ref(varBdd); // Referencia o BDD ao colocá-lo na pilha
            }
        }
        else if (isOperator(token[0]) && token[1] == '\0')
        {
            if (token[0] == '!')
            { // Se for NOT, realiza a operação, consome e reempilha o resultado
                if (top < 0)
                {
                    fprintf(stderr, "Erro: Pilha vazia para operador '!'\n");
                    return NULL;
                }
                DdNode *operand = pilha[top--];
                result = Cudd_Not(operand);
                Cudd_Ref(result);
                Cudd_RecursiveDeref(manager, operand);
                pilha[++top] = result;
            }
            else if (token[0] == '*' || token[0] == '+')
            { // Se for AND ou OR, realiza a operação, consome os operandos e reempilha o resultado
                if (top < 1)
                {
                    fprintf(stderr, "Erro: Pilha com operandos insuficientes para operador '%c'\n", token[0]);
                    return NULL;
                }
                DdNode *op1 = pilha[top--];
                DdNode *op2 = pilha[top--];
                DdNode *result;
                if (token[0] == '*')
                {
                    result = Cudd_bddAnd(manager, op2, op1);
                }
                else
                { // token[0] == '+'
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

DdNode *parseInputExpression(DdManager *manager, const char *input, Function **outVarMap, int *outVarCount, int *literalCount)
{
    char *postfix[256]; // Para montar a expressão em pós-fixada
    int postfix_count = 0;

    char op_stack[256]; // Pilha de operadores
    int op_top = -1;

    Function local_var_map[52];
    int var_count = 0;

    // Tokenização e Shunting-yard
    for (int i = 0; input[i] != '\0'; i++)
    {
        char c = input[i];

        // Normaliza o caracter caso seja minúsculo
        if (c >= 'a' && c <= 'z')
        {
            c = c - ('a' - 'A');
        }

        if (c == ' ')
            continue; // Ignora espaços

        if (c >= 'A' && c <= 'Z')
        {
            literalCount++;
             // Se é uma variável
            // Adiciona a variável ao nosso mapa se for nova
            bool found = false;
            for (int j = 0; j < var_count; j++)
            {
                if (local_var_map[j].impRoot->varName == c)
                    found = true;
            }
            if (!found)
            {
                //local_var_map[var_count].impRoot->varName = strdup(&c);
                local_var_map[var_count].impRoot = varNode(c);
                local_var_map[var_count].bdd = Cudd_bddIthVar(manager, var_count);
                printf("Criada variável BDD %d para '%c'\n", var_count, c);
                var_count++;
            }

            // Adiciona à fila de saída
            char *var_token = malloc(2);
            var_token[0] = c;
            var_token[1] = '\0';
            postfix[postfix_count++] = var_token;
        }
        else if (isOperator(c))
        { // Se é um operador
            while (op_top > -1 && getPrecedence(op_stack[op_top]) >= getPrecedence(c))
            {
                char *op_token = malloc(2);
                op_token[0] = op_stack[op_top--];
                op_token[1] = '\0';
                postfix[postfix_count++] = op_token;
            }
            op_stack[++op_top] = c;
        }
        else if (c == '(')
        {
            op_stack[++op_top] = c;
        }
        else if (c == ')')
        {
            while (op_top > -1 && op_stack[op_top] != '(')
            {
                char *op_token = malloc(2);
                op_token[0] = op_stack[op_top--];
                op_token[1] = '\0';
                postfix[postfix_count++] = op_token;
            }
            op_top--; // Descarta o '('
        }
    }

    // Pop dos operadores restantes da pilha
    while (op_top > -1)
    {
        char *op_token = malloc(2);
        op_token[0] = op_stack[op_top--];
        op_token[1] = '\0';
        postfix[postfix_count++] = op_token;
    }

    // Avalia a expressão
    *outVarMap = malloc(var_count * sizeof(Function));
    memcpy(*outVarMap, local_var_map, var_count * sizeof(Function));
    *outVarCount = var_count;

    DdNode *finalBdd = evaluatePostfix(manager, postfix, postfix_count, *outVarMap, var_count);

    // Limpa a memória dos tokens
    for (int i = 0; i < postfix_count; i++)
    {
        free(postfix[i]);
    }

    return finalBdd;
}

/*DdNode *initializeFirstBucket(DdManager *manager, Function *varMap, int varCount, Bucket *bucket, DdNode *objectiveExp, bool *found, st_table *uniqueCheck)
{
    bucket->order = 1;
    bucket->functions = (Function **)malloc(((varCount * 2) + 1) * sizeof(Function *));
    if (bucket->functions == NULL) exit(EXIT_FAILURE);

    int actualSize = 0;
    char varStr[2] = {'\0', '\0'};
    
    for (int i = 0; i < varCount; i++)
    {
        DdNode *varBdd = varMap[i].bdd;
        
        // Calcula Cofatores: f(x=1) e f(x=0)
        DdNode *cofPos = Cudd_Cofactor(manager, objectiveExp, varBdd);
        Cudd_Ref(cofPos);
        DdNode *cofNeg = Cudd_Cofactor(manager, objectiveExp, Cudd_Not(varBdd));
        Cudd_Ref(cofNeg);
        
        //Verifica Dependência: Se f(1) == f(0), a função não depende da variável
        if (cofPos == cofNeg) {
            Cudd_RecursiveDeref(manager, cofPos);
            Cudd_RecursiveDeref(manager, cofNeg);
            continue; 
        }

        //Verifica Unicidade

        bool isPosUnate = Cudd_bddLeq(manager, cofNeg, cofPos); 
        bool isNegUnate = Cudd_bddLeq(manager, cofPos, cofNeg);

        Cudd_RecursiveDeref(manager, cofPos);
        Cudd_RecursiveDeref(manager, cofNeg);

        // Adiciona se NÃO for estritamente negativa
        if (!isNegUnate) {
            bucket->functions[actualSize] = (Function *)malloc(sizeof(Function));
            varStr[0] = varMap[i].impRoot->varName;
            bucket->functions[actualSize]->impRoot = varNode(varStr[0]);
            bucket->functions[actualSize]->bdd = varMap[i].bdd;
            Cudd_Ref(bucket->functions[actualSize]->bdd);
            st_insert(uniqueCheck, (char *)bucket->functions[actualSize]->bdd, (char *)bucket->functions[actualSize]->bdd);

            if (bucket->functions[actualSize]->bdd == objectiveExp) {
                printf("Solução Encontrada (Ordem 1): %c\n", bucket->functions[actualSize]->impRoot->varName);
                *found = true;
            }
            actualSize++;
        }

        // Adiciona se NÃO for estritamente positiva
        if (!isPosUnate) {
            bucket->functions[actualSize] = (Function *)malloc(sizeof(Function));
            bucket->functions[actualSize]->impRoot = opNode(NOT, varNode(varMap[i].impRoot->varName), NULL);
            bucket->functions[actualSize]->bdd = Cudd_Not(varMap[i].bdd);
            Cudd_Ref(bucket->functions[actualSize]->bdd);
            st_insert(uniqueCheck, (char *)bucket->functions[actualSize]->bdd, (char *)bucket->functions[actualSize]->bdd);

            if (bucket->functions[actualSize]->bdd == objectiveExp) {
                 printf("Solução Encontrada (Ordem 1):" );

                 printImplementation(bucket->functions[actualSize]->impRoot);
                 printf("\n");
                *found = true;
            }
            actualSize++;
        }
    }

    bucket->size = actualSize;
    return NULL;
}*/

DdNode *initializeFirstBucket(DdManager *manager, Function *varMap, int varCount, Bucket *bucket, DdNode *objectiveExp, bool *found, st_table *uniqueCheck)
{
    printf("Inicializando o primeiro bucket...\n");
    bucket->order = 1;
    bucket->functions = (Function **)malloc(((varCount * 2) + 1) * sizeof(Function *));
    if (bucket->functions == NULL) exit(EXIT_FAILURE);

    int actualSize = 0;
    char varStr[2] = {'\0', '\0'};
    
    for (int i = 0; i < varCount; i++)
    {
        DdNode *varBdd = varMap[i].bdd;


        // Calcula Cofatores: f(x=1) e f(x=0)
        DdNode *cofPos = Cudd_Cofactor(manager, objectiveExp, varBdd);
        Cudd_Ref(cofPos);
        DdNode *cofNeg = Cudd_Cofactor(manager, objectiveExp, Cudd_Not(varBdd));
        Cudd_Ref(cofNeg);
        
        // Verifica Dependência: Se f(1) == f(0), a função não depende da variável
        if (Cudd_bddLeq(manager, cofPos, cofNeg) && Cudd_bddLeq(manager, cofNeg, cofPos)) {
            Cudd_RecursiveDeref(manager, cofPos);
            Cudd_RecursiveDeref(manager, cofNeg);
            continue; 
        }

        // Verifica Unicidade
        bool isPosUnate = !Cudd_bddLeq(manager, cofPos, cofNeg); 
        bool isNegUnate = !Cudd_bddLeq(manager, cofNeg, cofPos);


        Cudd_RecursiveDeref(manager, cofPos);
        Cudd_RecursiveDeref(manager, cofNeg);

        // Adiciona se NÃO for estritamente negativa
        if (!isNegUnate) {
            bucket->functions[actualSize] = (Function *)malloc(sizeof(Function));
            varStr[0] = varMap[i].impRoot->varName;
            bucket->functions[actualSize]->impRoot = varNode(varStr[0]);
            bucket->functions[actualSize]->bdd = varMap[i].bdd;
            Cudd_Ref(bucket->functions[actualSize]->bdd);
            st_insert(uniqueCheck, (char *)bucket->functions[actualSize]->bdd, (char *)bucket->functions[actualSize]->bdd);
            if (bucket->functions[actualSize]->bdd == objectiveExp) {
                
                printf("Solução Encontrada (Ordem 1): %c\n", bucket->functions[actualSize]->impRoot->varName);
                *found = true;
            }
            actualSize++;
        }

        // Adiciona se NÃO for estritamente positiva
        if (!isPosUnate) {
            bucket->functions[actualSize] = (Function *)malloc(sizeof(Function));
            bucket->functions[actualSize]->impRoot = opNode(NOT, varNode(varMap[i].impRoot->varName), NULL);
            bucket->functions[actualSize]->bdd = Cudd_Not(varMap[i].bdd);
            Cudd_Ref(bucket->functions[actualSize]->bdd);
            st_insert(uniqueCheck, (char *)bucket->functions[actualSize]->bdd, (char *)bucket->functions[actualSize]->bdd);
            if (bucket->functions[actualSize]->bdd == objectiveExp) {
                printf("Solução Encontrada (Ordem 1): ");
                printImplementation(bucket->functions[actualSize]->impRoot);
                printf("\n");
                *found = true;
            }
            actualSize++;
        }
    }

    bucket->size = actualSize;
    printf("Primeiro bucket inicializado com %d funções.\n", actualSize);
    return NULL;
}


DdNode *combineBdds(DdManager *manager, DdNode *bdd1, DdNode *bdd2, char operator)
{
    DdNode *result = NULL;
    if (operator == '*')
    {
        result = Cudd_bddAnd(manager, bdd1, bdd2);
    }
    else if (operator == '+')
    {
        result = Cudd_bddOr(manager, bdd1, bdd2);
    }
    else if (operator == '!')
    {
        result = Cudd_Not(bdd1);
    }
    else
    {
        fprintf(stderr, "Operador desconhecido: %c\n", operator);
        return NULL;
    }
    Cudd_Ref(result); // Referencia o BDD resultante
    return result;
}

// Função para realizar o realloc quando necessário
void addFunctionToDynamicArray(Function *func, Function ***array, int *count, int *capacity)
{
    if (*count == *capacity)
    {
        // Dobra a capacidade (ou inicializa se for 0)
        *capacity = (*capacity == 0) ? 256 : (*capacity) * 2;
        *array = (Function **)realloc(*array, (*capacity) * sizeof(Function *));
        if (*array == NULL)
        {
            fprintf(stderr, "Erro ao realocar array dinâmico de BDDs\n");
            exit(EXIT_FAILURE);
        }
    }
    (*array)[*count] = func;
    (*count)++;
}

bool createCombinedBucket(DdManager *manager, Bucket *buckets, int numBuckets, int targetOrder, DdNode *objectiveExp, st_table *uniqueCheck, char choice)
{
    Bucket *targetBucket = &buckets[targetOrder - 1];
    
    Function **newFunctions = NULL;
    int newFuncCount = 0;
    int newFuncCapacity = 0;

    for (int i = 0; i < numBuckets; i++)
    {
        int order1 = buckets[i].order; 
        int order2 = targetOrder - order1;
        if (order2 < order1) break; // Evita repetições desnecessárias
        int j = order2 - 1;
        
                Bucket *b1 = &buckets[i];
                Bucket *b2 = &buckets[j];

                for (int k = 0; k < b1->size; k++)
                {
                    // Se buckets iguais, l começa de k
                    int startL = (i == j) ? k : 0;
                    
                    for (int l = startL; l < b2->size; l++)
                    {
                        Function *f1 = b1->functions[k];
                        Function *f2 = b2->functions[l];
                        
                        for (int op = 0; op < 2; op++)
                        {
                            DdNode *newBdd;
                            char opChar = (op == 0) ? '*' : '+';
                            newBdd = combineBdds(manager, f1->bdd, f2->bdd, opChar);

                            //Parada imediata caso encontre equivalência
                            if (newBdd == objectiveExp && choice == 'e')
                            {
                                printf("\n!!! EQUIVALÊNCIA ENCONTRADA (Ordem %d) !!!\n", targetOrder);
                                
                                Implementation *temp = opNode((opChar == '*') ? AND : OR, f1->impRoot, f2->impRoot); 
                                printImplementation(temp);
                                printf("\n");

                                printf("No de literais: %d\n", targetOrder);
                                
                                // Limpa o que foi alocado temporariamente e retorna true
                                free(temp);
                                free(newFunctions); // Limpa o array parcial se houver
                                return true; 
                            }

                            if (st_lookup(uniqueCheck, (char *)newBdd, NULL) == 0 && newBdd != Cudd_ReadLogicZero(manager))
                            {

                                Function *newFunction = (Function *)malloc(sizeof(Function));
                                newFunction->bdd = newBdd;
                                newFunction->impRoot = opNode((opChar == '*') ? AND : OR, f1->impRoot, f2->impRoot);

                                st_insert(uniqueCheck, (char *)newBdd, (char *)newBdd);
                                addFunctionToDynamicArray(newFunction, &newFunctions, &newFuncCount, &newFuncCapacity);
                            }
                            else
                            {
                                Cudd_RecursiveDeref(manager, newBdd);
                            }
                        }
                    }
                }
            }

    targetBucket->order = targetOrder;
    targetBucket->size = newFuncCount;

    if (newFuncCount > 0) {
        // Não sei se precisa, mas economiza um pouco de memória
        targetBucket->functions = newFunctions;
    } else {
        free(newFunctions);
        targetBucket->functions = NULL;
    }
    
    // Verifica array final se a opção não era saída imediata
    for (int i = 0; i < newFuncCount; i++) {
        if (newFunctions[i]->bdd == objectiveExp) {
            printf("\n!!! EQUIVALÊNCIA ENCONTRADA (Ordem %d) !!!\n", targetOrder);
            printImplementation(newFunctions[i]->impRoot);
            printf("\n");
            printf("No de literais: %d\n", targetOrder);
            return true;
        }
    }

    return false;
}

Implementation* varNode(char varName) {
    Implementation* node = (Implementation*)malloc(sizeof(Implementation));
    node->operador = VAR; // variável
    node->varName = varName;
    node->left = NULL;
    node->right = NULL;
    return node;
}

Implementation* opNode(OpType operador, Implementation* left, Implementation* right) {
    Implementation* node = (Implementation*)malloc(sizeof(Implementation));
    node->operador = operador; // 1 p/ not, 2 p/ and, 3 p/ or
    node->varName = '\0'; // não usado para operadores
    node->left = left;
    node->right = right;
    return node;
}

void printImplementation(Implementation* node) {
    if (node == NULL) return;

    if (node->operador == VAR) { 
        printf("%c", node->varName);
    } else if (node->operador == NOT) { 
        printf("!");
        // Parenthesis check for NOT
        bool par = (node->left->operador != VAR && node->left->operador != NOT);
        if(par) printf("(");
        printImplementation(node->left);
        if(par) printf(")");
    } else { // AND or OR
        // Check priority for Left Child
        bool parLeft = (node->left->operador != VAR && node->left->operador != NOT && node->left->operador != node->operador);
        if (parLeft) printf("(");
        printImplementation(node->left);
        if (parLeft) printf(")");

        printf(node->operador == AND ? "*" : "+");

        // Check priority for Right Child
        bool parRight = (node->right->operador != VAR && node->right->operador != NOT && node->right->operador != node->operador);
        if (parRight) printf("(");
        printImplementation(node->right);
        if (parRight) printf(")");
    }
}