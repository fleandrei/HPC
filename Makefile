CC=gcc -Wall -O3

CFLAGS=-Iinc

LDFLAGS=-lm 

BIN=pathtracer pathtracer_MPI pathtracer_patron pathtracer_auto

HOST=hostfile

MAP=--map-by node

all : $(BIN)

% : %.c
	$(CC) -o $@ $^ $(LDFLAGS)

pathtracer_MPI: pathtracer_MPI.c
	mpicc -o $@ $^ $(LDFLAGS)

pathtracer_patron: pathtracer_patron.c
	mpicc -o $@ $^ $(LDFLAGS)

pathtracer_auto: pathtracer_auto.c
	mpicc -o $@ $^ $(LDFLAGS)

exec: pathtracer_auto
	mpirun -n 18 -../hostfile $(HOST) $(MAP) ./$^ 10
	
test:pathtracer_patron
	mpirun -n 5 -../hostfile $(HOST) $(MAP) ./$^ 200


clean :
	rm -f $(BIN) *.o *~




