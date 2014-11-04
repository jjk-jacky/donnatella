/*
 * donnatella - Copyright (C) 2014 Olivier Brunel
 *
 * provider-filter.c
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

#include "provider-filter.h"
#include "provider.h"
#include "filter-private.h"
#include "node.h"
#include "app.h"
#include "command.h"
#include "macros.h"
#include "util.h"
#include "debug.h"

struct _DonnaProviderFilterPrivate
{
    GRecMutex rec_mutex;
    GHashTable *filters;
};


/* GObject */
static void             provider_filter_contructed      (GObject            *object);
static void             provider_filter_finalize        (GObject            *object);
/* DonnaProvider */
static const gchar *    provider_filter_get_domain      (DonnaProvider      *provider);
static DonnaProviderFlags provider_filter_get_flags     (DonnaProvider      *provider);
static gchar *          provider_filter_get_context_alias_new_nodes (
                                                         DonnaProvider      *provider,
                                                         const gchar        *extra,
                                                         DonnaNode          *location,
                                                         const gchar        *prefix,
                                                         GError            **error);
static gboolean         provider_filter_get_context_item_info (
                                                         DonnaProvider      *provider,
                                                         const gchar        *item,
                                                         const gchar        *extra,
                                                         DonnaContextReference reference,
                                                         DonnaNode          *node_ref,
                                                         get_sel_fn          get_sel,
                                                         gpointer            get_sel_data,
                                                         DonnaContextInfo   *info,
                                                         GError            **error);
static DonnaTask *      provider_filter_trigger_node_task (
                                                         DonnaProvider      *provider,
                                                         DonnaNode          *node,
                                                         GError            **error);
/* DonnaProviderBase */
static DonnaTaskState   provider_filter_new_node        (DonnaProviderBase  *provider,
                                                         DonnaTask          *task,
                                                         const gchar        *location);
static DonnaTaskState   provider_filter_has_children    (DonnaProviderBase  *provider,
                                                         DonnaTask          *task,
                                                         DonnaNode          *node,
                                                         DonnaNodeType       node_types);
static DonnaTaskState   provider_filter_get_children    (DonnaProviderBase  *provider,
                                                         DonnaTask          *task,
                                                         DonnaNode          *node,
                                                         DonnaNodeType       node_types);
static DonnaTaskState   provider_filter_new_child       (DonnaProviderBase  *provider,
                                                         DonnaTask          *task,
                                                         DonnaNode          *parent,
                                                         DonnaNodeType       type,
                                                         const gchar        *name);
static DonnaTaskState   provider_filter_remove_from     (DonnaProviderBase  *provider,
                                                         DonnaTask          *task,
                                                         GPtrArray          *nodes,
                                                         DonnaNode          *source);

static DonnaNode *  get_node_for                        (DonnaProviderFilter   *pf,
                                                         gboolean               is_filter,
                                                         gpointer               ident,
                                                         gboolean               create_node,
                                                         GError               **error);
static void
provider_filter_provider_init (DonnaProviderInterface *interface)
{
    interface->get_domain                   = provider_filter_get_domain;
    interface->get_flags                    = provider_filter_get_flags;
    interface->get_context_alias_new_nodes  = provider_filter_get_context_alias_new_nodes;
    interface->get_context_item_info        = provider_filter_get_context_item_info;
    interface->trigger_node_task            = provider_filter_trigger_node_task;
}

G_DEFINE_TYPE_WITH_CODE (DonnaProviderFilter, donna_provider_filter,
        DONNA_TYPE_PROVIDER_BASE,
        G_IMPLEMENT_INTERFACE (DONNA_TYPE_PROVIDER, provider_filter_provider_init)
        )

static void
donna_provider_filter_class_init (DonnaProviderFilterClass *klass)
{
    DonnaProviderBaseClass *pb_class;
    GObjectClass *o_class;

    o_class = (GObjectClass *) klass;
    o_class->constructed        = provider_filter_contructed;
    o_class->finalize           = provider_filter_finalize;

    pb_class = (DonnaProviderBaseClass *) klass;

    pb_class->task_visibility.new_node      = DONNA_TASK_VISIBILITY_INTERNAL_FAST;
    pb_class->task_visibility.has_children  = DONNA_TASK_VISIBILITY_INTERNAL_FAST;
    pb_class->task_visibility.get_children  = DONNA_TASK_VISIBILITY_INTERNAL_FAST;
    pb_class->task_visibility.new_child     = DONNA_TASK_VISIBILITY_INTERNAL_FAST;
    pb_class->task_visibility.remove_from   = DONNA_TASK_VISIBILITY_INTERNAL_FAST;

    pb_class->new_node          = provider_filter_new_node;
    pb_class->has_children      = provider_filter_has_children;
    pb_class->get_children      = provider_filter_get_children;
    pb_class->new_child         = provider_filter_new_child;
    pb_class->remove_from       = provider_filter_remove_from;

    g_type_class_add_private (klass, sizeof (DonnaProviderFilterPrivate));
}

static void
donna_provider_filter_init (DonnaProviderFilter *provider)
{
    DonnaProviderFilterPrivate *priv;

    priv = provider->priv = G_TYPE_INSTANCE_GET_PRIVATE (provider,
            DONNA_TYPE_PROVIDER_FILTER,
            DonnaProviderFilterPrivate);
    g_rec_mutex_init (&priv->rec_mutex);
    priv->filters = g_hash_table_new_full (g_str_hash, g_str_equal,
            g_free, g_object_unref);
}

static void
provider_filter_finalize (GObject *object)
{
    DonnaProviderFilterPrivate *priv = ((DonnaProviderFilter *) object)->priv;

    g_rec_mutex_clear (&priv->rec_mutex);
#ifdef DONNA_DEBUG_ENABLED
    if (donna_debug_flags & DONNA_DEBUG_MEMORY)
    {
        GHashTableIter iter;
        GObject *o;

        g_hash_table_iter_init (&iter, priv->filters);
        while (g_hash_table_iter_next (&iter, NULL, (gpointer) &o))
        {
            if (o->ref_count > 1)
            {
                gchar *s = donna_filter_get_filter ((DonnaFilter *) o);
                g_debug ("Filter '%s' still has %d ref",
                        s, o->ref_count - 1);
                g_free (s);
            }
            g_hash_table_iter_remove (&iter);
        }
    }
    else
#endif
    g_hash_table_unref (priv->filters);

    /* chain up */
    G_OBJECT_CLASS (donna_provider_filter_parent_class)->finalize (object);
}

/* DonnaProvider */

static DonnaProviderFlags
provider_filter_get_flags (DonnaProvider *provider)
{
    g_return_val_if_fail (DONNA_IS_PROVIDER_FILTER (provider),
            DONNA_PROVIDER_FLAG_INVALID);
    return DONNA_PROVIDER_FLAG_FLAT;
}

static const gchar *
provider_filter_get_domain (DonnaProvider *provider)
{
    g_return_val_if_fail (DONNA_IS_PROVIDER_FILTER (provider), NULL);
    return "filter";
}

static gchar *
provider_filter_get_context_alias_new_nodes (DonnaProvider      *provider,
                                             const gchar        *extra,
                                             DonnaNode          *location,
                                             const gchar        *prefix,
                                             GError            **error)
{
    return g_strconcat (prefix, "new_filter", NULL);
}

static gboolean
provider_filter_get_context_item_info (DonnaProvider          *provider,
                                       const gchar            *item,
                                       const gchar            *extra,
                                       DonnaContextReference   reference,
                                       DonnaNode              *node_ref,
                                       get_sel_fn              get_sel,
                                       gpointer                get_sel_data,
                                       DonnaContextInfo       *info,
                                       GError                **error)
{
    if (streq (item, "new_filter"))
    {
        info->is_visible = info->is_sensitive = TRUE;
        info->name = "New Filter";
        info->icon_name = "document-new";
        info->trigger = "command:tv_goto_line (%o, f+s,"
            "@get_node_from (filter, @ask_text (Please enter the filter)))";
        return TRUE;
    }

    g_set_error (error, DONNA_CONTEXT_MENU_ERROR,
            DONNA_CONTEXT_MENU_ERROR_UNKNOWN_ITEM,
            "Provider 'filter': No such context item: '%s'", item);
    return FALSE;
}

/* DonnaProviderBase */

