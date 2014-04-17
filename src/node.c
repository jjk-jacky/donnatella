/*
 * donnatella - Copyright (C) 2014 Olivier Brunel
 *
 * node.c
 * Copyright (C) 2014 Olivier Brunel <jjk@jjacky.com>
 *
 * This file is part of donnatella.
 *
 * donnatella is free software: you can redistribute it and/or modify it under
 * the terms of the GNU General Public License as published by the Free Software
 * Foundation, either version 3 of the License, or (at your option) any later
 * version.
 *
 * donnatella is distributed in the hope that it will be useful, but WITHOUT ANY
 * WARRANTY; without even the implied warranty of MERCHANTABILITY or FITNESS
 * FOR A PARTICULAR PURPOSE.
 * See the GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License along with
 * donnatella. If not, see http://www.gnu.org/licenses/
 */

#include "config.h"

#include <glib-object.h>
#include <gobject/gvaluecollector.h>    /* G_VALUE_LCOPY */
#include <string.h>                     /* memset() */
#include "node.h"
#include "debug.h"
#include "provider.h"                   /* donna_provider_node_updated() */
#include "task.h"
#include "util.h"
#include "macros.h"                     /* streq() */
#include "app.h"                        /* donna_app_run_task() in node_refresh()
                                           for tasks LOOP */

/**
 * SECTION:node
 * @Short_description: An object holding dynamic properties
 * @See_also: #DonnaProvider, #DonnaTask
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
 * - icon: pointer to a #GIcon of the item's icon
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
 * You create a new node using donna_node_new() The refresher and setter
 * functions will be used for all (existing) basic properties. Additional
 * properties can be added using donna_node_add_property(), which can be done by
 * using the provider's signal #DonnaProvider::new-node, emitted upon node
 * creation for this purpose, by anyone (i.e. not just the node's provider).
 *
 * Only the owner of a property should use donna_node_set_property_value() when
 * such a change has effectively been observed on the item it represents.
 *
 * Once all properties & their initial values have been set, you must call
 * donna_node_mark_ready() to make sure further change of values will emit a
 * signal in the node's provider.
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
    /* node is ready, aka all initial values have been set, properties added,
     * provider is ready. Before that, we don't emit signals and whatnot */
    gboolean           ready;
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
    DonnaTaskVisibility visibility;
    refresher_task_fn   refresher_task;
    refresher_fn        refresher;
    setter_fn           setter;
    DonnaNodeFlags      flags;
    /* other properties */
    GHashTable      *props;
    GRWLock          props_lock; /* also applies to basic_props, name & icon */
};

typedef struct
{
    const gchar         *name; /* this pointer is also used as key in the hash table */
    DonnaTaskVisibility  visibility;
    refresher_task_fn    refresher_task;
    refresher_fn         refresher;
    setter_fn            setter;
    gpointer             data;
    GDestroyNotify       destroy;
    gboolean             has_value; /* is value set, or do we need to call refresher? */
    GValue               value;
} DonnaNodeProp;

static void donna_node_finalize (GObject *object);

static void free_node_prop (DonnaNodeProp *prop);

G_DEFINE_TYPE (DonnaNode, donna_node, G_TYPE_OBJECT)

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

    priv->visibility = DONNA_TASK_VISIBILITY_INTERNAL;

    g_value_init (&priv->basic_props[BASIC_PROP_ICON].value,      G_TYPE_ICON);
    g_value_init (&priv->basic_props[BASIC_PROP_FULL_NAME].value, G_TYPE_STRING);
    g_value_init (&priv->basic_props[BASIC_PROP_SIZE].value,      G_TYPE_UINT64);
    g_value_init (&priv->basic_props[BASIC_PROP_CTIME].value,     G_TYPE_UINT64);
    g_value_init (&priv->basic_props[BASIC_PROP_MTIME].value,     G_TYPE_UINT64);
    g_value_init (&priv->basic_props[BASIC_PROP_ATIME].value,     G_TYPE_UINT64);
    g_value_init (&priv->basic_props[BASIC_PROP_MODE].value,      G_TYPE_UINT);
    g_value_init (&priv->basic_props[BASIC_PROP_UID].value,       G_TYPE_UINT);
    g_value_init (&priv->basic_props[BASIC_PROP_GID].value,       G_TYPE_UINT);
    g_value_init (&priv->basic_props[BASIC_PROP_DESC].value,      G_TYPE_STRING);
}

