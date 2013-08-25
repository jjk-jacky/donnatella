
#include <string.h>
#include <ctype.h>      /* isblank() */
#include "filter.h"
#include "app.h"
#include "columntype.h"
#include "macros.h"

enum
{
    PROP_0,

    PROP_APP,
    PROP_FILTER,

    NB_PROPS
};

enum cond
{
    COND_AND    = 0,
    COND_OR,
};

struct element
{
    /* condition for the element */
    guint8           cond       : 1;
    /* was it prefixed with NOT */
    guint8           is_not     : 1;
    /* is it a block, or a (group of blocks) */
    guint8           is_block   : 1;
    /* pointer to the block, or the first element in the group */
    gpointer         data;
    /* pointer to the next element */
    struct element  *next;
};

struct block
{
    /* name of column */
    gchar           *col_name;
    /* columntype to use */
    DonnaColumnType *ct;
    /* filter */
    gchar           *filter;
    /* "compiled" data */
    gpointer         data;
};

struct _DonnaFilterPrivate
{
    gchar           *filter;
    DonnaApp        *app;
    gulong           option_set_sid;
    gulong           option_deleted_sid;
    struct element  *element;
};

static GParamSpec *donna_filter_props[NB_PROPS] = { NULL, };

static void     donna_filter_set_property       (GObject            *object,
                                                 guint               prop_id,
                                                 const GValue       *value,
                                                 GParamSpec         *pspec);
static void     donna_filter_get_property       (GObject            *object,
                                                 guint               prop_id,
                                                 GValue             *value,
                                                 GParamSpec         *pspec);
static void     donna_filter_finalize           (GObject            *object);

G_DEFINE_TYPE (DonnaFilter, donna_filter, G_TYPE_OBJECT)

static void
donna_filter_class_init (DonnaFilterClass *klass)
{
    GObjectClass *o_class;

    o_class = G_OBJECT_CLASS (klass);
    o_class->set_property   = donna_filter_set_property;
    o_class->get_property   = donna_filter_get_property;
    o_class->finalize       = donna_filter_finalize;

    donna_filter_props[PROP_APP] =
        g_param_spec_object ("app", "app", "The DonnaApp object",
                DONNA_TYPE_APP,
                G_PARAM_READWRITE | G_PARAM_CONSTRUCT_ONLY);

    donna_filter_props[PROP_FILTER] =
        g_param_spec_string ("filter", "filter", "Filter string",
                NULL,   /* default */
                G_PARAM_READWRITE | G_PARAM_CONSTRUCT_ONLY);

    g_object_class_install_properties (o_class, NB_PROPS, donna_filter_props);

    g_type_class_add_private (klass, sizeof (DonnaFilterPrivate));
}

static void
donna_filter_init (DonnaFilter *filter)
{
    DonnaFilterPrivate *priv;

    priv = filter->priv = G_TYPE_INSTANCE_GET_PRIVATE (filter,
            DONNA_TYPE_FILTER, DonnaFilterPrivate);
}

static void
donna_filter_set_property (GObject            *object,
                           guint               prop_id,
                           const GValue       *value,
                           GParamSpec         *pspec)
{
    DonnaFilterPrivate *priv = ((DonnaFilter *) object)->priv;
    DonnaApp *app;

    switch (prop_id)
    {
        case PROP_APP:
            app = g_value_get_object (value);
            g_return_if_fail (DONNA_IS_APP (app));
            priv->app = g_object_ref (app);
            break;
        case PROP_FILTER:
            priv->filter = g_value_dup_string (value);
            break;
        default:
            G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
            break;
    }
}

static void
donna_filter_get_property (GObject            *object,
                           guint               prop_id,
                           GValue             *value,
                           GParamSpec         *pspec)
{
    DonnaFilterPrivate *priv = ((DonnaFilter *) object)->priv;

    switch (prop_id)
    {
        case PROP_APP:
            g_value_set_object (value, priv->app);
            break;
        case PROP_FILTER:
            g_value_set_string (value, priv->filter);
            break;
        default:
            G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
            break;
    }
}

static void
free_block (struct block *block)
{
    if (block->data)
        donna_columntype_free_filter_data (block->ct, block->data);
    g_free (block->filter);
    g_free (block->col_name);
    if (block->ct)
        g_object_unref (block->ct);
    g_slice_free (struct block, block);
}

static void
free_element (struct element *element)
{
    if (!element)
        return;
    for (;;)
    {
        struct element *next;

        next = element->next;
        if (element->data)
        {
            if (element->is_block)
                free_block (element->data);
            else
                free_element (element->data);
        }
        g_slice_free (struct element, element);
        if (!next)
            break;
    }
}

static void
donna_filter_finalize (GObject *object)
{
    DonnaFilterPrivate *priv;
    DonnaConfig *config;

    priv = DONNA_FILTER (object)->priv;
    config = donna_app_peek_config (priv->app);
    if (priv->option_set_sid > 0)
        g_signal_handler_disconnect (config, priv->option_set_sid);
    if (priv->option_deleted_sid > 0)
        g_signal_handler_disconnect (config, priv->option_deleted_sid);
    g_object_unref (priv->app);
    g_free (priv->filter);
    free_element (priv->element);

    G_OBJECT_CLASS (donna_filter_parent_class)->finalize (object);
}

