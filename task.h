
#ifndef __DONNA_TASK_H__
#define __DONNA_TASK_H__

#include "common.h"
#include "taskui.h"

G_BEGIN_DECLS

#define DONNA_TASK_ERROR            g_quark_from_static_string ("DonnaTask-Error")
typedef enum
{
    DONNA_TASK_ERROR_NOMEM,
    DONNA_TASK_ERROR_OTHER
} DonnaTaskError;

typedef enum
{
    DONNA_TASK_PRIORITY_LOW,
    DONNA_TASK_PRIORITY_NORMAL,
    DONNA_TASK_PRIORITY_HIGH
} DonnaTaskPriority;

typedef enum
{
    /* internal task, to be ran in a new thread */
    DONNA_TASK_VISIBILITY_INTERNAL,
    /* internal task that must be run in the main thread (uses GTK functions) */
    DONNA_TASK_VISIBILITY_INTERNAL_GUI,
    /* internal task that will be fast/cannot block (i.e. 100% memory),
     * therefore can be run in current thread (even main/GUI one) */
    DONNA_TASK_VISIBILITY_INTERNAL_FAST,
    /* public task, i.e. to be sent to the task manager. Will ran in its own thread */
    DONNA_TASK_VISIBILITY_PULIC
} DonnaTaskVisibility;

typedef enum
{
    /* dona_task_get_state (not_a_task) */
    DONNA_TASK_STATE_UNKNOWN    = (1 << 0),
    /* task not started, no auto-start (by task manager) */
    DONNA_TASK_STOPPED          = (1 << 1),
    /* task not started, auto-start (by task manager) */
    DONNA_TASK_WAITING          = (1 << 2),
    /* function running */
    DONNA_TASK_RUNNING          = (1 << 3),
    /* user asked to pause */
    DONNA_TASK_PAUSING          = (1 << 4),
    /* function is paused */
    DONNA_TASK_PAUSED           = (1 << 5),
    /* user asked to cancel */
    DONNA_TASK_CANCELLING       = (1 << 6),
    /* function done, return codes: */
    DONNA_TASK_DONE             = (1 << 7),
    DONNA_TASK_CANCELLED        = (1 << 8),
    DONNA_TASK_FAILED           = (1 << 9),

    /* task hasn't ran yet */
    DONNA_TASK_PRE_RUN          = (DONNA_TASK_STOPPED | DONNA_TASK_WAITING),
    /* task is running */
    DONNA_TASK_IN_RUN           = (DONNA_TASK_RUNNING | DONNA_TASK_PAUSING
            | DONNA_TASK_CANCELLING),
    /* task has ran */
    DONNA_TASK_POST_RUN         = (DONNA_TASK_DONE | DONNA_TASK_CANCELLED
            | DONNA_TASK_FAILED),
} DonnaTaskState;

typedef DonnaTaskState  (*task_fn)              (DonnaTask  *task,
                                                 gpointer    data);
typedef void            (*task_timeout_fn)      (DonnaTask  *task,
                                                 gpointer    data);
typedef void            (*task_callback_fn)     (DonnaTask  *task,
                                                 gboolean    timeout_called,
                                                 gpointer    data);
typedef DonnaTask *     (*task_duplicate_fn)    (gpointer    data,
                                                 GError    **error);

struct _DonnaTask
{
    GInitiallyUnowned parent;

    DonnaTaskPrivate *priv;
};

struct _DonnaTaskClass
{
    GInitiallyUnownedClass parent;
};

DonnaTask *         donna_task_new              (task_fn             func,
                                                 gpointer            data,
                                                 GDestroyNotify      destroy);
DonnaTask *         donna_task_new_full         (task_fn             func,
                                                 gpointer            data,
                                                 GDestroyNotify      destroy,
                                                 DonnaTaskUi        *taskui,
                                                 GPtrArray          *devices,
                                                 DonnaTaskVisibility visibility,
                                                 DonnaTaskPriority   priority,
                                                 gboolean            autostart,
                                                 const gchar        *desc);
gboolean            donna_task_set_taskui       (DonnaTask          *task,
                                                 DonnaTaskUi        *taskui);
gboolean            donna_task_set_devices      (DonnaTask          *task,
                                                 GPtrArray          *devices);
gboolean            donna_task_set_visibility   (DonnaTask          *task,
                                                 DonnaTaskVisibility visibility);
gboolean            donna_task_set_duplicator   (DonnaTask          *task,
                                                 task_duplicate_fn   duplicate,
                                                 gpointer            data,
                                                 GDestroyNotify      destroy);

gboolean            donna_task_set_desc         (DonnaTask          *task,
                                                 const gchar        *desc);
gboolean            donna_task_take_desc        (DonnaTask          *task,
                                                 gchar              *desc);
gboolean            donna_task_prefix_desc      (DonnaTask          *task,
                                                 const gchar        *prefix);
gboolean            donna_task_set_callback     (DonnaTask          *task,
                                                 task_callback_fn    callback,
                                                 gpointer            data,
                                                 GDestroyNotify      destroy);
gboolean            donna_task_set_timeout      (DonnaTask          *task,
                                                 guint               delay,
                                                 task_timeout_fn     timeout,
                                                 gpointer            data,
                                                 GDestroyNotify      destroy);
gboolean            donna_task_set_can_block    (DonnaTask          *task);
gboolean            donna_task_wait_for_it      (DonnaTask          *task);
int                 donna_task_get_wait_fd      (DonnaTask          *task);

gboolean            donna_task_can_be_duplicated(DonnaTask          *task);
DonnaTask *         donna_task_get_duplicate    (DonnaTask          *task,
                                                 GError            **error);
DonnaTaskState      donna_task_get_state        (DonnaTask          *task);
const GError *      donna_task_get_error        (DonnaTask          *task);
const GValue *      donna_task_get_return_value (DonnaTask          *task);
void                donna_task_prepare          (DonnaTask          *task);
void                donna_task_run              (DonnaTask          *task);
void                donna_task_pause            (DonnaTask          *task);
void                donna_task_resume           (DonnaTask          *task);
void                donna_task_cancel           (DonnaTask          *task);

int                 donna_task_get_fd           (DonnaTask          *task);
gboolean            donna_task_is_cancelling    (DonnaTask          *task);
void                donna_task_update           (DonnaTask          *task,
                                                 gboolean            has_progress,
                                                 gdouble             progress,
                                                 gboolean            has_status,
                                                 const gchar        *status_fmt,
                                                 ...);
void                donna_task_set_nodes_for_selection (DonnaTask   *task,
                                                 GPtrArray          *nodes);
void                donna_task_set_error        (DonnaTask          *task,
                                                 GQuark              domain,
                                                 gint                code,
                                                 const gchar        *format,
                                                 ...);
void                donna_task_take_error       (DonnaTask          *task,
                                                 GError             *error);
void                donna_task_set_return_value (DonnaTask          *task,
                                                 const GValue       *value);
GValue *            donna_task_grab_return_value(DonnaTask          *task);
void                donna_task_release_return_value (DonnaTask      *task);

G_END_DECLS

#endif /* __DONNA_TASK_H__ */
