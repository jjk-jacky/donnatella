
#define _GNU_SOURCE             /* strchrnul() in string.h */
#include <gtk/gtk.h>
#include <string.h>             /* strchr(), memcmp() */
#include "treeview.h"
#include "common.h"
#include "provider.h"
#include "node.h"
#include "task.h"
#include "sharedstring.h"
#include "macros.h"

enum
{
    DONNA_TREE_COL_NODE = 0,
    DONNA_TREE_COL_EXPAND_STATE,
    DONNA_TREE_COL_NAME,
    DONNA_TREE_COL_ICON,
    DONNA_TREE_COL_BOX,
    DONNA_TREE_COL_HIGHLIGHT,
    DONNA_TREE_NB_COLS
};

enum
{
    DONNA_LIST_COL_NODE = 0,
    DONNA_LIST_NB_COLS
};

/* this column exists in both modes, and must have the same id */
#define DONNA_TREE_VIEW_COL_NODE     0

enum tree_expand
{
    DONNA_TREE_EXPAND_UNKNOWN = 0,  /* not known if node has children */
    DONNA_TREE_EXPAND_NONE,         /* node doesn't have children */
    DONNA_TREE_EXPAND_NEVER,        /* never expanded, children unknown */
    DONNA_TREE_EXPAND_NEVER_FULL,   /* never expanded, but children are there */
    DONNA_TREE_EXPAND_WIP,          /* we have a running task getting children */
    DONNA_TREE_EXPAND_PARTIAL,      /* minitree: only some children are listed */
    DONNA_TREE_EXPAND_FULL,         /* (was) expanded, children are there */
};

enum
{
    DONNA_TREE_VIEW_MODE_LIST = 0,
    DONNA_TREE_VIEW_MODE_TREE,
};

enum tree_sync
{
    DONNA_TREE_SYNC_NONE = 0,
    DONNA_TREE_SYNC_NODES,
    DONNA_TREE_SYNC_NODES_CHILDREN,
    DONNA_TREE_SYNC_FULL
};

enum
{
    RENDERER_TEXT,
    RENDERER_PIXBUF,
    RENDERER_PROGRESS,
    RENDERER_COMBO,
    RENDERER_TOGGLE,
    NB_RENDERERS
};

enum
{
    SORT_UNKNOWN = 0,
    SORT_ASC,
    SORT_DESC
};

struct col_prop
{
    DonnaSharedString *prop;
    GtkTreeViewColumn *column;
};

struct provider_signals
{
    DonnaProvider   *provider;
    guint            nb_nodes;
    gulong           sid_node_updated;
    gulong           sid_node_removed;
    gulong           sid_node_children;
    gulong           sid_node_new_child;
};

struct _DonnaTreeViewPrivate
{
    DonnaDonna          *donna;
    const gchar         *name;

    /* so we re-use the same renderer for all columns */
    GtkCellRenderer     *renderers[NB_RENDERERS];

    /* current arrangement */
    DonnaSharedString   *arrangement;

    /* properties used by our columns */
    GArray              *col_props;

    /* List: current/future location */
    DonnaNode           *location;
    DonnaNode           *future_location;

    /* hashtable of nodes & their iters on TV */
    GHashTable          *hashtable;

    /* providers we're connected to */
    GPtrArray           *providers;

    /* list of props on nodes being refreshed (see refresh_node_prop_cb) */
    GMutex               refresh_node_props_mutex;
    GSList              *refresh_node_props;

    /* "cached" options */
    guint                mode        : 1;
    guint                show_hidden : 1;
    /* mode Tree */
    guint                is_minitree : 1;
    guint                sync_mode   : 2;
};

#define is_tree(tree)       (tree->priv->mode == DONNA_TREE_VIEW_MODE_TREE)
#define get_filter(treev)   (GTK_TREE_MODEL_FILTER (gtk_tree_view_get_model (treev)))
#define get_store_from_filter(filter)   \
    (GTK_TREE_STORE (gtk_tree_model_filter_get_model (filter)))
#define get_store(treev)    (get_store_from_filter (get_filter (treev)))

static gboolean add_node_to_tree (DonnaTreeView *tree,
                                  GtkTreeIter   *parent,
                                  DonnaNode     *node);

static void free_col_prop (struct col_prop *cp);
static void free_provider_signals (struct provider_signals *ps);

static gboolean donna_tree_view_test_expand_row     (GtkTreeView    *treev,
                                                     GtkTreeIter    *iter,
                                                     GtkTreePath    *path);
static void     donna_tree_view_row_collapsed       (GtkTreeView    *treev,
                                                     GtkTreeIter    *iter,
                                                     GtkTreePath    *path);
static void     donna_tree_view_finalize            (GObject        *object);

G_DEFINE_TYPE (DonnaTreeView, donna_tree_view, GTK_TYPE_TREE_VIEW);

static void
donna_tree_view_class_init (DonnaTreeViewClass *klass)
{
    GtkTreeViewClass *tv_class;
    GObjectClass *o_class;

    tv_class = GTK_TREE_VIEW_CLASS (klass);
    tv_class->row_collapsed = donna_tree_view_row_collapsed;
    tv_class->test_expand_row = donna_tree_view_test_expand_row;

    o_class = G_OBJECT_CLASS (klass);
    o_class->finalize = donna_tree_view_finalize;

    g_type_class_add_private (klass, sizeof (DonnaTreeViewPrivate));
}

static void
donna_tree_view_init (DonnaTreeView *tv)
{
    DonnaTreeViewPrivate *priv;

    priv = tv->priv = G_TYPE_INSTANCE_GET_PRIVATE (tv, DONNA_TYPE_TREE_VIEW,
            DonnaTreeViewPrivate);
    /* we can't use g_hash_table_new_full and set a destroy, because we will
     * be replacing values often (since head of GSList can change) but don't
     * want the old value to be free-d, obviously */
    priv->hashtable = g_hash_table_new (g_direct_hash, g_direct_equal);

    priv->providers = g_ptr_array_new_with_free_func (
            (GDestroyNotify) free_provider_signals);
    g_mutex_init (&priv->refresh_node_props_mutex);
    priv->col_props = g_array_new (FALSE, FALSE, sizeof (struct col_prop));
    g_array_set_clear_func (priv->col_props, (GDestroyNotify) free_col_prop);
}

static void
free_col_prop (struct col_prop *cp)
{
    donna_shared_string_unref (cp->prop);
}

static void
free_provider_signals (struct provider_signals *ps)
{
    g_signal_handler_disconnect (ps->provider, ps->sid_node_updated);
    g_signal_handler_disconnect (ps->provider, ps->sid_node_removed);
    g_signal_handler_disconnect (ps->provider, ps->sid_node_children);
    g_signal_handler_disconnect (ps->provider, ps->sid_node_new_child);
    g_object_unref (ps->provider);
    g_free (ps);
}

static void
free_hashtable (gpointer key, GSList *list, gpointer data)
{
    g_slist_free_full (list, (GDestroyNotify) gtk_tree_iter_free);
}

static void
donna_tree_view_finalize (GObject *object)
{
    DonnaTreeViewPrivate *priv;

    priv = DONNA_TREE_VIEW (object)->priv;
    g_hash_table_foreach (priv->hashtable, (GHFunc) free_hashtable, NULL);
    g_hash_table_destroy (priv->hashtable);
    g_ptr_array_free (priv->providers, TRUE);
    g_mutex_clear (&priv->refresh_node_props_mutex);
    g_array_free (priv->col_props, TRUE);

    G_OBJECT_CLASS (donna_tree_view_parent_class)->finalize (object);
}

static void
load_config (DonnaTreeView *tree)
{
    DonnaTreeViewPrivate *priv;
    DonnaConfig *config;
    gint val;

    /* we load/cache some options, because usually we can't just get those when
     * needed, but they need to trigger some refresh or something. So we need to
     * listen on the option_{set,removed} signals of the config manager anyways.
     * Might as well save a few function calls... */

    priv = tree->priv;
    config = donna_donna_get_config (priv->donna);

    if (donna_config_get_uint (config, (guint *) &val,
                "treeviews/%s/mode", priv->name))
        priv->mode = val;
    else
    {
        g_warning ("Unable to find mode for tree '%s'", priv->name);
        /* set default */
        val = priv->mode = DONNA_TREE_VIEW_MODE_LIST;
        donna_config_set_uint (config, (guint) val,
                "treeviews/%s/mode", priv->mode);
    }

    if (donna_config_get_boolean (config, (gboolean *) &val,
                "treeviews/%s/show_hidden", priv->name))
        priv->show_hidden = val;
    else
    {
        /* set default */
        val = priv->show_hidden = TRUE;
        donna_config_set_boolean (config, (gboolean) val,
                "treeviews/%s/show_hidden", priv->name);
    }

    if (is_tree (tree))
    {
        if (donna_config_get_boolean (config, (gboolean *) &val,
                    "treeviews/%s/is_minitree", priv->name))
            priv->is_minitree = val;
        else
        {
            /* set default */
            val = priv->is_minitree = FALSE;
            donna_config_set_boolean (config, (gboolean) val,
                    "treeview/%s/is_minitree", priv->name);
        }
    }
    else
    {
    }

    g_object_unref (config);
}

