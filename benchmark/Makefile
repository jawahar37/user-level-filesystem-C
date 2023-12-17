CC = gcc
CFLAGS = -g

all: simple_test test_case my_test

simple_test:
	$(CC) $(CFLAGS) -o simple_test simple_test.c

test_case:
	$(CC) $(CFLAGS) -o test_case test_cases.c

my_test:
	$(CC) $(CFLAGS) -o my_test mytest.c

clean:
	rm -rf simple_test test_case my_test
