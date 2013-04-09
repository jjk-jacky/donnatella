
#include <glib-object.h>
#include "columntype.h"
#include "columntype-size.h"
#include "node.h"
#include "donna.h"
#include "conf.h"
#include "macros.h"

enum size_format
{
    SIZE_FORMAT_RAW = 0,
    SIZE_FORMAT_B,
    SIZE_FORMAT_KB,
    SIZE_FORMAT_MB,
    SIZE_FORMAT_ROUND,
};

struct tv_col_data
{
    enum size_format    format : 3;
    guint               digits : 2;
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

static guint
get_size_option (DonnaColumnTypeSize *ctsize,
                 const gchar         *tv_name,
                 const gchar         *col_name,
                 const gchar         *opt_name,
                 gint                 def)
{
    DonnaConfig *config;
    gint          value;

    config = donna_app_get_config (ctsize->priv->app);
    if (!donna_config_get_int (config, &value,
                "treeviews/%s/columns/%s/%s",
                tv_name, col_name, opt_name))
    {
        if (!donna_config_get_int (config, &value,
                    "columns/%s/%s", col_name, opt_name))
        {
            if (!donna_config_get_int (config, &value,
                        "defaults/size/%s", opt_name))
            {
                if (donna_config_set_int (config, value,
                            "defaults/size/%s", opt_name))
                    g_info ("Option 'defaults/size/%s' did not exists, initialized to %d",
                            opt_name, value);
            }
        }
    }
    g_object_unref (config);
    return value;
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
    struct tv_col_data *data = *_data;
    DonnaColumnTypeNeed need = DONNA_COLUMNTYPE_NEED_NOTHING;
    gint val;

    val = get_size_option (ctsize, tv_name, col_name, "format", SIZE_FORMAT_ROUND);
    if (data->format != val)
    {
        data->format = val;
        need = DONNA_COLUMNTYPE_NEED_REDRAW;
    }

    val = get_size_option (ctsize, tv_name, col_name, "digits", 1);
    /* we enforce this, because that's all we support (we can't stote more in
     * data) and that's what makes sense */
    val = MIN (MAX (0, val), 2);
    if (data->digits != val)
    {
        data->digits = val;
        need = DONNA_COLUMNTYPE_NEED_REDRAW;
    }

    return need;
}

static void
ct_size_free_data (DonnaColumnType    *ct,
                   const gchar        *tv_name,
                   const gchar        *col_name,
                   gpointer            data)
{
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
    g_ptr_array_add (props, g_strdup ("size"));

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
    sort_order = DONNA_COLUMNTYPE_GET_INTERFACE (ct)->_get_default_sort_order (
            config, tv_name, col_name, "size", GTK_SORT_DESCENDING);
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


static inline void
print_rounded (off_t size, gint digits, gchar **b)
{
    const gchar unit[] = { 'B', 'K', 'M', 'G', 'T' };
    gint u = 0;
    gint max = sizeof (unit) / sizeof (unit[0]);
    gdouble dbl;

    dbl = (gdouble) size;
    while (dbl > 1024.0)
    {
        if (++u >= max)
            break;
        dbl /= 1024.0;
    }
    if (snprintf (*b, 20, "%'.*lf %c", (u > 0) ? digits : 0, dbl, unit[u]) >= 20)
        *b = g_strdup_printf ("%'.*lf %c", (u > 0) ? digits : 0, dbl, unit[u]);
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
    gdouble dbl;
    gchar buf[20], *b = buf;

    g_return_val_if_fail (DONNA_IS_COLUMNTYPE_SIZE (ct), NULL);

    if (donna_node_get_node_type (node) == DONNA_NODE_CONTAINER)
    {
        g_object_set (renderer, "visible", FALSE, NULL);
        return NULL;
    }

    has = donna_node_get_size (node, FALSE, &size);
    if (has == DONNA_NODE_VALUE_NONE || has == DONNA_NODE_VALUE_ERROR)
        return NULL;
    else if (has == DONNA_NODE_VALUE_NEED_REFRESH)
    {
        GPtrArray *arr;

        arr = g_ptr_array_new ();
        g_ptr_array_add (arr, "size");
        return arr;
    }
    /* DONNA_NODE_VALUE_SET */

    switch (data->format)
    {
        case SIZE_FORMAT_RAW:
            if (snprintf (b, 20, "%li", size) >= 20)
                b = g_strdup_printf ("%li", size);
            break;

        case SIZE_FORMAT_B:
            if (snprintf (b, 20, "%'li", size) >= 20)
                b = g_strdup_printf ("%'li", size);
            break;

        case SIZE_FORMAT_KB:
            dbl = (gdouble) size / 1024.0;
            if (snprintf (b, 20, "%'.*lf K", data->digits, dbl) >= 20)
                b = g_strdup_printf ("%'.*lf K", data->digits, dbl);
            break;

        case SIZE_FORMAT_MB:
            dbl = (gdouble) size / (1024.0 * 1024.0);
            if (snprintf (b, 20, "%'.*lf M", data->digits, dbl) >= 20)
                b = g_strdup_printf ("%'.*lf M", data->digits, dbl);
            break;

        case SIZE_FORMAT_ROUND:
            print_rounded (size, data->digits, &b);
            break;
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
    gchar buf[20], *b = buf;
    off_t size;
    DonnaNodeHasValue has;

    if (donna_node_get_size (node, FALSE, &size) != DONNA_NODE_VALUE_SET)
        return FALSE;

    if (data->format == SIZE_FORMAT_RAW || data->format == SIZE_FORMAT_B)
        print_rounded (size, data->digits, &b);
    else
        if (snprintf (b, 20, "%'li B", size) >= 20)
            b = g_strdup_printf ("%'li B", size);

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

    has1 = donna_node_get_size (node1, TRUE, &size1);
    has2 = donna_node_get_size (node2, TRUE, &size2);

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