struct node_children_data
{
    GtkTreeView  *treev;
    GtkTreeStore *store;
    GtkTreeIter   iter;
    GtkTreeIter   iter_fake;
};

static void
free_node_children_data (struct node_children_data *data)
{
    g_slice_free (struct node_children_data, data);
}

static void
node_get_children_timeout (DonnaTask *task, struct node_children_data *data)
{
    GtkTreePath *path;

    /* we're slow to get the children, let's just show the fake node ("please
     * wait...") */
    path = gtk_tree_model_get_path (GTK_TREE_MODEL (data->store), &data->iter);
    gtk_tree_view_expand_row (data->treev, path, FALSE);
    gtk_tree_path_free (path);
}

static void
remove_row_from_tree (DonnaTreeView *tree, GtkTreeIter *iter)
{
    DonnaTreeViewPrivate *priv = tree->priv;
    GtkTreeStore *store;
    GtkTreeModel *model;
    DonnaNode *node;
    DonnaProvider *provider;
    guint i;
    GtkTreeIter parent;

    store = get_store (GTK_TREE_VIEW (tree));
    model = GTK_TREE_MODEL (store);

    /* get the node */
    gtk_tree_model_get (model, iter,
            DONNA_TREE_VIEW_COL_NODE,   &node,
            -1);
    if (node)
    {
        /* get its provider */
        donna_node_get (node, FALSE, "provider", &provider, NULL);
        /* and update the nb of nodes we have for this provider */
        for (i = 0; i < priv->providers->len; ++i)
        {
            struct provider_signals *ps = priv->providers->pdata[i];

            if (ps->provider == provider)
            {
                if (--ps->nb_nodes == 0)
                {
                    /* this will disconnect handlers from provider & free memory */
                    g_ptr_array_remove_index_fast (priv->providers, i);
                    break;
                }
            }
        }
        g_object_unref (provider);
        g_object_unref (node);
    }

    if (is_tree (tree))
        /* get the parent, in case we're removing its last child */
        gtk_tree_model_iter_parent (model, &parent, iter);
    /* now we can remove the row */
    gtk_tree_store_remove (store, iter);
    /* we have a parent, it has no more children, update expand state */
    if (is_tree (tree) && parent.stamp != 0
            && !gtk_tree_model_iter_has_child (model, &parent))
    {
        enum tree_expand es;

        gtk_tree_model_get (model, &parent,
                DONNA_TREE_COL_EXPAND_STATE,    &es,
                -1);
        if (es == DONNA_TREE_EXPAND_PARTIAL)
        {
            es = DONNA_TREE_EXPAND_UNKNOWN;
            /* add a fake row */
            gtk_tree_store_insert_with_values (store, NULL, &parent, 0,
                    DONNA_TREE_COL_NODE,    NULL,
                    -1);
        }
        else
            es = DONNA_TREE_EXPAND_NONE;
        gtk_tree_store_set (store, &parent,
                DONNA_TREE_COL_EXPAND_STATE,    es,
                -1);
    }
}

static void
set_children (DonnaTreeView *tree,
              GtkTreeIter   *iter,
              GPtrArray     *children,
              gboolean       expand)
{
    GtkTreeStore *store;
    GtkTreeModel *model;

    store = get_store (GTK_TREE_VIEW (tree));
    model = GTK_TREE_MODEL (store);

    if (children->len == 0)
    {
        GtkTreeIter child;

        /* set new expand state */
        gtk_tree_store_set (store, iter,
                DONNA_TREE_COL_EXPAND_STATE,    DONNA_TREE_EXPAND_NONE,
                -1);
        if (gtk_tree_model_iter_children (model, &child, iter))
            do
            {
                remove_row_from_tree (tree, &child);
            } while (gtk_tree_model_iter_next (model, &child));
    }
    else
    {
        guint i;

        for (i = 0; i < children->len; ++i)
        {
            DonnaNode *node = children->pdata[i];
            GSList *list;
            gboolean exists;

            /* is there already a row for this node at that level? */
            exists = FALSE;
            list = g_hash_table_lookup (tree->priv->hashtable, node);
            for ( ; list; list = list->next)
            {
                GtkTreeIter *i = list->data;
                GtkTreeIter  p;

                /* get the parent */
                if (gtk_tree_model_iter_parent (model, &p, i)
                        && memcmp (&p, iter, sizeof (GtkTreeIter)) == 0)
                {
                    exists = TRUE;
                    break;
                }
            }
            if (exists)
                continue;

            if (!add_node_to_tree (tree, iter, node))
            {
                const gchar *domain;
                DonnaSharedString *location;

                donna_node_get (node, FALSE,
                        "domain",   &domain,
                        "location", &location,
                        NULL);
                g_warning ("Treeview '%s': failed to add node for '%s:%s'",
                        tree->priv->name,
                        domain,
                        donna_shared_string (location));
                donna_shared_string_unref (location);
            }
        }

        /* set new expand state */
        gtk_tree_store_set (store, iter,
                DONNA_TREE_COL_EXPAND_STATE,
                (expand) ? DONNA_TREE_EXPAND_FULL : DONNA_TREE_EXPAND_NEVER_FULL,
                -1);
        if (expand)
        {
            GtkTreePath *path;

            /* and make sure the row gets expanded (since we "blocked" it when
             * clicked */
            path = gtk_tree_model_get_path (model, iter);
            gtk_tree_view_expand_row (GTK_TREE_VIEW (tree), path, FALSE);
            gtk_tree_path_free (path);
        }
    }
}

static void
node_get_children_callback (DonnaTask                   *task,
                            gboolean                     timeout_called,
                            struct node_children_data   *data)
{
    DonnaTreeView   *tree;
    DonnaTaskState   state;

    tree = DONNA_TREE_VIEW (data->treev);
    state = donna_task_get_state (task);
    if (state != DONNA_TASK_DONE)
    {
        GtkTreeModel      *model;
        GtkTreePath       *path;
        DonnaNode         *node;
        const gchar       *domain;
        DonnaSharedString *location;
        const GError      *error;

        /* collapse the node & set it to UNKNOWN (it might have been NEVER
         * before, but we don't know) so if the user tries an expansion again,
         * it is tried again. */
        model = GTK_TREE_MODEL (data->store);
        path = gtk_tree_model_get_path (model, &data->iter);
        gtk_tree_view_collapse_row (data->treev, path);
        gtk_tree_path_free (path);
        gtk_tree_store_set (data->store, &data->iter,
                DONNA_TREE_COL_EXPAND_STATE,    DONNA_TREE_EXPAND_UNKNOWN,
                -1);

        /* explain ourself */
        gtk_tree_model_get (model, &data->iter,
                DONNA_TREE_COL_NODE,    &node,
                -1);
        donna_node_get (node, FALSE,
                "domain",   &domain,
                "location", &location,
                NULL);
        error = donna_task_get_error (task);
        donna_error ("Treeview '%s': Failed to get children for node '%s:%s': %s",
                tree->priv->name,
                domain,
                donna_shared_string (location),
                error->message);
        donna_shared_string_unref (location);
        g_object_unref (node);

        free_node_children_data (data);
        return;
    }

    set_children (tree, &data->iter,
            g_value_get_boxed (donna_task_get_return_value (task)),
            TRUE /* expand node */);

    free_node_children_data (data);
}

struct import_children_data
{
    DonnaTreeView   *tree;
    GtkTreeModel    *model;
    GtkTreeIter     *iter;
    GtkTreeIter     *child;
};

