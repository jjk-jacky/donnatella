
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
};

/* ColumnType */
static gint             ct_name_get_renderers       (DonnaColumnType    *ct,
                                                     const gchar        *name,
                                                     DonnaRenderer     **renderers);
static void             ct_name_render              (DonnaColumnType    *ct,
                                                     const gchar        *name,
                                                     DonnaNode          *node,
                                                     gpointer            data,
                                                     GtkCellRenderer    *renderer);
static GtkMenu *        ct_name_get_options_menu    (DonnaColumnType    *ct,
                                                     const gchar        *name);
static gboolean         ct_name_handle_context      (DonnaColumnType    *ct,
                                                     const gchar        *name,
                                                     DonnaNode          *node);
static gboolean         ct_name_set_tooltip         (DonnaColumnType    *ct,
                                                     const gchar        *name,
                                                     DonnaNode          *node,
                                                     gpointer            data,
                                                     GtkTooltip         *tooltip);
static gint             ct_name_node_cmp            (DonnaColumnType    *ct,
                                                     const gchar        *name,
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
        G_IMPLEMENT_INTERFACE (DONNA_TYPE_COLUMNTYPE_NAME, ct_name_columntype_init)
        )

static gint
ct_name_get_renderers (DonnaColumnType   *ct,
                       const gchar       *name,
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
        /* FIXME: icon */
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
                          const gchar        *name)
{
    /* FIXME */
    return NULL;
}

static gboolean
ct_name_handle_context (DonnaColumnType    *ct,
                        const gchar        *name,
                        DonnaNode          *node)
{
    /* FIXME */
    return FALSE;
}

static gboolean
ct_name_set_tooltip (DonnaColumnType    *ct,
                     const gchar        *name,
                     DonnaNode          *node,
                     gpointer            data,
                     GtkTooltip         *tooltip)
{
    /* FIXME */
    return FALSE;
}

static gboolean
get_sort_option (const gchar *col_name, const gchar *opt_name)
{
    extern Donna *donna;
    gchar         buf[64];
    gboolean      value;

    snprintf (buf, 64, "columns/%s/sort_%s", col_name, opt_name);
    if (!donna_config_get_boolean (donna->config, buf, &value))
    {
        snprintf (buf, 64, "defaults/sort/%s", opt_name);
        if (!donna_config_get_boolean (donna->config, buf, &value))
        {
            value = TRUE;
            if (donna_config_set_boolean (donna->config, buf, value))
                g_info ("Option '%s' did not exists, initialized to TRUE",
                        buf);
        }
    }
    return value;
}

static gint
ct_name_node_cmp (DonnaColumnType    *ct,
                  const gchar        *name,
                  DonnaNode          *node1,
                  DonnaNode          *node2)
{
    gboolean dot_first;
    gboolean special_first;
    gboolean natural_order;
    gchar *name1;
    gchar *name2;
    gchar *key1;
    gchar *key2;
    gint   ret;

    dot_first     = get_sort_option (name, "dot_first");
    special_first = get_sort_option (name, "special_first");
    natural_order = get_sort_option (name, "natural_order");

    donna_node_get (node1, FALSE, "name", &name1, NULL);
    donna_node_get (node2, FALSE, "name", &name2, NULL);

    key1 = utf8_collate_key (name1, -1,
            dot_first,
            special_first,
            natural_order);
    key2 = utf8_collate_key (name2, -1,
            dot_first,
            special_first,
            natural_order);
    ret = strcmp (key1, key2);

    g_free (key1);
    g_free (key1);
    g_free (name1);
    g_free (name2);

    return ret;
}
