
#include <glib-object.h>
#include <gobject/gvaluecollector.h>    /* G_VALUE_LCOPY */
#include <string.h>                     /* memset() */
#include "node.h"
#include "util.h"
#include "macros.h"                     /* streq() */

struct _DonnaNodePrivate
{
    DonnaProvider   *provider;
    GHashTable      *props;
    GRWLock          props_lock;
};

typedef struct
{
    const gchar *name; /* this pointer is also used as key in the hask table */
    GValue       value;
    gboolean     has_value; /* is value set, or do we need to call get_value? */
    get_value_fn get_value;
    set_value_fn set_value;
} DonnaNodeProp;

static void donna_node_finalize (GObject *object);

static void
donna_node_class_init (DonnaNodeClass *klass)
{
    GObjectClass *o_class;

    o_class = G_OBJECT_CLASS (klass);
    o_class->finalize = donna_node_finalize;
    g_type_class_add_private (klass, sizeof (DonnaNodePrivate));
}

static void
donna_node_init (DonnaNode *node)
{
    node->priv = G_TYPE_INSTANCE_GET_PRIVATE (node,
            DONNA_TYPE_NODE,
            DonnaNodePrivate);
}

G_DEFINE_TYPE (DonnaNode, donna_node, G_TYPE_OBJECT)

static void
donna_node_finalize (GObject *object)
{
    DonnaNodePrivate *priv;

    priv = DONNA_NODE (object)->priv;
    /* it is said that dispose should do the unref-ing, but at the same time
     * the object is supposed to be able to be "revived" from dispose, and we
     * need a ref to provider to survive... */
    g_object_unref (priv->provider);
    g_hash_table_destroy (priv->props);
    g_rw_lock_clear (&priv->props_lock);

    G_OBJECT_CLASS (donna_node_parent_class)->finalize (object);
}

/* used to free properties when removed from hash table */
static void
free_prop (gpointer _prop)
{
    DonnaNodeProp *prop = _prop;

    /* prop->name will be free-d through g_hash_table_destroy, since it is also
     * used as key in there */
    g_value_unset (&prop->value);
    g_slice_free (DonnaNodeProp, prop);
}

DonnaNode *
donna_node_new (DonnaProvider   *provider,
                get_value_fn     location_get,
                set_value_fn     location_set,
                get_value_fn     name_get,
                set_value_fn     name_set,
                get_value_fn     is_container_get,
                set_value_fn     is_container_set,
                get_value_fn     has_children_get,
                set_value_fn     has_children_set)
{
    DonnaNode *node;
    DonnaNodePrivate *priv;
    DonnaNodeProp *prop;

    g_return_val_if_fail (DONNA_IS_PROVIDER (provider), NULL);
    g_return_val_if_fail (location_get != NULL, NULL);
    g_return_val_if_fail (is_container_get != NULL, NULL);
    g_return_val_if_fail (has_children_get != NULL, NULL);

    node = g_object_new (DONNA_TYPE_NODE, NULL);
    priv = node->priv;
    priv->provider = provider;
    priv->props = g_hash_table_new_full (g_str_hash, g_str_equal,
            g_free, free_prop);
    g_rw_lock_init (&priv->props_lock);

    /* provider is a "fake" property, in that it doesn't exists in the hash
     * table, but we act as if it did. This is just to save a few memory/calls
     * for each node, since we know the value and it is read-only, no need to
     * bother with a useless getter, etc
     * We do need to take a ref on our provider, though */
    g_object_ref (provider);

    /* we're creating the node, so no need to lock things here */

    prop = g_slice_new0 (DonnaNodeProp);
    prop->name      = g_strdup ("location");
    prop->get_value = location_get;
    prop->set_value = location_set;
    g_value_init (&prop->value, G_TYPE_STRING);
    g_hash_table_insert (priv->props, (gpointer) prop->name, prop);

    prop = g_slice_new0 (DonnaNodeProp);
    prop->name      = g_strdup ("is_container");
    prop->get_value = is_container_get;
    prop->set_value = is_container_set;
    g_value_init (&prop->value, G_TYPE_BOOLEAN);
    g_hash_table_insert (priv->props, (gpointer) prop->name, prop);

    prop = g_slice_new0 (DonnaNodeProp);
    prop->name      = g_strdup ("has_children");
    prop->get_value = has_children_get;
    prop->set_value = has_children_set;
    g_value_init (&prop->value, G_TYPE_BOOLEAN);
    g_hash_table_insert (priv->props, (gpointer) prop->name, prop);

    return node;
}

