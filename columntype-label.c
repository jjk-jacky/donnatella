
#include <glib-object.h>
#include <string.h>
#include "columntype.h"
#include "columntype-label.h"
#include "app.h"
#include "node.h"
#include "macros.h"

enum
{
    PROP_0,

    PROP_APP,

    NB_PROPS
};

struct label
{
    gint    id;
    gchar  *label;
    gint    len;
};

struct tv_col_data
{
    gchar           *property;
    gchar           *labels;
    struct label    *defs;
    gint             nb;
};

struct _DonnaColumnTypeLabelPrivate
{
    DonnaApp    *app;
};

static void             ct_label_set_property       (GObject            *object,
                                                     guint               prop_id,
                                                     const GValue       *value,
                                                     GParamSpec         *pspec);
static void             ct_label_get_property       (GObject            *object,
                                                     guint               prop_id,
                                                     GValue             *value,
                                                     GParamSpec         *pspec);
static void             ct_label_finalize           (GObject            *object);

/* ColumnType */
static const gchar *    ct_label_get_name           (DonnaColumnType    *ct);
static const gchar *    ct_label_get_renderers      (DonnaColumnType    *ct);
static DonnaColumnTypeNeed ct_label_refresh_data    (DonnaColumnType    *ct,
                                                     const gchar        *tv_name,
                                                     const gchar        *col_name,
                                                     const gchar        *arr_name,
                                                     gpointer           *data);
static void             ct_label_free_data          (DonnaColumnType    *ct,
                                                     gpointer            data);
static GPtrArray *      ct_label_get_props          (DonnaColumnType    *ct,
                                                     gpointer            data);
static GPtrArray *      ct_label_render             (DonnaColumnType    *ct,
                                                     gpointer            data,
                                                     guint               index,
                                                     DonnaNode          *node,
                                                     GtkCellRenderer    *renderer);
static gint             ct_label_node_cmp           (DonnaColumnType    *ct,
                                                     gpointer            data,
                                                     DonnaNode          *node1,
                                                     DonnaNode          *node2);

static void
ct_label_columntype_init (DonnaColumnTypeInterface *interface)
{
    interface->get_name         = ct_label_get_name;
    interface->get_renderers    = ct_label_get_renderers;
    interface->refresh_data     = ct_label_refresh_data;
    interface->free_data        = ct_label_free_data;
    interface->get_props        = ct_label_get_props;
    interface->render           = ct_label_render;
    interface->node_cmp         = ct_label_node_cmp;
}

static void
donna_column_type_label_class_init (DonnaColumnTypeLabelClass *klass)
{
    GObjectClass *o_class;

    o_class = (GObjectClass *) klass;
    o_class->set_property   = ct_label_set_property;
    o_class->get_property   = ct_label_get_property;
    o_class->finalize       = ct_label_finalize;

    g_object_class_override_property (o_class, PROP_APP, "app");

    g_type_class_add_private (klass, sizeof (DonnaColumnTypeLabelPrivate));
}

static void
donna_column_type_label_init (DonnaColumnTypeLabel *ct)
{
    DonnaColumnTypeLabelPrivate *priv;

    priv = ct->priv = G_TYPE_INSTANCE_GET_PRIVATE (ct,
            DONNA_TYPE_COLUMNTYPE_LABEL,
            DonnaColumnTypeLabelPrivate);
}

G_DEFINE_TYPE_WITH_CODE (DonnaColumnTypeLabel, donna_column_type_label,
        G_TYPE_OBJECT,
        G_IMPLEMENT_INTERFACE (DONNA_TYPE_COLUMNTYPE, ct_label_columntype_init)
        )

static void
ct_label_finalize (GObject *object)
{
    DonnaColumnTypeLabelPrivate *priv;

    priv = DONNA_COLUMNTYPE_LABEL (object)->priv;
    g_object_unref (priv->app);

    /* chain up */
    G_OBJECT_CLASS (donna_column_type_label_parent_class)->finalize (object);
}

static void
ct_label_set_property (GObject            *object,
                       guint               prop_id,
                       const GValue       *value,
                       GParamSpec         *pspec)
{
    if (G_LIKELY (prop_id == PROP_APP))
        DONNA_COLUMNTYPE_LABEL (object)->priv->app = g_value_dup_object (value);
    else
        G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
}

static void
ct_label_get_property (GObject            *object,
                       guint               prop_id,
                       GValue             *value,
                       GParamSpec         *pspec)
{
    if (G_LIKELY (prop_id == PROP_APP))
        g_value_set_object (value, DONNA_COLUMNTYPE_LABEL (object)->priv->app);
    else
        G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
}

static const gchar *
ct_label_get_name (DonnaColumnType *ct)
{
    g_return_val_if_fail (DONNA_IS_COLUMNTYPE_LABEL (ct), NULL);
    return "label";
}

static const gchar *
ct_label_get_renderers (DonnaColumnType   *ct)
{
    g_return_val_if_fail (DONNA_IS_COLUMNTYPE_LABEL (ct), NULL);
    return "t";
}

