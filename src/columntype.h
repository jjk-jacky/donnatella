
#ifndef __DONNA_COLUMN_TYPE_H__
#define __DONNA_COLUMN_TYPE_H__

#include <gtk/gtk.h>
#include "common.h"
#include "conf.h"
#include "contextmenu.h"

G_BEGIN_DECLS

typedef struct _DonnaColumnType             DonnaColumnType; /* dummy typedef */
typedef struct _DonnaColumnTypeInterface    DonnaColumnTypeInterface;

#define DONNA_TYPE_COLUMN_TYPE              (donna_column_type_get_type ())
#define DONNA_COLUMN_TYPE(obj)              (G_TYPE_CHECK_INSTANCE_CAST ((obj), DONNA_TYPE_COLUMN_TYPE, DonnaColumnType))
#define DONNA_IS_COLUMN_TYPE(obj)           (G_TYPE_CHECK_INSTANCE_TYPE ((obj), DONNA_TYPE_COLUMN_TYPE))
#define DONNA_COLUMN_TYPE_GET_INTERFACE(obj)(G_TYPE_INSTANCE_GET_INTERFACE ((obj), DONNA_TYPE_COLUMN_TYPE, DonnaColumnTypeInterface))

GType           donna_column_type_get_type  (void) G_GNUC_CONST;

#define DONNA_COLUMN_TYPE_ERROR             g_quark_from_static_string ("DonnaColumnType-Error")
typedef enum
{
    DONNA_COLUMN_TYPE_ERROR_INVALID_SYNTAX,
    DONNA_COLUMN_TYPE_ERROR_NOT_SUPPORTED,
    DONNA_COLUMN_TYPE_ERROR_NODE_NO_PROP,
    DONNA_COLUMN_TYPE_ERROR_NODE_NOT_WRITABLE,
    DONNA_COLUMN_TYPE_ERROR_PARTIAL_COMPLETION,
    DONNA_COLUMN_TYPE_ERROR_OTHER,
} DonnaColumnTypeError;


typedef DonnaColumnType *   (*new_ct)       (DonnaConfig        *config);

#define DONNA_COLUMN_TYPE_RENDERER_TEXT     't'
#define DONNA_COLUMN_TYPE_RENDERER_PIXBUF   'p'
#define DONNA_COLUMN_TYPE_RENDERER_PROGRESS 'P'
#define DONNA_COLUMN_TYPE_RENDERER_COMBO    'c'
#define DONNA_COLUMN_TYPE_RENDERER_TOGGLE   'T'
#define DONNA_COLUMN_TYPE_RENDERER_SPINNER  'S'

typedef enum
{
    DONNA_COLUMN_TYPE_NEED_NOTHING  = 0,
    DONNA_COLUMN_TYPE_NEED_REDRAW   = (1 << 0),
    DONNA_COLUMN_TYPE_NEED_RESORT   = (1 << 1)
} DonnaColumnTypeNeed;

/* keep DonnaTreeviewOptionSaveLocation in sync */
typedef enum
{
    DONNA_COLUMN_OPTION_SAVE_IN_MEMORY = 0, /* i.e. don't save, only apply */
    DONNA_COLUMN_OPTION_SAVE_IN_CURRENT,
    DONNA_COLUMN_OPTION_SAVE_IN_ARRANGEMENT,
    DONNA_COLUMN_OPTION_SAVE_IN_TREE,
    DONNA_COLUMN_OPTION_SAVE_IN_MODE,
    DONNA_COLUMN_OPTION_SAVE_IN_DEFAULT,
    DONNA_COLUMN_OPTION_SAVE_IN_ASK
} DonnaColumnOptionSaveLocation;

typedef gboolean    (*renderer_edit_fn)     (GtkCellRenderer    *renderer,
                                             gpointer            data);

struct _DonnaColumnTypeInterface
{
    GTypeInterface parent;

