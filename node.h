
#ifndef __DONNA_NODE_H__
#define __DONNA_NODE_H__

#include <gdk/gdk.h>
#include "common.h"
#include "task.h"

G_BEGIN_DECLS

#define DONNA_NODE_ERROR            g_quark_from_static_string ("DonnaNode-Error")
typedef enum
{
    DONNA_NODE_ERROR_NOMEM,
    DONNA_NODE_ERROR_ALREADY_EXISTS,
    DONNA_NODE_ERROR_NOT_FOUND,
    DONNA_NODE_ERROR_READ_ONLY,
    DONNA_NODE_ERROR_INVALID_TYPE,
    DONNA_NODE_ERROR_OTHER
} DonnaNodeError;

typedef enum
{
    DONNA_NODE_ITEM         = (1 << 0),
    DONNA_NODE_CONTAINER    = (1 << 1),
} DonnaNodeType;

typedef enum
{
    DONNA_NODE_VALUE_NONE = 0,
    DONNA_NODE_VALUE_NEED_REFRESH,
    DONNA_NODE_VALUE_SET,
    DONNA_NODE_VALUE_ERROR
} DonnaNodeHasValue;

#define DONNA_NODE_REFRESH_SET_VALUES       NULL
#define DONNA_NODE_REFRESH_ALL_VALUES       "-all"

extern const gchar *node_basic_properties[];

typedef enum
{
    /* PROVIDER, DOMAIN, LOCATION, NODE_TYPE are internal/always exist */
    /* NAME is required/always exists */
    DONNA_NODE_ICON_EXISTS          = (1 << 0),
    DONNA_NODE_FULL_NAME_EXISTS     = (1 << 1),
    DONNA_NODE_SIZE_EXISTS          = (1 << 2),
    DONNA_NODE_CTIME_EXISTS         = (1 << 3),
    DONNA_NODE_MTIME_EXISTS         = (1 << 4),
    DONNA_NODE_ATIME_EXISTS         = (1 << 5),
    DONNA_NODE_PERMS_EXISTS         = (1 << 6),
    DONNA_NODE_USER_EXISTS          = (1 << 7),
    DONNA_NODE_GROUP_EXISTS         = (1 << 8),
    DONNA_NODE_TYPE_EXISTS          = (1 << 9),

    DONNA_NODE_NAME_WRITABLE        = (1 << 10),
    DONNA_NODE_ICON_WRITABLE        = (1 << 11),
    DONNA_NODE_FULL_NAME_WRITABLE   = (1 << 12),
    DONNA_NODE_SIZE_WRITABLE        = (1 << 13),
    DONNA_NODE_CTIME_WRITABLE       = (1 << 14),
    DONNA_NODE_MTIME_WRITABLE       = (1 << 15),
    DONNA_NODE_ATIME_WRITABLE       = (1 << 16),
    DONNA_NODE_PERMS_WRITABLE       = (1 << 17),
    DONNA_NODE_USER_WRITABLE        = (1 << 18),
    DONNA_NODE_GROUP_WRITABLE       = (1 << 19),
    DONNA_NODE_TYPE_WRITABLE        = (1 << 20),

    DONNA_NODE_ALL_EXISTS           = (DONNA_NODE_ICON_EXISTS
        | DONNA_NODE_FULL_NAME_EXISTS | DONNA_NODE_SIZE_EXISTS
        | DONNA_NODE_CTIME_EXISTS | DONNA_NODE_MTIME_EXISTS
        | DONNA_NODE_ATIME_EXISTS | DONNA_NODE_PERMS_EXISTS
        | DONNA_NODE_USER_EXISTS  | DONNA_NODE_GROUP_EXISTS
        | DONNA_NODE_TYPE_EXISTS)
} DonnaNodeFlags;

typedef enum
{
    DONNA_NODE_PROP_UNKNOWN     = (1 << 0),
    DONNA_NODE_PROP_NONE        = (1 << 1),
    DONNA_NODE_PROP_EXISTS      = (1 << 2),
    DONNA_NODE_PROP_HAS_VALUE   = (1 << 3),
    DONNA_NODE_PROP_WRITABLE    = (1 << 4),
} DonnaNodeHasProp;

