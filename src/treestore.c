
#include "config.h"

#include <gtk/gtk.h>
#include "treestore.h"
#include "closures.h"

enum
{
    ROW_FAKE_DELETED,
    NB_SIGNALS
};

struct _DonnaTreeStorePrivate
{
    /* the actual store */
    GtkTreeStore        *store;
    /* keys are the iter->user_data-s, values booleans for visibility */
    GHashTable          *hashtable;
    /* function to determine is an iter is visible or not */
    store_visible_fn     is_visible;
    gpointer             is_visible_data;
    GDestroyNotify       is_visible_destroy;
};

static guint tree_store_signals[NB_SIGNALS] = { 0 };

/* GtkTreeModel */
static GtkTreeModelFlags tree_store_get_flags       (GtkTreeModel   *model);
static gint         tree_store_get_n_columns        (GtkTreeModel   *model);
static GType        tree_store_get_column_type      (GtkTreeModel   *model,
                                                     gint            index);
static gboolean     tree_store_get_iter             (GtkTreeModel   *model,
                                                     GtkTreeIter    *iter,
                                                     GtkTreePath    *path);
static GtkTreePath *tree_store_get_path             (GtkTreeModel   *model,
                                                     GtkTreeIter    *iter);
static void         tree_store_get_value            (GtkTreeModel   *model,
                                                     GtkTreeIter    *iter,
                                                     gint            column,
                                                     GValue         *value);
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
static void         tree_store_ref_node             (GtkTreeModel   *model,
                                                     GtkTreeIter    *iter);
static void         tree_store_unref_node           (GtkTreeModel   *model,
                                                     GtkTreeIter    *iter);
/* GtkTreeSortable */
static gboolean     tree_store_get_sort_column_id (
                                            GtkTreeSortable         *sortable,
                                            gint                    *sort_column_id,
                                            GtkSortType             *order);
static void         tree_store_set_sort_column_id (
                                            GtkTreeSortable         *sortable,
                                            gint                     sort_column_id,
                                            GtkSortType              order);
static void         tree_store_set_sort_func (
                                            GtkTreeSortable         *sortable,
                                            gint                     sort_column_id,
                                            GtkTreeIterCompareFunc   sort_func,
                                            gpointer                 data,
                                            GDestroyNotify           destroy);
static void         tree_store_set_default_sort_func (
                                            GtkTreeSortable         *sortable,
                                            GtkTreeIterCompareFunc   sort_func,
                                            gpointer                 data,
                                            GDestroyNotify           destroy);
static gboolean     tree_store_has_default_sort_func (
                                            GtkTreeSortable         *sortable);
#ifdef GTK_IS_JJK
/* GtkTreeBoxable */
static gint         tree_store_get_box_column (
                                            GtkTreeBoxable          *boxable);
static gboolean     tree_store_set_box_column (
                                            GtkTreeBoxable          *boxable,
                                            gint                     column);
static gboolean     tree_store_get_current_box_info (
                                            GtkTreeBoxable          *boxable,
                                            gchar                  **box,
                                            gint                    *depth,
                                            GtkTreeIter             *iter);
static gboolean     tree_store_get_in_box_info (
                                            GtkTreeBoxable          *boxable,
                                            GtkTreeIter             *iter_box,
                                            gchar                  **box,
                                            gint                    *depth,
                                            GtkTreeIter             *iter);
static gboolean     tree_store_get_main_box_info (
                                            GtkTreeBoxable          *boxable,
                                            GtkTreeIter             *iter_box,
                                            gchar                  **box,
                                            gint                    *depth,
                                            GtkTreeIter             *iter);
#endif


static void     donna_tree_store_finalize           (GObject            *object);

static void
donna_tree_store_tree_model_init (GtkTreeModelIface *interface)
{
    interface->get_flags        = tree_store_get_flags;
    interface->get_n_columns    = tree_store_get_n_columns;
    interface->get_column_type  = tree_store_get_column_type;
    interface->get_iter         = tree_store_get_iter;
    interface->get_path         = tree_store_get_path;
    interface->get_value        = tree_store_get_value;
    interface->iter_next        = tree_store_iter_next;
    interface->iter_previous    = tree_store_iter_previous;
    interface->iter_children    = tree_store_iter_children;
    interface->iter_has_child   = tree_store_iter_has_child;
    interface->iter_n_children  = tree_store_iter_n_children;
    interface->iter_nth_child   = tree_store_iter_nth_child;
    interface->iter_parent      = tree_store_iter_parent;
    interface->ref_node         = tree_store_ref_node;
    interface->unref_node       = tree_store_unref_node;
}

static void
donna_tree_store_sortable_init (GtkTreeSortableIface *interface)
{
    interface->get_sort_column_id    = tree_store_get_sort_column_id;
    interface->set_sort_column_id    = tree_store_set_sort_column_id;
    interface->set_sort_func         = tree_store_set_sort_func;
    interface->set_default_sort_func = tree_store_set_default_sort_func;
    interface->has_default_sort_func = tree_store_has_default_sort_func;
}

