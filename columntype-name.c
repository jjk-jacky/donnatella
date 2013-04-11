
#include <glib-object.h>
#include <string.h>
#include "columntype.h"
#include "columntype-name.h"
#include "node.h"
#include "donna.h"
#include "conf.h"
#include "sort.h"
#include "macros.h"

struct tv_col_data
{
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

static void             ct_name_finalize            (GObject            *object);

/* ColumnType */
static const gchar *    ct_name_get_renderers       (DonnaColumnType    *ct);
static gpointer         ct_name_get_data            (DonnaColumnType    *ct,
                                                     const gchar        *tv_name,
                                                     const gchar        *col_name);
static DonnaColumnTypeNeed ct_name_refresh_data     (DonnaColumnType    *ct,
                                                     const gchar        *tv_name,
                                                     const gchar        *col_name,
                                                     gpointer           *data);
static void             ct_name_free_data           (DonnaColumnType    *ct,
                                                     const gchar        *tv_name,
                                                     const gchar        *col_name,
                                                     gpointer            data);
static GPtrArray *      ct_name_get_props           (DonnaColumnType    *ct,
                                                     const gchar        *tv_name,
                                                     const gchar        *col_name,
                                                     gpointer            data);
static GtkSortType      ct_name_get_default_sort_order
                                                    (DonnaColumnType    *ct,
                                                     const gchar        *tv_name,
                                                     const gchar        *col_name,
                                                     gpointer            data);
static GtkMenu *        ct_name_get_options_menu    (DonnaColumnType    *ct,
                                                     const gchar        *tv_name,
                                                     const gchar        *col_name,
                                                     gpointer            data);
static gboolean         ct_name_handle_context      (DonnaColumnType    *ct,
                                                     const gchar        *tv_name,
                                                     const gchar        *col_name,
                                                     gpointer            data,
                                                     DonnaNode          *node,
                                                     DonnaTreeView      *treeview);
static GPtrArray *      ct_name_render              (DonnaColumnType    *ct,
                                                     const gchar        *tv_name,
                                                     const gchar        *col_name,
                                                     gpointer            data,
                                                     guint               index,
                                                     DonnaNode          *node,
                                                     GtkCellRenderer    *renderer);
static gboolean         ct_name_set_tooltip         (DonnaColumnType    *ct,
                                                     const gchar        *tv_name,
                                                     const gchar        *col_name,
                                                     gpointer            data,
                                                     guint               index,
                                                     DonnaNode          *node,
                                                     GtkTooltip         *tooltip);
static gint             ct_name_node_cmp            (DonnaColumnType    *ct,
                                                     const gchar        *tv_name,
                                                     const gchar        *col_name,
                                                     gpointer            data,
                                                     DonnaNode          *node1,
                                                     DonnaNode          *node2);

static void
ct_name_columntype_init (DonnaColumnTypeInterface *interface)
{
    interface->get_renderers            = ct_name_get_renderers;
    interface->get_data                 = ct_name_get_data;
    interface->refresh_data             = ct_name_refresh_data;
    interface->free_data                = ct_name_free_data;
    interface->get_props                = ct_name_get_props;
    interface->get_default_sort_order   = ct_name_get_default_sort_order;
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
    o_class->finalize = ct_name_finalize;

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

static const gchar *
ct_name_get_renderers (DonnaColumnType   *ct)
{
    g_return_val_if_fail (DONNA_IS_COLUMNTYPE_NAME (ct), NULL);
    return "pt";
}

static gpointer
ct_name_get_data (DonnaColumnType    *ct,
                  const gchar        *tv_name,
                  const gchar        *col_name)
{
    DonnaColumnTypeName *ctname = DONNA_COLUMNTYPE_NAME (ct);
    struct tv_col_data *data;

    data = g_new0 (struct tv_col_data, 1);
    ct_name_refresh_data (ct, tv_name, col_name, (gpointer *) &data);
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
ct_name_refresh_data (DonnaColumnType    *ct,
                      const gchar        *tv_name,
                      const gchar        *col_name,
                      gpointer           *_data)
{
    DonnaColumnTypeName *ctname = DONNA_COLUMNTYPE_NAME (ct);
    DonnaConfig *config;
    struct tv_col_data *data = *_data;
    DonnaColumnTypeNeed need = DONNA_COLUMNTYPE_NEED_NOTHING;

    config = donna_app_get_config (ctname->priv->app);

    if (data->is_locale_based != donna_config_get_boolean_column (config,
                tv_name, col_name, "sort", "locale_based", FALSE))
    {
        need |= DONNA_COLUMNTYPE_NEED_RESORT;
        data->is_locale_based = !data->is_locale_based;
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

    g_object_unref (config);
    return need;
}

static void
ct_name_free_data (DonnaColumnType    *ct,
                   const gchar        *tv_name,
                   const gchar        *col_name,
                   gpointer            data)
{
    g_free (data);
}

static GPtrArray *
ct_name_get_props (DonnaColumnType  *ct,
                   const gchar      *tv_name,
                   const gchar      *col_name,
                   gpointer          data)
{
    GPtrArray *props;

    g_return_val_if_fail (DONNA_IS_COLUMNTYPE_NAME (ct), NULL);

    props = g_ptr_array_new_full (2, g_free);
    g_ptr_array_add (props, g_strdup ("name"));
    g_ptr_array_add (props, g_strdup ("icon"));

    return props;
}

static GtkSortType
ct_name_get_default_sort_order (DonnaColumnType *ct,
                                const gchar     *tv_name,
                                const gchar     *col_name,
                                gpointer        data)
{
    DonnaConfig *config;
    GtkSortType sort_order;

    config = donna_app_get_config (DONNA_COLUMNTYPE_NAME (ct)->priv->app);
    sort_order = (donna_config_get_boolean_column (config,
                tv_name, col_name, "desc_first", "columntypes/name", FALSE))
        ? GTK_SORT_DESCENDING : GTK_SORT_ASCENDING;
    g_object_unref (config);
    return sort_order;
}

static GtkMenu *
ct_name_get_options_menu (DonnaColumnType    *ct,
                          const gchar        *tv_name,
                          const gchar        *col_name,
                          gpointer            data)
{
    /* FIXME */
    return NULL;
}

static gboolean
ct_name_handle_context (DonnaColumnType    *ct,
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
ct_name_render (DonnaColumnType    *ct,
                const gchar        *tv_name,
                const gchar        *col_name,
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
                     const gchar        *tv_name,
                     const gchar        *col_name,
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
              const gchar           *tv_name,
              const gchar           *col_name,
              DonnaNode             *node,
              gboolean               dot_first,
              gboolean               special_first,
              gboolean               natural_order)
{
    DonnaColumnTypeNamePrivate *priv = ctname->priv;
    gchar  buf[128];
    gchar *key;

    snprintf (buf, 128, "%s/%s/utf8-collate-key", tv_name, col_name);
    key = g_object_get_data (G_OBJECT (node), buf);
    /* no key, or invalid (options changed) */
    if (!key || *key != donna_sort_get_options_char (dot_first, special_first,
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
                        g_strdup (buf),
                        (GClosureNotify) g_free,
                        0);

                g_ptr_array_add (priv->domains, (gpointer) domain);
            }
        }

        name = donna_node_get_name (node);
        key = donna_sort_get_utf8_collate_key (name, -1,
                dot_first, special_first, natural_order);
        g_free (name);
        g_object_set_data_full (G_OBJECT (node), buf, key, g_free);
    }

    return key + 1; /* skip options_char */
}

static gint
ct_name_node_cmp (DonnaColumnType    *ct,
                  const gchar        *tv_name,
                  const gchar        *col_name,
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

        name1 = get_node_key (ctname, tv_name, col_name, node1,
                data->options & DONNA_SORT_DOT_FIRST,
                data->sort_special_first,
                data->options & DONNA_SORT_NATURAL_ORDER);
        name2 = get_node_key (ctname, tv_name, col_name, node2,
                data->options & DONNA_SORT_DOT_FIRST,
                data->sort_special_first,
                data->options & DONNA_SORT_NATURAL_ORDER);

        return strcmp (name1, name2);
    }

    name1 = donna_node_get_name (node1);
    name2 = donna_node_get_name (node2);
    ret = donna_strcmp (name1, name2, data->options);
    g_free (name1);
    g_free (name2);
    return ret;
}

DonnaColumnType *
donna_column_type_name_new (DonnaApp *app)
{
    DonnaColumnType *ct;

    g_return_val_if_fail (DONNA_IS_APP (app), NULL);

    ct = g_object_new (DONNA_TYPE_COLUMNTYPE_NAME, NULL);
    DONNA_COLUMNTYPE_NAME (ct)->priv->app = g_object_ref (app);

    return ct;
}