static gboolean
import_children (struct import_children_data *data)
{
    GtkTreePath *path;

    do
    {
        DonnaNode *node;

        gtk_tree_model_get (data->model, data->child,
                DONNA_TREE_COL_NODE,    &node,
                -1);
        add_node_to_tree (data->tree, data->iter, node);
        g_object_unref (node);
    } while (gtk_tree_model_iter_next (data->model, data->child));

    /* update expand state */
    gtk_tree_store_set (GTK_TREE_STORE (data->model), data->iter,
            DONNA_TREE_COL_EXPAND_STATE,    DONNA_TREE_EXPAND_FULL,
            -1);

    /* expand node */
    path = gtk_tree_model_get_path (data->model, data->iter);
    gtk_tree_view_expand_row (GTK_TREE_VIEW (data->tree), path, FALSE);
    gtk_tree_path_free (path);

    /* free */
    gtk_tree_iter_free (data->iter);
    gtk_tree_iter_free (data->child);
    g_free (data);

    /* don't repeat */
    return FALSE;
}

static gboolean
donna_tree_view_test_expand_row (GtkTreeView    *treev,
                                 GtkTreeIter    *_iter,
                                 GtkTreePath    *_path)
{
    GtkTreeModelFilter *filter;
    GtkTreeStore *store;
    enum tree_expand expand_state;

    filter = get_filter (treev);
    store = get_store_from_filter (filter);
    gtk_tree_model_get (GTK_TREE_MODEL (filter), _iter,
            DONNA_TREE_COL_EXPAND_STATE,    &expand_state,
            -1);
    switch (expand_state)
    {
        /* allow expansion, just update expand state */
        case DONNA_TREE_EXPAND_NEVER_FULL:
            {
                GtkTreeIter iter;

                gtk_tree_model_filter_convert_iter_to_child_iter (
                        filter,
                        &iter,
                        _iter);

                gtk_tree_store_set (store, &iter,
                        DONNA_TREE_COL_EXPAND_STATE,    DONNA_TREE_EXPAND_FULL,
                        -1);
            }
            /* fall through */

        /* allow expansion */
        case DONNA_TREE_EXPAND_WIP:
        case DONNA_TREE_EXPAND_PARTIAL:
        case DONNA_TREE_EXPAND_FULL:
            return FALSE;

        /* refuse expansion, start get_children task */
        case DONNA_TREE_EXPAND_UNKNOWN:
        case DONNA_TREE_EXPAND_NEVER:
            {
                DonnaTreeViewPrivate *priv;
                DonnaNode *node;
                DonnaProvider *provider;
                DonnaTask *task;
                struct node_children_data *data;
                GSList *list;

                priv = DONNA_TREE_VIEW (treev)->priv;

                gtk_tree_model_get (GTK_TREE_MODEL (filter), _iter,
                        DONNA_TREE_COL_NODE,    &node,
                        -1);
                if (!node)
                {
                    donna_error ("Treeview '%s': could not get node from model",
                            priv->name);
                    return TRUE;
                }

                /* is there another tree node for this node? */
                list = g_hash_table_lookup (priv->hashtable, node);
                if (list)
                {
                    GtkTreeIter iter;

                    gtk_tree_model_filter_convert_iter_to_child_iter (
                            filter,
                            &iter,
                            _iter);
                    for ( ; list; list = list->next)
                    {
                        GtkTreeIter *i = list->data;
                        enum tree_expand es;

                        /* skip ourself */
                        if (memcmp (&iter, i, sizeof (GtkTreeIter)) == 0)
                            continue;

                        gtk_tree_model_get (GTK_TREE_MODEL (store), i,
                                DONNA_TREE_COL_EXPAND_STATE,    &es,
                                -1);
                        if (es == DONNA_TREE_EXPAND_FULL
                                || es == DONNA_TREE_EXPAND_NEVER_FULL)
                        {
                            DonnaTreeView *tree = DONNA_TREE_VIEW (treev);
                            GtkTreeModel *model = GTK_TREE_MODEL (store);
                            GtkTreeIter child;
                            struct import_children_data *data;

                            /* let's import the children */

                            if (!gtk_tree_model_iter_children (model,
                                    &child, i))
                            {
                                g_warning ("Treeview '%s': Inconsistency detected",
                                        priv->name);
                                continue;
                            }

                            /* because the tree's model is filter, and with
                             * filter iters are not persistent, adding nodes
                             * would invalidate the current iter, thus
                             * generating a warning in the post-signal of the
                             * test-expand-row in GtkTreeView.
                             * So we need to have that code run later */
                            data = g_new (struct import_children_data, 1);
                            data->tree  = tree;
                            data->model = model;
                            data->iter  = gtk_tree_iter_copy (&iter);
                            data->child = gtk_tree_iter_copy (&child);
                            g_idle_add ((GSourceFunc) import_children, data);

                            gtk_tree_store_set (store, &iter,
                                    DONNA_TREE_COL_EXPAND_STATE,    DONNA_TREE_EXPAND_WIP,
                                    -1);

                            /* no expansion (yet) */
                            return TRUE;
                        }
                    }
                }
                donna_node_get (node, FALSE, "provider", &provider, NULL);
                task = donna_provider_get_node_children_task (provider, node,
                        /* FIXME type of nodes to show must be a config option */
                        DONNA_NODE_CONTAINER);

                data = g_slice_new0 (struct node_children_data);
                data->treev = treev;
                data->store = store;
                gtk_tree_model_filter_convert_iter_to_child_iter (filter,
                        &data->iter,
                        _iter);
                /* iter_fake is the first (and only) child of iter */
                gtk_tree_model_iter_children (GTK_TREE_MODEL (store),
                        &data->iter_fake, &data->iter);

                /* FIXME: timeout_delay must be an option */
                donna_task_set_timeout (task, 800,
                        (task_timeout_fn) node_get_children_timeout,
                        data,
                        NULL);
                donna_task_set_callback (task,
                        (task_callback_fn) node_get_children_callback,
                        data,
                        (GDestroyNotify) free_node_children_data);

                gtk_tree_store_set (store, &data->iter,
                        DONNA_TREE_COL_EXPAND_STATE,    DONNA_TREE_EXPAND_WIP,
                        -1);

                donna_donna_run_task (priv->donna, task);
                g_object_unref (node);
                g_object_unref (provider);
            }
            return TRUE;

        /* refuse expansion. This case should never happen */
        case DONNA_TREE_EXPAND_NONE:
            g_warning ("Treeview '%s' wanted to expand a node without children",
                    DONNA_TREE_VIEW (treev)->priv->name);
            return TRUE;
    }
    /* never reached -- this is to remove the warning */
    return FALSE;
}

static void
donna_tree_view_row_collapsed (GtkTreeView   *treev,
                               GtkTreeIter   *_iter,
                               GtkTreePath   *_path)
{
    /* After row is collapsed, there might still be an horizontal scrollbar,
     * because the column has been enlarged due to a long-ass children, and
     * it hasn't been resized since. So even though there's no need for the
     * scrollbar anymore, it remains there.
     * Since we only have one column, we trigger an autosize to get rid of the
     * horizontal scrollbar (or adjust its size) */
    gtk_tree_view_columns_autosize (treev);
}

static gboolean
visible_func (GtkTreeModel  *_model,
              GtkTreeIter   *_iter,
              gpointer       data)
{
    DonnaTreeViewPrivate *priv;
    DonnaNode *node;
    DonnaSharedString *name;
    gboolean ret;

    priv = DONNA_TREE_VIEW (data)->priv;
    if (priv->show_hidden)
        return TRUE;

    gtk_tree_model_get (_model, _iter, DONNA_TREE_VIEW_COL_NODE, &node, -1);
    if (!node)
        return FALSE;

    donna_node_get (node, FALSE, "name", &name, NULL);
    ret = donna_shared_string (name)[0] != '.';
    donna_shared_string_unref (name);
    g_object_unref (node);

    return ret;
}

struct refresh_node_props_data
{
    DonnaTreeView *tree;
    DonnaNode     *node;
    GPtrArray     *props;
};

static void
free_refresh_node_props_data (struct refresh_node_props_data *data)
{
    DonnaTreeViewPrivate *priv = data->tree->priv;

    g_mutex_lock (&priv->refresh_node_props_mutex);
    priv->refresh_node_props = g_slist_remove (priv->refresh_node_props, data);
    g_mutex_unlock (&priv->refresh_node_props_mutex);

    g_object_unref (data->node);
    g_ptr_array_unref (data->props);
    g_free (data);
}

