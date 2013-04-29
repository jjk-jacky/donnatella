
#include "app.h"

enum
{
    ACTIVE_LIST_CHANGED,
    NB_SIGNALS
};

static guint donna_app_signals[NB_SIGNALS] = { 0 };

static void
donna_app_default_init (DonnaAppInterface *interface)
{
    donna_app_signals[ACTIVE_LIST_CHANGED] =
        g_signal_new ("active-list-changed",
            DONNA_TYPE_APP,
            G_SIGNAL_RUN_LAST,
            G_STRUCT_OFFSET (DonnaAppInterface, active_list_changed),
            NULL,
            NULL,
            g_cclosure_marshal_VOID__BOXED,
            G_TYPE_NONE,
            1,
            DONNA_TYPE_TREE_VIEW);

    g_object_interface_install_property (interface,
            g_param_spec_object ("active-list", "active-list",
                "Active list",
                DONNA_TYPE_TREE_VIEW,
                G_PARAM_READWRITE));
}

G_DEFINE_INTERFACE (DonnaApp, donna_app, G_TYPE_OBJECT)

/* signals */

void
donna_app_active_list_changed (DonnaApp      *app,
                               DonnaTreeView *list)
{
    g_return_if_fail (DONNA_IS_APP (app));
    g_return_if_fail (DONNA_IS_TREE_VIEW (list));

    g_signal_emit (app, donna_app_signals[ACTIVE_LIST_CHANGED], 0, list);
}


/* API */

DonnaConfig *
donna_app_get_config (DonnaApp       *app)
{
    DonnaAppInterface *interface;

    g_return_val_if_fail (DONNA_IS_APP (app), NULL);

    interface = DONNA_APP_GET_INTERFACE (app);

    g_return_val_if_fail (interface != NULL, NULL);
    g_return_val_if_fail (interface->get_config != NULL, NULL);

    return (*interface->get_config) (app);
}

DonnaConfig *
donna_app_peek_config (DonnaApp *app)
{
    DonnaAppInterface *interface;

    g_return_val_if_fail (DONNA_IS_APP (app), NULL);

    interface = DONNA_APP_GET_INTERFACE (app);

    g_return_val_if_fail (interface != NULL, NULL);
    g_return_val_if_fail (interface->peek_config != NULL, NULL);

    return (*interface->peek_config) (app);
}

DonnaProvider *
donna_app_get_provider (DonnaApp       *app,
                        const gchar    *domain)
{
    DonnaAppInterface *interface;

    g_return_val_if_fail (DONNA_IS_APP (app), NULL);
    g_return_val_if_fail (domain != NULL, NULL);

    interface = DONNA_APP_GET_INTERFACE (app);

    g_return_val_if_fail (interface != NULL, NULL);
    g_return_val_if_fail (interface->get_provider != NULL, NULL);

    return (*interface->get_provider) (app, domain);
}

DonnaColumnType *
donna_app_get_columntype (DonnaApp       *app,
                          const gchar    *type)
{
    DonnaAppInterface *interface;

    g_return_val_if_fail (DONNA_IS_APP (app), NULL);
    g_return_val_if_fail (type != NULL, NULL);

    interface = DONNA_APP_GET_INTERFACE (app);

    g_return_val_if_fail (interface != NULL, NULL);
    g_return_val_if_fail (interface->get_columntype != NULL, NULL);

    return (*interface->get_columntype) (app, type);
}

DonnaFilter *
donna_app_get_filter (DonnaApp       *app,
                      const gchar    *filter)
{
    DonnaAppInterface *interface;

    g_return_val_if_fail (DONNA_IS_APP (app), NULL);
    g_return_val_if_fail (filter != NULL, NULL);

    interface = DONNA_APP_GET_INTERFACE (app);

    g_return_val_if_fail (interface != NULL, NULL);
    g_return_val_if_fail (interface->get_filter != NULL, NULL);

    return (*interface->get_filter) (app, filter);
}

void
donna_app_run_task (DonnaApp       *app,
                    DonnaTask      *task)
{
    DonnaAppInterface *interface;

    g_return_if_fail (DONNA_IS_APP (app));
    g_return_if_fail (DONNA_IS_TASK (task));

    interface = DONNA_APP_GET_INTERFACE (app);

    g_return_if_fail (interface != NULL);
    g_return_if_fail (interface->run_task != NULL);

    return (*interface->run_task) (app, task);
}

DonnaTreeView *
donna_app_get_treeview (DonnaApp    *app,
                        const gchar *name)
{
    DonnaAppInterface *interface;

    g_return_val_if_fail (DONNA_IS_APP (app), NULL);
    g_return_val_if_fail (name != NULL, NULL);

    interface = DONNA_APP_GET_INTERFACE (app);

    g_return_val_if_fail (interface != NULL, NULL);
    g_return_val_if_fail (interface->get_treeview != NULL, NULL);

    return (*interface->get_treeview) (app, name);
}

void
donna_app_show_error (DonnaApp       *app,
                      const GError   *error,
                      const gchar    *fmt,
                      ...)
{
    DonnaAppInterface *interface;
    gchar *title;
    va_list va_arg;

    g_return_if_fail (DONNA_IS_APP (app));
    g_return_if_fail (fmt != NULL);

    interface = DONNA_APP_GET_INTERFACE (app);

    g_return_if_fail (interface != NULL);
    g_return_if_fail (interface->show_error != NULL);

    va_start (va_arg, fmt);
    title = g_strdup_vprintf (fmt, va_arg);
    va_end (va_arg);

    (*interface->show_error) (app, title, error);
    g_free (title);
}