static gboolean refresher (DonnaTask    *task,
                           DonnaNode    *node,
                           const gchar  *name,
                           gpointer      data);

static void
filter_notify (DonnaFilter *filter, GParamSpec *pspec, DonnaProviderFilter *pf)
{
    DonnaProviderBaseClass *klass;
    DonnaProviderBase *_provider = (DonnaProviderBase *) pf;
    DonnaNode *node;
    GValue v = G_VALUE_INIT;
    gchar *s;

    /* only properties we care about/need to update on the node */
    if (!streq (pspec->name, "name") && !streq (pspec->name, "alias")
            && !streq (pspec->name, "icon-name"))
        return;

    s = donna_filter_get_filter (filter);
    klass = DONNA_PROVIDER_BASE_GET_CLASS (_provider);
    klass->lock_nodes (_provider);
    node = klass->get_cached_node (_provider, s);
    klass->unlock_nodes (_provider);
    g_free (s);

    if (!node)
        return;

    g_object_get ((GObject *) filter, pspec->name, &s, NULL);
    g_value_init (&v, G_TYPE_STRING);
    if (s)
        g_value_take_string (&v, s);
    else if (streq (pspec->name, "name"))
        g_value_take_string (&v, donna_filter_get_filter (filter));
    else
        g_value_set_static_string (&v, "");
    donna_node_set_property_value (node, pspec->name, &v);
    g_value_unset (&v);

    /* if icon-name was updated and there's an icon on the node, we need to
     * refresh it */
    if (*pspec->name == 'i'
            && (donna_node_has_property (node, "icon") & DONNA_NODE_PROP_HAS_VALUE))
        refresher (NULL, node, "icon", pf);

    g_object_unref (node);
}

static gboolean
filter_remove (DonnaFilter *filter)
{
    DonnaProviderFilter *pf;
    DonnaProviderFilterPrivate *priv;
    gchar *key;

    pf = (DonnaProviderFilter *) donna_app_get_provider (_donna_filter_peek_app (filter),
            "filter");
    priv = pf->priv;

    /* check if source is destroyed under lock, because filter_load() might
     * force-remove some filters and remove their source, so this ensures that
     * in such a race the source will be destroyed and we won't try to unref a
     * "finalized filter" */
    g_rec_mutex_lock (&priv->rec_mutex);
    if (g_source_is_destroyed (g_main_current_source ()))
    {
        g_rec_mutex_unlock (&priv->rec_mutex);
        g_object_unref (pf);
        return G_SOURCE_REMOVE;
    }

    /* can NOT use g_object_get, as it takes a ref on the object! This also
     * returns "|alias" if there's one, else the filter string. */
    key = _donna_filter_get_key (filter);
    /* will also unref filter */
    g_hash_table_remove (priv->filters, key);
    g_rec_mutex_unlock (&priv->rec_mutex);

    g_free (key);
    g_object_unref (pf);
    return G_SOURCE_REMOVE;
}

/* see node_toggle_ref_cb() in provider-base.c for more. Here we only add a
 * little extra: we don't unref/remove the filter (from hashtable) right away,
 * but after a little delay, on only if it doesn't have any (extra) properties,
 * i.e. alias, desc or icon (since then we need to remember those).
 * Mostly useful since on each location change/new arrangement, all color
 * filters are let go, then loaded again (assuming they stay active). */
static void
filter_toggle_ref_cb (DonnaProviderFilter *pf, DonnaFilter *filter, gboolean is_last)
{
    DonnaProviderFilterPrivate *priv = pf->priv;
    guint timeout;

    g_rec_mutex_lock (&priv->rec_mutex);
    if (is_last)
    {
        if (((GObject *) filter)->ref_count > 1)
        {
            g_rec_mutex_unlock (&priv->rec_mutex);
            return;
        }

        if (g_object_get_data ((GObject *) filter, "_donna_filter_removed"))
        {
            gchar *key;

            /* we're removing this filter, even if it has props and without
             * timeout delay */

            key = _donna_filter_get_key (filter);
            /* will also unref filter */
            g_hash_table_remove (priv->filters, key);
            g_rec_mutex_unlock (&priv->rec_mutex);
            g_free (key);
            return;
        }

        if (_donna_filter_has_props (filter, _DONNA_FILTER_PROP_ALIAS
                    | _DONNA_FILTER_PROP_NAME | _DONNA_FILTER_PROP_ICON_NAME))
        {
            g_rec_mutex_unlock (&priv->rec_mutex);
            return;
        }

        timeout = g_timeout_add_seconds_full (G_PRIORITY_LOW,
                60 * 15, /* 15min */
                (GSourceFunc) filter_remove, filter, NULL);
        g_object_set_data ((GObject *) filter,
                "_donna_filter_timeout", GUINT_TO_POINTER (timeout));
    }
    else
    {
        timeout = GPOINTER_TO_UINT (g_object_steal_data ((GObject *) filter,
                    "_donna_filter_timeout"));
        if (timeout)
            g_source_remove (timeout);
    }
    g_rec_mutex_unlock (&priv->rec_mutex);
}

static DonnaFilter *
get_filter (DonnaProviderFilter *pf,
            const gchar         *location,
            gboolean             create_filter,
            DonnaNode          **node,
            GError             **error)
{
    DonnaProviderFilterPrivate *priv = pf->priv;
    DonnaProviderBaseClass *klass;
    DonnaProviderBase *_provider;
    DonnaFilter *filter;
    DonnaNode *node_root;
    DonnaNode *node_filter;
    GHashTableIter iter;

    g_rec_mutex_lock (&priv->rec_mutex);
    filter = g_hash_table_lookup (priv->filters, location);
    if (filter)
    {
        g_object_ref (filter);
        g_rec_mutex_unlock (&priv->rec_mutex);
        return filter;
    }
    else if (*location == '|' && !strchr (location + 1, '|'))
    {
        /* alias doesn't exist */
        g_rec_mutex_unlock (&priv->rec_mutex);
        g_set_error (error, DONNA_PROVIDER_ERROR,
                DONNA_PROVIDER_ERROR_LOCATION_NOT_FOUND,
                "Provider '%s': No filter with alias '%s'",
                "filter", location + 1);
        return NULL;
    }

    g_hash_table_iter_init (&iter, priv->filters);
    while (g_hash_table_iter_next (&iter, NULL, (gpointer) &filter))
    {
        gboolean match;
        gchar *s;

        s = donna_filter_get_filter (filter);
        match = streq (location, s);
        g_free (s);
        if (match)
        {
            g_rec_mutex_unlock (&priv->rec_mutex);
            return g_object_ref (filter);
        }
    }

    if (!create_filter)
    {
        g_rec_mutex_unlock (&priv->rec_mutex);
        return NULL;
    }

    filter = g_object_new (DONNA_TYPE_FILTER,
            "app",      ((DonnaProviderBase *) pf)->app,
            "filter",   location,
            NULL);
    g_signal_connect (filter, "notify", (GCallback) filter_notify, pf);
    /* add a toggle ref, which adds a strong ref to filter */
    g_object_add_toggle_ref ((GObject *) filter,
            (GToggleNotify) filter_toggle_ref_cb, pf);
    g_hash_table_insert (priv->filters, g_strdup (location), filter);

    /* since we created the filter, we might have to emit a node-new-child */
    _provider = (DonnaProviderBase *) pf;
    klass = DONNA_PROVIDER_BASE_GET_CLASS (_provider);
    klass->lock_nodes (_provider);
    node_root = klass->get_cached_node (_provider, "/");
    klass->unlock_nodes (_provider);
    if (node_root)
        node_filter = get_node_for (pf, TRUE, filter, TRUE, NULL);
    else
        node_filter = NULL;

    g_rec_mutex_unlock (&priv->rec_mutex);

    if (node_filter)
    {
        donna_provider_node_new_child ((DonnaProvider *) pf, node_root, node_filter);
        if (node)
            *node = node_filter;
        else
            g_object_unref (node_filter);
    }
    if (node_root)
        g_object_unref (node_root);

    return filter;
}

