CC=gcc -Wall -O3

CFLAGS=-Iinc

LDFLAGS=-lm 

BIN= pathtracer_auto

HOST=hostfile

MAP=--map-by node

all : $(BIN)

% : %.c
	$(CC) -o $@ $^ $(LDFLAGS)


pathtracer_auto: pathtracer_auto.c
	mpicc -o $@ $^ $(LDFLAGS)

exec: pathtracer_auto
	mpirun -n 18 -hostfile $(HOST) $(MAP) ./$^ 10
	

clean :
	rm -f $(BIN) *.o *~




