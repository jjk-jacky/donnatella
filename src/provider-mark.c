
#include "config.h"

#include <gtk/gtk.h>
#include "provider-mark.h"
#include "provider-command.h"
#include "provider.h"
#include "node.h"
#include "app.h"
#include "command.h"
#include "macros.h"
#include "debug.h"

enum mark_type
{
    MARK_STANDARD,
    MARK_DYNAMIC
};


struct mark
{
    gchar *location;
    gchar *name;
    enum mark_type type;
    gchar *value;
};

struct _DonnaProviderMarkPrivate
{
    GMutex mutex;
    GHashTable *marks;
};


/* GObject */
static void             provider_mark_contructed        (GObject            *object);
static void             provider_mark_finalize          (GObject            *object);
/* DonnaProvider */
static const gchar *    provider_mark_get_domain        (DonnaProvider      *provider);
static DonnaProviderFlags provider_mark_get_flags       (DonnaProvider      *provider);
static gchar *          provider_mark_get_context_alias_new_nodes (
                                                         DonnaProvider      *provider,
                                                         const gchar        *extra,
                                                         DonnaNode          *location,
                                                         const gchar        *prefix,
                                                         GError            **error);
static gboolean         provider_mark_get_context_item_info (
                                                         DonnaProvider      *provider,
                                                         const gchar        *item,
                                                         const gchar        *extra,
                                                         DonnaContextReference reference,
                                                         DonnaNode          *node_ref,
                                                         get_sel_fn          get_sel,
                                                         gpointer            get_sel_data,
                                                         DonnaContextInfo   *info,
                                                         GError            **error);
/* DonnaProviderBase */
static DonnaTaskState   provider_mark_new_node          (DonnaProviderBase  *provider,
                                                         DonnaTask          *task,
                                                         const gchar        *location);
static DonnaTaskState   provider_mark_has_children      (DonnaProviderBase  *provider,
                                                         DonnaTask          *task,
                                                         DonnaNode          *node,
                                                         DonnaNodeType       node_types);
static DonnaTaskState   provider_mark_get_children      (DonnaProviderBase  *provider,
                                                         DonnaTask          *task,
                                                         DonnaNode          *node,
                                                         DonnaNodeType       node_types);
static DonnaTaskState   provider_mark_trigger_node      (DonnaProviderBase  *provider,
                                                         DonnaTask          *task,
                                                         DonnaNode          *node);
static DonnaTaskState   provider_mark_new_child         (DonnaProviderBase  *provider,
                                                         DonnaTask          *task,
                                                         DonnaNode          *parent,
                                                         DonnaNodeType       type,
                                                         const gchar        *name);
static DonnaTaskState   provider_mark_remove_from       (DonnaProviderBase  *provider,
                                                         DonnaTask          *task,
                                                         GPtrArray          *nodes,
                                                         DonnaNode          *source);

static void
provider_mark_provider_init (DonnaProviderInterface *interface)
{
    interface->get_domain                   = provider_mark_get_domain;
    interface->get_flags                    = provider_mark_get_flags;
    interface->get_context_alias_new_nodes  = provider_mark_get_context_alias_new_nodes;
    interface->get_context_item_info        = provider_mark_get_context_item_info;
}

G_DEFINE_TYPE_WITH_CODE (DonnaProviderMark, donna_provider_mark,
        DONNA_TYPE_PROVIDER_BASE,
        G_IMPLEMENT_INTERFACE (DONNA_TYPE_PROVIDER, provider_mark_provider_init)
        )

static void
donna_provider_mark_class_init (DonnaProviderMarkClass *klass)
{
    DonnaProviderBaseClass *pb_class;
    GObjectClass *o_class;

    o_class = (GObjectClass *) klass;
    o_class->constructed        = provider_mark_contructed;
    o_class->finalize           = provider_mark_finalize;

    pb_class = (DonnaProviderBaseClass *) klass;

    pb_class->task_visibility.new_node      = DONNA_TASK_VISIBILITY_INTERNAL_FAST;
    pb_class->task_visibility.has_children  = DONNA_TASK_VISIBILITY_INTERNAL_FAST;
    pb_class->task_visibility.get_children  = DONNA_TASK_VISIBILITY_INTERNAL_FAST;
    pb_class->task_visibility.trigger_node  = DONNA_TASK_VISIBILITY_INTERNAL_GUI;
    pb_class->task_visibility.new_child     = DONNA_TASK_VISIBILITY_INTERNAL_FAST;
    pb_class->task_visibility.remove_from   = DONNA_TASK_VISIBILITY_INTERNAL_FAST;

    pb_class->new_node          = provider_mark_new_node;
    pb_class->has_children      = provider_mark_has_children;
    pb_class->get_children      = provider_mark_get_children;
    pb_class->trigger_node      = provider_mark_trigger_node;
    pb_class->new_child         = provider_mark_new_child;
    pb_class->remove_from       = provider_mark_remove_from;

    g_type_class_add_private (klass, sizeof (DonnaProviderMarkPrivate));
}

static void
free_mark (struct mark *mark)
{
    g_free (mark->location);
    g_free (mark->name);
    g_free (mark->value);
    g_free (mark);
}

static void
donna_provider_mark_init (DonnaProviderMark *provider)
{
    DonnaProviderMarkPrivate *priv;

    priv = provider->priv = G_TYPE_INSTANCE_GET_PRIVATE (provider,
            DONNA_TYPE_PROVIDER_MARK,
            DonnaProviderMarkPrivate);
    g_mutex_init (&priv->mutex);
    priv->marks = g_hash_table_new_full (g_str_hash, g_str_equal,
            NULL, (GDestroyNotify) free_mark);
}

static void
provider_mark_finalize (GObject *object)
{
    DonnaProviderMarkPrivate *priv = ((DonnaProviderMark *) object)->priv;

    g_mutex_clear (&priv->mutex);
    g_hash_table_unref (priv->marks);
}


/* internals */

