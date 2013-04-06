
#ifndef __DONNA_APP_H__
#define __DONNA_APP_H__

#include <glib.h>
#include "common.h"
#include "conf.h"
#include "treeview.h"
#include "columntype.h"

G_BEGIN_DECLS

struct _DonnaAppInterface
{
    GTypeInterface parent;

    /* signals */
    void                (*active_list_changed)      (DonnaApp       *app,
                                                     DonnaTreeView  *list);

    /* virtual table */
    DonnaConfig *       (*get_config)               (DonnaApp       *app);
    DonnaProvider *     (*get_provider)             (DonnaApp       *app,
                                                     const gchar    *domain);
    DonnaColumnType *   (*get_columntype)           (DonnaApp       *app,
                                                     const gchar    *type);
    gchar *             (*get_arrangement)          (DonnaApp       *app,
                                                     DonnaNode      *node);
    void                (*run_task)                 (DonnaApp       *app,
                                                     DonnaTask      *task);
    DonnaTreeView *     (*get_treeview)             (DonnaApp       *app,
                                                     const gchar    *name);
    void                (*show_error)               (DonnaApp       *app,
                                                     GError         *error);
};

/* signals */
void                donna_app_active_list_changed   (DonnaApp       *app,
                                                     DonnaTreeView  *list);

/* API */
DonnaConfig *       donna_app_get_config            (DonnaApp       *app);
DonnaProvider *     donna_app_get_provider          (DonnaApp       *app,
                                                     const gchar    *domain);
DonnaColumnType *   donna_app_get_columntype        (DonnaApp       *app,
                                                     const gchar    *type);
gchar *             donna_app_get_arrangement       (DonnaApp       *app,
                                                     DonnaNode      *node);
void                donna_app_run_task              (DonnaApp       *app,
                                                     DonnaTask      *task);
DonnaTreeView *     donna_app_get_treeview          (DonnaApp       *app,
                                                     const gchar    *name);
void                donna_app_show_error            (DonnaApp       *app,
                                                     GError         *error);
#define donna_app_show_new_error(app, domain, code, ...)    \
    donna_app_show_error (app, g_error_new (domain, code, __VA_ARGS__))

G_END_DECLS

#endif /* __DONNA_APP_H__ */