/* Usually, upon a provider's node-updated signal, we check if the node is in
 * the tree, and if the property is one that our columns use; If so, we trigger
 * a refresh of that row (i.e. trigger a row-updated on store)
 * However, there's an exception: a columntype can, on render, give a list of
 * properties to be refreshed. We then store those properties on
 * priv->refresh_node_props as we run a task to refresh them. During that time,
 * those properties (on that node) will *not* trigger a refresh, as they usually
 * would. Instead, it's only when this callback is triggered that, if *all*
 * properties were refreshed, the refresh will be triggered (on the tree) */
static void
refresh_node_prop_cb (DonnaTask                      *task,
                      gboolean                        timeout_called,
                      struct refresh_node_props_data *data)
{
    if (donna_task_get_state (task) == DONNA_TASK_DONE)
    {
        /* no return value means all props were refreshed, i.e. full success */
        if (!donna_task_get_return_value (task))
        {
            DonnaTreeViewPrivate *priv = data->tree->priv;
            GtkTreeModel *model;
            GSList *list;

            list = g_hash_table_lookup (priv->hashtable, data->node);
            if (!list)
            {
                g_warning ("Treeview '%s': refresh_node_prop_cb for missing node",
                        priv->name);
                goto bail;
            }

            model = GTK_TREE_MODEL (get_store (GTK_TREE_VIEW (data->tree)));
            for ( ; list; list = list->next)
            {
                GtkTreeIter *iter = list->data;
                GtkTreePath *path;

                path = gtk_tree_model_get_path (model, iter);
                gtk_tree_model_row_changed (model, path, iter);
                gtk_tree_path_free (path);
            }
        }
    }
bail:
    free_refresh_node_props_data (data);
}

static void
rend_func (GtkTreeViewColumn  *column,
           GtkCellRenderer    *renderer,
           GtkTreeModel       *_model,
           GtkTreeIter        *_iter,
           gpointer            data)
{
    DonnaTreeView *tree;
    DonnaTreeViewPrivate *priv;
    DonnaColumnType *ct;
    DonnaNode *node;
    const gchar *col;
    guint index = GPOINTER_TO_UINT (data);
    GPtrArray *arr;

    tree = DONNA_TREE_VIEW (gtk_tree_view_column_get_tree_view (column));
    priv = tree->priv;
    ct   = g_object_get_data (G_OBJECT (column), "column-type");
    col  = g_object_get_data (G_OBJECT (column), "column-name");
    gtk_tree_model_get (_model, _iter, DONNA_TREE_VIEW_COL_NODE, &node, -1);

    if (is_tree (tree))
    {
        DonnaColumnType *ctname;

        ctname = donna_donna_get_columntype (priv->donna, "name");
        if (!node)
        {
            /* this is a "fake" node, shown as a "Please Wait..." */
            /* we can only do that for a column of type "name" */
            if (!ctname || ctname != ct)
            {
                if (ctname)
                    g_object_unref (ctname);
                return;
            }
            g_object_unref (ctname);

            if (index == 1)
                /* GtkRendererPixbuf */
                g_object_set (renderer, "visible", FALSE, NULL);
            else /* index == 2 */
                /* GtkRendererText */
                g_object_set (renderer,
                        "visible",  TRUE,
                        "text",     "Please Wait...",
                        NULL);

            return;
        }

        /* do we have some overriding to do? */
        if (ctname && ctname == ct)
        {
            if (index == 1)
            {
                /* GtkRendererPixbuf */
                GdkPixbuf *pixbuf;

                gtk_tree_model_get (_model, _iter,
                        DONNA_TREE_COL_ICON,    &pixbuf,
                        -1);
                if (pixbuf)
                {
                    g_object_set (renderer,
                            "visible",  TRUE,
                            "pixbuf",   pixbuf,
                            NULL);
                    g_object_unref (pixbuf);
                    g_object_unref (ctname);
                    g_object_unref (node);
                    return;
                }
            }
            else /* index == 2 */
            {
                /* GtkRendererText */
                gchar *name;

                gtk_tree_model_get (_model, _iter,
                        DONNA_TREE_COL_NAME,    &name,
                        -1);
                if (name)
                {
                    g_object_set (renderer,
                            "visible",  TRUE,
                            "text",     name,
                            NULL);
                    g_free (name);
                    g_object_unref (ctname);
                    g_object_unref (node);
                    return;
                }
            }
        }

        if (ctname)
            g_object_unref (ctname);
    }
    else if (!node)
        return;

    arr = donna_columntype_render (ct, priv->name, col, index, node, renderer);
    if (arr)
    {
        DonnaTask *task;
        struct refresh_node_props_data *data;

        /* ct wants some properties refreshed on node. See refresh_node_prop_cb */
        data = g_new0 (struct refresh_node_props_data, 1);
        data->tree  = tree;
        data->node  = g_object_ref (node);
        data->props = g_ptr_array_ref (arr);

        g_mutex_lock (&priv->refresh_node_props_mutex);
        priv->refresh_node_props = g_slist_append (priv->refresh_node_props, data);
        g_mutex_unlock (&priv->refresh_node_props_mutex);

        task = donna_node_refresh_arr_task (node, arr);
        donna_task_set_callback (task,
                (task_callback_fn) refresh_node_prop_cb,
                data,
                (GDestroyNotify) free_refresh_node_props_data);
        donna_donna_run_task (priv->donna, task);
    }
    g_object_unref (node);
}

static gint
sort_func (GtkTreeModel      *model,
           GtkTreeIter       *iter1,
           GtkTreeIter       *iter2,
           GtkTreeViewColumn *column)
{
    DonnaTreeViewPrivate *priv;
    DonnaColumnType *ct;
    const gchar *col;
    DonnaNode *node1;
    DonnaNode *node2;
    gint ret;

    gtk_tree_model_get (model, iter1, DONNA_TREE_COL_NODE, &node1, -1);
    /* one node could be a "fake" one, i.e. node is a NULL pointer */
    if (!node1)
        return -1;

    gtk_tree_model_get (model, iter2, DONNA_TREE_COL_NODE, &node2, -1);
    if (!node2)
    {
        g_object_unref (node1);
        return 1;
    }

    priv = DONNA_TREE_VIEW (gtk_tree_view_column_get_tree_view (column))->priv;
    ct   = g_object_get_data (G_OBJECT (column), "column-type");
    col  = g_object_get_data (G_OBJECT (column), "column-name");

    ret = donna_columntype_node_cmp (ct, priv->name, col, node1, node2);
    g_object_unref (node1);
    g_object_unref (node2);
    return ret;
}

static void
node_has_children_cb (DonnaTask                 *task,
                      gboolean                   timeout_called,
                      struct node_children_data *data)
{
    DonnaNode *node = (DonnaNode *) data;
    DonnaTaskState state;
    const GValue *value;
    gboolean has_children;
    enum tree_expand es;

    state = donna_task_get_state (task);
    if (state != DONNA_TASK_DONE)
    {
        /* we don't know if the node has children, so we'll keep the fake node
         * in, with expand state to UNKNOWN as it is. That way the user can ask
         * for expansion, which could simply have the expander removed if it
         * wasn't needed after all... */
        free_node_children_data (data);
        return;
    }

    value = donna_task_get_return_value (task);
    has_children = g_value_get_boolean (value);

    if (!has_children)
        /* remove the fake node */
        gtk_tree_store_remove (data->store, &data->iter_fake);
    /* update expand state.. */
    gtk_tree_model_get (GTK_TREE_MODEL (data->store), &data->iter,
            DONNA_TREE_COL_EXPAND_STATE,    &es,
            -1);
    /* ..unless there's a get_children in progress */
    if (es != DONNA_TREE_EXPAND_WIP)
        gtk_tree_store_set (data->store, &data->iter,
                DONNA_TREE_COL_EXPAND_STATE,
                (has_children) ? DONNA_TREE_EXPAND_NEVER : DONNA_TREE_EXPAND_NONE,
                -1);

    free_node_children_data (data);
}

