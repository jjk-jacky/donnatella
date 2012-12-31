
#ifndef __FSNODE_H__
#define __FSNODE_H__

G_BEGIN_DECLS

#define TYPE_FSNODE             (fsnode_get_type ())
#define FSNODE(obj)             (G_TYPE_CHECK_INSTANCE_CAST ((obj), TYPE_FSNODE, FsNode))
#define FSNODE_CLASS(klass)     (G_TYPE_CHECK_CLASS_CAST ((klass), TYPE_FSNODE, FsNodeClass))
#define IS_FSNODE(obj)          (G_TYPE_CHECK_INSTANCE_TYPE ((obj), TYPE_FSNODE))
#define IS_FSNODE_CLASS(klass)  (G_TYPE_CHECK_CLASS_TYPE ((klass), TYPE_FSNODE))
#define FSNODE_GET_CLASS(obj)   (G_TYPE_INSTANCE_GET_CLASS ((obj), TYPE_FSNODE, FsNodeClass))

typedef struct _FsNode          FsNode;
typedef struct _FsNodeClass     FsNodeClass;
typedef struct _FsNodePrivate   FsNodePrivate;

typedef gboolean (*get_value_fn) (FsNode *node, const gchar *name, GValue *value);
typedef gboolean (*set_value_fn) (FsNode *node, const gchar *name, const GValue *value);

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

FsNode *        fsnode_new              (void);
gboolean        fsnode_add_property     (FsNode          *node,
                                         const gchar     *name,
                                         GType            type,
                                         GValue          *value,
                                         get_value_fn     get_value,
                                         set_value_fn     set_value,
                                         GError         **error);
void            fsnode_set              (FsNode          *node,
                                         const gchar     *first_property_name,
                                         ...);
void            fsnode_get              (FsNode          *node,
                                         const gchar     *first_property_name,
                                         ...);
void            fsnode_refresh          (FsNode          *node);

G_END_DECLS

#endif /* __FSNODE_H__ */

