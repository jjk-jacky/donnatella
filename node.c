
#include <glib-object.h>
#include <gobject/gvaluecollector.h>    /* G_VALUE_LCOPY */
#include <string.h>                     /* memset() */
#include "node.h"
#include "provider.h"                   /* donna_provider_node_updated() */
#include "task.h"
#include "util.h"
#include "macros.h"                     /* streq() */

/* index of the first basic prop in node_basic_properties; i.e. after the
 * internal (e.g. provider) and required (e.g. name) ones */
#define FIRST_BASIC_PROP    6
/* we "re-list" basic properties here, to save their values */
enum
{
    BASIC_PROP_FULL_NAME = 0,
    BASIC_PROP_SIZE,
    BASIC_PROP_CTIME,
    BASIC_PROP_MTIME,
    BASIC_PROP_ATIME,
    BASIC_PROP_PERMS,
    BASIC_PROP_USER,
    BASIC_PROP_GROUP,
    BASIC_PROP_TYPE,
    NB_BASIC_PROPS
};

struct _DonnaNodePrivate
{
    /* internal properties */
    DonnaProvider   *provider;
    gchar           *location;
    DonnaNodeType    node_type;
    /* required properties */
    gchar           *name;
    gchar           *icon;
    /* basic properties */
    struct {
        DonnaNodeHasValue has_value;
        GValue            value;
    } basic_props[NB_BASIC_PROPS];
    /* properties handler */
    refresher_fn     refresher;
    setter_fn        setter;
    DonnaNodeFlags   flags;
    /* other properties */
    GHashTable      *props;
    GRWLock          props_lock; /* also applies to basic_props, name & icon */
    /* toggle count (for provider's toggle reference) */
    int              toggle_count;
};

typedef struct
{
    const gchar *name; /* this pointer is also used as key in the hash table */
    refresher_fn refresher;
    setter_fn    setter;
    gboolean     has_value; /* is value set, or do we need to call refresher? */
    GValue       value;
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
    DonnaNodePrivate *priv;

    priv = node->priv = G_TYPE_INSTANCE_GET_PRIVATE (node,
            DONNA_TYPE_NODE,
            DonnaNodePrivate);

    g_value_init (&priv->basic_props[BASIC_PROP_FULL_NAME].value, G_TYPE_STRING);
    g_value_init (&priv->basic_props[BASIC_PROP_SIZE].value,  G_TYPE_UINT);
    g_value_init (&priv->basic_props[BASIC_PROP_CTIME].value, G_TYPE_INT64);
    g_value_init (&priv->basic_props[BASIC_PROP_MTIME].value, G_TYPE_INT64);
    g_value_init (&priv->basic_props[BASIC_PROP_ATIME].value, G_TYPE_INT64);
    g_value_init (&priv->basic_props[BASIC_PROP_PERMS].value, G_TYPE_UINT);
    g_value_init (&priv->basic_props[BASIC_PROP_USER].value,  G_TYPE_STRING);
    g_value_init (&priv->basic_props[BASIC_PROP_GROUP].value, G_TYPE_STRING);
    g_value_init (&priv->basic_props[BASIC_PROP_TYPE].value,  G_TYPE_STRING);

    priv->toggle_count = 1;
}

G_DEFINE_TYPE (DonnaNode, donna_node, G_TYPE_OBJECT)

