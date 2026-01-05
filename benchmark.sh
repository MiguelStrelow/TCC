#!/bin/bash

# --- CONFIGURAÇÕES ---
ARQUIVO_ENTRADA="ninomiya_direct_isop.txt"
ARQUIVO_SAIDA="dados_tcc_amostra.csv" 
REPETICOES=20
THREADS_PARA_TESTAR=(2 4 6 8) 
MODO="e"
LIMITE_TEMPO="30s"
STEP=5  

# Executáveis
PROG_SEQ="./teste"       
PROG_PAR1="./parallel"   
PROG_PAR2="./parallel2"   
CHECKPOINT_FILE=".benchmark_sampling.checkpoint"

HEADER="Algoritmo;Threads;Input_Expressao;Repeticao;Tempo;Expressao_Encontrada;Literais;Clock_Time;Service_Time;Wait_Time"

# --- LEITURA DO CHECKPOINT ---
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

# Validação dos arquivos
for exe in "$PROG_SEQ" "$PROG_PAR1" "$PROG_PAR2"; do
    if [[ ! -f "$exe" ]]; then
        echo "Erro: Executável $exe não encontrado."
        exit 1
    fi
done

if [[ ! -f "$ARQUIVO_ENTRADA" ]]; then
    echo "Erro: Arquivo $ARQUIVO_ENTRADA não encontrado."
    exit 1
fi

echo "Iniciando Benchmark (Seq, Par1, Par2) com AMOSTRAGEM (Passo $STEP)..."
echo "Timeout: $LIMITE_TEMPO | Threads: ${THREADS_PARA_TESTAR[*]}"

