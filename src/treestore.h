
#ifndef __DONNA_TREE_STORE_H__
#define __DONNA_TREE_STORE_H__

#include <gtk/gtk.h>

G_BEGIN_DECLS

typedef struct _DonnaTreeStore          DonnaTreeStore;
typedef struct _DonnaTreeStorePrivate   DonnaTreeStorePrivate;
typedef struct _DonnaTreeStoreClass     DonnaTreeStoreClass;

#define DONNA_TYPE_TREE_STORE           (donna_tree_store_get_type ())
#define DONNA_TREE_STORE(obj)           (G_TYPE_CHECK_INSTANCE_CAST ((obj), DONNA_TYPE_TREE_STORE, DonnaTreeStore))
#define DONNA_TREE_STORE_CLASS(klass)   (G_TYPE_CHECK_CLASS_CAST ((klass), DONNA_TYPE_TREE_STORE, DonnaTreeStoreClass))
#define DONNA_IS_TREE_STORE(obj)        (G_TYPE_CHECK_INSTANCE_TYPE ((obj), DONNA_TYPE_TREE_STORE))
#define DONNA_IS_TREE_STORE_CLASS(klass)(G_TYPE_CHECK_CLASS_TYPE ((obj), DONNA_TYPE_TREE_STORE))
#define DONNA_TREE_STORE_GET_CLASS(obj) (G_TYPE_INSTANCE_GET_CLASS ((obj), DONNA_TYPE_TREE_STORE, DonnaTreeStoreClass))

GType   donna_tree_store_get_type       (void) G_GNUC_CONST;

struct _DonnaTreeStore
{
    /*< private >*/
    GtkTreeStore             treestore;
    DonnaTreeStorePrivate   *priv;
};

struct _DonnaTreeStoreClass
{
    GtkTreeStoreClass parent_class;

    /* signals */
    void        (*row_fake_deleted)             (DonnaTreeStore     *store,
                                                 GtkTreePath        *path,
                                                 GtkTreeIter        *iter);
};

typedef gboolean    (*store_visible_fn)         (DonnaTreeStore     *store,
                                                 GtkTreeIter        *iter,
                                                 gpointer            data);

/* API */

/* same as GtkTreeStore */
DonnaTreeStore *donna_tree_store_new            (gint n_columns,
                                                 ...);
void            donna_tree_store_set            (DonnaTreeStore     *store,
                                                 GtkTreeIter        *iter,
                                                 ...);
gboolean        donna_tree_store_remove         (DonnaTreeStore     *store,
                                                 GtkTreeIter        *iter);
#define donna_tree_store_insert_with_values(store, iter, parent, position, ...) \
        gtk_tree_store_insert_with_values (donna_tree_store_get_store (store), \
                iter, parent, position, __VA_ARGS__)
gboolean        donna_tree_store_is_ancestor    (DonnaTreeStore     *store,
                                                 GtkTreeIter        *iter,
                                                 GtkTreeIter        *descendant);
gint            donna_tree_store_iter_depth     (DonnaTreeStore     *store,
                                                 GtkTreeIter        *iter);
void            donna_tree_store_clear          (DonnaTreeStore     *store);

/* extensions to GtkTreeModel */
gboolean        donna_tree_model_iter_next      (GtkTreeModel       *model,
                                                 GtkTreeIter        *iter);
gboolean        donna_tree_model_iter_previous  (GtkTreeModel       *model,
                                                 GtkTreeIter        *iter);
gboolean        donna_tree_model_iter_last      (GtkTreeModel       *model,
                                                 GtkTreeIter        *iter);
gint            donna_tree_model_get_count      (GtkTreeModel       *model);

/* our version of GtkTreeModel interface, on all (visible & invisible) iters */
gboolean        donna_tree_store_iter_next      (DonnaTreeStore     *store,
                                                 GtkTreeIter        *iter);
gboolean        donna_tree_store_iter_previous  (DonnaTreeStore     *store,
                                                 GtkTreeIter        *iter);
gboolean        donna_tree_store_iter_children  (DonnaTreeStore     *store,
                                                 GtkTreeIter        *iter,
                                                 GtkTreeIter        *parent);
gboolean        donna_tree_store_iter_has_child (DonnaTreeStore     *store,
                                                 GtkTreeIter        *iter);
gint            donna_tree_store_iter_n_children(DonnaTreeStore     *store,
                                                 GtkTreeIter        *iter);
gboolean        donna_tree_store_iter_nth_child (DonnaTreeStore     *store,
                                                 GtkTreeIter        *iter,
                                                 GtkTreeIter        *parent,
                                                 gint                n);
gboolean        donna_tree_store_iter_parent    (DonnaTreeStore     *store,
                                                 GtkTreeIter        *iter,
                                                 GtkTreeIter        *child);
void            donna_tree_store_foreach        (DonnaTreeStore     *store,
                                                 GtkTreeModelForeachFunc func,
                                                 gpointer            data);
/* our version of extensions to GtkTreeModel */
gint            donna_tree_store_get_count      (DonnaTreeStore     *store);

/* DonnaTreeStore specific */
gboolean        donna_tree_store_set_visible_func (
                                                 DonnaTreeStore     *store,
                                                 store_visible_fn    is_visible,
                                                 gpointer            data,
                                                 GDestroyNotify      destroy);
gboolean        donna_tree_store_iter_is_visible(DonnaTreeStore     *store,
                                                 GtkTreeIter        *iter);
gboolean        donna_tree_store_refresh_visibility (
                                                 DonnaTreeStore     *store,
                                                 GtkTreeIter        *iter,
                                                 gboolean           *was_visible);
void            donna_tree_store_refilter       (DonnaTreeStore     *store,
                                                 GtkTreeIter        *iter);
GtkTreeStore *  donna_tree_store_get_store      (DonnaTreeStore     *store);

G_END_DECLS

#endif /* __DONNA_TREE_STORE_H__ */
