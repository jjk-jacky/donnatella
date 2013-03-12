
#define _GNU_SOURCE             /* strchrnul() in string.h */
#include <gtk/gtk.h>
#include <string.h>             /* strchr(), strncmp() */
#include "treeview.h"
#include "common.h"
#include "provider.h"
#include "node.h"
#include "task.h"
#include "sharedstring.h"
#include "macros.h"
#include "columntype-name.h"    /* DONNA_TYPE_COLUMNTYPE_NAME */

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
    RENDERER_SPINNER,
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

struct as_col
{
    GtkTreeViewColumn *column;
    GPtrArray *tasks;
    guint nb;
};

struct active_spinners
{
    DonnaNode   *node;
    GArray      *as_cols;   /* struct as_col[] */
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
    DonnaApp            *app;
    DonnaSharedString   *name;

    /* so we re-use the same renderer for all columns */
    GtkCellRenderer     *renderers[NB_RENDERERS];

    /* current arrangement */
    DonnaSharedString   *arrangement;

    /* properties used by our columns */
    GArray              *col_props;

    /* handling of spinners on columns (when setting node properties) */
    GPtrArray           *active_spinners;
    guint                active_spinners_id;
    guint                active_spinners_pulse;

    /* List: current/future location */
    DonnaNode           *location;
    GtkTreeIter          location_iter;
    DonnaNode           *future_location;

    /* hashtable of nodes & their iters on TV */
    GHashTable          *hashtable;

    /* list of iters to be used by callbacks. Because we use iters in cb's data,
     * we need to ensure they stay valid. We only use iters from the store, and
     * they are persistent. However, the row could be removed, thus the iter
     * wouldn't be valid anymore.
     * To handle this, whenver an iter is used in a cb's data, a pointer is
     * added in this list. When a row is removed, any iter pointing to that row
     * is removed, that way in the cb we can check if the iter is still there or
     * not. If not, it means it's invalid/the row was removed. */
    GSList              *watched_iters;

    /* providers we're connected to */
    GPtrArray           *providers;

    /* list of props on nodes being refreshed (see refresh_node_prop_cb) */
    GMutex               refresh_node_props_mutex;
    GSList              *refresh_node_props;

    /* "cached" options */
    guint                mode        : 1;
    guint                node_types  : 3;
    guint                show_hidden : 1;
    /* mode Tree */
    guint                is_minitree : 1;
    guint                sync_mode   : 2;
};

/* our internal renderers */
enum
{
    INTERNAL_RENDERER_SPINNER = 0,
    INTERNAL_RENDERER_PIXBUF,
    NB_INTERNAL_RENDERERS
};
static GtkCellRenderer *int_renderers[NB_INTERNAL_RENDERERS] = { NULL, };

/* we *need* to use this when creating an iter, because we compare *all* values
 * to determine if two iters match (and models might not always set all values) */
#define ITER_INIT           { 0, }
#define itereq(i1, i2)      \
    ((i1)->stamp == (i2)->stamp && (i1)->user_data == (i2)->user_data   \
     && (i1)->user_data2 == (i2)->user_data2      \
     && (i1)->user_data3 == (i2)->user_data3)

#define watch_iter(tree, iter)  \
    tree->priv->watched_iters = g_slist_append (tree->priv->watched_iters, iter)
#define remove_watch_iter(tree, iter)   \
    tree->priv->watched_iters = g_slist_remove (tree->priv->watched_iters, iter)

#define is_tree(tree)       (tree->priv->mode == DONNA_TREE_VIEW_MODE_TREE)
#define get_filter(tree)   \
    (GTK_TREE_MODEL_FILTER (gtk_tree_view_get_model (GTK_TREE_VIEW (tree))))
#define get_store_from_filter(filter)   \
    (GTK_TREE_STORE (gtk_tree_model_filter_get_model (filter)))
#define get_store(tree)     (get_store_from_filter (get_filter (tree)))
#define get_model(tree)     (gtk_tree_model_filter_get_model (get_filter (tree)))

#define get_name(priv)      (donna_shared_string (priv->name))

static gboolean add_node_to_tree (DonnaTreeView *tree,
                                  GtkTreeIter   *parent,
                                  DonnaNode     *node,
                                  GtkTreeIter   *row);

static struct active_spinners * get_as_for_node (DonnaTreeView   *tree,
                                                 DonnaNode       *node,
                                                 guint           *index,
                                                 gboolean         create);

static void free_col_prop (struct col_prop *cp);
static void free_provider_signals (struct provider_signals *ps);
static void free_active_spinners (struct active_spinners *as);

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

    if (!int_renderers[INTERNAL_RENDERER_SPINNER])
        int_renderers[INTERNAL_RENDERER_SPINNER] =
            gtk_cell_renderer_spinner_new ();
    if (!int_renderers[INTERNAL_RENDERER_PIXBUF])
        int_renderers[INTERNAL_RENDERER_PIXBUF] =
            gtk_cell_renderer_pixbuf_new ();
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
    priv->active_spinners = g_ptr_array_new_with_free_func (
            (GDestroyNotify) free_active_spinners);
}

static void
free_col_prop (struct col_prop *cp)
{
    donna_shared_string_unref (cp->prop);
}

static void
free_provider_signals (struct provider_signals *ps)
{
    if (ps->sid_node_updated)
        g_signal_handler_disconnect (ps->provider, ps->sid_node_updated);
    if (ps->sid_node_removed)
        g_signal_handler_disconnect (ps->provider, ps->sid_node_removed);
    if (ps->sid_node_children)
        g_signal_handler_disconnect (ps->provider, ps->sid_node_children);
    if (ps->sid_node_new_child)
        g_signal_handler_disconnect (ps->provider, ps->sid_node_new_child);
    g_object_unref (ps->provider);
    g_free (ps);
}

static void
free_active_spinners (struct active_spinners *as)
{
    g_object_unref (as->node);
    g_array_free (as->as_cols, TRUE);
    g_free (as);
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
    g_ptr_array_free (priv->active_spinners, TRUE);

    G_OBJECT_CLASS (donna_tree_view_parent_class)->finalize (object);
}

static void
load_config (DonnaTreeView *tree)
{
    DonnaTreeViewPrivate *priv;
    DonnaConfig *config;
    guint val;

    /* we load/cache some options, because usually we can't just get those when
     * needed, but they need to trigger some refresh or something. So we need to
     * listen on the option_{set,removed} signals of the config manager anyways.
     * Might as well save a few function calls... */

    priv = tree->priv;
    config = donna_app_get_config (priv->app);

    if (donna_config_get_uint (config, (guint *) &val,
                "treeviews/%s/mode", get_name (priv)))
        priv->mode = val;
    else
    {
        g_warning ("Treeview '%s': Unable to find mode, defaulting to list",
                get_name (priv));
        /* set default */
        val = priv->mode = DONNA_TREE_VIEW_MODE_LIST;
        donna_config_set_uint (config, (guint) val,
                "treeviews/%s/mode", priv->mode);
    }

    if (donna_config_get_boolean (config, (gboolean *) &val,
                "treeviews/%s/show_hidden", get_name (priv)))
        priv->show_hidden = val;
    else
    {
        /* set default */
        val = priv->show_hidden = TRUE;
        donna_config_set_boolean (config, (gboolean) val,
                "treeviews/%s/show_hidden", get_name (priv));
    }

    if (donna_config_get_uint (config, &val,
                "treeviews/%s/node_types", get_name (priv)))
        priv->node_types = val;
    else
    {
        /* set default */
        val = DONNA_NODE_CONTAINER;
        if (!is_tree (tree))
            val |= DONNA_NODE_ITEM;
        priv->node_types = val;
        donna_config_set_uint (config, val,
                "treeviews/%s/node_types", get_name (priv));
    }

    if (is_tree (tree))
    {
        if (donna_config_get_boolean (config, (gboolean *) &val,
                    "treeviews/%s/is_minitree", get_name (priv)))
            priv->is_minitree = val;
        else
        {
            /* set default */
            val = priv->is_minitree = FALSE;
            donna_config_set_boolean (config, (gboolean) val,
                    "treeview/%s/is_minitree", get_name (priv));
        }
    }
    else
    {
    }

    g_object_unref (config);
}

static gboolean
is_watched_iter_valid (DonnaTreeView *tree, GtkTreeIter *iter, gboolean remove)
{
    DonnaTreeViewPrivate *priv = tree->priv;
    GSList *l, *prev = NULL;

    l = priv->watched_iters;
    while (l)
    {
        if (l->data == iter)
        {
            if (remove)
            {
                if (prev)
                    prev->next = l->next;
                else
                    priv->watched_iters = l->next;

                g_slist_free_1 (l);
            }
            return TRUE;
        }
        else
        {
            prev = l;
            l = prev->next;
        }
    }
    return FALSE;
}

struct node_children_data
{
    DonnaTreeView   *tree;
    GtkTreeStore    *store;
    GtkTreeIter      iter;
};

static void
free_node_children_data (struct node_children_data *data)
{
    remove_watch_iter (data->tree, &data->iter);
    g_slice_free (struct node_children_data, data);
}

static void
node_get_children_timeout (DonnaTask *task, struct node_children_data *data)
{
    GtkTreePath *path;

    /* we're slow to get the children, let's just show the fake node ("please
     * wait...") */
    if (!is_watched_iter_valid (data->tree, &data->iter, FALSE))
        return;
    path = gtk_tree_model_get_path (GTK_TREE_MODEL (data->store), &data->iter);
    gtk_tree_view_expand_row (GTK_TREE_VIEW (data->tree), path, FALSE);
    gtk_tree_path_free (path);
}

