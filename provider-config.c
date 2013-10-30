
#define _GNU_SOURCE             /* strchrnul() in string.h */
#include <glib-object.h>
#include <stdlib.h>             /* atoi() */
#include <stdio.h>              /* sscanf() */
#include <string.h>
#include <ctype.h>              /* isblank() */
#include "provider-config.h"
#include "provider.h"
#include "conf.h"
#include "node.h"
#include "task.h"
#include "colorfilter.h"
#include "macros.h"
#include "debug.h"

enum
{
    PROP_0,

    PROP_APP,

    NB_PROPS
};

enum
{
    OPTION_SET,
    OPTION_DELETED,
    NB_SIGNALS
};

static guint donna_config_signals[NB_SIGNALS] = { 0 };

enum types
{
    TYPE_UNKNOWN = 0,
    TYPE_ENABLED,
    TYPE_DISABLED,
    TYPE_COMBINE,
    TYPE_IGNORE
};

struct parsed_data
{
    gchar               *name;
    gpointer             value;
    gchar               *comments;
    struct parsed_data  *next;
};

struct option
{
    /* name of the option */
    gchar       *name;
    /* comments from config file (to be exported) */
    gchar       *comments;
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
    DonnaApp        *app;
    /* to hold all common strings in config; i.e. option names */
    GStringChunk    *str_chunk;
    /* extra formats of options (list, list-int, etc) */
    GHashTable      *extras;
    /* root of config */
    GNode           *root;
    /* config lock */
    GRWLock          lock;
    /* a recursive mutex to handle (toggle ref) nodes. Should only be locked
     * after a lock on config (GRWLock above), reader is good enough */
    GRecMutex        nodes_mutex;
};

#define option_is_category(opt, root)    \
    (((struct option *) opt)->extra == root)

#define str_chunk(priv, string) \
    g_string_chunk_insert_const (priv->str_chunk, string)

static void             provider_config_set_property(GObject        *object,
                                                     guint           prop_id,
                                                     const GValue   *value,
                                                     GParamSpec     *pspec);
static void             provider_config_get_property(GObject        *object,
                                                     guint           prop_id,
                                                     GValue         *value,
                                                     GParamSpec     *pspec);
static void             provider_config_finalize    (GObject        *object);

/* DonnaProvider */
static const gchar *    provider_config_get_domain (
                                            DonnaProvider       *provider);
static DonnaProviderFlags provider_config_get_flags (
                                            DonnaProvider       *provider);
static gboolean         provider_config_get_node (
                                            DonnaProvider       *provider,
                                            const gchar         *location,
                                            gboolean            *is_node,
                                            gpointer            *ret,
                                            GError             **error);
static void             provider_config_unref_node (
                                            DonnaProvider       *provider,
                                            DonnaNode           *node);
static DonnaTask *      provider_config_has_node_children_task (
                                            DonnaProvider       *provider,
                                            DonnaNode           *node,
                                            DonnaNodeType        node_types,
                                            GError             **error);
static DonnaTask *      provider_config_get_node_children_task (
                                            DonnaProvider       *provider,
                                            DonnaNode           *node,
                                            DonnaNodeType        node_types,
                                            GError             **error);
static DonnaTask *      provider_config_trigger_node_task (
                                            DonnaProvider       *provider,
                                            DonnaNode           *node,
                                            GError             **error);
static DonnaTask *      provider_config_io_task (
                                            DonnaProvider       *provider,
                                            DonnaIoType          type,
                                            gboolean             is_source,
                                            GPtrArray           *sources,
                                            DonnaNode           *dest,
                                            const gchar         *new_name,
                                            GError             **error);


/* internal from provider-base.c */
gboolean _provider_base_set_property_icon (DonnaApp      *app,
                                           DonnaNode     *node,
                                           const gchar   *property,
                                           const gchar   *icon,
                                           GError       **error);

static gchar *get_option_full_name (GNode *root, GNode *gnode);
static void free_extra  (DonnaConfigExtra  *extra);

static void
provider_config_provider_init (DonnaProviderInterface *interface)
{
    interface->get_domain             = provider_config_get_domain;
    interface->get_flags              = provider_config_get_flags;
    interface->get_node               = provider_config_get_node;
    interface->unref_node             = provider_config_unref_node;
    interface->has_node_children_task = provider_config_has_node_children_task;
    interface->get_node_children_task = provider_config_get_node_children_task;
    interface->trigger_node_task      = provider_config_trigger_node_task;
    interface->io_task                = provider_config_io_task;
}

