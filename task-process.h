
#ifndef __DONNA_TASK_PROCESS_H__
#define __DONNA_TASK_PROCESS_H__

#include "task.h"

G_BEGIN_DECLS

#define DONNA_TYPE_TASK_PROCESS             (donna_task_process_get_type ())
#define DONNA_TASK_PROCESS(obj)             (G_TYPE_CHECK_INSTANCE_CAST ((obj), DONNA_TYPE_TASK_PROCESS, DonnaTaskProcess))
#define DONNA_TASK_PROCESS_CLASS(klass)     (G_TYPE_CHECK_CLASS_CAST ((klass), DONNA_TYPE_TASK_PROCESS, BonnaTaskProcessClass))
#define DONNA_IS_TASK_PROCESS(obj)          (G_TYPE_CHECK_INSTANCE_TYPE ((obj), DONNA_TYPE_TASK_PROCESS))
#define DONNA_IS_TASK_PROCESS_CLASS(klass)  (G_TYPE_CHECK_CLASS_TYPE ((klass), DONNA_TYPE_TASK_PROCESS))
#define DONNA_TASK_PROCESS_GET_CLASS(obj)   (G_TYPE_INSTANCE_GET_CLASS ((obj), DONNA_TYPE_TASK_PROCESS, DonnaTaskProcessClass))

typedef struct _DonnaTaskProcess            DonnaTaskProcess;
typedef struct _DonnaTaskProcessClass       DonnaTaskProcessClass;
typedef struct _DonnaTaskProcessPrivate     DonnaTaskProcessPrivate;

#define DONNA_TASK_PROCESS_ERROR            g_quark_from_static_string ("DonnaTaskProcess-Error")
typedef enum
{
    DONNA_TASK_PROCESS_ERROR_NO_CMDLINE,
    DONNA_TASK_PROCESS_ERROR_READ,
    DONNA_TASK_PROCESS_ERROR_OTHER,
} DonnaTaskProcessError;

typedef enum
{
    DONNA_PIPE_OUTPUT = 0,
    DONNA_PIPE_ERROR
} DonnaPipe;

typedef void            (*task_init_fn)     (DonnaTaskProcess   *taskp,
                                             gpointer            data,
                                             GError            **error);
typedef DonnaTaskState  (*task_closer_fn)   (DonnaTask          *task,
                                             gpointer            data,
                                             gint                rc,
                                             DonnaTaskState      state);

struct _DonnaTaskProcess
{
    DonnaTask parent;

    DonnaTaskProcessPrivate *priv;
};

struct _DonnaTaskProcessClass
{
    DonnaTaskClass parent;
};

GType               donna_task_process_get_type     (void) G_GNUC_CONST;

DonnaTask *         donna_task_process_new          (const gchar        *workdir,
                                                     const gchar        *cmdline,
                                                     gboolean            wait,
                                                     task_closer_fn      closer,
                                                     gpointer            closer_data);
DonnaTask *         donna_task_process_new_init     (task_init_fn        init,
                                                     gpointer            data,
                                                     GDestroyNotify      destroy,
                                                     gboolean            wait,
                                                     task_closer_fn      closer,
                                                     gpointer            closer_data);
gboolean            donna_task_process_set_default_closer (
                                                     DonnaTaskProcess   *taskp);
gboolean            donna_task_process_set_workdir_to_curdir (
                                                     DonnaTaskProcess   *taskp,
                                                     DonnaApp           *app,
                                                     GError            **error);
gboolean            donna_task_process_set_ui_msg   (DonnaTaskProcess   *taskp);

G_END_DECLS

#endif /* __DONNA_TASK_PROCESS_H__ */
