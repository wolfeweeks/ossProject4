all: .PHONY oss worker

.PHONY: clean

oss: oss.c
	gcc -o oss oss.c shared_memory.h shared_memory.c

worker:
	gcc -o worker worker.c shared_memory.h shared_memory.c

clean:
	rm -f oss worker