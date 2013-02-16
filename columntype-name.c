
#include <glib-object.h>
#include <string.h>
#include "columntype.h"
#include "columntype-name.h"
#include "node.h"
#include "donna.h"
#include "conf.h"
#include "sort.h"
#include "macros.h"

struct _DonnaColumnTypeNamePrivate
{
    GPtrArray *domains;
};

static void             ct_name_finalize            (GObject            *object);

/* ColumnType */
static gint             ct_name_get_renderers       (DonnaColumnType    *ct,
                                                     const gchar        *tv_name,
                                                     const gchar        *col_name,
                                                     DonnaRenderer     **renderers);
static void             ct_name_render              (DonnaColumnType    *ct,
                                                     const gchar        *tv_name,
                                                     const gchar        *col_name,
                                                     DonnaNode          *node,
                                                     gpointer            data,
                                                     GtkCellRenderer    *renderer);
static GtkMenu *        ct_name_get_options_menu    (DonnaColumnType    *ct,
                                                     const gchar        *tv_name,
                                                     const gchar        *col_name);
static gboolean         ct_name_handle_context      (DonnaColumnType    *ct,
                                                     const gchar        *tv_name,
                                                     const gchar        *col_name,
                                                     DonnaNode          *node);
static gboolean         ct_name_set_tooltip         (DonnaColumnType    *ct,
                                                     const gchar        *tv_name,
                                                     const gchar        *col_name,
                                                     DonnaNode          *node,
                                                     gpointer            data,
                                                     GtkTooltip         *tooltip);
static gint             ct_name_node_cmp            (DonnaColumnType    *ct,
                                                     const gchar        *tv_name,
                                                     const gchar        *col_name,
                                                     DonnaNode          *node1,
                                                     DonnaNode          *node2);

static void
ct_name_columntype_init (DonnaColumnTypeInterface *interface)
{
    interface->get_renderers          = ct_name_get_renderers;
    interface->render                 = ct_name_render;
    interface->get_options_menu       = ct_name_get_options_menu;
    interface->handle_context         = ct_name_handle_context;
    interface->set_tooltip            = ct_name_set_tooltip;
    interface->node_cmp               = ct_name_node_cmp;
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
        G_IMPLEMENT_INTERFACE (DONNA_TYPE_COLUMNTYPE_NAME, ct_name_columntype_init)
        )

static void
ct_name_finalize (GObject *object)
{
    DonnaColumnTypeNamePrivate *priv;

    priv = DONNA_COLUMNTYPE_NAME (object)->priv;
    g_ptr_array_free (priv->domains, TRUE);

    /* chain up */
    G_OBJECT_CLASS (donna_column_type_name_parent_class)->finalize (object);
}

static gint
ct_name_get_renderers (DonnaColumnType   *ct,
                       const gchar       *tv_name,
                       const gchar       *col_name,
                       DonnaRenderer    **renderers)
{
    g_return_val_if_fail (DONNA_IS_COLUMNTYPE_NAME (ct), 0);

    *renderers = g_new0 (DonnaRenderer, 2);
    (*renderers)[0].type = DONNA_COLUMNTYPE_RENDERER_PIXBUF;
    (*renderers)[0].data = GINT_TO_POINTER (DONNA_COLUMNTYPE_RENDERER_PIXBUF);
    (*renderers)[1].type = DONNA_COLUMNTYPE_RENDERER_TEXT;
    (*renderers)[1].data = GINT_TO_POINTER (DONNA_COLUMNTYPE_RENDERER_TEXT);

    return 2;
}

static void
ct_name_render (DonnaColumnType    *ct,
                const gchar        *tv_name,
                const gchar        *col_name,
                DonnaNode          *node,
                gpointer            data,
                GtkCellRenderer    *renderer)
{
    gint type = GPOINTER_TO_INT (data);
    gchar *name;

    g_return_if_fail (DONNA_IS_COLUMNTYPE_NAME (ct));

    if (type == DONNA_COLUMNTYPE_RENDERER_PIXBUF)
    {
        DonnaNodeHasValue has_value;
        GdkPixBuf *pixbuf;

        donna_node_get (node, FALSE, "icon", &has_value, &pixbuf, NULL);
        if (has_value == DONNA_NODE_VALUE_SET)
        {
            g_object_set (renderer, "pixbuf", pixbuf, NULL);
            g_object_unref (pixbuf);
        }
        else
        {
            DonnaNodeType node_type;

            donna_node_get (node, FALSE, "node-type", &node_type, NULL);
            if (node_type == DONNA_NODE_ITEM)
                g_object_set (renderer, "stock-id", GTK_STOCK_FILE, NULL);
            else if (node_type == DONNA_NODE_CONTAINER)
                g_object_set (renderer, "stock-id", GTK_STOCK_DIRECTORY, NULL);
            else /* DONNA_NODE_EXTENDED */
                g_object_set (renderer, "stock-id", GTK_STOCK_EXECUTE, NULL);

            if (has_value == DONNA_NODE_VALUE_NEED_REFRESH)
            {
                DonnaTask *task;

                /* a default icon was set up, now let's go get the "real" one */
                task = donna_node_refresh_task (node, "icon", NULL);
                if (G_LIKELY (task))
                {
                    //FIXME:donna_task_set_callback (task, cb, data, destroy);
                    donna_start_internal_task (task);
                }
            }
        }
    }
    else /* DONNA_COLUMNTYPE_RENDERER_TEXT */
    {
        donna_node_get (node, FALSE, "name", &name, NULL);
        g_object_set (renderer, "text", name, NULL);
        g_free (name);
    }
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
                        DonnaNode          *node)
{
    /* FIXME */
    return FALSE;
}

