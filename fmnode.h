
#ifndef __FMNODE_H__
#define __FMNODE_H__

G_BEGIN_DECLS

#define FMNODE_ERROR            g_quark_from_static_string ("FmNode-Error")
typedef enum
{
    FMNODE_ERROR_NOMEM,
    FMNODE_ERROR_ALREADY_EXISTS,
    FMNODE_ERROR_NOT_FOUND,
    FMNODE_ERROR_READ_ONLY,
    FMNODE_ERROR_INVALID_TYPE,
    FMNODE_ERROR_OTHER
} FmNodeError;

#define TYPE_FMNODE             (fmnode_get_type ())
#define FMNODE(obj)             (G_TYPE_CHECK_INSTANCE_CAST ((obj), TYPE_FMNODE, FmNode))
#define FMNODE_CLASS(klass)     (G_TYPE_CHECK_CLASS_CAST ((klass), TYPE_FMNODE, FmNodeClass))
#define IS_FMNODE(obj)          (G_TYPE_CHECK_INSTANCE_TYPE ((obj), TYPE_FMNODE))
#define IS_FMNODE_CLASS(klass)  (G_TYPE_CHECK_CLASS_TYPE ((klass), TYPE_FMNODE))
#define FMNODE_GET_CLASS(obj)   (G_TYPE_INSTANCE_GET_CLASS ((obj), TYPE_FMNODE, FmNodeClass))

typedef struct _FmNode          FmNode;
typedef struct _FmNodeClass     FmNodeClass;
typedef struct _FmNodePrivate   FmNodePrivate;

/* functions called by a node to get/set a property value */
typedef gboolean    (*get_value_fn) (FmNode         *node,
                                     const gchar    *name,
                                     GError        **error);
typedef gboolean    (*set_value_fn) (FmNode         *node,
                                     const gchar    *name,
                                     GValue         *value,
                                     GError        **error);

struct _FmNode
{
    GObject parent;

    FmNodePrivate *priv;
};

struct _FmNodeClass
{
    GObjectClass parent;
};

GType           fmnode_get_type             (void) G_GNUC_CONST;

FmNode *        fmnode_new                  (FmProvider             *provider,
                                             get_value_fn            location_get,
                                             set_value_fn            location_set,
                                             get_value_fn            is_container_get,
                                             set_value_fn            is_container_set,
                                             get_value_fn            has_children_get,
                                             set_value_fn            has_children_set);
FmNode *        fmnode_new_from_node        (FmProvider             *provider,
                                             get_value_fn            location_get,
                                             set_value_fn            location_set,
                                             get_value_fn            is_container_get,
                                             set_value_fn            is_container_set,
                                             get_value_fn            has_children_get,
                                             set_value_fn            has_children_set,
                                             FmNode                 *sce);
gboolean        fmnode_add_property         (FmNode                 *node,
                                             const gchar            *name,
                                             GType                   type,
                                             GValue                 *value,
                                             get_value_fn            get_value,
                                             set_value_fn            set_value,
                                             GError                **error);
gboolean        fmnode_set_property         (FmNode                 *node,
                                             const gchar            *name,
                                             GValue                 *value,
                                             GError                **error);
/********
FmTask *        fmnode_set_property_task    (FmNode                 *node,
                                             const gchar            *name,
                                             GValue                 *value,
                                             GCallback               callback,
                                             gpointer                callback_data,
                                             GError                **error);
***************/
void            fmnode_get                  (FmNode                 *node,
                                             GError                **error,
                                             const gchar            *first_name,
                                             ...);
void            fmnode_refresh              (FmNode                 *node);
void            fmnode_set_property_value   (FmNode                 *node,
                                             const gchar            *name,
                                             GValue                 *value);

G_END_DECLS

#endif /* __FMNODE_H__ */

