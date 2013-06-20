
#ifndef __DONNA_COMMAND_H__
#define __DONNA_COMMAND_H__

#include <gtk/gtk.h>
#include "app.h"
#include "node.h"
#include "task.h"

G_BEGIN_DECLS

#define COMMAND_MAX_ARGS        8

#define COMMAND_ERROR           g_quark_from_static_string ("Donna-Command-Error")
typedef enum
{
    COMMAND_ERROR_NOT_FOUND,
    COMMAND_ERROR_SYNTAX,
    COMMAND_ERROR_MISSING_ARG,
    COMMAND_ERROR_MIGHT_BLOCK,
    COMMAND_ERROR_OTHER,
} CommandError;

typedef DonnaTaskState (*cmd_fn) (DonnaTask *task, GPtrArray *args);

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
    DonnaArgType         arg_type[COMMAND_MAX_ARGS];
    DonnaArgType         return_type;
    DonnaTaskVisibility  visibility;
    cmd_fn               cmd_fn;
} DonnaCommand;

struct _donna_command_run
{
    DonnaApp    *app;
    gchar       *cmdline;
};

DonnaCommand *          _donna_command_init_parse   (gchar           *cmdline,
                                                     gchar          **first_arg,
                                                     gchar          **end,
                                                     GError         **error);
DonnaTaskState          _donna_command_run          (DonnaTask       *task,
                                                     struct _donna_command_run *cr);
void                    _donna_command_free_cr      (struct _donna_command_run *cr);
void                    _donna_command_parse_run    (DonnaApp        *app,
                                                     const gchar     *conv_flags,
                                                     _conv_flag_fn    conv_fn,
                                                     gpointer         conv_data,
                                                     GDestroyNotify   conv_destroy,
                                                     gchar           *fl);

G_END_DECLS

#endif /* __DONNA_COMMAND_H__ */