    /* virtual table */
    gboolean            (*helper_can_edit)  (DonnaColumnType    *ct,
                                             const gchar        *property,
                                             DonnaNode          *node,
                                             GError            **error);
    const gchar *       (*helper_get_save_location) (
                                             DonnaColumnType    *ct,
                                             const gchar       **extra,
                                             gboolean            from_alias,
                                             GError            **error);
    gboolean            (*helper_set_option) (
                                             DonnaColumnType    *ct,
                                             const gchar        *col_name,
                                             const gchar        *arr_name,
                                             const gchar        *tv_name,
                                             gboolean            is_tree,
                                             const gchar        *def_cat,
                                             DonnaColumnOptionSaveLocation save_location,
                                             const gchar        *option,
                                             GType               type,
                                             gpointer            current,
                                             gpointer            value,
                                             GError            **error);
    gchar *             (*helper_get_set_option_trigger) (
                                             const gchar  *option,
                                             const gchar  *value,
                                             gboolean      quote_value,
                                             const gchar  *ask_title,
                                             const gchar  *ask_details,
                                             const gchar  *ask_current,
                                             const gchar  *save_location);

    const gchar *       (*get_name)         (DonnaColumnType    *ct);
    const gchar *       (*get_renderers)    (DonnaColumnType    *ct);
    DonnaColumnTypeNeed (*refresh_data)     (DonnaColumnType    *ct,
                                             const gchar        *col_name,
                                             const gchar        *arr_name,
                                             const gchar        *tv_name,
                                             gboolean            is_tree,
                                             gpointer           *data);
    void                (*free_data)        (DonnaColumnType    *ct,
                                             gpointer            data);
    GPtrArray *         (*get_props)        (DonnaColumnType    *ct,
                                             gpointer            data);
    GtkSortType         (*get_default_sort_order)
                                            (DonnaColumnType    *ct,
                                             const gchar        *col_name,
                                             const gchar        *arr_name,
                                             const gchar        *tv_name,
                                             gboolean            is_tree,
                                             gpointer            data);
    gboolean            (*can_edit)         (DonnaColumnType    *ct,
                                             gpointer            data,
                                             DonnaNode          *node,
                                             GError            **error);
    gboolean            (*edit)             (DonnaColumnType    *ct,
                                             gpointer            data,
                                             DonnaNode          *node,
                                             GtkCellRenderer   **renderers,
                                             renderer_edit_fn    renderer_edit,
                                             gpointer            re_data,
                                             DonnaTreeView      *treeview,
                                             GError            **error);
    DonnaColumnTypeNeed (*set_option)       (DonnaColumnType    *ct,
                                             const gchar        *col_name,
                                             const gchar        *arr_name,
                                             const gchar        *tv_name,
                                             gboolean            is_tree,
                                             gpointer            data,
                                             const gchar        *option,
                                             const gchar        *value,
                                             DonnaColumnOptionSaveLocation save_location,
                                             GError            **error);
    gboolean            (*set_value)        (DonnaColumnType    *ct,
                                             gpointer            data,
                                             GPtrArray          *nodes,
                                             const gchar        *value,
                                             DonnaNode          *node_ref,
                                             DonnaTreeView      *treeview,
                                             GError            **error);
    GPtrArray *         (*render)           (DonnaColumnType    *ct,
                                             gpointer            data,
                                             guint               index,
                                             DonnaNode          *node,
                                             GtkCellRenderer    *renderer);
    gboolean            (*set_tooltip)      (DonnaColumnType    *ct,
                                             gpointer            data,
                                             guint               index,
                                             DonnaNode          *node,
                                             GtkTooltip         *tooltip);
    gint                (*node_cmp)         (DonnaColumnType    *ct,
                                             gpointer            data,
                                             DonnaNode          *node1,
                                             DonnaNode          *node2);
    gboolean            (*is_match_filter)  (DonnaColumnType    *ct,
                                             const gchar        *filter,
                                             gpointer           *filter_data,
                                             gpointer            data,
                                             DonnaNode          *node,
                                             GError            **error);
    void                (*free_filter_data) (DonnaColumnType    *ct,
                                             gpointer            filter_data);
    /* context related */
    gchar *             (*get_context_alias)(DonnaColumnType    *ct,
                                             gpointer            data,
                                             const gchar        *alias,
                                             const gchar        *extra,
                                             DonnaContextReference reference,
                                             DonnaNode          *node_ref,
                                             get_sel_fn          get_sel,
                                             gpointer            get_sel_data,
                                             const gchar        *prefix,
                                             GError            **error);
    gboolean            (*get_context_item_info) (
                                             DonnaColumnType    *ct,
                                             gpointer            data,
                                             const gchar        *item,
                                             const gchar        *extra,
                                             DonnaContextReference reference,
                                             DonnaNode          *node_ref,
                                             get_sel_fn          get_sel,
                                             gpointer            get_sel_data,
                                             DonnaContextInfo   *info,
                                             GError            **error);
};

