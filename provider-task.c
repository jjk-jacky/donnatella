
#include <gtk/gtk.h>
#include "provider-task.h"
#include "provider.h"
#include "app.h"
#include "node.h"
#include "task.h"
#include "macros.h"
#include "debug.h"

enum
{
    PROP_0,

    PROP_APP,

    NB_PROPS
};

enum
{
    ST_STOPPED = 0,
    ST_WAITING,
    ST_RUNNING,
    ST_ON_HOLD,
    ST_PAUSED,
    ST_CANCELLED,
    ST_FAILED,
    ST_DONE,
};

typedef enum
{
    TM_BUSY_WRITE       = (1 << 0),
    TM_BUSY_READ        = (1 << 1),
    TM_BUSY_REFRESH     = (1 << 2),

    TM_REFRESH_PENDING  = (1 << 3),

    TM_IS_BUSY          = (TM_BUSY_WRITE | TM_BUSY_READ | TM_BUSY_REFRESH)
} TmState;

struct task
{
    DonnaTask   *task;
    guint        in_pool    : 1; /* did we add it in a pool */
    guint        own_pause  : 1; /* did we pause it */
};

struct _DonnaProviderTaskPrivate
{
    DonnaApp    *app;
    /* mutex & cond are used for the locking mechanism. For more on how it
     * works, see lock_manager() */
    GMutex       mutex;
    GCond        cond;
    /* TM_* */
    TmState      state;
    /* for write operations, i.e. adding/removing tasks */
    guint        queued;
    /* current readers owing TM_BUSY_READ */
    guint        readers;
    /* struct task [] */
    GArray      *tasks;
    /* thread pool */
    GThreadPool *pool;
};

static GParamSpec * provider_task_props[NB_PROPS] = { NULL, };

static void             provider_task_get_property  (GObject            *object,
                                                     guint               prop_id,
                                                     GValue             *value,
                                                     GParamSpec         *pspec);
static void             provider_task_set_property  (GObject            *object,
                                                     guint               prop_id,
                                                     const GValue       *value,
                                                     GParamSpec         *pspec);
static void             provider_task_finalize      (GObject            *object);

/* DonnaProvider */
static const gchar *    provider_task_get_domain    (DonnaProvider      *provider);
static DonnaProviderFlags provider_task_get_flags   (DonnaProvider      *provider);
/* DonnaProviderBase */
static DonnaTaskState   provider_task_new_node      (DonnaProviderBase  *provider,
                                                     DonnaTask          *task,
                                                     const gchar        *location);
static DonnaTaskState   provider_task_has_children  (DonnaProviderBase  *provider,
                                                     DonnaTask          *task,
                                                     DonnaNode          *node,
                                                     DonnaNodeType       node_types);
static DonnaTaskState   provider_task_get_children  (DonnaProviderBase  *provider,
                                                     DonnaTask          *task,
                                                     DonnaNode          *node,
                                                     DonnaNodeType       node_types);
static DonnaTaskState   provider_task_remove_node   (DonnaProviderBase  *provider,
                                                     DonnaTask          *task,
                                                     DonnaNode          *node);
static DonnaTaskState   provider_task_trigger_node  (DonnaProviderBase  *provider,
                                                     DonnaTask          *task,
                                                     DonnaNode          *node);

/* internal -- from task.c */
inline const gchar *state_name (DonnaTaskState state);

static void
provider_task_provider_init (DonnaProviderInterface *interface)
{
    interface->get_domain = provider_task_get_domain;
    interface->get_flags  = provider_task_get_flags;
}

static void
donna_provider_task_class_init (DonnaProviderTaskClass *klass)
{
    DonnaProviderBaseClass *pb_class;
    GObjectClass *o_class;

    pb_class = (DonnaProviderBaseClass *) klass;
    pb_class->new_node      = provider_task_new_node;
    pb_class->has_children  = provider_task_has_children;
    pb_class->get_children  = provider_task_get_children;
    pb_class->remove_node   = provider_task_remove_node;
    pb_class->trigger_node  = provider_task_trigger_node;

    o_class = (GObjectClass *) klass;
    o_class->get_property   = provider_task_get_property;
    o_class->set_property   = provider_task_set_property;
    o_class->finalize       = provider_task_finalize;

    provider_task_props[PROP_APP] = g_param_spec_object ("app", "app",
            "App object", DONNA_TYPE_APP,
            G_PARAM_READWRITE | G_PARAM_CONSTRUCT_ONLY);

    g_object_class_install_properties (o_class, NB_PROPS, provider_task_props);

    g_type_class_add_private (klass, sizeof (DonnaProviderTaskPrivate));
}

static void
free_task (struct task *t)
{
    g_object_unref (t->task);
}

static void
donna_provider_task_init (DonnaProviderTask *provider)
{
    DonnaProviderTaskPrivate *priv;

    priv = provider->priv = G_TYPE_INSTANCE_GET_PRIVATE (provider,
            DONNA_TYPE_PROVIDER_TASK,
            DonnaProviderTaskPrivate);
    g_mutex_init (&priv->mutex);
    g_cond_init (&priv->cond);
    /* 4: random. Probably there won't be more than 4 tasks at once */
    priv->tasks = g_array_sized_new (FALSE, FALSE, sizeof (struct task), 4);
    g_array_set_clear_func (priv->tasks, (GDestroyNotify) free_task);
    priv->pool = g_thread_pool_new ((GFunc) donna_task_run, NULL,
            -1, FALSE, NULL);
}

