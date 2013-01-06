
typedef struct _FmProvider  FmProvider;
#define IS_FMPROVIDER(o)    NULL
typedef struct _FmTask      FmTask;

#include <gtk/gtk.h>
#include <gobject/gvaluecollector.h>    /* G_VALUE_LCOPY */
#include <string.h>                     /* memset() */
#include "fmnode.h"

struct _FmNodePrivate
{
    FmProvider  *provider;
    gchar       *location;
    gboolean     is_container;
    GMutex       location_mutex;
    GHashTable  *props;
    GRWLock      props_lock;
};

typedef struct
{
    const gchar *name; /* this pointer is also used as key in the hask table */
    GValue       value;
    gboolean     has_value; /* is value set, or do we need to call get_value? */
    get_value_fn get_value;
    set_value_fn set_value;
} FmNodeProp;

static void fmnode_finalize (GObject *object);

static void
fmnode_class_init (FmNodeClass *klass)
{
    GObjectClass *o_class;

    o_class = G_OBJECT_CLASS (klass);
    o_class->finalize = fmnode_finalize;
    g_type_class_add_private (klass, sizeof (FmNodePrivate));
}

static void
fmnode_init (FmNode *node)
{
    node->priv = G_TYPE_INSTANCE_GET_PRIVATE (node,
            TYPE_FMNODE,
            FmNodePrivate);
}

G_DEFINE_TYPE (FmNode, fmnode, G_TYPE_OBJECT)

static void
fmnode_finalize (GObject *object)
{
    FmNodePrivate *priv;

    priv = FMNODE (object)->priv;
    /* it is said that dispose should do the unref-ing, but at the same time
     * the object is supposed to be able to be "revived" from dispose, and we
     * need a ref to provider to survive... */
    g_object_unref (priv->provider);
    g_free (priv->location);
    g_mutex_clear (&priv->location_mutex);
    g_hash_table_destroy (priv->props);
    g_rw_lock_clear (&priv->props_lock);

    G_OBJECT_CLASS (fmnode_parent_class)->finalize (object);
}

/* used to free properties when removed from hash table */
static void
free_prop (gpointer _prop)
{
    FmNodeProp *prop = _prop;

    /* prop->name will be free-d through g_hash_table_destroy */
    g_value_unset (&prop->value);
    g_slice_free (FmNodeProp, prop);
}

FmNode *
fmnode_new (FmProvider  *provider,
            const gchar *location,
            gboolean     is_container)
{
    FmNode *node;
    FmNodePrivate *priv;

    g_return_val_if_fail (IS_FMPROVIDER (provider), NULL);
    g_return_val_if_fail (location != NULL, NULL);

    node = g_object_new (TYPE_FMNODE, NULL);
    priv = node->priv;
    priv->provider = provider;
    priv->location = g_strdup (location);
    g_mutex_init (&priv->location_mutex);
    priv->is_container = is_container;
    priv->props = g_hash_table_new_full (g_str_hash, g_str_equal,
            g_free, free_prop);
    g_rw_lock_init (&priv->props_lock);

    g_object_ref (provider);

    return node;
}

