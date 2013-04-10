
#include <glib-object.h>
#include "columntype.h"
#include "columntype-time.h"
#include "node.h"
#include "donna.h"
#include "conf.h"
#include "util.h"
#include "macros.h"

struct tv_col_data
{
    gchar   *property;
    gchar   *format;
};

struct _DonnaColumnTypeTimePrivate
{
    DonnaApp                    *app;
};

static void             ct_time_finalize            (GObject            *object);

/* ColumnType */
static const gchar *    ct_time_get_renderers       (DonnaColumnType    *ct);
static gpointer         ct_time_get_data            (DonnaColumnType    *ct,
                                                     const gchar        *tv_name,
                                                     const gchar        *col_name);
static DonnaColumnTypeNeed ct_time_refresh_data     (DonnaColumnType    *ct,
                                                     const gchar        *tv_name,
                                                     const gchar        *col_name,
                                                     gpointer           *data);
static void             ct_time_free_data           (DonnaColumnType    *ct,
                                                     const gchar        *tv_name,
                                                     const gchar        *col_name,
                                                     gpointer            data);
static GPtrArray *      ct_time_get_props           (DonnaColumnType    *ct,
                                                     const gchar        *tv_name,
                                                     const gchar        *col_name,
                                                     gpointer            data);
static GtkSortType      ct_time_get_default_sort_order
                                                    (DonnaColumnType    *ct,
                                                     const gchar        *tv_name,
                                                     const gchar        *col_name,
                                                     gpointer            data);
static GtkMenu *        ct_time_get_options_menu    (DonnaColumnType    *ct,
                                                     const gchar        *tv_name,
                                                     const gchar        *col_name,
                                                     gpointer            data);
static gboolean         ct_time_handle_context      (DonnaColumnType    *ct,
                                                     const gchar        *tv_name,
                                                     const gchar        *col_name,
                                                     gpointer            data,
                                                     DonnaNode          *node,
                                                     DonnaTreeView      *treeview);
static GPtrArray *      ct_time_render              (DonnaColumnType    *ct,
                                                     const gchar        *tv_name,
                                                     const gchar        *col_name,
                                                     gpointer            data,
                                                     guint               index,
                                                     DonnaNode          *node,
                                                     GtkCellRenderer    *renderer);
static gint             ct_time_node_cmp            (DonnaColumnType    *ct,
                                                     const gchar        *tv_name,
                                                     const gchar        *col_name,
                                                     gpointer            data,
                                                     DonnaNode          *node1,
                                                     DonnaNode          *node2);
static gboolean         ct_time_set_tooltip         (DonnaColumnType    *ct,
                                                     const gchar        *tv_name,
                                                     const gchar        *col_name,
                                                     gpointer            data,
                                                     guint               index,
                                                     DonnaNode          *node,
                                                     GtkTooltip         *tooltip);

static void
ct_time_columntype_init (DonnaColumnTypeInterface *interface)
{
    interface->get_renderers            = ct_time_get_renderers;
    interface->get_data                 = ct_time_get_data;
    interface->refresh_data             = ct_time_refresh_data;
    interface->free_data                = ct_time_free_data;
    interface->get_props                = ct_time_get_props;
    interface->get_default_sort_order   = ct_time_get_default_sort_order;
    interface->get_options_menu         = ct_time_get_options_menu;
    interface->handle_context           = ct_time_handle_context;
    interface->render                   = ct_time_render;
    interface->set_tooltip              = ct_time_set_tooltip;
    interface->node_cmp                 = ct_time_node_cmp;
}

static void
donna_column_type_time_class_init (DonnaColumnTypeTimeClass *klass)
{
    GObjectClass *o_class;

    o_class = (GObjectClass *) klass;
    o_class->finalize = ct_time_finalize;

    g_type_class_add_private (klass, sizeof (DonnaColumnTypeTimePrivate));
}

static void
donna_column_type_time_init (DonnaColumnTypeTime *ct)
{
    DonnaColumnTypeTimePrivate *priv;

    priv = ct->priv = G_TYPE_INSTANCE_GET_PRIVATE (ct,
            DONNA_TYPE_COLUMNTYPE_TIME,
            DonnaColumnTypeTimePrivate);
}

G_DEFINE_TYPE_WITH_CODE (DonnaColumnTypeTime, donna_column_type_time,
        G_TYPE_OBJECT,
        G_IMPLEMENT_INTERFACE (DONNA_TYPE_COLUMNTYPE, ct_time_columntype_init)
        )

static void
ct_time_finalize (GObject *object)
{
    DonnaColumnTypeTimePrivate *priv;

    priv = DONNA_COLUMNTYPE_TIME (object)->priv;
    g_object_unref (priv->app);

    /* chain up */
    G_OBJECT_CLASS (donna_column_type_time_parent_class)->finalize (object);
}

static const gchar *
ct_time_get_renderers (DonnaColumnType   *ct)
{
    g_return_val_if_fail (DONNA_IS_COLUMNTYPE_TIME (ct), NULL);
    return "t";
}

static gpointer
ct_time_get_data (DonnaColumnType    *ct,
                  const gchar        *tv_name,
                  const gchar        *col_name)
{
    struct tv_col_data *data;

    data = g_new0 (struct tv_col_data, 1);
    ct_time_refresh_data (ct, tv_name, col_name, (gpointer *) &data);
    return data;
}

