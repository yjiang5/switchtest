SRCS := test.c \
    app.c \
    pktgen.c

OBJS := ${SRCS:c=o} 
PROGS := ${SRCS:.c=}

DFLAGS := -g
CFLAGS := ${DFLAGS} -lpthread -D_GNU_SOURCE   -Wall -Wno-nonnull 
LDFLAGS := ${DFLAGS} -lrt -lpthread
.PHONY: all
all: test
test : ${OBJS}
	gcc ${LDFLAGS} ${OBJS} -o $@
%.o: %.c
	gcc ${CFLAGS} -c $<

clean:
	rm *.o
	rm test