FmNode *
fmnode_new_from_node (FmProvider    *provider,
                      const gchar   *location,
                      gboolean       is_container,
                      FmNode        *sce)
{
    FmNode *node;
    GHashTable *props;
    GHashTableIter iter;
    gpointer key, value;

    g_return_val_if_fail (IS_FMNODE (sce), NULL);

    /* create a new node */
    node = fmnode_new (provider, location, is_container);

    g_return_val_if_fail (IS_FMNODE (node), NULL);

    /* and copy over all the properties */
    props = node->priv->props;
    g_rw_lock_reader_lock (&sce->priv->props_lock);
    g_hash_table_iter_init (&iter, sce->priv->props);
    while (g_hash_table_iter_next (&iter, &key, &value))
    {
        FmNodeProp *prop;
        FmNodeProp *prop_sce = value;

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

FmProvider *
fmnode_get_provider (FmNode *node)
{
    g_return_val_if_fail (IS_FMNODE (node), NULL);
    return node->priv->provider;
}

gchar *
fmnode_get_location (FmNode *node)
{
    gchar *loc;

    g_return_val_if_fail (IS_FMNODE (node), NULL);

    g_mutex_lock (&node->priv->location_mutex);
    loc = g_strdup (node->priv->location);
    g_mutex_unlock (&node->priv->location_mutex);
    return loc;
}

gboolean
fmnode_is_container (FmNode *node)
{
    g_return_val_if_fail (IS_FMNODE (node), FALSE);

    return node->priv->is_container;
}

gboolean
fmnode_add_property (FmNode          *node,
                     const gchar     *name,
                     GType            type,
                     GValue          *value,
                     get_value_fn     get_value,
                     set_value_fn     set_value,
                     GError         **error)
{
    FmNodePrivate   *priv;
    FmNodeProp      *prop;

    g_return_val_if_fail (IS_FMNODE (node), FALSE);
    g_return_val_if_fail (name != NULL, FALSE);
    g_return_val_if_fail (get_value != NULL, FALSE);
    /* set_value is optional (can be read-only) */

    priv = node->priv;
    g_rw_lock_writer_lock (&priv->props_lock);
    if (g_hash_table_contains (priv->props, name))
    {
        g_rw_lock_writer_unlock (&priv->props_lock);
        g_set_error (error, FMNODE_ERROR, FMNODE_ERROR_ALREADY_EXISTS,
                "Node already contains a property %s", name);
        return FALSE;
    }
    /* allocate a new FmNodeProp to hold the property value */
    prop = g_slice_new0 (FmNodeProp);
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
            g_set_error (error, FMNODE_ERROR, FMNODE_ERROR_INVALID_TYPE,
                    "Invalid format for initial value of new property %s: "
                    "property is %s, initial value is %s",
                    name,
                    g_type_name (type),
                    g_type_name (G_VALUE_TYPE (&prop->value)));
            g_slice_free (FmNodeProp, prop);
            return FALSE;
        }
    }
    /* add prop to the hash table */
    g_hash_table_insert (priv->props, (gpointer) prop->name, prop);
    g_rw_lock_writer_unlock (&priv->props_lock);

    return TRUE;
}

static gboolean
set_property_checks (FmNode          *node,
                     const gchar     *name,
                     GValue          *value,
                     GError         **error,
                     FmNodeProp     **prop)
{
    FmNodePrivate *priv;

    g_return_val_if_fail (IS_FMNODE (node), FALSE);
    g_return_val_if_fail (name != NULL, FALSE);
    g_return_val_if_fail (value != NULL, FALSE);

    priv = node->priv;
    g_rw_lock_reader_lock (&priv->props_lock);
    *prop = g_hash_table_lookup (priv->props, (gpointer) name);
    /* the lock is for the hash table only. the FmNodeProp isn't going anywhere,
     * nor can it change, so we can let it go now. The only thing that can
     * happen is a change of value, but the type will/can not change */
    g_rw_lock_reader_unlock (&priv->props_lock);
    if (!*prop)
    {
        g_set_error (error, FMNODE_ERROR, FMNODE_ERROR_NOT_FOUND,
                "Node does not have a property %s", name);
        return FALSE;
    }

    if (!(*prop)->set_value)
    {
        g_set_error (error, FMNODE_ERROR, FMNODE_ERROR_READ_ONLY,
                "Property %s on node cannot be set", name);
        return FALSE;
    }

    if (!G_VALUE_HOLDS (value, G_VALUE_TYPE (&((*prop)->value))))
    {
        g_set_error (error, FMNODE_ERROR, FMNODE_ERROR_INVALID_TYPE,
                "Property %s on node is of type %s, value passed is %s",
                name,
                g_type_name (G_VALUE_TYPE (&((*prop)->value))),
                g_type_name (G_VALUE_TYPE (value)));
        return FALSE;
    }

    return TRUE;
}

gboolean
fmnode_set_property (FmNode          *node,
                     const gchar     *name,
                     GValue          *value,
                     GError         **error)
{
    GError *err = NULL;
    FmNodeProp *prop;

    if (!set_property_checks (node, name, value, error, &prop))
        return FALSE;

    /* no lock, because the provider/whoever will do the work might take a
     * while (slow fs, network connection, some timing out...) and during this
     * time, no need to keep a lock for nothing.
     * The callback (set_value) will use set_property_value to update value(s),
     * which will use a writer lock of course */
    if (!prop->set_value (node, name, value, &err))
    {
        g_propagate_error (error, err);
        return FALSE;
    }

    return TRUE;
}

