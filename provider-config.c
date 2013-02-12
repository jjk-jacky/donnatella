
#define _GNU_SOURCE             /* strchrnul() in string.h */
#include <glib-object.h>
#include <stdio.h>              /* sscanf() */
#include <string.h>
#include <ctype.h>              /* isblank() */
#include "provider-config.h"
#include "provider.h"
#include "node.h"
#include "task.h"
#include "macros.h"

struct parsed_data
{
    gchar               *name;
    gpointer             value;
    struct parsed_data  *next;
};

enum extra_type
{
    EXTRA_TYPE_LIST,
    EXTRA_TYPE_LIST_INT,
};

struct extra_list_int
{
    gint     value;
    gchar   *desc;
};

struct extra
{
    enum extra_type type;
    gpointer        values;
};

struct option
{
    /* name of the option */
    gchar       *name;
    /* priv->root if a category; NULL for standard (bool, int, etc) types, else
     * the key to search for in priv->extras */
    gpointer     extra;
    /* the actual value, or a pointer to the struct group if extra ==
     * priv->groups. An int for categories, the next index to use when
     * auto-creating subcategories (e.g. for "arrangements/" and such) */
    GValue       value;
    /* if the node is loaded, else NULL */
    DonnaNode   *node;
};

struct _DonnaProviderConfigPrivate
{
    /* extra formats of options (list, list-int, etc) */
    GHashTable  *extras;
    /* root of config */
    GNode       *root;
    /* config lock */
    GRWLock      lock;
    /* a recursive mutex to handle (toggle ref) nodes. Should only be locked
     * after a lock on config (GRWLock above), reader is good enough */
    GRecMutex    nodes_mutex;
};

#define option_is_category(opt, root)    \
    (((struct option *) opt)->extra == root)

static void             provider_config_finalize    (GObject    *object);

/* DonnaProvider */
static DonnaTask *      provider_config_get_node_task (
                                            DonnaProvider       *provider,
                                            const gchar         *location);
static DonnaTask *      provider_config_has_node_children_task (
                                            DonnaProvider       *provider,
                                            DonnaNode           *node,
                                            DonnaNodeType        node_types);
static DonnaTask *      provider_config_get_node_children_task (
                                            DonnaProvider       *provider,
                                            DonnaNode           *node,
                                            DonnaNodeType        node_types);
static DonnaTask *      provider_config_remove_node_task (
                                            DonnaProvider       *provider,
                                            DonnaNode           *node);


static void free_extra  (struct extra  *extra);

static void
provider_config_provider_init (DonnaProviderInterface *interface)
{
    interface->get_node_task          = provider_config_get_node_task;
    interface->has_node_children_task = provider_config_has_node_children_task;
    interface->get_node_children_task = provider_config_get_node_children_task;
    interface->remove_node_task       = provider_config_remove_node_task;
}

static void
donna_provider_config_class_init (DonnaProviderConfigClass *klass)
{
    GObjectClass *o_class;

    o_class = (GObjectClass *) klass;
    o_class->finalize = provider_config_finalize;

    g_type_class_add_private (klass, sizeof (DonnaProviderConfigPrivate));
}

static void
donna_provider_config_init (DonnaProviderConfig *provider)
{
    DonnaProviderConfigPrivate *priv;
    struct option *option;

    priv = provider->priv = G_TYPE_INSTANCE_GET_PRIVATE (provider,
            DONNA_TYPE_PROVIDER_CONFIG,
            DonnaProviderConfigPrivate);
    priv->extras = g_hash_table_new_full (g_str_hash, g_str_equal,
            g_free, (GDestroyNotify) free_extra);
    option = g_slice_new0 (struct option);
    /* categories hold the next index for auto-creation of subcategories. It
     * shouldn't be used on root, doesn't make much sense, but this avoids
     * special cases for that & free-ing stuff */
    g_value_init (&option->value, G_TYPE_INT);
    g_value_set_int (&option->value, 1);
    priv->root = g_node_new (option);
    option->extra = priv->root;
    g_rw_lock_init (&priv->lock);
    g_rec_mutex_init (&priv->nodes_mutex);
}

G_DEFINE_TYPE_WITH_CODE (DonnaProviderConfig, donna_provider_config,
        G_TYPE_OBJECT,
        G_IMPLEMENT_INTERFACE (DONNA_TYPE_PROVIDER, provider_config_provider_init)
        )

static void
free_extra (struct extra *extra)
{
    if (!extra)
        return;
    if (extra->type == EXTRA_TYPE_LIST)
        g_strfreev (extra->values);
    else
    {
        struct extra_list_int **values;

        for (values = extra->values; *values; ++values)
        {
            g_free ((*values)->desc);
            g_free (*values);
        }
    }
    g_free (extra);
}

static void
free_option (DonnaProviderConfig *config, struct option *option, gboolean is_removing)
{
    if (!option)
        return;
    g_free (option->name);
    if (option->extra != config->priv->root)
        g_free (option->extra);
    g_value_unset (&option->value);
    if (is_removing && option->node)
        donna_provider_node_removed (DONNA_PROVIDER (config), option->node);
    g_object_unref (option->node);
    g_slice_free (struct option, option);
}

static gboolean
free_node_data (GNode *node, DonnaProviderConfig *config)
{
    free_option (config, node->data, FALSE);
    return FALSE;
}

static gboolean
free_node_data_removing (GNode *node, DonnaProviderConfig *config)
{
    free_option (config, node->data, TRUE);
    return FALSE;
}

static void
provider_config_finalize (GObject *object)
{
    DonnaProviderConfigPrivate *priv;

    priv = DONNA_PROVIDER_CONFIG (object)->priv;

    g_hash_table_destroy (priv->extras);
    g_node_traverse (priv->root, G_IN_ORDER, G_TRAVERSE_ALL, -1,
            (GNodeTraverseFunc) free_node_data,
            DONNA_PROVIDER_CONFIG (object));
    g_node_destroy (priv->root);
    g_rw_lock_clear (&priv->lock);
    g_rec_mutex_clear (&priv->nodes_mutex);

    /* chain up */
    G_OBJECT_CLASS (donna_provider_config_parent_class)->finalize (object);
}

