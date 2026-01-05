#include <stdio.h>
#include <stdlib.h>
#include <stdbool.h>
#include <string.h>
#include <cudd.h>
#include <st.h>
#include <time.h>
#include <omp.h>

double global_total_time = 0.0;   // Tempo Fora (Espera + Serviço)
double global_service_time = 0.0;
#define PARALLEL_MIN_COMBINATIONS 3000
#define BATCH_SIZE 2048
#define QUEUE_SIZE 100000

typedef enum{
    VAR,
    NOT,
    AND,
    OR
} OpType;

typedef struct Function
{
    DdNode *bdd;
    struct Function *left;
    struct Function *right;
    OpType operador;
    char varName; //Apenas se operador == VAR
} Function;

typedef struct
{
    Function **functions;
    int order;
    int size;
} Bucket;

typedef struct {
    Function *f1;
    Function *f2;
    DdNode *bdd;
    char op;
} CombinationBuffer;

typedef struct {
    Function *f1[BATCH_SIZE];
    Function *f2[BATCH_SIZE];
    char op[BATCH_SIZE];
    int count; // Quantos itens validos neste batch
} TaskBatch;

typedef struct {
    TaskBatch batches[QUEUE_SIZE];
    int head;
    int tail;
    int count;
    omp_lock_t lock;
    bool finished; // Flag para indicar que os produtores acabaram
} TaskQueue;



// Protótipos para funções úteis

// Função para adicionar um novo bucket
Bucket *addBucket(Bucket *buckets, int *numBuckets);
// Imprimir o bucket para fins de debug
void printBucket(DdManager *manager, Bucket bucket, int varCount);
// Função para liberar memória alocada
// void freeAll(Bucket *buckets, int numBuckets);
void freeAllBuckets(DdManager *manager, Bucket *buckets, int numBuckets);
// Função para checar precedência de operadores
int getPrecedence(char op);
// Função para verificar se é operador
bool isOperator(char c);
// Avalia uma expressão em pós-fixada usando BDDs
DdNode *evaluatePostfix(DdManager *manager, char **postfix, int count, Function *varMap, int varCount);
// Analisa a expressão infixada de entrada, converte para pós-fixada, e retorna o BDD resultante
DdNode *parseInputExpression(DdManager *manager, const char *input, Function **outVarMap, int *outVarCount, int *literalCount);
// Gera o bucket 1 com base no varMap retornado por parseInputExpression
DdNode *initializeFirstBucket(DdManager *manager, Function *varMap, int varCount, Bucket *bucket, DdNode *objectiveExp, bool *found, st_table *uniqueCheck);
// Combina dois BDDs com AND, OR ou NOT -> gera todos os SOP'S e POS'S
DdNode *combineBdds(DdManager *manager, DdNode *bdd1, DdNode *bdd2, char operator);
// Função para criar um novo bucket de ordem l realizando todas as combinações possíveis entre todos os buckets de ordem n + m = l
bool createCombinedBucket(DdManager *manager, Bucket *buckets, int numBuckets, int targetOrder, DdNode *objectiveExp, st_table *uniqueCheck, char choice);
// Criar um novo nó caso seja variável
Function* varNode(char varName, DdNode *bdd);
//Criar um novo nó caso seja operador
Function* opNode(OpType operador, Function* left, Function* right, DdNode *bdd);
//Printar a implementação
void printFunction(Function* node);
//Para log
//void fprintFunction(FILE *f, Function* node);
//void logExpressionToFile(Function *node, int ordem);
void initQueue(TaskQueue *q);
void enqueue(TaskQueue *q, TaskBatch *t);
bool dequeue(TaskQueue *q, TaskBatch *t);

