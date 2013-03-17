
#include <glib-object.h>
#include <sys/eventfd.h>
#include <unistd.h>         /* read(), write(), close() */
#include <stdarg.h>         /* va_args stuff */
#include <string.h>         /* strlen() */
#include <stdio.h>          /* snprintf() */
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
    GPtrArray           *devices;
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
    /* GPtrArray of nodes to be selected on List */
    GPtrArray           *nodes_for_selection;
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
    PROP_0,

    PROP_DESC,
    PROP_PRIORITY,
    PROP_STATUS,
    PROP_PROGRESS,
    PROP_STATE,
    PROP_DEVICES,
    PROP_TASKUI,
    PROP_NODES_FOR_SELECTION,
    PROP_ERROR,
    PROP_RETURN_VALUE,

    NB_PROPS
};

static GParamSpec * donna_task_props[NB_PROPS] = { NULL, };

static void donna_task_get_property (GObject        *object,
                                     guint           prop_id,
                                     GValue         *value,
                                     GParamSpec     *pspec);
static void donna_task_set_property (GObject        *object,
                                     guint           prop_id,
                                     const GValue   *value,
                                     GParamSpec     *pspec);
static void donna_task_finalize     (GObject        *object);

static void
donna_task_class_init (DonnaTaskClass *klass)
{
    GObjectClass *o_class;

    o_class = G_OBJECT_CLASS (klass);
    o_class->get_property   = donna_task_get_property;
    o_class->set_property   = donna_task_set_property;
    o_class->finalize       = donna_task_finalize;

    donna_task_props[PROP_DESC] =
        g_param_spec_string ("desc", "desc",
                "Description of the task",
                NULL,     /* default */
                G_PARAM_READWRITE);
    donna_task_props[PROP_PRIORITY] =
        g_param_spec_int ("priority", "priority",
                "Priority of the task",
                DONNA_TASK_PRIORITY_LOW,    /* minimum */
                DONNA_TASK_PRIORITY_HIGH,   /* maximum */
                DONNA_TASK_PRIORITY_NORMAL, /* default */
                G_PARAM_READWRITE);
    donna_task_props[PROP_STATUS] =
        g_param_spec_string ("status", "status",
                "Current status/operation of the task",
                NULL,   /* default */
                G_PARAM_READABLE);
    donna_task_props[PROP_PROGRESS] =
        g_param_spec_double ("progress", "progress",
                "Current progress of the task",
                0.0,    /* minimum */
                1.0,    /* maximum */
                0.0,    /* default */
                G_PARAM_READABLE);
    donna_task_props[PROP_STATE] =
        g_param_spec_int ("state", "state",
                "Current state of the task",
                DONNA_TASK_STATE_UNKNOWN,   /* minimum */
                DONNA_TASK_FAILED,          /* maximum */
                DONNA_TASK_WAITING,         /* default */
                G_PARAM_READABLE);
    donna_task_props[PROP_DEVICES] =
        g_param_spec_boxed ("devices", "devices",
                "List of devices involved/used by the task",
                G_TYPE_PTR_ARRAY,
                G_PARAM_READABLE);
    donna_task_props[PROP_TASKUI] =
        g_param_spec_boxed ("taskui", "taskui",
                "TaskUI object to provider user interaction for the task",
                DONNA_TYPE_TASKUI,
                G_PARAM_READABLE);
    donna_task_props[PROP_NODES_FOR_SELECTION] =
        g_param_spec_boxed ("nodes-for-selection", "nodes-for-selection",
                "List of nodes to be selected on List",
                G_TYPE_PTR_ARRAY,
                G_PARAM_READABLE);
    donna_task_props[PROP_ERROR] =
        /* pointer even though it's boxed, to avoid needing to copy/free */
        g_param_spec_pointer ("error", "error",
                "Error of this task",
                G_PARAM_READABLE);
    donna_task_props[PROP_RETURN_VALUE] =
        g_param_spec_pointer ("return-value", "return-value",
                "Return value of the task",
                G_PARAM_READABLE);

    g_object_class_install_properties (o_class, NB_PROPS, donna_task_props);

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
    priv->progress  = 0.0;
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
donna_task_finalize (GObject *object)
{
    DonnaTaskPrivate *priv;

    priv = DONNA_TASK (object)->priv;
    g_free (priv->desc);
    if (priv->devices)
        g_ptr_array_unref (priv->devices);
    g_free (priv->status);
    if (priv->fd >= 0)
        close (priv->fd);
    if (priv->nodes_for_selection)
        g_ptr_array_unref (priv->nodes_for_selection);
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

static void
donna_task_get_property (GObject        *object,
                         guint           prop_id,
                         GValue         *value,
                         GParamSpec     *pspec)
{
    DonnaTaskPrivate *priv = DONNA_TASK (object)->priv;

    switch (prop_id)
    {
        case PROP_DESC:
            g_value_set_string (value, priv->desc);
            break;
        case PROP_PRIORITY:
            g_value_set_int (value, priv->priority);
            break;
        case PROP_STATUS:
            g_value_set_string (value, priv->status);
            break;
        case PROP_PROGRESS:
            g_value_set_double (value, priv->progress);
            break;
        case PROP_STATE:
            g_value_set_int (value, priv->state);
            break;
        case PROP_DEVICES:
            g_value_set_boxed (value, priv->devices);
            break;
        case PROP_TASKUI:
            g_value_set_boxed (value, priv->taskui);
            break;
        case PROP_NODES_FOR_SELECTION:
            g_value_set_boxed (value, priv->nodes_for_selection);
            break;
        case PROP_ERROR:
            g_value_set_pointer (value, priv->error);
            break;
        case PROP_RETURN_VALUE:
            g_value_set_pointer (value, priv->value);
            break;
        default:
            G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
            break;
    }

}

static void
donna_task_set_property (GObject        *object,
                         guint           prop_id,
                         const GValue   *value,
                         GParamSpec     *pspec)
{
    DonnaTaskPrivate *priv = DONNA_TASK (object)->priv;
    gchar *s;

    switch (prop_id)
    {
        case PROP_DESC:
            s = priv->desc;
            priv->desc = g_value_dup_string (value);
            g_free (s);
            break;
        case PROP_PRIORITY:
            priv->priority = g_value_get_int (value);
            break;
        default:
            G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
            break;
    }
}

struct notify_prop_data
{
    GObject *obj;
    gint     prop_id;
};

static gboolean
_notify_prop (struct notify_prop_data *data)
{
    g_object_notify_by_pspec (data->obj, donna_task_props[data->prop_id]);
    g_free (data);
    return FALSE;
}

static inline void
notify_prop (DonnaTask *task, gint prop_id)
{
    struct notify_prop_data *data;

    data = g_new (struct notify_prop_data, 1);
    data->obj = G_OBJECT (task);
    data->prop_id = prop_id;
    g_main_context_invoke (NULL, (GSourceFunc) _notify_prop, data);
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
                     GPtrArray          *devices,
                     DonnaTaskPriority   priority,
                     gboolean            autostart,
                     const gchar        *desc)
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
    priv->devices = g_ptr_array_ref (devices);
    priv->priority = priority;
    if (!autostart)
        priv->state = DONNA_TASK_STOPPED;
    if (desc)
        priv->desc = g_strdup (desc);

    return task;
}

gboolean
donna_task_set_taskui (DonnaTask *task, DonnaTaskUi *taskui)
{
    g_return_val_if_fail (DONNA_IS_TASK (task), FALSE);
    g_return_val_if_fail (DONNA_IS_TASKUI (taskui), FALSE);
    g_return_val_if_fail (task->priv->taskui == NULL, FALSE);

    task->priv->taskui = taskui;
    notify_prop (task, PROP_TASKUI);
    return TRUE;
}

gboolean
donna_task_set_devices (DonnaTask *task, GPtrArray *devices)
{
    g_return_val_if_fail (DONNA_IS_TASK (task), FALSE);
    g_return_val_if_fail (task->priv->devices == NULL, FALSE);

    task->priv->devices = g_ptr_array_ref (devices);
    notify_prop (task, PROP_DEVICES);
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
donna_task_set_desc (DonnaTask *task, const gchar *desc)
{
    DonnaTaskPrivate *priv;
    gchar *s;

    g_return_val_if_fail (DONNA_IS_TASK (task), FALSE);
    g_return_val_if_fail (desc != NULL, FALSE);

    priv = task->priv;

    s = priv->desc;
    priv->desc = g_strdup (desc);
    g_free (s);

    notify_prop (task, PROP_DESC);
    return TRUE;
}

gboolean
donna_task_take_desc (DonnaTask *task, gchar *desc)
{
    DonnaTaskPrivate *priv;
    gchar *s;

    g_return_val_if_fail (DONNA_IS_TASK (task), FALSE);
    g_return_val_if_fail (desc != NULL, FALSE);

    priv = task->priv;

    s = priv->desc;
    priv->desc = desc;
    g_free (s);

    notify_prop (task, PROP_DESC);
    return TRUE;
}

gboolean
donna_task_prefix_desc (DonnaTask *task, const gchar *prefix)
{
    DonnaTaskPrivate *priv;

    g_return_val_if_fail (DONNA_IS_TASK (task), FALSE);
    g_return_val_if_fail (prefix != NULL, FALSE);

    priv = task->priv;

    if (priv->desc)
    {
        gchar *old;
        gchar *s;
        gsize len;

        len = strlen (priv->desc) + strlen (prefix) + 1;
        s = g_new (gchar, len);
        snprintf (s, len, "%s%s", priv->desc, prefix);

        old = priv->desc;
        priv->desc = s;
        g_free (old);
    }
    else
        priv->desc = g_strdup (prefix);

    notify_prop (task, PROP_DESC);
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

DonnaTaskState
donna_task_get_state (DonnaTask *task)
{
    g_return_val_if_fail (DONNA_IS_TASK (task), DONNA_TASK_STATE_UNKNOWN);
    return task->priv->state;
}

const GError *
donna_task_get_error (DonnaTask *task)
{
    g_return_val_if_fail (DONNA_IS_TASK (task), NULL);
    return task->priv->error;
}

const GValue *
donna_task_get_return_value (DonnaTask *task)
{
    g_return_val_if_fail (DONNA_IS_TASK (task), NULL);
    return task->priv->value;
}

static inline const gchar *
state_name (DonnaTaskState state)
{
    switch (state)
    {
        case DONNA_TASK_STATE_UNKNOWN:
            return "unknown";
        case DONNA_TASK_STOPPED:
            return "stopped";
        case DONNA_TASK_WAITING:
            return "waiting";
        case DONNA_TASK_RUNNING:
            return "running";
        case DONNA_TASK_PAUSING:
            return "pausing";
        case DONNA_TASK_PAUSED:
            return "paused";
        case DONNA_TASK_CANCELLING:
            return "cancelling";
        case DONNA_TASK_DONE:
            return "done";
        case DONNA_TASK_CANCELLED:
            return "cancelled";
        case DONNA_TASK_FAILED:
            return "failed";
        default:
            return "invalid";
    }
}

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
    g_debug ("Starting task: %s",
            (priv->desc) ? priv->desc : "(no desc)");

    LOCK_TASK (task);

    /* can only run/start from waiting */
    if (!(priv->state & DONNA_TASK_PRE_RUN))
    {
        UNLOCK_TASK (task);
        g_debug ("Ending task, not in a pre-run state (%s): %s",
                state_name (priv->state),
                (priv->desc) ? priv->desc : "(no desc)");
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
    notify_prop (task, PROP_STATE);

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
    notify_prop (task, PROP_STATE);

    if (priv->callback_fn)
        /* trigger the callback in main thread -- our reference on task will
         * be removed after the callback has been triggered */
        g_main_context_invoke (NULL, callback_cb, task);
    else
        /* remove our reference on task */
        g_object_unref (task);

    g_debug ("Ending task (%s): %s",
            state_name (priv->state),
            (priv->desc) ? priv->desc : "(no desc)");
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
    notify_prop (task, PROP_STATE);
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
                notify_prop (task, PROP_STATE);
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
            g_critical ("task_is_cancelling was called on an invalid state (%d)",
                    priv->state);
            UNLOCK_TASK (task);
            return FALSE;
    }
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

    g_return_if_fail (DONNA_IS_TASK (task));

    priv = task->priv;

    if (has_progress)
    {
        priv->progress = progress;
        notify_prop (task, PROP_PROGRESS);
    }

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
        }
        else
            priv->status = NULL;

        UNLOCK_TASK (task);
        notify_prop (task, PROP_STATUS);
    }
}

void
donna_task_set_nodes_for_selection (DonnaTask *task, GPtrArray *nodes)
{
    DonnaTaskPrivate *priv;

    g_return_if_fail (DONNA_IS_TASK (task));

    if (priv->nodes_for_selection)
        g_ptr_array_unref (priv->nodes_for_selection);
    priv->nodes_for_selection = g_ptr_array_ref (nodes);
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
