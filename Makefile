CC=clang-8
CFLAGS=-std=c11 -Wall -g -m64
INCLUDEDIR=include
LIBDIR=lib
BINDIR=bin
OBJDIR=obj
SRCDIR=src
TESTDIR=tests
LTO=
SCPRE_TESTS=$(patsubst %.c,%.test, $(shell find $(TESTDIR)/scpre/ -type f -name '*.c'))

.PHONY: all clean tests

all: libsc_alloc libsc_io scpre

release: LTO+=-flto
release: CFLAGS=-std=c11 -O3 -fomit-frame-pointer -m64 -DNDEBUG
release: all

%.o: $(SRCDIR)/%.c
	$(CC) -c -o $(OBJDIR)/$@ $< $(CFLAGS) -I$(INCLUDEDIR)

%.o: $(SRCDIR)/tools/%.c
	$(CC) -c -o $(OBJDIR)/$@ $< $(CFLAGS) -I$(INCLUDEDIR)

libsc_alloc: sc_alloc.o
	ar -rcs $(LIBDIR)/libsc_alloc.a $(addprefix $(OBJDIR)/, $^)

libsc_io: sc_logging.o sc_file_io.o
	ar -rcs $(LIBDIR)/libsc_io.a $(addprefix $(OBJDIR)/, $^)

scpre: tokenizer.o strings.o scpre.o token_vector.o preprocessor.o macros.o
	$(CC) -o $(BINDIR)/scpre $(addprefix $(OBJDIR)/, $^) -lsc_io -lsc_alloc $(CFLAGS) $(LTO) -I$(INCLUDEDIR) -L$(LIBDIR)

$(TESTDIR)/scpre/%.test: $(TESTDIR)/scpre/%.c
	./bin/scpre $< $@.c
	rm $@.c

tests: $(SCPRE_TESTS)

clean:
	rm $(BINDIR)/*
	rm $(OBJDIR)/*.o
	rm $(LIBDIR)/*.a
