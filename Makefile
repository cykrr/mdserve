mdserve: main.c md.o membuf.o server.o
	gcc -DLINUX -D_REENTRANT -D_GNU_SOURCE -Iinclude -o mdserve main.c membuf.o md.o server.o -lmd4c-html -lmd4c

md.o: src/md.c
	gcc -c src/md.c -Iinclude

membuf.o: src/membuf.c
	gcc -c src/membuf.c -Iinclude 

server.o: src/server.c
	gcc -c src/server.c -Iinclude 

clean:
	rm -f mdserve *.o
