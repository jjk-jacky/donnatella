
#include <glib-object.h>
#include <sys/eventfd.h>
#include <unistd.h>         /* read(), write(), close() */
#include <stdarg.h>         /* va_args stuff */
#include "task.h"
#include "util.h"           /* duplicate_gvalue() */
#include "closures.h"

struct _DonnaTaskPrivate
{
    /* task desc */
    gchar               *desc;
    /* task priority */
    DonnaTaskPriority    priority;
    /* task status (current operation being done) */
    gchar               *status;
    /* task progress */
    gdouble              progress;
    /* task "public" state */
    DonnaTaskState       state;
    /* devices involved */
    gchar              **devices;
    /* TaskUI for the task */
    DonnaTaskUi         *taskui;

    /* task function */
    task_fn              task_fn;
    gpointer             task_data;
    GDestroyNotify       task_destroy;
    /* task duplicator */
    task_duplicate_fn    duplicate_fn;
    gpointer             duplicate_data;
    GDestroyNotify       duplicate_destroy;
    /* task callback */
    task_callback_fn     callback_fn;
    gpointer             callback_data;
    GDestroyNotify       callback_destroy;
    /* task timeout */
    guint                timeout;
    guint                timeout_delay;
    task_timeout_fn      timeout_fn;
    gpointer             timeout_data;
    GDestroyNotify       timeout_destroy;

    /* lock to change the task state (also for handling timeout) */
    GMutex               mutex;
    /* condition to handle pause */
    GCond                cond;
    /* fd that can be used to help handle cancel/pause stuff */
    int                  fd;
    /* NULL-terminated array of nodes to be selected on List */
    DonnaNode          **nodes_for_selection;
    /* to hold the return value */
    GValue              *value;
    /* to hold the error */
    GError              *error;

    guint                task_ran : 1;
    guint                timeout_ran : 1;
    guint                timeout_destroyed : 1;
};

#define LOCK_TASK(task)     g_mutex_lock (&task->priv->mutex)
#define UNLOCK_TASK(task)   g_mutex_unlock (&task->priv->mutex)

enum
{
    UPDATED,
    NEW_STATE,
    NEW_PRIORITY,
    NB_SIGNALS
};

static guint donna_task_signals[NB_SIGNALS] = { 0 };

static void donna_task_finalize     (GObject        *object);

static void
donna_task_class_init (DonnaTaskClass *klass)
{
    GObjectClass *o_class;

    o_class = G_OBJECT_CLASS (klass);
    o_class->finalize = donna_task_finalize;

    donna_task_signals[UPDATED] =
        g_signal_new ("updated",
            DONNA_TYPE_TASK,
            G_SIGNAL_RUN_FIRST,
            G_STRUCT_OFFSET (DonnaTaskClass, updated),
            NULL,
            NULL,
            g_cclosure_user_marshal_VOID__BOOLEAN_INT_BOOLEAN_STRING,
            G_TYPE_NONE,
            4,
            G_TYPE_BOOLEAN,
            G_TYPE_INT,
            G_TYPE_BOOLEAN,
            G_TYPE_STRING);
    donna_task_signals[NEW_STATE] =
        g_signal_new ("new-state",
            DONNA_TYPE_TASK,
            G_SIGNAL_RUN_FIRST,
            G_STRUCT_OFFSET (DonnaTaskClass, new_state),
            NULL,
            NULL,
            g_cclosure_marshal_VOID__INT,
            G_TYPE_NONE,
            1,
            G_TYPE_INT);
    donna_task_signals[NEW_PRIORITY] =
        g_signal_new ("new-priority",
            DONNA_TYPE_TASK,
            G_SIGNAL_RUN_FIRST,
            G_STRUCT_OFFSET (DonnaTaskClass, new_priority),
            NULL,
            NULL,
            g_cclosure_marshal_VOID__INT,
            G_TYPE_NONE,
            1,
            G_TYPE_INT);

    g_type_class_add_private (klass, sizeof (DonnaTaskPrivate));
}

static void
donna_task_init (DonnaTask *task)
{
    DonnaTaskPrivate *priv;

    priv = task->priv = G_TYPE_INSTANCE_GET_PRIVATE (task,
            DONNA_TYPE_TASK,
            DonnaTaskPrivate);

    priv->desc      = NULL;
    priv->priority  = DONNA_TASK_PRIORITY_NORMAL;
    priv->status    = NULL;
    priv->progress  = -1.0;
    priv->state     = DONNA_TASK_WAITING;
    priv->devices   = NULL;
    priv->fd        = -1;
    priv->nodes_for_selection = NULL;
    priv->value     = NULL;
    priv->error     = NULL;
    g_mutex_init (&priv->mutex);
    g_cond_init (&priv->cond);
}

