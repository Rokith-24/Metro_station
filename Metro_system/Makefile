
CC = gcc

CFLAGS = -Wall -Wextra -Wpedantic -std=c11 -g -pthread
CPPFLAGS = -Iinclude
LDFLAGS = -pthread

SRC_DIR = src
BUILD_DIR = build
TARGET = $(BUILD_DIR)/metro

SOURCES = $(wildcard $(SRC_DIR)/*.c)
HEADERS = $(wildcard include/*.h)

.PHONY: all run clean memcheck helgrind cppcheck

all: $(TARGET)

$(TARGET): $(SOURCES) $(HEADERS)
	mkdir -p $(BUILD_DIR)
	$(CC) $(CPPFLAGS) $(CFLAGS) $(SOURCES) $(LDFLAGS) -o $(TARGET)

run: $(TARGET)
	./$(TARGET)

clean:
	rm -f $(TARGET)

memcheck: $(TARGET)
	valgrind --leak-check=full --show-leak-kinds=all --track-origins=yes ./$(TARGET)

helgrind: $(TARGET)
	valgrind --tool=helgrind --history-level=full ./$(TARGET)

cppcheck:
	cppcheck --enable=all --inconclusive --std=c11 -I include $(SRC_DIR)
