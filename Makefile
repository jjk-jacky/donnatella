SRC = closures.c columntype.c columntype-name.c donna.c node.c provider.c \
	  provider-base.c provider-config.c provider-fs.c sort.c task.c \
	  taskui.c treeview.c util.c app.c treestore.c columntype-size.c size.c \
	  columntype-time.c columntype-perms.c columntype-text.c \
	  filter.c colorfilter.c
OBJ = ${SRC:.c=.o}

GTK_FLAGS := `pkg-config --cflags --libs gtk+-3.0`
CFLAGS := -Wall -Wextra -g $(CFLAGS) $(GTK_FLAGS)

all: donnatella

.c.o:
	gcc -c $(CFLAGS) $<

donnatella: ${OBJ}
	gcc -o $@ $(CFLAGS) ${OBJ}