int main(int argc, char *argv[])
{
    

    //A princípio toda a primeira parte da execução é sequencial, paralelizar iria gerar overhead desnecessário
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

    //Por estabilidade no paralelismo, desabilitar autodyn
    Cudd_AutodynDisable(manager);

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
    
     //Iniciar aqui para levar em conta apenas o algoritmo
    double start_time = omp_get_wtime();
    clock_t start_clock = clock();

    // Inicializa o bucket 1
    buckets = addBucket(buckets, &numBuckets);
    initializeFirstBucket(manager, varMap, varCount, &buckets[0], objectiveExp, &found, uniqueCheck);
    if (!found) {

    printBucket(manager, buckets[0], varCount);

    // Inicializa todos os buckets que poderão ser usados nesta execução do programa
    while (numBuckets < literalCount)
    {
        buckets = addBucket(buckets, &numBuckets);
    }
    for (int order = 2; order <= literalCount; order++)
    {
        //Aqui começa a parte paralela
        //Dentro da função, quero que cada thread trate de combinar buckets diferentes
        found = createCombinedBucket(manager, buckets, numBuckets, order, objectiveExp, uniqueCheck, choice);
        if (found) break; // Sai do loop se encontrou a equivalência
        //printBucket(manager, buckets[order - 1], varCount);
        
    }
    }
    if (!found) {
        printf("Nenhuma equivalência encontrada até a ordem %d.\n", literalCount);
    }
    
    //Acaba aqui, liberar a memória não faz parte do algoritmo
    double end_time = omp_get_wtime();
    clock_t end_clock = clock();

    // Após o uso, libera a hash
    // Vou ter que rever todos os frees mais pra frente
    st_free_table(uniqueCheck);

 
    Cudd_RecursiveDeref(manager, objectiveExp);
    freeAllBuckets(manager, buckets, numBuckets);

    if (varMap != NULL) {
        for (int i = 0; i < varCount; i++) {
            Cudd_RecursiveDeref(manager, varMap[i].bdd);
        }
        free(varMap);
    }

    Cudd_Quit(manager);
    double wait_time = global_total_time - global_service_time;
    double cpu_time_used = ((double) (end_clock - start_clock)) / CLOCKS_PER_SEC; 

    printf("BENCHMARK_TIME: %.6f\n", end_time - start_time);
    printf("RESULTADO_CLOCK_TIME: %.6f\n", cpu_time_used);
    printf("RESULTADO_SERVICE_TIME: %.6f\n", global_service_time); // Tempo útil
    printf("RESULTADO_WAIT_TIME: %.6f\n", wait_time); // Tempo de espera


    return 0;
}


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

