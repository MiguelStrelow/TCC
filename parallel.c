#include <stdio.h>
#include <stdlib.h>
#include <stdbool.h>
#include <string.h>
#include <cudd.h>
#include <st.h>
//OpenMP mais por simplicidade e adequação ao código, altamente dependente de loops, que acredito serem paralelizáveis.
//Eventualmente pode ser explorado o uso de MPI, para uma abordagem distribuída, mas isso seria um próximo trabalho. 
#include <omp.h>


#define PARALLEL_MIN_COMBINATIONS 3000

/* Iniciando a versão paralela do código. A partir daqui, não temos mais guias. O primeiro passo seria localizar os pontos críticos que podem gerar
condições de corrida. Vou fazer isso analisando novamente o código. Como o CUDD não é uma biblioteca thread-safe, vai dar um trabalhão, e o ganho
pode acabar não sendo tão grande quanto esperado inicialmente, mas agora não dá tempo de mudar :) 
Principais pontos que acredito serem críticos, antes da análise aprofundada:
-> O CUDD manager, provavelmente vai dar problema se for acessado simultaneamente
-> A hash de verificação, deve ser ok para verificações, mas a escrita provavelmente precisa ser única
Possíveis soluções iniciais:
Manager: 
-> Usar um sistema básico de travas/semáforos -> Preciso estudar um pouco mais sobre pra saber o que seria adequado
-> Cada Thread ter seu próprio manager -> Possivelmente piora o problema da explosão de memória, então não pretendo usar a princípio. Pode ser viável
se conseguir uma máquina com mais recursos disponíveis.

Hash:
-> Permitir consultas a qualquer momento, mas barrar escritas. -> Inevitavelmente vai causar lentidão, mas a ideia é que isso seja compensado pelo
paralelismo de tarefas / testar
-> Retornar à hash única para cada bucket. Problemas de memória, muitas combinações desnecessárias, mas pode ser útil eventualmente*/
typedef enum{
    VAR,
    NOT,
    AND,
    OR
} OpType;

