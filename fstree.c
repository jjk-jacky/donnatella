
#include <gtk/gtk.h>
#include <string.h>
#include "fstree.h"

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
has_folder (const gchar *root)
{
    GError      *err = NULL;
    GDir        *dir;
    const gchar *name;
    gboolean     ret = FALSE;
    gchar        buf[1024];
    gchar       *s;

    dir = g_dir_open (root, 0, &err);
    if (err)
    {
        g_clear_error (&err);
        return FALSE;
    }

    while ((name = g_dir_read_name (dir)))
    {
        s = NULL;
        if (g_snprintf (buf, 1024, "%s/%s", root, name) >= 1024)
        {
            s = g_strdup_printf ("%s/%s", root, name);
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
fill_folders (GtkTreeView   *tree,
              GtkTreeIter   *iter,
              const gchar   *root,
              GDir          *dir)
{
    GtkTreeModelFilter  *filter;
    GtkTreeModel        *model;
    GtkTreeStore        *store;
    GtkTreeIter          new_iter;
    const gchar         *name;
    gchar                buf[1024];
    gchar               *s;

    filter = GTK_TREE_MODEL_FILTER (gtk_tree_view_get_model (tree));
    model = gtk_tree_model_filter_get_model (filter);
    store = GTK_TREE_STORE (model);

    while ((name = g_dir_read_name (dir)))
    {
        s = NULL;
        if (g_snprintf (buf, 1024, "%s/%s", root, name) >= 1024)
        {
            s = g_strdup_printf ("%s/%s", root, name);
        }
        if (g_file_test ((s) ? s : buf, G_FILE_TEST_IS_DIR))
        {
            gtk_tree_store_insert_with_values (store, &new_iter, iter, -1,
                    FST_COL_FULL_NAME,      (s) ? s : buf,
                    FST_COL_DISPLAY_NAME,   name,
                    FST_COL_WAS_EXPANDED,   FALSE,
                    -1);
            if (has_folder ((s) ? s : buf))
                /* insert a fake node, because we haven't populated the children yet */
                gtk_tree_store_insert_with_values (store, NULL, &new_iter, 0,
                        FST_COL_FULL_NAME,      NULL,
                        FST_COL_DISPLAY_NAME,   NULL,
                        FST_COL_WAS_EXPANDED,   FALSE,
                        -1);
        }
        if (s)
            g_free (s);
    }
    return TRUE;
}

static gboolean
visible_func (GtkTreeModel  *_model,
              GtkTreeIter   *_iter,
              gpointer       data)
{
    FsTree *fstree = FSTREE (data);
    FsTreePrivate *priv = fstree->priv;
    gchar *s = NULL;

    if (priv->show_hidden)
        return TRUE;

    gtk_tree_model_get (_model, _iter, FST_COL_DISPLAY_NAME, &s, -1);
    return !s || *s != '.';
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
    GDir                *dir;
    gchar               *root;

    _model = gtk_tree_view_get_model (tree);
    gtk_tree_model_get (_model, _iter,
            FST_COL_FULL_NAME,      &root,
            FST_COL_WAS_EXPANDED,   &was_expanded,
            -1);
    if (!was_expanded)
    {
        GtkTreeModelFilter  *filter = GTK_TREE_MODEL_FILTER (_model);
        GtkTreeStore        *store;
        GtkTreeIter          iter;

        dir = g_dir_open (root, 0, &err);
        if (err)
        {
            g_free (root);
            g_clear_error (&err);
            return;
        }

        store = GTK_TREE_STORE (gtk_tree_model_filter_get_model (filter));
        gtk_tree_model_filter_convert_iter_to_child_iter (filter, &iter, _iter);
        if (!fill_folders (tree, &iter, root, dir))
        {
            // remove all children
        }
        else
        {
            GtkTreeIter iter_blank;

            gtk_tree_store_set (store, &iter,
                    FST_COL_WAS_EXPANDED, TRUE,
                    -1);
            /* remove first parent, aka "fake"/blank node */
            gtk_tree_model_iter_children (GTK_TREE_MODEL (store),
                    &iter_blank, &iter);
            gtk_tree_store_remove (store, &iter_blank);
        }

        g_dir_close (dir);
    }
    g_free (root);
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

gboolean
fstree_set_show_hidden (FsTree *fstree, gboolean show_hidden)
{
    FsTreePrivate *priv;

    g_return_val_if_fail (IS_FSTREE (fstree), FALSE);

    priv = fstree->priv;
    if (priv->show_hidden == show_hidden)
        return FALSE;
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
    GtkTreeView         *tree;
    GtkTreeStore        *store;
    GtkTreeIter          iter;
    const gchar         *s;

    g_return_val_if_fail (IS_FSTREE (fstree), FALSE);
    g_return_val_if_fail (root != NULL, FALSE);

    tree = GTK_TREE_VIEW (fstree);
    store = GTK_TREE_STORE (gtk_tree_model_filter_get_model (
            GTK_TREE_MODEL_FILTER (gtk_tree_view_get_model (tree))));

    s = strrchr (root, '/');
    if (s && s[1] != '\0')
        ++s;
    gtk_tree_store_insert_with_values (store, &iter, NULL, 0,
            FST_COL_FULL_NAME,      root,
            FST_COL_DISPLAY_NAME,   s,
            FST_COL_WAS_EXPANDED,   FALSE,
            -1);
    if (has_folder (root))
        /* insert a fake node, because we haven't populated the children yet */
        gtk_tree_store_insert_with_values (store, NULL, &iter, 0,
                FST_COL_FULL_NAME,      NULL,
                FST_COL_DISPLAY_NAME,   NULL,
                FST_COL_WAS_EXPANDED,   FALSE,
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
    GtkCellRenderer     *renderer;

    /* create a new FsTree */
    w = g_object_new (TYPE_FSTREE, NULL);
    tree = GTK_TREE_VIEW (w);

    /* create a new tree store for it */
    store = gtk_tree_store_new (FST_NB_COLS,
            G_TYPE_STRING,      /* FST_COL_FULL_NAME */
            G_TYPE_STRING,      /* FST_COL_DISPLAY_NAME */
            G_TYPE_BOOLEAN);    /* FST_COL_WAS_EXPANDED */
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
    renderer = gtk_cell_renderer_text_new ();
    gtk_tree_view_insert_column_with_attributes (tree, 0, "name", renderer,
            "text", FST_COL_DISPLAY_NAME, NULL);

    /* sort */
    gtk_tree_sortable_set_sort_column_id (GTK_TREE_SORTABLE (model),
            FST_COL_DISPLAY_NAME, GTK_SORT_ASCENDING);

    /* fill tree */
    fstree_set_root (FSTREE (tree), root);

    /* connetc to some signals */
    g_signal_connect (G_OBJECT (tree), "row-expanded",
            G_CALLBACK (row_expanded_cb), NULL);
    g_signal_connect (G_OBJECT (tree), "row-collapsed",
            G_CALLBACK (row_collapsed_cb), NULL);

    return w;
}
