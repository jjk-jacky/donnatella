/*
 * donnatella - Copyright (C) 2014 Olivier Brunel
 *
 * task.c
 * Copyright (C) 2014 Olivier Brunel <jjk@jjacky.com>
 *
 * This file is part of donnatella.
 *
 * donnatella is free software: you can redistribute it and/or modify it under
 * the terms of the GNU General Public License as published by the Free Software
 * Foundation, either version 3 of the License, or (at your option) any later
 * version.
 *
 * donnatella is distributed in the hope that it will be useful, but WITHOUT ANY
 * WARRANTY; without even the implied warranty of MERCHANTABILITY or FITNESS
 * FOR A PARTICULAR PURPOSE.
 * See the GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License along with
 * donnatella. If not, see http://www.gnu.org/licenses/
 */

#include "config.h"

#include <glib-unix.h>
#include <glib-object.h>
#include <sys/eventfd.h>
#include <sys/select.h>
#include <unistd.h>         /* read(), write(), close() */
#include <stdarg.h>         /* va_args stuff */
#include <string.h>         /* strlen() */
#include <stdio.h>          /* snprintf() */
#include <errno.h>
#include "task.h"
#include "debug.h"
#include "util.h"           /* duplicate_gvalue() */
#include "closures.h"
#include "macros.h"

/**
 * SECTION:task
 * @Short_description: A task to be run, preferably in a different thread
 *
 * In order not to block the main thread/UI, some operations should be run
 * asynchronously. For "internal" tasks (e.g. getting content of a folder) this
 * allows not to block the UI, for longer operations (e.g. copying files) it can
 * also allow to pause/abort said operation.
 *
 * Either way, this is done using #DonnaTask objects. A task represents an
 * operation to be ran, preferably not in the main thread. The #DonnaApp object
 * provides donna_app_run_task() to automatically run the task in another
 * thread for "internal" ones, or hand them over to the #DonnaTaskManager for
 * "public" ones (which will handle starting/pausing them automatically, as well
 * as providing user interface for pausing/cancelling them).
 *
 * Object will provide functions ending in _task to indicate they will simply
 * return a #DonnaTask to actually perform the operation. Parf of the API is
 * aimed at such "task creators," to set the worker (function to be ran), a
 * description of the task (used by the task manager or for debugging purposes),
 * etc
 *
 * Another part of the API allows the caller to optionally add a callback and/or
 * timeout to the task. Both will always be ran in the main thread. The timeout
 * allows to provide a visual feedback to the user should the task be "slow"
 * (especially useful for internal tasks), while the callback is triggered once
 * the task is finished (whether it was succesful, aborted or failed).
 *
 * A last part of the API is aimed for the worker, to update the task's progress
 * and/or status, as well as handle possible pausing/cancelling.
 *
 *
 * To create a task, that is to create a #DonnaTask from a *_task() function and
 * return it to the caller, you can use donna_task_new() to simply set the
 * worker (i.e. the function to be ran), the data pointer it will receive, and a
 * function to destroy said data.
 * Note that the destroy will only be called if the worker isn't ran, else it is
 * its responsibility to free the memory used by data.
 *
 * donna_task_new_full() allows to define more properties, that can also be set
 * afterwards :
 * - donna_task_set_taskui() to set the #DonnaTaskUi object that will provide
 *   additional UI about the task's process. This can be extra info while the
 *   process/task runs (e.g. file currently copied, speed, etc) or afterwards
 *   (e.g. detailled error messages/log).
 * - donna_task_set_devices() to set the list of devices involved in the task.
 *   This is to be used by #DonnaTaskManager to determine if multiple tasks can
 *   be run at the same time.
 * - donna_task_set_visibility() to set the #DonnaTask:visibility property of
 *   the task.  An internal task (%DONNA_TASK_VISIBILITY_INTERNAL) will be
 *   directly run in a dedicated thread by donna_app_run_task(), whereas a
 *   public task (%DONNA_TASK_VISIBILITY_PULIC) will be added to
 *   #DonnaTaskManager (in charge of starting the task as soon as possible; they
 *   also run in a dedicated thread).
 *   Internal GUI tasks (%DONNA_TASK_VISIBILITY_INTERNAL_GUI) require to be run
 *   in the main thread, as they need to use GTK+ functions; Fast internal ones
 *   (%DONNA_TASK_VISIBILITY_INTERNAL_FAST) are guaranteed to be fast/not block
 *   (i.e. all in memory, no slow process, disk access, etc) and can be run in
 *   the current thread, even the main/GUI one.
 * - The #DonnaTask:priority is a writable property also to be used by the task
 *   manager, and that can be changed even while the task is running.
 * - donna_task_set_autostart() is also aimed at the task manager: if %FALSE,
 *   the task's initial state will be %DONNA_TASK_STOPPED (instead of
 *   %DONNA_TASK_WAITING) and the task manager will therefore not automatically
 *   start the task. It obviously has no effect on "internal" tasks.
 * - donna_task_set_desc() and donna_task_take_desc() both allow to set the
 *   task's description, to be shown by the task manager (and in error/debugging
 *   messages). The caller can also use donna_task_prefix_desc() to add a prefix
 *   to the description.
 *
 * Additionally, for the task to be duplicated (so that it could be restarted)
 * use donna_task_set_duplicator().
 *
 * A task might also have a pre-worker, set with donna_task_set_pre_worker(). In
 * that case, when #DonnaApp or the task manager want to start the task, because
 * donna_task_need_prerun() will return %TRUE they'll call donna_task_prerun()
 * instead, to have the pre-worker called. Once the pre-worker is complete, the
 * task will run as usual. Soo donna_task_set_pre_worker() for more.
 *
 *
 * The caller can use donna_task_set_timeout() and donna_task_set_callback() to
 * set the timeout and callback respectively. In both cases, the destroy
 * function will only be called if the timeout/callback wasn't called, else it
 * is its responsibility to free the associated memory.
 * Although one could run a task directly (i.e. in the current thread) using
 * donna_task_run(), it should always be done using donna_app_run_task()
 *
 * Once a task is started, if can be paused, resumed or cancelled using
 * donna_task_pause(), donna_task_resume() and donna_task_cancel() resp.
 *
 * It is also possible to cancel a task *before* it was started (using
 * donna_task_cancel() as well), i.e. when it was still stopped or waiting. In
 * that case, the worker will never be called, but the callback (if any) will.
 *
 *
 * The worker (i.e. the task's function) should regularly use
 * donna_task_is_cancelling() to see if the task has been cancelled. If the task
 * has been paused, the function won't return until it is resumed or cancelled.
 * If cancelled, the worker should then obviously abort its job and return as
 * soon as possible.
 *
 * It is also possible to use donna_task_get_fd() to get a file descriptor that
 * can be used to determine if the task has been paused/cancelled, i.e. if
 * donna_task_is_cancelling() should be called.
 *
 * While running, it can change the task's progress and/or status using
 * donna_task_update()
 * In case an error occurs, the error can be set by using either
 * donna_task_set_error() or donna_task_take_error()
 *
 * A return value might also be set. This is not intended for the end-user, but
 * the caller. For example, a task to get a #DonnaNode from a #DonnaProvider
 * will set the node as return value.
 * The return value can be set by donna_task_set_return_value(), or the worker
 * can directly access the task's return value using
 * donna_task_grab_return_value() and then donna_task_release_return_value()
 * when done. It is important to call both of those while only setting the value
 * in between, as the task remains locked during that time, and trying to do
 * anything else (even a call to donna_task_is_cancelling()) could lead to
 * deadlock.
 *
 * If you need to run another task from a task worker, and wait until it is done
 * to continue execution, you can use donna_task_wait_for_it() or
 * donna_task_get_wait_fd() to do so.
 */

struct _DonnaTaskPrivate
{
    /* task desc */
    gchar               *desc;
    /* task visibility */
    DonnaTaskVisibility  visibility;
    /* task priority */
    DonnaTaskPriority    priority;
    /* task status (current operation being done) */
    gchar               *status;
    /* task progress */
    gdouble              progress;
    /* pulse value if the progress cannot be known */
    gint                 pulse;
    /* task "public" state */
    DonnaTaskState       state;
    /* devices involved */
    GPtrArray           *devices;
    /* TaskUI for the task */
    DonnaTaskUi         *taskui;

    /* task functions */
    task_pre_fn          task_pre_fn;
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
    /* fd that can be used when blocking, see donna_task_get_wait_fd() */
    int                  fd_block;
    /* to hold the return value */
    GValue              *value;
    /* to hold the error */
    GError              *error;

