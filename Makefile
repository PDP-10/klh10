CFLAGS = -Wall -Wextra -g
LDFLAGS = -g

all: ksim

clean:
	rm ksim.o ksim

ksim: ksim.o
