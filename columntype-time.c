
#include <glib-object.h>
#include <ctype.h>              /* isblank() */
#include <time.h>
#include "columntype.h"
#include "columntype-time.h"
#include "node.h"
#include "donna.h"
#include "conf.h"
#include "util.h"
#include "macros.h"

enum
{
    PROP_0,

    PROP_APP,

    NB_PROPS
};

enum
{
    WHICH_OTHER = 0,
    WHICH_MTIME,
    WHICH_ATIME,
    WHICH_CTIME,
};

struct tv_col_data
{
    gint8             which;
    gchar            *property;
    gchar            *format;
    DonnaTimeOptions  options;
    gint8             which_tooltip;
    gchar            *property_tooltip;
    gchar            *format_tooltip;
    DonnaTimeOptions  options_tooltip;
};

enum unit
{
    /* valid for unit & unit_age */
    UNIT_YEAR           = 'Y',
    UNIT_MONTH          = 'm',
    UNIT_WEEK           = 'V',
    UNIT_DAY            = 'd',
    UNIT_HOUR           = 'H',
    UNIT_MINUTE         = 'M',
    UNIT_SECOND         = 'S',
    /* invalid for unit_age */
    UNIT_DATE           = 'D',
    UNIT_DAY_OF_YEAR    = 'j',
    UNIT_DAY_OF_WEEK    = 'u',  /* 1-7, 1=Monday (ISO-8601) */
    UNIT_DAY_OF_WEEK_2  = 'w',  /* 0-6, 0=Sunday */
    /* special case */
    UNIT_AGE            = 'A',
};

enum comp
{
    COMP_LESSER_EQUAL,
    COMP_LESSER,
    COMP_EQUAL,
    COMP_GREATER,
    COMP_GREATER_EQUAL,
    COMP_IN_RANGE
};

struct filter_data
{
    enum unit   unit;
    enum unit   unit_age;
    enum comp   comp;
    guint64     ref;
    guint64     ref2;
};

struct _DonnaColumnTypeTimePrivate
{
    DonnaApp *app;
};

static void             ct_time_set_property        (GObject            *object,
                                                     guint               prop_id,
                                                     const GValue       *value,
                                                     GParamSpec         *pspec);
static void             ct_time_get_property        (GObject            *object,
                                                     guint               prop_id,
                                                     GValue             *value,
                                                     GParamSpec         *pspec);
static void             ct_time_finalize            (GObject            *object);

/* ColumnType */
static const gchar *    ct_time_get_name            (DonnaColumnType    *ct);
static const gchar *    ct_time_get_renderers       (DonnaColumnType    *ct);
static DonnaColumnTypeNeed ct_time_refresh_data     (DonnaColumnType    *ct,
                                                     const gchar        *tv_name,
                                                     const gchar        *col_name,
                                                     const gchar        *arr_name,
                                                     gpointer           *data);
static void             ct_time_free_data           (DonnaColumnType    *ct,
                                                     gpointer            data);
static GPtrArray *      ct_time_get_props           (DonnaColumnType    *ct,
                                                     gpointer            data);
static gboolean         ct_time_can_edit            (DonnaColumnType    *ct,
                                                     gpointer            data,
                                                     DonnaNode          *node,
                                                     GError            **error);
static gboolean         ct_time_edit                (DonnaColumnType    *ct,
                                                     gpointer            data,
                                                     DonnaNode          *node,
                                                     GtkCellRenderer   **renderers,
                                                     renderer_edit_fn    renderer_edit,
                                                     gpointer            re_data,
                                                     DonnaTreeView      *treeview,
                                                     GError            **error);
static gboolean         ct_time_set_value           (DonnaColumnType    *ct,
                                                     gpointer            data,
                                                     GPtrArray          *nodes,
                                                     const gchar        *value,
                                                     DonnaNode          *node_ref,
                                                     DonnaTreeView      *treeview,
                                                     GError            **error);
static GPtrArray *      ct_time_render              (DonnaColumnType    *ct,
                                                     gpointer            data,
                                                     guint               index,
                                                     DonnaNode          *node,
                                                     GtkCellRenderer    *renderer);
static gint             ct_time_node_cmp            (DonnaColumnType    *ct,
                                                     gpointer            data,
                                                     DonnaNode          *node1,
                                                     DonnaNode          *node2);
static gboolean         ct_time_set_tooltip         (DonnaColumnType    *ct,
                                                     gpointer            data,
                                                     guint               index,
                                                     DonnaNode          *node,
                                                     GtkTooltip         *tooltip);
static gboolean         ct_time_is_match_filter     (DonnaColumnType    *ct,
                                                     const gchar        *filter,
                                                     gpointer           *filter_data,
                                                     gpointer            data,
                                                     DonnaNode          *node,
                                                     GError            **error);
static void             ct_time_free_filter_data    (DonnaColumnType    *ct,
                                                     gpointer            filter_data);
static DonnaColumnTypeNeed ct_time_set_option       (DonnaColumnType    *ct,
                                                     const gchar        *tv_name,
                                                     const gchar        *col_name,
                                                     const gchar        *arr_name,
                                                     gpointer            data,
                                                     const gchar        *option,
                                                     const gchar        *value,
                                                     DonnaColumnOptionSaveLocation save_location,
                                                     GError            **error);
static gchar *          ct_time_get_context_alias   (DonnaColumnType   *ct,
                                                     gpointer            data,
                                                     const gchar       *alias,
                                                     const gchar       *extra,
                                                     DonnaContextReference reference,
                                                     DonnaNode         *node_ref,
                                                     get_sel_fn         get_sel,
                                                     gpointer           get_sel_data,
                                                     const gchar       *prefix,
                                                     GError           **error);
static gboolean         ct_time_get_context_item_info (
                                                     DonnaColumnType   *ct,
                                                     gpointer            data,
                                                     const gchar       *item,
                                                     const gchar       *extra,
                                                     DonnaContextReference reference,
                                                     DonnaNode         *node_ref,
                                                     get_sel_fn         get_sel,
                                                     gpointer           get_sel_data,
                                                     DonnaContextInfo  *info,
                                                     GError           **error);

static void
ct_time_columntype_init (DonnaColumnTypeInterface *interface)
{
    interface->get_name                 = ct_time_get_name;
    interface->get_renderers            = ct_time_get_renderers;
    interface->refresh_data             = ct_time_refresh_data;
    interface->free_data                = ct_time_free_data;
    interface->get_props                = ct_time_get_props;
    interface->can_edit                 = ct_time_can_edit;
    interface->edit                     = ct_time_edit;
    interface->set_value                = ct_time_set_value;
    interface->render                   = ct_time_render;
    interface->set_tooltip              = ct_time_set_tooltip;
    interface->node_cmp                 = ct_time_node_cmp;
    interface->is_match_filter          = ct_time_is_match_filter;
    interface->free_filter_data         = ct_time_free_filter_data;
    interface->set_option               = ct_time_set_option;
    interface->get_context_alias        = ct_time_get_context_alias;
    interface->get_context_item_info    = ct_time_get_context_item_info;
}

static void
donna_column_type_time_class_init (DonnaColumnTypeTimeClass *klass)
{
    GObjectClass *o_class;

    o_class = (GObjectClass *) klass;
    o_class->set_property   = ct_time_set_property;
    o_class->get_property   = ct_time_get_property;
    o_class->finalize       = ct_time_finalize;

    g_object_class_override_property (o_class, PROP_APP, "app");

    g_type_class_add_private (klass, sizeof (DonnaColumnTypeTimePrivate));
}

static void
donna_column_type_time_init (DonnaColumnTypeTime *ct)
{
    DonnaColumnTypeTimePrivate *priv;

    priv = ct->priv = G_TYPE_INSTANCE_GET_PRIVATE (ct,
            DONNA_TYPE_COLUMNTYPE_TIME,
            DonnaColumnTypeTimePrivate);
}

G_DEFINE_TYPE_WITH_CODE (DonnaColumnTypeTime, donna_column_type_time,
        G_TYPE_OBJECT,
        G_IMPLEMENT_INTERFACE (DONNA_TYPE_COLUMNTYPE, ct_time_columntype_init)
        )

static void
ct_time_finalize (GObject *object)
{
    DonnaColumnTypeTimePrivate *priv;

    priv = DONNA_COLUMNTYPE_TIME (object)->priv;
    g_object_unref (priv->app);

    /* chain up */
    G_OBJECT_CLASS (donna_column_type_time_parent_class)->finalize (object);
}

static void
ct_time_set_property (GObject            *object,
                      guint               prop_id,
                      const GValue       *value,
                      GParamSpec         *pspec)
{
    if (G_LIKELY (prop_id == PROP_APP))
        DONNA_COLUMNTYPE_TIME (object)->priv->app = g_value_dup_object (value);
    else
        G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
}

static void
ct_time_get_property (GObject            *object,
                      guint               prop_id,
                      GValue             *value,
                      GParamSpec         *pspec)
{
    if (G_LIKELY (prop_id == PROP_APP))
        g_value_set_object (value, DONNA_COLUMNTYPE_TIME (object)->priv->app);
    else
        G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
}

static const gchar *
ct_time_get_name (DonnaColumnType *ct)
{
    g_return_val_if_fail (DONNA_IS_COLUMNTYPE_TIME (ct), NULL);
    return "time";
}


static const gchar *
ct_time_get_renderers (DonnaColumnType   *ct)
{
    g_return_val_if_fail (DONNA_IS_COLUMNTYPE_TIME (ct), NULL);
    return "t";
}

