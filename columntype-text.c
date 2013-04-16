
#include <glib-object.h>
#include "columntype.h"
#include "columntype-text.h"
#include "node.h"
#include "donna.h"
#include "conf.h"
#include "sort.h"
#include "macros.h"

struct tv_col_data
{
    gchar            *property;
    DonnaSortOptions  options;
};

struct _DonnaColumnTypeTextPrivate
{
    DonnaApp                    *app;
};

static void             ct_text_finalize            (GObject            *object);

/* ColumnType */
static const gchar *    ct_text_get_renderers       (DonnaColumnType    *ct);
static gpointer         ct_text_get_data            (DonnaColumnType    *ct,
                                                     const gchar        *tv_name,
                                                     const gchar        *col_name);
static DonnaColumnTypeNeed ct_text_refresh_data     (DonnaColumnType    *ct,
                                                     const gchar        *tv_name,
                                                     const gchar        *col_name,
                                                     gpointer           *data);
static void             ct_text_free_data           (DonnaColumnType    *ct,
                                                     const gchar        *tv_name,
                                                     const gchar        *col_name,
                                                     gpointer            data);
static GPtrArray *      ct_text_get_props           (DonnaColumnType    *ct,
                                                     const gchar        *tv_name,
                                                     const gchar        *col_name,
                                                     gpointer            data);
static GtkSortType      ct_text_get_default_sort_order
                                                    (DonnaColumnType    *ct,
                                                     const gchar        *tv_name,
                                                     const gchar        *col_name,
                                                     gpointer            data);
static GtkMenu *        ct_text_get_options_menu    (DonnaColumnType    *ct,
                                                     const gchar        *tv_name,
                                                     const gchar        *col_name,
                                                     gpointer            data);
static gboolean         ct_text_handle_context      (DonnaColumnType    *ct,
                                                     const gchar        *tv_name,
                                                     const gchar        *col_name,
                                                     gpointer            data,
                                                     DonnaNode          *node,
                                                     DonnaTreeView      *treeview);
static GPtrArray *      ct_text_render              (DonnaColumnType    *ct,
                                                     const gchar        *tv_name,
                                                     const gchar        *col_name,
                                                     gpointer            data,
                                                     guint               index,
                                                     DonnaNode          *node,
                                                     GtkCellRenderer    *renderer);
static gint             ct_text_node_cmp            (DonnaColumnType    *ct,
                                                     const gchar        *tv_name,
                                                     const gchar        *col_name,
                                                     gpointer            data,
                                                     DonnaNode          *node1,
                                                     DonnaNode          *node2);
static gboolean         ct_text_set_tooltip         (DonnaColumnType    *ct,
                                                     const gchar        *tv_name,
                                                     const gchar        *col_name,
                                                     gpointer            data,
                                                     guint               index,
                                                     DonnaNode          *node,
                                                     GtkTooltip         *tooltip);

static void
ct_text_columntype_init (DonnaColumnTypeInterface *interface)
{
    interface->get_renderers            = ct_text_get_renderers;
    interface->get_data                 = ct_text_get_data;
    interface->refresh_data             = ct_text_refresh_data;
    interface->free_data                = ct_text_free_data;
    interface->get_props                = ct_text_get_props;
    interface->get_default_sort_order   = ct_text_get_default_sort_order;
    interface->get_options_menu         = ct_text_get_options_menu;
    interface->handle_context           = ct_text_handle_context;
    interface->render                   = ct_text_render;
    interface->set_tooltip              = ct_text_set_tooltip;
    interface->node_cmp                 = ct_text_node_cmp;
}

static void
donna_column_type_text_class_init (DonnaColumnTypeTextClass *klass)
{
    GObjectClass *o_class;

    o_class = (GObjectClass *) klass;
    o_class->finalize = ct_text_finalize;

    g_type_class_add_private (klass, sizeof (DonnaColumnTypeTextPrivate));
}

static void
donna_column_type_text_init (DonnaColumnTypeText *ct)
{
    DonnaColumnTypeTextPrivate *priv;

    priv = ct->priv = G_TYPE_INSTANCE_GET_PRIVATE (ct,
            DONNA_TYPE_COLUMNTYPE_TEXT,
            DonnaColumnTypeTextPrivate);
}

G_DEFINE_TYPE_WITH_CODE (DonnaColumnTypeText, donna_column_type_text,
        G_TYPE_OBJECT,
        G_IMPLEMENT_INTERFACE (DONNA_TYPE_COLUMNTYPE, ct_text_columntype_init)
        )

