
#ifndef __DONNA_APP_H__
#define __DONNA_APP_H__

#include <glib.h>
#include <glib-object.h>
#include "common.h"
#include "conf.h"
#include "task-manager.h"
#include "treeview.h"
#include "columntype.h"
#include "filter.h"

G_BEGIN_DECLS

#define DONNA_APP_ERROR         g_quark_from_static_string ("DonnaApp-Error")
typedef enum
{
    DONNA_APP_ERROR_EMPTY,
    DONNA_APP_ERROR_OTHER,
} DonnaAppError;

typedef enum
{
    DONNA_ENABLED_TYPE_UNKNOWN = 0,
    DONNA_ENABLED_TYPE_ENABLED,
    DONNA_ENABLED_TYPE_DISABLED,
    DONNA_ENABLED_TYPE_COMBINE,
    DONNA_ENABLED_TYPE_IGNORE
} DonnaEnabledTypes;

struct _DonnaAppInterface
{
    GTypeInterface parent;

    /* signals */
    void                (*treeview_loaded)          (DonnaApp       *app,
                                                     DonnaTreeView  *tree);
    gboolean            (*event)                    (DonnaApp       *app,
                                                     const gchar    *event,
                                                     const gchar    *source,
                                                     const gchar    *conv_flags,
                                                     conv_flag_fn    conv_fn,
                                                     gpointer        conv_data);

    /* virtual table */
    void                (*ensure_focused)           (DonnaApp       *app);
    void                (*add_window)               (DonnaApp       *app,
                                                     GtkWindow      *window,
                                                     gboolean        destroy_with_parent);
    void                (*set_floating_window)      (DonnaApp       *app,
                                                     GtkWindow      *window);
    DonnaConfig *       (*get_config)               (DonnaApp       *app);
    DonnaConfig *       (*peek_config)              (DonnaApp       *app);
    DonnaProvider *     (*get_provider)             (DonnaApp       *app,
                                                     const gchar    *domain);
    DonnaColumnType *   (*get_columntype)           (DonnaApp       *app,
                                                     const gchar    *type);
    DonnaFilter *       (*get_filter)               (DonnaApp       *app,
                                                     const gchar    *filter);
    void                (*run_task)                 (DonnaApp       *app,
                                                     DonnaTask      *task);
    DonnaTaskManager *  (*peek_task_manager)        (DonnaApp       *app);
    DonnaTreeView *     (*get_treeview)             (DonnaApp       *app,
                                                     const gchar    *name);
    gchar *             (*get_current_dirname)      (DonnaApp       *app);
    gchar *             (*get_conf_filename)        (DonnaApp       *app,
                                                     const gchar    *fmt,
                                                     va_list         va_args);
    gchar *             (*new_int_ref)              (DonnaApp       *app,
                                                     DonnaArgType    type,
                                                     gpointer        ptr);
    gpointer            (*get_int_ref)              (DonnaApp       *app,
                                                     const gchar    *intref,
                                                     DonnaArgType    type);
    gboolean            (*free_int_ref)             (DonnaApp       *app,
                                                     const gchar    *intref);
    gchar *             (*parse_fl)                 (DonnaApp       *app,
                                                     gchar          *fl,
                                                     const gchar    *conv_flags,
                                                     conv_flag_fn    conv_fn,
                                                     gpointer        conv_data,
                                                     GPtrArray     **intrefs);
    gboolean            (*trigger_fl)               (DonnaApp       *app,
                                                     const gchar    *fl,
                                                     GPtrArray      *intrefs,
                                                     gboolean        blocking,
                                                     GError        **error);
    gboolean            (*emit_event)               (DonnaApp       *app,
                                                     const gchar    *event,
                                                     gboolean        is_confirm,
                                                     const gchar    *source,
                                                     const gchar    *conv_flags,
                                                     conv_flag_fn    conv_fn,
                                                     gpointer        conv_data);
    gboolean            (*show_menu)                (DonnaApp       *app,
                                                     GPtrArray      *nodes,
                                                     const gchar    *menu,
                                                     GError       **error);
    void                (*show_error)               (DonnaApp       *app,
                                                     const gchar    *title,
                                                     const GError   *error);
    gpointer            (*get_ct_data)              (DonnaApp       *app,
                                                     const gchar    *col_name);
    DonnaTask *         (*nodes_io_task)            (DonnaApp       *app,
                                                     GPtrArray      *nodes,
                                                     DonnaIoType     io_type,
                                                     DonnaNode      *dest,
                                                     const gchar    *new_name,
                                                     GError        **error);
    gint                (*ask)                      (DonnaApp       *app,
                                                     const gchar    *title,
                                                     const gchar    *details,
                                                     const gchar    *btn1_icon,
                                                     const gchar    *btn1_label,
                                                     const gchar    *btn2_icon,
                                                     const gchar    *btn2_label,
                                                     va_list         va_args);
    gchar *             (*ask_text)                 (DonnaApp       *app,
                                                     const gchar    *title,
                                                     const gchar    *details,
                                                     const gchar    *main_default,
                                                     const gchar   **other_defaults,
                                                     GError        **error);

};