#ifdef GTK_IS_JJK
static void
donna_tree_store_boxable_init (GtkTreeBoxableInterface *interface)
{
    interface->get_box_column       = tree_store_get_box_column;
    interface->set_box_column       = tree_store_set_box_column;
    interface->get_current_box_info = tree_store_get_current_box_info;
    interface->get_in_box_info      = tree_store_get_in_box_info;
    interface->get_main_box_info    = tree_store_get_main_box_info;
}
#endif

G_DEFINE_TYPE_WITH_CODE (DonnaTreeStore, donna_tree_store, G_TYPE_OBJECT,
        G_IMPLEMENT_INTERFACE (GTK_TYPE_TREE_MODEL,
            donna_tree_store_tree_model_init)
        G_IMPLEMENT_INTERFACE (GTK_TYPE_TREE_SORTABLE,
            donna_tree_store_sortable_init)
#ifdef GTK_IS_JJK
        G_IMPLEMENT_INTERFACE (GTK_TYPE_TREE_BOXABLE,
            donna_tree_store_boxable_init)
#endif
        )

static void
donna_tree_store_class_init (DonnaTreeStoreClass *klass)
{
    GObjectClass *o_class;

    tree_store_signals[ROW_FAKE_DELETED] =
        g_signal_new ("row-fake-deleted",
                DONNA_TYPE_TREE_STORE,
                G_SIGNAL_RUN_LAST,
                G_STRUCT_OFFSET (DonnaTreeStoreClass, row_fake_deleted),
                NULL,
                NULL,
                g_cclosure_user_marshal_VOID__BOXED_BOXED,
                G_TYPE_NONE,
                2,
                GTK_TYPE_TREE_PATH,
                GTK_TYPE_TREE_ITER);

    o_class = G_OBJECT_CLASS (klass);
    o_class->finalize = donna_tree_store_finalize;

    g_type_class_add_private (klass, sizeof (DonnaTreeStorePrivate));
}

static void
donna_tree_store_init (DonnaTreeStore *store)
{
    DonnaTreeStorePrivate *priv;

    priv = store->priv = G_TYPE_INSTANCE_GET_PRIVATE (store,
            DONNA_TYPE_TREE_STORE, DonnaTreeStorePrivate);
    priv->hashtable = g_hash_table_new (g_direct_hash, g_direct_equal);
}

static void
donna_tree_store_finalize (GObject *object)
{
    DonnaTreeStorePrivate *priv;

    priv = DONNA_TREE_STORE (object)->priv;
    g_hash_table_destroy (priv->hashtable);
    if (priv->is_visible_data && priv->is_visible_destroy)
        priv->is_visible_destroy (priv->is_visible_data);
    if (priv->store)
        g_object_unref (priv->store);

    G_OBJECT_CLASS (donna_tree_store_parent_class)->finalize (object);
}

/* GtkTreeModel interface */

#define chain_up_if_possible(fn, model, ...)  do {          \
    if (!priv->is_visible)                                  \
        return gtk_tree_model_##fn ((GtkTreeModel *) (model), __VA_ARGS__); \
} while (0)

/* works because the value is a gboolean of visibility */
#define iter_is_visible(iter) \
    ((iter) && g_hash_table_lookup (priv->hashtable, (iter)->user_data))

static GtkTreeModelFlags
tree_store_get_flags (GtkTreeModel   *model)
{
    /* same as GtkTreeStore */
    return GTK_TREE_MODEL_ITERS_PERSIST;
}

static gint
tree_store_get_n_columns (GtkTreeModel   *model)
{
    return gtk_tree_model_get_n_columns (
            GTK_TREE_MODEL (DONNA_TREE_STORE (model)->priv->store));
}

static GType
tree_store_get_column_type (GtkTreeModel   *model,
                            gint            index)
{
    return gtk_tree_model_get_column_type (
            GTK_TREE_MODEL (DONNA_TREE_STORE (model)->priv->store), index);
}

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

    chain_up_if_possible (get_iter, priv->store, iter, path);

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
    GtkTreeModel *_model = GTK_TREE_MODEL (priv->store);
    GtkTreePath *path;
    GtkTreeIter it;
    gint i;

    g_return_val_if_fail (iter_is_visible (iter), FALSE);

    chain_up_if_possible (get_path, priv->store, iter);

    path = gtk_tree_path_new ();

    it = *iter;
    for (;;)
    {
        GtkTreeIter child = it;

        i = 0;
        while (tree_store_iter_previous (model, &it))
            ++i;
        gtk_tree_path_prepend_index (path, i);

        if (!gtk_tree_model_iter_parent (_model, &it, &child))
            break;
    }

    return path;
}

static void
tree_store_get_value (GtkTreeModel   *model,
                      GtkTreeIter    *iter,
                      gint            column,
                      GValue         *value)
{
    gtk_tree_model_get_value (
            GTK_TREE_MODEL (DONNA_TREE_STORE (model)->priv->store),
            iter, column, value);
}

