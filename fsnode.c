
typedef struct _FsProvider FsProvider;

#include <gtk/gtk.h>
#include "fsnode.h"

struct _FsNodePrivate
{
    FsProvider  *provider;
    const gchar *location;
    GHashTable  *props;
    GPtrArray   *iters;
};

typedef struct
{
    /* name is the key in the hash table */
    GValue       value;
    gboolean     has_value; /* is value set, or do we need to call get_value? */
    get_value_fn get_value;
    set_value_fn set_value;
} FsNodeProp;

static void
fsnode_class_init (FsNodeClass *klass)
{
    GObjectClass *o_class;

    o_class = G_OBJECT_CLASS (klass);
    g_type_class_add_private (klass, sizeof (FsNodePrivate));
}

static void
fsnode_init (FsNode *node)
{
    node->priv = G_TYPE_INSTANCE_GET_PRIVATE (node,
            TYPE_FSNODE,
            FsNodePrivate);
}

G_DEFINE_TYPE (FsNode, fsnode, G_TYPE_INITIALLY_UNOWNED)

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
            NULL, free_prop);
    priv->iters = g_ptr_array_new_with_free_func (
            (GDestroyNotify) gtk_tree_iter_free);

    g_object_ref_sink (provider);

    return node;
}

FsNode *
fsnode_new_from_node (FsProvider    *provider,
                      const gchar   *location,
                      FsNode        *sce)
{
    FsNode *node;
    FsNodePrivate *priv;

    g_return_val_if_fail (IS_FSPROVIDER (provider), NULL);
    g_return_val_if_fail (location != NULL, NULL);
    g_return_val_if_fail (IS_FSNODE (sce), NULL);

    node = fsnode_new (provider, location);
    priv = node->priv;
    /* TODO ... */
}

gboolean
fsnode_add_property (FsNode          *node,
                     const gchar     *name,
                     GType            type,
                     GValue          *value,
                     get_value_fn     get_value,
                     set_value_fn     set_value,
                     GError         **error)
{
    FsNodePrivate   *priv;
    FsNodeProp      *prop;
    GValue          *prop_value;

    g_return_val_if_fail (IS_FSNODE (node), FALSE);
    g_return_val_if_fail (name != NULL, FALSE);
    g_return_val_if_fail (get_value != NULL, FALSE);
    /* set_value is optional (can be read-only) */

    priv = node->priv;
    if (g_hash_table_contains (priv->props, name))
    {
        g_set_error (error, FSNODE_ERROR, FSNODE_ERROR_ALREADY_EXISTS,
                "Node already contains a property %s", name);
        return FALSE;
    }
    /* allocate a new GValue to hold the property value */
    prop = g_slice_new0 (FsNodeProp);
    prop->get_value = get_value;
    prop->set_value = set_value;
    /* get GValue and init it */
    prop_value = &(prop->value);
    g_value_init (prop_value, type);
    /* do we have an init value to set? */
    if (value != NULL)
    {
        if (G_VALUE_HOLDS (value, type))
        {
            g_value_copy (value, prop_value);
            prop->has_value = TRUE;
        }
        else
        {
            g_set_error (error, FSNODE_ERROR, FSNODE_ERROR_INVALID_TYPE,
                    "Invalid format for initial value of new property %s: "
                    "property is %s, initial value is %s",
                    name,
                    g_type_name (type),
                    g_type_name (G_VALUE_TYPE (prop_value)));
            g_slice_free (FsNodeProp, prop);
            return FALSE;
        }
    }
    /* add prop to the hash table */
    g_hash_table_insert (priv->props, (gpointer) name, prop);

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

    prop = g_hash_table_lookup (node->priv->props, (gpointer) name);
    if (prop)
    {
        g_value_copy (value, &(prop->value));
        /* we assume it worked, w/out checking types, etc because this should
         * only be used by providers and such, on properties they are handling,
         * so if they get it wrong, they're seriously bugged */
        prop->has_value = TRUE;
    }
}

gboolean
fsnode_set_property (FsNode          *node,
                     const gchar     *name,
                     GValue          *value,
                     GError         **error)
{
    GError *err = NULL;
    FsNodeProp *prop;

    g_return_val_if_fail (IS_FSNODE (node), FALSE);
    g_return_val_if_fail (name != NULL, FALSE);
    g_return_val_if_fail (value != NULL, FALSE);

    prop = g_hash_table_lookup (node->priv->props, (gpointer) name);
    if (!prop)
    {
        g_set_error (error, FSNODE_ERROR, FSNODE_ERROR_NOT_FOUND,
                "Node does not have a property %s", name);
        return FALSE;
    }

    if (!prop->set_value)
    {
        g_set_error (error, FSNODE_ERROR, FSNODE_ERROR_READ_ONLY,
                "Property %s on node cannot be set", name);
        return FALSE;
    }

    if (!G_VALUE_HOLDS (value, G_VALUE_TYPE (&(prop->value))))
    {
        g_set_error (error, FSNODE_ERROR, FSNODE_ERROR_INVALID_TYPE,
                "Property %s on node is of type %s, value passed is %s",
                name,
                g_type_name (G_VALUE_TYPE (&(prop->value))),
                g_type_name (G_VALUE_TYPE (value)));
        return FALSE;
    }

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
    name = first_property_name;
    while (name)
    {
        FsNodeProp *prop;
        gchar *err;

        prop = g_hash_table_lookup (props, (gpointer) name);
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

static void
unset_has_value (gpointer key, gpointer value, gpointer data)
{
    ((FsNodeProp *) value)->has_value = FALSE;
}

void
fsnode_refresh (FsNode *node)
{
    g_return_if_fail (IS_FSNODE (node));
    g_hash_table_foreach (node->priv->props, unset_has_value, NULL);
}

void
fsnode_add_iter (FsNode      *node,
                 GtkTreeIter *iter)
{
    g_return_if_fail (IS_FSNODE (node));
    g_ptr_array_add (node->priv->iters, (gpointer) gtk_tree_iter_copy (iter));
}

gboolean
fsnode_remove_iter (FsNode      *node,
                    GtkTreeIter *iter)
{
    GPtrArray *iters;
    guint len;
    guint i;

    iters = node->priv->iters;
    for (i = 0, len = iters->len; i < len; ++i)
    {
        GtkTreeIter *iter_arr = iters->pdata[i];

        /* same iter (i.e. they point to the same row) ? */
        if (iter->stamp && iter_arr->stamp
                && iter->user_data == iter_arr->user_data
                && iter->user_data2 == iter_arr->user_data2
                && iter->user_data3 == iter_arr->user_data3)
        {
            g_ptr_array_remove_index_fast (iters, i);
            return TRUE;
        }
    }
    return FALSE;
}

GtkTreeIter **
fsnode_get_iters (FsNode *node)
{
    g_return_val_if_fail (IS_FSNODE (node), NULL);
    return (GtkTreeIter **) (node->priv->iters->pdata);
}