G_DEFINE_TYPE_WITH_CODE (DonnaProviderTask, donna_provider_task,
        DONNA_TYPE_PROVIDER_BASE,
        G_IMPLEMENT_INTERFACE (DONNA_TYPE_PROVIDER, provider_task_provider_init)
        )

static void
provider_task_get_property (GObject        *object,
                            guint           prop_id,
                            GValue         *value,
                            GParamSpec     *pspec)
{
    if (prop_id == PROP_APP)
        g_value_set_object (value, ((DonnaProviderTask *) object)->priv->app);
    else
        G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
}

static void
provider_task_set_property (GObject        *object,
                            guint           prop_id,
                            const GValue   *value,
                            GParamSpec     *pspec)
{
    if (prop_id == PROP_APP)
        ((DonnaProviderTask *) object)->priv->app = g_value_dup_object (value);
    else
        G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
}

static void
provider_task_finalize (GObject *object)
{
    DonnaProviderTaskPrivate *priv;

    priv = DONNA_PROVIDER_TASK (object)->priv;
    g_mutex_clear (&priv->mutex);
    g_cond_clear (&priv->cond);
    g_array_free (priv->tasks, TRUE);
    /* FIXME: stop all running tasks */
    g_thread_pool_free (priv->pool, TRUE, FALSE);

    /* chain up */
    G_OBJECT_CLASS (donna_provider_task_parent_class)->finalize (object);
}

static DonnaProviderFlags
provider_task_get_flags (DonnaProvider *provider)
{
    g_return_val_if_fail (DONNA_IS_PROVIDER_TASK (provider),
            DONNA_PROVIDER_FLAG_INVALID);
    return 0;
}

static const gchar *
provider_task_get_domain (DonnaProvider *provider)
{
    g_return_val_if_fail (DONNA_IS_PROVIDER_TASK (provider), NULL);
    return "task";
}

/* this works as a read/write lock, but with a special handling for refreshers.
 * - there can only be one writer at a time; For a writer to get the lock
 *   nothing else can have it. Nothing can take the lock when a writer has it.
 * - there can be multiple readers at a time; A reader can take the lock as long
 *   as there are no writer having it, or waiting for it.
 * - there can only be one refresher at a time; A refresher can take the lock if
 *   there are no writer having or waiting for it, and if no refresher has it.
 *   If a refresher is waiting for the lock and another refresher ask for it, it
 *   will instantly return FALSE, indicating there's already a refresh pending
 *   so this one can be ignored.
 */
static gboolean
lock_manager (DonnaProviderTask *tm, TmState state)
{
    DonnaProviderTaskPrivate *priv = tm->priv;

    g_mutex_lock (&priv->mutex);
    if (state == TM_BUSY_WRITE)
    {
        ++priv->queued;
        while (priv->state & TM_IS_BUSY)
            g_cond_wait (&priv->cond, &priv->mutex);
        --priv->queued;
    }
    else if (state == TM_BUSY_READ)
    {
        while (priv->state & TM_BUSY_WRITE || priv->queued > 0)
            g_cond_wait (&priv->cond, &priv->mutex);

        ++priv->readers;
    }
    else /* TM_BUSY_REFRESH */
    {
        if (state & TM_REFRESH_PENDING)
        {
            g_mutex_unlock (&priv->mutex);
            return FALSE;
        }

        priv->state |= TM_REFRESH_PENDING;
        while ((priv->state & (TM_BUSY_WRITE | TM_BUSY_REFRESH))
                || priv->queued > 0)
            g_cond_wait (&priv->cond, &priv->mutex);

        priv->state &= ~TM_REFRESH_PENDING;
    }
    priv->state |= state;
    g_mutex_unlock (&priv->mutex);
    return TRUE;
}

static void
unlock_manager (DonnaProviderTask *tm, TmState state)
{
    DonnaProviderTaskPrivate *priv = tm->priv;

    g_mutex_lock (&priv->mutex);
    if (state != TM_BUSY_READ || --priv->readers == 0)
        priv->state &= ~state;
    if (priv->queued > 0 || state == TM_BUSY_WRITE)
        /* make sure the waiting writer(s) get woken up; Also wake up all
         * readers when WRITE is gone */
        g_cond_broadcast (&priv->cond);
    else
        g_cond_signal (&priv->cond);
    g_mutex_unlock (&priv->mutex);
}

static gboolean
refresher (DonnaTask    *task,
           DonnaNode    *node,
           const gchar  *name)
{
    DonnaTask *t;
    gchar *location;
    GValue v = G_VALUE_INIT;

    location = donna_node_get_location (node);
    if (sscanf (location, "/%p", &t) != 1)
    {
        g_free (location);
        return FALSE;
    }
    g_free (location);

    if (streq (name, "name"))
    {
        gchar *desc;
        g_object_get (t, "desc", &desc, NULL);
        g_value_init (&v, G_TYPE_STRING);
        g_value_take_string (&v, desc);
    }
    else if (streq (name, "progress"))
    {
        gdouble progress;
        g_object_get (t, "progress", &progress, NULL);
        g_value_init (&v, G_TYPE_DOUBLE);
        g_value_set_double (&v, progress);
    }
    else if (streq (name, "status"))
    {
        gchar *status;
        g_object_get (t, "status", &status, NULL);
        g_value_init (&v, G_TYPE_STRING);
        g_value_take_string (&v, status);
    }
    else if (streq (name, "state"))
    {
        gint state;
        g_object_get (t, "state", &state, NULL);
        g_value_init (&v, G_TYPE_INT);
        g_value_set_int (&v, state);
    }
    else
        return FALSE;

    donna_node_set_property_value (node, name, &v);
    g_value_unset (&v);
    return TRUE;
}

