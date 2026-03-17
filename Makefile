CC = gcc

COMMON_CFLAGS = -Wall -Wextra -Iinc -finput-charset=UTF-8 -fexec-charset=UTF-8
SRC = $(wildcard src/*.c)
GAME_SRC = $(filter-out src/console_game.c,$(SRC))
RULE_TEST_SRC = tests/rule_validation.c src/board_rules.c src/game_logic.c src/map.c src/debug_log.c
CONSOLE_SRC = src/console_game.c src/board_rules.c src/game_logic.c src/map.c src/debug_log.c

ifeq ($(OS),Windows_NT)
RAYLIB_INCLUDE ?= C:/raylib/w64devkit/x86_64-w64-mingw32/include
RAYLIB_LIB ?= C:/raylib/w64devkit/x86_64-w64-mingw32/lib

CFLAGS = $(COMMON_CFLAGS) -Wno-expansion-to-defined -isystem $(RAYLIB_INCLUDE)
GAME_LDFLAGS = -L$(RAYLIB_LIB) -lraylib -lopengl32 -lgdi32 -lwinmm -lws2_32
RULE_TEST_LDFLAGS = $(GAME_LDFLAGS) -lm
TARGET = settlers.exe
RULE_TEST_TARGET = rules_test.exe
CONSOLE_TARGET = soc_console.exe
CLEAN_CMD = del /Q $(TARGET) $(RULE_TEST_TARGET) $(CONSOLE_TARGET) 2>nul
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
CONSOLE_TARGET = soc_console
CLEAN_CMD = rm -f $(TARGET) $(RULE_TEST_TARGET) $(CONSOLE_TARGET)
endif

all: $(TARGET)

rules-test: $(RULE_TEST_TARGET)

console: $(CONSOLE_TARGET)

package-windows: $(TARGET)
	powershell -ExecutionPolicy Bypass -File scripts/package_windows.ps1

package-macos: $(TARGET)
	bash scripts/package_macos.sh

$(TARGET): $(GAME_SRC)
	$(CC) $(CFLAGS) $(GAME_SRC) $(GAME_LDFLAGS) -o $(TARGET)

$(RULE_TEST_TARGET): $(RULE_TEST_SRC)
	$(CC) $(CFLAGS) $(RULE_TEST_SRC) $(RULE_TEST_LDFLAGS) -o $(RULE_TEST_TARGET)

$(CONSOLE_TARGET): $(CONSOLE_SRC)
	$(CC) $(CFLAGS) $(CONSOLE_SRC) $(RULE_TEST_LDFLAGS) -o $(CONSOLE_TARGET)

clean:
	$(CLEAN_CMD)
