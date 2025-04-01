# Makefile for mdldec
# Copyright (c) nekonomicon 2020

MODULE = mdldec

CC	?= gcc
CFLAGS	?= -O3 -pipe -DHAVE_TGMATH_H -DSTDINT_H=\<stdint.h\>
LDFLAGS	?= -Wl,--no-undefined

SYS = $(shell $(CC) -dumpmachine)

ifneq (, $(findstring mingw, $(SYS)))
EXT = .exe
CFLAGS += -DHAVE_STRICMP -DHAVE_STRNICMP
else
EXT =
CFLAGS += -DHAVE_STRCASECMP -DHAVE_STRNCASECMP
endif

APP = $(MODULE)$(EXT)

SRC =	mdldec.c \
	qc.c \
	smd.c \
	texture.c \
	utils.c \
	../../public/xash3d_mathlib.c \
	../../public/matrixlib.c \
	../../public/crtlib.c \
	../../public/getopt.c

INCLUDE = -I. \
	  -I../../common \
	  -I../../engine \
	  -I../../engine/common \
	  -I../../engine/common/imagelib \
	  -I../../public

LIBS = -lm

OBJS = $(SRC:%.c=%.o)

all: $(APP)

$(APP): $(OBJS)
	$(CC) $(LDFLAGS) -o $(APP) $(OBJS) $(LIBS)

.c.o:
	$(CC) $(CFLAGS) $(INCLUDE) -c $< -o $@

.PHONY: all clean

clean:
	$(RM) $(OBJS)
	$(RM) $(APP)
