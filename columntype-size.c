
#include <glib-object.h>
#include "columntype.h"
#include "columntype-size.h"
#include "node.h"
#include "donna.h"
#include "conf.h"
#include "size.h"
#include "macros.h"

struct tv_col_data
{
    gchar   *property;
    gchar   *format;
    gchar   *format_tooltip;
    guint    digits     : 2;
    guint    long_unit  : 1;
};

struct _DonnaColumnTypeSizePrivate
{
    DonnaApp                    *app;
};

static void             ct_size_finalize            (GObject            *object);

/* ColumnType */
static const gchar *    ct_size_get_renderers       (DonnaColumnType    *ct);
static gpointer         ct_size_get_data            (DonnaColumnType    *ct,
                                                     const gchar        *tv_name,
                                                     const gchar        *col_name);
static DonnaColumnTypeNeed ct_size_refresh_data     (DonnaColumnType    *ct,
                                                     const gchar        *tv_name,
                                                     const gchar        *col_name,
                                                     gpointer           *data);
static void             ct_size_free_data           (DonnaColumnType    *ct,
                                                     const gchar        *tv_name,
                                                     const gchar        *col_name,
                                                     gpointer            data);
static GPtrArray *      ct_size_get_props           (DonnaColumnType    *ct,
                                                     const gchar        *tv_name,
                                                     const gchar        *col_name,
                                                     gpointer            data);
static GtkSortType      ct_size_get_default_sort_order
                                                    (DonnaColumnType    *ct,
                                                     const gchar        *tv_name,
                                                     const gchar        *col_name,
                                                     gpointer            data);
static GtkMenu *        ct_size_get_options_menu    (DonnaColumnType    *ct,
                                                     const gchar        *tv_name,
                                                     const gchar        *col_name,
                                                     gpointer            data);
static gboolean         ct_size_handle_context      (DonnaColumnType    *ct,
                                                     const gchar        *tv_name,
                                                     const gchar        *col_name,
                                                     gpointer            data,
                                                     DonnaNode          *node,
                                                     DonnaTreeView      *treeview);
static GPtrArray *      ct_size_render              (DonnaColumnType    *ct,
                                                     const gchar        *tv_name,
                                                     const gchar        *col_name,
                                                     gpointer            data,
                                                     guint               index,
                                                     DonnaNode          *node,
                                                     GtkCellRenderer    *renderer);
static gint             ct_size_node_cmp            (DonnaColumnType    *ct,
                                                     const gchar        *tv_name,
                                                     const gchar        *col_name,
                                                     gpointer            data,
                                                     DonnaNode          *node1,
                                                     DonnaNode          *node2);
static gboolean         ct_size_set_tooltip         (DonnaColumnType    *ct,
                                                     const gchar        *tv_name,
                                                     const gchar        *col_name,
                                                     gpointer            data,
                                                     guint               index,
                                                     DonnaNode          *node,
                                                     GtkTooltip         *tooltip);

static void
ct_size_columntype_init (DonnaColumnTypeInterface *interface)
{
    interface->get_renderers            = ct_size_get_renderers;
    interface->get_data                 = ct_size_get_data;
    interface->refresh_data             = ct_size_refresh_data;
    interface->free_data                = ct_size_free_data;
    interface->get_props                = ct_size_get_props;
    interface->get_default_sort_order   = ct_size_get_default_sort_order;
    interface->get_options_menu         = ct_size_get_options_menu;
    interface->handle_context           = ct_size_handle_context;
    interface->render                   = ct_size_render;
    interface->set_tooltip              = ct_size_set_tooltip;
    interface->node_cmp                 = ct_size_node_cmp;
}

static void
donna_column_type_size_class_init (DonnaColumnTypeSizeClass *klass)
{
    GObjectClass *o_class;

    o_class = (GObjectClass *) klass;
    o_class->finalize = ct_size_finalize;

    g_type_class_add_private (klass, sizeof (DonnaColumnTypeSizePrivate));
}

static void
donna_column_type_size_init (DonnaColumnTypeSize *ct)
{
    DonnaColumnTypeSizePrivate *priv;

    priv = ct->priv = G_TYPE_INSTANCE_GET_PRIVATE (ct,
            DONNA_TYPE_COLUMNTYPE_SIZE,
            DonnaColumnTypeSizePrivate);
}

G_DEFINE_TYPE_WITH_CODE (DonnaColumnTypeSize, donna_column_type_size,
        G_TYPE_OBJECT,
        G_IMPLEMENT_INTERFACE (DONNA_TYPE_COLUMNTYPE, ct_size_columntype_init)
        )