static DonnaFilter *
get_filter_from_node (DonnaProviderFilter *pf, DonnaNode *node, GError **error)
{
    DonnaProviderFilterPrivate *priv = pf->priv;
    DonnaFilter *filter;
    gchar *fl;

    /* we get the filter from the node under lock, to handle race where it is
     * being removed at the same time, then we'll properly get NULL and error
     * out. (And if we get the lock first, because we added a ref it won't be
     * removed.) */
    g_rec_mutex_lock (&priv->rec_mutex);
    filter = g_object_get_data ((GObject *) node, "_donna_filter");
    if (filter)
    {
        g_object_ref (filter);
        g_rec_mutex_unlock (&priv->rec_mutex);
        return filter;
    }
    g_rec_mutex_unlock (&priv->rec_mutex);

    /* filter has been removed, a node-deleted is about to be emitted for the
     * node (which will then be invalid), or it is/just did happen. */

    fl = donna_node_get_full_location (node);
    if (streqn (fl, "filter:", strlen ("filter:")))
        g_set_error (error, DONNA_PROVIDER_ERROR,
                DONNA_PROVIDER_ERROR_OTHER,
                "Provider '%s': Filter '%s' was just removed",
                "filter", fl + strlen ("filter:"));
    else
        g_set_error (error, DONNA_PROVIDER_ERROR,
                DONNA_PROVIDER_ERROR_OTHER,
                "Provider '%s': Node '%s' isn't a filter",
                "filter", fl);

    g_free (fl);
    return NULL;
}

static gboolean
refresher (DonnaTask    *task,
           DonnaNode    *node,
           const gchar  *name,
           gpointer      data)
{
    DonnaProviderFilter *pf = data;
    DonnaFilter *filter;
    GValue v = G_VALUE_INIT;
    gchar *s;

    if (!pf)
        /* because data is NULL for basic properties */
        pf = (DonnaProviderFilter *) donna_node_peek_provider (node);

    filter = get_filter_from_node (pf, node, NULL);
    if (!filter)
        return FALSE;

    if (streq (name, "name"))
    {
        g_object_get (filter, "name", &s, NULL);
        if (!s)
            /* no name/desc set, use filter string */
            s = donna_filter_get_filter (filter);

        g_value_init (&v, G_TYPE_STRING);
        g_value_take_string (&v, s);
        donna_node_set_property_value (node, "name", &v);
        g_value_unset (&v);
    }
    else if (streq (name, "icon"))
    {
        g_object_get (filter, "icon-name", &s, NULL);
        if (!s)
            /* if no property icon is set for the filter, use some default */
            s = g_strdup ("text-x-generic");

        g_value_init (&v, G_TYPE_ICON);
        if (*s == '/')
        {
            GFile *file;

            file = g_file_new_for_path (s);
            g_value_take_object (&v, g_file_icon_new (file));
            g_object_unref (file);
        }
        else
            g_value_take_object (&v, g_themed_icon_new (s));
        donna_node_set_property_value (node, "icon", &v);
        g_value_unset (&v);
        g_free (s);
    }
    else if (streq (name, "desc"))
    {
        g_value_init (&v, G_TYPE_STRING);
        g_value_take_string (&v, donna_filter_get_filter (filter));
        donna_node_set_property_value (node, "desc", &v);
        g_value_unset (&v);
    }
    else if (streq (name, "alias"))
    {
        g_object_get (filter, "alias", &s, NULL);
        g_value_init (&v, G_TYPE_STRING);
        if (s)
            g_value_take_string (&v, s);
        else
            g_value_set_static_string (&v, "");
        donna_node_set_property_value (node, "alias", &v);
        g_value_unset (&v);
    }
    else if (streq (name, "icon-name"))
    {
        g_object_get (filter, "icon-name", &s, NULL);
        g_value_init (&v, G_TYPE_STRING);
        if (s)
            g_value_take_string (&v, s);
        else
            g_value_set_static_string (&v, "");
        donna_node_set_property_value (node, "icon-name", &v);
        g_value_unset (&v);
    }

    g_object_unref (filter);
    return TRUE;
}

struct notify
{
    DonnaFilter *filter;
    guint prop;
};

static void
clear_notify (gpointer data)
{
    struct notify *n = data;
    g_object_unref (n->filter);
}

static gboolean
filter_set_alias (DonnaProviderFilter   *pf,
                  DonnaFilter           *filter,
                  const gchar           *alias,
                  GArray               **notify,
                  GError               **error)
{
    DonnaProviderFilterPrivate *priv = pf->priv;
    gchar buf_old[64], *b_old = buf_old;
    gchar buf_new[64], *b_new = buf_new;
    DonnaFilter *old_filter;
    gchar *s;

    /* an alias cannot contain a pipe sign, regardless of where */
    if (strchr (alias, '|'))
    {
        g_set_error (error, DONNA_PROVIDER_ERROR,
                DONNA_PROVIDER_ERROR_OTHER,
                "Provider '%s': Aliases cannot contain pipe sign",
                "filter");
        return FALSE;
    }

    if (strlen (alias) >= 63)
        b_new = g_strconcat ("|", alias, NULL);
    else
        g_snprintf (b_new, 64, "|%s", alias);

    g_rec_mutex_lock (&priv->rec_mutex);

    /* first: if the alias is already in use, remove it */
    old_filter = g_hash_table_lookup (priv->filters, b_new);
    if (old_filter)
    {
        if (old_filter == filter)
        {
            g_rec_mutex_unlock (&priv->rec_mutex);
            return TRUE;
        }

        g_hash_table_insert (priv->filters, donna_filter_get_filter (old_filter),
                g_object_ref (old_filter));
        g_hash_table_remove (priv->filters, b_new);
        _donna_filter_set_alias (old_filter, NULL, FALSE);
    }

    /* then: assign alias to filter */
    g_object_get ((GObject *) filter, "alias", &s, NULL);
    if (s)
    {
        /* we know s != alias, since old_filter would have been filter then, as
         * we do all this under lock */

        if (strlen (s) >= 63)
            b_old = g_strconcat ("|", s, NULL);
        else
            g_snprintf (b_old, 64, "|%s", s);
        g_free (s);
    }
    else
        b_old = donna_filter_get_filter (filter);
    /* take a ref since removing from hashtable will unref it */
    g_object_ref (filter);
    /* remove filter with its old alias/filter string */
    g_hash_table_remove (priv->filters, b_old);
    /* add it with the (new) alias -- the hashmap still/already has a ref on it
     * */
    g_hash_table_insert (priv->filters, g_strdup (b_new), filter);
    /* FALSE: no notify (not under lock) */
    _donna_filter_set_alias (filter, alias, FALSE);

    g_rec_mutex_unlock (&priv->rec_mutex);

    /* now we can emit signals */
    if (notify)
    {
        struct notify n;

        if (!*notify)
        {
            *notify = g_array_new (FALSE, FALSE, sizeof (struct notify));
            g_array_set_clear_func (*notify, clear_notify);
        }

        if (old_filter)
        {
            n.filter = g_object_ref (old_filter);
            n.prop = _DONNA_FILTER_PROP_ALIAS;
            g_array_append_val (*notify, n);
        }

        n.filter = g_object_ref (filter);
        n.prop = _DONNA_FILTER_PROP_ALIAS;
        g_array_append_val (*notify, n);
    }
    else
    {
        if (old_filter)
            g_object_notify ((GObject *) old_filter, "alias");
        g_object_notify ((GObject *) filter, "alias");
    }

    if (b_old != buf_old)
        g_free (b_old);
    if (b_new != buf_new)
        g_free (b_new);
    return TRUE;
}

static DonnaTaskState
setter (DonnaTask       *task,
        DonnaNode       *node,
        const gchar     *name,
        const GValue    *value,
        gpointer         data)
{
    GError *err = NULL;
    DonnaProviderFilter *pf = data;
    DonnaFilter *filter;

    if (!pf)
        /* because data is NULL for basic properties */
        pf = (DonnaProviderFilter *) donna_node_peek_provider (node);

    filter = get_filter_from_node (pf, node, &err);
    if (!filter)
    {
        donna_task_take_error (task, err);
        return DONNA_TASK_FAILED;
    }

    if (streq (name, "name"))
        _donna_filter_set_name (filter, g_value_get_string (value), TRUE);
    else if (streq (name, "alias"))
    {
        if (!filter_set_alias (pf, filter, g_value_get_string (value), NULL, &err))
        {
            donna_task_take_error (task, err);
            g_object_unref (filter);
            return DONNA_TASK_FAILED;
        }
    }
    else if (streq (name, "icon-name"))
        _donna_filter_set_icon_name (filter, g_value_get_string (value), TRUE);
    else
    {
        g_object_unref (filter);
        donna_task_set_error (task, DONNA_PROVIDER_ERROR,
                DONNA_PROVIDER_ERROR_OTHER,
                "Provider '%s': Tried to set unsupported property '%s'",
                "filter", name);
        return DONNA_TASK_FAILED;
    }

    g_object_unref (filter);
    /* no need to call donna_node_set_property_value() since it'll be done
     * automatically upon notify signal from filter (see filter_notify() below)
     * */
    return DONNA_TASK_DONE;
}