    guint                task_pre_ran : 1;
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
    PROP_VISIBILITY,
    PROP_PRIORITY,
    PROP_STATUS,
    PROP_PROGRESS,
    PROP_PULSE,
    PROP_STATE,
    PROP_DEVICES,
    PROP_TASKUI,
    PROP_ERROR,
    PROP_RETURN_VALUE,

    NB_PROPS
};

static GParamSpec * donna_task_props[NB_PROPS] = { NULL, };

/* internal; used by provider-task.c */
const gchar * state_name (DonnaTaskState state);

static void donna_task_get_property (GObject        *object,
                                     guint           prop_id,
                                     GValue         *value,
                                     GParamSpec     *pspec);
static void donna_task_set_property (GObject        *object,
                                     guint           prop_id,
                                     const GValue   *value,
                                     GParamSpec     *pspec);
static void donna_task_finalize     (GObject        *object);

G_DEFINE_TYPE (DonnaTask, donna_task, G_TYPE_INITIALLY_UNOWNED)

static void
donna_task_class_init (DonnaTaskClass *klass)
{
    GObjectClass *o_class;

    o_class = G_OBJECT_CLASS (klass);
    o_class->get_property   = donna_task_get_property;
    o_class->set_property   = donna_task_set_property;
    o_class->finalize       = donna_task_finalize;

    /**
     * DonnaTask:desc:
     *
     * The task's description. It will be used by #DonnaTaskManager, and could
     * also be used in error and/or debugging messages.
     *
     * The caller of a *_task() function can use donna_task_prefix_desc() to
     * prefix the current description.
     */
    donna_task_props[PROP_DESC] =
        g_param_spec_string ("desc", "desc",
                "Description of the task",
                NULL,     /* default */
                G_PARAM_READWRITE);
    /**
     * DonnaTask:visibility:
     *
     * The task's visibility. Internal tasks (%DONNA_TASK_VISIBILITY_INTERNAL)
     * will be ran into the internal thread pool managed by #DonnaApp while
     * public tasks (%DONNA_TASK_VISIBILITY_PULIC) will be handled through the
     * #DonnaTaskManager (which manages its own thread pool).
     * Internal GUI tasks (%DONNA_TASK_VISIBILITY_INTERNAL_GUI) are required to
     * be run in the main thread as they'll use GTK functions and such.
     * Fast internal ones (%DONNA_TASK_VISIBILITY_INTERNAL_FAST) will be run in
     * the current thread, as they guarantee not to be slow/block.
     */
    donna_task_props[PROP_VISIBILITY] =
        g_param_spec_int ("visibility", "visibility",
                "Visibility of the task",
                DONNA_TASK_VISIBILITY_INTERNAL, /* minimum */
                DONNA_TASK_VISIBILITY_PULIC,    /* maximum */
                DONNA_TASK_VISIBILITY_INTERNAL, /* default */
                G_PARAM_READABLE);
    /**
     * DonnaTask:priority:
     *
     * The taks priority is used by #DonnaTaskManager to determine which task
     * must be run first. Must be a #DonnaTaskPriority
     * This doesn't not affect "internal" tasks, as they don't go through the
     * task manager.
     *
     * The priority can be changed even as the task is running.
     */
    donna_task_props[PROP_PRIORITY] =
        g_param_spec_int ("priority", "priority",
                "Priority of the task",
                DONNA_TASK_PRIORITY_LOW,    /* minimum */
                DONNA_TASK_PRIORITY_HIGH,   /* maximum */
                DONNA_TASK_PRIORITY_NORMAL, /* default */
                G_PARAM_READWRITE);
    /**
     * DonnaTask:status:
     *
     * The status is a (short) text describing the current operation done by the
     * task. It coudl e.g. in case of copying files, indicate which file is
     * being copied at the time.
     * Like #DonnaTask:progress this is mostly only used by #DonnaTaskManager
     */
    donna_task_props[PROP_STATUS] =
        g_param_spec_string ("status", "status",
                "Current status/operation of the task",
                NULL,   /* default */
                G_PARAM_READABLE);
    /**
     * DonnaTask:progress:
     *
     * The current progress of the task (from 0.0 to 1.0); or -1.0 if no
     * progress can be determined.
     * This is used by #DonnaTaskManager to show a progress bar for the task.
     */
    donna_task_props[PROP_PROGRESS] =
        g_param_spec_double ("progress", "progress",
                "Current progress of the task",
                -1.0,   /* minimum */
                1.0,    /* maximum */
                0.0,    /* default */
                G_PARAM_READABLE);
    /**
     * DonnaTask:pulse:
     *
     * The current value for the pulsating progress, when #DonnaTask:progress
     * cannot be used/known. This is meant to be used as property pulse of a
     * #GtkCellRendererProgress (e.g. #DonnaColumnTypeProgress)
     */
    donna_task_props[PROP_PULSE] =
        g_param_spec_int ("pulse", "pulse",
                "Current pulse value for progress",
                -1,         /* minimum */
                G_MAXINT,   /* maximum */
                0,          /* default */
                G_PARAM_READABLE);
    /**
     * DonnaTask:state:
     *
     * State of the task, one of #DonnaTaskState
     * Note that %DONNA_TASK_PAUSING and %DONNA_TASK_CANCELLING are "internal"
     * states that should only be briefly used, between the moment
     * donna_task_pause()/donna_task_cancel() was called, and the worker
     * acknowledged it. As such, no signal #GObject::notify will be triggered
     * for those states.
     *
     * You can easily determine if the task has been started, or had already
     * ran, by using %DONNA_TASK_PRE_RUN and %DONNA_TASK_POST_RUN resp.
     * You can use donna_task_get_state() to get the state easilly.
     */
    donna_task_props[PROP_STATE] =
        g_param_spec_int ("state", "state",
                "Current state of the task",
                DONNA_TASK_STATE_UNKNOWN,   /* minimum */
                DONNA_TASK_FAILED,          /* maximum */
                DONNA_TASK_WAITING,         /* default */
                G_PARAM_READABLE);
    /**
     * DonnaTask:devices:
     *
     * List of devices involved in the task. This is used by #DonnaTaskManager
     * (alongside #DonnaTask:priority) to determine which task to run, and if
     * multiple tasks can be run simultaneously.
     *
     * Returns: (element-type gchar *): List of strings
     */
    donna_task_props[PROP_DEVICES] =
        g_param_spec_boxed ("devices", "devices",
                "List of devices involved/used by the task",
                G_TYPE_PTR_ARRAY,
                G_PARAM_READABLE);
    /**
     * DonnaTask:taskui:
     *
     * The #DonnaTaskUi object used to provide additional user interface than
     * what the task manager can offer (state, progress, status, ways to
     * pause/resume/cancel).
     *
     * This is meant to be optional UI that isn't required, e.g. during a file
     * operation it could include currently processed filename, current/total
     * size, speed, ETA, etc
     * It could also provide info after the task completed, e.g. detailled error
     * messages/log.
     *
     * Though it could include options that affect the worker, any required user
     * interaction (e.g. ask for confirmation) is done by the worker, using task
     * helpers (as they need to block the thread while waiting for user input).
     *
     * Will be available in the #DonnaTaskManager as "Details"
     */
    donna_task_props[PROP_TASKUI] =
        g_param_spec_object ("taskui", "taskui",
                "TaskUI object to provide additional UI for the task",
                DONNA_TYPE_TASKUI,
                G_PARAM_READABLE);
    /**
     * DonnaTask:error:
     *
     * #GError is case an error occured. The #GError remains owned by the task,
     * you should not free it. It will be done automatically when the last
     * reference on the task is removed.
     *
     * Returns: (transfer none): Task-owned #GError
     */
    donna_task_props[PROP_ERROR] =
        /* pointer even though it's boxed, to avoid needing to copy/free */
        g_param_spec_pointer ("error", "error",
                "Error of this task",
                G_PARAM_READABLE);
    /**
     * DonnaTask:return-value:
     *
     * The task's return value. This is not intended for the end-user, but for
     * the caller of the task. For example, a task from a #DonnaProvider to get
     * a #DonnaNode for a location will set the node as retun value.
     *
     * Like #DonnaTask:error the #GValue is owned by the task and should not be
     * unset.
     *
     * Returns: (transfer none): Task-owned #GValue
     */
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
    priv->pulse     = 0;
    priv->state     = DONNA_TASK_WAITING;
    priv->devices   = NULL;
    priv->fd        = -1;
    priv->fd_block  = -1;
    priv->value     = NULL;
    priv->error     = NULL;
    g_mutex_init (&priv->mutex);
    g_cond_init (&priv->cond);
}

