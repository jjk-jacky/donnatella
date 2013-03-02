
#include <glib-object.h>
#include <string.h>
#include "columntype.h"
#include "columntype-name.h"
#include "node.h"
#include "donna.h"
#include "conf.h"
#include "sort.h"
#include "sharedstring.h"
#include "macros.h"

struct _DonnaColumnTypeNamePrivate
{
    DonnaDonna  *donna;
    GPtrArray   *domains;
};

static void             ct_name_finalize            (GObject            *object);

/* ColumnType */
static const gchar *    ct_name_get_renderers       (DonnaColumnType    *ct);
static DonnaSharedString ** ct_name_get_props       (DonnaColumnType    *ct,
                                                     const gchar        *tv_name,
                                                     const gchar        *col_name);
static GPtrArray *      ct_name_render              (DonnaColumnType    *ct,
                                                     const gchar        *tv_name,
                                                     const gchar        *col_name,
                                                     guint               index,
                                                     DonnaNode          *node,
                                                     GtkCellRenderer    *renderer);
static GtkMenu *        ct_name_get_options_menu    (DonnaColumnType    *ct,
                                                     const gchar        *tv_name,
                                                     const gchar        *col_name);
static gboolean         ct_name_handle_context      (DonnaColumnType    *ct,
                                                     const gchar        *tv_name,
                                                     const gchar        *col_name,
                                                     DonnaNode          *node,
                                                     DonnaTreeView      *treeview);
static gboolean         ct_name_set_tooltip         (DonnaColumnType    *ct,
                                                     const gchar        *tv_name,
                                                     const gchar        *col_name,
                                                     guint               index,
                                                     DonnaNode          *node,
                                                     GtkTooltip         *tooltip);
static gint             ct_name_node_cmp            (DonnaColumnType    *ct,
                                                     const gchar        *tv_name,
                                                     const gchar        *col_name,
                                                     DonnaNode          *node1,
                                                     DonnaNode          *node2);

static void
ct_name_columntype_init (DonnaColumnTypeInterface *interface)
{
    interface->get_renderers    = ct_name_get_renderers;
    interface->get_props        = ct_name_get_props;
    interface->render           = ct_name_render;
    interface->get_options_menu = ct_name_get_options_menu;
    interface->handle_context   = ct_name_handle_context;
    interface->set_tooltip      = ct_name_set_tooltip;
    interface->node_cmp         = ct_name_node_cmp;
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
    priv->domains = g_ptr_array_new ();
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
    g_ptr_array_free (priv->domains, TRUE);
    g_object_unref (priv->donna);

    /* chain up */
    G_OBJECT_CLASS (donna_column_type_name_parent_class)->finalize (object);
}

static const gchar *
ct_name_get_renderers (DonnaColumnType   *ct)
{
    g_return_val_if_fail (DONNA_IS_COLUMNTYPE_NAME (ct), NULL);
    return "pt";
}

static DonnaSharedString **
ct_name_get_props (DonnaColumnType  *ct,
                   const gchar      *tv_name,
                   const gchar      *col_name)
{
    DonnaSharedString **ss;

    g_return_val_if_fail (DONNA_IS_COLUMNTYPE_NAME (ct), NULL);

    ss = g_new0 (DonnaSharedString *, 3);
    ss[0] = donna_shared_string_new_dup ("name");
    ss[1] = donna_shared_string_new_dup ("icon");
    return ss;
}