/*** PARSING CONFIGURATION ***/

static inline void
trim_line (gchar **line)
{
    gchar *e;

    e = *line + strlen (*line);
    for (--e; isblank (*e); --e)
        ;
    e[1] = '\0';

    for ( ; isblank (**line); ++*line)
        ;
}

static gboolean
is_valid_name (gchar *name, gboolean is_section)
{
    gint is_first = 1;

    for ( ; *name != '\0'; ++name)
    {
        gint  match;
        gchar c;

        match = 0;
        for (c = 'a'; c <= 'z'; ++c)
        {
            if (*name == c)
            {
                match = 1;
                break;
            }
        }
        if (match)
        {
            is_first = 0;
            continue;
        }
        else if (is_first)
            return FALSE;
        for (c = '0'; c <= '9'; ++c)
        {
            if (*name == c)
            {
                match = 1;
                break;
            }
        }
        if (match || *name == '-' || *name == '_' || *name == ' '
                || *name == ((is_section) ? '/' : ':'))
            continue;
        return FALSE;
    }
    return TRUE;
}

static struct parsed_data *
parse_data (gchar *data)
{
    struct parsed_data *first_section   = NULL;
    struct parsed_data *section         = NULL;
    struct parsed_data *option          = NULL;
    gint   eof  = 0;
    gint   line = 0;
    gint   skip = 0;
    gchar *s;
    gchar *e;
    gchar *eol;

    if (!data)
        return NULL;

    for ( ; !eof; data = eol + 1)
    {
        ++line;

        eol = strchr (data, '\n');
        if (!eol)
        {
            eof = 1;
            eol = data + strlen (data);
        }
        else
            *eol = '\0';

        if (*data == '[')
        {
            struct parsed_data *new_section;

            e = strchr (++data, ']');
            if (!e)
            {
                g_warning ("Invalid section definition at line %d, "
                        "skipping to next section", line);
                skip = 1;
                continue;
            }
            *e = '\0';
            /* trim because spaces are allowed characters *within* the name */
            trim_line (&data);
            if (!is_valid_name (data, TRUE))
            {
                g_warning ("Invalid section name (%s) at line %d, "
                        "skipping to next section", data, line);
                skip = 1;
                continue;
            }
            new_section = g_slice_new0 (struct parsed_data);
            new_section->name = data;
            if (section)
                section->next = new_section;
            else if (!first_section)
                first_section = new_section;
            section = new_section;
            option = NULL;
        }
        else if (!skip)
        {
            struct parsed_data *new_option;

            /* trim now, so we can check if it's a comment */
            trim_line (&data);
            if (*data == '#')
                continue;

            s = strchr (data, '=');
            if (!s)
            {
                g_warning ("Invalid value definition at line %d, "
                        "skipping to next line", line);
                continue;
            }
            *s = '\0';
            trim_line (&data);
            if (!is_valid_name (data, FALSE))
            {
                g_warning ("Invalid option name (%s) at line %d, "
                        "skipping to next line", data, line);
                continue;
            }
            new_option = g_slice_new0 (struct parsed_data);
            new_option->name = data;
            ++s;
            trim_line (&s);
            new_option->value = s;
            if (option)
                option->next = new_option;
            else if (section)
                section->value = new_option;
            else
            {
                section = g_slice_new0 (struct parsed_data);
                section->value = new_option;
                first_section = section;
            }
            option = new_option;
        }
    }
    return first_section;
}

static inline gchar *
get_value (struct parsed_data *value, const gchar *name)
{
    for ( ; value; value = value->next)
    {
        if (streq (value->name, name))
            return value->value;
    }
    return NULL;
}

static inline gint
get_count (struct parsed_data *parsed, const gchar *name)
{
    gint c = 0;
    for ( ; parsed; parsed = parsed->next)
        if (!name || streq (parsed->name, name))
            ++c;
    return c;
}

static inline void
free_parsed_data (struct parsed_data *parsed)
{
    struct parsed_data *next;

    for ( ; parsed; parsed = next)
    {
        next = parsed->next;
        g_slice_free (struct parsed_data, parsed);
    }
}

static inline void
free_parsed_data_section (struct parsed_data *parsed)
{
    struct parsed_data *next;

    for ( ; parsed; parsed = next)
    {
        free_parsed_data ((struct parsed_data *) parsed->value);
        next = parsed->next;
        g_slice_free (struct parsed_data, parsed);
    }
}

gboolean
donna_config_load_config_def (DonnaProviderConfig *config, gchar *data)
{
    DonnaProviderConfigPrivate *priv;
    struct parsed_data *first_section;
    struct parsed_data *section;

    g_return_val_if_fail (DONNA_IS_PROVIDER_CONFIG (config), FALSE);
    priv = config->priv;

    first_section = parse_data (data);
    if (!first_section)
    {
        g_free (data);
        return TRUE;
    }

    for (section = first_section ; section; section = section->next)
    {
        gchar *s;

        s = get_value (section->value, "type");
        if (!s)
        {
            g_warning ("Option type missing for config def of '%s'",
                    section->name);
            continue;
        }

        if (streq (s, "list"))
        {
            struct parsed_data *parsed;
            struct extra *extra;
            gchar **values;
            gint c;

            if (g_hash_table_contains (priv->extras, section->name))
            {
                g_warning ("Cannot redefine extra '%s'", section->name);
                continue;
            }

            values = g_new0 (gchar *, get_count (section->value, "value") + 1);
            for (parsed = section->value, c = 0; parsed; parsed = parsed->next)
            {
                if (streq (parsed->name, "value"))
                    values[c++] = g_strdup (parsed->value);
                else if (!streq (parsed->name, "type"))
                    g_warning ("Invalid option '%s' in definition of %s '%s'",
                            parsed->name, s, section->name);
            }

            extra = g_new (struct extra, 1);
            extra->type = EXTRA_TYPE_LIST;
            extra->values = values;

            g_hash_table_insert (priv->extras, g_strdup (section->name), extra);
        }
        else if (streq (s, "list-int"))
        {
            struct parsed_data *parsed;
            struct extra *extra;
            struct extra_list_int **values;
            gint c;

            if (g_hash_table_contains (priv->extras, section->name))
            {
                g_warning ("Cannot redefine extra '%s'", section->name);
                continue;
            }

            values = g_new0 (struct extra_list_int *,
                    get_count (section->value, "value") + 1);
            for (parsed = section->value, c = 0; parsed; parsed = parsed->next)
            {
                if (streq (parsed->name, "value"))
                {
                    struct extra_list_int *v;
                    gchar *sep;

                    sep = strchr (parsed->value, ':');
                    if (!sep)
                    {
                        struct extra_list_int **v;

                        g_warning ("Invalid format for value '%s' of extra '%s', "
                                "skipping entire definition",
                                (gchar *) parsed->value, section->name);
                        for (v = values; *v; ++v)
                        {
                            g_free ((*v)->desc);
                            g_free (*v);
                        }
                        g_free (values);
                        values = NULL;
                        break;
                    }

                    v = g_new0 (struct extra_list_int, 1);
                    *sep = '\0';
                    sscanf (parsed->value, "%d", &v->value);
                    *sep = ':';
                    v->desc = g_strdup (sep + 1);

                    values[c++] = v;
                }
                else if (!streq (parsed->name, "type"))
                    g_warning ("Invalid option '%s' in definition of %s '%s'",
                            parsed->name, s, section->name);
            }
            if (!values)
                continue;

            extra = g_new (struct extra, 1);
            extra->type = EXTRA_TYPE_LIST_INT;
            extra->values = values;

            g_hash_table_insert (priv->extras, g_strdup (section->name), extra);
        }
        else
            g_warning ("Unknown type '%s' for definition '%s'", s, section->name);
    }

    free_parsed_data_section (first_section);
    g_free (data);
    return TRUE;
}