static void
node_updated_cb (DonnaProvider  *provider,
                 DonnaNode      *node,
                 const gchar    *name,
                 DonnaTreeView  *tree)
{
    DonnaTreeViewPrivate *priv = tree->priv;
    GtkTreeModel *model;
    GSList *list, *l;
    guint i;

    /* do we have this node on tree? */
    l= g_hash_table_lookup (priv->hashtable, node);
    if (!l)
        return;

    /* should that property cause a refresh? */
    for (i = 0; i < priv->col_props->len; ++i)
    {
        struct col_prop *cp;

        cp = &g_array_index (priv->col_props, struct col_prop, i);
        if (streq (name, donna_shared_string (cp->prop)))
            break;
    }
    if (i >= priv->col_props->len)
        return;

    /* should we ignore this prop/node combo ? See refresh_node_prop_cb */
    g_mutex_lock (&priv->refresh_node_props_mutex);
    for (list = priv->refresh_node_props; list; list = list->next)
    {
        struct refresh_node_props_data *data = list->data;

        if (data->node == node)
        {
            for (i = 0; i < data->props->len; ++i)
            {
                if (streq (name, data->props->pdata[i]))
                    break;
            }
            if (i < data->props->len)
                break;
        }
    }
    g_mutex_unlock (&priv->refresh_node_props_mutex);
    if (list)
        return;

    /* trigger refresh on all rows for that node */
    model = GTK_TREE_MODEL (get_store (GTK_TREE_VIEW (tree)));
    for ( ; l; l = l->next)
    {
        GtkTreeIter *iter = l->data;
        GtkTreePath *path;

        path = gtk_tree_model_get_path (model, iter);
        gtk_tree_model_row_changed (model, path, iter);
        gtk_tree_path_free (path);
    }
}

static void
node_removed_cb (DonnaProvider  *provider,
                 DonnaNode      *node,
                 DonnaTreeView  *tree)
{
    /* TODO */
}

struct node_children_cb_data
{
    DonnaTreeView   *tree;
    DonnaNode       *node;
    DonnaNodeType    node_types;
    GPtrArray       *children;
};

static gboolean
real_node_children_cb (struct node_children_cb_data *data)
{
    DonnaTreeViewPrivate *priv = data->tree->priv;
    GSList *list;

    if (priv->location != data->node)
        goto free;

    /* FIXME node_types we show must be an option */
    if (!(data->node_types & DONNA_NODE_CONTAINER))
        goto free;

    list = g_hash_table_lookup (priv->hashtable, data->node);
    for ( ; list; list = list->next)
        set_children (data->tree, list->data, data->children,
                FALSE /* don't expand node */);

free:
    g_object_unref (data->node);
    g_ptr_array_unref (data->children);
    g_free (data);

    /* don't repeat */
    return FALSE;
}

static void
node_children_cb (DonnaProvider  *provider,
                  DonnaNode      *node,
                  DonnaNodeType   node_types,
                  GPtrArray      *children,
                  DonnaTreeView  *tree)
{
    struct node_children_cb_data *data;

    if (!is_tree (tree))
        return;

    /* we might not be in the main thread, but we need to be */
    data = g_new (struct node_children_cb_data, 1);
    data->tree       = tree;
    data->node       = g_object_ref (node);
    data->node_types = node_types;
    data->children   = g_ptr_array_ref (children);
    g_main_context_invoke (NULL, (GSourceFunc) real_node_children_cb, data);
}

static void
node_new_child_cb (DonnaProvider *provider,
                   DonnaNode     *node,
                   DonnaNode     *child,
                   DonnaTreeView *tree)
{
    /* TODO */
}

static gboolean
add_node_to_tree (DonnaTreeView *tree,
                  GtkTreeIter   *parent,
                  DonnaNode     *node)
{
    const gchar             *domain;
    DonnaSharedString       *ss;
    DonnaTreeViewPrivate    *priv;
    GtkTreeView             *treev;
    GtkTreeStore            *store;
    GtkTreeModel            *model;
    GtkTreeIter              iter;
    GSList                  *list;
    GSList                  *l;
    DonnaProvider           *provider;
    DonnaTask               *task;
    gboolean                 added;
    guint                    i;

    g_return_val_if_fail (DONNA_IS_TREE_VIEW (tree), FALSE);
    g_return_val_if_fail (DONNA_IS_NODE (node), FALSE);

    priv  = tree->priv;
    treev = GTK_TREE_VIEW (tree);
    store = get_store (treev);
    model = GTK_TREE_MODEL (store);

    donna_node_get (node, FALSE, "domain", &domain, "location", &ss, NULL);
    g_debug ("treeview '%s': adding new node %p for '%s:%s'",
            priv->name,
            node,
            domain,
            donna_shared_string (ss));
    donna_shared_string_unref (ss);

    if (!is_tree (tree))
    {
        /* TODO */
        return TRUE;
    }

    /* check if the parent has a "fake" node as child, in which case we'll
     * re-use it instead of adding a new node */
    added = FALSE;
    if (parent && gtk_tree_model_iter_children (model, &iter, parent))
    {
        DonnaNode *n;

        gtk_tree_model_get (model, &iter,
                DONNA_TREE_COL_NODE,    &n,
                -1);
        if (!n)
        {
            gtk_tree_store_set (store, &iter,
                    DONNA_TREE_COL_NODE,            node,
                    DONNA_TREE_COL_EXPAND_STATE,    DONNA_TREE_EXPAND_UNKNOWN,
                    -1);
            added = TRUE;
        }
        else
            g_object_unref (n);
    }
    if (!added)
        gtk_tree_store_insert_with_values (store, &iter, parent, 0,
                DONNA_TREE_COL_NODE,            node,
                DONNA_TREE_COL_EXPAND_STATE,    DONNA_TREE_EXPAND_UNKNOWN,
                -1);
    /* add it to our hashtable */
    list = g_hash_table_lookup (priv->hashtable, node);
    list = g_slist_append (list, gtk_tree_iter_copy (&iter));
    g_hash_table_insert (priv->hashtable, node, list);
    /* check the list in case we have another tree node for that node, in which
     * case we might get the has_children info from there */
    added = FALSE;
    for (l = list; l; l = l->next)
    {
        GtkTreeIter *i = l->data;
        enum tree_expand es;

        if (memcmp (&iter, i, sizeof (GtkTreeIter)) == 0)
            continue;

        gtk_tree_model_get (model, i,
                DONNA_TREE_COL_EXPAND_STATE,    &es,
                -1);
        switch (es)
        {
            /* node has children */
            case DONNA_TREE_EXPAND_NEVER:
            case DONNA_TREE_EXPAND_NEVER_FULL:
            case DONNA_TREE_EXPAND_PARTIAL:
            case DONNA_TREE_EXPAND_FULL:
                es = DONNA_TREE_EXPAND_NEVER;
                break;

            /* node doesn't have children */
            case DONNA_TREE_EXPAND_NONE:
                break;

            /* anything else is inconclusive */
            default:
                es = DONNA_TREE_EXPAND_UNKNOWN; /* == 0 */
        }

        if (es)
        {
            gtk_tree_store_set (store, &iter,
                    DONNA_TREE_COL_EXPAND_STATE,    es,
                    -1);
            if (es == DONNA_TREE_EXPAND_NEVER)
                /* insert a fake node so the user can ask for expansion */
                gtk_tree_store_insert_with_values (store, NULL, &iter, 0,
                        DONNA_TREE_COL_NODE,    NULL,
                        -1);
            added = TRUE;
        }
    }
    /* get provider to get task to know if it has children */
    donna_node_get (node, FALSE, "provider", &provider, NULL);
    for (i = 0; i < priv->providers->len; ++i)
    {
        struct provider_signals *ps = priv->providers->pdata[i];

        if (ps->provider == provider)
        {
            ps->nb_nodes++;
            break;
        }
    }
    if (i >= priv->providers->len)
    {
        struct provider_signals *ps;

        ps = g_new (struct provider_signals, 1);
        ps->provider = g_object_ref (provider);
        ps->nb_nodes = 1;
        ps->sid_node_updated = g_signal_connect (provider, "node-updated",
                G_CALLBACK (node_updated_cb), tree);
        ps->sid_node_removed = g_signal_connect (provider, "node-removed",
                G_CALLBACK (node_removed_cb), tree);
        ps->sid_node_children = g_signal_connect (provider, "node-children",
                G_CALLBACK (node_children_cb), tree);
        ps->sid_node_new_child = g_signal_connect (provider, "node-new-child",
                G_CALLBACK (node_new_child_cb), tree);

        g_ptr_array_add (priv->providers, ps);
    }

    if (added)
    {
        g_object_unref (provider);
        return TRUE;
    }

    task = donna_provider_has_node_children_task (provider, node,
            /* FIXME type of nodes to show must be a config option */
            DONNA_NODE_CONTAINER);
    if (task)
    {
        struct node_children_data *data;

        data = g_slice_new0 (struct node_children_data);
        data->treev = treev;
        data->store = store;
        data->iter  = iter;

        /* insert a fake node so the user can ask for expansion right away (the
         * node will disappear if needed asap) */
        gtk_tree_store_insert_with_values (data->store,
                &data->iter_fake, &data->iter, 0,
                DONNA_TREE_COL_NODE,    NULL,
                -1);

        donna_task_set_callback (task,
                (task_callback_fn) node_has_children_cb,
                data,
                (GDestroyNotify) free_node_children_data);
        donna_donna_run_task (priv->donna, task);
    }
    else
    {
        DonnaSharedString *location;

        /* insert a fake node, so user can try again by asking to expand it */
        gtk_tree_store_insert_with_values (store, NULL, &iter, 0,
            DONNA_TREE_COL_NODE,    NULL,
            -1);

        donna_node_get (node, FALSE, "location", &location, NULL);
        g_warning ("Unable to create a task to determine if the node '%s:%s' has children",
                domain, donna_shared_string (location));
        donna_shared_string_unref (location);
        g_object_unref (provider);
        return FALSE;
    }

    g_object_unref (provider);
    return TRUE;
}

