CC      = gcc
CFLAGS  = -Wall -Wextra -Wshadow -O2 -D_GNU_SOURCE -Iinclude -I. -Isrc
MGFLAGS = -DMG_ENABLE_DIRLIST=1 -DMG_ENABLE_IPV6=1 -DMG_TLS=MG_TLS_OPENSSL
LDLIBS  = -lmd4c -lssl -lcrypto

# Basic hardening: always on, minimal perf impact.
HARD_CFLAGS  = -D_FORTIFY_SOURCE=2 -fstack-protector-strong
HARD_LDFLAGS = -Wl,-z,relro -Wl,-z,now -fPIE -pie

# Sanitizers: debug builds only, 2-5x slowdown.
ASAN_CFLAGS  = -fsanitize=address,undefined -fno-omit-frame-pointer -g -O1
ASAN_LDFLAGS = -fsanitize=address,undefined

# md4c-html is vendored (Ubuntu doesn't package the shared library).
CORE = md.o membuf.o md4c-html.o entity.o frontmatter.o
OBJS = main.o $(CORE) mongoose.o

# mdbuild shares the render core but links neither mongoose nor OpenSSL.
BUILD_OBJS = mdbuild.o $(CORE)

mdserve: $(OBJS)
	$(CC) $(HARD_LDFLAGS) -o $@ $(OBJS) $(LDLIBS)

mdbuild: $(BUILD_OBJS)
	$(CC) $(HARD_LDFLAGS) -o $@ $(BUILD_OBJS) -lmd4c

main.o: main.c
	$(CC) $(CFLAGS) $(HARD_CFLAGS) $(MGFLAGS) -c main.c

mdbuild.o: mdbuild.c
	$(CC) $(CFLAGS) $(HARD_CFLAGS) -c mdbuild.c

frontmatter.o: src/frontmatter.c
	$(CC) $(CFLAGS) $(HARD_CFLAGS) -c src/frontmatter.c -o $@

md.o: src/md.c
	$(CC) $(CFLAGS) $(HARD_CFLAGS) -c src/md.c

membuf.o: src/membuf.c
	$(CC) $(CFLAGS) $(HARD_CFLAGS) -c src/membuf.c

md4c-html.o: src/md4c-html.c
	$(CC) $(CFLAGS) $(HARD_CFLAGS) -c src/md4c-html.c -o $@

entity.o: src/entity.c
	$(CC) $(CFLAGS) $(HARD_CFLAGS) -c src/entity.c -o $@

# mongoose es código vendorizado: se compila sin nuestros warnings.
mongoose.o: mongoose.c
	$(CC) -O2 -D_GNU_SOURCE $(HARD_CFLAGS) -I. $(MGFLAGS) -c mongoose.c

# make hardened — debug build with sanitizers for fuzzing and testing.
hardened: CFLAGS  = -Wall -Wextra -Wshadow -Werror -D_GNU_SOURCE -Iinclude -I. -Isrc
hardened: MGFLAGS = -DMG_ENABLE_DIRLIST=1 -DMG_ENABLE_IPV6=1 -DMG_TLS=MG_TLS_OPENSSL
hardened: clean
	$(CC) $(CFLAGS) $(ASAN_CFLAGS) $(HARD_CFLAGS) $(MGFLAGS) -c main.c -o main.o
	$(CC) $(CFLAGS) $(ASAN_CFLAGS) $(HARD_CFLAGS) -c src/md.c -o md.o
	$(CC) $(CFLAGS) $(ASAN_CFLAGS) $(HARD_CFLAGS) -c src/membuf.c -o membuf.o
	$(CC) $(CFLAGS) $(ASAN_CFLAGS) $(HARD_CFLAGS) -c src/md4c-html.c -o md4c-html.o
	$(CC) $(CFLAGS) $(ASAN_CFLAGS) $(HARD_CFLAGS) -c src/entity.c -o entity.o
	$(CC) $(CFLAGS) $(ASAN_CFLAGS) $(HARD_CFLAGS) -c src/frontmatter.c -o frontmatter.o
	$(CC) $(CFLAGS) $(ASAN_CFLAGS) $(HARD_CFLAGS) -c mdbuild.c -o mdbuild.o
	$(CC) -O1 -g -D_GNU_SOURCE $(ASAN_CFLAGS) $(HARD_CFLAGS) -I. $(MGFLAGS) -c mongoose.c -o mongoose.o
	$(CC) $(ASAN_LDFLAGS) $(HARD_LDFLAGS) -o mdserve main.o $(CORE) mongoose.o $(LDLIBS)
	$(CC) $(ASAN_LDFLAGS) $(HARD_LDFLAGS) -o mdbuild mdbuild.o $(CORE) -lmd4c

test: mdserve
	./test.sh

clean:
	rm -f mdserve mdbuild *.o

all: mdserve mdbuild

# make site — regenerate the static site into ./site
site: mdbuild
	./mdbuild ./md ./site

.PHONY: all clean hardened test site
