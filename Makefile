CC = gcc

CFLAGS = -Wall -Wextra -Iinc -IC:/raylib/w64devkit/x86_64-w64-mingw32/include
LDFLAGS = -LC:/raylib/w64devkit/x86_64-w64-mingw32/lib -lraylib -lopengl32 -lgdi32 -lwinmm
RULE_TEST_LDFLAGS = $(LDFLAGS) -lm

SRC = $(wildcard src/*.c)
TARGET = settlers.exe
RULE_TEST_SRC = tests/rule_validation.c src/board_rules.c src/game_logic.c src/map.c
RULE_TEST_TARGET = rules_test.exe

all: $(TARGET)

rules-test: $(RULE_TEST_TARGET)

$(TARGET): $(SRC)
	$(CC) $(SRC) $(CFLAGS) $(LDFLAGS) -o $(TARGET)

$(RULE_TEST_TARGET): $(RULE_TEST_SRC)
	$(CC) $(RULE_TEST_SRC) $(CFLAGS) $(RULE_TEST_LDFLAGS) -o $(RULE_TEST_TARGET)

clean:
	del /Q $(TARGET) 2>nul
	del /Q $(RULE_TEST_TARGET) 2>nul
