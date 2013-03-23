
#include <gtk/gtk.h>
#include "treestore.h"

enum
{
    SID_ROW_CHANGED,
    SID_ROW_INSERTED,
    SID_ROW_HAS_CHILD_TOGGLED,
    SID_ROW_DELETED,
    SID_ROWS_REORDERED,
    NB_SIDS
};

struct _DonnaTreeStorePrivate
{
    /* from GtkTreeStore, so we can chain-up */
    GtkTreeModelIface   *iface;
    /* hashtable of hidden iters; keys are the iter->user_data-s */
    GHashTable          *hashtable;
    /* function to determine is an iter is visible or not */
    store_visible_fn     is_visible;
    gpointer             is_visible_data;
    GDestroyNotify       is_visible_destroy;
};

/* we need to store the signal_ids of the GtkTreeModel, so we can block them
 * all. This is needed since they all include a GtkTreePath which is, obviously,
 * wrong (GtkTreeStore not taking into consideration our invisible iters).
 * The way we block those signals is:
 * - for RUN_FIRST, through the default/class handler
 * - for RUN_LAST, we connect as first handler (since we do it on class init)
 */
static guint sid[NB_SIDS];

/* GtkTreeModel */
static void         tree_store_row_changed          (GtkTreeModel   *model,
                                                     GtkTreePath    *path,
                                                     GtkTreeIter    *iter);
static void         tree_store_row_inserted         (GtkTreeModel   *model,
                                                     GtkTreePath    *path,
                                                     GtkTreeIter    *iter);
static void         tree_store_row_has_child_toggled(GtkTreeModel   *mode,
                                                     GtkTreePath    *path,
                                                     GtkTreeIter    *iter);
static void         tree_store_row_deleted          (GtkTreeModel   *model,
                                                     GtkTreePath    *path);
static void         tree_store_rows_reordered       (GtkTreeModel   *model,
                                                     GtkTreePath    *path,
                                                     GtkTreeIter    *iter,
                                                     gint           *new_order);
static gboolean     tree_store_get_iter             (GtkTreeModel   *model,
                                                     GtkTreeIter    *iter,
                                                     GtkTreePath    *path);
static GtkTreePath *tree_store_get_path             (GtkTreeModel   *model,
                                                     GtkTreeIter    *iter);
static gboolean     tree_store_iter_next            (GtkTreeModel   *model,
                                                     GtkTreeIter    *iter);
static gboolean     tree_store_iter_previous        (GtkTreeModel   *model,
                                                     GtkTreeIter    *iter);
static gboolean     tree_store_iter_children        (GtkTreeModel   *model,
                                                     GtkTreeIter    *iter,
                                                     GtkTreeIter    *parent);
static gboolean     tree_store_iter_has_child       (GtkTreeModel   *model,
                                                     GtkTreeIter    *iter);
static gint         tree_store_iter_n_children      (GtkTreeModel   *model,
                                                     GtkTreeIter    *iter);
static gboolean     tree_store_iter_nth_child       (GtkTreeModel   *model,
                                                     GtkTreeIter    *iter,
                                                     GtkTreeIter    *parent,
                                                     gint            n);
static gboolean     tree_store_iter_parent          (GtkTreeModel   *model,
                                                     GtkTreeIter    *iter,
                                                     GtkTreeIter    *child);

static void     donna_tree_store_finalize           (GObject        *object);

static void
donna_tree_store_tree_model_init (GtkTreeModelIface *interface)
{
    /* class handler for RUN_FIRST signals */
    interface->row_deleted      = tree_store_row_deleted;
    interface->row_inserted     = tree_store_row_inserted;
    interface->rows_reordered   = tree_store_rows_reordered;
    /* API */
    interface->get_iter         = tree_store_get_iter;
    interface->get_path         = tree_store_get_path;
    interface->iter_next        = tree_store_iter_next;
    interface->iter_previous    = tree_store_iter_previous;
    interface->iter_children    = tree_store_iter_children;
    interface->iter_has_child   = tree_store_iter_has_child;
    interface->iter_n_children  = tree_store_iter_n_children;
    interface->iter_nth_child   = tree_store_iter_nth_child;
    interface->iter_parent      = tree_store_iter_parent;
}

