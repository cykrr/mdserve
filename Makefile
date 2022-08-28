mdserve: main.c md.o
	gcc main.c -DLINUX -D_REENTRANT -D_GNU_SOURCE  -lmd4c-html -Iinclude -o mdserve md.o

md.o: src/md.c
	gcc -c src/md.c -lmd4c-html -Iinclude 
