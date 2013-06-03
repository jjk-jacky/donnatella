
#include <glib-object.h>
#include <string.h>
#include "columntype.h"
#include "columntype-progress.h"
#include "app.h"
#include "node.h"
#include "macros.h"

enum
{
    PROP_0,

    PROP_APP,

    NB_PROPS
};

struct tv_col_data
{
    gchar   *property;
    gchar   *property_lbl;
    gchar   *label;
};

struct _DonnaColumnTypeProgressPrivate
{
    DonnaApp    *app;
};

static void             ct_progress_set_property    (GObject            *object,
                                                     guint               prop_id,
                                                     const GValue       *value,
                                                     GParamSpec         *pspec);
static void             ct_progress_get_property    (GObject            *object,
                                                     guint               prop_id,
                                                     GValue             *value,
                                                     GParamSpec         *pspec);
static void             ct_progress_finalize        (GObject            *object);

/* ColumnType */
static const gchar *    ct_progress_get_name        (DonnaColumnType    *ct);
static const gchar *    ct_progress_get_renderers   (DonnaColumnType    *ct);
static DonnaColumnTypeNeed ct_progress_refresh_data (DonnaColumnType    *ct,
                                                     const gchar        *tv_name,
                                                     const gchar        *col_name,
                                                     const gchar        *arr_name,
                                                     gpointer           *data);
static void             ct_progress_free_data       (DonnaColumnType    *ct,
                                                     gpointer            data);
static GPtrArray *      ct_progress_get_props       (DonnaColumnType    *ct,
                                                     gpointer            data);
static GPtrArray *      ct_progress_render          (DonnaColumnType    *ct,
                                                     gpointer            data,
                                                     guint               index,
                                                     DonnaNode          *node,
                                                     GtkCellRenderer    *renderer);
static gint             ct_progress_node_cmp        (DonnaColumnType    *ct,
                                                     gpointer            data,
                                                     DonnaNode          *node1,
                                                     DonnaNode          *node2);

static void
ct_progress_columntype_init (DonnaColumnTypeInterface *interface)
{
    interface->get_name         = ct_progress_get_name;
    interface->get_renderers    = ct_progress_get_renderers;
    interface->refresh_data     = ct_progress_refresh_data;
    interface->free_data        = ct_progress_free_data;
    interface->get_props        = ct_progress_get_props;
    interface->render           = ct_progress_render;
    interface->node_cmp         = ct_progress_node_cmp;
}

static void
donna_column_type_progress_class_init (DonnaColumnTypeProgressClass *klass)
{
    GObjectClass *o_class;

    o_class = (GObjectClass *) klass;
    o_class->set_property   = ct_progress_set_property;
    o_class->get_property   = ct_progress_get_property;
    o_class->finalize       = ct_progress_finalize;

    g_object_class_override_property (o_class, PROP_APP, "app");

    g_type_class_add_private (klass, sizeof (DonnaColumnTypeProgressPrivate));
}

static void
donna_column_type_progress_init (DonnaColumnTypeProgress *ct)
{
    DonnaColumnTypeProgressPrivate *priv;

    priv = ct->priv = G_TYPE_INSTANCE_GET_PRIVATE (ct,
            DONNA_TYPE_COLUMNTYPE_PROGRESS,
            DonnaColumnTypeProgressPrivate);
}

G_DEFINE_TYPE_WITH_CODE (DonnaColumnTypeProgress, donna_column_type_progress,
        G_TYPE_OBJECT,
        G_IMPLEMENT_INTERFACE (DONNA_TYPE_COLUMNTYPE, ct_progress_columntype_init)
        )

static void
ct_progress_finalize (GObject *object)
{
    DonnaColumnTypeProgressPrivate *priv;

    priv = DONNA_COLUMNTYPE_PROGRESS (object)->priv;
    g_object_unref (priv->app);

    /* chain up */
    G_OBJECT_CLASS (donna_column_type_progress_parent_class)->finalize (object);
}

static void
ct_progress_set_property (GObject            *object,
                          guint               prop_id,
                          const GValue       *value,
                          GParamSpec         *pspec)
{
    if (G_LIKELY (prop_id == PROP_APP))
        DONNA_COLUMNTYPE_PROGRESS (object)->priv->app = g_value_dup_object (value);
    else
        G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
}

static void
ct_progress_get_property (GObject            *object,
                          guint               prop_id,
                          GValue             *value,
                          GParamSpec         *pspec)
{
    if (G_LIKELY (prop_id == PROP_APP))
        g_value_set_object (value, DONNA_COLUMNTYPE_PROGRESS (object)->priv->app);
    else
        G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
}

static const gchar *
ct_progress_get_name (DonnaColumnType *ct)
{
    g_return_val_if_fail (DONNA_IS_COLUMNTYPE_PROGRESS (ct), NULL);
    return "progress";
}

static const gchar *
ct_progress_get_renderers (DonnaColumnType   *ct)
{
    g_return_val_if_fail (DONNA_IS_COLUMNTYPE_PROGRESS (ct), NULL);
    return "P";
}

