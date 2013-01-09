SRC = util.c node.c provider.c
OBJ = ${SRC:.c=.o}

GOBJECT_FLAGS := `pkg-config --cflags --libs gobject-2.0`
CFLAGS := -Wall -Wextra -g $(CFLAGS) $(GOBJECT_FLAGS)

all: donna

.c.o:
	gcc -c $(CFLAGS) $<

donna: ${OBJ}
	gcc -o $@ $(CFLAGS) ${OBJ}
