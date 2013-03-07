
#include "columntype.h"

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

DonnaSharedString **
donna_columntype_get_props (DonnaColumnType    *ct,
                            const gchar        *tv_name,
                            const gchar        *col_name)
{
    DonnaColumnTypeInterface *interface;

    g_return_val_if_fail (DONNA_IS_COLUMNTYPE (ct), NULL);
    g_return_val_if_fail (tv_name != NULL, NULL);
    g_return_val_if_fail (col_name != NULL, NULL);

    interface = DONNA_COLUMNTYPE_GET_INTERFACE (ct);

    g_return_val_if_fail (interface != NULL, NULL);
    g_return_val_if_fail (interface->get_props != NULL, NULL);

    return (*interface->get_props) (ct, tv_name, col_name);
}

GPtrArray *
donna_columntype_render (DonnaColumnType    *ct,
                         const gchar        *tv_name,
                         const gchar        *col_name,
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

    return (*interface->render) (ct, tv_name, col_name, index, node, renderer);
}

GtkMenu *
donna_columntype_get_options_menu (DonnaColumnType  *ct,
                                   const gchar      *tv_name,
                                   const gchar      *col_name)
{
    DonnaColumnTypeInterface *interface;

    g_return_val_if_fail (DONNA_IS_COLUMNTYPE (ct), NULL);
    g_return_val_if_fail (tv_name != NULL, NULL);
    g_return_val_if_fail (col_name != NULL, NULL);

    interface = DONNA_COLUMNTYPE_GET_INTERFACE (ct);

    g_return_val_if_fail (interface != NULL, NULL);
    g_return_val_if_fail (interface->get_options_menu != NULL, NULL);

    return (*interface->get_options_menu) (ct, tv_name, col_name);
}

gboolean
donna_columntype_handle_context (DonnaColumnType    *ct,
                                 const gchar        *tv_name,
                                 const gchar        *col_name,
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

    return (*interface->handle_context) (ct, tv_name, col_name, node, treeview);
}

gboolean
donna_columntype_set_tooltip (DonnaColumnType    *ct,
                              const gchar        *tv_name,
                              const gchar        *col_name,
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

    return (*interface->set_tooltip) (ct, tv_name, col_name, index, node, tooltip);
}

gint
donna_columntype_node_cmp (DonnaColumnType    *ct,
                           const gchar        *tv_name,
                           const gchar        *col_name,
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

    return (*interface->node_cmp) (ct, tv_name, col_name, node1, node2);
}
