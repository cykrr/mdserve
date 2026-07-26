CC      = gcc
CFLAGS  = -Wall -Wextra -Wshadow -O2 -D_GNU_SOURCE -Iinclude -I.
MGFLAGS = -DMG_ENABLE_DIRLIST=1 -DMG_ENABLE_IPV6=1 -DMG_TLS=MG_TLS_OPENSSL
LDLIBS  = -lmd4c-html -lmd4c -lssl -lcrypto

# Basic hardening: always on, minimal perf impact.
HARD_CFLAGS  = -D_FORTIFY_SOURCE=2 -fstack-protector-strong
HARD_LDFLAGS = -Wl,-z,relro -Wl,-z,now -fPIE -pie

# Sanitizers: debug builds only, 2-5x slowdown.
ASAN_CFLAGS  = -fsanitize=address,undefined -fno-omit-frame-pointer -g -O1
ASAN_LDFLAGS = -fsanitize=address,undefined

OBJS = main.o md.o membuf.o mongoose.o

mdserve: $(OBJS)
	$(CC) $(HARD_LDFLAGS) -o $@ $(OBJS) $(LDLIBS)

main.o: main.c
	$(CC) $(CFLAGS) $(HARD_CFLAGS) $(MGFLAGS) -c main.c

md.o: src/md.c
	$(CC) $(CFLAGS) $(HARD_CFLAGS) -c src/md.c

membuf.o: src/membuf.c
	$(CC) $(CFLAGS) $(HARD_CFLAGS) -c src/membuf.c

# mongoose es código vendorizado: se compila sin nuestros warnings.
mongoose.o: mongoose.c
	$(CC) -O2 -D_GNU_SOURCE $(HARD_CFLAGS) -I. $(MGFLAGS) -c mongoose.c

# make hardened — debug build with sanitizers for fuzzing and testing.
hardened: CFLAGS  = -Wall -Wextra -Wshadow -Werror -D_GNU_SOURCE -Iinclude -I.
hardened: MGFLAGS = -DMG_ENABLE_DIRLIST=1 -DMG_ENABLE_IPV6=1 -DMG_TLS=MG_TLS_OPENSSL
hardened: clean
	$(CC) $(CFLAGS) $(ASAN_CFLAGS) $(HARD_CFLAGS) $(MGFLAGS) -c main.c -o main.o
	$(CC) $(CFLAGS) $(ASAN_CFLAGS) $(HARD_CFLAGS) -c src/md.c -o md.o
	$(CC) $(CFLAGS) $(ASAN_CFLAGS) $(HARD_CFLAGS) -c src/membuf.c -o membuf.o
	$(CC) -O1 -g -D_GNU_SOURCE $(ASAN_CFLAGS) $(HARD_CFLAGS) -I. $(MGFLAGS) -c mongoose.c -o mongoose.o
	$(CC) $(ASAN_LDFLAGS) $(HARD_LDFLAGS) -o mdserve main.o md.o membuf.o mongoose.o $(LDLIBS)

test: mdserve
	./test.sh

clean:
	rm -f mdserve *.o

all: mdserve

.PHONY: all clean hardened test