static gboolean
tree_store_iter_next (GtkTreeModel   *model,
                      GtkTreeIter    *iter)
{
    DonnaTreeStore *store = (DonnaTreeStore *) model;
    DonnaTreeStorePrivate *priv = store->priv;
    GtkTreeModel *_model = GTK_TREE_MODEL (priv->store);

    g_return_val_if_fail (iter_is_visible (iter), FALSE);

    chain_up_if_possible (iter_next, priv->store, iter);

    while (gtk_tree_model_iter_next (_model, iter))
        if (iter_is_visible (iter))
            return TRUE;

    return FALSE;
}

static gboolean
tree_store_iter_previous (GtkTreeModel   *model,
                          GtkTreeIter    *iter)
{
    DonnaTreeStore *store = (DonnaTreeStore *) model;
    DonnaTreeStorePrivate *priv = store->priv;
    GtkTreeModel *_model = GTK_TREE_MODEL (priv->store);

    g_return_val_if_fail (iter_is_visible (iter), FALSE);

    chain_up_if_possible (iter_previous, priv->store, iter);

    while (gtk_tree_model_iter_previous (_model, iter))
        if (iter_is_visible (iter))
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
    GtkTreeModel *_model = GTK_TREE_MODEL (priv->store);

    if (parent)
        g_return_val_if_fail (iter_is_visible (parent), FALSE);

    chain_up_if_possible (iter_children, priv->store, iter, parent);

    /* get the first children from store */
    if (gtk_tree_model_iter_children (_model, iter, parent))
    {
        /* move if needed to the first visible one */
        while (!iter_is_visible (iter))
            if (!gtk_tree_model_iter_next (_model, iter))
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

    g_return_val_if_fail (iter_is_visible (iter), FALSE);

    chain_up_if_possible (iter_has_child, priv->store, iter);

    /* if we can get the first child, it has children */
    return tree_store_iter_children (model, &child, iter);
}

static gint
tree_store_iter_n_children (GtkTreeModel   *model,
                            GtkTreeIter    *iter)
{
    DonnaTreeStore *store = (DonnaTreeStore *) model;
    DonnaTreeStorePrivate *priv = store->priv;
    GtkTreeIter child;
    gint n = 1;

    if (iter)
        g_return_val_if_fail (iter_is_visible (iter), 0);

    chain_up_if_possible (iter_n_children, priv->store, iter);

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

    if (parent)
        g_return_val_if_fail (iter_is_visible (parent), FALSE);

    chain_up_if_possible (iter_nth_child, priv->store, iter, parent, n);

    if (!tree_store_iter_children (model, iter, parent))
        return FALSE;
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

    g_return_val_if_fail (iter_is_visible (child), FALSE);

    /* if child is visible, its parent should be too, so we just chain up */
    return gtk_tree_model_iter_parent (GTK_TREE_MODEL (priv->store), iter, child);
}

static void
tree_store_ref_node (GtkTreeModel   *model,
                     GtkTreeIter    *iter)
{
    gtk_tree_model_ref_node (
            GTK_TREE_MODEL (DONNA_TREE_STORE (model)->priv->store), iter);
}

static void
tree_store_unref_node (GtkTreeModel   *model,
                       GtkTreeIter    *iter)
{
    gtk_tree_model_unref_node (
            GTK_TREE_MODEL (DONNA_TREE_STORE (model)->priv->store), iter);
}

#undef chain_up_if_possible

/* GtkTreeModel extensions */

gboolean
donna_tree_model_iter_next (GtkTreeModel       *model,
                            GtkTreeIter        *iter)
{
    DonnaTreeStorePrivate *priv;
    GtkTreeIter it;

    g_return_val_if_fail (DONNA_IS_TREE_STORE (model), FALSE);
    priv = ((DonnaTreeStore *) model)->priv;
    g_return_val_if_fail (iter_is_visible (iter), FALSE);

    /* get first child if any */
    if (tree_store_iter_children (model, &it, iter))
    {
        *iter = it;
        return TRUE;
    }
    /* then look for sibling */
    it = *iter;
    if (tree_store_iter_next (model, &it))
    {
        *iter = it;
        return TRUE;
    }
    /* then we need the parent's sibling */
    while (1)
    {
        if (!tree_store_iter_parent (model, &it, iter))
        {
            iter->stamp = 0;
            return FALSE;
        }
        *iter = it;
        if (tree_store_iter_next (model, &it))
        {
            *iter = it;
            return TRUE;
        }
    }
}

static gboolean
_get_last_child (GtkTreeModel *model, GtkTreeIter *iter)
{
    GtkTreeIter it = *iter;
    if (!tree_store_iter_children (model, &it, (iter->stamp == 0) ? NULL : iter))
        return FALSE;
    *iter = it;
    while (tree_store_iter_next (model, &it))
        *iter = it;
    return TRUE;
}

#define get_last_child(model, iter) for (;;) {  \
    if (!_get_last_child (model, iter))         \
        break;                                  \
}