gboolean
donna_tree_view_add_root (DonnaTreeView *tree, DonnaNode *node)
{
    g_return_val_if_fail (DONNA_IS_TREE_VIEW (tree), FALSE);
    g_return_val_if_fail (is_tree (tree), FALSE);
    return add_node_to_tree (tree, NULL, node);
}

static inline gint
natoi (const gchar *str, gsize len)
{
    gint i = 0;
    for ( ; *str != '\0' && len > 0; ++str, --len)
    {
        if (*str < '0' || *str > '9')
            break;
        i = (i * 10) + (*str - '0');
    }
    return i;
}

static void
load_arrangement (DonnaTreeView     *tree,
                  DonnaSharedString *arrangement,
                  DonnaNode         *location)
{
    DonnaTreeViewPrivate *priv  = tree->priv;
    DonnaConfig          *config;
    GtkTreeView          *treev = GTK_TREE_VIEW (tree);
    GtkTreeSortable      *sortable;
    GList                *list;
    DonnaSharedString    *ss;
    DonnaSharedString    *ss_columns;
    DonnaSharedString    *ss_sort = NULL;
    gsize                 sort_len;
    gint                  sort_order = SORT_UNKNOWN;
    const gchar          *col;
    GtkTreeViewColumn    *last_column = NULL;
    gint                  sort_id = 0;

    config = donna_donna_get_config (priv->donna);
    sortable = GTK_TREE_SORTABLE (get_store (treev));
    list = gtk_tree_view_get_columns (treev);

    /* get new set of columns to load. They might not come from the current
     * arrangement, because e.g. it might always set the sort order. In that
     * case, we try to get the arrangement:
     * - for tree: we try the tree default, if that doesn't work we use "name"
     * - for list: if we have an arrangement selector, we try the arrangement
     *   for the parent. If we don't have a selector, or there's no parent, we
     *   try the list default, if that doesn't work we use "name" */
    ss = donna_shared_string_ref (arrangement);
    for (;;)
    {
        if (donna_config_get_shared_string (config, &ss_columns,
                    "%s/columns", donna_shared_string (ss)))
        {
            col = donna_shared_string (ss_columns);
            break;
        }
        else
        {
            if (is_tree (tree))
            {
                if (streq (donna_shared_string (ss), "arrangements/tree"))
                {
                    g_warning ("No columns defined in 'arrangements/tree'; using 'name'");
                    ss_columns = NULL;
                    col = "name";
                    break;
                }
                else
                {
                    donna_shared_string_unref (ss);
                    ss = donna_shared_string_new_dup ("arrangements/tree");
                }
            }
            else
            {
                /* TODO
                 * if (arr/list)
                 *      col=name
                 *      break
                 * else
                 *      if (arr_selector && location=get_parent(location)
                 *          get_arr_for(location)
                 *      else
                 *          arr=arr/list
                 */
            }
        }
    }
    donna_shared_string_unref (ss);

    /* get sort order (has to come from the current arrangement) */
    if (donna_config_get_shared_string (config, &ss_sort,
                "%s/sort", donna_shared_string (arrangement)))
    {
        const gchar *sort = donna_shared_string (ss_sort);

        sort_len = strlen (sort);
        if (sort_len > 2)
        {
            sort_len -= 2;
            if (sort[sort_len] == ':')
                sort_order = (sort[sort_len + 1] == 'd') ? SORT_DESC : SORT_ASC;
        }
    }

    /* clear list of props we're watching to refresh tree */
    if (priv->col_props->len > 0)
        g_array_set_size (priv->col_props, 0);

    for (;;)
    {
        const gchar       *s;
        const gchar       *e;
        gsize              len;
        gchar              buf[64];
        gchar             *b;
        DonnaColumnType   *ct;
        GList             *l;
        GtkTreeViewColumn *column;
        GtkCellRenderer   *renderer;
        const gchar       *rend;
        gint               index;
        DonnaSharedString **props;

        e = strchrnul (col, ',');
        s = strchr (col, ':');
        if (s && s > e)
            s = NULL;

        len = ((s) ? s : e) - col;
        if (len < 64)
        {
            sprintf (buf, "%.*s", (int) len, col);
            b = buf;
        }
        else
            b = g_strdup_printf ("%.*s", (int) len, col);

        if (!donna_config_get_shared_string (config, &ss,
                    "treeviews/%s/columns/%s/type", priv->name, b))
        {
            if (!donna_config_get_shared_string (config, &ss,
                        "columns/%s/type", b))
            {
                g_warning ("No type defined for column '%s', fallback to its name",
                        b);
                ss = NULL;
            }
        }

        ct = donna_donna_get_columntype (priv->donna,
                (ss) ? donna_shared_string (ss) : b);
        if (!ct)
        {
            g_warning ("Unable to load column type '%s' for column '%s' in treeview '%s'",
                    (ss) ? donna_shared_string (ss) : b, b, priv->name);
            if (ss)
                donna_shared_string_unref (ss);
            goto next;
        }
        if (ss)
            donna_shared_string_unref (ss);

        /* look for an existing column of that type */
        column = NULL;
        for (l = list; l; l = l->next)
        {
            DonnaColumnType *col_ct;

            col_ct = g_object_get_data (l->data, "column-type");
            if (col_ct == ct)
            {
                gchar *name;

                column = l->data;
                list = g_list_delete_link (list, l);

                /* column has a ref already, we can release ours */
                g_object_unref (ct);
                /* update the name if needed */
                name = g_object_get_data (G_OBJECT (column), "column-name");
                if (!streq (name, b))
                    g_object_set_data_full (G_OBJECT (column), "column-name",
                            g_strdup (b), g_free);
                /* move column */
                gtk_tree_view_move_column_after (treev, column, last_column);

                break;
            }
        }

        if (!column)
        {
            /* create renderer(s) & column */
            column = gtk_tree_view_column_new ();
            /* store the name on it, so we can get it back from e.g. rend_func */
            g_object_set_data_full (G_OBJECT (column), "column-name",
                    g_strdup (b), g_free);
            /* give our ref on the ct to the column */
            g_object_set_data_full (G_OBJECT (column), "column-type",
                    ct, g_object_unref);
            /* sizing stuff */
            gtk_tree_view_column_set_sizing (column, GTK_TREE_VIEW_COLUMN_FIXED);
            /* load renderers */
            for (index = 1, rend = donna_columntype_get_renderers (ct);
                    *rend;
                    ++index, ++rend)
            {
                /* TODO: use an external (app-global) renderer loader? */
                switch (*rend)
                {
                    case DONNA_COLUMNTYPE_RENDERER_TEXT:
                        if (!priv->renderers[RENDERER_TEXT])
                            priv->renderers[RENDERER_TEXT] =
                                gtk_cell_renderer_text_new ();
                        renderer = priv->renderers[RENDERER_TEXT];
                        break;
                    case DONNA_COLUMNTYPE_RENDERER_PIXBUF:
                        if (!priv->renderers[RENDERER_PIXBUF])
                            priv->renderers[RENDERER_PIXBUF] =
                                gtk_cell_renderer_pixbuf_new ();
                        renderer = priv->renderers[RENDERER_PIXBUF];
                        break;
                    case DONNA_COLUMNTYPE_RENDERER_PROGRESS:
                        if (!priv->renderers[RENDERER_PROGRESS])
                            priv->renderers[RENDERER_PROGRESS] =
                                gtk_cell_renderer_progress_new ();
                        renderer = priv->renderers[RENDERER_PROGRESS];
                        break;
                    case DONNA_COLUMNTYPE_RENDERER_COMBO:
                        if (!priv->renderers[RENDERER_COMBO])
                            priv->renderers[RENDERER_COMBO] =
                                gtk_cell_renderer_combo_new ();
                        renderer = priv->renderers[RENDERER_COMBO];
                        break;
                    case DONNA_COLUMNTYPE_RENDERER_TOGGLE:
                        if (!priv->renderers[RENDERER_TOGGLE])
                            priv->renderers[RENDERER_TOGGLE] =
                                gtk_cell_renderer_toggle_new ();
                        renderer = priv->renderers[RENDERER_TOGGLE];
                        break;
                    default:
                        g_warning ("Unknown renderer type '%c' for column '%s' in treeview '%s'",
                                *rend, b, priv->name);
                        continue;
                }
                gtk_tree_view_column_set_cell_data_func (column, renderer,
                        rend_func, GINT_TO_POINTER (index), NULL);
                gtk_tree_view_column_pack_start (column, renderer, FALSE);
            }
            /* add it */
            gtk_tree_view_append_column (treev, column);
        }

        /* sizing stuff */
        if (s)
        {
            ++s;
            gtk_tree_view_column_set_fixed_width (column, natoi (s, e - s));
        }

        /* set title */
        ss = NULL;
        if (!donna_config_get_shared_string (config, &ss,
                    "treeviews/%s/columns/%s/title", priv->name, b))
            if (!donna_config_get_shared_string (config, &ss,
                        "columns/%s/title", b))
            {
                g_warning ("No title set for column '%s', using its name", b);
                gtk_tree_view_column_set_title (column, b);
            }
        if (ss)
        {
            gtk_tree_view_column_set_title (column, donna_shared_string (ss));
            donna_shared_string_unref (ss);
        }

        /* props to watch for refresh */
        props = donna_columntype_get_props (ct, priv->name, b);
        if (props)
        {
            DonnaSharedString **p;

            for (p = props; *p; ++p)
            {
                struct col_prop cp;

                cp.prop = *p;
                cp.column = column;
                g_array_append_val (priv->col_props, cp);
            }
            g_free (props);
        }
        else
            g_warning ("Treeview '%s': column '%s' reports no properties to watch for refresh",
                    priv->name, b);

        /* sort */
        gtk_tree_view_column_set_sort_column_id (column, sort_id++);
        gtk_tree_sortable_set_sort_func (sortable, sort_id - 1,
                (GtkTreeIterCompareFunc) sort_func, column, NULL);
        if (ss_sort)
        {
            /* SORT_UNKNOWN means we only had a column name (unlikely) */
            if (sort_order == SORT_UNKNOWN)
            {
                if (streq (donna_shared_string (ss_sort), b))
                    gtk_tree_sortable_set_sort_column_id (sortable, sort_id - 1,
                            GTK_SORT_ASCENDING);
            }
            else
            {
                /* ss_sort contains "column:o" */
                if (strlen (b) == sort_len
                        && streqn (donna_shared_string (ss_sort), b, sort_len))
                    gtk_tree_sortable_set_sort_column_id (sortable, sort_id - 1,
                            (sort_order == SORT_ASC) ? GTK_SORT_ASCENDING
                            : GTK_SORT_DESCENDING);
            }
            donna_shared_string_unref (ss_sort);
            ss_sort = NULL;
        }
        /* TODO else default sort order? */

        last_column = column;

next:
        if (b != buf)
            g_free (b);
        if (*e == '\0')
            break;
        col = e + 1;
    }

    if (ss_columns)
        donna_shared_string_unref (ss_columns);
    /* remove all columns left unused */
    while (list)
    {
        /* though we should never try to sort by a sort_id not used by a column,
         * let's make sure it that happens, we just get a warning (instead of
         * dereferencing a pointer pointing nowhere) */
        gtk_tree_sortable_set_sort_func (sortable, sort_id++, NULL, NULL, NULL);
        /* remove column */
        gtk_tree_view_remove_column (treev, list->data);
        list = g_list_delete_link (list, list);
    }

    if (priv->arrangement)
        donna_shared_string_unref (priv->arrangement);
    priv->arrangement = donna_shared_string_ref (arrangement);
    g_object_unref (config);
}

