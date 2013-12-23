
#ifndef __DONNA_NODE_H__
#define __DONNA_NODE_H__

#include <gdk/gdk.h>
#include "common.h"
#include "task.h"

G_BEGIN_DECLS

#define DONNA_NODE_ERROR            g_quark_from_static_string ("DonnaNode-Error")
/**
 * DonnaNodeError:
 * @DONNA_NODE_ERROR_ALREADY_EXISTS: Tried to add a property that already exists
 * on the node
 * @DONNA_NODE_ERROR_NOT_FOUND: Property doesn't exist on the node
 * @DONNA_NODE_ERROR_READ_ONLY: Property is read-only / value cannot be set
 * @DONNA_NODE_ERROR_INVALID_TYPE: Property is of a different/non-compatible
 * type than the one given
 * @DONNA_NODE_ERROR_OTHER: Other error
 */
typedef enum
{
    DONNA_NODE_ERROR_ALREADY_EXISTS,
    DONNA_NODE_ERROR_NOT_FOUND,
    DONNA_NODE_ERROR_READ_ONLY,
    DONNA_NODE_ERROR_INVALID_TYPE,
    DONNA_NODE_ERROR_OTHER
} DonnaNodeError;

/**
 * DonnaNodeType:
 * @DONNA_NODE_ITEM: Node is an item (e.g. file)
 * @DONNA_NODE_CONTAINER: Node is a container (e.g. a directory)
 *
 * The type of object the node represents. Containers can have other nodes as
 * children, while items can be triggered (e.g. execute/open a file)
 */
typedef enum
{
    DONNA_NODE_ITEM         = (1 << 0),
    DONNA_NODE_CONTAINER    = (1 << 1),
} DonnaNodeType;

/**
 * DonnaNodeHasValue:
 * @DONNA_NODE_VALUE_NONE: No value has been set (property doesn't exists)
 * @DONNA_NODE_VALUE_NEED_REFRESH: No value set, property needs a refresh
 * @DONNA_NODE_VALUE_SET: Value has been set
 * @DONNA_NODE_VALUE_ERROR: No value set, an error occured while trying to
 * refresh it
 *
 * Whether or not a property's value was retrieved from a node; e.g. using
 * donna_node_get()
 */
typedef enum
{
    DONNA_NODE_VALUE_NONE = 0,
    DONNA_NODE_VALUE_NEED_REFRESH,
    DONNA_NODE_VALUE_SET,
    DONNA_NODE_VALUE_ERROR
} DonnaNodeHasValue;

/**
 * DONNA_NODE_REFRESH_SET_VALUES:
 *
 * To refresh all properties that currently have a value set, using
 * donna_node_refresh_task()
 */
#define DONNA_NODE_REFRESH_SET_VALUES       NULL
/**
 * DONNA_NODE_REFRESH_ALL_VALUES:
 *
 * To refesh all properties, even those where no value has been set yet, using
 * donna_node_refresh_task()
 */
#define DONNA_NODE_REFRESH_ALL_VALUES       "-all"

extern const gchar *node_basic_properties[];