G_DEFINE_TYPE_WITH_CODE (DonnaTreeStore, donna_tree_store, GTK_TYPE_TREE_STORE,
        G_IMPLEMENT_INTERFACE (GTK_TYPE_TREE_MODEL,
            donna_tree_store_tree_model_init)
        )

static void
donna_tree_store_class_init (DonnaTreeStoreClass *klass)
{
    GObjectClass *o_class;

    o_class = G_OBJECT_CLASS (klass);
    o_class->finalize = donna_tree_store_finalize;

    g_type_class_add_private (klass, sizeof (DonnaTreeStorePrivate));

    sid[SID_ROW_CHANGED]           = g_signal_lookup ("row-changed", GTK_TYPE_TREE_MODEL);
    sid[SID_ROW_INSERTED]          = g_signal_lookup ("row-inserted", GTK_TYPE_TREE_MODEL);
    sid[SID_ROW_HAS_CHILD_TOGGLED] = g_signal_lookup ("row-has_child-toggled", GTK_TYPE_TREE_MODEL);
    sid[SID_ROW_DELETED]           = g_signal_lookup ("row-deleted", GTK_TYPE_TREE_MODEL);
    sid[SID_ROWS_REORDERED]        = g_signal_lookup ("rows-reordered", GTK_TYPE_TREE_MODEL);
}

static void
donna_tree_store_init (DonnaTreeStore *store)
{
    DonnaTreeStorePrivate *priv;
    GtkTreeModelIface *interface;
    guint sid;

    priv = store->priv = G_TYPE_INSTANCE_GET_PRIVATE (store,
            DONNA_TYPE_TREE_STORE, DonnaTreeStorePrivate);
    priv->hashtable = g_hash_table_new (g_direct_hash, g_direct_equal);

    interface = G_TYPE_INSTANCE_GET_INTERFACE (GTK_TREE_MODEL (store),
            GTK_TYPE_TREE_MODEL, GtkTreeModelIface);
    priv->iface = g_type_interface_peek_parent (interface);

    /* connect to RUN_LAST signals */
    g_signal_connect (store, "row-changed",
            G_CALLBACK (tree_store_row_changed), NULL);
    g_signal_connect (store, "row-has-child-toggled",
            G_CALLBACK (tree_store_row_has_child_toggled), NULL);
}

static void
donna_tree_store_finalize (GObject *object)
{
    DonnaTreeStorePrivate *priv;

    priv = DONNA_TREE_STORE (object)->priv;
    g_hash_table_destroy (priv->hashtable);
    if (priv->is_visible_data && priv->is_visible_destroy)
        priv->is_visible_destroy (priv->is_visible_data);

    G_OBJECT_CLASS (donna_tree_store_parent_class)->finalize (object);
}

/* GtkTreeModel interface */

#define chain_up_if_possible(fn, model, ...)  do {          \
    if (!priv->is_visible                                   \
            || g_hash_table_size (priv->hashtable) == 0)    \
        return priv->iface->fn (model, __VA_ARGS__);        \
} while (0)

#define is_visible(iter) g_hash_table_contains (priv->hashtable, iter->user_data)

/* signals */

static void
tree_store_row_changed (GtkTreeModel   *model,
                        GtkTreePath    *path,
                        GtkTreeIter    *iter)
{
    DonnaTreeStore *store = (DonnaTreeStore *) model;
    DonnaTreeStorePrivate *priv = store->priv;

    g_signal_stop_emission (model, sid[SID_ROW_CHANGED], 0);
}

static void
tree_store_row_inserted (GtkTreeModel   *model,
                         GtkTreePath    *path,
                         GtkTreeIter    *iter)
{
}

static void
tree_store_row_has_child_toggled (GtkTreeModel   *mode,
                                  GtkTreePath    *path,
                                  GtkTreeIter    *iter)
{
}

static void
tree_store_row_deleted (GtkTreeModel   *model,
                        GtkTreePath    *path)
{
}

static void
tree_store_rows_reordered (GtkTreeModel   *model,
                           GtkTreePath    *path,
                           GtkTreeIter    *iter,
                           gint           *new_order)
{
}

/* API */