/* similar to gtk_tree_store_remove() this will set iter to next row at that
 * level, or invalid it if it pointer to the last one.
 * Returns TRUE if iter is still valid, else FALSE */
static gboolean
remove_row_from_tree (DonnaTreeView *tree, GtkTreeIter *iter)
{
    DonnaTreeViewPrivate *priv = tree->priv;
    GtkTreeStore *store;
    GtkTreeModel *model;
    DonnaNode *node;
    DonnaProvider *provider;
    guint i;
    GSList *l, *prev = NULL;
    GtkTreeIter parent = ITER_INIT;
    gboolean ret;

    store = get_store (GTK_TREE_VIEW (tree));
    model = GTK_TREE_MODEL (store);

    /* get the node */
    gtk_tree_model_get (model, iter,
            DONNA_TREE_VIEW_COL_NODE,   &node,
            -1);
    if (node)
    {
        GSList *list;

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

        /* remove iter for that row in hashtable */
        l = list = g_hash_table_lookup (priv->hashtable, node);
        while (l)
        {
            if (itereq (iter, (GtkTreeIter *) l->data))
            {
                if (prev)
                    prev->next = l->next;
                else
                    list = l->next;

                gtk_tree_iter_free (l->data);
                g_slist_free_1 (l);
                break;
            }
            else
            {
                prev = l;
                l = l->next;
            }
        }
        if (list)
            g_hash_table_insert (priv->hashtable, node, list);
        else
            g_hash_table_remove (priv->hashtable, node);

        g_object_unref (node);
    }

    /* remove all watched_iters to this row */
    l = priv->watched_iters;
    while (l)
    {
        if (itereq (iter, (GtkTreeIter *) l->data))
        {
            GSList *next = l->next;

            if (prev)
                prev->next = next;
            else
                priv->watched_iters = next;

            g_slist_free_1 (l);
            l = next;
        }
        else
        {
            prev = l;
            l = l->next;
        }
    }

    if (is_tree (tree))
        /* get the parent, in case we're removing its last child */
        gtk_tree_model_iter_parent (model, &parent, iter);
    /* now we can remove the row */
    ret = gtk_tree_store_remove (store, iter);
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

    return ret;
}