void printBucket(DdManager *manager, Bucket bucket, int varCount)
{
    (void)manager;
    (void)varCount;
    (void)bucket;
    // Função mantida vazia para evitar prints de debug
}

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
                    if (buckets[i].functions[j]->bdd) 
                    Cudd_RecursiveDeref(manager, buckets[i].functions[j]->bdd);

                    //Libera a struct
                    free(buckets[i].functions[j]);
                }
            }
            free(buckets[i].functions); // Libera o array de ponteiros
        }
    }
    free(buckets); // Libera o array de buckets
}
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
                if (varMap[j].varName == token[0])
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
            (*literalCount)++;
             // Se é uma variável
            // Adiciona a variável ao nosso mapa se for nova
            bool found = false;
            for (int j = 0; j < var_count; j++)
            {
                if (local_var_map[j].varName == c)
                    found = true;
            }
            if (!found)
            {
                
                local_var_map[var_count].operador = VAR;
                local_var_map[var_count].varName = c;
                local_var_map[var_count].left = NULL;
                local_var_map[var_count].right = NULL;
                local_var_map[var_count].bdd = Cudd_bddIthVar(manager, var_count);
                Cudd_Ref(local_var_map[var_count].bdd);
                // debug print removed
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


DdNode *initializeFirstBucket(DdManager *manager, Function *varMap, int varCount, Bucket *bucket, DdNode *objectiveExp, bool *found, st_table *uniqueCheck)
{
    printf("Inicializando o primeiro bucket...\n");
    bucket->order = 1;
    bucket->functions = (Function **)malloc(((varCount * 2) + 1) * sizeof(Function *));
    if (bucket->functions == NULL) exit(EXIT_FAILURE);

    int actualSize = 0;
    
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
        bool isPosUnate = Cudd_bddLeq(manager, cofNeg, cofPos); 
        bool isNegUnate = Cudd_bddLeq(manager, cofPos, cofNeg);
        bool isBinate = !isPosUnate && !isNegUnate;

        Cudd_RecursiveDeref(manager, cofPos);
        Cudd_RecursiveDeref(manager, cofNeg);

        // Adiciona se for positivo unate ou binate
        if (isPosUnate || isBinate) {
            Cudd_Ref(varMap[i].bdd);
            // Cria o nó da função]
            bucket->functions[actualSize] = varNode(varMap[i].varName, varMap[i].bdd);
            // Insere na tabela hash de verificação
            st_insert(uniqueCheck, (char *)bucket->functions[actualSize]->bdd, (char *)bucket->functions[actualSize]->bdd);
            //Verifica se é solução
            if (bucket->functions[actualSize]->bdd == objectiveExp) {
                
                printf("Solução Encontrada (Ordem 1): ");
                printFunction(bucket->functions[actualSize]);
                printf("\n");
                *found = true;
            }
            actualSize++;
        }

        // Adiciona se for negativo unate ou binate
        if (isNegUnate || isBinate) {
            // Temporário para o BDD negado
            DdNode *notBdd = Cudd_Not(varMap[i].bdd);
            Cudd_Ref(notBdd);
            Function *varNodePtr = varNode(varMap[i].varName, varMap[i].bdd);
            Cudd_Ref(varMap[i].bdd);
            // Cria o nó da função
            bucket->functions[actualSize] = opNode(NOT, varNodePtr, NULL, notBdd);
            // Insere na tabela hash de verificação
            st_insert(uniqueCheck, (char *)bucket->functions[actualSize]->bdd, (char *)bucket->functions[actualSize]->bdd);
            //Verifica se é solução
            if (bucket->functions[actualSize]->bdd == objectiveExp) 
            {
                printf("Solução Encontrada (Ordem 1): ");
                printFunction(bucket->functions[actualSize]);
                printf("\n");
                *found = true;
            }
            actualSize++;
        }
    }

    bucket->size = actualSize;
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

    bool stop = false;

    TaskQueue *queue = (TaskQueue *)malloc(sizeof(TaskQueue));
    initQueue(queue);

    int active_workers;

    #pragma omp parallel
    {
        #pragma omp single
        active_workers = omp_get_num_threads() - 1;

        int tid = omp_get_thread_num();
        int numThreads = omp_get_num_threads();
        
        if (tid == 0){
            TaskBatch task;
            while (true){
                bool hasWork = dequeue(queue, &task);
                int current_workers;
                #pragma omp atomic read
                current_workers = active_workers;

                if (!hasWork) {
                    if (current_workers == 0 && queue->count == 0) break; // Tudo processado
                    continue; // File vazia mas ainda tem produção
                }
                if (stop) continue; //Parada ativada, limpa a fila

                double t_svc_start = omp_get_wtime();

                // Loop interno para processar o lote inteiro
                for (int i = 0; i < task.count; i++) {
                    if (stop) break; // Checa stop dentro do lote também

                    Function *func1 = task.f1[i];
                    Function *func2 = task.f2[i];
                    char op = task.op[i];

       
                    DdNode *newBdd = combineBdds(manager, func1->bdd, func2->bdd, op);
                if (newBdd != NULL && newBdd != Cudd_ReadLogicZero(manager) && newBdd != Cudd_ReadOne(manager)) {

                    if (newBdd == objectiveExp && choice == 'e'){
                        stop = true;
                        #pragma omp flush(stop)
                        printf("\n!!! EQUIVALÊNCIA ENCONTRADA (Ordem %d) !!!\n", targetOrder);
                        printf("RESULTADO_LITERAIS: %d\n", targetOrder);
                            
                        Function tempNode;
                        tempNode.operador = (task.op[i] == '*') ? AND : OR;
                        tempNode.left = task.f1[i];
                        tempNode.right = task.f2[i];
                        tempNode.varName = '\0';

                        printf("RESULTADO_EXPRESSAO: ");
                        printFunction(&tempNode);
                        printf("\n");
                    }

                    if (!stop) {
                        if (st_insert(uniqueCheck, (char *)newBdd, (char *)newBdd) == 0) {
                                Function *newFunction = opNode((task.op[i] == '*') ? AND : OR, task.f1[i], task.f2[i], newBdd);
                                addFunctionToDynamicArray(newFunction, &newFunctions, &newFuncCount, &newFuncCapacity);
                            } else {
                                Cudd_RecursiveDeref(manager, newBdd);
                            }
                    } else {
                        Cudd_RecursiveDeref(manager, newBdd);
                    }
                } else {
                    if(newBdd) Cudd_RecursiveDeref(manager, newBdd);
            }
            double t_svc_end = omp_get_wtime();
            #pragma omp atomic
            global_service_time += (t_svc_end - t_svc_start);
        }
    }
    } else
    {
    //Aqui as outras threads, que só fazem as combinações e enfileiram
    TaskBatch localBatch;
    localBatch.count = 0;    
    for (int i = 0; i < targetOrder-1; i++)
        {
            #pragma omp flush(stop)
            if (stop) break; // Sai do loop se a flag de parada foi ativada na iteração passada

    
            int order1 = buckets[i].order; 
            int order2 = targetOrder - order1;
            if (order2 < order1) break; // Evita repetições desnecessárias
            int j = order2 - 1;

                Bucket *b1 = &buckets[i];
                Bucket *b2 = &buckets[j];

                if (b1->size == 0 || b2->size == 0) continue;

                //Distribui manualmente o loop

                int worker_count = numThreads - 1;
                int my_id = tid - 1;
                
                for (int k = my_id; k < b1->size; k+= worker_count)
                {
                     // Verifica se a flag de parada foi ativada
                    #pragma omp flush(stop)
                    if (stop) continue;

                    for (int l = 0; l < b2->size; l++)
                    {
                        if (i == j && l < k) continue; // Evita repetições desnecessárias em buckets iguais

                        //Function *f1 = b1->functions[k];
                        //Function *f2 = b2->functions[l];
                        
                        // Enfileira as duas operações
                        int idx = localBatch.count;
                        localBatch.f1[idx] = b1->functions[k];
                        localBatch.f2[idx] = b2->functions[l];
                        localBatch.op[idx] = '*'; // Primeiro AND
                        localBatch.count++;
                        
                        if (localBatch.count == BATCH_SIZE) {
                            enqueue(queue, &localBatch);
                            localBatch.count = 0;
                        }
                        idx = localBatch.count;
                localBatch.f1[idx] = b1->functions[k];
                localBatch.f2[idx] = b2->functions[l];
                localBatch.op[idx] = '+'; 
                localBatch.count++;
                
                if (localBatch.count == BATCH_SIZE) {
                    enqueue(queue, &localBatch);
                    localBatch.count = 0;
                }
                    }
                }
            }

            if (localBatch.count > 0) {
            enqueue(queue, &localBatch);
            }

            #pragma omp atomic
            active_workers--;
        }
    } // Fim do parallel region
    
    omp_destroy_lock(&queue->lock);
    free(queue);
    if (stop) {
        // Libera todas as funções criadas
        for (int i = 0; i < newFuncCount; i++) {
            Cudd_RecursiveDeref(manager, newFunctions[i]->bdd);
            free(newFunctions[i]);
        }
        free(newFunctions);
        return true; // Equivalência encontrada
    }

    targetBucket->order = targetOrder;
    targetBucket->size = newFuncCount;
    if (newFuncCount > 0) {
        targetBucket->functions = newFunctions;
    } else
    {
        free(newFunctions);
        targetBucket->functions = NULL;
    }
    return false;        
}