/* functions called by a node to refresh/set a property value */
typedef gboolean    (*refresher_fn) (DonnaTask      *task,
                                     DonnaNode      *node,
                                     const gchar    *name);
typedef DonnaTaskState (*setter_fn) (DonnaTask      *task,
                                     DonnaNode      *node,
                                     const gchar    *name,
                                     const GValue   *value);
struct _DonnaNode
{
    GObject parent;

    DonnaNodePrivate *priv;
};

struct _DonnaNodeClass
{
    GObjectClass parent;
};

DonnaNode *     donna_node_new              (DonnaProvider          *provider,
                                             const gchar            *location,
                                             DonnaNodeType           node_type,
                                             refresher_fn            refresher,
                                             setter_fn               setter,
                                             const gchar            *name,
                                             DonnaNodeFlags          flags);
DonnaNode *     donna_node_new_from_node    (DonnaProvider          *provider,
                                             const gchar            *location,
                                             DonnaNode              *source_node,
                                             GError                **error);
gboolean        donna_node_add_property     (DonnaNode              *node,
                                             const gchar            *name,
                                             GType                   type,
                                             const GValue           *value,
                                             refresher_fn            refresher,
                                             setter_fn               setter,
                                             GError                **error);
DonnaNodeHasProp donna_node_has_property    (DonnaNode              *node,
                                             const gchar            *name);
void            donna_node_get              (DonnaNode              *node,
                                             gboolean                is_blocking,
                                             const gchar            *first_name,
                                             ...);
DonnaProvider * donna_node_get_provider     (DonnaNode              *node);
const gchar *   donna_node_get_domain       (DonnaNode              *node);
gchar *         donna_node_get_location     (DonnaNode              *node);
DonnaNodeType   donna_node_get_node_type    (DonnaNode              *node);
gchar *         donna_node_get_name         (DonnaNode              *node);
DonnaNodeHasValue donna_node_get_icon       (DonnaNode              *node,
                                             gboolean                is_blocking,
                                             GdkPixbuf             **icon);
DonnaNodeHasValue donna_node_get_full_name  (DonnaNode              *node,
                                             gboolean                is_blocking,
                                             gchar                 **full_name);
DonnaNodeHasValue donna_node_get_size       (DonnaNode              *node,
                                             gboolean                is_blocking,
                                             guint                  *size);
DonnaNodeHasValue donna_node_get_ctime      (DonnaNode              *node,
                                             gboolean                is_blocking,
                                             gint64                 *ctime);
DonnaNodeHasValue donna_node_get_mtime      (DonnaNode              *node,
                                             gboolean                is_blocking,
                                             gint64                 *mtime);
DonnaNodeHasValue donna_node_get_atime      (DonnaNode              *node,
                                             gboolean                is_blocking,
                                             gint64                 *atime);
DonnaNodeHasValue donna_node_get_perms      (DonnaNode              *node,
                                             gboolean                is_blocking,
                                             guint                  *perms);
DonnaNodeHasValue donna_node_get_user       (DonnaNode              *node,
                                             gboolean                is_blocking,
                                             gchar                 **user);
DonnaNodeHasValue donna_node_get_group      (DonnaNode              *node,
                                             gboolean                is_blocking,
                                             gchar                 **user);
DonnaNodeHasValue donna_node_get_Type       (DonnaNode              *node,
                                             gboolean                is_blocking,
                                             gchar                 **user);
DonnaTask *     donna_node_refresh_task     (DonnaNode              *node,
                                             const gchar            *first_name,
                                             ...);
DonnaTask *     donna_node_refresh_arr_task (DonnaNode              *node,
                                             GPtrArray              *props);
DonnaTask *     donna_node_set_property_task    (DonnaNode          *node,
                                                 const gchar        *name,
                                                 const GValue       *value,
                                                 GError            **error);
void            donna_node_set_property_value   (DonnaNode          *node,
                                                 const gchar        *name,
                                                 const GValue       *value);
int             donna_node_inc_toggle_count (DonnaNode              *node);
int             donna_node_dec_toggle_count (DonnaNode              *node);

G_END_DECLS

#endif /* __DONNA_NODE_H__ */