static void
donna_node_finalize (GObject *object)
{
    DonnaNodePrivate *priv;
    gint i;

    priv = DONNA_NODE (object)->priv;
    /* it is said that dispose should do the unref-ing, but at the same time
     * the object is supposed to be able to be "revived" from dispose, and we
     * need a ref to provider to survive... */
    g_object_unref (priv->provider);
    g_free (priv->location);
    g_free (priv->name);
    g_free (priv->icon);
    for (i = 0; i < NB_BASIC_PROPS; ++i)
        g_value_unset (&priv->basic_props[i].value);
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
                const gchar     *location,
                DonnaNodeType    node_type,
                refresher_fn     refresher,
                setter_fn        setter,
                const gchar     *name,
                const gchar     *icon,
                DonnaNodeFlags   flags)
{
    DonnaNode *node;
    DonnaNodePrivate *priv;
    DonnaNodeProp *prop;

    g_return_val_if_fail (DONNA_IS_PROVIDER (provider), NULL);
    g_return_val_if_fail (location != NULL, NULL);
    g_return_val_if_fail (refresher != NULL, NULL);
    g_return_val_if_fail (setter != NULL, NULL);
    g_return_val_if_fail (name != NULL, NULL);

    node = g_object_new (DONNA_TYPE_NODE, NULL);
    priv = node->priv;
    priv->provider  = g_object_ref (provider);
    priv->location  = g_strdup (location);
    priv->node_type = node_type;
    priv->name      = g_strdup (name);
    priv->icon      = g_strdup (icon);
    priv->refresher = refresher;
    priv->setter    = setter;
    priv->flags     = flags;
    priv->props     = g_hash_table_new_full (g_str_hash, g_str_equal,
            g_free, free_prop);
    g_rw_lock_init (&priv->props_lock);
    /* we want to store which basic prop exists in basic_props as well. We'll
     * use that so we can just loop other basic_props and see which ones exists,
     * etc */
    if (flags & DONNA_NODE_FULL_NAME_EXISTS)
        priv->basic_props[BASIC_PROP_FULL_NAME].has_value = DONNA_NODE_VALUE_NEED_REFRESH;
    if (flags & DONNA_NODE_SIZE_EXISTS)
        priv->basic_props[BASIC_PROP_SIZE].has_value = DONNA_NODE_VALUE_NEED_REFRESH;
    if (flags & DONNA_NODE_CTIME_EXISTS)
        priv->basic_props[BASIC_PROP_CTIME].has_value = DONNA_NODE_VALUE_NEED_REFRESH;
    if (flags & DONNA_NODE_MTIME_EXISTS)
        priv->basic_props[BASIC_PROP_MTIME].has_value = DONNA_NODE_VALUE_NEED_REFRESH;
    if (flags & DONNA_NODE_ATIME_EXISTS)
        priv->basic_props[BASIC_PROP_ATIME].has_value = DONNA_NODE_VALUE_NEED_REFRESH;
    if (flags & DONNA_NODE_PERMS_EXISTS)
        priv->basic_props[BASIC_PROP_PERMS].has_value = DONNA_NODE_VALUE_NEED_REFRESH;
    if (flags & DONNA_NODE_USER_EXISTS)
        priv->basic_props[BASIC_PROP_USER].has_value = DONNA_NODE_VALUE_NEED_REFRESH;
    if (flags & DONNA_NODE_GROUP_EXISTS)
        priv->basic_props[BASIC_PROP_GROUP].has_value = DONNA_NODE_VALUE_NEED_REFRESH;
    if (flags & DONNA_NODE_TYPE_EXISTS)
        priv->basic_props[BASIC_PROP_TYPE].has_value = DONNA_NODE_VALUE_NEED_REFRESH;

    return node;
}

DonnaNode *
donna_node_new_from_node (DonnaProvider *provider,
                          const gchar   *location,
                          DonnaNode     *sce)
{
    DonnaNode        *node;
    DonnaNodePrivate *priv;
    GHashTable       *props;
    GHashTableIter    iter;
    gpointer          key;
    gpointer          value;
    gint              i;

    g_return_val_if_fail (DONNA_IS_NODE (sce), NULL);

    /* create a new node, duplicate of sce but w/ different provider & location */
    node = donna_node_new (provider, location,
            sce->priv->node_type, sce->priv->refresher, sce->priv->setter,
            sce->priv->name, sce->priv->icon, sce->priv->flags);
    g_return_val_if_fail (DONNA_IS_NODE (node), NULL);

    /* and copy over all the (other) properties */
    priv = node->priv;
    props = priv->props;
    g_rw_lock_reader_lock (&sce->priv->props_lock);
    /* basic props */
    for (i = 0; i < NB_BASIC_PROPS; ++i)
    {
        if (sce->priv->basic_props[i].has_value == DONNA_NODE_VALUE_SET)
        {
            priv->basic_props[i].has_value = DONNA_NODE_VALUE_SET;
            g_value_copy (&sce->priv->basic_props[i].value,
                    &priv->basic_props[i].value);
        }
    }
    /* other props */
    g_hash_table_iter_init (&iter, sce->priv->props);
    while (g_hash_table_iter_next (&iter, &key, &value))
    {
        DonnaNodeProp *prop;
        DonnaNodeProp *prop_sce = value;

        prop = g_slice_copy (sizeof (*prop), prop_sce);
        /* the name must be copied */
        prop->name = g_strdup (prop_sce->name);
        /* for the GValue we'll need to reset the memory, i.e. re-init it */
        memset (&prop->value, 0, sizeof (GValue));
        g_value_init (&prop->value, G_VALUE_TYPE (&prop_sce->value));
        /* and if there's a value, re-copy it over */
        if (prop->has_value)
            g_value_copy (&prop_sce->value, &prop->value);

        g_hash_table_insert (props, (gpointer) prop->name, prop);
    }
    g_rw_lock_reader_unlock (&sce->priv->props_lock);

    return node;
}

