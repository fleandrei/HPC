CC=gcc -Wall -O3

CFLAGS=-Iinc

LDFLAGS=-lm 

BIN= pathtracer_OMP

HOST=hostfile

MAP=--map-by node

all : $(BIN)

% : %.c
	$(CC) -o $@ $^ $(LDFLAGS)


pathtracer_OMP: pathtracer_OMP.c
	mpicc -o $@ $^ $(LDFLAGS) -fopenmp

exec: pathtracer_OMP
	mpirun -n 18 -hostfile $(HOST) $(MAP) ./$^ 10
	

clean :
	rm -f $(BIN) *.o *~




