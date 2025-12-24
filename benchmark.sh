#!/bin/bash

# --- CONFIGURAÇÕES ---
ARQUIVO_ENTRADA="ninomiya_direct_isop.txt"
ARQUIVO_SAIDA="dados_tcc.csv"
REPETICOES=20
THREADS_PARA_TESTAR=(1 2 4 8) 
MODO="e"

# Executáveis
PROG_SEQ="./teste"      
PROG_PAR="./parallel"   

# --- ALTERAÇÃO 1: Novo Cabeçalho do CSV ---
# Adicionado: Expressao_Encontrada;Literais
echo "Algoritmo;Threads;Input_Expressao;Repeticao;Tempo;Expressao_Encontrada;Literais" > "$ARQUIVO_SAIDA"

# Verifica se os arquivos existem
if [[ ! -f "$PROG_SEQ" ]] || [[ ! -f "$PROG_PAR" ]]; then
    echo "Erro: Executáveis não encontrados. Compile com 'make' antes."
    exit 1
fi

if [[ ! -f "$ARQUIVO_ENTRADA" ]]; then
    echo "Erro: Arquivo $ARQUIVO_ENTRADA não encontrado."
    exit 1
fi

echo "Iniciando bateria de testes..."

# ==============================================================================
# LENDO AS EXPRESSÕES LINHA POR LINHA
# ==============================================================================
while IFS= read -r expressao || [ -n "$expressao" ]; do
    
    if [[ -z "$expressao" ]]; then continue; fi

    echo "--------------------------------------------------"
    echo "Testando Input: $expressao"

    # ==========================================================
    # 1. TESTE SEQUENCIAL
    # ==========================================================
    for (( i=1; i<=REPETICOES; i++ )); do
        OUTPUT=$($PROG_SEQ "$expressao" "$MODO")
        
        # --- ALTERAÇÃO 2: Captura das novas variáveis ---
        
        # 1. Tempo
        TEMPO=$(echo "$OUTPUT" | grep "BENCHMARK_TIME:" | awk '{print $2}')
        if [[ -z "$TEMPO" ]]; then TEMPO="ERRO"; fi
        TEMPO_FMT=${TEMPO/./,}

        # 2. Literais (Pega a 2ª palavra da linha RESULTADO_LITERAIS:)
        LITERAIS=$(echo "$OUTPUT" | grep "RESULTADO_LITERAIS:" | awk '{print $2}')
        if [[ -z "$LITERAIS" ]]; then LITERAIS="N/A"; fi

        # 3. Expressão (Pega tudo depois do prefixo RESULTADO_EXPRESSAO: )
        # O 'cut -d' ' -f2-' pega do segundo campo até o fim, usando espaço como delimitador
        RES_EXPR=$(echo "$OUTPUT" | grep "RESULTADO_EXPRESSAO:" | cut -d' ' -f2-)
        if [[ -z "$RES_EXPR" ]]; then RES_EXPR="N/A"; fi

        # Salva no CSV
        echo "Sequencial;1;$expressao;$i;$TEMPO_FMT;$RES_EXPR;$LITERAIS" >> "$ARQUIVO_SAIDA"
    done
    echo "  -> Sequencial concluído."

    # ==========================================================
    # 2. TESTE PARALELO
    # ==========================================================
    for t in "${THREADS_PARA_TESTAR[@]}"; do
        
        export OMP_NUM_THREADS=$t
        
        for (( i=1; i<=REPETICOES; i++ )); do
            OUTPUT=$($PROG_PAR "$expressao" "$MODO")
            
            # --- ALTERAÇÃO 3: Captura repetida para o paralelo ---
            
            # 1. Tempo
            TEMPO=$(echo "$OUTPUT" | grep "BENCHMARK_TIME:" | awk '{print $2}')
            if [[ -z "$TEMPO" ]]; then TEMPO="ERRO"; fi
            TEMPO_FMT=${TEMPO/./,}

            # 2. Literais
            LITERAIS=$(echo "$OUTPUT" | grep "RESULTADO_LITERAIS:" | awk '{print $2}')
            if [[ -z "$LITERAIS" ]]; then LITERAIS="N/A"; fi

            # 3. Expressão
            RES_EXPR=$(echo "$OUTPUT" | grep "RESULTADO_EXPRESSAO:" | cut -d' ' -f2-)
            if [[ -z "$RES_EXPR" ]]; then RES_EXPR="N/A"; fi

            echo "Paralelo;$t;$expressao;$i;$TEMPO_FMT;$RES_EXPR;$LITERAIS" >> "$ARQUIVO_SAIDA"
        done
        echo "  -> Paralelo ($t threads) concluído."
    done

done < "$ARQUIVO_ENTRADA"

echo "=================================================="
echo "Testes finalizados! Dados salvos em $ARQUIVO_SAIDA"