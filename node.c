
#include <glib-object.h>
#include <gobject/gvaluecollector.h>    /* G_VALUE_LCOPY */
#include <string.h>                     /* memset() */
#include "node.h"
#include "debug.h"
#include "provider.h"                   /* donna_provider_node_updated() */
#include "task.h"
#include "util.h"
#include "macros.h"                     /* streq() */

/**
 * SECTION:node
 * @Short_description: An object holding dynamic properties
 * @See_also: #DonnaProvider #DonnaTask
 *
 * A #DonnaNode represents an "item" from a domain/#DonnaProvider (e.g. a file
 * in the filesystem).
 * They should only be created by their #DonnaProvider and will be used
 * throughout the application to show/act on the item they represent.
 *
 * Unlike usual #GObject they use a system of "dynamic" properties, because not
 * every node has the same properties, plugins could add new properties to
 * specific nodes, etc
 *
 * For each node there is a set of basic properties, amongst which are the
 * required properties. In addition, there could be additional properties.
 *
 * - Required properties exists in all nodes, their values are always available
 *   directly.
 * - Basic properties might not always exist, and if they do their values might
 *   not be available directly (i.e. needing a refresh). However, their types
 *   are known.
 * - Additional properties might not always exists, their values might need a
 *   refresh, and their types aren't known (i.e. #GValue will be used).
 *
 * Required & basic properties are owned by the #DonnaProvider of the node,
 * additional properties can come from elsewhere (e.g. plugins).
 *
 * Basic properties are:
 *
 * - provider: (required): the #DonnaProvider of the node
 * - domain: (required): the domain of the provider (const gchar *)
 * - location: (required): the location of the node, unique string to identify
 *   it within its domain (e.g. full path/name for a file) (gchar *)
 * - node-type: (required): the #DonnaNodeType of the node
 * - filename (required): filename, in the filename encoding (gchar *)
 * - name: (required): the name of the item (gchar *)
 * - icon: pointer to a #GdkPixbuf of the item's icon
 * - full-name: the name of the item (e.g. /full/path/to/file -- often times
 *   will be the same as location) (gchar *)
 * - size: the size of the item (guint64)
 * - ctime: the ctime of the item (guint64)
 * - mtime: the mtime of the item (guint64)
 * - atime: the atime of the item (guint64)
 * - mode: the mode (type&perms) of the item (guint)
 * - uid: the user id of the item (guint)
 * - gid: the group id of the item (guint)
 * - desc: the desc of the item (gchar *)
 *
 * provider, domain, location, node-type and filename are all read-only. Every
 * other property might be writable.
 *
 * Properties might not have a value "loaded", i.e. they need a refresh. This is
 * so that if getting a property needs work, it can only be done if/when needed.
 *
 * You can see if a node has a property or not, and if so if it has a value (or
 * needs a refresh) and/or is writable using donna_node_has_property()
 *
 * You use donna_node_get() to access the properties of a node. For each
 * property name you specify the location of a #DonnaNodeHasValue, to known
 * whether the property exists and has a value on the node, and that of a
 * #GValue.
 * It is possible to ask that properties without a value
 * (%DONNA_NODE_VALUE_NEED_REFRESH) are automatically refreshed (in/blocking the
 * current thread).
 *
 * As always, for possibly slow/blocking operations you use a function that
 * returns a #DonnaTask to perform the operation (usually in another thread).
 * This is the case to refresh properties, done using donna_node_refresh_task()
 * or donna_node_refresh_arr_task() -- the former takes the properties names as
 * arguments, the later expects them in a #GPtrArray of strings.
 *
 * Helpers (such as donna_node_get_name()) allow you to quickly get
 * required/basic properties. Those are faster than using donna_node_get() and
 * can be especially useful in frequent operations (e.g. in columntypes, when
 * rendering/sorting)
 *
 * Property filename is an internal property returning the filename in the GLib
 * filename encoding. You're likely never to have to use it, as the node's
 * provider should handle all actual file operations. (E.g. to rename a file,
 * simply change its property name, encoded in UTF8 of course.)
 * If filename is set to %NULL then location will be used instead; Thus allowing
 * to only store the filename once if filename encoding is UTF8.
 *
 * To change the value of a property, use donna_node_set_property_task()
 *
 * Nodes do not have signals, any and all relevent signal for a node will occur
 * on its #DonnaProvider instead. For this reason, anyone who needs to work on a
 * node should first connect to the relevent signals on its provider first.
 * This is done to need to only connect to one signal even for hundreds of
 * nodes.
 *
 * For providers:
 *
 * You create a new node using donna_node_new() or donna_node_new_from_node();
 * The later allows you to create a new node based an existing node (usually
 * from a different provider.
 *
 * The refresher and setter functions will be used for all (existing) basic
 * properties. Additional properties can be added using
 * donna_node_add_property(), which can be done by using the provider's signal
 * #DonnaProvider::new-node, emitted upon node creation for this purpose.
 *
 * Only the owner of a property should use donna_node_set_property_value() when
 * such a change has effectively been observed on the item it represents.
 */

const gchar *node_basic_properties[] =
{
    "provider",
    "domain",
    "location",
    "node-type",
    "filename",
    "name",
    "icon",
    "full-name",
    "size",
    "ctime",
    "mtime",
    "atime",
    "mode",
    "uid",
    "gid",
    "desc",
    NULL
};

/* index of the first basic prop in node_basic_properties; i.e. after the
 * internal (e.g. provider) and required (e.g. name) ones */
#define FIRST_BASIC_PROP    6
/* we "re-list" basic properties here, to save their values */
enum
{
    BASIC_PROP_ICON = 0,
    BASIC_PROP_FULL_NAME,
    BASIC_PROP_SIZE,
    BASIC_PROP_CTIME,
    BASIC_PROP_MTIME,
    BASIC_PROP_ATIME,
    BASIC_PROP_MODE,
    BASIC_PROP_UID,
    BASIC_PROP_GID,
    BASIC_PROP_DESC,
    NB_BASIC_PROPS
};

/* index of the first required prop in node_basic_properties; i.e. after the
 * internal (e.g. provider) ones */
#define FIRST_REQUIRED_PROP 5
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
    DONNA_NODE_MODE_WRITABLE,
    DONNA_NODE_UID_WRITABLE,
    DONNA_NODE_GID_WRITABLE,
    DONNA_NODE_DESC_WRITABLE
};

struct _DonnaNodePrivate
{
    /* internal properties */
    DonnaProvider     *provider;
    gchar             *location;
    DonnaNodeType      node_type;
    gchar             *filename;
    /* required properties */
    gchar             *name;
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