/* assume lock */
static struct mark *
new_mark (DonnaProviderMark *pm,
          const gchar       *location,
          const gchar       *name,
          enum mark_type     type,
          const gchar       *value,
          GError           **error)
{
    DonnaProviderMarkPrivate *priv = pm->priv;
    struct mark *mark;

    if (strchr (location, '/'))
    {
        g_set_error (error, DONNA_PROVIDER_ERROR,
                DONNA_PROVIDER_ERROR_INVALID_NAME,
                "Provider 'mark': Invalid mark name '%s': cannot contain '/'",
                location);
        return NULL;
    }

    if (g_hash_table_lookup (priv->marks, location))
    {
        g_set_error (error, DONNA_PROVIDER_ERROR,
                DONNA_PROVIDER_ERROR_ALREADY_EXIST,
                "Provider 'mark': Mark '%s' already exists",
                location);
        return NULL;
    }

    mark = g_new (struct mark, 1);
    mark->location = g_strdup (location);
    mark->name = g_strdup ((name) ? name : location);
    mark->type = type;
    mark->value = g_strdup (value);

    g_hash_table_insert (priv->marks, mark->location, mark);

    return mark;
}

static gboolean
refresher (DonnaTask *task, DonnaNode *node, const gchar *name)
{
    DonnaProviderMark *pm = (DonnaProviderMark *) donna_node_peek_provider (node);
    DonnaProviderMarkPrivate *priv = pm->priv;
    struct mark *mark;
    gchar *location;
    GValue v = G_VALUE_INIT;
    gboolean ret = FALSE;

    location = donna_node_get_location (node);
    g_mutex_lock (&priv->mutex);
    mark = g_hash_table_lookup (priv->marks, location);
    g_free (location);
    if (G_UNLIKELY (!mark))
    {
        g_mutex_unlock (&priv->mutex);
        return FALSE;
    }

    if (streq (name, "name"))
    {
        g_value_init (&v, G_TYPE_STRING);
        g_value_set_string (&v, mark->name);
        ret = TRUE;
    }
    else if (streq (name, "full-name"))
    {
        g_value_init (&v, G_TYPE_STRING);
        g_value_set_string (&v, mark->location);
        ret = TRUE;
    }
    else if (streq (name, "value"))
    {
        g_value_init (&v, G_TYPE_STRING);
        g_value_set_string (&v, mark->value);
        ret = TRUE;
    }
    else if (streq (name, "mark-type"))
    {
        g_value_init (&v, G_TYPE_INT);
        g_value_set_int (&v, mark->type);
        ret = TRUE;
    }
    g_mutex_unlock (&priv->mutex);

    if (ret)
    {
        /* this needs to be out of the lock, since node-updated will be emitted
         * and this could otherwise lead to deadlcoks */
        donna_node_set_property_value (node, name, &v);
        g_value_unset (&v);
    }

    return ret;
}

static DonnaTaskState
setter (DonnaTask *task, DonnaNode *node, const gchar *name, const GValue *value)
{
    DonnaProviderMark *pm = (DonnaProviderMark *) donna_node_peek_provider (node);
    DonnaProviderMarkPrivate *priv = pm->priv;
    struct mark *mark;
    gchar *location;
    DonnaTaskState ret = DONNA_TASK_FAILED;

    location = donna_node_get_location (node);
    g_mutex_lock (&priv->mutex);
    mark =g_hash_table_lookup (priv->marks, location);
    g_free (location);
    if (G_UNLIKELY (!mark))
    {
        g_mutex_unlock (&priv->mutex);
        donna_task_set_error (task, DONNA_PROVIDER_ERROR,
                DONNA_PROVIDER_ERROR_LOCATION_NOT_FOUND,
                "Provider 'mark': Cannot set '%s', mark '%s' doesn't exist",
                name, location);
        return DONNA_TASK_FAILED;
    }

    if (streq (name, "name"))
    {
        g_free (mark->name);
        mark->name = g_value_dup_string (value);
        ret = DONNA_TASK_DONE;
    }
    else if (streq (name, "value"))
    {
        g_free (mark->value);
        mark->value = g_value_dup_string (value);
        ret = DONNA_TASK_DONE;
    }
    else if (streq (name, "mark-type"))
    {
        enum mark_type type;

        type = (enum mark_type) g_value_get_int (value);
        if (type == MARK_STANDARD || type == MARK_DYNAMIC)
        {
            mark->type = type;
            ret = DONNA_TASK_DONE;
        }
        else
        {
            g_mutex_unlock (&priv->mutex);
            donna_task_set_error (task, DONNA_PROVIDER_ERROR,
                    DONNA_PROVIDER_ERROR_INVALID_VALUE,
                    "Provider 'mark': Cannot set type of mark for '%s', "
                    "invalid value (%d)",
                    location, type);
            return DONNA_TASK_FAILED;
        }
    }
    g_mutex_unlock (&priv->mutex);

    if (ret == DONNA_TASK_DONE)
        donna_node_set_property_value (node, name, value);

    return DONNA_TASK_DONE;
}

/* assume lock if data_is_mark */
static DonnaNode *
new_node_for_mark (DonnaProviderMark *pm,
                   gboolean           data_is_mark,
                   gpointer           data,
                   GError **error)
{
    DonnaProviderMarkPrivate *priv = pm->priv;
    struct mark *mark;
    DonnaNode *node;
    GValue v = G_VALUE_INIT;

    if (data_is_mark)
        mark = (struct mark *) data;
    else
    {
        g_mutex_lock (&priv->mutex);
        mark = g_hash_table_lookup (priv->marks, (gchar *) data);
        if (G_UNLIKELY (!mark))
        {
            g_mutex_unlock (&priv->mutex);
            g_set_error (error, DONNA_PROVIDER_ERROR,
                    DONNA_PROVIDER_ERROR_LOCATION_NOT_FOUND,
                    "Provider 'mark': Mark '%s' doesn't exist",
                    (gchar *) data);
            return NULL;
        }
    }

    node = donna_node_new ((DonnaProvider *) pm, mark->location,
            DONNA_NODE_ITEM, NULL, refresher, setter, mark->name,
            DONNA_NODE_NAME_WRITABLE | DONNA_NODE_DESC_EXISTS);
    if (G_UNLIKELY (!node))
    {
        if (!data_is_mark)
            g_mutex_unlock (&priv->mutex);
        g_set_error (error, DONNA_PROVIDER_ERROR,
                DONNA_PROVIDER_ERROR_OTHER,
                "Provider 'mark': Failed to create node for mark '%s'",
                mark->location);
        return NULL;
    }

    g_value_init (&v, G_TYPE_STRING);
    g_value_take_string (&v, g_strconcat ("[", mark->location, "] ",
                mark->value, NULL));
    donna_node_set_property_value (node, "desc", &v);
    g_value_unset (&v);

    g_value_init (&v, G_TYPE_INT);
    g_value_set_int (&v, mark->type);
    if (G_UNLIKELY (!donna_node_add_property (node, "mark-type",
                    G_TYPE_INT, &v, refresher, setter, error)))
    {
        if (!data_is_mark)
            g_mutex_unlock (&priv->mutex);
        g_prefix_error (error, "Provider 'mark': Cannot create new node, "
                "failed to add property 'mark-type': ");
        g_value_unset (&v);
        g_object_unref (node);
        return NULL;
    }
    g_value_unset (&v);

    g_value_init (&v, G_TYPE_STRING);
    g_value_set_string (&v, "mark-type");
    if (G_UNLIKELY (!donna_node_add_property (node, "mark-type-extra",
                    G_TYPE_STRING, &v, refresher, NULL, error)))
    {
        if (!data_is_mark)
            g_mutex_unlock (&priv->mutex);
        g_prefix_error (error, "Provider 'mark': Cannot create new node, "
                "failed to add property 'mark-type-extra': ");
        g_value_unset (&v);
        g_object_unref (node);
        return NULL;
    }
    g_value_unset (&v);

    g_value_init (&v, G_TYPE_STRING);
    g_value_set_string (&v, mark->value);
    if (G_UNLIKELY (!donna_node_add_property (node, "value",
                    G_TYPE_STRING, &v, refresher, setter, error)))
    {
        if (!data_is_mark)
            g_mutex_unlock (&priv->mutex);
        g_prefix_error (error, "Provider 'mark': Cannot create new node, "
                "failed to add property 'value': ");
        g_value_unset (&v);
        g_object_unref (node);
        return NULL;
    }
    g_value_unset (&v);

    if (!data_is_mark)
        g_mutex_unlock (&priv->mutex);
    return node;
}