static DonnaColumnTypeNeed
ct_label_refresh_data (DonnaColumnType    *ct,
                       const gchar        *tv_name,
                       const gchar        *col_name,
                       const gchar        *arr_name,
                       gpointer           *_data)
{
    DonnaColumnTypeLabel *ctlbl = (DonnaColumnTypeLabel *) ct;
    DonnaConfig *config;
    struct tv_col_data *data;
    DonnaColumnTypeNeed need = DONNA_COLUMNTYPE_NEED_NOTHING;
    gchar *s;
    gint i;

    config = donna_app_peek_config (ctlbl->priv->app);

    if (!*_data)
        *_data = g_new0 (struct tv_col_data, 1);
    data = *_data;

    s = donna_config_get_string_column (config, tv_name, col_name, arr_name,
            NULL, "property", "id");
    if (!streq (data->property, s))
    {
        g_free (data->property);
        data->property = s;
        need = DONNA_COLUMNTYPE_NEED_REDRAW | DONNA_COLUMNTYPE_NEED_RESORT;
    }
    else
        g_free (s);

    s = donna_config_get_string_column (config, tv_name, col_name, arr_name,
            NULL, "labels", "0=false,1=true");
    if (!streq (data->labels, s))
    {
        gint alloc, i;
        gint id;
        gchar *label;
        gchar *l;

        g_free (data->labels);
        data->labels = s;

        g_free (data->defs);
        data->defs = NULL;
        alloc = i = 0;
        l = data->labels;

        for ( ; ; ++i)
        {
            if (i >= alloc)
            {
                alloc += 4;
                data->defs = g_renew (struct label, data->defs, alloc);
            }
            s = strchr (l, '=');
            if (!s)
            {
                g_warning ("ColumnType 'label': Invalid labels definition: %s",
                        data->labels);
                g_free (data->labels);
                data->labels = NULL;
                g_free (data->defs);
                data->defs = NULL;
                break;
            }
            data->defs[i].id = g_ascii_strtoll (l, NULL, 10);
            *s = '=';
            l = s + 1;
            s = strchr (l, ',');
            if (s)
                *s = '\0';
            data->defs[i].label = l;
            data->defs[i].len   = (gint) strlen (l);
            if (s)
            {
                *s = ',';
                l = s + 1;
            }
            else
                break;
        }
        data->nb = i + 1;

        need = DONNA_COLUMNTYPE_NEED_REDRAW;
    }

    return need;
}

static void
ct_label_free_data (DonnaColumnType    *ct,
                    gpointer            _data)
{
    struct tv_col_data *data = _data;

    g_free (data->property);
    g_free (data->labels);
    g_free (data->defs);
    g_free (data);
}

static GPtrArray *
ct_label_get_props (DonnaColumnType  *ct,
                    gpointer          data)
{
    GPtrArray *props;

    g_return_val_if_fail (DONNA_IS_COLUMNTYPE_LABEL (ct), NULL);

    props = g_ptr_array_new_full (1, g_free);
    g_ptr_array_add (props, g_strdup (((struct tv_col_data *) data)->property));

    return props;
}

#define warn_not_int(node)    do {                      \
    gchar *fl = donna_node_get_full_location (node);    \
    g_warning ("ColumnType 'label': property '%s' for node '%s' isn't of expected type (%s instead of %s)",  \
            data->property,                             \
            fl,                                         \
            G_VALUE_TYPE_NAME (&value),                 \
            g_type_name (G_TYPE_INT));                  \
    g_free (fl);                                        \
} while (0)

static GPtrArray *
ct_label_render (DonnaColumnType    *ct,
                 gpointer            _data,
                 guint               index,
                 DonnaNode          *node,
                 GtkCellRenderer    *renderer)
{
    struct tv_col_data *data = _data;
    DonnaNodeHasValue has;
    GValue value = G_VALUE_INIT;
    gint i, id;
    gchar *s;

    g_return_val_if_fail (DONNA_IS_COLUMNTYPE_LABEL (ct), NULL);

    if (!data->labels)
    {
        g_object_set (renderer, "visible", FALSE, NULL);
        return NULL;
    }

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
    else if (G_VALUE_TYPE (&value) != G_TYPE_INT)
    {
        warn_not_int (node);
        g_value_unset (&value);
        g_object_set (renderer, "visible", FALSE, NULL);
        return NULL;
    }

    id = g_value_get_int (&value);
    for (i = 0; i < data->nb; ++i)
        if (data->defs[i].id == id)
        {
            s = g_strdup_printf ("%.*s", data->defs[i].len, data->defs[i].label);
            break;
        }
    if (i >= data->nb)
        s = g_strdup_printf ("<unknown id:%d>", id);

    g_object_set (renderer,
            "visible",  TRUE,
            "text",     s,
            NULL);
    g_value_unset (&value);
    g_free (s);
    return NULL;
}

static gint
ct_label_node_cmp (DonnaColumnType    *ct,
                   gpointer            _data,
                   DonnaNode          *node1,
                   DonnaNode          *node2)
{
    struct tv_col_data *data = _data;
    DonnaNodeHasValue has1;
    DonnaNodeHasValue has2;
    GValue value = G_VALUE_INIT;
    gint id1;
    gint id2;
    gint ret;

    donna_node_get (node1, TRUE, data->property, &has1, &value, NULL);
    if (has1 == DONNA_NODE_VALUE_SET)
    {
        if (G_VALUE_TYPE (&value) != G_TYPE_INT)
            warn_not_int (node1);
        else
            id1 = g_value_get_int (&value);
        g_value_unset (&value);
    }

    donna_node_get (node2, TRUE, data->property, &has2, &value, NULL);
    if (has2 == DONNA_NODE_VALUE_SET)
    {
        if (G_VALUE_TYPE (&value) != G_TYPE_INT)
            warn_not_int (node2);
        else
            id2 = g_value_get_int (&value);
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

    return (id1 > id2) ? 1 : (id1 < id2) ? -1 : 0;
}
