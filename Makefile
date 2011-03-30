CC= gcc
CCFLAGS= -g -W -lpthread

ftpd: *.c
	$(CC) $(CCFLAGS) -o $@ $^

clean:
