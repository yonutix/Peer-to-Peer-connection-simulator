CFLAGS=-m32 -g 

all: send recv 

send: send.o lib.o
	gcc $(CFLAGS) -lm send.o lib.o -o send

recv: recv.o lib.o
	gcc $(CFLAGS) -lm recv.o lib.o -o recv

.c.o: 
	gcc $(CFLAGS) -Wall -c $? 

clean:
	-rm send.o recv.o send recv 