/* assumes a lock on config */
static inline GNode *
get_child_node (GNode *parent, const gchar *name, gsize len)
{
    GNode *node;

    for (node = parent->children; node; node = node->next)
        if (streqn (((struct option *) node->data)->name,
                    name,
                    len))
            return node;

    return NULL;
}

/* assumes a writer lock on config */
static GNode *
ensure_categories (DonnaProviderConfig *config, const gchar *name, gsize len)
{
    GNode *root;
    GNode *parent;
    GNode *node;
    const gchar *s;

    /* skip the main root, if specified */
    if (*name == '/')
        ++name;

    parent = root = config->priv->root;
    /* if name is "/" there's nothing to check, the config root exists */
    if (*name == '\0')
        return root;

    for (;;)
    {
        /* string ended with / i.e. we should auto-create a new category */
        if (*name == '\0')
        {
            struct option *option;
            gint i;

            s = name;
            /* the GValue for categories hold an integer value with the next
             * index to use for such cases */
            option = parent->data;
            i = g_value_get_int (&option->value);
            g_value_set_int (&option->value, i + 1);

            option = g_slice_new0 (struct option);
            option->name = g_strdup_printf ("%d", i);
            option->extra = root;
            g_value_init (&option->value, G_TYPE_INT);
            g_value_set_int (&option->value, 1);
            node = g_node_append_data (parent, option);
        }
        else
        {
            s = strchrnul (name, '/');
            node = get_child_node (parent, name, s - name);
            if (node)
            {
                if (!option_is_category (node->data, root))
                    return NULL;
            }
            else
            {
                /* create category/node */
                struct option *option;

                option = g_slice_new0 (struct option);
                option->name = g_strndup (name, s - name);
                option->extra = root;
                /* category hold an index, next number to use for auto-creating
                 * sub-categories. (See above) */
                g_value_init (&option->value, G_TYPE_INT);
                g_value_set_int (&option->value, 1);
                node = g_node_append_data (parent, option);
            }
        }

        if (*s == '\0' || len == (gsize) (s - name))
            break;
        else
        {
            len -= s - name + 1;
            name = s + 1;
            parent = node;
        }
    }

    return node;
}

static gboolean
is_extra_value (struct extra *extra, gpointer value)
{
    if (extra->type == EXTRA_TYPE_LIST)
    {
        gchar **v;
        gchar *val = * (gchar **) value;

        for (v = extra->values; *v; ++v)
            if (streq (*v, val))
                return TRUE;
    }
    else /* EXTRA_TYPE_LIST_INT */
    {
        struct extra_list_int **v;
        gint val = * (gint *) value;

        for (v = extra->values; *v; ++v)
            if ((*v)->value == val)
                return TRUE;
    }
    return FALSE;
}