/* signals */
void                donna_app_treeview_loaded       (DonnaApp       *app,
                                                     DonnaTreeView  *tree);

/* API */
void                donna_app_ensure_focused        (DonnaApp       *app);
void                donna_app_add_window            (DonnaApp       *app,
                                                     GtkWindow      *window,
                                                     gboolean        destroy_with_parent);
void                donna_app_set_floating_window   (DonnaApp       *app,
                                                     GtkWindow      *window);
DonnaConfig *       donna_app_get_config            (DonnaApp       *app);
DonnaConfig *       donna_app_peek_config           (DonnaApp       *app);
DonnaProvider *     donna_app_get_provider          (DonnaApp       *app,
                                                     const gchar    *domain);
DonnaNode *         donna_app_get_node              (DonnaApp       *app,
                                                     const gchar    *full_location,
                                                     GError        **error);
gboolean            donna_app_trigger_node          (DonnaApp       *app,
                                                     const gchar    *full_location,
                                                     GError        **error);
DonnaColumnType *   donna_app_get_columntype        (DonnaApp       *app,
                                                     const gchar    *type);
DonnaFilter *       donna_app_get_filter            (DonnaApp       *app,
                                                     const gchar    *filter);
void                donna_app_run_task              (DonnaApp       *app,
                                                     DonnaTask      *task);
gboolean            donna_app_run_task_and_wait     (DonnaApp       *app,
                                                     DonnaTask      *task,
                                                     DonnaTask      *current_task,
                                                     GError        **error);
DonnaTaskManager *  donna_app_peek_task_manager     (DonnaApp       *app);
DonnaTreeView *     donna_app_get_treeview          (DonnaApp       *app,
                                                     const gchar    *name);
DonnaNode *         donna_app_get_current_location  (DonnaApp       *app,
                                                     GError        **error);
gchar *             donna_app_get_current_dirname   (DonnaApp       *app);
gchar *             donna_app_get_conf_filename     (DonnaApp       *app,
                                                     const gchar    *fmt,
                                                     ...);
gchar *             donna_app_new_int_ref           (DonnaApp       *app,
                                                     DonnaArgType    type,
                                                     gpointer        ptr);
gpointer            donna_app_get_int_ref           (DonnaApp       *app,
                                                     const gchar    *intref,
                                                     DonnaArgType    type);
gboolean            donna_app_free_int_ref          (DonnaApp       *app,
                                                     const gchar    *intref);
gchar *             donna_app_parse_fl              (DonnaApp       *app,
                                                     gchar          *fl,
                                                     const gchar    *conv_flags,
                                                     conv_flag_fn    conv_fn,
                                                     gpointer        conv_data,
                                                     GPtrArray     **intrefs);
gboolean            donna_app_trigger_fl            (DonnaApp       *app,
                                                     const gchar    *fl,
                                                     GPtrArray      *intrefs,
                                                     gboolean        blocking,
                                                     GError        **error);
gboolean            donna_app_emit_event            (DonnaApp       *app,
                                                     const gchar    *event,
                                                     gboolean        is_confirm,
                                                     const gchar    *conv_flags,
                                                     conv_flag_fn    conv_fn,
                                                     gpointer        conv_data,
                                                     const gchar    *fmt_source,
                                                     ...);
gboolean            donna_app_show_menu             (DonnaApp       *app,
                                                     GPtrArray      *nodes,
                                                     const gchar    *menu,
                                                     GError        **error);
void                donna_app_show_error            (DonnaApp       *app,
                                                     const GError   *error,
                                                     const gchar    *fmt,
                                                     ...);
gpointer            donna_app_get_ct_data           (const gchar    *col_name,
                                                     DonnaApp       *app);
gboolean            donna_app_filter_nodes          (DonnaApp       *app,
                                                     GPtrArray      *nodes,
                                                     const gchar    *filter,
                                                     GError       **error);
DonnaTask *         donna_app_nodes_io_task         (DonnaApp       *app,
                                                     GPtrArray      *nodes,
                                                     DonnaIoType     io_type,
                                                     DonnaNode      *dest,
                                                     const gchar    *new_name,
                                                     GError        **error);
gint                donna_app_ask                   (DonnaApp       *app,
                                                     const gchar    *title,
                                                     const gchar    *details,
                                                     const gchar    *btn1_icon,
                                                     const gchar    *btn1_label,
                                                     const gchar    *btn2_icon,
                                                     const gchar    *btn2_label,
                                                     ...);
gchar *             donna_app_ask_text              (DonnaApp       *app,
                                                     const gchar    *title,
                                                     const gchar    *details,
                                                     const gchar    *main_default,
                                                     const gchar   **other_defaults,
                                                     GError        **error);

G_END_DECLS

#endif /* __DONNA_APP_H__ */