const gchar *   donna_column_type_get_name      (DonnaColumnType    *ct);
const gchar *   donna_column_type_get_renderers (DonnaColumnType    *ct);
DonnaColumnTypeNeed donna_column_type_refresh_data (DonnaColumnType *ct,
                                                 const gchar        *col_name,
                                                 const gchar        *arr_name,
                                                 const gchar        *tv_name,
                                                 gboolean            is_tree,
                                                 gpointer           *data);
void            donna_column_type_free_data     (DonnaColumnType    *ct,
                                                 gpointer            data);
GPtrArray *     donna_column_type_get_props     (DonnaColumnType    *ct,
                                                 gpointer            data);
GtkSortType     donna_column_type_get_default_sort_order
                                                (DonnaColumnType    *ct,
                                                 const gchar        *col_name,
                                                 const gchar        *arr_name,
                                                 const gchar        *tv_name,
                                                 gboolean            is_tree,
                                                 gpointer            data);
gboolean        donna_column_type_can_edit      (DonnaColumnType    *ct,
                                                 gpointer            data,
                                                 DonnaNode          *node,
                                                 GError            **error);
gboolean        donna_column_type_edit          (DonnaColumnType    *ct,
                                                 gpointer            data,
                                                 DonnaNode          *node,
                                                 GtkCellRenderer   **renderers,
                                                 renderer_edit_fn    renderer_edit,
                                                 gpointer            re_data,
                                                 DonnaTreeView      *treeview,
                                                 GError            **error);
DonnaColumnTypeNeed donna_column_type_set_option(DonnaColumnType    *ct,
                                                 const gchar        *col_name,
                                                 const gchar        *arr_name,
                                                 const gchar        *tv_name,
                                                 gboolean            is_tree,
                                                 gpointer            data,
                                                 const gchar        *option,
                                                 const gchar        *value,
                                                 DonnaColumnOptionSaveLocation save_location,
                                                 GError            **error);
gboolean        donna_column_type_set_value     (DonnaColumnType    *ct,
                                                 gpointer            data,
                                                 GPtrArray          *nodes,
                                                 const gchar        *value,
                                                 DonnaNode          *node,
                                                 DonnaTreeView      *treeview,
                                                 GError            **error);
GPtrArray *     donna_column_type_render        (DonnaColumnType    *ct,
                                                 gpointer            data,
                                                 guint               index,
                                                 DonnaNode          *node,
                                                 GtkCellRenderer    *renderer);
gboolean        donna_column_type_set_tooltip   (DonnaColumnType    *ct,
                                                 gpointer            data,
                                                 guint               index,
                                                 DonnaNode          *node,
                                                 GtkTooltip         *tooltip);
gint            donna_column_type_node_cmp      (DonnaColumnType    *ct,
                                                 gpointer            data,
                                                 DonnaNode          *node1,
                                                 DonnaNode          *node2);
gboolean        donna_column_type_is_match_filter
                                                (DonnaColumnType    *ct,
                                                 const gchar        *filter,
                                                 gpointer           *filter_data,
                                                 gpointer            data,
                                                 DonnaNode          *node,
                                                 GError            **error);
void            donna_column_type_free_filter_data
                                                (DonnaColumnType    *ct,
                                                 gpointer            filter_data);
/* context related */
gchar *         donna_column_type_get_context_alias (
                                                 DonnaColumnType    *ct,
                                                 gpointer            data,
                                                 const gchar        *alias,
                                                 const gchar        *extra,
                                                 DonnaContextReference reference,
                                                 DonnaNode          *node_ref,
                                                 get_sel_fn          get_sel,
                                                 gpointer            get_sel_data,
                                                 const gchar        *prefix,
                                                 GError            **error);
gboolean        donna_column_type_get_context_item_info (
                                                 DonnaColumnType    *ct,
                                                 gpointer            data,
                                                 const gchar        *item,
                                                 const gchar        *extra,
                                                 DonnaContextReference reference,
                                                 DonnaNode          *node_ref,
                                                 get_sel_fn          get_sel,
                                                 gpointer            get_sel_data,
                                                 DonnaContextInfo   *info,
                                                 GError            **error);


GtkWindow *     donna_column_type_new_floating_window (
                                                 DonnaTreeView      *tree,
                                                 gboolean            destroy_on_sel_changed);

G_END_DECLS

#endif /* __DONNA_COLUMN_TYPE_H__ */