static DonnaNode *
new_node (DonnaProviderBase *_provider,
          const gchar       *location,
          DonnaTask         *t,
          gboolean           has_lock,
          GError           **error)
{
    DonnaTaskManager *tm = (DonnaTaskManager *) _provider;
    DonnaProviderTaskPrivate *priv = tm->priv;
    DonnaNode *node;
    GValue v = G_VALUE_INIT;
    gchar *desc;
    gchar *status;
    gint state;
    gdouble progress;
    gchar buf[32];

    if (!location)
    {
        snprintf (buf, 32, "/%p", t);
        location = (const gchar *) buf;
    }

    g_object_get (t,
            "desc",     &desc,
            "status",   &status,
            "state",    &state,
            "progress", &progress,
            NULL);

    if (!desc)
        desc = g_strdup_printf ("<Task %p>", t);

    node = donna_node_new ((DonnaProvider *) _provider, location,
            DONNA_NODE_ITEM, NULL, refresher, NULL,
            desc, 0);
    g_free (desc);

    g_value_init (&v, G_TYPE_INT);
    switch (state)
    {
        case DONNA_TASK_STOPPED:
            g_value_set_int (&v, ST_STOPPED);
            break;
        case DONNA_TASK_WAITING:
            g_value_set_int (&v, ST_WAITING);
            break;
        case DONNA_TASK_RUNNING:
        case DONNA_TASK_PAUSING:
        case DONNA_TASK_CANCELLING:
        case DONNA_TASK_IN_RUN: /* silence warning */
            g_value_set_int (&v, ST_RUNNING);
            break;
        case DONNA_TASK_PAUSED:
            {
                guint i;

                if (!has_lock)
                    lock_manager (tm, TM_BUSY_READ);
                for (i = 0; i < priv->tasks->len; ++i)
                {
                    struct task *_t = &g_array_index (priv->tasks, struct task, i);
                    if (_t->task == t)
                    {
                        if (_t->own_pause)
                            g_value_set_int (&v, ST_ON_HOLD);
                        else
                            g_value_set_int (&v, ST_PAUSED);
                        break;
                    }
                }
                if (!has_lock)
                    unlock_manager (tm, TM_BUSY_READ);
                break;
            }
        case DONNA_TASK_CANCELLED:
            g_value_set_int (&v, ST_CANCELLED);
            break;
        case DONNA_TASK_FAILED:
            g_value_set_int (&v, ST_FAILED);
            break;
        case DONNA_TASK_DONE:
            g_value_set_int (&v, ST_DONE);
            break;
        case DONNA_TASK_STATE_UNKNOWN:
        case DONNA_TASK_PRE_RUN:
        case DONNA_TASK_POST_RUN:
            /* silence warning */
            break;
    }
    if (!donna_node_add_property (node, "state", G_TYPE_INT,
                &v, refresher, NULL, error))
    {
        g_prefix_error (error, "Provider 'task': Cannot create new node, "
                "failed to add property 'state': ");
        g_value_unset (&v);
        g_free (status);
        g_object_unref (node);
        return NULL;
    }
    g_value_unset (&v);

    g_value_init (&v, G_TYPE_DOUBLE);
    g_value_set_double (&v, progress);
    if (!donna_node_add_property (node, "progress", G_TYPE_DOUBLE,
                &v, refresher, NULL, error))
    {
        g_prefix_error (error, "Provider 'task': Cannot create new node, "
                "failed to add property 'progress': ");
        g_value_unset (&v);
        g_free (status);
        g_object_unref (node);
        return NULL;
    }
    g_value_unset (&v);

    g_value_init (&v, G_TYPE_STRING);
    g_value_take_string (&v, status);
    if (!donna_node_add_property (node, "status", G_TYPE_STRING,
                &v, refresher, NULL, error))
    {
        g_prefix_error (error, "Provider 'task': Cannot create new node, "
                "failed to add property 'status': ");
        g_value_unset (&v);
        g_object_unref (node);
        return NULL;
    }
    g_value_unset (&v);
    return node;
}