gboolean
donna_tree_model_iter_previous (GtkTreeModel       *model,
                                GtkTreeIter        *iter)
{
    DonnaTreeStorePrivate *priv;
    GtkTreeIter it;

    g_return_val_if_fail (DONNA_IS_TREE_STORE (model), FALSE);
    priv = ((DonnaTreeStore *) model)->priv;
    g_return_val_if_fail (iter_is_visible (iter), FALSE);

    /* get previous sibbling if any */
    it = *iter;
    if (tree_store_iter_previous (model, &it))
    {
        *iter = it;
        /* and go down to its last child */
        get_last_child (model, iter);
        return TRUE;
    }
    /* else we get the parent */
    if (tree_store_iter_parent (model, &it, iter))
    {
        *iter = it;
        return TRUE;
    }
    iter->stamp = 0;
    return FALSE;
}

gboolean
donna_tree_model_iter_last (GtkTreeModel       *model,
                            GtkTreeIter        *iter)
{
    g_return_val_if_fail (DONNA_IS_TREE_STORE (model), FALSE);
    g_return_val_if_fail (iter != NULL, FALSE);

    iter->stamp = 0;
    get_last_child (model, iter);
    return iter->stamp != 0;
}

static void
get_count_visible (gpointer key, gpointer value, gpointer data)
{
    if (value)
        * (gint *) data = 1 + * (gint *) data;
}

gint
donna_tree_model_get_count (GtkTreeModel       *model)
{
    DonnaTreeStorePrivate *priv;
    gint count = 0;

    g_return_val_if_fail (DONNA_IS_TREE_STORE (model), FALSE);
    priv = ((DonnaTreeStore *) model)->priv;

    g_hash_table_foreach (priv->hashtable, get_count_visible, &count);
    return count;
}


/* GtkTreeSortable */

static gboolean
tree_store_get_sort_column_id (GtkTreeSortable         *sortable,
                               gint                    *sort_column_id,
                               GtkSortType             *order)
{
    return gtk_tree_sortable_get_sort_column_id (
            GTK_TREE_SORTABLE (DONNA_TREE_STORE (sortable)->priv->store),
            sort_column_id, order);
}

static void
tree_store_set_sort_column_id (GtkTreeSortable         *sortable,
                               gint                     sort_column_id,
                               GtkSortType              order)
{
    gtk_tree_sortable_set_sort_column_id (
            GTK_TREE_SORTABLE (DONNA_TREE_STORE (sortable)->priv->store),
            sort_column_id, order);
}

/* XXX: so this is a "bad" implementation of GtkTreeSortable, because since we
 * just "relay" to our internal GtkTreeStore, when the sort_func is called, it
 * will receive said GtkTreeStore as model, and not our store as it should/would
 * be expected.
 * But, since this store is only used in DonnaTreeView and we know about this,
 * and I'm lazy, it stays like this (for now?). */
static void
tree_store_set_sort_func (GtkTreeSortable         *sortable,
                          gint                     sort_column_id,
                          GtkTreeIterCompareFunc   sort_func,
                          gpointer                 data,
                          GDestroyNotify           destroy)
{
    gtk_tree_sortable_set_sort_func (
            GTK_TREE_SORTABLE (DONNA_TREE_STORE (sortable)->priv->store),
            sort_column_id, sort_func, data, destroy);
}

static void
tree_store_set_default_sort_func (GtkTreeSortable         *sortable,
                                  GtkTreeIterCompareFunc   sort_func,
                                  gpointer                 data,
                                  GDestroyNotify           destroy)
{
    gtk_tree_sortable_set_default_sort_func (
            GTK_TREE_SORTABLE (DONNA_TREE_STORE (sortable)->priv->store),
            sort_func, data, destroy);
}

static gboolean
tree_store_has_default_sort_func (GtkTreeSortable         *sortable)
{
    return gtk_tree_sortable_has_default_sort_func (
            GTK_TREE_SORTABLE (DONNA_TREE_STORE (sortable)->priv->store));
}


#ifdef GTK_IS_JJK
/* GtkTreeBoxable */

static gint
tree_store_get_box_column (GtkTreeBoxable          *boxable)
{
    return gtk_tree_boxable_get_box_column (
            (GtkTreeBoxable *) ((DonnaTreeStore *) boxable)->priv->store);
}

static gboolean
tree_store_set_box_column (GtkTreeBoxable          *boxable,
                           gint                     column)
{
    return gtk_tree_boxable_set_box_column (
            (GtkTreeBoxable *) ((DonnaTreeStore *) boxable)->priv->store,
            column);
}

static gboolean
tree_store_get_current_box_info (GtkTreeBoxable          *boxable,
                                 gchar                  **box,
                                 gint                    *depth,
                                 GtkTreeIter             *iter)
{
    return gtk_tree_boxable_get_current_box_info (
            (GtkTreeBoxable *) ((DonnaTreeStore *) boxable)->priv->store,
            box, depth, iter);
}

