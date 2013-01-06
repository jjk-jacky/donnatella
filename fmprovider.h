
#ifndef __FMPROVIDER_H__
#define __FMPROVIDER_H__

G_BEGIN_DECLS

#define TYPE_FMPROVIDER                (fmprovider_get_type ())
#define FMPROVIDER(obj)                (G_TYPE_CHECK_INSTANCE_CAST ((obj), TYPE_FMPROVIDER, FmProvider))
#define IS_FMPROVIDER(obj)             (G_TYPE_CHECK_INSTANCE_TYPE ((obj), TYPE_FMPROVIDER))
#define FMPROVIDER_GET_INTERFACE(inst) (G_TYPE_INSTANCE_GET_INTERFACE ((inst), TYPE_FMPROVIDER, FmProviderInterface))

typedef struct _FmProvider              FmProvider; /* dummy typedef */
typedef struct _FmProviderInterface     FmProviderInterface;


struct _FmProviderInterface
{
    GTypeInterface parent;

    /* signals */
    void            (*node_created)             (FmProvider  *provider,
                                                 FmNode      *node);
    void            (*node_removed)             (FmProvider  *provider,
                                                 FmNode      *node);
    void            (*node_location_updated)    (FmProvider  *provider,
                                                 FmNode      *node,
                                                 const gchar *old_location);
    void            (*node_updated)             (FmProvider  *provider,
                                                 FmNode      *node,
                                                 const gchar *name);
    void            (*node_children)            (FmProvider  *provider,
                                                 FmNode      *node,
                                                 FmNode     **children);
    void            (*node_new_child)           (FmProvider  *provider,
                                                 FmNode      *node,
                                                 FmNode      *child);
    void            (*node_new_content)         (FmProvider *provider,
                                                 FmNode      *node,
                                                 FmNode      *content);

    /* virtual table */
    FmNode *        (*get_node)                 (FmProvider  *provider,
                                                 const gchar *location,
                                                 gboolean     is_container,
                                                 GError     **error);
/************
    FmTask *        (*get_node_task)            (FmProvider  *provider,
                                                 const gchar *location,
                                                 gboolean     is_container,
                                                 GCallback    callback,
                                                 gpointer     callback_data
                                                 GError     **error);
********/
    FmNode **       (*get_content)              (FmProvider  *provider,
                                                 FmNode      *node,
                                                 GError     **error);
/**********
    FmTask *        (*get_content_task)         (FmProvider  *provider,
                                                 FmNode      *node,
                                                 GCallback    callback,
                                                 gpointer     callback_data,
                                                 GError     **error);
*************/
    FmNode **       (*get_children)             (FmProvider  *provider,
                                                 FmNode      *node,
                                                 GError     **error);
/**********
    FmTask *        (*get_children_task)        (FmProvider  *provider,
                                                 FmNode      *node,
                                                 GCallback    callback,
                                                 gpointer     callback_data,
                                                 GError     **error);
************/
    gboolean        (*remove_node)              (FmProvider  *provider,
                                                 FmNode      *node,
                                                 GError     **error);
/************
    FmTask *        (*remove_node_task)         (FmProvider  *provider,
                                                 FmNode      *node,
                                                 GCallback    callback,
                                                 gpointer     calback_data,
                                                 GError     **error);
************/
};

GType           fmprovider_get_type             (void) G_GNUC_CONST;

FmNode *        fmprovider_get_node             (FmProvider  *provider,
                                                 const gchar *location,
                                                 gboolean     is_container,
                                                 GError     **error);
/**********
FmTask *        fmprovider_get_node_task        (FmProvider  *provider,
                                                 const gchar *location,
                                                 gboolean     is_container,
                                                 GCallback    callback,
                                                 gpointer     callback_data,
                                                 GError     **error);
*********/
FmNode **       fmprovider_get_content          (FmProvider  *provider,
                                                 FmNode      *node,
                                                 GError     **error);
/*******
FmTask *        fmprovider_get_content_task     (FmProvider  *provider,
                                                 FmNode      *node,
                                                 GCallback    callback,
                                                 gpointer     callback_data,
                                                 GError     **error);
*********/
FmNode **       fmprovider_get_children         (FmProvider  *provider,
                                                 FmNode      *node,
                                                 GError     **error);
/************
FmTask *        fmprovider_get_children_task    (FmProvider  *provider,
                                                 FmNode      *node,
                                                 GCallback    callback,
                                                 gpointer     callback_data,
                                                 GError     **error);
*********/
gboolean        fmprovider_remove_node          (FmProvider  *provider,
                                                 FmNode      *node,
                                                 GError     **error);
/*************
FmTask *        fmprovider_remove_node_task     (FmProvider  *provider,
                                                 FmNode      *node,
                                                 GCallback    callback,
                                                 gpointer     callback_data,
                                                 GError     **error);
**********/

G_END_DECLS

#endif /* __FMPROVIDER_H__ */
