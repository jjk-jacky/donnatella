
#include <glib-object.h>
#include <gobject/gvaluecollector.h>    /* G_VALUE_LCOPY */
#include <string.h>                     /* memset() */
#include "node.h"
#include "provider.h"                   /* donna_provider_node_updated() */
#include "task.h"
#include "util.h"
#include "macros.h"                     /* streq() */

const gchar *node_basic_properties[] =
{
    "provider",
    "domain",
    "location",
    "node-type",
    "name",
    "icon",
    "full-name",
    "size",
    "ctime",
    "mtime",
    "atime",
    "perms",
    "user",
    "group",
    "type",
    NULL
};

/* index of the first basic prop in node_basic_properties; i.e. after the
 * internal (e.g. provider) and required (e.g. name) ones */
#define FIRST_BASIC_PROP    5
/* we "re-list" basic properties here, to save their values */
enum
{
    BASIC_PROP_ICON = 0,
    BASIC_PROP_FULL_NAME,
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

/* index of the first required prop in node_basic_properties; i.e. after the
 * internal (e.g. provider) ones */
#define FIRST_REQUIRED_PROP 4
/* list the writable flags so we can use them easily */
static DonnaNodeFlags prop_writable_flags[] =
{
    DONNA_NODE_NAME_WRITABLE,
    DONNA_NODE_ICON_WRITABLE,
    DONNA_NODE_FULL_NAME_WRITABLE,
    DONNA_NODE_SIZE_WRITABLE,
    DONNA_NODE_CTIME_WRITABLE,
    DONNA_NODE_MTIME_WRITABLE,
    DONNA_NODE_ATIME_WRITABLE,
    DONNA_NODE_PERMS_WRITABLE,
    DONNA_NODE_USER_WRITABLE,
    DONNA_NODE_GROUP_WRITABLE,
    DONNA_NODE_TYPE_WRITABLE
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

static void free_node_prop (DonnaNodeProp *prop);

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

    priv->props = g_hash_table_new_full (g_str_hash, g_str_equal,
            g_free, (GDestroyNotify) free_node_prop);
    g_rw_lock_init (&priv->props_lock);

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
    for (i = 0; i < NB_BASIC_PROPS; ++i)
        g_value_unset (&priv->basic_props[i].value);
    g_hash_table_destroy (priv->props);
    g_rw_lock_clear (&priv->props_lock);

    G_OBJECT_CLASS (donna_node_parent_class)->finalize (object);
}

/* used to free properties when removed from hash table */
static void
free_node_prop (DonnaNodeProp *prop)
{
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
                DonnaNodeFlags   flags)
{
    DonnaNode *node;
    DonnaNodePrivate *priv;

    g_return_val_if_fail (DONNA_IS_PROVIDER (provider), NULL);
    g_return_val_if_fail (location != NULL, NULL);
    g_return_val_if_fail (node_type != DONNA_NODE_ITEM
            && node_type != DONNA_NODE_CONTAINER
            && node_type != DONNA_NODE_EXTENDED, NULL);
    g_return_val_if_fail (refresher != NULL, NULL);
    g_return_val_if_fail (setter != NULL, NULL);
    g_return_val_if_fail (name != NULL, NULL);

    node = g_object_new (DONNA_TYPE_NODE, NULL);
    priv = node->priv;
    priv->provider  = g_object_ref (provider);
    priv->location  = g_strdup (location);
    priv->node_type = node_type;
    priv->name      = g_strdup (name);
    priv->refresher = refresher;
    priv->setter    = setter;
    priv->flags     = flags;
    /* we want to store which basic prop exists in basic_props as well. We'll
     * use that so we can just loop other basic_props and see which ones exists,
     * etc */
    if (flags & DONNA_NODE_ICON_EXISTS)
        priv->basic_props[BASIC_PROP_ICON].has_value = DONNA_NODE_VALUE_NEED_REFRESH;
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
            sce->priv->name, sce->priv->flags);
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
                         const GValue    *value,
                         refresher_fn     refresher,
                         setter_fn        setter,
                         GError         **error)
{
    DonnaNodePrivate    *priv;
    DonnaNodeProp       *prop;
    const gchar        **s;

    g_return_val_if_fail (DONNA_IS_NODE (node), FALSE);
    g_return_val_if_fail (name != NULL, FALSE);
    /* initial value is optional */
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
    if (value)
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

    priv = node->priv;
    props = priv->props;
    g_rw_lock_reader_lock (&priv->props_lock);
    name = first_name;
    while (name)
    {
        DonnaNodeProp     *prop;
        DonnaNodeHasValue *has_value;
        GValue *value;
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
            *s = (gchar *) donna_provider_get_domain (priv->provider);
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

        has_value = va_arg (va_args, DonnaNodeHasValue *);

        /* basic properties: might not have a value, so there's a has_value */
        i = 0;
        for (s = (gchar **) &node_basic_properties[FIRST_BASIC_PROP]; *s; ++s, ++i)
        {
            if (streq (name, *s))
            {
                *has_value = priv->basic_props[i].has_value;
                if (*has_value == DONNA_NODE_VALUE_SET)
                {
grab_basic_value:
                    G_VALUE_LCOPY (&priv->basic_props[i].value, va_args, 0, &err);
                    if (err)
                    {
                        g_warning (
                                "Error while trying to copy value of basic property '%s' from node '%s:%s': %s",
                                name,
                                donna_provider_get_domain (priv->provider),
                                priv->location,
                                err);
                        g_free (err);
                    }
                    goto next;
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
                                goto grab_basic_value;
                        }
                        else
                            g_rw_lock_reader_lock (&priv->props_lock);
                        *has_value = DONNA_NODE_VALUE_ERROR;
                    }
                    va_arg (va_args, gpointer);
                }
                goto next;
            }
        }

        /* other properties */
        value = va_arg (va_args, GValue *);
        prop = g_hash_table_lookup (props, (gpointer) name);

        if (!prop)
            *has_value = DONNA_NODE_VALUE_NONE;
        else if (!prop->has_value)
        {
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
                        g_value_init (value, G_VALUE_TYPE (&prop->value));
                        g_value_copy (&prop->value, value);
                        goto next;
                    }
                }
                else
                    g_rw_lock_reader_lock (&priv->props_lock);
                *has_value = DONNA_NODE_VALUE_ERROR;
            }
            else
                *has_value = DONNA_NODE_VALUE_NEED_REFRESH;
        }
        else /* prop->has_value == TRUE */
        {
            *has_value = DONNA_NODE_VALUE_SET;
            g_value_init (value, G_VALUE_TYPE (&prop->value));
            g_value_copy (&prop->value, value);
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
node_refresh (DonnaTask *task, struct refresh_data *data)
{
    DonnaNodePrivate    *priv;
    GHashTable          *props;
    gulong               sig;
    GPtrArray           *names;
    GPtrArray           *refreshed;
    guint                i;
    DonnaTaskState       ret;
    GValue              *value;

    priv = data->node->priv;
    names = data->names;
    refreshed = data->refreshed = g_ptr_array_sized_new (names->len);
    ret = DONNA_TASK_DONE;

    /* connect to the provider's signal, so we know which properties are
     * actually refreshed */
    sig = g_signal_connect (priv->provider, "node-updated",
            G_CALLBACK (node_updated_cb), data);

    props = priv->props;
    for (i = 0; i < names->len; ++i)
    {
        DonnaNodeProp   *prop;
        refresher_fn     refresher;
        guint            j;
        gboolean         done;
        gchar          **s;

        if (donna_task_is_cancelling (task))
        {
            ret = DONNA_TASK_CANCELLED;
            break;
        }

        refresher = NULL;

        /* basic properties. We skip internal ones (provider, domain, location,
         * node-type) since they can't be refreshed (should never be needed
         * anyway) */
        j = 0;
        for (s = (gchar **) &node_basic_properties[FIRST_REQUIRED_PROP]; *s; ++s, ++j)
        {
            if (streq (names->pdata[i], *s))
            {
                refresher = priv->refresher;
                break;
            }
        }

        if (!refresher)
        {
            /* look for other properties then */
            g_rw_lock_reader_lock (&priv->props_lock);
            prop = g_hash_table_lookup (props, names->pdata[i]);
            g_rw_lock_reader_unlock (&priv->props_lock);
            if (prop)
                refresher = prop->refresher;
        }

        if (!refresher)
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

        if (!refresher (task, data->node, names->pdata[i]))
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

    if (!first_name /* == DONNA_NODE_REFRESH_SET_VALUES */
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
            /* TODO: check property exists on node, else g_warning ? */
            name = (gpointer) g_strdup (name);
            g_ptr_array_add (names, name);
            name = va_arg (va_args, gpointer);
        }
        va_end (va_args);
    }

    data = g_slice_new0 (struct refresh_data);
    data->node = g_object_ref (node);
    data->names = names;

    return donna_task_new ((task_fn) node_refresh, data,
            (GDestroyNotify) free_refresh_data);
}

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
    /* no refresher == "fake" DonnaNodeProp for a basic property */
    if (!data->prop->refresher)
        g_slice_free (DonnaNodeProp, data->prop);
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
donna_node_set_property_task (DonnaNode     *node,
                              const gchar   *name,
                              GValue        *value,
                              GError       **error)
{
    DonnaNodePrivate *priv;
    DonnaNodeProp *prop;
    struct set_property *data;
    const gchar **s;
    gint i;

    g_return_val_if_fail (DONNA_IS_NODE (node), FALSE);
    g_return_val_if_fail (name != NULL, FALSE);
    g_return_val_if_fail (value != NULL, FALSE);
    priv = node->priv;
    prop = NULL;

    /* internal properties cannot be set */
    if (streq (name, "provider") || streq (name, "domain")
            || streq (name, "location") || streq (name, "node-type"))
    {
        g_set_error (error, DONNA_NODE_ERROR, DONNA_NODE_ERROR_READ_ONLY,
                "Internal property %s on node cannot be set", name);
        return NULL;
    }

    /* if it's a basic properties, check it can be set */
    for (s = &node_basic_properties[FIRST_REQUIRED_PROP], i = 0; *s; ++s, ++i)
    {
        if (streq (name, *s))
        {
            if (!(priv->flags & prop_writable_flags[i]))
            {
                /* TODO: check if the property exists on the node? */
                g_set_error (error, DONNA_NODE_ERROR, DONNA_NODE_ERROR_READ_ONLY,
                        "Property %s on node cannot be set", name);
                return NULL;
            }

            /* NAME */
            if (i == 0)
            {
                if (!G_VALUE_HOLDS (value, G_TYPE_STRING))
                {
                    g_set_error (error, DONNA_NODE_ERROR, DONNA_NODE_ERROR_INVALID_TYPE,
                            "Property %s on node is of type %s, value passed is %s",
                            name,
                            g_type_name (G_TYPE_STRING),
                            g_type_name (G_VALUE_TYPE (value)));
                    return NULL;
                }
            }
            else
            {
                /* basic_props[i - 1] */
                --i;
                if (!G_VALUE_HOLDS (value, G_VALUE_TYPE (&(priv->basic_props[i].value))))
                {
                    g_set_error (error, DONNA_NODE_ERROR, DONNA_NODE_ERROR_INVALID_TYPE,
                            "Property %s on node is of type %s, value passed is %s",
                            name,
                            g_type_name (G_VALUE_TYPE (&priv->basic_props[i].value)),
                            g_type_name (G_VALUE_TYPE (value)));
                    return NULL;
                }
            }

            /* let's create a "fake" DonnaNodeProp for the task */
            prop = g_slice_new0 (DonnaNodeProp);
            /* *s isn't going anywhere */
            prop->name = *s;
            /* this will indicate it's a "fake" one, and must be free-d */
            prop->refresher = NULL;
            /* this (alongside name) is what will be used */
            prop->setter = priv->setter;
            break;
        }
    }

    if (!prop)
    {
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

        if (!prop->setter)
        {
            g_set_error (error, DONNA_NODE_ERROR, DONNA_NODE_ERROR_READ_ONLY,
                    "Property %s on node cannot be set", name);
            return NULL;
        }

        if (!G_VALUE_HOLDS (value, G_VALUE_TYPE (&prop->value)))
        {
            g_set_error (error, DONNA_NODE_ERROR, DONNA_NODE_ERROR_INVALID_TYPE,
                    "Property %s on node is of type %s, value passed is %s",
                    name,
                    g_type_name (G_VALUE_TYPE (&prop->value)),
                    g_type_name (G_VALUE_TYPE (value)));
            return NULL;
        }
    }

    data = g_slice_new (struct set_property);
    /* take a ref on node, for the task */
    data->node = g_object_ref (node);
    data->prop = prop;
    data->value = duplicate_gvalue (value);
    return donna_task_new ((task_fn) set_property, data,
            (GDestroyNotify) free_set_property);
}

void
donna_node_set_property_value (DonnaNode     *node,
                               const gchar   *name,
                               const GValue  *value)
{
    DonnaNodeProp *prop;
    const gchar **s;
    gint i;
    gboolean emit = FALSE;

    g_return_if_fail (DONNA_IS_NODE (node));
    g_return_if_fail (name != NULL);

    g_rw_lock_writer_lock (&node->priv->props_lock);

    /* name? */
    if (streq (name, "name"))
    {
        g_free (node->priv->name);
        node->priv->name = g_strdup (g_value_get_string (value));
        emit = TRUE;
        goto finish;
    }

    /* basic prop? */
    for (s = &node_basic_properties[FIRST_BASIC_PROP], i = 0; *s; ++s, ++i)
    {
        if (streq (name, *s))
        {
            /* copy the new value over */
            g_value_copy (value, &node->priv->basic_props[i].value);
            /* we assume it worked, w/out checking types, etc because this should
             * only be used by providers and such, on properties they are handling,
             * so if they get it wrong, they're seriously bugged */
            emit = TRUE;
            goto finish;
        }
    }

    /* other prop? */
    prop = g_hash_table_lookup (node->priv->props, (gpointer) name);
    if (prop)
    {
        /* copy the new value over */
        g_value_copy (value, &(prop->value));
        /* we assume it worked, w/out checking types, etc because this should
         * only be used by providers and such, on properties they are handling,
         * so if they get it wrong, they're seriously bugged */
        prop->has_value = TRUE;
        emit = TRUE;
    }

finish:
    g_rw_lock_writer_unlock (&node->priv->props_lock);

    if (emit)
        donna_provider_node_updated (node->priv->provider, node, name);
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
