
#include <gtk/gtk.h>
#include <string.h>             /* strchr() */
#include <stdlib.h>             /* atoi() */
#include "treeview.h"
#include "common.h"
#include "node.h"
#include "task.h"
#include "sharedstring.h"

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
    DONNA_TREE_EXPAND_NEVER = 0,
    DONNA_TREE_EXPAND_WIP,
    DONNA_TREE_EXPAND_PARTIAL,
    DONNA_TREE_EXPAND_FULL
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

struct _DonnaTreeViewPrivate
{
    DonnaConfig         *config;
    const gchar         *name;

    run_task_fn          run_task;
    gpointer             run_task_data;
    GDestroyNotify       run_task_destroy;

    get_arrangement_fn   get_arrangement;
    gpointer             get_arrangement_data;
    GDestroyNotify       get_arrangement_destroy;

    get_column_type_fn   get_ct;
    gpointer             get_ct_data;
    GDestroyNotify       get_ct_destroy;

    /* internal states */
    DonnaSharedString   *arrangement;

    /* List: current location */
    DonnaNode           *location;

    /* "cached" options */
    guint                mode        : 1;
    guint                show_hidden : 1;
    /* mode Tree */
    guint                is_minitree : 1;
    guint                sync_mode   : 2;
};

#define is_tree(tree)   (tree->priv->mode == DONNA_TREE_VIEW_MODE_TREE)

static void     donna_tree_view_row_collapsed       (GtkTreeView    *tree,
                                                     GtkTreeIter    *iter,
                                                     GtkTreePath    *path);

G_DEFINE_TYPE (DonnaTreeView, donna_tree_view, GTK_TYPE_TREE_VIEW);

static void
donna_tree_view_class_init (DonnaTreeViewClass *klass)
{
    GtkTreeViewClass *tv_class;

    tv_class = GTK_TREE_VIEW_CLASS (klass);
    tv_class->row_collapsed = donna_tree_view_row_collapsed;
//    tv_class->test_expand_row = fn;

    g_type_class_add_private (klass, sizeof (DonnaTreeViewPrivate));
}

static void
donna_tree_view_init (DonnaTreeView *tv)
{
    DonnaTreeViewPrivate *priv;

    priv = tv->priv = G_TYPE_INSTANCE_GET_PRIVATE (tv, DONNA_TYPE_TREE_VIEW,
            DonnaTreeViewPrivate);
}

