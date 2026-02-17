CC = gcc
CFLAGS = $(shell pkg-config --cflags glib-2.0)
LIBS = $(shell pkg-config --libs glib-2.0)
SRC = code/sat_solver.c
OUT = sat_solver

all:
	$(CC) $(CFLAGS) $(SRC) -o $(OUT) $(LIBS)

clean:
	rm -f $(OUT)
