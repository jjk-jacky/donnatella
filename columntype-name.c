
#include <glib-object.h>
#include <string.h>
#include "columntype.h"
#include "columntype-name.h"
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
    gchar               *collate_key;
    guint                is_locale_based    : 1;
    DonnaSortOptions     options            : 5;
    /* not used in strcmp_ext so included in DonnaSortOptions */
    guint                sort_special_first : 1;
};

struct _DonnaColumnTypeNamePrivate
{
    DonnaApp                    *app;
    /* domains we're connected for node being renamed, when we use node_key */
    GPtrArray                   *domains;
};

static void             ct_name_set_property        (GObject            *object,
                                                     guint               prop_id,
                                                     const GValue       *value,
                                                     GParamSpec         *pspec);
static void             ct_name_get_property        (GObject            *object,
                                                     guint               prop_id,
                                                     GValue             *value,
                                                     GParamSpec         *pspec);
static void             ct_name_finalize            (GObject            *object);

/* ColumnType */
static const gchar *    ct_name_get_name            (DonnaColumnType    *ct);
static const gchar *    ct_name_get_renderers       (DonnaColumnType    *ct);
static DonnaColumnTypeNeed ct_name_refresh_data     (DonnaColumnType    *ct,
                                                     const gchar        *tv_name,
                                                     const gchar        *col_name,
                                                     gpointer           *data);
static void             ct_name_free_data           (DonnaColumnType    *ct,
                                                     gpointer            data);
static GPtrArray *      ct_name_get_props           (DonnaColumnType    *ct,
                                                     gpointer            data);
static GtkMenu *        ct_name_get_options_menu    (DonnaColumnType    *ct,
                                                     gpointer            data);
static gboolean         ct_name_handle_context      (DonnaColumnType    *ct,
                                                     gpointer            data,
                                                     DonnaNode          *node,
                                                     DonnaTreeView      *treeview);
static GPtrArray *      ct_name_render              (DonnaColumnType    *ct,
                                                     gpointer            data,
                                                     guint               index,
                                                     DonnaNode          *node,
                                                     GtkCellRenderer    *renderer);
static gboolean         ct_name_set_tooltip         (DonnaColumnType    *ct,
                                                     gpointer            data,
                                                     guint               index,
                                                     DonnaNode          *node,
                                                     GtkTooltip         *tooltip);
static gint             ct_name_node_cmp            (DonnaColumnType    *ct,
                                                     gpointer            data,
                                                     DonnaNode          *node1,
                                                     DonnaNode          *node2);

static void
ct_name_columntype_init (DonnaColumnTypeInterface *interface)
{
    interface->get_name                 = ct_name_get_name;
    interface->get_renderers            = ct_name_get_renderers;
    interface->refresh_data             = ct_name_refresh_data;
    interface->free_data                = ct_name_free_data;
    interface->get_props                = ct_name_get_props;
    interface->get_options_menu         = ct_name_get_options_menu;
    interface->handle_context           = ct_name_handle_context;
    interface->render                   = ct_name_render;
    interface->set_tooltip              = ct_name_set_tooltip;
    interface->node_cmp                 = ct_name_node_cmp;
}

static void
donna_column_type_name_class_init (DonnaColumnTypeNameClass *klass)
{
    GObjectClass *o_class;

    o_class = (GObjectClass *) klass;
    o_class->set_property   = ct_name_set_property;
    o_class->get_property   = ct_name_get_property;
    o_class->finalize       = ct_name_finalize;

    g_object_class_override_property (o_class, PROP_APP, "app");

    g_type_class_add_private (klass, sizeof (DonnaColumnTypeNamePrivate));
}

static void
donna_column_type_name_init (DonnaColumnTypeName *ct)
{
    DonnaColumnTypeNamePrivate *priv;

    priv = ct->priv = G_TYPE_INSTANCE_GET_PRIVATE (ct,
            DONNA_TYPE_COLUMNTYPE_NAME,
            DonnaColumnTypeNamePrivate);
}

G_DEFINE_TYPE_WITH_CODE (DonnaColumnTypeName, donna_column_type_name,
        G_TYPE_OBJECT,
        G_IMPLEMENT_INTERFACE (DONNA_TYPE_COLUMNTYPE, ct_name_columntype_init)
        )

static void
ct_name_finalize (GObject *object)
{
    DonnaColumnTypeNamePrivate *priv;

    priv = DONNA_COLUMNTYPE_NAME (object)->priv;
    g_object_unref (priv->app);
    if (priv->domains)
        g_ptr_array_free (priv->domains, TRUE);

    /* chain up */
    G_OBJECT_CLASS (donna_column_type_name_parent_class)->finalize (object);
}

static void
ct_name_set_property (GObject            *object,
                      guint               prop_id,
                      const GValue       *value,
                      GParamSpec         *pspec)
{
    if (G_LIKELY (prop_id == PROP_APP))
        DONNA_COLUMNTYPE_NAME (object)->priv->app = g_value_dup_object (value);
    else
        G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
}