static DonnaTaskState
provider_task_new_node (DonnaProviderBase  *_provider,
                        DonnaTask          *task,
                        const gchar        *location)
{
    DonnaProviderTaskPrivate *priv = ((DonnaProviderTask *) _provider)->priv;
    DonnaProviderBaseClass *klass;
    DonnaNode *node;
    GValue *value;

    if (streq (location, "/"))
    {
        node = donna_node_new ((DonnaProvider *) _provider, location,
                DONNA_NODE_CONTAINER, NULL, (refresher_fn) gtk_true, NULL,
                "Task Manager", 0);
        klass = DONNA_PROVIDER_BASE_GET_CLASS (_provider);
        klass->lock_nodes (_provider);
    }
    else
    {
        DonnaTask *t;
        guint i;

        if (sscanf (location, "/%p", &t) != 1)
        {
            donna_task_set_error (task, DONNA_PROVIDER_ERROR,
                    DONNA_PROVIDER_ERROR_OTHER,
                    "Provider 'task': invalid location '%s'", location);
            return DONNA_TASK_FAILED;
        }

        klass = DONNA_PROVIDER_BASE_GET_CLASS (_provider);
        klass->lock_nodes (_provider);
        node = klass->get_cached_node (_provider, location);
        if (node)
            goto found;

        lock_manager ((DonnaProviderTask *) _provider, TM_BUSY_READ);
        for (i = 0; i < priv->tasks->len; ++i)
            if (t == g_array_index (priv->tasks, struct task, i).task)
            {
                GError *err = NULL;

                node = new_node (_provider, location, t, TRUE, &err);
                if (!node)
                {
                    donna_task_take_error (task, err);
                    klass->unlock_nodes (_provider);
                    unlock_manager ((DonnaProviderTask *) _provider, TM_BUSY_READ);
                    return DONNA_TASK_FAILED;
                }

                break;
            }
        unlock_manager ((DonnaProviderTask *) _provider, TM_BUSY_READ);

        if (i >= priv->tasks->len)
        {
            donna_task_set_error (task, DONNA_PROVIDER_ERROR,
                    DONNA_PROVIDER_ERROR_LOCATION_NOT_FOUND,
                    "Provider 'task': No task found for '%s'", location);
            klass->unlock_nodes (_provider);
            return DONNA_TASK_FAILED;
        }
    }

    /* adds another reference, for the caller/task */
    klass->add_node_to_cache (_provider, node);
found:
    klass->unlock_nodes (_provider);

    value = donna_task_grab_return_value (task);
    g_value_init (value, G_TYPE_OBJECT);
    g_value_take_object (value, node);
    donna_task_release_return_value (task);

    return DONNA_TASK_DONE;
}

static DonnaTaskState
provider_task_has_children (DonnaProviderBase  *_provider,
                            DonnaTask          *task,
                            DonnaNode          *node,
                            DonnaNodeType       node_types)
{
    DonnaProviderTaskPrivate *priv = ((DonnaProviderTask *) _provider)->priv;
    GValue *value;

    value = donna_task_grab_return_value (task);
    g_value_init (value, G_TYPE_BOOLEAN);
    /* DonnaProvider made sure node is a CONTAINER, and the only container in
     * task is the root, therefore we can do this: */
    g_value_set_boolean (value, (node_types & DONNA_NODE_ITEM)
            && priv->tasks->len > 0);
    donna_task_release_return_value (task);

    return DONNA_TASK_DONE;
}

static DonnaTaskState
provider_task_get_children (DonnaProviderBase  *_provider,
                            DonnaTask          *task,
                            DonnaNode          *node,
                            DonnaNodeType       node_types)
{
    DonnaProviderTaskPrivate *priv = ((DonnaProviderTask *) _provider)->priv;
    GValue *value;
    GPtrArray *arr;

    /* DonnaProvider made sure node is a CONTAINER, and the only container in
     * task in the root, hence: */
    if ((node_types & DONNA_NODE_ITEM) && priv->tasks->len > 0)
    {
        DonnaProviderBaseClass *klass;
        guint i;

        arr = g_ptr_array_new_full (priv->tasks->len, g_object_unref);
        klass = DONNA_PROVIDER_BASE_GET_CLASS (_provider);
        klass->lock_nodes (_provider);
        lock_manager ((DonnaProviderTask *) _provider, TM_BUSY_READ);
        for (i = 0; i < priv->tasks->len; ++i)
        {
            GError *err = NULL;
            struct task *t;
            DonnaNode *node;
            gchar location[32];

            t = &g_array_index (priv->tasks, struct task, i);
            snprintf (location, 32, "/%p", t->task);
            node = klass->get_cached_node (_provider, location);
            if (!node)
            {
                node = new_node (_provider, location, t->task, TRUE, &err);
                if (node)
                    /* adds another reference, for the caller/task */
                    klass->add_node_to_cache (_provider, node);
            }
            if (node)
                g_ptr_array_add (arr, node);
            else
            {
                g_warning ("Provider 'task': Failed to create children node: %s",
                        err->message);
                g_clear_error (&err);
            }
        }
        unlock_manager ((DonnaProviderTask *) _provider, TM_BUSY_READ);
        klass->unlock_nodes (_provider);
    }
    else
        arr = g_ptr_array_sized_new (0);

    value = donna_task_grab_return_value (task);
    g_value_init (value, G_TYPE_PTR_ARRAY);
    g_value_take_boxed (value, arr);
    donna_task_release_return_value (task);

    return DONNA_TASK_DONE;
}

static DonnaTaskState
provider_task_remove_node (DonnaProviderBase  *_provider,
                           DonnaTask          *task,
                           DonnaNode          *node)
{
    /* TODO */
    return DONNA_TASK_FAILED;
}

inline const gchar *
st_name (guint st)
{
    switch (st)
    {
        case ST_STOPPED:
            return "stopped";
        case ST_WAITING:
            return "waiting";
        case ST_RUNNING:
            return "running";
        case ST_ON_HOLD:
            return "on hold";
        case ST_PAUSED:
            return "paused";
        case ST_CANCELLED:
            return "cancelled";
        case ST_FAILED:
            return "failed";
        case ST_DONE:
            return "done";
        default:
            return "unknown";
    }
}