static void
ct_size_finalize (GObject *object)
{
    DonnaColumnTypeSizePrivate *priv;

    priv = DONNA_COLUMNTYPE_SIZE (object)->priv;
    g_object_unref (priv->app);

    /* chain up */
    G_OBJECT_CLASS (donna_column_type_size_parent_class)->finalize (object);
}

static const gchar *
ct_size_get_renderers (DonnaColumnType   *ct)
{
    g_return_val_if_fail (DONNA_IS_COLUMNTYPE_SIZE (ct), NULL);
    return "t";
}

static gpointer
ct_size_get_data (DonnaColumnType    *ct,
                  const gchar        *tv_name,
                  const gchar        *col_name)
{
    struct tv_col_data *data;

    data = g_new0 (struct tv_col_data, 1);
    ct_size_refresh_data (ct, tv_name, col_name, (gpointer *) &data);
    return data;
}

static DonnaColumnTypeNeed
ct_size_refresh_data (DonnaColumnType    *ct,
                      const gchar        *tv_name,
                      const gchar        *col_name,
                      gpointer           *_data)
{
    DonnaColumnTypeSize *ctsize = DONNA_COLUMNTYPE_SIZE (ct);
    DonnaConfig *config;
    struct tv_col_data *data = *_data;
    DonnaColumnTypeNeed need = DONNA_COLUMNTYPE_NEED_NOTHING;
    gchar *s;
    gint i;

    config = donna_app_get_config (ctsize->priv->app);

    s = donna_config_get_string_column (config, tv_name, col_name, "columntypes/size",
            "property", "size");
    if (!streq (data->property, s))
    {
        g_free (data->property);
        data->property = s;
        need = DONNA_COLUMNTYPE_NEED_REDRAW;
    }
    else
        g_free (s);

    s = donna_config_get_string_column (config, tv_name, col_name, "size",
            "format", "%R");
    if (data->format != s)
    {
        g_free (data->format);
        data->format = s;
        need = DONNA_COLUMNTYPE_NEED_REDRAW;
    }
    else
        g_free (s);

    s = donna_config_get_string_column (config, tv_name, col_name, NULL,
            "format_tooltip", "%B");
    if (data->format_tooltip != s)
    {
        g_free (data->format_tooltip);
        data->format_tooltip = s;
        need = DONNA_COLUMNTYPE_NEED_REDRAW;
    }
    else
        g_free (s);

    i = donna_config_get_int_column (config, tv_name, col_name, "size",
            "digits", 1);
    /* we enforce this, because that's all we support (we can't stote more in
     * data) and that's what makes sense */
    i = MIN (MAX (0, i), 2);
    if (data->digits != i)
    {
        data->digits = i;
        need = DONNA_COLUMNTYPE_NEED_REDRAW;
    }

    i = donna_config_get_boolean_column (config, tv_name, col_name, "size",
            "long_unit", FALSE);
    if (data->long_unit != i)
    {
        data->long_unit = i;
        need = DONNA_COLUMNTYPE_NEED_REDRAW;
    }

    g_object_unref (config);

    return need;
}

static void
ct_size_free_data (DonnaColumnType    *ct,
                   const gchar        *tv_name,
                   const gchar        *col_name,
                   gpointer            _data)
{
    struct tv_col_data *data = _data;

    g_free (data->property);
    g_free (data->format);
    g_free (data->format_tooltip);
    g_free (data);
}

static GPtrArray *
ct_size_get_props (DonnaColumnType  *ct,
                   const gchar      *tv_name,
                   const gchar      *col_name,
                   gpointer          data)
{
    GPtrArray *props;

    g_return_val_if_fail (DONNA_IS_COLUMNTYPE_SIZE (ct), NULL);

    props = g_ptr_array_new_full (1, g_free);
    g_ptr_array_add (props, g_strdup (((struct tv_col_data *) data)->property));

    return props;
}

static GtkSortType
ct_size_get_default_sort_order (DonnaColumnType *ct,
                                const gchar     *tv_name,
                                const gchar     *col_name,
                                gpointer        data)
{
    DonnaConfig *config;
    GtkSortType sort_order;

    config = donna_app_get_config (DONNA_COLUMNTYPE_SIZE (ct)->priv->app);
    sort_order = (donna_config_get_boolean_column (config,
                tv_name, col_name, "desc_first", "columntypes/size", TRUE))
        ? GTK_SORT_DESCENDING : GTK_SORT_ASCENDING;
    g_object_unref (config);
    return sort_order;
}

static GtkMenu *
ct_size_get_options_menu (DonnaColumnType    *ct,
                          const gchar        *tv_name,
                          const gchar        *col_name,
                          gpointer            data)
{
    /* FIXME */
    return NULL;
}

