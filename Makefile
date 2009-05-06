CC = gcc
LD = $(CC)
CFLAGS = -ansi -Wall -W -O2 -pedantic -g
LIBS = `pkg-config --cflags --libs libmpd` \
		 -lX11 -lpthread \
		 `pkg-config --cflags --libs xcb-atom` \
		 `pkg-config --cflags --libs xcb`
TARGET = xmpdp
OBJS = xalloc.o xmpdp.o

%.o : %.c
	$(CC) $(CFLAGS) -c $< -o $@

$(TARGET) : $(OBJS)
	$(LD) $(OBJS) -o $@ $(LIBS)

clean:
	@rm -vf $(OBJS) $(TARGET) *~
