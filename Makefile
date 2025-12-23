# Compiler
CC = gcc

# --- CONFIGURAÇÃO DO CUDD ---
CUDD_PREFIX = /home/miguel/cudd_installation/cudd
# --- FIM DA CONFIGURAÇÃO ---

# Nomes dos executáveis que queremos gerar
EXEC1 = parallel
EXEC2 = teste

# Flags do Compilador
CFLAGS = -g -Wall -D_POSIX_C_SOURCE=199309L\
    -I$(CUDD_PREFIX)/epd/include \
    -I$(CUDD_PREFIX)/st
	

# Flags do Linker
LDFLAGS = -L$(CUDD_PREFIX)/lib

# Bibliotecas
LDLIBS = -fopenmp -lcudd -lm

# Phony targets
.PHONY: all clean run run_teste debug

# Target padrão: compila TODOS os executáveis listados
all: $(EXEC1) $(EXEC2)

# --- REGRAS DE COMPILAÇÃO ---

# Regra Genérica (Pattern Rule):
# "Para criar qualquer arquivo sem extensão (%) a partir de um .c (%.c)..."
# O $@ representa o alvo (ex: parallel) e o $< representa a fonte (ex: parallel.c)
%: %.c
	$(CC) $(CFLAGS) -o $@ $< $(LDFLAGS) $(LDLIBS)

# --- OPÇÃO DE DEBUG (Baseado na nossa conversa anterior) ---
# Se você rodar 'make debug', ele adiciona a flag -DDEBUG e recompila tudo
debug: CFLAGS += -DDEBUG
debug: clean all

# --- LIMPEZA ---
clean:
	rm -f $(EXEC1) $(EXEC2) *.o *.log

# --- EXECUÇÃO ---

# Roda o programa principal (Parallel)
# Uso: make run ARGS="A+B" OPTS="c"
run: $(EXEC1)
	./$(EXEC1) "$(ARGS)" $(OPTS)

# Roda o programa de teste
run_teste: $(EXEC2)
	./$(EXEC2)