static GPtrArray *
ct_name_render (DonnaColumnType    *ct,
                const gchar        *tv_name,
                const gchar        *col_name,
                guint               index,
                DonnaNode          *node,
                GtkCellRenderer    *renderer)
{
    g_return_val_if_fail (DONNA_IS_COLUMNTYPE_NAME (ct), NULL);

    if (index == 1)
    {
        DonnaNodeHasValue has_value;
        GdkPixbuf *pixbuf;

        donna_node_get (node, FALSE, "icon", &has_value, &pixbuf, NULL);
        if (has_value == DONNA_NODE_VALUE_SET)
        {
            g_object_set (renderer, "visible", TRUE, "pixbuf", pixbuf, NULL);
            g_object_unref (pixbuf);
        }
        else
        {
            DonnaNodeType node_type;

            donna_node_get (node, FALSE, "node-type", &node_type, NULL);
            if (node_type == DONNA_NODE_ITEM)
                g_object_set (renderer,
                        "visible",  TRUE,
                        "stock-id", GTK_STOCK_FILE,
                        NULL);
            else if (node_type == DONNA_NODE_CONTAINER)
                g_object_set (renderer,
                        "visible",  TRUE,
                        "stock-id", GTK_STOCK_DIRECTORY,
                        NULL);
            else /* DONNA_NODE_EXTENDED */
                g_object_set (renderer,
                        "visible",  TRUE,
                        "stock-id", GTK_STOCK_EXECUTE,
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
        DonnaSharedString *name;

        donna_node_get (node, FALSE, "name", &name, NULL);
        g_object_set (renderer,
                "visible",      TRUE,
                "text",         donna_shared_string (name),
                "ellipsize",    PANGO_ELLIPSIZE_END,
                NULL);
        donna_shared_string_unref (name);
    }

    return NULL;
}

static GtkMenu *
ct_name_get_options_menu (DonnaColumnType    *ct,
                          const gchar        *tv_name,
                          const gchar        *col_name)
{
    /* FIXME */
    return NULL;
}

static gboolean
ct_name_handle_context (DonnaColumnType    *ct,
                        const gchar        *tv_name,
                        const gchar        *col_name,
                        DonnaNode          *node,
                        DonnaTreeView      *treeview)
{
    /* FIXME */
    return FALSE;
}

static gboolean
ct_name_set_tooltip (DonnaColumnType    *ct,
                     const gchar        *tv_name,
                     const gchar        *col_name,
                     guint               index,
                     DonnaNode          *node,
                     GtkTooltip         *tooltip)
{
    DonnaSharedString *ss;
    DonnaNodeHasValue has;

    /* FIXME:
     * 1 (icon) : show full-name (using location as fallback only)
     * 2 (name) : show name if ellipsed, else no tooltip. Not sure how to find
     * out if the text was ellipsed or not... */

    if (index == 1)
    {
        donna_node_get (node, FALSE, "full-name", &has, &ss, NULL);
        if (has == DONNA_NODE_VALUE_NONE)
            donna_node_get (node, FALSE, "location", &ss, NULL);
        /* FIXME: if NEED_REFRESH do a task and whatnot? */
        else if (has != DONNA_NODE_VALUE_SET)
            return FALSE;
    }
    else
        donna_node_get (node, FALSE, "name", &ss, NULL);

    gtk_tooltip_set_text (tooltip, donna_shared_string (ss));
    donna_shared_string_unref (ss);
    return TRUE;
}

static gboolean
get_sort_option (DonnaColumnTypeName *ctname,
                 const gchar         *tv_name,
                 const gchar         *col_name,
                 const gchar         *opt_name)
{
    DonnaConfig *config;
    gboolean      value;

    config = donna_donna_get_config (ctname->priv->donna);
    if (!donna_config_get_boolean (config, &value,
                "treeviews/%s/columns/%s/sort_%s",
                tv_name, col_name, opt_name))
    {
        if (!donna_config_get_boolean (config, &value,
                    "columns/%s/sort_%s", col_name, opt_name))
        {
            if (!donna_config_get_boolean (config, &value,
                        "defaults/sort/%s", opt_name))
            {
                value = TRUE;
                if (donna_config_set_boolean (config, value,
                            "defaults/sort/%s", opt_name))
                    g_info ("Option 'defaults/sort/%s' did not exists, initialized to TRUE",
                            opt_name);
            }
        }
    }
    g_object_unref (config);
    return value;
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
    if (!key || *key != sort_get_options_char (dot_first, special_first,
                natural_order))
    {
        DonnaSharedString *name;

        /* if we're installing the key (i.e. not updating an invalid one) we
         * need to make sure we're listening on the provider's
         * node-updated::name signal, to remove the key on rename */
        if (!key)
        {
            const gchar *domain;
            guint i;

            donna_node_get (node, FALSE, "domain", &domain, NULL);
            for (i = 0; i < priv->domains->len; ++i)
                if (streq (domain, priv->domains->pdata[i]))
                    break;
            /* no match, must connect */
            if (i >= priv->domains->len)
            {
                DonnaProvider *provider;

                donna_node_get (node, FALSE, "provider", &provider, NULL);
                /* FIXME? (not actually needed since our cb is "self-contained")
                 * - also connect to a new signal "destroy" when provider is
                 *   being finalized. in handler, we remove it from the ptrarr
                 * - when we're finalized, disconnect all hanlers */
                g_signal_connect_data (provider, "node-updated::name",
                        G_CALLBACK (node_updated_cb),
                        g_strdup (buf),
                        (GClosureNotify) g_free,
                        0);
                g_object_unref (provider);

                g_ptr_array_add (priv->domains, (gpointer) domain);
            }
        }

        donna_node_get (node, FALSE, "name", &name, NULL);
        key = sort_get_utf8_collate_key (donna_shared_string (name), -1,
                dot_first, special_first, natural_order);
        donna_shared_string_unref (name);
        g_object_set_data_full (G_OBJECT (node), buf, key, g_free);
    }

    return key + 1; /* skip options_char */
}

static gint
ct_name_node_cmp (DonnaColumnType    *ct,
                  const gchar        *tv_name,
                  const gchar        *col_name,
                  DonnaNode          *node1,
                  DonnaNode          *node2)
{
    DonnaColumnTypeName *ctname = DONNA_COLUMNTYPE_NAME (ct);
    gboolean dot_first;
    gboolean special_first;
    gboolean natural_order;
    gchar *key1;
    gchar *key2;

    dot_first     = get_sort_option (ctname, tv_name, col_name, "dot_first");
    special_first = get_sort_option (ctname, tv_name, col_name, "special_first");
    natural_order = get_sort_option (ctname, tv_name, col_name, "natural_order");

    key1 = get_node_key (ctname, tv_name, col_name, node1,
            dot_first, special_first, natural_order);
    key2 = get_node_key (ctname, tv_name, col_name, node2,
            dot_first, special_first, natural_order);

    return strcmp (key1, key2);
}

DonnaColumnType *
donna_column_type_name_new (DonnaDonna *donna)
{
    DonnaColumnType *ct;

    g_return_val_if_fail (DONNA_IS_DONNA (donna), NULL);

    g_debug ("creatig new ColumnTypeName");
    ct = g_object_new (DONNA_TYPE_COLUMNTYPE_NAME, NULL);
    DONNA_COLUMNTYPE_NAME (ct)->priv->donna = g_object_ref (donna);

    return ct;
}
