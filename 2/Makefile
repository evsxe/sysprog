GCC_FLAGS = -Wextra -Werror -Wall -Wno-gnu-folding-constant -ldl -rdynamic
INCLUDE_DIRS = -I../utils/heap_help/
SOURCE_DIR = ./
UTILS_DIR = ../utils/heap_help

all: main

main: $(SOURCE_DIR)/parser.c $(SOURCE_DIR)/solution.c $(UTILS_DIR)/heap_help.c
	gcc $(GCC_FLAGS) $(INCLUDE_DIRS) $^ -o $@

.PHONY: clean
clean:
	rm -f main out.txt