    g_value_init (&priv->basic_props[BASIC_PROP_ICON].value,      G_TYPE_OBJECT);
    g_value_init (&priv->basic_props[BASIC_PROP_FULL_NAME].value, G_TYPE_STRING);
    g_value_init (&priv->basic_props[BASIC_PROP_SIZE].value,      G_TYPE_UINT64);
    g_value_init (&priv->basic_props[BASIC_PROP_CTIME].value,     G_TYPE_UINT64);
    g_value_init (&priv->basic_props[BASIC_PROP_MTIME].value,     G_TYPE_UINT64);
    g_value_init (&priv->basic_props[BASIC_PROP_ATIME].value,     G_TYPE_UINT64);
    g_value_init (&priv->basic_props[BASIC_PROP_MODE].value,      G_TYPE_UINT);
    g_value_init (&priv->basic_props[BASIC_PROP_UID].value,       G_TYPE_UINT);
    g_value_init (&priv->basic_props[BASIC_PROP_GID].value,       G_TYPE_UINT);
    g_value_init (&priv->basic_props[BASIC_PROP_DESC].value,      G_TYPE_STRING);

    priv->toggle_count = 1;
}

G_DEFINE_TYPE (DonnaNode, donna_node, G_TYPE_OBJECT)

static void
donna_node_finalize (GObject *object)
{
    DonnaNodePrivate *priv;
    gint i;

    priv = DONNA_NODE (object)->priv;
    DONNA_DEBUG (NODE,
        g_debug4 ("Finalizing node '%s:%s'",
            donna_provider_get_domain (priv->provider),
            priv->location));
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

/**
 * donna_node_new:
 * @provider: provider of the node
 * @location: location of the node
 * @node_type: type of node
 * @filename: (allow none): filename of the node (in GLib filename encoding),
 * or %NULL if we can use @name)
 * @refresher: function called to refresh a basic property
 * @setter: function to change value of a basic property
 * @name: name of the node
 * @flags: flags to define which basic properties exists/are writable
 *
 * Creates a new node, according to the specified parameters. This should only
 * be called by the #DonnaProvider of the node.
 *
 * If you need a node to use it, see donna_provider_get_node_task() or
 * donna_app_get_node_task()
 *
 * Returns: (transfer full): The new node
 */
DonnaNode *
donna_node_new (DonnaProvider       *provider,
                const gchar         *location,
                DonnaNodeType        node_type,
                const gchar         *filename,
                refresher_fn         refresher,
                setter_fn            setter,
                const gchar         *name,
                DonnaNodeFlags       flags)
{
    DonnaNode *node;
    DonnaNodePrivate *priv;

    g_return_val_if_fail (DONNA_IS_PROVIDER (provider), NULL);
    g_return_val_if_fail (location != NULL, NULL);
    g_return_val_if_fail (node_type == DONNA_NODE_ITEM
            || node_type == DONNA_NODE_CONTAINER, NULL);
    g_return_val_if_fail (refresher != NULL, NULL);
    if ((flags & DONNA_NODE_ALL_WRITABLE) != 0)
        g_return_val_if_fail (setter != NULL, NULL);
    g_return_val_if_fail (name != NULL, NULL);

    node = g_object_new (DONNA_TYPE_NODE, NULL);
    priv = node->priv;
    priv->provider  = g_object_ref (provider);
    priv->location  = g_strdup (location);
    priv->node_type = node_type;
    if (filename)
        priv->filename = g_strdup (filename);
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
    if (flags & DONNA_NODE_MODE_EXISTS)
        priv->basic_props[BASIC_PROP_MODE].has_value = DONNA_NODE_VALUE_NEED_REFRESH;
    if (flags & DONNA_NODE_UID_EXISTS)
        priv->basic_props[BASIC_PROP_UID].has_value = DONNA_NODE_VALUE_NEED_REFRESH;
    if (flags & DONNA_NODE_GID_EXISTS)
        priv->basic_props[BASIC_PROP_GID].has_value = DONNA_NODE_VALUE_NEED_REFRESH;
    if (flags & DONNA_NODE_DESC_EXISTS)
        priv->basic_props[BASIC_PROP_DESC].has_value = DONNA_NODE_VALUE_NEED_REFRESH;

    return node;
}

/**
 * donna_node_new_from_node:
 * @provider: provider of the node
 * @location: location of the node
 * @sce: source node upon which the node is based
 * @error: (allow-none): return location for error, or %NULL
 *
 * Creates a new node based upon an existing one (from a different provider).
 *
 * The new node will have the specified provider and location, but keep its type
 * as well as the definition of all (basic & additional) properties.
 *
 * This would be useful to e.g. create nodes based on filesystem items, but with
 * a different location so as to show the same item more than once. For example,
 * results of a `grep` could have the same item listed twice, for different
 * lines matching.
 *
 * Like donna_node_new() this should only be called by the node's provider. If
 * you need a node to use it, see donna_provider_get_node_task()
 *
 * Returns: (transfer full): The new node
 */
DonnaNode *
donna_node_new_from_node (DonnaProvider     *provider,
                          const gchar       *location,
                          DonnaNode         *sce,
                          GError           **error)
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
    g_rw_lock_reader_lock (&sce->priv->props_lock);
    node = donna_node_new (provider, location, sce->priv->node_type,
            sce->priv->filename,
            sce->priv->refresher, sce->priv->setter,
            sce->priv->name, sce->priv->flags);
    if (!node)
    {
        g_set_error (error, DONNA_NODE_ERROR, DONNA_NODE_ERROR_OTHER,
                "Failed to create a new node '%s:%s' when trying to make a new node from '%s:%s'",
                donna_provider_get_domain (provider),
                location,
                donna_provider_get_domain (sce->priv->provider),
                sce->priv->location);
        g_rw_lock_reader_unlock (&sce->priv->props_lock);
        return NULL;
    }

    /* and copy over all the (other) properties */
    priv = node->priv;
    props = priv->props;
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

/**
 * donna_node_add_property:
 * @node: The node to add the property to
 * @name: Name of the property
 * @type: type of the property
 * @value: (allow-none): Initial value of the property
 * @refresher: function to be called to refresh the property's value
 * @setter: (allow-none): Function to be called to change the property's value
 * @error: (allow-none): Return location of error (or %NULL)
 *
 * Adds a new additional property of the given node.
 *
 * Returns: %TRUE if the property was added, else %FALSE
 */
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
    DONNA_DEBUG (NODE,
            g_debug2 ("Node '%s:%s': added property '%s'",
                donna_provider_get_domain (priv->provider),
                priv->location,
                name));
    g_rw_lock_writer_unlock (&priv->props_lock);

    return TRUE;
}

