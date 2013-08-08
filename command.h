
#ifndef __DONNA_COMMAND_H__
#define __DONNA_COMMAND_H__

#include <gtk/gtk.h>
#include "provider-command.h"
#include "app.h"
#include "node.h"
#include "task.h"

G_BEGIN_DECLS

struct command
{
    gchar               *name;
    guint                argc;
    DonnaArgType        *arg_type;
    DonnaArgType         return_type;
    DonnaTaskVisibility  visibility;
    command_fn           func;
    gpointer             data;
    GDestroyNotify       destroy;
};

typedef gboolean (*_conv_flag_fn) (const gchar     c,
                                   DonnaArgType   *type,
                                   gpointer       *ptr,
                                   GDestroyNotify *destroy,
                                   gpointer        data);


gchar *             _donna_command_parse_fl         (DonnaApp       *app,
                                                     gchar          *fl,
                                                     const gchar    *conv_flags,
                                                     _conv_flag_fn   conv_fn,
                                                     gpointer        conv_data,
                                                     GPtrArray     **intrefs);
gboolean            _donna_command_trigger_fl       (DonnaApp       *app,
                                                     const gchar    *fl,
                                                     GPtrArray      *intrefs,
                                                     gboolean        blocking);

G_END_DECLS

#endif /* __DONNA_COMMAND_H__ */