enum
{
    GET_IF_IN_CACHE = 0,
    GET_CREATE_FROM_LOCATION,
    GET_CREATE_FROM_MARK,
};

/* assume lock if GET_CREATE_FROM_MARK */
static DonnaNode *
get_node_for (DonnaProviderMark *pm,
              guint              how,
              gpointer           data,
              GError           **error)
{
    DonnaProviderBase *pb = (DonnaProviderBase *) pm;
    DonnaProviderBaseClass *klass;
    DonnaNode *node;

    klass = DONNA_PROVIDER_BASE_GET_CLASS (pb);
    klass->lock_nodes (pb);
    node = klass->get_cached_node (pb, (how == GET_CREATE_FROM_MARK)
            ? ((struct mark *) data)->location : (gchar *) data);
    if (!node && how != GET_IF_IN_CACHE)
    {
        node = new_node_for_mark (pm, how == GET_CREATE_FROM_MARK, data, error);
        if (G_LIKELY (node))
            klass->add_node_to_cache (pb, node);
    }
    klass->unlock_nodes (pb);

    return node;
}

static DonnaTaskState
get_mark_node (DonnaTask            *task,
               DonnaApp             *app,
               const gchar          *location,
               DonnaNode           **node,
               DonnaProviderMark    *pm)
{
    GError *err = NULL;
    DonnaProviderMarkPrivate *priv = pm->priv;
    struct mark *mark;
    DonnaNode *n = NULL;
    enum mark_type type;
    DonnaTaskState state = DONNA_TASK_DONE;

    g_assert (node);

    g_mutex_lock (&priv->mutex);
    mark = g_hash_table_lookup (priv->marks, location);
    if (G_UNLIKELY (!mark))
    {
        g_mutex_unlock (&priv->mutex);
        donna_task_set_error (task, DONNA_PROVIDER_ERROR,
                DONNA_PROVIDER_ERROR_LOCATION_NOT_FOUND,
                "Provider 'mark': Mark '%s' doesn't exist",
                location);
        return DONNA_TASK_FAILED;
    }

    n = donna_app_get_node (app, mark->value, TRUE, &err);
    if (G_UNLIKELY (!n))
    {
        g_mutex_unlock (&priv->mutex);
        g_prefix_error (&err, "Provider 'mark': "
                "Cannot get %s's node for mark '%s' [%s]: ",
                (mark->type == MARK_STANDARD) ? "dest" : "trigger",
                location, mark->value);
        donna_task_take_error (task, err);
        return DONNA_TASK_FAILED;
    }
    type = mark->type;
    g_mutex_unlock (&priv->mutex);

    /* in STANDARD we have the node we want. In DYNAMIC we have the node to
     * trigger, which should give us the node we want */
    if (type == MARK_DYNAMIC)
    {
        DonnaTask *t;

        t = donna_node_trigger_task (n, &err);
        if (G_UNLIKELY (!t))
        {
            donna_task_set_error (task, DONNA_PROVIDER_ERROR,
                    DONNA_PROVIDER_ERROR_OTHER,
                    "Provider 'mark': Cannot get trigger task for mark '%s'",
                    location);
            return DONNA_TASK_FAILED;
        }
        if (!donna_app_run_task_and_wait (app, g_object_ref (t), task, &err))
        {
            g_prefix_error (&err, "Provider 'mark': "
                    "Failed to run trigger task for mark '%s': ",
                    location);
            donna_task_take_error (task, err);
            g_object_unref (n);
            g_object_unref (t);
            return DONNA_TASK_FAILED;
        }
        g_object_unref (n);
        n = NULL;

        state = donna_task_get_state (t);
        if (state == DONNA_TASK_DONE)
        {
            const GValue *value;

            value = donna_task_get_return_value (t);
            if (!value)
            {
                donna_task_set_error (task, DONNA_PROVIDER_ERROR,
                        DONNA_PROVIDER_ERROR_OTHER,
                        "Provider 'mark': Failed to get node for mark '%s' "
                        "from its trigger: No return value",
                        location);
                state = DONNA_TASK_FAILED;
            }
            else if (!G_VALUE_HOLDS (value, DONNA_TYPE_NODE))
            {
                donna_task_set_error (task, DONNA_PROVIDER_ERROR,
                        DONNA_PROVIDER_ERROR_OTHER,
                        "Provider 'mark': Failed to get node for mark '%s' "
                        "from its trigger: Invalid return type (%s)",
                        location, G_VALUE_TYPE_NAME (value));
                state = DONNA_TASK_FAILED;
            }
            else
                n = g_value_dup_object (donna_task_get_return_value (t));
        }
        else if (state == DONNA_TASK_FAILED)
        {
            err = g_error_copy (donna_task_get_error (t));
            g_prefix_error (&err, "Provider 'mark': Failed to get node for mark '%s' "
                    "from its trigger: ",
                    location);
            donna_task_take_error (task, err);
        }
        g_object_unref (t);
    }

    *node = n;
    return state;
}