typedef struct Implementation
{
    OpType operador; // 0 p/ var, 1 p/ not, 2 p/ and, 3 p/ or -> Substituido por enum pra ficar mais claro
    char varName; //Apenas se operador == VAR
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
Implementation* varNode(char varName);
//Criar um novo nó caso seja operador
Implementation* opNode(OpType operador, Implementation* left, Implementation* right);
//Printar a implementação
void printImplementation(Implementation* node);

int main(int argc, char *argv[])
{
    //A princípio toda a primeira parte da execução é sequencial, paralelizar iria gerar overhead desnecessário
    if (argc < 2)
    {
        fprintf(stderr, "Uso: %s <expressão>\n", argv[0]);
        return EXIT_FAILURE;
    }
    /* Primeiro possível ponto crítico é esse carinha aqui.
    Usar uma trava para evitar acessos simultâneos dentro da função, permitindo que o resto das
    operações sejam paralelas. Vai reduzir bastante o ganho potencial, para ganhos maiores o ideal é usar uma biblioteca de CUDD thread-safe.
    */
    DdManager *manager = Cudd_Init(0, 0, CUDD_UNIQUE_SLOTS, CUDD_CACHE_SLOTS, 0);
    if (manager == NULL)
    {
        fprintf(stderr, "Erro ao inicializar o CUDD.\n");
        return EXIT_FAILURE;
    }

    //tabela hash para verificar duplicatas, migrada para cá pra permitir verificação entre buckets
    //Segundo possível ponto crítico de corrida
    /*Soluções possíveis:
    -> Usar travas/semáforos para controlar o acesso à tabela hash durante operações de escrita.
    -> Cada thread ter sua própria tabela hash e depois mesclar os resultados (mais trabalhoso).
    -> Utilizar uma biblioteca de hash thread-safe (complicado de integrar, mas pode ser necessário).
    -> Manter a tabela hash única, mas limitar as operações de escrita a momentos específicos, usando barreiras.
    */
    st_table *uniqueCheck = st_init_table(st_ptrcmp, st_ptrhash);
    if (uniqueCheck == NULL)
    {
        fprintf(stderr, "Erro ao criar tabela hash ST\n");
        Cudd_Quit(manager);
        return EXIT_FAILURE;
    }
    char choice;
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
        printf("Parar ao encontrar equivalencia, ou completar o bucket? (e/c): ");
        scanf(" %c", &choice);  
    for (int order = 2; order <= varCount+1; order++)
    {
        //Aqui começa a parte paralela
        //Dentro da função, quero que cada thread trate de combinar buckets diferentes
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
    // Vou ter que rever todos os frees mais pra frente
    st_free_table(uniqueCheck);

 
    Cudd_RecursiveDeref(manager, objectiveExp);
    freeAllBuckets(manager, buckets, numBuckets);
    free(varMap);
    Cudd_Quit(manager);
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

    bool stop = false;

    for (int i = 0; i < numBuckets; i++)
    {
        int order1 = buckets[i].order; 
        int order2 = targetOrder - order1;
        if (order2 < order1) break; // Evita repetições desnecessárias
        int j = order2 - 1;
        
                Bucket *b1 = &buckets[i];
                Bucket *b2 = &buckets[j];
                
                if (stop) break; // Sai do loop se a flag de parada foi ativada na iteração passada

                if (b1->size == 0 || b2->size == 0) continue;

                // Calcula a carga de trabalho estimada
                //long long por que o valor cresce de forma explosiva
                long long total_iterations = (long long)b1->size * (long long)b2->size;
                // Parallel inicia o paralelismo, for define o loop a ser paralelizado
                // collapse(2) combina os loops aninhados, schedule(dynamid) distribui a carga dinamicamente
                //if() define que só paralelize trabalho que compense o overhead (Valor estimado com base em testes)
                #pragma omp parallel for collapse(2) schedule(dynamic) if(total_iterations > PARALLEL_MIN_COMBINATIONS)
                for (int k = 0; k < b1->size; k++)
                {
                    // Se buckets iguais, l começa de k
                    int startL = (i == j) ? k : 0;
                    
                    for (int l = startL; l < b2->size; l++)
                    {
                        // Verifica se a flag de parada foi ativada
                        #pragma omp flush(stop)
                        if (stop) continue;

                        Function *f1 = b1->functions[k];
                        Function *f2 = b2->functions[l];
                        
                        for (int op = 0; op < 2; op++)
                        {
                            if (stop) continue; //Só pra garantir
                            DdNode *newBdd = NULL;
                            char opChar = (op == 0) ? '*' : '+';
                            //Crítico pois precisa acessar o manager, que é compartilhado
                            #pragma omp critical(combine_bdds)
                            {
                                if(!stop)
                                newBdd = combineBdds(manager, f1->bdd, f2->bdd, opChar);
                            }
                            if (newBdd == NULL) continue; //Caso tenha parado dentro do critical
                            
                            //Parada imediata caso encontre equivalência
                            if (newBdd == objectiveExp && choice == 'e')
                            {
                                #pragma omp critical(success_report)
                                {
                                    if(!stop) 
                                    {
                                        stop = true; // Ativa a flag de parada
                                    
                                        printf("\n!!! EQUIVALÊNCIA ENCONTRADA (Ordem %d) !!!\n", targetOrder);
                                
                                        Implementation *temp = opNode((opChar == '*') ? AND : OR, f1->impRoot, f2->impRoot); 
                                        printImplementation(temp);

                                        printf("\nNo de literais: %d\n", targetOrder);
                                
                                         // Limpa o que foi alocado temporariamente e retorna true
                                        free(temp);
                                    }
                                }

                                #pragma omp critical(deref_bdd)
                                {
                                    Cudd_RecursiveDeref(manager, newBdd);
                                }
                                #pragma omp flush(stop)
                                continue;
                            }

                            if (newBdd != Cudd_ReadLogicZero(manager) || newBdd != Cudd_ReadOne(manager))
                            {
                                bool inserted = false;

                                #pragma omp critical(hash_insert)
                                {
                                    st_insert(uniqueCheck, (char *)newBdd, (char *)newBdd);
                                    
                                    Function *newFunction = (Function *)malloc(sizeof(Function));
                                    newFunction->bdd = newBdd;
                                    newFunction->impRoot = opNode((opChar == '*') ? AND : OR, f1->impRoot, f2->impRoot);

                                
                                    addFunctionToDynamicArray(newFunction, &newFunctions, &newFuncCount, &newFuncCapacity);
                                    inserted = true;    
                                }
                            
                                if(!inserted)
                                {
                                    #pragma omp critical(deref_bdd)
                                    {
                                        Cudd_RecursiveDeref(manager, newBdd);
                                    }
                                }
                            }

                            else
                            {
                                #pragma omp critical(deref_bdd)
                                {
                                    Cudd_RecursiveDeref(manager, newBdd);
                                }
                            }
                            
                        }
                    }
                }
            }

            if(stop){
                //Limpar o que foi alocado
                for(int i=0; i<newFuncCount; i++) {
                    Cudd_RecursiveDeref(manager, newFunctions[i]->bdd);
                    free(newFunctions[i]->impRoot);
                    free(newFunctions[i]);
                }
                free(newFunctions);
                return true;    
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