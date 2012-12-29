
#include <gtk/gtk.h>
#include <string.h>
#include <stdlib.h>
#include "fstree.h"

static FsTreeNode *new_folder_node (const gchar *root);

struct _FsTreePrivate
{
    guint show_hidden : 1;
    guint is_minitree : 1;
};

G_DEFINE_TYPE (FsTree, fstree, GTK_TYPE_TREE_VIEW);

static void
fstree_class_init (FsTreeClass *klass)
{
    g_type_class_add_private (klass, sizeof (FsTreePrivate));
}

static void
fstree_init (FsTree *fstree)
{
    FsTreePrivate *priv;

    priv = fstree->priv = G_TYPE_INSTANCE_GET_PRIVATE (fstree, TYPE_FSTREE,
            FsTreePrivate);

    priv->show_hidden = 1;
    priv->is_minitree = 0;
}

static gboolean
has_folder (const FsTreeNode *fstreenode)
{
    GError      *err = NULL;
    GDir        *dir;
    const gchar *name;
    gboolean     ret = FALSE;
    gchar        buf[1024];
    gchar       *s;

    /* key is actualy the full path of the folder */
    dir = g_dir_open (fstreenode->key, 0, &err);
    if (err)
    {
        g_clear_error (&err);
        return FALSE;
    }

    while ((name = g_dir_read_name (dir)))
    {
        s = NULL;
        if (g_snprintf (buf, 1024, "%s/%s", fstreenode->key, name) >= 1024)
        {
            s = g_strdup_printf ("%s/%s", fstreenode->key, name);
        }
        if (g_file_test ((s) ? s : buf, G_FILE_TEST_IS_DIR))
        {
            ret = TRUE;
            if (s)
                g_free (s);
            break;
        }
        if (s)
            g_free (s);
    }
    g_dir_close (dir);
    return ret;
}

static gboolean
fill_folders (const FsTreeNode  *node,
              FsTree            *fstree,
              GtkTreeIter       *iter)
{
    GError              *err = NULL;
    GtkTreeModelFilter  *filter;
    GtkTreeModel        *model;
    GtkTreeStore        *store;
    GtkTreeIter          new_iter;
    GDir                *dir;
    const gchar         *name;
    gchar                buf[1024];
    gchar               *s;
    gboolean             ret = TRUE;

    dir = g_dir_open (node->key, 0, &err);
    if (err)
    {
        g_clear_error (&err);
        return FALSE;
    }

    filter = GTK_TREE_MODEL_FILTER (gtk_tree_view_get_model (
                GTK_TREE_VIEW (fstree)));
    model = gtk_tree_model_filter_get_model (filter);
    store = GTK_TREE_STORE (model);

    while ((name = g_dir_read_name (dir)))
    {
        s = NULL;
        /* special case: root, to not get //bin */
        if (node->key[0] == '/' && node->key[1] == '\0')
        {
            if (g_snprintf (buf, 1024, "/%s", name) >= 1024)
            {
                s = g_strdup_printf ("/%s", name);
            }
        }
        else if (g_snprintf (buf, 1024, "%s/%s", node->key, name) >= 1024)
        {
            s = g_strdup_printf ("%s/%s", node->key, name);
        }
        if (g_file_test ((s) ? s : buf, G_FILE_TEST_IS_DIR))
        {
            FsTreeNode *child;

            child = new_folder_node ((s) ? s : buf);
            if (!child)
            {
                if (s)
                    g_free (s);
                ret = FALSE;
                break;
            }
            gtk_tree_store_insert_with_values (store, &new_iter, iter, -1,
                    FST_COL_NODE,           child,
                    FST_COL_EXPAND_STATE,   FST_EXPAND_NEVER,
                    -1);
            if (has_folder (child))
                /* insert a fake node, because we haven't populated the children yet */
                gtk_tree_store_insert_with_values (store, NULL, &new_iter, 0,
                        FST_COL_NODE,           NULL,
                        FST_COL_EXPAND_STATE,   FST_EXPAND_NEVER,
                        -1);
        }
        if (s)
            g_free (s);
    }
    g_dir_close (dir);
    return ret;
}

static FsTreeNode *
new_folder_node (const gchar *root)
{
    FsTreeNode *node;

    node = calloc (1, sizeof (*node));
    if (!node)
        return NULL;

    node->key           = strdup (root);
    node->name          = strrchr (node->key, '/');
    /* unless this is root ("/") we go past the / for display */
    if (node->name[1] != '\0')
        ++(node->name);
    node->tooltip       = node->key;
    node->has_children  = has_folder;
    node->fill_children = fill_folders;
    return node;
}