static void
ct_text_finalize (GObject *object)
{
    DonnaColumnTypeTextPrivate *priv;

    priv = DONNA_COLUMNTYPE_TEXT (object)->priv;
    g_object_unref (priv->app);

    /* chain up */
    G_OBJECT_CLASS (donna_column_type_text_parent_class)->finalize (object);
}

static const gchar *
ct_text_get_renderers (DonnaColumnType   *ct)
{
    g_return_val_if_fail (DONNA_IS_COLUMNTYPE_TEXT (ct), NULL);
    return "t";
}

static gpointer
ct_text_get_data (DonnaColumnType    *ct,
                  const gchar        *tv_name,
                  const gchar        *col_name)
{
    struct tv_col_data *data;

    data = g_new0 (struct tv_col_data, 1);
    ct_text_refresh_data (ct, tv_name, col_name, (gpointer *) &data);
    return data;
}

#define check_option(opt_name_lower, opt_name_upper, value, def_val)          \
    if (donna_config_get_boolean_column (config, tv_name, col_name, "sort",   \
                opt_name_lower, def_val) == value)                            \
    {                                                                         \
        if (!(data->options & opt_name_upper))                                \
        {                                                                     \
            need |= DONNA_COLUMNTYPE_NEED_RESORT;                             \
            data->options |= opt_name_upper;                                  \
        }                                                                     \
    }                                                                         \
    else if (data->options & opt_name_upper)                                  \
    {                                                                         \
        need |= DONNA_COLUMNTYPE_NEED_RESORT;                                 \
        data->options &= ~opt_name_upper;                                     \
    }

static DonnaColumnTypeNeed
ct_text_refresh_data (DonnaColumnType    *ct,
                      const gchar        *tv_name,
                      const gchar        *col_name,
                      gpointer           *_data)
{
    DonnaColumnTypeText *cttext = DONNA_COLUMNTYPE_TEXT (ct);
    DonnaConfig *config;
    struct tv_col_data *data = *_data;
    DonnaColumnTypeNeed need = DONNA_COLUMNTYPE_NEED_NOTHING;
    gchar *s;
    gint i;

    config = donna_app_peek_config (cttext->priv->app);

    s = donna_config_get_string_column (config, tv_name, col_name, NULL,
            "property", "name");
    if (!streq (data->property, s))
    {
        g_free (data->property);
        data->property = s;
        need = DONNA_COLUMNTYPE_NEED_REDRAW | DONNA_COLUMNTYPE_NEED_RESORT;
    }
    else
        g_free (s);

    check_option ("natural_order",  DONNA_SORT_NATURAL_ORDER,    TRUE,  TRUE);
    check_option ("dot_first",      DONNA_SORT_DOT_FIRST,        TRUE,  TRUE);
    check_option ("dot_mixed",      DONNA_SORT_DOT_MIXED,        TRUE,  FALSE);
    check_option ("case_sensitive", DONNA_SORT_CASE_INSENSITIVE, FALSE, FALSE);
    check_option ("ignore_spunct",  DONNA_SORT_IGNORE_SPUNCT,    TRUE,  FALSE);

    return need;
}

static void
ct_text_free_data (DonnaColumnType    *ct,
                   const gchar        *tv_name,
                   const gchar        *col_name,
                   gpointer            _data)
{
    struct tv_col_data *data = _data;

    g_free (data->property);
    g_free (data);
}

static GPtrArray *
ct_text_get_props (DonnaColumnType  *ct,
                   const gchar      *tv_name,
                   const gchar      *col_name,
                   gpointer          data)
{
    GPtrArray *props;

    g_return_val_if_fail (DONNA_IS_COLUMNTYPE_TEXT (ct), NULL);

    props = g_ptr_array_new_full (1, g_free);
    g_ptr_array_add (props, g_strdup (((struct tv_col_data *) data)->property));

    return props;
}

static GtkSortType
ct_text_get_default_sort_order (DonnaColumnType *ct,
                                const gchar     *tv_name,
                                const gchar     *col_name,
                                gpointer        data)
{
    return (donna_config_get_boolean_column (donna_app_peek_config (
                    DONNA_COLUMNTYPE_TEXT (ct)->priv->app),
                tv_name, col_name, "columntypes/text", "desc_first", FALSE))
        ? GTK_SORT_DESCENDING : GTK_SORT_ASCENDING;
}

static GtkMenu *
ct_text_get_options_menu (DonnaColumnType    *ct,
                          const gchar        *tv_name,
                          const gchar        *col_name,
                          gpointer            data)
{
    /* FIXME */
    return NULL;
}