static void
set_children (DonnaTreeView *tree,
              GtkTreeIter   *iter,
              GPtrArray     *children,
              gboolean       expand)
{
    GtkTreeStore *store;
    GtkTreeModel *model;

    if (!is_tree (tree))
        return;

    store = get_store (GTK_TREE_VIEW (tree));
    model = GTK_TREE_MODEL (store);

    if (children->len == 0)
    {
        GtkTreeIter child = ITER_INIT;

        /* set new expand state */
        gtk_tree_store_set (store, iter,
                DONNA_TREE_COL_EXPAND_STATE,    DONNA_TREE_EXPAND_NONE,
                -1);
        if (gtk_tree_model_iter_children (model, &child, iter))
            while (remove_row_from_tree (tree, &child))
                ;
    }
    else
    {
        GSList *list = NULL;
        enum tree_expand es;
        guint i;

        gtk_tree_model_get (model, iter,
                DONNA_TREE_COL_EXPAND_STATE,    &es,
                -1);
        if (es == DONNA_TREE_EXPAND_FULL || es == DONNA_TREE_EXPAND_PARTIAL
                || es == DONNA_TREE_EXPAND_NEVER_FULL)
        {
            GtkTreeIter i = ITER_INIT;

            gtk_tree_model_iter_children (model, &i, iter);
            do
            {
                list = g_slist_append (list, gtk_tree_iter_copy (&i));
            } while (gtk_tree_model_iter_next (model, &i));
        }
        else
            es = DONNA_TREE_EXPAND_UNKNOWN;

        for (i = 0; i < children->len; ++i)
        {
            GtkTreeIter row = ITER_INIT;

            /* shouldn't be able to fail/return FALSE */
            if (!add_node_to_tree (tree, iter, children->pdata[i], &row))
            {
                const gchar *domain;
                DonnaSharedString *location;

                donna_node_get (children->pdata[i], FALSE,
                        "domain",   &domain,
                        "location", &location,
                        NULL);
                g_critical ("Treeview '%s': failed to add node for '%s:%s'",
                        get_name (tree->priv),
                        domain,
                        donna_shared_string (location));
                donna_shared_string_unref (location);
            }
            else if (es)
            {
                GSList *l, *prev = NULL;

                /* remove the iter for that row */
                l = list;
                while (l)
                {
                    if (itereq ((GtkTreeIter *) l->data, &row))
                    {
                        if (prev)
                            prev->next = l->next;
                        else
                            list = l->next;

                        gtk_tree_iter_free (l->data);
                        g_slist_free_1 (l);
                        break;
                    }
                    prev = l;
                    l = prev->next;
                }
            }
        }
        /* remove rows not in children */
        while (list)
        {
            GSList *l;

            l = list;
            remove_row_from_tree (tree, l->data);
            gtk_tree_iter_free (l->data);
            list = l->next;
            g_slist_free_1 (l);
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
    DonnaTaskState   state;

    if (!is_watched_iter_valid (data->tree, &data->iter, TRUE))
        return;

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
        gtk_tree_view_collapse_row (GTK_TREE_VIEW (data->tree), path);
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
        /* FIXME */
        donna_error ("Treeview '%s': Failed to get children for node '%s:%s': %s",
                get_name (data->tree->priv),
                domain,
                donna_shared_string (location),
                (error) ? error->message : "No error message");
        donna_shared_string_unref (location);
        g_object_unref (node);

        free_node_children_data (data);
        return;
    }

    set_children (data->tree, &data->iter,
            g_value_get_boxed (donna_task_get_return_value (task)),
            TRUE /* expand node */);

    free_node_children_data (data);
}

struct import_children_data
{
    DonnaTreeView   *tree;
    GtkTreeStore    *store;
    GtkTreeIter     *iter;
    GtkTreeIter     *child;
};

static gboolean
import_children (struct import_children_data *data)
{
    GtkTreeModel *model;
    GtkTreePath *path;

    if (!is_watched_iter_valid (data->tree, data->iter, TRUE))
    {
        remove_watch_iter (data->tree, data->child);
        goto free;
    }
    if (!is_watched_iter_valid (data->tree, data->child, TRUE))
        goto free;

    model = GTK_TREE_MODEL (data->store);
    do
    {
        DonnaNode *node;

        gtk_tree_model_get (model, data->child,
                DONNA_TREE_COL_NODE,    &node,
                -1);
        add_node_to_tree (data->tree, data->iter, node, NULL);
        g_object_unref (node);
    } while (gtk_tree_model_iter_next (model, data->child));

    /* update expand state */
    gtk_tree_store_set (data->store, data->iter,
            DONNA_TREE_COL_EXPAND_STATE,    DONNA_TREE_EXPAND_FULL,
            -1);

    /* expand node */
    path = gtk_tree_model_get_path (model, data->iter);
    gtk_tree_view_expand_row (GTK_TREE_VIEW (data->tree), path, FALSE);
    gtk_tree_path_free (path);

free:
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
    DonnaTreeView *tree = DONNA_TREE_VIEW (treev);
    GtkTreeModelFilter *filter;
    GtkTreeStore *store;
    GtkTreeModel *model;
    enum tree_expand expand_state;

    filter = get_filter (tree);
    store  = get_store_from_filter (filter);
    model  = GTK_TREE_MODEL (store);

    gtk_tree_model_get (GTK_TREE_MODEL (filter), _iter,
            DONNA_TREE_COL_EXPAND_STATE,    &expand_state,
            -1);
    switch (expand_state)
    {
        /* allow expansion, just update expand state */
        case DONNA_TREE_EXPAND_NEVER_FULL:
            {
                GtkTreeIter iter = ITER_INIT;

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
                    /* FIXME */
                    donna_error ("Treeview '%s': could not get node from model",
                            get_name (priv));
                    return TRUE;
                }

                /* is there another tree node for this node? */
                list = g_hash_table_lookup (priv->hashtable, node);
                if (list)
                {
                    GtkTreeIter iter = ITER_INIT;

                    gtk_tree_model_filter_convert_iter_to_child_iter (
                            filter,
                            &iter,
                            _iter);
                    for ( ; list; list = list->next)
                    {
                        GtkTreeIter *i = list->data;
                        enum tree_expand es;

                        /* skip ourself */
                        if (itereq (&iter, i))
                            continue;

                        gtk_tree_model_get (model, i,
                                DONNA_TREE_COL_EXPAND_STATE,    &es,
                                -1);
                        if (es == DONNA_TREE_EXPAND_FULL
                                || es == DONNA_TREE_EXPAND_NEVER_FULL)
                        {
                            GtkTreeIter child = ITER_INIT;
                            struct import_children_data *data;

                            /* let's import the children */

                            if (!gtk_tree_model_iter_children (model,
                                    &child, i))
                            {
                                g_critical ("Treeview '%s': Inconsistency detected",
                                        get_name (priv));
                                continue;
                            }

                            /* because the tree's model is filter, and with
                             * filter iters are not persistent, adding nodes
                             * would invalidate the current _iter, thus
                             * generating a warning in the post-signal of the
                             * test-expand-row in GtkTreeView.
                             * So we need to have that code run later */
                            data = g_new (struct import_children_data, 1);
                            data->tree  = tree;
                            data->store = store;
                            data->iter  = gtk_tree_iter_copy (&iter);
                            data->child = gtk_tree_iter_copy (&child);
                            watch_iter (tree, data->iter);
                            watch_iter (tree, data->child);
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
                        priv->node_types);

                data = g_slice_new0 (struct node_children_data);
                data->tree  = tree;
                data->store = store;
                gtk_tree_model_filter_convert_iter_to_child_iter (filter,
                        &data->iter,
                        _iter);
                watch_iter (tree, &data->iter);

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

                donna_app_run_task (priv->app, task);
                g_object_unref (node);
                g_object_unref (provider);
            }
            return TRUE;

        /* refuse expansion. This case should never happen */
        case DONNA_TREE_EXPAND_NONE:
            g_critical ("Treeview '%s' wanted to expand a node without children",
                    get_name (tree->priv));
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
              DonnaTreeView *tree)
{
    DonnaNode *node;
    DonnaSharedString *name;
    gboolean ret;

    if (tree->priv->show_hidden)
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
                g_critical ("Treeview '%s': refresh_node_prop_cb for missing node",
                        get_name (priv));
                goto bail;
            }

            model = get_model (data->tree);
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

static gboolean
spinner_fn (DonnaTreeView *tree)
{
    DonnaTreeViewPrivate *priv = tree->priv;
    GtkTreeModel *model;
    guint i;
    gboolean active = FALSE;

    if (!priv->active_spinners_id)
        return FALSE;

    if (!priv->active_spinners->len)
    {
        g_source_remove (priv->active_spinners_id);
        priv->active_spinners_id = 0;
        priv->active_spinners_pulse = 0;
        return FALSE;
    }

    model = get_model (tree);
    for (i = 0; i < priv->active_spinners->len; ++i)
    {
        struct active_spinners *as = priv->active_spinners->pdata[i];
        GSList *list;
        guint j;
        gboolean refresh;

        refresh = FALSE;
        for (j = 0; j < as->as_cols->len; ++j)
        {
            struct as_col *as_col;

            as_col = &g_array_index (as->as_cols, struct as_col, j);
            if (as_col->nb > 0)
            {
                active = refresh = TRUE;
                break;
            }
        }
        if (!refresh)
            continue;

        list = g_hash_table_lookup (priv->hashtable, as->node);
        for ( ; list; list = list->next)
        {
            GtkTreeIter *iter = list->data;
            GtkTreePath *path;

            path = gtk_tree_model_get_path (model, iter);
            gtk_tree_model_row_changed (model, path, iter);
            gtk_tree_path_free (path);
        }
    }

    if (!active)
    {
        /* there are active spinners only for error messages */
        g_source_remove (priv->active_spinners_id);
        priv->active_spinners_id = 0;
        priv->active_spinners_pulse = 0;
        return FALSE;
    }

    ++priv->active_spinners_pulse;
    return TRUE;
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

    /* spinner */
    if (index == INTERNAL_RENDERER_SPINNER || index == INTERNAL_RENDERER_PIXBUF)
    {
        GtkTreeModelFilter *filter;
        GtkTreeIter iter = ITER_INIT;
        struct active_spinners *as;
        DonnaNode *node;

        if (!priv->active_spinners->len)
        {
            g_object_set (renderer,
                    "visible",  FALSE,
                    NULL);
            return;
        }

        gtk_tree_model_get (_model, _iter, DONNA_TREE_VIEW_COL_NODE, &node, -1);
        if (!node)
            return;

        as = get_as_for_node (tree, node, NULL, FALSE);
        g_object_unref (node);
        if (as)
        {
            guint i;

            for (i = 0; i < as->as_cols->len; ++i)
            {
                struct as_col *as_col;

                as_col = &g_array_index (as->as_cols, struct as_col, i);
                if (as_col->column != column)
                    continue;

                if (index == INTERNAL_RENDERER_SPINNER)
                {
                    if (as_col->nb > 0)
                    {
                        g_object_set (renderer,
                                "visible",  TRUE,
                                "active",   TRUE,
                                "pulse",    priv->active_spinners_pulse,
                                NULL);
                        return;
                    }
                }
                else /* INTERNAL_RENDERER_PIXBUF */
                {
                    for (i = 0; i < as_col->tasks->len; ++i)
                    {
                        DonnaTask *task = as_col->tasks->pdata[i];

                        if (donna_task_get_state (task) == DONNA_TASK_FAILED)
                        {
                            g_object_set (renderer,
                                    "visible",      TRUE,
                                    "stock-id",     GTK_STOCK_DIALOG_WARNING,
                                    "follow-state", TRUE,
                                    NULL);
                            return;
                        }
                    }
                }
                break;
            }
        }

        g_object_set (renderer,
                "visible",  FALSE,
                NULL);
        return;
    }

    index -= NB_INTERNAL_RENDERERS - 1; /* -1 to start with index 1 */

    ct   = g_object_get_data (G_OBJECT (column), "column-type");
    col  = g_object_get_data (G_OBJECT (column), "column-name");
    gtk_tree_model_get (_model, _iter, DONNA_TREE_VIEW_COL_NODE, &node, -1);

    if (is_tree (tree))
    {
        if (!node)
        {
            /* this is a "fake" node, shown as a "Please Wait..." */
            /* we can only do that for a column of type "name" */
            if (G_TYPE_FROM_INSTANCE (ct) != DONNA_TYPE_COLUMNTYPE_NAME)
                return;

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
        if (G_TYPE_FROM_INSTANCE (ct) == DONNA_TYPE_COLUMNTYPE_NAME)
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
                    g_object_unref (node);
                    return;
                }
            }
        }
    }
    else if (!node)
        return;

    arr = donna_columntype_render (ct, get_name (priv), col, index, node, renderer);
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
        donna_app_run_task (priv->app, task);
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

    ret = donna_columntype_node_cmp (ct, get_name (priv), col, node1, node2);
    g_object_unref (node1);
    g_object_unref (node2);
    return ret;
}

static void
node_has_children_cb (DonnaTask                 *task,
                      gboolean                   timeout_called,
                      struct node_children_data *data)
{
    GtkTreeModel *model = GTK_TREE_MODEL (data->store);
    DonnaTaskState state;
    const GValue *value;
    gboolean has_children;
    enum tree_expand es;

    if (!is_watched_iter_valid (data->tree, &data->iter, TRUE))
        goto free;

    state = donna_task_get_state (task);
    if (state != DONNA_TASK_DONE)
        /* we don't know if the node has children, so we'll keep the fake node
         * in, with expand state to UNKNOWN as it is. That way the user can ask
         * for expansion, which could simply have the expander removed if it
         * wasn't needed after all... */
        goto free;

    value = donna_task_get_return_value (task);
    has_children = g_value_get_boolean (value);

    gtk_tree_model_get (model, &data->iter,
            DONNA_TREE_COL_EXPAND_STATE,    &es,
            -1);
    switch (es)
    {
        case DONNA_TREE_EXPAND_UNKNOWN:
        case DONNA_TREE_EXPAND_NEVER:
        case DONNA_TREE_EXPAND_WIP:
            if (!has_children)
            {
                GtkTreeIter iter;

                /* remove fake node */
                if (gtk_tree_model_iter_children (model, &iter, &data->iter))
                    gtk_tree_store_remove (data->store, &iter);
                /* update expand state */
                gtk_tree_store_set (data->store, &data->iter,
                        DONNA_TREE_COL_EXPAND_STATE,    DONNA_TREE_EXPAND_NONE,
                        -1);
            }
            else
            {
                /* fake node already there, we just update the expand state,
                 * unless we're WIP then we'll let get_children set it right
                 * once the children have been added */
                if (es == DONNA_TREE_EXPAND_UNKNOWN)
                    gtk_tree_store_set (data->store, &data->iter,
                            DONNA_TREE_COL_EXPAND_STATE, DONNA_TREE_EXPAND_NEVER,
                            -1);
            }
            break;

        case DONNA_TREE_EXPAND_NEVER_FULL:
        case DONNA_TREE_EXPAND_PARTIAL:
        case DONNA_TREE_EXPAND_FULL:
            if (!has_children)
            {
                GtkTreeIter iter;

                /* remove all children */
                if (gtk_tree_model_iter_children (model, &iter, &data->iter))
                    while (remove_row_from_tree (data->tree, &iter))
                        ;
                /* update expand state */
                gtk_tree_store_set (data->store, &data->iter,
                        DONNA_TREE_COL_EXPAND_STATE, DONNA_TREE_EXPAND_NONE,
                        -1);
            }
            /* else: children and expand state obviously already good */
            break;

        case DONNA_TREE_EXPAND_NONE:
            if (has_children)
            {
                /* add fake node */
                gtk_tree_store_insert_with_values (data->store,
                        NULL, &data->iter, 0,
                        DONNA_TREE_COL_NODE,    NULL,
                        -1);
                /* update expand state */
                gtk_tree_store_set (data->store, &data->iter,
                        DONNA_TREE_COL_EXPAND_STATE, DONNA_TREE_EXPAND_NEVER,
                        -1);
            }
            /* else: already no fake node */
            break;
    }

free:
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
    l = g_hash_table_lookup (priv->hashtable, node);
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
    model = get_model (tree);
    for ( ; l; l = l->next)
    {
        GtkTreeIter *iter = l->data;
        GtkTreePath *path;

        path = gtk_tree_model_get_path (model, iter);
        gtk_tree_model_row_changed (model, path, iter);
        gtk_tree_path_free (path);
    }
}

struct node_removed_data
{
    DonnaTreeView   *tree;
    DonnaNode       *node;
};

static gboolean
real_node_removed_cb (struct node_removed_data *data)
{
    DonnaTreeViewPrivate *priv = data->tree->priv;
    GSList *list;
    GSList *next;

    list = g_hash_table_lookup (priv->hashtable, data->node);
    if (!list)
        goto free;

    for ( ; list; list = next)
    {
        next = list->next;
        /* this will remove the row from the list in hashtable. IOW, it will
         * remove the current list element (list); which is why we took the next
         * element ahead of time */
        remove_row_from_tree (data->tree, list->data);
    }

free:
    g_object_unref (data->node);
    g_free (data);
    /* don't repeat */
    return FALSE;
}

static void
node_removed_cb (DonnaProvider  *provider,
                 DonnaNode      *node,
                 DonnaTreeView  *tree)
{
    struct node_removed_data *data;

    if (!is_tree (tree))
        return;

    /* we might not be in the main thread, but we need to be */
    data = g_new (struct node_removed_data, 1);
    data->tree       = tree;
    data->node       = g_object_ref (node);
    g_main_context_invoke (NULL, (GSourceFunc) real_node_removed_cb, data);
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

    if (priv->location != data->node)
        goto free;

    if (!(data->node_types & priv->node_types))
        goto free;

    set_children (data->tree, &priv->location_iter, data->children, FALSE);

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

struct new_child_data
{
    DonnaTreeView   *tree;
    DonnaNode       *node;
    DonnaNode       *child;
};

static gboolean
real_new_child_cb (struct new_child_data *data)
{
    DonnaTreeViewPrivate *priv = data->tree->priv;
    GSList *list;

    if (!is_tree (data->tree))
    {
        if (priv->location == data->node)
            add_node_to_tree (data->tree, NULL, data->child, NULL);
        goto free;
    }

    list = g_hash_table_lookup (priv->hashtable, data->node);
    if (!list)
        goto free;

    for ( ; list; list = list->next)
        add_node_to_tree (data->tree, list->data, data->child, NULL);

free:
    g_object_unref (data->node);
    g_object_unref (data->child);
    g_free (data);
    /* no repeat */
    return FALSE;
}

static void
node_new_child_cb (DonnaProvider *provider,
                   DonnaNode     *node,
                   DonnaNode     *child,
                   DonnaTreeView *tree)
{
    DonnaTreeViewPrivate *priv = tree->priv;
    DonnaNodeType type;
    struct new_child_data *data;

    /* list: unless we're in node, we don't care */
    if (!is_tree (tree) && priv->location != node)
        return;

    /* if we don't care for this type of nodes, nothing to do */
    donna_node_get (child, FALSE, "node-type", &type, NULL);
    if (!(type & priv->node_types))
        return;

    /* we can't check if node is in the tree though, because there's no lock,
     * and we might not be in the main thread, and so we need to be */
    data = g_new (struct new_child_data, 1);
    data->tree  = tree;
    data->node  = g_object_ref (node);
    data->child = g_object_ref (child);
    g_main_context_invoke (NULL, (GSourceFunc) real_new_child_cb, data);
}

/* mode tree only */
static inline GtkTreeIter *
get_child_iter_for_node (DonnaTreeView  *tree,
                         GtkTreeIter    *parent,
                         DonnaNode      *node)
{
    DonnaTreeViewPrivate *priv = tree->priv;
    GtkTreeModel *model;
    GSList *list;

    model = get_model (tree);
    list = g_hash_table_lookup (priv->hashtable, node);
    for ( ; list; list = list->next)
    {
        GtkTreeIter *i = list->data;
        GtkTreeIter  p = ITER_INIT;

        /* get the parent and compare with our parent iter */
        if (gtk_tree_model_iter_parent (model, &p, i)
                && itereq (&p, parent))
            return i;
    }
    return NULL;
}

static gboolean
add_node_to_tree (DonnaTreeView *tree,
                  GtkTreeIter   *parent,
                  DonnaNode     *node,
                  GtkTreeIter   *iter_row)
{
    const gchar             *domain;
    DonnaSharedString       *ss;
    DonnaTreeViewPrivate    *priv;
    GtkTreeView             *treev;
    GtkTreeStore            *store;
    GtkTreeModel            *model;
    GtkTreeIter              iter = ITER_INIT;
    GSList                  *list;
    GSList                  *l;
    DonnaProvider           *provider;
    DonnaNodeType            node_type;
    DonnaTask               *task;
    gboolean                 added;
    guint                    i;

    g_return_val_if_fail (DONNA_IS_TREE_VIEW (tree), FALSE);
    g_return_val_if_fail (DONNA_IS_NODE (node), FALSE);

    priv  = tree->priv;
    treev = GTK_TREE_VIEW (tree);
    model = get_model (tree);
    store = GTK_TREE_STORE (model);

    /* is there already a row for this node at that level? */
    if (parent)
    {
        GtkTreeIter *i;

        i = get_child_iter_for_node (tree, parent, node);
        if (i)
        {
            /* already exists under the same parent, nothing to do */
            if (iter_row)
                *iter_row = *i;
            return TRUE;
        }
    }

    donna_node_get (node, FALSE, "domain", &domain, "location", &ss, NULL);
    g_debug ("treeview '%s': adding new node %p for '%s:%s'",
            get_name (priv),
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
    if (iter_row)
        *iter_row = iter;
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

        if (itereq (&iter, i))
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
            break;
        }
    }
    /* get provider to get task to know if it has children */
    donna_node_get (node, FALSE,
            "provider",  &provider,
            "node-type", &node_type,
            NULL);
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

        ps = g_new0 (struct provider_signals, 1);
        ps->provider = g_object_ref (provider);
        ps->nb_nodes = 1;
        ps->sid_node_updated = g_signal_connect (provider, "node-updated",
                G_CALLBACK (node_updated_cb), tree);
        ps->sid_node_removed = g_signal_connect (provider, "node-removed",
                G_CALLBACK (node_removed_cb), tree);
        if (node_type != DONNA_NODE_ITEM)
        {
            ps->sid_node_children = g_signal_connect (provider, "node-children",
                    G_CALLBACK (node_children_cb), tree);
            ps->sid_node_new_child = g_signal_connect (provider, "node-new-child",
                    G_CALLBACK (node_new_child_cb), tree);
        }

        g_ptr_array_add (priv->providers, ps);
    }

    if (added || node_type == DONNA_NODE_ITEM)
    {
        if (node_type == DONNA_NODE_ITEM)
            gtk_tree_store_set (store, &iter,
                    DONNA_TREE_COL_EXPAND_STATE,    DONNA_TREE_EXPAND_NONE,
                    -1);
        g_object_unref (provider);
        return TRUE;
    }

    task = donna_provider_has_node_children_task (provider, node, priv->node_types);
    if (task)
    {
        struct node_children_data *data;

        data = g_slice_new0 (struct node_children_data);
        data->tree  = tree;
        data->store = store;
        data->iter  = iter;
        watch_iter (tree, &data->iter);

        /* insert a fake node so the user can ask for expansion right away (the
         * node will disappear if needed asap) */
        gtk_tree_store_insert_with_values (data->store,
                NULL, &data->iter, 0,
                DONNA_TREE_COL_NODE,    NULL,
                -1);

        donna_task_set_callback (task,
                (task_callback_fn) node_has_children_cb,
                data,
                (GDestroyNotify) free_node_children_data);
        donna_app_run_task (priv->app, task);
    }
    else
    {
        DonnaSharedString *location;

        /* insert a fake node, so user can try again by asking to expand it */
        gtk_tree_store_insert_with_values (store, NULL, &iter, 0,
            DONNA_TREE_COL_NODE,    NULL,
            -1);

        donna_node_get (node, FALSE, "location", &location, NULL);
        g_warning ("Treeview '%s': Unable to create a task to determine if the node '%s:%s' has children",
                get_name (priv), domain, donna_shared_string (location));
        donna_shared_string_unref (location);
    }

    g_object_unref (provider);
    return TRUE;
}