Function* varNode(char varName, DdNode *bdd) {
    Function* node = (Function*)malloc(sizeof(Function));
    node->bdd = bdd;
    node->operador = VAR; // variável
    node->varName = varName;
    node->left = NULL;
    node->right = NULL;
    return node;
}

Function* opNode(OpType operador, Function* left, Function* right, DdNode *bdd) {
    Function* node = (Function*)malloc(sizeof(Function));
    node->bdd = bdd;
    node->operador = operador; // 1 p/ not, 2 p/ and, 3 p/ or
    node->varName = '\0'; // não usado para operadores
    node->left = left;
    node->right = right;
    return node;
}

void printFunction(Function* node) {
    if (node == NULL) return;

    if (node->operador == VAR) { 
        printf("%c", node->varName);
    } else if (node->operador == NOT) { 
        printf("!");
        // Parenthesis check for NOT
        bool par = (node->left->operador != VAR && node->left->operador != NOT);
        if(par) printf("(");
        printFunction(node->left);
        if(par) printf(")");
    } else { // AND or OR
        // Check priority for Left Child
        bool parLeft = (node->left->operador != VAR && node->left->operador != NOT && node->left->operador != node->operador);
        if (parLeft) printf("(");
        printFunction(node->left);
        if (parLeft) printf(")");

        printf(node->operador == AND ? "*" : "+");

        // Check priority for Right Child
        bool parRight = (node->right->operador != VAR && node->right->operador != NOT && node->right->operador != node->operador);
        if (parRight) printf("(");
        printFunction(node->right);
        if (parRight) printf(")");
    }
}


