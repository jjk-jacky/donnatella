
#include "config.h"

#include <locale.h>
#include <gtk/gtk.h>
#include "app.h"
#include "donna.h"

int
main (int argc, char *argv[])
{
    setlocale (LC_ALL, "");
    gtk_init (&argc, &argv);
    return donna_app_run (g_object_new (DONNA_TYPE_DONNA, NULL), argc, argv);
}