static gboolean
unref_taskui (DonnaTaskUi *tui)
{
    g_object_unref (tui);
    return G_SOURCE_REMOVE;
}

static void
donna_task_finalize (GObject *object)
{
    DonnaTaskPrivate *priv;

    priv = DONNA_TASK (object)->priv;
    DONNA_DEBUG (TASK, NULL,
            g_debug4 ("Finalizing task %p: %s",
                object, (priv->desc) ? priv->desc : "(no desc)"));
    g_free (priv->desc);
    if (priv->devices)
        g_ptr_array_unref (priv->devices);
    g_free (priv->status);
    if (priv->fd >= 0)
        close (priv->fd);
    if (priv->fd_block >= 0)
        close (priv->fd_block);
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

    if (priv->taskui)
        /* because the taskui, as its name implies, is probably dealing with UI
         * element, it might need to free/destroy them. If we just unref the
         * taskui here (i.e. possibly from a thread) all taskui would have to
         * make sure to do their calls to e.g. gtk_widget_destroy() back in the
         * main thread UI, all that from their finalize... i.e. it's a
         * complicated mess.
         * Much simpler: we'll do our unref of the taskui from the main thread,
         * thus they can do things directly. (This obviously also implies anyone
         * else having ref-ed the taskui shall only ever unref it from thread UI
         * as well, but that is indeed the idea.) */
        g_idle_add ((GSourceFunc) unref_taskui, priv->taskui);

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
        case PROP_VISIBILITY:
            g_value_set_int (value, priv->visibility);
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
        case PROP_PULSE:
            g_value_set_int (value, priv->pulse);
            break;
        case PROP_STATE:
            g_value_set_int (value, priv->state);
            break;
        case PROP_DEVICES:
            g_value_set_boxed (value, priv->devices);
            break;
        case PROP_TASKUI:
            g_value_set_object (value, priv->taskui);
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
            DONNA_DEBUG (TASK, NULL,
                    g_debug2 ("Task %p (%s): new description: %s",
                        object, (s) ? s : "(no desc)", priv->desc));
            g_free (s);
            break;
        case PROP_PRIORITY:
            priv->priority = g_value_get_int (value);
            DONNA_DEBUG (TASK, NULL,
                    g_debug2 ("Task %p (%s): set priority to %d",
                        object, (priv->desc) ? priv->desc : "(no desc)",
                        priv->priority));
            break;
        default:
            G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
            break;
    }
}

static inline void
unblock_fd (gint fd)
{
    guint64 one = 1;

again:
    if (write (fd, &one, sizeof (one)) == -1 && errno == EINTR)
        goto again;
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
    g_object_unref (data->obj);
    g_free (data);
    return FALSE;
}

/* this is only used from donna_task_is_cancelling() because we have a lock on
 * the task then, and would like to avoid deadlocks.
 * Any task running in the main/UI thread should never be pausable, since such
 * request come from the main thread (and those tasks are meant to be very fast
 * anyways), so anything else (INTERNAL/PUBLIC) will have its thread waiting,
 * while the main one can process the state change and then cancel/resume. */
static inline void
idle_notify_prop (DonnaTask *task, gint prop_id)
{
    struct notify_prop_data *data;

    data = g_new (struct notify_prop_data, 1);
    data->obj = g_object_ref (G_OBJECT (task));
    data->prop_id = prop_id;
    g_idle_add ((GSourceFunc) _notify_prop, data);
}

#define notify_prop(task, prop_id) \
    g_object_notify_by_pspec ((GObject *) task, donna_task_props[prop_id])

/**
 * donna_task_new:
 * @func: Function to be run as the task (aka the "worker")
 * @data: user-data sent as parameter to @func
 * @destroy: Function called if the task isn't run, to free @data
 *
 * This must be used when you need to create/return a task, so the requested
 * operation can be run in a separate thread.
 *
 * Returns: (transfer floating): New floating #DonnaTask
 */
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

    return task;
}

/**
 * donna_task_new_full:
 * @func: Function to be run as the task (aka the "worker")
 * @data: user-data sent as parameter to @func
 * @destroy: Function called if the task isn't run, to free @data
 * @taskui: #DonnaTaskUi to be provide extra GUI feedback
 * @devices: List of devices involved in the task
 * @visibility: #DonnaTaskVisibility of the task
 * @priority: #DonnaTaskPriority of the task
 * @autostart: Should #DonnaTaskManager start the task, or wait for manual start
 * @desc: Task's description
 *
 * Create a task while setting a few more properties than donna_task_new() See
 * description of those properties for more.
 *
 * Returns: (transfer floating): New floating #DonnaTask
 */
DonnaTask *
donna_task_new_full (task_fn             func,
                     gpointer            data,
                     GDestroyNotify      destroy,
                     DonnaTaskUi        *taskui,
                     GPtrArray          *devices,
                     DonnaTaskVisibility visibility,
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

    if (taskui)
        priv->taskui = g_object_ref_sink (taskui);
    priv->devices = g_ptr_array_ref (devices);
    priv->visibility = visibility;
    priv->priority = priority;
    if (!autostart)
        priv->state = DONNA_TASK_STOPPED;
    if (desc)
        priv->desc = g_strdup (desc);

    return task;
}

/**
 * donna_task_set_pre_worker:
 * @task: Task to set the pre-worker for
 * @func: Function to be run as pre-worker for @task
 *
 * Sets @func to be the pre-worker for the task, as described in
 * donna_task_prerun().
 *
 * The user-data used for @func will be the one set for the task worker, when
 * creating the task (via donna_task_new() or donna_task_new_full())
 *
 * Returns: %TRUE if the preworker of @task has been set
 */
gboolean
donna_task_set_pre_worker (DonnaTask          *task,
                           task_pre_fn         func)
{
    g_return_val_if_fail (DONNA_IS_TASK (task), FALSE);
    g_return_val_if_fail (func != NULL, FALSE);
    g_return_val_if_fail (task->priv->task_pre_fn == NULL, FALSE);

    task->priv->task_pre_fn = func;
    return TRUE;
}

/**
 * donna_task_set_worker:
 * @task: Task to set the worker for
 * @func: Function to be run as the task (aka the "worker")
 * @data: user-data sent as parameter to @func
 * @destroy: Function called if the task isn't run, to free @data
 *
 * This is only needed if you created a #DonnaTask using g_object_new(), which
 * you really have no reason to do when donna_task_new() exists.
 *
 * This function is only intended to be used for object extending
 * #DonnaTaskClass, so they can still set the worker of the task. It should not
 * be used to try to change a task's worker.
 *
 * Returns: Whether or not the worker was set to @task
 */
gboolean
donna_task_set_worker (DonnaTask          *task,
                       task_fn             func,
                       gpointer            data,
                       GDestroyNotify      destroy)
{
    DonnaTaskPrivate *priv;

    g_return_val_if_fail (DONNA_IS_TASK (task), FALSE);
    g_return_val_if_fail (func != NULL, FALSE);
    g_return_val_if_fail (task->priv->task_fn == NULL, FALSE);

    priv = task->priv;

    priv->task_fn = func;
    priv->task_data = data;
    priv->task_destroy = destroy;

    return TRUE;
}

/**
 * donna_task_set_taskui:
 * @task: Task to set the #DonnaTask:taskui for
 * @taskui: #DonnaTaskUi to assign to the task
 *
 * This should only be used by the task creator, after a donna_task_new()
 *
 * Returns: Whether or not @taskui was set to @task
 */
gboolean
donna_task_set_taskui (DonnaTask *task, DonnaTaskUi *taskui)
{
    g_return_val_if_fail (DONNA_IS_TASK (task), FALSE);
    g_return_val_if_fail (DONNA_IS_TASKUI (taskui), FALSE);
    g_return_val_if_fail (task->priv->taskui == NULL, FALSE);

    task->priv->taskui = g_object_ref_sink (taskui);
    notify_prop (task, PROP_TASKUI);
    return TRUE;
}

/**
 * donna_task_set_devices:
 * @task: Task to set #DonnaTask:devices for
 * @devices: (element-type gchar *): List of devices involved
 *
 * This should only be used by the task creator, after a donna_task_new()
 *
 * Returns: Whether or not @devices was set to @task
 */
