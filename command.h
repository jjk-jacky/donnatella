
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
gboolean                _donna_command_get_next_arg (DonnaCommand   *command,
                                                     guint            i,
                                                     gchar          **arg,
                                                     gchar          **end,
                                                     GError         **error);
gboolean                _donna_command_checks_post_parsing (
                                                     DonnaCommand   *command,
                                                     guint            i,
                                                     gchar           *start,
                                                     gchar           *end,
                                                     GError         **error);
gboolean                _donna_command_convert_arg  (DonnaApp        *app,
                                                     DonnaArgType     type,
                                                     gboolean         from_string,
                                                     gboolean         can_block,
                                                     gpointer         sce,
                                                     gpointer        *dst,
                                                     GError         **error);
void                    _donna_command_free_args    (DonnaCommand   *command,
                                                     GPtrArray       *arr);
DonnaTaskState          _donna_command_run          (DonnaTask       *task,
                                                     struct _donna_command_run *cr);
void                    _donna_command_free_cr      (struct _donna_command_run *cr);

G_END_DECLS

#endif /* __DONNA_COMMAND_H__ */
