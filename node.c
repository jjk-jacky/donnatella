
#include <glib-object.h>
#include <gobject/gvaluecollector.h>    /* G_VALUE_LCOPY */
#include <string.h>                     /* memset() */
#include "node.h"
#include "provider.h"                   /* donna_provider_node_updated() */
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
    GValue value = G_VALUE_INIT;
    DonnaTaskState ret;

    ret = data->prop->set_value (task, data->node, data->prop->name,
            (const GValue *) data->value);

    /* set the return value */
    g_value_init (&value, G_TYPE_BOOLEAN);
    g_value_set_boolean (&value, (ret == DONNA_TASK_DONE));
    donna_task_set_return_value (task, &value);
    g_value_unset (&value);

    /* free memory */
    g_object_unref (data->node);
    g_value_unset (data->value);
    g_slice_free (GValue, data->value);
    g_slice_free (struct set_property, data);

    return ret;
}

DonnaTask *
set_property_task (DonnaNode        *node,
                   const gchar      *name,
                   GValue           *value,
                   task_callback_fn  callback,
                   gpointer          callback_data,
                   guint             timeout,
                   task_timeout_fn   timeout_fn,
                   gpointer          timeout_data,
                   GError          **error)
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
    task = donna_task_new (NULL /* internal task */,
            (task_fn) set_property, data,
            callback, callback_data,
            timeout, timeout_fn, timeout_data);

    return task;
}

static void
get_valist (DonnaNode    *node,
            const gchar  *first_name,
            va_list       va_args)
{
    GHashTable *props;
    const gchar *name;
    gboolean *has_value;

    props = node->priv->props;
    g_rw_lock_reader_lock (&node->priv->props_lock);
    name = first_name;
    while (name)
    {
        DonnaNodeProp *prop;
        gchar *err;

        has_value = va_arg (va_args, gboolean *);

        /* special property, that doesn't actually exists in the hash table */
        if (streq (name, "provider"))
        {
            DonnaProvider **p;

            *has_value = TRUE;
            p = va_arg (va_args, DonnaProvider **);
            *p = g_object_ref (node->priv->provider);
            goto next;
        }

        prop = g_hash_table_lookup (props, (gpointer) name);
        if (!prop)
        {
            gpointer ptr;

            *has_value = FALSE;
            ptr = va_arg (va_args, gpointer);
            goto next;
        }
        *has_value = prop->has_value;
        if (*has_value)
            G_VALUE_LCOPY (&(prop->value), va_args, 0, &err);
        if (err)
            g_free (err);
next:
        name = va_arg (va_args, gchar *);
    }
    g_rw_lock_reader_unlock (&node->priv->props_lock);
}

void
donna_node_get (DonnaNode    *node,
                const gchar  *first_name,
                ...)
{
    va_list va_args;

    g_return_if_fail (DONNA_IS_NODE (node));

    va_start (va_args, first_name);
    get_valist (node, first_name, va_args);
    va_end (va_args);
}

struct refresh_data
{
    DonnaNode   *node;
    GPtrArray   *names;
    GPtrArray   *refreshed;
};

static void 
node_updated_cb (DonnaProvider       *provider,
                 DonnaNode           *node,
                 const gchar         *name,
                 const GValue        *old_value,
                 struct refresh_data *data)
{
    GPtrArray *names;
    GPtrArray *refreshed;
    guint      i;

    if (data->node != node)
        return;

    names = data->names;
    refreshed = data->refreshed;

    /* is the updated property one we're "watching" */
    for (i = 0; i < names->len; ++i)
    {
        if (streq (names->pdata[i], name))
        {
            guint    j;
            gboolean done;

            /* make sure it isn't already in refreshed */
            done = FALSE;
            for (j = 0; j < refreshed->len; ++j)
            {
                /* refreshed contains the *same pointers* as names */
                if (refreshed->pdata[j] == names->pdata[i])
                {
                    done = TRUE;
                    break;
                }
            }

            if (!done)
                g_ptr_array_add (refreshed, names->pdata[i]);

            break;
        }
    }
}

