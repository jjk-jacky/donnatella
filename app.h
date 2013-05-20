
#ifndef __DONNA_APP_H__
#define __DONNA_APP_H__

#include <glib.h>
#include <glib-object.h>
#include "common.h"
#include "conf.h"
#include "treeview.h"
#include "columntype.h"
#include "filter.h"

G_BEGIN_DECLS

struct _DonnaAppInterface
{
    GTypeInterface parent;

    /* signals */
    void                (*active_list_changed)      (DonnaApp       *app,
                                                     DonnaTreeView  *list);

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
    DonnaTreeView *     (*get_treeview)             (DonnaApp       *app,
                                                     const gchar    *name);
    void                (*show_error)               (DonnaApp       *app,
                                                     const gchar    *title,
                                                     const GError   *error);
};

/* signals */
void                donna_app_active_list_changed   (DonnaApp       *app,
                                                     DonnaTreeView  *list);

/* API */
void                donna_app_ensure_focused        (DonnaApp       *app);
void                donna_app_set_floating_window   (DonnaApp       *app,
                                                     GtkWindow      *window);
DonnaConfig *       donna_app_get_config            (DonnaApp       *app);
DonnaConfig *       donna_app_peek_config           (DonnaApp       *app);
DonnaProvider *     donna_app_get_provider          (DonnaApp       *app,
                                                     const gchar    *domain);
DonnaColumnType *   donna_app_get_columntype        (DonnaApp       *app,
                                                     const gchar    *type);
DonnaFilter *       donna_app_get_filter            (DonnaApp       *app,
                                                     const gchar    *filter);
void                donna_app_run_task              (DonnaApp       *app,
                                                     DonnaTask      *task);
DonnaTreeView *     donna_app_get_treeview          (DonnaApp       *app,
                                                     const gchar    *name);
void                donna_app_show_error            (DonnaApp       *app,
                                                     const GError   *error,
                                                     const gchar    *fmt,
                                                     ...);

G_END_DECLS

#endif /* __DONNA_APP_H__ */