gboolean
donna_config_load_config (DonnaProviderConfig *config, gchar *data)
{
    DonnaProviderConfigPrivate *priv;
    GNode *parent;
    struct parsed_data *first_section;
    struct parsed_data *section;
    GRegex *re_int;
    GRegex *re_uint;
    GRegex *re_double;

    g_return_val_if_fail (DONNA_IS_PROVIDER_CONFIG (config), FALSE);
    priv = config->priv;

    first_section = parse_data (data);
    if (!first_section)
    {
        g_free (data);
        return TRUE;
    }

    re_int      = g_regex_new ("^[+-][0-9]+$", G_REGEX_OPTIMIZE, 0, NULL);
    re_uint     = g_regex_new ("^[0-9]+$", G_REGEX_OPTIMIZE, 0, NULL);
    re_double   = g_regex_new ("^[0-9]+\\.[0-9]+$", G_REGEX_OPTIMIZE, 0, NULL);

    g_rw_lock_writer_lock (&priv->lock);
    for (section = first_section; section; section = section->next)
    {
        struct parsed_data *parsed;

        if (section->name)
        {
            parent = ensure_categories (config,
                    section->name,
                    strlen (section->name));
            if (!parent)
            {
                g_warning ("Invalid category '%s'; skipping to next section",
                        section->name);
                continue;
            }
        }
        else
            parent = priv->root;

        for (parsed = section->value; parsed; parsed = parsed->next)
        {
            struct option *option;
            gchar *s;

            /* extra? */
            s = strchr (parsed->name, ':');
            if (s)
            {
                struct extra *extra;

                *s = '\0';
                extra = g_hash_table_lookup (priv->extras, ++s);
                if (!extra)
                {
                    g_warning ("Unknown extra format '%s' for option '%s' in '%s', skipped",
                            s, parsed->name, section->name);
                    *--s = ':';
                    continue;
                }

                option = g_slice_new0 (struct option);
                if (extra->type == EXTRA_TYPE_LIST)
                {
                    if (!is_extra_value (extra, &parsed->value))
                    {
                        g_warning ("Value for option '%s' isn't valid for extra '%s', skipped",
                                parsed->name, s);
                        *--s = ':';
                        g_slice_free (struct option, option);
                        continue;
                    }
                    g_value_init (&option->value, G_TYPE_STRING);
                    g_value_set_string (&option->value, parsed->value);
                }
                else /* EXTRA_TYPE_LIST_INT */
                {
                    gint v;

                    if (!sscanf (parsed->value, "%d", &v))
                    {
                        g_warning ("Failed to get INT value for option '%s' in '%s', skipped",
                                parsed->name, section->name);
                        *--s = ':';
                        g_slice_free (struct option, option);
                        continue;
                    }
                    if (!is_extra_value (extra, &v))
                    {
                        g_warning ("Value for option '%s' isn't valid for extra '%s', skipped",
                                parsed->name, s);
                        *--s = ':';
                        g_slice_free (struct option, option);
                        continue;
                    }
                    g_value_init (&option->value, G_TYPE_INT);
                    g_value_set_int (&option->value, v);

                }
                option->name = g_strdup (parsed->name);
                option->extra = g_strdup (s);
                g_node_append_data (parent, option);
                *--s = ':';
            }
            else
            {
                /* boolean */
                if (streq (parsed->value, "true")
                        || streq (parsed->value, "false"))
                {
                    option = g_slice_new0 (struct option);
                    option->name = g_strdup (parsed->name);
                    g_value_init (&option->value, G_TYPE_BOOLEAN);
                    g_value_set_boolean (&option->value,
                            streq (parsed->value, "true"));
                    g_node_append_data (parent, option);
                }
                /* int */
                else if (g_regex_match (re_int, parsed->value, 0, NULL))
                {
                    gint v;

                    if (!sscanf (parsed->value, "%d", &v))
                    {
                        g_warning ("Failed to get INT value for option '%s' in '%s', skipped",
                                parsed->name, section->name);
                        continue;
                    }
                    option = g_slice_new0 (struct option);
                    option->name = g_strdup (parsed->name);
                    g_value_init (&option->value, G_TYPE_INT);
                    g_value_set_int (&option->value, v);
                    g_node_append_data (parent, option);
                }
                /* uint */
                else if (g_regex_match (re_uint, parsed->value, 0, NULL))
                {
                    guint v;

                    if (!sscanf (parsed->value, "%u", &v))
                    {
                        g_warning ("Failed to get UINT value for option '%s' in '%s', skipped",
                                parsed->name, section->name);
                        continue;
                    }
                    option = g_slice_new0 (struct option);
                    option->name = g_strdup (parsed->name);
                    g_value_init (&option->value, G_TYPE_UINT);
                    g_value_set_uint (&option->value, v);
                    g_node_append_data (parent, option);
                }
                /* double */
                else if (g_regex_match (re_double, parsed->value, 0, NULL))
                {
                    gdouble v;

                    if (!sscanf (parsed->value, "%lf", &v))
                    {
                        g_warning ("Failed to get DOUBLE value for option '%s' in '%s', skipped",
                                parsed->name, section->name);
                        continue;
                    }
                    option = g_slice_new0 (struct option);
                    option->name = g_strdup (parsed->name);
                    g_value_init (&option->value, G_TYPE_DOUBLE);
                    g_value_set_double (&option->value, v);
                    g_node_append_data (parent, option);
                }
                /* string */
                else
                {
                    gsize len;
                    gchar *v = parsed->value;

                    /* remove quotes for quoted values */
                    if (v[0] == '"')
                    {
                        len = strlen (v) - 1;
                        if (len > 0 && v[len] == '"')
                            (++v)[--len] = '\0';
                    }

                    option = g_slice_new0 (struct option);
                    option->name = g_strdup (parsed->name);
                    g_value_init (&option->value, G_TYPE_STRING);
                    g_value_set_string (&option->value, v);
                    g_node_append_data (parent, option);

                    if (v != parsed->value)
                        v[len] = '"';
                }
            }
        }
    }
    g_rw_lock_writer_unlock (&priv->lock);

    g_regex_unref (re_int);
    g_regex_unref (re_uint);
    g_regex_unref (re_double);

    free_parsed_data_section (first_section);
    g_free (data);
    return TRUE;
}

/*** EXPORTING CONFIGURATION ***/