gboolean
donna_task_set_devices (DonnaTask *task, GPtrArray *devices)
{
    g_return_val_if_fail (DONNA_IS_TASK (task), FALSE);
    g_return_val_if_fail (task->priv->devices == NULL, FALSE);

    task->priv->devices = g_ptr_array_ref (devices);
    notify_prop (task, PROP_DEVICES);
    return TRUE;
}

/**
 * donna_task_set_visibility:
 * @task: The task to set visibility of
 * @visibility: The new #DonnaTaskVisibility to set
 *
 * This should only be used by the task creator, after a donna_task_new()
 *
 * Returns: Whether or not @visibility was set to @task
 */
gboolean
donna_task_set_visibility (DonnaTask          *task,
                           DonnaTaskVisibility visibility)
{
    g_return_val_if_fail (DONNA_IS_TASK (task), FALSE);
    g_return_val_if_fail (visibility == DONNA_TASK_VISIBILITY_INTERNAL
            || visibility == DONNA_TASK_VISIBILITY_INTERNAL_FAST
            || visibility == DONNA_TASK_VISIBILITY_INTERNAL_GUI
            || visibility == DONNA_TASK_VISIBILITY_PULIC, FALSE);

    task->priv->visibility = visibility;
    DONNA_DEBUG (TASK, NULL,
            g_debug2 ("Task %p (%s): set visibility to %s",
                task,
                (task->priv->desc) ? task->priv->desc : "(no desc)",
                (visibility == DONNA_TASK_VISIBILITY_INTERNAL) ? "internal"
                : ((visibility == DONNA_TASK_VISIBILITY_INTERNAL_GUI) ? "internal GUI"
                    : ((visibility == DONNA_TASK_VISIBILITY_INTERNAL_FAST) ? "internal fast"
                            : "public"))));
    return TRUE;
}

/**
 * donna_task_set_duplicator:
 * @task: Task to set a duplicator for
 * @duplicate: Function to be used to duplicate @task
 * @data: user-data sent to @duplicate when triggered
 * @destroy: Function called to free @data (when @task is finalized)
 *
 * If @task should be able to de duplicated (i.e. restarted after if ran a first
 * time, regardless of its success), this will provide the function to create a
 * new task.
 * This will be called by donna_task_get_duplicate()
 *
 * Returns: Whether the duplicator was set to @task
 */
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

/**
 * donna_task_set_desc:
 * @task: Task to set description for
 * @desc: String to be duplicated into #DonnaTask:desc
 *
 * Set the task's description. This should be used by the task's creator; If you
 * want to add something to the description (as caller of a *_task() function),
 * see donna_task_prefix_desc()
 *
 * Returns: Whether a copy of @desc was set as new description of @task
 */
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
    DONNA_DEBUG (TASK, NULL,
            g_debug2 ("Task %p (%s): new description: %s",
                task, (s) ? s : "(no desc)", priv->desc));
    g_free (s);

    notify_prop (task, PROP_DESC);
    return TRUE;
}

/**
 * donna_task_take_desc:
 * @task: Task to set description for
 * @desc: String to be put into #DonnaTask:desc
 *
 * Set the task's description to @desc, which will be freed (using g_free())
 * when not needed anymore by the task. This should be used by the task's
 * creator; If you want to add something to the description (as caller of a
 * *_task() function), see donna_task_prefix_desc()
 *
 * Returns: Whether @desc was set as new description of @task
 */
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
    DONNA_DEBUG (TASK, NULL,
            g_debug2 ("Task %p (%s): new description: %s",
                task, (s) ? s : "(no desc)", priv->desc));
    g_free (s);

    notify_prop (task, PROP_DESC);
    return TRUE;
}

/**
 * donna_task_prefix_desc:
 * @task: Task to prefix the description of
 * @prefix: String to prefix into @task's #DonnaTask:desc
 *
 * Will add @prefix into the task's current description. This can be useful for
 * the caller of *_task() function, to add more precision to what the task is.
 *
 * Returns: Whether @prefix was added to @task's #DonnaTask:desc
 */
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
        DONNA_DEBUG (TASK, NULL,
                g_debug2 ("Task %p (%s): new description: %s",
                    task, (old) ? old : "(new desc)", priv->desc));
        g_free (old);
    }
    else
    {
        priv->desc = g_strdup (prefix);
        DONNA_DEBUG (TASK, NULL,
                g_debug2 ("Task %p (no desc): new description: %s",
                    task, priv->desc));
    }

    notify_prop (task, PROP_DESC);
    return TRUE;
}

/**
 * donna_task_set_callback:
 * @task: Task to set the callback for
 * @callback: Function to be called once the task has ran
 * @data: User-data for @callback
 * @destroy: Function called to free @data if the task doesn't run
 *
 * Sets the callback to be called in the main thread once a task has run. This
 * will be called regardless of the task's success.
 * If the task is finalized without having ran, @destroy will be called to free
 * the memory associated with @data, else it's the responsibility of @callback
 * to do it.
 *
 * This function should only be called once, as a task can only have one
 * callback.
 *
 * Returns: Whether or not @callback was set to @task
 */
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

/**
 * donna_task_set_timeout:
 * @task: Task to set the timeout for
 * @delay: Delay (in ms) after which @timeout must be called
 * @timeout: Function to be called after @delay ms (unless @task is done)
 * @data: User-data for @timeout
 * @destroy: Function called to free @data if the timeout isn't called
 *
 * Sets the timeout to be called in the main thread @delay ms after the task
 * has been prepared (i.e. donna_task_prepare() was called - which will usually
 * be called by donna_app_run_task() right away), unless it's already in
 * %DONNA_TASK_POST_RUN state. This can be useful (esp. for internal tasks) to
 * provide some feedback to the user.
 * If the task is finalized without having ran or it ended before @delay ms,
 * @destroy will be called to free the memory associated with @data, else it's
 * the responsibility of @timeout to do it.
 *
 * This function should only be called once, as a task can only have one
 * timeout.
 *
 * Returns: Whether or not @timeout was set to @task
 */
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

/**
 * donna_task_wait_for_it:
 * @task: Task to wait for execution to be over
 * @current_task: (allow-none): Current task (i.e. caller must be
 * @current_task's worker)
 * @error: (allow-none): Return location for a #GError
 *
 * This will return only if @task has already finished its execution, or wait
 * until it has before returning; See donna_task_get_wait_fd() for more.
 *
 * This is intended for task worker (i.e. function that run as task) that need
 * to run another task as part of its execution. In such cases, you might need
 * to wait for the task to be done, but you can't simply call donna_task_run()
 * directly, because the task might need to run in the main thread, or to be
 * managed by the task manager; And using a callback isn't practical/possible
 * since nothing else needs to happen in the current thread while waiting for
 * @task.
 *
 * If @current_task is specified, it will block until either @task is done, or
 * @current_task gets cancelled. It will also handle when it gets paused
 * automatically.
 *
 * This means when @current_task's fd is ready, @task will be paused and
 * donna_task_is_cancelling() called on @task. If %FALSE, @current_task will be
 * resumed and it starts over; If %TRUE, @task will be cancelled, and it will
 * block again on its wait fd until it gets ready before returning.
 *
 * It should be noted that this could lead to a case where, if @current_task is
 * paused, its state will switch to %DONNA_TASK_PAUSED while @task isn't
 * actually paused: donna_task_pause() was called, but its worker didn't process
 * it (yet).
 *
 * %FALSE will be returned in case of error, e.g. donna_task_get_wait_fd()
 * returned -1.
 * Note that it will return %TRUE regardless of the #DonnaTask:state of @task;
 * and that it could return %FALSE while @task is in %DONNA_TASK_POST_RUN state,
 * if again getting the waiting fd failed. (I.e. you should always check @task's
 * state afterwards, regardless of the return value).
 *
 * Don't forget to reference @task before starting the task & calling this
 * function. E.g. typically one would do:
 * <programlisting>
 * donna_app_run_task (app, g_object_ref (task));
 * donna_task_wait_for_it (task, current_task);
 * // check donna_task_get_state (task) ...
 * g_object_unref (task);
 * </programlisting>
 * An easier alternative is to call donna_app_run_task_and_wait() which does
 * exactly that, but will also switch @task visibility from
 * %DONNA_TASK_VISIBILITY_INTERNAL to %DONNA_TASK_VISIBILITY_INTERNAL_FAST in
 * order to make it run in the same thread as @current_task (You still need to
 * add a reference on @task before calling it, though).
 *
 * If you need to wait for the task to complete as well as some other event, you
 * can use donna_task_get_wait_fd() to get the file descriptor to use.
 *
 * Returns: %TRUE after waiting for @task to be in %DONNA_TASK_POST_RUN state,
 * else %FALSE
 */