G_DEFINE_TYPE (DonnaTask, donna_task, G_TYPE_INITIALLY_UNOWNED)

static void
free_nodes_array (DonnaNode **nodes)
{
    DonnaNode **n;
    if (!nodes)
        return;
    for (n = nodes; *n; ++n)
        g_object_unref (*n);
    g_free (nodes);
}

static void
donna_task_finalize (GObject *object)
{
    DonnaTaskPrivate *priv;

    priv = DONNA_TASK (object)->priv;
    g_free (priv->desc);
    g_strfreev (priv->devices);
    g_free (priv->status);
    if (priv->fd >= 0)
        close (priv->fd);
    free_nodes_array (priv->nodes_for_selection);
    if (priv->value)
    {
        g_value_unset (priv->value);
        g_slice_free (GValue, priv->value);
    }
    g_clear_error (&priv->error);
    g_mutex_clear (&priv->mutex);
    g_cond_clear (&priv->cond);

    if (!priv->task_ran)
    {
        if (priv->task_data && priv->task_destroy)
            priv->task_destroy (priv->task_data);
        if (priv->callback_data && priv->callback_destroy)
            priv->callback_destroy (priv->callback_data);
    }
    if (!priv->timeout_ran && priv->timeout_data && priv->timeout_destroy)
        priv->timeout_destroy (priv->timeout_data);
    if (priv->duplicate_data && priv->duplicate_destroy)
        priv->duplicate_destroy (priv->duplicate_data);

    G_OBJECT_CLASS (donna_task_parent_class)->finalize (object);
}

DonnaTask *
donna_task_new (task_fn             func,
                gpointer            data,
                GDestroyNotify      destroy)
{
    DonnaTask *task;
    DonnaTaskPrivate *priv;

    g_return_val_if_fail (func != NULL, NULL);

    task = g_object_new (DONNA_TYPE_TASK, NULL);
    priv = task->priv;

    priv->task_fn = func;
    priv->task_data = data;
    priv->task_destroy = destroy;
    priv->task_ran = 0;
    priv->timeout = 0;
    priv->timeout_ran = 0;
    priv->timeout_destroyed = 0;

    return task;
}

DonnaTask *
donna_task_new_full (task_fn             func,
                     gpointer            data,
                     GDestroyNotify      destroy,
                     DonnaTaskUi        *taskui,
                     gchar             **devices,
                     DonnaTaskPriority   priority,
                     gboolean            autostart,
                     gchar              *desc)
{
    DonnaTask *task;
    DonnaTaskPrivate *priv;

    g_return_val_if_fail (func != NULL, NULL);

    task = g_object_new (DONNA_TYPE_TASK, NULL);
    priv = task->priv;

    priv->task_fn = func;
    priv->task_data = data;
    priv->task_destroy = destroy;
    priv->task_ran = 0;
    priv->timeout = 0;
    priv->timeout_ran = 0;
    priv->timeout_destroyed = 0;

    priv->taskui = taskui;
    priv->devices = devices;
    priv->priority = priority;
    if (!autostart)
        priv->state = DONNA_TASK_STOPPED;
    priv->desc = desc;

    return task;
}

gboolean
donna_task_set_taskui (DonnaTask *task, DonnaTaskUi *taskui)
{
    g_return_val_if_fail (DONNA_IS_TASK (task), FALSE);
    g_return_val_if_fail (DONNA_IS_TASKUI (taskui), FALSE);
    g_return_val_if_fail (task->priv->taskui == NULL, FALSE);

    task->priv->taskui = taskui;
    return TRUE;
}

gboolean
donna_task_set_devices (DonnaTask *task, gchar **devices)
{
    g_return_val_if_fail (DONNA_IS_TASK (task), FALSE);
    g_return_val_if_fail (task->priv->devices == NULL, FALSE);

    task->priv->devices = devices;
    return TRUE;
}