static gboolean
delete_mark (DonnaProviderMark *pm, const gchar *location, GError **error)
{
    DonnaProviderMarkPrivate *priv = pm->priv;
    struct mark *mark;
    DonnaNode *node;

    g_mutex_lock (&priv->mutex);
    mark = g_hash_table_lookup (priv->marks, location);
    if (G_UNLIKELY (!mark))
    {
        g_mutex_unlock (&priv->mutex);
        g_set_error (error, DONNA_PROVIDER_ERROR,
                DONNA_PROVIDER_ERROR_LOCATION_NOT_FOUND,
                "Provider 'mark': Cannot delete mark '%s', it doesn't exist",
                location);
        return FALSE;
    }

    node = get_node_for (pm, GET_IF_IN_CACHE, (gpointer) location, NULL);

    g_hash_table_remove (priv->marks, location);
    g_mutex_unlock (&priv->mutex);

    if (node)
    {
        donna_provider_node_deleted ((DonnaProvider *) pm, node);
        g_object_unref (node);
    }

    return TRUE;
}


/* DonnaProvider */

static DonnaProviderFlags
provider_mark_get_flags (DonnaProvider *provider)
{
    g_return_val_if_fail (DONNA_IS_PROVIDER_MARK (provider),
            DONNA_PROVIDER_FLAG_INVALID);
    return DONNA_PROVIDER_FLAG_FLAT;
}

static const gchar *
provider_mark_get_domain (DonnaProvider *provider)
{
    g_return_val_if_fail (DONNA_IS_PROVIDER_MARK (provider), NULL);
    return "mark";
}

static gchar *
provider_mark_get_context_alias_new_nodes (DonnaProvider      *provider,
                                           const gchar        *extra,
                                           DonnaNode          *location,
                                           const gchar        *prefix,
                                           GError            **error)
{
    return g_strdup_printf ("%snew_mark,%snew_dynamic_mark", prefix, prefix);
}

static gboolean
provider_mark_get_context_item_info (DonnaProvider          *provider,
                                     const gchar            *item,
                                     const gchar            *extra,
                                     DonnaContextReference   reference,
                                     DonnaNode              *node_ref,
                                     get_sel_fn              get_sel,
                                     gpointer                get_sel_data,
                                     DonnaContextInfo       *info,
                                     GError                **error)
{
    if (streq (item, "new_mark"))
    {
        info->is_visible = info->is_sensitive = TRUE;
        info->name = "New (Standard) Mark";
        info->icon_name = "document-new";
        info->trigger = "command:tv_goto_line (%o, f+s, @mark_set ("
            "@ask_text (Please enter the location for the new mark), "
            "@ask_text (Please enter the name of the new mark), s,"
            "@ask_text (\"Please enter the destination (full location) of the new mark\")))";
        return TRUE;
    }
    else if (streq (item, "new_dynamic_mark"))
    {
        info->is_visible = info->is_sensitive = TRUE;
        info->name = "New Dynamic Mark";
        info->icon_name = "document-new";
        info->trigger = "command:tv_goto_line (%o, f+s, @mark_set ("
            "@ask_text (Please enter the location for the new mark), "
            "@ask_text (Please enter the name of the new mark), d,"
            "@ask_text (\"Please enter the trigger (full location) for the new mark\")))";
        return TRUE;
    }

    g_set_error (error, DONNA_CONTEXT_MENU_ERROR,
            DONNA_CONTEXT_MENU_ERROR_UNKNOWN_ITEM,
            "Provider 'mark': No such context item: '%s'", item);
    return FALSE;
}

/* DonnaProviderBase */

static DonnaTaskState
provider_mark_new_node (DonnaProviderBase  *_provider,
                        DonnaTask          *task,
                        const gchar        *location)
{
    GError *err = NULL;
    DonnaNode *node;
    GValue *value;

    if (streq (location, "/"))
    {
        DonnaProviderBaseClass *klass;
        DonnaNode *n;

        node = donna_node_new ((DonnaProvider *) _provider, location,
                DONNA_NODE_CONTAINER, NULL, (refresher_fn) gtk_true, NULL,
                "Marks", 0);
        if (G_UNLIKELY (!node))
        {
            donna_task_set_error (task, DONNA_PROVIDER_ERROR,
                    DONNA_PROVIDER_ERROR_OTHER,
                    "Provider 'mark': Unable to create a new node");
            return DONNA_TASK_FAILED;
        }

        klass = DONNA_PROVIDER_BASE_GET_CLASS (_provider);
        klass->lock_nodes (_provider);
        n = klass->get_cached_node (_provider, location);
        if (n)
        {
            /* already added while we were busy */
            g_object_unref (node);
            node = n;
        }
        else
            klass->add_node_to_cache (_provider, node);
        klass->unlock_nodes (_provider);
    }
    else
    {
        node = get_node_for ((DonnaProviderMark *) _provider,
                GET_CREATE_FROM_LOCATION, (gpointer) location, &err);
        if (G_UNLIKELY (!node))
        {
            donna_task_take_error (task, err);
            return DONNA_TASK_FAILED;
        }
    }

    value = donna_task_grab_return_value (task);
    g_value_init (value, DONNA_TYPE_NODE);
    g_value_take_object (value, node);
    donna_task_release_return_value (task);

    return DONNA_TASK_DONE;
}

static DonnaTaskState
provider_mark_has_children (DonnaProviderBase  *_provider,
                            DonnaTask          *task,
                            DonnaNode          *node,
                            DonnaNodeType       node_types)
{
    DonnaProviderMarkPrivate *priv = ((DonnaProviderMark *) _provider)->priv;
    GValue *value;

    value = donna_task_grab_return_value (task);
    g_value_init (value, G_TYPE_BOOLEAN);
    g_mutex_lock (&priv->mutex);
    g_value_set_boolean (value, g_hash_table_size (priv->marks) > 0);
    g_mutex_unlock (&priv->mutex);
    donna_task_release_return_value (task);

    return DONNA_TASK_DONE;
}

