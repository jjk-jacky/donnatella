
#include <gtk/gtk.h>
#include "treeview.h"
#include "common.h"
#include "node.h"
#include "task.h"

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
#define DONNA_TREEVIEW_COL_NODE     0

typedef enum
{
    DONNA_TREEVIEW_EXPAND_NEVER = 0,
    DONNA_TREEVIEW_EXPAND_WIP,
    DONNA_TREEVIEW_EXPAND_PARTIAL,
    DONNA_TREEVIEW_EXPAND_FULL
} DonnaTreeViewExpand;

struct _DonnaTreeViewPrivate
{
    DonnaTreeViewMode   mode;

    /* both modes */
    /* show hidden places (.folders) */
    guint show_hidden : 1;

    /* mode Tree */
    /* MiniTree */
    guint is_minitree : 1;
    /* DonnaTreeViewSync */
    guint sync : 2;

    /* mode List */
};

#define is_tree(tree)   (tree->priv->mode == DONNA_TREEVIEW_MODE_TREE)

static void     donna_treeview_row_collapsed        (GtkTreeView    *tree,
                                                     GtkTreeIter    *iter,
                                                     GtkTreePath    *path);

G_DEFINE_TYPE (DonnaTreeView, donna_treeview, GTK_TYPE_TREE_VIEW);

static void
donna_treeview_class_init (DonnaTreeViewClass *klass)
{
    GtkTreeViewClass *tv_class;

    tv_class = GTK_TREE_VIEW_CLASS (klass);
    tv_class->row_collapsed = donna_treeview_row_collapsed;
//    tv_class->test_expand_row = fn;

    g_type_class_add_private (klass, sizeof (DonnaTreeViewPrivate));
}

static void
donna_treeview_init (DonnaTreeView *tv)
{
    DonnaTreeViewPrivate *priv;

    priv = tv->priv = G_TYPE_INSTANCE_GET_PRIVATE (tv, DONNA_TYPE_TREEVIEW,
            DonnaTreeViewPrivate);
}

static gboolean
visible_func (GtkTreeModel  *_model,
              GtkTreeIter   *_iter,
              gpointer       data)
{
    DonnaTreeViewPrivate *priv;
    DonnaNode *node;
    gchar *name;
    gboolean ret;

    priv = DONNA_TREEVIEW (data)->priv;
    if (priv->show_hidden)
        return TRUE;

    gtk_tree_model_get (_model, _iter, DONNA_TREEVIEW_COL_NODE, &node, -1);
    if (!node)
        return FALSE;

    donna_node_get (node, FALSE, "name", &name, NULL);
    ret = *name != '.';
    g_free (name);
    g_object_unref (node);

    return ret;
}

static void
donna_tree_row_collapsed (GtkTreeView   *tree,
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

    donna_node_get (node, "name", &name, NULL);
    g_object_set (renderer, "text", name, NULL);
    g_free (name);
    g_object_unref (node);
}

#define COLLATION_SENTINEL  "\1\1\1"

