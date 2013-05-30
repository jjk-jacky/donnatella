
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
    COMMAND_ERROR_OTHER,
} CommandError;

typedef enum
{
    DONNA_COMMAND_SET_FOCUS,
    DONNA_COMMAND_SET_CURSOR,
    DONNA_COMMAND_SELECTION,
    DONNA_COMMAND_ACTIVATE_ROW,
    DONNA_COMMAND_TOGGLE_ROW,
} DonnaCommand;

typedef DonnaTaskState (*cmd_fn) (DonnaTask *task, GPtrArray *args);

typedef struct
{
    DonnaCommand         command;
    gchar               *name;
    guint                argc;
    DonnaArgType         arg_type[COMMAND_MAX_ARGS];
    DonnaArgType         return_type;
    DonnaTaskVisibility  visibility;
    cmd_fn               cmd_fn;
} DonnaCommandDef;

struct _donna_command_run
{
    DonnaApp    *app;
    gchar       *cmdline;
};

DonnaCommandDef *       _donna_command_init_parse   (gchar           *cmdline,
                                                     gchar          **first_arg,
                                                     gchar          **end,
                                                     GError         **error);
gboolean                _donna_command_get_next_arg (DonnaCommandDef *command,
                                                     guint            i,
                                                     gchar          **arg,
                                                     gchar          **end,
                                                     GError         **error);
gboolean                _donna_command_checks_post_parsing (
                                                     DonnaCommandDef *command,
                                                     guint            i,
                                                     gchar           *start,
                                                     gchar           *end,
                                                     GError         **error);
gboolean                _donna_command_convert_arg  (DonnaApp        *app,
                                                     DonnaArgType     type,
                                                     gboolean         from_string,
                                                     gpointer         sce,
                                                     gpointer        *dst,
                                                     GError         **error);
void                    _donna_command_free_args    (DonnaCommandDef *command,
                                                     GPtrArray       *arr);
DonnaTaskState          _donna_command_run          (DonnaTask       *task,
                                                     struct _donna_command_run *cr);
void                    _donna_command_free_cr      (struct _donna_command_run *cr);

G_END_DECLS

#endif /* __DONNA_COMMAND_H__ */
