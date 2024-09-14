CC = gcc
CFLAGS := -O2 -I.


OBJS = \
	mp_msg.c \
	spudec.c \
	subtomov.c \
	vobsub.c

LIBS := -lm

OUTPUT = subtomov


$(OUTPUT): $(OBJS)
	$(CC) -o $(OUTPUT) $(OBJS) $(LIBS)

clean:
        rm -f $(OUTPUT)
