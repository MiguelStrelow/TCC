#!/bin/bash

# --- CONFIGURAÇÕES ---
ARQUIVO_ENTRADA="ninomiya_direct_isop.txt"
ARQUIVO_SAIDA="dados_tcc_amostra.csv" 
REPETICOES=20
THREADS_PARA_TESTAR=(1 2 4 8) 
MODO="e"
LIMITE_TEMPO="30s"
STEP=5  

# Executáveis
PROG_SEQ="./teste"      
PROG_PAR="./parallel"   
CHECKPOINT_FILE=".benchmark_sampling.checkpoint"


HEADER="Algoritmo;Threads;Input_Expressao;Repeticao;Tempo;Expressao_Encontrada;Literais;Clock_Time;Service_Time;Wait_Time"

if [[ -f "$CHECKPOINT_FILE" ]]; then
    echo "Resuming from checkpoint: $CHECKPOINT_FILE"
    # shellcheck disable=SC1090
    source "$CHECKPOINT_FILE" || true
    if [[ ! -f "$ARQUIVO_SAIDA" ]]; then
        echo "$HEADER" > "$ARQUIVO_SAIDA"
    fi
else
    echo "$HEADER" > "$ARQUIVO_SAIDA"
    unset LINE_IDX STAGE THREAD REP
fi

save_checkpoint() {
    local idx="$1" stage="$2" thread="$3" rep="$4"
    {
        echo "LINE_IDX=$idx"
        echo "STAGE=$stage"
        echo "THREAD=$thread"
        echo "REP=$rep"
        echo "TS=$(date +%s)"
    } > "$CHECKPOINT_FILE" || echo "Warning: failed to write checkpoint"
}

clear_checkpoint() {
    rm -f "$CHECKPOINT_FILE"
}

if [[ ! -f "$PROG_SEQ" ]] || [[ ! -f "$PROG_PAR" ]]; then
    echo "Erro: Executáveis não encontrados."
    exit 1
fi

if [[ ! -f "$ARQUIVO_ENTRADA" ]]; then
    echo "Erro: Arquivo $ARQUIVO_ENTRADA não encontrado."
    exit 1
fi

echo "Iniciando Benchmark com AMOSTRAGEM (Passo $STEP)..."
echo "Timeout configurado para: $LIMITE_TEMPO"

