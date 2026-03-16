CC = gcc

COMMON_CFLAGS = -Wall -Wextra -Iinc -finput-charset=UTF-8 -fexec-charset=UTF-8
SRC = $(wildcard src/*.c)
RULE_TEST_SRC = tests/rule_validation.c src/board_rules.c src/game_logic.c src/map.c

ifeq ($(OS),Windows_NT)
RAYLIB_INCLUDE ?= C:/raylib/w64devkit/x86_64-w64-mingw32/include
RAYLIB_LIB ?= C:/raylib/w64devkit/x86_64-w64-mingw32/lib

CFLAGS = $(COMMON_CFLAGS) -I$(RAYLIB_INCLUDE)
GAME_LDFLAGS = -L$(RAYLIB_LIB) -lraylib -lopengl32 -lgdi32 -lwinmm
RULE_TEST_LDFLAGS = $(GAME_LDFLAGS) -lm
TARGET = settlers.exe
RULE_TEST_TARGET = rules_test.exe
CLEAN_CMD = del /Q $(TARGET) $(RULE_TEST_TARGET) 2>nul
else
PKG_CONFIG ?= pkg-config
RAYLIB_CFLAGS ?= $(shell $(PKG_CONFIG) --cflags raylib 2>/dev/null)
RAYLIB_LIBS ?= $(shell $(PKG_CONFIG) --libs raylib 2>/dev/null)

ifeq ($(strip $(RAYLIB_LIBS)),)
$(error raylib was not found via pkg-config. Install raylib and pkg-config, or pass RAYLIB_CFLAGS/RAYLIB_LIBS to make)
endif

CFLAGS = $(COMMON_CFLAGS) $(RAYLIB_CFLAGS)
GAME_LDFLAGS = $(RAYLIB_LIBS)
RULE_TEST_LDFLAGS = $(GAME_LDFLAGS) -lm
TARGET = settlers
RULE_TEST_TARGET = rules_test
CLEAN_CMD = rm -f $(TARGET) $(RULE_TEST_TARGET)
endif

all: $(TARGET)

rules-test: $(RULE_TEST_TARGET)

$(TARGET): $(SRC)
	$(CC) $(CFLAGS) $(SRC) $(GAME_LDFLAGS) -o $(TARGET)

$(RULE_TEST_TARGET): $(RULE_TEST_SRC)
	$(CC) $(CFLAGS) $(RULE_TEST_SRC) $(RULE_TEST_LDFLAGS) -o $(RULE_TEST_TARGET)

clean:
	$(CLEAN_CMD)