static void
load_config (DonnaTreeView *tree)
{
    DonnaTreeViewPrivate *priv;
    gint val;

    /* we load/cache some options, because usually we can't just get those when
     * needed, but they need to trigger some refresh or something. So we need to
     * listen on the option_{set,removed} signals of the config manager anyways.
     * Might as well save a few function calls... */

    priv = tree->priv;

    if (donna_config_get_uint (priv->config, (guint *) &val,
                "treeviews/%s/mode", priv->name))
        priv->mode = val;
    else
    {
        g_warning ("Unable to find mode for tree '%s'", priv->name);
        /* set default */
        val = priv->mode = DONNA_TREE_VIEW_MODE_LIST;
        donna_config_set_uint (priv->config, (guint) val,
                "treeviews/%s/mode", priv->mode);
    }

    if (donna_config_get_boolean (priv->config, (gboolean *) &val,
                "treeviews/%s/show_hidden", priv->name))
        priv->show_hidden = val;
    else
    {
        /* set default */
        val = priv->show_hidden = TRUE;
        donna_config_set_boolean (priv->config, (gboolean) val,
                "treeviews/%s/show_hidden", priv->name);
    }

    if (is_tree (tree))
    {
        if (donna_config_get_boolean (priv->config, (gboolean *) &val,
                    "treeviews/%s/is_minitree", priv->name))
            priv->is_minitree = val;
        else
        {
            /* set default */
            val = priv->is_minitree = FALSE;
            donna_config_set_boolean (priv->config, (gboolean) val,
                    "treeview/%s/is_minitree", priv->name);
        }
    }
    else
    {
    }
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

static void
donna_tree_view_row_collapsed (GtkTreeView   *tree,
                               GtkTreeIter   *_iter,
                               GtkTreePath   *_path)
{
    /* After row is collapsed, there might still be an horizontal scrollbar,
     * because the column has been enlarged due to a long-ass children, and
     * it hasn't been resized since. So even though there's no need for the
     * scrollbar anymore, it remains there.
     * Since we only have one column, we trigger an autosize to get rid of the
     * horizontal scrollbar (or adjust its size) */
    gtk_tree_view_columns_autosize (tree);
}

static void
rend_fn (GtkTreeViewColumn  *column,
         GtkCellRenderer    *renderer,
         GtkTreeModel       *model,
         GtkTreeIter        *iter,
         gpointer            data)
{
    DonnaNode *node;
    gchar     *name;

    gtk_tree_model_get (model, iter, DONNA_TREE_COL_NODE, &node, -1);
    if (!node)
        return;

    /* FIXME */
    donna_node_get (node, "name", &name, NULL);
    g_object_set (renderer, "text", name, NULL);
    g_free (name);
    g_object_unref (node);
}

static gint
sort_func (GtkTreeModel *model,
           GtkTreeIter  *iter1,
           GtkTreeIter  *iter2,
           gpointer      data)
{
    DonnaTreeView           *tree = DONNA_TREE_VIEW (data);
    DonnaTreeViewPrivate    *priv;
    DonnaNode               *node1;
    DonnaNode               *node2;
    gchar            *name1;
    gchar            *name2;
    gchar            *key1;
    gchar            *key2;
    gint              ret;

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

    priv = tree->priv;

    /* FIXME */
    donna_node_get (node1, "name", &name1, NULL);
    donna_node_get (node2, "name", &name2, NULL);

    g_object_unref (node1);
    g_object_unref (node2);

    return ret;
}

static void
node_has_children_cb (DonnaTask *task, gboolean timeout_called, gpointer data)
{
    DonnaNode *node = (DonnaNode *) data;
    DonnaTaskState state;
    const GValue *value;

    /* TODO: everything */

    g_object_get (G_OBJECT (task), "state", &state, NULL);
    if (state != DONNA_TASK_DONE)
    {
        /* we don't know if the node has children, so we'll keep the fake node
         * in, and switch its expand_state to never; that way the user can ask
         * for expansion, which could simply have the expander removed if it
         * wasn't needed after all... */
        return;
    }

    value = donna_task_get_return_value (task);
    if (!value)
    {
        gboolean has_children;

        /* nothing failed, i.e. has_children has been set/refreshed */
        donna_node_get (node, "has_children", &has_children, &has_children, NULL);
        printf("new h_c=%d\n", has_children);
    }
    else
    {
        /* same as state != DONE */
    }
}

gboolean
donna_tree_view_add_root (DonnaTreeView *tree, DonnaNode *node)
{
    GtkTreeView     *treev;
    GtkTreeStore    *store;
    GtkTreeIter      iter;
    gboolean         has_value;
    gboolean         has_children;

    g_return_val_if_fail (DONNA_IS_TREE_VIEW (tree), FALSE);
    g_return_val_if_fail (DONNA_IS_NODE (node), FALSE);

    treev = GTK_TREE_VIEW (tree);
    store = GTK_TREE_STORE (gtk_tree_model_filter_get_model (
            GTK_TREE_MODEL_FILTER (gtk_tree_view_get_model (treev))));

    gtk_tree_store_insert_with_values (store, &iter, NULL, 0,
            DONNA_TREE_COL_NODE,            node,
            DONNA_TREE_COL_EXPAND_STATE,    DONNA_TREE_EXPAND_NEVER,
            -1);
    /* FIXME */
    donna_node_get (node, "has_children", &has_value, &has_children, NULL);
    if (!has_value)
    {
        DonnaTask *task;

        /* insert a fake node, and get/run a task to determine whether there
         * are children or not. we put a node first so the user can ask for
         * expansion right away (the node will disappear if needed asap) */
        gtk_tree_store_insert_with_values (store, NULL, &iter, 0,
                DONNA_TREE_COL_NODE,            NULL,
                DONNA_TREE_COL_EXPAND_STATE,    DONNA_TREE_EXPAND_WIP,
                -1);
        task = donna_node_refresh (node,
                node_has_children_cb, node, NULL,
                0, NULL, NULL, NULL,
                "has_children", NULL);
        /* FIXME: in new thread */
        g_object_ref_sink (task);
        donna_task_run (task);
        g_object_unref (task);
    }
    else if (has_children)
        /* insert a fake node, because we haven't populated the children yet */
        gtk_tree_store_insert_with_values (store, NULL, &iter, 0,
                DONNA_TREE_COL_NODE,            NULL,
                DONNA_TREE_COL_EXPAND_STATE,    DONNA_TREE_EXPAND_NEVER,
                -1);

    return TRUE;
}

gboolean
donna_tree_view_set_root (DonnaTreeView *tree, DonnaNode *node)
{
    GtkTreeView         *treev;
    GtkTreeModel        *model_filter;
    GtkTreeModel        *model;
    GtkTreeIter          iter;
    GtkTreeSelection    *sel;

    g_return_val_if_fail (DONNA_IS_TREE_VIEW (tree), FALSE);

    if (!node)
        /* FIXME: get node for "fs:/" */
        return FALSE;
    if (!node)
        return FALSE;

    treev = GTK_TREE_VIEW (tree);
    model_filter = gtk_tree_view_get_model (treev);
    model = gtk_tree_model_filter_get_model (GTK_TREE_MODEL_FILTER (model_filter));
    sel = gtk_tree_view_get_selection (treev);

    gtk_tree_store_clear (GTK_TREE_STORE (model));
    if (!donna_tree_view_add_root (tree, node))
        return FALSE;

    if (!gtk_tree_model_get_iter_first (model_filter, &iter))
        return FALSE;
    gtk_tree_selection_select_iter (sel, &iter);

    /* Horizontal scrollbar stuff. See row_collapsed_cb for more */
    gtk_tree_view_columns_autosize (treev);

    return TRUE;
}

static gboolean
load_arrangement (DonnaTreeView *tree, DonnaSharedString *arrangement)
{
    DonnaTreeViewPrivate *priv  = tree->priv;
    GtkTreeView          *treev = GTK_TREE_VIEW (tree);
    GList                *list;
    DonnaSharedString    *ss_columns = NULL;
    gchar                *col;

    if (!priv->get_ct)
    {
        g_warning ("No column type loader defined for treeview '%s'", priv->name);
        return FALSE;
    }

    /* FIXME: reuse loaded columns of the right type */

    /* remove all current columns */
    for (list = gtk_tree_view_get_columns (treev); list; list = list->next)
        gtk_tree_view_remove_column (treev, list->data);
    g_list_free (list);

    /* get new set of columns to load */
    if (donna_config_get_shared_string (priv->config, &ss_columns,
                "%s/columns", donna_shared_string (arrangement)))
        col = (gchar *) donna_shared_string (ss_columns);
    else
    {
        g_warning ("No columns defined in '%s/columns'; using 'name'",
                donna_shared_string (ss_columns));
        col = (gchar *) "name";
    }

    for (;;)
    {
        gchar             *s;
        gchar             *e;
        DonnaSharedString *ss;
        DonnaColumnType   *ct;
        GtkTreeViewColumn *column;
        GtkCellRenderer   *renderer;
        const gchar       *rend;
        gint               index;

        /* FIXME: col is const */
        e = strchr (col, ',');
        if (e)
            *e = '\0';
        s = strchr (col, ':');
        if (s && s < e)
            *s = '\0';
        else
            s = NULL;

        if (!donna_config_get_shared_string (priv->config, &ss,
                    "treeviews/%s/columns/%s/type", priv->name, col))
        {
            if (!donna_config_get_shared_string (priv->config, &ss,
                        "columns/%s/type", col))
            {
                g_warning ("No definition found for column '%s'", col);
                goto next;
            }
        }

        ct = priv->get_ct (donna_shared_string (ss), priv->get_ct_data);
        if (!ct)
        {
            g_warning ("Unable to load column type '%s' for column '%s' in treeview '%s'",
                    donna_shared_string (ss), col, priv->name);
            donna_shared_string_unref (ss);
            goto next;
        }
        donna_shared_string_unref (ss);

        /* create renderer(s) & column */
        column = gtk_tree_view_column_new ();
        /* store the name on it, so we can get it back from e.g. rend_fn */
        g_object_set_data_full (G_OBJECT (column), "column-name",
                g_strdup (col), g_free);
        /* give our ref on the ct to the column */
        g_object_set_data_full (G_OBJECT (column), "column-type",
                ct, g_object_unref);
        /* sizing stuff */
        gtk_tree_view_column_set_sizing (column, GTK_TREE_VIEW_COLUMN_FIXED);
        if (s)
            gtk_tree_view_column_set_fixed_width (column, atoi (s + 1));

        /* load renderers */
        for (index = 1, rend = donna_columntype_get_renderers (ct);
                *rend;
                ++index, ++rend)
        {
            switch (*rend)
            {
                case DONNA_COLUMNTYPE_RENDERER_TEXT:
                    /* FIXME: re-use same renderer for all columns */
                    renderer = gtk_cell_renderer_text_new ();
                    break;
                default:
                    g_warning ("Unknown renderer type '%c' for column '%s' in treeview '%s'",
                            *rend, col, priv->name);
                    continue;
            }
            gtk_tree_view_column_set_cell_data_func (column, renderer,
                    rend_fn, GINT_TO_POINTER (index), NULL);
            gtk_tree_view_column_pack_start (column, renderer, TRUE);
        }

        ss = NULL;
        if (!donna_config_get_shared_string (priv->config, &ss,
                    "treeviews/%s/columns/%s/title", priv->name, col))
            if (!donna_config_get_shared_string (priv->config, &ss,
                        "columns/%s/title", col))
            {
                g_warning ("No title set for column '%s', using its name", col);
                gtk_tree_view_column_set_title (column, col);
            }
        if (ss)
        {
            gtk_tree_view_column_set_title (column, donna_shared_string (ss));
            donna_shared_string_unref (ss);
        }

        gtk_tree_view_append_column (treev, column);

next:
        if (s)
            *s = ':';
        if (e)
            *e = ',';
        else
            break;
        col = e + 1;
    }

    if (ss_columns)
        donna_shared_string_unref (ss_columns);

    return TRUE;
}

static DonnaSharedString *
select_arrangement (DonnaTreeView *tree, DonnaNode *new_location)
{
    DonnaTreeViewPrivate *priv;
    DonnaSharedString    *ss;

    priv = tree->priv;

    if (priv->mode == DONNA_TREE_VIEW_MODE_TREE)
    {
        if (donna_config_has_category (priv->config,
                    "treeviews/%s/arrangement", priv->name))
            ss = donna_shared_string_new_take (
                    g_strdup_printf ("treeviews/%s/arrangement", priv->name));
        else
            ss = donna_shared_string_new_dup ("arrangements/tree");
    }
    else /* DONNA_TREE_VIEW_MODE_LIST */
    {
        /* do we have an arrangement selector? */
        if (priv->get_arrangement && new_location)
            ss = priv->get_arrangement (new_location, priv->get_arrangement_data);
        else
            ss = NULL;

        if (!ss)
        {
            if (donna_config_has_category (priv->config,
                        "treeviews/%s/arrangement", priv->name))
                ss = donna_shared_string_new_take (
                        g_strdup_printf ("treeviews/%s/arrangement", priv->name));
            else
                ss = donna_shared_string_new_dup ("arrangements/list");
        }
    }

    return ss;
}

GtkWidget *
donna_tree_view_new (DonnaConfig *config, const gchar *name)
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

    g_return_val_if_fail (DONNA_IS_CONFIG (config), NULL);
    g_return_val_if_fail (name != NULL, NULL);

    w = g_object_new (DONNA_TYPE_TREE_VIEW, NULL);
    treev = GTK_TREE_VIEW (w);

    tree         = DONNA_TREE_VIEW (w);
    priv         = tree->priv;
    priv->config = config;
    priv->name   = name;

    load_config (tree);

    if (priv->mode == DONNA_TREE_VIEW_MODE_TREE)
    {
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
    else /* DONNA_TREE_VIEW_MODE_LIST */
    {
        /* store */
        store = gtk_tree_store_new (DONNA_LIST_NB_COLS,
                G_TYPE_OBJECT); /* DONNA_LIST_COL_NODE */
        model = GTK_TREE_MODEL (store);
        /* some stylling */
        gtk_tree_view_set_rules_hint (treev, TRUE);
        gtk_tree_view_set_headers_visible (treev, TRUE);
    }

    /* we use a filter (to show/hide .files, set Visual Filters, etc) */
    model_filter = gtk_tree_model_filter_new (model, NULL);
    filter = GTK_TREE_MODEL_FILTER (model_filter);
    gtk_tree_model_filter_set_visible_func (filter, visible_func, treev, NULL);
    /* add to tree */
    gtk_tree_view_set_model (treev, model_filter);

    /* selection mode */
    sel = gtk_tree_view_get_selection (treev);
    gtk_tree_selection_set_mode (sel, (priv->mode == DONNA_TREE_VIEW_MODE_TREE)
            ? GTK_SELECTION_BROWSE : GTK_SELECTION_MULTIPLE);

    /* sort */
    gtk_tree_sortable_set_sort_func (GTK_TREE_SORTABLE (model),
            DONNA_TREE_VIEW_COL_NODE, sort_func, treev, NULL);
    gtk_tree_sortable_set_sort_column_id (GTK_TREE_SORTABLE (model),
            DONNA_TREE_VIEW_COL_NODE, GTK_SORT_ASCENDING);

    return w;
}
