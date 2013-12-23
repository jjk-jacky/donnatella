
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

/**
 * DonnaTaskPriority:
 * @DONNA_TASK_PRIORITY_LOW: Task has a low priority
 * @DONNA_TASK_PRIORITY_NORMAL: Task has a normal priority
 * @DONNA_TASK_PRIORITY_HIGH: Task has a high priority
 *
 * When handling (public) tasks, the #DonnaTaskManager will start tasks with
 * higher priority first, and lower priority last.
 */
typedef enum
{
    DONNA_TASK_PRIORITY_LOW,
    DONNA_TASK_PRIORITY_NORMAL,
    DONNA_TASK_PRIORITY_HIGH
} DonnaTaskPriority;

/**
 * DonnaTaskVisibility:
 * @DONNA_TASK_VISIBILITY_INTERNAL: Internal task (to run in its own thread)
 * @DONNA_TASK_VISIBILITY_INTERNAL_GUI: Internal task that must run in the
 * main/UI thread
 * @DONNA_TASK_VISIBILITY_INTERNAL_FAST: Internal task that will be fast &
 * cannot block (i.e. 100% in memory), so it can run directly in the current
 * thread (even main/UI one)
 * @DONNA_TASK_VISIBILITY_PULIC: Public task, to be handled by
 * #DonnaTaskManager. (Will ran in its own thread.)
 *
 * Visiblity of a task determines how it will be started. Internal tasks are not
 * visible to the user, can require to be run in the main/UI thread, or might be
 * run in the current thread.
 *
 * Public task will be handled by the task manager, providing the user a way to
 * interact with them (e.g. pause/cancel them, change their #DonnaTask:priority,
 * etc)
 */
typedef enum
{
    DONNA_TASK_VISIBILITY_INTERNAL,
    DONNA_TASK_VISIBILITY_INTERNAL_GUI,
    DONNA_TASK_VISIBILITY_INTERNAL_FAST,
    DONNA_TASK_VISIBILITY_PULIC
} DonnaTaskVisibility;

/**
 * DonnaTaskState:
 * @DONNA_TASK_STATE_UNKNOWN: State is unknown
 * @DONNA_TASK_STOPPED: Task has not started, and won't be auto-started by
 * #DonnaTaskManager
 * @DONNA_TASK_WAITING: Task has not started, will be auto-started by
 * #DonnaTaskManager as soon as possible
 * @DONNA_TASK_RUNNING: Task is running
 * @DONNA_TASK_PAUSING: A request to pause the task was sent
 * @DONNA_TASK_PAUSED: Task is paused
 * @DONNA_TASK_CANCELLING: A request to cancel the task was sent
 * @DONNA_TASK_CANCELLED: Task has been cancelled
 * @DONNA_TASK_DONE: Task successfully ran
 * @DONNA_TASK_FAILED: Task unsuccessfully ran
 * @DONNA_TASK_PRE_RUN: Task hasn't ran yet
 * @DONNA_TASK_IN_RUN: Task is currently running
 * @DONNA_TASK_POST_RUN: Task has ran
 *
 * State of a task.
 *
 * The difference between %DONNA_TASK_PAUSING & %DONNA_TASK_PAUSED matters
 * because the first one indicates that, while the request was sent, the task
 * worker hasn't (yet) taken it into account, and the task is still running.
 * Same applies when cancelling a task.
 *
 * The cycle of a task is to start in %DONNA_TASK_PRE_RUN state, eventually get
 * into %DONNA_TASK_IN_RUN and finally end in %DONNA_TASK_POST_RUN.
 * (Note that a task could go from %DONNA_TASK_PRE_RUN to %DONNA_TASK_POST_RUN
 * directly.)
 */
typedef enum
{
    /* donna_task_get_state (not_a_task) */
    DONNA_TASK_STATE_UNKNOWN    = (1 << 0),
    DONNA_TASK_STOPPED          = (1 << 1),
    DONNA_TASK_WAITING          = (1 << 2),
    DONNA_TASK_RUNNING          = (1 << 3),
    DONNA_TASK_PAUSING          = (1 << 4),
    DONNA_TASK_PAUSED           = (1 << 5),
    DONNA_TASK_CANCELLING       = (1 << 6),
    DONNA_TASK_DONE             = (1 << 7),
    DONNA_TASK_CANCELLED        = (1 << 8),
    DONNA_TASK_FAILED           = (1 << 9),

    DONNA_TASK_PRE_RUN          = (DONNA_TASK_STOPPED | DONNA_TASK_WAITING),
    DONNA_TASK_IN_RUN           = (DONNA_TASK_RUNNING | DONNA_TASK_PAUSING
            | DONNA_TASK_CANCELLING),
    DONNA_TASK_POST_RUN         = (DONNA_TASK_DONE | DONNA_TASK_CANCELLED
            | DONNA_TASK_FAILED),
} DonnaTaskState;

