
#include <string.h>
#include <ctype.h>      /* isblank() */
#include "filter.h"
#include "app.h"
#include "columntype.h"

enum
{
    PROP_0,

    PROP_APP,
    PROP_FILTER,

    NB_PROPS
};

enum cond
{
    COND_AND,
    COND_OR,
};

struct block
{
    /* condition for the block */
    enum cond        condition;
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
    gchar       *filter;
    DonnaApp    *app;
    GSList      *blocks;
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
donna_filter_finalize (GObject *object)
{
    DonnaFilterPrivate *priv;

    priv = DONNA_FILTER (object)->priv;
    g_object_unref (priv->app);
    g_free (priv->filter);
    if (priv->blocks)
        g_slist_free_full (priv->blocks, (GDestroyNotify) free_block);

    G_OBJECT_CLASS (donna_filter_parent_class)->finalize (object);
}

static inline DonnaColumnType *
get_ct (DonnaFilter *filter, const gchar *col_name)
{
    DonnaFilterPrivate *priv = filter->priv;
    DonnaConfig *config;
    DonnaColumnType *ct;
    gchar *type = NULL;

    config = donna_app_peek_config (priv->app);
    if (donna_config_get_string (config, &type, "columns/%s", col_name))
        ct = donna_app_get_columntype (priv->app, type);
    else
        /* fallback to its name */
        ct = donna_app_get_columntype (priv->app, col_name);

    g_free (type);
    return ct;
}

gboolean
donna_filter_is_match (DonnaFilter    *filter,
                       DonnaNode      *node,
                       get_ct_data_fn  get_ct_data,
                       gpointer        data,
                       GError       **error)
{
    DonnaFilterPrivate *priv;
    GError *err = NULL;
    struct block *block;
    GSList *l;
    gboolean match;

    g_return_val_if_fail (DONNA_IS_FILTER (filter), FALSE);
    g_return_val_if_fail (DONNA_IS_NODE (node), FALSE);
    g_return_val_if_fail (get_ct_data != NULL, FALSE);

    priv = filter->priv;

    /* if needed, compile the filter into blocks */
    if (!priv->blocks)
    {
        /* first block should be AND */
        enum cond cond = COND_AND;
        gchar *f = priv->filter;
        gchar *s;

        for (;;)
        {
            block = g_slice_new0 (struct block);
            block->condition = cond;

            for ( ; isblank (*f); ++f)
                ;

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
                    goto undo;
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
                    goto undo;
                }
                *s = ':';
                /* move to the actual filter */
                f = s + 1;
            }

            /* get filter */
            if (f[0] == '"')
            {
                gint i;

                /* it is quoted (there's probably more behind) */
                s = f + 1;
                for (;;)
                {
                    s = strchr (s, '"');
                    if (!s)
                    {
                        g_set_error (error, DONNA_FILTER_ERROR,
                                DONNA_FILTER_ERROR_INVALID_SYNTAX,
                                "Missing closing quote: %s", f);
                        free_block (block);
                        goto undo;
                    }
                    /* check for escaped quotes within filter */
                    for (i = 0; s[i-1] == '\\'; --i)
                        ;
                    if ((i % 2) == 0)
                        break;
                    ++s;
                }
                /* no quotes within, just dup */
                if (i == 0)
                    block->filter = g_strndup (f + 1, s - f - 1);
                else
                {
                    gsize len = s - f - 1;
                    gchar *ss;

                    /* we'll have to unescape the quotes */
                    ss = g_new (gchar, len + 1);
                    for (i = 0; (guint) i < len; ++i)
                    {
                        if (f[1 + i] != '\\')
                            *ss++ = f[1 + i];
                        else if (f[2 + i] == '\\')
                            *ss++ = f[1 + ++i];
                    }
                    *ss = '\0';
                    block->filter = ss;
                }
                /* move to the next one (if any) */
                f = s + 1;
            }
            else
            {
                block->filter = g_strdup (f);
                /* i.e. nothing after */
                f = NULL;
            }

            /* add the block */
            priv->blocks = g_slist_prepend (priv->blocks, block);

            /* we're done */
            if (!f || *f == '\0')
                break;
            /* there's more, so we get the condition */

            for ( ; isblank (*f); ++f)
                ;
            if ((f[0] == 'a' || f[0] == 'A') && (f[1] == 'n' || f[1] == 'N')
                    && (f[2] == 'd' || f[2] == 'D') && isblank (f[3]))
            {
                cond = COND_AND;
                f += 4;
            }
            else if ((f[0] == 'o' || f[0] == 'O')
                    && (f[1] == 'r' || f[1] == 'R') && isblank (f[2]))
            {
                cond = COND_OR;
                f += 3;
            }
            else
            {
                g_set_error (error, DONNA_FILTER_ERROR,
                        DONNA_FILTER_ERROR_INVALID_SYNTAX,
                        "Expected 'AND' or 'OR': %s", f);
                goto undo;
            }
        }
        priv->blocks = g_slist_reverse (priv->blocks);
    }

    /* see if node matches the filter */
    match = TRUE;
    for (l = priv->blocks; l; l = l->next)
    {
        block = l->data;

        if (match && block->condition == COND_OR)
            break;
        if (!match && block->condition == COND_AND)
            break;

        match = donna_columntype_is_match_filter (block->ct,
                block->filter, &block->data,
                get_ct_data (block->col_name, data), node, &err);
        if (err)
        {
            if (error)
                g_propagate_error (error, err);
            else
                g_clear_error (&err);
            return FALSE;
        }
    }

    return match;

undo:
    g_slist_free_full (priv->blocks, (GDestroyNotify) free_block);
    priv->blocks = NULL;
    return FALSE;
}