gboolean
donna_node_add_property (DonnaNode       *node,
                         const gchar     *name,
                         GType            type,
                         GValue          *value,
                         refresher_fn     refresher,
                         setter_fn        setter,
                         GError         **error)
{
    DonnaNodePrivate    *priv;
    DonnaNodeProp       *prop;
    const gchar        **s;

    g_return_val_if_fail (DONNA_IS_NODE (node), FALSE);
    g_return_val_if_fail (name != NULL, FALSE);
    g_return_val_if_fail (refresher != NULL, FALSE);
    /* setter is optional (can be read-only) */

    priv = node->priv;
    g_rw_lock_writer_lock (&priv->props_lock);
    /* cannot add a basic property */
    for (s = node_basic_properties; *s; ++s)
    {
        if (streq (name, *s))
        {
            g_rw_lock_writer_unlock (&priv->props_lock);
            g_set_error (error, DONNA_NODE_ERROR, DONNA_NODE_ERROR_ALREADY_EXISTS,
                    "Cannot add property %s: basic property", name);
            return FALSE;
        }
    }
    /* make sure it doesn't already exists */
    if (g_hash_table_contains (priv->props, name))
    {
        g_rw_lock_writer_unlock (&priv->props_lock);
        g_set_error (error, DONNA_NODE_ERROR, DONNA_NODE_ERROR_ALREADY_EXISTS,
                "Node already contains a property %s", name);
        return FALSE;
    }
    /* allocate a new DonnaNodeProp to hold the property value */
    prop = g_slice_new0 (DonnaNodeProp);
    prop->name      = g_strdup (name);
    prop->refresher = refresher;
    prop->setter    = setter;
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

static void
get_valist (DonnaNode   *node,
            gboolean     is_blocking,
            const gchar *first_name,
            va_list      va_args)
{
    DonnaNodePrivate *priv;
    GHashTable *props;
    const gchar *name;
    gboolean *has_value;

    priv = node->priv;
    props = priv->props;
    g_rw_lock_reader_lock (&priv->props_lock);
    name = first_name;
    while (name)
    {
        DonnaNodeProp     *prop;
        DonnaNodeHasValue *has_value;
        gchar **s;
        gint i;
        gchar *err = NULL;

        /* internal/required properties: there's a value, just set it */
        if (streq (name, "provider"))
        {
            DonnaProvider **p;

            p = va_arg (va_args, DonnaProvider **);
            *p = g_object_ref (priv->provider);
            goto next;
        }
        else if (streq (name, "domain"))
        {
            s = va_arg (va_args, gchar **);
            *s = priv->provider->domain;
            goto next;
        }
        else if (streq (name, "location"))
        {
            s = va_arg (va_args, gchar **);
            *s = g_strdup (priv->location);
            goto next;
        }
        else if (streq (name, "node-type"))
        {
            DonnaNodeType *t;

            t = va_arg (va_args, DonnaNodeType *);
            *t = priv->node_type;
            goto next;
        }
        else if (streq (name, "name"))
        {
            s = va_arg (va_args, gchar **);
            *s = g_strdup (priv->name);
            goto next;
        }
        else if (streq (name, "icon"))
        {
            s = va_arg (va_args, gchar **);
            *s = g_strdup (priv->icon);
            goto next;
        }

        has_value = va_arg (va_args, DonnaNodeHasValue *);

        /* basic properties: might not have a value, so there's a has_value */
        i = 0;
        for (s = (gchar **) node_basic_properties[FIRST_BASIC_PROP]; s; ++s, ++i)
        {
            if (streq (name, *s))
            {
                *has_value = priv->basic_props[i].has_value;
                if (*has_value == DONNA_NODE_VALUE_SET)
                {
                    G_VALUE_LCOPY (&priv->basic_props[i].value, va_args, 0, &err);
                    if (err)
                        g_free (err);
                }
                else
                {
                    if (*has_value == DONNA_NODE_VALUE_NEED_REFRESH && is_blocking)
                    {
                        /* we need to release the lock, since the refresher
                         * should call set_property_value, hence need a writer
                         * lock */
                        g_rw_lock_reader_unlock (&priv->props_lock);
                        if (priv->refresher (NULL /* no task */, node, name))
                        {
                            g_rw_lock_reader_lock (&priv->props_lock);
                            /* check if the value has actually been set */
                            *has_value = priv->basic_props[i].has_value;
                            if (*has_value == DONNA_NODE_VALUE_SET)
                            {
                                G_VALUE_LCOPY (&priv->basic_props[i].value,
                                        va_args, 0, &err);
                                if (err)
                                    g_free (err);
                                goto next;
                            }
                        }
                        else
                            g_rw_lock_reader_lock (&priv->props_lock);
                        *has_value = DONNA_NODE_VALUE_ERROR;
                    }
                    gpointer ptr;
                    ptr = va_arg (va_args, gpointer);
                }
                goto next;
            }
        }

        /* other properties */
        prop = g_hash_table_lookup (props, (gpointer) name);

        if (!prop)
        {
            gpointer ptr;

            *has_value = DONNA_NODE_VALUE_NONE;
            ptr = va_arg (va_args, gpointer);
        }
        else if (!prop->has_value)
        {
            gpointer ptr;

            if (is_blocking)
            {
                /* release the lock for refresher */
                g_rw_lock_reader_unlock (&priv->props_lock);
                if (prop->refresher (NULL /* no task */, node, name))
                {
                    g_rw_lock_reader_lock (&priv->props_lock);
                    /* check if the value has actually been set. We can still
                     * use prop because the property cannot be removed, so the
                     * reference is still valid. */
                    if (prop->has_value)
                    {
                        *has_value = DONNA_NODE_VALUE_SET;
                        G_VALUE_LCOPY (&prop->value, va_args, 0, &err);
                        if (err)
                            g_free (err);
                        goto next;
                    }
                }
                else
                    g_rw_lock_reader_lock (&priv->props_lock);
                *has_value = DONNA_NODE_VALUE_ERROR;
            }
            else
                *has_value = DONNA_NODE_VALUE_NEED_REFRESH;
            ptr = va_arg (va_args, gpointer);
        }
        else /* prop->has_value == TRUE */
        {
            *has_value = DONNA_NODE_VALUE_SET;
            G_VALUE_LCOPY (&prop->value, va_args, 0, &err);
            if (err)
                g_free (err);
        }
next:
        name = va_arg (va_args, gchar *);
    }
    g_rw_lock_reader_unlock (&priv->props_lock);
}

void
donna_node_get (DonnaNode   *node,
                gboolean     is_blocking,
                const gchar *first_name,
                ...)
{
    va_list va_args;

    g_return_if_fail (DONNA_IS_NODE (node));

    va_start (va_args, first_name);
    get_valist (node, is_blocking, first_name, va_args);
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

        /* only call the refresher if the prop hasn't already been refreshed */
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

        if (!prop->refresher (task, data->node, prop->name))
            ret = DONNA_TASK_FAILED;
    }

    if (donna_task_is_cancelling (task))
        ret = DONNA_TASK_CANCELLED;

    /* disconnect our handler -- any signal that we care about would have come
     * from the refresher, so in this thread, so it would have been processed. */
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
         * In the odd chance that the refresher for prop1 failed (returned
         * FALSE) but e.g. the getter for prop2 did take care of both prop1 &
         * prop2 */
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
        value = donna_task_grab_return_value (task);
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

static void
free_refresh_data (struct refresh_data *data)
{
    g_object_unref (data->node);
    g_ptr_array_free (data->names, TRUE);
    g_free (g_ptr_array_free (data->refreshed, FALSE));
    g_slice_free (struct refresh_data, data);
}

DonnaTask *
donna_node_refresh_task (DonnaNode   *node,
                         const gchar *first_name,
                         ...)
{
    GPtrArray           *names;
    struct refresh_data *data;

    g_return_val_if_fail (DONNA_IS_NODE (node), NULL);

    if (!first_name /* DONNA_NODE_REFRESH_SET_VALUES */
            || streq (first_name, DONNA_NODE_REFRESH_ALL_VALUES))
    {
        GHashTableIter iter;
        gpointer key, value;

        /* we'll send the list of all properties, because node_refresh() needs
         * to know which refresher to call, and it can't have a lock on the hash
         * table since the refresher will call set_property_value which needs to
         * take a writer lock... */

        g_rw_lock_reader_lock (&node->priv->props_lock);
        names = g_ptr_array_sized_new (g_hash_table_size (node->priv->props));
        g_hash_table_iter_init (&iter, node->priv->props);
        while (g_hash_table_iter_next (&iter, &key, &value))
        {
            if (first_name || ((DonnaNodeProp *)value)->has_value)
            {
                value = (gpointer) g_strdup ((gchar *) key);
                g_ptr_array_add (names, value);
            }
        }
        g_rw_lock_reader_unlock (&node->priv->props_lock);
    }
    else
    {
        va_list     va_args;
        gpointer    name;

        names = g_ptr_array_new ();

        va_start (va_args, first_name);
        name = (gpointer) first_name;
        while (name)
        {
            /* TODO: check property exists on node, else fill a GError? */
            name = (gpointer) g_strdup (name);
            g_ptr_array_add (names, name);
            name = va_arg (va_args, gpointer);
        }
        va_end (va_args);
    }

    data = g_slice_new0 (struct refresh_data);
    data->node = g_object_ref (node);
    data->names = names;

    return donna_task_new (node_refresh, data, (GDestroyNotify) free_refresh_data);
}



DonnaTask *     donna_node_set_property_task    (DonnaNode          *node,
                                                 const gchar        *name,
                                                 GValue             *value,
                                                 GError            **error);
void            donna_node_set_property_value   (DonnaNode          *node,
                                                 const gchar        *name,
                                                 GValue             *value);
int             donna_node_inc_toggle_count (DonnaNode              *node);
int             donna_node_dec_toggle_count (DonnaNode              *node);











struct set_property
{
    DonnaNode       *node;
    DonnaNodeProp   *prop;
    GValue          *value;
};

static void
free_set_property (struct set_property *data)
{
    g_object_unref (data->node);
    g_value_unset (data->value);
    g_slice_free (GValue, data->value);
    g_slice_free (struct set_property, data);
}

static DonnaTaskState
set_property (DonnaTask *task, struct set_property *data)
{
    GValue value = G_VALUE_INIT;
    DonnaTaskState ret;

    ret = data->prop->setter (task, data->node, data->prop->name,
            (const GValue *) data->value);

    /* set the return value */
    g_value_init (&value, G_TYPE_BOOLEAN);
    g_value_set_boolean (&value, (ret == DONNA_TASK_DONE));
    donna_task_set_return_value (task, &value);
    g_value_unset (&value);

    free_set_property (data);

    return ret;
}

DonnaTask *
donna_node_set_property (DonnaNode        *node,
                         const gchar      *name,
                         GValue           *value,
                         task_callback_fn  callback,
                         gpointer          callback_data,
                         GDestroyNotify    callback_destroy,
                         guint             timeout,
                         task_timeout_fn   timeout_fn,
                         gpointer          timeout_data,
                         GDestroyNotify    timeout_destroy,
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
            (task_fn) set_property, data, (GDestroyNotify) free_set_property,
            callback, callback_data, callback_destroy,
            timeout, timeout_fn, timeout_data, timeout_destroy);

    return task;
}

void
donna_node_set_property_value (DonnaNode     *node,
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
    if (has_old_value)
        g_value_unset (&old_value);
}

int
donna_node_inc_toggle_count (DonnaNode *node)
{
    g_return_val_if_fail (DONNA_IS_NODE (node), -1);
    return ++node->priv->toggle_count;
}

int
donna_node_dec_toggle_count (DonnaNode *node)
{
    g_return_val_if_fail (DONNA_IS_NODE (node), -1);
    return --node->priv->toggle_count;
}
