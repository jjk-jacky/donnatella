SRC = main.c util.c node.c provider.c provider-base.c task.c provider-fs.c \
	  tree.c closures.c
OBJ = ${SRC:.c=.o}

GTK_FLAGS := `pkg-config --cflags --libs gtk+-3.0`
CFLAGS := -Wall -Wextra -g $(CFLAGS) $(GTK_FLAGS)

all: donnatella

.c.o:
	gcc -c $(CFLAGS) $<

donnatella: ${OBJ}
	gcc -o $@ $(CFLAGS) ${OBJ}