static gboolean
tree_store_get_iter (GtkTreeModel   *model,
                     GtkTreeIter    *iter,
                     GtkTreePath    *path)
{
    DonnaTreeStore *store = (DonnaTreeStore *) model;
    DonnaTreeStorePrivate *priv = store->priv;
    GtkTreeIter parent;
    gint *indices;
    gint depth, i;

    chain_up_if_possible (get_iter, model, iter, path);

    indices = gtk_tree_path_get_indices_with_depth (path, &depth);
    g_return_val_if_fail (depth > 0, FALSE);

    if (!tree_store_iter_nth_child (model, iter, NULL, indices[0]))
        return FALSE;

    for (i = 1; i < depth; ++i)
    {
        parent = *iter;
        if (!tree_store_iter_nth_child (model, iter, &parent, indices[i]))
            return FALSE;
    }

    return TRUE;
}

static GtkTreePath *
tree_store_get_path (GtkTreeModel   *model,
                     GtkTreeIter    *iter)
{
    DonnaTreeStore *store = (DonnaTreeStore *) model;
    DonnaTreeStorePrivate *priv = store->priv;
    GtkTreePath *path;
    GtkTreeIter it;
    gint i;

    g_return_val_if_fail (iter && is_visible (iter), FALSE);

    chain_up_if_possible (get_path, model, iter);

    path = gtk_tree_path_new ();

    it = *iter;
    for (;;)
    {
        GtkTreeIter child;

        i = 0;
        while (tree_store_iter_previous (model, &it))
            ++i;
        gtk_tree_path_prepend_index (path, i);

        child = it;
        if (!priv->iface->iter_parent (model, &it, &child))
            break;
    }

    return path;
}

static gboolean
tree_store_iter_next (GtkTreeModel   *model,
                      GtkTreeIter    *iter)
{
    DonnaTreeStore *store = (DonnaTreeStore *) model;
    DonnaTreeStorePrivate *priv = store->priv;

    g_return_val_if_fail (iter && is_visible (iter), FALSE);

    chain_up_if_possible (iter_next, model, iter);

    while (priv->iface->iter_next (model, iter))
        if (is_visible (iter))
            return TRUE;

    return FALSE;
}

static gboolean
tree_store_iter_previous (GtkTreeModel   *model,
                          GtkTreeIter    *iter)
{
    DonnaTreeStore *store = (DonnaTreeStore *) model;
    DonnaTreeStorePrivate *priv = store->priv;

    g_return_val_if_fail (iter && is_visible (iter), FALSE);

    chain_up_if_possible (iter_previous, model, iter);

    while (priv->iface->iter_previous (model, iter))
        if (is_visible (iter))
            return TRUE;

    return FALSE;
}

static gboolean
tree_store_iter_children (GtkTreeModel   *model,
                          GtkTreeIter    *iter,
                          GtkTreeIter    *parent)
{
    DonnaTreeStore *store = (DonnaTreeStore *) model;
    DonnaTreeStorePrivate *priv = store->priv;

    g_return_val_if_fail (iter && is_visible (parent), FALSE);

    chain_up_if_possible (iter_children, model, iter, parent);

    /* get the first children from store */
    if (priv->iface->iter_children (model, iter, parent))
    {
        /* move if needed to the first visible one */
        while (!is_visible (iter))
            if (!priv->iface->iter_next (model, iter))
                return FALSE;
        return TRUE;
    }

    return FALSE;
}

static gboolean
tree_store_iter_has_child (GtkTreeModel   *model,
                           GtkTreeIter    *iter)
{
    DonnaTreeStore *store = (DonnaTreeStore *) model;
    DonnaTreeStorePrivate *priv = store->priv;
    GtkTreeIter child;

    g_return_val_if_fail (iter && is_visible (iter), FALSE);

    chain_up_if_possible (iter_has_child, model, iter);

    /* if we can get the first child, if has children */
    return tree_store_iter_children (model, &child, iter);
}

static gint
tree_store_iter_n_children (GtkTreeModel   *model,
                            GtkTreeIter    *iter)
{
    DonnaTreeStore *store = (DonnaTreeStore *) model;
    DonnaTreeStorePrivate *priv = store->priv;
    GtkTreeIter child;
    gint n = 0;

    g_return_val_if_fail (iter && is_visible (iter), 0);

    chain_up_if_possible (iter_n_children, model, iter);

    if (!tree_store_iter_children (model, &child, iter))
        return 0;

    while (tree_store_iter_next (model, &child))
        ++n;
    return n;
}

