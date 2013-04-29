
#include <glib-object.h>
#include "columntype.h"
#include "columntype-text.h"
#include "node.h"
#include "donna.h"
#include "conf.h"
#include "sort.h"
#include "macros.h"

enum
{
    PROP_0,

    PROP_APP,

    NB_PROPS
};

struct tv_col_data
{
    gchar            *property;
    DonnaSortOptions  options;
};

struct _DonnaColumnTypeTextPrivate
{
    DonnaApp                    *app;
};

static void             ct_text_set_property        (GObject            *object,
                                                     guint               prop_id,
                                                     const GValue       *value,
                                                     GParamSpec         *pspec);
static void             ct_text_get_property        (GObject            *object,
                                                     guint               prop_id,
                                                     GValue             *value,
                                                     GParamSpec         *pspec);
static void             ct_text_finalize            (GObject            *object);

/* ColumnType */
static const gchar *    ct_text_get_name            (DonnaColumnType    *ct);
static const gchar *    ct_text_get_renderers       (DonnaColumnType    *ct);
static DonnaColumnTypeNeed ct_text_refresh_data     (DonnaColumnType    *ct,
                                                     const gchar        *tv_name,
                                                     const gchar        *col_name,
                                                     const gchar        *arr_name,
                                                     gpointer           *data);
static void             ct_text_free_data           (DonnaColumnType    *ct,
                                                     gpointer            data);
static GPtrArray *      ct_text_get_props           (DonnaColumnType    *ct,
                                                     gpointer            data);
static GPtrArray *      ct_text_render              (DonnaColumnType    *ct,
                                                     gpointer            data,
                                                     guint               index,
                                                     DonnaNode          *node,
                                                     GtkCellRenderer    *renderer);
static gint             ct_text_node_cmp            (DonnaColumnType    *ct,
                                                     gpointer            data,
                                                     DonnaNode          *node1,
                                                     DonnaNode          *node2);
static gboolean         ct_text_is_match_filter     (DonnaColumnType    *ct,
                                                     const gchar        *filter,
                                                     gpointer           *filter_data,
                                                     gpointer            data,
                                                     DonnaNode          *node,
                                                     GError            **error);
static void             ct_text_free_filter_data    (DonnaColumnType    *ct,
                                                     gpointer            filter_data);

static void
ct_text_columntype_init (DonnaColumnTypeInterface *interface)
{
    interface->get_name                 = ct_text_get_name;
    interface->get_renderers            = ct_text_get_renderers;
    interface->refresh_data             = ct_text_refresh_data;
    interface->free_data                = ct_text_free_data;
    interface->get_props                = ct_text_get_props;
    interface->render                   = ct_text_render;
    interface->node_cmp                 = ct_text_node_cmp;
    interface->is_match_filter          = ct_text_is_match_filter;
    interface->free_filter_data         = ct_text_free_filter_data;
}

static void
donna_column_type_text_class_init (DonnaColumnTypeTextClass *klass)
{
    GObjectClass *o_class;

    o_class = (GObjectClass *) klass;
    o_class->set_property   = ct_text_set_property;
    o_class->get_property   = ct_text_get_property;
    o_class->finalize       = ct_text_finalize;

    g_object_class_override_property (o_class, PROP_APP, "app");

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

static void
ct_text_set_property (GObject            *object,
                      guint               prop_id,
                      const GValue       *value,
                      GParamSpec         *pspec)
{
    if (G_LIKELY (prop_id == PROP_APP))
        DONNA_COLUMNTYPE_TEXT (object)->priv->app = g_value_dup_object (value);
    else
        G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
}

static void
ct_text_get_property (GObject            *object,
                      guint               prop_id,
                      GValue             *value,
                      GParamSpec         *pspec)
{
    if (G_LIKELY (prop_id == PROP_APP))
        g_value_set_object (value, DONNA_COLUMNTYPE_TEXT (object)->priv->app);
    else
        G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
}

static const gchar *
ct_text_get_name (DonnaColumnType *ct)
{
    g_return_val_if_fail (DONNA_IS_COLUMNTYPE_TEXT (ct), NULL);
    return "text";
}

static const gchar *
ct_text_get_renderers (DonnaColumnType   *ct)
{
    g_return_val_if_fail (DONNA_IS_COLUMNTYPE_TEXT (ct), NULL);
    return "t";
}

#define check_option(opt_name_lower, opt_name_upper, value, def_val)          \
    if (donna_config_get_boolean_column (config, tv_name, col_name, arr_name, \
                "sort", opt_name_lower, def_val) == value)                    \
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
                      const gchar        *arr_name,
                      gpointer           *_data)
{
    DonnaColumnTypeText *cttext = DONNA_COLUMNTYPE_TEXT (ct);
    DonnaConfig *config;
    struct tv_col_data *data;
    DonnaColumnTypeNeed need = DONNA_COLUMNTYPE_NEED_NOTHING;
    gchar *s;
    gint i;

    config = donna_app_peek_config (cttext->priv->app);

    if (!*_data)
        *_data = g_new0 (struct tv_col_data, 1);
    data = *_data;

    s = donna_config_get_string_column (config, tv_name, col_name, arr_name,
            NULL, "property", "name");
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
                   gpointer            _data)
{
    struct tv_col_data *data = _data;

    g_free (data->property);
    g_free (data);
}

static GPtrArray *
ct_text_get_props (DonnaColumnType  *ct,
                   gpointer          data)
{
    GPtrArray *props;

    g_return_val_if_fail (DONNA_IS_COLUMNTYPE_TEXT (ct), NULL);

    props = g_ptr_array_new_full (1, g_free);
    g_ptr_array_add (props, g_strdup (((struct tv_col_data *) data)->property));

    return props;
}

#define warn_not_string(node)    do {                   \
    gchar *location = donna_node_get_location (node);   \
    g_warning ("ColumnType 'text': property '%s' for node '%s:%s' isn't of expected type (%s instead of %s)",  \
            data->property,                             \
            donna_node_get_domain (node), location,     \
            G_VALUE_TYPE_NAME (&value),                 \
            g_type_name (G_TYPE_STRING));               \
    g_free (location);                                  \
} while (0)

static GPtrArray *
ct_text_render (DonnaColumnType    *ct,
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

static gint
ct_text_node_cmp (DonnaColumnType    *ct,
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

static gboolean
ct_text_is_match_filter (DonnaColumnType    *ct,
                         const gchar        *filter,
                         gpointer           *filter_data,
                         gpointer            _data,
                         DonnaNode          *node,
                         GError            **error)
{
    struct tv_col_data *data = _data;
    GPatternSpec *pspec;
    DonnaNodeHasValue has;
    GValue value = G_VALUE_INIT;
    gboolean ret;

    if (G_UNLIKELY (!*filter_data))
        pspec = *filter_data = g_pattern_spec_new (filter);
    else
        pspec = *filter_data;

    donna_node_get (node, TRUE, data->property, &has, &value, NULL);
    if (has == DONNA_NODE_VALUE_SET)
    {
        if (G_VALUE_TYPE (&value) != G_TYPE_STRING)
        {
            warn_not_string (node);
            g_value_unset (&value);
            return FALSE;
        }
    }
    else
        return FALSE;

    ret = g_pattern_match_string (pspec, g_value_get_string (&value));
    g_value_unset (&value);
    return ret;
}

static void
ct_text_free_filter_data (DonnaColumnType    *ct,
                          gpointer            filter_data)
{
    g_pattern_spec_free (filter_data);
}
