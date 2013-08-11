
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

struct _DonnaAppInterface
{
    GTypeInterface parent;

    /* signals */
    void                (*treeview_loaded)          (DonnaApp       *app,
                                                     DonnaTreeView  *tree);

    /* virtual table */
    void                (*ensure_focused)           (DonnaApp       *app);
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
    DonnaTaskManager *  (*get_task_manager)         (DonnaApp       *app);
    DonnaTreeView *     (*get_treeview)             (DonnaApp       *app,
                                                     const gchar    *name);
    gchar *             (*new_int_ref)              (DonnaApp       *app,
                                                     DonnaArgType    type,
                                                     gpointer        ptr);
    gpointer            (*get_int_ref)              (DonnaApp       *app,
                                                     const gchar    *intref,
                                                     DonnaArgType    type);
    gboolean            (*free_int_ref)             (DonnaApp       *app,
                                                     const gchar    *intref);
    gboolean            (*show_menu)                (DonnaApp       *app,
                                                     GPtrArray      *nodes,
                                                     const gchar    *menu,
                                                     GError       **error);
    void                (*show_error)               (DonnaApp       *app,
                                                     const gchar    *title,
                                                     const GError   *error);
    gpointer            (*get_ct_data)              (DonnaApp       *app,
                                                     const gchar    *col_name);
    gboolean            (*nodes_io)                 (DonnaApp       *app,
                                                     GPtrArray      *nodes,
                                                     DonnaIoType     io_type,
                                                     DonnaNode      *dest,
                                                     GError        **error);
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
void                donna_app_set_floating_window   (DonnaApp       *app,
                                                     GtkWindow      *window);
DonnaConfig *       donna_app_get_config            (DonnaApp       *app);
DonnaConfig *       donna_app_peek_config           (DonnaApp       *app);
DonnaProvider *     donna_app_get_provider          (DonnaApp       *app,
                                                     const gchar    *domain);
DonnaTask *         donna_app_get_node_task         (DonnaApp       *app,
                                                     const gchar    *full_location);
gboolean            donna_app_trigger_node          (DonnaApp       *app,
                                                     const gchar    *full_location);
DonnaColumnType *   donna_app_get_columntype        (DonnaApp       *app,
                                                     const gchar    *type);
DonnaFilter *       donna_app_get_filter            (DonnaApp       *app,
                                                     const gchar    *filter);
void                donna_app_run_task              (DonnaApp       *app,
                                                     DonnaTask      *task);
DonnaTaskManager *  donna_app_get_task_manager      (DonnaApp       *app);
DonnaTreeView *     donna_app_get_treeview          (DonnaApp       *app,
                                                     const gchar    *name);
DonnaNode *         donna_app_get_current_location  (DonnaApp       *app,
                                                     GError        **error);
gchar *             donna_app_get_current_dirname   (DonnaApp       *app,
                                                     GError        **error);
gchar *             donna_app_new_int_ref           (DonnaApp       *app,
                                                     DonnaArgType    type,
                                                     gpointer        ptr);
gpointer            donna_app_get_int_ref           (DonnaApp       *app,
                                                     const gchar    *intref,
                                                     DonnaArgType    type);
gboolean            donna_app_free_int_ref          (DonnaApp       *app,
                                                     const gchar    *intref);
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
gboolean            donna_app_nodes_io              (DonnaApp       *app,
                                                     GPtrArray      *nodes,
                                                     DonnaIoType     io_type,
                                                     DonnaNode      *dest,
                                                     GError        **error);
gchar *             donna_app_ask_text              (DonnaApp       *app,
                                                     const gchar    *title,
                                                     const gchar    *details,
                                                     const gchar    *main_default,
                                                     const gchar   **other_defaults,
                                                     GError        **error);

G_END_DECLS

#endif /* __DONNA_APP_H__ */