static DonnaColumnTypeNeed
ct_time_refresh_data (DonnaColumnType    *ct,
                      const gchar        *tv_name,
                      const gchar        *col_name,
                      const gchar        *arr_name,
                      gpointer           *_data)
{
    DonnaColumnTypeTime *cttime = DONNA_COLUMNTYPE_TIME (ct);
    DonnaConfig *config;
    struct tv_col_data *data;
    DonnaColumnTypeNeed need = DONNA_COLUMNTYPE_NEED_NOTHING;
    gchar *s;
    guint sec;

    config = donna_app_peek_config (cttime->priv->app);

    if (!*_data)
        *_data = g_new0 (struct tv_col_data, 1);
    data = *_data;

    s = donna_config_get_string_column (config, tv_name, col_name, arr_name,
            "columntypes/time", "property", "mtime", NULL);
    if (!streq (data->property, s))
    {
        g_free (data->property);
        data->property = s;
        need = DONNA_COLUMNTYPE_NEED_REDRAW | DONNA_COLUMNTYPE_NEED_RESORT;

        if (streq (s, "mtime"))
            data->which = WHICH_MTIME;
        else if (streq (s, "atime"))
            data->which = WHICH_ATIME;
        else if (streq (s, "ctime"))
            data->which = WHICH_CTIME;
        else
            data->which = WHICH_OTHER;
    }
    else
        g_free (s);

    s = donna_config_get_string_column (config, tv_name, col_name, arr_name,
            "time", "format", "%O", NULL);
    if (!streq (data->format, s))
    {
        g_free (data->format);
        data->format = s;
        need = DONNA_COLUMNTYPE_NEED_REDRAW;
    }
    else
        g_free (s);

    sec = (guint) donna_config_get_int_column (config, tv_name, col_name, arr_name,
            "time", "age_span_seconds", 7*24*3600, NULL);
    if (data->options.age_span_seconds != sec)
    {
        data->options.age_span_seconds = sec;
        need = DONNA_COLUMNTYPE_NEED_REDRAW;
    }

    s = donna_config_get_string_column (config, tv_name, col_name, arr_name,
            "time", "age_fallback_format", "%F %T", NULL);
    if (!streq (data->options.age_fallback_format, s))
    {
        g_free ((gchar *) data->options.age_fallback_format);
        data->options.age_fallback_format = s;
        need = DONNA_COLUMNTYPE_NEED_REDRAW;
    }
    else
        g_free (s);

    s = donna_config_get_string_column (config, tv_name, col_name, arr_name,
            "columntypes/time", "property_tooltip", "mtime", NULL);
    if (!streq (data->property_tooltip, s))
    {
        g_free (data->property_tooltip);
        data->property_tooltip = s;

        if (streq (s, "mtime"))
            data->which_tooltip = WHICH_MTIME;
        else if (streq (s, "atime"))
            data->which_tooltip = WHICH_ATIME;
        else if (streq (s, "ctime"))
            data->which_tooltip = WHICH_CTIME;
        else
            data->which_tooltip = WHICH_OTHER;
    }
    else
        g_free (s);

    s = donna_config_get_string_column (config, tv_name, col_name, arr_name,
            "columntypes/time", "format_tooltip", "%c", NULL);
    if (!streq (data->format_tooltip, s))
    {
        g_free (data->format_tooltip);
        data->format_tooltip = s;
    }
    else
        g_free (s);

    sec = (guint) donna_config_get_int_column (config, tv_name, col_name, arr_name,
            NULL, "age_span_seconds_tooltip", 7*24*3600, NULL);
    if (data->options_tooltip.age_span_seconds != sec)
    {
        data->options_tooltip.age_span_seconds = sec;
    }

    s = donna_config_get_string_column (config, tv_name, col_name, arr_name,
            NULL, "age_fallback_format_tooltip", "%F %T", NULL);
    if (!streq (data->options_tooltip.age_fallback_format, s))
    {
        g_free ((gchar *) data->options_tooltip.age_fallback_format);
        data->options_tooltip.age_fallback_format = s;
    }
    else
        g_free (s);

    return need;
}

static void
ct_time_free_data (DonnaColumnType    *ct,
                   gpointer            _data)
{
    struct tv_col_data *data = _data;

    g_free (data->property);
    g_free (data->format);
    g_free ((gchar *) data->options.age_fallback_format);
    g_free (data->property_tooltip);
    g_free (data->format_tooltip);
    g_free ((gchar *) data->options_tooltip.age_fallback_format);
    g_free (data);
}

static GPtrArray *
ct_time_get_props (DonnaColumnType  *ct,
                   gpointer          data)
{
    GPtrArray *props;

    g_return_val_if_fail (DONNA_IS_COLUMNTYPE_TIME (ct), NULL);

    props = g_ptr_array_new_full (1, g_free);
    g_ptr_array_add (props, g_strdup (((struct tv_col_data *) data)->property));

    return props;
}

#define warn_not_uint64(node)    do {                   \
    gchar *location = donna_node_get_location (node);   \
    g_warning ("ColumnType 'time': property '%s' for node '%s:%s' isn't of expected type (%s instead of %s)",  \
            data->property,                             \
            donna_node_get_domain (node), location,     \
            G_VALUE_TYPE_NAME (&value),                 \
            g_type_name (G_TYPE_UINT64));               \
    g_free (location);                                  \
} while (0)

struct editing_data
{
    struct tv_col_data *data;
    DonnaApp        *app;
    DonnaTreeView   *tree;
    DonnaNode       *node;
    guint            sid;
    GPtrArray       *arr;
    GtkWidget       *window;
    GtkToggleButton *rad_sel;
    GtkToggleButton *rad_ref;
    GtkEntry        *entry;
};

static void
window_destroy_cb (struct editing_data *ed)
{
    if (ed->arr)
        g_ptr_array_unref (ed->arr);
    g_free (ed);
}

static gboolean
key_press_event_cb (GtkWidget *w, GdkEventKey *event, struct editing_data *ed)
{
    if (event->keyval == GDK_KEY_Escape)
    {
        gtk_widget_destroy (ed->window);
        return TRUE;
    }
    return FALSE;
}

static guint64
get_ref_time (struct tv_col_data *data, DonnaNode *node, gint which)
{
    GValue value = G_VALUE_INIT;
    DonnaNodeHasValue has;
    guint64 time;

    if (which == WHICH_OTHER)
        which = data->which;

    if (which == WHICH_MTIME)
        has = donna_node_get_mtime (node, TRUE, &time);
    else if (which == WHICH_ATIME)
        has = donna_node_get_atime (node, TRUE, &time);
    else if (which == WHICH_CTIME)
        has = donna_node_get_ctime (node, TRUE, &time);
    else
        donna_node_get (node, TRUE, data->property, &has, &value, NULL);

    if (has != DONNA_NODE_VALUE_SET && which == WHICH_OTHER)
    {
        if (G_VALUE_TYPE (&value) != G_TYPE_UINT64)
        {
            warn_not_uint64 (node);
            g_value_unset (&value);
            return (guint64) -1;
        }
        time = g_value_get_uint64 (&value);
        g_value_unset (&value);
    }

    return (has == DONNA_NODE_VALUE_SET) ? time : (guint64) -1;
}

enum
{
    /* no joker in use */
    JOKER_NOT_SET = 0,
    /* before first element, use dt_ref */
    JOKER_BEFORE,
    /* use the joker */
    JOKER_USE
};

#define syntax_error()  do {                        \
    g_set_error (error, DONNA_COLUMNTYPE_ERROR,     \
            DONNA_COLUMNTYPE_ERROR_INVALID_SYNTAX,  \
            "Invalid date format (must be [m|a|c|v[+/- x Y|m|V|d|h|m|s] [YYYY-MM-DD[ HH:MM[:SS]] or HH:MM[:SS]]): %s", \
            fmt);                                   \
    g_date_time_unref (dt_ref);                     \
    if (dt_cur)                                     \
        g_date_time_unref (dt_cur);                 \
    return (guint64) -1;                            \
} while (0)

#define get_element_from_dt(unit, dt)   do {                \
    switch (unit)                                           \
    {                                                       \
        case UNIT_YEAR:                                     \
            year = g_date_time_get_year (dt);               \
            break;                                          \
        case UNIT_MONTH:                                    \
            month = g_date_time_get_month (dt);             \
            break;                                          \
        case UNIT_DAY:                                      \
            day = g_date_time_get_day_of_month (dt);        \
            break;                                          \
        case UNIT_HOUR:                                     \
            hour = g_date_time_get_hour (dt);               \
            break;                                          \
        case UNIT_MINUTE:                                   \
            minute = g_date_time_get_minute (dt);           \
            break;                                          \
        case UNIT_SECOND:                                   \
            seconds = g_date_time_get_second (dt);          \
            break;                                          \
    }                                                       \
} while (0)