static gboolean
tree_store_get_in_box_info (GtkTreeBoxable          *boxable,
                            GtkTreeIter             *iter_box,
                            gchar                  **box,
                            gint                    *depth,
                            GtkTreeIter             *iter)
{
    return gtk_tree_boxable_get_in_box_info (
            (GtkTreeBoxable *) ((DonnaTreeStore *) boxable)->priv->store,
            iter_box, box, depth, iter);
}

static gboolean
tree_store_get_main_box_info (GtkTreeBoxable          *boxable,
                              GtkTreeIter             *iter_box,
                              gchar                  **box,
                              gint                    *depth,
                              GtkTreeIter             *iter)
{
    return gtk_tree_boxable_get_main_box_info (
            (GtkTreeBoxable *) ((DonnaTreeStore *) boxable)->priv->store,
            iter_box, box, depth, iter);
}
#endif


/* API */

static void
tree_store_sort_column_changed (GtkTreeSortable *_sortable,
                                DonnaTreeStore *store)
{
    /* we just re-emit this signal */
    gtk_tree_sortable_sort_column_changed (GTK_TREE_SORTABLE (store));
}

static void
tree_store_row_inserted (GtkTreeModel   *_model,
                         GtkTreePath    *_path,
                         GtkTreeIter    *iter,
                         DonnaTreeStore *store)
{
    DonnaTreeStorePrivate *priv = store->priv;
    gboolean is_visible;

    /* since insert are done directly to the GtkTreeStore, we need to do a few
     * things here :
     * 1. calculate visibility, and add it to our hashtable
     * 2. if visible, emit our own row-inserted signal
     * 3. if visible, check if this is the first (visible) child and if so,
     *    emit row-has-child-toggled
     */

    is_visible = (priv->is_visible)
        ? priv->is_visible (store, iter, priv->is_visible_data)
        : TRUE;

    g_hash_table_insert (priv->hashtable, iter->user_data,
            GINT_TO_POINTER (is_visible));

    if (is_visible)
    {
        GtkTreeModel *model = GTK_TREE_MODEL (store);
        GtkTreePath *path;
        GtkTreeIter parent;

        path = tree_store_get_path (model, iter);
        gtk_tree_model_row_inserted (model, path, iter);

        if (gtk_tree_model_iter_parent (GTK_TREE_MODEL (priv->store),
                    &parent, iter))
        {
            GtkTreeIter child;
            gboolean emit = TRUE;

            /* since we have a parent, let's see if we just added the first
             * (visible) child */
            tree_store_iter_children (model, &child, &parent);
            do
            {
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Waddress"
                if (child.user_data != iter->user_data
                        && iter_is_visible (&child))
#pragma GCC diagnostic pop
                {
                    /* another visible child */
                    emit = FALSE;
                    break;
                }
            } while (tree_store_iter_next (model, &child));

            if (emit)
            {
                gtk_tree_path_up (path);
                gtk_tree_model_row_has_child_toggled (model, path, &parent);
            }
        }

        gtk_tree_path_free (path);
    }
}

static void
tree_store_rows_reordered (GtkTreeModel     *_model,
                           GtkTreePath      *_path,
                           GtkTreeIter      *iter,
                           gint             *_new_order,
                           DonnaTreeStore   *store)
{
    DonnaTreeStorePrivate *priv = store->priv;
    GtkTreeIter it;
    GtkTreePath *path;
    gint *convert;
    gint *new_order;
    gint _n, n;
    gint i;

    /* we can have a valid iter that actually points to the hidden/never exposed
     * root of the store, i.e. one that's not in the treeview, or in our
     * hashtable of visibility. Fix this by using NULL to refer to it. */
    if (iter && gtk_tree_path_get_indices (_path) == NULL)
        iter = NULL;

    /* iter not visible == we don't care */
    if (iter && !iter_is_visible (iter))
        return;

    _n = gtk_tree_model_iter_n_children (_model, iter);
    g_return_if_fail (_n > 0);

    /* create convertion table */
    convert = g_new (gint, (gsize) _n);
    i = n = 0;
    gtk_tree_model_iter_children (_model, &it, iter);
    do
    {
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Waddress"
        convert[i++] = (iter_is_visible (&it)) ? n++ : -1;
#pragma GCC diagnostic pop
    } while (gtk_tree_model_iter_next (_model, &it));

    /* any difference? */
    if (n != _n)
    {
        /* create new new_order */
        new_order = g_new (gint, (gsize) n);
        n = 0;
        for (i = 0; i < _n; ++i)
            if (convert[_new_order[i]] != -1)
                new_order[n++] = convert[_new_order[i]];
    }
    else
        /* all children visible, same new_order */
        new_order = _new_order;

    /* emit our signal */
    if (iter)
        path = tree_store_get_path (GTK_TREE_MODEL (store), iter);
    else
        path = _path;
    gtk_tree_model_rows_reordered (GTK_TREE_MODEL (store), path, iter, new_order);
    if (iter)
        gtk_tree_path_free (path);

    g_free (convert);
    if (new_order != _new_order)
        g_free (new_order);
}