static void
export_config (DonnaProviderConfigPrivate   *priv,
               GNode                        *node,
               GString                      *str_loc,
               GString                      *str,
               gboolean                      do_options)
{
    GNode *child;

    for (child = node->children; child; child = child->next)
    {
        struct option *option = child->data;

        if (do_options)
        {
            if (option->extra == priv->root)
                continue;

            if (option->extra)
            {
                if (G_VALUE_HOLDS (&option->value, G_TYPE_STRING))
                {
                    const gchar *s;

                    s = g_value_get_string (&option->value);
                    if (streq (s, "true") || streq (s, "false")
                            || isblank (s[0])
                            || (s[0] != '\0' && isblank (s[strlen (s) - 1])))
                        g_string_append_printf (str, "%s:%s=\"%s\"\n",
                                option->name,
                                (gchar *) option->extra,
                                s);
                    else
                        g_string_append_printf (str, "%s:%s=%s\n",
                                option->name,
                                (gchar *) option->extra,
                                s);
                }
                else
                {
                    g_string_append_printf (str, "%s:%s=%d\n",
                            option->name,
                            (gchar *) option->extra,
                            g_value_get_int (&option->value));
                }
            }
            else
            {
                switch (G_VALUE_TYPE (&option->value))
                {
                    case G_TYPE_BOOLEAN:
                        g_string_append_printf (str, "%s=%s\n",
                                option->name,
                                (g_value_get_boolean (&option->value))
                                ? "true" : "false");
                        break;
                    case G_TYPE_UINT:
                        g_string_append_printf (str, "%s=%u\n",
                                option->name,
                                g_value_get_uint (&option->value));
                        break;
                    case G_TYPE_INT:
                        {
                            gint i;

                            i = g_value_get_int (&option->value);
                            if (i >= 0)
                                g_string_append_printf (str, "%s=+%d\n",
                                        option->name,
                                        g_value_get_int (&option->value));
                            else
                                g_string_append_printf (str, "%s=%d\n",
                                        option->name,
                                        g_value_get_int (&option->value));
                            break;
                        }
                    case G_TYPE_DOUBLE:
                        g_string_append_printf (str, "%s=%lf\n",
                                option->name,
                                g_value_get_double (&option->value));
                        break;
                    case G_TYPE_STRING:
                        {
                            const gchar *s;

                            s = g_value_get_string (&option->value);
                            if (streq (s, "true") || streq (s, "false")
                                    || isblank (s[0])
                                    || (s[0] != '\0' && isblank (s[strlen (s) - 1])))
                                g_string_append_printf (str, "%s=\"%s\"\n",
                                        option->name,
                                        s);
                            else
                                g_string_append_printf (str, "%s=%s\n",
                                        option->name,
                                        s);
                            break;
                        }
                }
            }
        }
        else if (option_is_category (option, priv->root))
        {
            gsize len;

            len = str_loc->len;
            if (len)
                g_string_append_c (str_loc, '/');
            /* name starting with a number is invalid, so this is an
             * auto-indexed category. In that case, we don't export the name */
            if (option->name[0] < '0' || option->name[0] > '9')
                g_string_append (str_loc, option->name);
            g_string_append_printf (str, "[%s]\n", str_loc->str);
            export_config (priv, child, str_loc, str, TRUE);
            g_string_erase (str_loc, len, -1);
        }
    }
    if (do_options)
        export_config (priv, node, str_loc, str, FALSE);
}

gchar *
donna_config_export_config (DonnaProviderConfig *config)
{
    DonnaProviderConfigPrivate *priv;
    GString *str;
    GString *str_loc;

    g_return_val_if_fail (DONNA_IS_PROVIDER_CONFIG (config), NULL);
    priv = config->priv;
    str = g_string_sized_new (2048);
    str_loc = g_string_sized_new (23); /* random size, to hold section's name */

    g_rw_lock_reader_lock (&priv->lock);
    export_config (priv, priv->root, str_loc, str, TRUE);
    g_rw_lock_reader_unlock (&priv->lock);

    g_string_free (str_loc, TRUE);
    return g_string_free (str, FALSE);
}

/*** ACCESSING CONFIGURATION ***/

/* assumes reader lock on config */
static GNode *
get_option_node (GNode *root, const gchar *name)
{
    GNode *node;
    gchar *s;

    /* skip the main root, if specified */
    if (*name == '/')
        ++name;

    /* no option name? */
    if (*name == '\0')
        return NULL;

    node = root;
    for (;;)
    {
        s = strchrnul (name, '/');

        if (*name != '\0')
            node = get_child_node (node, name, s - name);
        else
            node = NULL;

        if (!node)
            return NULL;

        if (*s == '\0')
            break;
        else
            name = s + 1;
    }

    return node;
}

/* assumes reader lock on config */
static inline struct option *
get_option (GNode *root, const gchar *name)
{
    GNode *node;

    node = get_option_node (root, name);
    if (node)
        return (struct option *) node->data;
    else
        return NULL;
}

#define _get_option(type, get_value)    do  {                           \
    DonnaProviderConfigPrivate *priv;                                   \
    struct option *option;                                              \
    gboolean ret;                                                       \
                                                                        \
    g_return_val_if_fail (DONNA_IS_PROVIDER_CONFIG (config), FALSE);    \
    g_return_val_if_fail (name != NULL, FALSE);                         \
    g_return_val_if_fail (value != NULL, FALSE);                        \
                                                                        \
    priv = config->priv;                                                \
    g_rw_lock_reader_lock (&priv->lock);                                \
    option = get_option (priv->root, name);                             \
    if (option)                                                         \
        ret = G_VALUE_HOLDS (&option->value, type);                     \
    else                                                                \
        ret = FALSE;                                                    \
    if (ret)                                                            \
        *value = get_value (&option->value);                            \
    g_rw_lock_reader_unlock (&priv->lock);                              \
                                                                        \
    return ret;                                                         \
} while (0)

gboolean
donna_config_get_boolean (DonnaProviderConfig    *config,
                          const gchar            *name,
                          gboolean               *value)
{
    _get_option (G_TYPE_BOOLEAN, g_value_get_boolean);
}

gboolean
donna_config_get_int (DonnaProviderConfig    *config,
                      const gchar            *name,
                      gint                   *value)
{
    _get_option (G_TYPE_INT, g_value_get_int);
}

gboolean
donna_config_get_uint (DonnaProviderConfig    *config,
                       const gchar            *name,
                       guint                  *value)
{
    _get_option (G_TYPE_UINT, g_value_get_uint);
}

gboolean
donna_config_get_double (DonnaProviderConfig    *config,
                         const gchar            *name,
                         gdouble                *value)
{
    _get_option (G_TYPE_DOUBLE, g_value_get_double);
}

gboolean
donna_config_get_string (DonnaProviderConfig    *config,
                         const gchar            *name,
                         gchar                 **value)
{
    _get_option (G_TYPE_STRING, g_value_dup_string);
}