static guint64
get_ts (struct tv_col_data  *data,
        const gchar         *fmt,
        DonnaNode           *node_ref,
        DonnaNode           *node_cur,
        gboolean            *is_ts_fixed,
        GError             **error)
{
    gsize len = strlen (fmt);
    GDateTime *dt_ref;
    GDateTime *dt_cur = NULL;
    guint i;
    guint64 ts;
    /* "default" values, when using joker '!' */
    gint year       = 1970;
    gint month      = 1;
    gint day        = 1;
    guint hour      = 0;
    guint minute    = 0;
    gdouble seconds = 0;

    if (is_ts_fixed)
        *is_ts_fixed = TRUE;
    skip_blank (fmt);

    /* is there a reference specified? */
    if (*fmt == 'n')
    {
        dt_ref = g_date_time_new_now_local ();
        ++fmt;
        skip_blank (fmt);
    }
    else
    {
        guint64 ref;

        switch (*fmt)
        {
            case 'm':
                ref = get_ref_time (data, node_ref, WHICH_MTIME);
                ++fmt;
                skip_blank (fmt);
                break;
            case 'a':
                ref = get_ref_time (data, node_ref, WHICH_ATIME);
                ++fmt;
                skip_blank (fmt);
                break;
            case 'c':
                ref = get_ref_time (data, node_ref, WHICH_CTIME);
                ++fmt;
                skip_blank (fmt);
                break;
            case 'v':
                ++fmt;
                skip_blank (fmt);
                /* fall through */
            default:
                ref = get_ref_time (data, node_ref, WHICH_OTHER);
                break;
        }
        if (ref == (guint64) -1)
        {
            g_set_error (error, DONNA_COLUMNTYPE_ERROR,
                    DONNA_COLUMNTYPE_ERROR_OTHER,
                    "Invalid reference time");
            return (guint64) -1;
        }
        dt_ref = g_date_time_new_from_unix_local (ref);
    }

    /* any math to do on ref */
    while (*fmt == '+' || *fmt == '-')
    {
        GDateTime *dt;
        gint64 nb;

        nb = g_ascii_strtoll (fmt, (gchar **) &fmt, 10);
        skip_blank (fmt);
        switch (*fmt)
        {
            case UNIT_YEAR:
                dt = g_date_time_add_years (dt_ref, nb);
                break;
            case UNIT_MONTH:
                dt = g_date_time_add_months (dt_ref, nb);
                break;
            case UNIT_WEEK:
                dt = g_date_time_add_weeks (dt_ref, nb);
                break;
            case UNIT_DAY:
                dt = g_date_time_add_days (dt_ref, nb);
                break;
            case UNIT_HOUR:
                dt = g_date_time_add_hours (dt_ref, nb);
                break;
            case UNIT_MINUTE:
                dt = g_date_time_add_minutes (dt_ref, nb);
                break;
            case UNIT_SECOND:
                dt = g_date_time_add_seconds (dt_ref, nb);
                break;
            default:
                g_set_error (error, DONNA_COLUMNTYPE_ERROR,
                        DONNA_COLUMNTYPE_ERROR_OTHER,
                        "Invalid unit '%c' in reference math: %s",
                        *fmt, fmt);
                g_date_time_unref (dt_ref);
                return (guint64) -1;
        }
        g_date_time_unref (dt_ref);
        dt_ref = dt;
        ++fmt;
        skip_blank (fmt);
    }

    struct
    {
        gchar unit;
        gchar after;
    } elements[] = {
        { UNIT_YEAR,    '-' },
        { UNIT_MONTH,   '-' },
        { UNIT_DAY,      0  },
        { UNIT_HOUR,    ':' },
        { UNIT_MINUTE,  ':' },
        { UNIT_SECOND,   0  }
    };
    guint nb = sizeof (elements) / sizeof (elements[0]);
    gchar joker = 0;
    gchar unit_joker;
    gint  joker_st = JOKER_NOT_SET;

    /* process all date/time elements */
    for (i = 0; i < nb; ++i)
    {
        /* can we fill things with jokers? */
        if (joker_st != JOKER_NOT_SET)
        {
            if (elements[i].unit == unit_joker)
                joker_st = JOKER_USE;

            if (joker_st == JOKER_BEFORE)
                get_element_from_dt (elements[i].unit, dt_ref);
            else /* JOKER_USE */
            {
                if (joker == '#')
                    get_element_from_dt (elements[i].unit, dt_ref);
                else if (joker == '*')
                {
                    if (is_ts_fixed)
                        *is_ts_fixed = FALSE;
                    if (!dt_cur)
                    {
                        guint64 ref;

                        ref = get_ref_time (data, node_cur, WHICH_OTHER);
                        if (ref == (guint64) -1)
                        {
                            g_set_error (error, DONNA_COLUMNTYPE_ERROR,
                                    DONNA_COLUMNTYPE_ERROR_OTHER,
                                    "Invalid current time");
                            g_date_time_unref (dt_ref);
                            return (guint64) -1;
                        }
                        dt_cur = g_date_time_new_from_unix_local (ref);
                    }
                    get_element_from_dt (elements[i].unit, dt_cur);
                }
                /* else ('!') nothing to do, "defaults" are already set */
            }

            continue;
        }

        if (*fmt == '!' || *fmt == '*' || *fmt == '#')
        {
            joker = *fmt++;
            if (*fmt == UNIT_YEAR || *fmt == UNIT_MONTH || *fmt == UNIT_DAY
                    || *fmt == UNIT_HOUR || *fmt == UNIT_MINUTE
                    || *fmt == UNIT_SECOND || *fmt == '\0')
            {
                unit_joker = (*fmt == '\0') ? elements[i].unit : *fmt;
                /* will be adjusted on next loop iteration */
                joker_st = JOKER_BEFORE;
                /* next iteration should do thing element (again) */
                --i;
                continue;
            }
        }
        else if (*fmt >= '0' && *fmt <= '9')
        {
            guint64 nb;

            nb = g_ascii_strtoull (fmt, (gchar **) &fmt, 10);
            switch (elements[i].unit)
            {
                case UNIT_YEAR:
                    year = (gint) nb;
                    break;
                case UNIT_MONTH:
                    month = (gint) nb;
                    break;
                case UNIT_DAY:
                    day = (gint) nb;
                    break;
                case UNIT_HOUR:
                    hour = (guint) nb;
                    break;
                case UNIT_MINUTE:
                    minute = (guint) nb;
                    break;
                case UNIT_SECOND:
                    seconds = (gdouble) nb;
                    break;
            }
        }
        else if (*fmt == '\0')
            joker = '#';
        else
            syntax_error ();

        if (joker)
        {
            if (joker == '#')
                get_element_from_dt (elements[i].unit, dt_ref);
            else if (joker == '*')
            {
                if (is_ts_fixed)
                    *is_ts_fixed = FALSE;
                if (!dt_cur)
                {
                    guint64 ref;

                    ref = get_ref_time (data, node_cur, WHICH_OTHER);
                    if (ref == (guint64) -1)
                    {
                        g_set_error (error, DONNA_COLUMNTYPE_ERROR,
                                DONNA_COLUMNTYPE_ERROR_OTHER,
                                "Invalid current time");
                        g_date_time_unref (dt_ref);
                        return (guint64) -1;
                    }
                    dt_cur = g_date_time_new_from_unix_local (ref);
                }
                get_element_from_dt (elements[i].unit, dt_cur);
            }
            /* else ('!') nothing to do, "defaults" are already set */

            joker = 0;
        }

        if (elements[i].unit == UNIT_DAY)
        {
            /* moving to possible time */
            skip_blank (fmt);
        }
        else if (elements[i].after && *fmt == elements[i].after)
            /* skip separator */
            ++fmt;
    }

    g_date_time_unref (dt_ref);
    if (dt_cur)
        g_date_time_unref (dt_cur);

    dt_ref = g_date_time_new_local (year, month, day, hour, minute, seconds);
    ts = g_date_time_to_unix (dt_ref);
    g_date_time_unref (dt_ref);
    return ts;
}

#undef get_date_bit
#undef syntax_error

static inline gboolean
set_prop (struct tv_col_data    *data,
          const gchar           *prop,
          guint64                ts,
          DonnaNode             *node,
          DonnaTreeView         *tree,
          GError               **error)
{
    GValue v = G_VALUE_INIT;
    guint64 ref;
    gboolean ret;

    ref = get_ref_time (data, node, data->which);
    if (ref != (guint64) -1 && ref == ts)
        return TRUE;

    g_value_init (&v, G_TYPE_UINT64);
    g_value_set_uint64 (&v, ts);
    ret = donna_tree_view_set_node_property (tree, node, prop, &v, error);
    g_value_unset (&v);
    return ret;
}

static gboolean
set_value (struct tv_col_data   *data,
           const gchar          *value,
           DonnaNode            *node_ref,
           GPtrArray            *nodes,
           DonnaTreeView        *tree,
           GError              **error)
{
    GError *err = NULL;
    GString *str = NULL;
    gboolean is_ts_fixed;
    const gchar *prop;
    guint64 ts;
    guint i = 0;

    if (data->which == WHICH_MTIME)
        prop = "mtime";
    else if (data->which == WHICH_ATIME)
        prop = "atime";
    else if (data->which == WHICH_CTIME)
        prop = "ctime";
    else
        prop = data->property;

    if (node_ref)
    {
        ts = get_ts (data, value,
                /* node for reference time */
                node_ref,
                /* node for time preservation */
                nodes->pdata[0],
                /* whether there's time preservation or not */
                &is_ts_fixed,
                error);
        if (ts == (guint64) -1)
            return FALSE;
    }
    else
        is_ts_fixed = FALSE;

    if (node_ref)
        set_prop (data, prop, ts, nodes->pdata[i++], tree, error);

    for ( ; i < nodes->len; ++i)
    {
        if (!is_ts_fixed)
        {
            ts = get_ts (data, value,
                    (node_ref) ? node_ref : nodes->pdata[i],
                    nodes->pdata[i],
                    NULL, &err);
            if (ts == (guint64) -1)
            {
                gchar *fl = donna_node_get_full_location (nodes->pdata[i]);

                if (!str)
                    str = g_string_new (NULL);

                g_string_append_printf (str,
                        "\n- Failed to get new timestamp for '%s', skipping",
                        fl);

                g_free (fl);
                g_clear_error (&err);
                continue;
            }
        }

        if (!set_prop (data, prop, ts, nodes->pdata[i], tree, &err))
        {
            gchar *fl = donna_node_get_full_location (nodes->pdata[i]);

            if (!str)
                str= g_string_new (NULL);

            g_string_append_printf (str, "\n- Failed to set '%s' on '%s': %s",
                    prop, fl, (err) ? err->message : "(no error message)");

            g_free (fl);
            g_clear_error (&err);
        }
    }

    if (!str)
        return TRUE;

    g_set_error (error, DONNA_COLUMNTYPE_ERROR,
            DONNA_COLUMNTYPE_ERROR_PARTIAL_COMPLETION,
            "Some operations failed :\n%s", str->str);
    g_string_free (str, TRUE);

    return FALSE;
}

static void
apply_cb (struct editing_data *ed)
{
    GError *err = NULL;
    GPtrArray *arr;
    gboolean use_arr;
    gboolean is_ref_focused;
    gboolean is_ref_unique;

    if (ed->window)
    {
        gtk_widget_hide (ed->window);

        use_arr = (ed->arr && gtk_toggle_button_get_active (ed->rad_sel));
        is_ref_focused = !use_arr || !gtk_toggle_button_get_active (ed->rad_ref);
        is_ref_unique = !use_arr || (ed->arr->len == 1) || is_ref_focused;
    }
    else
    {
        use_arr = FALSE;
        is_ref_focused = is_ref_unique = TRUE;
    }

    if (use_arr)
        arr = g_ptr_array_ref (ed->arr);
    else
    {
        arr = g_ptr_array_sized_new (1);
        g_ptr_array_add (arr, ed->node);
    }

    if (!set_value (ed->data, gtk_entry_get_text (ed->entry),
            /* node for reference time */
            (is_ref_focused) ? ed->node
            : ((use_arr) ? ed->arr->pdata[0] : ed->node),
            arr, ed->tree, &err))
    {
        donna_app_show_error (ed->app, err,
                "ColumnType 'time': Operation failed");
        g_clear_error (&err);
    }

    if (ed->window)
        gtk_widget_destroy (ed->window);
}

