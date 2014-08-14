/*
 * donnatella - Copyright (C) 2014 Olivier Brunel
 *
 * provider-mru.c
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

#include "provider-mru.h"
#include "provider.h"
#include "node.h"
#include "app.h"
#include "command.h"
#include "macros.h"
#include "util.h"
#include "debug.h"

struct mru
{
    gchar *id;
    guint max_items;
    gboolean items_are_nodes;
    guint alloc;
    guint len;
    gchar **items;
};

struct _DonnaProviderMruPrivate
{
    GMutex mutex;
    GHashTable *mrus;
};


/* GObject */
static void             provider_mru_contructed         (GObject            *object);
static void             provider_mru_finalize           (GObject            *object);
/* DonnaProvider */
static const gchar *    provider_mru_get_domain         (DonnaProvider      *provider);
static DonnaProviderFlags provider_mru_get_flags        (DonnaProvider      *provider);
static gchar *          provider_mru_get_context_alias_new_nodes (
                                                         DonnaProvider      *provider,
                                                         const gchar        *extra,
                                                         DonnaNode          *location,
                                                         const gchar        *prefix,
                                                         GError            **error);
static gboolean         provider_mru_get_context_item_info (
                                                         DonnaProvider      *provider,
                                                         const gchar        *item,
                                                         const gchar        *extra,
                                                         DonnaContextReference reference,
                                                         DonnaNode          *node_ref,
                                                         get_sel_fn          get_sel,
                                                         gpointer            get_sel_data,
                                                         DonnaContextInfo   *info,
                                                         GError            **error);
/* DonnaProviderBase */
static DonnaTaskState   provider_mru_new_node           (DonnaProviderBase  *provider,
                                                         DonnaTask          *task,
                                                         const gchar        *location);
static DonnaTaskState   provider_mru_has_children       (DonnaProviderBase  *provider,
                                                         DonnaTask          *task,
                                                         DonnaNode          *node,
                                                         DonnaNodeType       node_types);
static DonnaTaskState   provider_mru_get_children       (DonnaProviderBase  *provider,
                                                         DonnaTask          *task,
                                                         DonnaNode          *node,
                                                         DonnaNodeType       node_types);
static DonnaTaskState   provider_mru_new_child          (DonnaProviderBase  *provider,
                                                         DonnaTask          *task,
                                                         DonnaNode          *parent,
                                                         DonnaNodeType       type,
                                                         const gchar        *name);
static DonnaTaskState   provider_mru_remove_from        (DonnaProviderBase  *provider,
                                                         DonnaTask          *task,
                                                         GPtrArray          *nodes,
                                                         DonnaNode          *source);

static void
provider_mru_provider_init (DonnaProviderInterface *interface)
{
    interface->get_domain                   = provider_mru_get_domain;
    interface->get_flags                    = provider_mru_get_flags;
    interface->get_context_alias_new_nodes  = provider_mru_get_context_alias_new_nodes;
    interface->get_context_item_info        = provider_mru_get_context_item_info;
}

G_DEFINE_TYPE_WITH_CODE (DonnaProviderMru, donna_provider_mru,
        DONNA_TYPE_PROVIDER_BASE,
        G_IMPLEMENT_INTERFACE (DONNA_TYPE_PROVIDER, provider_mru_provider_init)
        )

static void
donna_provider_mru_class_init (DonnaProviderMruClass *klass)
{
    DonnaProviderBaseClass *pb_class;
    GObjectClass *o_class;

    o_class = (GObjectClass *) klass;
    o_class->constructed        = provider_mru_contructed;
    o_class->finalize           = provider_mru_finalize;

    pb_class = (DonnaProviderBaseClass *) klass;

    pb_class->task_visibility.new_node      = DONNA_TASK_VISIBILITY_INTERNAL_FAST;
    pb_class->task_visibility.has_children  = DONNA_TASK_VISIBILITY_INTERNAL_FAST;
    pb_class->task_visibility.get_children  = DONNA_TASK_VISIBILITY_INTERNAL;
    pb_class->task_visibility.new_child     = DONNA_TASK_VISIBILITY_INTERNAL_FAST;
    pb_class->task_visibility.remove_from   = DONNA_TASK_VISIBILITY_INTERNAL_FAST;

    pb_class->new_node          = provider_mru_new_node;
    pb_class->has_children      = provider_mru_has_children;
    pb_class->get_children      = provider_mru_get_children;
    pb_class->new_child         = provider_mru_new_child;
    pb_class->remove_from       = provider_mru_remove_from;

    g_type_class_add_private (klass, sizeof (DonnaProviderMruPrivate));
}

static void
free_mru (gpointer data)
{
    struct mru *mru = data;
    guint i;

    g_free (mru->id);
    for (i = 0; i < mru->len; ++i)
        g_free (mru->items[i]);
    g_free (mru->items);
    g_slice_free (struct mru, mru);
}

static void
donna_provider_mru_init (DonnaProviderMru *provider)
{
    DonnaProviderMruPrivate *priv;

    priv = provider->priv = G_TYPE_INSTANCE_GET_PRIVATE (provider,
            DONNA_TYPE_PROVIDER_MRU,
            DonnaProviderMruPrivate);
    g_mutex_init (&priv->mutex);
    priv->mrus = g_hash_table_new_full (g_str_hash, g_str_equal,
            NULL, free_mru);
}

static void
provider_mru_finalize (GObject *object)
{
    DonnaProviderMruPrivate *priv = ((DonnaProviderMru *) object)->priv;

    g_mutex_clear (&priv->mutex);
    g_hash_table_unref (priv->mrus);

    /* chain up */
    G_OBJECT_CLASS (donna_provider_mru_parent_class)->finalize (object);
}


/* assume lock */
static gboolean
add_to_mru (struct mru *mru, gchar *s, gboolean own_s, gchar **removed)
{
    gboolean added = FALSE;
    guint i;

    if (removed)
        *removed = NULL;

    for (i = 0; i < mru->len; ++i)
    {
        if (streq (mru->items[i], s))
        {
            gchar *item = mru->items[i];

            /* already in mru, move it to first place (i.e. last item) */

            if (mru->len > i + 1)
                memmove (&mru->items[i], &mru->items[i + 1],
                        sizeof (gchar *) * (mru->len - i - 1));
            mru->items[mru->len - 1] = item;
            break;
        }
    }

    if (i >= mru->len)
    {
        /* is mru full? */
        if (mru->len == mru->max_items)
        {
            if (removed)
                *removed = mru->items[0];
            else
                g_free (mru->items[0]);
            memmove (&mru->items[0], &mru->items[1],
                    sizeof (gchar *) * (mru->len - 1));
            mru->items[mru->len - 1] = (own_s) ? s : g_strdup (s);
        }
        else
        {
            /* do we need to realloc? */
            if (mru->len + 1 > mru->alloc)
            {
                mru->alloc += 16;
                if (mru->alloc > mru->max_items)
                    mru->alloc = mru->max_items;
                mru->items = g_realloc (mru->items,
                        sizeof (gchar *) * mru->alloc);
            }
            mru->items[mru->len] = (own_s) ? s : g_strdup (s);
            ++mru->len;
        }

        added = TRUE;
    }
    else if (own_s)
        g_free (s);

    return added;
}

/* assume lock */
static void
remove_index_from_mru (struct mru *mru, guint i)
{
    g_free (mru->items[i]);
    if (mru->len > i + 1)
        memmove (&mru->items[i], &mru->items[i + 1],
                sizeof (gchar *) * (mru->len - i - 1));
    mru->items[mru->len - 1] = NULL;
    --mru->len;
}

/* DonnaProvider */

static DonnaProviderFlags
provider_mru_get_flags (DonnaProvider *provider)
{
    g_return_val_if_fail (DONNA_IS_PROVIDER_MRU (provider),
            DONNA_PROVIDER_FLAG_INVALID);
    return DONNA_PROVIDER_FLAG_FLAT;
}

static const gchar *
provider_mru_get_domain (DonnaProvider *provider)
{
    g_return_val_if_fail (DONNA_IS_PROVIDER_MRU (provider), NULL);
    return "mru";
}

static gchar *
provider_mru_get_context_alias_new_nodes (DonnaProvider      *provider,
                                          const gchar        *extra,
                                          DonnaNode          *location,
                                          const gchar        *prefix,
                                          GError            **error)
{
    return g_strconcat (prefix, "new_mru,", prefix, "new_mru:strings", NULL);
}