static DonnaTaskState
provider_task_trigger_node (DonnaProviderBase  *_provider,
                            DonnaTask          *task,
                            DonnaNode          *node)
{
    DonnaProviderTaskPrivate *priv = ((DonnaProviderTask *) _provider)->priv;
    guint state;
    DonnaNodeHasValue has;
    GValue value = G_VALUE_INIT;
    GError *err = NULL;

    donna_node_get (node, FALSE, "state", &has, &value, NULL);
    /* we know it's a node ITEM of ours, so the property should exist & be set */
    if (G_UNLIKELY (has != DONNA_NODE_VALUE_SET))
    {
        gchar *fl = donna_node_get_full_location (node);
        donna_task_set_error (task, DONNA_PROVIDER_ERROR,
                DONNA_PROVIDER_ERROR_OTHER,
                "Failed to get property 'state' from node '%s'", fl);
        g_free (fl);
        return DONNA_TASK_FAILED;
    }

    state = (guint) g_value_get_int (&value);
    g_value_unset (&value);

    if (state == ST_PAUSED || state == ST_STOPPED)
        state = DONNA_TASK_RUNNING;
    else if (state == ST_RUNNING || state == ST_ON_HOLD)
        state = DONNA_TASK_PAUSED;
    else if (state == ST_WAITING)
        state = DONNA_TASK_STOPPED;
    else
    {
        gchar *desc = donna_node_get_name (node);
        donna_task_set_error (task, DONNA_TASK_MANAGER_ERROR,
                DONNA_TASK_MANAGER_ERROR_INVALID_TASK_STATE,
                "Cannot toggle task '%s', incompatible current state (%s)",
                desc, st_name (state));
        g_free (desc);
        return DONNA_TASK_FAILED;
    }

    if (!donna_task_manager_set_state ((DonnaTaskManager *) _provider,
                node, state, &err))
    {
        donna_task_take_error (task, err);
        return DONNA_TASK_FAILED;
    }

    return DONNA_TASK_DONE;
}


/* Task Manager */

static gboolean
is_task_override (DonnaTask *t1, DonnaTask *t2)
{
    DonnaTaskState       t1_state;
    DonnaTaskPriority    t1_priority;
    DonnaTaskState       t2_state;
    DonnaTaskPriority    t2_priority;

    g_object_get (t1, "state", &t1_state, "priority", &t1_priority, NULL);
    g_object_get (t2, "state", &t2_state, "priority", &t2_priority, NULL);

    if (t1_priority > t2_priority)
        return TRUE;
    else if (t1_priority < t2_priority)
        return FALSE;

    if ((t1_state & DONNA_TASK_IN_RUN) && !(t2_state & DONNA_TASK_IN_RUN))
        return TRUE;
    else
        return FALSE;
}

static gboolean
is_task_conflicting (DonnaTask *task, GPtrArray *devices)
{
    GPtrArray *task_devices;
    guint td;

    g_object_get (task, "devices", &task_devices, NULL);

    for (td = 0; td < task_devices->len; ++td)
    {
        guint d;

        for (d = 0; d < devices->len; ++d)
            if (streq (task_devices->pdata[td], devices->pdata[d]))
            {
                g_ptr_array_unref (task_devices);
                return TRUE;
            }
    }

    g_ptr_array_unref (task_devices);
    return FALSE;
}