static gboolean
element_need_recompile (struct element *element, const gchar *option)
{
    for ( ; element; element = element->next)
    {
        gboolean ret;

        if (element->is_block)
        {
            struct block *block = element->data;
            gchar buf[255], *b = buf;

            if (snprintf (buf, 255, "/columns/%s/type", block->col_name) >= 255)
                b = g_strdup_printf ("/columns/%s/type", block->col_name);
            ret = streq (option, b);
            if (b != buf)
                g_free (b);
        }
        else
            ret = element_need_recompile (element->data, option);

        if (ret)
            return TRUE;
    }

    return FALSE;
}

static void
option_cb (DonnaConfig *config, const gchar *option, DonnaFilter *filter)
{
    DonnaFilterPrivate *priv = filter->priv;

    if (!streqn (option, "/columns/", 9))
        return;

    if (element_need_recompile (priv->element, option))
    {
        free_element (priv->element);
        priv->element = NULL;
    }
}

static inline DonnaColumnType *
get_ct (DonnaFilter *filter, const gchar *col_name)
{
    DonnaFilterPrivate *priv = filter->priv;
    DonnaConfig *config;
    DonnaColumnType *ct;
    gchar *type = NULL;

    config = donna_app_peek_config (priv->app);

    if (priv->option_set_sid == 0)
        priv->option_set_sid = g_signal_connect (config, "option-set",
                (GCallback) option_cb, filter);
    if (priv->option_deleted_sid == 0)
        priv->option_deleted_sid = g_signal_connect (config, "option-deleted",
                (GCallback) option_cb, filter);

    if (donna_config_get_string (config, &type, "columns/%s/type", col_name))
        ct = donna_app_get_columntype (priv->app, type);
    else
        /* fallback to its name */
        ct = donna_app_get_columntype (priv->app, col_name);

    g_free (type);
    return ct;
}

static inline gchar *
get_quoted_string (gchar **str, gboolean get_string)
{
    GString *string;
    gchar *start = *str;
    gchar *end;

    if (get_string)
        string = g_string_new (NULL);

    for (end = ++start; ; ++end)
    {
        if (*end == '\\')
        {
            ++end;
            if (get_string)
                g_string_append_c (string, *end);
            continue;
        }
        if (*end == '"')
            break;
        else if (*end == '\0')
        {
            if (get_string)
                g_string_free (string, TRUE);
            return NULL;
        }
        if (get_string)
            g_string_append_c (string, *end);
    }
    *str = end;
    return (get_string) ? g_string_free (string, FALSE) : *str;
}

static struct block *
parse_block (DonnaFilter *filter, gchar **str, GError **error)
{
    struct block *block;
    gchar *f = *str;
    gchar *s;

    block = g_slice_new0 (struct block);
    skip_blank (f);
    /* get columntype */
    s = strchr (f, ':');
    if (!s)
    {
        /* this means this is just a filter for name */
        block->col_name = g_strdup ("name");
        block->ct = get_ct (filter, block->col_name);
        if (!block->ct)
        {
            g_set_error (error, DONNA_FILTER_ERROR,
                    DONNA_FILTER_ERROR_INVALID_COLUMNTYPE,
                    "Unable to load columntype for 'name'");
            free_block (block);
            *str = f;
            return NULL;
        }
    }
    else
    {
        *s = '\0';
        block->col_name = g_strdup (f);
        block->ct = get_ct (filter, block->col_name);
        if (!block->ct)
        {
            g_set_error (error, DONNA_FILTER_ERROR,
                    DONNA_FILTER_ERROR_INVALID_COLUMNTYPE,
                    "Unable to load columntype for '%s'",
                    block->col_name);
            *s = ':';
            free_block (block);
            *str = f;
            return NULL;
        }
        *s = ':';
        /* move to the actual filter */
        f = s + 1;
    }

    /* get filter */
    if (f[0] == '"')
    {
        block->filter = get_quoted_string (&f, TRUE);
        if (!block->filter)
        {
            g_set_error (error, DONNA_FILTER_ERROR,
                    DONNA_FILTER_ERROR_INVALID_SYNTAX,
                    "Missing closing quote: %s", f);
            free_block (block);
            *str = f;
            return NULL;
        }
    }
    else
    {
        block->filter = g_strdup (f);
        /* i.e. nothing after */
        f = NULL;
    }

    *str = f;
    return block;
}