static void
free_folder_node (FsTreeNode *node)
{
    if (!node)
        return;
    free (node->key);
    free (node);
}

static gboolean
visible_func (GtkTreeModel  *_model,
              GtkTreeIter   *_iter,
              gpointer       data)
{
    FsTree *fstree = FSTREE (data);
    FsTreePrivate *priv = fstree->priv;
    FsTreeNode *node;

    if (priv->show_hidden)
        return TRUE;

    gtk_tree_model_get (_model, _iter, FST_COL_NODE, &node, -1);
    return !node || *node->name != '.';
}

static void
row_expanded_cb (GtkTreeView    *tree,
                 GtkTreeIter    *_iter,
                 GtkTreePath    *_path,
                 gpointer        data)
{
    GError              *err = NULL;
    GtkTreeModelFilter  *filter;
    GtkTreeModel        *_model;
    gboolean             was_expanded;
    FsTreeNode          *node;
    expand_state_t       expand_state;

    _model = gtk_tree_view_get_model (tree);
    gtk_tree_model_get (_model, _iter,
            FST_COL_NODE,           &node,
            FST_COL_EXPAND_STATE,   &expand_state,
            -1);
    if (expand_state == FST_EXPAND_NEVER)
    {
        GtkTreeModelFilter  *filter = GTK_TREE_MODEL_FILTER (_model);
        GtkTreeStore        *store;
        GtkTreeIter          iter;

        store = GTK_TREE_STORE (gtk_tree_model_filter_get_model (filter));
        gtk_tree_model_filter_convert_iter_to_child_iter (filter, &iter, _iter);
        if (!node->fill_children (node, FSTREE (tree), &iter))
        {
            // remove all children
            printf ("fail fill children\n");
        }
        else
        {
            GtkTreeIter iter_blank;

            gtk_tree_store_set (store, &iter,
                    FST_COL_EXPAND_STATE, FST_EXPAND_FULL,
                    -1);
            /* remove first parent, aka "fake"/blank node */
            gtk_tree_model_iter_children (GTK_TREE_MODEL (store),
                    &iter_blank, &iter);
            gtk_tree_store_remove (store, &iter_blank);
        }
    }
}

static void
row_collapsed_cb (GtkTreeView   *tree,
                  GtkTreeIter   *_iter,
                  GtkTreePath   *_path,
                  gpointer       data)
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
    FsTreeNode *node;

    gtk_tree_model_get (model, iter, FST_COL_NODE, &node, -1);
    g_object_set (renderer, "text", node->name, NULL);
}

#define COLLATION_SENTINEL  "\1\1\1"