static DonnaNode *
get_node_for (DonnaProviderFilter   *pf,
              gboolean               is_filter,
              gpointer               ident,
              gboolean               create_node,
              GError               **error)
{
    DonnaProviderBaseClass *klass;
    DonnaProviderBase *_provider;
    DonnaFilter *filter;
    DonnaNode *node = NULL;
    DonnaNode *n;
    gchar *free_me = NULL;
    gchar *filter_str;
    gchar *s;

    if (!is_filter)
    {
        gchar *location = ident;

        /* get the filter, resolving alias/creating it if needed */
        filter = get_filter (pf, location, TRUE, &node, error);
        if (!filter)
            /* alias not found */
            return NULL;

        /* if the filter was created, and so was the node to emit
         * node-new-child, then we're done */
        if (node)
        {
            g_object_unref (filter);
            return node;
        }

        /* is location an alias? */
        if (*location == '|' && !strchr (location + 1, '|'))
            filter_str = free_me = donna_filter_get_filter (filter);
        else
            filter_str = (gchar *) location;
    }
    else
    {
        filter = ident;
        filter_str = free_me = donna_filter_get_filter (filter);
    }

    /* check cache */
    _provider = (DonnaProviderBase *) pf;
    klass = DONNA_PROVIDER_BASE_GET_CLASS (_provider);
    klass->lock_nodes (_provider);
    node = klass->get_cached_node (_provider, filter_str);
    klass->unlock_nodes (_provider);
    if (node)
    {
        g_free (free_me);
        if (!is_filter)
            g_object_unref (filter);
        return node;
    }
    else if (!create_node)
    {
        g_free (free_me);
        if (!is_filter)
            g_object_unref (filter);
        /* don't set error since it isn't one */
        return NULL;
    }

    g_object_get (filter, "name", &s, NULL);
    node = donna_node_new ((DonnaProvider *) pf, filter_str,
            DONNA_NODE_ITEM,
            NULL,
            DONNA_TASK_VISIBILITY_INTERNAL,
            NULL, refresher,
            setter,
            (s) ? s : filter_str,
            DONNA_NODE_ICON_EXISTS | DONNA_NODE_DESC_EXISTS | DONNA_NODE_NAME_WRITABLE);
    g_free (s);
    if (G_UNLIKELY (!node))
    {
        g_free (free_me);
        if (!is_filter)
            g_object_unref (filter);
        g_set_error (error, DONNA_PROVIDER_ERROR,
                DONNA_PROVIDER_ERROR_OTHER,
                "Provider '%s': Unable to create a new node",
                "filter");
        return NULL;
    }

    g_object_set_data_full ((GObject *) node, "_donna_filter",
            /* if !is_filter then transmit our ref to the node */
            (is_filter) ? g_object_ref (filter) : filter, g_object_unref);

    if (G_UNLIKELY (!donna_node_add_property (node, "alias",
                    G_TYPE_STRING, NULL,
                    DONNA_TASK_VISIBILITY_INTERNAL_FAST,
                    NULL, refresher,
                    setter,
                    pf, NULL,
                    error)))
    {
        g_free (free_me);
        g_prefix_error (error, "Provider '%s': Failed to add property '%s': ",
                "filter", "alias");
        g_object_unref (node);
        return NULL;
    }

    if (G_UNLIKELY (!donna_node_add_property (node, "icon-name",
                    G_TYPE_STRING, NULL,
                    DONNA_TASK_VISIBILITY_INTERNAL_FAST,
                    NULL, refresher,
                    setter,
                    pf, NULL,
                    error)))
    {
        g_free (free_me);
        g_prefix_error (error, "Provider '%s': Failed to add property '%s': ",
                "filter", "icon-name");
        g_object_unref (node);
        return NULL;
    }

    klass->lock_nodes (_provider);
    n = klass->get_cached_node (_provider, filter_str);
    if (n)
    {
        /* already added while we were busy */
        g_object_unref (node);
        node = n;
    }
    else
        klass->add_node_to_cache (_provider, node);
    /* if node for root exists, get it to emit node-new-child */
    n = klass->get_cached_node (_provider, "/");
    klass->unlock_nodes (_provider);

    if (n)
    {
        donna_provider_node_new_child ((DonnaProvider *) _provider, n, node);
        g_object_unref (n);
    }

    g_free (free_me);
    return node;
}

