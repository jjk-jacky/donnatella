
#include "columntype.h"
#include "conf.h"
#include "macros.h"

static void
donna_columntype_default_init (DonnaColumnTypeInterface *klass)
{
}

G_DEFINE_INTERFACE (DonnaColumnType, donna_columntype, G_TYPE_OBJECT)

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

gpointer
donna_columntype_get_data (DonnaColumnType    *ct,
                           const gchar        *tv_name,
                           const gchar        *col_name)
{
    DonnaColumnTypeInterface *interface;

    g_return_val_if_fail (DONNA_IS_COLUMNTYPE (ct), NULL);
    g_return_val_if_fail (tv_name != NULL, NULL);
    g_return_val_if_fail (col_name != NULL, NULL);

    interface = DONNA_COLUMNTYPE_GET_INTERFACE (ct);

    g_return_val_if_fail (interface != NULL, NULL);
    g_return_val_if_fail (interface->get_data != NULL, NULL);

    return (*interface->get_data) (ct, tv_name, col_name);
}

DonnaColumnTypeNeed
donna_columntype_refresh_data (DonnaColumnType  *ct,
                               const gchar        *tv_name,
                               const gchar        *col_name,
                               gpointer           *data)
{
    DonnaColumnTypeInterface *interface;

    g_return_val_if_fail (DONNA_IS_COLUMNTYPE (ct), DONNA_COLUMNTYPE_NEED_NOTHING);
    g_return_val_if_fail (tv_name != NULL, DONNA_COLUMNTYPE_NEED_NOTHING);
    g_return_val_if_fail (col_name != NULL, DONNA_COLUMNTYPE_NEED_NOTHING);
    g_return_val_if_fail (data != NULL && *data != NULL, DONNA_COLUMNTYPE_NEED_NOTHING);

    interface = DONNA_COLUMNTYPE_GET_INTERFACE (ct);

    g_return_val_if_fail (interface != NULL, DONNA_COLUMNTYPE_NEED_NOTHING);
    g_return_val_if_fail (interface->refresh_data != NULL, DONNA_COLUMNTYPE_NEED_NOTHING);

    return (*interface->refresh_data) (ct, tv_name, col_name, data);
}

void
donna_columntype_free_data (DonnaColumnType    *ct,
                            const gchar        *tv_name,
                            const gchar        *col_name,
                            gpointer            data)
{
    DonnaColumnTypeInterface *interface;

    g_return_if_fail (DONNA_IS_COLUMNTYPE (ct));
    g_return_if_fail (tv_name != NULL);
    g_return_if_fail (col_name != NULL);
    g_return_if_fail (data != NULL);

    interface = DONNA_COLUMNTYPE_GET_INTERFACE (ct);

    g_return_if_fail (interface != NULL);
    g_return_if_fail (interface->free_data != NULL);

    return (*interface->free_data) (ct, tv_name, col_name, data);
}

GPtrArray *
donna_columntype_get_props (DonnaColumnType    *ct,
                            const gchar        *tv_name,
                            const gchar        *col_name,
                            gpointer            data)
{
    DonnaColumnTypeInterface *interface;

    g_return_val_if_fail (DONNA_IS_COLUMNTYPE (ct), NULL);
    g_return_val_if_fail (tv_name != NULL, NULL);
    g_return_val_if_fail (col_name != NULL, NULL);

    interface = DONNA_COLUMNTYPE_GET_INTERFACE (ct);

    g_return_val_if_fail (interface != NULL, NULL);
    g_return_val_if_fail (interface->get_props != NULL, NULL);

    return (*interface->get_props) (ct, tv_name, col_name, data);
}

GtkSortType
donna_columntype_get_default_sort_order (DonnaColumnType    *ct,
                                         const gchar        *tv_name,
                                         const gchar        *col_name,
                                         gpointer            data)
{
    DonnaColumnTypeInterface *interface;

    g_return_val_if_fail (DONNA_IS_COLUMNTYPE (ct), GTK_SORT_ASCENDING);
    g_return_val_if_fail (tv_name != NULL, GTK_SORT_ASCENDING);
    g_return_val_if_fail (col_name != NULL, GTK_SORT_ASCENDING);

    interface = DONNA_COLUMNTYPE_GET_INTERFACE (ct);

    g_return_val_if_fail (interface != NULL, GTK_SORT_ASCENDING);
    g_return_val_if_fail (interface->get_default_sort_order != NULL, GTK_SORT_ASCENDING);

    return (*interface->get_default_sort_order) (ct, tv_name, col_name, data);
}