static DonnaColumnTypeNeed
ct_progress_refresh_data (DonnaColumnType    *ct,
                          const gchar        *tv_name,
                          const gchar        *col_name,
                          const gchar        *arr_name,
                          gpointer           *_data)
{
    DonnaColumnTypeProgress *ctpg = (DonnaColumnTypeProgress *) ct;
    DonnaConfig *config;
    struct tv_col_data *data;
    DonnaColumnTypeNeed need = DONNA_COLUMNTYPE_NEED_NOTHING;
    gchar *s;
    gint i;

    config = donna_app_peek_config (ctpg->priv->app);

    if (!*_data)
        *_data = g_new0 (struct tv_col_data, 1);
    data = *_data;

    s = donna_config_get_string_column (config, tv_name, col_name, arr_name,
            NULL, "property", "progress");
    if (!streq (data->property, s))
    {
        g_free (data->property);
        data->property = s;
        need = DONNA_COLUMNTYPE_NEED_REDRAW | DONNA_COLUMNTYPE_NEED_RESORT;
    }
    else
        g_free (s);

    s = donna_config_get_string_column (config, tv_name, col_name, arr_name,
            NULL, "label", "%P");
    if (!streq (data->label, s))
    {
        g_free (data->label);
        data->label = s;
        need = DONNA_COLUMNTYPE_NEED_REDRAW;
    }

    s = donna_config_get_string_column (config, tv_name, col_name, arr_name,
            NULL, "property_lbl", "");
    if (*s == '\0')
    {
        g_free (s);
        s = NULL;
    }
    if (!streq (data->property_lbl, s))
    {
        g_free (data->property_lbl);
        data->property_lbl = s;
        need = DONNA_COLUMNTYPE_NEED_REDRAW;
    }
    else
        g_free (s);

    return need;
}

static void
ct_progress_free_data (DonnaColumnType    *ct,
                       gpointer            _data)
{
    struct tv_col_data *data = _data;

    g_free (data->property);
    g_free (data->property_lbl);
    g_free (data->label);
    g_free (data);
}

static GPtrArray *
ct_progress_get_props (DonnaColumnType  *ct,
                       gpointer          _data)
{
    struct tv_col_data *data = _data;
    GPtrArray *props;

    g_return_val_if_fail (DONNA_IS_COLUMNTYPE_PROGRESS (ct), NULL);

    props = g_ptr_array_new_full ((data->property_lbl) ? 2 : 1, g_free);
    g_ptr_array_add (props, g_strdup (data->property));
    if (data->property_lbl)
        g_ptr_array_add (props, g_strdup (data->property_lbl));

    return props;
}

#define warn_not_type(node)    do {                     \
    gchar *fl = donna_node_get_full_location (node);    \
    g_warning ("ColumnType 'progress': property '%s' for node '%s' isn't of expected type (%s instead of %s or %s)",  \
            data->property,                             \
            fl,                                         \
            G_VALUE_TYPE_NAME (&value),                 \
            g_type_name (G_TYPE_INT),                   \
            g_type_name (G_TYPE_DOUBLE));               \
    g_free (fl);                                        \
} while (0)

static GPtrArray *
ct_progress_render (DonnaColumnType    *ct,
                    gpointer            _data,
                    guint               index,
                    DonnaNode          *node,
                    GtkCellRenderer    *renderer)
{
    struct tv_col_data *data = _data;
    DonnaNodeHasValue has;
    GValue value = G_VALUE_INIT;
    gint progress;
    gint pulse;
    gchar *s;

    g_return_val_if_fail (DONNA_IS_COLUMNTYPE_PROGRESS (ct), NULL);

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
    else if (G_VALUE_TYPE (&value) == G_TYPE_INT)
        progress = g_value_get_int (&value);
    else if (G_VALUE_TYPE (&value) == G_TYPE_DOUBLE)
        progress = (gint) (100.0 * g_value_get_double (&value));
    else
    {
        warn_not_type (node);
        g_value_unset (&value);
        g_object_set (renderer, "visible", FALSE, NULL);
        return NULL;
    }
    g_value_unset (&value);

    if (progress < 0 || progress > 100)
        pulse = 0;
    else
        pulse = -1;

    s = strchr (data->label, '%');
    if (s)
    {
        GString *str;
        gchar *ss;

        *s = '\0';
        str = g_string_new (data->label);
        *s = '%';
        for (;;)
        {
            if (s[1] == 'p')
                g_string_append_printf (str, "%d", progress);
            else if (s[1] == 'P')
                g_string_append_printf (str, "%d%%", progress);
            else
                --s;
            ss = s + 2;
            s = strchr (ss, '%');
            if (!s)
                break;
        }
        g_string_append (str, ss);
        s = g_string_free (str, FALSE);
    }

    g_object_set (renderer,
            "visible",  TRUE,
            "pulse",    pulse,
            "value",    progress,
            "text",     (s) ? s : data->label,
            NULL);
    g_free (s);
    return NULL;
}

static gint
ct_progress_node_cmp (DonnaColumnType    *ct,
                      gpointer            _data,
                      DonnaNode          *node1,
                      DonnaNode          *node2)
{
    struct tv_col_data *data = _data;
    DonnaNodeHasValue has1;
    DonnaNodeHasValue has2;
    GValue value = G_VALUE_INIT;
    gint p1;
    gint p2;
    gint ret;

    donna_node_get (node1, TRUE, data->property, &has1, &value, NULL);
    if (has1 == DONNA_NODE_VALUE_SET)
    {
        if (G_VALUE_TYPE (&value) == G_TYPE_INT)
            p1 = g_value_get_int (&value);
        else if (G_VALUE_TYPE (&value) == G_TYPE_DOUBLE)
            p1 = (gint) (100.0 * g_value_get_double (&value));
        else
            warn_not_type (node1);
        g_value_unset (&value);
    }

    donna_node_get (node2, TRUE, data->property, &has2, &value, NULL);
    if (has2 == DONNA_NODE_VALUE_SET)
    {
        if (G_VALUE_TYPE (&value) == G_TYPE_INT)
            p2 = g_value_get_int (&value);
        else if (G_VALUE_TYPE (&value) == G_TYPE_DOUBLE)
            p2 = (gint) (100.0 * g_value_get_double (&value));
        else
            warn_not_type (node2);
        g_value_unset (&value);
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

    return (p1 > p2) ? 1 : (p1 < p2) ? -1 : 0;
}
