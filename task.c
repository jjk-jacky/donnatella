
#include <glib-object.h>
#include <sys/eventfd.h>
#include <unistd.h>         /* read(), write(), close() */
#include <stdarg.h>         /* va_args stuff */
#include "task.h"
#include "util.h"           /* duplicate_gvalue() */

struct _DonnaTaskPrivate
{
    /* task desc -- NULL for internal task */
    gchar               *desc;
    /* task priority */
    DonnaTaskPriority    priority;
    /* task status (current operation being done) */
    gchar               *status;
    /* task progress */
    gdouble              progress;
    /* task "public" state */
    DonnaTaskState       state;

    /* task function */
    task_fn              task_fn;
    gpointer             task_data;
    /* task callback */
    task_callback_fn     callback_fn;
    gpointer             callback_data;
    /* task timeout */
    guint                timeout;
    guint                timeout_delay;
    task_timeout_fn      timeout_fn;
    gpointer             timeout_data;

    /* lock to change the task state (also for handling timeout) */
    GMutex               mutex;
    /* condition to handle pause */
    GCond                cond;
    /* fd that can be used to help handle cancel/pause stuff */
    int                  fd;
    /* to hold the return value */
    GValue              *value;
    /* to hold the error */
    GError              *error;
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
    NB_PROPERTIES
};

static GParamSpec *donna_task_properties[NB_PROPERTIES] = { NULL, };

static void donna_task_set_property (GObject        *object,
                                     guint           id,
                                     const GValue   *value,
                                     GParamSpec     *pspec);
static void donna_task_get_property (GObject        *object,
                                     guint           id,
                                     GValue         *value,
                                     GParamSpec     *pspec);
static void donna_task_finalize     (GObject        *object);

static void
donna_task_class_init (DonnaTaskClass *klass)
{
    GObjectClass *o_class;

    o_class = G_OBJECT_CLASS (klass);
    donna_task_properties[PROP_DESC] = g_param_spec_string ("desc",
            "desc",
            "Task description",
            NULL,
            G_PARAM_READABLE | G_PARAM_CONSTRUCT_ONLY);
    donna_task_properties[PROP_PRIORITY] = g_param_spec_int ("priority",
            "priority",
            "Task priority",
            DONNA_TASK_PRIORITY_LOW,    /* minimum */
            DONNA_TASK_PRIORITY_HIGH,   /* maximum */
            DONNA_TASK_PRIORITY_NORMAL, /* default */
            G_PARAM_READABLE | G_PARAM_WRITABLE);
    donna_task_properties[PROP_STATUS] = g_param_spec_string ("status",
            "status",
            "Task current status",
            NULL,
            G_PARAM_READABLE);
    donna_task_properties[PROP_PROGRESS] = g_param_spec_double ("progress",
            "progress",
            "Progress indicator of the task",
            0.0, /* minimum */
            1.0, /* maximum */
            0.0, /* default */
            G_PARAM_READABLE);
    donna_task_properties[PROP_STATUS] = g_param_spec_int ("state",
            "state",
            "Task current state",
            DONNA_TASK_WAITING, /* minimum */
            DONNA_TASK_FAILED,  /* maximum */
            DONNA_TASK_WAITING, /* default */
            G_PARAM_READABLE);

    g_object_class_install_properties (o_class, NB_PROPERTIES,
            donna_task_properties);

    o_class->set_property = donna_task_set_property;
    o_class->get_property = donna_task_get_property;
    o_class->finalize = donna_task_finalize;
    g_type_class_add_private (klass, sizeof (DonnaTaskPrivate));
}

static void
donna_task_init (DonnaTask *task)
{
    DonnaTaskPrivate *priv;

    priv = task->priv = G_TYPE_INSTANCE_GET_PRIVATE (task,
            DONNA_TYPE_TASK,
            DonnaTaskPrivate);

    priv->desc = NULL;
    priv->priority = DONNA_TASK_PRIORITY_NORMAL;
    priv->status = NULL;
    priv->progress = -1.0;
    priv->state = DONNA_TASK_WAITING;
    priv->fd = -1;
    priv->value = NULL;
    priv->error = NULL;
    g_mutex_init (&priv->mutex);
    g_cond_init (&priv->cond);
}

G_DEFINE_TYPE (DonnaTask, donna_task, G_TYPE_OBJECT)

static void
donna_task_finalize (GObject *object)
{
    DonnaTaskPrivate *priv;

    priv = DONNA_TASK (object)->priv;
    if (priv->desc)
        g_free (priv->desc);
    if (priv->status)
        g_free (priv->status);
    if (priv->fd >= 0)
        close (priv->fd);
    if (priv->value)
    {
        g_value_unset (priv->value);
        g_slice_free (GValue, priv->value);
    }
    g_clear_error (&priv->error);
    g_mutex_clear (&priv->mutex);
    g_cond_clear (&priv->cond);

    G_OBJECT_CLASS (donna_task_parent_class)->finalize (object);
}

static void
donna_task_set_property (GObject        *object,
                         guint           id,
                         const GValue   *value,
                         GParamSpec     *pspec)
{
    DonnaTask *task = DONNA_TASK (object);

    switch (id)
    {
        case PROP_PRIORITY:
            task->priv->priority = g_value_get_int (value);
            break;

        default:
            G_OBJECT_WARN_INVALID_PROPERTY_ID (object, id, pspec);
            break;
    }
}

