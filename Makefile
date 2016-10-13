CC=gcc
CFLAGS=-std=c11 -Wall -g -m64
INCLUDEDIR=include
LIBDIR=lib
BINDIR=bin
OBJDIR=obj
SRCDIR=src
LTO=

.PHONY: all clean

all: libsc_alloc libsc_io scpre

release: LTO+=-flto
release: CFLAGS=-std=c11 -O3 -fomit-frame-pointer -m64 -DNDEBUG
release: all

%.o: $(SRCDIR)/%.c
	$(CC) -c -o $(OBJDIR)/$@ $< $(CFLAGS) -I$(INCLUDEDIR) -L$(LIBDIR)

%.o: $(SRCDIR)/tools/%.c
	$(CC) -c -o $(OBJDIR)/$@ $< $(CFLAGS) -I$(INCLUDEDIR) -L$(LIBDIR)

libsc_alloc: sc_alloc.o
	ar -rcs $(LIBDIR)/libsc_alloc.a $(addprefix $(OBJDIR)/, $^)

libsc_io: sc_logging.o sc_file_io.o
	ar -rcs $(LIBDIR)/libsc_io.a $(addprefix $(OBJDIR)/, $^)

scpre: tokenizer.o strings.o scpre.o token_vector.o preprocessor.o macros.o
	$(CC) -o $(BINDIR)/scpre $(addprefix $(OBJDIR)/, $^) -lsc_io -lsc_alloc $(CFLAGS) $(LTO) -I$(INCLUDEDIR) -L$(LIBDIR)

clean:
	rm $(BINDIR)/*
	rm $(OBJDIR)/*.o
	rm $(LIBDIR)/*.a