#define _set_option(type, value_set)  do {                              \
    DonnaProviderConfigPrivate *priv;                                   \
    GNode *parent;                                                      \
    GNode *node;                                                        \
    struct option *option;                                              \
    const gchar *s;                                                     \
    gboolean ret;                                                       \
                                                                        \
    g_return_val_if_fail (DONNA_IS_PROVIDER_CONFIG (config), FALSE);    \
    g_return_val_if_fail (name != NULL, FALSE);                         \
                                                                        \
    if (*name == '/')                                                   \
        ++name;                                                         \
                                                                        \
    priv = config->priv;                                                \
    g_rw_lock_writer_lock (&priv->lock);                                \
    s = strrchr (name, '/');                                            \
    if (s)                                                              \
    {                                                                   \
        parent = ensure_categories (config, name, s - name);            \
        if (!parent)                                                    \
        {                                                               \
            g_rw_lock_writer_unlock (&priv->lock);                      \
            return FALSE;                                               \
        }                                                               \
        ++s;                                                            \
    }                                                                   \
    else                                                                \
    {                                                                   \
        s = name;                                                       \
        parent = priv->root;                                            \
    }                                                                   \
                                                                        \
    node = get_child_node (parent, s, strlen (s));                      \
    if (node)                                                           \
    {                                                                   \
        option = node->data;                                            \
        ret = G_VALUE_HOLDS (&option->value, type);                     \
    }                                                                   \
    else                                                                \
    {                                                                   \
        option = g_slice_new0 (struct option);                          \
        option->name = g_strdup (s);                                    \
        g_value_init (&option->value, type);                            \
        g_node_append_data (parent, option);                            \
        ret = TRUE;                                                     \
    }                                                                   \
    if (ret)                                                            \
    {                                                                   \
        value_set (&option->value, value);                              \
        if (option->node)                                               \
        {                                                               \
            donna_node_set_property_value (option->node,                \
                    "option-value",                                     \
                    &option->value);                                    \
        }                                                               \
    }                                                                   \
    g_rw_lock_writer_unlock (&priv->lock);                              \
                                                                        \
    return ret;                                                         \
} while (0)

gboolean
donna_config_set_boolean (DonnaProviderConfig    *config,
                          const gchar            *name,
                          gboolean                value)
{
    _set_option (G_TYPE_BOOLEAN, g_value_set_boolean);
}

gboolean
donna_config_set_int (DonnaProviderConfig    *config,
                      const gchar            *name,
                      gint                    value)
{
    _set_option (G_TYPE_INT, g_value_set_int);
}

gboolean
donna_config_set_uint (DonnaProviderConfig    *config,
                       const gchar            *name,
                       guint                   value)
{
    _set_option (G_TYPE_UINT, g_value_set_uint);
}

gboolean
donna_config_set_double (DonnaProviderConfig    *config,
                         const gchar            *name,
                         gdouble                 value)
{
    _set_option (G_TYPE_DOUBLE, g_value_set_double);
}

gboolean
donna_config_set_string (DonnaProviderConfig    *config,
                         const gchar            *name,
                         gchar                  *value)
{
    _set_option (G_TYPE_STRING, g_value_set_string);
}

gboolean
donna_config_take_string (DonnaProviderConfig    *config,
                          const gchar            *name,
                          gchar                  *value)
{
    _set_option (G_TYPE_STRING, g_value_take_string);
}

static inline gboolean
_remove_option (DonnaProviderConfig *config,
                const gchar *name,
                gboolean category)
{
    DonnaProviderConfigPrivate *priv;
    GNode *parent;
    GNode *node;
    const gchar *s;

    g_return_val_if_fail (DONNA_IS_PROVIDER_CONFIG (config), FALSE);
    g_return_val_if_fail (name != NULL, FALSE);

    if (*name == '/')
        ++name;

    priv = config->priv;
    g_rw_lock_writer_lock (&priv->lock);
    s = strrchr (name, '/');
    if (s)
    {
        parent = ensure_categories (config, name, s - name);
        if (!parent)
        {
            g_rw_lock_writer_unlock (&priv->lock);
            return FALSE;
        }
        ++s;
    }
    else
    {
        s = name;
        parent = priv->root;
    }
    node = get_child_node (parent, s, strlen (s));
    if (!node)
    {
        g_rw_lock_writer_unlock (&priv->lock);
        return FALSE;
    }
    if ((!category && option_is_category (node->data, priv->root))
            || (category && !option_is_category (node->data, priv->root)))
    {
        g_rw_lock_writer_unlock (&priv->lock);
        return FALSE;
    }

    /* actually remove the nodes/options */
    g_node_traverse (node, G_IN_ORDER, G_TRAVERSE_ALL, -1,
            (GNodeTraverseFunc) free_node_data_removing,
            priv->root);
    g_node_destroy (node);

    g_rw_lock_writer_unlock (&priv->lock);

    return TRUE;
}

gboolean
donna_config_remove_option (DonnaProviderConfig    *config,
                            const gchar            *name)
{
    return _remove_option (config, name, FALSE);
}

gboolean
donna_config_remove_category (DonnaProviderConfig    *config,
                              const gchar            *name)
{
    return _remove_option (config, name, TRUE);
}

/*** PROVIDER INTERFACE ***/

static gboolean
node_prop_refresher (DonnaTask   *task,
                     DonnaNode   *node,
                     const gchar *name)
{
    /* config is always up-to-date */
    return TRUE;
}