gboolean
donna_tree_view_add_root (DonnaTreeView *tree, DonnaNode *node)
{
    g_return_val_if_fail (DONNA_IS_TREE_VIEW (tree), FALSE);
    g_return_val_if_fail (is_tree (tree), FALSE);
    return add_node_to_tree (tree, NULL, node, NULL);
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

    config = donna_app_get_config (priv->app);
    sortable = GTK_TREE_SORTABLE (get_store (tree));
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
                    g_warning ("Treeview '%s': No columns defined in 'arrangements/tree'; using 'name'",
                            get_name (priv));
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
                    "treeviews/%s/columns/%s/type", get_name (priv), b))
        {
            if (!donna_config_get_shared_string (config, &ss,
                        "columns/%s/type", b))
            {
                g_warning ("Treeview '%s': No type defined for column '%s', fallback to its name",
                        get_name (priv), b);
                ss = NULL;
            }
        }

        ct = donna_app_get_columntype (priv->app,
                (ss) ? donna_shared_string (ss) : b);
        if (!ct)
        {
            g_warning ("Treeview '%s': Unable to load column-type '%s' for column '%s'",
                    get_name (priv), (ss) ? donna_shared_string (ss) : b, b);
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
            /* put our internal renderers */
            for (index = 0; index < NB_INTERNAL_RENDERERS; ++index)
            {
                gtk_tree_view_column_set_cell_data_func (column,
                        int_renderers[index],
                        rend_func,
                        GINT_TO_POINTER (index),
                        NULL);
                gtk_tree_view_column_pack_start (column, int_renderers[index],
                        FALSE);
            }
            /* load renderers */
            for (rend = donna_columntype_get_renderers (ct);
                    *rend;
                    ++index, ++rend)
            {
                GtkCellRenderer * (*load_renderer) (void);
                /* TODO: use an external (app-global) renderer loader? */
                switch (*rend)
                {
                    case DONNA_COLUMNTYPE_RENDERER_TEXT:
                        renderer = priv->renderers[RENDERER_TEXT];
                        load_renderer = gtk_cell_renderer_text_new;
                        break;
                    case DONNA_COLUMNTYPE_RENDERER_PIXBUF:
                        renderer = priv->renderers[RENDERER_PIXBUF];
                        load_renderer = gtk_cell_renderer_pixbuf_new;
                        break;
                    case DONNA_COLUMNTYPE_RENDERER_PROGRESS:
                        renderer = priv->renderers[RENDERER_PROGRESS];
                        load_renderer = gtk_cell_renderer_progress_new;
                        break;
                    case DONNA_COLUMNTYPE_RENDERER_COMBO:
                        renderer = priv->renderers[RENDERER_COMBO];
                        load_renderer = gtk_cell_renderer_combo_new;
                        break;
                    case DONNA_COLUMNTYPE_RENDERER_TOGGLE:
                        renderer = priv->renderers[RENDERER_TOGGLE];
                        load_renderer = gtk_cell_renderer_toggle_new;
                        break;
                    case DONNA_COLUMNTYPE_RENDERER_SPINNER:
                        renderer = priv->renderers[RENDERER_SPINNER];
                        load_renderer = gtk_cell_renderer_spinner_new;
                        break;
                    default:
                        g_critical ("Treeview '%s': Unknown renderer type '%c' for column '%s'",
                                get_name (priv), *rend, b);
                        continue;
                }
                if (!renderer)
                {
                    renderer = load_renderer ();
                    g_object_set_data (G_OBJECT (renderer), "renderer-type",
                            GINT_TO_POINTER (*rend));
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
                    "treeviews/%s/columns/%s/title", get_name (priv), b))
            if (!donna_config_get_shared_string (config, &ss,
                        "columns/%s/title", b))
            {
                g_warning ("Treeview '%s': No title set for column '%s', using its name",
                        get_name (priv), b);
                gtk_tree_view_column_set_title (column, b);
            }
        if (ss)
        {
            gtk_tree_view_column_set_title (column, donna_shared_string (ss));
            donna_shared_string_unref (ss);
        }

        /* props to watch for refresh */
        props = donna_columntype_get_props (ct, get_name (priv), b);
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
            g_critical ("Treeview '%s': column '%s' reports no properties to watch for refresh",
                    get_name (priv), b);

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
    config = donna_app_get_config (priv->app);
    g_debug ("treeview '%s': select arrangement", get_name (priv));

    if (is_tree (tree))
    {
        if (donna_config_has_category (config,
                    "treeviews/%s/arrangement", get_name (priv)))
            ss = donna_shared_string_new_take (
                    g_strdup_printf ("treeviews/%s/arrangement", get_name (priv)));
        else
            ss = donna_shared_string_new_dup ("arrangements/tree");
    }
    else
    {
        /* do we have an arrangement selector? */
        if (location)
            ss = donna_app_get_arrangement (priv->app, location);
        else
            ss = NULL;

        if (!ss)
        {
            if (donna_config_has_category (config,
                        "treeviews/%s/arrangement", get_name (priv)))
                ss = donna_shared_string_new_take (
                        g_strdup_printf ("treeviews/%s/arrangement", get_name (priv)));
            else
                ss = donna_shared_string_new_dup ("arrangements/list");
        }
    }

    g_debug ("treeview '%s': selected arrangement: %s", get_name (priv),
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

    priv = tree->priv;
    g_debug ("treeiew '%s': build arrangement (force=%d)",
            get_name (priv), force);

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

static struct active_spinners *
get_as_for_node (DonnaTreeView  *tree,
                 DonnaNode      *node,
                 guint          *index,
                 gboolean        create)
{
    DonnaTreeViewPrivate *priv = tree->priv;
    struct active_spinners *as;
    guint i;

    /* is there already an as for this node? */
    for (i = 0; i < priv->active_spinners->len; ++i)
    {
        as = priv->active_spinners->pdata[i];
        if (as->node == node)
            break;
    }

    if (i >= priv->active_spinners->len)
    {
        if (create)
        {
            as = g_new0 (struct active_spinners, 1);
            as->node = g_object_ref (node);
            as->as_cols = g_array_new (FALSE, FALSE, sizeof (struct as_col));

            g_ptr_array_add (priv->active_spinners, as);
        }
        else
            as = NULL;
    }

    if (index)
        *index = i;

    return as;
}

static void
set_node_prop_callbak (DonnaTask                 *task,
                       gboolean                   timeout_called,
                       struct set_node_prop_data *data)
{
    DonnaTreeViewPrivate *priv = data->tree->priv;
    gboolean task_failed;
    guint i;
    GPtrArray *arr;

    task_failed = donna_task_get_state (task) == DONNA_TASK_FAILED;

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
        /* FIXME error message if FAILED */
        return;
    }

    /* timeout called == spinners set; task failed == error message */
    if (timeout_called || task_failed)
    {
        struct active_spinners *as;
        guint as_idx; /* in case we need to remove this as */
        gboolean refresh = FALSE;

        as = get_as_for_node (data->tree, data->node, &as_idx, task_failed);
        if (!as)
            goto free;

        /* for every column using that property */
        for (i = 0; i < arr->len; ++i)
        {
            GtkTreeViewColumn *column = arr->pdata[i];
            struct as_col *as_col;
            guint j;

            /* does this as have a spinner for this column? */
            for (j = 0; j < as->as_cols->len; ++j)
            {
                as_col = &g_array_index (as->as_cols, struct as_col, j);
                if (as_col->column == column)
                    break;
            }
            if (j >= as->as_cols->len)
            {
                if (task_failed)
                {
                    struct as_col as_col_new;

                    as_col_new.column = column;
                    /* no as_col means no timeout called, so we can safely set
                     * nb to 0 */
                    as_col_new.nb = 0;
                    as_col_new.tasks = g_ptr_array_new_full (1, g_object_unref);
                    g_ptr_array_add (as_col_new.tasks, g_object_ref (task));
                    g_array_append_val (as->as_cols, as_col_new);
                    as_col = &g_array_index (as->as_cols, struct as_col, j);
                }
                else
                    continue;
            }
            else if (!timeout_called) /* implies task_failed */
                g_ptr_array_add (as_col->tasks, g_object_ref (task));

            if (!task_failed)
                g_ptr_array_remove_fast (as_col->tasks, task);

            if (timeout_called)
                --as_col->nb;

            if (as_col->nb == 0)
            {
                refresh = TRUE;
#ifndef GTK_IS_JJK
                if (task_failed)
                    /* a bug in GTK means that because when the size of renderer
                     * is first computed and renderer if not visible, it has a
                     * natural size of 0 and therefore even when it becomes
                     * visible it isn't actually drawn.
                     * This is a hack to workaround this, by enforcing the
                     * column to re-compute its size now that we'll have the
                     * renderer visible, so it should have a natural size and
                     * actually be drawn */
                    gtk_tree_view_column_queue_resize (column);
#endif
                /* can we remove this as_col? */
                if (as_col->tasks->len == 0)
                {
                    /* can we remove the whole as? */
                    if (as->as_cols->len == 1)
                        g_ptr_array_remove_index_fast (priv->active_spinners,
                                as_idx);
                    else
                        g_array_remove_index_fast (as->as_cols, j);
                }
            }
        }

        if (refresh)
        {
            GtkTreeModel *model;
            GSList *list;

            model = get_model (data->tree);
            /* for every row of this node */
            list = g_hash_table_lookup (priv->hashtable, data->node);
            for ( ; list; list = list->next)
            {
                GtkTreeIter *iter = list->data;
                GtkTreePath *path;

                /* make sure a redraw will be done for this row, else the
                 * last spinner frame stays there until a redraw happens */

                path = gtk_tree_model_get_path (model, iter);
                gtk_tree_model_row_changed (model, path, iter);
                gtk_tree_path_free (path);
            }
        }

        /* no more as == we can stop spinner_fn. If there's still one (or more)
         * but only for error messages, on its next call spinner_fn will see it
         * and stop itself */
        if (!priv->active_spinners->len && priv->active_spinners_id)
        {
            g_source_remove (priv->active_spinners_id);
            priv->active_spinners_id = 0;
            priv->active_spinners_pulse = 0;
        }
    }

free:
    g_ptr_array_free (arr, TRUE);
    free_set_node_prop_data (data);
}

static void
set_node_prop_timeout (DonnaTask *task, struct set_node_prop_data *data)
{
    DonnaTreeViewPrivate *priv = data->tree->priv;
    GtkTreeModel *model;
    GSList *list;
    guint i;
    GPtrArray *arr;
    struct active_spinners *as;

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

    as = get_as_for_node (data->tree, data->node, NULL, TRUE);
    /* for every column using that property */
    for (i = 0; i < arr->len; ++i)
    {
        GtkTreeViewColumn *column = arr->pdata[i];
        struct as_col *as_col;
        guint j;

        /* does this as already have a spinner for this column? */
        for (j = 0; j < as->as_cols->len; ++j)
        {
            as_col = &g_array_index (as->as_cols, struct as_col, j);
            if (as_col->column == column)
                break;
        }
        if (j >= as->as_cols->len)
        {
            struct as_col as_col_new;

            as_col_new.column = column;
            as_col_new.nb = 1;
            as_col_new.tasks = g_ptr_array_new_full (1, g_object_unref);
            g_array_append_val (as->as_cols, as_col_new);
            as_col = &g_array_index (as->as_cols, struct as_col, j);

#ifndef GTK_IS_JJK
            /* a bug in GTK means that because when the size of renderer is
             * first computed and spinner if not visible, it has a natural
             * size of 0 and therefore even when it becomes visible it isn't
             * actually drawn.
             * This is a hack to workaround this, by enforcing the column to
             * re-compute its size now that we'll have the spinner visible,
             * so it should have a natural size and actually be drawn */
            gtk_tree_view_column_queue_resize (column);
#endif
        }
        else
            ++as_col->nb;

        g_ptr_array_add (as_col->tasks, g_object_ref (task));
    }

#ifdef GTK_IS_JJK
    model = get_model (data->tree);
    /* for every row of this node */
    list = g_hash_table_lookup (priv->hashtable, data->node);
    for ( ; list; list = list->next)
    {
        GtkTreeIter *iter = list->data;
        GtkTreePath *path;

        path = gtk_tree_model_get_path (model, iter);
        gtk_tree_model_row_changed (model, path, iter);
        gtk_tree_path_free (path);
    }
#endif

    if (!priv->active_spinners_id)
        priv->active_spinners_id = g_timeout_add (42,
                (GSourceFunc) spinner_fn, data->tree);

    g_ptr_array_free (arr, TRUE);
}

gboolean
donna_tree_view_set_node_property (DonnaTreeView      *tree,
                                   DonnaNode          *node,
                                   DonnaSharedString  *prop,
                                   const GValue       *value,
                                   GError           **error)
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
        g_set_error (error, DONNA_TREE_VIEW_ERROR, DONNA_TREE_VIEW_ERROR_NOT_FOUND,
                "Treeview '%s': Cannot set property '%s' on node '%s:%s', "
                "the node is not represented in the treeview",
                get_name (priv),
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
        g_propagate_prefixed_error (error, err,
                "Treeview '%s': Cannot set property '%s' on node '%s:%s': ",
                get_name (priv),
                donna_shared_string (prop),
                domain,
                donna_shared_string (ss));
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
            NULL);
    donna_task_set_callback (task,
            (task_callback_fn) set_node_prop_callbak,
            data,
            (GDestroyNotify) free_set_node_prop_data);
    donna_app_run_task (priv->app, task);
    return TRUE;
}