static DonnaSharedString *
select_arrangement (DonnaTreeView *tree, DonnaNode *location)
{
    DonnaTreeViewPrivate *priv;
    DonnaConfig          *config;
    DonnaSharedString    *ss;

    priv = tree->priv;
    config = donna_donna_get_config (priv->donna);
    g_debug ("treeview '%s': select arrangement", priv->name);

    if (is_tree (tree))
    {
        if (donna_config_has_category (config,
                    "treeviews/%s/arrangement", priv->name))
            ss = donna_shared_string_new_take (
                    g_strdup_printf ("treeviews/%s/arrangement", priv->name));
        else
            ss = donna_shared_string_new_dup ("arrangements/tree");
    }
    else
    {
        /* do we have an arrangement selector? */
        if (location)
            ss = donna_donna_get_arrangement (priv->donna, location);
        else
            ss = NULL;

        if (!ss)
        {
            if (donna_config_has_category (config,
                        "treeviews/%s/arrangement", priv->name))
                ss = donna_shared_string_new_take (
                        g_strdup_printf ("treeviews/%s/arrangement", priv->name));
            else
                ss = donna_shared_string_new_dup ("arrangements/list");
        }
    }

    g_debug ("treeview '%s': selected arrangement: %s", priv->name,
            (ss) ? donna_shared_string (ss) : "(none)");
    g_object_unref (config);
    return ss;
}

void
donna_tree_view_build_arrangement (DonnaTreeView *tree, gboolean force)
{
    DonnaTreeViewPrivate *priv;
    DonnaSharedString *ss;

    g_return_if_fail (DONNA_IS_TREE_VIEW (tree));
    g_debug ("treeiew '%s': build arrangement (force=%d)",
            tree->priv->name, force);

    priv = tree->priv;
    ss = select_arrangement (tree, priv->location);
    if (force || !priv->arrangement || !streq (donna_shared_string (ss),
                donna_shared_string (priv->arrangement)))
        load_arrangement (tree, ss, priv->location);
    donna_shared_string_unref (ss);
}

struct set_node_prop_data
{
    DonnaTreeView     *tree;
    DonnaNode         *node;
    DonnaSharedString *prop;
};

static void
free_set_node_prop_data (struct set_node_prop_data *data)
{
    donna_shared_string_unref (data->prop);
    g_free (data);
}

static void
set_node_prop_callbak (DonnaTask                 *task,
                       gboolean                   timeout_called,
                       struct set_node_prop_data *data)
{
    DonnaTreeViewPrivate *priv = data->tree->priv;
    GSList *list, *l;
    guint i;
    GPtrArray *arr;

    /* search column(s) linked to that prop */
    arr = g_ptr_array_sized_new (1);
    for (i = 0; i < priv->col_props->len; ++i)
    {
        struct col_prop *cp;

        cp = &g_array_index (priv->col_props, struct col_prop, i);
        if (streq (donna_shared_string (data->prop),
                    donna_shared_string (cp->prop)))
            g_ptr_array_add (arr, cp->column);
    }
    /* on the off chance there's no columns linked to that prop */
    if (arr->len == 0)
    {
        g_ptr_array_free (arr, TRUE);
        free_set_node_prop_data (data);
        return;
    }

    list = g_hash_table_lookup (priv->hashtable, data->node);
    for (l = list; l; l = l->next)
    {
        GtkTreeIter *iter = l->data;

        for (i = 0; i < arr->len; ++i)
        {
            /* TODO g_object_get_data (column) to do -1 on the nb of spins;
             * if nb==0 then we remove the spinner.
             * In case of task failure, we set the error message and whatnot */
        }
    }

    g_ptr_array_free (arr, TRUE);
    free_set_node_prop_data (data);
}

static void
set_node_prop_timeout (DonnaTask *task, struct set_node_prop_data *data)
{
    DonnaTreeViewPrivate *priv = data->tree->priv;
    GSList *list, *l;
    guint i;
    GPtrArray *arr;

    /* search column(s) linked to that prop */
    arr = g_ptr_array_sized_new (1);
    for (i = 0; i < priv->col_props->len; ++i)
    {
        struct col_prop *cp;

        cp = &g_array_index (priv->col_props, struct col_prop, i);
        if (streq (donna_shared_string (data->prop),
                    donna_shared_string (cp->prop)))
            g_ptr_array_add (arr, cp->column);
    }
    /* on the off chance there's no columns linked to that prop */
    if (arr->len == 0)
    {
        g_ptr_array_free (arr, TRUE);
        return;
    }

    list = g_hash_table_lookup (priv->hashtable, data->node);
    for (l = list; l; l = l->next)
    {
        GtkTreeIter *iter = l->data;

        for (i = 0; i < arr->len; ++i)
        {
            /* TODO set spinner on column arr->pdata[i] for iter;
             * w/ g_object_set_data (column) for the nb of spins */
        }
    }

    g_ptr_array_free (arr, TRUE);
}