static DonnaColumnTypeNeed
ct_time_refresh_data (DonnaColumnType    *ct,
                      const gchar        *tv_name,
                      const gchar        *col_name,
                      gpointer           *_data)
{
    DonnaColumnTypeTime *cttime = DONNA_COLUMNTYPE_TIME (ct);
    DonnaConfig *config;
    struct tv_col_data *data = *_data;
    DonnaColumnTypeNeed need = DONNA_COLUMNTYPE_NEED_NOTHING;
    gchar *s;

    config = donna_app_get_config (cttime->priv->app);

    s = donna_config_get_string_column (config, tv_name, col_name,
            "columntypes/time", "property", "mtime");
    if (!streq (data->property, s))
    {
        g_free (data->property);
        data->property = s;
        need = DONNA_COLUMNTYPE_NEED_REDRAW;
    }
    else
        g_free (s);

    s = donna_config_get_string_column (config, tv_name, col_name, "time",
            "format", "%F %T");
    if (data->format != s)
    {
        g_free (data->format);
        data->format = s;
        need = DONNA_COLUMNTYPE_NEED_REDRAW;
    }
    else
        g_free (s);

    g_object_unref (config);

    return need;
}

static void
ct_time_free_data (DonnaColumnType    *ct,
                   const gchar        *tv_name,
                   const gchar        *col_name,
                   gpointer            _data)
{
    struct tv_col_data *data = _data;

    g_free (data->property);
    g_free (data->format);
    g_free (data);
}

static GPtrArray *
ct_time_get_props (DonnaColumnType  *ct,
                   const gchar      *tv_name,
                   const gchar      *col_name,
                   gpointer          data)
{
    GPtrArray *props;

    g_return_val_if_fail (DONNA_IS_COLUMNTYPE_TIME (ct), NULL);

    props = g_ptr_array_new_full (1, g_free);
    g_ptr_array_add (props, g_strdup (((struct tv_col_data *) data)->property));

    return props;
}

static GtkSortType
ct_time_get_default_sort_order (DonnaColumnType *ct,
                                const gchar     *tv_name,
                                const gchar     *col_name,
                                gpointer        data)
{
    DonnaConfig *config;
    GtkSortType sort_order;

    config = donna_app_get_config (DONNA_COLUMNTYPE_TIME (ct)->priv->app);
    sort_order = (donna_config_get_boolean_column (config,
                tv_name, col_name, "desc_first", "columntypes/time", TRUE))
        ? GTK_SORT_DESCENDING : GTK_SORT_ASCENDING;
    g_object_unref (config);
    return sort_order;
}

static GtkMenu *
ct_time_get_options_menu (DonnaColumnType    *ct,
                          const gchar        *tv_name,
                          const gchar        *col_name,
                          gpointer            data)
{
    /* FIXME */
    return NULL;
}

static gboolean
ct_time_handle_context (DonnaColumnType    *ct,
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
ct_time_render (DonnaColumnType    *ct,
                const gchar        *tv_name,
                const gchar        *col_name,
                gpointer            _data,
                guint               index,
                DonnaNode          *node,
                GtkCellRenderer    *renderer)
{
    struct tv_col_data *data = _data;
    DonnaNodeHasValue has;
    time_t time;
    gchar *s;

    g_return_val_if_fail (DONNA_IS_COLUMNTYPE_TIME (ct), NULL);

    if (streq (data->property, "mtime"))
        has = donna_node_get_mtime (node, FALSE, &time);
    else if (streq (data->property, "atime"))
        has = donna_node_get_atime (node, FALSE, &time);
    else if (streq (data->property, "ctime"))
        has = donna_node_get_ctime (node, FALSE, &time);
    else
        donna_node_get (node, FALSE, data->property, &has, &time, NULL);

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
    s = donna_print_time (time, data->format);
    g_object_set (renderer, "visible", TRUE, "text", s, NULL);
    g_free (s);
    return NULL;
}

static gboolean
ct_time_set_tooltip (DonnaColumnType    *ct,
                     const gchar        *tv_name,
                     const gchar        *col_name,
                     gpointer            _data,
                     guint               index,
                     DonnaNode          *node,
                     GtkTooltip         *tooltip)
{
    return FALSE;
}

static gint
ct_time_node_cmp (DonnaColumnType    *ct,
                  const gchar        *tv_name,
                  const gchar        *col_name,
                  gpointer            _data,
                  DonnaNode          *node1,
                  DonnaNode          *node2)
{
    struct tv_col_data *data = _data;
    DonnaNodeHasValue has1;
    DonnaNodeHasValue has2;
    time_t time1;
    time_t time2;

    if (streq (data->property, "mtime"))
    {
        has1 = donna_node_get_mtime (node1, TRUE, &time1);
        has2 = donna_node_get_mtime (node2, TRUE, &time2);
    }
    else if (streq (data->property, "atime"))
    {
        has1 = donna_node_get_atime (node1, TRUE, &time1);
        has2 = donna_node_get_atime (node2, TRUE, &time2);
    }
    else if (streq (data->property, "ctime"))
    {
        has1 = donna_node_get_ctime (node1, TRUE, &time1);
        has2 = donna_node_get_ctime (node2, TRUE, &time2);
    }
    else
    {
        donna_node_get (node1, FALSE, data->property, &has1, &time1, NULL);
        donna_node_get (node2, FALSE, data->property, &has2, &time2, NULL);
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

    return (time1 > time2) ? 1 : (time1 < time2) ? -1 : 0;
}

DonnaColumnType *
donna_column_type_time_new (DonnaApp *app)
{
    DonnaColumnType *ct;

    g_return_val_if_fail (DONNA_IS_APP (app), NULL);

    ct = g_object_new (DONNA_TYPE_COLUMNTYPE_TIME, NULL);
    DONNA_COLUMNTYPE_TIME (ct)->priv->app = g_object_ref (app);

    return ct;
}
