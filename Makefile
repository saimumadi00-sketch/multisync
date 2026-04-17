CC      = gcc
CFLAGS  = -std=c11 -Wall -Wextra -Wpedantic -g -Iinclude -D_POSIX_C_SOURCE=200809L
LDFLAGS = -lpthread

SRC     = src/main.c src/rle.c src/worker.c src/logger.c
OBJ     = $(SRC:.c=.o)
TARGET  = multisync

.PHONY: all clean test valgrind

all: $(TARGET)

$(TARGET): $(OBJ)
	$(CC) $(CFLAGS) -o $@ $^ $(LDFLAGS)

%.o: %.c
	$(CC) $(CFLAGS) -c -o $@ $<

test: all
	@bash tests/run_tests.sh

valgrind: all
	@echo "=== Valgrind memcheck ==="
	valgrind --leak-check=full --error-exitcode=1 \
	         ./multisync tests/data/hello.txt tests/data/repeat.txt
	@echo "=== Valgrind helgrind (thread errors) ==="
	valgrind --tool=helgrind --error-exitcode=1 \
	         ./multisync tests/data/hello.txt tests/data/repeat.txt

clean:
	rm -f $(OBJ) $(TARGET) tests/data/*.rle tests/data/*.out
