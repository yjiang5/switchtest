test: test.c
	gcc  test.c -pthread -D_GNU_SOURCE -g -o test