static void
donna_task_get_property (GObject        *object,
                         guint           id,
                         GValue         *value,
                         GParamSpec     *pspec)
{
    DonnaTask *task = DONNA_TASK (object);

    switch (id)
    {
        case PROP_DESC:
            g_value_set_string (value, task->priv->desc);
            break;

        case PROP_PRIORITY:
            g_value_set_int (value, task->priv->priority);
            break;

        case PROP_STATUS:
            g_value_set_string (value, task->priv->status);
            break;

        case PROP_PROGRESS:
            g_value_set_double (value, task->priv->progress);
            break;

        case PROP_STATE:
            g_value_set_int (value, task->priv->state);
            break;

        default:
            G_OBJECT_WARN_INVALID_PROPERTY_ID (object, id, pspec);
            break;
    }
}

DonnaTask *
donna_task_new (gchar              *desc,
                task_fn             func,
                gpointer            data,
                task_callback_fn    callback,
                gpointer            callback_data,
                guint               timeout_delay,
                task_timeout_fn     timeout_callback,
                gpointer            timeout_data)
{
    DonnaTask *task;
    DonnaTaskPrivate *priv;

    g_return_val_if_fail (func != NULL, NULL);

    task = g_object_new (DONNA_TYPE_TASK, "desc", desc, NULL);
    priv = task->priv;

    priv->task_fn = func;
    priv->task_data = data;
    priv->callback_fn = callback;
    priv->callback_data = callback_data;
    priv->timeout = 0;
    priv->timeout_delay = timeout_delay;
    priv->timeout_fn = timeout_callback;
    priv->timeout_data = timeout_data;

    return task;
}

static gboolean
notify_state_change_cb (gpointer data)
{
    g_object_notify_by_pspec (G_OBJECT (data),
            donna_task_properties[PROP_STATE]);

    return FALSE;
}
#define notify_state_change(task)   \
    g_main_context_invoke (NULL, notify_state_change_cb, task)

static gboolean
timeout_cb (gpointer data)
{
    DonnaTask *task = (DonnaTask *) data;
    DonnaTaskPrivate *priv = task->priv;

    LOCK_TASK (task);

    /* make sure the timeout shall be triggered (it could have been removed
     * right before we took the lock) */
    if (priv->timeout == 0)
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

    /* done */
    UNLOCK_TASK (task);
    return FALSE;
}

void
donna_task_run (DonnaTask *task)
{
    DonnaTaskPrivate *priv;
    gboolean timeout_called = FALSE;

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
    notify_state_change (task);

    /* do the work & get new state */
    priv->state = priv->task_fn (task, priv->task_data);

    /* get the lock back */
    LOCK_TASK (task);

    /* remove the timeout if it's still there */
    if (priv->timeout > 0)
    {
        g_source_remove (priv->timeout);
        priv->timeout = 0;
    }
    else if (priv->timeout_fn)
        timeout_called = TRUE;

    /* we're done with the lock */
    UNLOCK_TASK (task);

    /* trigger the callback */
    priv->callback_fn (task, timeout_called, priv->callback_data);

    /* notify change of state */
    g_object_notify_by_pspec (G_OBJECT (task),
            donna_task_properties[PROP_STATE]);

    /* done with our reference */
    g_object_unref (task);
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

    /* notify change of state */
    g_object_notify_by_pspec (G_OBJECT (task),
            donna_task_properties[PROP_STATE]);
}

GError *
donna_task_get_error (DonnaTask  *task)
{
    g_return_val_if_fail (DONNA_IS_TASK (task), NULL);

    if (!task->priv->error)
        return NULL;

    return g_error_copy (task->priv->error);
}

gboolean
donna_task_get_return_value (DonnaTask  *task,
                             gboolean   *has_value,
                             GValue     *value)
{
    GValue *src;

    g_return_val_if_fail (DONNA_IS_TASK (task), FALSE);
    g_return_val_if_fail (has_value != NULL, FALSE);
    g_return_val_if_fail (G_IS_VALUE (value), FALSE);

    src = task->priv->value;
    if (!src)
    {
        *has_value = FALSE;
        return TRUE;
    }

    *has_value = TRUE;
    if (!g_value_type_compatible (G_VALUE_TYPE (src), G_VALUE_TYPE (value)))
        return FALSE;

    /* FIXME: instead, why not send the pointer to the GValue, so no need to
     * copy or anything, it gets free when the task is finalized (and one can
     * still do a g_value_copy shall they want/need to */
    g_value_copy (src, value);
    return TRUE;
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
    {
        priv->fd = eventfd (0, EFD_CLOEXEC | EFD_NONBLOCK);
    }

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
                /* notify change of state (in main thread, to avoid risks of
                 * deadlocks since we have a lock on the task) */
                notify_state_change (task);
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
        g_object_notify_by_pspec (G_OBJECT (task),
                donna_task_properties[PROP_PROGRESS]);
    }

    if (has_status)
    {
        LOCK_TASK (task);

        if (priv->status)
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

        g_object_notify_by_pspec (G_OBJECT (task),
                donna_task_properties[PROP_STATUS]);
    }
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
    {
        priv->value = duplicate_gvalue (value);
    }
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

    /* we de NOT unlock the task, this is done by release_reutrn_value below */

    return priv->value;
}

void
donna_task_release_return_value (DonnaTask *task)
{
    DonnaTaskPrivate *priv;

    g_return_if_fail (DONNA_IS_TASK (task));

    priv = task->priv;

    UNLOCK_TASK (task);
}