/**
 * donna_node_has_property:
 * @node: The node to check for the property
 * @name: The name of the property to check
 *
 * Determines whether a property exists, has a value (or needs a refresh), and
 * is writable on a node.
 *
 * Returns: Flags indicating the state of the property on the node
 */
DonnaNodeHasProp
donna_node_has_property (DonnaNode   *node,
                         const gchar *name)
{
    DonnaNodePrivate *priv;
    const gchar **s;
    guint i;
    DonnaNodeProp *prop;
    DonnaNodeHasProp ret;

    g_return_val_if_fail (DONNA_IS_NODE (node), DONNA_NODE_PROP_UNKNOWN);
    g_return_val_if_fail (name != NULL, DONNA_NODE_PROP_UNKNOWN);

    priv = node->priv;

    for (i = 0, s = node_basic_properties; *s; ++s, ++i)
    {
        if (streq (name, *s))
        {
            /* required; or basic w/ value */
            if (i < FIRST_BASIC_PROP
                    || priv->basic_props[i - FIRST_BASIC_PROP].has_value
                    == DONNA_NODE_VALUE_SET)
                ret = DONNA_NODE_PROP_EXISTS | DONNA_NODE_PROP_HAS_VALUE;
            /* basic w/out value */
            else if (priv->basic_props[i - FIRST_BASIC_PROP].has_value
                    == DONNA_NODE_VALUE_NEED_REFRESH)
                ret = DONNA_NODE_PROP_EXISTS;
            /* basic that doesn't exist */
            else
                return DONNA_NODE_PROP_NONE;

            /* internal props are not writable, for the rest we check the flags */
            if (i >= FIRST_REQUIRED_PROP
                    && priv->flags & prop_writable_flags[i - FIRST_REQUIRED_PROP])
                    ret |= DONNA_NODE_PROP_WRITABLE;

            return ret;
        }
    }

    g_rw_lock_reader_lock (&priv->props_lock);
    prop = g_hash_table_lookup (priv->props, name);
    g_rw_lock_reader_unlock (&priv->props_lock);
    if (prop)
    {
        ret = DONNA_NODE_PROP_EXISTS;
        if (prop->has_value)
            ret |= DONNA_NODE_PROP_HAS_VALUE;
        if (prop->setter)
            ret |= DONNA_NODE_PROP_WRITABLE;
    }
    else
        ret = DONNA_NODE_PROP_NONE;

    return ret;
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

        has_value = va_arg (va_args, DonnaNodeHasValue *);
        value = va_arg (va_args, GValue *);

        /* internal/required properties (there's always a value) */
        if (streq (name, "provider"))
        {
            *has_value = DONNA_NODE_VALUE_SET;
            g_value_init (value, G_TYPE_OBJECT);
            g_value_set_object (value, priv->provider);
            goto next;
        }
        else if (streq (name, "domain"))
        {
            *has_value = DONNA_NODE_VALUE_SET;
            g_value_init (value, G_TYPE_STRING);
            g_value_set_static_string (value,
                    donna_provider_get_domain (priv->provider));
            goto next;
        }
        else if (streq (name, "location"))
        {
            *has_value = DONNA_NODE_VALUE_SET;
            g_value_init (value, G_TYPE_STRING);
            g_value_set_string (value, priv->location);
            goto next;
        }
        else if (streq (name, "node-type"))
        {
            *has_value = DONNA_NODE_VALUE_SET;
            g_value_init (value, G_TYPE_INT);
            g_value_set_int (value, priv->node_type);
            goto next;
        }
        else if (streq (name, "filename"))
        {
            *has_value = DONNA_NODE_VALUE_SET;
            g_value_init (value, G_TYPE_STRING);
            g_value_set_string (value,
                    (priv->filename) ? priv->filename : priv->location);
            goto next;
        }
        else if (streq (name, "name"))
        {
            *has_value = DONNA_NODE_VALUE_SET;
            g_value_init (value, G_TYPE_STRING);
            g_value_set_string (value, priv->name);
            goto next;
        }

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
                    g_value_init (value, G_VALUE_TYPE (&priv->basic_props[i].value));
                    g_value_copy (&priv->basic_props[i].value, value);
                    goto next;
                }
                else
                {
                    if (*has_value == DONNA_NODE_VALUE_NEED_REFRESH && is_blocking)
                    {
                        DONNA_DEBUG (NODE,
                                g_debug2 ("node_get() for '%s:%s': refreshing %s",
                                    donna_provider_get_domain (priv->provider),
                                    priv->location,
                                    name));
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
                }
                goto next;
            }
        }

        /* other properties */
        prop = g_hash_table_lookup (props, (gpointer) name);
        if (!prop)
            *has_value = DONNA_NODE_VALUE_NONE;
        else if (!prop->has_value)
        {
            if (is_blocking)
            {
                DONNA_DEBUG (NODE,
                        g_debug2 ("node_get() for '%s:%s': refreshing %s",
                            donna_provider_get_domain (priv->provider),
                            priv->location,
                            name));
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

/**
 * donna_node_get:
 * @node: Node to get property values from
 * @is_blocking: Whether to refresh properties needing it or not
 * @first_name: First name of the properties to get
 *
 * Get the value of specified properties, if possible. Each property name should
 * be followed by two parameters: the location of a #DonnaNodeHasValue variable,
 * to know whether the property exists/has a value, and that of a #GValue, to
 * hold the value. E.g:
 * <programlisting>
 * DonnaNodeHasValue has;
 * GValue value = G_VALUE_INIT;
 * donna_node_get (node, FALSE, "some_property", &has, &value, NULL);
 * </programlisting>
 *
 * If @is_blocking is %FALSE the #DonnaHasValue might be
 * %DONNA_NODE_VALUE_NEED_REFRESH while with %TRUE a refresh will be
 * automatically called (within/blocking the thread). It can then also be set
 * to %DONNA_NODE_VALUE_ERROR if the refresher failed.
 *
 * Note that for required & basic properties, it is faster (and might be
 * simpler) to use the available helpers, e.g. donna_node_get_name()
 */
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

/**
 * donna_node_get_provider:
 * @node: Node to get the provider of
 *
 * Helper to quickly get the provider of @node
 * Free it with g_object_unref() when done.
 *
 * If you don't need to take a reference on the provider, see
 * donna_node_peek_provider()
 *
 * Returns: (transfer full): #DonnaProvider of @node
 */
DonnaProvider *
donna_node_get_provider (DonnaNode *node)
{
    g_return_val_if_fail (DONNA_IS_NODE (node), NULL);
    return g_object_ref (node->priv->provider);
}

/**
 * donna_node_peek_provider:
 * @node: Node to get the provider of
 *
 * Helper to quickly get the provider of @node
 * No reference will be added on the provider, so you shouldn't call
 * g_object_unref() on it. If you need to take a reference on the provider, see
 * donna_node_get_provider()
 *
 * Using this saves the need to ref/unref the provider, and is safe as long as
 * you have a reference on the node (since it has a reference on its provider).
 *
 * Returns: (transfer none): #DonnaProvider of @node
 */
DonnaProvider *
donna_node_peek_provider (DonnaNode *node)
{
    g_return_val_if_fail (DONNA_IS_NODE (node), NULL);
    return node->priv->provider;
}

/**
 * donna_node_get_domain:
 * @node: Node to get the provider's domain of
 *
 * Helper to quickly get the domain of the provider of @node
 *
 * Returns: (transfer none): Domain of the node's provider
 */
const gchar *
donna_node_get_domain (DonnaNode *node)
{
    g_return_val_if_fail (DONNA_IS_NODE (node), NULL);
    return donna_provider_get_domain (node->priv->provider);
}

/**
 * donna_node_get_location:
 * @node: Node to get the location of
 *
 * Helper to quickly get the location of @node
 * Free it with g_free() when done.
 *
 * Returns: (transfer full): Location of @node
 */
gchar *
donna_node_get_location (DonnaNode *node)
{
    DonnaNodePrivate *priv;
    gchar *location;

    g_return_val_if_fail (DONNA_IS_NODE (node), NULL);
    priv = node->priv;
    g_rw_lock_reader_lock (&priv->props_lock);
    location = g_strdup (priv->location);
    g_rw_lock_reader_unlock (&priv->props_lock);
    return location;
}

/**
 * donna_node_get_full_location:
 * @node: Node to get the full location of
 *
 * Helper to quickly get the full location of @node, that is the location
 * prefixed with the domain and ':' (e.g: "fs:/home")
 * Free it with g_free() when done.
 *
 * Returns: (transfer full): Full location of @node
 */
gchar *
donna_node_get_full_location (DonnaNode *node)
{
    DonnaNodePrivate *priv;
    const gchar *domain;
    size_t len_d;
    size_t len_l;
    gchar *fl;

    g_return_val_if_fail (DONNA_IS_NODE (node), NULL);
    priv = node->priv;
    g_rw_lock_reader_lock (&priv->props_lock);
    domain = donna_provider_get_domain (priv->provider);
    len_d = strlen (domain);
    len_l = strlen (priv->location);
    fl = g_new (gchar, len_d + len_l + 2); /* +2: ':' and NUL */
    memcpy (fl, domain, sizeof (gchar) * len_d);
    fl[len_d] = ':';
    memcpy (fl + len_d + 1 /* NUL */, priv->location, sizeof (gchar) * ++len_l);
    g_rw_lock_reader_unlock (&priv->props_lock);
    return fl;
}

/**
 * donna_node_get_node_type:
 * @node: Node to get the node-type of
 *
 * Helper to quickly get the type of @node
 *
 * Returns: #DonnanodeType of @node
 */
DonnaNodeType
donna_node_get_node_type (DonnaNode *node)
{
    g_return_val_if_fail (DONNA_IS_NODE (node), 0);
    return node->priv->node_type;
}

/**
 * donna_node_get_filename:
 * @node: Node to get the property filename of
 *
 * Helper to quickly get the property filename of @node
 * Free it with g_free() when done.
 *
 * Returns: (transfer full): Filename of @node (in GLib filename encoding)
 */
gchar *
donna_node_get_filename (DonnaNode *node)
{
    DonnaNodePrivate *priv;
    gchar *filename;

    g_return_val_if_fail (DONNA_IS_NODE (node), NULL);
    priv = node->priv;
    g_rw_lock_reader_lock (&priv->props_lock);
    filename = g_strdup ((priv->filename) ? priv->filename : priv->location);
    g_rw_lock_reader_unlock (&priv->props_lock);
    return filename;
}

/**
 * donna_node_get_name:
 * @node: Node to get the property name of
 *
 * Helper to quickly get the property name of @node
 * Free it with g_free() when done.
 *
 * Returns: (transfer full): Name of @node
 */
gchar *
donna_node_get_name (DonnaNode *node)
{
    DonnaNodePrivate *priv;
    gchar *name;

    g_return_val_if_fail (DONNA_IS_NODE (node), NULL);
    priv = node->priv;
    g_rw_lock_reader_lock (&priv->props_lock);
    name = g_strdup (node->priv->name);
    g_rw_lock_reader_unlock (&priv->props_lock);
    return name;
}

typedef guint64 (*value_dup_fn) (const GValue *value);

static DonnaNodeHasValue
get_basic_prop (DonnaNode   *node,
                gboolean     is_blocking,
                gint         basic_id,
                guint64     *dest,
                value_dup_fn dup_value)
{
    DonnaNodePrivate *priv = node->priv;
    DonnaNodeHasValue has_value;

    g_rw_lock_reader_lock (&priv->props_lock);
    has_value = priv->basic_props[basic_id].has_value;
    if (has_value == DONNA_NODE_VALUE_SET)
    {
grab_basic_value:
        *dest = dup_value (&priv->basic_props[basic_id].value);
    }
    else if (has_value == DONNA_NODE_VALUE_NEED_REFRESH && is_blocking)
    {
        DONNA_DEBUG (NODE,
                g_debug2 ("node_get_*() for '%s:%s': refreshing %s",
                    donna_provider_get_domain (priv->provider),
                    priv->location,
                    node_basic_properties[FIRST_BASIC_PROP + basic_id]));
        /* we need to release the lock, since the refresher
         * should call set_property_value, hence need a writer
         * lock */
        g_rw_lock_reader_unlock (&priv->props_lock);
        if (priv->refresher (NULL /* no task */, node,
                    node_basic_properties[FIRST_BASIC_PROP + basic_id]))
        {
            g_rw_lock_reader_lock (&priv->props_lock);
            /* check if the value has actually been set */
            has_value = priv->basic_props[basic_id].has_value;
            if (has_value == DONNA_NODE_VALUE_SET)
                goto grab_basic_value;
        }
        else
            g_rw_lock_reader_lock (&priv->props_lock);
        has_value = DONNA_NODE_VALUE_ERROR;
    }
    g_rw_lock_reader_unlock (&priv->props_lock);

    return has_value;
}
#define _get_basic_prop(BASIC_PROP, get_fn, type, var) do { \
    DonnaNodeHasValue has;                                  \
    guint64 value;                                          \
    has = get_basic_prop (node, is_blocking, BASIC_PROP,    \
            &value, (value_dup_fn) get_fn);                 \
    *var = (type) value;                                    \
    return has;                                             \
} while (0)

/**
 * donna_node_get_icon:
 * @node: Node to get the icon from
 * @is_blocking: Whether to block and refresh if needed
 * @icon: Return location for @node's icon
 *
 * Helper to quickly get the property icon of @node
 * Free it with g_object_unref() when done.
 *
 * If @is_blocking is %FALSE it might return %DONNA_NODE_VALUE_NEED_REFRESH
 * while with %TRUE a refresh will automatically be called (within/blocking the
 * thread). It can then also be set to %DONNA_NODE_VALUE_ERROR if the refresher
 * failed.
 *
 * Returns: Whether the value was set, needs a refresh or doesn't exists
 */
DonnaNodeHasValue
donna_node_get_icon (DonnaNode  *node,
                     gboolean    is_blocking,
                     GdkPixbuf **icon)
{
    _get_basic_prop (BASIC_PROP_ICON, g_value_dup_object, GdkPixbuf *, icon);
}

/**
 * donna_node_get_full_name:
 * @node: Node to get the full name from
 * @is_blocking: Whether to block and refresh if needed
 * @full_name: Return location for @node's full name
 *
 * Helper to quickly get the property full-name of @node
 * Free it with g_free() when done.
 *
 * If @is_blocking is %FALSE it might return %DONNA_NODE_VALUE_NEED_REFRESH
 * while with %TRUE a refresh will automatically be called (within/blocking the
 * thread). It can then also be set to %DONNA_NODE_VALUE_ERROR if the refresher
 * failed.
 *
 * Returns: Whether the value was set, needs a refresh or doesn't exists
 */
DonnaNodeHasValue
donna_node_get_full_name (DonnaNode  *node,
                          gboolean    is_blocking,
                          gchar     **full_name)
{
    _get_basic_prop (BASIC_PROP_FULL_NAME, g_value_dup_string, gchar *, full_name);
}

/**
 * donna_node_get_size:
 * @node: Node to get the size from
 * @is_blocking: Whether to block and refresh if needed
 * @size: Return location for @node's size
 *
 * Helper to quickly get the property size of @node
 *
 * If @is_blocking is %FALSE it might return %DONNA_NODE_VALUE_NEED_REFRESH
 * while with %TRUE a refresh will automatically be called (within/blocking the
 * thread). It can then also be set to %DONNA_NODE_VALUE_ERROR if the refresher
 * failed.
 *
 * Returns: Whether the value was set, needs a refresh or doesn't exists
 */
DonnaNodeHasValue
donna_node_get_size (DonnaNode  *node,
                     gboolean    is_blocking,
                     guint64    *size)
{
    _get_basic_prop (BASIC_PROP_SIZE, g_value_get_uint64, guint64, size);
}

/**
 * donna_node_get_ctime:
 * @node: Node to get the ctime from
 * @is_blocking: Whether to block and refresh if needed
 * @ctime: Return location for @node's ctime
 *
 * Helper to quickly get the property ctime of @node
 *
 * If @is_blocking is %FALSE it might return %DONNA_NODE_VALUE_NEED_REFRESH
 * while with %TRUE a refresh will automatically be called (within/blocking the
 * thread). It can then also be set to %DONNA_NODE_VALUE_ERROR if the refresher
 * failed.
 *
 * Returns: Whether the value was set, needs a refresh or doesn't exists
 */
DonnaNodeHasValue
donna_node_get_ctime (DonnaNode *node,
                      gboolean   is_blocking,
                      guint64   *ctime)
{
    _get_basic_prop (BASIC_PROP_CTIME, g_value_get_uint64, guint64, ctime);
}

/**
 * donna_node_get_mtime:
 * @node: Node to get the mtime from
 * @is_blocking: Whether to block and refresh if needed
 * @mtime: Return location for @node's mtime
 *
 * Helper to quickly get the property mtime of @node
 *
 * If @is_blocking is %FALSE it might return %DONNA_NODE_VALUE_NEED_REFRESH
 * while with %TRUE a refresh will automatically be called (within/blocking the
 * thread). It can then also be set to %DONNA_NODE_VALUE_ERROR if the refresher
 * failed.
 *
 * Returns: Whether the value was set, needs a refresh or doesn't exists
 */
DonnaNodeHasValue
donna_node_get_mtime (DonnaNode *node,
                      gboolean   is_blocking,
                      guint64   *mtime)
{
    _get_basic_prop (BASIC_PROP_MTIME, g_value_get_uint64, guint64, mtime);
}

/**
 * donna_node_get_atime:
 * @node: Node to get the atime from
 * @is_blocking: Whether to block and refresh if needed
 * @atime: Return location for @node's atime
 *
 * Helper to quickly get the property atime of @node
 *
 * If @is_blocking is %FALSE it might return %DONNA_NODE_VALUE_NEED_REFRESH
 * while with %TRUE a refresh will automatically be called (within/blocking the
 * thread). It can then also be set to %DONNA_NODE_VALUE_ERROR if the refresher
 * failed.
 *
 * Returns: Whether the value was set, needs a refresh or doesn't exists
 */
DonnaNodeHasValue
donna_node_get_atime (DonnaNode *node,
                      gboolean   is_blocking,
                      guint64   *atime)
{
    _get_basic_prop (BASIC_PROP_ATIME, g_value_get_uint64, guint64, atime);
}

/**
 * donna_node_get_mode:
 * @node: Node to get the perms from
 * @is_blocking: Whether to block and refresh if needed
 * @mode: Return location for @node's mode (type&perms)
 *
 * Helper to quickly get the property mode of @node
 *
 * If @is_blocking is %FALSE it might return %DONNA_NODE_VALUE_NEED_REFRESH
 * while with %TRUE a refresh will automatically be called (within/blocking the
 * thread). It can then also be set to %DONNA_NODE_VALUE_ERROR if the refresher
 * failed.
 *
 * Returns: Whether the value was set, needs a refresh or doesn't exists
 */
DonnaNodeHasValue
donna_node_get_mode (DonnaNode *node,
                     gboolean   is_blocking,
                     guint     *mode)
{
    _get_basic_prop (BASIC_PROP_MODE, g_value_get_uint, guint, mode);
}

/**
 * donna_node_get_uid:
 * @node: Node to get the user from
 * @is_blocking: Whether to block and refresh if needed
 * @uid: Return location for @node's user id
 *
 * Helper to quickly get the property uid of @node
 *
 * If @is_blocking is %FALSE it might return %DONNA_NODE_VALUE_NEED_REFRESH
 * while with %TRUE a refresh will automatically be called (within/blocking the
 * thread). It can then also be set to %DONNA_NODE_VALUE_ERROR if the refresher
 * failed.
 *
 * Returns: Whether the value was set, needs a refresh or doesn't exists
 */
DonnaNodeHasValue
donna_node_get_uid (DonnaNode  *node,
                    gboolean    is_blocking,
                    guint      *uid)
{
    _get_basic_prop (BASIC_PROP_UID, g_value_get_uint, guint, uid);
}

/**
 * donna_node_get_gid:
 * @node: Node to get the group from
 * @is_blocking: Whether to block and refresh if needed
 * @gid: Return location for @node's group id
 *
 * Helper to quickly get the property gid of @node
 *
 * If @is_blocking is %FALSE it might return %DONNA_NODE_VALUE_NEED_REFRESH
 * while with %TRUE a refresh will automatically be called (within/blocking the
 * thread). It can then also be set to %DONNA_NODE_VALUE_ERROR if the refresher
 * failed.
 *
 * Returns: Whether the value was set, needs a refresh or doesn't exists
 */
DonnaNodeHasValue
donna_node_get_gid (DonnaNode *node,
                    gboolean   is_blocking,
                    guint     *gid)
{
    _get_basic_prop (BASIC_PROP_GID, g_value_get_uint, guint, gid);
}

/**
 * donna_node_get_desc:
 * @node: Node to get the desc from
 * @is_blocking: Whether to block and refresh if needed
 * @icon: Return location for @node's desc
 *
 * Helper to quickly get the property desc of @node
 * Free it with g_free() when done.
 *
 * If @is_blocking is %FALSE it might return %DONNA_NODE_VALUE_NEED_REFRESH
 * while with %TRUE a refresh will automatically be called (within/blocking the
 * thread). It can then also be set to %DONNA_NODE_VALUE_ERROR if the refresher
 * failed.
 *
 * Returns: Whether the value was set, needs a refresh or doesn't exists
 */
DonnaNodeHasValue
donna_node_get_desc (DonnaNode  *node,
                     gboolean    is_blocking,
                     gchar     **desc)
{
    _get_basic_prop (BASIC_PROP_DESC, g_value_dup_string, gchar *, desc);
}

struct refresh_data
{
    DonnaNode   *node;
    GPtrArray   *names;
    GPtrArray   *refreshed;
};

/**
 * node_updated_cb:
 * @provider: The node's provider
 * @node: The node
 * @name: Name of the property that was updated
 * @data: Our data
 *
 * Keeps track of properties being updated while we're calling refreshers. This
 * allows to call one refresher, notice it updated a group of properties and not
 * call refresh on those.
 *
 * See node_refresh()
 */
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

/**
 * node_refresh:
 * @task: Our task
 * @data: Our data
 *
 * Task's worker to refresh properties on a node
 *
 * See donna_node_refresh_task()
 *
 * Returns: The #DonnaTaskState for the task
 */
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

        DONNA_DEBUG (NODE,
                g_debug2 ("node_refresh() for '%s:%s': refreshing %s",
                    donna_provider_get_domain (priv->provider),
                    priv->location,
                    (gchar *) names->pdata[i]));
        if (!refresher (task, data->node, names->pdata[i]))
            ret = DONNA_TASK_FAILED;
    }

    if (donna_task_is_cancelling (task))
        ret = DONNA_TASK_CANCELLED;

    /* disconnect our handler -- any signal that we care about would have come
     * from the refresher, so in this thread, so it would have been processed. */
    g_signal_handler_disconnect (priv->provider, sig);

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

        /* set the return value. the task will take ownership of names */
        value = donna_task_grab_return_value (task);
        g_value_init (value, G_TYPE_PTR_ARRAY);
        g_value_take_boxed (value, names);
        donna_task_release_return_value (task);
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