/**
 * DonnaNodeFlags:
 * @DONNA_NODE_ICON_EXISTS: Property "icon" exists
 * @DONNA_NODE_FULL_NAME_EXISTS: Property "full-name" exists
 * @DONNA_NODE_SIZE_EXISTS: Property "size" exists
 * @DONNA_NODE_CTIME_EXISTS: Property "ctime" exists
 * @DONNA_NODE_MTIME_EXISTS: Property "mtime" exists
 * @DONNA_NODE_ATIME_EXISTS: Property "atime" exists
 * @DONNA_NODE_MODE_EXISTS: Property "mode" exists
 * @DONNA_NODE_UID_EXISTS: Property "uid" exists
 * @DONNA_NODE_GID_EXISTS: Property "gid" exists
 * @DONNA_NODE_DESC_EXISTS: Property "desc" exists
 * @DONNA_NODE_NAME_WRITABLE: Property "name" is writable (value can be set/changed)
 * @DONNA_NODE_ICON_WRITABLE: Property "icon" is writable (value can be set/changed)
 * @DONNA_NODE_FULL_NAME_WRITABLE: Property "full-name" is writable (value can be
 * set/changed)
 * @DONNA_NODE_SIZE_WRITABLE: Property "size" is writable (value can be set/changed)
 * @DONNA_NODE_CTIME_WRITABLE: Property "ctime" is writable (value can be
 * set/changed)
 * @DONNA_NODE_MTIME_WRITABLE: Property "mtime" is writable (value can be
 * set/changed)
 * @DONNA_NODE_ATIME_WRITABLE: Property "atime" is writable (value can be
 * set/changed)
 * @DONNA_NODE_MODE_WRITABLE: Property "mode" is writable (value can be set/changed)
 * @DONNA_NODE_UID_WRITABLE: Property "uid" is writable (value can be set/changed)
 * @DONNA_NODE_GID_WRITABLE: Property "gid" is writable (value can be set/changed)
 * @DONNA_NODE_DESC_WRITABLE: Property "desc" is writable (value can be set/changed)
 * @DONNA_NODE_ALL_EXISTS: All basic properties exists
 * @DONNA_NODE_ALL_WRITABLE: All basic properties are writable (values can be
 * set/changed)
 *
 * Flags to define which basic properties exist (and which are writable) when
 * creating a new node.
 *
 * Properties "provider", "domain", "location", "node-type" and "filename" are
 * internals, and as such always exist (but cannot be set). Property "name" is
 * required and always exists as well.
 *
 * This is used by #DonnaProvider<!-- -->s when calling donna_node_new()
 */
typedef enum
{
    DONNA_NODE_ICON_EXISTS          = (1 << 0),
    DONNA_NODE_FULL_NAME_EXISTS     = (1 << 1),
    DONNA_NODE_SIZE_EXISTS          = (1 << 2),
    DONNA_NODE_CTIME_EXISTS         = (1 << 3),
    DONNA_NODE_MTIME_EXISTS         = (1 << 4),
    DONNA_NODE_ATIME_EXISTS         = (1 << 5),
    DONNA_NODE_MODE_EXISTS          = (1 << 6),
    DONNA_NODE_UID_EXISTS           = (1 << 7),
    DONNA_NODE_GID_EXISTS           = (1 << 8),
    DONNA_NODE_DESC_EXISTS          = (1 << 9),

    DONNA_NODE_NAME_WRITABLE        = (1 << 10),
    DONNA_NODE_ICON_WRITABLE        = (1 << 11),
    DONNA_NODE_FULL_NAME_WRITABLE   = (1 << 12),
    DONNA_NODE_SIZE_WRITABLE        = (1 << 13),
    DONNA_NODE_CTIME_WRITABLE       = (1 << 14),
    DONNA_NODE_MTIME_WRITABLE       = (1 << 15),
    DONNA_NODE_ATIME_WRITABLE       = (1 << 16),
    DONNA_NODE_MODE_WRITABLE        = (1 << 17),
    DONNA_NODE_UID_WRITABLE         = (1 << 18),
    DONNA_NODE_GID_WRITABLE         = (1 << 19),
    DONNA_NODE_DESC_WRITABLE        = (1 << 20),

    DONNA_NODE_ALL_EXISTS           = (DONNA_NODE_ICON_EXISTS
        | DONNA_NODE_FULL_NAME_EXISTS | DONNA_NODE_SIZE_EXISTS
        | DONNA_NODE_CTIME_EXISTS | DONNA_NODE_MTIME_EXISTS
        | DONNA_NODE_ATIME_EXISTS | DONNA_NODE_MODE_EXISTS
        | DONNA_NODE_UID_EXISTS  | DONNA_NODE_GID_EXISTS
        | DONNA_NODE_DESC_EXISTS),
    DONNA_NODE_ALL_WRITABLE         = (DONNA_NODE_ICON_WRITABLE
        | DONNA_NODE_FULL_NAME_WRITABLE | DONNA_NODE_SIZE_WRITABLE
        | DONNA_NODE_CTIME_WRITABLE | DONNA_NODE_MTIME_WRITABLE
        | DONNA_NODE_ATIME_WRITABLE | DONNA_NODE_MODE_WRITABLE
        | DONNA_NODE_UID_WRITABLE  | DONNA_NODE_GID_WRITABLE
        | DONNA_NODE_DESC_WRITABLE)
} DonnaNodeFlags;

