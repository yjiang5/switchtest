test: test.c
	gcc  test.c -pthread -O2 -lrt -D_GNU_SOURCE -g -o test -Wall -Wno-nonnull 