static void
editing_done_cb (GtkCellEditable *editable, struct editing_data *ed)
{
    gboolean canceled;

    g_signal_handler_disconnect (editable, ed->sid);

    g_object_get (editable, "editing-canceled", &canceled, NULL);
    if (!canceled)
        apply_cb (ed);
    /* when there's a window, ed gets free-d in window_destroy_cb; here we need
     * to do it now (and no known there's no ed->arr) */
    g_free (ed);
}

static void
set_entry_icon (GtkEntry *entry)
{
    gtk_entry_set_icon_from_stock (entry, GTK_ENTRY_ICON_SECONDARY,
            GTK_STOCK_HELP);
    gtk_entry_set_icon_activatable (entry, GTK_ENTRY_ICON_SECONDARY,
            FALSE);
    gtk_entry_set_icon_tooltip_markup (entry, GTK_ENTRY_ICON_SECONDARY,
            "[&lt;ref&gt;[&lt;math...&gt;]] [YYYY-MM-DD] [HH:MM:SS]\n"
            "\n"
            "&lt;ref&gt; can be <b>c</b>, <b>a</b>, <b>m</b>, <b>v</b> or <b>n</b> "
            "for ctime, atime, mtime, current value or current time (now).\n"
            "&lt;math&gt; must be +/-, a number and a unit: Y, m, V, d, H, M or S\n"
            "Calculation will be done to &lt;ref&gt; to get the reference value.\n"
            "If not specified, defaults to 'v' (i.e. current value).\n"
            "\n"
            "Each component of the date/time can be a number/value to use, or a joker:\n"
            "-<tt> * </tt> to preserve element from the current value\n"
            "-<tt> # </tt> to use element from the reference value\n"
            "-<tt> ! </tt> to use element from 1970-01-01 00:00:00\n"
            "\n"
            "If the last character is a joker, it applies to all remaining elements.\n"
            "Else, default joker<tt> # </tt>is used.\n"
            "\n"
            "The last joker can be followed by a unit (Y, m, d, H, M, or S);\n"
            "Then<tt> # </tt>is used until that unit, then the joker is used "
            "for this element and all remaining ones."
            );
}

static void
editing_started_cb (GtkCellRenderer     *renderer,
                    GtkCellEditable     *editable,
                    gchar               *path,
                    struct editing_data *ed)
{
    g_signal_handler_disconnect (renderer, ed->sid);
    ed->entry = (GtkEntry *) editable;
    set_entry_icon (ed->entry);
    ed->sid = g_signal_connect (editable, "editing-done",
            (GCallback) editing_done_cb, ed);
}

static gboolean
ct_time_can_edit (DonnaColumnType    *ct,
                  gpointer            _data,
                  DonnaNode          *node,
                  GError            **error)
{
    struct tv_col_data *data = _data;
    const gchar *prop;

    if (data->which == WHICH_MTIME)
        prop = "mtime";
    else if (data->which == WHICH_ATIME)
        prop = "atime";
    else if (data->which == WHICH_CTIME)
        prop = "ctime";
    else
        prop = data->property;

    return DONNA_COLUMNTYPE_GET_INTERFACE (ct)->helper_can_edit (ct,
            prop, node, error);
}

static gboolean
ct_time_edit (DonnaColumnType    *ct,
              gpointer            data,
              DonnaNode          *node,
              GtkCellRenderer   **renderers,
              renderer_edit_fn    renderer_edit,
              gpointer            re_data,
              DonnaTreeView      *treeview,
              GError            **error)
{
    DonnaNodeHasValue has;
    GPtrArray *arr;
    struct editing_data *ed;
    GtkWindow *win;
    GtkWidget *w;
    GtkGrid *grid;
    GtkBox *box;
    PangoAttrList *attr_list;
    gint row;
    gchar *s;
    gchar *ss;

    if (!ct_time_can_edit (ct, data, node, error))
        return FALSE;

    /* get selected nodes (if any) */
    arr = donna_tree_view_get_selected_nodes (treeview, NULL);

    ed = g_new0 (struct editing_data, 1);
    ed->data = data;
    ed->app  = ((DonnaColumnTypeTime *) ct)->priv->app;
    ed->tree = treeview;
    ed->node = node;

    if (!arr || (arr->len == 1 && node == arr->pdata[0]))
    {
        if (arr)
            g_ptr_array_unref (arr);

        ed->sid = g_signal_connect (renderers[0],
                "editing-started",
                (GCallback) editing_started_cb, ed);

        g_object_set (renderers[0], "editable", TRUE, NULL);
        if (!renderer_edit (renderers[0], re_data))
        {
            g_signal_handler_disconnect (renderers[0], ed->sid);
            g_free (ed);
            g_set_error (error, DONNA_COLUMNTYPE_ERROR, DONNA_COLUMNTYPE_ERROR_OTHER,
                    "ColumnType 'time': Failed to put renderer in edit mode");
            return FALSE;
        }
        return TRUE;
    }

    win = donna_columntype_new_floating_window (treeview, !!arr);
    ed->window = w = (GtkWidget *) win;
    g_signal_connect_swapped (win, "destroy",
            (GCallback) window_destroy_cb, ed);

    w = gtk_grid_new ();
    grid = (GtkGrid *) w;
    g_object_set (w, "column-spacing", 12, NULL);
    gtk_container_add ((GtkContainer *) win, w);

    row = 0;
    ed->arr = arr;

    w = gtk_label_new (NULL);
    gtk_label_set_markup ((GtkLabel *) w, "<i>Apply to:</i>");
    gtk_grid_attach (grid, w, 0, row++, 4, 1);

    s = ss = donna_node_get_name (node);
    w = gtk_radio_button_new_with_label (NULL, s);
    gtk_widget_set_tooltip_text (w, "Clicked item");
    gtk_grid_attach (grid, w, 0, row, 4, 1);

    ++row;
    if (arr->len == 1)
        s = donna_node_get_name (arr->pdata[0]);
    else
        s = g_strdup_printf ("%d selected items", arr->len);
    w = gtk_radio_button_new_with_label_from_widget (
            (GtkRadioButton *) w, s);
    gtk_widget_set_tooltip_text (w, (arr->len == 1)
            ? "Selected item" : "Selected items");
    g_free (s);
    ed->rad_sel = (GtkToggleButton *) w;
    gtk_grid_attach (grid, w, 0, row, 4, 1);

    ++row;
    w = gtk_label_new (NULL);
    g_object_set (w, "margin-top", 4, NULL);
    gtk_label_set_markup ((GtkLabel *) w,
            "<b>c</b>time, <b>m</b>time, <b>a</b>time and current <b>v</b>alue relate to:");
    attr_list = pango_attr_list_new ();
    pango_attr_list_insert (attr_list,
            pango_attr_style_new (PANGO_STYLE_ITALIC));
    gtk_label_set_attributes ((GtkLabel *) w, attr_list);
    pango_attr_list_unref (attr_list);
    gtk_grid_attach (grid, w, 0, row, 4, 1);

    ++row;
    w = gtk_radio_button_new_with_label (NULL, ss);
    gtk_widget_set_tooltip_text (w, "Clicked item");
    gtk_grid_attach (grid, w, 0, row, 4, 1);

    ++row;
    w = gtk_radio_button_new_with_label_from_widget (
            (GtkRadioButton *) w, "Touched item");
    gtk_widget_set_tooltip_text (w, "The item on which the time is set");
    ed->rad_ref = (GtkToggleButton *) w;
    gtk_grid_attach (grid, w, 0, row, 4, 1);

    g_free (ss);
    g_object_set (w, "margin-bottom", 9, NULL);

    ++row;
    w = gtk_entry_new ();
    ed->entry = (GtkEntry *) w;
    set_entry_icon (ed->entry);
    g_signal_connect (w, "key-press-event", (GCallback) key_press_event_cb, ed);
    g_signal_connect_swapped (w, "activate", (GCallback) apply_cb, ed);
    gtk_grid_attach (grid, w, 0, row, 4, 1);

    ++row;
    w = gtk_box_new (GTK_ORIENTATION_HORIZONTAL, FALSE);
    box = (GtkBox *) w;
    g_object_set (w, "margin-top", 15, NULL);
    gtk_grid_attach (grid, w, 0, row, 4, 1);

    w = gtk_button_new_from_stock (GTK_STOCK_CANCEL);
    g_object_set (gtk_button_get_image ((GtkButton *) w),
            "icon-size", GTK_ICON_SIZE_MENU, NULL);
    g_signal_connect_swapped (w, "clicked",
            (GCallback) gtk_widget_destroy, win);
    gtk_box_pack_end (box, w, FALSE, FALSE, 3);
    w = gtk_button_new_with_label ("Set time");
    gtk_button_set_image ((GtkButton *) w,
            gtk_image_new_from_stock (GTK_STOCK_OK, GTK_ICON_SIZE_MENU));
    g_signal_connect_swapped (w, "clicked", (GCallback) apply_cb, ed);
    gtk_box_pack_end (box, w, FALSE, FALSE, 3);


    gtk_widget_show_all (ed->window);
    gtk_widget_grab_focus ((GtkWidget *) ed->entry);
    donna_app_set_floating_window (((DonnaColumnTypeTime *) ct)->priv->app, win);
    return TRUE;
}

static gboolean
ct_time_set_value (DonnaColumnType    *ct,
                   gpointer            _data,
                   GPtrArray          *nodes,
                   const gchar        *value,
                   DonnaNode          *node_ref,
                   DonnaTreeView      *treeview,
                   GError            **error)
{
    struct tv_col_data *data = _data;
    guint i;

    if (*value == '=')
    {
        if (!node_ref)
        {
            g_set_error (error, DONNA_COLUMNTYPE_ERROR,
                    DONNA_COLUMNTYPE_ERROR_INVALID_SYNTAX,
                    "ColumnType 'time': Prefix '=' to set_value given without reference");
            return FALSE;
        }
        else
            ++value;
    }
    else
        node_ref = NULL;

    return set_value (data, value, node_ref, nodes, treeview, error);
}

