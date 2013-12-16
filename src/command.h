
#ifndef __DONNA_COMMAND_H__
#define __DONNA_COMMAND_H__

#include <gtk/gtk.h>
#include "provider-command.h"
#include "app.h"
#include "node.h"
#include "task.h"

G_BEGIN_DECLS

#define _get_choice(choices, sel)   \
    _donna_get_choice_from_list (sizeof (choices) / sizeof (choices[0]), choices, sel)
#define _get_flags(choices, flags, sel) \
    _donna_get_flags_from_list (sizeof (choices) / sizeof (choices[0]), choices, flags, sel)

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

gint                _donna_get_choice_from_list     (guint           nb,
                                                     const gchar    *choices[],
                                                     const gchar    *sel);
guint               _donna_get_flags_from_list      (guint           nb,
                                                     const gchar    *choices[],
                                                     guint           flags[],
                                                     gchar          *sel);

G_END_DECLS

#endif /* __DONNA_COMMAND_H__ */