static void
donna_provider_config_class_init (DonnaProviderConfigClass *klass)
{
    GObjectClass *o_class;

    /* signals for the config manager */
    donna_config_signals[OPTION_SET] =
        g_signal_new ("option-set",
                DONNA_TYPE_PROVIDER_CONFIG,
                G_SIGNAL_DETAILED | G_SIGNAL_RUN_LAST,
                G_STRUCT_OFFSET (DonnaProviderConfigClass, option_set),
                NULL,
                NULL,
                g_cclosure_marshal_VOID__STRING,
                G_TYPE_NONE,
                1,
                G_TYPE_STRING);
    donna_config_signals[OPTION_DELETED] =
        g_signal_new ("option-deleted",
                DONNA_TYPE_PROVIDER_CONFIG,
                G_SIGNAL_DETAILED | G_SIGNAL_RUN_LAST,
                G_STRUCT_OFFSET (DonnaProviderConfigClass, option_deleted),
                NULL,
                NULL,
                g_cclosure_marshal_VOID__STRING,
                G_TYPE_NONE,
                1,
                G_TYPE_STRING);

    o_class = (GObjectClass *) klass;
    o_class->set_property = provider_config_set_property;
    o_class->get_property = provider_config_get_property;
    o_class->finalize       = provider_config_finalize;

    g_object_class_override_property (o_class, PROP_APP, "app");

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
    priv->str_chunk = g_string_chunk_new (1024);
    priv->extras = g_hash_table_new_full (g_str_hash, g_str_equal,
            NULL, (GDestroyNotify) free_extra);
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

static inline void
config_option_set (DonnaConfig *config, const gchar *name)
{
    g_return_if_fail (DONNA_IS_CONFIG (config));
    g_return_if_fail (name != NULL);

    g_signal_emit (config, donna_config_signals[OPTION_SET],
            g_quark_from_string (name), name);
}

static inline void
config_option_deleted (DonnaConfig *config, const gchar *name)
{
    g_return_if_fail (DONNA_IS_CONFIG (config));
    g_return_if_fail (name != NULL);

    g_signal_emit (config, donna_config_signals[OPTION_DELETED],
            g_quark_from_string (name), name);
}

static void
free_extra (DonnaConfigExtra *extra)
{
    if (!extra)
        return;
    g_free (extra->title);
    if (extra->type == DONNA_CONFIG_EXTRA_TYPE_LIST)
    {
        DonnaConfigExtraList **values;

        for (values = (DonnaConfigExtraList **) extra->values; *values; ++values)
        {
            g_free ((*values)->value);
            g_free ((*values)->label);
            g_slice_free (DonnaConfigExtraList, *values);
        }
    }
    else if (extra->type == DONNA_CONFIG_EXTRA_TYPE_LIST_INT)
    {
        DonnaConfigExtraListInt **values;

        for (values = (DonnaConfigExtraListInt **) extra->values; *values; ++values)
        {
            g_free ((*values)->in_file);
            g_free ((*values)->label);
            g_slice_free (DonnaConfigExtraListInt, *values);
        }
    }
    else if (extra->type == DONNA_CONFIG_EXTRA_TYPE_LIST_FLAGS)
    {
        DonnaConfigExtraListFlags **values;

        for (values = (DonnaConfigExtraListFlags **) extra->values; *values; ++values)
        {
            g_free ((*values)->in_file);
            g_free ((*values)->label);
            g_slice_free (DonnaConfigExtraListFlags, *values);
        }
    }
    else
        g_warning ("Config: free-ing invalid type of extra: %d", extra->type);

    g_slice_free (DonnaConfigExtra, extra);
}

struct removing_data
{
    DonnaProviderConfig *config;
    GPtrArray           *nodes;
};

static void
free_option (DonnaProviderConfig *config,
             struct option       *option,
             GPtrArray           *nodes)
{
    if (!option)
        return;
    g_free (option->comments);
    g_value_unset (&option->value);
    if (option->node)
    {
        if (nodes)
            /* add for later removal, outside of the lock */
            g_ptr_array_add (nodes, option->node);
        else
            g_object_unref (option->node);
    }
    g_slice_free (struct option, option);
}

static gboolean
free_node_data (GNode *node, DonnaProviderConfig *config)
{
    free_option (config, node->data, NULL);
    return FALSE;
}

static gboolean
free_node_data_removing (GNode *node, struct removing_data *data)
{
    free_option (data->config, node->data, data->nodes);
    return FALSE;
}

static void
provider_config_finalize (GObject *object)
{
    DonnaProviderConfigPrivate *priv;

    priv = DONNA_PROVIDER_CONFIG (object)->priv;

    g_object_unref (priv->app);
    g_string_chunk_free (priv->str_chunk);
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

static void
provider_config_set_property (GObject        *object,
                              guint           prop_id,
                              const GValue   *value,
                              GParamSpec     *pspec)
{
    if (G_LIKELY (prop_id == PROP_APP))
        ((DonnaProviderConfig *) object)->priv->app = g_value_dup_object (value);
    else
        G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
}

static void
provider_config_get_property (GObject        *object,
                              guint           prop_id,
                              GValue         *value,
                              GParamSpec     *pspec)
{
    if (G_LIKELY (prop_id == PROP_APP))
        g_value_set_object (value, ((DonnaProviderConfig *) object)->priv->app);
    else
        G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
}

static gchar *
str_chunk_len (DonnaProviderConfigPrivate   *priv,
               const gchar                  *string,
               gssize                        len)
{
    gchar  buf[255];
    gchar *b = buf;
    gchar *s;

    /* to easily use auto-numbered categories, we can send NULL as string, and
     * the number to print as len */
    if (!string)
        snprintf (buf, 255, "%d", (int) len);
    else if (len < 255)
        snprintf (buf, 255, "%.*s", (int) len, string);
    else
        b = g_strdup_printf ("%.*s", (int) len, string);

    s = g_string_chunk_insert_const (priv->str_chunk, b);
    if (b != buf)
        g_free (b);

    return s;
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
is_valid_name_len (const gchar *name, gsize len, gboolean is_section)
{
    gint is_first = 1;

    for ( ; *name != '\0' && len != 0; ++name, --len)
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
        for (c = 'A'; c <= 'Z'; ++c)
        {
            if (*name == c)
            {
                match = 1;
                break;
            }
        }
        for (c = '0'; !match && c <= '9'; ++c)
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

#define is_valid_name(name, is_section)  is_valid_name_len (name, -1, is_section)

static struct parsed_data *
parse_data (gchar **_data)
{
    gchar *data = *_data;
    struct parsed_data *first_section   = NULL;
    struct parsed_data *section         = NULL;
    struct parsed_data *option          = NULL;
    gint   eof  = 0;
    gint   line = 0;
    gint   skip = 0;
    gchar *s;
    gchar *e;
    gchar *eol;
    gchar *cmt = NULL;

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
        else if (eol == data)
        {
            if (!cmt)
                cmt = data;
            continue;
        }
        else
            *eol = '\0';

        if (*data == '[')
        {
            struct parsed_data *new_section;

            skip = 0;
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
            if (cmt)
            {
                new_section->comments = cmt;
                if (line > 1)
                    *(data - 2) = '\0';
                cmt = NULL;
            }
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
            gchar *c;

            /* check for comments */
            for (c = data; isblank (*c); ++c)
                ;
            if (*c == '#')
            {
                *eol = '\n';
                if (!cmt)
                    cmt = data;
                continue;
            }

            trim_line (&data);
            if (*data == '\0')
                continue;
            else if (cmt == data)
                cmt = NULL;

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
            if (cmt)
            {
                new_option->comments = cmt;
                if (line > 1)
                    *(data - 1) = '\0';
                cmt = NULL;
            }
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
    /* return any comments at the end of the file (so couldn't be assigned to
     * any section or option) */
    *_data = cmt;
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
donna_config_load_config_def (DonnaConfig *config, gchar *data)
{
    DonnaProviderConfigPrivate *priv;
    struct parsed_data *first_section;
    struct parsed_data *section;
    gchar *d = data;

    g_return_val_if_fail (DONNA_IS_PROVIDER_CONFIG (config), FALSE);
    priv = config->priv;

    first_section = parse_data (&d);
    if (!first_section)
    {
        g_free (data);
        return TRUE;
    }

    for (section = first_section ; section; section = section->next)
    {
        gchar *title;
        gchar *s;

        s = get_value (section->value, "type");
        if (!s)
        {
            g_warning ("Option type missing for config def of '%s'",
                    section->name);
            continue;
        }

        title = NULL;
        if (streq (s, "list"))
        {
            struct parsed_data *parsed;
            DonnaConfigExtra *extra;
            DonnaConfigExtraList **values;
            gint c;

            if (g_hash_table_contains (priv->extras, section->name))
            {
                g_warning ("Cannot redefine extra '%s'", section->name);
                continue;
            }

            values = g_new0 (DonnaConfigExtraList *,
                    get_count (section->value, "value") + 1);
            for (parsed = section->value, c = 0; parsed; parsed = parsed->next)
            {
                if (streq (parsed->name, "value"))
                {
                    DonnaConfigExtraList *v;
                    gchar *sep;

                    v = g_slice_new0 (DonnaConfigExtraList);
                    sep = strchr (parsed->value, ':');
                    if (sep)
                        *sep = '\0';
                    v->value = g_strdup (parsed->value);
                    if (sep)
                    {
                        *sep = ':';
                        v->label = g_strdup (sep + 1);
                    }

                    values[c++] = v;
                }
                else if (streq (parsed->name, "title"))
                    title = g_strdup (parsed->value);
                else if (!streq (parsed->name, "type"))
                    g_warning ("Invalid option '%s' in definition of %s '%s'",
                            parsed->name, s, section->name);
            }

            extra = g_new (DonnaConfigExtra, 1);
            extra->type = DONNA_CONFIG_EXTRA_TYPE_LIST;
            extra->title = title;
            extra->values = (gpointer) values;

            g_hash_table_insert (priv->extras,
                    str_chunk (priv, section->name),
                    extra);
        }
        else if (streq (s, "list-int"))
        {
            struct parsed_data *parsed;
            DonnaConfigExtra *extra;
            DonnaConfigExtraListInt **values;
            gint c;

            if (g_hash_table_contains (priv->extras, section->name))
            {
                g_warning ("Cannot redefine extra '%s'", section->name);
                continue;
            }

            values = g_new0 (DonnaConfigExtraListInt *,
                    get_count (section->value, "value") + 1);
            for (parsed = section->value, c = 0; parsed; parsed = parsed->next)
            {
                if (streq (parsed->name, "value"))
                {
                    DonnaConfigExtraListInt *v;
                    gchar *sep;

                    sep = strchr (parsed->value, ':');
                    if (!sep)
                    {
                        DonnaConfigExtraListInt **v;

                        g_warning ("Invalid format for value '%s' of extra '%s', "
                                "skipping entire definition",
                                (gchar *) parsed->value, section->name);
                        for (v = values; *v; ++v)
                        {
                            g_free ((*v)->in_file);
                            g_free ((*v)->label);
                            g_slice_free (DonnaConfigExtraListInt, *v);
                        }
                        g_free (values);
                        values = NULL;
                        break;
                    }

                    v = g_slice_new0 (DonnaConfigExtraListInt);
                    *sep = '\0';
                    sscanf (parsed->value, "%d", &v->value);
                    *sep = ':';
                    v->in_file = sep + 1;
                    sep = strchr (v->in_file, ':');
                    if (sep)
                    {
                        *sep = '\0';
                        v->in_file = g_strdup (v->in_file);
                        *sep = ':';
                        v->label = g_strdup (sep + 1);
                    }
                    else
                        v->in_file = g_strdup (v->in_file);

                    values[c++] = v;
                }
                else if (streq (parsed->name, "title"))
                    title = g_strdup (parsed->value);
                else if (!streq (parsed->name, "type"))
                    g_warning ("Invalid option '%s' in definition of %s '%s'",
                            parsed->name, s, section->name);
            }
            if (!values)
                continue;

            extra = g_new (DonnaConfigExtra, 1);
            extra->type = DONNA_CONFIG_EXTRA_TYPE_LIST_INT;
            extra->title = title;
            extra->values = (gpointer) values;

            g_hash_table_insert (priv->extras,
                    str_chunk (priv, section->name),
                    extra);
        }
        else if (streq (s, "list-flags"))
        {
            struct parsed_data *parsed;
            DonnaConfigExtra *extra;
            DonnaConfigExtraListFlags **values;
            gint c;

            if (g_hash_table_contains (priv->extras, section->name))
            {
                g_warning ("Cannot redefine extra '%s'", section->name);
                continue;
            }

            values = g_new0 (DonnaConfigExtraListFlags *,
                    get_count (section->value, "value") + 1);
            for (parsed = section->value, c = 0; parsed; parsed = parsed->next)
            {
                if (streq (parsed->name, "value"))
                {
                    DonnaConfigExtraListFlags *e;
                    gchar *sep;

                    sep = strchr (parsed->value, ':');
                    if (!sep)
                    {
                        DonnaConfigExtraListFlags **v;

                        g_warning ("Invalid format for value '%s' of extra '%s', "
                                "skipping entire definition",
                                (gchar *) parsed->value, section->name);
                        for (v = values; *v; ++v)
                        {
                            g_free ((*v)->in_file);
                            g_free ((*v)->label);
                            g_slice_free (DonnaConfigExtraListFlags, *v);
                        }
                        g_free (values);
                        values = NULL;
                        break;
                    }

                    e = g_slice_new0 (DonnaConfigExtraListFlags);
                    *sep = '\0';
                    sscanf (parsed->value, "%d", &e->value);
                    if (e->value == 0 || (e->value & (~e->value + 1)) != e->value)
                    {
                        DonnaConfigExtraListFlags **v;

                        g_warning ("Invalid value (%u) for extra '%s', only non-zero power of 2 are allowed, "
                                "skipping entire definition",
                                e->value, section->name);
                        g_slice_free (DonnaConfigExtraListFlags, e);
                        for (v = values; *v; ++v)
                        {
                            g_free ((*v)->in_file);
                            g_free ((*v)->label);
                            g_slice_free (DonnaConfigExtraListFlags, *v);
                        }
                        g_free (values);
                        values = NULL;
                        break;
                    }
                    *sep = ':';
                    e->in_file = sep + 1;
                    sep = strchr (e->in_file, ':');
                    if (sep)
                    {
                        *sep = '\0';
                        e->in_file = g_strdup (e->in_file);
                        *sep = ':';
                        e->label = g_strdup (sep + 1);
                    }
                    else
                        e->in_file = g_strdup (e->in_file);

                    values[c++] = e;
                }
                else if (streq (parsed->name, "title"))
                    title = g_strdup (parsed->value);
                else if (!streq (parsed->name, "type"))
                    g_warning ("Invalid option '%s' in definition of %s '%s'",
                            parsed->name, s, section->name);
            }
            if (!values)
                continue;

            extra = g_new (DonnaConfigExtra, 1);
            extra->type = DONNA_CONFIG_EXTRA_TYPE_LIST_FLAGS;
            extra->title = title;
            extra->values = (gpointer) values;

            g_hash_table_insert (priv->extras,
                    str_chunk (priv, section->name),
                    extra);
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
                    len)
                && ((struct option *) node->data)->name[len] == '\0')
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
            option->name = str_chunk_len (config->priv, NULL, i);
            option->extra = root;
            g_value_init (&option->value, G_TYPE_INT);
            g_value_set_int (&option->value, 1);
            node = g_node_append_data (parent, option);
        }
        else
        {
            s = strchrnul (name, '/');
            if (s == name)
            {
                struct option *option;
                gint i;

                /* sanity check: must be "//new_cat" (category must start with a
                 * lowercase letter) */
                if (!(s[1] >= 'a' && s[1] <= 'z'))
                    return NULL;

                /* this was a "//" i.e. create a category in the last
                 * auto-created category (within parent) */
                option = parent->data;
                i = g_value_get_int (&option->value) - 1;

                /* find the node that will be parent to our new category, i.e.
                 * the last auto-created category (within parent) */
                for (node = parent->children; node; node = node->next)
                    if (atoi (((struct option *) node->data)->name) == i)
                        break;
                if (G_UNLIKELY (!node))
                    return NULL;
                goto next;
            }

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

                if (!is_valid_name_len (name, s - name, TRUE))
                    return NULL;

                option = g_slice_new0 (struct option);
                option->name = str_chunk_len (config->priv, name, s - name);
                option->extra = root;
                /* category hold an index, next number to use for auto-creating
                 * sub-categories. (See above) */
                g_value_init (&option->value, G_TYPE_INT);
                g_value_set_int (&option->value, 1);
                node = g_node_append_data (parent, option);
            }
        }

next:
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
add_flag_value (DonnaConfigExtra *extra, gchar *str, gint *val)
{
    DonnaConfigExtraListFlags **v;

    for (v = (DonnaConfigExtraListFlags **) extra->values; *v; ++v)
        if (streq (str, (*v)->in_file))
        {
            *val += (*v)->value;
            return TRUE;
        }
    return FALSE;
}

static gboolean
get_extra_value (DonnaConfigExtra *extra, gchar *str, gpointer value)
{
    if (extra->type == DONNA_CONFIG_EXTRA_TYPE_LIST)
    {
        DonnaConfigExtraList **v;

        for (v = (DonnaConfigExtraList **) extra->values; *v; ++v)
            if (streq (str, (*v)->value))
            {
                * (gchar **) value = (*v)->value;
                return TRUE;
            }
    }
    else if (extra->type == DONNA_CONFIG_EXTRA_TYPE_LIST_INT)
    {
        DonnaConfigExtraListInt **v;

        for (v = (DonnaConfigExtraListInt **) extra->values; *v; ++v)
            if (streq (str, (*v)->in_file))
            {
                * (gint *) value = (*v)->value;
                return TRUE;
            }
    }
    else /* DONNA_CONFIG_EXTRA_TYPE_LIST_FLAGS */
    {
        gint val = 0;
        gchar *s;

        for (;;)
        {
            s = strchr (str, ',');
            if (s)
                *s = '\0';
            if (!add_flag_value (extra, str, &val))
            {
                if (s)
                    *s = ',';
                return FALSE;
            }
            if (s)
            {
                *s = ',';
                str = s + 1;
            }
            else
                break;
        }
        * (gint *) value = val;
        return TRUE;
    }
    return FALSE;
}

gboolean
donna_config_load_config (DonnaConfig *config, gchar *data)
{
    DonnaProviderConfigPrivate *priv;
    gchar *d = data;
    GNode *parent;
    struct parsed_data *first_section;
    struct parsed_data *section;
    GRegex *re_int;
    GRegex *re_double;

    g_return_val_if_fail (DONNA_IS_PROVIDER_CONFIG (config), FALSE);
    priv = config->priv;

    first_section = parse_data (&d);
    if (!first_section)
    {
        g_free (data);
        return TRUE;
    }
    /* store end-of-file comments */
    ((struct option *) priv->root->data)->comments = g_strdup (d);

    re_int      = g_regex_new ("^[+-]{0,1}[0-9]+$", G_REGEX_OPTIMIZE, 0, NULL);
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
            if (section->comments)
                ((struct option *) parent->data)->comments = g_strdup (section->comments);
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
                *s = '\0';
            if (get_child_node (parent, parsed->name, strlen (parsed->name)))
            {
                g_info ("Option '%s' in '%s' already defined, skipped",
                        parsed->name, section->name);
                if (s)
                    *s = ':';
                continue;
            }

            if (s)
            {
                DonnaConfigExtra *extra;

                extra = g_hash_table_lookup (priv->extras, ++s);
                if (!extra)
                {
                    g_warning ("Unknown extra format '%s' for option '%s' in '%s', skipped",
                            s, parsed->name, section->name);
                    *--s = ':';
                    continue;
                }

                option = g_slice_new0 (struct option);
                if (extra->type == DONNA_CONFIG_EXTRA_TYPE_LIST)
                {
                    gchar *v;

                    if (!get_extra_value (extra, parsed->value, &v))
                    {
                        g_warning ("Value for option '%s' isn't valid for extra '%s', skipped",
                                parsed->name, s);
                        *--s = ':';
                        g_slice_free (struct option, option);
                        continue;
                    }
                    g_value_init (&option->value, G_TYPE_STRING);
                    g_value_set_string (&option->value, v);
                }
                else if (extra->type == DONNA_CONFIG_EXTRA_TYPE_LIST_INT)
                {
                    gint v;

                    if (!get_extra_value (extra, parsed->value, &v))
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
                else /* DONNA_CONFIG_EXTRA_TYPE_LIST_FLAGS */
                {
                    gint v;

                    if (!get_extra_value (extra, parsed->value, &v))
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
                option->name = str_chunk (priv, parsed->name);
                option->comments = g_strdup (parsed->comments);
                option->extra = str_chunk (priv, s);
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
                    option->name = str_chunk (priv, parsed->name);
                    option->comments = g_strdup (parsed->comments);
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
                    option->name = str_chunk (priv, parsed->name);
                    option->comments = g_strdup (parsed->comments);
                    g_value_init (&option->value, G_TYPE_INT);
                    g_value_set_int (&option->value, v);
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
                    option->name = str_chunk (priv, parsed->name);
                    option->comments = g_strdup (parsed->comments);
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
                    option->name = str_chunk (priv, parsed->name);
                    option->comments = g_strdup (parsed->comments);
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
    g_regex_unref (re_double);

    free_parsed_data_section (first_section);
    g_free (data);
    return TRUE;
}

/*** EXPORTING CONFIGURATION ***/

/* assumes a (reader) lock on config */
static void
export_config (DonnaProviderConfigPrivate   *priv,
               GNode                        *node,
               GString                      *str_loc,
               GString                      *str,
               gboolean                      do_options)
{
    GNode *child;
    gboolean first = TRUE;

    for (child = node->children; child; child = child->next)
    {
        struct option *option = child->data;

        if (do_options)
        {
            /* skip categories */
            if (option->extra == priv->root)
                continue;

            if (first)
            {
                if (G_LIKELY (str_loc->len))
                    g_string_append_printf (str, "[%s]\n", str_loc->str);
                first = FALSE;
            }

            if (option->comments)
            {
                g_string_append (str, option->comments);
                g_string_append_c (str, '\n');
            }

            if (option->extra)
            {
                DonnaConfigExtra *extra;

                extra = g_hash_table_lookup (priv->extras, option->extra);
                if (G_UNLIKELY (!extra))
                {
                    gchar *fn = get_option_full_name (priv->root, child);
                    g_warning ("Failed to export option '%s': extra '%s' not found",
                            fn, (gchar *) option->extra);
                    g_free (fn);
                    continue;
                }

                if (extra->type == DONNA_CONFIG_EXTRA_TYPE_LIST)
                {
                    DonnaConfigExtraList **v;
                    const gchar *cur = g_value_get_string (&option->value);

                    for (v = (DonnaConfigExtraList **) extra->values;
                            *v; ++v)
                        if (streq (cur, (*v)->value))
                            break;
                    if (G_UNLIKELY (!v))
                    {
                        gchar *fn = get_option_full_name (priv->root, child);
                        g_warning ("Failed to export option '%s': value '%s' not found for extra '%s'",
                                fn, cur, (gchar *) option->extra);
                        g_free (fn);
                    }
                    else
                    {
                        if (streq (cur, "true") || streq (cur, "false")
                                || isblank (cur[0])
                                || (cur[0] != '\0' && isblank (cur[strlen (cur) - 1])))
                            g_string_append_printf (str, "%s:%s=\"%s\"\n",
                                    option->name,
                                    (gchar *) option->extra,
                                    cur);
                        else
                            g_string_append_printf (str, "%s:%s=%s\n",
                                    option->name,
                                    (gchar *) option->extra,
                                    cur);
                    }
                }
                else if (extra->type == DONNA_CONFIG_EXTRA_TYPE_LIST_INT)
                {
                    DonnaConfigExtraListInt **v;
                    gint cur = g_value_get_int (&option->value);

                    for (v = (DonnaConfigExtraListInt **) extra->values;
                            *v; ++v)
                        if (cur == (*v)->value)
                            break;
                    if (G_UNLIKELY (!v))
                    {
                        gchar *fn = get_option_full_name (priv->root, child);
                        g_warning ("Failed to export option '%s': value %d not found for extra '%s'",
                                fn, cur, (gchar *) option->extra);
                        g_free (fn);
                    }
                    else
                        g_string_append_printf (str, "%s:%s=%s\n",
                                option->name,
                                (gchar *) option->extra,
                                (*v)->in_file);
                }
                else /* DONNA_CONFIG_EXTRA_TYPE_LIST_FLAGS */
                {
                    DonnaConfigExtraListFlags **v;
                    gint cur = g_value_get_int (&option->value);

                    if (cur > 0)
                    {
                        GString *s_val;

                        /* 23 = random magic number */
                        s_val = g_string_sized_new (23);
                        for (v = (DonnaConfigExtraListFlags **) extra->values;
                                *v; ++v)
                        {
                            if (cur & (*v)->value)
                                g_string_append_printf (s_val, "%s,", (*v)->in_file);
                        }
                        /* remove trailing ',' */
                        g_string_truncate (s_val, s_val->len - 1);

                        g_string_append_printf (str, "%s:%s=%s\n",
                                option->name,
                                (gchar *) option->extra,
                                s_val->str);
                        g_string_free (s_val, TRUE);
                    }
                    else
                        g_string_append_printf (str, "%s:%s=""\n",
                                option->name,
                                (gchar *) option->extra);
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
                    default:
                        if (G_VALUE_TYPE (&option->value) == G_TYPE_STRING)
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
                        }
                        break;
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
            if (option->comments)
            {
                g_string_append (str, option->comments);
                g_string_append_c (str, '\n');
            }
            export_config (priv, child, str_loc, str, TRUE);
            g_string_erase (str_loc, len, -1);
        }
    }
    if (do_options)
        export_config (priv, node, str_loc, str, FALSE);
}

gchar *
donna_config_export_config (DonnaConfig *config)
{
    DonnaProviderConfigPrivate *priv;
    GString *str;
    GString *str_loc;

    g_return_val_if_fail (DONNA_IS_PROVIDER_CONFIG (config), NULL);
    priv = config->priv;
    str = g_string_sized_new (2048);
    str_loc = g_string_sized_new (42); /* random size, to hold section's name */

    g_rw_lock_reader_lock (&priv->lock);
    export_config (priv, priv->root, str_loc, str, TRUE);
    /* end-of-file comments */
    if (((struct option *) priv->root->data)->comments)
        g_string_append (str, ((struct option *) priv->root->data)->comments);
    g_rw_lock_reader_unlock (&priv->lock);

    g_string_free (str_loc, TRUE);
    return g_string_free (str, FALSE);
}

/*** ACCESSING CONFIGURATION ***/

const DonnaConfigExtra *
donna_config_get_extras (DonnaConfig            *config,
                         const gchar            *name,
                         GError                **error)
{
    DonnaProviderConfigPrivate *priv;
    DonnaConfigExtra *extra;

    g_return_val_if_fail (DONNA_IS_PROVIDER_CONFIG (config), NULL);
    priv = config->priv;

    extra = g_hash_table_lookup (priv->extras, name);
    if (!extra)
    {
        g_set_error (error, DONNA_PROVIDER_ERROR, DONNA_PROVIDER_ERROR_OTHER,
                "No extra '%s' found", name);
        return NULL;
    }

    return extra;
}

/* assumes reader lock on config */
static GNode *
get_option_node (GNode *root, const gchar *name)
{
    GNode *node;
    gchar *s;

    /* root (most likely, from provider, or to list options) */
    if (name[0] == '/' && name[1] == '\0')
        return root;

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

/* assumes reader lock on config */
static gchar *
get_option_full_name (GNode *root, GNode *gnode)
{
    GString *str = NULL;

    for (;;)
    {
        struct option *option = gnode->data;

        if (!gnode || gnode == root)
            break;

        if (str)
        {
            g_string_prepend_c (str, '/');
            g_string_prepend (str, option->name);
        }
        else
            str = g_string_new (option->name);

        gnode = gnode->parent;
    }

    g_string_prepend_c (str, '/');
    return g_string_free (str, FALSE);
}

static struct option *
_get_option (DonnaConfig *config,
             GType        type,
             gboolean     leave_lock_on,
             const gchar *fmt,
             va_list      va_arg)
{
    DonnaProviderConfigPrivate *priv;
    struct option *option;
    gchar  buf[255];
    gchar *b = buf;
    gint len;
    va_list va_arg2;
    gboolean ret;

    g_return_val_if_fail (DONNA_IS_PROVIDER_CONFIG (config), FALSE);
    g_return_val_if_fail (fmt != NULL, FALSE);

    va_copy (va_arg2, va_arg);
    len = vsnprintf (buf, 255, fmt, va_arg);
    if (len >= 255)
    {
        b = g_new (gchar, ++len); /* +1 for NUL */
        vsnprintf (b, len, fmt, va_arg2);
    }
    va_end (va_arg2);

    priv = config->priv;
    g_rw_lock_reader_lock (&priv->lock);
    option = get_option (priv->root, b);
    if (option)
    {
        /* G_TYPE_INVALID means we want a category */
        if (type == G_TYPE_INVALID)
            ret = option_is_category (option, priv->root);
        else if (!option_is_category (option, priv->root))
            ret = G_VALUE_HOLDS (&option->value, type);
        else
            ret = FALSE;
    }
    else
        ret = FALSE;

    /* allows caller to get the option value, then unlock */
    if (!leave_lock_on)
        g_rw_lock_reader_unlock (&priv->lock);

    if (b != buf)
        g_free (b);

    return (ret) ? option : NULL;
}

#define _has_opt(gtype)    do {             \
    struct option *option;                  \
    va_list va_arg;                         \
                                            \
    va_start (va_arg, fmt);                 \
    option = _get_option (config, gtype,    \
            FALSE, fmt, va_arg);            \
    va_end (va_arg);                        \
    return option != NULL;                  \
} while (0)

gboolean
donna_config_has_boolean (DonnaConfig *config,
                          const gchar *fmt,
                          ...)
{
    _has_opt (G_TYPE_BOOLEAN);
}

gboolean
donna_config_has_int (DonnaConfig *config,
                      const gchar *fmt,
                      ...)
{
    _has_opt (G_TYPE_INT);
}

gboolean
donna_config_has_double (DonnaConfig *config,
                         const gchar *fmt,
                         ...)
{
    _has_opt (G_TYPE_DOUBLE);
}

gboolean
donna_config_has_string (DonnaConfig *config,
                         const gchar *fmt,
                         ...)
{
    _has_opt (G_TYPE_STRING);
}

gboolean
donna_config_has_category (DonnaConfig *config,
                           const gchar *fmt,
                           ...)
{
    _has_opt (G_TYPE_INVALID /* i.e. category */);
}

#define _get_opt(gtype, get_fn)  do {                   \
    struct option *option;                              \
    va_list va_arg;                                     \
                                                        \
    va_start (va_arg, fmt);                             \
    option = _get_option (config, gtype, TRUE,          \
            fmt, va_arg);                               \
    va_end (va_arg);                                    \
    if (option)                                         \
        *value = get_fn (&option->value);               \
    g_rw_lock_reader_unlock (&config->priv->lock);      \
    return option != NULL;                              \
} while (0)

gboolean
donna_config_get_boolean (DonnaConfig    *config,
                          gboolean       *value,
                          const gchar    *fmt,
                          ...)
{
    _get_opt (G_TYPE_BOOLEAN, g_value_get_boolean);
}

gboolean
donna_config_get_int (DonnaConfig    *config,
                      gint           *value,
                      const gchar    *fmt,
                      ...)
{
    _get_opt (G_TYPE_INT, g_value_get_int);
}

gboolean
donna_config_get_double (DonnaConfig    *config,
                         gdouble        *value,
                         const gchar    *fmt,
                         ...)
{
    _get_opt (G_TYPE_DOUBLE, g_value_get_double);
}

gboolean
donna_config_get_string (DonnaConfig          *config,
                         gchar               **value,
                         const gchar          *fmt,
                         ...)
{
    _get_opt (G_TYPE_STRING, g_value_dup_string);
}

gboolean
donna_config_list_options (DonnaConfig               *config,
                           GPtrArray                **options,
                           DonnaConfigOptionType      type,
                           const gchar               *fmt,
                           ...)
{
    DonnaProviderConfigPrivate *priv;
    GNode *node;
    gchar  buf[255];
    gchar *b = buf;
    gint len;
    va_list va_arg;

    g_return_val_if_fail (DONNA_IS_PROVIDER_CONFIG (config), FALSE);
    g_return_val_if_fail (fmt != NULL, FALSE);
    g_return_val_if_fail (type != 0, FALSE);
    g_return_val_if_fail (options != NULL, FALSE);
    g_return_val_if_fail (*options == NULL, FALSE);

    priv = config->priv;

    va_start (va_arg, fmt);
    len = vsnprintf (buf, 255, fmt, va_arg);
    if (len >= 255)
    {
        b = g_new (gchar, ++len); /* +1 for NUL */
        va_end (va_arg);
        va_start (va_arg, fmt);
        vsnprintf (b, len, fmt, va_arg);
    }
    va_end (va_arg);

    g_rw_lock_reader_lock (&priv->lock);
    node = get_option_node (priv->root, b);
    if (node)
    {
        for (node = node->children; node; node = node->next)
        {
            if ((type & DONNA_CONFIG_OPTION_TYPE_BOTH) == DONNA_CONFIG_OPTION_TYPE_BOTH
                    || ((type & (DONNA_CONFIG_OPTION_TYPE_CATEGORY
                            | DONNA_CONFIG_OPTION_TYPE_NUMBERED))
                        && option_is_category (node->data, priv->root))
                    || (type & DONNA_CONFIG_OPTION_TYPE_OPTION
                        && !option_is_category (node->data, priv->root)))
            {
                if (type & DONNA_CONFIG_OPTION_TYPE_NUMBERED)
                {
                    const gchar *s = ((struct option *) node->data)->name;
                    if (*s < '0' || *s > '9')
                        continue;
                }

                if (!*options)
                    *options = g_ptr_array_new ();
                /* we can add option->name because it is in the GStringChunk,
                 * and therefore isn't going anywhere (even if the option is
                 * renamed or deleted) */
                g_ptr_array_add (*options, ((struct option *) node->data)->name);
            }
        }
    }
    g_rw_lock_reader_unlock (&priv->lock);

    if (b != buf)
        g_free (b);

    return (*options != NULL);
}

#define get_child_cat(opt_name, len_opt, dest)  do {                \
    child = get_child_node (node, opt_name, len_opt);               \
    if (!child || !option_is_category (child->data, priv->root))    \
        goto dest;                                                  \
    node = child;                                                   \
} while (0)

#define get_child_opt(opt_name, len_opt, opt_type, dest)  do {      \
    child = get_child_node (node, opt_name, len_opt);               \
    if (!child || option_is_category (child->data, priv->root))     \
        goto dest;                                                  \
                                                                    \
    v = &((struct option *) child->data)->value;                    \
    if (!G_VALUE_HOLDS (v, opt_type))                               \
        goto dest;                                                  \
} while (0)

static inline struct option *
__get_option (DonnaConfig   *config,
              GType          type,
              gboolean       leave_lock_on,
              const gchar   *fmt,
              ...)
{
    struct option *option;
    va_list va_arg;

    va_start (va_arg, fmt);
    option = _get_option (config, type, leave_lock_on, fmt, va_arg);
    va_end (va_arg);
    return option;
}

enum
{
    TREE_COL_TREE = 1,      /* mode tree, clicks */
    TREE_COL_LIST,          /* mode list */
    TREE_COL_LIST_SELECTED, /* mode list, selected */
};

static gboolean
_get_option_column (DonnaConfig  *config,
                    GType         type,
                    GValue       *value,
                    const gchar  *tv_name,
                    const gchar  *col_name,
                    const gchar  *arr_name,
                    const gchar  *def_cat,
                    const gchar  *opt_name,
                    guint         tree_col,
                    guint        *from)
{
    DonnaProviderConfigPrivate *priv = config->priv;
    GNode *node;
    GNode *child;
    GValue *v;
    gsize len_col = (col_name) ? strlen (col_name) : 0;
    gsize len_opt = strlen (opt_name);
    struct option *option;

    g_rw_lock_reader_lock (&priv->lock);

    if (tree_col == TREE_COL_TREE && arr_name)
    {
        node = priv->root;
        get_child_cat ("clicks", 6, treeview);
        get_child_cat (arr_name, strlen (arr_name), treeview);
        get_child_opt (opt_name, len_opt, type, treeview);
        goto get_value;
    }

    if (!arr_name || !col_name)
        goto treeview;
    node = get_option_node (priv->root, arr_name);
    if (!node)
        goto treeview;
    get_child_cat ("columns_options", 15, treeview);
    get_child_cat (col_name, len_col, treeview);
    if (tree_col == TREE_COL_LIST_SELECTED)
        get_child_cat ("selected", 8, treeview);
    get_child_opt (opt_name, len_opt, type, treeview);
    if (from)
        *from = _DONNA_CONFIG_COLUMN_FROM_ARRANGEMENT;
    goto get_value;

treeview:
    if (!tv_name)
        goto column;
    node = priv->root;
    get_child_cat ("treeviews", 9, column);
    get_child_cat (tv_name, strlen (tv_name), column);
    get_child_cat ("columns", 7, column);
    get_child_cat (col_name, len_col, column);
    if (tree_col == TREE_COL_LIST_SELECTED)
        get_child_cat ("selected", 8, treeview);
    get_child_opt (opt_name, len_opt, type, column);
    if (from)
        *from = _DONNA_CONFIG_COLUMN_FROM_TREE;
    goto get_value;

column:
    if (!col_name)
        /* implies is_tree_col, since then we can ask for the blank column */
        goto tree_col;
    node = priv->root;
    if (def_cat)
    {
        if (tree_col)
        {
            get_child_cat ("columns", 7, tree_col);
            get_child_cat (col_name, len_col, tree_col);
            if (tree_col == TREE_COL_LIST_SELECTED)
                get_child_cat ("selected", 8, tree_col);
            get_child_opt (opt_name, len_opt, type, tree_col);
        }
        else
        {
            get_child_cat ("columns", 7, def);
            get_child_cat (col_name, len_col, def);
            get_child_opt (opt_name, len_opt, type, def);
        }
        if (from)
            *from = _DONNA_CONFIG_COLUMN_FROM_COLUMN;
        goto get_value;
    }
    /* we need to check columns, and set the default if nothing found */
    g_rw_lock_reader_unlock (&priv->lock);
    option = __get_option (config, type, TRUE, "columns/%s/%s",
            col_name, opt_name);
    if (option)
    {
        g_value_copy (&option->value, value);
        if (from)
            *from = _DONNA_CONFIG_COLUMN_FROM_COLUMN;
    }
    g_rw_lock_reader_unlock (&priv->lock);
    return option != NULL;

tree_col:
    node = priv->root;
    get_child_cat ("treeviews", 9, def);
    get_child_cat (tv_name, strlen (tv_name), def);
    if (tree_col == TREE_COL_LIST_SELECTED)
        get_child_cat ("selected", 8, def);
    get_child_opt (opt_name, len_opt, type, def);
    goto get_value;

def:
    g_rw_lock_reader_unlock (&priv->lock);
    if (tree_col)
    {
        option = __get_option (config, type, TRUE, "defaults/%s/columns/%s/%s%s",
                def_cat,
                col_name,
                (tree_col == TREE_COL_LIST_SELECTED) ? "selected/" : "",
                opt_name);
        if (option)
        {
            g_value_copy (&option->value, value);
            g_rw_lock_reader_unlock (&priv->lock);
            return TRUE;
        }
        g_rw_lock_reader_unlock (&priv->lock);
    }
    option = __get_option (config, type, TRUE, "defaults/%s/%s%s",
            def_cat,
            (tree_col == TREE_COL_LIST_SELECTED) ? "selected/" : "",
            opt_name);
    if (option)
    {
        g_value_copy (&option->value, value);
        if (from)
            *from = _DONNA_CONFIG_COLUMN_FROM_DEFAULT;
    }
    g_rw_lock_reader_unlock (&priv->lock);
    return option != NULL;

get_value:
    g_value_copy (v, value);
    g_rw_lock_reader_unlock (&priv->lock);
    return TRUE;
}

#undef get_child_opt
#undef get_child_cat

#define _get_cfg_column(type, gtype, cfg_set, gvalue_get, dup_def, from) do { \
    GValue value = G_VALUE_INIT;                                    \
    type ret;                                                       \
                                                                    \
    g_value_init (&value, G_TYPE_##gtype);                          \
    if (!_get_option_column (config, G_TYPE_##gtype, &value,        \
                tv_name, col_name, arr_name,                        \
                def_cat, opt_name, 0, from))                        \
    {                                                               \
        g_value_unset (&value);                                     \
        donna_config_set_##cfg_set (config, def_val,                \
                (def_cat) ? "defaults/%s/%s" : "columns/%s/%s",     \
                (def_cat) ? def_cat : col_name,                     \
                opt_name);                                          \
        if (from)                                                   \
            *from = (def_cat) ? _DONNA_CONFIG_COLUMN_FROM_DEFAULT   \
                : _DONNA_CONFIG_COLUMN_FROM_COLUMN;                 \
        return dup_def (def_val);                                   \
    }                                                               \
    ret = g_value_##gvalue_get (&value);                            \
    g_value_unset (&value);                                         \
    return ret;                                                     \
} while (0)

gboolean
donna_config_get_boolean_column (DonnaConfig *config,
                                 const gchar *tv_name,
                                 const gchar *col_name,
                                 const gchar *arr_name,
                                 const gchar *def_cat,
                                 const gchar *opt_name,
                                 gboolean     def_val,
                                 guint       *from)
{
    _get_cfg_column (gboolean, BOOLEAN, boolean, get_boolean, , from);
}

gint
donna_config_get_int_column (DonnaConfig *config,
                             const gchar *tv_name,
                             const gchar *col_name,
                             const gchar *arr_name,
                             const gchar *def_cat,
                             const gchar *opt_name,
                             gint         def_val,
                             guint       *from)
{
    _get_cfg_column (gint, INT, int, get_int, , from);
}

gdouble
donna_config_get_double_column (DonnaConfig *config,
                                const gchar *tv_name,
                                const gchar *col_name,
                                const gchar *arr_name,
                                const gchar *def_cat,
                                const gchar *opt_name,
                                gdouble      def_val,
                                guint       *from)
{
    _get_cfg_column (gdouble, DOUBLE, double, get_double, , from);
}

gchar *
donna_config_get_string_column (DonnaConfig *config,
                                const gchar *tv_name,
                                const gchar *col_name,
                                const gchar *arr_name,
                                const gchar *def_cat,
                                const gchar *opt_name,
                                gchar       *def_val,
                                guint       *from)
{
    _get_cfg_column (gchar *, STRING, string, dup_string, g_strdup, from);
}

gchar *
_donna_config_get_string_tree_column (DonnaConfig   *config,
                                      const gchar   *tv_name,
                                      const gchar   *col_name,
                                      guint          tree_col,
                                      const gchar   *arr_name,
                                      const gchar   *def_cat,
                                      const gchar   *opt_name,
                                      gchar         *def_val)
{
    GValue value = G_VALUE_INIT;
    gchar *ret;

    g_return_val_if_fail (def_cat != NULL, NULL);

    g_value_init (&value, G_TYPE_STRING);
    if (!_get_option_column (config, G_TYPE_STRING, &value,
                tv_name, col_name, arr_name, def_cat, opt_name, tree_col, NULL))
    {
        g_value_unset (&value);
        if (!def_val)
            return NULL;
        donna_config_set_string (config, def_val, "defaults/%s/%s%s",
                def_cat,
                (tree_col == TREE_COL_LIST_SELECTED) ? "selected/" : "",
                opt_name);
        return g_strdup (def_val);
    }
    ret = g_value_dup_string (&value);
    g_value_unset (&value);
    return ret;
}

gboolean
_donna_config_get_boolean_tree_column (DonnaConfig   *config,
                                       const gchar   *tv_name,
                                       const gchar   *col_name,
                                       guint          tree_col,
                                       const gchar   *arr_name,
                                       const gchar   *def_cat,
                                       const gchar   *opt_name,
                                       gboolean      *ret)
{
    GValue value = G_VALUE_INIT;

    g_return_val_if_fail (def_cat != NULL, NULL);

    g_value_init (&value, G_TYPE_BOOLEAN);
    if (!_get_option_column (config, G_TYPE_BOOLEAN, &value,
                tv_name, col_name, arr_name, def_cat, opt_name, tree_col, NULL))
    {
        g_value_unset (&value);
        return FALSE;
    }
    *ret = g_value_get_boolean (&value);
    g_value_unset (&value);
    return TRUE;
}


#define get_node()  do {                                            \
    if (*fmt == '/')                                                \
        ++fmt;                                                      \
                                                                    \
    va_start (va_arg, fmt);                                         \
    len = vsnprintf (buf, 255, fmt, va_arg);                        \
    if (len >= 255)                                                 \
    {                                                               \
        b = g_new (gchar, ++len); /* +1 for NUL */                  \
        va_end (va_arg);                                            \
        va_start (va_arg, fmt);                                     \
        vsnprintf (b, len, fmt, va_arg);                            \
    }                                                               \
    va_end (va_arg);                                                \
                                                                    \
    g_rw_lock_reader_lock (&priv->lock);                            \
    node = get_option_node (priv->root, b);                         \
    if (!node || !option_is_category (node->data, priv->root))      \
        goto done;                                                  \
} while (0)

#define get_child(opt_name, len_opt, opt_type, is_req)  do {        \
    child = get_child_node (node, opt_name, len_opt);               \
    if (!child || option_is_category (child->data, priv->root))     \
    {                                                               \
        if (is_req)                                                 \
            goto done;                                              \
        else                                                        \
        {                                                           \
            child = NULL;                                           \
            break;                                                  \
        }                                                           \
    }                                                               \
                                                                    \
    value = &((struct option *) child->data)->value;                \
    if (!G_VALUE_HOLDS (value, G_TYPE_##opt_type))                  \
    {                                                               \
        if (is_req)                                                 \
            goto done;                                              \
        else                                                        \
            child = NULL;                                           \
    }                                                               \
} while (0)

gboolean
donna_config_arr_load_columns (DonnaConfig            *config,
                               DonnaArrangement       *arr,
                               const gchar            *fmt,
                               ...)
{
    DonnaProviderConfigPrivate *priv;
    va_list  va_arg;
    GNode   *node;
    GNode   *child;
    GValue  *value;
    gchar    buf[255], *b = buf;
    gsize    len;
    gboolean ret = FALSE;

    g_return_val_if_fail (DONNA_IS_CONFIG (config), FALSE);
    g_return_val_if_fail (arr != NULL, FALSE);
    g_return_val_if_fail (fmt != NULL, FALSE);

    priv = config->priv;

    /* sanity check */
    if (arr->flags & DONNA_ARRANGEMENT_HAS_COLUMNS)
        return FALSE;

    get_node ();

    get_child ("columns", 7, STRING, TRUE);
    ret = TRUE;
    arr->flags |= DONNA_ARRANGEMENT_HAS_COLUMNS;
    arr->columns = g_value_dup_string (value);

    get_child ("main_column", 11, STRING, FALSE);
    if (child)
        arr->main_column = g_value_dup_string (value);

    get_child ("columns_always", 14, BOOLEAN, TRUE);
    if (g_value_get_boolean (value))
        arr->flags |= DONNA_ARRANGEMENT_COLUMNS_ALWAYS;

done:
    g_rw_lock_reader_unlock (&priv->lock);
    if (b != buf)
        g_free (b);
    return ret;
}

gboolean
donna_config_arr_load_sort (DonnaConfig            *config,
                            DonnaArrangement       *arr,
                            const gchar            *fmt,
                            ...)
{
    DonnaProviderConfigPrivate *priv;
    va_list  va_arg;
    GNode   *node;
    GNode   *child;
    GValue  *value;
    gchar    buf[255], *b = buf;
    gsize    len;
    gboolean ret = FALSE;

    g_return_val_if_fail (DONNA_IS_CONFIG (config), FALSE);
    g_return_val_if_fail (arr != NULL, FALSE);
    g_return_val_if_fail (fmt != NULL, FALSE);

    priv = config->priv;

    /* sanity check */
    if (arr->flags & DONNA_ARRANGEMENT_HAS_SORT)
        return FALSE;

    get_node ();

    get_child ("sort_column", 11, STRING, TRUE);
    ret = TRUE;
    arr->flags |= DONNA_ARRANGEMENT_HAS_SORT;
    arr->sort_column = g_value_dup_string (value);

    get_child ("sort_order", 10, INT, FALSE);
    if (child)
        arr->sort_order = g_value_get_int (value);

    get_child ("sort_always", 11, BOOLEAN, TRUE);
    if (g_value_get_boolean (value))
        arr->flags |= DONNA_ARRANGEMENT_SORT_ALWAYS;

done:
    g_rw_lock_reader_unlock (&priv->lock);
    if (b != buf)
        g_free (b);
    return ret;
}

gboolean
donna_config_arr_load_second_sort (DonnaConfig            *config,
                                   DonnaArrangement       *arr,
                                   const gchar            *fmt,
                                   ...)
{
    DonnaProviderConfigPrivate *priv;
    va_list  va_arg;
    GNode   *node;
    GNode   *child;
    GValue  *value;
    gchar    buf[255], *b = buf;
    gsize    len;
    gboolean ret = FALSE;

    g_return_val_if_fail (DONNA_IS_CONFIG (config), FALSE);
    g_return_val_if_fail (arr != NULL, FALSE);
    g_return_val_if_fail (fmt != NULL, FALSE);

    priv = config->priv;

    /* sanity check */
    if (arr->flags & DONNA_ARRANGEMENT_HAS_SECOND_SORT)
        return FALSE;

    get_node ();

    get_child ("second_sort_column", 18, STRING, TRUE);
    ret = TRUE;
    arr->flags |= DONNA_ARRANGEMENT_HAS_SECOND_SORT;
    arr->second_sort_column = g_value_dup_string (value);

    get_child ("second_sort_order", 17, INT, FALSE);
    if (child)
        arr->second_sort_order = g_value_get_int (value);

    get_child ("second_sort_sticky", 18, BOOLEAN, FALSE);
    if (child)
        arr->second_sort_sticky = (g_value_get_boolean (value))
            ? DONNA_SECOND_SORT_STICKY_ENABLED
            : DONNA_SECOND_SORT_STICKY_DISABLED;

    get_child ("second_sort_always", 18, BOOLEAN, TRUE);
    if (g_value_get_boolean (value))
        arr->flags |= DONNA_ARRANGEMENT_SECOND_SORT_ALWAYS;

done:
    g_rw_lock_reader_unlock (&priv->lock);
    if (b != buf)
        g_free (b);
    return ret;
}

gboolean
donna_config_arr_load_columns_options (DonnaConfig          *config,
                                       DonnaArrangement       *arr,
                                       const gchar            *fmt,
                                       ...)
{
    DonnaProviderConfigPrivate *priv;
    va_list  va_arg;
    GNode   *node;
    GNode   *child;
    GValue  *value;
    gchar    buf[255], *b = buf;
    gsize    len;
    gboolean ret = FALSE;

    g_return_val_if_fail (DONNA_IS_CONFIG (config), FALSE);
    g_return_val_if_fail (arr != NULL, FALSE);
    g_return_val_if_fail (fmt != NULL, FALSE);

    priv = config->priv;

    /* sanity check */
    if (arr->flags & DONNA_ARRANGEMENT_HAS_COLUMNS_OPTIONS)
        return FALSE;

    get_node ();

    /* special case: we want this one to be a category */
    child = get_child_node (node, "columns_options", 15);
    if (!child || !option_is_category (child->data, priv->root))
        goto done;

    ret = TRUE;
    arr->flags |= DONNA_ARRANGEMENT_HAS_COLUMNS_OPTIONS;
    arr->columns_options = g_strdup (b);

    get_child ("columns_options_always", 22, BOOLEAN, TRUE);
    if (g_value_get_boolean (value))
        arr->flags |= DONNA_ARRANGEMENT_COLUMNS_OPTIONS_ALWAYS;

done:
    g_rw_lock_reader_unlock (&priv->lock);
    if (b != buf)
        g_free (b);
    return ret;
}

gboolean
donna_config_arr_load_color_filters (DonnaConfig            *config,
                                     DonnaApp               *app,
                                     DonnaArrangement       *arr,
                                     const gchar            *fmt,
                                     ...)
{
    DonnaProviderConfigPrivate *priv;
    va_list      va_arg;
    GNode       *node;
    GNode       *child;
    GValue      *value;
    gchar        buf[255], *b = buf;
    gsize        len;
    gboolean     ret = FALSE;
    enum types   type;

    g_return_val_if_fail (DONNA_IS_CONFIG (config), FALSE);
    g_return_val_if_fail (arr != NULL, FALSE);
    g_return_val_if_fail (fmt != NULL, FALSE);

    priv = config->priv;

    /* sanity check */
    if (arr->flags & DONNA_ARRANGEMENT_HAS_COLOR_FILTERS)
        return FALSE;

    get_node ();

    /* special case: we want this one to be a category */
    child = get_child_node (node, "color_filters", 13);
    if (!child || !option_is_category (child->data, priv->root))
        goto done;

    /* color filters are special, in that the option "type" defines whether or
     * not we load them, and also whether or not we set the flag
     * - enabled  : load; set flag
     * - disabled : set flag
     * - combine  : load
     * - ignore   : nothing
     */

    node = child;
    get_child ("type", 4, INT, FALSE);
    if (child)
        type = g_value_get_int (value);
    else
        /* default */
        type = TYPE_ENABLED;

    switch (type)
    {
        case TYPE_DISABLED:
            ret = TRUE;
            arr->flags |= DONNA_ARRANGEMENT_HAS_COLOR_FILTERS;
            goto done;
            break;

        case TYPE_ENABLED:
            ret = TRUE;
            arr->flags |= DONNA_ARRANGEMENT_HAS_COLOR_FILTERS;
            /* fall through */

        case TYPE_COMBINE:
            break;

        case TYPE_IGNORE:
            goto done;
            break;

        case TYPE_UNKNOWN:
        default:
            g_warning ("Invalid option 'type' for '%s/color_filters'", b);
            goto done;
    }

    /* only ENABLED and COMBINE reach here, to load color filters */

    if (arr->color_filters)
        arr->color_filters = g_slist_reverse (arr->color_filters);

    for (node = node->children; node; node = node->next)
    {
        struct option *option = node->data;
        DonnaColorFilter *cf;

        if (!option_is_category (option, priv->root)
                || streq (option->name, "type"))
            continue;

        get_child ("filter", 6, STRING, FALSE);
        if (!child)
            continue;

        cf = g_object_new (DONNA_TYPE_COLOR_FILTER,
                "app",      app,
                "filter",   g_value_get_string (value),
                NULL);

        get_child ("column", 6, STRING, FALSE);
        if (child)
            g_object_set (cf, "column", g_value_get_string (value), NULL);

        get_child ("keep_going", 10, BOOLEAN, FALSE);
        if (child && g_value_get_boolean (value))
            g_object_set (cf, "keep-going", TRUE, NULL);

        get_child ("via_treeview", 12, BOOLEAN, FALSE);
        if (child && !g_value_get_boolean (value))
            g_object_set (cf, "via-treeview", FALSE, NULL);

        /* all properties that we can set must be:
         * - supported by GtkCellRendererText
         * - listed in treeview, rend_func() (in order to reset the *-set
         *   properties before rendering, see there for more
         */

        get_child ("foreground", 10, STRING, FALSE);
        if (child)
            donna_color_filter_add_prop (cf, "foreground-set",
                    "foreground", value);
        else
        {
            get_child ("foreground-rgba", 15, STRING, FALSE);
            if (child)
                donna_color_filter_add_prop (cf, "foreground-set",
                        "foreground-rgba", value);
        }

        get_child ("background", 10, STRING, FALSE);
        if (child)
            donna_color_filter_add_prop (cf, "background-set",
                    "background", value);
        else
        {
            get_child ("background-rgba", 15, STRING, FALSE);
            if (child)
                donna_color_filter_add_prop (cf, "background-set",
                        "background-rgba", value);
        }

        get_child ("bold", 4, BOOLEAN, FALSE);
        if (child)
        {
            GValue v = G_VALUE_INIT;

            g_value_init (&v, G_TYPE_INT);
            g_value_set_int (&v, (g_value_get_boolean (value))
                        ? PANGO_WEIGHT_BOLD : PANGO_WEIGHT_NORMAL);
            donna_color_filter_add_prop (cf, "weight-set", "weight", &v);
            g_value_unset (&v);
        }

        get_child ("italic", 6, BOOLEAN, FALSE);
        if (child)
        {
            GValue v = G_VALUE_INIT;

            g_value_init (&v, G_TYPE_UINT);
            g_value_set_int (&v, (g_value_get_boolean (value))
                        ? PANGO_STYLE_ITALIC : PANGO_STYLE_NORMAL);
            donna_color_filter_add_prop (cf, "style-set", "style", &v);
            g_value_unset (&v);
        }

        arr->color_filters = g_slist_prepend (arr->color_filters, cf);
    }

    if (arr->color_filters)
        arr->color_filters = g_slist_reverse (arr->color_filters);

done:
    g_rw_lock_reader_unlock (&priv->lock);
    if (b != buf)
        g_free (b);
    return ret;
}

#undef get_node
#undef get_child

static gboolean
_set_option (DonnaConfig    *config,
             GType           type,
             GValue         *value,
             const gchar    *fmt,
             va_list         va_arg)
{
    DonnaProviderConfigPrivate *priv;
    GNode *parent;
    GNode *node;
    struct option *option;
    gchar  buf[255];
    gchar *b = buf;
    gchar *st;
    gint len;
    va_list va_arg2;
    const gchar *s;
    gboolean ret;

    g_return_val_if_fail (DONNA_IS_PROVIDER_CONFIG (config), FALSE);
    g_return_val_if_fail (fmt != NULL, FALSE);

    va_copy (va_arg2, va_arg);
    len = vsnprintf (buf, 255, fmt, va_arg);
    if (len >= 255)
    {
        b = g_new (gchar, ++len); /* +1 for NUL */
        vsnprintf (b, len, fmt, va_arg2);
    }
    va_end (va_arg2);

    st = (*b == '/') ? b + 1 : b;

    priv = config->priv;
    g_rw_lock_writer_lock (&priv->lock);
    s = strrchr (st, '/');
    if (s)
    {
        parent = ensure_categories (config, st, s - st);
        if (!parent)
        {
            g_rw_lock_writer_unlock (&priv->lock);
            return FALSE;
        }
        ++s;
    }
    else
    {
        s = st;
        parent = priv->root;
    }

    if (!is_valid_name ((gchar *) s, FALSE))
    {
        g_rw_lock_writer_unlock (&priv->lock);
        return FALSE;
    }

    node = get_child_node (parent, s, strlen (s));
    if (node)
    {
        option = node->data;
        ret = !option_is_category (option, priv->root)
            && G_VALUE_HOLDS (&option->value, type);
    }
    else
    {
        option = g_slice_new0 (struct option);
        option->name = str_chunk (priv, s);
        g_value_init (&option->value, type);
        g_node_append_data (parent, option);
        ret = TRUE;
    }
    if (ret)
        g_value_copy (value, &option->value);
    g_rw_lock_writer_unlock (&priv->lock);
    /* signal & set value on node after releasing the lock, to avoid
     * any deadlocks */
    if (ret)
    {
        config_option_set (DONNA_CONFIG (config), b);
        if (option->node)
            donna_node_set_property_value (option->node,
                    "option-value",
                    &option->value);
    }

    if (b != buf)
        g_free (b);

    return ret;
}

#define _set_opt(gtype, set_fn)  do {           \
    va_list va_arg;                             \
    GValue gvalue = G_VALUE_INIT;               \
    gboolean ret;                               \
                                                \
    va_start (va_arg, fmt);                     \
    g_value_init (&gvalue, gtype);              \
    set_fn (&gvalue, value);                    \
    ret = _set_option (config, gtype, &gvalue,  \
            fmt, va_arg);                       \
    g_value_unset (&gvalue);                    \
    va_end (va_arg);                            \
    return ret;                                 \
} while (0)

gboolean
donna_config_set_boolean (DonnaConfig   *config,
                          gboolean       value,
                          const gchar   *fmt,
                          ...)
{
    _set_opt (G_TYPE_BOOLEAN, g_value_set_boolean);
}

gboolean
donna_config_set_int (DonnaConfig   *config,
                      gint           value,
                      const gchar   *fmt,
                      ...)
{
    _set_opt (G_TYPE_INT, g_value_set_int);
}

gboolean
donna_config_set_double (DonnaConfig    *config,
                         gdouble         value,
                         const gchar    *fmt,
                         ...)
{
    _set_opt (G_TYPE_DOUBLE, g_value_set_double);
}

gboolean
donna_config_set_string (DonnaConfig         *config,
                         const gchar         *value,
                         const gchar         *fmt,
                         ...)
{
    _set_opt (G_TYPE_STRING, g_value_set_string);
}

gboolean
donna_config_take_string (DonnaConfig        *config,
                          gchar              *value,
                          const gchar        *fmt,
                          ...)
{
    _set_opt (G_TYPE_STRING, g_value_take_string);
}

static inline gboolean
_remove_option (DonnaProviderConfig *config,
                const gchar         *name,
                gboolean             category)
{
    DonnaProviderConfigPrivate *priv;
    GNode *parent;
    GNode *node;
    const gchar *s;
    struct removing_data data;
    guint i;

    g_return_val_if_fail (DONNA_IS_PROVIDER_CONFIG (config), FALSE);

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
    data.config = config;
    data.nodes  = g_ptr_array_new ();
    g_node_traverse (node, G_IN_ORDER, G_TRAVERSE_ALL, -1,
            (GNodeTraverseFunc) free_node_data_removing,
            &data);
    g_node_destroy (node);

    g_rw_lock_writer_unlock (&priv->lock);

    /* signals after releasing the lock, to avoid dead locks */
    /* config: we oly send one signal, e.g. only the category (no children) */
    config_option_deleted (DONNA_CONFIG (config), name);
    /* for provider: we must do it for all existing nodes, as it also serves as
     * a "destroy" i.e. to mean unref it, the node doesn't exist anymore */
    for (i = 0; i < data.nodes->len; ++i)
    {
        donna_provider_node_deleted (DONNA_PROVIDER (config),
                data.nodes->pdata[i]);
        /* we should be the only ref left, and can let it go now */
        g_object_unref (data.nodes->pdata[1]);
    }
    g_ptr_array_free (data.nodes, TRUE);

    return TRUE;
}

#define __remove_option(is_category)    do {                    \
    gchar  buf[255];                                            \
    gchar *b = buf;                                             \
    gint len;                                                   \
    va_list  va_arg;                                            \
    gboolean ret;                                               \
                                                                \
    g_return_val_if_fail (fmt != NULL, FALSE);                  \
                                                                \
    if (*fmt == '/')                                            \
        ++fmt;                                                  \
                                                                \
    len = vsnprintf (buf, 255, fmt, va_arg);                    \
    if (len >= 255)                                             \
    {                                                           \
        b = g_new (gchar, ++len); /* +1 for NUL */              \
        va_end (va_arg);                                        \
        va_start (va_arg, fmt);                                 \
        vsnprintf (b, len, fmt, va_arg);                        \
    }                                                           \
                                                                \
    ret = _remove_option (config, b, is_category);              \
                                                                \
    if (b != buf)                                               \
        g_free (b);                                             \
                                                                \
    return ret;                                                 \
} while (0)

gboolean
donna_config_remove_option (DonnaConfig    *config,
                            const gchar    *fmt,
                            ...)
{
    __remove_option (FALSE);
}

gboolean
donna_config_remove_category (DonnaConfig    *config,
                              const gchar    *fmt,
                              ...)
{
    __remove_option (TRUE);
}

/*** PROVIDER INTERFACE ***/

static const gchar *
provider_config_get_domain (DonnaProvider *provider)
{
    g_return_val_if_fail (DONNA_IS_PROVIDER_CONFIG (provider), NULL);
    return "config";
}

static DonnaProviderFlags
provider_config_get_flags (DonnaProvider *provider)
{
    g_return_val_if_fail (DONNA_IS_PROVIDER_CONFIG (provider),
            DONNA_PROVIDER_FLAG_INVALID);
    return 0;
}

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
    GNode *gnode;
    struct option *option;
    GValue v = G_VALUE_INIT;
    gchar *location;
    gchar *fl;
    gchar *old;
    gboolean is_set_value;

    is_set_value = streq (name, "option-value");
    if (is_set_value || streq (name, "name"))
    {
        provider = donna_node_peek_provider (node);
        location = donna_node_get_location (node);
        if (G_UNLIKELY (!DONNA_IS_PROVIDER_CONFIG (provider)))
        {
            g_warning ("Property setter of 'config' was called on a wrong node: '%s:%s'",
                    donna_node_get_domain (node), location);
            g_free (location);
            return DONNA_TASK_FAILED;
        }
        priv = DONNA_PROVIDER_CONFIG (provider)->priv;

        g_rw_lock_writer_lock (&priv->lock);
        gnode = get_option_node (priv->root, location);
        if (!gnode)
        {
            g_critical ("Unable to find option '%s' while trying to change its value through the associated node",
                    location);

            donna_task_set_error (task, DONNA_PROVIDER_ERROR,
                    DONNA_PROVIDER_ERROR_LOCATION_NOT_FOUND,
                    "Option '%s' does not exists",
                    location);
            g_free (location);
            g_rw_lock_writer_unlock (&priv->lock);
            return DONNA_TASK_FAILED;
        }
        option = (struct option *) gnode->data;

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
            g_free (location);
            g_rw_lock_writer_unlock (&priv->lock);
            return DONNA_TASK_FAILED;
        }

        if (is_set_value)
            /* set the new value */
            g_value_copy (value, &option->value);
        else
        {
            gchar *s;

            /* rename option */

            s = (gchar *) g_value_get_string (value);
            if (!is_valid_name (s, option_is_category (option, priv->root)))
            {
                donna_task_set_error (task, DONNA_NODE_ERROR,
                        DONNA_NODE_ERROR_OTHER,
                        "Cannot rename '%s' to '%s': Invalid name",
                        location, s);
                g_free (location);
                g_rw_lock_writer_unlock (&priv->lock);
                return DONNA_TASK_FAILED;
            }
            old = get_option_full_name (priv->root, gnode);
            option->name = str_chunk (priv, s);
        }
        g_rw_lock_writer_unlock (&priv->lock);

        if (!is_set_value)
        {
            config_option_deleted ((DonnaConfig *) provider, old);
            g_free (old);
        }
        fl = get_option_full_name (priv->root, gnode);
        config_option_set ((DonnaConfig *) provider, fl);

        /* update the node */
        g_value_init (&v, G_TYPE_STRING);
        g_value_take_string (&v, fl);
        donna_node_set_property_value (node, "location", &v);
        g_value_unset (&v);
        donna_node_set_property_value (node, name, value);

        g_free (location);
        return DONNA_TASK_DONE;
    }

    /* should never happened, since the only WRITABLE properties on our nodes
     * are the ones dealt with above */
    return DONNA_TASK_FAILED;
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
        location = donna_node_get_location (node);
        gnode = get_option_node (config->priv->root, location);
        if (G_UNLIKELY (!gnode))
            g_critical ("Unable to find option '%s' while processing toggle_ref for the associated node",
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

/* assumes a reader lock on config; will lock/unlock nodes_mutex as needed */
static gboolean
ensure_option_has_node (DonnaProviderConfig *config,
                        gchar               *location,
                        struct option       *option)
{
    DonnaProviderConfigPrivate *priv = config->priv;

    g_rec_mutex_lock (&priv->nodes_mutex);
    if (!option->node)
    {
        /* we need to create the node */
        option->node = donna_node_new (DONNA_PROVIDER (config),
                location,
                (option->extra == priv->root) ? DONNA_NODE_CONTAINER : DONNA_NODE_ITEM,
                NULL, /* filename */
                node_prop_refresher,
                node_prop_setter,
                option->name,
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

        g_rec_mutex_unlock (&priv->nodes_mutex);

        /* set icon */
        if (option->extra != priv->root
                || (location[0] == '/' && location [1] == '\0'))
            /* takes care of handling GTK in the main/UI thread */
            _provider_base_set_property_icon (priv->app, option->node, "icon",
                    (option->extra != priv->root)
                    ? "document-properties" : "preferences-desktop", NULL);

        /* have provider emit the new_node signal */
        donna_provider_new_node (DONNA_PROVIDER (config), option->node);

        /* mark node ready */
        donna_node_mark_ready (option->node);

        return TRUE;
    }
    else
    {
        g_object_ref (option->node);
        g_rec_mutex_unlock (&priv->nodes_mutex);
        return FALSE;
    }
}

static gboolean
provider_config_get_node (DonnaProvider       *provider,
                          const gchar         *location,
                          gboolean            *is_node,
                          gpointer            *ret,
                          GError             **error)
{
    DonnaProviderConfigPrivate *priv;
    GNode *gnode;
    struct option *option;

    g_return_val_if_fail (DONNA_IS_PROVIDER_CONFIG (provider), FALSE);
    priv = ((DonnaProviderConfig *) provider)->priv;

    g_rw_lock_reader_lock (&priv->lock);
    gnode = get_option_node (priv->root, location);
    if (!gnode)
    {
        g_rw_lock_reader_unlock (&priv->lock);
        g_set_error (error, DONNA_PROVIDER_ERROR,
                DONNA_PROVIDER_ERROR_LOCATION_NOT_FOUND,
                "Option '%s' does not exists",
                location);
        return FALSE;
    }
    option = gnode->data;
    /* root doesn't have a name, but still needs one */
    if (gnode == priv->root && !option->name)
        option->name = g_strdup ("Configuration");

    ensure_option_has_node ((DonnaProviderConfig *) provider, (gchar *) location, option);

    *is_node = TRUE;
    /* either the node was just created, and adding the toggle_ref took a strong
     * reference on it, or we added one in ensure_option_has_node() */
    *ret = option->node;

    g_rw_lock_reader_unlock (&priv->lock);
    return TRUE;
}

static void
provider_config_unref_node (DonnaProvider       *provider,
                            DonnaNode           *node)
{
    /* node_toggle_ref_cb() will remove the node from our hashmap, and as such
     * remove our reference to the node (amongst other things), but since we
     * want to actually remove the toggle_ref, we need to add a ref to node here
     * (that will be removed right after, in node_toggle_ref_cb()) */
    node_toggle_ref_cb ((DonnaProviderConfig *) provider, g_object_ref (node), TRUE);
    /* this will remove our actual (strong) ref to the node, as well the
     * toggle_ref */
    g_object_remove_toggle_ref (G_OBJECT (node),
            (GToggleNotify) node_toggle_ref_cb,
            provider);
}

struct node_children_data
{
    DonnaProviderConfig *config;
    DonnaNode           *node;
    DonnaNodeType        node_types;
    GPtrArray           *children;  /* NULL for has_children */
    gint                 ref_count;
};

static void
free_node_children_data (struct node_children_data *data)
{
    if (g_atomic_int_dec_and_test (&data->ref_count))
    {
        g_object_unref (data->node);
        if (data->children)
            /* array might not yet be free-d, if the caller of the get_children
             * task is still using it. It will then be free-d when the task's
             * return value is unset, when the task is finalized */
            g_ptr_array_unref (data->children);
        g_slice_free (struct node_children_data, data);
    }
}

static gboolean
emit_node_children (struct node_children_data *data)
{
    donna_provider_node_children (DONNA_PROVIDER (data->config),
            data->node,
            data->node_types,
            data->children);
    free_node_children_data (data);
    return FALSE;
}

static DonnaTaskState
node_children (DonnaTask *task, struct node_children_data *data)
{
    DonnaProviderConfigPrivate *priv;
    GNode *gnode;
    gboolean is_root;
    gchar *location;
    gsize len;
    gboolean want_item;
    gboolean want_categories;
    GValue *value;
    gboolean match;

    location = donna_node_get_location (data->node);
    priv = data->config->priv;

    g_rw_lock_reader_lock (&priv->lock);
    gnode = get_option_node (priv->root, location);
    if (G_UNLIKELY (!gnode))
    {
        g_critical ("Unable to find option '%s' while processing has_children on the associated node",
                location);

        g_rw_lock_reader_unlock (&priv->lock);
        free_node_children_data (data);
        g_free (location);
        return DONNA_TASK_FAILED;
    }

    is_root = gnode == priv->root;
    want_item = data->node_types & DONNA_NODE_ITEM;
    want_categories = data->node_types & DONNA_NODE_CONTAINER;

    if (data->children)
        /* 2 == 1 for NULL, 1 for trailing / */
        len = strlen (location) + 2;

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
            if (data->children)
            {
                struct option *option;
                gchar buf[255];
                gchar *s;

                option = gnode->data;
                if (len + strlen (option->name) > 255)
                    s = g_strdup_printf ("%s/%s",
                            (is_root) ? "" : location, option->name);
                else
                {
                    s = buf;
                    sprintf (s, "%s/%s", (is_root) ? "" : location, option->name);
                }

                ensure_option_has_node (data->config, s, option);
                g_ptr_array_add (data->children, option->node);

                if (s != buf)
                    g_free (s);
            }
            else
                break;
        }
    }

    value = donna_task_grab_return_value (task);
    if (data->children)
    {
        /* set task's return value */
        g_value_init (value, G_TYPE_PTR_ARRAY);
        /* take its own ref on the array, as ours goes to the signal below */
        g_value_set_boxed (value, data->children);

        /* take a ref on data */
        g_atomic_int_inc (&data->ref_count);
        /* and emit the node_children signal in the main thread */
        g_idle_add_full (G_PRIORITY_DEFAULT_IDLE,
                (GSourceFunc) emit_node_children,
                data,
                (GDestroyNotify) free_node_children_data);
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
                                        DonnaNodeType        node_types,
                                        GError             **error)
{
    DonnaTask *task;
    struct node_children_data *data;

    g_return_val_if_fail (DONNA_IS_PROVIDER_CONFIG (provider), NULL);

    data = g_slice_new0 (struct node_children_data);
    data->config     = DONNA_PROVIDER_CONFIG (provider);
    data->node       = g_object_ref (node);
    data->node_types = node_types;
    data->ref_count  = 1;

    task = donna_task_new ((task_fn) node_children, data,
            (GDestroyNotify) free_node_children_data);
    donna_task_set_visibility (task, DONNA_TASK_VISIBILITY_INTERNAL_FAST);

    DONNA_DEBUG (TASK,
            gchar *location = donna_node_get_location (node);
            donna_task_take_desc (task, g_strdup_printf (
                    "has_children() for node '%s:%s'",
                    donna_node_get_domain (node),
                    location));
            g_free (location));

    return task;
}

static DonnaTask *
provider_config_get_node_children_task (DonnaProvider       *provider,
                                        DonnaNode           *node,
                                        DonnaNodeType        node_types,
                                        GError             **error)
{
    DonnaTask *task;
    struct node_children_data *data;

    g_return_val_if_fail (DONNA_IS_PROVIDER_CONFIG (provider), NULL);

    data = g_slice_new0 (struct node_children_data);
    data->config     = DONNA_PROVIDER_CONFIG (provider);
    data->node       = g_object_ref (node);
    data->node_types = node_types;
    data->children   = g_ptr_array_new_with_free_func (g_object_unref);
    data->ref_count  = 1;

    task = donna_task_new ((task_fn) node_children, data,
            (GDestroyNotify) free_node_children_data);
    donna_task_set_visibility (task, DONNA_TASK_VISIBILITY_INTERNAL_FAST);

    DONNA_DEBUG (TASK,
            gchar *location = donna_node_get_location (node);
            donna_task_take_desc (task, g_strdup_printf (
                    "get_children() for node '%s:%s'",
                    donna_node_get_domain (node),
                    location));
            g_free (location));

    return task;
}

static DonnaTaskState
node_remove_options (DonnaTask *task, GPtrArray *arr)
{
    DonnaProviderConfig *pcfg;
    gboolean ret = TRUE;
    guint i;

    pcfg = (DonnaProviderConfig *) donna_node_peek_provider (arr->pdata[0]);

    for (i = 0; i < arr->len; ++i)
    {
        DonnaNode *node = arr->pdata[i];
        gchar *location;

        location = donna_node_get_location (node);
        if (donna_node_get_node_type (node) != DONNA_NODE_CONTAINER)
        {
            if (!donna_config_remove_option (pcfg, location))
                ret = FALSE;
        }
        else
        {
            if (!donna_config_remove_category (pcfg, location))
                ret = FALSE;
        }
        g_free (location);
    }

    g_ptr_array_unref (arr);
    return (ret) ? DONNA_TASK_DONE : DONNA_TASK_FAILED;
}

static DonnaTask *
provider_config_io_task (DonnaProvider       *provider,
                         DonnaIoType          type,
                         gboolean             is_source,
                         GPtrArray           *sources,
                         DonnaNode           *dest,
                         const gchar         *new_name,
                         GError             **error)
{
    DonnaTask *task;

    if (type != DONNA_IO_DELETE)
    {
        g_set_error (error, DONNA_PROVIDER_ERROR,
                DONNA_PROVIDER_ERROR_NOT_SUPPORTED,
                "Provider 'config': Copy/Move operations not supported");
        return NULL;
    }

    task = donna_task_new ((task_fn) node_remove_options,
            g_ptr_array_ref (sources),
            (GDestroyNotify) g_ptr_array_unref);
    donna_task_set_visibility (task, DONNA_TASK_VISIBILITY_INTERNAL_FAST);

    DONNA_DEBUG (TASK,
            donna_task_take_desc (task, g_strdup_printf (
                    "config_io_task() to remove %d option(s)",
                    sources->len)));

    return task;
}

static DonnaTask *
provider_config_trigger_node_task (DonnaProvider       *provider,
                                   DonnaNode           *node,
                                   GError             **error)
{
    g_set_error (error, DONNA_PROVIDER_ERROR, DONNA_PROVIDER_ERROR_OTHER,
            "Options cannot be triggered -- What would it even do?");
    return NULL;
}
