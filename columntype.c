
#include <string.h>
#include "columntype.h"
#include "app.h"
#include "conf.h"
#include "macros.h"

static GtkSortType
default_get_default_sort_order (DonnaColumnType    *ct,
                                const gchar        *tv_name,
                                const gchar        *col_name,
                                const gchar        *arr_name,
                                gpointer            data)
{
    DonnaColumnTypeInterface *interface;
    DonnaApp *app;
    const gchar *type;
    gchar buf[55], *b = buf;
    GtkSortType order;

    g_object_get (ct, "app", &app, NULL);
    type = donna_columntype_get_name (ct);
    /* 42 == 55 - strlen ("columntypes/") - 1 */
    if (G_UNLIKELY (strnlen (type, 42) >= 42))
        b = g_strconcat ("columntypes/", type, NULL);
    else
        strcpy (stpcpy (buf, "columntypes/"), type);

    order = (donna_config_get_boolean_column (donna_app_peek_config (app),
                tv_name, col_name, arr_name, b, "desc_first", FALSE))
        ? GTK_SORT_DESCENDING : GTK_SORT_ASCENDING;

    if (G_UNLIKELY (b != buf))
        g_free (b);
    g_object_unref (app);
    return order;
}

static GtkMenu *
default_get_options_menu (DonnaColumnType    *ct,
                          gpointer            data)
{
    return NULL;
}

static gboolean
default_edit (DonnaColumnType    *ct,
              gpointer            data,
              DonnaNode          *node,
              GtkCellRenderer   **renderer,
              renderer_edit_fn    renderer_edit,
              gpointer            re_data,
              DonnaTreeView      *treeview,
              GError            **error)
{
    g_set_error (error, DONNA_COLUMNTYPE_ERROR, DONNA_COLUMNTYPE_ERROR_OTHER,
            "ColumnType '%s': No editing supported",
            donna_columntype_get_name (ct));
    return FALSE;
}

static gboolean
default_set_tooltip (DonnaColumnType    *ct,
                     gpointer            data,
                     guint               index,
                     DonnaNode          *node,
                     GtkTooltip         *tooltip)
{
    return FALSE;
}

static void
donna_columntype_default_init (DonnaColumnTypeInterface *interface)
{
    interface->get_default_sort_order   = default_get_default_sort_order;
    interface->get_options_menu         = default_get_options_menu;
    interface->edit                     = default_edit;
    interface->set_tooltip              = default_set_tooltip;

    g_object_interface_install_property (interface,
            g_param_spec_object ("app", "app", "Application",
                DONNA_TYPE_APP,
                G_PARAM_READWRITE | G_PARAM_CONSTRUCT_ONLY));
}

G_DEFINE_INTERFACE (DonnaColumnType, donna_columntype, G_TYPE_OBJECT)

const gchar *
donna_columntype_get_name (DonnaColumnType *ct)
{
    DonnaColumnTypeInterface *interface;

    g_return_val_if_fail (DONNA_IS_COLUMNTYPE (ct), NULL);

    interface = DONNA_COLUMNTYPE_GET_INTERFACE (ct);

    g_return_val_if_fail (interface != NULL, NULL);
    g_return_val_if_fail (interface->get_name != NULL, NULL);

    return (*interface->get_name) (ct);
}

const gchar *
donna_columntype_get_renderers (DonnaColumnType  *ct)
{
    DonnaColumnTypeInterface *interface;

    g_return_val_if_fail (DONNA_IS_COLUMNTYPE (ct), NULL);

    interface = DONNA_COLUMNTYPE_GET_INTERFACE (ct);

    g_return_val_if_fail (interface != NULL, NULL);
    g_return_val_if_fail (interface->get_renderers != NULL, NULL);

    return (*interface->get_renderers) (ct);
}

DonnaColumnTypeNeed
donna_columntype_refresh_data (DonnaColumnType  *ct,
                               const gchar        *tv_name,
                               const gchar        *col_name,
                               const gchar        *arr_name,
                               gpointer           *data)
{
    DonnaColumnTypeInterface *interface;

    g_return_val_if_fail (DONNA_IS_COLUMNTYPE (ct), DONNA_COLUMNTYPE_NEED_NOTHING);
    g_return_val_if_fail (col_name != NULL, DONNA_COLUMNTYPE_NEED_NOTHING);
    g_return_val_if_fail (data != NULL, DONNA_COLUMNTYPE_NEED_NOTHING);

    interface = DONNA_COLUMNTYPE_GET_INTERFACE (ct);

    g_return_val_if_fail (interface != NULL, DONNA_COLUMNTYPE_NEED_NOTHING);
    g_return_val_if_fail (interface->refresh_data != NULL, DONNA_COLUMNTYPE_NEED_NOTHING);

    return (*interface->refresh_data) (ct, tv_name, col_name, arr_name, data);
}