static DonnaTaskState
node_prop_setter (DonnaTask     *task,
                  DonnaNode     *node,
                  const gchar   *name,
                  const GValue  *value)
{
    DonnaProvider *provider;
    DonnaProviderConfigPrivate *priv;
    struct option *option;
    gchar *location;
    gboolean is_set_value;

    is_set_value = streq (name, "option-value");
    if (is_set_value || streq (name, "name"))
    {
        donna_node_get (node, FALSE,
                "provider", &provider,
                "location", &location,
                NULL);
        if (G_UNLIKELY (!DONNA_IS_PROVIDER_CONFIG (provider)))
        {
            const gchar *domain;

            donna_node_get (node, FALSE, "domain", &domain, NULL);
            g_warning ("Property setter of 'config' was called on a wrong node: '%s:%s'",
                    domain, location);

            donna_task_set_error (task, DONNA_NODE_ERROR,
                    DONNA_NODE_ERROR_OTHER,
                    "Wrong provider");
            g_object_unref (provider);
            g_free (location);
            return DONNA_TASK_FAILED;
        }
        priv = DONNA_PROVIDER_CONFIG (provider)->priv;

        g_rw_lock_writer_lock (&priv->lock);
        option = get_option (priv->root, location);
        if (!option)
        {
            g_warning ("Unable to find option '%s' while trying to change its value through the associated node",
                    location);

            donna_task_set_error (task, DONNA_NODE_ERROR,
                    DONNA_NODE_ERROR_NOT_FOUND,
                    "Option '%s' does not exists",
                    location);
            g_object_unref (provider);
            g_free (location);
            g_rw_lock_writer_unlock (&priv->lock);
            return DONNA_TASK_FAILED;
        }

        if (G_UNLIKELY (!G_VALUE_HOLDS (value,
                        (is_set_value) ? G_VALUE_TYPE (&option->value)
                        : G_TYPE_STRING)))
        {
            donna_task_set_error (task, DONNA_NODE_ERROR,
                    DONNA_NODE_ERROR_INVALID_TYPE,
                    (is_set_value) ? "Option '%s' is of type '%s', value passed is '%s'"
                    : "Property '%s' is of type '%s', value passed if '%s'",
                    (is_set_value) ? location : "name",
                    g_type_name ((is_set_value) ? G_VALUE_TYPE (&option->value)
                        : G_TYPE_STRING),
                    g_type_name (G_VALUE_TYPE (value)));
            g_object_unref (provider);
            g_free (location);
            g_rw_lock_writer_unlock (&priv->lock);
            return DONNA_TASK_FAILED;
        }

        if (is_set_value)
            /* set the new value */
            g_value_copy (value, &option->value);
        else
        {
            /* rename option */
            g_free (option->name);
            option->name = g_value_dup_string (value);
        }
        g_rw_lock_writer_unlock (&priv->lock);

        /* update the node */
        donna_node_set_property_value (node, (is_set_value) ? name : "name", value);

        g_object_unref (provider);
        g_free (location);
        return DONNA_TASK_DONE;
    }

    /* should never happened, since the only WRITABLE properties on our nodes
     * are the ones dealt with above */
    return DONNA_TASK_FAILED;
}

struct get_node_data
{
    DonnaProviderConfig *config;
    gchar               *location;
};

static void
free_get_node_data (struct get_node_data *data)
{
    g_object_unref (data->config);
    g_free (data->location);
    g_slice_free (struct get_node_data, data);
}

static void
node_toggle_ref_cb (DonnaProviderConfig *config,
                    DonnaNode           *node,
                    gboolean             is_last)
{
    /* we need to lock the config before we can lock the nodes. We need to lock
     * the nodes here in case this is triggered w/ is_last=TRUE while at the
     * same time (i.e. in another thread) there's a get_option_node that
     * happens.
     * See same stuff in provider-base.c for more */

    g_rw_lock_reader_lock (&config->priv->lock);
    g_rec_mutex_lock (&config->priv->nodes_mutex);
    if (is_last)
    {
        GNode *gnode;
        gchar *location;
        int c;

        c = donna_node_dec_toggle_count (node);
        if (c > 0)
        {
            g_rec_mutex_unlock (&config->priv->nodes_mutex);
            g_rw_lock_reader_unlock (&config->priv->lock);
            return;
        }
        donna_node_get (node, FALSE, "location", &location, NULL);
        gnode = get_option_node (config->priv->root, location);
        if (G_UNLIKELY (!gnode))
            g_warning ("Unable to find option '%s' while processing toggle_ref for the associated node",
                    location);
        else
            ((struct option *) gnode->data)->node = NULL;
        g_object_unref (node);
        g_free (location);
    }
    else
        donna_node_inc_toggle_count (node);

    g_rec_mutex_unlock (&config->priv->nodes_mutex);
    g_rw_lock_reader_unlock (&config->priv->lock);
}

/* assumes a lock on nodes_mutex */
static gboolean
ensure_option_has_node (DonnaProviderConfig *config,
                        gchar               *location,
                        struct option       *option)
{
    DonnaProviderConfigPrivate *priv = config->priv;

    if (!option->node)
    {
        /* we need to create the node */
        option->node = donna_node_new (DONNA_PROVIDER (config),
                location,
                (option->extra == priv->root) ? DONNA_NODE_CONTAINER : DONNA_NODE_ITEM,
                node_prop_refresher,
                node_prop_setter,
                option->name,
                NULL /* no icon */,
                DONNA_NODE_FULL_NAME_EXISTS | DONNA_NODE_NAME_WRITABLE);

        /* if an option, add some properties */
        if (option->extra != priv->root)
        {
            /* is this an extra? */
            if (option->extra)
            {
                GValue val = G_VALUE_INIT;

                g_value_init (&val, G_TYPE_STRING);
                g_value_set_string (&val, option->extra);
                donna_node_add_property (option->node,
                        "option-extra",
                        G_TYPE_STRING,
                        &val,
                        node_prop_refresher,
                        NULL /* no setter */,
                        NULL);
                g_value_unset (&val);
            }

            /* add the value of the option */
            donna_node_add_property (option->node,
                    "option-value",
                    G_VALUE_TYPE (&option->value),
                    &option->value,
                    node_prop_refresher,
                    node_prop_setter,
                    NULL);
        }

        /* add a toggleref, so when we have the last reference on the node, we
         * can let it go (Note: this adds a (strong) reference to node) */
        g_object_add_toggle_ref (G_OBJECT (option->node),
                (GToggleNotify) node_toggle_ref_cb,
                config);
        return TRUE;
    }
    else
        return FALSE;
}

static DonnaTaskState
return_option_node (DonnaTask *task, struct get_node_data *data)
{
    DonnaProviderConfigPrivate *priv;
    GNode *gnode;
    struct option *option;
    GValue *value;
    gboolean node_created = FALSE;

    priv = data->config->priv;

    g_rw_lock_reader_lock (&priv->lock);
    gnode = get_option_node (priv->root, data->location);
    if (!gnode)
    {
        donna_task_set_error (task, DONNA_PROVIDER_ERROR,
                DONNA_PROVIDER_ERROR_LOCATION_NOT_FOUND,
                "Option '%s' does not exists",
                data->location);
        g_rw_lock_reader_unlock (&priv->lock);
        free_get_node_data (data);
        return DONNA_TASK_FAILED;
    }
    option = gnode->data;

    g_rec_mutex_lock (&priv->nodes_mutex);
    node_created = ensure_option_has_node (data->config, data->location, option);
    g_rec_mutex_unlock (&priv->nodes_mutex);

    /* set node as return value */
    value = donna_task_grab_return_value (task);
    g_value_init (value, G_TYPE_OBJECT);
    if (node_created)
        /* if the node was just created, adding the toggle_ref took a strong
         * reference on it, so we just send this extra ref to the task */
        g_value_take_object (value, option->node);
    else
        g_value_set_object (value, option->node);
    donna_task_release_return_value (task);

    g_rw_lock_reader_unlock (&priv->lock);
    free_get_node_data (data);
    return DONNA_TASK_DONE;
}