static gboolean
provider_mru_get_context_item_info (DonnaProvider          *provider,
                                    const gchar            *item,
                                    const gchar            *extra,
                                    DonnaContextReference   reference,
                                    DonnaNode              *node_ref,
                                    get_sel_fn              get_sel,
                                    gpointer                get_sel_data,
                                    DonnaContextInfo       *info,
                                    GError                **error)
{
    if (streq (item, "new_mru"))
    {
        info->is_visible = info->is_sensitive = TRUE;
        if (!extra || streq (extra, "nodes"))
            info->name = "New MRU list (nodes)";
        else if (streq (extra, "strings"))
            info->name = "New MRU list (strings)";
        else
        {
            g_set_error (error, DONNA_CONTEXT_MENU_ERROR,
                    DONNA_CONTEXT_MENU_ERROR_OTHER,
                    "Provider '%s': Invalid extra '%s' for item '%s'",
                    "mru", extra, item);
            return FALSE;
        }
        info->icon_name = "document-new";
        info->trigger = g_strconcat ("command:tv_goto_line (%o, f+s, @mru_new ("
            "@ask_text (Please enter the name of the MRU), ",
            (extra && *extra == 's') ? "strings" : "nodes",
            "))", NULL);
        info->free_trigger = TRUE;
        return TRUE;
    }

    g_set_error (error, DONNA_CONTEXT_MENU_ERROR,
            DONNA_CONTEXT_MENU_ERROR_UNKNOWN_ITEM,
            "Provider '%s': No such context item: '%s'",
            "mru", item);
    return FALSE;
}

/* DonnaProviderBase */

/* assume lock -- if @created is NULL then we assume we need to create it */
static struct mru *
get_mru (DonnaProviderMru    *pmru,
         const gchar         *mru_id,
         guint                max_items,
         gboolean             items_are_nodes,
         gboolean            *created,
         GError             **error)
{
    struct mru *mru = NULL;

    if (*mru_id == ' ' || *mru_id == '\0' || streq (mru_id, "/"))
    {
        g_set_error (error, DONNA_PROVIDER_ERROR,
                DONNA_PROVIDER_ERROR_INVALID_NAME,
                "Provider '%s': Invalid MRU name '%s'",
                "mru", mru_id);
        return NULL;
    }

    if (created)
        mru = g_hash_table_lookup (pmru->priv->mrus, mru_id);
    if (mru)
    {
        *created = FALSE;
        return mru;
    }

    if (max_items == 0)
    {
        DonnaApp *app = ((DonnaProviderBase *) pmru)->app;

        if (!donna_config_get_int (donna_app_peek_config (app),
                    NULL, (gint *) &max_items,
                    "defaults/mru_max_items"))
            max_items = 50;
        else if (max_items > 100)
            max_items = 100;
    }

    mru = g_slice_new0 (struct mru);
    mru->id = g_strdup (mru_id);
    mru->max_items = max_items;
    mru->items_are_nodes = items_are_nodes;
    g_hash_table_insert (pmru->priv->mrus, mru->id, mru);

    if (created)
        *created = TRUE;
    return mru;
}

static DonnaTaskState
setter (DonnaTask       *task,
        DonnaNode       *node,
        const gchar     *name,
        const GValue    *value,
        gpointer         data)
{
    DonnaProviderMru *pmru = data;
    DonnaProviderMruPrivate *priv = pmru->priv;
    struct mru *mru;
    gchar *mru_id;
    guint new_max;
    gboolean reduced = FALSE;
    GPtrArray *removed = NULL;

    /* name == "max_items" */

    mru_id = donna_node_get_location (node);

    g_mutex_lock (&priv->mutex);
    mru = g_hash_table_lookup (pmru->priv->mrus, mru_id);
    if (G_UNLIKELY (!mru))
    {
        g_mutex_unlock (&priv->mutex);
        donna_task_set_error (task, DONNA_PROVIDER_ERROR,
                DONNA_PROVIDER_ERROR_OTHER,
                "Provider '%s': MRU '%s' not found",
                "mru", mru_id);
        g_free (mru_id);
        return DONNA_TASK_FAILED;
    }

    new_max = g_value_get_uint (value);
    if (mru->len > new_max)
    {
        guint i;

        i = mru->len - new_max - 1;
        for (;;)
        {
            if (mru->items_are_nodes)
            {
                if (!removed)
                    removed = g_ptr_array_new_with_free_func (g_free);
                g_ptr_array_add (removed, mru->items[i]);
            }
            else
                g_free (mru->items[i]);

            if (i == 0)
                break;
            --i;
        }

        i = mru->len - new_max;
        mru->len -= i;
        memmove (&mru->items[0], &mru->items[i], sizeof (gchar *) * mru->len);
        memset (&mru->items[mru->len], 0, sizeof (gchar *) * i);
        reduced = TRUE;
    }
    mru->max_items = new_max;
    g_mutex_unlock (&priv->mutex);

    if (reduced)
    {
        GValue v = G_VALUE_INIT;

        if (removed)
        {
            DonnaApp *app = ((DonnaProviderBase *) pmru)->app;
            guint i;

            for (i = 0; i < removed->len; ++i)
            {
                DonnaNode *n;

                n = donna_app_get_node (app, removed->pdata[i], FALSE, NULL);
                if (n)
                {
                    donna_provider_node_removed_from ((DonnaProvider *) pmru, n, node);
                    g_object_unref (n);
                }
            }
            g_ptr_array_unref (removed);
        }

        g_value_init (&v, G_TYPE_UINT);
        g_value_set_uint (&v, mru->len);
        donna_node_set_property_value (node, "nb-items", &v);
        g_value_unset (&v);
    }
    donna_node_set_property_value (node, name, value);

    g_free (mru_id);
    return DONNA_TASK_DONE;
}

/* assume lock */
static DonnaNode *
get_node_for (DonnaProviderMru  *pmru,
              struct mru        *mru,
              GError           **error)
{
    DonnaProviderBase *_provider = (DonnaProviderBase *) pmru;
    DonnaProviderBaseClass *klass;
    DonnaNode *node;
    GValue v = G_VALUE_INIT;

    klass = DONNA_PROVIDER_BASE_GET_CLASS (_provider);
    klass->lock_nodes (_provider);
    node = klass->get_cached_node (_provider, mru->id);
    klass->unlock_nodes (_provider);
    if (node)
        return node;

    node = donna_node_new ((DonnaProvider *) pmru, mru->id,
            (mru->items_are_nodes) ? DONNA_NODE_CONTAINER : DONNA_NODE_ITEM,
            NULL,
            DONNA_TASK_VISIBILITY_INTERNAL_FAST,
            NULL, (refresher_fn) gtk_true,
            NULL,
            mru->id,
            0);
    if (G_UNLIKELY (!node))
    {
        g_set_error (error, DONNA_PROVIDER_ERROR,
                DONNA_PROVIDER_ERROR_OTHER,
                "Provider '%s': Unable to create a new node",
                "mru");
        return NULL;
    }

    g_value_init (&v, G_TYPE_UINT);
    g_value_set_uint (&v, mru->max_items);
    if (!donna_node_add_property (node, "max-items", G_TYPE_UINT, &v,
                DONNA_TASK_VISIBILITY_INTERNAL_FAST,
                NULL,
                (refresher_fn) gtk_true,
                setter,
                pmru,
                NULL,
                error))
    {
        g_prefix_error (error, "Provider '%s': Cannot create node for MRU '%s'; "
                "Failed to add property '%s': ",
                "mru", mru->id, "max-items");
        g_object_unref (node);
        g_value_unset (&v);
        return NULL;
    }
    g_value_unset (&v);

    g_value_init (&v, G_TYPE_UINT);
    g_value_set_uint (&v, mru->len);
    if (!donna_node_add_property (node, "nb-items", G_TYPE_UINT, &v,
                DONNA_TASK_VISIBILITY_INTERNAL_FAST,
                NULL,
                (refresher_fn) gtk_true,
                NULL,
                NULL,
                NULL,
                error))
    {
        g_prefix_error (error, "Provider '%s': Cannot create node for MRU '%s'; "
                "Failed to add property '%s': ",
                "mru", mru->id, "nb-items");
        g_object_unref (node);
        g_value_unset (&v);
        return NULL;
    }
    g_value_unset (&v);

    /* because we have the lock it's not possible the node was created if it
     * didn't exist when we started, as that would require the lock to get the
     * mru, so we can add the node directly */
    klass->lock_nodes (_provider);
    klass->add_node_to_cache (_provider, node);
    klass->unlock_nodes (_provider);

    return node;
}

