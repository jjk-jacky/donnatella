
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

G_END_DECLS

#endif /* __DONNA_APP_H__ */