void
donna_columntype_free_data (DonnaColumnType    *ct,
                            gpointer            data)
{
    DonnaColumnTypeInterface *interface;

    g_return_if_fail (DONNA_IS_COLUMNTYPE (ct));

    if (G_UNLIKELY (!data))
        return;

    interface = DONNA_COLUMNTYPE_GET_INTERFACE (ct);

    g_return_if_fail (interface != NULL);
    g_return_if_fail (interface->free_data != NULL);

    return (*interface->free_data) (ct, data);
}

GPtrArray *
donna_columntype_get_props (DonnaColumnType    *ct,
                            gpointer            data)
{
    DonnaColumnTypeInterface *interface;

    g_return_val_if_fail (DONNA_IS_COLUMNTYPE (ct), NULL);

    interface = DONNA_COLUMNTYPE_GET_INTERFACE (ct);

    g_return_val_if_fail (interface != NULL, NULL);
    g_return_val_if_fail (interface->get_props != NULL, NULL);

    return (*interface->get_props) (ct, data);
}

GtkSortType
donna_columntype_get_default_sort_order (DonnaColumnType    *ct,
                                         const gchar        *tv_name,
                                         const gchar        *col_name,
                                         const gchar        *arr_name,
                                         gpointer            data)
{
    DonnaColumnTypeInterface *interface;

    g_return_val_if_fail (DONNA_IS_COLUMNTYPE (ct), GTK_SORT_ASCENDING);
    g_return_val_if_fail (tv_name != NULL, GTK_SORT_ASCENDING);
    g_return_val_if_fail (col_name != NULL, GTK_SORT_ASCENDING);

    interface = DONNA_COLUMNTYPE_GET_INTERFACE (ct);

    g_return_val_if_fail (interface != NULL, GTK_SORT_ASCENDING);
    g_return_val_if_fail (interface->get_default_sort_order != NULL, GTK_SORT_ASCENDING);

    return (*interface->get_default_sort_order) (ct, tv_name, col_name, arr_name, data);
}

GtkMenu *
donna_columntype_get_options_menu (DonnaColumnType  *ct,
                                   gpointer          data)
{
    DonnaColumnTypeInterface *interface;

    g_return_val_if_fail (DONNA_IS_COLUMNTYPE (ct), NULL);

    interface = DONNA_COLUMNTYPE_GET_INTERFACE (ct);

    g_return_val_if_fail (interface != NULL, NULL);
    g_return_val_if_fail (interface->get_options_menu != NULL, NULL);

    return (*interface->get_options_menu) (ct, data);
}

gboolean
donna_columntype_edit (DonnaColumnType    *ct,
                       gpointer            data,
                       DonnaNode          *node,
                       GtkCellRenderer   **renderers,
                       renderer_edit_fn    renderer_edit,
                       gpointer            re_data,
                       DonnaTreeView      *treeview,
                       GError            **error)
{
    DonnaColumnTypeInterface *interface;

    g_return_val_if_fail (DONNA_IS_COLUMNTYPE (ct), FALSE);
    g_return_val_if_fail (DONNA_IS_NODE (node), FALSE);
    g_return_val_if_fail (renderers != NULL && GTK_IS_CELL_RENDERER (renderers[0]), FALSE);
    g_return_val_if_fail (renderer_edit != NULL, FALSE);
    g_return_val_if_fail (DONNA_IS_TREE_VIEW (treeview), FALSE);

    interface = DONNA_COLUMNTYPE_GET_INTERFACE (ct);

    g_return_val_if_fail (interface != NULL, FALSE);
    g_return_val_if_fail (interface->edit != NULL, FALSE);

    return (*interface->edit) (ct, data, node, renderers,
            renderer_edit, re_data, treeview, error);
}

GPtrArray *
donna_columntype_render (DonnaColumnType    *ct,
                         gpointer            data,
                         guint               index,
                         DonnaNode          *node,
                         GtkCellRenderer    *renderer)
{
    DonnaColumnTypeInterface *interface;

    g_return_val_if_fail (DONNA_IS_COLUMNTYPE (ct), NULL);
    g_return_val_if_fail (DONNA_IS_NODE (node), NULL);
    g_return_val_if_fail (GTK_IS_CELL_RENDERER (renderer), NULL);

    interface = DONNA_COLUMNTYPE_GET_INTERFACE (ct);

    g_return_val_if_fail (interface != NULL, NULL);
    g_return_val_if_fail (interface->render != NULL, NULL);

    return (*interface->render) (ct, data, index, node, renderer);
}

gboolean
donna_columntype_set_tooltip (DonnaColumnType    *ct,
                              gpointer            data,
                              guint               index,
                              DonnaNode          *node,
                              GtkTooltip         *tooltip)
{
    DonnaColumnTypeInterface *interface;

    g_return_val_if_fail (DONNA_IS_COLUMNTYPE (ct), FALSE);
    g_return_val_if_fail (DONNA_IS_NODE (node), FALSE);
    g_return_val_if_fail (GTK_IS_TOOLTIP (tooltip), FALSE);

    interface = DONNA_COLUMNTYPE_GET_INTERFACE (ct);

    g_return_val_if_fail (interface != NULL, FALSE);
    g_return_val_if_fail (interface->set_tooltip != NULL, FALSE);

    return (*interface->set_tooltip) (ct, data, index, node, tooltip);
}

