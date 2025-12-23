# Compiler
CC = gcc

# --- CONFIGURAÇÃO DO CUDD ---
# O caminho para o diretório do CUDD (que agora está compilado e instalado)
# Atualizado para o caminho local do repositório: permita sobrescrever pela linha de comando (ex: make CUDD_PREFIX=/path/to/cudd)
CUDD_PREFIX = /home/miguel/cudd_installation/cudd
# --- FIM DA CONFIGURAÇÃO ---

# Executable name
TARGET = teste

# Source file
SRC = teste.c

# Compiler flags: 
# -g (debug), -Wall (warnings)
# Aponta para os locais REAIS dos cabeçalhos:
# -I.../epd/include (para cudd.h, como visto na imagem)
# -I.../st          (para st.h, como visto na imagem)
CFLAGS = -g -Wall \
	-I$(CUDD_PREFIX)/epd/include \
	-I$(CUDD_PREFIX)/st

# Linker flags:
# Aponta para a nova pasta 'lib' que foi criada
LDFLAGS = -L$(CUDD_PREFIX)/lib

# Linker libraries:
# Agora só precisamos de -lcudd, pois st e util estão incluídas nela.
LDLIBS = -fopenmp -lcudd -lm

# Phony targets are not actual files.
.PHONY: all clean run

# Default target: builds the executable.
all: $(TARGET)

# Rule to build the executable from the source file.
# O $(LDFLAGS) é adicionado aqui, antes de $(LDLIBS)
$(TARGET): $(SRC)
	$(CC) $(CFLAGS) -o $(TARGET) $(SRC) $(LDFLAGS) $(LDLIBS)

# Rule to clean up generated files.
clean:
	rm -f $(TARGET)

# Rule to run the executable.
# Use assim: make run ARGS="A*B+!C"
run: $(TARGET)
	./$(TARGET) "$(ARGS)"