static DonnaTaskState
provider_mru_new_node (DonnaProviderBase  *_provider,
                       DonnaTask          *task,
                       const gchar        *location)
{
    GError *err = NULL;
    DonnaProviderMru *pmru = (DonnaProviderMru *) _provider;
    DonnaProviderMruPrivate *priv = pmru->priv;
    DonnaProviderBaseClass *klass;
    DonnaNode *node;
    GValue *value;

    if (streq (location, "/"))
    {
        DonnaNode *n;

        node = donna_node_new ((DonnaProvider *) _provider, location,
                DONNA_NODE_CONTAINER,
                NULL,
                DONNA_TASK_VISIBILITY_INTERNAL_FAST,
                NULL, (refresher_fn) gtk_true,
                NULL,
                "MRU lists",
                0);
        if (G_UNLIKELY (!node))
        {
            donna_task_set_error (task, DONNA_PROVIDER_ERROR,
                    DONNA_PROVIDER_ERROR_OTHER,
                    "Provider '%s': Unable to create a new node",
                    "mru");
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
        struct mru *mru;
        gboolean created;

        g_mutex_lock (&priv->mutex);
        mru = get_mru (pmru, location, 0, TRUE, &created, &err);
        if (!mru)
        {
            g_mutex_unlock (&priv->mutex);
            donna_task_take_error (task, err);
            return DONNA_TASK_FAILED;
        }
        node = get_node_for (pmru, mru, &err);
        g_mutex_unlock (&priv->mutex);
        if (G_UNLIKELY (!node))
        {
            g_prefix_error (&err, "Provider '%s': Failed to get node for MRU '%s': ",
                    "mru", mru->id);
            donna_task_take_error (task, err);
            return DONNA_TASK_FAILED;
        }

        if (created)
        {
            DonnaNode *parent;

            klass = DONNA_PROVIDER_BASE_GET_CLASS (_provider);
            klass->lock_nodes (_provider);
            parent = klass->get_cached_node (_provider, "/");
            klass->unlock_nodes (_provider);

            if (parent)
            {
                donna_provider_node_new_child ((DonnaProvider *) _provider,
                        parent, node);
                g_object_unref (parent);
            }
        }
    }

    value = donna_task_grab_return_value (task);
    g_value_init (value, DONNA_TYPE_NODE);
    g_value_take_object (value, node);
    donna_task_release_return_value (task);

    return DONNA_TASK_DONE;
}

static DonnaTaskState
provider_mru_has_children (DonnaProviderBase  *_provider,
                           DonnaTask          *task,
                           DonnaNode          *node,
                           DonnaNodeType       node_types)
{
    DonnaProviderMruPrivate *priv = ((DonnaProviderMru *) _provider)->priv;
    GValue *value;
    gchar *location;
    struct mru *mru;

    location = donna_node_get_location (node);

    if (streq (location, "/"))
    {
        value = donna_task_grab_return_value (task);
        g_value_init (value, G_TYPE_BOOLEAN);
        g_mutex_lock (&priv->mutex);
        g_value_set_boolean (value, g_hash_table_size (priv->mrus) > 0);
        g_mutex_unlock (&priv->mutex);
        donna_task_release_return_value (task);
        goto done;
    }

    g_mutex_lock (&priv->mutex);
    mru = g_hash_table_lookup (priv->mrus, location);
    if (G_UNLIKELY (!mru))
    {
        g_mutex_unlock (&priv->mutex);
        donna_task_set_error (task, DONNA_PROVIDER_ERROR,
                DONNA_PROVIDER_ERROR_OTHER,
                "Provider '%s': Failed to get MRU for node 'mru:%s'",
                "mru", location);
        g_free (location);
        return DONNA_TASK_FAILED;
    }
    else if (G_UNLIKELY (!mru->items_are_nodes))
    {
        g_mutex_unlock (&priv->mutex);
        donna_task_set_error (task, DONNA_PROVIDER_ERROR,
                DONNA_PROVIDER_ERROR_OTHER,
                "Provider '%s': Node 'mru:%s' isn't a container (MRU contains strings)",
                "mru", location);
        g_free (location);
        return DONNA_TASK_FAILED;
    }

    value = donna_task_grab_return_value (task);
    g_value_init (value, G_TYPE_BOOLEAN);
    /* XXX: this is wrong, because we don't check node_types to make sure there
     * are children of the asked type(s). Also, since we don't try to get the
     * nodes, we don't check that said nodes still exist...
     * IOW we might say TRUE here, but getting children will result in an empty
     * array, either because nodes aren't of the node_types type, or because
     * they couldn't be obtained (and items were auto-removed from MRU).
     * We could improve things, but OTOH that's a lot of work for almost no
     * reason, since has_children() isn't likely to ever be used on MRU nodes
     * (And if it was, just means clicking/asking for children will result in no
     * children existing, nothing else)
     */
    g_value_set_boolean (value, mru->len > 0);
    g_mutex_unlock (&priv->mutex);
    donna_task_release_return_value (task);

done:
    g_free (location);
    return DONNA_TASK_DONE;
}

static DonnaTaskState
provider_mru_get_children (DonnaProviderBase  *_provider,
                           DonnaTask          *task,
                           DonnaNode          *node,
                           DonnaNodeType       node_types)
{
    DonnaProviderMru *pmru = (DonnaProviderMru *) _provider;
    DonnaProviderMruPrivate *priv = pmru->priv;
    GValue *value;
    GPtrArray *nodes;
    gchar *location;
    struct mru *mru;
    guint len;
    gchar **items;

    location = donna_node_get_location (node);

    if (streq (location, "/"))
    {
        GHashTableIter iter;

        g_mutex_lock (&priv->mutex);
        nodes = g_ptr_array_new_full (g_hash_table_size (priv->mrus),
                g_object_unref);

        g_hash_table_iter_init (&iter, priv->mrus);
        while (g_hash_table_iter_next (&iter, NULL, (gpointer) &mru))
        {
            GError *err = NULL;
            DonnaNode *n;

            if (mru->items_are_nodes)
            {
                if (!(node_types & DONNA_NODE_CONTAINER))
                    continue;
            }
            else if (!(node_types & DONNA_NODE_ITEM))
                continue;

            n = get_node_for (pmru, mru, &err);
            if (G_UNLIKELY (!n))
            {
                g_mutex_unlock (&priv->mutex);
                g_ptr_array_unref (nodes);
                donna_task_take_error (task, err);
                return DONNA_TASK_FAILED;
            }
            g_ptr_array_add (nodes, n);
        }
        g_mutex_unlock (&priv->mutex);
        goto done;
    }

    g_mutex_lock (&priv->mutex);
    mru = g_hash_table_lookup (priv->mrus, location);
    if (G_UNLIKELY (!mru))
    {
        g_mutex_unlock (&priv->mutex);
        donna_task_set_error (task, DONNA_PROVIDER_ERROR,
                DONNA_PROVIDER_ERROR_OTHER,
                "Provider '%s': Failed to get MRU for node 'mru:%s'",
                "mru", location);
        g_free (location);
        return DONNA_TASK_FAILED;
    }
    else if (!mru->items_are_nodes)
    {
        g_mutex_unlock (&priv->mutex);
        donna_task_set_error (task, DONNA_PROVIDER_ERROR,
                DONNA_PROVIDER_ERROR_OTHER,
                "Provider '%s': Node 'mru:%s' isn't a container (MRU contains strings)",
                "mru", location);
        g_free (location);
        return DONNA_TASK_FAILED;
    }

    if (mru->len > 0)
    {
        guint i;

        len = mru->len;
        items = g_new (gchar *, len);
        for (i = 0; i < len; ++i)
            items[i] = g_strdup (mru->items[i]);
    }
    else
        items = NULL;
    g_mutex_unlock (&priv->mutex);

    nodes = g_ptr_array_new_with_free_func (g_object_unref);
    if (items)
    {
        guint i = len - 1;

        for (;;)
        {
            DonnaNode *n;

            n = donna_app_get_node (_provider->app, items[i], FALSE, NULL);
            if (n)
            {
                if (donna_node_get_node_type (n) & node_types)
                    g_ptr_array_add (nodes, n);
                else
                    g_object_unref (n);
            }
            g_free (items[i]);

            if (i == 0)
                break;
            --i;
        }
        g_free (items);
    }

done:
    value = donna_task_grab_return_value (task);
    g_value_init (value, G_TYPE_PTR_ARRAY);
    g_value_take_boxed (value, nodes);
    donna_task_release_return_value (task);

    g_free (location);
    return DONNA_TASK_DONE;
}

static DonnaTaskState
provider_mru_new_child (DonnaProviderBase  *_provider,
                        DonnaTask          *task,
                        DonnaNode          *parent,
                        DonnaNodeType       type,
                        const gchar        *name)
{
    GError *err = NULL;
    DonnaProviderMru *pmru = (DonnaProviderMru *) _provider;
    DonnaProviderMruPrivate *priv = pmru->priv;
    DonnaNode *node;
    GValue *value;
    gchar *location;
    struct mru *mru;

    if (streq (name, "/"))
    {
        donna_task_set_error (task, DONNA_PROVIDER_ERROR,
                DONNA_PROVIDER_ERROR_ALREADY_EXIST,
                "Provider '%s': Cannot create an MRU '%s' - invalid name",
                "mru", name);
        return DONNA_TASK_FAILED;
    }

    location = donna_node_get_location (parent);
    if (!streq (location, "/"))
    {
        donna_task_set_error (task, DONNA_PROVIDER_ERROR,
                DONNA_PROVIDER_ERROR_NOT_SUPPORTED,
                "Provider '%s': Cannot create new item into a list (mru:%s); "
                "Simply add nodes (e.g. copy, paste from register or command %s)",
                "mru", location, "mru_add_node()");
        g_free (location);
        return DONNA_TASK_FAILED;
    }
    g_free (location);

    g_mutex_lock (&priv->mutex);
    if (g_hash_table_contains (priv->mrus, name))
    {
        g_mutex_unlock (&priv->mutex);
        donna_task_set_error (task, DONNA_PROVIDER_ERROR,
                DONNA_PROVIDER_ERROR_ALREADY_EXIST,
                "Provider '%s': MRU '%s' already exists",
                "filter", name);
        return DONNA_TASK_FAILED;
    }

    mru = get_mru (pmru, name, 0, type == DONNA_NODE_CONTAINER, NULL, &err);
    if (!mru)
    {
        g_mutex_unlock (&priv->mutex);
        donna_task_take_error (task, err);
        return DONNA_TASK_FAILED;
    }
    node = get_node_for (pmru, mru, &err);
    if (!node)
    {
        g_mutex_unlock (&priv->mutex);
        g_prefix_error (&err, "Provider '%s': Failed to get node for MRU '%s': ",
                "mru", mru->id);
        donna_task_take_error (task, err);
        return DONNA_TASK_FAILED;
    }
    g_mutex_unlock (&priv->mutex);

    donna_provider_node_new_child ((DonnaProvider *) pmru, parent, node);

    value = donna_task_grab_return_value (task);
    g_value_init (value, DONNA_TYPE_NODE);
    g_value_take_object (value, node);
    donna_task_release_return_value (task);

    return DONNA_TASK_DONE;
}

static DonnaTaskState
provider_mru_remove_from (DonnaProviderBase  *_provider,
                          DonnaTask          *task,
                          GPtrArray          *nodes,
                          DonnaNode          *source)
{
    DonnaProviderMru *pmru = (DonnaProviderMru *) _provider;
    DonnaProviderMruPrivate *priv = pmru->priv;
    gchar *location;
    GPtrArray *deleted = NULL;
    GString *str = NULL;
    struct mru *mru;
    guint len;
    guint i;

    location = donna_node_get_location (source);
    if (streq (location, "/"))
    {
        /* trying to delete some MRU(s) */

        g_free (location);

        g_mutex_lock (&priv->mutex);
        for (i = 0; i < nodes->len; ++i)
        {
            DonnaNode *node = nodes->pdata[i];
            gchar *s;

            if (donna_node_peek_provider (node) != (DonnaProvider *) pmru)
            {
                if (!str)
                    str = g_string_new (NULL);
                s = donna_node_get_full_location (node);
                g_string_append_printf (str, "\n- Cannot remove '%s': node isn't an MRU",
                        s);
                g_free (s);
                continue;
            }

            if (node == source)
            {
                if (!str)
                    str = g_string_new (NULL);
                g_string_append_printf (str, "\n- Cannot remove '%s'",
                        "mru:/");
                continue;
            }

            s = donna_node_get_location (node);
            if (!g_hash_table_remove (priv->mrus, s))
            {
                if (!str)
                    str = g_string_new (NULL);
                g_string_append_printf (str, "\n- Failed to remove MRU '%s': Not found",
                        s);
            }
            else
            {
                if (!deleted)
                    deleted = g_ptr_array_new ();
                g_ptr_array_add (deleted, node);
            }
            g_free (s);
        }
        g_mutex_unlock (&priv->mutex);

        /* do we need to emit some node-deleted (outside lock) ? */
        if (deleted)
        {
            for (i = 0; i < deleted->len; ++i)
                donna_provider_node_deleted ((DonnaProvider *) pmru, deleted->pdata[i]);
            g_ptr_array_unref (deleted);
        }

        if (str)
        {
            donna_task_set_error (task, DONNA_PROVIDER_ERROR,
                    DONNA_PROVIDER_ERROR_OTHER,
                    "Provider '%s': Couldn't remove all nodes from '%s':\n%s",
                    "mru", "mru:/", str->str);
            g_string_free (str, TRUE);
            return DONNA_TASK_FAILED;
        }

        return DONNA_TASK_DONE;
    }

    /* delete @nodes from @source's MRU */

    g_mutex_lock (&priv->mutex);

    mru = g_hash_table_lookup (priv->mrus, location);
    if (G_UNLIKELY (!mru))
    {
        g_mutex_unlock (&priv->mutex);
        donna_task_set_error (task, DONNA_PROVIDER_ERROR,
                DONNA_PROVIDER_ERROR_OTHER,
                "Provider '%s': Failed to get MRU '%s'",
                "mru", location);
        g_free (location);
        return DONNA_TASK_FAILED;
    }
    if (G_UNLIKELY (!mru->items_are_nodes))
    {
        g_mutex_unlock (&priv->mutex);
        donna_task_set_error (task, DONNA_PROVIDER_ERROR,
                DONNA_PROVIDER_ERROR_OTHER,
                "Provider '%s': Cannot remove nodes from MRU '%s', it contains strings",
                "mru", location);
        g_free (location);
        return DONNA_TASK_FAILED;
    }

    for (i = 0; i < nodes->len; ++i)
    {
        DonnaNode *node = nodes->pdata[i];
        gchar *s = donna_node_get_full_location (node);
        guint j;

        for (j = 0; j < mru->len; ++j)
        {
            if (streq (mru->items[j], s))
            {
                remove_index_from_mru (mru, j);
                if (!deleted)
                    deleted = g_ptr_array_new ();
                g_ptr_array_add (deleted, node);
                j = (guint) -1;
                break;
            }
        }

        if (j != (guint) -1)
        {
            if (!str)
                str = g_string_new (NULL);
            g_string_append_printf (str, "\n- Failed to remove '%s' from MRU '%s': Not found",
                    s, location);
        }

        g_free (s);
    }

    len = mru->len;
    g_mutex_unlock (&priv->mutex);

    /* do we need to emit some node-removed-from (outside lock) ? */
    if (deleted)
    {
        GValue v = G_VALUE_INIT;

        for (i = 0; i < deleted->len; ++i)
            donna_provider_node_removed_from ((DonnaProvider *) pmru,
                    deleted->pdata[i], source);
        g_ptr_array_unref (deleted);

        g_value_init (&v, G_TYPE_UINT);
        g_value_set_uint (&v, len);
        donna_node_set_property_value (source, "nb-items", &v);
        g_value_unset (&v);
    }

    if (str)
    {
        donna_task_set_error (task, DONNA_PROVIDER_ERROR,
                DONNA_PROVIDER_ERROR_OTHER,
                "Provider '%s': Couldn't remove all nodes from MRU '%s':\n%s",
                "mru", location, str->str);
        g_string_free (str, TRUE);
        g_free (location);
        return DONNA_TASK_FAILED;
    }

    g_free (location);
    return DONNA_TASK_DONE;
}

/* commands */

static inline gboolean
ensure_node_is_mru (DonnaProviderMru    *pmru,
                    DonnaNode           *node,
                    gchar              **mru_id,
                    GError             **error)
{
    if (donna_node_peek_provider (node) != (DonnaProvider *) pmru)
    {
        gchar *fl = donna_node_get_full_location (node);
        g_set_error (error, DONNA_COMMAND_ERROR,
                DONNA_COMMAND_ERROR_OTHER,
                "Node '%s' it isn't an MRU list", fl);
        g_free (fl);
        return FALSE;
    }

    *mru_id = donna_node_get_location (node);
    if (streq (*mru_id, "/"))
    {
        g_set_error (error, DONNA_COMMAND_ERROR,
                DONNA_COMMAND_ERROR_OTHER,
                "Node '%s' isn't an MRU list", "mru:/");
        g_free (*mru_id);
        return FALSE;
    }

    return TRUE;
}

/**
 * mru_add_node:
 * @node_mru: Node of an MRU
 * @node: Node to add to the MRU
 *
 * Adds @node to the MRU @node_mru
 *
 * Returns: @node
 */
static DonnaTaskState
cmd_mru_add_node (DonnaTask         *task,
                  DonnaApp          *app,
                  gpointer          *args,
                  DonnaProviderMru  *pmru)
{
    GError *err = NULL;
    DonnaProviderMruPrivate *priv = pmru->priv;
    DonnaNode *node_mru = args[0];
    DonnaNode *node = args[1];

    gchar *mru_id;
    struct mru *mru;
    gboolean added;
    gchar *removed;
    GValue *value;
    guint len;

    if (!ensure_node_is_mru (pmru, node_mru, &mru_id, &err))
    {
        g_prefix_error (&err, "Command '%s': ", "mru_add_node");
        donna_task_take_error (task, err);
        return DONNA_TASK_FAILED;
    }

    g_mutex_lock (&priv->mutex);
    mru = g_hash_table_lookup (priv->mrus, mru_id);
    if (!mru)
    {
        g_mutex_unlock (&priv->mutex);
        donna_task_set_error (task, DONNA_COMMAND_ERROR,
                DONNA_COMMAND_ERROR_OTHER,
                "Command '%s': MRU '%s' not found",
                "mru_add_node", mru_id);
        g_free (mru_id);
        return DONNA_TASK_FAILED;
    }

    if (!mru->items_are_nodes)
    {
        g_mutex_unlock (&priv->mutex);
        donna_task_set_error (task, DONNA_COMMAND_ERROR,
                DONNA_COMMAND_ERROR_OTHER,
                "Command '%s': Cannot add node to MRU '%s', it contains strings",
                "mru_add_node", mru_id);
        g_free (mru_id);
        return DONNA_TASK_FAILED;
    }

    added = add_to_mru (mru, donna_node_get_full_location (node), TRUE, &removed);
    len = mru->len;
    g_mutex_unlock (&priv->mutex);

    if (added)
    {
        if (removed)
        {
            DonnaNode *n;

            n = donna_app_get_node (app, removed, FALSE, NULL);
            if (n)
            {
                donna_provider_node_removed_from ((DonnaProvider *) pmru, n, node_mru);
                g_object_unref (n);
            }
            g_free (removed);
        }

        donna_provider_node_new_child ((DonnaProvider *) pmru, node_mru, node);

        if (!removed)
        {
            GValue v = G_VALUE_INIT;

            g_value_init (&v, G_TYPE_UINT);
            g_value_set_uint (&v, len);
            donna_node_set_property_value (node_mru, "nb-items", &v);
            g_value_unset (&v);
        }
    }

    value = donna_task_grab_return_value (task);
    g_value_init (value, DONNA_TYPE_NODE);
    g_value_set_object (value, node);
    donna_task_release_return_value (task);

    g_free (mru_id);
    return DONNA_TASK_DONE;
}

/**
 * mru_add_string:
 * @node_mru: Node of an MRU
 * @string: String to add to the MRU
 *
 * Adds @string to the MRU @node_mru
 *
 * Returns: @string
 */
static DonnaTaskState
cmd_mru_add_string (DonnaTask         *task,
                    DonnaApp          *app,
                    gpointer          *args,
                    DonnaProviderMru  *pmru)
{
    GError *err = NULL;
    DonnaProviderMruPrivate *priv = pmru->priv;
    DonnaNode *node_mru = args[0];
    gchar *string = args[1];

    gchar *mru_id;
    struct mru *mru;
    gchar *removed;
    GValue *value;
    gboolean added;
    guint len;

    if (!ensure_node_is_mru (pmru, node_mru, &mru_id, &err))
    {
        g_prefix_error (&err, "Command '%s': ", "mru_add_string");
        donna_task_take_error (task, err);
        return DONNA_TASK_FAILED;
    }

    g_mutex_lock (&priv->mutex);
    mru = g_hash_table_lookup (priv->mrus, mru_id);
    if (!mru)
    {
        g_mutex_unlock (&priv->mutex);
        donna_task_set_error (task, DONNA_COMMAND_ERROR,
                DONNA_COMMAND_ERROR_OTHER,
                "Command '%s': MRU '%s' not found",
                "mru_add_string", mru_id);
        g_free (mru_id);
        return DONNA_TASK_FAILED;
    }

    if (mru->items_are_nodes)
    {
        g_mutex_unlock (&priv->mutex);
        donna_task_set_error (task, DONNA_COMMAND_ERROR,
                DONNA_COMMAND_ERROR_OTHER,
                "Command '%s': Cannot add string to MRU '%s', it contains nodes",
                "mru_add_string", mru_id);
        g_free (mru_id);
        return DONNA_TASK_FAILED;
    }

    added = add_to_mru (mru, string, FALSE, &removed);
    len = mru->len;
    g_mutex_unlock (&priv->mutex);

    if (added && !removed)
    {
        GValue v = G_VALUE_INIT;

        g_value_init (&v, G_TYPE_UINT);
        g_value_set_uint (&v, len);
        donna_node_set_property_value (node_mru, "nb-items", &v);
        g_value_unset (&v);
    }
    g_free (removed);

    value = donna_task_grab_return_value (task);
    g_value_init (value, G_TYPE_STRING);
    g_value_set_string (value, string);
    donna_task_release_return_value (task);

    g_free (mru_id);
    return DONNA_TASK_DONE;
}

/**
 * mru_clear:
 * @node_mru: Node of an MRU
 *
 * Clears (i.e. remove all elements) of MRU @node_mru
 */
static DonnaTaskState
cmd_mru_clear (DonnaTask        *task,
               DonnaApp         *app,
               gpointer         *args,
               DonnaProviderMru *pmru)
{
    GError *err = NULL;
    DonnaProviderMruPrivate *priv = pmru->priv;
    DonnaNode *node_mru = args[0];

    gchar *mru_id;
    struct mru *mru;
    GValue v = G_VALUE_INIT;
    guint len;
    gchar **items = NULL;

    if (!ensure_node_is_mru (pmru, node_mru, &mru_id, &err))
    {
        g_prefix_error (&err, "Command '%s': ", "mru_clear");
        donna_task_take_error (task, err);
        return DONNA_TASK_FAILED;
    }

    g_mutex_lock (&priv->mutex);
    mru = g_hash_table_lookup (priv->mrus, mru_id);
    if (!mru)
    {
        g_mutex_unlock (&priv->mutex);
        donna_task_set_error (task, DONNA_COMMAND_ERROR,
                DONNA_COMMAND_ERROR_OTHER,
                "Command '%s': MRU '%s' not found",
                "mru_clear", mru_id);
        g_free (mru_id);
        return DONNA_TASK_FAILED;
    }

    len = mru->len;
    if (len > 0)
    {
        if (mru->items_are_nodes)
        {
            items = mru->items;
            mru->items = NULL;
            mru->alloc = 0;
        }
        else
        {
            guint i;

            for (i = 0; i < len; ++i)
            {
                g_free (mru->items[i]);
                mru->items[i] = NULL;
            }
        }
        mru->len = 0;
    }
    g_mutex_unlock (&priv->mutex);

    if (items)
    {
        guint i;

        for (i = 0; i < len; ++i)
        {
            DonnaNode *n;

            n = donna_app_get_node (app, items[i], FALSE, NULL);
            if (n)
            {
                donna_provider_node_removed_from ((DonnaProvider *) pmru, n, node_mru);
                g_object_unref (n);
            }
            g_free (items[i]);
        }
        g_free (items);
    }

    g_value_init (&v, G_TYPE_UINT);
    g_value_set_uint (&v, 0);
    donna_node_set_property_value (node_mru, "nb-items", &v);
    g_value_unset (&v);

    g_free (mru_id);
    return DONNA_TASK_DONE;
}

/**
 * mru_delete:
 * @node_mru: Node of an MRU
 *
 * Deletes MRU @node_mru
 */
static DonnaTaskState
cmd_mru_delete (DonnaTask        *task,
                DonnaApp         *app,
                gpointer         *args,
                DonnaProviderMru *pmru)
{
    GError *err = NULL;
    DonnaProviderMruPrivate *priv = pmru->priv;
    DonnaNode *node_mru = args[0];

    gchar *mru_id;

    if (!ensure_node_is_mru (pmru, node_mru, &mru_id, &err))
    {
        g_prefix_error (&err, "Command '%s': ", "mru_delete");
        donna_task_take_error (task, err);
        return DONNA_TASK_FAILED;
    }

    g_mutex_lock (&priv->mutex);
    if (!g_hash_table_remove (priv->mrus, mru_id))
    {
        g_mutex_unlock (&priv->mutex);
        donna_task_set_error (task, DONNA_COMMAND_ERROR,
                DONNA_COMMAND_ERROR_OTHER,
                "Command '%s': MRU '%s' not found",
                "mru_delete", mru_id);
        g_free (mru_id);
        return DONNA_TASK_FAILED;
    }
    g_mutex_unlock (&priv->mutex);

    donna_provider_node_deleted ((DonnaProvider *) pmru, node_mru);

    g_free (mru_id);
    return DONNA_TASK_DONE;
}

/**
 * mru_get_nodes:
 * @node_mru: Node of an MRU
 * @max: (allow-none): Number of nodes to return
 *
 * Returns an array of the last @max nodes from @node_mru. If @max is not
 * specified (or 0) then all items are returned.
 *
 * It should be noted that the MRU doesn't have a reference on the actual nodes,
 * and will only ask for the nodes when needed. If a node cannot be obtained
 * (e.g. doesn't exist anymore) it will simply be skipped; So asking the the
 * last 5 items (@max = 5) will always return 5 nodes, unless there are not
 * enough items in the MRU of course.
 *
 * Returns: (array): The last @max nodes from @node_mru
 */
static DonnaTaskState
cmd_mru_get_nodes (DonnaTask        *task,
                   DonnaApp         *app,
                   gpointer         *args,
                   DonnaProviderMru *pmru)
{
    GError *err = NULL;
    DonnaProviderMruPrivate *priv = pmru->priv;
    DonnaNode *node_mru = args[0];
    guint max = GPOINTER_TO_UINT (args[1]); /* opt */

    gchar *mru_id;
    struct mru *mru;
    GPtrArray *items = NULL;
    GPtrArray *nodes = NULL;
    guint last = 0;
    guint mru_len;
    GValue *value;

    if (!ensure_node_is_mru (pmru, node_mru, &mru_id, &err))
    {
        g_prefix_error (&err, "Command '%s': ", "mru_get_nodes");
        donna_task_take_error (task, err);
        return DONNA_TASK_FAILED;
    }

    /* This gets a little more complicated than one could have thought, because
     * we don't want to get the nodes (donna_app_get_node) while under the lock,
     * to avoid possible deadlock (or UI freeze).
     *
     * And things get a bit more complicated because we might be asked for only
     * @max nodes, but getting as many items doesn't guarantee getting as many
     * nodes, in which case we'll need to get the lock back to get more items,
     * but making sure the list hasn't changed (or been deleted) meanwhile!
     *
     * So here's a basic idea of how we do this:
     *
     * items = nodes = NULL
     * idx = 0
     *
     * again:
     *
     * lock
     * want = MIN (max, mru->len);
     * if (items)
     * {
     *      check mru->items & items are the same
     *      if not, drop items, drop nodes, reset idx, start fresh
     * }
     * add to items (want - n_nodes) items starting at idx
     * unlock
     *
     * make nodes from items, starting at idx, success or not: always ++idx
     * if n_nodes < want && n_items < mru_len goto again
     *
     * free items
     * return nodes
     *
     */
    for (;;)
    {
        guint i;
        guint want;

        g_mutex_lock (&priv->mutex);

        mru = g_hash_table_lookup (priv->mrus, mru_id);
        if (!mru)
        {
            g_mutex_unlock (&priv->mutex);
            donna_task_set_error (task, DONNA_COMMAND_ERROR,
                    DONNA_COMMAND_ERROR_OTHER,
                    "Command '%s': MRU '%s' not found",
                    "mru_get_nodes", mru_id);
            g_free (mru_id);
            if (items)
                g_ptr_array_unref (items);
            if (nodes)
                g_ptr_array_unref (nodes);
            return DONNA_TASK_FAILED;
        }

        if (!mru->items_are_nodes)
        {
            g_mutex_unlock (&priv->mutex);
            donna_task_set_error (task, DONNA_COMMAND_ERROR,
                    DONNA_COMMAND_ERROR_OTHER,
                    "Command '%s': Cannot get nodes from MRU '%s', it contains strings",
                    "mru_get_nodes", mru_id);
            g_free (mru_id);
            if (items)
                g_ptr_array_unref (items);
            if (nodes)
                g_ptr_array_unref (nodes);
            return DONNA_TASK_FAILED;
        }

        /* cached for after we unlock */
        mru_len = mru->len;

        if (mru_len == 0)
        {
            g_mutex_unlock (&priv->mutex);
            if (!nodes)
                nodes = g_ptr_array_new_with_free_func (g_object_unref);
            break;
        }

        if (items && items->len > 0)
        {
            gboolean reset = FALSE;

            /* make sure the items we already have/processed are still the same
             * in the MRU, else we start over */

            if (items->len > mru_len)
                reset = TRUE;
            else
            {
                for (i = 0; i < items->len; ++i)
                {
                    /* in mru we start with the last item and move down */
                    if (!streq (items->pdata[i], mru->items[mru_len - 1 - i]))
                    {
                        reset = TRUE;
                        break;
                    }
                }
            }

            if (reset)
            {
                g_ptr_array_set_size (items, 0);
                if (nodes)
                    g_ptr_array_set_size (nodes, 0);
                last = 0;
            }
        }

        want = (max > 0 && max <= mru_len) ? max : mru_len;
        if (nodes)
            /* remove nodes we already have */
            want -= nodes->len;
        /* again, in mru we need to start with the last item and move down */
        i = mru_len - 1;
        if (items)
            i -= items->len;
        /* make want the index of the last item (to stop at) - 1 */
        want = (i > want) ? i - want : 0;
        /* IOW: add items from i to want (step -1) */
        for (;;)
        {
            if (!items)
                items = g_ptr_array_new_with_free_func (g_free);
            g_ptr_array_add (items, g_strdup (mru->items[i]));
            if (i == 0 || --i == want)
                break;
        }

        g_mutex_unlock (&priv->mutex);

        /* outside of the lock, we can (try to) get the nodes */
        for ( ; last < items->len; ++last)
        {
            DonnaNode *n;

            n = donna_app_get_node (app, items->pdata[last], FALSE, NULL);
            if (n)
            {
                if (!nodes)
                    nodes = g_ptr_array_new_with_free_func (g_object_unref);
                g_ptr_array_add (nodes, n);
            }
        }

        want = (max > 0 && max <= mru_len) ? max : mru_len;
        if (nodes)
        {
            if (nodes->len == want)
                break;
        }
        else if (items && items->len == mru_len)
            break;
    }

    if (items)
        g_ptr_array_unref (items);

    value = donna_task_grab_return_value (task);
    g_value_init (value, G_TYPE_PTR_ARRAY);
    g_value_take_boxed (value, nodes);
    donna_task_release_return_value (task);

    g_free (mru_id);
    return DONNA_TASK_DONE;
}

/**
 * mru_get_strings:
 * @node_mru: Node of an MRU
 * @max: (allow-none): Number of strings to return
 *
 * Returns an array of the last @max strings from @node_mru. If @max is not
 * specified (or 0) then all items are returned.
 *
 * Returns: (array): The last @max strings from @node_mru
 */
static DonnaTaskState
cmd_mru_get_strings (DonnaTask        *task,
                     DonnaApp         *app,
                     gpointer         *args,
                     DonnaProviderMru *pmru)
{
    GError *err = NULL;
    DonnaProviderMruPrivate *priv = pmru->priv;
    DonnaNode *node_mru = args[0];
    guint max = GPOINTER_TO_UINT (args[1]); /* opt */

    gchar *mru_id;
    struct mru *mru;
    GPtrArray *strings;
    GValue *value;

    if (!ensure_node_is_mru (pmru, node_mru, &mru_id, &err))
    {
        g_prefix_error (&err, "Command '%s': ", "mru_get_strings");
        donna_task_take_error (task, err);
        return DONNA_TASK_FAILED;
    }

    g_mutex_lock (&priv->mutex);
    mru = g_hash_table_lookup (priv->mrus, mru_id);
    if (!mru)
    {
        g_mutex_unlock (&priv->mutex);
        donna_task_set_error (task, DONNA_COMMAND_ERROR,
                DONNA_COMMAND_ERROR_OTHER,
                "Command '%s': MRU '%s' not found",
                "mru_get_strings", mru_id);
        g_free (mru_id);
        return DONNA_TASK_FAILED;
    }

    if (mru->items_are_nodes)
    {
        g_mutex_unlock (&priv->mutex);
        donna_task_set_error (task, DONNA_COMMAND_ERROR,
                DONNA_COMMAND_ERROR_OTHER,
                "Command '%s': Cannot get strings from MRU '%s', it contains nodes",
                "mru_get_strings", mru_id);
        g_free (mru_id);
        return DONNA_TASK_FAILED;
    }

    strings = g_ptr_array_new_with_free_func (g_free);
    if (mru->len > 0)
    {
        guint i = mru->len - 1;

        for (;;)
        {
            g_ptr_array_add (strings, g_strdup (mru->items[i]));

            if (i == 0 || (max > 0 && strings->len == max))
                break;
            --i;
        }
    }
    g_mutex_unlock (&priv->mutex);

    value = donna_task_grab_return_value (task);
    g_value_init (value, G_TYPE_PTR_ARRAY);
    g_value_take_boxed (value, strings);
    donna_task_release_return_value (task);

    g_free (mru_id);
    return DONNA_TASK_DONE;
}

/**
 * mru_new:
 * @mru_id: Name of the new MRU to create
 * @children: (allow-none): Type of items to be contained in MRU
 * @max_items: (allow-none): Maximum number of items in MRU
 *
 * Creates a new MRU. @children can be either 'nodes' or 'strings' (defaults to
 * 'nodes') whether it'll contain nodes or strings. In the former case, the node
 * of the MRU will be a container, else an item.
 *
 * @max_items defines the maximum number of items that the MRU will be able to
 * hold.
 *
 * Returns: The node of the nelwy-created MRU
 */
static DonnaTaskState
cmd_mru_new (DonnaTask          *task,
             DonnaApp           *app,
             gpointer           *args,
             DonnaProviderMru   *pmru)
{
    GError *err = NULL;
    DonnaProviderMruPrivate *priv = pmru->priv;
    const gchar *mru_id = args[0];
    gchar *children = args[1]; /* opt */
    gint max_items = GPOINTER_TO_INT (args[2]); /* opt */

    const gchar *s_children[] = { "nodes", "strings" };
    gboolean _children[] = { TRUE, FALSE };
    gint c;

    DonnaProviderBaseClass *klass;
    DonnaProviderBase *_provider;
    DonnaNode *node_root;
    struct mru *mru;
    gboolean created;
    DonnaNode *node;
    GValue *value;

    if (children)
    {
        c = _get_choice (s_children, children);
        if (c < 0)
        {
            donna_task_set_error (task, DONNA_COMMAND_ERROR,
                    DONNA_COMMAND_ERROR_OTHER,
                    "Command '%s': Cannot create new MRU, invalid children type: '%s'; "
                    "Must be 'nodes' or 'strings'",
                    "mru-new", children);
            return DONNA_TASK_FAILED;
        }
    }
    else
        /* nodes */
        c = 0;

    if (max_items < 0)
    {
        donna_task_set_error (task, DONNA_COMMAND_ERROR,
                DONNA_COMMAND_ERROR_OTHER,
                "Command '%s': Invalid argument '%s': Must be a positive integer",
                "mru_new", "max_items");
        return DONNA_TASK_FAILED;
    }

    g_mutex_lock (&priv->mutex);
    mru = get_mru (pmru, mru_id, (guint) max_items, _children[c], &created, &err);
    if (!mru)
    {
        g_mutex_unlock (&priv->mutex);
        g_prefix_error (&err, "Command '%s': Cannot create MRU '%s': ",
                "mru_new", mru_id);
        donna_task_take_error (task, err);
        return DONNA_TASK_FAILED;
    }

    if (!created)
    {
        g_mutex_unlock (&priv->mutex);
        donna_task_set_error (task, DONNA_COMMAND_ERROR,
                DONNA_COMMAND_ERROR_OTHER,
                "Command '%s': Cannot create MRU '%s', it already exists",
                "mru_new", mru_id);
        return DONNA_TASK_FAILED;
    }

    node = get_node_for (pmru, mru, &err);
    g_mutex_unlock (&priv->mutex);

    if (!node)
    {
        g_prefix_error (&err, "Command '%s': Failed get node for new MRU '%s': ",
                "mru_new", mru_id);
        donna_task_take_error (task, err);
        return DONNA_TASK_FAILED;
    }

    _provider = (DonnaProviderBase *) pmru;
    klass = DONNA_PROVIDER_BASE_GET_CLASS (_provider);
    klass->lock_nodes (_provider);
    node_root = klass->get_cached_node (_provider, "/");
    klass->unlock_nodes (_provider);

    if (node_root)
    {
        donna_provider_node_new_child ((DonnaProvider *) pmru, node_root, node);
        g_object_unref (node_root);
    }

    value = donna_task_grab_return_value (task);
    g_value_init (value, DONNA_TYPE_NODE);
    g_value_take_object (value, node);
    donna_task_release_return_value (task);

    return DONNA_TASK_DONE;
}

static gint
arr_str_cmp (gconstpointer a, gconstpointer b)
{
    return strcmp (* (const gchar **) a, * (const gchar **) b);
}

/**
 * mru_load:
 * @category: (allow-none): Name of the category to load MRUs from
 * @keep_current: (allow-none): Set to 1 to keep current MRUs
 *
 * Loads MRUs from config (previously saved using mru_save()).If @keep_current
 * is set to 1 hen current MRUs are kept, else they're all removed first.
 *
 * If @category isn't specified "providers/mru/mrus" is used.
 */
static DonnaTaskState
cmd_mru_load (DonnaTask        *task,
              DonnaApp         *app,
              gpointer         *args,
              DonnaProviderMru *pmru)
{
    DonnaProviderMruPrivate *priv = pmru->priv;
    DonnaConfig *config = donna_app_peek_config (app);
    const gchar *category = args[0]; /* opt */
    gboolean keep_current = GPOINTER_TO_INT (args[1]); /* opt */

    GString *str = NULL;
    GPtrArray *deleted = NULL;
    GPtrArray *arr = NULL;
    guint i;

    if (!category)
        category = "providers/mru/mrus";

    g_mutex_lock (&priv->mutex);

    if (!keep_current)
    {
        DonnaProviderBase *_provider = (DonnaProviderBase *) pmru;
        DonnaProviderBaseClass *klass;
        GHashTableIter iter;
        struct mru *mru;

        klass = DONNA_PROVIDER_BASE_GET_CLASS (_provider);
        klass->lock_nodes (_provider);

        g_hash_table_iter_init (&iter, priv->mrus);
        while ((g_hash_table_iter_next (&iter, NULL, (gpointer) &mru)))
        {
            DonnaNode *n;

            n = klass->get_cached_node (_provider, mru->id);
            if (n)
            {
                if (!deleted)
                    deleted = g_ptr_array_new_with_free_func (g_object_unref);
                g_ptr_array_add (deleted, n);
            }
        }

        klass->unlock_nodes (_provider);

        g_hash_table_remove_all (priv->mrus);
    }

    if (!donna_config_list_options (config, &arr,
                DONNA_CONFIG_OPTION_TYPE_NUMBERED, category))
        goto done;

    for (i = 0; i < arr->len; ++i)
    {
        gchar *num = arr->pdata[i];
        struct mru *mru;
        struct mru m = { NULL, };
        GPtrArray *arr_items = NULL;

        if (!donna_config_get_string (config, NULL, &m.id,
                    "%s/%s/id", category, num))
        {
            if (!str)
                str = g_string_new (NULL);
            g_string_append_printf (str, "\n- Cannot load MRU list: "
                    "No option '%s', skipping '%s/%s'",
                    "id", category, num);
            continue;
        }

        if (G_UNLIKELY (g_hash_table_contains (priv->mrus, m.id)))
        {
            if (!str)
                str = g_string_new (NULL);
            g_string_append_printf (str, "\n- Cannot load MRU '%s': Already exists",
                    m.id);
            g_free (m.id);
            continue;
        }

        if (!donna_config_get_int (config, NULL, (gint *) &m.max_items,
                    "%s/%s/max_items", category, num)
                && !donna_config_get_int (config, NULL, (gint *) &m.max_items,
                    "defautls/mru_max_items"))
            m.max_items = 50;
        else if (m.max_items > 100)
            m.max_items = 100;

        if (!donna_config_get_boolean (config, NULL, &m.items_are_nodes,
                    "%s/%s/items_are_nodes", category, num))
            m.items_are_nodes = TRUE;

        if (donna_config_list_options (config, &arr_items,
                    DONNA_CONFIG_OPTION_TYPE_OPTION, "%s/%s",
                    category, num))
        {
            guint j;

            g_ptr_array_sort (arr_items, arr_str_cmp);
            for (j = 0; j < arr_items->len; ++j)
            {
                gchar *opt = arr_items->pdata[j];
                gchar *s;
                size_t len;

                if (!streqn (opt, "item", 4))
                    continue;
                len = strspn (opt + 4, "0123456789");
                if (opt[4 + len] != '\0')
                    continue;

                if (donna_config_get_string (config, NULL, &s, "%s/%s/%s",
                            category, num, opt))
                    add_to_mru (&m, s, TRUE, NULL);
            }
            g_ptr_array_unref (arr_items);
        }

        mru = g_slice_new0 (struct mru);
        memcpy (mru, &m, sizeof (struct mru));
        g_hash_table_insert (priv->mrus, mru->id, mru);
    }
    g_ptr_array_unref (arr);

done:
    g_mutex_unlock (&priv->mutex);

    if (deleted)
    {
        for (i = 0; i < deleted->len; ++i)
            donna_provider_node_deleted ((DonnaProvider *) pmru, deleted->pdata[i]);
        g_ptr_array_unref (deleted);
    }

    if (str)
    {
        donna_task_set_error (task, DONNA_COMMAND_ERROR,
                DONNA_COMMAND_ERROR_OTHER,
                "Command '%s': Failed to load everything:\n%s",
                "mru_load", str->str);
        g_string_free (str, TRUE);
        return DONNA_TASK_FAILED;
    }

    return DONNA_TASK_DONE;
}

/**
 * mru_save:
 * @category: (allow-none): Name of the category to save MRUs to
 *
 * Saves MRUs to config, inside @category. Note that @category will first be
 * removed.
 *
 * If @category isn't specified "providers/mru/mrus" is used.
 */
static DonnaTaskState
cmd_mru_save (DonnaTask        *task,
              DonnaApp         *app,
              gpointer         *args,
              DonnaProviderMru *pmru)
{
    GError *err = NULL;
    DonnaProviderMruPrivate *priv = pmru->priv;
    DonnaConfig *config = donna_app_peek_config (app);
    const gchar *category = args[0]; /* opt */

    GHashTableIter iter;
    struct mru *mru;
    guint i;

    if (!category)
        category = "providers/mru/mrus";

    g_mutex_lock (&priv->mutex);

    donna_config_remove_category (config, NULL, "%s", category);

    g_hash_table_iter_init (&iter, priv->mrus);
    while ((g_hash_table_iter_next (&iter, NULL, (gpointer) &mru)))
    {
        guint j;

        ++i;

        if (!donna_config_set_string (config, &err, mru->id,
                    "%s/%u/id", category, i))
        {
            g_mutex_unlock (&priv->mutex);
            g_prefix_error (&err, "Command '%s': Failed to save MRUs: ",
                    "mru_save");
            donna_task_take_error (task, err);
            return DONNA_TASK_FAILED;
        }

        if (!donna_config_set_int (config, &err, (gint) mru->max_items,
                    "%s/%u/max_items", category, i))
        {
            g_mutex_unlock (&priv->mutex);
            g_prefix_error (&err, "Command '%s': Failed to save MRUs: ",
                    "mru_save");
            donna_task_take_error (task, err);
            return DONNA_TASK_FAILED;
        }

        if (!donna_config_set_boolean (config, &err, mru->items_are_nodes,
                    "%s/%u/items_are_nodes", category, i))
        {
            g_mutex_unlock (&priv->mutex);
            g_prefix_error (&err, "Command '%s': Failed to save MRUs: ",
                    "mru_save");
            donna_task_take_error (task, err);
            return DONNA_TASK_FAILED;
        }

        for (j = 0; j + 1 <= mru->len ; ++j)
        {
            if (!donna_config_set_string (config, &err, mru->items[j],
                        "%s/%u/item%2u", category, i, j + 1))
            {
                g_mutex_unlock (&priv->mutex);
                g_prefix_error (&err, "Command '%s': Failed to save MRUs: ",
                        "mru_save");
                donna_task_take_error (task, err);
                return DONNA_TASK_FAILED;
            }
        }
    }

    g_mutex_unlock (&priv->mutex);

    return DONNA_TASK_DONE;
}


#define add_command(cmd_name, cmd_argc, cmd_visibility, cmd_return_value)     \
if (G_UNLIKELY (!donna_provider_command_add_command (pc, #cmd_name,           \
                (guint) cmd_argc, arg_type, cmd_return_value, cmd_visibility, \
                (command_fn) cmd_##cmd_name, object, NULL, &err)))            \
{                                                                             \
    g_warning ("Provider '%s': Failed to add command '%s': %s",               \
        "mru", #cmd_name, err->message);                                      \
    g_clear_error (&err);                                                     \
}
static void
provider_mru_contructed (GObject *object)
{
    GError *err = NULL;
    DonnaApp *app = ((DonnaProviderBase *) object)->app;

    DonnaProviderCommand *pc;
    DonnaArgType arg_type[8];
    gint i;

    G_OBJECT_CLASS (donna_provider_mru_parent_class)->constructed (object);

    pc = (DonnaProviderCommand *) donna_app_get_provider (app, "command");
    if (G_UNLIKELY (!pc))
    {
        g_warning ("Provider '%s': Failed to add commands, "
                "couldn't get provider '%s'",
                "mru", "command");
        return;
    }

    i = -1;
    arg_type[++i] = DONNA_ARG_TYPE_NODE;
    arg_type[++i] = DONNA_ARG_TYPE_NODE;
    add_command (mru_add_node, ++i, DONNA_TASK_VISIBILITY_INTERNAL_FAST,
            DONNA_ARG_TYPE_NODE);

    i = -1;
    arg_type[++i] = DONNA_ARG_TYPE_NODE;
    arg_type[++i] = DONNA_ARG_TYPE_STRING;
    add_command (mru_add_string, ++i, DONNA_TASK_VISIBILITY_INTERNAL_FAST,
            DONNA_ARG_TYPE_STRING);

    i = -1;
    arg_type[++i] = DONNA_ARG_TYPE_NODE;
    add_command (mru_clear, ++i, DONNA_TASK_VISIBILITY_INTERNAL_FAST,
            DONNA_ARG_TYPE_NOTHING);

    i = -1;
    arg_type[++i] = DONNA_ARG_TYPE_NODE;
    add_command (mru_delete, ++i, DONNA_TASK_VISIBILITY_INTERNAL_FAST,
            DONNA_ARG_TYPE_NOTHING);

    i = -1;
    arg_type[++i] = DONNA_ARG_TYPE_NODE;
    arg_type[++i] = DONNA_ARG_TYPE_INT | DONNA_ARG_IS_OPTIONAL;
    add_command (mru_get_nodes, ++i, DONNA_TASK_VISIBILITY_INTERNAL_FAST,
            DONNA_ARG_TYPE_NODE | DONNA_ARG_IS_ARRAY);

    i = -1;
    arg_type[++i] = DONNA_ARG_TYPE_NODE;
    arg_type[++i] = DONNA_ARG_TYPE_INT | DONNA_ARG_IS_OPTIONAL;
    add_command (mru_get_strings, ++i, DONNA_TASK_VISIBILITY_INTERNAL_FAST,
            DONNA_ARG_TYPE_STRING | DONNA_ARG_IS_ARRAY);

    i = -1;
    arg_type[++i] = DONNA_ARG_TYPE_STRING;
    arg_type[++i] = DONNA_ARG_TYPE_STRING | DONNA_ARG_IS_OPTIONAL;
    arg_type[++i] = DONNA_ARG_TYPE_INT | DONNA_ARG_IS_OPTIONAL;
    add_command (mru_new, ++i, DONNA_TASK_VISIBILITY_INTERNAL_FAST,
            DONNA_ARG_TYPE_NODE);

    i = -1;
    arg_type[++i] = DONNA_ARG_TYPE_STRING | DONNA_ARG_IS_OPTIONAL;
    arg_type[++i] = DONNA_ARG_TYPE_INT | DONNA_ARG_IS_OPTIONAL;
    add_command (mru_load, ++i, DONNA_TASK_VISIBILITY_INTERNAL_FAST,
            DONNA_ARG_TYPE_NOTHING);

    i = -1;
    arg_type[++i] = DONNA_ARG_TYPE_STRING | DONNA_ARG_IS_OPTIONAL;
    add_command (mru_save, ++i, DONNA_TASK_VISIBILITY_INTERNAL_FAST,
            DONNA_ARG_TYPE_NOTHING);

    g_object_unref (pc);
}
#undef add_command