gboolean
donna_task_wait_for_it (DonnaTask          *task,
                        DonnaTask          *current_task,
                        GError            **error)
{
    fd_set fd_set;
    int fd_wait;
    int fd_current;

    g_return_val_if_fail (DONNA_IS_TASK (task), FALSE);
    g_return_val_if_fail (!current_task || DONNA_IS_TASK (current_task), FALSE);

    fd_wait = donna_task_get_wait_fd (task);
    if (G_UNLIKELY (fd_wait == -1))
    {
        g_set_error (error, DONNA_TASK_ERROR,
                DONNA_TASK_ERROR_OTHER,
                "Failed to get wait fd from task");
        return FALSE;
    }

    if (current_task)
    {
        fd_current = donna_task_get_fd (current_task);
        if (G_UNLIKELY (fd_current == -1))
        {
            g_set_error (error, DONNA_TASK_ERROR,
                    DONNA_TASK_ERROR_OTHER,
                    "Failed to get fd from current task");
            return FALSE;
        }
    }
    else
        fd_current = -1;

    /* in the off chance we're in the main thrad (UI), we start a new main loop
     * to make sure (a) the UI isn't frozen, and (b) we don't deadlock.
     * Note that in this case, we ignore fd_current (if any), in order to avoid
     * deadlocks (since otherwise we would block/deadlock when calling
     * is_cancelling()).
     * This really shouldn't be called from thread UI anyways, and when it is it
     * should only be for rare/fast cases, e.g. loading a submenu. */
    if (g_main_context_is_owner (g_main_context_default ()))
    {
        GMainLoop *loop;

        loop = g_main_loop_new (NULL, TRUE);
        g_unix_fd_add (fd_wait, G_IO_IN,
                (GUnixFDSourceFunc) donna_on_fd_close_main_loop, loop);
        g_main_loop_run (loop);
    }
    else
        for (;;)
        {
            gint ret;

            FD_ZERO (&fd_set);
            FD_SET (fd_wait, &fd_set);
            if (fd_current >= 0)
                FD_SET (fd_current, &fd_set);

            ret = select (MAX (fd_wait, fd_current) + 1, &fd_set, NULL, NULL, 0);
            if (ret < 0)
            {
                int _errno = errno;

                if (errno == EINTR)
                    continue;

                g_set_error (error, DONNA_TASK_ERROR,
                        DONNA_TASK_ERROR_OTHER,
                        "Unexpected error in select() waiting for task: %s",
                        g_strerror (_errno));
                return FALSE;
            }

            if (FD_ISSET (fd_wait, &fd_set))
                break;
            else if (fd_current >= 0 && FD_ISSET (fd_current, &fd_set))
            {
                donna_task_pause (task);
                if (donna_task_is_cancelling (current_task))
                {
                    donna_task_cancel (task);
                    fd_current = -1;
                }
                else
                    donna_task_resume (task);
            }
        }

    return TRUE;
}

/**
 * donna_task_get_wait_fd:
 * @task: Task to get the file descriptor to wait for from
 *
 * This will return a file descriptor that you can use to wait until the task
 * finished its execution, more precisely until it gets to %DONNA_TASK_POST_RUN
 * state (i.e. it could get cancelled without running at all).
 *
 * This fd can be polled for reading. When data is available, it means the task
 * has reached %DONNA_TASK_POST_RUN state. There's no guarantee that the
 * callback has - or han't - been called at that time.
 *
 * Note that you should not try and read data from the fd, only test for it.
 *
 * If you don't need to use other file descriptors, and just want to wait for a
 * task to be done, even from another task, it might be easier to simply use
 * donna_task_wait_for_it()
 *
 * Returns: File descriptor to be polled for reading; or -1 on error
 */
int
donna_task_get_wait_fd (DonnaTask          *task)
{
    DonnaTaskPrivate *priv;

    g_return_val_if_fail (DONNA_IS_TASK (task), -1);
    priv = task->priv;

    if (priv->fd_block >= 0)
        return priv->fd_block;

    LOCK_TASK (task);

    if (priv->fd_block >= 0)
        goto done;

    priv->fd_block = eventfd (0, EFD_CLOEXEC | EFD_NONBLOCK);
    if (G_UNLIKELY (priv->fd_block == -1))
        goto done;

    if (priv->state & DONNA_TASK_POST_RUN)
        unblock_fd (priv->fd_block);

done:
    UNLOCK_TASK (task);
    return priv->fd_block;
}

/**
 * donna_task_has_taskui:
 * @task: Task to check if it has a #DonnaTaskUi or not
 *
 * This is a simple helper mostly meant for the task manager, to quickly
 * determine whether there's a TaskUI for the task or not (e.g. whether a
 * "Details" context menuitem should be sensitive or not)
 *
 * Returns: Whether the task has a #DonnaTaskUi or not
 */
gboolean
donna_task_has_taskui (DonnaTask          *task)
{
    g_return_val_if_fail (DONNA_IS_TASK (task), FALSE);
    return !!task->priv->taskui;
}

/**
 * donna_task_can_be_duplicated:
 * @task: Task to check if it can be duplicated
 *
 * Check whether @task can be duplicated or not, that is whether
 * donna_task_get_duplicate() can return a new #DonnaTask to perform the same
 * operation again, or not.
 *
 * Returns: Whether the task can be duplicated
 */
gboolean
donna_task_can_be_duplicated (DonnaTask *task)
{
    g_return_val_if_fail (DONNA_IS_TASK (task), FALSE);
    return task->priv->duplicate_fn != NULL;
}

/**
 * donna_task_get_duplicate:
 * @task: Task to duplicate
 * @error: (allow-none): Return location for a GError, or %NULL
 *
 * A task can only be run once, and there is no possibility or re-starting it
 * after it ran (regardless or why it stopped, e.g. failure, cancellation, etc)
 *
 * It might however be possible to duplicate a task, getting a new one ready to
 * perform the same operation (again).
 * If you just need to know whether or not it is possible to duplicate a task,
 * without actually creating a new one, use donna_task_can_be_duplicated()
 *
 * Returns: (transfer floating): A new floating #DonnaTask, or %NULL
 */
DonnaTask *
donna_task_get_duplicate (DonnaTask *task, GError **error)
{
    DonnaTaskPrivate *priv;

    g_return_val_if_fail (DONNA_IS_TASK (task), NULL);
    priv = task->priv;

    if (!priv->duplicate_fn)
        return NULL;

    return priv->duplicate_fn (priv->duplicate_data, error);
}

/**
 * donna_task_get_state:
 * @task: Task to get the state of
 *
 * Helper function to get the #DonnaTask:state property of @task
 *
 * Returns: Current #DonnaTaskState of @task
 */
DonnaTaskState
donna_task_get_state (DonnaTask *task)
{
    g_return_val_if_fail (DONNA_IS_TASK (task), DONNA_TASK_STATE_UNKNOWN);
    return task->priv->state;
}

/**
 * donna_task_get_desc:
 * @task: Task to get the description of
 *
 * Helper function to get the #DonnaTask:desc property of @task
 *
 * Returns: Current description of @task (if any)
 */
gchar *
donna_task_get_desc (DonnaTask *task)
{
    g_return_val_if_fail (DONNA_IS_TASK (task), NULL);
    return g_strdup (task->priv->desc);
}

/**
 * donna_task_get_error:
 * @task: Task to get the error from
 *
 * Helper function to get the #DonnaTask:error property of @task
 * The #GError returned remains owned by @task and should not be freed.
 *
 * Returns: (transfer none): Task-owned #GError
 */
const GError *
donna_task_get_error (DonnaTask *task)
{
    g_return_val_if_fail (DONNA_IS_TASK (task), NULL);
    return task->priv->error;
}

/**
 * donna_task_get_return_value:
 * @task: Task to get the return value from
 *
 * Helper function to get the #DonnaTask:return-value property of @task
 * The #GValue returned remains owned by @task and should not be unset.
 *
 * Returns: (transfer none): Task-owned #GValue
 */
const GValue *
donna_task_get_return_value (DonnaTask *task)
{
    g_return_val_if_fail (DONNA_IS_TASK (task), NULL);
    return task->priv->value;
}

