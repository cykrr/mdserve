mdserve: main.c md.o membuf.o server.o
	gcc main.c -DLINUX -D_REENTRANT -D_GNU_SOURCE  -lmd4c-html -Iinclude -o mdserve membuf.o md.o server.o

md.o: src/md.c
	gcc -c src/md.c -lmd4c-html -Iinclude 

membuf.o: src/membuf.c
	gcc -c src/membuf.c -Iinclude 


server.o: src/server.c
	gcc -c src/server.c -Iinclude 
