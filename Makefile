SRC = closures.c columntype.c columntype-name.c donna.c node.c provider.c \
	  provider-base.c provider-config.c provider-fs.c sort.c task.c \
	  taskui.c treeview.c util.c app.c treestore.c columntype-size.c size.c \
	  columntype-time.c columntype-perms.c columntype-text.c \
	  filter.c colorfilter.c cellrenderertext.c command.c provider-command.c \
	  provider-task.c columntype-label.c columntype-progress.c \
	  columntype-value.c statusprovider.c statusbar.c task-process.c \
	  taskui-messages.c provider-exec.c imagemenuitem.c misc.c \
	  provider-register.c provider-internal.c history.c provider-mark.c \
	  contextmenu.c task-helpers.c fsengine-basic.c provider-invalid.c
OBJ = ${SRC:.c=.o}

GTK_FLAGS := `pkg-config --cflags --libs gtk+-3.0`
CFLAGS := -Wall -Wextra -g $(CFLAGS) $(GTK_FLAGS)

all: donnatella

.c.o:
	gcc -c $(CFLAGS) $<

donnatella: ${OBJ}
	gcc -o $@ $(CFLAGS) ${OBJ}