static gchar *
utf8_collate_key (const gchar   *str,
                  gssize         len,
                  gboolean       dot_first,
                  gboolean       special_first,
                  gboolean       natural_order)
{
    GString *result;
    GString *append;
    const gchar *p;
    const gchar *prev;
    const gchar *end;
    gchar *collate_key;
    gint digits;
    gint leading_zeros;

    if (len < 0)
        len = strlen (str);

    result = g_string_sized_new (len * 2);
    append = g_string_sized_new (0);

    end = str + len;
    p = str;

    if (special_first)
    {
        const gchar *s = str;
        gboolean prefix = FALSE;

        while ((s = g_utf8_find_next_char (s, end)))
        {
            gunichar c;

            c = g_utf8_get_char (s);
            if (!g_unichar_isalnum (c))
            {
                if (!prefix)
                    prefix = TRUE;
            }
            else
            {
                if (prefix)
                {
                    /* adding the string itself and not a collate_key
                     * so that ! comes before - */
                    g_string_append_len (result, str, s - str);
                    g_string_append (result, COLLATION_SENTINEL "\1");
                    p += s - str;
                }
                break;
            }
        }
    }

    /* No need to use utf8 functions, since we're only looking for ascii chars */
    for (prev = p; p < end; ++p)
    {
        switch (*p)
        {
            case '.':
                if (!dot_first && p == str)
                    break;

                if (prev != p)
                {
                    collate_key = g_utf8_collate_key (prev, p - prev);
                    g_string_append (result, collate_key);
                    g_free (collate_key);
                }

                g_string_append (result, COLLATION_SENTINEL "\1");

                /* skip the dot */
                prev = p + 1;
                break;

            case '0':
            case '1':
            case '2':
            case '3':
            case '4':
            case '5':
            case '6':
            case '7':
            case '8':
            case '9':
                if (!natural_order)
                    break;

                if (prev != p)
                {
                    collate_key = g_utf8_collate_key (prev, p - prev);
                    g_string_append (result, collate_key);
                    g_free (collate_key);
                }

                g_string_append (result, COLLATION_SENTINEL "\2");

                prev = p;

                /* write d-1 colons */
                if (*p == '0')
                {
                    leading_zeros = 1;
                    digits = 0;
                }
                else
                {
                    leading_zeros = 0;
                    digits = 1;
                }

                while (++p < end)
                {
                    if (*p == '0' && !digits)
                        ++leading_zeros;
                    else if (g_ascii_isdigit (*p))
                        ++digits;
                    else
                    {
                        /* count an all-zero sequence as one digit plus leading
                         * zeros */
                        if (!digits)
                        {
                            ++digits;
                            --leading_zeros;
                        }
                        break;
                    }
                }

                while (digits > 1)
                {
                    g_string_append_c (result, ':');
                    --digits;
                }

                if (leading_zeros > 0)
                {
                    g_string_append_c (append, (char) leading_zeros);
                    prev += leading_zeros;
                }

                /* write the number itself */
                g_string_append_len (result, prev, p - prev);

                prev = p;
                --p; /* go one step back to avoid disturbing outer loop */
                break;

            default:
                /* other characters just accumulate */
                break;
        }
    }

    if (prev != p)
    {
        collate_key = g_utf8_collate_key (prev, p - prev);
        g_string_append (result, collate_key);
        g_free (collate_key);
    }

    g_string_append (result, append->str);
    g_string_free (append, TRUE);

    return g_string_free (result, FALSE);
}

static gint
sort_func (GtkTreeModel *model,
           GtkTreeIter  *iter1,
           GtkTreeIter  *iter2,
           gpointer      data)
{
    DonnaTree        *tree = DONNA_TREE (data);
    DonnaTreePrivate *priv;
    DonnaNode        *node1;
    DonnaNode        *node2;
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

    donna_node_get (node1, "name", &name1, NULL);
    donna_node_get (node2, "name", &name2, NULL);

    key1 = utf8_collate_key (name1, -1,
            priv->sort_dot_first,
            priv->sort_special_first,
            priv->sort_natural_order);
    key2 = utf8_collate_key (name2, -1,
            priv->sort_dot_first,
            priv->sort_special_first,
            priv->sort_natural_order);
    ret = strcmp (key1, key2);

    g_free (key1);
    g_free (key1);
    g_free (name1);
    g_free (name2);
    g_object_unref (node1);
    g_object_unref (node2);

    return ret;
}

gboolean
donna_tree_set_show_hidden (DonnaTree *tree, gboolean show_hidden)
{
    DonnaTreePrivate *priv;

    g_return_val_if_fail (DONNA_IS_TREE (tree), FALSE);

    priv = tree->priv;
    if (priv->show_hidden == show_hidden)
        return TRUE;
    priv->show_hidden = show_hidden;
    gtk_tree_model_filter_refilter (GTK_TREE_MODEL_FILTER (
                gtk_tree_view_get_model (GTK_TREE_VIEW (tree))));
    return TRUE;
}