static void
donna_node_finalize (GObject *object)
{
    DonnaNodePrivate *priv;
    gint i;

    priv = DONNA_NODE (object)->priv;
    DONNA_DEBUG (NODE, donna_provider_get_domain (priv->provider),
            g_debug ("Finalizing node '%s:%s'",
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
    if (prop->destroy && prop->data)
        prop->destroy (prop->data);
    g_slice_free (DonnaNodeProp, prop);
}

/**
 * donna_node_new:
 * @provider: provider of the node
 * @location: location of the node
 * @node_type: type of node
 * @filename: (allow-none): filename of the node (in GLib filename encoding),
 * or %NULL if we can use @name)
 * @visibility: Visibility for the refresher
 * @refresher_task: function to return a task to refresh basic property, if
 * @visibility is #DONNA_TASK_VISIBILITY_INTERNAL_LOOP
 * @refresher: function called to refresh a basic property
 * @setter: function to change value of a basic property
 * @name: name of the node
 * @flags: flags to define which basic properties exists/are writable
 *
 * Creates a new node, according to the specified parameters. This should only
 * be called by the #DonnaProvider of the node.
 *
 * @refresher migh be called without a #DonnaTask when from a blocking call, or
 * it can be called from a task worker to refresh one or more properties on
 * @node. In such a case, the node creating the task will use @visibility to
 * determine the appropriate visibility of the task: If all refreshers are
 * #DONNA_TASK_VISIBILITY_INTERNAL_FAST so will the task be; else it will be
 * #DONNA_TASK_VISIBILITY_INTERNAL.
 *
 * #DONNA_TASK_VISIBILITY_INTERNAL_GUI and #DONNA_TASK_VISIBILITY_PULIC aren't
 * allowed, and will be reverted back to #DONNA_TASK_VISIBILITY_INTERNAL.
 *
 * Additionally if a refresher is #DONNA_TASK_VISIBILITY_INTERNAL_LOOP then the
 * refresher will only be called in blocking calls, else @refresher_task will be
 * called and the returned task will be used to refresh the property; See
 * #refresher_task_fn for more.
 *
 * If you need a node to use it, see donna_provider_get_node() or
 * donna_app_get_node()
 *
 * Returns: (transfer full): The new node
 */
DonnaNode *
donna_node_new (DonnaProvider       *provider,
                const gchar         *location,
                DonnaNodeType        node_type,
                const gchar         *filename,
                DonnaTaskVisibility  visibility,
                refresher_task_fn    refresher_task,
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
    if (visibility == DONNA_TASK_VISIBILITY_INTERNAL_LOOP)
        g_return_val_if_fail (refresher_task != NULL, NULL);
    if ((flags & DONNA_NODE_ALL_WRITABLE) != 0)
        g_return_val_if_fail (setter != NULL, NULL);
    g_return_val_if_fail (name != NULL, NULL);

    if (visibility != DONNA_TASK_VISIBILITY_INTERNAL_FAST
            && visibility != DONNA_TASK_VISIBILITY_INTERNAL
            && visibility != DONNA_TASK_VISIBILITY_INTERNAL_LOOP)
        visibility = DONNA_TASK_VISIBILITY_INTERNAL;

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

    priv->refresher_task = refresher_task;
    if (visibility != DONNA_TASK_VISIBILITY_INTERNAL)
        priv->visibility = visibility;
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

    DONNA_DEBUG (NODE, donna_provider_get_domain (priv->provider),
            g_debug ("Created new node '%s:%s'",
                donna_provider_get_domain (priv->provider),
                priv->location));
    return node;
}

/**
 * donna_node_add_property:
 * @node: The node to add the property to
 * @name: Name of the property
 * @type: type of the property
 * @value: (allow-none): Initial value of the property
 * @visibility: Visibility for the refresher
 * @refresher_task: function to return a task to refresh basic property, if
 * @visibility is #DONNA_TASK_VISIBILITY_INTERNAL_LOOP
 * @refresher: function to be called to refresh the property's value
 * @setter: (allow-none): Function to be called to change the property's value
 * @data: (allow-none): Data to given to @refresher and @setter
 * @destroy: (allow-none): Function to call to free @data when @node is
 * finalized
 * @error: (allow-none): Return location of error (or %NULL)
 *
 * Adds a new additional property of the given node.
 *
 * See donna_node_new() for more about @visibility, @refresher_task and
 * @refresher
 *
 * Returns: %TRUE if the property was added, else %FALSE
 */
gboolean
donna_node_add_property (DonnaNode       *node,
                         const gchar     *name,
                         GType            type,
                         const GValue    *value,
                         DonnaTaskVisibility visibility,
                         refresher_task_fn refresher_task,
                         refresher_fn     refresher,
                         setter_fn        setter,
                         gpointer         data,
                         GDestroyNotify   destroy,
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

    if (visibility != DONNA_TASK_VISIBILITY_INTERNAL_FAST
            && visibility != DONNA_TASK_VISIBILITY_INTERNAL
            && visibility != DONNA_TASK_VISIBILITY_INTERNAL_LOOP)
        visibility = DONNA_TASK_VISIBILITY_INTERNAL;

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
    prop->name           = g_strdup (name);
    prop->visibility     = visibility;
    prop->refresher_task = refresher_task;
    prop->refresher      = refresher;
    prop->setter         = setter;
    prop->data           = data;
    prop->destroy        = destroy;
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
    DONNA_DEBUG (NODE, donna_provider_get_domain (priv->provider),
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
                        DONNA_DEBUG (NODE, donna_provider_get_domain (priv->provider),
                                g_debug2 ("node_get() for '%s:%s': refreshing %s",
                                    donna_provider_get_domain (priv->provider),
                                    priv->location,
                                    name));
                        /* we need to release the lock, since the refresher
                         * should call set_property_value, hence need a writer
                         * lock */
                        g_rw_lock_reader_unlock (&priv->props_lock);
                        if (priv->refresher (NULL /* no task */, node, name, NULL))
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
                DONNA_DEBUG (NODE, donna_provider_get_domain (priv->provider),
                        g_debug2 ("node_get() for '%s:%s': refreshing %s",
                            donna_provider_get_domain (priv->provider),
                            priv->location,
                            name));
                /* release the lock for refresher */
                g_rw_lock_reader_unlock (&priv->props_lock);
                if (prop->refresher (NULL /* no task */, node, name, prop->data))
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
 * @...: %NULL-terminated list of (names and) return locations for the values
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
 * If @is_blocking is %FALSE the #DonnaNodeHasValue might be
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
 * Returns: #DonnaNodeType of @node
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

typedef guintptr (*value_dup_fn) (const GValue *value);

static DonnaNodeHasValue
get_basic_prop (DonnaNode   *node,
                gboolean     is_blocking,
                gint         basic_id,
                guintptr    *dest,
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
        DONNA_DEBUG (NODE, donna_provider_get_domain (priv->provider),
                g_debug2 ("node_get_*() for '%s:%s': refreshing %s",
                    donna_provider_get_domain (priv->provider),
                    priv->location,
                    node_basic_properties[FIRST_BASIC_PROP + basic_id]));
        /* we need to release the lock, since the refresher
         * should call set_property_value, hence need a writer
         * lock */
        g_rw_lock_reader_unlock (&priv->props_lock);
        if (priv->refresher (NULL /* no task */, node,
                    node_basic_properties[FIRST_BASIC_PROP + basic_id], NULL))
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
    guintptr value;                                         \
    has = get_basic_prop (node, is_blocking, BASIC_PROP,    \
            &value, (value_dup_fn) get_fn);                 \
    if (has == DONNA_NODE_VALUE_SET)                        \
        *var = (type) value;                                \
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
                     GIcon     **icon)
{
    _get_basic_prop (BASIC_PROP_ICON, g_value_dup_object, GIcon *, icon);
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
 * @desc: Return location for @node's desc
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

/**
 * donna_node_get_parent:
 * @node: Node to get parent node of
 * @error: (allow-none): Return location of a #GError, or %NULL
 *
 * Returns the parent #DonnaNode of @node
 *
 * Note that this will call donna_provider_get_node(), and as such a new main
 * loop could be started while getting the node.
 *
 * Returns: (transfer full): The #DonnaNode, or %NULL
 */
DonnaNode *
donna_node_get_parent (DonnaNode          *node,
                       GError            **error)
{
    DonnaNodePrivate *priv;
    gchar *parent_location;
    gchar *s;
    DonnaNode *parent;

    g_return_val_if_fail (DONNA_IS_NODE (node), NULL);
    priv = node->priv;

    if (donna_provider_get_flags (priv->provider) & DONNA_PROVIDER_FLAG_FLAT)
    {
        g_set_error (error, DONNA_PROVIDER_ERROR,
                DONNA_PROVIDER_ERROR_NOT_SUPPORTED,
                "Provider '%s': Cannot get parent of '%s', provider is flat",
                donna_provider_get_domain (priv->provider),
                priv->location);
        return NULL;
    }

    if (streq (priv->location, "/"))
    {
        g_set_error (error, DONNA_PROVIDER_ERROR,
                DONNA_PROVIDER_ERROR_LOCATION_NOT_FOUND,
                "Provider '%s': Root has no parent",
                donna_provider_get_domain (priv->provider));
        return NULL;
    }

    parent_location = g_strdup (priv->location);
    s = strrchr (parent_location, '/');
    if (s == parent_location)
        ++s;
    *s = '\0';
    parent = donna_provider_get_node (priv->provider, parent_location, error);
    g_free (parent_location);
    return parent;
}

struct refresh_data
{
    DonnaNode   *node;
    GPtrArray   *names;
    GPtrArray   *refreshed;
    GMutex       mutex;
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
    g_mutex_lock (&data->mutex);
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
    g_mutex_unlock (&data->mutex);
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
    GPtrArray           *arr_tasks = NULL;
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

    /* we need to protect refreshed under a lock, because both this thread and
     * the thread tasks-loop might access it. Also, nothing says another thread
     * couldn't refresh one of those properties on this node at the same time
     * either... */
    g_mutex_init (&data->mutex);

    props = priv->props;
    for (i = 0; i < names->len; ++i)
    {
        DonnaNodeProp      *prop;
        refresher_task_fn   refresher_task;
        refresher_fn        refresher;
        gpointer            refresher_data;
        guint               j;
        gboolean            done;
        gchar             **s;

        if (donna_task_is_cancelling (task))
        {
            ret = DONNA_TASK_CANCELLED;
            break;
        }

        refresher_task = NULL;
        refresher = NULL;
        refresher_data = NULL;

        /* basic properties. We skip internal ones (provider, domain, location,
         * node-type) since they can't be refreshed (should never be needed
         * anyway) */
        j = 0;
        for (s = (gchar **) &node_basic_properties[FIRST_REQUIRED_PROP]; *s; ++s, ++j)
        {
            if (streq (names->pdata[i], *s))
            {
                refresher = priv->refresher;
                if (priv->visibility == DONNA_TASK_VISIBILITY_INTERNAL_LOOP)
                    refresher_task = priv->refresher_task;
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
            {
                refresher = prop->refresher;
                refresher_data = prop->data;
                if (prop->visibility == DONNA_TASK_VISIBILITY_INTERNAL_LOOP)
                    refresher_task = prop->refresher_task;
            }
        }

        if (!refresher)
            continue;

        /* only call the refresher if the prop hasn't already been refreshed */
        done = FALSE;
        g_mutex_lock (&data->mutex);
        for (j = 0; j < refreshed->len; ++j)
        {
            /* refreshed contains the *same pointers* as names */
            if (refreshed->pdata[j] == names->pdata[i])
            {
                done = TRUE;
                break;
            }
        }
        g_mutex_unlock (&data->mutex);
        if (done)
            continue;

        if (refresher_task)
        {
            DonnaApp *app;
            DonnaTask *t;

            t = refresher_task (data->node, names->pdata[i], refresher_data, &app);
            if (G_LIKELY (t && app))
            {
                if (!arr_tasks)
                    arr_tasks = g_ptr_array_new_with_free_func (g_object_unref);
                g_ptr_array_add (arr_tasks, g_object_ref (t));
                donna_app_run_task (app, t);
                continue;
            }
            else if (t)
            {
                gchar *fl = donna_node_get_full_location (data->node);
                g_warning ("node_refresh(): refresher_task failed to set app "
                        "for '%s' on '%s', fallback to standard/blocking refresher",
                        (gchar *) names->pdata[i], fl);
                g_free (fl);
                g_object_unref (g_object_ref_sink (t));
            }
            else
            {
                gchar *fl = donna_node_get_full_location (data->node);
                g_warning ("node_refresh(): refresher_task failed to return a task "
                        "for '%s' on '%s', fallback to standard/blocking refresher",
                        (gchar *) names->pdata[i], fl);
                g_free (fl);
            }
        }

        DONNA_DEBUG (NODE, donna_provider_get_domain (priv->provider),
                g_debug2 ("node_refresh() for '%s:%s': refreshing %s",
                    donna_provider_get_domain (priv->provider),
                    priv->location,
                    (gchar *) names->pdata[i]));
        if (!refresher (task, data->node, names->pdata[i], refresher_data))
            ret = DONNA_TASK_FAILED;
    }

    if (donna_task_is_cancelling (task))
        ret = DONNA_TASK_CANCELLED;

    if (arr_tasks)
    {
        for (i = 0; i < arr_tasks->len; )
        {
            DonnaTask *t = arr_tasks->pdata[i];

            if (ret == DONNA_TASK_CANCELLED)
                donna_task_cancel (t);

            donna_task_wait_for_it (t, task, NULL);
            if (ret == DONNA_TASK_DONE && donna_task_get_state (t) == DONNA_TASK_FAILED)
                ret = DONNA_TASK_FAILED;

            g_ptr_array_remove_index_fast (arr_tasks, i);
        }
        g_ptr_array_unref (arr_tasks);
    }

    /* disconnect our handler -- any signal that we care about would have come
     * from the refresher, so in this thread, or the tasks-loop thread, but we
     * waited for those tasks, so it would have been processed. */
    g_signal_handler_disconnect (priv->provider, sig);
    g_mutex_clear (&data->mutex);

    /* did everything get refreshed? */
    if (names->len == refreshed->len)
    {
        /* we don't set a return value. A lack of return value (or NULL) will
         * mean that no properties was not refreshed */
        g_ptr_array_free (refreshed, TRUE);
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
        for (i = 0; i < refreshed->len; )
        {
            guint j;

            for (j = 0; j < names->len; ++j)
            {
                if (names->pdata[j] == refreshed->pdata[i])
                {
                    /* remove from both arrays. Removing from names will free
                     * the string; Removing will move the last item to the
                     * current one, effectively replacing it, so next iteration
                     * we don't need to move inside refreshed/increment i */
                    g_ptr_array_remove_index_fast (names, j);
                    g_ptr_array_remove_index_fast (refreshed, i);
                    break;
                }
            }
            /* since we only put in refreshed pointers from names, it's
             * impossible that we failed to find (and remove) it in names, hence
             * why there can't be an infinite loop: we always removed an item
             * from refreshed */
        }
        /* names now only contains the names of non-refreshed properties, it's
         * our return value. (refreshed isn't needed anymore, and can be freed) */
        g_ptr_array_free (refreshed, TRUE);

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
    g_ptr_array_free (data->refreshed, TRUE);
    g_slice_free (struct refresh_data, data);
}

/* assumes reader lock on props_lock */
static void
add_prop_to_arr (DonnaNode              *node,
                 const gchar            *name,
                 GPtrArray             **arr_names,
                 GPtrArray             **arr_tasks,
                 DonnaTaskVisibility    *visibility,
                 refresher_task_fn      *refresher_task,
                 gpointer               *refresher_data)
{
    DonnaNodePrivate *priv = node->priv;
    DonnaNodeProp *prop = NULL;
    enum {
        _PROP_NOT_FOUND,
        _PROP_FOUND,
        _PROP_ADD
    } st = _PROP_NOT_FOUND;
    const gchar **s;
    guint i;

    if (streq (name, "name"))
        st = _PROP_ADD;
    else
    {
        for (s = node_basic_properties + FIRST_BASIC_PROP, i = 0; *s; ++s, ++i)
            if (streq (name, *s))
            {
                if (priv->basic_props[i].has_value != DONNA_NODE_VALUE_NONE)
                    st = _PROP_ADD;
                else
                    /* FOUND means we've processed it, but it doesn't exist on
                     * the node, i.e. don't look into extra properties */
                    st = _PROP_FOUND;
                break;
            }
    }

    if (st == _PROP_NOT_FOUND)
    {
        prop = g_hash_table_lookup (priv->props, name);
        if (prop)
            st = _PROP_ADD;
    }

    if (st == _PROP_ADD)
    {
        gboolean add_name = TRUE;

        *refresher_task = NULL;
        if (prop)
        {
            if (prop->visibility == DONNA_TASK_VISIBILITY_INTERNAL)
                *visibility = DONNA_TASK_VISIBILITY_INTERNAL;
            else if (prop->visibility == DONNA_TASK_VISIBILITY_INTERNAL_LOOP)
            {
                *visibility = DONNA_TASK_VISIBILITY_INTERNAL;
                *refresher_task = prop->refresher_task;
                *refresher_data = prop->data;
            }
        }
        else
        {
            if (priv->visibility == DONNA_TASK_VISIBILITY_INTERNAL)
                *visibility = DONNA_TASK_VISIBILITY_INTERNAL;
            else if (priv->visibility == DONNA_TASK_VISIBILITY_INTERNAL_LOOP)
            {
                *visibility = DONNA_TASK_VISIBILITY_INTERNAL;
                *refresher_task = priv->refresher_task;
                *refresher_data = NULL;
            }
        }

        if (arr_tasks && *refresher_task)
        {
            DonnaTask *t;

            t = (*refresher_task) (node, name, *refresher_data, NULL);
            if (G_LIKELY (t))
            {
                if (!*arr_tasks)
                    *arr_tasks = g_ptr_array_new ();

                g_ptr_array_add (*arr_tasks, t);
                add_name = FALSE;
            }
        }

        if (add_name)
        {
            if (!*arr_names)
                *arr_names = g_ptr_array_new_with_free_func (g_free);

            /* we can't have dupes */
            for (i = 0; i < (*arr_names)->len; ++i)
                if (streq (name, (*arr_names)->pdata[i]))
                    break;
            if (i >= (*arr_names)->len)
                g_ptr_array_add (*arr_names, g_strdup (name));
        }
    }
    else
    {
        DONNA_DEBUG (NODE, donna_provider_get_domain (priv->provider),
                gchar *location = donna_node_get_full_location (node);
                g_debug ("Cannot refresh property '%s' on node '%s': No such property",
                    name, location);
                g_free (location));
    }
}

static gpointer
_donna_node_refresh (DonnaNode   *node,
                     const gchar *first_name,
                     va_list      va_args,
                     gboolean     get_tasks_array,
                     GPtrArray   *tasks,
                     GError     **error)
{
    DonnaNodePrivate    *priv;
    DonnaTask           *task;
    DonnaTaskVisibility  visibility = DONNA_TASK_VISIBILITY_INTERNAL_FAST;
    refresher_task_fn    refresher_task = NULL;
    gpointer             refresher_data = NULL;
    GPtrArray           *names = NULL;
    struct refresh_data *data;

    g_return_val_if_fail (DONNA_IS_NODE (node), NULL);
    priv = node->priv;

    if (!first_name /* == DONNA_NODE_REFRESH_SET_VALUES */
            || streq (first_name, DONNA_NODE_REFRESH_ALL_VALUES))
    {
        GHashTableIter iter;
        gpointer key, value;
        const gchar **s;
        guint i;

        if (get_tasks_array && !tasks)
            tasks = g_ptr_array_new ();

        /* we'll send the list of all properties, because node_refresh() needs
         * to know which refresher to call, and it can't have a lock on the hash
         * table since the refresher will call set_property_value which needs to
         * take a writer lock... */

        g_rw_lock_reader_lock (&priv->props_lock);

        if (priv->visibility == DONNA_TASK_VISIBILITY_INTERNAL_LOOP
                && get_tasks_array)
            names = g_ptr_array_new_with_free_func (g_free);
        else
            names = g_ptr_array_new_full (
                    /* basic props + required props - those that never need to
                     * be refreshed, i.e. provider/domain/location/node-type */
                    NB_BASIC_PROPS + FIRST_BASIC_PROP - 4
                    + g_hash_table_size (priv->props),
                    g_free);

        /* always have name, since it's always set */
        if (priv->visibility == DONNA_TASK_VISIBILITY_INTERNAL_LOOP)
        {
            refresher_task = priv->refresher_task;
            if (get_tasks_array)
            {
                DonnaTask *t;

                t = priv->refresher_task (node, "name", NULL, NULL);
                if (G_LIKELY (t))
                    g_ptr_array_add (tasks, t);
                else
                    g_ptr_array_add (names, g_strdup ("name"));
            }
            else
                g_ptr_array_add (names, g_strdup ("name"));
        }
        else
            g_ptr_array_add (names, g_strdup ("name"));

        /* add basic props that exists/are set */
        for (s = node_basic_properties + FIRST_BASIC_PROP, i = 0; *s; ++s, ++i)
            if (priv->basic_props[i].has_value == DONNA_NODE_VALUE_SET
                    || (first_name /* ALL_VALUES */
                        && priv->basic_props[i].has_value != DONNA_NODE_VALUE_NONE))
            {
                if (get_tasks_array
                        && priv->visibility == DONNA_TASK_VISIBILITY_INTERNAL_LOOP)
                {
                    DonnaTask *t;

                    t = priv->refresher_task (node, *s, NULL, NULL);
                    if (G_LIKELY (t))
                        g_ptr_array_add (tasks, t);
                    else
                        g_ptr_array_add (names, g_strdup (*s));
                }
                else
                {
                    g_ptr_array_add (names, g_strdup (*s));
                    if (priv->visibility == DONNA_TASK_VISIBILITY_INTERNAL)
                        visibility = DONNA_TASK_VISIBILITY_INTERNAL;
                    else if (priv->visibility == DONNA_TASK_VISIBILITY_INTERNAL_LOOP)
                    {
                        visibility = DONNA_TASK_VISIBILITY_INTERNAL;
                        refresher_task = priv->refresher_task;
                    }
                }
            }

        g_hash_table_iter_init (&iter, priv->props);
        while (g_hash_table_iter_next (&iter, &key, &value))
        {
            DonnaNodeProp *prop = value;

            if (first_name || prop->has_value)
            {
                if (get_tasks_array
                        && prop->visibility == DONNA_TASK_VISIBILITY_INTERNAL_LOOP)
                {
                    DonnaTask *t;

                    t = prop->refresher_task (node, (gchar *) key, prop->data, NULL);
                    if (G_LIKELY (t))
                        g_ptr_array_add (tasks, t);
                    else
                        g_ptr_array_add (names, g_strdup ((gchar *) key));
                }
                else
                {
                    value = (gpointer) g_strdup ((gchar *) key);
                    g_ptr_array_add (names, value);
                    if (prop->visibility == DONNA_TASK_VISIBILITY_INTERNAL)
                        visibility = DONNA_TASK_VISIBILITY_INTERNAL;
                    else if (prop->visibility == DONNA_TASK_VISIBILITY_INTERNAL_LOOP)
                    {
                        visibility = DONNA_TASK_VISIBILITY_INTERNAL;
                        refresher_task = prop->refresher_task;
                        refresher_data = prop->data;
                    }
                }
            }
        }
        g_rw_lock_reader_unlock (&priv->props_lock);
    }
    else
    {
        gpointer name;

        name = (gpointer) first_name;
        g_rw_lock_reader_lock (&priv->props_lock);
        while (name)
        {
            add_prop_to_arr (node, name,
                    &names, (get_tasks_array) ? &tasks : NULL,
                    &visibility, &refresher_task, &refresher_data);
            name = va_arg (va_args, gpointer);
        }
        g_rw_lock_reader_unlock (&priv->props_lock);
    }

    if (!get_tasks_array)
    {
        if (G_UNLIKELY (!names))
        {
            gchar *fl = donna_node_get_full_location (node);
            g_set_error (error, DONNA_NODE_ERROR, DONNA_NODE_ERROR_OTHER,
                    "Cannot get refresh_task() on node '%s': no properties to refresh",
                    fl);
            g_free (fl);
            return NULL;
        }
        else if (names->len == 1 && refresher_task)
        {
            gchar *fl;

            task = refresher_task (node, names->pdata[0], refresher_data, NULL);
            if (G_LIKELY (task))
            {
                g_ptr_array_unref (names);
                return task;
            }

            /* this really should never be reached */
            fl = donna_node_get_full_location (node);
            g_warning ("refresher_task() for property '%s' on node '%s' returned NULL, "
                    "fallback to internal task and refresher",
                    (gchar *) names->pdata[0], fl);
            g_free (fl);
        }
    }

    /* because if get_tasks_array == TRUE names could still be NULL */
    if (names)
    {
        data = g_slice_new0 (struct refresh_data);
        data->node = g_object_ref (node);
        data->names = names;

        task = donna_task_new ((task_fn) node_refresh, data,
                (GDestroyNotify) free_refresh_data);
        donna_task_set_visibility (task, visibility);

        DONNA_DEBUG (TASK, NULL,
                gchar *location = donna_node_get_location (node);
                donna_task_take_desc (task, g_strdup_printf ("refresh() for %d properties on node '%s:%s'",
                        names->len,
                        donna_node_get_domain (node),
                        location));
                g_free (location));

        if (get_tasks_array)
        {
            if (!tasks)
                tasks = g_ptr_array_new ();
            g_ptr_array_add (tasks, task);
        }
        else
            return task;
    }
    /* get_tasks_array == TRUE */

    if (!tasks)
    {
        gchar *fl = donna_node_get_full_location (node);
        g_set_error (error, DONNA_NODE_ERROR, DONNA_NODE_ERROR_OTHER,
                "Cannot get refresh_tasks_arr() on node '%s': no properties to refresh",
                fl);
        g_free (fl);
        return NULL;
    }

    return tasks;
}

/**
 * donna_node_refresh_task:
 * @node: Node to refresh properties of
 * @error: (allow-none): Return location of a #GError, or %NULL
 * @first_name: Name of the first property to refresh
 * @...: %NULL-terminated list of names of property to refresh
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
    DonnaTask *task;
    va_list va_args;

    va_start (va_args, first_name);
    task = _donna_node_refresh (node, first_name, va_args, FALSE, NULL, error);
    va_end (va_args);

    return task;
}

/**
 * donna_node_refresh_tasks_arr:
 * @node: Node to refresh properties of
 * @tasks: (allow-none): Array to add tasks to
 * @error: (allow-none): Return location of a #GError, or %NULL
 * @first_name: Name of the first property to refresh
 * @...: %NULL-terminated list of names of property to refresh
 *
 * Same as donna_node_refresh_task() but returns an array of #DonnaTask
 * instead of a single one. This can be useful when some properties have their
 * refresher using a #DONNA_TASK_VISIBILITY_INTERNAL_LOOP
 *
 * All tasks are floating, so need to be ref_sinked then unref-ed (or e.g. call
 * donna_app_run_task()). The array itself is @tasks unless it was %NULL, in
 * which case a new #GPtrArray is created, and will need to be unref-ed when
 * done obviously.
 *
 * Returns: (transfer full): A #GPtrArray of floating #DonnaTask<!-- -->s, or
 * %NULL on error
 */
GPtrArray *
donna_node_refresh_tasks_arr (DonnaNode   *node,
                              GError     **error,
                              GPtrArray   *tasks,
                              const gchar *first_name,
                              ...)
{
    va_list va_args;

    va_start (va_args, first_name);
    tasks = _donna_node_refresh (node, first_name, va_args, TRUE, tasks, error);
    va_end (va_args);

    return tasks;
}

static gpointer
_donna_node_refresh_arr (DonnaNode    *node,
                         GPtrArray    *props,
                         gboolean      get_tasks_array,
                         GPtrArray    *tasks,
                         GError      **error)
{
    DonnaTask *task;
    DonnaTaskVisibility visibility = DONNA_TASK_VISIBILITY_INTERNAL_FAST;
    refresher_task_fn refresher_task = NULL;
    gpointer refresher_data = NULL;
    GPtrArray *names = NULL;
    guint i;

    g_return_val_if_fail (DONNA_IS_NODE (node), NULL);
    g_return_val_if_fail (props != NULL, NULL);
    g_return_val_if_fail (props->len > 0, NULL);

    /* because the task will change the array, we need to copy it */

    if (!get_tasks_array)
        /* let's assume all properties exist on node */
        names = g_ptr_array_new_full (props->len, g_free);

    g_rw_lock_reader_lock (&node->priv->props_lock);
    for (i = 0; i < props->len; ++i)
        add_prop_to_arr (node, props->pdata[i],
                &names, (get_tasks_array) ? &tasks : NULL,
                &visibility, &refresher_task, &refresher_data);
    g_rw_lock_reader_unlock (&node->priv->props_lock);
    g_ptr_array_unref (props);

    if (!get_tasks_array)
    {
        if (G_UNLIKELY (names->len == 0))
        {
            gchar *fl = donna_node_get_full_location (node);

            g_ptr_array_unref (names);
            g_set_error (error, DONNA_NODE_ERROR, DONNA_NODE_ERROR_OTHER,
                    "Cannot get refresh_arr_task() on node '%s': no properties to refresh",
                    fl);
            g_free (fl);
            return NULL;
        }
        else if (names->len == 1 && refresher_task)
        {
            gchar *fl;

            task = refresher_task (node, names->pdata[0], refresher_data, NULL);
            if (G_LIKELY (task))
            {
                g_ptr_array_unref (names);
                return task;
            }

            /* this really should never be reached */
            fl = donna_node_get_full_location (node);
            g_warning ("refresher_task() for property '%s' on node '%s' returned NULL, "
                    "fallback to internal task and refresher",
                    (gchar *) names->pdata[0], fl);
            g_free (fl);
        }
    }

    /* because if get_tasks_array == TRUE then names could still be NULL (if no
     * properties were found) */
    if (names)
    {
        struct refresh_data *data;

        data = g_slice_new0 (struct refresh_data);
        data->node  = g_object_ref (node);
        data->names = names;

        task = donna_task_new ((task_fn) node_refresh, data,
                (GDestroyNotify) free_refresh_data);
        donna_task_set_visibility (task, visibility);

        DONNA_DEBUG (TASK, NULL,
                gchar *location = donna_node_get_location (node);
                donna_task_take_desc (task,
                    g_strdup_printf ("refresh_arr() for %d properties on node '%s:%s'",
                        names->len,
                        donna_node_get_domain (node),
                        location));
                g_free (location));

        if (get_tasks_array)
        {
            if (!tasks)
                tasks = g_ptr_array_new ();
            g_ptr_array_add (tasks, task);
        }
        else
            return task;
    }
    /* get_tasks_array == TRUE */

    if (!tasks)
    {
        gchar *fl = donna_node_get_full_location (node);
        g_set_error (error, DONNA_NODE_ERROR, DONNA_NODE_ERROR_OTHER,
                "Cannot get refresh_arr_tasks_arr() on node '%s': no properties to refresh",
                fl);
        g_free (fl);
        return NULL;
    }

    return tasks;
}

/**
 * donna_node_refresh_arr_task:
 * @node: The node to refresh properties of
 * @props: (element-type const gchar *): A #GPtrArray of properties names
 * @error: (allow-none): Return location of a #GError, or %NULL
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
    return _donna_node_refresh_arr (node, props, FALSE, NULL, error);
}

/**
 * donna_node_refresh_arr_tasks_arr:
 * @node: The node to refresh properties of
 * @tasks: (allow-none): Array to add tasks to
 * @props: (element-type const gchar *): A #GPtrArray of properties names
 * @error: (allow-none): Return location of a #GError, or %NULL
 *
 * Same as donna_node_refresh_task_arr() but returns an array of #DonnaTask
 * instead of a single one. This can be useful when some properties have their
 * refresher using a #DONNA_TASK_VISIBILITY_INTERNAL_LOOP
 *
 * All tasks are floating, so need to be ref_sinked then unref-ed (or e.g. call
 * donna_app_run_task()). The array itself is @tasks unless it was %NULL, in
 * which case a new #GPtrArray is created, and will need to be unref-ed when
 * done obviously.
 *
 * Returns: (transfer full): A #GPtrArray of floating #DonnaTask<!-- -->s, or
 * %NULL on error
 */
GPtrArray *
donna_node_refresh_arr_tasks_arr (DonnaNode *node,
                                  GPtrArray *tasks,
                                  GPtrArray *props,
                                  GError   **error)
{
    return _donna_node_refresh_arr (node, props, TRUE, tasks, error);
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

    /* This is for the rare case where the task would start after the node has
     * been marked invalid. Since it could happen, let's avoid calling a setter
     * that might assume the node to be in a valid state which could cause all
     * kinds of issues... */
    if (G_UNLIKELY (data->node->priv->flags & DONNA_NODE_INVALID))
    {
        /* name is now the old full location prefixed w/ "[invalid]" */
        donna_task_set_error (task, DONNA_NODE_ERROR, DONNA_NODE_ERROR_OTHER,
                "Cannot set property '%s' on '%s': Node is invalid",
                data->prop->name, data->node->priv->name);
        return DONNA_TASK_FAILED;
    }

    DONNA_DEBUG (TASK, NULL,
            g_debug3 ("set_property(%s) for '%s:%s'",
                data->prop->name,
                donna_provider_get_domain (data->node->priv->provider),
                data->node->priv->location));
    ret = data->prop->setter (task, data->node, data->prop->name,
            (const GValue *) data->value, data->prop->data);

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

    DONNA_DEBUG (TASK, NULL,
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
 * @error: (allow-none): Return location of a #GError, or %NULL
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
 * @error: (allow-none): Return location of a #GError, or %NULL
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
 * donna_node_trigger_task:
 * @node: Node to trigger
 * @error: (allow-none): Return location of a #GError, or %NULL
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
 * donna_node_new_child_task:
 * @node: Parent of the child to create
 * @type: Type of child to create
 * @name: Name of the new child
 * @error: (allow-none): Return location of a #GError, or %NULL
 *
 * Returns a task to create a new child in @node
 *
 * Note: this is a helper function, that calls donna_provider_new_child_task()
 * or @node's provider
 *
 * Returns: (transfer floating): The floating #DonnaTask, or %NULL
 */
DonnaTask *
donna_node_new_child_task (DonnaNode          *node,
                           DonnaNodeType       type,
                           const gchar        *name,
                           GError            **error)
{
    g_return_val_if_fail (DONNA_IS_NODE (node), NULL);
    return donna_provider_new_child_task (node->priv->provider, node, type,
            name, error);
}

/**
 * donna_node_mark_ready:
 * @node: The node to mark ready
 *
 * This is only meant to be used by @node's provider, once all properties have
 * been added & initial values set. Only after a node has been marked ready will
 * signals be emitted on the provider (e.g. node-updated)
 */
void
donna_node_mark_ready (DonnaNode          *node)
{
    g_return_if_fail (DONNA_IS_NODE (node));
    node->priv->ready = TRUE;
}

/**
 * donna_node_mark_invalid:
 * @node: The node to mark invalid
 * @pinv: #DonnaProviderInvalid
 *
 * This should only be used by the interface #DonnaProvider, if needed, from an
 * idle sourfce after emission of a node-deleted signal. That is, is there are
 * still references on the node, then the provider's virtual function
 * unref_node<!-- -->() will be called, then this to make the node "invalid."
 *
 * This is in case e.g. a task still has a ref on a node (e.g. as return value)
 * to make sure it won't create conflicts in case a new file is created uner the
 * same name.
 */
void
donna_node_mark_invalid (DonnaNode          *node,
                         DonnaProvider      *pinv)
{
    DonnaNodePrivate *priv;
    guint i;

    g_return_if_fail (DONNA_IS_NODE (node));
    priv = node->priv;

    g_rw_lock_writer_lock (&priv->props_lock);
    DONNA_DEBUG (NODE, donna_provider_get_domain (priv->provider),
            g_debug ("mark_invalid() on '%s:%s'",
                donna_provider_get_domain (priv->provider),
                priv->location));

    g_hash_table_remove_all (priv->props);

    g_free (priv->name);
    priv->name = g_strconcat ("[invalid] ", donna_provider_get_domain (priv->provider),
            ":", priv->location, NULL);

    g_free (priv->location);
    priv->location = g_strdup_printf ("%p", node);

    priv->flags = DONNA_NODE_INVALID; //|DONNA_NODE_ICON_EXISTS;
    priv->refresher = (refresher_fn) gtk_true;
    for (i = 0; i < NB_BASIC_PROPS; ++i)
    {
        GType type;

        type = G_VALUE_TYPE (&priv->basic_props[i].value);
        g_value_unset (&priv->basic_props[i].value);
        g_value_init (&priv->basic_props[i].value, type);
        priv->basic_props[i].has_value = DONNA_NODE_VALUE_NONE;
    }

    g_object_unref (priv->provider);
    priv->provider = pinv;

    g_rw_lock_writer_unlock (&priv->props_lock);
}

static void
set_property_value (DonnaNode     *node,
                    const gchar   *name,
                    const GValue  *value,
                    gboolean       can_emit)
{
    DonnaNodePrivate *priv;
    DonnaNodeProp *prop;
    const gchar **s;
    gint i;
    gboolean emit = FALSE;

    g_return_if_fail (DONNA_IS_NODE (node));
    g_return_if_fail (name != NULL);
    priv = node->priv;

    g_rw_lock_writer_lock (&priv->props_lock);
    DONNA_DEBUG (NODE, donna_provider_get_domain (priv->provider),
            g_debug3 ("set_property_value(%s) on '%s:%s'",
                name,
                donna_provider_get_domain (priv->provider),
                priv->location));

    if (streq (name, "name"))
    {
        g_free (priv->name);
        priv->name = g_value_dup_string (value);
        emit = TRUE;
        goto finish;
    }
    else if (streq (name, "filename"))
    {
        g_free (priv->filename);
        priv->filename = g_value_dup_string (value);
        emit = TRUE;
        goto finish;
    }
    else if (streq (name, "location"))
    {
        g_free (priv->location);
        priv->location = g_value_dup_string (value);
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
                g_value_copy (value, &priv->basic_props[i].value);
                /* we assume it worked, w/out checking types, etc because this
                 * should only be used by providers and such, on properties they
                 * are handling, so if they get it wrong, they're seriously
                 * bugged */
                priv->basic_props[i].has_value = DONNA_NODE_VALUE_SET;
            }
            else
            {
                GType type;

                type = G_VALUE_TYPE (&priv->basic_props[i].value);
                g_value_unset (&priv->basic_props[i].value);
                g_value_init (&priv->basic_props[i].value, type);
                priv->basic_props[i].has_value = DONNA_NODE_VALUE_NEED_REFRESH;
            }
            emit = TRUE;
            goto finish;
        }
    }

    /* other prop? */
    prop = g_hash_table_lookup (priv->props, (gpointer) name);
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
    g_rw_lock_writer_unlock (&priv->props_lock);

    if (can_emit && emit && priv->ready)
        donna_provider_node_updated (priv->provider, node, name);
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
    set_property_value (node, name, value, TRUE);
}

/**
 * donna_node_set_property_value_no_signal:
 * @node: The node
 * @name: Name of the property
 * @value: New value of the property
 *
 * Same as donna_node_set_property_value() only will not trigger the emission of
 * the node-updated signal on @node's provider.
 *
 * Note that if you use this, you should emit said signal (using
 * donna_provider_node_updated()) when ready (e.g. outside a lock).
 *
 * To (try to) change the value of a property, use
 * donna_node_set_property_task()
 */
void
donna_node_set_property_value_no_signal (DonnaNode          *node,
                                         const gchar        *name,
                                         const GValue       *value)
{
    set_property_value (node, name, value, FALSE);
}