/* mode tree only */
static inline GtkTreeIter *
get_current_root_iter (DonnaTreeView *tree)
{
    DonnaTreeViewPrivate *priv = tree->priv;

    if (priv->location_iter.stamp != 0)
    {
        GtkTreeModel *model = get_model (tree);
        GtkTreeIter iter = ITER_INIT;
        DonnaNode *node;
        GSList *list;

        if (gtk_tree_store_iter_depth (GTK_TREE_STORE (model),
                    &priv->location_iter) > 0)
        {
            gchar *str;

            str = gtk_tree_model_get_string_from_iter (model,
                    &priv->location_iter);
            /* there is at least one ':' since it's not a root */
            *strchr (str, ':') = '\0';
            gtk_tree_model_get_iter_from_string (model, &iter, str);
            g_free (str);
        }
        else
            /* current location is a root */
            iter = priv->location_iter;

        /* get the iter from the hashtable */
        gtk_tree_model_get (model, &iter, DONNA_TREE_COL_NODE, &node, -1);
        list = g_hash_table_lookup (priv->hashtable, node);
        g_object_unref (node);
        for ( ; list; list = list->next)
            if (itereq (&iter, (GtkTreeIter *) list->data))
                return list->data;
    }
    return NULL;
}

/* mode tree only */
static gboolean
is_row_accessible (DonnaTreeView *tree, GtkTreeIter *iter)
{
    DonnaTreeViewPrivate *priv = tree->priv;
    GtkTreeView *treev = GTK_TREE_VIEW (tree);
    GtkTreeModel *model = get_model (tree);
    GtkTreeIter parent = ITER_INIT;
    GtkTreeIter child  = *iter;
    GtkTreePath *path;

    while (gtk_tree_model_iter_parent (model, &parent, &child))
    {
        gboolean is_expanded;

        path = gtk_tree_model_get_path (model, &parent);
        is_expanded = gtk_tree_view_row_expanded (treev, path);
        gtk_tree_path_free (path);
        if (!is_expanded)
            return FALSE;
        /* go up */
        child = parent;
    }
    return TRUE;
}