static DonnaTaskState
provider_mark_get_children (DonnaProviderBase  *_provider,
                            DonnaTask          *task,
                            DonnaNode          *node,
                            DonnaNodeType       node_types)
{
    DonnaProviderMark *pm = (DonnaProviderMark *) _provider;
    DonnaProviderMarkPrivate *priv = pm->priv;
    GValue *value;
    GPtrArray *nodes;

    /* only one container, root. So we get nodes for all marks */

    if (!(node_types & DONNA_NODE_ITEM))
        /* no containers == return an empty array */
        nodes = g_ptr_array_sized_new (0);
    else
    {
        GHashTableIter iter;
        struct mark *mark;

        g_mutex_lock (&priv->mutex);
        nodes = g_ptr_array_new_full (g_hash_table_size (priv->marks),
                g_object_unref);

        g_hash_table_iter_init (&iter, priv->marks);
        while (g_hash_table_iter_next (&iter, NULL, (gpointer) &mark))
        {
            GError *err = NULL;
            DonnaNode *n;

            n = get_node_for (pm, GET_CREATE_FROM_MARK, mark, &err);
            if (G_UNLIKELY (!n))
            {
                g_mutex_unlock (&priv->mutex);
                g_ptr_array_unref (nodes);
                donna_task_take_error (task, err);
                return DONNA_TASK_FAILED;
            }
            g_ptr_array_add (nodes, n);
        }
        g_mutex_unlock (&priv->mutex);
    }

    value = donna_task_grab_return_value (task);
    g_value_init (value, G_TYPE_PTR_ARRAY);
    g_value_take_boxed (value, nodes);
    donna_task_release_return_value (task);

    return DONNA_TASK_DONE;
}

static DonnaTaskState
provider_mark_trigger_node (DonnaProviderBase  *_provider,
                            DonnaTask          *task,
                            DonnaNode          *node)
{
    GError *err = NULL;
    DonnaTreeView *tree;
    DonnaNode *n;
    DonnaTaskState state;
    gchar *location;

    g_object_get (_provider->app, "active-list", &tree, NULL);
    if (G_UNLIKELY (!tree))
    {
        location = donna_node_get_location (node);
        donna_task_set_error (task, DONNA_PROVIDER_ERROR,
                DONNA_PROVIDER_ERROR_OTHER,
                "Provider 'mark': Failed to trigger mark '%s', couldn't get active-list",
                location);
        g_free (location);
        return DONNA_TASK_FAILED;
    }

    location = donna_node_get_location (node);
    state = get_mark_node (task, _provider->app, location, &n,
            (DonnaProviderMark *) _provider);
    if (state != DONNA_TASK_DONE)
    {
        g_free (location);
        return state;
    }

    if (!donna_tree_view_set_location (tree, n, &err))
    {
        g_prefix_error (&err, "Provider 'mark': Failed to trigger '%s': ",
                location);
        donna_task_take_error (task, err);
        g_object_unref (n);
        g_free (location);
        return DONNA_TASK_FAILED;
    }

    g_object_unref (n);
    g_free (location);
    return DONNA_TASK_DONE;
}

static DonnaTaskState
provider_mark_new_child (DonnaProviderBase  *_provider,
                         DonnaTask          *task,
                         DonnaNode          *parent,
                         DonnaNodeType       type,
                         const gchar        *name)
{
    GError *err = NULL;
    DonnaProviderMarkPrivate *priv = ((DonnaProviderMark *) _provider)->priv;
    struct mark *mark;
    DonnaNode *node_root;
    DonnaNode *node;
    GValue *value;

    if (type == DONNA_NODE_CONTAINER)
    {
        donna_task_set_error (task, DONNA_PROVIDER_ERROR,
                DONNA_PROVIDER_ERROR_NOT_SUPPORTED,
                "Provider 'mark': Cannot create a CONTAINER (marks are ITEMs)");
        return DONNA_TASK_FAILED;
    }

    g_mutex_lock (&priv->mutex);
    mark = new_mark ((DonnaProviderMark *) _provider, name, NULL,
            MARK_STANDARD, NULL, &err);
    if (!mark)
    {
        g_mutex_unlock (&priv->mutex);
        donna_task_take_error (task, err);
        return DONNA_TASK_FAILED;
    }

    node = get_node_for ((DonnaProviderMark *) _provider, GET_CREATE_FROM_MARK,
            mark, &err);
    g_mutex_unlock (&priv->mutex);
    if (G_UNLIKELY (!node))
    {
        g_prefix_error (&err, "Provider 'mark': Failed to get node for new mark '%s': ",
                name);
        donna_task_take_error (task, err);
        return DONNA_TASK_FAILED;
    }

    node_root = get_node_for ((DonnaProviderMark *) _provider, GET_IF_IN_CACHE,
            (gpointer) "/", NULL);
    if (node_root)
    {
        donna_provider_node_new_child ((DonnaProvider *) _provider,
                node_root, node);
        g_object_unref (node_root);
    }

    value = donna_task_grab_return_value (task);
    g_value_init (value, DONNA_TYPE_NODE);
    g_value_take_object (value, node);
    donna_task_release_return_value (task);

    return DONNA_TASK_DONE;
}

static DonnaTaskState
provider_mark_remove_from (DonnaProviderBase  *_provider,
                           DonnaTask          *task,
                           GPtrArray          *nodes,
                           DonnaNode          *source)
{
    GError *err = NULL;
    GString *str = NULL;
    guint i;

    /* since we can only ever have one container, our "root" (only ever
     * containing marks), this can only be about deleting marks */
    for (i = 0; i < nodes->len; ++i)
    {
        DonnaNode *node = nodes->pdata[i];
        gchar *fl = donna_node_get_full_location (node);

        if (G_UNLIKELY (donna_node_peek_provider (node) != (DonnaProvider *) _provider))
        {
            if (!str)
                str = g_string_new (NULL);
            g_string_append_printf (str, "\n- Cannot remove '%s': node isn't in 'mark:/'",
                    fl);
            g_free (fl);
            continue;
        }

        /* 5 == strlen ("mark:"); IOW: fl + 5 == location */
        if (G_UNLIKELY (!delete_mark ((DonnaProviderMark *) _provider, fl + 5, &err)))
        {
            if (!str)
                str = g_string_new (NULL);
            g_string_append_printf (str, "\n- Failed to remove '%s' from 'mark:/': %s",
                    fl, err->message);
            g_clear_error (&err);
            g_free (fl);
            continue;
        }

        g_free (fl);
    }

    if (str)
    {
        donna_task_set_error (task, DONNA_PROVIDER_ERROR,
                DONNA_PROVIDER_ERROR_OTHER,
                "Provider 'mark': Couldn't remove all nodes from 'mark:/':\n%s",
                str->str);
        g_string_free (str, TRUE);
        return DONNA_TASK_FAILED;
    }

    return DONNA_TASK_DONE;
}

