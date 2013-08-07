
#ifndef __DONNA_COMMAND_H__
#define __DONNA_COMMAND_H__

#include <gtk/gtk.h>
#include "provider-command.h"
#include "app.h"
#include "node.h"
#include "task.h"

G_BEGIN_DECLS

typedef gboolean (*_conv_flag_fn) (const gchar  c,
                                   DonnaArgType type,
                                   gboolean     dereferenced,
                                   DonnaApp    *app,
                                   gpointer    *out,
                                   gpointer     data);

typedef struct
{
    gchar               *name;
    guint                argc;
    DonnaArgType        *arg_type;
    DonnaArgType         return_type;
    DonnaTaskVisibility  visibility;
    command_fn           func;
    gpointer             data;
    GDestroyNotify       destroy;
} DonnaCommand;


struct _donna_command_run
{
    DonnaApp    *app;
    gchar       *cmdline;
};

DonnaCommand *          _donna_command_init_parse   (gchar           *cmdline,
                                                     gchar          **first_arg,
                                                     GError         **error);
DonnaTaskState          _donna_command_run          (DonnaTask       *task,
                                                     struct _donna_command_run *cr);
void                    _donna_command_free_cr      (struct _donna_command_run *cr);
gboolean                _donna_command_parse_run    (DonnaApp        *app,
                                                     gboolean         blocking,
                                                     const gchar     *conv_flags,
                                                     _conv_flag_fn    conv_fn,
                                                     gpointer         conv_data,
                                                     GDestroyNotify   conv_destroy,
                                                     gchar           *fl);

G_END_DECLS

#endif /* __DONNA_COMMAND_H__ */
