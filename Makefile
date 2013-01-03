SRC = main.c fstree.c fsnode.c
OBJ = ${SRC:.c=.o}

GTK_FLAGS := `pkg-config --cflags --libs gtk+-3.0`
CFLAGS := -Wall -Wextra -g $(CFLAGS) $(GTK_FLAGS)

all: donna

.c.o:
	gcc -c $(CFLAGS) $<

donna: ${OBJ}
	gcc -o $@ $(CFLAGS) ${OBJ}