/* commands */

/**
 * mark_delete:
 * @name: Name of the mark to delete
 *
 * Delete the mark @mark
 */
static DonnaTaskState
cmd_mark_delete (DonnaTask         *task,
                 DonnaApp          *app,
                 gpointer          *args,
                 DonnaProviderMark *pm)
{
    GError *err = NULL;
    const gchar *location = args[0];

    if (!delete_mark (pm, location, &err))
    {
        g_prefix_error (&err, "Command 'mark_delete': ");
        donna_task_take_error (task, err);
        return DONNA_TASK_FAILED;
    }

    return DONNA_TASK_DONE;
}

static DonnaTaskState
cmd_mark_get_node (DonnaTask         *task,
                   DonnaApp          *app,
                   gpointer          *args,
                   DonnaProviderMark *pm)
{
    const gchar *location = args[0];

    DonnaTaskState state;
    DonnaNode *node;
    GValue *value;

    state = get_mark_node (task, app, location, &node, pm);
    if (state != DONNA_TASK_DONE)
        return state;

    value = donna_task_grab_return_value (task);
    g_value_init (value, DONNA_TYPE_NODE);
    g_value_take_object (value, node);
    donna_task_release_return_value (task);

    return DONNA_TASK_DONE;
}

struct upd
{
    DonnaNode *node;
    gchar *name;
    guint type;
    gchar *value;
};

static void
free_upd (struct upd *upd)
{
    g_object_unref (upd->node);
    g_free (upd->name);
    g_free (upd->value);
    g_free (upd);
}

static void
set_mark (DonnaProviderMark *pm,
          struct mark       *m,
          gboolean           might_replace,
          GPtrArray         *nodes_new,
          GPtrArray         *nodes_upd)
{
    GError *err = NULL;
    DonnaProviderMarkPrivate *priv = pm->priv;
    struct mark *mark = NULL;

    if (might_replace)
        mark = g_hash_table_lookup (priv->marks, m->location);

    if (mark)
    {
        struct upd *upd;

        upd = g_new0 (struct upd, 1);
        upd->type = (guint) -1;

        if (!streq (m->name, mark->name))
        {
            g_free (mark->name);
            mark->name = g_strdup (m->name);
            upd->name = g_strdup (mark->name);
        }
        if (m->type != mark->type)
        {
            mark->type = m->type;
            upd->type = mark->type;
        }
        if (!streq (m->value, mark->value))
        {
            g_free (mark->value);
            mark->value = g_strdup (m->value);
            upd->value = g_strdup (mark->value);
        }

        if (upd->name || upd->type != (guint) -1 || upd->value)
        {
            upd->node = get_node_for (pm, GET_IF_IN_CACHE, mark, NULL);
            if (upd->node)
                g_ptr_array_add (nodes_upd, upd);
            else
                free_upd (upd);
        }
    }
    else if (!new_mark (pm, m->location, m->name, m->type, m->value, &err))
    {
        g_warning ("Provider 'mark': Failed to load mark '%s': %s",
                m->location, err->message);
        g_clear_error (&err);
    }
    else if (nodes_new)
        g_ptr_array_add (nodes_new,
                get_node_for (pm, GET_CREATE_FROM_MARK, m, NULL));
}

static DonnaTaskState
cmd_mark_load (DonnaTask         *task,
               DonnaApp          *app,
               gpointer          *args,
               DonnaProviderMark *pm)
{
    GError *err = NULL;
    DonnaProviderMarkPrivate *priv = pm->priv;

    const gchar *filename = args[0]; /* opt */
    gboolean ignore_no_file = GPOINTER_TO_INT (args[1]); /* opt */
    gboolean reset = GPOINTER_TO_INT (args[2]); /* opt */

    GPtrArray *nodes_del = NULL;
    GPtrArray *nodes_new = NULL;
    GPtrArray *nodes_upd = NULL;
    DonnaNode *node_root;
    struct mark m;
    gboolean in_mark = FALSE;
    gchar *file;
    gchar *data;
    gchar *s, *e;
    guint i;

    if (filename && *filename == '/')
    {
        if (!g_get_filename_charsets (NULL))
            file = g_filename_from_utf8 (filename, -1, NULL, NULL, NULL);
        else
            file = (gchar *) filename;
    }
    else
        file = donna_app_get_conf_filename (app, (filename) ? filename : "marks.conf");

    if (!g_file_get_contents (file, &data, NULL, &err))
    {
        if (file != filename)
            g_free (file);
        if (ignore_no_file && g_error_matches (err, G_FILE_ERROR, G_FILE_ERROR_NOENT))
        {
            g_clear_error (&err);
            return DONNA_TASK_DONE;
        }
        else
        {
            g_prefix_error (&err, "Command 'mark_load': Failed to load marks from '%s': ",
                    (filename) ? filename : "marks.conf");
            donna_task_take_error (task, err);
            return DONNA_TASK_FAILED;
        }
    }
    if (file != filename)
        g_free (file);

    g_mutex_lock (&priv->mutex);
    if (reset && g_hash_table_size (priv->marks) > 0)
    {
        GHashTableIter iter;
        struct mark *mark;

        nodes_del = g_ptr_array_new_full (g_hash_table_size (priv->marks),
                g_object_unref);
        g_hash_table_iter_init (&iter, priv->marks);
        while (g_hash_table_iter_next (&iter, NULL, (gpointer) &mark))
        {
            DonnaNode *node;

            node = get_node_for (pm, GET_IF_IN_CACHE, mark->location, NULL);
            if (node)
                g_ptr_array_add (nodes_del, node);
        }
        g_hash_table_remove_all (priv->marks);
    }

    node_root = get_node_for (pm, GET_IF_IN_CACHE, (gpointer) "/", NULL);
    if (node_root)
        nodes_new = g_ptr_array_new_with_free_func (g_object_unref);
    if (!reset)
        nodes_upd = g_ptr_array_new_with_free_func ((GDestroyNotify) free_upd);

    s = data;
    for (;;)
    {
        e = strchr (s, '\n');
        if (e)
            *e = '\0';
        if (streqn (s, "mark=", 5))
        {
            if (in_mark)
                set_mark (pm, &m, !reset, nodes_new, nodes_upd);
            else
                in_mark = TRUE;
            memset (&m, 0, sizeof (struct mark));
            m.location = s + 5;
        }
        else if (in_mark)
        {
            if (streqn (s, "name=", 5))
            {
                if (!strchr (s, '/'))
                    m.name = s + 5;
            }
            else if (streqn (s, "type=", 5))
            {
                if (s[5] == '0' || s[5] == '1')
                    m.type = (s[5] == '0') ? MARK_STANDARD : MARK_DYNAMIC;
            }
            else if (streqn (s, "value=", 6))
                m.value = s + 6;
        }
        if (e)
            s = e + 1;
        else
            break;
    }

    if (in_mark)
        set_mark (pm, &m, !reset, nodes_new, nodes_upd);
    g_mutex_unlock (&priv->mutex);

    if (nodes_del)
    {
        for (i = 0; i < nodes_del->len; ++i)
            donna_provider_node_deleted ((DonnaProvider *) pm, nodes_del->pdata[i]);
        g_ptr_array_unref (nodes_del);
    }

    if (nodes_upd)
    {
        for (i = 0; i < nodes_upd->len; ++i)
        {
            GValue v = G_VALUE_INIT;
            struct upd *upd = nodes_upd->pdata[i];

            if (upd->name)
            {
                g_value_init (&v, G_TYPE_STRING);
                g_value_take_string (&v, upd->name);
                donna_node_set_property_value (upd->node, "name", &v);
                g_value_unset (&v);
                upd->name = NULL;
            }

            if (upd->type != (guint) -1)
            {
                g_value_init (&v, G_TYPE_INT);
                g_value_set_int (&v, (gint) upd->type);
                donna_node_set_property_value (upd->node, "mark-type", &v);
                g_value_unset (&v);
            }

            if (upd->value)
            {
                g_value_init (&v, G_TYPE_STRING);
                g_value_take_string (&v, upd->value);
                donna_node_set_property_value (upd->node, "value", &v);
                g_value_unset (&v);
                upd->value = NULL;
            }
        }
        g_ptr_array_unref (nodes_upd);
    }

    if (nodes_new)
    {
        for (i = 0; i < nodes_new->len; ++i)
            donna_provider_node_new_child ((DonnaProvider *) pm, node_root,
                    nodes_new->pdata[i]);
        g_ptr_array_unref (nodes_new);
    }

    g_free (data);
    return DONNA_TASK_DONE;
}

