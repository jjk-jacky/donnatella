
typedef struct _FsProvider FsProvider;
#define IS_FSPROVIDER(o)    NULL

#include <gtk/gtk.h>
#include <gobject/gvaluecollector.h>    /* G_VALUE_LCOPY */
#include <string.h>                     /* memset() */
#include "fsnode.h"

struct _FsNodePrivate
{
    FsProvider  *provider;
    const gchar *location;
    GHashTable  *props;
    GRWLock      props_lock;
    GPtrArray   *iters;
    GMutex       iters_mutex;
};

typedef struct
{
    /* name is the key in the hash table */
    GValue       value;
    gboolean     has_value; /* is value set, or do we need to call get_value? */
    get_value_fn get_value;
    set_value_fn set_value;
} FsNodeProp;

static void fsnode_finalize (GObject *object);

static void
fsnode_class_init (FsNodeClass *klass)
{
    GObjectClass *o_class;

    o_class = G_OBJECT_CLASS (klass);
    o_class->finalize = fsnode_finalize;
    g_type_class_add_private (klass, sizeof (FsNodePrivate));
}

static void
fsnode_init (FsNode *node)
{
    node->priv = G_TYPE_INSTANCE_GET_PRIVATE (node,
            TYPE_FSNODE,
            FsNodePrivate);
}

G_DEFINE_TYPE (FsNode, fsnode, G_TYPE_OBJECT)

static void
fsnode_finalize (GObject *object)
{
    FsNodePrivate *priv;

    priv = FSNODE (object)->priv;
    /* it is said that dispose should do the unref-ing, but at the same time
     * the object is supposed to be able to be "revived" from dispose, and we
     * need a ref to provider to survive... */
    g_object_unref (priv->provider);
    g_hash_table_destroy (priv->props);
    g_rw_lock_clear (&priv->props_lock);
    g_ptr_array_free (priv->iters, TRUE);
    g_mutex_clear (&priv->iters_mutex);

    G_OBJECT_CLASS (fsnode_parent_class)->finalize (object);
}

/* used to free properties when removed from hash table */
static void
free_prop (gpointer prop)
{
    g_slice_free (FsNodeProp, prop);
}

FsNode *
fsnode_new (FsProvider  *provider,
            const gchar *location)
{
    FsNode *node;
    FsNodePrivate *priv;

    g_return_val_if_fail (IS_FSPROVIDER (provider), NULL);
    g_return_val_if_fail (location != NULL, NULL);

    node = g_object_new (TYPE_FSNODE, NULL);
    priv = node->priv;
    priv->provider = provider;
    priv->location = location;
    priv->props = g_hash_table_new_full (g_str_hash, g_str_equal,
            g_free, free_prop);
    g_rw_lock_init (&priv->props_lock);
    priv->iters = g_ptr_array_new_with_free_func (
            (GDestroyNotify) gtk_tree_iter_free);
    g_mutex_init (&priv->iters_mutex);

    g_object_ref (provider);

    return node;
}

FsNode *
fsnode_new_from_node (FsProvider    *provider,
                      const gchar   *location,
                      FsNode        *sce)
{
    FsNode *node;
    GHashTable *props;
    GHashTableIter iter;
    gpointer key, value;

    g_return_val_if_fail (IS_FSNODE (sce), NULL);

    /* create a new node */
    node = fsnode_new (provider, location);

    g_return_val_if_fail (IS_FSNODE (node), NULL);

    /* and copy over all the properties */
    props = node->priv->props;
    g_rw_lock_reader_lock (&sce->priv->props_lock);
    g_hash_table_iter_init (&iter, sce->priv->props);
    while (g_hash_table_iter_next (&iter, &key, &value))
    {
        FsNodeProp *prop;
        FsNodeProp *prop_sce = value;

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

FsProvider *
fsnode_get_provider (FsNode *node)
{
    g_return_val_if_fail (IS_FSNODE (node), NULL);
    return node->priv->provider;
}

const gchar *
fsnode_get_location (FsNode *node)
{
    g_return_val_if_fail (IS_FSNODE (node), NULL);
    return node->priv->location;
}

gboolean
fsnode_add_property (FsNode          *node,
                     gchar           *name,
                     GType            type,
                     GValue          *value,
                     get_value_fn     get_value,
                     set_value_fn     set_value,
                     GError         **error)
{
    FsNodePrivate   *priv;
    FsNodeProp      *prop;

    g_return_val_if_fail (IS_FSNODE (node), FALSE);
    g_return_val_if_fail (name != NULL, FALSE);
    g_return_val_if_fail (get_value != NULL, FALSE);
    /* set_value is optional (can be read-only) */

    priv = node->priv;
    g_rw_lock_writer_lock (&priv->props_lock);
    if (g_hash_table_contains (priv->props, name))
    {
        g_rw_lock_writer_unlock (&priv->props_lock);
        g_set_error (error, FSNODE_ERROR, FSNODE_ERROR_ALREADY_EXISTS,
                "Node already contains a property %s", name);
        return FALSE;
    }
    /* allocate a new FsNodeProp to hold the property value */
    prop = g_slice_new0 (FsNodeProp);
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
            g_set_error (error, FSNODE_ERROR, FSNODE_ERROR_INVALID_TYPE,
                    "Invalid format for initial value of new property %s: "
                    "property is %s, initial value is %s",
                    name,
                    g_type_name (type),
                    g_type_name (G_VALUE_TYPE (&prop->value)));
            g_slice_free (FsNodeProp, prop);
            return FALSE;
        }
    }
    /* add prop to the hash table */
    g_hash_table_insert (priv->props, (gpointer) g_strdup (name), prop);
    g_rw_lock_writer_unlock (&priv->props_lock);

    return TRUE;
}