DonnaNode *
donna_node_new_from_node (DonnaProvider *provider,
                          get_value_fn   location_get,
                          set_value_fn   location_set,
                          get_value_fn   name_get,
                          set_value_fn   name_set,
                          get_value_fn   is_container_get,
                          set_value_fn   is_container_set,
                          get_value_fn   has_children_get,
                          set_value_fn   has_children_set,
                          DonnaNode     *sce)
{
    DonnaNode       *node;
    GHashTable      *props;
    GHashTableIter   iter;
    gpointer         key;
    gpointer         value;

    g_return_val_if_fail (DONNA_IS_NODE (sce), NULL);

    /* create a new node */
    node = donna_node_new (provider,
            location_get, location_set,
            name_get, name_set,
            is_container_get, is_container_set,
            has_children_get, has_children_set);

    g_return_val_if_fail (DONNA_IS_NODE (node), NULL);

    /* and copy over all the other properties */
    props = node->priv->props;
    g_rw_lock_reader_lock (&sce->priv->props_lock);
    g_hash_table_iter_init (&iter, sce->priv->props);
    while (g_hash_table_iter_next (&iter, &key, &value))
    {
        DonnaNodeProp *prop;
        DonnaNodeProp *prop_sce = value;

        /* those "special" properties will be handled by the new provider */
        if (streq (prop_sce->name, "location")
                || streq (prop_sce->name, "is_container")
                || streq (prop_sce->name, "has_children"))
        {
            continue;
        }

        prop = g_slice_copy (sizeof (*prop), prop_sce);
        /* for the GValue we'll need to reset the memory, re-init it */
        memset (&prop->value, 0, sizeof (GValue));
        g_value_init (&prop->value, G_VALUE_TYPE (&prop_sce->value));
        /* and if there's a value, re-copy it over */
        if (prop->has_value)
            g_value_copy (&prop_sce->value, &prop->value);

        g_hash_table_insert (props, (gpointer) g_strdup (key), value);
    }
    g_rw_lock_reader_unlock (&sce->priv->props_lock);

    return node;
}

gboolean
donna_node_add_property (DonnaNode       *node,
                         const gchar     *name,
                         GType            type,
                         GValue          *value,
                         get_value_fn     get_value,
                         set_value_fn     set_value,
                         GError         **error)
{
    DonnaNodePrivate   *priv;
    DonnaNodeProp      *prop;

    g_return_val_if_fail (DONNA_IS_NODE (node), FALSE);
    g_return_val_if_fail (name != NULL, FALSE);
    g_return_val_if_fail (get_value != NULL, FALSE);
    /* set_value is optional (can be read-only) */

    priv = node->priv;
    g_rw_lock_writer_lock (&priv->props_lock);
    if (streq (name, "provider")
            || g_hash_table_contains (priv->props, name))
    {
        g_rw_lock_writer_unlock (&priv->props_lock);
        g_set_error (error, DONNA_NODE_ERROR, DONNA_NODE_ERROR_ALREADY_EXISTS,
                "Node already contains a property %s", name);
        return FALSE;
    }
    /* allocate a new FmNodeProp to hold the property value */
    prop = g_slice_new0 (DonnaNodeProp);
    prop->name      = g_strdup (name);
    prop->get_value = get_value;
    prop->set_value = set_value;
    /* init the GValue */
    g_value_init (&prop->value, type);
    /* do we have an init value to set? */
    if (value != NULL)
    {
        if (G_VALUE_HOLDS (value, type))
        {
            g_value_copy (value, &prop->value);
            prop->has_value = TRUE;
        }
        else
        {
            g_rw_lock_writer_unlock (&priv->props_lock);
            g_set_error (error, DONNA_NODE_ERROR, DONNA_NODE_ERROR_INVALID_TYPE,
                    "Invalid format for initial value of new property %s: "
                    "property is %s, initial value is %s",
                    name,
                    g_type_name (type),
                    g_type_name (G_VALUE_TYPE (&prop->value)));
            g_slice_free (DonnaNodeProp, prop);
            return FALSE;
        }
    }
    /* add prop to the hash table */
    g_hash_table_insert (priv->props, (gpointer) prop->name, prop);
    g_rw_lock_writer_unlock (&priv->props_lock);

    return TRUE;
}