static struct element *
parse_element (DonnaFilter *filter, gchar **str, GError **error)
{
    struct element *first_element = NULL;
    struct element *last_element = NULL;
    struct element *element;
    gchar *f = *str;

    for (;;)
    {
        element = g_slice_new0 (struct element);
        if (last_element)
        {
            if ((f[0] == 'a' || f[0] == 'A') && (f[1] == 'n' || f[1] == 'N')
                    && (f[2] == 'd' || f[2] == 'D')
                    && (f[3] == '(' || isblank (f[3])))
            {
                element->cond = COND_AND;
                f += 3;
            }
            else if ((f[0] == 'o' || f[0] == 'O')
                    && (f[1] == 'r' || f[1] == 'R')
                    && (f[2] == '(' || isblank (f[2])))
            {
                element->cond = COND_OR;
                f += 2;
            }
            else
            {
                g_set_error (error, DONNA_FILTER_ERROR,
                        DONNA_FILTER_ERROR_INVALID_SYNTAX,
                        "Expected 'AND' or 'OR': %s", f);
                free_element (element);
                goto undo;
            }
        }
        else
            /* first element must be AND */
            element->cond = COND_AND;

        skip_blank (f);
        if ((f[0] == 'N' || f[0] == 'n') && (f[1] == 'O' || f[1] == 'o')
                && (f[2] == 'T' || f[2] == 't')
                && (f[3] == '(' || isblank (f[3])))
        {
            element->is_not = TRUE;
            f += 3;
        }

        skip_blank (f);
        if (*f == '(')
        {
            /* remember beginning, as f will move to the end */
            gchar *s = ++f;
            /* parenthesis contained within */
            gint c = 0;

            /* find closing parenthesis */
            for ( ; ; ++f)
            {
                if (*f == '\0')
                {
                    g_set_error (error, DONNA_FILTER_ERROR,
                            DONNA_FILTER_ERROR_INVALID_SYNTAX,
                            "Missing closing parenthesis: %s", *str);
                    free_element (element);
                    goto undo;
                }
                else if (*f == '"')
                {
                    /* FALSE: simply move f past the closing parenthesis */
                    if (!get_quoted_string (&f, FALSE))
                    {
                        g_set_error (error, DONNA_FILTER_ERROR,
                                DONNA_FILTER_ERROR_INVALID_SYNTAX,
                                "Missing closing quote: %s", f);
                        free_element (element);
                        goto undo;
                    }
                    /* f was moved after the closing quote, move it back so when
                     * moving forward in the loop we don't skip anything */
                    --f;
                }
                else if (*f == '(')
                    ++c;
                else if (*f == ')')
                {
                    if (c > 0)
                        --c;
                    else
                        break;
                }
            }
            /* parse the string within parenthesis */
            *f = '\0';
            element->data = parse_element (filter, &s, error);
            *f = ')';
            f = s + 1;
        }
        else
        {
            element->is_block = TRUE;
            element->data = parse_block (filter, &f, error);
        }
        if (!element->data)
        {
            free_element (element);
            goto undo;
        }

        if (last_element)
            last_element->next = element;
        else
            first_element = element;
        last_element = element;

        if (!f)
            break;
        skip_blank (f);
        if (*f == '\0')
            break;
    }

    *str = f;
    return first_element;

undo:
    free_element (first_element);
    *str = f;
    return NULL;
}

static gboolean
is_match_element (struct element    *element,
                  DonnaNode         *node,
                  get_ct_data_fn     get_ct_data,
                  gpointer           data,
                  GError           **error)
{
    GError *err = NULL;
    gboolean match = TRUE;

    for ( ; element; element = element->next)
    {
        if (match && element->cond == COND_OR)
            break;
        if (!match && element->cond == COND_AND)
            break;

        if (element->is_block)
        {
            struct block *block = element->data;

            match = donna_columntype_is_match_filter (block->ct,
                    block->filter, &block->data,
                    get_ct_data (block->col_name, data), node, &err);
        }
        else
            match = is_match_element (element->data, node, get_ct_data, data, &err);

        if (err)
        {
            if (error)
                g_propagate_error (error, err);
            else
                g_clear_error (&err);
            return FALSE;
        }

        if (element->is_not)
            match = !match;
    }
    return match;
}

gboolean
donna_filter_is_match (DonnaFilter    *filter,
                       DonnaNode      *node,
                       get_ct_data_fn  get_ct_data,
                       gpointer        data,
                       GError       **error)
{
    DonnaFilterPrivate *priv;

    g_return_val_if_fail (DONNA_IS_FILTER (filter), FALSE);
    g_return_val_if_fail (DONNA_IS_NODE (node), FALSE);

    priv = filter->priv;

    /* if needed, compile the filter into elements */
    if (!priv->element)
    {
        gchar *f = priv->filter;
        priv->element = parse_element (filter, &f, error);
        if (!priv->element)
            return FALSE;
    }

    /* see if node matches the filter */
    return is_match_element (priv->element, node,
            (get_ct_data) ? get_ct_data : (get_ct_data_fn) donna_app_get_ct_data,
            (get_ct_data) ? data : priv->app,
            error);
}

/* this is needed for filter_toggle_ref_cb() in donna.c where we need to get the
 * filter string, but can't use g_object_get() as it would take a ref on it,
 * thus triggering the toggle_ref and enterring an infinite recursion... */
gchar *
donna_filter_get_filter (DonnaFilter *filter)
{
    g_return_val_if_fail (DONNA_IS_FILTER (filter), NULL);
    return g_strdup (filter->priv->filter);
}