static DonnaTaskState
cmd_mark_save (DonnaTask        *task,
              DonnaApp          *app,
              gpointer          *args,
              DonnaProviderMark *pm)
{
    GError *err = NULL;
    DonnaProviderMarkPrivate *priv = pm->priv;

    const gchar *filename = args[0]; /* opt */

    struct mark *mark;
    gchar *file;
    GString *str;
    GHashTableIter iter;

    if (filename && *filename == '/')
    {
        if (!g_get_filename_charsets (NULL))
            file = g_filename_from_utf8 (filename, -1, NULL, NULL, NULL);
        else
            file = (gchar *) filename;
    }
    else
        file = donna_app_get_conf_filename (app, (filename) ? filename : "marks.conf");

    str = g_string_new (NULL);
    g_mutex_lock (&priv->mutex);
    g_hash_table_iter_init (&iter, priv->marks);
    while ((g_hash_table_iter_next (&iter, NULL, (gpointer) &mark)))
    {
        g_string_append_printf (str, "mark=%s\n", mark->location);
        if (mark->type != MARK_STANDARD)
            g_string_append_printf (str, "type=%d\n", mark->type);
        if (!streq (mark->location, mark->name))
            g_string_append_printf (str, "name=%s\n", mark->name);
        g_string_append_printf (str, "value=%s\n", mark->value);
    }
    g_mutex_unlock (&priv->mutex);

    if (!g_file_set_contents (file, str->str, (gssize) str->len, &err))
    {
        g_prefix_error (&err, "Command 'mark_save': Failed to save marks to '%s': ",
                (filename) ? filename : "marks.conf");
        donna_task_take_error (task, err);
        if (file != filename)
            g_free (file);
        g_string_free (str, TRUE);
        return DONNA_TASK_FAILED;
    }

    g_string_free (str, TRUE);
    if (file != filename)
        g_free (file);
    return DONNA_TASK_DONE;
}