mapfile -t LINES < "$ARQUIVO_ENTRADA"
TOTAL_LINES=${#LINES[@]}

# Defaults caso não existam no checkpoint
: "${LINE_IDX:=0}"
: "${STAGE:=seq}"
: "${THREAD:=0}"
: "${REP:=1}"

# --- LOOP PRINCIPAL (INPUTS) ---
for (( idx=0; idx<TOTAL_LINES; idx+=STEP )); do
    
    # Pula linhas já processadas
    if (( idx < LINE_IDX )); then
        continue
    fi

    lineno=$((idx+1))
    expressao="${LINES[idx]}"

    if [[ -z "$expressao" ]]; then continue; fi

    echo "--------------------------------------------------"
    echo "Testando Input ($lineno/$TOTAL_LINES): $expressao"

    # =================================================================
    # 1. SEQUENCIAL
    # =================================================================
    # Verifica se devemos pular o sequencial (se o checkpoint já está em par1 ou par2 na mesma linha)
    should_run_seq=true
    if (( idx == LINE_IDX )); then
        if [[ "$STAGE" == "par1" ]] || [[ "$STAGE" == "par2" ]]; then
            should_run_seq=false
        fi
    fi

    if $should_run_seq; then
        start_rep=1
        # Se parou no meio do sequencial desta linha
        if (( idx == LINE_IDX )) && [[ "$STAGE" == "seq" ]]; then
            start_rep=$REP
        fi

        if (( start_rep <= REPETICOES )); then
            for (( i=start_rep; i<=REPETICOES; i++ )); do
                save_checkpoint "$idx" "seq" 1 "$i"
                
                OUTPUT=$(timeout "$LIMITE_TEMPO" $PROG_SEQ "$expressao" "$MODO")
                EXIT_CODE=$? 

                if [ $EXIT_CODE -eq 124 ]; then
                    echo "  -> Sequencial: TIMEOUT ($i/$REPETICOES)"
                    echo "Sequencial;1;$expressao;$i;TIMEOUT;N/A;N/A;N/A;N/A;N/A" >> "$ARQUIVO_SAIDA"
                else
                    TEMPO=$(echo "$OUTPUT" | grep "BENCHMARK_TIME:" | awk '{print $2}')
                    if [[ -z "$TEMPO" ]]; then TEMPO="ERRO"; fi
                    TEMPO_FMT=${TEMPO/./,} 

                    LITERAIS=$(echo "$OUTPUT" | grep "RESULTADO_LITERAIS:" | awk '{print $2}')
                    if [[ -z "$LITERAIS" ]]; then LITERAIS="N/A"; fi

                    RES_EXPR=$(echo "$OUTPUT" | grep "RESULTADO_EXPRESSAO:" | cut -d' ' -f2-)
                    if [[ -z "$RES_EXPR" ]]; then RES_EXPR="N/A"; fi

                    # Métricas extras
                    CLOCK_TIME="0"; SERVICE_TIME="0"; WAIT_TIME="0"
                    if [[ "$TEMPO" != "ERRO" ]]; then
                        VAL=$(echo "$OUTPUT" | grep "RESULTADO_CLOCK_TIME:" | awk '{print $2}')
                        if [[ -n "$VAL" ]]; then CLOCK_TIME=${VAL/./,}; fi
                        VAL=$(echo "$OUTPUT" | grep "RESULTADO_SERVICE_TIME:" | awk '{print $2}')
                        if [[ -n "$VAL" ]]; then SERVICE_TIME=${VAL/./,}; fi
                        # Wait time pode vir zero ou calculado
                        VAL=$(echo "$OUTPUT" | grep "RESULTADO_WAIT_TIME:" | awk '{print $2}')
                        if [[ -n "$VAL" ]]; then WAIT_TIME=${VAL/./,}; fi
                    fi

                    echo "Sequencial;1;$expressao;$i;$TEMPO_FMT;$RES_EXPR;$LITERAIS;$CLOCK_TIME;$SERVICE_TIME;$WAIT_TIME" >> "$ARQUIVO_SAIDA"
                fi
            done
            echo "  -> Sequencial concluído."
        fi
        # Ao terminar Seq, prepara checkpoint para Par1
        save_checkpoint "$idx" "par1" 0 1
        STAGE="par1" # Atualiza variável em memória para o fluxo seguir sem recarregar script
        THREAD=0
        REP=1
    else
        echo "  -> Sequencial já processado (Pulando)."
    fi

    # =================================================================
    # 2. PARALELO V1 (./parallel)
    # =================================================================
    should_run_par1=true
    if (( idx == LINE_IDX )); then
        if [[ "$STAGE" == "par2" ]]; then
            should_run_par1=false
        fi
    fi

    if $should_run_par1; then
        for t in "${THREADS_PARA_TESTAR[@]}"; do
            # Lógica de Resume para Threads
            if (( idx == LINE_IDX )) && [[ "$STAGE" == "par1" ]]; then
                if (( THREAD > t )); then continue; fi
            fi

            start_rep=1
            if (( idx == LINE_IDX )) && [[ "$STAGE" == "par1" ]] && (( THREAD == t )); then
                start_rep=$REP
            fi

            export OMP_NUM_THREADS=$t

            for (( i=start_rep; i<=REPETICOES; i++ )); do
                save_checkpoint "$idx" "par1" "$t" "$i"
                
                OUTPUT=$(timeout "$LIMITE_TEMPO" $PROG_PAR1 "$expressao" "$MODO")
                EXIT_CODE=$?

                if [ $EXIT_CODE -eq 124 ]; then
                    echo "  -> Paralelo_V1 ($t threads): TIMEOUT ($i/$REPETICOES)"
                    echo "Paralelo_V1;$t;$expressao;$i;TIMEOUT;N/A;N/A;N/A;N/A;N/A" >> "$ARQUIVO_SAIDA"
                else
                    TEMPO=$(echo "$OUTPUT" | grep "BENCHMARK_TIME:" | awk '{print $2}')
                    if [[ -z "$TEMPO" ]]; then TEMPO="ERRO"; fi
                    TEMPO_FMT=${TEMPO/./,}

                    LITERAIS=$(echo "$OUTPUT" | grep "RESULTADO_LITERAIS:" | awk '{print $2}')
                    if [[ -z "$LITERAIS" ]]; then LITERAIS="N/A"; fi
                    RES_EXPR=$(echo "$OUTPUT" | grep "RESULTADO_EXPRESSAO:" | cut -d' ' -f2-)
                    if [[ -z "$RES_EXPR" ]]; then RES_EXPR="N/A"; fi
                    
                    CLOCK_TIME=$(echo "$OUTPUT" | grep "RESULTADO_CLOCK_TIME:" | awk '{print $2}'); CLOCK_TIME=${CLOCK_TIME:-0}; CLOCK_TIME=${CLOCK_TIME/./,}
                    SERVICE_TIME=$(echo "$OUTPUT" | grep "RESULTADO_SERVICE_TIME:" | awk '{print $2}'); SERVICE_TIME=${SERVICE_TIME:-0}; SERVICE_TIME=${SERVICE_TIME/./,}
                    WAIT_TIME=$(echo "$OUTPUT" | grep "RESULTADO_WAIT_TIME:" | awk '{print $2}'); WAIT_TIME=${WAIT_TIME:-0}; WAIT_TIME=${WAIT_TIME/./,}

                    echo "Paralelo_V1;$t;$expressao;$i;$TEMPO_FMT;$RES_EXPR;$LITERAIS;$CLOCK_TIME;$SERVICE_TIME;$WAIT_TIME" >> "$ARQUIVO_SAIDA"
                fi
            done
            echo "  -> Paralelo_V1 ($t threads) concluído."
        done
        
        # Ao terminar Par1, prepara checkpoint para Par2
        save_checkpoint "$idx" "par2" 0 1
        STAGE="par2"
        THREAD=0
        REP=1
    else
        echo "  -> Paralelo_V1 já processado (Pulando)."
    fi

    # =================================================================
    # 3. PARALELO V2 (./parallel2)
    # =================================================================
    # Aqui não precisamos de check "should_run" complexo, pois é o último estágio
    
    for t in "${THREADS_PARA_TESTAR[@]}"; do
        # Lógica de Resume para Threads
        if (( idx == LINE_IDX )) && [[ "$STAGE" == "par2" ]]; then
            if (( THREAD > t )); then continue; fi
        fi

        start_rep=1
        if (( idx == LINE_IDX )) && [[ "$STAGE" == "par2" ]] && (( THREAD == t )); then
            start_rep=$REP
        fi

        export OMP_NUM_THREADS=$t

        for (( i=start_rep; i<=REPETICOES; i++ )); do
            save_checkpoint "$idx" "par2" "$t" "$i"
            
            OUTPUT=$(timeout "$LIMITE_TEMPO" $PROG_PAR2 "$expressao" "$MODO")
            EXIT_CODE=$?

            if [ $EXIT_CODE -eq 124 ]; then
                echo "  -> Paralelo_V2 ($t threads): TIMEOUT ($i/$REPETICOES)"
                echo "Paralelo_V2;$t;$expressao;$i;TIMEOUT;N/A;N/A;N/A;N/A;N/A" >> "$ARQUIVO_SAIDA"
            else
                TEMPO=$(echo "$OUTPUT" | grep "BENCHMARK_TIME:" | awk '{print $2}')
                if [[ -z "$TEMPO" ]]; then TEMPO="ERRO"; fi
                TEMPO_FMT=${TEMPO/./,}

                LITERAIS=$(echo "$OUTPUT" | grep "RESULTADO_LITERAIS:" | awk '{print $2}')
                if [[ -z "$LITERAIS" ]]; then LITERAIS="N/A"; fi
                RES_EXPR=$(echo "$OUTPUT" | grep "RESULTADO_EXPRESSAO:" | cut -d' ' -f2-)
                if [[ -z "$RES_EXPR" ]]; then RES_EXPR="N/A"; fi

                CLOCK_TIME=$(echo "$OUTPUT" | grep "RESULTADO_CLOCK_TIME:" | awk '{print $2}'); CLOCK_TIME=${CLOCK_TIME:-0}; CLOCK_TIME=${CLOCK_TIME/./,}
                SERVICE_TIME=$(echo "$OUTPUT" | grep "RESULTADO_SERVICE_TIME:" | awk '{print $2}'); SERVICE_TIME=${SERVICE_TIME:-0}; SERVICE_TIME=${SERVICE_TIME/./,}
                WAIT_TIME=$(echo "$OUTPUT" | grep "RESULTADO_WAIT_TIME:" | awk '{print $2}'); WAIT_TIME=${WAIT_TIME:-0}; WAIT_TIME=${WAIT_TIME/./,}

                echo "Paralelo_V2;$t;$expressao;$i;$TEMPO_FMT;$RES_EXPR;$LITERAIS;$CLOCK_TIME;$SERVICE_TIME;$WAIT_TIME" >> "$ARQUIVO_SAIDA"
            fi
        done
        echo "  -> Paralelo_V2 ($t threads) concluído."
    done

    # Prepara para próxima linha (idx + step) e reseta status para seq
    next_idx=$((idx + STEP))
    save_checkpoint "$next_idx" "seq" 0 1
    # Reseta variáveis em memória caso o loop continue sem restart
    STAGE="seq"
    THREAD=0
    REP=1

done

echo "=================================================="
echo "Benchmark Completo (Seq + Par1 + Par2) Finalizado!"
clear_checkpoint