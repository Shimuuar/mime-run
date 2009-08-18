
CC     = gcc
CFLAGS = -O2 -Wall -Wextra

all: mime-run

mime-run: mime-run.c
	${CC} ${CFLAGS} -o $@ $<
