
#ifndef __FSNODE_H__
#define __FSNODE_H__

G_BEGIN_DECLS

#define FSNODE_ERROR            g_quark_from_static_string ("FsNode-Error")
typedef enum
{
    FSNODE_ERROR_NOMEM,
    FSNODE_ERROR_ALREADY_EXISTS,
    FSNODE_ERROR_NOT_FOUND,
    FSNODE_ERROR_READ_ONLY,
    FSNODE_ERROR_INVALID_TYPE,
    FSNODE_ERROR_OTHER
} FsNodeError;

#define TYPE_FSNODE             (fsnode_get_type ())
#define FSNODE(obj)             (G_TYPE_CHECK_INSTANCE_CAST ((obj), TYPE_FSNODE, FsNode))
#define FSNODE_CLASS(klass)     (G_TYPE_CHECK_CLASS_CAST ((klass), TYPE_FSNODE, FsNodeClass))
#define IS_FSNODE(obj)          (G_TYPE_CHECK_INSTANCE_TYPE ((obj), TYPE_FSNODE))
#define IS_FSNODE_CLASS(klass)  (G_TYPE_CHECK_CLASS_TYPE ((klass), TYPE_FSNODE))
#define FSNODE_GET_CLASS(obj)   (G_TYPE_INSTANCE_GET_CLASS ((obj), TYPE_FSNODE, FsNodeClass))

typedef struct _FsNode          FsNode;
typedef struct _FsNodeClass     FsNodeClass;
typedef struct _FsNodePrivate   FsNodePrivate;

/* function used from set_value_fn implementations, to update values */
typedef void        (*set_fn)       (FsNode         *node,
                                     const gchar    *name,
                                     GValue         *value);
/* functions called by a node to get/set a property value */
typedef gboolean    (*get_value_fn) (FsNode         *node,
                                     const gchar    *name,
                                     set_fn          set_value,
                                     GError        **error);
typedef gboolean    (*set_value_fn) (FsNode         *node,
                                     const gchar    *name,
                                     set_fn          set_value,
                                     GValue         *value,
                                     GError        **error);

struct _FsNode
{
    GObject parent;

    FsNodePrivate *priv;
};

struct _FsNodeClass
{
    GObjectClass parent;
};

GType           fsnode_get_type         (void) G_GNUC_CONST;

FsNode *        fsnode_new              (FsProvider      *provider,
                                         const gchar     *location);
FsNode *        fsnode_new_from_node    (FsProvider      *provider,
                                         const gchar     *location,
                                         FsNode          *sce);
FsProvider *    fsnode_get_provider     (FsNode          *node);
const gchar *   fsnode_get_location     (FsNode          *node);
gboolean        fsnode_add_property     (FsNode          *node,
                                         gchar           *name,
                                         GType            type,
                                         GValue          *value,
                                         get_value_fn     get_value,
                                         set_value_fn     set_value,
                                         GError         **error);
gboolean        fsnode_set_property     (FsNode          *node,
                                         const gchar     *name,
                                         GValue          *value,
                                         GError         **error);
void            fsnode_get              (FsNode          *node,
                                         GError         **error,
                                         const gchar     *first_property_name,
                                         ...);
void            fsnode_refresh          (FsNode          *node);
void            fsnode_add_iter         (FsNode          *node,
                                         GtkTreeIter     *iter);
gboolean        fsnode_remove_iter      (FsNode          *node,
                                         GtkTreeIter     *iter);
GtkTreeIter **  fsnode_get_iters        (FsNode          *node);

G_END_DECLS

#endif /* __FSNODE_H__ */