DonnaTreeStore *
donna_tree_store_new (gint n_columns,
                      ...)
{
    DonnaTreeStore *store;
    DonnaTreeStorePrivate *priv;
    GType _types[10], *types;
    va_list va_args;
    gint i;

    g_return_val_if_fail (n_columns > 0, NULL);

    store = g_object_new (DONNA_TYPE_TREE_STORE, NULL);
    priv = store->priv;

    if (n_columns > 10)
        types = g_new (GType, (gsize) n_columns);
    else
        types = _types;

    va_start (va_args, n_columns);
    for (i = 0; i < n_columns; ++i)
        types[i] = va_arg (va_args, GType);
    va_end (va_args);
    priv->store = gtk_tree_store_newv (n_columns, types);

    if (types != _types)
        g_free (types);

    /* GtkTreeSortable: we need to re-emit this signal */
    g_signal_connect (priv->store, "sort-column-changed",
            G_CALLBACK (tree_store_sort_column_changed), store);
    /* GtkTreeModel: we need to re-emit/listen to some signals */
    g_signal_connect (priv->store, "row-inserted",
            G_CALLBACK (tree_store_row_inserted), store);
    g_signal_connect (priv->store, "rows-reordered",
            G_CALLBACK (tree_store_rows_reordered), store);

    return store;
}

void
donna_tree_store_set (DonnaTreeStore     *store,
                      GtkTreeIter        *iter,
                      ...)
{
    DonnaTreeStorePrivate *priv;
    va_list va_args;

    g_return_if_fail (DONNA_IS_TREE_STORE (store));
    priv = store->priv;

    va_start (va_args, iter);
    gtk_tree_store_set_valist (store->priv->store, iter, va_args);
    va_end (va_args);

    if (iter_is_visible (iter))
    {
        GtkTreeModel *model = GTK_TREE_MODEL (store);
        GtkTreePath *path;

        path = tree_store_get_path (model, iter);
        gtk_tree_model_row_changed (model, path, iter);
        gtk_tree_path_free (path);
    }
}

/* remove the given iter, all children & siblings from hashtable */
static void
remove_from_hashtable (GHashTable *ht, GtkTreeModel *_model, GtkTreeIter *iter)
{
    GtkTreeIter it;

    if (gtk_tree_model_iter_children (_model, &it, iter))
        remove_from_hashtable (ht, _model, &it);

    it = *iter;
    do
    {
        g_hash_table_remove (ht, it.user_data);
    } while (gtk_tree_model_iter_next (_model, &it));
}

gboolean
donna_tree_store_remove (DonnaTreeStore     *store,
                         GtkTreeIter        *iter)
{
    DonnaTreeStorePrivate *priv;
    GtkTreeModel *model = (GtkTreeModel *) store;
    GtkTreeModel *_model;
    GtkTreePath *path = NULL;
    GtkTreeIter parent;
    GtkTreeIter it;
    gboolean ret;

    g_return_val_if_fail (DONNA_IS_TREE_STORE (store), FALSE);
    priv = store->priv;
    _model = (GtkTreeModel *) priv->store;

    if (iter_is_visible (iter))
    {
        /* get the parent, for row-has-child-toggled */
        gtk_tree_model_iter_parent (_model, &parent, iter);
        /* get our path now that we can */
        path = tree_store_get_path (model, iter);
    }

    /* remove from hashtable */
    g_hash_table_remove (priv->hashtable, iter->user_data);
    /* also remove all children, as they'll be removed from the store */
    if (gtk_tree_model_iter_children (_model, &it, iter))
        remove_from_hashtable (priv->hashtable, _model, &it);

    /* ret does NOT mean iter was removed, but that iter is still valid and set
     * to the next iter. FIXME: We just assume removal was done */
    ret = gtk_tree_store_remove (store->priv->store, iter);

    if (path)
    {
        /* emit signal */
        gtk_tree_model_row_deleted (model, path);

        /* if there are no more (visible) children (iter's siblings), we need to
         * emit row-has-child-toggled as well */
        if (parent.stamp != 0 && !tree_store_iter_has_child (model, &parent))
        {
            gtk_tree_path_up (path);
            gtk_tree_model_row_has_child_toggled (model, path, &parent);
        }

        gtk_tree_path_free (path);
    }

    return ret;
}

gboolean
donna_tree_store_is_ancestor (DonnaTreeStore     *store,
                              GtkTreeIter        *iter,
                              GtkTreeIter        *descendant)
{
    g_return_val_if_fail (DONNA_IS_TREE_STORE (store), FALSE);
    return gtk_tree_store_is_ancestor (store->priv->store, iter, descendant);
}

gint
donna_tree_store_iter_depth (DonnaTreeStore     *store,
                             GtkTreeIter        *iter)
{
    g_return_val_if_fail (DONNA_IS_TREE_STORE (store), 0);
    return gtk_tree_store_iter_depth (store->priv->store, iter);
}