static GPtrArray *
ct_time_render (DonnaColumnType    *ct,
                gpointer            _data,
                guint               index,
                DonnaNode          *node,
                GtkCellRenderer    *renderer)
{
    struct tv_col_data *data = _data;
    DonnaNodeHasValue has;
    GValue value = G_VALUE_INIT;
    guint64 time;
    gchar *s;

    g_return_val_if_fail (DONNA_IS_COLUMNTYPE_TIME (ct), NULL);

    if (data->which == WHICH_MTIME)
        has = donna_node_get_mtime (node, FALSE, &time);
    else if (data->which == WHICH_ATIME)
        has = donna_node_get_atime (node, FALSE, &time);
    else if (data->which == WHICH_CTIME)
        has = donna_node_get_ctime (node, FALSE, &time);
    else
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
    else if (data->which == WHICH_OTHER)
    {
        if (G_VALUE_TYPE (&value) != G_TYPE_UINT64)
        {
            warn_not_uint64 (node);
            g_value_unset (&value);
            g_object_set (renderer, "visible", FALSE, NULL);
            return NULL;
        }
        time = g_value_get_uint64 (&value);
        g_value_unset (&value);
    }

    s = donna_print_time (time, data->format, &data->options);
    g_object_set (renderer, "visible", TRUE, "text", s, NULL);
    g_free (s);
    return NULL;
}

static gboolean
ct_time_set_tooltip (DonnaColumnType    *ct,
                     gpointer            _data,
                     guint               index,
                     DonnaNode          *node,
                     GtkTooltip         *tooltip)
{
    struct tv_col_data *data = _data;
    DonnaNodeHasValue has;
    GValue value = G_VALUE_INIT;
    guint64 time;
    gchar *s;

    g_return_val_if_fail (DONNA_IS_COLUMNTYPE_TIME (ct), NULL);

    if (data->format_tooltip[0] == '\0')
        return FALSE;

    if (data->which_tooltip == WHICH_MTIME)
        has = donna_node_get_mtime (node, FALSE, &time);
    else if (data->which_tooltip == WHICH_ATIME)
        has = donna_node_get_atime (node, FALSE, &time);
    else if (data->which_tooltip == WHICH_CTIME)
        has = donna_node_get_ctime (node, FALSE, &time);
    else
        donna_node_get (node, FALSE, data->property_tooltip, &has, &value, NULL);

    if (has != DONNA_NODE_VALUE_SET)
        return FALSE;
    if (data->which == WHICH_OTHER)
    {
        if (G_VALUE_TYPE (&value) != G_TYPE_UINT64)
        {
            warn_not_uint64 (node);
            g_value_unset (&value);
            return FALSE;
        }
        time = g_value_get_uint64 (&value);
        g_value_unset (&value);
    }

    s = donna_print_time (time, data->format_tooltip, &data->options_tooltip);
    gtk_tooltip_set_text (tooltip, s);
    g_free (s);
    return TRUE;
}

static gint
ct_time_node_cmp (DonnaColumnType    *ct,
                  gpointer            _data,
                  DonnaNode          *node1,
                  DonnaNode          *node2)
{
    struct tv_col_data *data = _data;
    DonnaNodeHasValue has1;
    DonnaNodeHasValue has2;
    guint64 time1;
    guint64 time2;

    if (data->which == WHICH_MTIME)
    {
        has1 = donna_node_get_mtime (node1, TRUE, &time1);
        has2 = donna_node_get_mtime (node2, TRUE, &time2);
    }
    else if (data->which == WHICH_ATIME)
    {
        has1 = donna_node_get_atime (node1, TRUE, &time1);
        has2 = donna_node_get_atime (node2, TRUE, &time2);
    }
    else if (data->which == WHICH_CTIME)
    {
        has1 = donna_node_get_ctime (node1, TRUE, &time1);
        has2 = donna_node_get_ctime (node2, TRUE, &time2);
    }
    else
    {
        GValue value = G_VALUE_INIT;

        donna_node_get (node1, TRUE, data->property, &has1, &value, NULL);
        if (has1 == DONNA_NODE_VALUE_SET)
        {
            if (G_VALUE_TYPE (&value) != G_TYPE_UINT64)
            {
                warn_not_uint64 (node1);
                has1 = DONNA_NODE_VALUE_ERROR;
            }
            else
                time1 = g_value_get_uint64 (&value);
            g_value_unset (&value);
        }
        donna_node_get (node2, TRUE, data->property, &has2, &value, NULL);
        if (has2 == DONNA_NODE_VALUE_SET)
        {
            if (G_VALUE_TYPE (&value) != G_TYPE_UINT64)
            {
                warn_not_uint64 (node2);
                has2 = DONNA_NODE_VALUE_ERROR;
            }
            else
                time2 = g_value_get_uint64 (&value);
            g_value_unset (&value);
        }
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

    return (time1 > time2) ? 1 : (time1 < time2) ? -1 : 0;
}

#define error_out(err_code, ...) do {       \
    g_set_error (error, DONNA_FILTER_ERROR, \
            err_code, __VA_ARGS__);         \
    g_free (fd);                            \
    *filter_data = NULL;                    \
    return FALSE;                           \
} while (0)

#define get_date_bit(variable, length)  do {                \
    variable = 0;                                           \
    for (i = 0; i < length; ++i)                            \
    {                                                       \
        if (*filter < '0' || *filter > '9')                 \
            error_out (DONNA_FILTER_ERROR_INVALID_SYNTAX,   \
                    "Invalid date format (must be YYYY-MM-DD[ HH:MM[:SS]]): %s",\
                    filter);                                \
        variable = variable * 10 + (*filter - '0');         \
        ++filter;                                           \
    }                                                       \
} while (0)