/* mode tree only */
/* return the best iter for the given node. Iter must exists on tree, and must
 * be expanded unless even_collapsed is TRUE.
 * This is how we get the new current location in DONNA_TREE_SYNC_NODES */
static GtkTreeIter *
get_best_existing_iter_for_node (DonnaTreeView  *tree,
                                 DonnaNode      *node,
                                 gboolean        even_collapsed)
{
    DonnaTreeViewPrivate *priv = tree->priv;
    GtkTreeView *treev = GTK_TREE_VIEW (tree);
    GSList *list;
    GtkTreeModel *model;
    GtkTreeStore *store;
    GtkTreeIter *iter_cur_root;
    GtkTreeIter *iter_vis = NULL;
    GtkTreeIter *iter_non_vis = NULL;
    GdkRectangle rect_visible;

    /* we only want iters on tree */
    list = g_hash_table_lookup (priv->hashtable, node);
    if (!list)
        return NULL;

    treev = GTK_TREE_VIEW (tree);
    model = get_model (tree);
    store = GTK_TREE_STORE (model);

    /* just the one? */
    if (!list->next)
    {
        if (even_collapsed || is_row_accesible (tree, list->data))
            return list->data;
        return NULL;
    }

    iter_cur_root = get_current_root_iter (tree);
    /* get visible area, so we can determine which iters are visible */
    gtk_tree_view_get_visible_rect (treev, &rect_visible);
    gtk_tree_view_convert_tree_to_bin_window_coords (treev, 0, rect_visible.y,
            &rect_visible.x, &rect_visible.y);

    for ( ; list; list = list->next)
    {
        GtkTreeIter *iter = list->data;
        GdkRectangle rect;

        /* not "accessible" w/out expanding, we skip */
        if (!even_collapsed && !is_row_accesible (tree, iter))
            continue;

        /* if in the current location's root branch, it's the one */
        if (iter_cur_root
                && gtk_tree_store_is_ancestor (store, iter_cur_root, iter))
            return iter;

        /* if we haven't found a visible match yet... */
        if (!iter_vis)
        {
            GtkTreePath *path;

            /* determine if it is visible or not */
            path = gtk_tree_model_get_path (model, iter);
            gtk_tree_view_get_background_area (treev, path, NULL, &rect);
            gtk_tree_path_free (path);
            if (rect.y >= rect_visible.y
                    && rect.y + rect.height <= rect_visible.y + rect_visible.height)
                iter_vis = iter;
            else if (!iter_non_vis)
                iter_non_vis = iter;
        }
    }

    return (iter_vis) ? iter_vis : iter_non_vis;
}

/* mode tree only -- node must be in a non-flat domain */
static gboolean
is_node_ancestor (DonnaNode         *node,
                  DonnaNode         *descendant,
                  DonnaProvider     *descendant_provider,
                  DonnaSharedString *descendant_location)
{
    DonnaProvider *provider;
    DonnaSharedString *location;
    size_t len;
    gboolean ret;

    donna_node_get (node, FALSE, "provider", &provider, NULL);
    g_object_unref (provider);
    if (descendant_provider != provider)
        return FALSE;

    /* descandant is in the same domain as node, and we know node's domain isn't
     * flat, so we can assume that if descendant is a child, its location starts
     * with its parent's location and a slash */
    donna_node_get (node, FALSE, "location", &location, NULL);
    len = strlen (donna_shared_string (location));
    ret = strncmp (donna_shared_string (location),
            donna_shared_string (descendant_location), len) == 0
        && donna_shared_string (descendant_location)[len] == '/';
    donna_shared_string_unref (location);
    return ret;
}

/* mode tree only -- node must have its iter ending up under iter_root, and must
 * be in a non-flat domain */
/* get an iter (under iter_root) for the node. If only_expanded we don't want
 * collapsed rows, and if allow_creation we will add new rows to the tree to get
 * the iter we want, else we stop at the closest one we could find.
 * So basically, it should always be TRUE,FALSE or FALSE,TRUE for those 2.
 * This is used to get the new iter for current location in
 * DONNA_TREE_SYNC_NODES_CHILDREN */