/**
 * DonnaNodeHasProp:
 * @DONNA_NODE_PROP_NONE: Property doesn't exist on node
 * @DONNA_NODE_PROP_EXISTS: Property exists on node
 * @DONNA_NODE_PROP_HAS_VALUE: Property has a value set
 * @DONNA_NODE_PROP_WRITABLE: Property is writable (value can be set/changed)
 *
 * Information about a property. See donna_node_has_property()
 */
typedef enum
{
    /*< private >*/
    DONNA_NODE_PROP_UNKNOWN     = (1 << 0),
    /*< public >*/
    DONNA_NODE_PROP_NONE        = (1 << 1),
    DONNA_NODE_PROP_EXISTS      = (1 << 2),
    DONNA_NODE_PROP_HAS_VALUE   = (1 << 3),
    DONNA_NODE_PROP_WRITABLE    = (1 << 4),
} DonnaNodeHasProp;

/**
 * refresher_fn:
 * @task: (allow-none): The #DonnaTask in which the refresher is ran, or %NULL
 * @node: The #DonnaNode for which the property must be refreshed
 * @name: The name of the property to refresh
 *
 * The function called when a node was asked to refresh a property. This
 * refresher must call donna_node_set_property_value() to set the new value, and
 * then return %TRUE.
 *
 * @task will be %NULL if the call results from using e.g. donna_node_get() with
 * parameter blocking set to %TRUE.
 * It will be the #DonnaTask when called from donna_node_refresh_task(), in
 * which case it should only use it to see whether it has been cancelled or not
 * (in which case, stop and return %FALSE)
 *
 * The final state of @task (as well as any error/return value) will be handled
 * by @node itself. This is to allow refreshing multiple properties within one
 * single task.
 *
 * Returns: %TRUE if the refresh was succesful, else %FALSE
 */
typedef gboolean    (*refresher_fn) (DonnaTask      *task,
                                     DonnaNode      *node,
                                     const gchar    *name);
/**
 * setter_fn:
 * @task: The #DonnaTask under which the setter is run
 * @node: The #DonnaNode for which to set the property
 * @name: The name of the property to set
 * @value: A #GValue holding the value to set
 *
 * This setter must (try to) set the specified property to the given value.
 * @value is guaranteed to be of the same type as the property on @node.
 *
 * This is basically the worker of @task, in that - in addition to checking if
 * the task is paused/cancelled - it should set its #DonnaTask:error property in
 * case on failure.
 *
 * It should not, however, set a return value.
 *
 * Returns: The #DonnaTaskState for @task (must be a %DONNA_TASK_POST_RUN state)
 */
typedef DonnaTaskState (*setter_fn) (DonnaTask      *task,
                                     DonnaNode      *node,
                                     const gchar    *name,
                                     const GValue   *value);
struct _DonnaNode
{
    /*< private >*/
    GObject parent;

    DonnaNodePrivate *priv;
};

struct _DonnaNodeClass
{
    /*< private >*/
    GObjectClass parent;
};

DonnaNode *         donna_node_new                  (DonnaProvider      *provider,
                                                     const gchar        *location,
                                                     DonnaNodeType       node_type,
                                                     const gchar        *filename,
                                                     refresher_fn        refresher,
                                                     setter_fn           setter,
                                                     const gchar        *name,
                                                     DonnaNodeFlags      flags);
DonnaNode *         donna_node_new_from_node        (DonnaProvider      *provider,
                                                     const gchar        *location,
                                                     DonnaNode          *source_node,
                                                     GError            **error);
gboolean            donna_node_add_property         (DonnaNode          *node,
                                                     const gchar        *name,
                                                     GType               type,
                                                     const GValue       *value,
                                                     refresher_fn        refresher,
                                                     setter_fn           setter,
                                                     GError            **error);
