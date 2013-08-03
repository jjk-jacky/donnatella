
#include <string.h>
#include "app.h"
#include "provider.h"
#include "macros.h"

enum
{
    TREEVIEW_LOADED,
    NB_SIGNALS
};

static guint donna_app_signals[NB_SIGNALS] = { 0 };

static void
donna_app_default_init (DonnaAppInterface *interface)
{
    donna_app_signals[TREEVIEW_LOADED] =
        g_signal_new ("treeview-loaded",
            DONNA_TYPE_APP,
            G_SIGNAL_RUN_LAST,
            G_STRUCT_OFFSET (DonnaAppInterface, treeview_loaded),
            NULL,
            NULL,
            g_cclosure_marshal_VOID__OBJECT,
            G_TYPE_NONE,
            1,
            DONNA_TYPE_TREE_VIEW);

    g_object_interface_install_property (interface,
            g_param_spec_object ("active-list", "active-list",
                "Active list",
                DONNA_TYPE_TREE_VIEW,
                G_PARAM_READWRITE));
    g_object_interface_install_property (interface,
            g_param_spec_boolean ("just-focused", "just-focused",
                "Whether or not the main window was just focused",
                FALSE,  /* default */
                G_PARAM_READWRITE));
}

G_DEFINE_INTERFACE (DonnaApp, donna_app, G_TYPE_OBJECT)

/* signals */

void
donna_app_treeview_loaded (DonnaApp       *app,
                           DonnaTreeView  *tree)
{
    g_return_if_fail (DONNA_IS_APP (app));
    g_return_if_fail (DONNA_IS_TREE_VIEW (tree));

    g_signal_emit (app, donna_app_signals[TREEVIEW_LOADED], 0, tree);
}


/* API */

void
donna_app_ensure_focused (DonnaApp       *app)
{
    DonnaAppInterface *interface;

    g_return_if_fail (DONNA_IS_APP (app));

    interface = DONNA_APP_GET_INTERFACE (app);

    g_return_if_fail (interface != NULL);
    g_return_if_fail (interface->ensure_focused != NULL);

    (*interface->ensure_focused) (app);
}

void
donna_app_set_floating_window (DonnaApp       *app,
                               GtkWindow      *window)
{
    DonnaAppInterface *interface;

    g_return_if_fail (DONNA_IS_APP (app));
    g_return_if_fail (window == NULL || GTK_IS_WINDOW (window));

    interface = DONNA_APP_GET_INTERFACE (app);

    g_return_if_fail (interface != NULL);
    g_return_if_fail (interface->set_floating_window != NULL);

    (*interface->set_floating_window) (app, window);
}

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

DonnaTask *
donna_app_get_node_task (DonnaApp    *app,
                         const gchar *full_location)
{
    DonnaAppInterface *interface;
    DonnaProvider *provider;
    DonnaTask *task;
    gchar buf[64], *b = buf;
    const gchar *location;

    g_return_val_if_fail (DONNA_IS_APP (app), NULL);
    g_return_val_if_fail (full_location != NULL, NULL);

    interface = DONNA_APP_GET_INTERFACE (app);

    g_return_val_if_fail (interface != NULL, NULL);
    g_return_val_if_fail (interface->get_provider != NULL, NULL);

    location = strchr (full_location, ':');
    if (!location)
        return NULL;

    if (location - full_location >= 64)
        b = g_strdup_printf ("%.*s", (gint) (location - full_location),
                full_location);
    else
    {
        *buf = '\0';
        strncat (buf, full_location, location - full_location);
    }
    provider = (*interface->get_provider) (app, buf);
    if (b != buf)
        g_free (b);
    if (!provider)
        return NULL;

    task = donna_provider_get_node_task (provider, ++location, NULL);
    g_object_unref (provider);
    return task;
}

static void
trigger_node_cb (DonnaTask *task, gboolean timeout_called, DonnaApp *app)
{
    if (donna_task_get_state (task) == DONNA_TASK_FAILED)
        donna_app_show_error (app, donna_task_get_error (task),
                "Failed to trigger node");
}

