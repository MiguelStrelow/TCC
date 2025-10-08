# Minimal Makefile that runs exactly: gcc teste.c -o teste

.PHONY: all clean run

all: teste

teste:
	gcc teste.c -o teste

clean:
	rm -f teste

run: teste
	./teste