static void
set_prop (FsNode        *node,
          const gchar   *name,
          GValue        *value)
{
    FsNodeProp *prop;

    g_return_if_fail (IS_FSNODE (node));
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

gboolean
fsnode_set_property (FsNode          *node,
                     const gchar     *name,
                     GValue          *value,
                     GError         **error)
{
    GError *err = NULL;
    FsNodeProp *prop;
    FsNodePrivate *priv;

    g_return_val_if_fail (IS_FSNODE (node), FALSE);
    g_return_val_if_fail (name != NULL, FALSE);
    g_return_val_if_fail (value != NULL, FALSE);

    priv = node->priv;
    g_rw_lock_reader_lock (&priv->props_lock);
    prop = g_hash_table_lookup (priv->props, (gpointer) name);
    if (!prop)
    {
        g_rw_lock_reader_unlock (&priv->props_lock);
        g_set_error (error, FSNODE_ERROR, FSNODE_ERROR_NOT_FOUND,
                "Node does not have a property %s", name);
        return FALSE;
    }

    if (!prop->set_value)
    {
        g_rw_lock_reader_unlock (&priv->props_lock);
        g_set_error (error, FSNODE_ERROR, FSNODE_ERROR_READ_ONLY,
                "Property %s on node cannot be set", name);
        return FALSE;
    }

    if (!G_VALUE_HOLDS (value, G_VALUE_TYPE (&(prop->value))))
    {
        g_rw_lock_reader_unlock (&priv->props_lock);
        g_set_error (error, FSNODE_ERROR, FSNODE_ERROR_INVALID_TYPE,
                "Property %s on node is of type %s, value passed is %s",
                name,
                g_type_name (G_VALUE_TYPE (&(prop->value))),
                g_type_name (G_VALUE_TYPE (value)));
        return FALSE;
    }

    /* we unlock now, because the provider/whoever will do the work, might take
     * a while (slow fs, network connection, some timing out...) and during
     * this time, no need to keep a lock for nothing. The set_prop will use a
     * writer lock of course */
    g_rw_lock_reader_unlock (&priv->props_lock);
    if (!prop->set_value (node, name, set_prop, value, &err))
    {
        g_propagate_error (error, err);
        return FALSE;
    }

    return TRUE;
}

static void
get_valist (FsNode       *node,
            GError      **error,
            const gchar  *first_property_name,
            va_list       va_args)
{
    GHashTable *props;
    const gchar *name;

    props = node->priv->props;
    g_rw_lock_reader_lock (&node->priv->props_lock);
    name = first_property_name;
    while (name)
    {
        FsNodeProp *prop;
        gchar *err;

        prop = g_hash_table_lookup (props, (gpointer) name);
        if (!prop)
        {
            g_set_error (error, FSNODE_ERROR, FSNODE_ERROR_NOT_FOUND,
                    "Node does not have a property %s", name);
            break;
        }
        if (!prop->has_value)
        {
            GError *err_local = NULL;

            /* we remove the reader lock, to allow get_value to do its work
             * and call set_prop, which needs a writer lock obviously */
            g_rw_lock_reader_unlock (&node->priv->props_lock);
            if (!prop->get_value (node, name, set_prop, &err_local))
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
            g_set_error (error, FSNODE_ERROR, FSNODE_ERROR_OTHER,
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
fsnode_get (FsNode       *node,
            GError      **error,
            const gchar  *first_property_name,
            ...)
{
    va_list va_args;

    g_return_if_fail (IS_FSNODE (node));

    va_start (va_args, first_property_name);
    get_valist (node, error, first_property_name, va_args);
    va_end (va_args);
}

void
fsnode_refresh (FsNode *node)
{
    GHashTableIter iter;
    gpointer key, value;

    g_return_if_fail (IS_FSNODE (node));
    g_rw_lock_writer_lock (&node->priv->props_lock);
    g_hash_table_iter_init (&iter, node->priv->props);
    while (g_hash_table_iter_next (&iter, &key, &value))
    {
        ((FsNodeProp *) value)->has_value = FALSE;
    }
    g_rw_lock_writer_unlock (&node->priv->props_lock);
}