/**
 * DonnaTaskUpdate:
 * @DONNA_TASK_UPDATE_PROGRESS: Progress value of the task's process
 * @DONNA_TASK_UPDATE_PROGRESS_PULSE: Pulsate the task's progress
 * @DONNA_TASK_UPDATE_STATUS: A (small) text about the current state/progress
 *
 * Indicators of the task progress. When a %DONNA_TASK_VISIBILITY_PULIC task is
 * running, its worker can set those to provide feedback to the user.
 *
 * Those are of no use for internal tasks.
 *
 * See donna_task_update() for more.
 */
typedef enum
{
    DONNA_TASK_UPDATE_PROGRESS          = (1 << 0),
    DONNA_TASK_UPDATE_PROGRESS_PULSE    = (1 << 1),
    DONNA_TASK_UPDATE_STATUS            = (1 << 2),
} DonnaTaskUpdate;

/**
 * task_fn:
 * @task: The #DonnaTask the worker is running for
 * @data: The data given when creating @task
 *
 * A task worker, i.e. the function that will be called when the task is run.
 * The worker shall interact with @task using different functions such as
 * donna_task_update(), donna_task_set_return_value() or donna_task_set_error()
 *
 * It must return the state in which the task will end. This obviously must be a
 * %DONNA_TASK_POST_RUN state.
 */
typedef DonnaTaskState  (*task_fn)              (DonnaTask  *task,
                                                 gpointer    data);
/**
 * task_timeout_fn:
 * @task: The #DonnaTask for which the timeout expired
 * @data: The data given when setting up the timeout
 *
 * When the timeout delay given to donna_task_set_timeout() expires and the task
 * is still not done (i.e. in %DONNA_TASK_POST_RUN state) the function will be
 * called, always in the main/UI thread.
 * The timeout starts counting from the call to donna_task_prepare(), which is
 * usually called when donna_app_run_task() is called (and regardless of whether
 * it is started immediately or not, e.g. if the pool is full or, for public
 * tasks, if the task managers waits for another task to complete first, the
 * timeout might be called even before the task started running).
 *
 * Note that if the task were to end (it its own thread) while the timeout is
 * running, it would only be "official" (signals & callback triggered) after
 * the timeout has completed.
 */
typedef void            (*task_timeout_fn)      (DonnaTask  *task,
                                                 gpointer    data);
/**
 * task_callback_fn:
 * @task: The #DonnaTask which just reached %DONNA_TASK_POST_RUN state
 * @timeout_called: Whether or not the timeout for @task was triggered
 * @data: The data given when setting up the callback
 *
 * After the task reached its %DONNA_TASK_POST_RUN state, the callback will be
 * called, always in the main/UI thread.
 */
typedef void            (*task_callback_fn)     (DonnaTask  *task,
                                                 gboolean    timeout_called,
                                                 gpointer    data);
/**
 * task_duplicate_fn:
 * @data: The data given when setting up the duplicator
 * @error: %GError to use in case of error
 *
 * Must create a new #DonnaTask which is a duplicate of the original task is was
 * set to. (All required info needed must be provided by @data, since no pointer
 * to the original task is provided.)
 *
 * Returns: (transfer floating): A newly created floating #DonnaTask, or %NULL
 */
typedef DonnaTask *     (*task_duplicate_fn)    (gpointer    data,
                                                 GError    **error);

struct _DonnaTask
{
    /*< private >*/
    GInitiallyUnowned parent;

    DonnaTaskPrivate *priv;
};

struct _DonnaTaskClass
{
    /*< private >*/
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
gboolean            donna_task_set_worker       (DonnaTask          *task,
                                                 task_fn             func,
                                                 gpointer            data,
                                                 GDestroyNotify      destroy);
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
gboolean            donna_task_wait_for_it      (DonnaTask          *task,
                                                 DonnaTask          *current_task,
                                                 GError            **error);
int                 donna_task_get_wait_fd      (DonnaTask          *task);

gboolean            donna_task_has_taskui       (DonnaTask          *task);
gboolean            donna_task_can_be_duplicated(DonnaTask          *task);
DonnaTask *         donna_task_get_duplicate    (DonnaTask          *task,
                                                 GError            **error);
DonnaTaskState      donna_task_get_state        (DonnaTask          *task);
gchar *             donna_task_get_desc         (DonnaTask          *task);
const GError *      donna_task_get_error        (DonnaTask          *task);
const GValue *      donna_task_get_return_value (DonnaTask          *task);
void                donna_task_prepare          (DonnaTask          *task);
void                donna_task_run              (DonnaTask          *task);
gboolean            donna_task_set_autostart    (DonnaTask          *task,
                                                 gboolean            autostart);
void                donna_task_pause            (DonnaTask          *task);
void                donna_task_resume           (DonnaTask          *task);
void                donna_task_cancel           (DonnaTask          *task);

int                 donna_task_get_fd           (DonnaTask          *task);
gboolean            donna_task_is_cancelling    (DonnaTask          *task);
void                donna_task_update           (DonnaTask          *task,
                                                 DonnaTaskUpdate     update,
                                                 gdouble             progress,
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