static gboolean
ct_text_handle_context (DonnaColumnType    *ct,
                        const gchar        *tv_name,
                        const gchar        *col_name,
                        gpointer            data,
                        DonnaNode          *node,
                        DonnaTreeView      *treeview)
{
    /* FIXME */
    return FALSE;
}

#define warn_not_string(node)    do {                   \
    gchar *location = donna_node_get_location (node);   \
    g_warning ("Treeview '%s', Column '%s': property '%s' for node '%s:%s' isn't of expected type (%s instead of %s)",  \
            tv_name, col_name, data->property,          \
            donna_node_get_domain (node), location,     \
            G_VALUE_TYPE_NAME (&value),                 \
            g_type_name (G_TYPE_STRING));               \
    g_free (location);                                  \
} while (0)

static GPtrArray *
ct_text_render (DonnaColumnType    *ct,
                const gchar        *tv_name,
                const gchar        *col_name,
                gpointer            _data,
                guint               index,
                DonnaNode          *node,
                GtkCellRenderer    *renderer)
{
    struct tv_col_data *data = _data;
    DonnaNodeHasValue has;
    GValue value = G_VALUE_INIT;

    g_return_val_if_fail (DONNA_IS_COLUMNTYPE_TEXT (ct), NULL);

    donna_node_get (node, FALSE, data->property, &has, &value, NULL);
    if (has == DONNA_NODE_VALUE_NONE || has == DONNA_NODE_VALUE_ERROR)
    {
        g_object_set (renderer, "visible", FALSE, NULL);
        return NULL;
    }
    else if (has == DONNA_NODE_VALUE_NEED_REFRESH)
    {
        GPtrArray *arr;

        arr = g_ptr_array_new_full (1, g_free);
        g_ptr_array_add (arr, g_strdup (data->property));
        g_object_set (renderer, "visible", FALSE, NULL);
        return arr;
    }
    /* DONNA_NODE_VALUE_SET */
    else if (G_VALUE_TYPE (&value) != G_TYPE_STRING)
    {
        warn_not_string (node);
        g_value_unset (&value);
        g_object_set (renderer, "visible", FALSE, NULL);
        return NULL;
    }

    g_object_set (renderer,
            "visible",  TRUE,
            "text",     g_value_get_string (&value),
            NULL);
    g_value_unset (&value);
    return NULL;
}

static gboolean
ct_text_set_tooltip (DonnaColumnType    *ct,
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
ct_text_node_cmp (DonnaColumnType    *ct,
                  const gchar        *tv_name,
                  const gchar        *col_name,
                  gpointer            _data,
                  DonnaNode          *node1,
                  DonnaNode          *node2)
{
    struct tv_col_data *data = _data;
    DonnaNodeHasValue has1;
    DonnaNodeHasValue has2;
    GValue value = G_VALUE_INIT;
    gchar *s1 = NULL;
    gchar *s2 = NULL;
    gint ret;

    donna_node_get (node1, TRUE, data->property, &has1, &value, NULL);
    if (has1 == DONNA_NODE_VALUE_SET)
    {
        if (G_VALUE_TYPE (&value) != G_TYPE_STRING)
            warn_not_string (node1);
        else
            s1 = g_value_dup_string (&value);
        g_value_unset (&value);
    }

    donna_node_get (node2, TRUE, data->property, &has2, &value, NULL);
    if (has2 == DONNA_NODE_VALUE_SET)
    {
        if (G_VALUE_TYPE (&value) != G_TYPE_STRING)
            warn_not_string (node2);
        else
            s2 = g_value_dup_string (&value);
        g_value_unset (&value);
    }

    /* since we're blocking, has can only be SET, ERROR or NONE */

    if (has1 != DONNA_NODE_VALUE_SET)
    {
        if (has2 == DONNA_NODE_VALUE_SET)
            ret = -1;
        else
            ret = 0;
        goto done;
    }
    else if (has2 != DONNA_NODE_VALUE_SET)
    {
        ret = 1;
        goto done;
    }

    ret = donna_strcmp (s1, s2, data->options);
done:
    g_free (s1);
    g_free (s2);
    return ret;
}

DonnaColumnType *
donna_column_type_text_new (DonnaApp *app)
{
    DonnaColumnType *ct;

    g_return_val_if_fail (DONNA_IS_APP (app), NULL);

    ct = g_object_new (DONNA_TYPE_COLUMNTYPE_TEXT, NULL);
    DONNA_COLUMNTYPE_TEXT (ct)->priv->app = g_object_ref (app);

    return ct;
}