gboolean
donna_task_set_duplicator (DonnaTask        *task,
                           task_duplicate_fn duplicate,
                           gpointer          data,
                           GDestroyNotify    destroy)
{
    g_return_val_if_fail (DONNA_IS_TASK (task), FALSE);
    g_return_val_if_fail (duplicate != NULL, FALSE);
    g_return_val_if_fail (task->priv->duplicate_fn == NULL, FALSE);

    task->priv->duplicate_fn      = duplicate;
    task->priv->duplicate_data    = data;
    task->priv->duplicate_destroy = destroy;
    return TRUE;
}

gboolean
donna_task_set_desc (DonnaTask *task, gchar *desc)
{
    g_return_val_if_fail (DONNA_IS_TASK (task), FALSE);
    g_return_val_if_fail (desc != NULL, FALSE);

    g_free (task->priv->desc);
    task->priv->desc = g_strdup (desc);
    return TRUE;
}

gboolean
donna_task_set_priority (DonnaTask *task, DonnaTaskPriority priority)
{
    g_return_val_if_fail (DONNA_IS_TASK (task), FALSE);

    task->priv->priority = priority;
    return TRUE;
}

gboolean
donna_task_set_autostart (DonnaTask *task, gboolean autostart)
{
    g_return_val_if_fail (DONNA_IS_TASK (task), FALSE);
    g_return_val_if_fail (task->priv->state & DONNA_TASK_PRE_RUN, FALSE);

    task->priv->state = (autostart) ? DONNA_TASK_WAITING : DONNA_TASK_STOPPED;
    return TRUE;
}

gboolean
donna_task_set_callback (DonnaTask       *task,
                         task_callback_fn callback,
                         gpointer         data,
                         GDestroyNotify   destroy)
{
    g_return_val_if_fail (DONNA_IS_TASK (task), FALSE);
    g_return_val_if_fail (callback != NULL, FALSE);
    g_return_val_if_fail (task->priv->callback_fn == NULL, FALSE);

    task->priv->callback_fn      = callback;
    task->priv->callback_data    = data;
    task->priv->callback_destroy = destroy;
    return TRUE;
}

gboolean
donna_task_set_timeout (DonnaTask       *task,
                        guint            delay,
                        task_timeout_fn  timeout,
                        gpointer         data,
                        GDestroyNotify   destroy)
{
    g_return_val_if_fail (DONNA_IS_TASK (task), FALSE);
    g_return_val_if_fail (delay > 0, FALSE);
    g_return_val_if_fail (timeout != NULL, FALSE);
    g_return_val_if_fail (task->priv->timeout_fn == NULL, FALSE);

    task->priv->timeout_delay   = delay;
    task->priv->timeout_fn      = timeout;
    task->priv->timeout_data    = data;
    task->priv->timeout_destroy = destroy;
    return TRUE;
}

gchar **
donna_task_get_devices (DonnaTask *task)
{
    gchar **devices;

    g_return_val_if_fail (DONNA_IS_TASK (task), NULL);
    LOCK_TASK (task);
    devices = g_strdupv (task->priv->devices);
    UNLOCK_TASK (task);
    return devices;
}

DonnaTaskPriority
donna_task_get_priority (DonnaTask *task)
{
    g_return_val_if_fail (DONNA_IS_TASK (task), DONNA_TASK_PRIORITY_UNKNOWN);
    return task->priv->priority;
}

gchar *
donna_task_get_desc (DonnaTask *task)
{
    gchar *desc;

    g_return_val_if_fail (DONNA_IS_TASK (task), NULL);
    LOCK_TASK (task);
    desc = g_strdup (task->priv->desc);
    UNLOCK_TASK (task);
    return desc;
}

DonnaTaskUi *
donna_task_get_taskui (DonnaTask *task)
{
    g_return_val_if_fail (DONNA_IS_TASK (task), NULL);
    if (task->priv->taskui)
        return g_object_ref (task->priv->taskui);
    else
        return NULL;
}

DonnaTaskState
donna_task_get_state (DonnaTask *task)
{
    g_return_val_if_fail (DONNA_IS_TASK (task), DONNA_TASK_STATE_UNKNOWN);
    return task->priv->state;
}

gdouble
donna_task_get_progress (DonnaTask *task)
{
    g_return_val_if_fail (DONNA_IS_TASK (task), -1);
    return task->priv->progress;
}

gchar *
donna_task_get_status (DonnaTask *task)
{
    gchar *status;

    g_return_val_if_fail (DONNA_IS_TASK (task), NULL);
    LOCK_TASK (task);
    status = g_strdup (task->priv->status);
    UNLOCK_TASK (task);
    return status;
}