static DonnaTaskState
cmd_mark_set (DonnaTask         *task,
              DonnaApp          *app,
              gpointer          *args,
              DonnaProviderMark *pm)
{
    GError *err = NULL;
    DonnaProviderMarkPrivate *priv = pm->priv;

    const gchar *location = args[0];
    const gchar *name = args[1]; /*opt */
    const gchar *type = args[2]; /* opt */
    const gchar *value = args[3]; /*opt */

    struct mark *mark;
    const gchar *s_types[] = { "standard", "dynamic" };
    enum mark_type m_types[] = { MARK_STANDARD, MARK_DYNAMIC };
    enum mark_type m_type;
    gint t = -1;
    DonnaNode *node = NULL;
    enum {
        UPD_NAME    = (1 << 0),
        UPD_TYPE    = (1 << 1),
        UPD_VALUE   = (1 << 2),
    } updated = 0;
    GValue *rv;

    if (type)
    {
        t = _get_choice (s_types, type);
        if (t < 0)
        {
            donna_task_set_error (task, DONNA_COMMAND_ERROR,
                    DONNA_COMMAND_ERROR_SYNTAX,
                    "Command 'mark_set': Cannot set mark '%s', invalid type '%s'; "
                    "Must be 'standard' or 'dynamic'",
                    location, type);
            return DONNA_TASK_FAILED;
        }
        m_type = m_types[t];
    }
    else
        m_type = MARK_STANDARD;

    g_mutex_lock (&priv->mutex);
    mark = g_hash_table_lookup (priv->marks, location);
    if (mark)
    {
        if (name && !streq (mark->name, name))
        {
            g_free (mark->name);
            mark->name = g_strdup (name);
            updated |= UPD_NAME;
        }
        if (type && mark->type != m_type)
        {
            mark->type = m_type;
            updated |= UPD_TYPE;
        }
        if (value && !streq (mark->value, value))
        {
            g_free (mark->value);
            mark->value = g_strdup (value);
            updated |= UPD_VALUE;
        }
        g_mutex_unlock (&priv->mutex);

        node = get_node_for (pm, GET_IF_IN_CACHE, (gpointer) location, NULL);
        if (node)
        {
            GValue v = G_VALUE_INIT;

            if (updated & UPD_NAME)
            {
                g_value_init (&v, G_TYPE_STRING);
                g_value_set_string (&v, name);
                donna_node_set_property_value (node, "name", &v);
                g_value_unset (&v);
            }

            if (updated & UPD_TYPE)
            {
                g_value_init (&v, G_TYPE_INT);
                g_value_set_int (&v, (gint) m_type);
                donna_node_set_property_value (node, "mark-type", &v);
                g_value_unset (&v);
            }

            if (updated & UPD_VALUE)
            {
                g_value_init (&v, G_TYPE_STRING);
                g_value_set_string (&v, value);
                donna_node_set_property_value (node, "value", &v);
                g_value_unset (&v);
            }

            g_object_unref (node);
        }
    }
    else
    {
        DonnaNode *node_root;

        mark = new_mark (pm, location, name, m_type, value, &err);
        if (!mark)
        {
            g_mutex_unlock (&priv->mutex);
            donna_task_take_error (task, err);
            return DONNA_TASK_FAILED;
        }

        node_root = get_node_for (pm, GET_IF_IN_CACHE, (gpointer) "/", NULL);
        if (node_root)
        {
            node = get_node_for (pm, GET_CREATE_FROM_MARK, mark, NULL);
            g_mutex_unlock (&priv->mutex);

            donna_provider_node_new_child ((DonnaProvider *) pm, node_root, node);
            g_object_unref (node_root);
        }
        else
            g_mutex_unlock (&priv->mutex);
    }

    /* we *might* have the node already, either because it was in cache or the
     * root was and we needed to create it (for new-child) */
    if (!node)
        node = get_node_for (pm, GET_CREATE_FROM_LOCATION, (gpointer) location, &err);
    if (G_UNLIKELY (!node))
    {
        if (err)
        {
            g_prefix_error (&err, "Command 'mark_set': "
                    "Failed to get node for newly create mark: ");
            donna_task_take_error (task, err);
        }
        else
            donna_task_set_error (task, DONNA_PROVIDER_ERROR,
                    DONNA_PROVIDER_ERROR_OTHER,
                    "Command 'mark_set': Failed to get node for newly created mark");

        return DONNA_TASK_FAILED;
    }

    rv = donna_task_grab_return_value (task);
    g_value_init (rv, DONNA_TYPE_NODE);
    g_value_take_object (rv, node);
    donna_task_release_return_value (task);

    return DONNA_TASK_DONE;
}


#define add_command(cmd_name, cmd_argc, cmd_visibility, cmd_return_value)     \
if (G_UNLIKELY (!donna_provider_command_add_command (pc, #cmd_name,           \
                (guint) cmd_argc, arg_type, cmd_return_value, cmd_visibility, \
                (command_fn) cmd_##cmd_name, object, NULL, &err)))            \
{                                                                             \
    g_warning ("Provider 'mark': Failed to add command '" #cmd_name "': %s",  \
        err->message);                                                        \
    g_clear_error (&err);                                                     \
}
static void
provider_mark_contructed (GObject *object)
{
    GError *err = NULL;
    DonnaApp *app = ((DonnaProviderBase *) object)->app;

    DonnaConfigItemExtraListInt it[2];

    DonnaProviderCommand *pc;
    DonnaArgType arg_type[8];
    gint i;

    G_OBJECT_CLASS (donna_provider_mark_parent_class)->constructed (object);

    it[0].value     = MARK_STANDARD;
    it[0].in_file   = "standard";
    it[0].label     = "Standard mark";
    it[1].value     = MARK_DYNAMIC;
    it[1].in_file   = "dynamic";
    it[1].label     = "Dynamic Mark";
    if (G_UNLIKELY (!donna_config_add_extra (donna_app_peek_config (app),
                    DONNA_CONFIG_EXTRA_TYPE_LIST_INT, "mark-type", "Type of mark",
                    2, it, &err)))
    {
        g_warning ("Provider 'mark': Failed to set up configuration extra 'mark-type': %s",
                err->message);
        g_clear_error (&err);
    }

    pc = (DonnaProviderCommand *) donna_app_get_provider (app, "command");
    if (G_UNLIKELY (!pc))
    {
        g_warning ("Provider 'mark': Failed to add commands, "
                "couldn't get provider 'command'");
        return;
    }

    i = -1;
    arg_type[++i] = DONNA_ARG_TYPE_STRING;
    add_command (mark_delete, ++i, DONNA_TASK_VISIBILITY_INTERNAL_FAST,
            DONNA_ARG_TYPE_NOTHING);

    i = -1;
    arg_type[++i] = DONNA_ARG_TYPE_STRING;
    add_command (mark_get_node, ++i, DONNA_TASK_VISIBILITY_INTERNAL,
            DONNA_ARG_TYPE_NODE);

    i = -1;
    arg_type[++i] = DONNA_ARG_TYPE_STRING | DONNA_ARG_IS_OPTIONAL;
    arg_type[++i] = DONNA_ARG_TYPE_INT | DONNA_ARG_IS_OPTIONAL;
    arg_type[++i] = DONNA_ARG_TYPE_INT | DONNA_ARG_IS_OPTIONAL;
    add_command (mark_load, ++i, DONNA_TASK_VISIBILITY_INTERNAL,
            DONNA_ARG_TYPE_NOTHING);

    i = -1;
    arg_type[++i] = DONNA_ARG_TYPE_STRING | DONNA_ARG_IS_OPTIONAL;
    add_command (mark_save, ++i, DONNA_TASK_VISIBILITY_INTERNAL,
            DONNA_ARG_TYPE_NOTHING);

    i = -1;
    arg_type[++i] = DONNA_ARG_TYPE_STRING;
    arg_type[++i] = DONNA_ARG_TYPE_STRING | DONNA_ARG_IS_OPTIONAL;
    arg_type[++i] = DONNA_ARG_TYPE_STRING | DONNA_ARG_IS_OPTIONAL;
    arg_type[++i] = DONNA_ARG_TYPE_STRING | DONNA_ARG_IS_OPTIONAL;
    add_command (mark_set, ++i, DONNA_TASK_VISIBILITY_INTERNAL_FAST,
            DONNA_ARG_TYPE_NODE);

    g_object_unref (pc);
}
#undef add_command