gboolean
donna_tree_get_show_hidden (DonnaTree *tree, gboolean *show_hidden)
{
    g_return_val_if_fail (DONNA_IS_TREE (tree), FALSE);
    g_return_val_if_fail (show_hidden != NULL, FALSE);

    *show_hidden = tree->priv->show_hidden;
    return TRUE;
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
donna_tree_add_root (DonnaTree *tree, DonnaNode *node)
{
    GtkTreeView     *treev;
    GtkTreeStore    *store;
    GtkTreeIter      iter;
    gboolean         has_value;
    gboolean         has_children;

    g_return_val_if_fail (DONNA_IS_TREE (tree), FALSE);
    g_return_val_if_fail (DONNA_IS_NODE (node), FALSE);

    treev = GTK_TREE_VIEW (tree);
    store = GTK_TREE_STORE (gtk_tree_model_filter_get_model (
            GTK_TREE_MODEL_FILTER (gtk_tree_view_get_model (treev))));

    gtk_tree_store_insert_with_values (store, &iter, NULL, 0,
            DONNA_TREE_COL_NODE,            node,
            DONNA_TREE_COL_EXPAND_STATE,    DONNA_TREE_EXPAND_NEVER,
            -1);
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
donna_tree_set_root (DonnaTree *tree, DonnaNode *node)
{
    GtkTreeView         *treev;
    GtkTreeModel        *model_filter;
    GtkTreeModel        *model;
    GtkTreeIter          iter;
    GtkTreeSelection    *sel;

    g_return_val_if_fail (DONNA_IS_TREE (tree), FALSE);

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
    if (!donna_tree_add_root (tree, node))
        return FALSE;

    if (!gtk_tree_model_get_iter_first (model_filter, &iter))
        return FALSE;
    gtk_tree_selection_select_iter (sel, &iter);

    /* Horizontal scrollbar stuff. See row_collapsed_cb for more */
    gtk_tree_view_columns_autosize (treev);

    return TRUE;
}

GtkWidget *
donna_tree_new (DonnaNode *node)
{
    GtkWidget           *w;
    GtkTreeView         *tree;
    GtkTreeStore        *store;
    GtkTreeModel        *model;
    GtkTreeModelFilter  *filter;
    GtkTreeModel        *model_filter;
    GtkTreeSelection    *sel;
    GtkTreeViewColumn   *column;
    GtkCellRenderer     *renderer;

    /* create a new tree */
    w = g_object_new (DONNA_TYPE_TREE, NULL);
    tree = GTK_TREE_VIEW (w);

    /* create a new tree store for it */
    store = gtk_tree_store_new (DONNA_TREE_NB_COLS,
            G_TYPE_OBJECT,  /* DONNA_TREE_COL_NODE */
            G_TYPE_INT);    /* DONNA_TREE_COL_EXPAND_STATE */
    model = GTK_TREE_MODEL (store);
    /* create a filter */
    model_filter = gtk_tree_model_filter_new (model, NULL);
    filter = GTK_TREE_MODEL_FILTER (model_filter);
    gtk_tree_model_filter_set_visible_func (filter, visible_func, tree, NULL);
    /* add to tree */
    gtk_tree_view_set_model (tree, model_filter);

    /* enable some stylling */
    gtk_tree_view_set_enable_tree_lines (tree, TRUE);
    gtk_tree_view_set_rules_hint (tree, TRUE);
    gtk_tree_view_set_headers_visible (tree, FALSE);

    /* force one (and only one) item to be selected at all time */
    sel = gtk_tree_view_get_selection (tree);
    gtk_tree_selection_set_mode (sel, GTK_SELECTION_BROWSE);

    /* create renderer & column */
    column = gtk_tree_view_column_new ();
    renderer = gtk_cell_renderer_text_new ();
    gtk_tree_view_column_set_cell_data_func (column, renderer,
            rend_fn, NULL, NULL);
    gtk_tree_view_column_pack_start (column, renderer, TRUE);
    gtk_tree_view_insert_column (tree, column, 0);

    /* sort */
    gtk_tree_sortable_set_sort_func (GTK_TREE_SORTABLE (model),
            DONNA_TREE_COL_NODE, sort_func, tree, NULL);
    gtk_tree_sortable_set_sort_column_id (GTK_TREE_SORTABLE (model),
            DONNA_TREE_COL_NODE, GTK_SORT_ASCENDING);

    /* fill tree */
    donna_tree_set_root (DONNA_TREE (tree), node);

    return w;
}