mapfile -t LINES < "$ARQUIVO_ENTRADA"
TOTAL_LINES=${#LINES[@]}

: "${LINE_IDX:=0}"
: "${STAGE:=none}"
: "${THREAD:=0}"
: "${REP:=1}"

# --- LOOP COM AMOSTRAGEM ---
for (( idx=0; idx<TOTAL_LINES; idx+=STEP )); do
    
    if (( idx < LINE_IDX )); then
        continue
    fi

    lineno=$((idx+1))
    expressao="${LINES[idx]}"

    if [[ -z "$expressao" ]]; then continue; fi

    echo "--------------------------------------------------"
    echo "Testando Input ($lineno/$TOTAL_LINES): $expressao"

    # --------------------- SEQUENCIAL ---------------------
    seq_start=1
    if (( idx == LINE_IDX )) && [[ "$STAGE" == "seq" ]]; then
        seq_start=$REP
    fi
    if (( idx == LINE_IDX )) && [[ "$STAGE" == "paralelo" ]]; then
        seq_start=$((REPETICOES+1))
    fi

    if (( seq_start <= REPETICOES )); then
        for (( i=seq_start; i<=REPETICOES; i++ )); do
            save_checkpoint "$idx" "seq" 1 "$i"
            
            OUTPUT=$(timeout "$LIMITE_TEMPO" $PROG_SEQ "$expressao" "$MODO")
            EXIT_CODE=$? 

            if [ $EXIT_CODE -eq 124 ]; then
                echo "  -> Sequencial: TIMEOUT ($i/$REPETICOES)"
                # --- 2. TIMEOUT AJUSTADO (11 N/As para preencher as colunas extras) ---
                echo "Sequencial;1;$expressao;$i;TIMEOUT;N/A;N/A;N/A;N/A;N/A;N/A;N/A;N/A" >> "$ARQUIVO_SAIDA"
            else
                TEMPO=$(echo "$OUTPUT" | grep "BENCHMARK_TIME:" | awk '{print $2}')
                if [[ -z "$TEMPO" ]]; then TEMPO="ERRO"; fi
                TEMPO_FMT=${TEMPO/./,} 

                LITERAIS=$(echo "$OUTPUT" | grep "RESULTADO_LITERAIS:" | awk '{print $2}')
                if [[ -z "$LITERAIS" ]]; then LITERAIS="N/A"; fi

                RES_EXPR=$(echo "$OUTPUT" | grep "RESULTADO_EXPRESSAO:" | cut -d' ' -f2-)
                if [[ -z "$RES_EXPR" ]]; then RES_EXPR="N/A"; fi

                # --- 3. CAPTURA DE MÉTRICAS ---

                # Inicializa zerado (sem Lock e Total_CPU)
                CLOCK_TIME="0"; SERVICE_TIME="0"; WAIT_TIME="0"

                if [[ "$TEMPO" != "ERRO" ]]; then
                    VAL=$(echo "$OUTPUT" | grep "RESULTADO_CLOCK_TIME:" | awk '{print $2}')
                    if [[ -n "$VAL" ]]; then CLOCK_TIME=${VAL/./,}; fi

                    VAL=$(echo "$OUTPUT" | grep "RESULTADO_SERVICE_TIME:" | awk '{print $2}')
                    if [[ -n "$VAL" ]]; then SERVICE_TIME=${VAL/./,}; fi
                fi

                echo "Sequencial;1;$expressao;$i;$TEMPO_FMT;$RES_EXPR;$LITERAIS;$CLOCK_TIME;$SERVICE_TIME;$WAIT_TIME" >> "$ARQUIVO_SAIDA"
            fi
        done
        echo "  -> Sequencial concluído."
    else
        echo "  -> Sequencial já concluído."
    fi

    save_checkpoint "$idx" "paralelo" 0 1

    # ---------------------- PARALELO ----------------------
    for t in "${THREADS_PARA_TESTAR[@]}"; do
        start_rep=1
        if (( idx == LINE_IDX )) && [[ "$STAGE" == "paralelo" ]]; then
            if (( THREAD > t )); then
                continue
            elif (( THREAD == t )); then
                start_rep=$REP
            fi
        fi

        export OMP_NUM_THREADS=$t

        for (( i=start_rep; i<=REPETICOES; i++ )); do
            save_checkpoint "$idx" "paralelo" "$t" "$i"
            
            OUTPUT=$(timeout "$LIMITE_TEMPO" $PROG_PAR "$expressao" "$MODO")
            EXIT_CODE=$?

            if [ $EXIT_CODE -eq 124 ]; then
                echo "  -> Paralelo ($t threads): TIMEOUT ($i/$REPETICOES)"
                # --- TIMEOUT AJUSTADO ---
                echo "Paralelo;$t;$expressao;$i;TIMEOUT;N/A;N/A;N/A;N/A;N/A" >> "$ARQUIVO_SAIDA"
            else
                TEMPO=$(echo "$OUTPUT" | grep "BENCHMARK_TIME:" | awk '{print $2}')
                if [[ -z "$TEMPO" ]]; then TEMPO="ERRO"; fi
                TEMPO_FMT=${TEMPO/./,}

                LITERAIS=$(echo "$OUTPUT" | grep "RESULTADO_LITERAIS:" | awk '{print $2}')
                if [[ -z "$LITERAIS" ]]; then LITERAIS="N/A"; fi

                RES_EXPR=$(echo "$OUTPUT" | grep "RESULTADO_EXPRESSAO:" | cut -d' ' -f2-)
                if [[ -z "$RES_EXPR" ]]; then RES_EXPR="N/A"; fi

                # --- CAPTURA COMPLETA (sem Lock e Total_CPU) ---

                CLOCK_TIME=$(echo "$OUTPUT" | grep "RESULTADO_CLOCK_TIME:" | awk '{print $2}')
                if [[ -z "$CLOCK_TIME" ]]; then CLOCK_TIME="0"; fi
                CLOCK_TIME_FMT=${CLOCK_TIME/./,}

                SERVICE_TIME=$(echo "$OUTPUT" | grep "RESULTADO_SERVICE_TIME:" | awk '{print $2}')
                if [[ -z "$SERVICE_TIME" ]]; then SERVICE_TIME="0"; fi
                SERVICE_TIME_FMT=${SERVICE_TIME/./,}

                WAIT_TIME=$(echo "$OUTPUT" | grep "RESULTADO_WAIT_TIME:" | awk '{print $2}')
                if [[ -z "$WAIT_TIME" ]]; then WAIT_TIME="0"; fi
                WAIT_TIME_FMT=${WAIT_TIME/./,}

                echo "Paralelo;$t;$expressao;$i;$TEMPO_FMT;$RES_EXPR;$LITERAIS;$CLOCK_TIME_FMT;$SERVICE_TIME_FMT;$WAIT_TIME_FMT" >> "$ARQUIVO_SAIDA"
            fi
        done
        echo "  -> Paralelo ($t threads) concluído."
    done

    next_idx=$((idx + STEP))
    save_checkpoint "$next_idx" "none" 0 1

done

echo "=================================================="
echo "Benchmark por Amostragem Finalizado!"
clear_checkpoint