const gchar *
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
    DONNA_DEBUG (TASK, NULL,
            g_debug2 ("Timeout for task %p: %s",
                task, (priv->desc) ? priv->desc : "(no desc)"));
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

    DONNA_DEBUG (TASK, NULL,
            g_debug2 ("Callback for task %p: %s",
                task, (priv->desc) ? priv->desc : "(no desc)"));
    priv->callback_fn (task, priv->timeout_ran, priv->callback_data);

    /* remove the reference we had on the task */
    g_object_unref (task);

    return FALSE;
}

/**
 * donna_task_prepare:
 * @task: Task to prepare
 *
 * This function will "prepare" @task, installing the timeout (if any). This is
 * useful for cases where a task is created, but might not ran instantly, e.g.
 * because the thread pool might be full.
 *
 * This ensures the timeout works as expected by installing it ASAP. This will
 * be called by app/task manager when they get a new task.
 * Note that donna_task_run() will still install the timeout if there was no
 * call to donna_task_prepare()
 */
void
donna_task_prepare (DonnaTask *task)
{
    DonnaTaskPrivate *priv;

    g_return_if_fail (DONNA_IS_TASK (task));
    priv = task->priv;

    LOCK_TASK (task);

    /* can only prepare from waiting */
    if (!(priv->state & DONNA_TASK_PRE_RUN))
    {
        DONNA_DEBUG (TASK, NULL,
                /* if task has pre-ran, this is likely from the second call to
                 * donna_app_run_task() to really start it this time, so it's
                 * all fine */
                if (!(!priv->task_pre_fn && priv->task_pre_ran))
                    g_debug ("Cannot prepare task %p, not in a pre-run state (%s): %s",
                        task,
                        state_name (priv->state),
                        (priv->desc) ? priv->desc : "(no desc)"));
        UNLOCK_TASK (task);
        return;
    }
    DONNA_DEBUG (TASK, NULL,
            g_debug ("Preparing task %p: %s",
                task, (priv->desc) ? priv->desc : "(no desc)"));

    /* install the timeout (will be triggered on main thread) */
    if (priv->timeout_fn)
        priv->timeout = g_timeout_add (priv->timeout_delay, timeout_cb, task);

    /* that's all for us */
    UNLOCK_TASK (task);
}

/**
 * donna_task_need_prerun:
 * @task: Task to check if it needs to prerun
 *
 * Returns whether or not @task needs to prerun.
 *
 * This is intended to be called by #DonnaApp or the task manager, to run a
 * task. It will return %TRUE is donna_task_prerun() should be called instead of
 * donna_task_run() so the pre-worker can do initialization work.
 *
 * Returns: %TRUE if donna_task_prerun() should be called to run the task,
 * %FALSE if donna_task_run() should be called
 */
gboolean
donna_task_need_prerun (DonnaTask          *task)
{
    g_return_val_if_fail (DONNA_IS_TASK (task), FALSE);
    return task->priv->task_pre_fn != NULL;
}

/**
 * donna_task_prerun:
 * @task: Task to pre-run
 * @run_task: Callback to run the task after pre-worker completed
 * @run_task_data: Data to give @run_task
 *
 * This is intended to be used by donna_app_run_task() or the task manager, when
 * donna_task_need_prerun() returned %TRUE, instead of donna_task_run()
 *
 * The pre-worker will be started, and once done (which might not happen
 * directly, but after sources have been attached in the main loop &
 * dispatched), @run_task will be called to actully run @task.
 *
 * At this point, donna_task_need_prerun() would return %FALSE so it is safe for
 * @run_task to be the very function calling donna_task_prerun() (e.g.
 * donna_app_run_task())
 *
 * Note that, much like donna_task_run(), this only works for tasks in
 * #DONNA_TASK_PRE_RUN state.
 * Also note that calling this function more than once will have any further
 * call simply be no-op; while calling it on a task without a pre-worker set
 * will have @run_task called instantly.
 */
void
donna_task_prerun (DonnaTask          *task,
                   task_run_fn         run_task,
                   gpointer            run_task_data)
{
    DonnaTaskPrivate *priv;

    g_return_if_fail (DONNA_IS_TASK (task));
    priv = task->priv;

    DONNA_DEBUG (TASK, NULL,
            g_debug ("Prerunning task %p: %s",
                task, (priv->desc) ? priv->desc : "(no desc)"));

    LOCK_TASK (task);

    /* can only run/start from waiting */
    if (!(priv->state & DONNA_TASK_PRE_RUN))
    {
        UNLOCK_TASK (task);
        DONNA_DEBUG (TASK, NULL,
                g_debug ("Abort pre-run of task %p, not in a pre-run state (%s): %s",
                    task,
                    state_name (priv->state),
                    (priv->desc) ? priv->desc : "(no desc)"));
        return;
    }

    if (G_UNLIKELY (!priv->task_pre_fn))
    {
        UNLOCK_TASK (task);
        DONNA_DEBUG (TASK, NULL,
                g_debug ("No pre-worker on task %p, running it: %s",
                    task,
                    (priv->desc) ? priv->desc : "(no desc)"));
        run_task (run_task_data, task);
        return;
    }

    /* ref task, in the off chance someone cancels/unrefs it during the
     * preworker call */
    g_object_ref (task);

    /* let's switch and release the lock */
    priv->state = DONNA_TASK_RUNNING;
    UNLOCK_TASK (task);

    notify_prop (task, PROP_STATE);

    /* start the pre-worker. It must call donna_task_set_preran() when
     * completed, which is where e.g. the unref will be done */
    priv->task_pre_fn (task, run_task, run_task_data, priv->task_data);
}

/**
 * donna_task_run:
 * @task: Task to run
 *
 * This function will run @task, taking care of installing the timeout if one
 * was set using donna_task_set_timeout() and calling the callback once it's
 * done, if one was set using donna_task_set_callback()
 * Both the timeout and the callback will be called in the main thread.
 *
 * Note that the task's worker will be run in the current thread, so you
 * probably shouldn't be using this function be use donna_app_run_task()
 * instead, so it is started in a new thread.
 */
void
donna_task_run (DonnaTask *task)
{
    DonnaTaskPrivate *priv;

    g_return_if_fail (DONNA_IS_TASK (task));

    priv = task->priv;
    DONNA_DEBUG (TASK, NULL,
            g_debug ("Starting task %p: %s",
                task, (priv->desc) ? priv->desc : "(no desc)"));

    LOCK_TASK (task);

    /* can only run/start from waiting */
    if (!(priv->state & DONNA_TASK_PRE_RUN)
            /* special case: if RUNNING but the task did pre-ran, i.e. the
             * pre-worker set the task RUNNING and now we're "really" running it
             * */
            && !(priv->state == DONNA_TASK_RUNNING && priv->task_pre_ran))
    {
        UNLOCK_TASK (task);
        DONNA_DEBUG (TASK, NULL,
                g_debug ("Ending task %p, not in a pre-run state (%s): %s",
                    task,
                    state_name (priv->state),
                    (priv->desc) ? priv->desc : "(no desc)"));
        return;
    }

    /* we take a ref on the task, to ensure it won't die while running */
    g_object_ref_sink (task);

    /* install the timeout (will be triggered on main thread). Note that this
     * can have been done by a call to donna_task_prepare() e.g. when the task
     * was added to the thread pool or soemthing, hence why we need to check no
     * timeout exists or has already ran. */
    if (priv->timeout_fn && priv->timeout == 0 && priv->timeout_ran == 0)
        priv->timeout = g_timeout_add (priv->timeout_delay, timeout_cb, task);

    /* let's switch and release the lock */
    priv->state = DONNA_TASK_RUNNING;
    /* in case donna_task_run() is called again on the task */
    priv->task_pre_ran = 0;
    UNLOCK_TASK (task);

    notify_prop (task, PROP_STATE);

    /* do the work & get new state */
    priv->state = priv->task_fn (task, priv->task_data);
    if (G_UNLIKELY (!(priv->state & DONNA_TASK_POST_RUN)))
    {
        g_critical ("Task '%s': worker didn't set a valid (POST_RUN) state: %s (%d)",
                (priv->desc) ? priv->desc : "(no desc)",
                state_name (priv->state),
                priv->state);
        if (priv->error)
            g_error_free (priv->error);
        priv->error = g_error_new (DONNA_TASK_ERROR, DONNA_TASK_ERROR_OTHER,
                "Task worker didn't set a valid (POST_RUN) state: %s",
                state_name (priv->state));
        priv->state = DONNA_TASK_FAILED;
    }

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

    /* someone blocked for us? */
    if (priv->fd_block >= 0)
        unblock_fd (priv->fd_block);

    /* we're done with the lock */
    UNLOCK_TASK (task);

    notify_prop (task, PROP_STATE);

    DONNA_DEBUG (TASK, NULL,
            g_debug ("Ending task %p (%s): %s",
                task,
                state_name (priv->state),
                (priv->desc) ? priv->desc : "(no desc)"));

    if (priv->callback_fn)
        /* trigger the callback in main thread -- our reference on task will
         * be removed after the callback has been triggered */
        g_main_context_invoke (NULL, callback_cb, task);
    else
        /* remove our reference on task */
        g_object_unref (task);
}

