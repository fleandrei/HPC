CC=gcc -Wall -O3

CFLAGS=-Iinc

LDFLAGS=-lm 

BIN=pathtracer pathtracer_MPI

HOST=hostfile

MAP=--map-by node

all : $(BIN)

% : %.c
	$(CC) -o $@ $^ $(LDFLAGS)

pathtracer_MPI: pathtracer_MPI.c
	mpicc -o $@ $^ $(LDFLAGS)

exec: pathtracer_MPI
	mpirun -n 2 -hostfile $(HOST) $(MAP) ./$^
	 
clean :
	rm -f $(BIN) *.o *~