static DonnaTaskState
refresh_tm (DonnaTask *task, DonnaTaskManager *tm)
{
    DonnaProviderTaskPrivate *priv = tm->priv;
    GSList *active  = NULL;
    GSList *should  = NULL;
    GSList *l;
    guint i;
    gboolean no_devices = FALSE;
    gboolean did_pause  = FALSE;

    if (!lock_manager (tm, TM_BUSY_REFRESH))
        /* already a refresh pending */
        return DONNA_TASK_DONE;

    for (i = 0; i < priv->tasks->len; ++i)
    {
        struct task *t = &g_array_index (priv->tasks, struct task, i);
        DonnaTaskState state;
        GPtrArray *devices;
        gboolean do_continue;

        for (l = should; l; l = l->next)
            if (t == l->data)
                continue;

        state = donna_task_get_state (t->task);
        if (state == DONNA_TASK_STOPPED || (state & DONNA_TASK_POST_RUN)
                || (state == DONNA_TASK_PAUSED && !t->own_pause))
            continue;

        /* we get devices even if no_devices is TRUE, for in-memory tasks */
        g_object_get (t->task, "devices", &devices, NULL);
        if (!devices)
        {
            if (!no_devices)
            {
                no_devices = TRUE;
                /* only keep one task */
                if (should && should->next)
                {
                    g_slist_free (should->next);
                    should->next = NULL;
                }
            }
        }
        else if (devices->len == 1 && !devices->pdata[0])
        {
            if (!(state & DONNA_TASK_IN_RUN))
                g_thread_pool_push (priv->pool, t->task, NULL);
            g_ptr_array_unref (devices);
            continue;
        }
        else if (no_devices)
        {
            g_ptr_array_unref (devices);
            devices = NULL;
        }

        if ((state & DONNA_TASK_IN_RUN) && !g_slist_find (active, t))
            active = g_slist_prepend (active, t);

        if (!should)
        {
            should = g_slist_prepend (should, t);
            if (devices)
                g_ptr_array_unref (devices);
            continue;
        }

        do_continue = FALSE;
        for (l = should; l; )
        {
            struct task *_t = (struct task *) l->data;

            /* is there a conflict in devices? */
            if (no_devices || is_task_conflicting (_t->task, devices))
            {
                if (is_task_override (t->task, _t->task))
                {
                    /* t overrides, so we want to use it */
                    if (l != should && is_task_override (t->task,
                                ((struct task *) should->data)->task))
                    {
                        /* it becomes the first task */
                        l->data = should->data;
                        should->data = t;
                        do_continue = TRUE;
                        /* we keep processing should, then we'll reprocess all
                         * tasks, now that conflicts are different */
                        i = (guint) -1;
                    }
                    /* i.e. t not already been added, so we add it */
                    else if (!do_continue)
                    {
                        l->data = t;
                        do_continue = TRUE;
                        if (no_devices)
                            /* only one task in should, so we just move on */
                            break;
                        /* we keep processing should, then we'll reprocess all
                         * tasks, now that conflicts are different */
                        i = (guint) -1;
                    }
                    /* t already added, we just remove the conflicting task */
                    else
                    {
                        GSList *r = l;
                        l = l->next;
                        should = g_slist_delete_link (should, r);
                        continue;
                    }
                }
                /* t already added, so we remove the conflicting task */
                else if (do_continue)
                {
                    GSList *r = l;
                    l = l->next;
                    should = g_slist_delete_link (should, r);
                    continue;
                }
                else
                {
                    /* no override, just skip t */
                    do_continue = TRUE;
                    break;
                }
            }
            l = l->next;
        }
        if (devices)
            g_ptr_array_unref (devices);
        if (do_continue)
            continue;

        /* no conflict, we can add t */
        if (is_task_override (t->task, ((struct task *) should->data)->task))
            should = g_slist_prepend (should, t);
        else
            /* preserve should->data */
            should = g_slist_insert (should, t, 1);
    }

    if (!should) /* implies !active */
        goto done;

    if (active)
    {
        for (l = active; l; l = l->next)
            if (!g_slist_find (should, l->data))
            {
                struct task *t = (struct task *) l->data;
                DONNA_DEBUG (TASK_MANAGER,
                        gchar *d = donna_task_get_desc (t->task);
                        g_debug ("TaskManager: auto-pause task '%s' (%p)",
                            d, t->task);
                        g_free (d));
                donna_task_pause (t->task);
                t->own_pause = TRUE;
                did_pause = TRUE;
            }
    }

    if (!active || !did_pause)
        for (l = should; l; l = l->next)
        {
            struct task *t = (struct task *) l->data;
            DonnaTaskState state = donna_task_get_state (t->task);
            if (state == DONNA_TASK_PAUSED)
            {
                DONNA_DEBUG (TASK_MANAGER,
                        gchar *d = donna_task_get_desc (t->task);
                        g_debug ("TaskManager: auto-resume task '%s' (%p)",
                            d, t->task);
                        g_free (d));
                donna_task_resume (t->task);
                t->own_pause = FALSE;
            }
            /* avoid race condition where we already put it in our pool, but
             * it's still WAITING (i.e. about to go RUNNING, nothing to do) */
            else if (state == DONNA_TASK_WAITING && !t->in_pool)
            {
                DONNA_DEBUG (TASK_MANAGER,
                        gchar *d = donna_task_get_desc (t->task);
                        g_debug ("TaskManager: auto-start task '%s' (%p)",
                            d, t->task);
                        g_free (d));
                g_thread_pool_push (priv->pool, t->task, NULL);
                t->in_pool = TRUE;
            }
        }

    if (active)
        g_slist_free (active);
    g_slist_free (should);
done:
    unlock_manager (tm, TM_BUSY_REFRESH);
    return DONNA_TASK_DONE;
}