/**
 * donna_task_set_autostart:
 * @task: Task to set autostart on
 * @autostart: whether to enable or disable autostart
 *
 * When set, tasks have their #DonnaTask:state set to %DONNA_TASK_WAITING so the task
 * manager can start them as soon as possible; otherwise it's set to
 * %DONNA_TASK_STOPPED and a manual intervention is required.
 *
 * This will return %TRUE is @task's property #DonnaTask:state either was already set as
 * needed, or was just changed, which can only happen if the previous value
 * of #DonnaTask:state was either %DONNA_TASK_WAITING or %DONNA_TASK_STOPPED.
 * Else, %FALSE will be returned.
 *
 * Note that autostart is a feature that only applies to task handled by the
 * task manager, i.e. with a #DonnaTask:visibility of %DONNA_TASK_VISIBILITY_PULIC
 *
 * Returns: %TRUE if @task's #DonnaTask:state was (just/already) set to
 * %DONNA_TASK_WAITING or %DONNA_TASK_STOPPED (based on @autostart)
 */
gboolean
donna_task_set_autostart (DonnaTask          *task,
                          gboolean            autostart)
{
    DonnaTaskPrivate *priv;
    /* 2 == changed (need to notify); 1 == already there; 0 == fail */
    gint ret = 2;

    g_return_val_if_fail (DONNA_IS_TASK (task), FALSE);
    priv = task->priv;

    LOCK_TASK (task);

    if (autostart)
    {
        if (priv->state == DONNA_TASK_STOPPED)
            priv->state = DONNA_TASK_WAITING;
        else if (priv->state == DONNA_TASK_WAITING)
            ret = 1;
        else
            ret = 0;
    }
    else
    {
        if (priv->state == DONNA_TASK_WAITING)
            priv->state = DONNA_TASK_STOPPED;
        else if (priv->state == DONNA_TASK_STOPPED)
            ret = 1;
        else
            ret = 0;
    }

    UNLOCK_TASK (task);

    /* notify change of state */
    if (ret == 2)
        notify_prop (task, PROP_STATE);

    return ret > 0;
}

/**
 * donna_task_pause:
 * @task: Task to pause
 *
 * Send a request to the task's worker to pause. It might take a little while
 * before the request is taken into account (a worker might even ignore it
 * completely).
 * If you need to know if/when the task gets actually paused, watch the task's
 * property #DonnaTask:state, which will be set to %DONNA_TASK_PAUSED once the
 * worker is paused.
 *
 * This has no effect if the task isn't running (%DONNA_TASK_RUNNING)
 */
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
        unblock_fd (priv->fd);

    UNLOCK_TASK (task);

    /* we don't notify the change of state, because it's a "transitional" state,
     * and we should only notify once we changed to PAUSED */
}

/**
 * donna_task_resume:
 * @task: Task to resume
 *
 * Send a request to the task's worker to resume.
 *
 * This has no effect if the task isn't paused (or about to be, i.e.
 * donna_task_pause() was called but the task hasn't been paused yet)
 */
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

    /* wake up task_fn if we're resuming a paused task */
    if (state == DONNA_TASK_PAUSED)
        g_cond_signal (&priv->cond);

    UNLOCK_TASK (task);

    /* notify change of state */
    notify_prop (task, PROP_STATE);
}

/**
 * donna_task_cancel:
 * @task: Task to cancel
 *
 * Send a request to the task's worker to cancel. It might take a little while
 * before the request is taken into account (a worker might even ignore it
 * completely).
 * If you need to know if/when the task gets actually cancelled, watch the task's
 * property #DonnaTask:state Note that it might not be set to
 * %DONNA_TASK_CANCELLED (e.g.  if the task was already completed)
 *
 * It is also possible to cancel a task that hasn't yet started, i.e. still
 * %DONNA_TASK_WAITING or %DONNA_TASK_STOPPED.
 */
void
donna_task_cancel (DonnaTask *task)
{
    DonnaTaskPrivate *priv;
    DonnaTaskState state;

    g_return_if_fail (DONNA_IS_TASK (task));

    LOCK_TASK (task);

    priv = task->priv;
    state = priv->state;
    if (state & (DONNA_TASK_STOPPED | DONNA_TASK_WAITING))
    {
        g_object_ref_sink (task);
        priv->state = DONNA_TASK_CANCELLED;

        /* someone blocked for us? */
        if (priv->fd_block >= 0)
            unblock_fd (priv->fd_block);

        /* set the flag so we don't free callback_data, since we're going to
         * call the callback (even though the task didn't actually ran) */
        priv->task_ran = 1;
        /* we do need to free task_data however */
        if (priv->task_data && priv->task_destroy)
            priv->task_destroy (priv->task_data);

        UNLOCK_TASK (task);

        /* notify change of state */
        notify_prop (task, PROP_STATE);

        if (priv->callback_fn)
            /* trigger the callback in main thread -- our reference on task will
             * be removed after the callback has been triggered */
            g_main_context_invoke (NULL, callback_cb, task);
        else
            /* remove our reference on task */
            g_object_unref (task);

        return;
    }
    else if (!(state & (DONNA_TASK_RUNNING | DONNA_TASK_PAUSING | DONNA_TASK_PAUSED)))
    {
        /* Neither running, pausing or paused can't be paused.
         * Going from pausing to cancelling isn't a problem */
        UNLOCK_TASK (task);
        return;
    }

    priv->state = DONNA_TASK_CANCELLING;

    /* unblock the fd, so task_fn can see the change of state */
    if (priv->fd >= 0)
        unblock_fd (priv->fd);

    /* wake up task_fn if we're cancelling a paused task */
    if (state == DONNA_TASK_PAUSED)
        g_cond_signal (&priv->cond);

    UNLOCK_TASK (task);

    /* we don't notify the change of state, because it's a "transitional" state,
     * and we should only notify once the function returns */
}

/**
 * donna_task_set_preran:
 * @task: The #DonnaTask for which the pre-worker completed
 * @state: A %DONNA_TASK_POST_RUN state for the pre-worker
 * @run_task: Given to the pre-worker (from donna_task_prerun())
 * @run_task_data: Given to the pre-worker (from donna_task_prerun())
 *
 * This is intended to only be called by @task's pre-worker once it has
 * completed its initialization and the task worker can be called.
 *
 * @state must be either %DONNA_TASK_FAILED in case an error occured,
 * %DONNA_TASK_CANCELLED if @task was cancelled during the pre-worker's run, or
 * %DONNA_TASK_DONE when the task worker can now be run. Any other state will
 * result in a failure (as if %DONNA_TASK_FAILED was given).
 *
 * Unless @state is %DONNA_TASK_DONE, @task will automatically go to the
 * corresponding state, the task worker will never be called (though the
 * callback, if any, will be called).
 *
 * If %DONNA_TASK_DONE, @task's state will remain %DONNA_TASK_RUNNING and
 * @run_task will be called, so the task can be run as expected.
 * donna_task_need_prerun() can be called and will now return %FALSE, and
 * donna_task_run() will work (even though @task is %DONNA_TASK_RUNNING).
 */