static gboolean
remove_iter (DonnaTreeStore *store, GtkTreeIter *iter)
{
    DonnaTreeStorePrivate *priv = store->priv;
    GtkTreeModel *_model = GTK_TREE_MODEL (priv->store);
    GtkTreeIter child;

    if (gtk_tree_model_iter_children (_model, &child, iter))
        while (remove_iter (store, &child))
            ;

    if (iter)
        return donna_tree_store_remove (store, iter);

    return FALSE;
}

void
donna_tree_store_clear (DonnaTreeStore     *store)
{
    g_return_if_fail (DONNA_IS_TREE_STORE (store));
    /* we need to implement this on our own (instead of calling
     * gtk_tree_store_clear) so we can handle the row-deleted signals properly,
     * dealing with iter's visibility */
    remove_iter (store, NULL);
}


/* unlike our implementation of GtkTreeModel interface above, those work on
 * *all* iters, visible or not. So yes, they just call GtkTreeStore's
 * implementation of GtkTreeModel */

gboolean
donna_tree_store_iter_next (DonnaTreeStore     *store,
                            GtkTreeIter        *iter)
{
    g_return_val_if_fail (DONNA_IS_TREE_STORE (store), FALSE);
    return gtk_tree_model_iter_next (GTK_TREE_MODEL (store->priv->store),
            iter);
}

gboolean
donna_tree_store_iter_previous (DonnaTreeStore     *store,
                                GtkTreeIter        *iter)
{
    g_return_val_if_fail (DONNA_IS_TREE_STORE (store), FALSE);
    return gtk_tree_model_iter_previous (GTK_TREE_MODEL (store->priv->store),
            iter);
}

gboolean
donna_tree_store_iter_children (DonnaTreeStore     *store,
                                GtkTreeIter        *iter,
                                GtkTreeIter        *parent)
{
    g_return_val_if_fail (DONNA_IS_TREE_STORE (store), FALSE);
    return gtk_tree_model_iter_children (GTK_TREE_MODEL (store->priv->store),
            iter, parent);
}

gboolean
donna_tree_store_iter_has_child (DonnaTreeStore     *store,
                                 GtkTreeIter        *iter)
{
    g_return_val_if_fail (DONNA_IS_TREE_STORE (store), FALSE);
    return gtk_tree_model_iter_has_child (GTK_TREE_MODEL (store->priv->store),
            iter);
}

gint
donna_tree_store_iter_n_children (DonnaTreeStore     *store,
                                  GtkTreeIter        *iter)
{
    g_return_val_if_fail (DONNA_IS_TREE_STORE (store), 0);
    return gtk_tree_model_iter_n_children (GTK_TREE_MODEL (store->priv->store),
            iter);
}

gboolean
donna_tree_store_iter_nth_child (DonnaTreeStore     *store,
                                 GtkTreeIter        *iter,
                                 GtkTreeIter        *parent,
                                 gint                n)
{
    g_return_val_if_fail (DONNA_IS_TREE_STORE (store), FALSE);
    return gtk_tree_model_iter_nth_child (GTK_TREE_MODEL (store->priv->store),
            iter, parent, n);
}

gboolean
donna_tree_store_iter_parent (DonnaTreeStore     *store,
                              GtkTreeIter        *iter,
                              GtkTreeIter        *child)
{
    g_return_val_if_fail (DONNA_IS_TREE_STORE (store), FALSE);
    return gtk_tree_model_iter_parent (GTK_TREE_MODEL (store->priv->store),
            iter, child);
}

void
donna_tree_store_foreach (DonnaTreeStore         *store,
                          GtkTreeModelForeachFunc func,
                          gpointer                data)
{
    g_return_if_fail (DONNA_IS_TREE_STORE (store));
    gtk_tree_model_foreach ((GtkTreeModel *) store->priv->store, func, data);
}

/* extension to GtkTreeModel, version for *all* iters */

gint
donna_tree_store_get_count (DonnaTreeStore     *store)
{
    g_return_val_if_fail (DONNA_IS_TREE_STORE (store), FALSE);
    return (gint) g_hash_table_size (store->priv->hashtable);
}


/* finally some DonnaTreeStore specific stuff */

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

gboolean
donna_tree_store_iter_is_visible (DonnaTreeStore     *store,
                                  GtkTreeIter        *iter)
{
    DonnaTreeStorePrivate *priv;

    g_return_val_if_fail (DONNA_IS_TREE_STORE (store), FALSE);
    priv = store->priv;
    return iter_is_visible (iter);
}

static void
hide_in_hashtable (GHashTable *ht, GtkTreeModel *_model, GtkTreeIter *iter)
{
    GtkTreeIter it;

    if (gtk_tree_model_iter_children (_model, &it, iter))
        hide_in_hashtable (ht, _model, &it);

    it = *iter;
    do
    {
        g_hash_table_insert (ht, it.user_data, NULL);
    } while (gtk_tree_model_iter_next (_model, &it));
}