/*void fprintFunction(FILE *f, Function* node) {
    if (node == NULL) return;

    if (node->operador == VAR) { 
        fprintf(f, "%c", node->varName);
    } else if (node->operador == NOT) { 
        fprintf(f, "!");
        bool par = (node->left->operador != VAR && node->left->operador != NOT);
        if(par) fprintf(f, "(");
        fprintFunction(f, node->left);
        if(par) fprintf(f, ")");
    } else { // AND or OR
        bool parLeft = (node->left->operador != VAR && node->left->operador != NOT && node->left->operador != node->operador);
        if (parLeft) fprintf(f, "(");
        fprintFunction(f, node->left);
        if (parLeft) fprintf(f, ")");

        fprintf(f, node->operador == AND ? "*" : "+");

        bool parRight = (node->right->operador != VAR && node->right->operador != NOT && node->right->operador != node->operador);
        if (parRight) fprintf(f, "(");
        fprintFunction(f, node->right);
        if (parRight) fprintf(f, ")");
    }
}

void logExpressionToFile(Function *node, int ordem) {
    // Abre em modo 'a' (append) para não sobrescrever
    FILE *f = fopen("expressoes.log", "a"); 
    if (f == NULL) {
        perror("Erro ao abrir log");
        return;
    }
    
    fprintf(f, "[Ordem %d] ", ordem);
    fprintFunction(f, node);
    fprintf(f, "\n"); // Nova linha ao final da expressão
    
    fclose(f);
}*/
void initQueue(TaskQueue *q) {
    q->head = 0;
    q->tail = 0;
    q->count = 0;
    q->finished = false;
    omp_init_lock(&q->lock);
}

void enqueue(TaskQueue *q, TaskBatch *t) {
    // Spinlock simples se a fila estiver cheia para não bloquear indefinidamente
    while (1) {
        omp_set_lock(&q->lock);
        if (q->count < QUEUE_SIZE) {
           q->batches[q->tail] = *t; 
            q->tail = (q->tail + 1) % QUEUE_SIZE;
            q->count++;
            omp_unset_lock(&q->lock);
            break;
        }
        omp_unset_lock(&q->lock);
        // Pequena pausa para dar chance ao consumidor
        
        #pragma omp flush
    }
}

bool dequeue(TaskQueue *q, TaskBatch *t) {
    bool hasItem = false;
    omp_set_lock(&q->lock);
    if (q->count > 0) {
        *t = q->batches[q->head];
        q->head = (q->head + 1) % QUEUE_SIZE;
        q->count--;
        hasItem = true;
    }
    omp_unset_lock(&q->lock);
    return hasItem;
}