struct set_property
{
    DonnaNode       *node;
    DonnaNodeProp   *prop;
    GValue          *value;
};

static DonnaTaskState
set_property (DonnaTask *task, struct set_property *data)
{
    GError *err = NULL;
    GValue value = G_VALUE_INIT;
    gboolean ret;

    /* TODO: set_value should get a pointer to an int, as long as it's 0 it can
     * do its work, as soon as it's 1 the task is being cancelled. This int
     * should come from fmTask, but can also be supported w/out a task ofc */
    ret = data->prop->set_value (data->node, data->prop->name, data->value, &err);
    if (!ret)
        donna_task_take_error (task, err);
    /* set the return value */
    g_value_init (&value, G_TYPE_BOOLEAN);
    g_value_set_boolean (&value, ret);
    donna_task_set_ret_value (task, &value);
    g_value_unset (&value);

    /* free memory */
    g_object_unref (data->node);
    g_value_unset (data->value);
    g_slice_free (GValue, data->value);
    g_slice_free (struct set_property, data);

    /* this defines the task's state */
    return (ret) ? DONNA_TASK_COMPLETED
        : (donna_task_is_cancelled (task)
                ? DONNA_TASK_CANCELLED
                : DONNA_TASK_FAILED);
}

DonnaTask *
set_property_task (DonnaNode     *node,
                   const gchar   *name,
                   GValue        *value,
                   GCallback      callback,
                   gpointer       callback_data,
                   GError       **error)
{
    DonnaNodePrivate *priv;
    DonnaNodeProp *prop;
    DonnaTask *task;
    struct set_property *data;

    g_return_val_if_fail (DONNA_IS_NODE (node), FALSE);
    g_return_val_if_fail (name != NULL, FALSE);
    g_return_val_if_fail (value != NULL, FALSE);

    if (streq (name, "provider"))
    {
        g_set_error (error, DONNA_NODE_ERROR, DONNA_NODE_ERROR_READ_ONLY,
                "Property %s on node cannot be set", name);
        return NULL;
    }

    priv = node->priv;
    g_rw_lock_reader_lock (&priv->props_lock);
    prop = g_hash_table_lookup (priv->props, (gpointer) name);
    /* the lock is for the hash table only. the DonnaNodeProp isn't going
     * anywhere, nor can it change, so we can let it go now. The only thing that
     * can happen is a change of value, but the type will/can not change */
    g_rw_lock_reader_unlock (&priv->props_lock);
    if (!prop)
    {
        g_set_error (error, DONNA_NODE_ERROR, DONNA_NODE_ERROR_NOT_FOUND,
                "Node does not have a property %s", name);
        return NULL;
    }

    if (!prop->set_value)
    {
        g_set_error (error, DONNA_NODE_ERROR, DONNA_NODE_ERROR_READ_ONLY,
                "Property %s on node cannot be set", name);
        return NULL;
    }

    if (!G_VALUE_HOLDS (value, G_VALUE_TYPE (&(prop->value))))
    {
        g_set_error (error, DONNA_NODE_ERROR, DONNA_NODE_ERROR_INVALID_TYPE,
                "Property %s on node is of type %s, value passed is %s",
                name,
                g_type_name (G_VALUE_TYPE (&(prop->value))),
                g_type_name (G_VALUE_TYPE (value)));
        return NULL;
    }

    data = g_slice_new (struct set_property);
    /* take a ref on node, for the task */
    g_object_ref (node);
    data->node = node;
    data->prop = prop;
    data->value = duplicate_gvalue (value);
    task = donna_task_new (NULL, set_property, data, callback, callback_data);
    donna_task_manager_add_task (task);

    return task;
}

