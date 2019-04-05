CC=gcc -Wall -O3

CFLAGS=-Iinc

LDFLAGS=-lm 

BIN=pathtracer pathtracer_MPI pathtracer_patron

HOST=hostfile

MAP=--map-by node

all : $(BIN)

% : %.c
	$(CC) -o $@ $^ $(LDFLAGS)

pathtracer_MPI: pathtracer_MPI.c
	mpicc -o $@ $^ $(LDFLAGS)

pathtracer_patron: pathtracer_patron.c
	mpicc -o $@ $^ $(LDFLAGS)

exec: pathtracer_MPI
	mpirun -n 2 -hostfile $(HOST) $(MAP) ./$^ 10
	
test:pathtracer_patron
	mpirun -n 10 -hostfile $(HOST) $(MAP) ./$^ 200


clean :
	rm -f $(BIN) *.o *~