static GtkTreeIter *
get_iter_expanding_if_needed (DonnaTreeView *tree,
                              GtkTreeIter   *iter_root,
                              DonnaNode     *node,
                              gboolean       only_expanded,
                              gboolean       allow_creation)
{
    GtkTreeView *treev= GTK_TREE_VIEW (tree);
    DonnaTreeViewPrivate *priv = tree->priv;
    GtkTreeModel *model;
    GtkTreeIter *last_iter = NULL;
    GtkTreeIter *iter;
    DonnaProvider *provider;
    DonnaNode *n;
    DonnaSharedString *location;
    const gchar *loc;
    DonnaSharedString *ss;
    size_t len;
    gchar *s;
    DonnaTask *task;
    const GValue *value;

    model = get_model (tree);
    iter = iter_root;
    donna_node_get (node, FALSE,
            "provider", &provider,
            "location", &location,
            NULL);
    loc = donna_shared_string (location);

    /* get the node for the given iter_root, our starting point */
    gtk_tree_model_get (model, iter, DONNA_TREE_COL_NODE, &n, -1);
    for (;;)
    {
        if (n == node)
        {
            /* this _is_ the iter we're looking for */
            donna_shared_string_unref (location);
            g_object_unref (provider);
            g_object_unref (n);
            if (only_expanded)
                return (is_row_accessible (tree, iter)) ? iter : last_iter;
            else
                return iter;
        }

        /* get the node's location, and obtain the location of the next child */
        donna_node_get (n, FALSE, "location", &ss, NULL);
        len = strlen (donna_shared_string (ss));
        donna_shared_string_unref (ss);
        g_object_unref (n);
        s = strchr (loc + len + 1, '/');
        if (s)
            s = strndup (loc, s - loc - 1);
        else
            s = (gchar *) loc;

        /* get the corresponding node */
        task = donna_provider_get_node_task (provider, (const gchar *) s);
        g_object_ref_sink (task);
        /* FIXME? should this be in a separate thread, and continue in a
         * callback and all that? might not be worth the trouble... */
        donna_task_run (task);
        if (donna_task_get_state (task) != DONNA_TASK_DONE)
        {
            /* TODO */
            return NULL;
        }
        value = donna_task_get_return_value (task);
        n = g_value_dup_object (value);
        g_object_unref (task);

        if (only_expanded)
        {
            /* we only remember this iter as last possible match if expanded */
            if (is_row_accessible (tree, iter))
                last_iter = iter;
        }
        else
            last_iter = iter;

        /* now get the child iter for that node */
        iter = get_child_iter_for_node (tree, iter, n);
        if (!iter)
        {
            if (allow_creation)
            {
                GtkTreeIter i = ITER_INIT;
                GSList *list;

                /* we need to add a new row */
                if (!add_node_to_tree (tree, iter, node, &i))
                {
                    /* TODO */
                    return NULL;
                }

                gtk_tree_store_set (GTK_TREE_STORE (model), iter,
                        DONNA_TREE_COL_EXPAND_STATE,    (priv->is_minitree)
                        ? DONNA_TREE_EXPAND_PARTIAL : DONNA_TREE_EXPAND_UNKNOWN,
                        -1);
                if (!priv->is_minitree)
                {
                    GtkTreePath *path;

                    /* will import children or start a get_children task in a new
                     * thread */
                    path = gtk_tree_model_get_path (model, iter);
                    gtk_tree_view_expand_row (treev, path, FALSE);
                    gtk_tree_path_free (path);
                }

                /* get the iter from the hashtable for the row we added (we
                 * cannot end up return the pointer to a local iter) */
                list = g_hash_table_lookup (priv->hashtable, node);
                for ( ; list; list = list->next)
                    if (itereq (&i, (GtkTreeIter *) list->data))
                    {
                        iter = list->data;
                        break;
                    }
            }
            else
                return last_iter;
        }
    }
}

/* mode tree only */
/* this will get the best iter for new location in
 * DONNA_TREE_SYNC_NODES_CHILDREN */
static GtkTreeIter *
get_best_iter_for_node (DonnaTreeView *tree, DonnaNode *node, GError **error)
{
    GtkTreeView *treev = GTK_TREE_VIEW (tree);
    DonnaTreeViewPrivate *priv = tree->priv;
    GtkTreeModel *model;
    DonnaProvider *provider;
    DonnaProviderFlags flags;
    DonnaSharedString *location;
    GtkTreeIter *iter_cur_root;
    DonnaNode *n;
    GtkTreeIter iter = ITER_INIT;
    GdkRectangle rect_visible;
    GdkRectangle rect;
    GtkTreeIter *iter_non_vis = NULL;

    donna_node_get (node, FALSE, "provider", &provider, NULL);
    flags = donna_provider_get_flags (provider);
    if (G_UNLIKELY (flags & DONNA_PROVIDER_FLAG_INVALID))
    {
        g_set_error (error, DONNA_TREE_VIEW_ERROR, DONNA_TREE_VIEW_ERROR_OTHER,
                "Treeview '%s': Unable to get flags for provider '%s'",
                get_name (priv),
                donna_provider_get_domain (provider));
        g_object_unref (provider);
        return NULL;
    }
    /* w/ flat provider we can't do anything else but rely on existing rows */
    else if (flags & DONNA_PROVIDER_FLAG_FLAT)
    {
        g_object_unref (provider);
        /* TRUE not to ignore non-"accesible" (collapsed) ones */
        return get_best_existing_iter_for_node (tree, node, TRUE);
    }

    model = get_model (tree);
    donna_node_get (node, FALSE, "location", &location, NULL);

    /* try inside the current branch first */
    iter_cur_root = get_current_root_iter (tree);
    if (iter_cur_root)
    {
        gtk_tree_model_get (model, iter_cur_root, DONNA_TREE_COL_NODE, &n, -1);
        if (is_node_ancestor (n, node, provider, location))
        {
            donna_shared_string_unref (location);
            g_object_unref (provider);
            g_object_unref (n);
            return get_iter_expanding_if_needed (tree, iter_cur_root, node,
                    FALSE, TRUE);
        }
    }

    /* get visible area, so we can determine which iters are visible */
    gtk_tree_view_get_visible_rect (treev, &rect_visible);
    gtk_tree_view_convert_tree_to_bin_window_coords (treev,
            0, rect_visible.y, &rect_visible.x, &rect_visible.y);

    /* try all existing tree roots */
    gtk_tree_model_iter_children (model, &iter, NULL);
    do
    {
        /* we've already excluded the current location's branch */
        if (itereq (&iter, iter_cur_root))
            continue;

        gtk_tree_model_get (model, &iter, DONNA_TREE_COL_NODE, &n, -1);
        if (is_node_ancestor (n, node, provider, location))
        {
            GSList *list;
            GtkTreeIter *i;

            /* get the iter from the hashtable for the row we added (we
             * cannot end up return the pointer to a local iter) */
            list = g_hash_table_lookup (priv->hashtable, node);
            for ( ; list; list = list->next)
                if (itereq (&iter, (GtkTreeIter *) list->data))
                {
                    i= list->data;
                    break;
                }

            /* find the closest "accesible" iter (expanded, no creation) */
            i = get_iter_expanding_if_needed (tree, &iter, node, TRUE, FALSE);
            if (i)
            {
                GtkTreePath *path;

                /* determine if it is visible or not */
                path = gtk_tree_model_get_path (model, i);
                gtk_tree_view_get_background_area (treev, path, NULL, &rect);
                gtk_tree_path_free (path);
                if (rect.y >= rect_visible.y
                        && rect.y + rect.height <= rect_visible.y +
                        rect_visible.height)
                    /* it is, this is our match */
                    return get_iter_expanding_if_needed (tree, i, node,
                            FALSE, TRUE);
                else if (!iter_non_vis)
                    /* a fallback in case we don't find one visible */
                    iter_non_vis = i;
            }
        }
    }
    while (gtk_tree_model_iter_next (model, &iter));

    if (iter_non_vis)
        return get_iter_expanding_if_needed (tree, iter_non_vis, node,
                FALSE, TRUE);
    else
        return NULL;
}

gboolean
donna_tree_view_set_location (DonnaTreeView  *tree,
                              DonnaNode      *node,
                              GError        **error)
{
    /* TODO */
}

DonnaNode *
donna_tree_view_get_location (DonnaTreeView      *tree)
{
    g_return_val_if_fail (DONNA_IS_TREE_VIEW (tree), NULL);

    if (tree->priv->location)
        return g_object_ref (tree->priv->location);
    else
        return NULL;
}

static gboolean
query_tooltip_cb (GtkTreeView   *treev,
                  gint           x,
                  gint           y,
                  gboolean       keyboard_mode,
                  GtkTooltip    *tooltip)
{
    DonnaTreeView *tree = DONNA_TREE_VIEW (treev);
    DonnaTreeViewPrivate *priv;
    GtkTreeViewColumn *column;
#ifdef GTK_IS_JJK
    GtkCellRenderer *renderer;
#endif
    GtkTreeModel *_model;
    GtkTreeIter _iter = ITER_INIT;
    gboolean ret = FALSE;

    if (gtk_tree_view_get_tooltip_context (treev, &x, &y, keyboard_mode,
                &_model, NULL, &_iter))
    {
#ifdef GTK_IS_JJK
        if (!gtk_tree_view_is_blank_at_pos_full (treev, x, y, NULL, &column,
                    &renderer, NULL, NULL))
#else
        if (!gtk_tree_view_is_blank_at_pos (treev, x, y, NULL, &column, NULL, NULL))
#endif
        {
            DonnaNode *node;
            DonnaColumnType *ct;
            const gchar *col;
            guint index = 0;

            priv = tree->priv;

            gtk_tree_model_get (_model, &_iter,
                    DONNA_TREE_VIEW_COL_NODE,   &node,
                    -1);
            if (!node)
                return FALSE;

#ifdef GTK_IS_JJK
            if (renderer == int_renderers[INTERNAL_RENDERER_SPINNER])
                return FALSE;
            else if (renderer == int_renderers[INTERNAL_RENDERER_PIXBUF])
            {
                struct active_spinners *as;
                guint i;

                as = get_as_for_node (tree, node, NULL, FALSE);
                if (G_UNLIKELY (!as))
                {
                    g_object_unref (node);
                    return FALSE;
                }

                for (i = 0; i < as->as_cols->len; ++i)
                {
                    struct as_col *as_col;
                    GString *str;

                    as_col = &g_array_index (as->as_cols, struct as_col, i);
                    if (as_col->column != column)
                        continue;

                    str = g_string_new (NULL);
                    for (i = 0; i < as_col->tasks->len; ++i)
                    {
                        DonnaTask *task = as_col->tasks->pdata[i];

                        if (donna_task_get_state (task) == DONNA_TASK_FAILED)
                        {
                            const GError *err;

                            if (str->len > 0)
                                g_string_append_c (str, '\n');

                            err = donna_task_get_error (task);
                            if (err)
                                g_string_append (str, err->message);
                            else
                                g_string_append (str, "Task failed, no error message");
                        }
                    }

                    i = str->len > 0;
                    if (i)
                        gtk_tooltip_set_text (tooltip, str->str);
                    g_string_free (str, TRUE);
                    return (gboolean) i;
                }
                return FALSE;
            }
#endif
            ct  = g_object_get_data (G_OBJECT (column), "column-type");
            col = g_object_get_data (G_OBJECT (column), "column-name");

#ifdef GTK_IS_JJK
            if (renderer)
            {
                const gchar *rend;

                rend = donna_columntype_get_renderers (ct);
                if (rend[1] == '\0')
                    /* only one renderer in this column */
                    index = 1;
                else
                {
                    gchar r;

                    r = GPOINTER_TO_INT (g_object_get_data (G_OBJECT (renderer),
                                "renderer-type"));
                    for (index = 1; *rend && *rend != r; ++index, ++rend)
                        ;
                }
            }
#endif
            ret = donna_columntype_set_tooltip (ct, get_name (priv), col, index,
                    node, tooltip);

            g_object_unref (node);
        }
    }
    return ret;
}