static gboolean
ct_name_set_tooltip (DonnaColumnType    *ct,
                     const gchar        *tv_name,
                     const gchar        *col_name,
                     DonnaNode          *node,
                     gpointer            data,
                     GtkTooltip         *tooltip)
{
    /* FIXME */
    return FALSE;
}

static gboolean
get_sort_option (const gchar *tv_name,
                 const gchar *col_name,
                 const gchar *opt_name)
{
    extern Donna *donna;
    gchar         buf[128];
    gboolean      value;

    snprintf (buf, 128, "treeviews/%s/columns/%s/sort_%s",
            tv_name, col_name, opt_name);
    if (!donna_config_get_boolean (donna->config, buf, &value))
    {
        snprintf (buf, 128, "columns/%s/sort_%s", col_name, opt_name);
        if (!donna_config_get_boolean (donna->config, buf, &value))
        {
            snprintf (buf, 128, "defaults/sort/%s", opt_name);
            if (!donna_config_get_boolean (donna->config, buf, &value))
            {
                value = TRUE;
                if (donna_config_set_boolean (donna->config, buf, value))
                    g_info ("Option '%s' did not exists, initialized to TRUE",
                            buf);
            }
        }
    }
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
get_node_key (DonnaColumnType   *ct,
              const gchar       *tv_name,
              const gchar       *col_name,
              DonnaNode         *node,
              gboolean           dot_first,
              gboolean           special_first,
              gboolean           natural_order)
{
    DonnaColumnTypeNamePrivate *priv;
    gchar  buf[128];
    gchar *key;

    snprintf (buf, 128, "%s/%s/utf8-collate-key", tv_name, col_name);
    key = g_object_get_data (G_OBJECT (node), buf);
    /* no key, or invalid (options changed) */
    if (!key || *key != sort_get_options_char (dot_first, special_first,
                natural_order))
    {
        gchar *s;

        /* if we're installing the key (i.e. not updating an invalid one) we
         * need to make sure we're listening on the provider's
         * node-updated::name signal, to remove the key on rename */
        if (!key)
        {
            const gchar *domain;
            guint i;

            priv = DONNA_COLUMNTYPE_NAME (ct)->priv;
            donna_node_get (node, FALSE, "domain", &domain, NULL);
            for (i = 0; i < priv->domains->len; ++i)
                if (streq (domain, priv->domains->pdata[i]))
                    break;
            /* no match, must connect */
            if (i >= priv->domains->len)
            {
                DonnaProvider *provider;

                donna_node_get (node, FALSE, "provider", &provider, NULL);
                g_signal_connect_data (provider, "node-updated::name",
                        G_CALLBACK (node_updated_cb),
                        g_strdup (buf),
                        (GClosureNotify) g_free,
                        0);
                g_object_unref (provider);

                g_ptr_array_add (priv->domains, (gpointer) domain);
            }
        }

        donna_node_get (node, FALSE, "name", &s, NULL);
        key = sort_get_utf8_collate_key (s, -1, dot_first, special_first,
                natural_order);
        g_free (s);
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
    gboolean dot_first;
    gboolean special_first;
    gboolean natural_order;
    gchar *key1;
    gchar *key2;

    dot_first     = get_sort_option (tv_name, col_name, "dot_first");
    special_first = get_sort_option (tv_name, col_name, "special_first");
    natural_order = get_sort_option (tv_name, col_name, "natural_order");

    key1 = get_node_key (ct, tv_name, col_name, node1,
            dot_first, special_first, natural_order);
    key2 = get_node_key (ct, tv_name, col_name, node2,
            dot_first, special_first, natural_order);

    return strcmp (key1, key2);
}