static gboolean
tree_store_iter_nth_child (GtkTreeModel   *model,
                           GtkTreeIter    *iter,
                           GtkTreeIter    *parent,
                           gint            n)
{
    DonnaTreeStore *store = (DonnaTreeStore *) model;
    DonnaTreeStorePrivate *priv = store->priv;
    gint i;

    g_return_val_if_fail (iter && is_visible (parent), FALSE);

    chain_up_if_possible (iter_nth_child, model, iter, parent, n);

    tree_store_iter_children (model, iter, parent);
    for (i = n - 1; i >= 0; --i)
        if (!tree_store_iter_next (model, iter))
            return FALSE;
    return TRUE;
}

static gboolean
tree_store_iter_parent (GtkTreeModel   *model,
                        GtkTreeIter    *iter,
                        GtkTreeIter    *child)
{
    DonnaTreeStore *store = (DonnaTreeStore *) model;
    DonnaTreeStorePrivate *priv = store->priv;

    g_return_val_if_fail (iter && is_visible (child), FALSE);

    /* if child is visible, its parent should be too, so we just chain up */
    return priv->iface->iter_parent (model, iter, child);
}


/* API */

gboolean
donna_tree_store_set_visible_func   (DonnaTreeStore     *store,
                                     store_visible_fn    is_visible,
                                     gpointer            data,
                                     GDestroyNotify      destroy)
{
    DonnaTreeStorePrivate *priv;

    g_return_val_if_fail (DONNA_IS_TREE_STORE (store), FALSE);

    priv = store->priv;

    if (priv->is_visible_data && priv->is_visible_destroy)
        priv->is_visible_destroy (priv->is_visible_data);

    priv->is_visible         = is_visible;
    priv->is_visible_data    = data;
    priv->is_visible_destroy = destroy;

    return TRUE;
}

/* unlike our implementation of GtkTreeModel interface above, those work on
 * *all* iters. So yes, they just call GtkTreeStore's implementation of
 * GtkTreeModel */

gboolean
donna_tree_store_iter_next (DonnaTreeStore     *store,
                            GtkTreeIter        *iter)
{
    g_return_val_if_fail (DONNA_IS_TREE_STORE (store), FALSE);
    return store->priv->iface->iter_next (GTK_TREE_MODEL (store), iter);
}

gboolean
donna_tree_store_iter_previous (DonnaTreeStore     *store,
                                GtkTreeIter        *iter)
{
    g_return_val_if_fail (DONNA_IS_TREE_STORE (store), FALSE);
    return store->priv->iface->iter_previous (GTK_TREE_MODEL (store), iter);
}

gboolean
donna_tree_store_iter_children (DonnaTreeStore     *store,
                                GtkTreeIter        *iter,
                                GtkTreeIter        *parent)
{
    g_return_val_if_fail (DONNA_IS_TREE_STORE (store), FALSE);
    return store->priv->iface->iter_children (GTK_TREE_MODEL (store), iter, parent);
}

gboolean
donna_tree_store_iter_has_child (DonnaTreeStore     *store,
                                 GtkTreeIter        *iter)
{
    g_return_val_if_fail (DONNA_IS_TREE_STORE (store), FALSE);
    return store->priv->iface->iter_has_child (GTK_TREE_MODEL (store), iter);
}

gint
donna_tree_store_iter_n_children (DonnaTreeStore     *store,
                                  GtkTreeIter        *iter)
{
    g_return_val_if_fail (DONNA_IS_TREE_STORE (store), 0);
    return store->priv->iface->iter_n_children (GTK_TREE_MODEL (store), iter);
}

gboolean
donna_tree_store_iter_nth_child (DonnaTreeStore     *store,
                                 GtkTreeIter        *iter,
                                 GtkTreeIter        *parent,
                                 gint                n)
{
    g_return_val_if_fail (DONNA_IS_TREE_STORE (store), FALSE);
    return store->priv->iface->iter_nth_child (GTK_TREE_MODEL (store),
            iter, parent, n);
}

gboolean
donna_tree_store_iter_parent (DonnaTreeStore     *store,
                              GtkTreeIter        *iter,
                              GtkTreeIter        *child)
{
    g_return_val_if_fail (DONNA_IS_TREE_STORE (store), FALSE);
    return store->priv->iface->iter_parent (GTK_TREE_MODEL (store), iter, child);
}