DonnaNodeHasProp    donna_node_has_property         (DonnaNode          *node,
                                                     const gchar        *name);
void                donna_node_get                  (DonnaNode          *node,
                                                     gboolean            is_blocking,
                                                     const gchar        *first_name,
                                                     ...);
DonnaProvider *     donna_node_get_provider         (DonnaNode          *node);
DonnaProvider *     donna_node_peek_provider        (DonnaNode          *node);
const gchar *       donna_node_get_domain           (DonnaNode          *node);
gchar *             donna_node_get_location         (DonnaNode          *node);
gchar *             donna_node_get_full_location    (DonnaNode          *node);
DonnaNodeType       donna_node_get_node_type        (DonnaNode          *node);
gchar *             donna_node_get_filename         (DonnaNode          *node);
gchar *             donna_node_get_name             (DonnaNode          *node);
DonnaNodeHasValue   donna_node_get_icon             (DonnaNode          *node,
                                                     gboolean            is_blocking,
                                                     GIcon             **icon);
DonnaNodeHasValue   donna_node_get_full_name        (DonnaNode          *node,
                                                     gboolean            is_blocking,
                                                     gchar             **full_name);
DonnaNodeHasValue   donna_node_get_size             (DonnaNode          *node,
                                                     gboolean            is_blocking,
                                                     guint64            *size);
DonnaNodeHasValue   donna_node_get_ctime            (DonnaNode          *node,
                                                     gboolean            is_blocking,
                                                     guint64            *ctime);
DonnaNodeHasValue   donna_node_get_mtime            (DonnaNode          *node,
                                                     gboolean            is_blocking,
                                                     guint64            *mtime);
DonnaNodeHasValue   donna_node_get_atime            (DonnaNode          *node,
                                                     gboolean            is_blocking,
                                                     guint64            *atime);
DonnaNodeHasValue   donna_node_get_mode             (DonnaNode          *node,
                                                     gboolean            is_blocking,
                                                     guint              *mode);
DonnaNodeHasValue   donna_node_get_uid              (DonnaNode          *node,
                                                     gboolean            is_blocking,
                                                     guint              *uid);
DonnaNodeHasValue   donna_node_get_gid              (DonnaNode          *node,
                                                     gboolean            is_blocking,
                                                     guint              *gid);
DonnaNodeHasValue   donna_node_get_desc             (DonnaNode          *node,
                                                     gboolean            is_blocking,
                                                     gchar             **desc);
DonnaNode *         donna_node_get_parent           (DonnaNode          *node,
                                                     GError            **error);
DonnaTask *         donna_node_refresh_task         (DonnaNode          *node,
                                                     GError            **error,
                                                     const gchar        *first_name,
                                                     ...);
DonnaTask *         donna_node_refresh_arr_task     (DonnaNode          *node,
                                                     GPtrArray          *props,
                                                     GError            **error);
DonnaTask *         donna_node_set_property_task    (DonnaNode          *node,
                                                     const gchar        *name,
                                                     const GValue       *value,
                                                     GError            **error);
DonnaTask *         donna_node_has_children_task    (DonnaNode          *node,
                                                     DonnaNodeType       node_types,
                                                     GError            **error);
DonnaTask *         donna_node_get_children_task    (DonnaNode          *node,
                                                     DonnaNodeType       node_types,
                                                     GError            **error);
DonnaTask *         donna_node_trigger_task         (DonnaNode          *node,
                                                     GError            **error);
DonnaTask *         donna_node_new_child_task       (DonnaNode          *node,
                                                     DonnaNodeType       type,
                                                     const gchar        *name,
                                                     GError            **error);
void                donna_node_mark_ready           (DonnaNode          *node);
void                donna_node_mark_invalid         (DonnaNode          *node,
                                                     DonnaProvider      *pinv);
void                donna_node_set_property_value   (DonnaNode          *node,
                                                     const gchar        *name,
                                                     const GValue       *value);
void                donna_node_set_property_value_no_signal (
                                                     DonnaNode          *node,
                                                     const gchar        *name,
                                                     const GValue       *value);

G_END_DECLS

#endif /* __DONNA_NODE_H__ */