static DonnaTaskState
node_refresh (DonnaTask *task, gpointer _data)
{
    struct refresh_data *data;
    DonnaNodePrivate    *priv;
    GHashTable          *props;
    gulong               sig;
    GPtrArray           *names;
    GPtrArray           *refreshed;
    guint                i;
    DonnaTaskState       ret;
    GValue              *value;

    data = (struct refresh_data *) _data;
    priv = data->node->priv;
    names = data->names;
    refreshed = data->refreshed = g_ptr_array_sized_new (names->len);
    ret = DONNA_TASK_DONE;

    /* connect to the provider's signal, so we know which properties are
     * actually refreshed */
    sig = g_signal_connect (priv->provider, "node-updated",
            G_CALLBACK (node_updated_cb), _data);

    props = priv->props;
    for (i = 0; i < names->len; ++i)
    {
        DonnaNodeProp   *prop;
        guint            j;
        gboolean         done;

        if (donna_task_is_cancelling (task))
        {
            ret = DONNA_TASK_CANCELLED;
            break;
        }

        g_rw_lock_reader_lock (&priv->props_lock);
        prop = g_hash_table_lookup (props, names->pdata[i]);
        g_rw_lock_reader_unlock (&priv->props_lock);
        if (!prop)
            continue;

        /* only call the getter if the prop hasn't already been refreshed */
        done = FALSE;
        for (j = 0; j < refreshed->len; ++j)
        {
            /* refreshed contains the *same pointers* as names */
            if (refreshed->pdata[j] == names->pdata[i])
            {
                done = TRUE;
                break;
            }
        }
        if (done)
            continue;

        if (!prop->get_value (task, data->node, names->pdata[i]))
            ret = DONNA_TASK_FAILED;
    }

    /* disconnect our handler -- any signal that we care about would have come
     * from the getter, so in this thread, so it would have been processed. */
    g_signal_handler_disconnect (priv->provider, sig);

    g_ptr_array_set_free_func (names, g_free);
    /* did everything get refreshed? */
    if (names->len == refreshed->len)
    {
        /* we don't set a return value. A lack of return value (or NULL) will
         * mean that no properties was not refreshed */
        g_free (g_ptr_array_free (refreshed, FALSE));
        g_ptr_array_free (names, TRUE);
        /* force the return state to DONE, since all properties were refreshed.
         * In the odd chance that the getter for prop1 failed (returned FALSE)
         * but e.g. the getter for prop2 did take care of both prop1 & prop2 */
        ret = DONNA_TASK_DONE;
    }
    else
    {
        /* construct the list of non-refreshed properties */
        for (i = 0; i < names->len; )
        {
            guint    j;
            gboolean done;

            done = FALSE;
            for (j = 0; j < refreshed->len; ++j)
            {
                if (refreshed->pdata[j] == names->pdata[i])
                {
                    done = TRUE;
                    break;
                }
            }

            if (done)
                /* done, so we remove it from names. this will free the string,
                 * and get the last element moved to the current one,
                 * effectively replacing it. So next iteration we don't need to
                 * move inside the array */
                g_ptr_array_remove_index_fast (names, i);
            else
                /* move to the next element */
                ++i;
        }
        /* names now only contains the names of non-refreshed properties, it's
         * our return value. (refreshed isn't needed anymore, and can be freed)
         * */
        g_free (g_ptr_array_free (refreshed, FALSE));

        /* because the return value must be a NULL-terminated array */
        g_ptr_array_add (names, NULL);

        /* set the return value. the task will take ownership of names->pdata,
         * so we shouldn't free it, only names itself */
        value = donna_task_take_return_value (task);
        g_value_init (value, G_TYPE_STRV);
        g_value_take_boxed (value, names->pdata);
        donna_task_release_return_value (task);

        /* free names (but not the pdata, now owned by the task's return value */
        g_ptr_array_free (names, FALSE);
    }

    /* free memory (names & refreshed have been taken care of already) */
    g_object_unref (data->node);
    g_slice_free (struct refresh_data, data);

    return ret;
}

DonnaTask *
donna_node_refresh (DonnaNode           *node,
                    task_callback_fn     callback,
                    gpointer             callback_data,
                    guint                timeout,
                    task_timeout_fn      timeout_callback,
                    gpointer             timeout_data,
                    const gchar         *first_name,
                    ...)
{
    DonnaTask           *task;
    GPtrArray           *names;
    struct refresh_data *data;

    g_return_val_if_fail (DONNA_IS_NODE (task), NULL);

    if (first_name)
    {
        va_list     va_args;
        gpointer    name;

        names = g_ptr_array_new ();

        va_start (va_args, first_name);
        name = (gpointer) first_name;
        while (name)
        {
            name = (gpointer) g_strdup (name);
            g_ptr_array_add (names, name);
            name = va_arg (va_args, gpointer);
        }
        va_end (va_args);
    }
    else
    {
        GHashTableIter iter;
        gpointer key, value;

        /* we'll send the list of all properties, because node_refresh() needs
         * to know which getter to call, and it can't have a lock on the hash
         * table since the getter will call set_property_value which needs to
         * take a writer lock... */

        g_rw_lock_reader_lock (&node->priv->props_lock);
        names = g_ptr_array_sized_new (g_hash_table_size (node->priv->props));
        g_hash_table_iter_init (&iter, node->priv->props);
        while (g_hash_table_iter_next (&iter, &key, &value))
        {
            value = (gpointer) g_strdup ((gchar *) key);
            g_ptr_array_add (names, value);
        }
        g_rw_lock_reader_unlock (&node->priv->props_lock);
    }

    data = g_slice_new0 (struct refresh_data);
    data->node = g_object_ref (node);
    data->names = names;

    task = donna_task_new (NULL /* internal task */,
            node_refresh,   data,
            callback,       callback_data,
            timeout,        timeout_callback, timeout_data);
    return task;
}

void
set_property_value (DonnaNode     *node,
                    const gchar   *name,
                    GValue        *value)
{
    DonnaNodeProp *prop;
    GValue old_value = G_VALUE_INIT;
    gboolean has_old_value = FALSE;

    g_return_if_fail (DONNA_IS_NODE (node));
    g_return_if_fail (name != NULL);

    g_rw_lock_writer_lock (&node->priv->props_lock);
    prop = g_hash_table_lookup (node->priv->props, (gpointer) name);
    if (prop)
    {
        if (prop->has_value)
        {
            has_old_value = TRUE;
            /* make a copy of the old value (for signal) */
            g_value_init (&old_value, G_VALUE_TYPE (&(prop->value)));
            g_value_copy (&(prop->value), &old_value);
        }

        /* copy the new value over */
        g_value_copy (value, &(prop->value));
        /* we assume it worked, w/out checking types, etc because this should
         * only be used by providers and such, on properties they are handling,
         * so if they get it wrong, they're seriously bugged */
        prop->has_value = TRUE;
    }
    g_rw_lock_writer_unlock (&node->priv->props_lock);

    donna_provider_node_updated (node->priv->provider, node, name,
            (has_old_value) ? &old_value : NULL);
    g_value_unset (&old_value);
}