static gboolean
button_press_cb (DonnaTreeView *tree, GdkEventButton *event, gpointer data)
{
    GtkTreeView *treev = GTK_TREE_VIEW (tree);
    DonnaTreeViewPrivate *priv = tree->priv;
    GtkTreeViewColumn *column;
#ifdef GTK_IS_JJK
    GtkCellRenderer *renderer;
#endif
    GtkTreeModel *_model;
    GtkTreeIter _iter = ITER_INIT;
    gint x, y;

    if (event->button != 1 || event->type != GDK_BUTTON_PRESS)
        return FALSE;

    x = (gint) event->x;
    y = (gint) event->y;

    if (gtk_tree_view_get_tooltip_context (treev, &x, &y, 0,
                &_model, NULL, &_iter))
    {
#ifdef GTK_IS_JJK
        if (!gtk_tree_view_is_blank_at_pos_full (treev, x, y, NULL, &column,
                    &renderer, NULL, NULL))
#else
        if (!gtk_tree_view_is_blank_at_pos (treev, x, y, NULL, &column, NULL, NULL))
#endif
        {
            DonnaNode *node;
            struct active_spinners *as;
            guint as_idx;
            guint i;

#ifdef GTK_IS_JJK
            if (renderer != int_renderers[INTERNAL_RENDERER_PIXBUF])
                return FALSE;
#endif

            gtk_tree_model_get (_model, &_iter,
                    DONNA_TREE_VIEW_COL_NODE,   &node,
                    -1);
            if (!node)
                return FALSE;

            as = get_as_for_node (tree, node, &as_idx, FALSE);
            if (G_UNLIKELY (!as))
            {
                g_object_unref (node);
                return FALSE;
            }

            for (i = 0; i < as->as_cols->len; ++i)
            {
                struct as_col *as_col;
                GtkTreeModel *model;
                GSList *list;
                GString *str;
                guint j;

                as_col = &g_array_index (as->as_cols, struct as_col, i);
                if (as_col->column != column)
                    continue;

                str = g_string_new (NULL);
                for (j = 0; j < as_col->tasks->len; )
                {
                    DonnaTask *task = as_col->tasks->pdata[j];

                    if (donna_task_get_state (task) == DONNA_TASK_FAILED)
                    {
                        const GError *err;

                        if (str->len > 0)
                            g_string_append_c (str, '\n');

                        err = donna_task_get_error (task);
                        if (err)
                            g_string_append (str, err->message);
                        else
                            g_string_append (str, "Task failed, no error message");

                        /* this will get the last task in the array to j,
                         * hence to need to move/increment j */
                        g_ptr_array_remove_index_fast (as_col->tasks, j);

                        /* can we remove the as_col? */
                        if (as_col->nb == 0 && as_col->tasks->len == 0)
                        {
                            /* can we remove the whole as? */
                            if (as->as_cols->len == 1)
                                g_ptr_array_remove_index_fast (priv->active_spinners,
                                        as_idx);
                            else
                                g_array_remove_index_fast (as->as_cols, i);
                        }
                    }
                    else
                        /* move to next task */
                        ++j;
                }

                if (str->len > 0)
                    /* FIXME show error */
                    g_info ("Error: %s", str->str);
                g_string_free (str, TRUE);

                /* we can safely assume we found/removed a task, so a refresh
                 * is in order for every row of this node */
                model = get_model (tree);
                list = g_hash_table_lookup (priv->hashtable, node);
                for ( ; list; list = list->next)
                {
                    GtkTreeIter *iter = list->data;
                    GtkTreePath *path;

                    path = gtk_tree_model_get_path (model, iter);
                    gtk_tree_model_row_changed (model, path, iter);
                    gtk_tree_path_free (path);
                }

                /* we've handled it */
                g_object_unref (node);
                return TRUE;
            }
            g_object_unref (node);
        }
    }

    return FALSE;
}

static void
selection_changed_cb (GtkTreeSelection *selection, DonnaTreeView *tree)
{
    DonnaTreeViewPrivate *priv = tree->priv;
    GtkTreeIter   _iter = ITER_INIT;

    if (!is_tree (tree))
        return;

    if (gtk_tree_selection_get_selected (selection, NULL, &_iter))
    {
        GtkTreeModelFilter *filter;
        GtkTreeModel *model;
        GtkTreeStore *store;
        GtkTreeIter iter = ITER_INIT;
        DonnaNode *node;
        enum tree_expand es;

        filter = get_filter (tree);
        model  = gtk_tree_model_filter_get_model (filter);
        store  = GTK_TREE_STORE (model);

        gtk_tree_model_filter_convert_iter_to_child_iter (filter, &iter, &_iter);

        if (priv->location)
        {
            if (itereq (&priv->location_iter, &iter))
                return;

            gtk_tree_model_get (model, &priv->location_iter,
                    DONNA_TREE_COL_EXPAND_STATE,    &es,
                    -1);
            /* if we had cached children (NEVER_FULL) we go back to NEVER */
            if (es == DONNA_TREE_EXPAND_NEVER_FULL)
            {
                GtkTreeIter child = ITER_INIT;

                if (gtk_tree_model_iter_children (model, &child, &priv->location_iter))
                    while (remove_row_from_tree (tree, &child))
                        ;

                /* add a fake row */
                gtk_tree_store_insert_with_values (store,
                        NULL, &priv->location_iter, 0,
                        DONNA_TREE_COL_NODE,    NULL,
                        -1);

                gtk_tree_store_set (store, &priv->location_iter,
                        DONNA_TREE_COL_EXPAND_STATE,    DONNA_TREE_EXPAND_NEVER,
                        -1);
            }
        }
        priv->location_iter = iter;

        gtk_tree_model_get (model, &iter,
                DONNA_TREE_COL_NODE,    &node,
                -1);
        if (priv->location != node)
        {
            if (priv->location)
                g_object_unref (priv->location);
            priv->location = node;
        }
        else if (node)
            g_object_unref (node);
    }
}

GtkWidget *
donna_tree_view_new (DonnaApp           *app,
                     DonnaSharedString  *name)
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

    g_return_val_if_fail (DONNA_IS_APP (app), NULL);
    g_return_val_if_fail (name != NULL, NULL);

    w = g_object_new (DONNA_TYPE_TREE_VIEW, NULL);
    treev = GTK_TREE_VIEW (w);
    gtk_tree_view_set_fixed_height_mode (treev, TRUE);

    /* tooltip */
    g_signal_connect (G_OBJECT (w), "query-tooltip",
            G_CALLBACK (query_tooltip_cb), NULL);
    gtk_widget_set_has_tooltip (w, TRUE);

    /* click */
    gtk_widget_add_events (w, GDK_BUTTON_RELEASE_MASK);
    g_signal_connect (G_OBJECT (w), "button-press-event",
            G_CALLBACK (button_press_cb), NULL);

    tree        = DONNA_TREE_VIEW (w);
    priv        = tree->priv;
    priv->app   = app;
    priv->name  = donna_shared_string_ref (name);

    g_debug ("load_config for new tree '%s'", get_name (priv));
    load_config (tree);

    if (is_tree (tree))
    {
        g_debug ("treeview '%s': setting up as tree", get_name (priv));
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
        g_debug ("treeview '%s': setting up as list", get_name (priv));
        /* store */
        store = gtk_tree_store_new (DONNA_LIST_NB_COLS,
                G_TYPE_OBJECT); /* DONNA_LIST_COL_NODE */
        model = GTK_TREE_MODEL (store);
        /* some stylling */
        gtk_tree_view_set_rules_hint (treev, TRUE);
        gtk_tree_view_set_headers_visible (treev, TRUE);
    }

    g_debug ("treeview '%s': setting up filter & selection", get_name (priv));

    /* we use a filter (to show/hide .files, set Visual Filters, etc) */
    model_filter = gtk_tree_model_filter_new (model, NULL);
    filter = GTK_TREE_MODEL_FILTER (model_filter);
    gtk_tree_model_filter_set_visible_func (filter,
            (GtkTreeModelFilterVisibleFunc) visible_func, tree, NULL);
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
