CC = gcc
CFLAGS = -g -fPIC -pthread -Wall -Wextra
LDFLAGS = -m32

.PHONY: build
build: libscheduler.so

libscheduler.so: so_scheduler.o
	$(CC) $(CFLAGS) -shared -o libscheduler.so so_scheduler.o

so_scheduler.o: so_scheduler.c so_scheduler.h
	$(CC) $(CFLAGS) -o so_scheduler.o -c so_scheduler.c

.PHONY: clean
clean:
	-rm -rf so_scheduler.o libscheduler.so