static gchar *
utf8_collate_key (const gchar *str, gssize len)
{
    GString *result;
    GString *append;
    const gchar *p;
    const gchar *prev;
    const gchar *end;
    gchar *collate_key;
    gint digits;
    gint leading_zeros;
    gint prefix = 0;

    if (len < 0)
        len = strlen (str);

    result = g_string_sized_new (len * 2);
    append = g_string_sized_new (0);

    end = str + len;

    /* No need to use utf8 functions, since we're only looking for ascii chars */
    for (prev = p = str; p < end; ++p)
    {
        if (prefix < 2)
            switch (*p)
            {
                /* filenames starting with those characters will be sorted
                 * before others */
                case '-':
                case '+':
                case ' ':
                case '#':
                case '$':
                case '!':
                    if (prefix == 0)
                        ++prefix;
                    break;

                default:
                    if (prefix == 1)
                    {
                        if (prev != p)
                        {
                            /* adding the string itself and not a collate_key
                             * so that ! comes before - */
                            g_string_append_len (result, prev, p - prev);
                        }
                        g_string_append (result, COLLATION_SENTINEL "\1");
                    }
                    prefix = 2;
            }

        switch (*p)
        {
            case '.':
                if (0&&p == str)
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
    FsTreeNode *node1;
    FsTreeNode *node2;
    gchar *key1;
    gchar *key2;
    gint   ret;

    gtk_tree_model_get (model, iter1, FST_COL_NODE, &node1, -1);
    gtk_tree_model_get (model, iter2, FST_COL_NODE, &node2, -1);

    /* one node could be a "fake" one, i.e. node is a NULL pointer */
    if (!node1)
        return -1;
    else if (!node2)
        return 1;

    key1 = utf8_collate_key (node1->name, -1);
    key2 = utf8_collate_key (node2->name, -1);
    ret = strcmp (key1, key2);
    g_free (key1);
    g_free (key2);
    return ret;
}

gboolean
fstree_set_show_hidden (FsTree *fstree, gboolean show_hidden)
{
    FsTreePrivate *priv;

    g_return_val_if_fail (IS_FSTREE (fstree), FALSE);

    priv = fstree->priv;
    if (priv->show_hidden == show_hidden)
        return TRUE;
    priv->show_hidden = show_hidden;
    gtk_tree_model_filter_refilter (GTK_TREE_MODEL_FILTER (
                gtk_tree_view_get_model (GTK_TREE_VIEW (fstree))));
    return TRUE;
}

gboolean
fstree_get_show_hidden (FsTree *fstree, gboolean *show_hidden)
{
    g_return_val_if_fail (IS_FSTREE (fstree), FALSE);
    g_return_val_if_fail (show_hidden != NULL, FALSE);

    *show_hidden = fstree->priv->show_hidden;
    return TRUE;
}

gboolean
fstree_add_root (FsTree *fstree, const gchar *root)
{
    GtkTreeView     *tree;
    GtkTreeStore    *store;
    GtkTreeIter      iter;
    FsTreeNode      *node;

    g_return_val_if_fail (IS_FSTREE (fstree), FALSE);
    g_return_val_if_fail (root != NULL, FALSE);

    tree = GTK_TREE_VIEW (fstree);
    store = GTK_TREE_STORE (gtk_tree_model_filter_get_model (
            GTK_TREE_MODEL_FILTER (gtk_tree_view_get_model (tree))));

    node = new_folder_node (root);
    if (!node)
        return FALSE;
    gtk_tree_store_insert_with_values (store, &iter, NULL, 0,
            FST_COL_NODE,           node,
            FST_COL_EXPAND_STATE,   FST_EXPAND_NEVER,
            -1);
    if (node->has_children (node))
        /* insert a fake node, because we haven't populated the children yet */
        gtk_tree_store_insert_with_values (store, NULL, &iter, 0,
                FST_COL_NODE,           NULL,
                FST_COL_EXPAND_STATE,   FST_EXPAND_NEVER,
                -1);

    return TRUE;
}

gboolean
fstree_set_root (FsTree *fstree, const gchar *root)
{
    GtkTreeView         *tree;
    GtkTreeModel        *model_filter;
    GtkTreeModel        *model;
    GtkTreeIter          iter;
    GtkTreeSelection    *sel;

    g_return_val_if_fail (IS_FSTREE (fstree), FALSE);
    if (!root)
        root = "/";

    tree = GTK_TREE_VIEW (fstree);
    model_filter = gtk_tree_view_get_model (tree);
    model = gtk_tree_model_filter_get_model (GTK_TREE_MODEL_FILTER (model_filter));
    sel = gtk_tree_view_get_selection (tree);

    gtk_tree_store_clear (GTK_TREE_STORE (model));
    if (!fstree_add_root (fstree, root))
        return FALSE;

    if (!gtk_tree_model_get_iter_first (model_filter, &iter))
        return FALSE;
    gtk_tree_selection_select_iter (sel, &iter);

    /* Horizontal scrollbar stuff. See row_collapsed_cb for more */
    gtk_tree_view_columns_autosize (tree);

    return TRUE;
}

GtkWidget *
fstree_new (const gchar *root)
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

    /* create a new FsTree */
    w = g_object_new (TYPE_FSTREE, NULL);
    tree = GTK_TREE_VIEW (w);

    /* create a new tree store for it */
    store = gtk_tree_store_new (FST_NB_COLS,
            G_TYPE_POINTER, /* FST_COL_NODE */
            G_TYPE_INT);    /* FST_COL_EXPAND_STATE */
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
            FST_COL_NODE, sort_func, NULL, NULL);
    gtk_tree_sortable_set_sort_column_id (GTK_TREE_SORTABLE (model),
            FST_COL_NODE, GTK_SORT_ASCENDING);

    /* fill tree */
    fstree_set_root (FSTREE (tree), root);

    /* connetc to some signals */
    g_signal_connect (G_OBJECT (tree), "row-expanded",
            G_CALLBACK (row_expanded_cb), NULL);
    g_signal_connect (G_OBJECT (tree), "row-collapsed",
            G_CALLBACK (row_collapsed_cb), NULL);

    return w;
}