/**
 * donna_node_refresh_task:
 * @node: Node to refresh properties of
 * @error: (allow none): Return location of a #GError, or %NULL
 * @first_name: Name of the first property to refresh
 *
 * A task to refresh the specified properties. @first_name can be
 * %DONNA_NODE_REFRESH_SET_VALUES to refresh all set values, i.e. that already
 * have a value; or %DONNA_NODE_REFRESH_ALL_VALUES to refresh all properties,
 * including those who do not have a value yet.
 *
 * Returns: (transfer floating): The floating #DonnaTask, or %NULL on error
 */
DonnaTask *
donna_node_refresh_task (DonnaNode   *node,
                         GError     **error,
                         const gchar *first_name,
                         ...)
{
    DonnaTask           *task;
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
        names = g_ptr_array_new_full (g_hash_table_size (node->priv->props),
                g_free);
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

        names = g_ptr_array_new_with_free_func (g_free);

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

    task = donna_task_new ((task_fn) node_refresh, data,
            (GDestroyNotify) free_refresh_data);

    DONNA_DEBUG (TASK,
            gchar *location = donna_node_get_location (node);
            donna_task_take_desc (task, g_strdup_printf ("refresh() for %d properties on node '%s:%s'",
                    names->len,
                    donna_node_get_domain (node),
                    location));
            g_free (location));

    return task;
}

