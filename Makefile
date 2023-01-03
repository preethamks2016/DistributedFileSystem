CC   = gcc
OPTS = -Wall -g

all: server lib client

# this generates the target executables
server: server.o udp.o
	$(CC)  -o server server.o udp.o 

client: client.o udp.o
	$(CC)  -o client client.o udp.o 


lib: mfs.o udp.o
	$(CC) -Wall -Werror -shared -fPIC -g -o libmfs.so mfs.c udp.c

clean:
# this is a generic rule for .o files 
%.o: %.c 
	$(CC) $(OPTS) -c $< -o $@

clean:
	rm -f server.o client.o udp.o mfs.o libmfs.so server client lib
