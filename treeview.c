
#define _GNU_SOURCE             /* strchrnul() in string.h */
#include <gtk/gtk.h>
#include <string.h>             /* strchr(), strncmp() */
#include "treeview.h"
#include "treestore.h"
#include "common.h"
#include "provider.h"
#include "node.h"
#include "task.h"
#include "macros.h"
#include "columntype-name.h"    /* DONNA_TYPE_COLUMNTYPE_NAME */

enum
{
    PROP_0,

    PROP_LOCATION,

    NB_PROPS
};

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

enum
{
    SORT_CONTAINER_FIRST = 0,
    SORT_CONTAINER_FIRST_ALWAYS,
    SORT_CONTAINER_MIXED
};

enum
{
    DRAW_NOTHING = 0,
    DRAW_WAIT,
    DRAW_EMPTY
};

struct col_prop
{
    gchar             *prop;
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
    gchar               *name;

    DonnaTreeStore      *store;

    /* so we re-use the same renderer for all columns */
    GtkCellRenderer     *renderers[NB_RENDERERS];

    /* current arrangement */
    gchar               *arrangement;

    /* properties used by our columns */
    GArray              *col_props;

    /* handling of spinners on columns (when setting node properties) */
    GPtrArray           *active_spinners;
    guint                active_spinners_id;
    guint                active_spinners_pulse;

    /* current location */
    DonnaNode           *location;
    /* Tree: iter of current location */
    GtkTreeIter          location_iter;

    /* List: future location (task get_children running) */
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

    /* Tree: list we're synching with */
    DonnaTreeView       *sync_with;
    gulong               sid_sw_location_changed;
    gulong               sid_active_list_changed;

    /* "cached" options */
    guint                mode        : 1;
    guint                node_types  : 3;
    guint                show_hidden : 1;
    guint                sane_arrow  : 1;
    guint                sort_groups : 2;
    /* mode Tree */
    guint                is_minitree : 1;
    guint                sync_mode   : 2;
    /* mode List */
    guint                draw_state  : 2;
};

static GParamSpec *donna_tree_view_props[NB_PROPS] = { NULL, };

/* our internal renderers */
enum
{
    INTERNAL_RENDERER_SPINNER = 0,
    INTERNAL_RENDERER_PIXBUF,
    NB_INTERNAL_RENDERERS
};
static GtkCellRenderer *int_renderers[NB_INTERNAL_RENDERERS] = { NULL, };

/* iters only uses stamp & user_data */
#define itereq(i1, i2)      \
    ((i1)->stamp == (i2)->stamp && (i1)->user_data == (i2)->user_data)

#define watch_iter(tree, iter)  \
    tree->priv->watched_iters = g_slist_prepend (tree->priv->watched_iters, iter)
#define remove_watch_iter(tree, iter)   \
    tree->priv->watched_iters = g_slist_remove (tree->priv->watched_iters, iter)

#define is_tree(tree)       (tree->priv->mode == DONNA_TREE_VIEW_MODE_TREE)


static gboolean add_node_to_tree                        (DonnaTreeView *tree,
                                                         GtkTreeIter   *parent,
                                                         DonnaNode     *node,
                                                         GtkTreeIter   *row);
static GtkTreeIter *get_best_existing_iter_for_node     (DonnaTreeView  *tree,
                                                         DonnaNode      *node,
                                                         gboolean        even_collapsed);
static GtkTreeIter *get_iter_expanding_if_needed        (DonnaTreeView *tree,
                                                         GtkTreeIter   *iter_root,
                                                         DonnaNode     *node,
                                                         gboolean       only_expanded,
                                                         gboolean       allow_creation);
static GtkTreeIter *get_best_iter_for_node              (DonnaTreeView *tree,
                                                         DonnaNode *node,
                                                         GError **error);
static struct active_spinners * get_as_for_node         (DonnaTreeView   *tree,
                                                         DonnaNode       *node,
                                                         guint           *index,
                                                         gboolean         create);
static gboolean scroll_to_current                       (DonnaTreeView *tree);

static void free_col_prop (struct col_prop *cp);
static void free_provider_signals (struct provider_signals *ps);
static void free_active_spinners (struct active_spinners *as);

static gboolean donna_tree_view_test_expand_row     (GtkTreeView    *treev,
                                                     GtkTreeIter    *iter,
                                                     GtkTreePath    *path);
static void     donna_tree_view_row_collapsed       (GtkTreeView    *treev,
                                                     GtkTreeIter    *iter,
                                                     GtkTreePath    *path);
