
#include <glib-object.h>
#include <string.h>
#include "columntype-name.h"
#include "columntype.h"
#include "node.h"
#include "macros.h"

struct _DonnaColumnTypeNamePrivate
{
};

enum
{
    CT_NAME_SORT_DEFAULT = 0,
    CT_NAME_SORT_OFF,
    CT_NAME_SORT_ON
};

struct CtNameOptions
{
    gchar   *prop_name;
    gchar   *prop_icon;

    /* visual options */
    guint    show_icon          : 1;

    /* sort options */
    guint    sort_dot_first     : 2;
    guint    sort_special_first : 2;
    guint    sort_natural_order : 2;
};

/* ColumnType */
static gpointer         ct_name_parse_options       (DonnaColumnType    *ct,
                                                     gchar              *data);
static void             ct_name_free_options        (DonnaColumnType    *ct,
                                                     gpointer            options);
static gint             ct_name_get_renderers       (DonnaColumnType    *ct,
                                                     gpointer            options,
                                                     DonnaRenderer     **renderers);
static void             ct_name_render              (DonnaColumnType    *ct,
                                                     gpointer            options,
                                                     DonnaNode          *node,
                                                     gpointer            data,
                                                     GtkCellRenderer    *renderer);
static GtkMenu *        ct_name_get_options_menu    (DonnaColumnType    *ct,
                                                     gpointer            options);
static gboolean         ct_name_handle_context      (DonnaColumnType    *ct,
                                                     gpointer            options,
                                                     DonnaNode          *node);
static gboolean         ct_name_set_tooltip         (DonnaColumnType    *ct,
                                                     gpointer            options,
                                                     DonnaNode          *node,
                                                     gpointer            data,
                                                     GtkTooltip         *tooltip);
static gint             ct_name_node_cmp            (DonnaColumnType    *ct,
                                                     gpointer            options,
                                                     DonnaNode          *node1,
                                                     DonnaNode          *node2);

static void
ct_name_columntype_init (DonnaColumnTypeInterface *interface)
{
    interface->parse_options          = ct_name_parse_options;
    interface->free_options           = ct_name_free_options;
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

static gpointer
ct_name_parse_options (DonnaColumnType    *ct,
                       gchar              *data)
{
    struct CtNameOptions *options;

    g_return_val_if_fail (DONNA_IS_COLUMNTYPE_NAME (ct), NULL);

    options = g_slice_new0 (struct CtNameOptions);
    options->show_icon = 1;

    if (data)
    {
        GKeyFile *kf;
        gchar *s;

        kf = g_key_file_new ();
        if (!g_key_file_load_from_data (kf, data, -1, G_KEY_FILE_NONE, NULL))
        {
            g_slice_free (struct CtNameOptions, options);
            return NULL;
        }
        options->prop_name = g_key_file_get_value (kf,
                DONNA_COLUMNTYPE_OPTIONS_GROUP, "name", NULL);
        options->prop_icon = g_key_file_get_value (kf,
                DONNA_COLUMNTYPE_OPTIONS_GROUP, "icon", NULL);
        /* check first, because we want to keep our default or TRUE */
        if (g_key_file_has_key (kf, DONNA_COLUMNTYPE_OPTIONS_GROUP,
                    "show_icon", NULL))
            options->show_icon = g_key_file_get_boolean (kf,
                    DONNA_COLUMNTYPE_OPTIONS_GROUP, "show_icon", NULL);
        /* we get the value instead of using a boolean because a third value is
         * allowed - "default" - to use donna's default */
        s = g_key_file_get_value (kf, DONNA_COLUMNTYPE_OPTIONS_GROUP,
                "sort_dot_first", NULL);
        if (s)
        {
            if (streq (s, "true"))
                options->sort_dot_first = CT_NAME_SORT_ON;
            else if (streq (s, "false"))
                options->sort_dot_first = CT_NAME_SORT_OFF;
            /* already defaulting to default, no need for this:
            else if (streq (s, "default"))
                options->sort_dot_first = CT_NAME_SORT_DEFAULT;
            */
        }
        g_key_file_free (kf);
    }

    return (gpointer) options;
}

static void
ct_name_free_options (DonnaColumnType *ct, gpointer options)
{
    struct CtNameOptions *o = options;

    g_return_if_fail (DONNA_IS_COLUMNTYPE_NAME (ct));

    if (!options)
        return;

    g_free (o->prop_name);
    g_free (o->prop_icon);
    g_slice_free (struct CtNameOptions, options);
}

static gint
ct_name_get_renderers (DonnaColumnType   *ct,
                       gpointer           options,
                       DonnaRenderer    **renderers)
{
    g_return_val_if_fail (DONNA_IS_COLUMNTYPE_NAME (ct), 0);
    g_return_val_if_fail (options != NULL, 0);

    *renderers = g_new0 (DonnaRenderer, 2);
    (*renderers)[0].type = DONNA_COLUMNTYPE_RENDERER_PIXBUF;
    (*renderers)[1].type = DONNA_COLUMNTYPE_RENDERER_TEXT;

    return 2;
}

static void
ct_name_render (DonnaColumnType    *ct,
                gpointer            options,
                DonnaNode          *node,
                gpointer            data,
                GtkCellRenderer    *renderer)
{
}

static GtkMenu *
ct_name_get_options_menu (DonnaColumnType    *ct,
                          gpointer            options)
{
}

static gboolean
ct_name_handle_context (DonnaColumnType    *ct,
                        gpointer            options,
                        DonnaNode          *node)
{
}

static gboolean
ct_name_set_tooltip (DonnaColumnType    *ct,
                     gpointer            options,
                     DonnaNode          *node,
                     gpointer            data,
                     GtkTooltip         *tooltip)
{
}

static gint
ct_name_node_cmp (DonnaColumnType    *ct,
                  gpointer            options,
                  DonnaNode          *node1,
                  DonnaNode          *node2)
{
}