static void
get_valist (DonnaNode    *node,
            GError      **error,
            const gchar  *first_name,
            va_list       va_args)
{
    GHashTable *props;
    const gchar *name;

    props = node->priv->props;
    g_rw_lock_reader_lock (&node->priv->props_lock);
    name = first_name;
    while (name)
    {
        DonnaNodeProp *prop;
        gchar *err;

        /* special property, that doesn't actually exists in the hash table */
        if (streq (name, "provider"))
        {
            DonnaProvider **p;
            p = va_arg (va_args, DonnaProvider **);
            *p = g_object_ref (node->priv->provider);
            goto next;
        }

        prop = g_hash_table_lookup (props, (gpointer) name);
        if (!prop)
        {
            g_set_error (error, DONNA_NODE_ERROR, DONNA_NODE_ERROR_NOT_FOUND,
                    "Node does not have a property %s", name);
            break;
        }
        if (!prop->has_value)
        {
            GError *err_local = NULL;

            /* we remove the reader lock, to allow get_value to do its work
             * and call set_property_value, which uses a writer lock obviously */
            g_rw_lock_reader_unlock (&node->priv->props_lock);
            if (!prop->get_value (node, name, &err_local))
            {
                g_propagate_error (error, err_local);
                return;
            }
            g_rw_lock_reader_lock (&node->priv->props_lock);
            /* let's assume prop is still valid. Properties cannot be removed,
             * so it has to still exists after all */
        }
        G_VALUE_LCOPY (&(prop->value), va_args, 0, &err);
        if (err)
        {
            g_set_error (error, DONNA_NODE_ERROR, DONNA_NODE_ERROR_OTHER,
                    "Failed to get node property %s: %s",
                    name,
                    err);
            g_free (err);
            break;
        }
next:
        name = va_arg (va_args, gchar *);
    }
    g_rw_lock_reader_unlock (&node->priv->props_lock);
}

void
donna_node_get (DonnaNode    *node,
                GError      **error,
                const gchar  *first_name,
                ...)
{
    va_list va_args;

    g_return_if_fail (DONNA_IS_NODE (node));

    va_start (va_args, first_name);
    get_valist (node, error, first_name, va_args);
    va_end (va_args);
}

void
donna_mode_refresh (DonnaNode *node)
{
    GHashTableIter iter;
    gpointer key, value;

    g_return_if_fail (DONNA_IS_NODE (node));

    g_rw_lock_writer_lock (&node->priv->props_lock);
    g_hash_table_iter_init (&iter, node->priv->props);
    while (g_hash_table_iter_next (&iter, &key, &value))
    {
        ((DonnaNodeProp *) value)->has_value = FALSE;
    }
    g_rw_lock_writer_unlock (&node->priv->props_lock);
}

void
set_property_value (DonnaNode     *node,
                    const gchar   *name,
                    GValue        *value)
{
    DonnaNodeProp *prop;

    g_return_if_fail (DONNA_IS_NODE (node));
    g_return_if_fail (name != NULL);

    g_rw_lock_writer_lock (&node->priv->props_lock);
    prop = g_hash_table_lookup (node->priv->props, (gpointer) name);
    if (prop)
    {
        g_value_copy (value, &(prop->value));
        /* we assume it worked, w/out checking types, etc because this should
         * only be used by providers and such, on properties they are handling,
         * so if they get it wrong, they're seriously bugged */
        prop->has_value = TRUE;
    }
    g_rw_lock_writer_unlock (&node->priv->props_lock);
}