/************************

struct set_property
{
    FmNode      *node;
    FmNodeProp  *prop;
    GValue      *value;
};

static FmTaskState
set_property (FmTask *task, struct set_property *data)
{
    GError *err = NULL;
    GValue value = G_VALUE_INIT;
    gboolean ret;

    /* TODO: set_value should get a pointer to an int, as long as it's 0 it can
     * do its work, as soon as it's 1 the task is being cancelled. This int
     * should come from fmTask, but can also be supported w/out a task ofc *
    ret = data->prop->set_value (data->node, data->prop->name, data->value, &err);
    if (!ret)
        fmtask_take_error (task, err);
    /* set the return value *
    g_value_init (&value, G_TYPE_BOOLEAN);
    g_value_set_boolean (&value, ret);
    fmtask_set_ret_value (task, &value);
    g_value_unset (&value);

    /* free memory *
    g_object_unref (data->node);
    g_value_unset (data->value);
    g_free (data->value);
    g_slice_free (struct set_property, data);

    /* this defines the task's state *
    return (ret) ? FMTASK_SUCCESS :
        fmtask_is_cancelled (task) ? FMTASK_CANCELLED : FMTASK_ERROR;
}

static GValue *
duplicate_gvalue (const GValue *src)
{
    GValue *dst;

    dst = g_new0 (GValue, 1);
    g_value_init (dst, G_VALUE_TYPE (src));
    g_value_copy (src, dst);

    return dst;
}

FmTask *
set_property_task (FmNode        *node,
                   const gchar   *name,
                   GValue        *value,
                   GCallback      callback,
                   gpointer       callback_data,
                   GError       **error)
{
    FmNodeProp *prop;
    FmTask *task;
    struct set_property *data;

    if (!set_property_checks (node, name, value, error, &prop))
        return NULL;

    data = g_slice_new (struct set_property);
    /* take a ref on node, for the task *
    g_object_ref (node);
    data->node = node;
    data->prop = prop;
    data->value = duplicate_gvalue (value);
    task = fmtask_new (NULL, set_property, data, callback, callback_data);
    fmtask_manager_add_task (task);

    return task;
}
***************************/

static void
get_valist (FmNode       *node,
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
        FmNodeProp *prop;
        gchar *err;

        prop = g_hash_table_lookup (props, (gpointer) name);
        if (!prop)
        {
            g_set_error (error, FMNODE_ERROR, FMNODE_ERROR_NOT_FOUND,
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
            g_set_error (error, FMNODE_ERROR, FMNODE_ERROR_OTHER,
                    "Failed to get node property %s: %s",
                    name,
                    err);
            g_free (err);
            break;
        }
        name = va_arg (va_args, gchar *);
    }
    g_rw_lock_reader_unlock (&node->priv->props_lock);
}

void
fmnode_get (FmNode       *node,
            GError      **error,
            const gchar  *first_name,
            ...)
{
    va_list va_args;

    g_return_if_fail (IS_FMNODE (node));

    va_start (va_args, first_name);
    get_valist (node, error, first_name, va_args);
    va_end (va_args);
}

void
fsmode_refresh (FmNode *node)
{
    GHashTableIter iter;
    gpointer key, value;

    g_return_if_fail (IS_FMNODE (node));
    g_rw_lock_writer_lock (&node->priv->props_lock);
    g_hash_table_iter_init (&iter, node->priv->props);
    while (g_hash_table_iter_next (&iter, &key, &value))
    {
        ((FmNodeProp *) value)->has_value = FALSE;
    }
    g_rw_lock_writer_unlock (&node->priv->props_lock);
}

gchar *
fmnode_set_location (FmNode      *node,
                     const gchar *new_location)
{
    gchar *old_location;

    g_return_val_if_fail (IS_FMNODE (node), NULL);

    g_mutex_lock (&node->priv->location_mutex);
    old_location = node->priv->location;
    node->priv->location = g_strdup (new_location);
    g_mutex_unlock (&node->priv->location_mutex);

    /* as usual, nodes do not have signals, it'll be up to the provider (only
     * one that should use this function) to emit the signal. That's why we
     * send it the old_location, and it's his job to free it */
    return old_location;
}

void
set_property_value (FmNode        *node,
                    const gchar   *name,
                    GValue        *value)
{
    FmNodeProp *prop;

    g_return_if_fail (IS_FMNODE (node));
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