static void     donna_tree_view_get_property        (GObject        *object,
                                                     guint           prop_id,
                                                     GValue         *value,
                                                     GParamSpec     *pspec);
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
    o_class->get_property   = donna_tree_view_get_property;
    o_class->finalize       = donna_tree_view_finalize;

    donna_tree_view_props[PROP_LOCATION] =
        g_param_spec_object ("location", "location",
                "Current location of the treeview",
                DONNA_TYPE_NODE,
                G_PARAM_READABLE);

    g_object_class_install_properties (o_class, NB_PROPS, donna_tree_view_props);

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
    g_free (cp->prop);
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
donna_tree_view_get_property (GObject        *object,
                              guint           prop_id,
                              GValue         *value,
                              GParamSpec     *pspec)
{
    DonnaTreeViewPrivate *priv = DONNA_TREE_VIEW (object)->priv;

    if (prop_id == PROP_LOCATION)
        g_value_set_object (value, priv->location);
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
sync_with_location_changed_cb (GObject       *object,
                               GParamSpec    *pspec,
                               DonnaTreeView *tree)
{
    DonnaTreeViewPrivate *priv = tree->priv;
    GtkTreeSelection *sel;
    GtkTreeIter *iter = NULL;
    DonnaNode *node;

    g_object_get (object, "location", &node, NULL);
    if (node == priv->location)
    {
        g_object_unref (node);
        return;
    }

    switch (priv->sync_mode)
    {
        case DONNA_TREE_SYNC_NODES:
            iter = get_best_existing_iter_for_node (tree, node, FALSE);
            break;

        case DONNA_TREE_SYNC_NODES_CHILDREN:
            if (priv->location)
                iter = get_iter_expanding_if_needed (tree,
                        &priv->location_iter, node, FALSE, TRUE);
            break;

        case DONNA_TREE_SYNC_FULL:
            iter = get_best_iter_for_node (tree, node, NULL);
            break;
    }

    sel = gtk_tree_view_get_selection (GTK_TREE_VIEW (tree));
    if (iter)
    {
        gtk_tree_selection_set_mode (sel, GTK_SELECTION_BROWSE);
        /* we select the new row */
        gtk_tree_selection_select_iter (sel, iter);
        /* we want to scroll to this current row, but we do it in an idle
         * source to make sure any pending drawing has been processed;
         * specifically any expanding that might have been requested */
        g_idle_add ((GSourceFunc) scroll_to_current, tree);
    }
    else
    {
        /* unselect, but allow a new selection to be made (will then switch
         * automatically back to SELECTION_BROWSE) */
        gtk_tree_selection_set_mode (sel, GTK_SELECTION_SINGLE);
        gtk_tree_selection_unselect_all (sel);
    }

    g_object_unref (node);
}

static void
active_list_changed_cb (GObject         *object,
                        GParamSpec      *pspec,
                        DonnaTreeView   *tree)
{
    DonnaTreeViewPrivate *priv = tree->priv;
    DonnaNode *node;

    if (priv->sync_with)
    {
        g_signal_handler_disconnect (priv->sync_with, priv->sid_sw_location_changed);
        g_object_unref (priv->sync_with);
    }
    g_object_get (object, "active-list", &priv->sync_with, NULL);
    priv->sid_sw_location_changed = g_signal_connect (priv->sync_with,
            "notify::location",
            G_CALLBACK (sync_with_location_changed_cb), tree);

    g_object_get (priv->sync_with, "location", &node, NULL);
    if (!node)
        return;

    /* FIXME use a GError */
    donna_tree_view_set_location (tree, node, NULL);
    g_object_unref (node);
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
                "treeviews/%s/mode", priv->name))
        priv->mode = val;
    else
    {
        g_warning ("Treeview '%s': Unable to find mode, defaulting to list",
                priv->name);
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

    if (donna_config_get_uint (config, &val,
                "treeviews/%s/node_types", priv->name))
        priv->node_types = val;
    else
    {
        /* set default */
        val = DONNA_NODE_CONTAINER;
        if (!is_tree (tree))
            val |= DONNA_NODE_ITEM;
        priv->node_types = val;
        donna_config_set_uint (config, val,
                "treeviews/%s/node_types", priv->name);
    }

    if (donna_config_get_uint (config, &val,
                "treeviews/%s/sort_groups", priv->name))
        priv->sort_groups = val;
    else
    {
        /* set default */
        val = SORT_CONTAINER_FIRST;
        priv->sort_groups = val;
        donna_config_set_uint (config, val,
                "treeviews/%s/sort_groups", priv->name);
    }

    if (is_tree (tree))
    {
        gchar *s;

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

        if (donna_config_get_uint (config, &val,
                    "treeviews/%s/sync_mode", priv->name))
            priv->sync_mode = val;
        else
        {
            /* set default */
            val = priv->sync_mode = DONNA_TREE_SYNC_FULL;
            donna_config_set_uint (config, val,
                    "treeviews/%s/sync_mode", priv->name);
        }

        if (donna_config_get_string (config, &s,
                    "treeviews/%s/sync_with", priv->name))
            priv->sync_with = donna_app_get_treeview (priv->app, s);
        else
        {
            /* active list */
            g_object_get (priv->app, "active-list", &priv->sync_with, NULL);
            priv->sid_active_list_changed = g_signal_connect (priv->app,
                    "notify::active-list",
                    G_CALLBACK (active_list_changed_cb), tree);
        }

        if (priv->sync_with)
            priv->sid_sw_location_changed = g_signal_connect (priv->sync_with,
                    "notify::location",
                    G_CALLBACK (sync_with_location_changed_cb), tree);
    }
    else
    {
        if (donna_config_get_boolean (config, (gboolean *) &val,
                    "treeviews/%s/sane_arrow", priv->name))
            priv->sane_arrow = val;
        else
        {
            val = priv->sane_arrow = TRUE;
            donna_config_set_boolean (config, (gboolean) val,
                    "treeviews/%s/sane_arrow", priv->name);
        }
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
    GtkTreeIter      iter;
    gboolean         scroll_to_current;
};

static void
free_node_children_data (struct node_children_data *data)
{
    remove_watch_iter (data->tree, &data->iter);
    g_slice_free (struct node_children_data, data);
}

/* mode tree only */
static void
node_get_children_tree_timeout (DonnaTask                   *task,
                                struct node_children_data   *data)
{
    GtkTreePath *path;

    /* we're slow to get the children, let's just show the fake node ("please
     * wait...") */
    if (!is_watched_iter_valid (data->tree, &data->iter, FALSE))
        return;
    path = gtk_tree_model_get_path (GTK_TREE_MODEL (data->tree->priv->store),
            &data->iter);
    gtk_tree_view_expand_row (GTK_TREE_VIEW (data->tree), path, FALSE);
    gtk_tree_path_free (path);
}

/* similar to gtk_tree_store_remove() this will set iter to next row at that
 * level, or invalid it if it pointer to the last one.
 * Returns TRUE if iter is still valid, else FALSE */
/* FIXME: should be the handler for store's row-deleted */
static gboolean
remove_row_from_tree (DonnaTreeView *tree, GtkTreeIter *iter)
{
    DonnaTreeViewPrivate *priv = tree->priv;
    GtkTreeModel *model = GTK_TREE_MODEL (priv->store);
    DonnaNode *node;
    DonnaProvider *provider;
    guint i;
    GSList *l, *prev = NULL;
    GtkTreeIter parent;
    gboolean ret;

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
    ret = donna_tree_store_remove (priv->store, iter);
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
            donna_tree_store_insert_with_values (priv->store, NULL, &parent, 0,
                    DONNA_TREE_COL_NODE,    NULL,
                    -1);
        }
        else
            es = DONNA_TREE_EXPAND_NONE;
        donna_tree_store_set (priv->store, &parent,
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
    DonnaTreeViewPrivate *priv = tree->priv;
    GtkTreeModel *model;

    if (!is_tree (tree))
        return;

    model = GTK_TREE_MODEL (priv->store);

    if (children->len == 0)
    {
        GtkTreeIter child;

        /* set new expand state */
        donna_tree_store_set (priv->store, iter,
                DONNA_TREE_COL_EXPAND_STATE,    DONNA_TREE_EXPAND_NONE,
                -1);
        if (donna_tree_store_iter_children (priv->store, &child, iter))
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
            GtkTreeIter i;

            donna_tree_store_iter_children (priv->store, &i, iter);
            do
            {
                list = g_slist_prepend (list, gtk_tree_iter_copy (&i));
            } while (donna_tree_store_iter_next (priv->store, &i));
            list = g_slist_reverse (list);
        }
        else
            es = DONNA_TREE_EXPAND_UNKNOWN;

        for (i = 0; i < children->len; ++i)
        {
            GtkTreeIter row;
            DonnaNode *node = children->pdata[i];
            DonnaNodeType node_type;

            /* in case we got children from a node_children signal, and there's
             * more types that we care for */
            donna_node_get (node, FALSE, "node-type", &node_type, NULL);
            if (!(node_type & priv->node_types))
                continue;

            /* shouldn't be able to fail/return FALSE */
            if (!add_node_to_tree (tree, iter, node, &row))
            {
                const gchar *domain;
                gchar *location;

                donna_node_get (node, FALSE,
                        "domain",   &domain,
                        "location", &location,
                        NULL);
                g_critical ("Treeview '%s': failed to add node for '%s:%s'",
                        tree->priv->name,
                        domain,
                        location);
                g_free (location);
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
        donna_tree_store_set (priv->store, iter,
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

/* mode tree only */
static void
node_get_children_tree_cb (DonnaTask                   *task,
                           gboolean                     timeout_called,
                           struct node_children_data   *data)
{
    if (!is_watched_iter_valid (data->tree, &data->iter, TRUE))
        return;

    if (donna_task_get_state (task) != DONNA_TASK_DONE)
    {
        GtkTreeModel      *model;
        GtkTreePath       *path;
        DonnaNode         *node;
        const gchar       *domain;
        gchar             *location;
        const GError      *error;

        /* collapse the node & set it to UNKNOWN (it might have been NEVER
         * before, but we don't know) so if the user tries an expansion again,
         * it is tried again. */
        model = GTK_TREE_MODEL (data->tree->priv->store);
        path = gtk_tree_model_get_path (GTK_TREE_MODEL (data->tree->priv->store),
                &data->iter);
        gtk_tree_view_collapse_row (GTK_TREE_VIEW (data->tree), path);
        gtk_tree_path_free (path);
        donna_tree_store_set (data->tree->priv->store, &data->iter,
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
                data->tree->priv->name,
                domain,
                location,
                (error) ? error->message : "No error message");
        g_free (location);
        g_object_unref (node);

        free_node_children_data (data);
        return;
    }

    set_children (data->tree, &data->iter,
            g_value_get_boxed (donna_task_get_return_value (task)),
            TRUE /* expand row */);

    if (data->scroll_to_current)
        scroll_to_current (data->tree);

    free_node_children_data (data);
}

/* mode tree only */
/* this should only be used for rows that we want expanded and are either
 * NEVER or UNKNOWN -- everything else is either already being done, or can be
 * done already (needing at most just a switch of expand_state (NEVER_FULL)) */
static void
expand_row (DonnaTreeView *tree, GtkTreeIter *iter, gboolean scroll_current)
{
    DonnaTreeViewPrivate *priv = tree->priv;
    GtkTreeModel *model = GTK_TREE_MODEL (priv->store);
    DonnaNode *node;
    DonnaProvider *provider;
    DonnaTask *task;
    struct node_children_data *data;
    GSList *list;

    gtk_tree_model_get (model, iter,
            DONNA_TREE_COL_NODE,    &node,
            -1);
    if (!node)
    {
        /* FIXME */
        donna_error ("Treeview '%s': could not get node from model",
                priv->name);
        return;
    }

    /* is there another tree node for this node? */
    list = g_hash_table_lookup (priv->hashtable, node);
    if (list)
    {
        for ( ; list; list = list->next)
        {
            GtkTreeIter *i = list->data;
            enum tree_expand es;

            /* skip ourself */
            if (itereq (iter, i))
                continue;

            gtk_tree_model_get (model, i,
                    DONNA_TREE_COL_EXPAND_STATE,    &es,
                    -1);
            if (es == DONNA_TREE_EXPAND_FULL
                    || es == DONNA_TREE_EXPAND_NEVER_FULL)
            {
                GtkTreeIter child;
                GtkTreePath *path;

                /* let's import the children */

                if (!donna_tree_store_iter_children (priv->store,
                            &child, i))
                {
                    g_critical ("Treeview '%s': Inconsistency detected",
                            priv->name);
                    continue;
                }

                do
                {
                    DonnaNode *node;

                    gtk_tree_model_get (model, &child,
                            DONNA_TREE_COL_NODE,    &node,
                            -1);
                    add_node_to_tree (tree, iter, node, NULL);
                    g_object_unref (node);
                } while (donna_tree_store_iter_next (priv->store, &child));

                /* update expand state */
                donna_tree_store_set (priv->store, iter,
                        DONNA_TREE_COL_EXPAND_STATE,    DONNA_TREE_EXPAND_FULL,
                        -1);

                /* expand node */
                path = gtk_tree_model_get_path (model, iter);
                gtk_tree_view_expand_row (GTK_TREE_VIEW (tree), path, FALSE);
                gtk_tree_path_free (path);

                if (scroll_current)
                    scroll_to_current (tree);

                return;
            }
        }
    }

    /* can we get them from our sync_with list ? (typical case of expansion of
     * the current location, when it was set from sync_with, i.e. we ignored the
     * node_children signal then because it wasn't yet our current location) */
    if (node == priv->location && priv->sync_with)
    {
        GPtrArray *arr;

        arr = donna_tree_view_get_children (priv->sync_with, priv->node_types);
        if (arr)
        {
            guint i;
            GtkTreePath *path;

            for (i = 0; i < arr->len; ++i)
                add_node_to_tree (tree, iter, arr->pdata[i], NULL);
            g_ptr_array_unref (arr);

            /* update expand state */
            donna_tree_store_set (priv->store, iter,
                    DONNA_TREE_COL_EXPAND_STATE,    DONNA_TREE_EXPAND_FULL,
                    -1);

            /* expand node */
            path = gtk_tree_model_get_path (model, iter);
            gtk_tree_view_expand_row (GTK_TREE_VIEW (tree), path, FALSE);
            gtk_tree_path_free (path);

            if (scroll_current)
                scroll_to_current (tree);

            return;
        }
    }

    donna_node_get (node, FALSE, "provider", &provider, NULL);
    task = donna_provider_get_node_children_task (provider, node,
            priv->node_types);

    data = g_slice_new0 (struct node_children_data);
    data->tree  = tree;
    data->scroll_to_current = scroll_current;
    data->iter = *iter;
    watch_iter (tree, &data->iter);

    /* FIXME: timeout_delay must be an option */
    donna_task_set_timeout (task, 800,
            (task_timeout_fn) node_get_children_tree_timeout,
            data,
            NULL);
    donna_task_set_callback (task,
            (task_callback_fn) node_get_children_tree_cb,
            data,
            (GDestroyNotify) free_node_children_data);

    donna_tree_store_set (priv->store, &data->iter,
            DONNA_TREE_COL_EXPAND_STATE,    DONNA_TREE_EXPAND_WIP,
            -1);

    donna_app_run_task (priv->app, task);
    g_object_unref (node);
    g_object_unref (provider);
}

static gboolean
donna_tree_view_test_expand_row (GtkTreeView    *treev,
                                 GtkTreeIter    *iter,
                                 GtkTreePath    *path)
{
    DonnaTreeView *tree = DONNA_TREE_VIEW (treev);
    DonnaTreeViewPrivate *priv = tree->priv;
    enum tree_expand expand_state;

    if (!is_tree (tree))
        /* no expansion */
        return TRUE;

    gtk_tree_model_get (GTK_TREE_MODEL (priv->store), iter,
            DONNA_TREE_COL_EXPAND_STATE,    &expand_state,
            -1);
    switch (expand_state)
    {
        /* allow expansion, just update expand state */
        case DONNA_TREE_EXPAND_NEVER_FULL:
            donna_tree_store_set (priv->store, iter,
                    DONNA_TREE_COL_EXPAND_STATE,    DONNA_TREE_EXPAND_FULL,
                    -1);
            /* fall through */

        /* allow expansion */
        case DONNA_TREE_EXPAND_WIP:
        case DONNA_TREE_EXPAND_PARTIAL:
        case DONNA_TREE_EXPAND_FULL:
            return FALSE;

        /* refuse expansion, import_children or get_children */
        case DONNA_TREE_EXPAND_UNKNOWN:
        case DONNA_TREE_EXPAND_NEVER:
            /* this will add an idle source import_children, or start a new task
             * get_children */
            expand_row (tree, iter, FALSE);
            return TRUE;

        /* refuse expansion. This case should never happen */
        case DONNA_TREE_EXPAND_NONE:
            g_critical ("Treeview '%s' wanted to expand a node without children",
                    priv->name);
            return TRUE;
    }
    /* should never be reached */
    g_critical ("Treeview '%s': invalid expand state: %d",
            priv->name,
            expand_state);
    return FALSE;
}

static void
donna_tree_view_row_collapsed (GtkTreeView   *treev,
                               GtkTreeIter   *iter,
                               GtkTreePath   *path)
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
visible_func (GtkTreeModel  *model,
              GtkTreeIter   *iter,
              DonnaTreeView *tree)
{
    DonnaNode *node;
    gchar *name;
    gboolean ret;

    if (tree->priv->show_hidden)
        return TRUE;

    gtk_tree_model_get (model, iter, DONNA_TREE_VIEW_COL_NODE, &node, -1);
    if (!node)
        return FALSE;

    donna_node_get (node, FALSE, "name", &name, NULL);
    ret = name[0] != '.';
    g_free (name);
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
                        priv->name);
                goto bail;
            }

            model = GTK_TREE_MODEL (data->tree->priv->store);
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

    model = GTK_TREE_MODEL (priv->store);
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
           GtkTreeModel       *model,
           GtkTreeIter        *iter,
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
        struct active_spinners *as;
        DonnaNode *node;

        if (!priv->active_spinners->len)
        {
            g_object_set (renderer,
                    "visible",  FALSE,
                    NULL);
            return;
        }

        gtk_tree_model_get (model, iter, DONNA_TREE_VIEW_COL_NODE, &node, -1);
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
    gtk_tree_model_get (model, iter, DONNA_TREE_VIEW_COL_NODE, &node, -1);

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

                gtk_tree_model_get (model, iter,
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

                gtk_tree_model_get (model, iter,
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

    if (priv->sort_groups != SORT_CONTAINER_MIXED)
    {
        DonnaNodeType type1, type2;

        donna_node_get (node1, FALSE, "node-type", &type1, NULL);
        donna_node_get (node2, FALSE, "node-type", &type2, NULL);

        if (type1 == DONNA_NODE_CONTAINER)
        {
            if (type2 != DONNA_NODE_CONTAINER)
            {
                if (priv->sort_groups == SORT_CONTAINER_FIRST)
                    ret = -1;
                else /* SORT_CONTAINER_FIRST_ALWAYS */
                {
                    ret = (gtk_tree_view_column_get_sort_order (column)
                            == GTK_SORT_ASCENDING) ? -1 : 1;
                    /* with sane_arrow the sort order on column is reversed */
                    if (priv->sane_arrow)
                        ret *= -1;
                }
                goto done;
            }
        }
        else if (type2 == DONNA_NODE_CONTAINER)
        {
            if (priv->sort_groups == SORT_CONTAINER_FIRST)
                ret = 1;
            else /* SORT_CONTAINER_FIRST_ALWAYS */
            {
                ret = (gtk_tree_view_column_get_sort_order (column)
                        == GTK_SORT_ASCENDING) ? 1 : -1;
                /* with sane_arrow the sort order on column is reversed */
                if (priv->sane_arrow)
                    ret *= -1;
            }
            goto done;
        }
    }

    ct   = g_object_get_data (G_OBJECT (column), "column-type");
    col  = g_object_get_data (G_OBJECT (column), "column-name");

    ret = donna_columntype_node_cmp (ct, priv->name, col, node1, node2);
done:
    g_object_unref (node1);
    g_object_unref (node2);
    return ret;
}

static void
node_has_children_cb (DonnaTask                 *task,
                      gboolean                   timeout_called,
                      struct node_children_data *data)
{
    DonnaTreeStore *store = data->tree->priv->store;
    GtkTreeModel *model = GTK_TREE_MODEL (store);
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
                if (donna_tree_store_iter_children (store, &iter, &data->iter))
                {
                    DonnaNode *node;

                    gtk_tree_model_get (model, &iter,
                            DONNA_TREE_VIEW_COL_NODE,   &node,
                            -1);
                    if (!node)
                        donna_tree_store_remove (store, &iter);
                }
                /* update expand state */
                donna_tree_store_set (store, &data->iter,
                        DONNA_TREE_COL_EXPAND_STATE,    DONNA_TREE_EXPAND_NONE,
                        -1);
            }
            else
            {
                /* fake node already there, we just update the expand state,
                 * unless we're WIP then we'll let get_children set it right
                 * once the children have been added */
                if (es == DONNA_TREE_EXPAND_UNKNOWN)
                    donna_tree_store_set (store, &data->iter,
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
                if (donna_tree_store_iter_children (store, &iter, &data->iter))
                    while (remove_row_from_tree (data->tree, &iter))
                        ;
                /* update expand state */
                donna_tree_store_set (store, &data->iter,
                        DONNA_TREE_COL_EXPAND_STATE, DONNA_TREE_EXPAND_NONE,
                        -1);
            }
            /* else: children and expand state obviously already good */
            break;

        case DONNA_TREE_EXPAND_NONE:
            if (has_children)
            {
                /* add fake node */
                donna_tree_store_insert_with_values (store,
                        NULL, &data->iter, 0,
                        DONNA_TREE_COL_NODE,    NULL,
                        -1);
                /* update expand state */
                donna_tree_store_set (store, &data->iter,
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
        if (streq (name, cp->prop))
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
    model = GTK_TREE_MODEL (priv->store);
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

/* mode tree only */
static gboolean
real_node_children_cb (struct node_children_cb_data *data)
{
    DonnaTreeViewPrivate *priv = data->tree->priv;
    enum tree_expand es;

    if (priv->location != data->node)
        goto free;

    if (!(data->node_types & priv->node_types))
        goto free;

    gtk_tree_model_get (GTK_TREE_MODEL (priv->store), &priv->location_iter,
            DONNA_TREE_COL_EXPAND_STATE,    &es,
            -1);
    if (es == DONNA_TREE_EXPAND_UNKNOWN || es == DONNA_TREE_EXPAND_NEVER
            || es == DONNA_TREE_EXPAND_NONE)
    {
        g_debug ("treeview '%s': pre-loading children for current location (NEVER_FULL)",
                priv->name);
        set_children (data->tree, &priv->location_iter, data->children, FALSE);
    }

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

    model = GTK_TREE_MODEL (priv->store);
    list = g_hash_table_lookup (priv->hashtable, node);
    for ( ; list; list = list->next)
    {
        GtkTreeIter *i = list->data;
        GtkTreeIter  p;

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
    gchar                   *s;
    DonnaTreeViewPrivate    *priv;
    GtkTreeView             *treev;
    GtkTreeModel            *model;
    GtkTreeIter              iter;
    GSList                  *list;
    GSList                  *l;
    DonnaProvider           *provider;
    DonnaNodeType            node_type;
    DonnaTask               *task;
    gboolean                 added;
    guint                    i;

    g_return_val_if_fail (DONNA_IS_TREE_VIEW (tree), FALSE);
    g_return_val_if_fail (DONNA_IS_NODE (node), FALSE);

    treev = GTK_TREE_VIEW (tree);
    priv  = tree->priv;
    model = GTK_TREE_MODEL (priv->store);

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

    donna_node_get (node, FALSE, "domain", &domain, "location", &s, NULL);
    g_debug ("treeview '%s': adding new node %p for '%s:%s'",
            priv->name,
            node,
            domain,
            s);
    g_free (s);

    if (!is_tree (tree))
    {
        /* mode list only */
        donna_tree_store_insert_with_values (priv->store, &iter, parent, 0,
                DONNA_LIST_COL_NODE,    node,
                -1);
        if (iter_row)
            *iter_row = iter;
        /* add it to our hashtable */
        list = g_hash_table_lookup (priv->hashtable, node);
        list = g_slist_prepend (list, gtk_tree_iter_copy (&iter));
        g_hash_table_insert (priv->hashtable, node, list);

        return TRUE;
    }

    /* mode tree only */

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
            donna_tree_store_set (priv->store, &iter,
                    DONNA_TREE_COL_NODE,            node,
                    DONNA_TREE_COL_EXPAND_STATE,    DONNA_TREE_EXPAND_UNKNOWN,
                    -1);
            added = TRUE;
        }
        else
            g_object_unref (n);
    }
    if (!added)
        donna_tree_store_insert_with_values (priv->store, &iter, parent, 0,
                DONNA_TREE_COL_NODE,            node,
                DONNA_TREE_COL_EXPAND_STATE,    DONNA_TREE_EXPAND_UNKNOWN,
                -1);
    if (iter_row)
        *iter_row = iter;
    /* add it to our hashtable */
    list = g_hash_table_lookup (priv->hashtable, node);
    list = g_slist_prepend (list, gtk_tree_iter_copy (&iter));
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
            donna_tree_store_set (priv->store, &iter,
                    DONNA_TREE_COL_EXPAND_STATE,    es,
                    -1);
            if (es == DONNA_TREE_EXPAND_NEVER)
                /* insert a fake node so the user can ask for expansion */
                donna_tree_store_insert_with_values (priv->store, NULL, &iter, 0,
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
            donna_tree_store_set (priv->store, &iter,
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
        data->iter  = iter;
        watch_iter (tree, &data->iter);

        /* insert a fake node so the user can ask for expansion right away (the
         * node will disappear if needed asap) */
        donna_tree_store_insert_with_values (priv->store,
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
        gchar *location;

        /* insert a fake node, so user can try again by asking to expand it */
        donna_tree_store_insert_with_values (priv->store, NULL, &iter, 0,
            DONNA_TREE_COL_NODE,    NULL,
            -1);

        donna_node_get (node, FALSE, "location", &location, NULL);
        g_warning ("Treeview '%s': Unable to create a task to determine if the node '%s:%s' has children",
                priv->name, domain, location);
        g_free (location);
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

/* we don't use gtk_tree_view_column_set_sort_column_id() to handle the sorting
 * because we want conrol, to do things like have a default order (ASC/DESC)
 * based on the type, etc */
static void
column_clicked_cb (GtkTreeViewColumn *column, DonnaTreeView *tree)
{
    DonnaTreeViewPrivate *priv = tree->priv;
    GtkTreeSortable *sortable;
    gboolean is_sorted;
    gint cur_sort_id;
    GtkSortType cur_sort_order;
    gint col_sort_id;
    GtkSortType sort_order;

    sortable = GTK_TREE_SORTABLE (priv->store);
    is_sorted = gtk_tree_sortable_get_sort_column_id (sortable,
            &cur_sort_id, &cur_sort_order);
    col_sort_id = GPOINTER_TO_INT (g_object_get_data (G_OBJECT (column),
                "sort_id"));

    /* new sort order */
    sort_order = (is_sorted && cur_sort_id == col_sort_id)
            /* revert order */
            ? ((cur_sort_order == GTK_SORT_ASCENDING)
                ? GTK_SORT_DESCENDING : GTK_SORT_ASCENDING)
            /* default */
            : GTK_SORT_ASCENDING;
    /* important to set the sort order on column before the sort_id on sortable,
     * since sort_func might use the column's sort_order (when putting container
     * always first) */
    gtk_tree_view_column_set_sort_indicator (column, TRUE);
    gtk_tree_view_column_set_sort_order (column, (priv->sane_arrow)
            ? (sort_order == GTK_SORT_ASCENDING) ? GTK_SORT_DESCENDING : GTK_SORT_ASCENDING
            : sort_order);
    gtk_tree_sortable_set_sort_column_id (sortable, col_sort_id, sort_order);
    /* force refresh */
    gtk_widget_queue_draw (GTK_WIDGET (tree));
}

static void
load_arrangement (DonnaTreeView *tree,
                  const gchar   *arrangement,
                  DonnaNode     *location)
{
    DonnaTreeViewPrivate *priv  = tree->priv;
    DonnaConfig          *config;
    GtkTreeView          *treev = GTK_TREE_VIEW (tree);
    GtkTreeSortable      *sortable;
    GList                *list;
    gchar                *s;
    gchar                *s_columns;
    gchar                *s_sort = NULL;
    gsize                 sort_len;
    gint                  sort_order = SORT_UNKNOWN;
    const gchar          *col;
    GtkTreeViewColumn    *last_column = NULL;
    GtkTreeViewColumn    *expander_column = NULL;
    DonnaColumnType      *ctname;
    gint                  sort_id = 0;

    config = donna_app_get_config (priv->app);
    sortable = GTK_TREE_SORTABLE (priv->store);
    list = gtk_tree_view_get_columns (treev);

    /* get new set of columns to load. They might not come from the current
     * arrangement, because e.g. it might always set the sort order. In that
     * case, we try to get the arrangement:
     * - for tree: we try the tree default, if that doesn't work we use "name"
     * - for list: if we have an arrangement selector, we try the arrangement
     *   for the parent. If we don't have a selector, or there's no parent, we
     *   try the list default, if that doesn't work we use "name" */
    s = g_strdup (arrangement);
    for (;;)
    {
        if (donna_config_get_string (config, &s_columns, "%s/columns", s))
        {
            col = s_columns;
            break;
        }
        else
        {
            if (is_tree (tree))
            {
                if (streq (s, "arrangements/tree"))
                {
                    g_warning ("Treeview '%s': No columns defined in 'arrangements/tree'; using 'name'",
                            priv->name);
                    s_columns = NULL;
                    col = "name";
                    break;
                }
                else
                {
                    g_free (s);
                    s = g_strdup ("arrangements/tree");
                }
            }
            else
            {
                /* FIXME
                 * if (arr/list)
                 *      col=name
                 *      break
                 * else
                 *      if (arr_selector && location=get_parent(location)
                 *          get_arr_for(location)
                 *      else
                 *          arr=arr/list
                 */
                if (streq (s, "arrangements/list"))
                {
                    g_warning ("Treeview '%s': No columns defined in 'arrangements/list'; using 'name'",
                            priv->name);
                    s_columns = NULL;
                    col = "name";
                    break;
                }
                else if (location)
                {
                    g_critical ("TODO");
                }
                else
                {
                    g_free (s);
                    s = g_strdup ("arrangements/list");
                }
            }
        }
    }
    g_free (s);

    /* get sort order (has to come from the current arrangement) */
    if (donna_config_get_string (config, &s_sort, "%s/sort", arrangement))
    {
        sort_len = strlen (s_sort);
        if (sort_len > 2)
        {
            sort_len -= 2;
            if (s_sort[sort_len] == ':')
                sort_order = (s_sort[sort_len + 1] == 'd') ? SORT_DESC : SORT_ASC;
        }
    }

    /* clear list of props we're watching to refresh tree */
    if (priv->col_props->len > 0)
        g_array_set_size (priv->col_props, 0);

    if (!is_tree (tree))
    {
        /* because setting it to NULL means the first visible column will be
         * used. If we don't want an expander to show (and just eat space), we
         * need to add an invisible column and set it as expander column */
        expander_column = gtk_tree_view_column_new ();
        gtk_tree_view_column_set_sizing (expander_column,
                GTK_TREE_VIEW_COLUMN_FIXED);
        gtk_tree_view_append_column (treev, expander_column);
    }
    else
        /* so we can make the first colmun to use it the expander column */
        ctname = donna_app_get_columntype (priv->app, "name");

    for (;;)
    {
        gchar             *ss;
        const gchar       *s;
        const gchar       *e;
        gsize              len;
        gchar              buf[64];
        gchar             *b;
        DonnaColumnType   *ct;
        DonnaColumnType   *col_ct;
        GList             *l;
        GtkTreeViewColumn *column;
        GtkCellRenderer   *renderer;
        const gchar       *rend;
        gint               index;
        GPtrArray         *props;

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

        if (!donna_config_get_string (config, &ss,
                    "treeviews/%s/columns/%s/type", priv->name, b))
        {
            if (!donna_config_get_string (config, &ss,
                        "columns/%s/type", b))
            {
                g_warning ("Treeview '%s': No type defined for column '%s', fallback to its name",
                        priv->name, b);
                ss = NULL;
            }
        }

        ct = donna_app_get_columntype (priv->app, (ss) ? ss : b);
        if (!ct)
        {
            g_warning ("Treeview '%s': Unable to load column-type '%s' for column '%s'",
                    priv->name, (ss) ? ss : b, b);
            g_free (ss);
            goto next;
        }
        g_free (ss);

        /* look for an existing column of that type */
        column = NULL;
        for (l = list; l; l = l->next)
        {
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
            /* to test for expander column */
            col_ct = ct;
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
                                priv->name, *rend, b);
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
            /* clicking header (see column_clicked_cb() for more) */
            g_signal_connect (column, "clicked",
                    G_CALLBACK (column_clicked_cb), tree);
            gtk_tree_view_column_set_clickable (column, TRUE);
            /* add it */
            gtk_tree_view_append_column (treev, column);
        }

        if (!expander_column && col_ct == ctname)
            expander_column = column;

        /* sizing stuff */
        if (s)
        {
            ++s;
            gtk_tree_view_column_set_fixed_width (column, natoi (s, e - s));
        }

        /* set title */
        ss = NULL;
        if (!donna_config_get_string (config, &ss,
                    "treeviews/%s/columns/%s/title", priv->name, b))
            if (!donna_config_get_string (config, &ss,
                        "columns/%s/title", b))
            {
                g_warning ("Treeview '%s': No title set for column '%s', using its name",
                        priv->name, b);
                gtk_tree_view_column_set_title (column, b);
            }
        if (ss)
        {
            gtk_tree_view_column_set_title (column, ss);
            g_free (ss);
        }

        /* props to watch for refresh */
        props = donna_columntype_get_props (ct, priv->name, b);
        if (props)
        {
            guint i;

            for (i = 0; i < props->len; ++i)
            {
                struct col_prop cp;

                cp.prop = g_strdup (props->pdata[i]);
                cp.column = column;
                g_array_append_val (priv->col_props, cp);
            }
            g_ptr_array_unref (props);
        }
        else
            g_critical ("Treeview '%s': column '%s' reports no properties to watch for refresh",
                    priv->name, b);

        /* sort -- (see column_clicked_cb() for more) */
        g_object_set_data (G_OBJECT (column), "sort_id", GINT_TO_POINTER (sort_id));
        gtk_tree_sortable_set_sort_func (sortable, sort_id,
                (GtkTreeIterCompareFunc) sort_func, column, NULL);
        if (s_sort)
        {
            gboolean sorted = FALSE;
            GtkSortType order = GTK_SORT_ASCENDING;

            /* SORT_UNKNOWN means we only had a column name (unlikely) */
            if (sort_order == SORT_UNKNOWN)
            {
                if (streq (s_sort, b))
                    sorted = TRUE;
            }
            else
            {
                /* ss_sort contains "column:o" */
                if (strlen (b) == sort_len && streqn (s_sort, b, sort_len))
                {
                    sorted = TRUE;
                    order = (sort_order == SORT_ASC) ? GTK_SORT_ASCENDING
                            : GTK_SORT_DESCENDING;
                }
            }

            if (sorted)
            {
                /* important to set the sort order on column before the sort_id
                 * on sortable, since sort_func might use the column's
                 * sort_order (when putting container always first) */
                gtk_tree_view_column_set_sort_indicator (column, TRUE);
                gtk_tree_view_column_set_sort_order (column, (priv->sane_arrow)
                        ? ((order == GTK_SORT_ASCENDING)
                            ? GTK_SORT_DESCENDING : GTK_SORT_ASCENDING)
                        : order);
                gtk_tree_sortable_set_sort_column_id (sortable, sort_id, order);

                g_free (s_sort);
                s_sort = NULL;
            }
        }
        ++sort_id;
        /* TODO else default sort order? */

        last_column = column;

next:
        if (b != buf)
            g_free (b);
        if (*e == '\0')
            break;
        col = e + 1;
    }

    /* set expander column */
    gtk_tree_view_set_expander_column (treev, expander_column);

    if (s_sort)
        g_free (s_sort);
    if (s_columns)
        g_free (s_columns);
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
        g_free (priv->arrangement);
    priv->arrangement = g_strdup (arrangement);
    g_object_unref (config);
}

static gchar *
select_arrangement (DonnaTreeView *tree, DonnaNode *location)
{
    DonnaTreeViewPrivate *priv;
    DonnaConfig          *config;
    gchar                *s;

    priv = tree->priv;
    config = donna_app_get_config (priv->app);
    g_debug ("treeview '%s': select arrangement", priv->name);

    if (is_tree (tree))
    {
        if (donna_config_has_category (config,
                    "treeviews/%s/arrangement", priv->name))
            s = g_strdup_printf ("treeviews/%s/arrangement", priv->name);
        else
            s = g_strdup ("arrangements/tree");
    }
    else
    {
        /* do we have an arrangement selector? */
        if (location)
            s = donna_app_get_arrangement (priv->app, location);
        else
            s = NULL;

        if (!s)
        {
            if (donna_config_has_category (config,
                        "treeviews/%s/arrangement", priv->name))
                s = g_strdup_printf ("treeviews/%s/arrangement", priv->name);
            else
                s = g_strdup ("arrangements/list");
        }
    }

    g_debug ("treeview '%s': selected arrangement: %s",
            priv->name,
            (s) ? s : "(none)");
    g_object_unref (config);
    return s;
}

void
donna_tree_view_build_arrangement (DonnaTreeView *tree, gboolean force)
{
    DonnaTreeViewPrivate *priv;
    gchar *s;

    g_return_if_fail (DONNA_IS_TREE_VIEW (tree));

    priv = tree->priv;
    g_debug ("treeview '%s': build arrangement (force=%d)",
            priv->name, force);

    s = select_arrangement (tree, priv->location);
    if (force || !priv->arrangement || !streq (s, priv->arrangement))
        load_arrangement (tree, s, priv->location);
    g_free (s);
}

struct set_node_prop_data
{
    DonnaTreeView   *tree;
    DonnaNode       *node;
    gchar           *prop;
};

static void
free_set_node_prop_data (struct set_node_prop_data *data)
{
    g_free (data->prop);
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
        if (streq (data->prop, cp->prop))
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

            model = GTK_TREE_MODEL (priv->store);
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
        if (streq (data->prop, cp->prop))
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
    model = GTK_TREE_MODEL (priv->store);
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
                                   const gchar        *prop,
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
        gchar *location;

        donna_node_get (node, FALSE,
                "domain",   &domain,
                "location", &location,
                NULL);
        g_set_error (error, DONNA_TREE_VIEW_ERROR, DONNA_TREE_VIEW_ERROR_NOT_FOUND,
                "Treeview '%s': Cannot set property '%s' on node '%s:%s', "
                "the node is not represented in the treeview",
                priv->name,
                prop,
                domain,
                location);
        g_free (location);
        return FALSE;
    }

    task = donna_node_set_property_task (node, prop, value, &err);
    if (!task)
    {
        const gchar *domain;
        gchar *location;

        donna_node_get (node, FALSE,
                "domain",   &domain,
                "location", &location,
                NULL);
        g_propagate_prefixed_error (error, err,
                "Treeview '%s': Cannot set property '%s' on node '%s:%s': ",
                priv->name,
                prop,
                domain,
                location);
        g_free (location);
        g_clear_error (&err);
        return FALSE;
    }

    data = g_new0 (struct set_node_prop_data, 1);
    data->tree = tree;
    /* don't need to take a ref on node for timeout or cb, since task has one */
    data->node = node;
    data->prop = g_strdup (prop);

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
        GtkTreeModel *model = GTK_TREE_MODEL (priv->store);
        GtkTreeIter iter;
        DonnaNode *node;
        GSList *list;

        if (donna_tree_store_iter_depth (priv->store,
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
    GtkTreeModel *model = GTK_TREE_MODEL (priv->store);
    GtkTreeView *treev = GTK_TREE_VIEW (tree);
    GtkTreeIter parent;
    GtkTreeIter child;
    GtkTreePath *path;

    child = *iter;
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
    GtkTreeView *treev;
    GSList *list;
    GtkTreeModel *model;
    GtkTreeIter *iter_cur_root;
    GtkTreeIter *iter_vis = NULL;
    GtkTreeIter *iter_non_vis = NULL;
    GdkRectangle rect_visible;

    /* we only want iters on tree */
    list = g_hash_table_lookup (priv->hashtable, node);
    if (!list)
        return NULL;

    treev = GTK_TREE_VIEW (tree);
    model = GTK_TREE_MODEL (priv->store);

    /* just the one? */
    if (!list->next)
    {
        if (even_collapsed || is_row_accessible (tree, list->data))
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
        if (!even_collapsed && !is_row_accessible (tree, iter))
            continue;

        /* if in the current location's root branch, it's the one */
        if (iter_cur_root
                && donna_tree_store_is_ancestor (priv->store, iter_cur_root, iter))
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
                  const gchar       *descendant_location)
{
    DonnaProvider *provider;
    gchar *location;
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
    len = strlen (location);
    ret = strncmp (location, descendant_location, len) == 0
        /* FIXME root isn't always len==1 */
        && (len == 1 || descendant_location[len] == '/');
    g_free (location);
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
    GtkTreeView *treev = GTK_TREE_VIEW (tree);
    DonnaTreeViewPrivate *priv = tree->priv;
    GtkTreeModel *model;
    GtkTreeIter *last_iter = NULL;
    GtkTreeIter *iter;
    DonnaProvider *provider;
    DonnaNode *n;
    gchar *location;
    size_t len;
    gchar *s;
    gchar *ss;
    DonnaTask *task;
    const GValue *value;

    model = GTK_TREE_MODEL (priv->store);
    iter = iter_root;
    donna_node_get (node, FALSE,
            "provider", &provider,
            "location", &location,
            NULL);

    /* get the node for the given iter_root, our starting point */
    gtk_tree_model_get (model, iter,
            DONNA_TREE_COL_NODE,    &n,
            -1);
    for (;;)
    {
        GtkTreeIter *prev_iter;
        GtkTreePath *path;
        enum tree_expand es;

        if (n == node)
        {
            /* this _is_ the iter we're looking for */
            g_free (location);
            g_object_unref (provider);
            g_object_unref (n);
            if (only_expanded)
                return (is_row_accessible (tree, iter)) ? iter : last_iter;
            else
                return iter;
        }

        /* get the node's location, and obtain the location of the next child */
        donna_node_get (n, FALSE, "location", &ss, NULL);
        len = strlen (ss);
        g_free (ss);
        g_object_unref (n);
        s = strchr (location + len + 1, '/');
        if (s)
            s = strndup (location, s - location);
        else
            s = (gchar *) location;

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
        prev_iter = iter;
        iter = get_child_iter_for_node (tree, prev_iter, n);
        if (!iter)
        {
            if (allow_creation)
            {
                GtkTreeIter i;
                GSList *list;

                /* we need to add a new row */
                if (!add_node_to_tree (tree, prev_iter, n, &i))
                {
                    /* TODO */
                    return NULL;
                }

                /* get the iter from the hashtable for the row we added (we
                 * cannot end up return the pointer to a local iter) */
                list = g_hash_table_lookup (priv->hashtable, n);
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

        /* check if the parent (prev_iter) is expanded */
        path = gtk_tree_model_get_path (model, prev_iter);
        if (!gtk_tree_view_row_expanded (treev, path))
        {
            gtk_tree_model_get (model, prev_iter,
                    DONNA_TREE_COL_EXPAND_STATE,    &es,
                    -1);
            if (es == DONNA_TREE_EXPAND_FULL
                    || es == DONNA_TREE_EXPAND_PARTIAL
                    || es == DONNA_TREE_EXPAND_NEVER_FULL)
                gtk_tree_view_expand_row (treev, path, FALSE);
            else
            {
                donna_tree_store_set (priv->store, prev_iter,
                        DONNA_TREE_COL_EXPAND_STATE,    (priv->is_minitree)
                        ? DONNA_TREE_EXPAND_PARTIAL : DONNA_TREE_EXPAND_UNKNOWN,
                        -1);

                if (priv->is_minitree)
                    gtk_tree_view_expand_row (treev, path, FALSE);
                else
                {
                    /* this will take care of the import/get-children, TRUE to
                     * make sure to scroll to current once children are added */
                    expand_row (tree, prev_iter, TRUE);
                    /* now that the thread is started (or an idle source was
                     * created to import children), we need to trigger it again,
                     * so the row actually gets expanded this time, which we
                     * require to be able to continue adding children &
                     * expanding them */
                    gtk_tree_view_expand_row (treev, path, FALSE);
                }
            }
        }
        gtk_tree_path_free (path);
    }
}

/* mode tree only */
/* this will get the best iter for new location in DONNA_TREE_SYNC_FULL */
static GtkTreeIter *
get_best_iter_for_node (DonnaTreeView *tree, DonnaNode *node, GError **error)
{
    GtkTreeView *treev = GTK_TREE_VIEW (tree);
    DonnaTreeViewPrivate *priv = tree->priv;
    GtkTreeModel *model;
    DonnaProvider *provider;
    DonnaProviderFlags flags;
    gchar *location;
    GtkTreeIter *iter_cur_root;
    DonnaNode *n;
    GtkTreeIter iter;
    GdkRectangle rect_visible;
    GdkRectangle rect;
    GtkTreeIter *iter_non_vis = NULL;

    donna_node_get (node, FALSE, "provider", &provider, NULL);
    flags = donna_provider_get_flags (provider);
    if (G_UNLIKELY (flags & DONNA_PROVIDER_FLAG_INVALID))
    {
        g_set_error (error, DONNA_TREE_VIEW_ERROR, DONNA_TREE_VIEW_ERROR_OTHER,
                "Treeview '%s': Unable to get flags for provider '%s'",
                priv->name,
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

    model  = GTK_TREE_MODEL (priv->store);
    donna_node_get (node, FALSE, "location", &location, NULL);

    /* try inside the current branch first */
    iter_cur_root = get_current_root_iter (tree);
    if (iter_cur_root)
    {
        gtk_tree_model_get (model, iter_cur_root, DONNA_TREE_COL_NODE, &n, -1);
        if (is_node_ancestor (n, node, provider, location))
        {
            g_free (location);
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
    if (!gtk_tree_model_iter_children (model, &iter, NULL))
    {
        DonnaTask *task;
        const GValue *value;
        gchar *s;
        GSList *list;
        GtkTreeIter *i;

        /* the tree is empty, we need to add the first root */
        s = strchr (location, '/');
        if (s)
            *++s = '\0';

        task = donna_provider_get_node_task (provider, location);
        g_object_ref_sink (task);
        donna_task_run (task);
        if (donna_task_get_state (task) != DONNA_TASK_DONE)
        {
            g_object_unref (task);
            g_free (location);
            g_object_unref (provider);
            /* FIXME set error */
            return NULL;
        }
        value = donna_task_get_return_value (task);
        n = g_value_dup_object (value);
        g_object_unref (task);
        g_free (location);
        g_object_unref (provider);

        add_node_to_tree (tree, NULL, n, &iter);
        /* get the iter from the hashtable for the row we added (we
         * cannot end up return the pointer to a local iter) */
        list = g_hash_table_lookup (priv->hashtable, n);
        for ( ; list; list = list->next)
            if (itereq (&iter, (GtkTreeIter *) list->data))
            {
                i = list->data;
                break;
            }

        g_object_unref (n);
        return get_iter_expanding_if_needed (tree, i, node,
                FALSE, TRUE);
    }
    do
    {
        /* we've already excluded the current location's branch */
        if (iter_cur_root && itereq (&iter, iter_cur_root))
            continue;

        gtk_tree_model_get (model, &iter, DONNA_TREE_COL_NODE, &n, -1);
        if (is_node_ancestor (n, node, provider, location))
        {
            GSList *list;
            GtkTreeIter *i;

            /* get the iter from the hashtable (we cannot end up return the
             * pointer to a local iter) */
            list = g_hash_table_lookup (priv->hashtable, n);
            for ( ; list; list = list->next)
                if (itereq (&iter, (GtkTreeIter *) list->data))
                {
                    i = list->data;
                    break;
                }

            /* find the closest "accessible" iter (only expanded, no creation) */
            i = get_iter_expanding_if_needed (tree, i, node, TRUE, FALSE);
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
                {
                    /* it is, this is our match */
                    g_free (location);
                    g_object_unref (provider);
                    return get_iter_expanding_if_needed (tree, i, node,
                            FALSE, TRUE);
                }
                else if (!iter_non_vis)
                    /* a fallback in case we don't find one visible */
                    iter_non_vis = i;
            }
        }
    }
    while (gtk_tree_model_iter_next (model, &iter));

    g_free (location);
    g_object_unref (provider);

    if (iter_non_vis)
        return get_iter_expanding_if_needed (tree, iter_non_vis, node,
                FALSE, TRUE);
    else
        return NULL;
}

/* mode tree only */
static gboolean
scroll_to_current (DonnaTreeView *tree)
{
    GtkTreeView *treev = GTK_TREE_VIEW (tree);
    GtkTreeSelection *sel;
    GtkTreeModel *model;
    GtkTreeIter iter;
    GtkTreePath *path;
    GdkRectangle rect_visible, rect;

    sel = gtk_tree_view_get_selection (treev);
    if (!gtk_tree_selection_get_selected (sel, &model, &iter))
        return FALSE;

    /* get visible area, so we can determine if it is already visible */
    gtk_tree_view_get_visible_rect (treev, &rect_visible);
    gtk_tree_view_convert_tree_to_bin_window_coords (treev,
            0, rect_visible.y, &rect_visible.x, &rect_visible.y);

    path = gtk_tree_model_get_path (model, &iter);
    gtk_tree_view_get_background_area (treev, path, NULL, &rect);
    if (!(rect.y >= rect_visible.y
            && rect.y + rect.height <= rect_visible.y +
            rect_visible.height))
        /* only scroll if not visible */
        gtk_tree_view_scroll_to_cell (treev, path, NULL, TRUE, 0.5, 0.0);
    gtk_tree_path_free (path);

    return FALSE;
}

struct node_get_children_list_data
{
    DonnaTreeView *tree;
    DonnaNode     *node;
};

static inline void
free_node_get_children_list_data (struct node_get_children_list_data *data)
{
    g_object_unref (data->node);
    g_slice_free (struct node_get_children_list_data, data);
}

/* mode list only */
static void
node_get_children_list_timeout (DonnaTask *task, DonnaTreeView *tree)
{
    /* clear the list */
    donna_tree_store_clear (tree->priv->store);
    /* and show the "please wait" message */
    tree->priv->draw_state = DRAW_WAIT;
    gtk_widget_queue_draw (GTK_WIDGET (tree));
}

/* mode list only */
static void
node_get_children_list_cb (DonnaTask                            *task,
                           gboolean                              timeout_called,
                           struct node_get_children_list_data   *data)
{
    DonnaTreeViewPrivate *priv = data->tree->priv;
    const GValue *value;
    GPtrArray *arr;
    guint i;

    if (donna_task_get_state (task) != DONNA_TASK_DONE)
    {
        GtkTreeModel      *model;
        GtkTreePath       *path;
        DonnaNode         *node;
        const gchar       *domain;
        gchar             *location;
        const GError      *error;

        donna_node_get (data->node, FALSE,
                "domain",   &domain,
                "location", &location,
                NULL);
        error = donna_task_get_error (task);
        /* FIXME */
        donna_error ("Treeview '%s': Failed to get children for node '%s:%s': %s",
                data->tree->priv->name,
                domain,
                location,
                (error) ? error->message : "No error message");
        g_free (location);

        goto free;
    }

    /* clear the list */
    donna_tree_store_clear (priv->store);

    value = donna_task_get_return_value (task);
    arr = g_value_get_boxed (value);
    if (arr->len > 0)
        for (i = 0; i < arr->len; ++i)
            add_node_to_tree (data->tree, NULL, arr->pdata[i], NULL);
    else
    {
        /* show the "location empty" message */
        priv->draw_state = DRAW_EMPTY;
        gtk_widget_queue_draw (GTK_WIDGET (data->tree));
    }

    /* update current location */
    if (priv->location)
        g_object_unref (priv->location);
    priv->location = g_object_ref (data->node);

    /* emit signal */
    g_object_notify_by_pspec (G_OBJECT (data->tree),
            donna_tree_view_props[PROP_LOCATION]);

free:
    free_node_get_children_list_data (data);
}

gboolean
donna_tree_view_set_location (DonnaTreeView  *tree,
                              DonnaNode      *node,
                              GError        **error)
{
    DonnaTreeViewPrivate *priv;
    GSList *list;

    g_return_val_if_fail (DONNA_IS_TREE_VIEW (tree), FALSE);
    g_return_val_if_fail (DONNA_IS_NODE (node), FALSE);

    priv = tree->priv;

    if (is_tree (tree))
    {
        GtkTreeIter *iter;

        iter = get_best_iter_for_node (tree, node, error);
        if (iter)
        {
            GtkTreeSelection *sel;

            /* we select the new row */
            sel = gtk_tree_view_get_selection (GTK_TREE_VIEW (tree));
            gtk_tree_selection_select_iter (sel, iter);
            /* we want to scroll to this current row, but we do it in an idle
             * source to make sure any pending drawing has been processed;
             * specifically any expanding that might have been requested */
            g_idle_add ((GSourceFunc) scroll_to_current, tree);
        }

        return !!iter;
    }
    else
    {
        struct node_get_children_list_data *data;
        DonnaNodeType node_type;
        DonnaProvider *provider;
        DonnaTask *task;

        donna_node_get (node, FALSE,
                "node-type", &node_type,
                "provider",  &provider,
                NULL);
        /* we can only show content of container */
        if (node_type != DONNA_NODE_CONTAINER)
        {
            /* FIXME set error */
            g_object_unref (provider);
            return FALSE;
        }

        data = g_slice_new0 (struct node_get_children_list_data);
        data->tree = tree;
        data->node = g_object_ref (node);

        task = donna_provider_get_node_children_task (provider, node,
                priv->node_types);
        donna_task_set_timeout (task, 800, /* FIXME */
                (task_timeout_fn) node_get_children_list_timeout,
                tree,
                NULL);
        donna_task_set_callback (task,
                (task_callback_fn) node_get_children_list_cb,
                data,
                (GDestroyNotify) free_node_get_children_list_data);
        donna_app_run_task (priv->app, task);

        g_object_unref (provider);
        return TRUE;
    }
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

GPtrArray *
donna_tree_view_get_children (DonnaTreeView      *tree,
                              DonnaNodeType       node_types)
{
    DonnaTreeViewPrivate *priv;
    GList *list;
    GPtrArray *arr;

    g_return_val_if_fail (DONNA_IS_TREE_VIEW (tree), NULL);
    g_return_val_if_fail (!is_tree (tree), NULL);

    if (!(node_types & priv->node_types))
        return NULL;

    priv = tree->priv;

    /* get list of nodes we have in tree */
    list = g_hash_table_get_keys (priv->hashtable);
    /* create an array that could hold them all */
    arr = g_ptr_array_new_full (g_hash_table_size (priv->hashtable),
            g_object_unref);
    /* fill array based on requested node_types */
    for ( ; list; list = list->next)
    {
        DonnaNodeType type;

        donna_node_get (list->data, FALSE, "node-type", &type, NULL);
        if (type & node_types)
            g_ptr_array_add (arr, g_object_ref (list->data));
    }
    g_list_free (list);

    return arr;
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
    GtkTreeModel *model;
    GtkTreeIter iter;
    gboolean ret = FALSE;

    if (gtk_tree_view_get_tooltip_context (treev, &x, &y, keyboard_mode,
                &model, NULL, &iter))
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

            gtk_tree_model_get (model, &iter,
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
            ret = donna_columntype_set_tooltip (ct, priv->name, col, index,
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
    GtkTreeModel *model;
    GtkTreeIter iter;
    gint x, y;

    if (event->button != 1 || event->type != GDK_BUTTON_PRESS)
        return FALSE;

    x = (gint) event->x;
    y = (gint) event->y;

    if (gtk_tree_view_get_tooltip_context (treev, &x, &y, 0,
                &model, NULL, &iter))
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

            gtk_tree_model_get (model, &iter,
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
    GtkTreeIter iter;

    if (!is_tree (tree))
        return;

    if (gtk_tree_selection_get_selected (selection, NULL, &iter))
    {
        GtkTreeModelFilter *filter;
        GtkTreeModel *model;
        DonnaNode *node;
        enum tree_expand es;

        /* might have been to SELECTION_SINGLE if there was no selection, due to
         * unsync with the list (or minitree mode) */
        gtk_tree_selection_set_mode (selection, GTK_SELECTION_BROWSE);

        model = GTK_TREE_MODEL (priv->store);

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
                GtkTreeIter child;

                if (donna_tree_store_iter_children (priv->store, &child,
                            &priv->location_iter))
                    while (remove_row_from_tree (tree, &child))
                        ;

                /* add a fake row */
                donna_tree_store_insert_with_values (priv->store,
                        NULL, &priv->location_iter, 0,
                        DONNA_TREE_COL_NODE,    NULL,
                        -1);

                donna_tree_store_set (priv->store, &priv->location_iter,
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

/* mode list only */
static gboolean
widget_draw_cb (GtkWidget *w, cairo_t *cr, DonnaTreeView *tree)
{
    DonnaTreeViewPrivate *priv = tree->priv;
    GtkTreeView *treev = GTK_TREE_VIEW (w);
    gint x, y, width;
    GtkStyleContext *context;
    PangoLayout *layout;

    if (is_tree (tree) || priv->draw_state == DRAW_NOTHING)
        return FALSE;

    gtk_tree_view_convert_tree_to_widget_coords (treev, 0, 0, &x, &y);
    width = gtk_widget_get_allocated_width (w);

    context = gtk_widget_get_style_context (w);
    if (priv->draw_state == DRAW_EMPTY)
    {
        gtk_style_context_save (context);
        gtk_style_context_set_state (context, GTK_STATE_FLAG_INSENSITIVE);
    }

    layout = gtk_widget_create_pango_layout (w,
            (priv->draw_state == DRAW_WAIT)
            ? "Please wait..." : "(Location is empty)");
    pango_layout_set_width (layout, width * PANGO_SCALE);
    pango_layout_set_alignment (layout, PANGO_ALIGN_CENTER);
    gtk_render_layout (context, cr, x, y, layout);

    if (priv->draw_state == DRAW_EMPTY)
        gtk_style_context_restore (context);
    g_object_unref (layout);
    return FALSE;
}

GtkWidget *
donna_tree_view_new (DonnaApp    *app,
                     const gchar *name)
{
    DonnaTreeViewPrivate *priv;
    DonnaTreeView        *tree;
    GtkWidget            *w;
    GtkTreeView          *treev;
    GtkTreeModel         *model;
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
    priv->name  = g_strdup (name);

    g_debug ("load_config for new tree '%s'", priv->name);
    load_config (tree);

    if (is_tree (tree))
    {
        g_debug ("treeview '%s': setting up as tree", priv->name);
        /* store */
        priv->store = donna_tree_store_new (DONNA_TREE_NB_COLS,
                G_TYPE_OBJECT,  /* DONNA_TREE_COL_NODE */
                G_TYPE_INT,     /* DONNA_TREE_COL_EXPAND_STATE */
                G_TYPE_STRING,  /* DONNA_TREE_COL_NAME */
                G_TYPE_OBJECT,  /* DONNA_TREE_COL_ICON */
                G_TYPE_STRING,  /* DONNA_TREE_COL_BOX */
                G_TYPE_STRING); /* DONNA_TREE_COL_HIGHLIGHT */
        model = GTK_TREE_MODEL (priv->store);
        /* some stylling */
        gtk_tree_view_set_enable_tree_lines (treev, TRUE);
        gtk_tree_view_set_rules_hint (treev, FALSE);
        gtk_tree_view_set_headers_visible (treev, FALSE);
    }
    else
    {
        g_debug ("treeview '%s': setting up as list", priv->name);
        /* store */
        priv->store = donna_tree_store_new (DONNA_LIST_NB_COLS,
                G_TYPE_OBJECT); /* DONNA_LIST_COL_NODE */
        model = GTK_TREE_MODEL (priv->store);
        /* some stylling */
        gtk_tree_view_set_rules_hint (treev, TRUE);
        gtk_tree_view_set_headers_visible (treev, TRUE);
        /* connect to draw for "please wait"/"location empty" messages */
        g_signal_connect (G_OBJECT (tree), "draw",
                G_CALLBACK (widget_draw_cb), tree);
    }

    g_debug ("treeview '%s': setting up filter & selection", priv->name);

    /* to show/hide .files, set Visual Filters, etc */
    donna_tree_store_set_visible_func (priv->store,
            (store_visible_fn) visible_func, tree, NULL);
    /* add to tree */
    gtk_tree_view_set_model (treev, model);

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