static DonnaTaskState
provider_filter_new_node (DonnaProviderBase  *_provider,
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
                DONNA_NODE_CONTAINER,
                NULL,
                DONNA_TASK_VISIBILITY_INTERNAL_FAST,
                NULL, (refresher_fn) gtk_true,
                NULL,
                "Filters",
                0);
        if (G_UNLIKELY (!node))
        {
            donna_task_set_error (task, DONNA_PROVIDER_ERROR,
                    DONNA_PROVIDER_ERROR_OTHER,
                    "Provider '%s': Unable to create a new node",
                    "filter");
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
        node = get_node_for ((DonnaProviderFilter *) _provider,
                FALSE, (gpointer) location, TRUE, &err);
        if (!node)
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
provider_filter_has_children (DonnaProviderBase  *_provider,
                              DonnaTask          *task,
                              DonnaNode          *node,
                              DonnaNodeType       node_types)
{
    DonnaProviderFilterPrivate *priv = ((DonnaProviderFilter *) _provider)->priv;
    GValue *value;

    value = donna_task_grab_return_value (task);
    g_value_init (value, G_TYPE_BOOLEAN);
    g_rec_mutex_lock (&priv->rec_mutex);
    g_value_set_boolean (value, g_hash_table_size (priv->filters) > 0);
    g_rec_mutex_unlock (&priv->rec_mutex);
    donna_task_release_return_value (task);

    return DONNA_TASK_DONE;
}

static DonnaTaskState
provider_filter_get_children (DonnaProviderBase  *_provider,
                              DonnaTask          *task,
                              DonnaNode          *node,
                              DonnaNodeType       node_types)
{
    DonnaProviderFilter *pf = (DonnaProviderFilter *) _provider;
    DonnaProviderFilterPrivate *priv = pf->priv;
    GValue *value;
    GPtrArray *nodes;

    /* only one container, root. So we get nodes for all known filters */

    if (!(node_types & DONNA_NODE_ITEM))
        /* no containers == return an empty array */
        nodes = g_ptr_array_sized_new (0);
    else
    {
        GHashTableIter iter;
        DonnaFilter *filter;

        g_rec_mutex_lock (&priv->rec_mutex);
        nodes = g_ptr_array_new_full (g_hash_table_size (priv->filters),
                g_object_unref);

        g_hash_table_iter_init (&iter, priv->filters);
        while (g_hash_table_iter_next (&iter, NULL, (gpointer) &filter))
        {
            GError *err = NULL;
            DonnaNode *n;

            n = get_node_for (pf, TRUE, filter, TRUE, &err);
            if (G_UNLIKELY (!n))
            {
                g_rec_mutex_unlock (&priv->rec_mutex);
                g_ptr_array_unref (nodes);
                donna_task_take_error (task, err);
                return DONNA_TASK_FAILED;
            }
            g_ptr_array_add (nodes, n);
        }
        g_rec_mutex_unlock (&priv->rec_mutex);
    }

    value = donna_task_grab_return_value (task);
    g_value_init (value, G_TYPE_PTR_ARRAY);
    g_value_take_boxed (value, nodes);
    donna_task_release_return_value (task);

    return DONNA_TASK_DONE;
}

static DonnaTask *
provider_filter_trigger_node_task (DonnaProvider      *provider,
                                   DonnaNode          *node,
                                   GError            **error)
{
    g_set_error (error, DONNA_PROVIDER_ERROR,
            DONNA_PROVIDER_ERROR_NOT_SUPPORTED,
            "Provider '%s': Triggering node not supported",
            "filter");
    return NULL;
}

static DonnaTaskState
provider_filter_new_child (DonnaProviderBase  *_provider,
                           DonnaTask          *task,
                           DonnaNode          *parent,
                           DonnaNodeType       type,
                           const gchar        *name)
{
    GError *err = NULL;
    DonnaProviderFilter *pf = (DonnaProviderFilter *) _provider;
    DonnaProviderFilterPrivate *priv = pf->priv;
    DonnaNode *node;
    GValue *value;

    if (type == DONNA_NODE_CONTAINER)
    {
        donna_task_set_error (task, DONNA_PROVIDER_ERROR,
                DONNA_PROVIDER_ERROR_NOT_SUPPORTED,
                "Provider '%s': Cannot create a CONTAINER (filters are ITEMs)",
                "filter");
        return DONNA_TASK_FAILED;
    }
    else if ((*name == '|' && !strchr (name + 1, '|')) || streq (name, "/"))
    {
        donna_task_set_error (task, DONNA_PROVIDER_ERROR,
                DONNA_PROVIDER_ERROR_INVALID_NAME,
                "Provider '%s': Invalid filter: '%s'",
                "filter", name);
        return DONNA_TASK_FAILED;
    }

    g_rec_mutex_lock (&priv->rec_mutex);
    if (g_hash_table_contains (priv->filters, name))
    {
        g_rec_mutex_unlock (&priv->rec_mutex);
        donna_task_set_error (task, DONNA_PROVIDER_ERROR,
                DONNA_PROVIDER_ERROR_ALREADY_EXIST,
                "Provider '%s': Filter '%s' already exists",
                "filter", name);
        return DONNA_TASK_FAILED;
    }
    g_rec_mutex_unlock (&priv->rec_mutex);

    /* node-new-child is handled if/when creating the filter (in get_filter) */
    node = get_node_for (pf, FALSE, (gpointer) name, TRUE, &err);
    if (!node)
    {
        donna_task_take_error (task, err);
        return DONNA_TASK_FAILED;
    }

    value = donna_task_grab_return_value (task);
    g_value_init (value, DONNA_TYPE_NODE);
    g_value_take_object (value, node);
    donna_task_release_return_value (task);

    return DONNA_TASK_DONE;
}

static DonnaTaskState
provider_filter_remove_from (DonnaProviderBase  *_provider,
                             DonnaTask          *task,
                             GPtrArray          *nodes,
                             DonnaNode          *source)
{
    DonnaProviderFilter *pf = (DonnaProviderFilter *) _provider;
    DonnaProviderFilterPrivate *priv = pf->priv;
    GPtrArray *deleted = NULL;
    GString *str = NULL;
    guint i;

    /* since we can only ever have one container, our "root" (only ever
     * containing filters), this can only be about deleting filters */
    g_rec_mutex_lock (&priv->rec_mutex);
    for (i = 0; i < nodes->len; ++i)
    {
        DonnaNode *node = nodes->pdata[i];
        DonnaFilter *filter;
        gchar *s;

        if (donna_node_peek_provider (node) != (DonnaProvider *) pf)
        {
            if (!str)
                str = g_string_new (NULL);
            s = donna_node_get_full_location (node);
            g_string_append_printf (str, "\n- Cannot remove '%s': node isn't a filter",
                    s);
            g_free (s);
            continue;
        }

        if (node == source)
        {
            if (!str)
                str = g_string_new (NULL);
            g_string_append_printf (str, "\n- Cannot remove '%s'",
                    "filter:/");
            continue;
        }

        filter = g_object_get_data ((GObject *) node, "_donna_filter");
        if (!filter)
            /* since right above the node was in 'filter' it means:
             * - it still is, node-deleted is imminent as filter has already
             *   been removed right before (node is about to go invalid)
             * - node has now been marked invalid
             * Either way, it was a filter and it has been removed, so we can
             * consider this a success */
            continue;

        /* we can only remove a filter if it isn't used, i.e. there are only 2
         * refs on it right now: provider, and node. And since we're under lock
         * and getting the filter from the node happens under lock, we know it
         * won't get ref-ed */
        if (((GObject *) filter)->ref_count > 2)
        {
            if (!str)
                str = g_string_new (NULL);
            s = donna_node_get_name (node);
            g_string_append_printf (str, "\n- Cannot remove filter '%s': filter in use",
                    s);
            g_free (s);
            continue;
        }

        /* so it is removed right away (no timeout) even if it has a name,
         * alias, ... */
        g_object_set_data ((GObject *) filter, "_donna_filter_removed",
                GUINT_TO_POINTER (1));
        /* will also unref filter, thus triggering the toggle_ref */
        g_object_set_data ((GObject *) node, "_donna_filter", NULL);

        if (!deleted)
            deleted = g_ptr_array_new ();
        g_ptr_array_add (deleted, node);
    }
    g_rec_mutex_unlock (&priv->rec_mutex);

    /* do we need to emit some node-deleted (outside lock) ? */
    if (deleted)
    {
        for (i = 0; i < deleted->len; ++i)
            donna_provider_node_deleted ((DonnaProvider *) pf, deleted->pdata[i]);
        g_ptr_array_unref (deleted);
    }

    if (str)
    {
        donna_task_set_error (task, DONNA_PROVIDER_ERROR,
                DONNA_PROVIDER_ERROR_OTHER,
                "Provider '%s': Couldn't remove all nodes from '%s':\n%s",
                "filter", "filter:/", str->str);
        g_string_free (str, TRUE);
        return DONNA_TASK_FAILED;
    }

    return DONNA_TASK_DONE;
}

/* extra API */

DonnaFilter *
donna_provider_filter_get_filter (DonnaProviderFilter    *pf,
                                  const gchar            *filter_str,
                                  GError                **error)
{
    g_return_val_if_fail (DONNA_IS_PROVIDER_FILTER (pf), NULL);
    g_return_val_if_fail (filter_str != NULL, NULL);

    if (*filter_str == '\0')
        return NULL;
    else
        return get_filter (pf, filter_str, TRUE, NULL, error);
}

DonnaFilter *
donna_provider_filter_get_filter_from_node (DonnaProviderFilter    *pf,
                                            DonnaNode              *node,
                                            GError                **error)
{
    gchar *s;

    g_return_val_if_fail (DONNA_IS_PROVIDER_FILTER (pf), NULL);
    g_return_val_if_fail (DONNA_IS_NODE (node), NULL);

    if (donna_node_peek_provider (node) != (DonnaProvider *) pf
            || donna_node_get_node_type (node) != DONNA_NODE_ITEM)
    {
        gchar *fl = donna_node_get_full_location (node);
        g_set_error (error, DONNA_PROVIDER_ERROR,
                DONNA_PROVIDER_ERROR_INVALID_CALL,
                "Provider '%s': Node '%s' isn't a filter",
                "filter", fl);
        g_free (fl);
        return NULL;
    }

    s = donna_node_get_location (node);
    if (*s == '\0')
    {
        g_free (s);
        return NULL;
    }
    g_free (s);

    return get_filter_from_node (pf, node, error);
}

DonnaNode *
donna_provider_filter_get_node_for_filter (DonnaProviderFilter    *pf,
                                           DonnaFilter            *filter,
                                           GError                **error)
{
    g_return_val_if_fail (DONNA_IS_PROVIDER_FILTER (pf), NULL);
    g_return_val_if_fail (filter == NULL || DONNA_IS_FILTER (filter), NULL);
    if (filter)
        return get_node_for (pf, TRUE, filter, TRUE, error);
    else
        return get_node_for (pf, FALSE, (gpointer) "", TRUE, error);
}


/* commands */

/**
 * filter_ensure_valid:
 * @node: Node of a filter to check
 * @allow_no_filter: (allow-none): Set to 1 for the special node "filter:"
 * (meaning no filter) to be accepted as valid, else it will be an error
 *
 * Ensures that @node is a node for a valid filter. That is, the filter was
 * properly compiled (if it isn't yet, do it), i.e. there isn't any syntax error
 * and all referenced columns exists.
 *
 * If @node isn't node of a filter, or not a valid one, or if it is the special
 * node ("filter:") for no filter (unless @allow_no_filter was set to 1) then
 * the task will fail; Else it returns the given @node.
 *
 * Returns: The node @node
 */
static DonnaTaskState
cmd_filter_ensure_valid (DonnaTask              *task,
                         DonnaApp               *app,
                         gpointer               *args,
                         DonnaProviderFilter    *pf)
{
    GError *err = NULL;
    DonnaNode *node = args[0];
    gboolean allow_no_filter = GPOINTER_TO_INT (args[1]); /* opt */

    DonnaFilter *filter;
    GValue *value;

    filter = donna_provider_filter_get_filter_from_node (pf, node, &err);
    if (err)
    {
        g_prefix_error (&err, "Command '%s': ", "filter_ensure_valid");
        donna_task_take_error (task, err);
        return DONNA_TASK_FAILED;
    }

    /* filter could be NULL for the special node meaning no filter */
    if (filter)
    {
        if (!donna_filter_is_compiled (filter) && !donna_filter_compile (filter, &err))
        {
            g_prefix_error (&err, "Command '%s': ", "filter_ensure_valid");
            donna_task_take_error (task, err);
            g_object_unref (filter);
            return DONNA_TASK_FAILED;
        }
        g_object_unref (filter);
    }
    else if (!allow_no_filter)
    {
        donna_task_set_error (task, DONNA_COMMAND_ERROR,
                DONNA_COMMAND_ERROR_OTHER,
                "Command '%s': Node is the special node for 'no filter'",
                "filter_ensure_valid");
        return DONNA_TASK_FAILED;
    }

    value = donna_task_grab_return_value (task);
    g_value_init (value, DONNA_TYPE_NODE);
    g_value_set_object (value, node);
    donna_task_release_return_value (task);

    return DONNA_TASK_DONE;
}

/**
 * filter_load:
 * @category: (allow-none): Config category where to load filters from
 * @keep_current: (allow-none): Set to 1 to keep current filters
 *
 * Loads filters based on definition found in configuration under @category.
 * Current filters will be removed unless @keep_current is set to 1. Note that
 * removing filters is only done for unused ones.
 *
 * You can save current filters using filter_save()
 */
static DonnaTaskState
cmd_filter_load (DonnaTask              *task,
                 DonnaApp               *app,
                 gpointer               *args,
                 DonnaProviderFilter    *pf)
{
    const gchar *category = args[0]; /* opt */
    gboolean keep = GPOINTER_TO_INT (args[1]); /* opt */

    DonnaProviderFilterPrivate *priv = pf->priv;
    DonnaProviderBaseClass *klass;
    DonnaProviderBase *_provider;
    DonnaNode *node_root;
    DonnaConfig *config;
    GPtrArray *arr = NULL;
    GArray *notify = NULL;
    guint i;

    if (!category)
        category = "providers/filter";

    g_rec_mutex_lock (&priv->rec_mutex);

    if (!keep)
    {
        GHashTableIter iter;
        DonnaFilter *filter;

        g_hash_table_iter_init (&iter, priv->filters);
        while (g_hash_table_iter_next (&iter, NULL, (gpointer) &filter))
        {
            if (((GObject *) filter)->ref_count == 1)
            {
                guint timeout;

                /* filter isn't used, if it has a timeout pending let's remove
                 * it. Timeout works under lock to ensure it'll see the source
                 * has been destroyed and not try removing an already removed
                 * filter */
                timeout = GPOINTER_TO_UINT (g_object_steal_data ((GObject *) filter,
                            "_donna_filter_timeout"));
                if (timeout)
                    g_source_remove (timeout);

                g_hash_table_iter_remove (&iter);
            }
        }
    }

    config = donna_app_peek_config (app);
    donna_config_list_options (config, &arr, DONNA_CONFIG_OPTION_TYPE_NUMBERED,
            "%s", category);
    if (!arr)
    {
        g_rec_mutex_unlock (&priv->rec_mutex);
        return DONNA_TASK_DONE;
    }

    _provider = (DonnaProviderBase *) pf;
    klass = DONNA_PROVIDER_BASE_GET_CLASS (_provider);
    klass->lock_nodes (_provider);
    node_root = klass->get_cached_node (_provider, "/");
    klass->unlock_nodes (_provider);

    for (i = 0; i < arr->len; ++i)
    {
        GError *err = NULL;
        DonnaFilter *filter;
        gboolean is_new;
        gchar *s_num;
        gchar *filter_str;
        gchar *alias;
        gchar *s;

        s_num = arr->pdata[i];

        if (!donna_config_get_string (config, &err, &filter_str, "%s/%s/filter",
                    category, s_num))
        {
            g_warning ("Provider '%s': cannot load filter: %s -- Skipping",
                    "filter", err->message);
            g_clear_error (&err);
            continue;
        }

        if (*filter_str == '|' && !strchr (filter_str + 1, '|'))
        {
            g_warning ("Provider '%s': Invalid filter '%s': "
                    "cannot start with a pipe sign, unless it's a multi-pattern",
                    "filter", filter_str);
            g_free (filter_str);
            continue;
        }

        filter = get_filter (pf, filter_str, FALSE, NULL, NULL);
        is_new = !filter;
        if (is_new)
        {
            filter = g_object_new (DONNA_TYPE_FILTER,
                    "app",      app,
                    "filter",   filter_str,
                    NULL);
            g_signal_connect (filter, "notify", (GCallback) filter_notify, pf);
            /* add a toggle ref, which adds a strong ref to filter */
            g_object_add_toggle_ref ((GObject *) filter,
                    (GToggleNotify) filter_toggle_ref_cb, pf);
        }

        if (donna_config_get_string (config, NULL, &alias, "%s/%s/alias",
                    category, s_num))
        {
            if (is_new)
            {
                if (strchr (alias, '|'))
                {
                    g_warning ("Provider '%s': Cannot set alias '%s' on filter '%s': "
                            "Aliases cannot contain pipe sign",
                            "filter", alias, filter_str);
                    g_free (alias);
                    alias = NULL;
                }
                _donna_filter_set_alias (filter, alias, FALSE);
            }
            else if (!filter_set_alias (pf, filter, alias, &notify, &err))
            {
                g_warning ("Provider '%s': Failed to set alias on filter '%s': %s",
                        "filter", filter_str, err->message);
                g_clear_error (&err);
                g_free (alias);
                alias = NULL;
            }
        }
        else
            alias = NULL;

        if (donna_config_get_string (config, NULL, &s, "%s/%s/name",
                    category, s_num))
        {
            _donna_filter_set_name (filter, s, FALSE);
            g_free (s);
            if (!is_new)
            {
                struct notify n;

                if (!notify)
                {
                    notify = g_array_new (FALSE, FALSE, sizeof (struct notify));
                    g_array_set_clear_func (notify, clear_notify);
                }

                n.filter = g_object_ref (filter);
                n.prop = _DONNA_FILTER_PROP_NAME;
                g_array_append_val (notify, n);
            }
        }

        if (donna_config_get_string (config, NULL, &s, "%s/%s/icon_name",
                    category, s_num))
        {
            _donna_filter_set_icon_name (filter, s, FALSE);
            g_free (s);
            if (!is_new)
            {
                struct notify n;

                if (!notify)
                {
                    notify = g_array_new (FALSE, FALSE, sizeof (struct notify));
                    g_array_set_clear_func (notify, clear_notify);
                }

                n.filter = g_object_ref (filter);
                n.prop = _DONNA_FILTER_PROP_ICON_NAME;
                g_array_append_val (notify, n);
            }
        }

        if (is_new)
        {
            if (alias)
            {
                g_free (filter_str);
                g_hash_table_insert (priv->filters, g_strconcat ("|", alias, NULL), filter);
            }
            else
                g_hash_table_insert (priv->filters, filter_str, filter);

            /* since we created the filter, we might have to emit a node-new-child */
            if (node_root)
            {
                struct notify n;

                if (!notify)
                {
                    notify = g_array_new (FALSE, FALSE, sizeof (struct notify));
                    g_array_set_clear_func (notify, clear_notify);
                }

                n.filter = g_object_ref (filter);
                n.prop = 0; /* i.e. node-new-child */
                g_array_append_val (notify, n);
            }
        }
        else
            g_free (filter_str);
        g_free (alias);
        g_object_unref (filter);
    }
    g_ptr_array_unref (arr);

    g_rec_mutex_unlock (&priv->rec_mutex);

    if (notify)
    {
        for (i = 0; i < notify->len; ++i)
        {
            struct notify *n;
            const gchar *prop = NULL;

            n = &g_array_index (notify, struct notify, i);
            if (n->prop == 0)
            {
                DonnaNode *node;

                /* emit a node-new-child for this filter */
                node = get_node_for (pf, TRUE, n->filter, TRUE, NULL);
                if (node)
                {
                    donna_provider_node_new_child ((DonnaProvider *) pf,
                            node_root, node);
                    g_object_unref (node);
                }
            }
            else if (n->prop == _DONNA_FILTER_PROP_ALIAS)
                prop = "alias";
            else if (n->prop == _DONNA_FILTER_PROP_NAME)
                prop = "name";
            else /* _DONNA_FILTER_PROP_ICON_NAME */
                prop = "icon-name";
            if (prop)
                g_object_notify ((GObject *) n->filter, prop);
        }
        g_array_unref (notify);
    }
    if (node_root)
        g_object_unref (node_root);

    return DONNA_TASK_DONE;
}

/**
 * filter_save:
 * @category: (allow-none): Config category where to save filters to
 *
 * Saves current filters (that have at least one of "alias", "name" and
 * "icon-name" set) in configuration under @category, for later loading with
 * filter_load()
 */
static DonnaTaskState
cmd_filter_save (DonnaTask              *task,
                 DonnaApp               *app,
                 gpointer               *args,
                 DonnaProviderFilter    *pf)
{
    const gchar *category = args[0]; /* opt */

    DonnaProviderFilterPrivate *priv = pf->priv;
    DonnaConfig *config;
    GHashTableIter iter;
    DonnaFilter *filter;
    GPtrArray *arr = NULL;
    guint i;

    if (!category)
        category = "providers/filter";

    config = donna_app_peek_config (app);

    if (donna_config_list_options (config, &arr, DONNA_CONFIG_OPTION_TYPE_NUMBERED,
            "%s", category))
    {
        for (i = 0; i < arr->len; ++i)
            donna_config_remove_category (config, NULL, "%s/%s",
                    category, (gchar *) arr->pdata[i]);
        g_ptr_array_unref (arr);
        arr = NULL;
    }

    g_rec_mutex_lock (&priv->rec_mutex);
    i = 0;
    g_hash_table_iter_init (&iter, priv->filters);
    while (g_hash_table_iter_next (&iter, NULL, (gpointer) &filter))
    {
        GError *err = NULL;
        gchar *filter_str;
        gchar *s;

        if (!_donna_filter_has_props (filter, _DONNA_FILTER_PROP_ALIAS
                    | _DONNA_FILTER_PROP_NAME | _DONNA_FILTER_PROP_ICON_NAME))
            continue;

        ++i;
        filter_str = donna_filter_get_filter (filter);
        if (!donna_config_set_string (config, &err, filter_str, "%s/%u/filter",
                    category, i))
        {
            g_rec_mutex_unlock (&priv->rec_mutex);
            g_prefix_error (&err, "Command '%s': Failed to save filter '%s': %s"
                    " -- Aborting",
                    "filter_save", filter_str, err->message);
            donna_task_take_error (task, err);
            g_free (filter_str);
            return DONNA_TASK_FAILED;
        }

        g_object_get (filter, "alias", &s, NULL);
        if (s && !donna_config_set_string (config, &err, s, "%s/%u/alias",
                    category, i))
        {
            g_rec_mutex_unlock (&priv->rec_mutex);
            g_free (s);
            g_prefix_error (&err, "Command '%s': Failed to save alias for filter '%s': %s"
                    " -- Aborting",
                    "filter_save", filter_str, err->message);
            donna_task_take_error (task, err);
            return DONNA_TASK_FAILED;
        }
        g_free (s);

        g_object_get (filter, "name", &s, NULL);
        if (s && !donna_config_set_string (config, &err, s, "%s/%u/name",
                    category, i))
        {
            g_rec_mutex_unlock (&priv->rec_mutex);
            g_free (s);
            g_prefix_error (&err, "Command '%s': Failed to save name for filter '%s': %s"
                    " -- Aborting",
                    "filter_save", filter_str, err->message);
            donna_task_take_error (task, err);
            return DONNA_TASK_FAILED;
        }
        g_free (s);

        g_object_get (filter, "icon-name", &s, NULL);
        if (s && !donna_config_set_string (config, &err, s, "%s/%u/icon_name",
                    category, i))
        {
            g_rec_mutex_unlock (&priv->rec_mutex);
            g_free (s);
            g_prefix_error (&err,
                    "Command '%s': Failed to save icon name for filter '%s': %s"
                    " -- Aborting",
                    "filter_save", filter_str, err->message);
            donna_task_take_error (task, err);
            return DONNA_TASK_FAILED;
        }
        g_free (s);

        g_free (filter_str);
    }
    g_rec_mutex_unlock (&priv->rec_mutex);

    return DONNA_TASK_DONE;
}

/**
 * filter_set_alias:
 * @dest: Node of destination filter
 * @alias: Alias to set for filter @dest
 * @flags: (allow-none): Flags to import properties/delete original filter
 *
 * Sets alias @alias on filter behind node @dest, optionally importing
 * properties and/or removing original filter behind @alias (if any)
 *
 * The idea is that you not only want to set an alias on a filter, but also
 * import the name and/or icon-name that were set in the filter previously using
 * @alias, even removing said filter.
 * In other words, you are changing the actual filter (i.e. filter string)
 * behind an alias, while preserving its name and icon.
 *
 * @flags must be a combination of 'name', 'icon-name' and 'delete' and defines
 * which properties will be set on filter @dest as they were on the filter
 * originally behind @alias. If a property doesn't exist, it is ignored (i.e. if
 * already set on @dest, it remains unchanged).
 *
 * If 'delete' was set the original filter is then removed, unless it is in use
 * (or a node for said filter exists), in which case it remains untouched (i.e.
 * its name and icon-name aren't cleared, though its alias obvisouly is gone)
 * and the command still succeeds.
 *
 * If unspecified, @flags defaults to "name+icon-name+delete"
 *
 * If there was no filter behind @alias, the command simply sets the alias and
 * succeed.
 *
 * Returns: Node of filter @dest
 */
static DonnaTaskState
cmd_filter_set_alias (DonnaTask             *task,
                      DonnaApp              *app,
                      gpointer              *args,
                      DonnaProviderFilter   *pf)
{
    DonnaProviderFilterPrivate *priv = pf->priv;
    DonnaNode *dest = args[0];
    const gchar *alias = args[1];
    gchar *s_flags = args[2]; /* opt */

    const gchar *_s_flags[] = { "name", "icon-name", "delete" };
    enum fsa
    {
        FSA_NAME        = (1 << 0),
        FSA_ICON_NAME   = (1 << 1),
        FSA_DELETE      = (1 << 2)
    } _flags[] = { FSA_NAME, FSA_ICON_NAME, FSA_DELETE };
    guint flags;

    GError *err = NULL;
    gchar buf[64], *b = buf;
    DonnaFilter *filter_sce;
    DonnaFilter *filter_dst;
    GValue *value;

    if (s_flags)
    {
        flags = _get_flags (_s_flags, _flags, s_flags);
        if (flags == (guint) -1)
        {
            donna_task_set_error (task, DONNA_COMMAND_ERROR,
                    DONNA_COMMAND_ERROR_SYNTAX,
                    "Command '%s': Invalid flags '%s'; "
                    "Must be (a '+'-separated list of) '%s', '%s' and/or '%s'",
                    "filter_set_alias",
                    s_flags,
                    "name", "icon-name", "delete");
            return DONNA_TASK_FAILED;
        }
    }
    else
        flags = FSA_NAME | FSA_ICON_NAME | FSA_DELETE;

    if (donna_node_peek_provider (dest) != (DonnaProvider *) pf
            || donna_node_get_node_type (dest) != DONNA_NODE_ITEM)
    {
        gchar *fl = donna_node_get_full_location (dest);
        donna_task_set_error (task, DONNA_COMMAND_ERROR,
                DONNA_COMMAND_ERROR_OTHER,
                "Command '%s': Node '%s' isn't a filter",
                "filter_set_alias", fl);
        g_free (fl);
        return DONNA_TASK_FAILED;
    }

    if (strchr (alias, '|'))
    {
        donna_task_set_error (task, DONNA_PROVIDER_ERROR,
                DONNA_PROVIDER_ERROR_OTHER,
                "Command '%s': Cannot set alias '%s'; "
                "Aliases cannot contain pipe sign",
                "filter_set_alias", alias);
        return DONNA_TASK_FAILED;
    }

    filter_dst = get_filter_from_node (pf, dest, &err);
    if (!filter_dst)
    {
        g_prefix_error (&err, "Command '%s': Failed to get filter: ",
                "filter_set_alias");
        donna_task_take_error (task, err);
        return DONNA_TASK_FAILED;
    }

    if (g_snprintf (b, 64, "|%s", alias) >= 64)
        b = g_strconcat ("|", alias, NULL);

    g_rec_mutex_lock (&priv->rec_mutex);
    filter_sce = g_hash_table_lookup (priv->filters, b);
    if (filter_sce)
        g_object_ref (filter_sce);
    g_rec_mutex_unlock (&priv->rec_mutex);

    if (b != buf)
        g_free (b);

    if (!filter_set_alias (pf, filter_dst, alias, NULL, &err))
    {
        g_prefix_error (&err, "Command '%s': Failed to set alias: ",
                "filter_set_alias");
        donna_task_take_error (task, err);
        donna_g_object_unref (filter_sce);
        g_object_unref (filter_dst);
        return DONNA_TASK_FAILED;
    }

    if (!filter_sce || filter_sce == filter_dst)
    {
        /* no filter source, or same filter, so nothing to import/delete; i.e.
         * we're done */

        donna_g_object_unref (filter_sce);
        g_object_unref (filter_dst);
        goto done;
    }

    if ((flags & FSA_NAME)
            && _donna_filter_has_props (filter_sce, _DONNA_FILTER_PROP_NAME))
    {
        gchar *name;

        g_object_get (filter_sce, "name", &name, NULL);
        _donna_filter_set_name (filter_dst, name, TRUE);
        g_free (name);
    }

    if ((flags & FSA_ICON_NAME)
            && _donna_filter_has_props (filter_sce, _DONNA_FILTER_PROP_ICON_NAME))
    {
        gchar *icon_name;

        g_object_get (filter_sce, "icon-name", &icon_name, NULL);
        _donna_filter_set_icon_name (filter_dst, icon_name, TRUE);
        g_free (icon_name);
    }

    g_object_unref (filter_dst);

    if (flags & FSA_DELETE)
    {
        /* we need to do this under lock, to make sure e.g. there isn't a node
         * being created/taking a ref on the filter at the same time */
        g_rec_mutex_lock (&priv->rec_mutex);

        /* if filter has more than 2 references (provider and our own), it is in
         * use. We then simply skip it and the task is a success */
        if (((GObject *) filter_sce)->ref_count <= 2)
            /* so it is removed right away (no timeout) even if it has a name,
             * alias, ... */
            g_object_set_data ((GObject *) filter_sce, "_donna_filter_removed",
                    GUINT_TO_POINTER (1));

        g_object_unref (filter_sce);

        g_rec_mutex_unlock (&priv->rec_mutex);
    }
    else
        g_object_unref (filter_sce);

done:
    value = donna_task_grab_return_value (task);
    g_value_init (value, DONNA_TYPE_NODE);
    g_value_set_object (value, dest);
    donna_task_release_return_value (task);

    return DONNA_TASK_DONE;
}

/**
 * filter_resolve_alias:
 * @alias: Filter alias to resolve
 * @default: (allow-none): Default string to return if there's no filter aliased
 * to @alias
 *
 * Returns the filter string of the filter aliased to @alias, or @default if
 * there's none. If @default isn't set an empty string is used.
 *
 * Returns: Filter string for @alias, or @default (or empty string) if not
 * filter alias @alias exist
 */
static DonnaTaskState
cmd_filter_resolve_alias (DonnaTask             *task,
                          DonnaApp              *app,
                          gpointer              *args,
                          DonnaProviderFilter   *pf)
{
    DonnaProviderFilterPrivate *priv = pf->priv;
    const gchar *alias = args[0];
    const gchar *def_filter = args[1]; /* opt */

    DonnaFilter *filter;
    gchar buf[64], *b = buf;
    gchar *filter_str = NULL;
    GValue *value;

    if (strchr (alias, '|'))
    {
        donna_task_set_error (task, DONNA_PROVIDER_ERROR,
                DONNA_PROVIDER_ERROR_LOCATION_NOT_FOUND,
                "Command '%s': Invalid alias '%s': Aliases cannot contain pipe sign",
                "filter_resolve_alias", alias);
        return DONNA_TASK_FAILED;
    }

    if (g_snprintf (b, 64, "|%s", alias) >= 64)
        b = g_strconcat ("|", alias, NULL);

    g_rec_mutex_lock (&priv->rec_mutex);
    filter = g_hash_table_lookup (priv->filters, b);
    if (filter)
        filter_str = donna_filter_get_filter (filter);
    g_rec_mutex_unlock (&priv->rec_mutex);

    if (b != buf)
        g_free (b);

    value = donna_task_grab_return_value (task);
    g_value_init (value, G_TYPE_STRING);
    if (filter_str)
        g_value_take_string (value, filter_str);
    else if (def_filter)
        g_value_set_string (value, def_filter);
    else
        g_value_set_static_string (value, "");
    donna_task_release_return_value (task);

    return DONNA_TASK_DONE;
}


#define add_command(cmd_name, cmd_argc, cmd_visibility, cmd_return_value)     \
if (G_UNLIKELY (!donna_provider_command_add_command (pc, #cmd_name,           \
                (guint) cmd_argc, arg_type, cmd_return_value, cmd_visibility, \
                (command_fn) cmd_##cmd_name, object, NULL, &err)))            \
{                                                                             \
    g_warning ("Provider '%s': Failed to add command '%s': %s",               \
        "filter", #cmd_name, err->message);                                   \
    g_clear_error (&err);                                                     \
}
static void
provider_filter_contructed (GObject *object)
{
    GError *err = NULL;
    DonnaApp *app = ((DonnaProviderBase *) object)->app;

    DonnaProviderCommand *pc;
    DonnaArgType arg_type[8];
    gint i;

    G_OBJECT_CLASS (donna_provider_filter_parent_class)->constructed (object);

    pc = (DonnaProviderCommand *) donna_app_get_provider (app, "command");
    if (G_UNLIKELY (!pc))
    {
        g_warning ("Provider '%s': Failed to add commands, "
                "couldn't get provider '%s'",
                "filter", "command");
        return;
    }

    i = -1;
    arg_type[++i] = DONNA_ARG_TYPE_NODE;
    arg_type[++i] = DONNA_ARG_TYPE_INT | DONNA_ARG_IS_OPTIONAL;
    add_command (filter_ensure_valid, ++i, DONNA_TASK_VISIBILITY_INTERNAL_FAST,
            DONNA_ARG_TYPE_NODE);

    i = -1;
    arg_type[++i] = DONNA_ARG_TYPE_STRING | DONNA_ARG_IS_OPTIONAL;
    arg_type[++i] = DONNA_ARG_TYPE_INT | DONNA_ARG_IS_OPTIONAL;
    add_command (filter_load, ++i, DONNA_TASK_VISIBILITY_INTERNAL_FAST,
            DONNA_ARG_TYPE_NOTHING);

    i = -1;
    arg_type[++i] = DONNA_ARG_TYPE_STRING | DONNA_ARG_IS_OPTIONAL;
    add_command (filter_save, ++i, DONNA_TASK_VISIBILITY_INTERNAL_FAST,
            DONNA_ARG_TYPE_NOTHING);

    i = -1;
    arg_type[++i] = DONNA_ARG_TYPE_NODE;
    arg_type[++i] = DONNA_ARG_TYPE_STRING;
    arg_type[++i] = DONNA_ARG_TYPE_STRING | DONNA_ARG_IS_OPTIONAL;
    add_command (filter_set_alias, ++i, DONNA_TASK_VISIBILITY_INTERNAL_FAST,
            DONNA_ARG_TYPE_NODE);

    i = -1;
    arg_type[++i] = DONNA_ARG_TYPE_STRING;
    arg_type[++i] = DONNA_ARG_TYPE_STRING | DONNA_ARG_IS_OPTIONAL;
    add_command (filter_resolve_alias, ++i, DONNA_TASK_VISIBILITY_INTERNAL_FAST,
            DONNA_ARG_TYPE_STRING);

    g_object_unref (pc);
}
#undef add_command