static DonnaTask *
provider_config_get_node_task (DonnaProvider       *provider,
                               const gchar         *location)
{
    struct get_node_data *data;

    g_return_val_if_fail (DONNA_IS_PROVIDER_CONFIG (provider), NULL);

    data = g_slice_new0 (struct get_node_data);
    data->config = g_object_ref (provider);
    data->location = g_strdup (location);

    return donna_task_new ((task_fn) return_option_node, data,
            (GDestroyNotify) free_get_node_data);
}

struct node_children_data
{
    DonnaProviderConfig *config;
    DonnaNode           *node;
    DonnaNodeType        node_types;
    gboolean             is_getting_children;
};

static void
free_node_children_data (struct node_children_data *data)
{
    g_object_unref (data->node);
    g_slice_free (struct node_children_data, data);
}

static DonnaTaskState
node_children (DonnaTask *task, struct node_children_data *data)
{
    DonnaProviderConfigPrivate *priv;
    GNode *gnode;
    gchar *location;
    gsize len;
    gboolean want_item;
    gboolean want_categories;
    GValue *value;
    gboolean match;
    GPtrArray *arr;

    donna_node_get (data->node, FALSE, "location", &location, NULL);
    priv = data->config->priv;

    g_rw_lock_reader_lock (&priv->lock);
    gnode = get_option_node (priv->root, location);
    if (G_UNLIKELY (!gnode))
    {
        g_warning ("Unable to find option '%s' while processing has_children on the associated node",
                location);

        g_rw_lock_reader_unlock (&priv->lock);
        free_node_children_data (data);
        g_free (location);
        return DONNA_TASK_FAILED;
    }

    want_item = data->node_types & DONNA_NODE_ITEM;
    want_categories = data->node_types & DONNA_NODE_CONTAINER;

    if (data->is_getting_children)
    {
        arr = g_ptr_array_new ();
        len = strlen (location) + 2; /* 1 for NULL, 1 for trailing / */
    }

    for (gnode = gnode->children; gnode; gnode = gnode->next)
    {
        match = FALSE;
        if (option_is_category (gnode->data, priv->root))
        {
            if (want_categories)
                match = TRUE;
        }
        else if (want_item)
            match = TRUE;

        if (match)
        {
            if (data->is_getting_children)
            {
                struct option *option;
                gchar buf[255];
                gchar *s;

                option = gnode->data;
                if (len + strlen (option->name) > 255)
                {
                    s = g_strdup_printf ("%s/%s", location, option->name);
                }
                else
                {
                    s = buf;
                    sprintf (s, "%s/%s", location, option->name);
                }

                g_rec_mutex_lock (&priv->nodes_mutex);
                /* if node wasn't just created, we need to take a ref on it */
                if (!ensure_option_has_node (data->config, s, option))
                    g_object_ref (option->node);
                g_ptr_array_add (arr, option->node);
                g_rec_mutex_unlock (&priv->nodes_mutex);

                if (s != buf)
                    g_free (s);
            }
            else
                break;
        }
    }

    value = donna_task_grab_return_value (task);
    if (data->is_getting_children)
    {
        g_value_init (value, G_TYPE_PTR_ARRAY);
        g_value_take_boxed (value, arr);
    }
    else
    {
        g_value_init (value, G_TYPE_BOOLEAN);
        g_value_set_boolean (value, match);
    }
    donna_task_release_return_value (task);

    g_rw_lock_reader_unlock (&priv->lock);
    free_node_children_data (data);
    g_free (location);
    return DONNA_TASK_DONE;
}

static DonnaTask *
provider_config_has_node_children_task (DonnaProvider       *provider,
                                        DonnaNode           *node,
                                        DonnaNodeType        node_types)
{
    struct node_children_data *data;

    g_return_val_if_fail (DONNA_IS_PROVIDER_CONFIG (provider), NULL);

    data = g_slice_new0 (struct node_children_data);
    data->config     = DONNA_PROVIDER_CONFIG (provider);
    data->node       = g_object_ref (node);
    data->node_types = node_types;
    data->is_getting_children = FALSE;

    return donna_task_new ((task_fn) node_children, data,
            (GDestroyNotify) free_node_children_data);
}

static DonnaTask *
provider_config_get_node_children_task (DonnaProvider       *provider,
                                        DonnaNode           *node,
                                        DonnaNodeType        node_types)
{
    struct node_children_data *data;

    g_return_val_if_fail (DONNA_IS_PROVIDER_CONFIG (provider), NULL);

    data = g_slice_new0 (struct node_children_data);
    data->config     = DONNA_PROVIDER_CONFIG (provider);
    data->node       = g_object_ref (node);
    data->node_types = node_types;
    data->is_getting_children = TRUE;

    return donna_task_new ((task_fn) node_children, data,
            (GDestroyNotify) free_node_children_data);
}

static DonnaTaskState
node_remove_option (DonnaTask *task, DonnaNode *node)
{
    DonnaProvider *provider;
    gchar *location;
    DonnaNodeType node_type;
    gboolean ret;

    donna_node_get (node, FALSE,
            "provider",  &provider,
            "location",  &location,
            "node-type", &node_type,
            NULL);
    ret =_remove_option (DONNA_PROVIDER_CONFIG (provider), location,
            (node_type == DONNA_NODE_CONTAINER));
    g_free (location);
    g_object_unref (node);
    g_object_unref (provider);

    return (ret) ? DONNA_TASK_DONE : DONNA_TASK_FAILED;
}

static DonnaTask *
provider_config_remove_node_task (DonnaProvider       *provider,
                                  DonnaNode           *node)
{
    g_return_val_if_fail (DONNA_IS_PROVIDER_CONFIG (provider), NULL);

    return donna_task_new ((task_fn) node_remove_option,
            g_object_ref (node),
            g_object_unref);
}