void
donna_task_set_preran (DonnaTask          *task,
                       DonnaTaskState      state,
                       task_run_fn         run_task,
                       gpointer            run_task_data)
{
    DonnaTaskPrivate *priv;

    g_return_if_fail (DONNA_IS_TASK (task));
    priv = task->priv;

    LOCK_TASK (task);
    if (G_UNLIKELY (!priv->task_pre_fn))
    {
        g_critical ("Task %p (%s): set_preran() called while there's no pre-worker",
                task, priv->desc);
        UNLOCK_TASK (task);
        /* since this is an error, assume we don't have a ref on task */
        return;
    }
    else if (G_UNLIKELY (priv->state != DONNA_TASK_RUNNING))
    {
        g_critical ("Task %p (%s): set_preran() called while task state is %s",
                task, priv->desc, state_name (priv->state));
        UNLOCK_TASK (task);
        /* since this is an error, assume we don't have a ref on task */
        return;
    }

    priv->task_pre_fn = NULL;
    priv->task_pre_ran = 1;

    if (!(state & DONNA_TASK_POST_RUN))
    {
        priv->state = state = DONNA_TASK_FAILED;
        donna_task_set_error (task, DONNA_TASK_ERROR, DONNA_TASK_ERROR_OTHER,
                "Pre-worker failed to set a valid state (%d:%s)",
                state, state_name (state));
    }
    else if (state != DONNA_TASK_DONE)
        /* for FAILED the pre-worker is assumed to have set an error on task */
        priv->state = state;

    /* if not DONE, then the task shouldn't actually run, i.e. the worker
     * shouldn't be called since it was cancelled or failed already */
    if (state != DONNA_TASK_DONE)
    {
        g_object_ref_sink (task);

        /* someone blocked for us? */
        if (priv->fd_block >= 0)
            unblock_fd (priv->fd_block);

        /* set the flag so we don't free callback_data, since we're going to
         * call the callback (even though the task didn't actually ran) */
        priv->task_ran = 1;
        /* we do need to free task_data however */
        if (priv->task_data && priv->task_destroy)
            priv->task_destroy (priv->task_data);

        UNLOCK_TASK (task);

        /* notify change of state */
        notify_prop (task, PROP_STATE);

        if (priv->callback_fn)
            /* trigger the callback in main thread -- our reference on task will
             * be removed after the callback has been triggered */
            g_main_context_invoke (NULL, callback_cb, task);
        else
            /* remove our reference on task */
            g_object_unref (task);
    }
    else
    {
        UNLOCK_TASK (task);

        /* send our ref to run_task */
        run_task (run_task_data, task);
    }

    /* remove our reference taken in donna_task_prerun() */
    g_object_unref (task);
}

/**
 * donna_task_get_fd:
 * @task: The task
 *
 * This function must only be called by the task's worker. It can be used to
 * return a file descriptor. This fd can be polled for reading. When data is
 * available, it means the worker should call donna_task_is_cancelling() as the
 * task has been paused or cancelled.
 * The worker should not try and read data from the fd.
 *
 * If the task was only paused, and donna_task_is_cancelling() returned %FALSE
 * the fd will automatically have been reset, and be ready to be used again.
 * This function should not be called more than once.
 *
 * Returns: File descriptor to be polled for reading
 */
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

/**
 * donna_task_is_cancelling:
 * @task: Task to check
 *
 * This function should only be called by the task's worker, to determine
 * whether or not it has been cancelled. It will also handle pausing, only
 * returning once the task has been resumed or cancelled.
 *
 * A worker should either call this function regularly during its work, or can
 * use a file descriptor to determine when to call it. See donna_task_get_fd()
 *
 * Note that the final state of a task remains set by the returning value of the
 * worker, and e.g. if the task was completed it's ok to return %DONNA_TASK_DONE
 *
 * Returns: Whether @task was cancelled (and worker should abort) or not
 */
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
                DONNA_DEBUG (TASK, NULL,
                        g_debug ("Paused task %p: %s",
                            task, (priv->desc) ? priv->desc : "(no desc)"));
                /* notify change of state; we add this one in the main thread to
                 * avoid deadlock, since we currently own a lock on the task
                 * right now */
                idle_notify_prop (task, PROP_STATE);
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
                DONNA_DEBUG (TASK, NULL,
                        g_debug ("Unpaused task %p (%s): %s",
                            task,
                            (ret) ? "cancelled" : "resumed",
                            (priv->desc) ? priv->desc : "(no desc)"));
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

/**
 * donna_task_update:
 * @task: Task to update
 * @update: What will be updated
 * @progress: new value for #DonnaTask:progress
 * @status_fmt: format for a printf-like new value of #DonnaTask:status
 * @...: printf()-like arguments (if any)
 *
 * This function should only be called by the task's worker, to set a new
 * #DonnaTask:progress/#DonnaTask:pulse and/or #DonnaTask:status on @task. The
 * corresponding #GObject::notify signals will be triggered accordingly.
 *
 * If @update has #DONNA_TASK_UPDATE_PROGRESS set then @progress will be the new
 * value of #DonnaTask:progress; if #DONNA_TASK_UPDATE_PROGRESS_PULSE is set
 * then the current value of #DonnaTask:pulse will be incremented; if
 * #DONNA_TASK_UPDATE_STATUS is set then @status_fmt will be used to set the new
 * #DonnaTask:status
 *
 * If #DONNA_TASK_UPDATE_PROGRESS is set, #DonnaTask:pulse will not be updated.
 * Else, if #DONNA_TASK_UPDATE_PROGRESS_PULSE is set and @progress is less than
 * zero, #DonnaTask:pulse is set to -1, else it is incremented (or reset to 1 if
 * #G_MAXINT would have been reached) and #DonnaTask:progress is set to -1 (if
 * it isn't already).
 */
void
donna_task_update (DonnaTask        *task,
                   DonnaTaskUpdate   update,
                   gdouble           progress,
                   const gchar      *status_fmt,
                   ...)
{
    DonnaTaskPrivate *priv;

    g_return_if_fail (DONNA_IS_TASK (task));

    priv = task->priv;

    if (update & DONNA_TASK_UPDATE_PROGRESS)
    {
        priv->progress = progress;
        notify_prop (task, PROP_PROGRESS);
    }
    else if (update & DONNA_TASK_UPDATE_PROGRESS_PULSE)
    {
        if (progress < 0)
            priv->pulse = -1;
        else if (++priv->pulse == G_MAXINT)
            priv->pulse = 1;
        notify_prop (task, PROP_PULSE);

        if (priv->progress != -1)
        {
            priv->progress = -1;
            notify_prop (task, PROP_PROGRESS);
        }
    }

    if (update & DONNA_TASK_UPDATE_STATUS)
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

/**
 * donna_task_set_error:
 * @task: Task to set the error of
 * @domain: Domain of the error
 * @code: Code of the error
 * @format: printf()-like error message format
 * @...: printf()-like arguments
 *
 * This function should only be called by the task's worker.
 *
 * Sets the #DonnaTask:error property for @task
 */
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

/**
 * donna_task_take_error:
 * @task: Task to set the error of
 * @error: (transfer full): #GError to be used as #DonnaTask:error on @task
 *
 * This function should only be called by the task's worker.
 * @error will be used as new value for the #DonnaTask:error property. It will
 * be freed when @task is finalized.
 */
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

/**
 * donna_task_set_return_value:
 * @task: Task to set the return value of
 * @value: #GValue containing the value to be copied
 *
 * This function should only be called by the task's worker.
 * The #DonnaTask:return-value property of @task will be set to a copy of the
 * value held inside @value
 * If you want to directly set the value into @task's #GValue for
 * #DonnaTask:return-value, see donna_task_grab_return_value() and
 * donna_task_release_return_value()
 */
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

/**
 * donna_task_grab_return_value:
 * @task: Task to get the return value of
 *
 * This function should only be called by the task's worker.
 * It will return the #GValue used as to hold the #DonnaTask:return-value
 * property, so you can directly call g_value_init/g_value_set functions on it.
 *
 * It is important to only do this and call donna_task_release_return_value()
 * right after, a the task remains locked during that time, and attempting to do
 * anything else (e.g. donna_task_is_cancelling()) would result in e.g.
 * deadlock.
 *
 * Returns: (transfer none): Task-owned #GValue
 */
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

    DONNA_DEBUG (TASK, NULL,
            g_debug4 ("Grabbing return value of task %p: %s",
                task, (priv->desc) ? priv->desc : "(no desc)"));
    return priv->value;
}

/**
 * donna_task_release_return_value:
 * @task: Task to release the return value of
 *
 * This function should only be called by the task's worker, after having called
 * donna_task_grab_return_value() and set its value.
 */
void
donna_task_release_return_value (DonnaTask *task)
{
    g_return_if_fail (DONNA_IS_TASK (task));
    DONNA_DEBUG (TASK, NULL,
            g_debug4 ("Releasing return value of task %p: %s",
                task, (task->priv->desc) ? task->priv->desc : "(no desc)"));
    UNLOCK_TASK (task);
}
