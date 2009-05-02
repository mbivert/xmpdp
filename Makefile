CC = gcc
LD = $(CC)
CFLAGS = -ansi -Wall -W -O2 -pedantic -g
LIBS = -lmpd -lX11 `pkg-config --cflags --libs xcb` -lpthread
TARGET = xmpdp
OBJS = xalloc.o xmpdp.o

%.o : %.c
	$(CC) $(CFLAGS) -c $< -o $@

$(TARGET) : $(OBJS)
	$(LD) $(OBJS) -o $@ $(LIBS)

clean:
	@rm -vf $(OBJS) $(TARGET) *~