GtkMenu *
donna_columntype_get_options_menu (DonnaColumnType  *ct,
                                   const gchar      *tv_name,
                                   const gchar      *col_name,
                                   gpointer          data)
{
    DonnaColumnTypeInterface *interface;

    g_return_val_if_fail (DONNA_IS_COLUMNTYPE (ct), NULL);
    g_return_val_if_fail (tv_name != NULL, NULL);
    g_return_val_if_fail (col_name != NULL, NULL);

    interface = DONNA_COLUMNTYPE_GET_INTERFACE (ct);

    g_return_val_if_fail (interface != NULL, NULL);
    g_return_val_if_fail (interface->get_options_menu != NULL, NULL);

    return (*interface->get_options_menu) (ct, tv_name, col_name, data);
}

gboolean
donna_columntype_handle_context (DonnaColumnType    *ct,
                                 const gchar        *tv_name,
                                 const gchar        *col_name,
                                 gpointer            data,
                                 DonnaNode          *node,
                                 DonnaTreeView      *treeview)
{
    DonnaColumnTypeInterface *interface;

    g_return_val_if_fail (DONNA_IS_COLUMNTYPE (ct), FALSE);
    g_return_val_if_fail (tv_name != NULL, FALSE);
    g_return_val_if_fail (col_name != NULL, FALSE);
    g_return_val_if_fail (DONNA_IS_NODE (node), FALSE);
    g_return_val_if_fail (DONNA_IS_TREE_VIEW (treeview), FALSE);

    interface = DONNA_COLUMNTYPE_GET_INTERFACE (ct);

    g_return_val_if_fail (interface != NULL, FALSE);
    g_return_val_if_fail (interface->handle_context != NULL, FALSE);

    return (*interface->handle_context) (ct, tv_name, col_name, data,
            node, treeview);
}

GPtrArray *
donna_columntype_render (DonnaColumnType    *ct,
                         const gchar        *tv_name,
                         const gchar        *col_name,
                         gpointer            data,
                         guint               index,
                         DonnaNode          *node,
                         GtkCellRenderer    *renderer)
{
    DonnaColumnTypeInterface *interface;

    g_return_val_if_fail (DONNA_IS_COLUMNTYPE (ct), NULL);
    g_return_val_if_fail (tv_name != NULL, NULL);
    g_return_val_if_fail (col_name != NULL, NULL);
    g_return_val_if_fail (index > 0, NULL);
    g_return_val_if_fail (DONNA_IS_NODE (node), NULL);
    g_return_val_if_fail (GTK_IS_CELL_RENDERER (renderer), NULL);

    interface = DONNA_COLUMNTYPE_GET_INTERFACE (ct);

    g_return_val_if_fail (interface != NULL, NULL);
    g_return_val_if_fail (interface->render != NULL, NULL);

    return (*interface->render) (ct, tv_name, col_name, data,
            index, node, renderer);
}

gboolean
donna_columntype_set_tooltip (DonnaColumnType    *ct,
                              const gchar        *tv_name,
                              const gchar        *col_name,
                              gpointer            data,
                              guint               index,
                              DonnaNode          *node,
                              GtkTooltip         *tooltip)
{
    DonnaColumnTypeInterface *interface;

    g_return_val_if_fail (DONNA_IS_COLUMNTYPE (ct), FALSE);
    g_return_val_if_fail (tv_name != NULL, FALSE);
    g_return_val_if_fail (col_name != NULL, FALSE);
    g_return_val_if_fail (DONNA_IS_NODE (node), FALSE);
    g_return_val_if_fail (GTK_IS_TOOLTIP (tooltip), FALSE);

    interface = DONNA_COLUMNTYPE_GET_INTERFACE (ct);

    g_return_val_if_fail (interface != NULL, FALSE);
    g_return_val_if_fail (interface->set_tooltip != NULL, FALSE);

    return (*interface->set_tooltip) (ct, tv_name, col_name, data,
            index, node, tooltip);
}

gint
donna_columntype_node_cmp (DonnaColumnType    *ct,
                           const gchar        *tv_name,
                           const gchar        *col_name,
                           gpointer            data,
                           DonnaNode          *node1,
                           DonnaNode          *node2)
{
    DonnaColumnTypeInterface *interface;

    g_return_val_if_fail (DONNA_IS_COLUMNTYPE (ct), 0);
    g_return_val_if_fail (tv_name != NULL, 0);
    g_return_val_if_fail (col_name != NULL, 0);
    g_return_val_if_fail (DONNA_IS_NODE (node1), 0);
    g_return_val_if_fail (DONNA_IS_NODE (node2), 0);

    interface = DONNA_COLUMNTYPE_GET_INTERFACE (ct);

    g_return_val_if_fail (interface != NULL, 0);
    g_return_val_if_fail (interface->node_cmp != NULL, 0);

    return (*interface->node_cmp) (ct, tv_name, col_name, data, node1, node2);
}