static void
notify_cb (DonnaTask *task, GParamSpec *pspec, DonnaTaskManager *tm)
{
    DonnaProviderTaskPrivate *priv = tm->priv;
    gboolean is_state;
    gboolean is_progress;
    gboolean check_refresh = TRUE;

    is_state = streq (pspec->name, "state");
    is_progress = !is_state && streq (pspec->name, "progress");
    if (is_state || is_progress || streq (pspec->name, "status"))
    {
        DonnaProviderBaseClass *klass;
        DonnaProviderBase *pb = (DonnaProviderBase *) tm;
        DonnaNode *node;
        gchar location[32];
        GValue v = G_VALUE_INIT;

        snprintf (location, 32, "/%p", task);
        klass = DONNA_PROVIDER_BASE_GET_CLASS (pb);
        klass->lock_nodes (pb);
        node = klass->get_cached_node (pb, location);
        klass->unlock_nodes (pb);

        if (!node)
            return;

        if (is_state)
        {
            g_value_init (&v, G_TYPE_INT);
            switch (donna_task_get_state (task))
            {
                case DONNA_TASK_STOPPED:
                    g_value_set_int (&v, ST_STOPPED);
                    break;
                case DONNA_TASK_WAITING:
                    g_value_set_int (&v, ST_WAITING);
                    break;
                case DONNA_TASK_RUNNING:
                case DONNA_TASK_PAUSING:
                case DONNA_TASK_CANCELLING:
                case DONNA_TASK_IN_RUN: /* silence warning */
                    g_value_set_int (&v, ST_RUNNING);
                    break;
                case DONNA_TASK_PAUSED:
                    {
                        guint i;

                        lock_manager (tm, TM_BUSY_READ);
                        for (i = 0; i < priv->tasks->len; ++i)
                        {
                            struct task *t = &g_array_index (priv->tasks, struct task, i);
                            if (t->task == task)
                            {
                                if (t->own_pause)
                                    g_value_set_int (&v, ST_ON_HOLD);
                                else
                                    g_value_set_int (&v, ST_PAUSED);
                                break;
                            }
                        }
                        unlock_manager (tm, TM_BUSY_READ);
                        break;
                    }
                case DONNA_TASK_CANCELLED:
                    g_value_set_int (&v, ST_CANCELLED);
                    break;
                case DONNA_TASK_FAILED:
                    g_value_set_int (&v, ST_FAILED);
                    break;
                case DONNA_TASK_DONE:
                    g_value_set_int (&v, ST_DONE);
                    break;
                case DONNA_TASK_STATE_UNKNOWN:
                case DONNA_TASK_PRE_RUN:
                case DONNA_TASK_POST_RUN:
                    /* silence warning */
                    break;
            }
        }
        else if (is_progress)
        {
            gdouble progress;

            g_object_get (task, "progress", &progress, NULL);
            g_value_init (&v, G_TYPE_DOUBLE);
            g_value_set_double (&v, progress);
        }
        else
        {
            gchar *status;

            g_object_get (task, "status", &status, NULL);
            g_value_init (&v, G_TYPE_STRING);
            g_value_take_string (&v, status);
        }
        donna_node_set_property_value (node, pspec->name, &v);
        g_value_unset (&v);
        g_object_unref (node);

        check_refresh = is_state;
    }

    if (check_refresh && (is_state || streq (pspec->name, "priority")
            || streq (pspec->name, "devices")))
    {
        DonnaTask *t;

        t = donna_task_new ((task_fn) refresh_tm, tm, NULL);
        donna_app_run_task (priv->app, t);
    }
}

gboolean
donna_task_manager_add_task (DonnaTaskManager       *tm,
                             DonnaTask              *task,
                             GError                **error)
{
    DonnaProviderTaskPrivate *priv;
    DonnaProviderBaseClass *klass;
    DonnaProviderBase *pb = (DonnaProviderBase *) tm;
    DonnaTaskVisibility visibility;
    struct task t;
    DonnaNode *node;

    g_return_val_if_fail (DONNA_IS_TASK_MANAGER (tm), FALSE);
    priv = tm->priv;

    g_object_get (task, "visibility", &visibility, NULL);
    if (visibility != DONNA_TASK_VISIBILITY_PULIC)
    {
        g_set_error (error, DONNA_TASK_MANAGER_ERROR,
                DONNA_TASK_MANAGER_ERROR_INVALID_TASK_VISIBILITY,
                "Only public task can be added to the task manager");
        return FALSE;
    }

    DONNA_DEBUG (TASK_MANAGER,
            gchar *d = donna_task_get_desc (task);
            g_debug ("TaskManager: add task '%s' (%p)", d, task);
            g_free (d));

    t.task = g_object_ref_sink (task);
    t.in_pool = t.own_pause = FALSE;
    lock_manager (tm, TM_BUSY_WRITE);
    g_array_append_val (priv->tasks, t);
    unlock_manager (tm, TM_BUSY_WRITE);

    g_signal_connect (task, "notify", (GCallback) notify_cb, tm);

    donna_app_run_task (priv->app,
            donna_task_new ((task_fn) refresh_tm, tm, NULL));

    /* we should signal a new child? */
    klass = DONNA_PROVIDER_BASE_GET_CLASS (pb);
    klass->lock_nodes (pb);
    node = klass->get_cached_node (pb, "/");

    if (node)
    {
        GError *err = NULL;
        DonnaNode *child;

        child = new_node (pb, NULL, task, FALSE, &err);
        if (G_LIKELY (child))
        {
            klass->add_node_to_cache (pb, child);
            klass->unlock_nodes (pb);
            donna_provider_node_new_child ((DonnaProvider *) tm, node, child);
            g_object_unref (child);
        }
        else
        {
            g_warning ("Provider 'task': Failed to create node for new task: %s",
                    err->message);
            g_clear_error (&err);
            klass->unlock_nodes (pb);
        }
        g_object_unref (node);
    }
    else
        klass->unlock_nodes (pb);

    return TRUE;
}

