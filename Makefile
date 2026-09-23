# Arcade Volleyball - SDL2 software-rendered port
CC      ?= cc
CFLAGS  ?= -O2 -g -Wall -Wextra -Wno-unused-parameter
CFLAGS  += $(shell sdl2-config --cflags)
LDLIBS  += $(shell sdl2-config --libs) -lm
SRC      = src/main.c src/plat.c src/game.c src/cga.c src/font8x8.c src/avdat.c
OBJ      = $(SRC:.c=.o)
BIN      = arcadevolleyball

all: $(BIN)

$(BIN): $(OBJ)
	$(CC) $(CFLAGS) -o $@ $(OBJ) $(LDLIBS)

src/%.o: src/%.c src/cga.h src/plat.h src/avdat.h
	$(CC) $(CFLAGS) -Isrc -c -o $@ $<

clean:
	rm -f $(OBJ) $(BIN)

.PHONY: all clean