/**
 * donna_node_refresh_arr_task:
 * @node: The node to refresh properties of
 * @props: (element-type const gchar *): A #GPtrArray of properties names
 * @error: (allow none): Return location of a #GError, or %NULL
 *
 * Same as donna_node_refresh_task() but using a #GPtrArray
 *
 * Returns: (transfer floating): The floating #DonnaTask or %NULL on error
 */
DonnaTask *
donna_node_refresh_arr_task (DonnaNode *node,
                             GPtrArray *props,
                             GError   **error)
{
    DonnaTask *task;
    GPtrArray *names;
    guint i;
    struct refresh_data *data;

    g_return_val_if_fail (DONNA_IS_NODE (node), NULL);
    g_return_val_if_fail (props != NULL, NULL);
    g_return_val_if_fail (props->len > 0, NULL);

    /* because the task will change the array, we need to copy it */
    names = g_ptr_array_new_full (props->len, g_free);
    for (i = 0; i < props->len; ++i)
        g_ptr_array_add (names, g_strdup (props->pdata[i]));
    g_ptr_array_unref (props);

    data = g_slice_new0 (struct refresh_data);
    data->node  = g_object_ref (node);
    data->names = names;

    task = donna_task_new ((task_fn) node_refresh, data,
            (GDestroyNotify) free_refresh_data);

    DONNA_DEBUG (TASK,
            gchar *location = donna_node_get_location (node);
            donna_task_take_desc (task, g_strdup_printf ("refresh_arr() for %d properties on node '%s:%s'",
                names->len,
                donna_node_get_domain (node),
                location));
            g_free (location));

    return task;
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

/**
 * set_property:
 * @task: Our task
 * @data: Our data
 *
 * Task's worker to set a property's value
 *
 * See donna_node_set_property_task()
 *
 * Returns: The #DonnaTaskState for the task
 */
static DonnaTaskState
set_property (DonnaTask *task, struct set_property *data)
{
    GValue value = G_VALUE_INIT;
    DonnaTaskState ret;

    DONNA_DEBUG (TASK,
            g_debug3 ("set_property(%s) for '%s:%s'",
                data->prop->name,
                donna_provider_get_domain (data->node->priv->provider),
                data->node->priv->location));
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

/**
 * donna_node_set_property_task:
 * @node: Node on which to change a property
 * @name: Name of the property to change
 * @value: New value to set
 * @error: (allow-none): Return location of a #GError, or %NULL
 *
 * Returns a task to change the value of the specified property on the given
 * node.
 *
 * Returns: (transfer floating): A floating #DonnaTask or %NULL on error
 */
DonnaTask *
donna_node_set_property_task (DonnaNode     *node,
                              const gchar   *name,
                              const GValue  *value,
                              GError       **error)
{
    DonnaTask *task;
    DonnaNodePrivate *priv;
    DonnaNodeProp *prop;
    struct set_property *data;
    const gchar **s;
    gint i;

    g_return_val_if_fail (DONNA_IS_NODE (node), NULL);
    g_return_val_if_fail (name != NULL, NULL);
    g_return_val_if_fail (value != NULL, NULL);
    priv = node->priv;
    prop = NULL;

    /* internal properties cannot be set */
    if (streq (name, "provider") || streq (name, "domain")
            || streq (name, "location") || streq (name, "node-type"))
    {
        gchar *location = donna_node_get_location (node);
        g_warning ("Internal property %s (on node '%s:%s') cannot be set",
                name,
                donna_node_get_domain (node),
                location);
        g_free (location);
        return NULL;
    }

    /* if it's a basic properties, check it can be set */
    for (s = &node_basic_properties[FIRST_REQUIRED_PROP], i = 0; *s; ++s, ++i)
    {
        if (streq (name, *s))
        {
            if (i > 0 && priv->basic_props[i - 1].has_value == DONNA_NODE_VALUE_NONE)
            {
                gchar *location = donna_node_get_location (node);
                g_set_error (error, DONNA_NODE_ERROR, DONNA_NODE_ERROR_NOT_FOUND,
                        "Property %s doesn't exist on node '%s:%s'",
                        name,
                        donna_node_get_domain (node),
                        location);
                g_free (location);
                return NULL;
            }
            else if (!(priv->flags & prop_writable_flags[i]))
            {
                gchar *location = donna_node_get_location (node);
                g_set_error (error, DONNA_NODE_ERROR, DONNA_NODE_ERROR_READ_ONLY,
                        "Property %s on node '%s:%s' cannot be set",
                        name,
                        donna_node_get_domain (node),
                        location);
                g_free (location);
                return NULL;
            }

            /* NAME */
            if (i == 0)
            {
                if (!G_VALUE_HOLDS_STRING (value))
                {
                    g_warning ("Basic property %s is of type %s, value passed is %s",
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
                    g_warning ("Basic property %s is of type %s, value passed is %s",
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
            gchar *location = donna_node_get_location (node);
            g_set_error (error, DONNA_NODE_ERROR, DONNA_NODE_ERROR_NOT_FOUND,
                    "Node '%s:%s' doesn't have a property %s",
                    donna_node_get_domain (node),
                    location,
                    name);
            g_free (location);
            return NULL;
        }

        if (!prop->setter)
        {
            gchar *location = donna_node_get_location (node);
            g_set_error (error, DONNA_NODE_ERROR, DONNA_NODE_ERROR_READ_ONLY,
                    "Property %s on node '%s:%s' can't be set",
                    name,
                    donna_node_get_domain (node),
                    location);
            g_free (location);
            return NULL;
        }

        if (!G_VALUE_HOLDS (value, G_VALUE_TYPE (&prop->value)))
        {
            gchar *location = donna_node_get_location (node);
            g_set_error (error, DONNA_NODE_ERROR, DONNA_NODE_ERROR_INVALID_TYPE,
                    "Property %s on node '%s:%s' is of type %s, value passed is %s",
                    name,
                    donna_node_get_domain (node),
                    location,
                    g_type_name (G_VALUE_TYPE (&prop->value)),
                    g_type_name (G_VALUE_TYPE (value)));
            g_free (location);
            return NULL;
        }
    }

    data = g_slice_new (struct set_property);
    /* take a ref on node, for the task */
    data->node = g_object_ref (node);
    data->prop = prop;
    data->value = duplicate_gvalue (value);

    task = donna_task_new ((task_fn) set_property, data,
            (GDestroyNotify) free_set_property);

    DONNA_DEBUG (TASK,
            gchar *location = donna_node_get_location (node);
            donna_task_take_desc (task, g_strdup_printf ("set_property(%s) on node '%s:%s'",
                    name,
                    donna_node_get_domain (node),
                    location));
            g_free (location));

    return task;
}

/**
 * donna_node_has_children_task:
 * @node: Node to check whether it has children
 * @node_types: %DonnaNodeType of children to check for
 * @error: (allow none): Return location of a #GError, or %NULL
 *
 * Returns a task to determine whether @node has children of the specified
 * type(s) or not
 *
 * Note: this is an helper function, that calls
 * donna_provider_has_node_children_task() on @node's provider
 *
 * Returns: (transfer floating): The floating #DonnaTask, or %NULL
 */
DonnaTask *
donna_node_has_children_task (DonnaNode          *node,
                              DonnaNodeType       node_types,
                              GError            **error)
{
    g_return_val_if_fail (DONNA_IS_NODE (node), NULL);
    return donna_provider_has_node_children_task (node->priv->provider, node,
            node_types, error);
}

/**
 * donna_node_get_children_task:
 * @node: Node to check whether it has children
 * @node_types: %DonnaNodeType of children to get
 * @error: (allow none): Return location of a #GError, or %NULL
 *
 * Returns a task to get children of the specified type(s) from @node
 *
 * Note: this is an helper function, that calls
 * donna_provider_get_node_children_task() on @node's provider
 *
 * Returns: (transfer floating): The floating #DonnaTask, or %NULL
 */
DonnaTask *
donna_node_get_children_task (DonnaNode          *node,
                              DonnaNodeType       node_types,
                              GError            **error)
{
    g_return_val_if_fail (DONNA_IS_NODE (node), NULL);
    return donna_provider_get_node_children_task (node->priv->provider, node,
            node_types, error);
}

/**
 * donna_node_remove_task:
 * @node: Node to remove
 * @error: (allow none): Return location of a #GError, or %NULL
 *
 * Returns a task to delete the item represented by @node
 *
 * Note: this is an helper function, that calls
 * donna_provider_remove_node_task() on @node's provider
 *
 * Returns: (transfer floating): The floating #DonnaTask, or %NULL
 */
DonnaTask *
donna_node_remove_task (DonnaNode          *node,
                        GError            **error)
{
    g_return_val_if_fail (DONNA_IS_NODE (node), NULL);
    return donna_provider_remove_node_task (node->priv->provider, node, error);
}

/**
 * donna_node_get_parent_task:
 * @node: Node to get parent node of
 * @error: (allow none): Return location of a #GError, or %NULL
 *
 * Returns a task to get the parent #DonnaNode of @node
 *
 * Note: this is an helper function, that calls
 * donna_provider_get_node_parent_task() on @node's provider
 *
 * Returns: (transfer floating): The floating #DonnaTask, or %NULL
 */
DonnaTask *
donna_node_get_parent_task (DonnaNode          *node,
                            GError            **error)
{
    g_return_val_if_fail (DONNA_IS_NODE (node), NULL);
    return donna_provider_get_node_parent_task (node->priv->provider, node, error);
}

/**
 * donna_node_trigger_task:
 * @node: Node to trigger
 * @error: (allow none): Return location of a #GError, or %NULL
 *
 * Returns a task to trigger @node
 *
 * Note: this is a helper function, that calls
 * donna_provider_trigger_node_task() on @node's provider
 *
 * Returns: (transfer floating): The floating #DonnaTask, or %NULL
 */
DonnaTask *
donna_node_trigger_task (DonnaNode          *node,
                         GError            **error)
{
    g_return_val_if_fail (DONNA_IS_NODE (node), NULL);
    return donna_provider_trigger_node_task (node->priv->provider, node, error);
}

/**
 * donna_node_set_property_value:
 * @node: The node
 * @name: Name of the property
 * @value: New value of the property
 *
 * Updates the value of a property on a node. This should only be called by the
 * owner of the property, when the value has actually changed on the item.
 * It's usually called by the setter, or when some autorefresh is triggered.
 *
 * For properties which can have no value set (i.e. a refresh is needed, so all
 * properties but required ones) you can pass %NULL as @value to simply unset
 * whatever value is currently set.
 *
 * To (try to) change the value of a property, use
 * donna_node_set_property_task()
 */
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
    DONNA_DEBUG (NODE,
            g_debug3 ("set_property_value(%s) on '%s:%s'",
                name,
                donna_provider_get_domain (node->priv->provider),
                node->priv->location));

    if (streq (name, "name"))
    {
        g_free (node->priv->name);
        node->priv->name = g_value_dup_string (value);
        emit = TRUE;
        goto finish;
    }
    else if (streq (name, "filename"))
    {
        g_free (node->priv->filename);
        node->priv->filename = g_value_dup_string (value);
        emit = TRUE;
        goto finish;
    }
    else if (streq (name, "location"))
    {
        g_free (node->priv->location);
        node->priv->location = g_value_dup_string (value);
        emit = TRUE;
        goto finish;
    }

    /* basic prop? */
    for (s = &node_basic_properties[FIRST_BASIC_PROP], i = 0; *s; ++s, ++i)
    {
        if (streq (name, *s))
        {
            if (value)
            {
                /* copy the new value over */
                g_value_copy (value, &node->priv->basic_props[i].value);
                /* we assume it worked, w/out checking types, etc because this
                 * should only be used by providers and such, on properties they
                 * are handling, so if they get it wrong, they're seriously
                 * bugged */
                node->priv->basic_props[i].has_value = DONNA_NODE_VALUE_SET;
            }
            else
            {
                GType type;

                type = G_VALUE_TYPE (&node->priv->basic_props[i].value);
                g_value_unset (&node->priv->basic_props[i].value);
                g_value_init (&node->priv->basic_props[i].value, type);
                node->priv->basic_props[i].has_value = DONNA_NODE_VALUE_NEED_REFRESH;
            }
            emit = TRUE;
            goto finish;
        }
    }

    /* other prop? */
    prop = g_hash_table_lookup (node->priv->props, (gpointer) name);
    if (prop)
    {
        if (value)
        {
            /* copy the new value over */
            g_value_copy (value, &(prop->value));
            /* we assume it worked, w/out checking types, etc because this
             * should only be used by providers and such, on properties they are
             * handling, so if they get it wrong, they're seriously bugged */
            prop->has_value = TRUE;
        }
        else
        {
            GType type;

            type = G_VALUE_TYPE (&(prop->value));
            g_value_unset (&(prop->value));
            g_value_init (&(prop->value), type);
            prop->has_value = FALSE;
        }
        emit = TRUE;
    }

finish:
    g_rw_lock_writer_unlock (&node->priv->props_lock);

    if (emit)
        donna_provider_node_updated (node->priv->provider, node, name);
}

/**
 * donna_node_inc_toggle_count:
 * @node: The node
 *
 * Increments the toggle count for that node. This should only be used by the
 * node's provider, to handle its toggle reference in multi-threaded
 * environment.
 *
 * Returns: The new toggle count
 */
int
donna_node_inc_toggle_count (DonnaNode *node)
{
    g_return_val_if_fail (DONNA_IS_NODE (node), -1);
    return ++node->priv->toggle_count;
}

/**
 * donna_node_dec_toggle_count:
 * @node: The node
 *
 * Decrements the toggle count for that node. This should only be used by the
 * node's provider, to handle its toggle reference in multi-threaded
 * environment.
 *
 * Returns: The new toggle count
 */
int
donna_node_dec_toggle_count (DonnaNode *node)
{
    g_return_val_if_fail (DONNA_IS_NODE (node), -1);
    return --node->priv->toggle_count;
}
