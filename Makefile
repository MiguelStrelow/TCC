# Compiler
CC = gcc

# Executable name
TARGET = teste

# Source file
SRC = teste.c

# Compiler flags: -g for debugging, -Wall for all warnings.
# You may need to add -I/path/to/cudd/include if headers are not in a standard location.
CFLAGS = -g -Wall

# Linker flags and libraries:
# -lcudd: Links the CUDD library
# -lm:    Links the math library
# You may need to add -L/path/to/cudd/lib if libraries are not in a standard location.
LDLIBS = -lcudd -lm

# Phony targets are not actual files.
.PHONY: all clean run

# Default target: builds the executable.
all: $(TARGET)

# Rule to build the executable from the source file.
$(TARGET): $(SRC)
	$(CC) $(CFLAGS) -o $(TARGET) $(SRC) $(LDLIBS)

# Rule to clean up generated files.
clean:
	rm -f $(TARGET)

# Rule to run the executable.
run: $(TARGET)
	./$(TARGET)