gboolean
donna_tree_view_set_node_property (DonnaTreeView      *tree,
                                   DonnaNode          *node,
                                   DonnaSharedString  *prop,
                                   const GValue       *value)
{
    DonnaTreeViewPrivate *priv;
    GError *err = NULL;
    GSList *list;
    DonnaTask *task;
    struct set_node_prop_data *data;

    g_return_val_if_fail (DONNA_IS_TREE_VIEW (tree), FALSE);
    g_return_val_if_fail (DONNA_IS_NODE (node), FALSE);
    g_return_val_if_fail (prop != NULL, FALSE);
    g_return_val_if_fail (G_IS_VALUE (value), FALSE);

    priv = tree->priv;

    /* make sure the node is on the tree */
    list = g_hash_table_lookup (priv->hashtable, node);
    if (!list)
    {
        const gchar *domain;
        DonnaSharedString *ss;

        donna_node_get (node, FALSE, "domain", &domain, "location", &ss, NULL);
        g_warning ("Treeview '%s': Cannot set property '%s' on node '%s:%s', "
                "the node is not represented in the treeview",
                priv->name,
                donna_shared_string (prop),
                domain,
                donna_shared_string (ss));
        donna_shared_string_unref (ss);
        return FALSE;
    }

    task = donna_node_set_property_task (node, donna_shared_string (prop),
            value, &err);
    if (!task)
    {
        const gchar *domain;
        DonnaSharedString *ss;

        donna_node_get (node, FALSE, "domain", &domain, "location", &ss, NULL);
        g_warning ("Treeview '%s': Cannot set property '%s' on node '%s:%s': %s",
                priv->name,
                donna_shared_string (prop),
                domain,
                donna_shared_string (ss),
                err->message);
        donna_shared_string_unref (ss);
        g_clear_error (&err);
        return FALSE;
    }

    data = g_new0 (struct set_node_prop_data, 1);
    data->tree = tree;
    /* don't need to take a ref on node for timeout or cb, since task has one */
    data->node = node;
    data->prop = donna_shared_string_ref (prop);

    donna_task_set_timeout (task, 800 /* FIXME an option */,
            (task_timeout_fn) set_node_prop_timeout,
            data,
            (GDestroyNotify) free_set_node_prop_data);
    donna_task_set_callback (task,
            (task_callback_fn) set_node_prop_callbak,
            data,
            (GDestroyNotify) free_set_node_prop_data);
    donna_donna_run_task (priv->donna, task);
    return TRUE;
}

static gboolean
query_tooltip_cb (GtkTreeView   *treev,
                  gint           x,
                  gint           y,
                  gboolean       keyboard_mode,
                  GtkTooltip    *tooltip)
{
    DonnaTreeViewPrivate *priv;
    GtkTreeViewColumn *column;
    GtkTreeModel *_model;
    GtkTreeIter _iter;
    gboolean ret = FALSE;

    if (gtk_tree_view_get_tooltip_context (treev, &x, &y, keyboard_mode,
                &_model, NULL, &_iter))
    {
        if (gtk_tree_view_get_path_at_pos (treev, x, y, NULL, &column, NULL, NULL))
        {
            DonnaNode *node;
            DonnaColumnType *ct;
            const gchar *col;

            gtk_tree_model_get (_model, &_iter,
                    DONNA_TREE_VIEW_COL_NODE,   &node,
                    -1);
            if (!node)
                return FALSE;

            ct  = g_object_get_data (G_OBJECT (column), "column-type");
            col = g_object_get_data (G_OBJECT (column), "column-name");

            priv = DONNA_TREE_VIEW (treev)->priv;
            /* FIXME we want to give the index, i.e. on which renderer is the
             * mouse cursor. Not sure how/if that's doable with GTK though. */
            ret = donna_columntype_set_tooltip (ct, priv->name, col, 1,
                    node, tooltip);

            g_object_unref (node);
        }
    }
    return ret;
}

static void
selection_changed_cb (GtkTreeSelection *selection, DonnaTreeView *tree)
{
    DonnaTreeViewPrivate *priv = tree->priv;
    GtkTreeModel *model;
    GtkTreeIter iter;

    if (!is_tree (tree))
        return;

    if (gtk_tree_selection_get_selected (selection, &model, &iter))
    {
        DonnaNode *node;

        gtk_tree_model_get (model, &iter,
                DONNA_TREE_COL_NODE,    &node,
                -1);
        if (priv->location != node)
        {
            if (priv->location)
                g_object_unref (priv->location);
            priv->location = node;
        }
        else
            g_object_unref (node);
    }
}

GtkWidget *
donna_tree_view_new (DonnaDonna         *donna,
                     const gchar        *name)
{
    DonnaTreeViewPrivate *priv;
    DonnaTreeView        *tree;
    GtkWidget            *w;
    GtkTreeView          *treev;
    GtkTreeStore         *store;
    GtkTreeModel         *model;
    GtkTreeModelFilter   *filter;
    GtkTreeModel         *model_filter;
    GtkTreeSelection     *sel;

    g_return_val_if_fail (DONNA_IS_DONNA (donna), NULL);
    g_return_val_if_fail (name != NULL, NULL);

    w = g_object_new (DONNA_TYPE_TREE_VIEW, NULL);
    treev = GTK_TREE_VIEW (w);
    gtk_tree_view_set_fixed_height_mode (treev, TRUE);
    g_signal_connect (G_OBJECT (w), "query-tooltip",
            G_CALLBACK (query_tooltip_cb), NULL);
    gtk_widget_set_has_tooltip (w, TRUE);

    tree         = DONNA_TREE_VIEW (w);
    priv         = tree->priv;
    priv->donna  = donna;
    priv->name   = name;

    g_debug ("load_config for new tree '%s'", priv->name);
    load_config (tree);

    if (is_tree (tree))
    {
        g_debug ("treeview '%s': setting up as tree", priv->name);
        /* store */
        store = gtk_tree_store_new (DONNA_TREE_NB_COLS,
                G_TYPE_OBJECT,  /* DONNA_TREE_COL_NODE */
                G_TYPE_INT,     /* DONNA_TREE_COL_EXPAND_STATE */
                G_TYPE_STRING,  /* DONNA_TREE_COL_NAME */
                G_TYPE_OBJECT,  /* DONNA_TREE_COL_ICON */
                G_TYPE_STRING,  /* DONNA_TREE_COL_BOX */
                G_TYPE_STRING); /* DONNA_TREE_COL_HIGHLIGHT */
        model = GTK_TREE_MODEL (store);
        /* some stylling */
        gtk_tree_view_set_enable_tree_lines (treev, TRUE);
        gtk_tree_view_set_rules_hint (treev, FALSE);
        gtk_tree_view_set_headers_visible (treev, FALSE);
    }
    else
    {
        g_debug ("treeview '%s': setting up as list", priv->name);
        /* store */
        store = gtk_tree_store_new (DONNA_LIST_NB_COLS,
                G_TYPE_OBJECT); /* DONNA_LIST_COL_NODE */
        model = GTK_TREE_MODEL (store);
        /* some stylling */
        gtk_tree_view_set_rules_hint (treev, TRUE);
        gtk_tree_view_set_headers_visible (treev, TRUE);
    }

    g_debug ("treeview '%s': setting up filter & selection", priv->name);

    /* we use a filter (to show/hide .files, set Visual Filters, etc) */
    model_filter = gtk_tree_model_filter_new (model, NULL);
    filter = GTK_TREE_MODEL_FILTER (model_filter);
    gtk_tree_model_filter_set_visible_func (filter, visible_func, treev, NULL);
    /* add to tree */
    gtk_tree_view_set_model (treev, model_filter);

    /* selection mode */
    sel = gtk_tree_view_get_selection (treev);
    gtk_tree_selection_set_mode (sel, (is_tree (tree))
            ? GTK_SELECTION_BROWSE : GTK_SELECTION_MULTIPLE);

    g_signal_connect (G_OBJECT (sel), "changed",
            G_CALLBACK (selection_changed_cb), tree);

    /* columns */
    donna_tree_view_build_arrangement (tree, FALSE);

    return w;
}
