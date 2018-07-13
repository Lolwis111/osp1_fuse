#!/usr/bin/make
.SUFFIXES:
.PHONY: all run umount pack clean

TAR = dedup
PCK = lab-6.zip
SRC = $(wildcard *.c)
OBJ = $(SRC:%.c=%.o)
CFLAGS := -std=gnu11 -c -g -Os -Wall -Werror -Wno-unused-value -Wno-unused-result `pkg-config fuse3 --cflags`
LFLAGS := `pkg-config fuse3 --libs` -Wl,-rpath="`pkg-config fuse3 --libs-only-L | cut -c 3-`"

%.o: %.c %.h
	$(CC) $< $(CFLAGS) -o $@

%.o: %.c
	$(CC) $< $(CFLAGS) -o $@

$(TAR): $(OBJ)
	$(CC) $^ $(LFLAGS) -o $@

$(TAR)_fs:
	mkdir $@

all: $(TAR)

run: all $(TAR)_fs umount
	./$(TAR) $(TAR)_fs

umount:
	@if `grep -qs $(TAR)_fs /etc/mtab`; then fusermount -u $(TAR)_fs; fi

pack: clean
	zip $(PCK) *.c *.h Makefile -x "./.*"

clean: umount
	$(RM) $(OBJ) $(TAR)