DonnaNode **
donna_task_get_nodes_for_selection (DonnaTask *task)
{
    g_return_val_if_fail (DONNA_IS_TASK (task), NULL);
    return task->priv->nodes_for_selection;
}

const GError *
donna_task_get_error (DonnaTask *task)
{
    g_return_val_if_fail (DONNA_IS_TASK (task), NULL);
    return (const GError *) task->priv->error;
}

const GValue *
donna_task_get_return_value (DonnaTask *task)
{
    g_return_val_if_fail (DONNA_IS_TASK (task), NULL);
    return (const GValue *) task->priv->value;
}

gboolean
donna_task_can_be_duplicated (DonnaTask *task)
{
    g_return_val_if_fail (DONNA_IS_TASK (task), FALSE);
    return task->priv->duplicate_fn != NULL;
}

DonnaTask *
donna_task_get_duplicate (DonnaTask *task)
{
    DonnaTaskPrivate *priv;

    g_return_val_if_fail (DONNA_IS_TASK (task), NULL);
    priv = task->priv;

    if (!priv->duplicate_fn)
        return NULL;

    return priv->duplicate_fn (priv->duplicate_data);
}

static gboolean
signal_new_state_cb (gpointer data)
{
    g_signal_emit (G_OBJECT (data), donna_task_signals[NEW_STATE], 0,
            DONNA_TASK (data)->priv->state);
    return FALSE;
}
#define signal_new_state(task)   \
    g_main_context_invoke (NULL, signal_new_state_cb, task)

static gboolean
timeout_cb (gpointer data)
{
    DonnaTask *task = (DonnaTask *) data;
    DonnaTaskPrivate *priv = task->priv;

    LOCK_TASK (task);

    /* if the timeout data was destroyed, we don't trigger it. It could have
     * been destroyed in the task's thread right before we took the lock */
    if (priv->timeout_destroyed)
    {
        UNLOCK_TASK (task);
        return FALSE;
    }

    /* remove the timeout */
    g_source_remove (priv->timeout);
    priv->timeout = 0;

    /* call the timeout callback under lock (to ensure if task_fn ends
     * meanwhile (this is in main thread), it'll wait for the timeout callback
     * to end) */
    priv->timeout_fn (task, priv->timeout_data);
    priv->timeout_ran = 1;

    /* done */
    UNLOCK_TASK (task);
    return FALSE;
}

static gboolean
callback_cb (gpointer data)
{
    DonnaTask *task = (DonnaTask *) data;
    DonnaTaskPrivate *priv = task->priv;

    priv->callback_fn (task, priv->timeout_ran, priv->callback_data);
    /* remove the reference we had on the task */
    g_object_unref (task);

    return FALSE;
}

void
donna_task_run (DonnaTask *task)
{
    DonnaTaskPrivate *priv;

    g_return_if_fail (DONNA_IS_TASK (task));

    priv = task->priv;

    LOCK_TASK (task);

    /* can only run/start from waiting */
    if (priv->state != DONNA_TASK_WAITING)
    {
        UNLOCK_TASK (task);
        return;
    }

    /* we take a ref on the task, to ensure it won't die while running */
    g_object_ref (task);

    /* install the timeout (will be triggered on main thread) */
    if (priv->timeout_fn)
        priv->timeout = g_timeout_add (priv->timeout_delay, timeout_cb, task);

    /* let's switch and release the lock */
    priv->state = DONNA_TASK_RUNNING;
    UNLOCK_TASK (task);

    /* notify change of state (in main thread, to avoiding blocking/delaying
     * the task function) */
    signal_new_state (task);

    /* do the work & get new state */
    priv->state = priv->task_fn (task, priv->task_data);

    /* get the lock back */
    LOCK_TASK (task);
    priv->task_ran = 1;

    /* remove the timeout if it's still there */
    if (priv->timeout > 0)
    {
        g_source_remove (priv->timeout);
        priv->timeout = 0;

        if (priv->timeout_data && priv->timeout_destroy)
            priv->timeout_destroy (priv->timeout_data);
        priv->timeout_destroyed = 1;
    }

    /* we're done with the lock */
    UNLOCK_TASK (task);

    /* notify change of state (in main thread) */
    signal_new_state (task);

    if (priv->callback_fn)
        /* trigger the callback in main thread -- our reference on task will
         * be removed after the callback has been triggered */
        g_main_context_invoke (NULL, callback_cb, task);
    else
        /* remove our reference on task */
        g_object_unref (task);
}