gint
donna_columntype_node_cmp (DonnaColumnType    *ct,
                           gpointer            data,
                           DonnaNode          *node1,
                           DonnaNode          *node2)
{
    DonnaColumnTypeInterface *interface;

    g_return_val_if_fail (DONNA_IS_COLUMNTYPE (ct), 0);
    g_return_val_if_fail (DONNA_IS_NODE (node1), 0);
    g_return_val_if_fail (DONNA_IS_NODE (node2), 0);

    interface = DONNA_COLUMNTYPE_GET_INTERFACE (ct);

    g_return_val_if_fail (interface != NULL, 0);
    g_return_val_if_fail (interface->node_cmp != NULL, 0);

    return (*interface->node_cmp) (ct, data, node1, node2);
}

gboolean
donna_columntype_is_match_filter (DonnaColumnType    *ct,
                                  const gchar        *filter,
                                  gpointer           *filter_data,
                                  gpointer            data,
                                  DonnaNode          *node,
                                  GError            **error)
{
    DonnaColumnTypeInterface *interface;

    g_return_val_if_fail (DONNA_IS_COLUMNTYPE (ct), 0);
    g_return_val_if_fail (filter != NULL, 0);
    g_return_val_if_fail (filter_data != NULL, 0);
    g_return_val_if_fail (DONNA_IS_NODE (node), 0);

    interface = DONNA_COLUMNTYPE_GET_INTERFACE (ct);

    g_return_val_if_fail (interface != NULL, 0);
    if (!interface->is_match_filter)
    {
        g_set_error (error, DONNA_COLUMNTYPE_ERROR,
                DONNA_COLUMNTYPE_ERROR_OTHER,
                "ColumnType '%s': no filtering supported",
                donna_columntype_get_name (ct));
        return FALSE;
    }

    return (*interface->is_match_filter) (ct, filter, filter_data, data,
            node, error);
}

void
donna_columntype_free_filter_data (DonnaColumnType   *ct,
                                   gpointer           filter_data)
{
    DonnaColumnTypeInterface *interface;

    g_return_val_if_fail (DONNA_IS_COLUMNTYPE (ct), 0);

    if (G_UNLIKELY (!filter_data))
        return;

    interface = DONNA_COLUMNTYPE_GET_INTERFACE (ct);

    g_return_val_if_fail (interface != NULL, 0);
    g_return_val_if_fail (interface->free_filter_data != NULL, 0);

    return (*interface->free_filter_data) (ct, filter_data);
}

/** donna_columntype_new_floating_window:
 * @tree: #DonnaTreeView where the column is
 * @destroy_on_sel_changed: Whether to destroy the window on selection change
 *
 * Helper function to create a little window that can be used to perform some
 * property editing on a given node (or selection of nodes).
 * This will create the window, attach it to @tree, remove decorations, set
 * %GDK_WINDOW_TYPE_HINT_UTILITY, set position to mouse pointer, set a border of
 * 6 pixels, make it non-resizable, and make it destroyed on location change on
 * @tree, as well as selection change if @destroy_on_sel_changed is %TRUE.
 *
 * You should call donna_app_set_floating_window() on the returned window after
 * having made it visible, otherwise this could lead to an instant destruction
 * on the window (as this call can destroy a previous floating window, thus
 * giving the focus back to app, thus leading to destruction of the new floating
 * window).
 *
 * Return: (transfer full): A new g_object_ref_sink()ed #GtkWindow
 */
inline GtkWindow *
donna_columntype_new_floating_window (DonnaTreeView *tree,
                                      gboolean       destroy_on_sel_changed)
{
    GtkWindow *win;

    win = (GtkWindow *) gtk_window_new (GTK_WINDOW_TOPLEVEL);
    gtk_window_set_decorated (win, FALSE);
    gtk_window_set_type_hint (win, GDK_WINDOW_TYPE_HINT_UTILITY);
    gtk_window_set_attached_to (win, (GtkWidget *) tree);
    gtk_window_set_position (win, GTK_WIN_POS_MOUSE);
    gtk_window_set_resizable (win, FALSE);
    g_object_set (win, "border-width", 6, NULL);
    g_object_ref_sink (win);
    g_signal_connect_swapped (tree, "notify::location",
            (GCallback) gtk_widget_destroy, win);
    if (destroy_on_sel_changed)
    {
        GtkTreeSelection *sel;

        sel = gtk_tree_view_get_selection ((GtkTreeView *) tree);
        g_signal_connect_swapped (sel, "changed",
                (GCallback) gtk_widget_destroy, win);
    }
    return win;
}
