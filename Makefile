SRCS := test.c \
    app.c \
    pktgen.c

OBJS := ${SRCS:c=o} 
PROGS := ${SRCS:.c=}

.PHONY: all
all: test
test : ${OBJS}
	gcc -g ${OBJS} -lrt -lpthread -o $@
%.o: %.c
	gcc -g -pthread -O2 -D_GNU_SOURCE   -Wall -Wno-nonnull -c $<

clean:
	rm *.o