static void
ct_name_get_property (GObject            *object,
                      guint               prop_id,
                      GValue             *value,
                      GParamSpec         *pspec)
{
    if (G_LIKELY (prop_id == PROP_APP))
        g_value_set_object (value, DONNA_COLUMNTYPE_NAME (object)->priv->app);
    else
        G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
}

static const gchar *
ct_name_get_name (DonnaColumnType *ct)
{
    g_return_val_if_fail (DONNA_IS_COLUMNTYPE_NAME (ct), NULL);
    return "name";
}

static const gchar *
ct_name_get_renderers (DonnaColumnType   *ct)
{
    g_return_val_if_fail (DONNA_IS_COLUMNTYPE_NAME (ct), NULL);
    return "pt";
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
ct_name_refresh_data (DonnaColumnType    *ct,
                      const gchar        *tv_name,
                      const gchar        *col_name,
                      gpointer           *_data)
{
    DonnaColumnTypeName *ctname = DONNA_COLUMNTYPE_NAME (ct);
    DonnaConfig *config;
    struct tv_col_data *data;
    DonnaColumnTypeNeed need = DONNA_COLUMNTYPE_NEED_NOTHING;

    config = donna_app_peek_config (ctname->priv->app);

    if (!*_data)
        *_data = g_new0 (struct tv_col_data, 1);
    data = *_data;

    if (data->is_locale_based != donna_config_get_boolean_column (config,
                tv_name, col_name, "sort", "locale_based", FALSE))
    {
        need |= DONNA_COLUMNTYPE_NEED_RESORT;
        data->is_locale_based = !data->is_locale_based;

        if (data->is_locale_based)
        {
            g_free (data->collate_key);
            data->collate_key = g_strdup_printf ("%s/%s/utf8-collate-key",
                        tv_name, col_name);
        }
        else
            g_free (data->collate_key);
    }

    check_option ("natural_order",  DONNA_SORT_NATURAL_ORDER,   TRUE, TRUE);
    check_option ("dot_first",      DONNA_SORT_DOT_FIRST,       TRUE, TRUE);

    if (data->is_locale_based)
    {
        if (data->sort_special_first != donna_config_get_boolean_column (config,
                    tv_name, col_name, "sort", "special_first", TRUE))
        {
            need |= DONNA_COLUMNTYPE_NEED_RESORT;
            data->sort_special_first = !data->sort_special_first;
        }
    }
    else
    {
        check_option ("dot_mixed",      DONNA_SORT_DOT_MIXED,        TRUE,  FALSE);
        check_option ("case_sensitive", DONNA_SORT_CASE_INSENSITIVE, FALSE, FALSE);
        check_option ("ignore_spunct",  DONNA_SORT_IGNORE_SPUNCT,    TRUE,  FALSE);
    }

    return need;
}

static void
ct_name_free_data (DonnaColumnType    *ct,
                   gpointer            data)
{
    g_free (((struct tv_col_data *) data)->collate_key);
    g_free (data);
}

static GPtrArray *
ct_name_get_props (DonnaColumnType  *ct,
                   gpointer          data)
{
    GPtrArray *props;

    g_return_val_if_fail (DONNA_IS_COLUMNTYPE_NAME (ct), NULL);

    props = g_ptr_array_new_full (2, g_free);
    g_ptr_array_add (props, g_strdup ("name"));
    g_ptr_array_add (props, g_strdup ("icon"));

    return props;
}

static GtkMenu *
ct_name_get_options_menu (DonnaColumnType    *ct,
                          gpointer            data)
{
    /* FIXME */
    return NULL;
}

static gboolean
ct_name_handle_context (DonnaColumnType    *ct,
                        gpointer            data,
                        DonnaNode          *node,
                        DonnaTreeView      *treeview)
{
    /* FIXME */
    return FALSE;
}

static GPtrArray *
ct_name_render (DonnaColumnType    *ct,
                gpointer            data,
                guint               index,
                DonnaNode          *node,
                GtkCellRenderer    *renderer)
{
    g_return_val_if_fail (DONNA_IS_COLUMNTYPE_NAME (ct), NULL);

    if (index == 1)
    {
        DonnaNodeHasValue has_value;
        GdkPixbuf *pixbuf;

        has_value = donna_node_get_icon (node, FALSE, &pixbuf);
        if (has_value == DONNA_NODE_VALUE_SET)
        {
            g_object_set (renderer, "visible", TRUE, "pixbuf", pixbuf, NULL);
            g_object_unref (pixbuf);
        }
        else
        {
            DonnaNodeType node_type;

            if (donna_node_get_node_type (node) == DONNA_NODE_ITEM)
                g_object_set (renderer,
                        "visible",  TRUE,
                        "stock-id", GTK_STOCK_FILE,
                        NULL);
            else /* DONNA_NODE_CONTAINER */
                g_object_set (renderer,
                        "visible",  TRUE,
                        "stock-id", GTK_STOCK_DIRECTORY,
                        NULL);

            return NULL; /* not done, so it never gets refreshed */
            if (has_value == DONNA_NODE_VALUE_NEED_REFRESH)
            {
                GPtrArray *arr;

                arr = g_ptr_array_new ();
                g_ptr_array_add (arr, "icon");
                return arr;
            }
        }
    }
    else /* index == 2 */
    {
        gchar *name;

        name = donna_node_get_name (node);
        g_object_set (renderer,
                "visible",      TRUE,
                "text",         name,
                "ellipsize",    PANGO_ELLIPSIZE_END,
                NULL);
        g_free (name);
    }

    return NULL;
}

static void
node_updated_cb (DonnaProvider  *provider,
                 DonnaNode      *node,
                 const gchar    *name,
                 gchar          *key)
{
    /* removes the data for key */
    g_object_set_data (G_OBJECT (node), key, NULL);
}

static gboolean
ct_name_set_tooltip (DonnaColumnType    *ct,
                     gpointer            data,
                     guint               index,
                     DonnaNode          *node,
                     GtkTooltip         *tooltip)
{
    gchar *s;
    DonnaNodeHasValue has;

    /* FIXME:
     * 1 (icon) : show full-name (using location as fallback only)
     * 2 (name) : show name if ellipsed, else no tooltip. Not sure how to find
     * out if the text was ellipsed or not... */

    if (index <= 1)
    {
        has = donna_node_get_full_name (node, FALSE, &s);
        if (has == DONNA_NODE_VALUE_NONE /*FIXME*/||has==DONNA_NODE_VALUE_NEED_REFRESH)
            s = donna_node_get_location (node);
        /* FIXME: if NEED_REFRESH do a task and whatnot? */
        else if (has != DONNA_NODE_VALUE_SET)
            return FALSE;
    }
    else
        s = donna_node_get_name (node);

    gtk_tooltip_set_text (tooltip, s);
    g_free (s);
    return TRUE;
}

static inline gchar *
get_node_key (DonnaColumnTypeName   *ctname,
              struct tv_col_data    *data,
              DonnaNode             *node)
{
    DonnaColumnTypeNamePrivate *priv = ctname->priv;
    gchar *key;
    gboolean dot_first = data->options & DONNA_SORT_DOT_FIRST;
    gboolean natural_order = data->options & DONNA_SORT_NATURAL_ORDER;

    key = g_object_get_data (G_OBJECT (node), data->collate_key);
    /* no key, or invalid (options changed) */
    if (!key || *key != donna_sort_get_options_char (dot_first,
                data->sort_special_first,
                natural_order))
    {
        gchar *name;

        /* if we're installing the key (i.e. not updating an invalid one) we
         * need to make sure we're listening on the provider's
         * node-updated::name signal, to remove the key on rename */
        if (!key)
        {
            const gchar *domain;
            guint i;

            domain = donna_node_get_domain (node);
            for (i = 0; i < priv->domains->len; ++i)
                if (streq (domain, priv->domains->pdata[i]))
                    break;
            /* no match, must connect */
            if (!priv->domains)
                priv->domains = g_ptr_array_new ();
            if (i >= priv->domains->len)
            {
                /* FIXME? (not actually needed since our cb is "self-contained")
                 * - also connect to a new signal "destroy" when provider is
                 *   being finalized. in handler, we remove it from the ptrarr
                 * - when we're finalized, disconnect all hanlers */
                g_signal_connect_data (donna_node_peek_provider (node),
                        "node-updated::name",
                        G_CALLBACK (node_updated_cb),
                        g_strdup (data->collate_key),
                        (GClosureNotify) g_free,
                        0);

                g_ptr_array_add (priv->domains, (gpointer) domain);
            }
        }

        name = donna_node_get_name (node);
        key = donna_sort_get_utf8_collate_key (name, -1,
                dot_first, data->sort_special_first, natural_order);
        g_free (name);
        g_object_set_data_full (G_OBJECT (node), data->collate_key, key, g_free);
    }

    return key + 1; /* skip options_char */
}

static gint
ct_name_node_cmp (DonnaColumnType    *ct,
                  gpointer            _data,
                  DonnaNode          *node1,
                  DonnaNode          *node2)
{
    struct tv_col_data *data = _data;
    gchar *name1;
    gchar *name2;
    gint ret;

    if (data->is_locale_based)
    {
        DonnaColumnTypeName *ctname = DONNA_COLUMNTYPE_NAME (ct);
        return strcmp (get_node_key (ctname, data, node1),
                       get_node_key (ctname, data, node2));
    }

    name1 = donna_node_get_name (node1);
    name2 = donna_node_get_name (node2);
    ret = donna_strcmp (name1, name2, data->options);
    g_free (name1);
    g_free (name2);
    return ret;
}
