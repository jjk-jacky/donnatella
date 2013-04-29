
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
default_handle_context (DonnaColumnType    *ct,
                        gpointer            data,
                        DonnaNode          *node,
                        DonnaTreeView      *treeview)
{
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
    interface->handle_context           = default_handle_context;
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
    g_return_val_if_fail (tv_name != NULL, DONNA_COLUMNTYPE_NEED_NOTHING);
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
donna_columntype_handle_context (DonnaColumnType    *ct,
                                 gpointer            data,
                                 DonnaNode          *node,
                                 DonnaTreeView      *treeview)
{
    DonnaColumnTypeInterface *interface;

    g_return_val_if_fail (DONNA_IS_COLUMNTYPE (ct), FALSE);
    g_return_val_if_fail (DONNA_IS_NODE (node), FALSE);
    g_return_val_if_fail (DONNA_IS_TREE_VIEW (treeview), FALSE);

    interface = DONNA_COLUMNTYPE_GET_INTERFACE (ct);

    g_return_val_if_fail (interface != NULL, FALSE);
    g_return_val_if_fail (interface->handle_context != NULL, FALSE);

    return (*interface->handle_context) (ct, data, node, treeview);
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