gboolean
donna_task_manager_set_state (DonnaTaskManager  *tm,
                              DonnaNode         *node,
                              DonnaTaskState     state,
                              GError           **error)
{
    DonnaProviderTaskPrivate *priv;
    DonnaTask *task;
    DonnaTaskState cur_state;
    gchar *location;
    gboolean ret = TRUE;

    g_return_val_if_fail (DONNA_IS_TASK_MANAGER (tm), FALSE);
    priv = tm->priv;

    if (donna_node_peek_provider (node) != (DonnaProvider *) tm
            /* not an item == a container == root/task manager */
            || donna_node_get_node_type (node) != DONNA_NODE_ITEM)
    {
        gchar *fl = donna_node_get_full_location (node);
        g_set_error (error, DONNA_TASK_MANAGER_ERROR,
                DONNA_TASK_MANAGER_ERROR_OTHER,
                "Cannot set task state, node '%s' isn't a task", fl);
        g_free (fl);
        return FALSE;
    }

    location = donna_node_get_location (node);
    if (sscanf (location, "/%p", &task) != 1)
    {
        g_set_error (error, DONNA_TASK_MANAGER_ERROR,
                DONNA_TASK_MANAGER_ERROR_OTHER,
                "Failed to get task from node 'task:%s'", location);
        g_free (location);
        return FALSE;
    }
    g_free (location);

    cur_state = donna_task_get_state (task);

    DONNA_DEBUG (TASK_MANAGER,
            gchar *d = donna_task_get_desc (task);
            g_debug ("TaskManager: switch task '%s' (%p) from %s to %s",
                d, task, state_name (cur_state), state_name (state));
            g_free (d));

    switch (state)
    {
        case DONNA_TASK_RUNNING:
            if (cur_state == DONNA_TASK_PAUSED)
            {
                guint i;

                /* if we didn't own the pause (i.e. it was a manual one) then we
                 * take ownership (make it "on hold") & trigger a refresh. This
                 * might start the task or not, based or other tasks in the
                 * manager.  If we already own the pause, nothing to do. */

                lock_manager (tm, TM_BUSY_READ);
                for (i = 0; i < priv->tasks->len; ++i)
                {
                    struct task *t = &g_array_index (priv->tasks, struct task, i);
                    if (t->task == task)
                    {
                        if (!t->own_pause)
                        {
                            GValue v = G_VALUE_INIT;

                            t->own_pause = TRUE;

                            g_value_init (&v, G_TYPE_INT);
                            g_value_set_int (&v, ST_ON_HOLD);
                            donna_node_set_property_value (node, "state", &v);
                            g_value_unset (&v);

                            donna_app_run_task (priv->app,
                                    donna_task_new ((task_fn) refresh_tm, tm, NULL));
                        }
                        break;
                    }
                }
                unlock_manager (tm, TM_BUSY_READ);
            }
            else if (cur_state == DONNA_TASK_PAUSING)
                /* try to override the pausing we a resume */
                donna_task_resume (task);
            else if (cur_state == DONNA_TASK_STOPPED)
                /* make it WAITING, which will trigger a refresh. It may or may
                 * not start the task, again, based on other tasks in the TM */
                donna_task_set_autostart (task, TRUE);
            else if (cur_state != DONNA_TASK_RUNNING
                    && cur_state != DONNA_TASK_WAITING)
                ret = FALSE;
            break;

        case DONNA_TASK_PAUSING:
        case DONNA_TASK_PAUSED:
            if (cur_state == DONNA_TASK_RUNNING)
                donna_task_pause (task);
            else if (cur_state == DONNA_TASK_PAUSED)
            {
                guint i;

                /* if we owned the pause, we shall release it, so it becomes a
                 * manual pause again (and not "on hold") */

                lock_manager (tm, TM_BUSY_READ);
                for (i = 0; i < priv->tasks->len; ++i)
                {
                    struct task *t = &g_array_index (priv->tasks, struct task, i);
                    if (t->task == task)
                    {
                        if (t->own_pause)
                        {
                            GValue v = G_VALUE_INIT;

                            t->own_pause = FALSE;

                            g_value_init (&v, G_TYPE_INT);
                            g_value_set_int (&v, ST_PAUSED);
                            donna_node_set_property_value (node, "state", &v);
                            g_value_unset (&v);

                            donna_app_run_task (priv->app,
                                    donna_task_new ((task_fn) refresh_tm, tm, NULL));
                        }
                        break;
                    }
                }
                unlock_manager (tm, TM_BUSY_READ);
            }
            else if (cur_state != DONNA_TASK_PAUSING)
                ret = FALSE;
            break;

        case DONNA_TASK_CANCELLING:
        case DONNA_TASK_CANCELLED:
            if (cur_state == DONNA_TASK_RUNNING
                    || cur_state == DONNA_TASK_PAUSED
                    || cur_state == DONNA_TASK_PAUSING)
                donna_task_cancel (task);
            else if (cur_state != DONNA_TASK_CANCELLED
                    && cur_state != DONNA_TASK_CANCELLING)
                ret = FALSE;
            break;

        case DONNA_TASK_STOPPED:
            if (cur_state == DONNA_TASK_WAITING)
                donna_task_set_autostart (task, FALSE);
            else if (cur_state != DONNA_TASK_STOPPED)
                ret = FALSE;
            break;

        case DONNA_TASK_WAITING:
            if (cur_state == DONNA_TASK_STOPPED)
                donna_task_set_autostart (task, TRUE);
            else if (cur_state != DONNA_TASK_WAITING)
                ret = FALSE;
            break;

        case DONNA_TASK_DONE:
        case DONNA_TASK_FAILED:
        case DONNA_TASK_STATE_UNKNOWN:
        default:
            {
                gchar *desc = donna_task_get_desc (task);
                g_set_error (error, DONNA_TASK_MANAGER_ERROR,
                        DONNA_TASK_MANAGER_ERROR_OTHER,
                        "Cannot set state of task '%s', invalid state (%d)",
                        desc, state);
                g_free (desc);
                return FALSE;
            }

    }

    if (!ret)
    {
        gchar *desc = donna_task_get_desc (task);
        g_set_error (error, DONNA_TASK_MANAGER_ERROR,
                DONNA_TASK_MANAGER_ERROR_INVALID_TASK_STATE,
                "Cannot set state of task '%s' to '%s', incompatible current state (%s)",
                desc, state_name (state), state_name (cur_state));
        g_free (desc);
    }

    return ret;
}