static gboolean
ct_size_handle_context (DonnaColumnType    *ct,
                        const gchar        *tv_name,
                        const gchar        *col_name,
                        gpointer            data,
                        DonnaNode          *node,
                        DonnaTreeView      *treeview)
{
    /* FIXME */
    return FALSE;
}

static GPtrArray *
ct_size_render (DonnaColumnType    *ct,
                const gchar        *tv_name,
                const gchar        *col_name,
                gpointer            _data,
                guint               index,
                DonnaNode          *node,
                GtkCellRenderer    *renderer)
{
    struct tv_col_data *data = _data;
    DonnaNodeHasValue has;
    off_t size;
    gchar buf[20], *b = buf;
    gssize len;

    g_return_val_if_fail (DONNA_IS_COLUMNTYPE_SIZE (ct), NULL);

    if (donna_node_get_node_type (node) == DONNA_NODE_CONTAINER)
    {
        g_object_set (renderer, "visible", FALSE, NULL);
        return NULL;
    }

    if (streq (data->property, "size"))
        has = donna_node_get_size (node, FALSE, &size);
    else
        donna_node_get (node, FALSE, data->property, &has, &size, NULL);

    if (has == DONNA_NODE_VALUE_NONE || has == DONNA_NODE_VALUE_ERROR)
        return NULL;
    else if (has == DONNA_NODE_VALUE_NEED_REFRESH)
    {
        GPtrArray *arr;

        arr = g_ptr_array_new_full (1, g_free);
        g_ptr_array_add (arr, g_strdup (data->property));
        return arr;
    }
    /* DONNA_NODE_VALUE_SET */
    len = donna_print_size (b, 20, data->format, size, data->digits, data->long_unit);
    if (len >= 20)
    {
        b = g_new (gchar, ++len);
        donna_print_size (b, len, data->format, size, data->digits, data->long_unit);
    }
    g_object_set (renderer, "visible", TRUE, "text", b, "xalign", 1.0, NULL);
    if (b != buf)
        g_free (b);
    return NULL;
}

static gboolean
ct_size_set_tooltip (DonnaColumnType    *ct,
                     const gchar        *tv_name,
                     const gchar        *col_name,
                     gpointer            _data,
                     guint               index,
                     DonnaNode          *node,
                     GtkTooltip         *tooltip)
{
    struct tv_col_data *data = _data;
    off_t size;
    gchar buf[20], *b = buf;
    gssize len;
    DonnaNodeHasValue has;

    if (streq (data->property, "size"))
        has = donna_node_get_size (node, FALSE, &size);
    else
        donna_node_get (node, FALSE, data->property, &has, &size, NULL);

    if (has != DONNA_NODE_VALUE_SET)
        return FALSE;

    len = donna_print_size (b, 20, data->format_tooltip, size,
            data->digits, data->long_unit);
    if (len >= 20)
    {
        b = g_new (gchar, ++len);
        donna_print_size (b, len, data->format_tooltip, size,
                data->digits, data->long_unit);
    }
    gtk_tooltip_set_text (tooltip, b);
    if (b != buf)
        g_free (b);
    return TRUE;
}

static gint
ct_size_node_cmp (DonnaColumnType    *ct,
                  const gchar        *tv_name,
                  const gchar        *col_name,
                  gpointer            _data,
                  DonnaNode          *node1,
                  DonnaNode          *node2)
{
    struct tv_col_data *data = _data;
    DonnaNodeHasValue has1;
    DonnaNodeHasValue has2;
    off_t size1;
    off_t size2;

    if (streq (data->property, "size"))
    {
        has1 = donna_node_get_size (node1, TRUE, &size1);
        has2 = donna_node_get_size (node2, TRUE, &size2);
    }
    else
    {
        donna_node_get (node1, FALSE, data->property, &has1, &size1, NULL);
        donna_node_get (node2, FALSE, data->property, &has2, &size2, NULL);
    }

    /* since we're blocking, has can only be SET, ERROR or NONE */

    if (has1 != DONNA_NODE_VALUE_SET)
    {
        if (has2 == DONNA_NODE_VALUE_SET)
            return -1;
        else
            return 0;
    }
    else if (has2 != DONNA_NODE_VALUE_SET)
        return 1;

    return (size1 > size2) ? 1 : (size1 < size2) ? -1 : 0;
}

DonnaColumnType *
donna_column_type_size_new (DonnaApp *app)
{
    DonnaColumnType *ct;

    g_return_val_if_fail (DONNA_IS_APP (app), NULL);

    ct = g_object_new (DONNA_TYPE_COLUMNTYPE_SIZE, NULL);
    DONNA_COLUMNTYPE_SIZE (ct)->priv->app = g_object_ref (app);

    return ct;
}
