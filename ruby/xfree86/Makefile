
CC = gcc
LDFLAGS = -shared
DEBUG_CFLAGS = -g -O0
OPT_CFLAGS = 
WARN_CFLAGS = -Wall  -pedantic
XCROOT = /usr/src/xc 

CFLAGS = $(DEBUG_CFLAGS) $(OPT_CFLAGS) $(WARN_CFLAGS) -fPIC -DDEBUG -DORFLOG $(INCLUDES)
INCLUDES = -I$(XCROOT)/include -I$(XCROOT)/include/extensions	\
           -I$(XCROOT)/programs/Xserver/			\
           -I$(XCROOT)/programs/Xserver/include			\
           -I$(XCROOT)/programs/Xserver/hw/xfree86/		\
           -I$(XCROOT)/programs/Xserver/hw/xfree86/os-support	\
           -I$(XCROOT)/programs/Xserver/hw/xfree86/common/


TUNTITKO_COMMON_SRC = tuntitko-devconfig.c \
		      tuntitko-common.c    \
		      tuntitko-names.c	   \
		       tuntitko-postevent.c

TUNTITKO33_SRC = $(TUNTITKO_COMMON_SRC) tuntitko-33.c
TUNTITKO40_SRC = 

TUNTITKO33_OBJ = $(TUNTITKO33_SRC:.c=.o)
TUNTITKO40_OBJ = $(TUNTITKO40_SRC:.c=.o)


all: tuntitko-33.so

clean:
	-rm $(TUNTITKO33_OBJ) $(TUNTITKO40_OBJ) tuntitko-33.so tuntitko-40.so *~ tuntitko-log

tuntitko-33.so: $(TUNTITKO33_OBJ)
	$(CC) $(LDFLAGS) $(TUNTITKO33_OBJ) -o tuntitko-33.so


tuntitko-33.o: tuntitko-33.c tuntitko-common.h
tuntitko-common.o: tuntitko-common.c tuntitko-common.h
tuntitko-names.o: tuntitko-names.c
tuntitko-devconfig.o: tuntitko-devconfig.c tuntitko-common.h
tuntitko-postevent.o: tuntitko-postevent.c tuntitko-common.h
