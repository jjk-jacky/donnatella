
#include <glib-object.h>
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

struct extra
{
    enum extra_type   type;
    /* NULL-terminated array of values to present as choices. LIST_INT should be
     * of form "x:string" where x is the integer value */
    gchar           **values;
};

struct group
{
    /* name of the option/group, because the key in the hashtable is child */
    gchar       *option;
    /* name of the option to use when listing children. Defaults to name */
    gchar       *name;
    /* if TRUE we don't use name above, and use a dynamic counter */
    gboolean     is_indexed;
};

struct option
{
    /* priv->groups if a group; NULL for standard (bool, int, etc) types, else
     * the key to search for in priv->extras */
    gpointer     extra;
    /* the actual value, or a pointer to the struct group if extra ==
     * priv->groups */
    GValue       value;
    /* if the node is loaded, else NULL */
    DonnaNode   *node;
};

struct _DonnaProviderConfigPrivate
{
    /* extra formats of options (list, list-int, etc) */
    GHashTable  *extras;
    /* groups are a block of options making an "object" : columns, etc */
    GHashTable  *groups;
    /* all existing options */
    GHashTable  *options;
};

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
static void free_group  (struct group  *group);
static void free_option (struct option *option);

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

    priv = provider->priv = G_TYPE_INSTANCE_GET_PRIVATE (provider,
            DONNA_TYPE_PROVIDER_CONFIG,
            DonnaProviderConfigPrivate);
    priv->extras = g_hash_table_new_full (g_str_hash, g_str_equal,
            g_free, (GDestroyNotify) free_extra);
    priv->groups = g_hash_table_new_full (g_str_hash, g_str_equal,
            g_free, (GDestroyNotify) free_group);
    priv->options = g_hash_table_new_full (g_str_hash, g_str_equal,
            g_free, (GDestroyNotify) free_option);
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
    g_strfreev (extra->values);
    g_free (extra);
}

static void
free_group (struct group *group)
{
    if (!group)
        return;
    g_free (group->name);
    g_free (group->option);
    g_free (group);
}

static void
free_option (struct option *option)
{
    if (!option)
        return;
    g_free (option->extra);
    g_object_unref (option->node);
    g_free (option);
}

static void
provider_config_finalize (GObject *object)
{
    DonnaProviderConfigPrivate *priv;

    priv = DONNA_PROVIDER_CONFIG (object)->priv;

    g_hash_table_destroy (priv->extras);
    g_hash_table_destroy (priv->groups);
    g_hash_table_destroy (priv->options);

    /* chain up */
    G_OBJECT_CLASS (donna_provider_config_parent_class)->finalize (object);
}

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
is_valid_name (gchar *name)
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
        if (match || *name == '-' || *name == '_' || *name == ' ' || *name == ':')
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

            e = strchr (data, ']');
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
            if (!is_valid_name (data))
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
            size_t len;

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
            if (!is_valid_name (data))
            {
                g_warning ("Invalid option name (%s) at line %d, "
                        "skipping to next line", data, line);
                continue;
            }
            new_option = g_slice_new0 (struct parsed_data);
            new_option->name = data;
            ++s;
            trim_line (&s);
            /* handle double-quoted values (i.e. remove quotes) */
            len = strlen (s);
            if (len > 0 && *s == '"' && s[--len] == '"')
            {
                s[len] = '\0';
                ++s;
            }
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

        if (streq (s, "list") || streq (s, "list-int"))
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
            extra->type = (streq (s, "list")) ? EXTRA_TYPE_LIST
                : EXTRA_TYPE_LIST_INT;
            extra->values = values;

            g_hash_table_insert (priv->extras, g_strdup (section->name), extra);
        }
        else if (streq (s, "group"))
        {
            struct parsed_data *parsed;
            struct group *group;
            struct option *option;
            gchar *child = NULL;

            if (g_hash_table_contains (priv->options, section->name))
            {
                /* all we know is that an option already exists by that name,
                 * but this function is supposed to be called prior to loading
                 * the config, so the only options that can exist at this point
                 * are, indeed, groups */
                g_warning ("Cannot define group '%s', group already exists",
                        section->name);
                continue;
            }

            group = g_new0 (struct group, 1);
            group->option = g_strdup (section->name);
            for (parsed = section->value; parsed; parsed = parsed->next)
            {
                if (streq (parsed->name, "name"))
                    if (!group->name)
                        group->name = g_strdup (parsed->value);
                    else
                        g_warning ("Cannot redefine option '%s' for group '%s'",
                                parsed->name, group->option);
                else if (streq (parsed->name, "child"))
                    if (!child)
                        child = g_strdup (parsed->value);
                    else
                        g_warning ("Cannot redefine option '%s' for group '%s'",
                                parsed->name, group->option);
                else if (streq (parsed->name, "indexed"))
                    group->is_indexed = streq (parsed->value, "1");
                else if (!streq (parsed->name, "type"))
                    g_warning ("Invalid option '%s' in definition of group '%s'",
                            parsed->name, section->name);
            }

            if (!child)
            {
                size_t len;

                /* defaults to the name of the group */
                child = g_strdup (group->option);
                /* minus the last "s" if there's one */
                len = strlen (child) - 1;
                if (child[len] == 's')
                    child[len] = '\0';
            }

            /* now that we know the child, make sure it isn't already used */
            if (g_hash_table_contains (priv->groups, child))
            {
                g_warning ("Cannot use '%s' as child for group '%s', already in use",
                        child, section->name);
                g_free (child);
                free_group (group);
                continue;
            }

            g_hash_table_insert (priv->groups, child, group);

            /* create an option for the group (via provider, it lists all the
             * "items" in the group) */
            option = g_new0 (struct option, 1);
            option->extra = priv->groups;
            g_value_init (&option->value, G_TYPE_POINTER);
            g_value_set_pointer (&option->value, group);
            g_hash_table_insert (priv->options, g_strdup (group->option), option);
        }
        else
            g_warning ("Unknown type '%s' for definition '%s'", s, section->name);
    }

    free_parsed_data_section (first_section);
    g_free (data);
    return TRUE;
}

gboolean
donna_config_load_config (DonnaProviderConfig *config, gchar *data)
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
        struct parsed_data *parsed;

        for (parsed = section->value; parsed; parsed = parsed->next)
        {

        }
    }

    free_parsed_data_section (first_section);
    g_free (data);
    return TRUE;
}
