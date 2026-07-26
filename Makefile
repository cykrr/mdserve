CC      = gcc
CFLAGS  = -Wall -Wextra -Wshadow -O2 -D_GNU_SOURCE -Iinclude -I.
MGFLAGS = -DMG_ENABLE_DIRLIST=1
LDLIBS  = -lmd4c-html -lmd4c

OBJS = main.o md.o membuf.o mongoose.o

mdserve: $(OBJS)
	$(CC) -o $@ $(OBJS) $(LDLIBS)

main.o: main.c
	$(CC) $(CFLAGS) $(MGFLAGS) -c main.c

md.o: src/md.c
	$(CC) $(CFLAGS) -c src/md.c

membuf.o: src/membuf.c
	$(CC) $(CFLAGS) -c src/membuf.c

# mongoose es código vendorizado: se compila sin nuestros warnings.
mongoose.o: mongoose.c
	$(CC) -O2 -D_GNU_SOURCE -I. $(MGFLAGS) -c mongoose.c

clean:
	rm -f mdserve *.o

.PHONY: clean
