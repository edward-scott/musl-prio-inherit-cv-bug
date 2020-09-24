all: bug

clean:
	rm -f *.o bug

thread.o: thread.c
	$(CC) -c thread.c -o thread.o

threadpool.o: threadpool.c
	$(CC) -c threadpool.c -o threadpool.o

main.o: main.c
	$(CC) -c main.c -o main.o

bug: main.o thread.o threadpool.o
	$(CC) -o bug main.o thread.o threadpool.o -lpthread