static void
ensure_visible (DonnaTreeStore *store, GtkTreeIter *iter)
{
    DonnaTreeStorePrivate *priv = store->priv;
    GtkTreeIter it;

    if (gtk_tree_model_iter_parent (GTK_TREE_MODEL (priv->store), &it, iter))
        /* if parent isn't visible, recurse to make sure it becomes visible */
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Waddress"
        if (!iter_is_visible (&it))
#pragma GCC diagnostic pop
            ensure_visible (store, &it);

    if (!iter_is_visible (iter))
    {
        GtkTreePath *path;

        g_hash_table_insert (priv->hashtable, iter->user_data,
                GINT_TO_POINTER (TRUE));
        path = tree_store_get_path (GTK_TREE_MODEL (store), iter);
        gtk_tree_model_row_inserted (GTK_TREE_MODEL (store), path, iter);
        gtk_tree_path_free (path);
    }
}

gboolean
donna_tree_store_refresh_visibility (DonnaTreeStore     *store,
                                     GtkTreeIter        *iter,
                                     gboolean           *was_visible)
{
    DonnaTreeStorePrivate *priv;
    GtkTreeModel *model = (GtkTreeModel *) store;
    gboolean old, new;

    g_return_val_if_fail (DONNA_IS_TREE_STORE (store), FALSE);
    g_return_val_if_fail (iter != NULL, FALSE);

    priv = store->priv;

    old = iter_is_visible (iter);
    new = (priv->is_visible)
        ? priv->is_visible (store, iter, priv->is_visible_data)
        : TRUE;

    if (old != new)
    {
        GtkTreeIter parent;
        gint nb_children;
        gboolean has_child_toggled;

        /* get the parent row. We ask the "real" model because iter might not
         * (yet) be visible, so asking our interface would then fail. */
        gtk_tree_model_iter_parent ((GtkTreeModel *) priv->store, &parent, iter);
        nb_children = gtk_tree_model_iter_n_children (
                (GtkTreeModel *) priv->store,
                (parent.stamp == 0) ? NULL : &parent);

        if (old)
        {
            GtkTreePath *path;
            GtkTreeIter it;

            /* get the path before updating, otherwise we couldn't get a path */
            path = tree_store_get_path (model, iter);
            /* this is to allow DonnaTreeView to move the focus if it is the the
             * about-to-be-deleted row, to avoid GTK's default behavior of doing
             * a set_cursor(). This requires access to the model (to move
             * next/prev) but also the view *before* the row has been removed.
             * Simply processing row-deleted before isn't possible, because the
             * view is then in an "invalid" state, since it hasn't yet processed
             * the row removal, and its paths don't point where they should... */
            g_signal_emit (store, tree_store_signals[ROW_FAKE_DELETED], 0,
                    path, iter);

            /* update hashtable, as we can now assume all children are not
             * visible as well (no need to emit row-deleted for them) */
            g_hash_table_insert (priv->hashtable, iter->user_data, NULL);
            if (gtk_tree_model_iter_children ((GtkTreeModel *) priv->store,
                        &it, iter))
                hide_in_hashtable (priv->hashtable,
                        (GtkTreeModel *) priv->store, &it);

            /* now that the row is "officially" gone, emit the signal */
            gtk_tree_model_row_deleted (model, path);
            gtk_tree_path_free (path);

            /* did the last child go away? */
            has_child_toggled = nb_children == 1;
        }
        else
        {
            /* make sure all parents are visible, if not switch them & emit the
             * row-inserted signal for them (including iter) */
            ensure_visible (store, iter);

            /* did we add the first child? */
            has_child_toggled = nb_children == 0;
        }

        if (has_child_toggled)
        {
            GtkTreePath *path;

            path = tree_store_get_path (model, &parent);
            gtk_tree_model_row_has_child_toggled (model, path, &parent);
            gtk_tree_path_free (path);
        }
    }

    if (was_visible)
        *was_visible = old;

    return new;
}

static void
refilter (DonnaTreeStore *store, GtkTreeIter *iter)
{
    GtkTreeModel *_model = GTK_TREE_MODEL (store->priv->store);
    GtkTreeIter it;

    if (!gtk_tree_model_iter_children (_model, &it, iter))
        return;

    do
    {
        if (donna_tree_store_refresh_visibility (store, &it, NULL))
            refilter (store, &it);
    } while (gtk_tree_model_iter_next (_model, &it));
}

void
donna_tree_store_refilter (DonnaTreeStore     *store,
                           GtkTreeIter        *iter)
{
    g_return_if_fail (DONNA_IS_TREE_STORE (store));
    if (iter)
        donna_tree_store_refresh_visibility (store, iter, NULL);
    refilter (store, iter);
}

GtkTreeStore *
donna_tree_store_get_store (DonnaTreeStore *store)
{
    g_return_val_if_fail (DONNA_IS_TREE_STORE (store), NULL);
    /* we really shouldn't expose this, but it would be a PITA to implement our
     * own insert_with_values() because there's no function using a valist; So
     * we would have to provide an array of gint (columns) and an array of
     * GValue (values), which implies we need to have cached or ask for the type
     * of each columns, etc
     * Lazy: let's return the store, and handle the row-inserted signal */
    return store->priv->store;
}