void
donna_task_pause (DonnaTask *task)
{
    DonnaTaskPrivate *priv;

    g_return_if_fail (DONNA_IS_TASK (task));

    LOCK_TASK (task);

    priv = task->priv;
    /* one can only pause a running task */
    if (priv->state != DONNA_TASK_RUNNING)
    {
        UNLOCK_TASK (task);
        return;
    }

    priv->state = DONNA_TASK_PAUSING;

    /* unblock the fd, so task_fn can see the change of state */
    if (priv->fd >= 0)
    {
        guint64 one = 1;
        write (priv->fd, &one, sizeof (one));
    }

    UNLOCK_TASK (task);

    /* we don't notify the change of state, because it's a "transitional" state,
     * and we should only notify once we changed to PAUSED */
}

void
donna_task_resume (DonnaTask *task)
{
    DonnaTaskPrivate *priv;
    DonnaTaskState state;

    g_return_if_fail (DONNA_IS_TASK (task));

    LOCK_TASK (task);

    priv = task->priv;
    state = priv->state;
    /* one can only resume a paused task. Going from pausing to running is ok */
    if (state != DONNA_TASK_PAUSING && state != DONNA_TASK_PAUSED)
    {
        UNLOCK_TASK (task);
        return;
    }

    priv->state = DONNA_TASK_RUNNING;

    /* wake up task_fn if we're cancelling a paused task */
    if (state == DONNA_TASK_PAUSED)
        g_cond_signal (&priv->cond);

    UNLOCK_TASK (task);

    /* notify change of state (in main thread) */
    signal_new_state (task);
}

void
donna_task_cancel (DonnaTask *task)
{
    DonnaTaskPrivate *priv;
    DonnaTaskState state;

    g_return_if_fail (DONNA_IS_TASK (task));

    LOCK_TASK (task);

    priv = task->priv;
    state = priv->state;
    /* one can cancel a task if it's running, paused or about to be paused.
     * Going from pausing to cancelling isn't a problem */
    if (state != DONNA_TASK_RUNNING && state != DONNA_TASK_PAUSING
            && state != DONNA_TASK_PAUSED)
    {
        UNLOCK_TASK (task);
        return;
    }

    priv->state = DONNA_TASK_CANCELLING;

    /* unblock the fd, so task_fn can see the change of state */
    if (priv->fd >= 0)
    {
        guint64 one = 1;
        write (priv->fd, &one, sizeof (one));
    }

    /* wake up task_fn if we're cancelling a paused task */
    if (state == DONNA_TASK_PAUSED)
        g_cond_signal (&priv->cond);

    UNLOCK_TASK (task);

    /* we don't notify the change of state, because it's a "transitional" state,
     * and we should only notify once the function returns */
}

int
donna_task_get_fd (DonnaTask *task)
{
    DonnaTaskPrivate *priv;

    g_return_val_if_fail (DONNA_IS_TASK (task), -1);

    priv = task->priv;

    LOCK_TASK (task);

    /* though this function should only called once, by task_fn, and therefore
     * this should always be true... let's avoid re-creating another one */
    if (priv->fd == -1)
        priv->fd = eventfd (0, EFD_CLOEXEC | EFD_NONBLOCK);

    UNLOCK_TASK (task);

    return priv->fd;
}

gboolean
donna_task_is_cancelling (DonnaTask *task)
{
    DonnaTaskPrivate *priv;

    g_return_val_if_fail (DONNA_IS_TASK (task), FALSE);

    priv = task->priv;
    if (priv->state == DONNA_TASK_RUNNING)
        return FALSE;
    if (priv->state == DONNA_TASK_CANCELLING)
        return TRUE;

    LOCK_TASK (task);

    switch (priv->state)
    {
        case DONNA_TASK_PAUSING:
            {
                gboolean ret;
                priv->state = DONNA_TASK_PAUSED;
                /* notify change of state (in main thread) */
                signal_new_state (task);
                /* wait for a change of state */
                while (priv->state == DONNA_TASK_PAUSED)
                    g_cond_wait (&priv->cond, &priv->mutex);
                /* now we can make the fd blocking again */
                if (priv->fd >= 0)
                {
                    char buffer[8];
                    while (read (priv->fd, buffer, sizeof (buffer)) == sizeof (buffer));
                }
                /* state can now only by running (resume) or cancelling */
                ret = priv->state == DONNA_TASK_CANCELLING;
                UNLOCK_TASK (task);
                return ret;
            }

        case DONNA_TASK_RUNNING:
            UNLOCK_TASK (task);
            return FALSE;

        case DONNA_TASK_CANCELLING:
            UNLOCK_TASK (task);
            return TRUE;

        default:
            g_warning ("task_is_cancelling was called on an invalid state (%d), "
                    "there is definately a bug somewhere",
                    priv->state);
            UNLOCK_TASK (task);
            return FALSE;
    }
}