static void
get_node_cb (DonnaTask *task, gboolean timeout_called, DonnaApp *app)
{
    GError *err = NULL;
    DonnaTask *trigger_task;

    if (donna_task_get_state (task) != DONNA_TASK_DONE)
    {
        donna_app_show_error (app, donna_task_get_error (task),
                "Cannot trigger node");
        return;
    }

    trigger_task = donna_node_trigger_task (
            g_value_get_object (donna_task_get_return_value (task)), &err);
    if (!trigger_task)
    {
        donna_app_show_error (app, err, "Cannot trigger node");
        g_clear_error (&err);
        return;
    }

    donna_task_set_callback (trigger_task, (task_callback_fn) trigger_node_cb,
            app, NULL);
    donna_app_run_task (app, trigger_task);
}

gboolean
donna_app_trigger_node (DonnaApp       *app,
                        const gchar    *full_location)
{
    DonnaAppInterface *interface;
    DonnaTask *task;

    g_return_val_if_fail (DONNA_IS_APP (app), FALSE);
    g_return_val_if_fail (full_location != NULL, FALSE);

    interface = DONNA_APP_GET_INTERFACE (app);

    g_return_val_if_fail (interface != NULL, FALSE);
    g_return_val_if_fail (interface->run_task != NULL, FALSE);

    task = donna_app_get_node_task (app, full_location);
    if (G_UNLIKELY (!task))
        return FALSE;
    donna_task_set_callback (task, (task_callback_fn) get_node_cb, app, NULL);
    (*interface->run_task) (app, task);
    return TRUE;
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

DonnaTaskManager *
donna_app_get_task_manager (DonnaApp       *app)
{
    DonnaAppInterface *interface;

    g_return_val_if_fail (DONNA_IS_APP (app), NULL);

    interface = DONNA_APP_GET_INTERFACE (app);

    g_return_val_if_fail (interface != NULL, NULL);
    g_return_val_if_fail (interface->get_task_manager != NULL, NULL);

    return (*interface->get_task_manager) (app);
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

DonnaNode *
donna_app_get_current_location (DonnaApp       *app,
                                GError        **error)
{
    DonnaTreeView *tree;
    DonnaNode *node;

    g_return_val_if_fail (DONNA_IS_APP (app), NULL);

    g_object_get (app, "active-list", &tree, NULL);
    if (!tree)
    {
        g_set_error (error, DONNA_APP_ERROR, DONNA_APP_ERROR_OTHER,
                "Cannot get current location: failed to get active-list");
        return NULL;
    }

    g_object_get (tree, "location", &node, NULL);
    if (!node)
    {
        g_set_error (error, DONNA_APP_ERROR, DONNA_APP_ERROR_OTHER,
                "Cannot get current location: failed to get it from treeview '%s'",
                donna_tree_view_get_name (tree));
        g_object_unref (tree);
        return NULL;
    }
    g_object_unref (tree);

    return node;
}

gchar *
donna_app_get_current_dirname (DonnaApp       *app,
                               GError        **error)
{
    DonnaNode *node;
    gchar *dirname;

    g_return_val_if_fail (DONNA_IS_APP (app), NULL);

    node = donna_app_get_current_location (app, error);
    if (!node)
        return NULL;

    if (!streq ("fs", donna_node_get_domain (node)))
    {
        gchar *fl = donna_node_get_full_location (node);
        g_set_error (error, DONNA_APP_ERROR, DONNA_APP_ERROR_OTHER,
                "Cannot get current dirname: "
                "current location (%s) of active-list is not in domain 'fs'",
                fl);
        g_object_unref (node);
        g_free (fl);
        return NULL;
    }

    dirname = donna_node_get_location (node);
    g_object_unref (node);
    return dirname;
}

gchar *
donna_app_new_int_ref (DonnaApp       *app,
                       DonnaArgType    type,
                       gpointer        ptr)
{
    DonnaAppInterface *interface;

    g_return_val_if_fail (DONNA_IS_APP (app), NULL);
    g_return_val_if_fail (ptr != NULL, NULL);
    g_return_val_if_fail (type == DONNA_ARG_TYPE_TREEVIEW
            || type == DONNA_ARG_TYPE_NODE, NULL);

    interface = DONNA_APP_GET_INTERFACE (app);

    g_return_val_if_fail (interface != NULL, NULL);
    g_return_val_if_fail (interface->new_int_ref != NULL, NULL);

    return (*interface->new_int_ref) (app, type, ptr);
}

gpointer
donna_app_get_int_ref (DonnaApp       *app,
                       const gchar    *intref,
                       DonnaArgType    type)

{
    DonnaAppInterface *interface;

    g_return_val_if_fail (DONNA_IS_APP (app), NULL);
    g_return_val_if_fail (intref != NULL, NULL);
    g_return_val_if_fail (type != DONNA_ARG_TYPE_NOTHING, NULL);

    interface = DONNA_APP_GET_INTERFACE (app);

    g_return_val_if_fail (interface != NULL, NULL);
    g_return_val_if_fail (interface->get_int_ref != NULL, NULL);

    return (*interface->get_int_ref) (app, intref, type);
}

gboolean
donna_app_free_int_ref (DonnaApp       *app,
                        const gchar    *intref)
{
    DonnaAppInterface *interface;

    g_return_val_if_fail (DONNA_IS_APP (app), NULL);
    g_return_val_if_fail (intref != NULL, NULL);

    interface = DONNA_APP_GET_INTERFACE (app);

    g_return_val_if_fail (interface != NULL, NULL);
    g_return_val_if_fail (interface->free_int_ref != NULL, NULL);

    return (*interface->free_int_ref) (app, intref);
}

gboolean
donna_app_show_menu (DonnaApp       *app,
                     GPtrArray      *nodes,
                     const gchar    *menu,
                     GError        **error)
{
    DonnaAppInterface *interface;

    g_return_val_if_fail (DONNA_IS_APP (app), NULL);
    g_return_val_if_fail (nodes != NULL, NULL);

    if (G_UNLIKELY (nodes->len == 0))
    {
        g_set_error (error, DONNA_APP_ERROR, DONNA_APP_ERROR_OTHER,
                "Unable to show menu, empty array of nodes given");
        g_ptr_array_unref (nodes);
        return FALSE;
    }

    interface = DONNA_APP_GET_INTERFACE (app);

    g_return_val_if_fail (interface != NULL, NULL);
    g_return_val_if_fail (interface->show_menu != NULL, NULL);

    return (*interface->show_menu) (app, nodes, menu, error);
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

/* not static so it can be used from treeview.c with its own get_ct_data */
gboolean
_donna_app_filter_nodes (DonnaApp        *app,
                         GPtrArray       *nodes,
                         const gchar     *filter_str,
                         get_ct_data_fn   get_ct_data,
                         gpointer         data,
                         GError         **error)
{
    GError *err = NULL;
    DonnaFilter *filter;
    guint i;

    g_return_val_if_fail (get_ct_data != NULL, FALSE);
    g_return_val_if_fail (nodes != NULL, FALSE);
    g_return_val_if_fail (filter_str != NULL, FALSE);

    if (G_UNLIKELY (nodes->len == 0))
        return FALSE;

    filter = donna_app_get_filter (app, filter_str);
    if (G_UNLIKELY (!filter))
    {
        g_set_error (error, DONNA_APP_ERROR, DONNA_APP_ERROR_OTHER,
                "Failed to create a filter object for '%s'",
                filter_str);
        return FALSE;
    }

    for (i = 0; i < nodes->len; )
        if (!donna_filter_is_match (filter, nodes->pdata[i],
                    get_ct_data, data, &err))
        {
            if (err)
            {
                g_propagate_error (error, err);
                g_object_unref (filter);
                return FALSE;
            }
            /* last element comes here, hence no need to increment i */
            g_ptr_array_remove_index_fast (nodes, i);
        }
        else
            ++i;

    g_object_unref (filter);
    return TRUE;
}

gpointer
donna_app_get_ct_data (const gchar    *col_name,
                       DonnaApp       *app)
{
    DonnaAppInterface *interface;

    g_return_val_if_fail (col_name != NULL, NULL);
    g_return_val_if_fail (DONNA_IS_APP (app), NULL);

    interface = DONNA_APP_GET_INTERFACE (app);

    g_return_val_if_fail (interface != NULL, NULL);
    g_return_val_if_fail (interface->get_ct_data != NULL, NULL);

    return (*interface->get_ct_data) (app, col_name);
}

gboolean
donna_app_filter_nodes (DonnaApp       *app,
                        GPtrArray      *nodes,
                        const gchar    *filter_str,
                        GError       **error)
{
    g_return_val_if_fail (DONNA_IS_APP (app), FALSE);

    return _donna_app_filter_nodes (app, nodes, filter_str,
            (get_ct_data_fn) donna_app_get_ct_data, app, error);
}

gboolean
donna_app_nodes_io (DonnaApp       *app,
                    GPtrArray      *nodes,
                    DonnaIoType     io_type,
                    DonnaNode      *dest,
                    GError        **error)
{
    DonnaAppInterface *interface;

    g_return_val_if_fail (DONNA_IS_APP (app), FALSE);
    g_return_val_if_fail (nodes != NULL, FALSE);
    g_return_val_if_fail (io_type == DONNA_IO_COPY || io_type == DONNA_IO_MOVE
            || io_type == DONNA_IO_DELETE, FALSE);
    if (io_type != DONNA_IO_DELETE)
        g_return_val_if_fail (DONNA_IS_NODE (dest), FALSE);

    interface = DONNA_APP_GET_INTERFACE (app);

    g_return_val_if_fail (interface != NULL, FALSE);
    g_return_val_if_fail (interface->nodes_io != NULL, FALSE);

    return (*interface->nodes_io) (app, nodes, io_type, dest, error);
}

gboolean
donna_app_register_drop (DonnaApp       *app,
                         const gchar    *name,
                         GError        **error)
{
    DonnaAppInterface *interface;

    g_return_val_if_fail (DONNA_IS_APP (app), FALSE);
    g_return_val_if_fail (name != NULL, FALSE);

    interface = DONNA_APP_GET_INTERFACE (app);

    g_return_val_if_fail (interface != NULL, FALSE);
    g_return_val_if_fail (interface->register_drop != NULL, FALSE);

    return (*interface->register_drop) (app, name, error);
}

gboolean
donna_app_register_set (DonnaApp       *app,
                        const gchar    *name,
                        DonnaRegisterType type,
                        GPtrArray      *nodes,
                        GError        **error)
{
    DonnaAppInterface *interface;

    g_return_val_if_fail (DONNA_IS_APP (app), FALSE);
    g_return_val_if_fail (name != NULL, FALSE);
    g_return_val_if_fail (nodes != NULL, FALSE);
    g_return_val_if_fail (nodes->len > 0, FALSE);

    interface = DONNA_APP_GET_INTERFACE (app);

    g_return_val_if_fail (interface != NULL, FALSE);
    g_return_val_if_fail (interface->register_set != NULL, FALSE);

    return (*interface->register_set) (app, name, type, nodes, error);
}

gboolean
donna_app_register_add_nodes (DonnaApp       *app,
                              const gchar    *name,
                              GPtrArray      *nodes,
                              GError        **error)
{
    DonnaAppInterface *interface;

    g_return_val_if_fail (DONNA_IS_APP (app), FALSE);
    g_return_val_if_fail (name != NULL, FALSE);
    g_return_val_if_fail (nodes != NULL, FALSE);
    g_return_val_if_fail (nodes->len > 0, FALSE);

    interface = DONNA_APP_GET_INTERFACE (app);

    g_return_val_if_fail (interface != NULL, FALSE);
    g_return_val_if_fail (interface->register_add_nodes != NULL, FALSE);

    return (*interface->register_add_nodes) (app, name, nodes, error);
}

gboolean
donna_app_register_set_type (DonnaApp       *app,
                             const gchar    *name,
                             DonnaRegisterType type,
                             GError        **error)
{
    DonnaAppInterface *interface;

    g_return_val_if_fail (DONNA_IS_APP (app), FALSE);
    g_return_val_if_fail (name != NULL, FALSE);

    interface = DONNA_APP_GET_INTERFACE (app);

    g_return_val_if_fail (interface != NULL, FALSE);
    g_return_val_if_fail (interface->register_set_type != NULL, FALSE);

    return (*interface->register_set_type) (app, name, type, error);
}

gboolean
donna_app_register_get_nodes (DonnaApp       *app,
                              const gchar    *name,
                              DonnaDropRegister drop,
                              DonnaRegisterType *type,
                              GPtrArray     **nodes,
                              GError        **error)
{
    DonnaAppInterface *interface;

    g_return_val_if_fail (DONNA_IS_APP (app), FALSE);
    g_return_val_if_fail (name != NULL, FALSE);
    g_return_val_if_fail (drop == DONNA_DROP_REGISTER_NOT
            || drop == DONNA_DROP_REGISTER_ALWAYS
            || drop == DONNA_DROP_REGISTER_ON_CUT, FALSE);

    interface = DONNA_APP_GET_INTERFACE (app);

    g_return_val_if_fail (interface != NULL, FALSE);
    g_return_val_if_fail (interface->register_get_nodes != NULL, FALSE);

    return (*interface->register_get_nodes) (app, name, drop, type, nodes, error);
}

gboolean
donna_app_register_load (DonnaApp       *app,
                         const gchar    *name,
                         const gchar    *file,
                         DonnaRegisterFile file_type,
                         GError        **error)
{
    DonnaAppInterface *interface;

    g_return_val_if_fail (DONNA_IS_APP (app), FALSE);
    g_return_val_if_fail (name != NULL, FALSE);
    g_return_val_if_fail (file != NULL, FALSE);
    g_return_val_if_fail (file_type == DONNA_REGISTER_FILE_NODES
            || file_type == DONNA_REGISTER_FILE_FILE
            || file_type == DONNA_REGISTER_FILE_URIS, FALSE);

    interface = DONNA_APP_GET_INTERFACE (app);

    g_return_val_if_fail (interface != NULL, FALSE);
    g_return_val_if_fail (interface->register_load != NULL, FALSE);

    return (*interface->register_load) (app, name, file, file_type, error);
}

gboolean
donna_app_register_save (DonnaApp       *app,
                         const gchar    *name,
                         const gchar    *file,
                         DonnaRegisterFile file_type,
                         GError        **error)
{
    DonnaAppInterface *interface;

    g_return_val_if_fail (DONNA_IS_APP (app), FALSE);
    g_return_val_if_fail (name != NULL, FALSE);
    g_return_val_if_fail (file != NULL, FALSE);
    g_return_val_if_fail (file_type == DONNA_REGISTER_FILE_NODES
            || file_type == DONNA_REGISTER_FILE_FILE
            || file_type == DONNA_REGISTER_FILE_URIS, FALSE);

    interface = DONNA_APP_GET_INTERFACE (app);

    g_return_val_if_fail (interface != NULL, FALSE);
    g_return_val_if_fail (interface->register_save != NULL, FALSE);

    return (*interface->register_save) (app, name, file, file_type, error);
}

gchar *
donna_app_ask_text (DonnaApp       *app,
                    const gchar    *title,
                    const gchar    *details,
                    const gchar    *main_default,
                    const gchar   **other_defaults,
                    GError        **error)
{
    DonnaAppInterface *interface;

    g_return_val_if_fail (DONNA_IS_APP (app), FALSE);
    g_return_val_if_fail (title != NULL, FALSE);

    interface = DONNA_APP_GET_INTERFACE (app);

    g_return_val_if_fail (interface != NULL, FALSE);
    g_return_val_if_fail (interface->ask_text != NULL, FALSE);

    return (*interface->ask_text) (app, title, details, main_default,
            other_defaults, error);
}