#define check(type, unit)   do {                \
    if (g_date_time_get_##type (dt)             \
            != g_date_time_get_##type (dt2))    \
        goto age_done;                          \
    if (fd->unit_age == UNIT_##unit)            \
    {                                           \
        ret = TRUE;                             \
        goto age_done;                          \
    }                                           \
} while (0)

static gboolean
ct_time_is_match_filter (DonnaColumnType    *ct,
                         const gchar        *filter,
                         gpointer           *filter_data,
                         gpointer            _data,
                         DonnaNode          *node,
                         GError            **error)
{
    struct tv_col_data *data = _data;
    struct filter_data *fd;
    DonnaNodeHasValue has;
    guint64 time;
    GDateTime *dt;
    guint64 r;

    if (G_UNLIKELY (!*filter_data))
    {
        gchar units[] = { UNIT_YEAR, UNIT_MONTH, UNIT_WEEK, UNIT_DAY, UNIT_HOUR,
            UNIT_MINUTE, UNIT_SECOND };
        guint nb_units = sizeof (units) / sizeof (units[0]);
        gchar units_extra[] = { UNIT_DATE, UNIT_DAY_OF_YEAR, UNIT_DAY_OF_WEEK,
            UNIT_DAY_OF_WEEK_2, UNIT_AGE };
        guint nb_units_extra = sizeof (units_extra) / sizeof (units_extra[0]);
        guint i;
        gchar *s;

        fd = *filter_data = g_new0 (struct filter_data, 1);
        fd->comp = COMP_EQUAL;

        while (isblank (*filter))
            ++filter;

        /* get unit */
        switch (*filter)
        {
            case UNIT_YEAR:
            case UNIT_MONTH:
            case UNIT_WEEK:
            case UNIT_DAY:
            case UNIT_HOUR:
            case UNIT_MINUTE:
            case UNIT_SECOND:
            case UNIT_DATE:
            case UNIT_DAY_OF_YEAR:
            case UNIT_DAY_OF_WEEK:
            case UNIT_DAY_OF_WEEK_2:
            case UNIT_AGE:
                fd->unit = *filter++;
        }
        if (fd->unit == 0)
            fd->unit = UNIT_DATE;

        while (isblank (*filter))
            ++filter;

        /* get comp */
        if (*filter == '<')
        {
            ++filter;
            if (*filter == '=')
            {
                ++filter;
                fd->comp = COMP_LESSER_EQUAL;
            }
            else
                fd->comp = COMP_LESSER;
        }
        else if (*filter == '>')
        {
            ++filter;
            if (*filter == '=')
            {
                ++filter;
                fd->comp = COMP_GREATER_EQUAL;
            }
            else
                fd->comp = COMP_GREATER;
        }
        else if (*filter == '=')
            ++filter;

        while (isblank (*filter))
            ++filter;

        /* get ref */
        if (fd->unit == UNIT_DATE)
        {
            gint year, month, day, hour, minute;
            gdouble seconds;
            gboolean again = FALSE;

get_date:
            /* must be formatted as such: YYYY-MM-DD[ HH:MM[:SS]] */
            get_date_bit (year, 4);
            if (*filter != '-')
                error_out (DONNA_FILTER_ERROR_INVALID_SYNTAX,
                        "Invalid date format (must be YYYY-MM-DD[ HH:MM[:SS]]): %s",
                        filter);
            get_date_bit (month, 2);
            if (*filter != '-')
                error_out (DONNA_FILTER_ERROR_INVALID_SYNTAX,
                        "Invalid date format (must be YYYY-MM-DD[ HH:MM[:SS]]): %s",
                        filter);
            get_date_bit (day, 2);

            while (isblank (*filter))
                ++filter;

            /* what we can have here is nothing, '-' before another date (RANGE)
             * or, ofc, a time */
            if (*filter >= '0' && *filter <= '9')
            {
                get_date_bit (hour, 2);
                if (*filter != ':')
                    error_out (DONNA_FILTER_ERROR_INVALID_SYNTAX,
                            "Invalid date format (must be YYYY-MM-DD[ HH:MM[:SS]]): %s",
                            filter);
                get_date_bit (minute, 2);
                if (*filter == ':')
                {
                    ++filter;
                    get_date_bit (seconds, 2);
                }
            }
            else
                hour = minute = seconds = 0;

            dt = g_date_time_new_local (year, month, day, hour, minute, seconds);
            if (again)
            {
                r = (guint64) g_date_time_to_unix (dt);
                if (r > fd->ref)
                    fd->ref2 = r;
                else
                {
                    fd->ref2 = fd->ref;
                    fd->ref  = r;
                }
            }
            else
                fd->ref = (guint64) g_date_time_to_unix (dt);
            g_date_time_unref (dt);

            if (!again && fd->comp == COMP_EQUAL && *filter == '-')
            {
                fd->comp = COMP_IN_RANGE;
                again = TRUE;
                ++filter;
                while (isblank (*filter))
                    ++filter;
                goto get_date;
            }

            goto compile_done;
        }

        /* just a number */
        fd->ref = g_ascii_strtoull (filter, &s, 10);
        filter = (const gchar *) s;

        while (isblank (*filter))
            ++filter;

        /* AGE requires another unit (unit_age) */
        if (fd->unit == UNIT_AGE)
        {
            switch (*filter)
            {
                case UNIT_YEAR:
                case UNIT_MONTH:
                case UNIT_WEEK:
                case UNIT_DAY:
                case UNIT_HOUR:
                case UNIT_MINUTE:
                case UNIT_SECOND:
                    fd->unit_age = *filter++;
            }
            if (fd->unit_age == 0)
                fd->unit_age = UNIT_DAY;
        }
        else if (fd->comp == COMP_EQUAL && *filter == '-')
        {
            fd->comp = COMP_IN_RANGE;
            ++filter;
            r = g_ascii_strtoull (filter, &s, 10);
            filter = (const gchar *) s;

            /* we keep them in the specified order for day of week-s, so we
             * support e.g. 6-1 to say from Sat to Mon (Sat, Sun, Mon) */
            if (fd->unit == UNIT_DAY_OF_WEEK || fd->unit == UNIT_DAY_OF_WEEK_2
                    || r > fd->ref)
                fd->ref2 = r;
            else
            {
                fd->ref2 = fd->ref;
                fd->ref  = r;
            }
        }
    }
    else
        fd = *filter_data;

compile_done:

    if (data->which == WHICH_MTIME)
        has = donna_node_get_mtime (node, TRUE, &time);
    else if (data->which == WHICH_ATIME)
        has = donna_node_get_atime (node, TRUE, &time);
    else if (data->which == WHICH_CTIME)
        has = donna_node_get_ctime (node, TRUE, &time);
    else
    {
        GValue value = G_VALUE_INIT;

        donna_node_get (node, TRUE, data->property, &has, &value, NULL);
        if (has == DONNA_NODE_VALUE_SET)
        {
            if (G_VALUE_TYPE (&value) != G_TYPE_UINT64)
            {
                warn_not_uint64 (node);
                has = DONNA_NODE_VALUE_ERROR;
            }
            else
                time = g_value_get_uint64 (&value);
            g_value_unset (&value);
        }
    }

    if (has != DONNA_NODE_VALUE_SET)
        return FALSE;

    switch (fd->unit)
    {
        case UNIT_YEAR:
            dt = g_date_time_new_from_unix_local (time);
            r = g_date_time_get_year (dt);
            g_date_time_unref (dt);
            break;
        case UNIT_MONTH:
            dt = g_date_time_new_from_unix_local (time);
            r = g_date_time_get_month (dt);
            g_date_time_unref (dt);
            break;
        case UNIT_WEEK:
            dt = g_date_time_new_from_unix_local (time);
            r = g_date_time_get_week_of_year (dt);
            g_date_time_unref (dt);
            break;
        case UNIT_DAY:
            dt = g_date_time_new_from_unix_local (time);
            r = g_date_time_get_day_of_month (dt);
            g_date_time_unref (dt);
            break;
        case UNIT_HOUR:
            dt = g_date_time_new_from_unix_local (time);
            r = g_date_time_get_hour (dt);
            g_date_time_unref (dt);
            break;
        case UNIT_MINUTE:
            dt = g_date_time_new_from_unix_local (time);
            r = g_date_time_get_minute (dt);
            g_date_time_unref (dt);
            break;
        case UNIT_SECOND:
            dt = g_date_time_new_from_unix_local (time);
            r = g_date_time_get_second (dt);
            g_date_time_unref (dt);
            break;
        case UNIT_DATE:
            r = time;
            break;
        case UNIT_DAY_OF_YEAR:
            dt = g_date_time_new_from_unix_local (time);
            r = g_date_time_get_day_of_year (dt);
            g_date_time_unref (dt);
            break;
        case UNIT_DAY_OF_WEEK:
            dt = g_date_time_new_from_unix_local (time);
            r = g_date_time_get_day_of_week (dt);
            g_date_time_unref (dt);
            break;
        case UNIT_DAY_OF_WEEK_2:
            {
                gchar *s;

                dt = g_date_time_new_from_unix_local (time);
                s = g_date_time_format (dt, "%w");
                g_date_time_unref (dt);
                r = g_ascii_strtoull (s, NULL, 10);
                g_free (s);
                break;
            }
        case UNIT_AGE:
            {
                GDateTime *dt2;

                dt = g_date_time_new_now_local ();
                if (fd->ref == 0)
                    dt2 = g_date_time_ref (dt);
                else
                    switch (fd->unit_age)
                    {
                        case UNIT_YEAR:
                            dt2 = g_date_time_add_years (dt, -1 * fd->ref);
                            break;
                        case UNIT_MONTH:
                            dt2 = g_date_time_add_months (dt, -1 * fd->ref);
                            break;
                        case UNIT_WEEK:
                            dt2 = g_date_time_add_weeks (dt, -1 * fd->ref);
                            break;
                        case UNIT_DAY:
                            dt2 = g_date_time_add_days (dt, -1 * fd->ref);
                            break;
                        case UNIT_HOUR:
                            dt2 = g_date_time_add_hours (dt, -1 * fd->ref);
                            break;
                        case UNIT_MINUTE:
                            dt2 = g_date_time_add_minutes (dt, -1 * fd->ref);
                            break;
                        case UNIT_SECOND:
                            dt2 = g_date_time_add_seconds (dt, -1 * fd->ref);
                            break;
                        case UNIT_DATE:
                        case UNIT_DAY_OF_YEAR:
                        case UNIT_DAY_OF_WEEK:
                        case UNIT_DAY_OF_WEEK_2:
                        case UNIT_AGE:
                            /* silence warnings */
                            g_return_val_if_reached (FALSE);
                    }
                r = g_date_time_to_unix (dt2);
                g_date_time_unref (dt);
                if (fd->comp != COMP_EQUAL)
                    g_date_time_unref (dt2);

                /* comparisons are reversed: age <= 5m == its time >= t-5m */
                switch (fd->comp)
                {
                    case COMP_LESSER_EQUAL:
                        return time >= r;

                    case COMP_LESSER:
                        return time > r;

                    case COMP_EQUAL:
                        {
                            gboolean ret = FALSE;

                            /* special case:
                             * age = 0d == today
                             * age = 2V == 2 weeks ago, i.e. during that week
                             * etc */
                            dt = g_date_time_new_from_unix_local (time);

                            if (fd->unit_age == UNIT_WEEK)
                            {
                                /* week is a special case, as one week can
                                 * spread over two months, even two years. This
                                 * is why here we use week_numbering_year
                                 * instead of year, and do not check month. */
                                check (week_numbering_year, YEAR);
                                check (week_of_year,        WEEK);
                            }
                            else
                            {
                                check (year,         YEAR);
                                check (month,        MONTH);
                                check (day_of_month, DAY);
                                check (hour,         HOUR);
                                check (minute,       MINUTE);
                                check (second,       SECOND);
                            }
age_done:
                            g_date_time_unref (dt);
                            g_date_time_unref (dt2);
                            return ret;
                        }

                    case COMP_GREATER:
                        return time < r;

                    case COMP_GREATER_EQUAL:
                        return time <= r;

                    case COMP_IN_RANGE:
                        /* silence warning */
                        break;
                }
                g_return_val_if_reached (FALSE);
            }
    }

    switch (fd->comp)
    {
        case COMP_LESSER_EQUAL:
            return r <= fd->ref;

        case COMP_LESSER:
            return r < fd->ref;

        case COMP_EQUAL:
            return r == fd->ref;

        case COMP_GREATER:
            return r > fd->ref;

        case COMP_GREATER_EQUAL:
            return r >= fd->ref;

        case COMP_IN_RANGE:
            /* spacial handling for DAY_OF_WEEK cases */
            if (fd->ref > fd->ref2)
                return r >= fd->ref || r <= fd->ref2;
            else
                return r >= fd->ref && r <= fd->ref2;
    }

    g_return_val_if_reached (FALSE);
}

static void
ct_time_free_filter_data (DonnaColumnType    *ct,
                          gpointer            filter_data)
{
    g_free (filter_data);
}

static DonnaColumnTypeNeed
ct_time_set_option (DonnaColumnType    *ct,
                    const gchar        *tv_name,
                    const gchar        *col_name,
                    const gchar        *arr_name,
                    gpointer            _data,
                    const gchar        *option,
                    const gchar        *value,
                    DonnaColumnOptionSaveLocation save_location,
                    GError            **error)
{
    struct tv_col_data *data = _data;

    if (streq (option, "property"))
    {
        if (!DONNA_COLUMNTYPE_GET_INTERFACE (ct)->helper_set_option (ct,
                    tv_name, col_name, arr_name, "columntypes/time", save_location,
                    option, G_TYPE_STRING, &data->property, &value, error))
            return DONNA_COLUMNTYPE_NEED_NOTHING;

        g_free (data->property);
        data->property = g_strdup (value);

        if (streq (value, "mtime"))
            data->which = WHICH_MTIME;
        else if (streq (value, "atime"))
            data->which = WHICH_ATIME;
        else if (streq (value, "ctime"))
            data->which = WHICH_CTIME;
        else
            data->which = WHICH_OTHER;

        return DONNA_COLUMNTYPE_NEED_RESORT | DONNA_COLUMNTYPE_NEED_REDRAW;
    }
    else if (streq (option, "format"))
    {
        if (!DONNA_COLUMNTYPE_GET_INTERFACE (ct)->helper_set_option (ct,
                    tv_name, col_name, arr_name, "size", save_location,
                    option, G_TYPE_STRING, &data->format, &value, error))
            return DONNA_COLUMNTYPE_NEED_NOTHING;

        g_free (data->format);
        data->format = g_strdup (value);
        return DONNA_COLUMNTYPE_NEED_REDRAW;
    }
    else if (streq (option, "age_span_seconds"))
    {
        gint c, v;

        c = data->options.age_span_seconds;
        v = g_ascii_strtoull (value, NULL, 10);
        if (!DONNA_COLUMNTYPE_GET_INTERFACE (ct)->helper_set_option (ct,
                    tv_name, col_name, arr_name, "size", save_location,
                    option, G_TYPE_INT, &c, &v, error))
            return DONNA_COLUMNTYPE_NEED_NOTHING;

        data->options.age_span_seconds = (guint) v;
        return DONNA_COLUMNTYPE_NEED_REDRAW;
    }
    else if (streq (option, "age_fallback_format"))
    {
        gchar *c;

        c = (gchar *) data->options.age_fallback_format;
        if (!DONNA_COLUMNTYPE_GET_INTERFACE (ct)->helper_set_option (ct,
                    tv_name, col_name, arr_name, "size", save_location,
                    option, G_TYPE_STRING, &c, &value, error))
            return DONNA_COLUMNTYPE_NEED_NOTHING;

        g_free (c);
        data->options.age_fallback_format = g_strdup (value);
        return DONNA_COLUMNTYPE_NEED_REDRAW;
    }
    else if (streq (option, "property_tooltip"))
    {
        if (!DONNA_COLUMNTYPE_GET_INTERFACE (ct)->helper_set_option (ct,
                    tv_name, col_name, arr_name, "columntypes/time", save_location,
                    option, G_TYPE_STRING, &data->property_tooltip, &value, error))
            return DONNA_COLUMNTYPE_NEED_NOTHING;

        g_free (data->property_tooltip);
        data->property_tooltip = g_strdup (value);

        if (streq (value, "mtime"))
            data->which_tooltip = WHICH_MTIME;
        else if (streq (value, "atime"))
            data->which_tooltip = WHICH_ATIME;
        else if (streq (value, "ctime"))
            data->which_tooltip = WHICH_CTIME;
        else
            data->which_tooltip = WHICH_OTHER;

        return DONNA_COLUMNTYPE_NEED_NOTHING;
    }
    else if (streq (option, "format_tooltip"))
    {
        if (!DONNA_COLUMNTYPE_GET_INTERFACE (ct)->helper_set_option (ct,
                    tv_name, col_name, arr_name, "size", save_location,
                    option, G_TYPE_STRING, &data->format_tooltip, &value, error))
            return DONNA_COLUMNTYPE_NEED_NOTHING;

        g_free (data->format_tooltip);
        data->format_tooltip = g_strdup (value);
        return DONNA_COLUMNTYPE_NEED_NOTHING;
    }
    else if (streq (option, "age_span_seconds_tooltip"))
    {
        gint c, v;

        c = data->options_tooltip.age_span_seconds;
        v = g_ascii_strtoull (value, NULL, 10);
        if (!DONNA_COLUMNTYPE_GET_INTERFACE (ct)->helper_set_option (ct,
                    tv_name, col_name, arr_name, NULL, save_location,
                    option, G_TYPE_INT, &c, &v, error))
            return DONNA_COLUMNTYPE_NEED_NOTHING;

        data->options_tooltip.age_span_seconds = (guint) v;
        return DONNA_COLUMNTYPE_NEED_NOTHING;
    }
    else if (streq (option, "age_fallback_format_tooltip"))
    {
        gchar *c;

        c = (gchar *) data->options_tooltip.age_fallback_format;
        if (!DONNA_COLUMNTYPE_GET_INTERFACE (ct)->helper_set_option (ct,
                    tv_name, col_name, arr_name, NULL, save_location,
                    option, G_TYPE_STRING, &c, &value, error))
            return DONNA_COLUMNTYPE_NEED_NOTHING;

        g_free (c);
        data->options_tooltip.age_fallback_format = g_strdup (value);
        return DONNA_COLUMNTYPE_NEED_NOTHING;
    }

    g_set_error (error, DONNA_COLUMNTYPE_ERROR,
            DONNA_COLUMNTYPE_ERROR_OTHER,
            "ColumnType 'time': Unknown option '%s'",
            option);
    return DONNA_COLUMNTYPE_NEED_NOTHING;
}

static gchar *
ct_time_get_context_alias (DonnaColumnType   *ct,
                           gpointer           _data,
                           const gchar       *alias,
                           const gchar       *extra,
                           DonnaContextReference reference,
                           DonnaNode         *node_ref,
                           get_sel_fn         get_sel,
                           gpointer           get_sel_data,
                           const gchar       *prefix,
                           GError           **error)
{
    const gchar *save_location;

    if (!streq (alias, "options"))
    {
        g_set_error (error, DONNA_CONTEXT_MENU_ERROR,
                DONNA_CONTEXT_MENU_ERROR_UNKNOWN_ALIAS,
                "ColumnType 'time': Unknown alias '%s'",
                alias);
        return NULL;
    }

    save_location = DONNA_COLUMNTYPE_GET_INTERFACE (ct)->
        helper_get_save_location (ct, &extra, TRUE, error);
    if (!save_location)
        return NULL;

    if (extra)
    {
        g_set_error (error, DONNA_CONTEXT_MENU_ERROR,
                DONNA_CONTEXT_MENU_ERROR_OTHER,
                "ColumnType 'time': Invalid extra '%s' for alias '%s'",
                extra, alias);
        return NULL;
    }

    return g_strconcat (
            prefix, "format:@", save_location, "<",
                prefix, "format:@", save_location, ":%O,",
                prefix, "format:@", save_location, ":%x %X,",
                prefix, "format:@", save_location, ":%x,",
                prefix, "format:@", save_location, ":%X,",
                prefix, "format:@", save_location, ":%F %T,",
                prefix, "format:@", save_location, ":%F,",
                prefix, "format:@", save_location, ":%T,",
                prefix, "format:@", save_location, ":%d/%m/%Y %T,",
                prefix, "format:@", save_location, ":%d/%m/%Y,-,",
                prefix, "format:@", save_location, ":=>,",
            prefix, "agesec:@", save_location, "<",
                prefix, "agesec:@", save_location, ":1h,",
                prefix, "agesec:@", save_location, ":24h,",
                prefix, "agesec:@", save_location, ":48h,",
                prefix, "agesec:@", save_location, ":1w,-,",
                prefix, "agesec:@", save_location, ":=>,",
            prefix, "format:@", save_location, ":age<",
                prefix, "format:@", save_location, ":age:%o,",
                prefix, "format:@", save_location, ":age:%x %X,",
                prefix, "format:@", save_location, ":age:%x,",
                prefix, "format:@", save_location, ":age:%X,",
                prefix, "format:@", save_location, ":age:%F %T,",
                prefix, "format:@", save_location, ":age:%F,",
                prefix, "format:@", save_location, ":age:%T,",
                prefix, "format:@", save_location, ":age:%d/%m/%Y %T,",
                prefix, "format:@", save_location, ":age:%d/%m/%Y,-,",
                prefix, "format:@", save_location, ":age:=>,",
            prefix, "prop:@", save_location, "<",
                prefix, "prop:@", save_location, ":mtime,",
                prefix, "prop:@", save_location, ":atime,",
                prefix, "prop:@", save_location, ":ctime,-,",
                prefix, "prop:@", save_location, ":custom>,-,",
            prefix, "format:@", save_location, ":tt<",
                prefix, "format:@", save_location, ":tt:%O,",
                prefix, "format:@", save_location, ":tt:%x %X,",
                prefix, "format:@", save_location, ":tt:%x,",
                prefix, "format:@", save_location, ":tt:%X,",
                prefix, "format:@", save_location, ":tt:%F %T,",
                prefix, "format:@", save_location, ":tt:%F,",
                prefix, "format:@", save_location, ":tt:%T,",
                prefix, "format:@", save_location, ":tt:%d/%m/%Y %T,",
                prefix, "format:@", save_location, ":tt:%d/%m/%Y,-,",
                prefix, "format:@", save_location, ":tt:=>,",
            prefix, "agesec:@", save_location, ":tt<",
                prefix, "agesec:@", save_location, ":tt:1h,",
                prefix, "agesec:@", save_location, ":tt:24h,",
                prefix, "agesec:@", save_location, ":tt:48h,",
                prefix, "agesec:@", save_location, ":tt:1w,-,",
                prefix, "agesec:@", save_location, ":tt:=>,",
            prefix, "format:@", save_location, ":agett<",
                prefix, "format:@", save_location, ":agett:%o,",
                prefix, "format:@", save_location, ":agett:%x %X,",
                prefix, "format:@", save_location, ":agett:%x,",
                prefix, "format:@", save_location, ":agett:%X,",
                prefix, "format:@", save_location, ":agett:%F %T,",
                prefix, "format:@", save_location, ":agett:%F,",
                prefix, "format:@", save_location, ":agett:%T,",
                prefix, "format:@", save_location, ":agett:%d/%m/%Y %T,",
                prefix, "format:@", save_location, ":agett:%d/%m/%Y,-,",
                prefix, "format:@", save_location, ":agett:=>,",
            prefix, "prop:@", save_location, ":tt<",
                prefix, "prop:@", save_location, ":tt:mtime,",
                prefix, "prop:@", save_location, ":tt:atime,",
                prefix, "prop:@", save_location, ":tt:ctime,-,",
                prefix, "prop:@", save_location, ":tt:custom>",
            NULL);
}

static gboolean
ct_time_get_context_item_info (DonnaColumnType   *ct,
                               gpointer           _data,
                               const gchar       *item,
                               const gchar       *extra,
                               DonnaContextReference reference,
                               DonnaNode         *node_ref,
                               get_sel_fn         get_sel,
                               gpointer           get_sel_data,
                               DonnaContextInfo  *info,
                               GError           **error)
{
    struct tv_col_data *data = _data;
    gchar buf[10];
    const gchar *option = NULL;
    const gchar *value;
    const gchar *ask_title;
    const gchar *ask_current;
    const gchar *save_location;
    gboolean quote_value = FALSE;
    gboolean is_tt = FALSE;
    guint which;
    gchar *prop;
    gchar *fmt;

    save_location = DONNA_COLUMNTYPE_GET_INTERFACE (ct)->
        helper_get_save_location (ct, &extra, FALSE, error);
    if (!save_location)
        return FALSE;

    if (streqn (extra, "tt", 2))
    {
        if (extra[2] == '\0')
        {
            is_tt = TRUE;
            extra = NULL;
        }
        else if (extra[2] == ':')
        {
            is_tt = TRUE;
            extra += 3;
        }

        which = data->which_tooltip;
        prop  = data->property_tooltip;
        fmt   = data->format_tooltip;
    }
    else
    {
        which = data->which;
        prop  = data->property;
        fmt   = data->format;
    }

    if (streq (item, "prop"))
    {
        info->is_visible = TRUE;
        info->is_sensitive = TRUE;
        option = (is_tt) ? "property_tooltip" : "property";

        if (!extra)
        {
            if (is_tt)
                info->name = "Tooltip: Node Property";
            else
                info->name = "Node Property";
            info->submenus = 1;
            option = NULL;
        }
        else if (streq (extra, "mtime"))
        {
            info->name = "Modified Time";
            info->desc = "mtime";
            info->icon_special = DONNA_CONTEXT_ICON_IS_RADIO;
            info->is_active = which == WHICH_MTIME;
            value = "mtime";
        }
        else if (streq (extra, "atime"))
        {
            info->name = "Accessed Time";
            info->desc = "atime";
            info->icon_special = DONNA_CONTEXT_ICON_IS_RADIO;
            info->is_active = which == WHICH_ATIME;
            value = "atime";
        }
        else if (streq (extra, "ctime"))
        {
            info->name = "Status Change Time";
            info->desc = "ctime";
            info->icon_special = DONNA_CONTEXT_ICON_IS_RADIO;
            info->is_active = which == WHICH_CTIME;
            value = "ctime";
        }
        else if (streq (extra, "custom"))
        {
            if (which == WHICH_MTIME)
                info->name = "Custom: mtime";
            else if (which == WHICH_ATIME)
                info->name = "Custom: atime";
            else if (which == WHICH_CTIME)
                info->name = "Custom: ctime";
            else /* WHICH_OTHER */
            {
                info->name = g_strconcat ("Custom: ", prop, NULL);
                info->free_name = TRUE;
            }
            info->icon_special = DONNA_CONTEXT_ICON_IS_RADIO;
            value = NULL;
            ask_title = "Enter the name of the property";
            ask_current = prop;
        }
        else
        {
            g_set_error (error, DONNA_CONTEXT_MENU_ERROR,
                    DONNA_CONTEXT_MENU_ERROR_OTHER,
                    "ColumnType 'time': Invalid extra '%s' for item '%s'",
                    extra, item);
            return FALSE;
        }
    }
    else if (streq (item, "format"))
    {
        gchar *title;
        gchar *s;
        guint64 now = time (NULL);

        if (is_tt)
        {
            title = "Tooltip: ";
            option = "format_tooltip";
            ask_title = "Enter the format for the tooltip";
        }
        else if (streqn (extra, "agett", 5))
        {
            if (extra[5] == '\0')
            {
                is_tt = TRUE;
                extra = NULL;
            }
            else if (extra[5] == ':')
            {
                is_tt = TRUE;
                extra += 6;
            }

            title = "Tooltip Fallback: ";
            option = "age_fallback_format_tooltip";
            ask_title = "Enter the fallback format for the tooltip";
            fmt = (gchar *) data->options_tooltip.age_fallback_format;
        }
        else if (streqn (extra, "age", 3))
        {
            if (extra[3] == '\0')
                extra = NULL;
            else if (extra[3] == ':')
                extra += 4;

            title = "Fallback: ";
            option = "age_fallback_format";
            ask_title = "Enter the fallback format for the column";
            fmt = (gchar *) data->options.age_fallback_format;
        }
        else
        {
            title = "Column: ";
            option = "format";
            ask_title = "Enter the format for the column";
        }

        info->is_visible = TRUE;
        info->is_sensitive = TRUE;
        if (!extra)
        {
            s = donna_print_time (now, fmt,
                    (is_tt) ? &data->options_tooltip : &data->options);
            info->name = g_strconcat (title, s, NULL);
            info->free_name = TRUE;
            info->desc = g_strconcat ("Format: ", fmt, NULL);
            info->free_desc = TRUE;
            value = NULL;
            ask_current = fmt;
            g_free (s);
        }
        else if (*extra == '=')
        {
            if (extra[1] == '\0')
                info->name = "Custom...";
            else
            {
                info->name = g_strdup (extra + 1);
                info->free_name = TRUE;
            }
            info->desc = g_strconcat ("Current format: ", fmt, NULL);
            info->free_desc = TRUE;
            value = NULL;
            ask_current = fmt;
        }
        else
        {
            if (*extra == ':')
                ++extra;
            info->icon_special = DONNA_CONTEXT_ICON_IS_RADIO;
            info->is_active = streq (extra, fmt);
            s = donna_print_time (now, extra,
                    (is_tt) ? &data->options_tooltip : &data->options);
            info->name = s;
            info->free_name = TRUE;
            info->desc = g_strconcat ("Format: ", extra, NULL);
            info->free_desc = TRUE;
            value = extra;
            quote_value = TRUE;
        }
    }
    else if (streq (item, "agesec"))
    {
        info->is_visible = TRUE;
        info->is_sensitive = TRUE;
        if (!extra)
        {
            if (is_tt)
            {
                info->name = g_strdup_printf ("Tooltip: Age fallback after: %d seconds",
                        data->options_tooltip.age_span_seconds);
                option = "age_span_seconds_tooltip";
                snprintf (buf, 10, "%d", data->options_tooltip.age_span_seconds);
                ask_title = "Age fallback after how many seconds ? (toolip)";
            }
            else
            {
                info->name = g_strdup_printf ("Age fallback after: %d seconds",
                        data->options.age_span_seconds);
                option = "age_span_seconds";
                snprintf (buf, 10, "%d", data->options.age_span_seconds);
                ask_title = "Age fallback after how many seconds ?";
            }
            info->free_name = TRUE;
            value = buf;
        }
        else if (*extra == '=')
        {
            ++extra;
            if (*extra == '\0')
                info->name = "Custom...";
            else
            {
                info->name = g_strdup (extra);
                info->free_name = TRUE;
            }

            if (is_tt)
            {
                option = "age_span_seconds_tooltip";
                snprintf (buf, 10, "%d", data->options_tooltip.age_span_seconds);
                ask_title = "Age fallback after how many seconds ? (toolip)";
            }
            else
            {
                option = "age_span_seconds";
                snprintf (buf, 10, "%d", data->options.age_span_seconds);
                ask_title = "Age fallback after how many seconds ?";
            }
        }
        else if (streq (extra, "1h"))
        {
            info->icon_special = DONNA_CONTEXT_ICON_IS_RADIO;
            if (is_tt)
            {
                info->is_active = data->options_tooltip.age_span_seconds == 3600;
                option = "age_span_seconds_tooltip";
            }
            else
            {
                info->is_active = data->options.age_span_seconds == 3600;
                option = "age_span_seconds";
            }
            info->name = g_strdup (extra);
            info->free_name = TRUE;
            value = "3600";
        }
        else if (streq (extra, "24h"))
        {
            info->icon_special = DONNA_CONTEXT_ICON_IS_RADIO;
            if (is_tt)
            {
                info->is_active = data->options_tooltip.age_span_seconds == 86400;
                option = "age_span_seconds_tooltip";
            }
            else
            {
                info->is_active = data->options.age_span_seconds == 86400;
                option = "age_span_seconds";
            }
            info->name = g_strdup (extra);
            info->free_name = TRUE;
            value = "86400";
        }
        else if (streq (extra, "48h"))
        {
            info->icon_special = DONNA_CONTEXT_ICON_IS_RADIO;
            if (is_tt)
            {
                info->is_active = data->options_tooltip.age_span_seconds == 172800;
                option = "age_span_seconds_tooltip";
            }
            else
            {
                info->is_active = data->options.age_span_seconds == 172800;
                option = "age_span_seconds";
            }
            info->name = g_strdup (extra);
            info->free_name = TRUE;
            value = "172800";
        }
        else if (streq (extra, "1w"))
        {
            info->icon_special = DONNA_CONTEXT_ICON_IS_RADIO;
            if (is_tt)
            {
                info->is_active = data->options_tooltip.age_span_seconds == 604800;
                option = "age_span_seconds_tooltip";
            }
            else
            {
                info->is_active = data->options.age_span_seconds == 604800;
                option = "age_span_seconds";
            }
            info->name = g_strdup (extra);
            info->free_name = TRUE;
            value = "604800";
        }
        else
        {
            g_set_error (error, DONNA_CONTEXT_MENU_ERROR,
                    DONNA_CONTEXT_MENU_ERROR_OTHER,
                    "ColumnType 'time': Invalid extra '%s' for item '%s'",
                    extra, item);
            return FALSE;
        }
    }
    else
    {
        g_set_error (error, DONNA_CONTEXT_MENU_ERROR,
                DONNA_CONTEXT_MENU_ERROR_UNKNOWN_ITEM,
                "ColumnType 'time': Unknown item '%s'",
                item);
        return FALSE;
    }

    if (option)
    {
        GString *str = g_string_new ("command:tree_column_set_option (%o,%R,");
        g_string_append (str, option);
        g_string_append_c (str, ',');
        if (quote_value)
            donna_g_string_append_quoted (str, value, TRUE);
        else if (value)
            g_string_append (str, value);
        else
        {
            g_string_append (str, "@ask_text(");
            g_string_append (str, ask_title);
            g_string_append_c (str, ',');
            g_string_append_c (str, ',');
            donna_g_string_append_quoted (str, ask_current, TRUE);
            g_string_append_c (str, ')');
        }
        if (*save_location != '\0')
        {
            g_string_append_c (str, ',');
            g_string_append (str, save_location);
        }
        g_string_append_c (str, ')');
        info->trigger = g_string_free (str, FALSE);
        info->free_trigger = TRUE;
    }

    return TRUE;
}