struct signal_updated_data
{
    DonnaTask   *task;
    gboolean     new_progress;
    gchar       *status;
};

static gboolean
signal_updated_cb (gpointer data)
{
    struct signal_updated_data *su_data = data;
    DonnaTaskPrivate *priv = su_data->task->priv;

    g_signal_emit (su_data->task, donna_task_signals[UPDATED], 0,
            su_data->new_progress,
            priv->progress,
            (su_data->status != NULL),
            su_data->status);
    g_free (su_data->status);
    g_slice_free (struct signal_updated_data, su_data);
    return FALSE;
}

void
donna_task_update (DonnaTask    *task,
                   gboolean      has_progress,
                   gdouble       progress,
                   gboolean      has_status,
                   const gchar  *status_fmt,
                   ...)
{
    DonnaTaskPrivate *priv;
    struct signal_updated_data *su_data;

    g_return_if_fail (DONNA_IS_TASK (task));

    priv = task->priv;
    su_data = g_slice_new0 (struct signal_updated_data);
    su_data->task = task;

    if (has_progress)
        su_data->new_progress = priv->progress = progress;

    if (has_status)
    {
        LOCK_TASK (task);

        g_free (priv->status);

        if (status_fmt)
        {
            va_list args;

            va_start (args, status_fmt);
            priv->status = g_strdup_vprintf (status_fmt, args);
            va_end (args);
            /* copy the status for the signal, while we have the lock */
            su_data->status = g_strdup (priv->status);
        }
        else
            priv->status = NULL;

        UNLOCK_TASK (task);
    }

    /* emit signal updated in main thread */
    g_main_context_invoke (NULL, signal_updated_cb, su_data);
}

void
donna_task_set_nodes_for_selection (DonnaTask *task, DonnaNode **nodes)
{
    g_return_if_fail (DONNA_IS_TASK (task));
    free_nodes_array (task->priv->nodes_for_selection);
    task->priv->nodes_for_selection = nodes;
}

void
donna_task_set_error (DonnaTask     *task,
                      GQuark         domain,
                      gint           code,
                      const gchar   *format,
                      ...)
{
    DonnaTaskPrivate *priv;
    va_list args;

    g_return_if_fail (DONNA_IS_TASK (task));

    priv = task->priv;

    LOCK_TASK (task);

    if (priv->error)
        g_error_free (priv->error);

    va_start (args, format);
    priv->error = g_error_new_valist (domain, code, format, args);
    va_end (args);

    UNLOCK_TASK (task);
}

void
donna_task_take_error (DonnaTask *task,
                       GError    *error)
{
    DonnaTaskPrivate *priv;

    g_return_if_fail (DONNA_IS_TASK (task));

    priv = task->priv;

    LOCK_TASK (task);

    if (priv->error)
        g_error_free (priv->error);

    priv->error = error;

    UNLOCK_TASK (task);
}

void
donna_task_set_return_value (DonnaTask      *task,
                             const GValue   *value)
{
    DonnaTaskPrivate *priv;

    g_return_if_fail (DONNA_IS_TASK (task));

    priv = task->priv;

    LOCK_TASK (task);

    if (!priv->value)
        priv->value = duplicate_gvalue (value);
    else
    {
        g_value_unset (priv->value);
        g_value_init (priv->value, G_VALUE_TYPE (value));
        g_value_copy (value, priv->value);
    }

    UNLOCK_TASK (task);
}

GValue *
donna_task_grab_return_value (DonnaTask *task)
{
    DonnaTaskPrivate *priv;

    g_return_val_if_fail (DONNA_IS_TASK (task), NULL);

    priv = task->priv;

    LOCK_TASK (task);

    if (!priv->value)
        priv->value = g_slice_new0 (GValue);

    /* we de NOT unlock the task, this is done by release_return_value below */

    return priv->value;
}

void
donna_task_release_return_value (DonnaTask *task)
{
    g_return_if_fail (DONNA_IS_TASK (task));
    UNLOCK_TASK (task);
}
