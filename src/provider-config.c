/*
 * donnatella - Copyright (C) 2014 Olivier Brunel
 *
 * provider-config.c
 * Copyright (C) 2014 Olivier Brunel <jjk@jjacky.com>
 *
 * This file is part of donnatella.
 *
 * donnatella is free software: you can redistribute it and/or modify it under
 * the terms of the GNU General Public License as published by the Free Software
 * Foundation, either version 3 of the License, or (at your option) any later
 * version.
 *
 * donnatella is distributed in the hope that it will be useful, but WITHOUT ANY
 * WARRANTY; without even the implied warranty of MERCHANTABILITY or FITNESS
 * FOR A PARTICULAR PURPOSE.
 * See the GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License along with
 * donnatella. If not, see http://www.gnu.org/licenses/
 */

#include "config.h"

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
#include "util.h"
#include "macros.h"
#include "debug.h"

/**
 * SECTION:provider-config
 * @Short_description: Configuration Manager
 *
 * The configuration file is a simple text file using classic format for
 * conf/INI files, where empty lines are lines starting with a semi-colon are
 * ignored, and options are organized in sections.
 *
 * <refsect2 id="config-overview">
 * <title>What's an option?</title>
 * <para>
 * A section is simply a name in between brackets, e.g:
 * <programlisting>
 * [section]
 * </programlisting>
 *
 * Section names must start with a lowercase letter, and can be made of
 * lower/upper letters, numbers, dashes ('-'), underscores ('_') and spaces.
 *
 * It is also possible to end a section name with a slash to create "numbered
 * categories." Numbered categories will be auto-named on startup, and processed
 * in order when needed.
 *
 * This is used to define multiple "elements" made of a group of options, e.g.
 * arrangements or color filters.
 *
 * One can create categories inside such numbered categories by using a two
 * consecutive slahes, e.g:
 * <programlisting>
 * [foo//bar/]
 * </programlisting>
 *
 * Option names must follow the same rule as section names. An option is simply
 * defined via the <systemitem>name=value</systemitem> syntax.
 *
 * However, as hinted before, options in donna are typed. Basic types are
 * boolean, integer, double and string. An option whose value is either true or
 * false will be a boolean; A number will be an integer, unless it contains a
 * dot (as decimal separator) then it will be a double. Anything else is a
 * string; Optionally strings can be quoted, required to include spaces/tabs in
 * the beginning/end of the value.
 *
 * Additionally, donna introduces "extras" which can restrict the possible value
 * of an option, as well as making things more user-friendly. An extra can
 * either be a list of strings, a list of integers or an integer as bitfield
 * flags.
 *
 * While the list of strings is only a list of possible values, there's a little
 * more to it for the other two. For lists of integers, each possible value will
 * also have a string that can be used in the configuration file, to make things
 * easier.
 *
 * So if an extra foobar has two possible values, 0 (for foo) and 1 (for bar),
 * the option could be written either as <systemitem>option=1</systemitem> or
 * (preferably) as <systemitem>option:foobar=bar</systemitem>
 *
 * Similarly for flags, if extra foobar allows for 1 (foo) and 2 (bar) it could
 * be found as either <systemitem>option=3</systemitem> or (preferably)
 * <systemitem>option:foobar=foo,bar</systemitem>
 * </para>
 * </refsect2>
 *
 * <refsect2 id="option-paths">
 * <title>One option, in multiple categories at once</title>
 * <para>
 * As happens often in donna, options can be looked for in multiple places. That
 * is, instead of looking for one option under a single category, a list of
 * possible categories will be searched, the first one found being used.  This
 * is referred to as option paths.
 *
 * Option paths allow for instance to be able to set an option as default, and
 * override it in specific cases. For example, treeview options (except for
 * <systemitem>is_tree</systemitem>) will be looked for in the following
 * categories :
 *
 * - tree_views/<systemitem>&lt;TREEVIEW-NAME&gt;</systemitem> : for options
 *   specific to this treeview only
 * - default/<systemitem>&lt;TREEVIEW-MODE&gt;s</systemitem> : for
 *   default options to all tree views of the same mode (tree/list)
 *
 * For example, when looking for option <systemitem>show_hidden</systemitem> for
 * treeview foobar, which is a list:
 *
 * - if <systemitem>tree_views/foobar/show_hidden</systemitem> exists, it is
 *   used; Else
 * - if <systemitem>default/lists/show_hidden</systemitem> exists, it
 *   is used; Else
 * - when no more possible categories to look in are left, an
 *   internal/hard-coded default will be used. (It might also set the last
 *   possible option at the same time.)
 *   In our example, <systemitem>default/lists/show_hidden</systemitem>
 *   would be set to <systemitem>true</systemitem>, and be used.
 *
 * It should be noted that when looking for an option, its type is taken into
 * account. That is, in our example show_hidden is of type boolean; If somehow
 * <systemitem>tree_views/foobar/show_hidden</systemitem> existed in the config,
 * but not as a boolean, donna would behave as if the option did not exist.
 * </para></refsect2>
 */

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


/* internal */
guint
_donna_config_get_from_column (DonnaConfig *config,
                               const gchar *col_name,
                               const gchar *arr_name,
                               const gchar *tv_name,
                               gboolean     is_tree,
                               const gchar *def_cat,
                               const gchar *opt_name,
                               GType        type);


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
static gchar *          provider_config_get_context_alias_new_nodes (
                                            DonnaProvider      *provider,
                                            const gchar        *extra,
                                            DonnaNode          *location,
                                            const gchar        *prefix,
                                            GError            **error);
static gboolean         provider_config_get_context_item_info (
                                            DonnaProvider      *provider,
                                            const gchar        *item,
                                            const gchar        *extra,
                                            DonnaContextReference reference,
                                            DonnaNode          *node_ref,
                                            get_sel_fn          get_sel,
                                            gpointer            get_sel_data,
                                            DonnaContextInfo   *info,
                                            GError            **error);


static gchar *get_option_full_name (GNode *root, GNode *gnode);
static void free_extra  (DonnaConfigExtra  *extra);
static gboolean ensure_option_has_node (DonnaProviderConfig *config,
                                        gchar               *location,
                                        struct option       *option);

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
    interface->get_context_alias_new_nodes  = provider_config_get_context_alias_new_nodes;
    interface->get_context_item_info        = provider_config_get_context_item_info;
}

G_DEFINE_TYPE_WITH_CODE (DonnaProviderConfig, donna_provider_config,
        G_TYPE_OBJECT,
        G_IMPLEMENT_INTERFACE (DONNA_TYPE_PROVIDER, provider_config_provider_init)
        )

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

static inline void
config_option_set (DonnaConfig *config, const gchar *name)
{
    g_signal_emit (config, donna_config_signals[OPTION_SET],
            g_quark_from_string (name), name);
}

static inline void
config_option_deleted (DonnaConfig *config, const gchar *name)
{
    g_signal_emit (config, donna_config_signals[OPTION_DELETED],
            g_quark_from_string (name), name);
}

static void
free_extra (DonnaConfigExtra *extra)
{
    if (!extra)
        return;
    g_free (extra->any.title);
    g_free (extra);
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
            /* add for later removal, outside of the lock.
             * Add a ref so it doesn't go away before we sent the signal */
            g_ptr_array_add (nodes, g_object_ref (option->node));
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

enum valid
{
    VALID_CONFIG_SECTION = 1,
    VALID_CONFIG_OPTION,
    VALID_CATEGORY_NAME,
    VALID_OPTION_NAME,
    VALID_EXTRA_NAME
};

static gboolean
is_valid_name_len (const gchar *name, gsize len, enum valid valid)
{
    gboolean is_first  = TRUE;
    gboolean got_colon = valid == VALID_EXTRA_NAME;
    gboolean is_number = FALSE;

    g_return_val_if_fail (valid == VALID_CONFIG_SECTION
            || valid == VALID_CONFIG_OPTION || valid == VALID_CATEGORY_NAME
            || valid == VALID_OPTION_NAME || valid == VALID_EXTRA_NAME, FALSE);

    if (*name == '\0')
        return FALSE;

    for ( ; *name != '\0' && len != 0; ++name, --len)
    {
        if (is_number && !(*name >= '0' && *name <= '9'))
            return FALSE;

        if (*name >= 'a' && *name <= 'z')
        {
            is_first = FALSE;
            continue;
        }
        else if (is_first)
        {
            if (valid == VALID_CATEGORY_NAME && *name >= '1' && *name <= '9')
            {
                is_first  = FALSE;
                is_number = TRUE;
                continue;
            }
            return FALSE;
        }

        if ((*name >= 'A' && *name <= 'Z') || (*name >= '0' && *name <= '9')
                || *name == '-' || *name == '_' || (!got_colon && *name == ' '))
            continue;

        if (valid == VALID_CONFIG_SECTION)
        {
            if (*name == '/')
                continue;
        }
        else if (valid == VALID_CONFIG_OPTION)
        {
            /* option name can have a colon and then the type */
            if (!got_colon && *name == ':')
            {
                got_colon = TRUE;
                continue;
            }
        }

        return FALSE;
    }

    return TRUE;
}

#define is_valid_name(name, valid)  is_valid_name_len (name, (gsize) -1, valid)

gboolean
donna_config_add_extra (DonnaConfig          *config,
                        DonnaConfigExtraType  type,
                        const gchar          *name,
                        const gchar          *title,
                        gint                  nb_items,
                        gpointer              items,
                        GError              **error)
{
    DonnaProviderConfigPrivate *priv;
    DonnaConfigExtraAny *extra;
    gsize size_extra;
    gsize size_items;
    gint i;
    gint j;

    g_return_val_if_fail (DONNA_IS_PROVIDER_CONFIG (config), FALSE);
    g_return_val_if_fail (type == DONNA_CONFIG_EXTRA_TYPE_LIST
            || type == DONNA_CONFIG_EXTRA_TYPE_LIST_INT
            || type == DONNA_CONFIG_EXTRA_TYPE_LIST_FLAGS, FALSE);
    g_return_val_if_fail (name != NULL, FALSE);
    g_return_val_if_fail (title != NULL, FALSE);
    g_return_val_if_fail (nb_items > 0, FALSE);
    g_return_val_if_fail (items != NULL, FALSE);
    priv = config->priv;

    if (G_UNLIKELY (!is_valid_name (name, VALID_EXTRA_NAME)))
    {
        g_set_error (error, DONNA_CONFIG_ERROR,
                DONNA_CONFIG_ERROR_ALREADY_EXISTS,
                "Config: Cannot add extra '%s': Invalid name",
                name);
        return FALSE;
    }

    g_rw_lock_writer_lock (&priv->lock);
    if (g_hash_table_contains (priv->extras, name))
    {
        g_rw_lock_writer_unlock (&priv->lock);
        g_set_error (error, DONNA_CONFIG_ERROR,
                DONNA_CONFIG_ERROR_ALREADY_EXISTS,
                "Config: Cannot add extra '%s': One already exists",
                name);
        return FALSE;
    }

    if (type == DONNA_CONFIG_EXTRA_TYPE_LIST)
    {
        DonnaConfigItemExtraList *it = items;

        size_extra = sizeof (DonnaConfigExtraList);
        size_items = (gsize) nb_items * sizeof (DonnaConfigItemExtraList);

        for (i = 0; i < nb_items; ++i)
        {
            for (j = i + 1; j < nb_items; ++j)
            {
                if (streq (it[i].value, it[j].value))
                {
                    g_rw_lock_writer_unlock (&priv->lock);
                    g_set_error (error, DONNA_CONFIG_ERROR,
                            DONNA_CONFIG_ERROR_OTHER,
                            "Config: Cannot add extra '%s': "
                            "Duplicate value (%s) for items #%d & #%d",
                            name, it[i].value, i + 1, j + 1);
                    return FALSE;
                }
            }
        }
    }
    else if (type == DONNA_CONFIG_EXTRA_TYPE_LIST_INT)
    {
        DonnaConfigItemExtraListInt *it = items;

        size_extra = sizeof (DonnaConfigExtraListInt);
        size_items = (gsize) nb_items * sizeof (DonnaConfigItemExtraListInt);

        for (i = 0; i < nb_items; ++i)
        {
            for (j = i + 1; j < nb_items; ++j)
            {
                if (it[i].value == it[j].value)
                {
                    g_rw_lock_writer_unlock (&priv->lock);
                    g_set_error (error, DONNA_CONFIG_ERROR,
                            DONNA_CONFIG_ERROR_OTHER,
                            "Config: Cannot add extra '%s': "
                            "Duplicate value (%d) for items #%d & #%d",
                            name, it[i].value, i + 1, j + 1);
                    return FALSE;
                }
            }
        }
    }
    else /* DONNA_CONFIG_EXTRA_TYPE_LIST_FLAGS */
    {
        DonnaConfigItemExtraListFlags *it = items;

        size_extra = sizeof (DonnaConfigExtraListFlags);
        size_items = (gsize) nb_items * sizeof (DonnaConfigItemExtraListFlags);

        for (i = 0; i < nb_items; ++i)
        {
            if (it[i].value <= 0
                    || (it[i].value & (~it[i].value + 1)) != it[i].value)
            {
                g_rw_lock_writer_unlock (&priv->lock);
                g_set_error (error, DONNA_CONFIG_ERROR,
                        DONNA_CONFIG_ERROR_OTHER,
                        "Config: Cannot add extra '%s': Invalid value (%d) for item #%d; "
                        "Only non-zero power of 2 are allowed",
                        name, it[i].value, i + 1);
                return FALSE;
            }

            for (j = i + 1; j < nb_items; ++j)
            {
                if (it[i].value == it[j].value)
                {
                    g_rw_lock_writer_unlock (&priv->lock);
                    g_set_error (error, DONNA_CONFIG_ERROR,
                            DONNA_CONFIG_ERROR_OTHER,
                            "Config: Cannot add extra '%s': "
                            "Duplicate value (%d) for items #%d & #%d",
                            name, it[i].value, i + 1, j + 1);
                    return FALSE;
                }
            }
        }
    }

    extra = g_malloc (size_extra + size_items);
    extra->type     = type;
    extra->title    = g_strdup (title);
    extra->nb_items = nb_items;
    memcpy (&extra->items, items, size_items);

    g_hash_table_insert (priv->extras, str_chunk (priv, name), extra);
    g_rw_lock_writer_unlock (&priv->lock);

    return TRUE;
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
            if (!is_valid_name (data, VALID_CONFIG_SECTION))
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
            if (!is_valid_name (data, VALID_CONFIG_OPTION))
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

/* assumes a lock on config */
static GNode *
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

/* assumes a writer lock on config. If parent_node/child_node are set, the
 * caller must emit node-new-child once the lock is released */
static GNode *
ensure_categories (DonnaProviderConfig  *config,
                   const gchar          *name,
                   gsize                 len,
                   DonnaNode           **parent_node,
                   DonnaNode           **child_node)
{
    GNode *root;
    GNode *parent;
    GNode *node;
    const gchar *s;

    /* skip the main root, if specified */
    if (*name == '/')
    {
        ++name;
        --len;
    }

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
            struct option *po;
            gint i;

            s = name;
            /* the GValue for categories hold an integer value with the next
             * index to use for such cases */
            po = parent->data;
            i = g_value_get_int (&po->value);
            g_value_set_int (&po->value, i + 1);

            option = g_slice_new0 (struct option);
            option->name = str_chunk_len (config->priv, NULL, i);
            option->extra = root;
            g_value_init (&option->value, G_TYPE_INT);
            g_value_set_int (&option->value, 1);
            node = g_node_append_data (parent, option);

            /* avoid deadlock -- see _set_option() for more */
            if (parent_node && !*parent_node && po->node
                    && ((GObject *) po->node)->ref_count > 1)
            {
                gchar *fn;

                *parent_node = g_object_ref (po->node);
                fn = get_option_full_name (root, node);
                ensure_option_has_node (config, fn, option);
                g_free (fn);
                *child_node = option->node;
            }
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

            node = get_child_node (parent, name, (gsize) (s - name));
            if (node)
            {
                if (!option_is_category (node->data, root))
                    return NULL;
            }
            else
            {
                /* create category/node */
                struct option *option;
                struct option *po = parent->data;

                if (!is_valid_name_len (name, (gsize) (s - name), VALID_CATEGORY_NAME))
                    return NULL;

                option = g_slice_new0 (struct option);
                option->name = str_chunk_len (config->priv, name, s - name);
                option->extra = root;
                /* category hold an index, next number to use for auto-creating
                 * sub-categories. (See above) */
                g_value_init (&option->value, G_TYPE_INT);
                g_value_set_int (&option->value, 1);
                node = g_node_append_data (parent, option);

                /* avoid deadlock -- see _set_option() for more */
                if (parent_node && !*parent_node && po->node
                        && ((GObject *) po->node)->ref_count > 1)
                {
                    gchar *fn;

                    *parent_node = g_object_ref (po->node);
                    fn = get_option_full_name (root, node);
                    ensure_option_has_node (config, fn, option);
                    g_free (fn);
                    *child_node = option->node;
                }
            }
        }

next:
        if (*s == '\0' || len == (gsize) (s - name))
            break;
        else
        {
            len -= (gsize) (s - name + 1);
            name = s + 1;
            parent = node;
        }
    }

    return node;
}

static gboolean
add_flag_value (DonnaConfigExtra *extra, const gchar *str, gsize len, gint *val)
{
    DonnaConfigExtraListFlags *e = (DonnaConfigExtraListFlags *) extra;
    gint i;

    for (i = 0; i < e->nb_items; ++i)
        if (streqn (str, e->items[i].in_file, len)
                && (str[len] == '\0' || str[len] == ','))
        {
            *val += e->items[i].value;
            return TRUE;
        }
    return FALSE;
}

static gboolean
get_extra_value (DonnaConfigExtra *extra, const gchar *str, gpointer value)
{
    gint i;

    if (extra->any.type == DONNA_CONFIG_EXTRA_TYPE_LIST)
    {
        DonnaConfigExtraList *e = (DonnaConfigExtraList *) extra;

        for (i = 0; i < e->nb_items; ++i)
            if (streq (str, e->items[i].value))
            {
                * (gchar **) value = (gchar *) e->items[i].value;
                return TRUE;
            }
    }
    else if (extra->any.type == DONNA_CONFIG_EXTRA_TYPE_LIST_INT)
    {
        DonnaConfigExtraListInt *e = (DonnaConfigExtraListInt *) extra;

        for (i = 0; i < e->nb_items; ++i)
            if (streq (str, e->items[i].in_file))
            {
                * (gint *) value = e->items[i].value;
                return TRUE;
            }
    }
    else /* DONNA_CONFIG_EXTRA_TYPE_LIST_FLAGS */
    {
        gint val = 0;
        gchar *s;
        gsize len;

        for (;;)
        {
            s = strchr (str, ',');
            if (s)
                len = (gsize) (s - str);
            else
                len = strlen (str);
            if (!add_flag_value (extra, str, len, &val))
                return FALSE;

            if (s)
                str = s + 1;
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
                    strlen (section->name),
                    /* no need to bother w/ node-new-child at this point */
                    NULL, NULL);
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
                if (extra->any.type == DONNA_CONFIG_EXTRA_TYPE_LIST)
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
                else if (extra->any.type == DONNA_CONFIG_EXTRA_TYPE_LIST_INT)
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
                    gsize len = 0;
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
                DonnaConfigExtraAny *extra;

                extra = g_hash_table_lookup (priv->extras, option->extra);
                if (G_UNLIKELY (!extra))
                {
                    gchar *fn = get_option_full_name (priv->root, child);
                    g_warning ("Failed to export option '%s': extra '%s' not found",
                            fn + 1, (gchar *) option->extra);
                    g_free (fn);
                    continue;
                }

                if (extra->type == DONNA_CONFIG_EXTRA_TYPE_LIST)
                {
                    DonnaConfigExtraList *e = (DonnaConfigExtraList *) extra;
                    const gchar *cur = g_value_get_string (&option->value);
                    gint i;

                    for (i = 0; i < extra->nb_items; ++i)
                        if (streq (cur, e->items[i].value))
                            break;
                    if (G_UNLIKELY (i >= extra->nb_items))
                    {
                        gchar *fn = get_option_full_name (priv->root, child);
                        g_warning ("Failed to export option '%s': value '%s' not found for extra '%s'",
                                fn + 1, cur, (gchar *) option->extra);
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
                    DonnaConfigExtraListInt *e = (DonnaConfigExtraListInt *) extra;
                    gint cur = g_value_get_int (&option->value);
                    gint i;

                    for (i = 0; i < extra->nb_items; ++i)
                        if (cur == e->items[i].value)
                            break;
                    if (G_UNLIKELY (i >= extra->nb_items))
                    {
                        gchar *fn = get_option_full_name (priv->root, child);
                        g_warning ("Failed to export option '%s': value %d not found for extra '%s'",
                                fn + 1, cur, (gchar *) option->extra);
                        g_free (fn);
                    }
                    else
                        g_string_append_printf (str, "%s:%s=%s\n",
                                option->name,
                                (gchar *) option->extra,
                                e->items[i].in_file);
                }
                else /* DONNA_CONFIG_EXTRA_TYPE_LIST_FLAGS */
                {
                    DonnaConfigExtraListFlags *e = (DonnaConfigExtraListFlags *) extra;
                    gint cur = g_value_get_int (&option->value);
                    gint i;

                    if (cur > 0)
                    {
                        GString *s_val;

                        /* 23 = random magic number */
                        s_val = g_string_sized_new (23);
                        for (i = 0; i < extra->nb_items; ++i)
                        {
                            if (cur & e->items[i].value)
                                g_string_append_printf (s_val, "%s,",
                                        e->items[i].in_file);
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
                        g_string_append_printf (str, "%s:%s=\"\"\n",
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
                        g_string_append_printf (str, "%s=%d\n",
                                option->name,
                                g_value_get_int (&option->value));
                        break;
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
            g_string_erase (str_loc, (gssize) len, -1);
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
donna_config_get_extra (DonnaConfig            *config,
                        const gchar            *name,
                        GError                **error)
{
    DonnaProviderConfigPrivate *priv;
    DonnaConfigExtra *extra;

    g_return_val_if_fail (DONNA_IS_PROVIDER_CONFIG (config), NULL);
    priv = config->priv;

    g_rw_lock_reader_lock (&priv->lock);
    extra = g_hash_table_lookup (priv->extras, name);
    g_rw_lock_reader_unlock (&priv->lock);
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
            node = get_child_node (node, name, (gsize) (s - name));
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
             GError     **error,
             GType        type,
             gboolean     leave_lock_on,
             const gchar *fmt,
             va_list      va_arg)
{
    DonnaProviderConfigPrivate *priv;
    struct option *option;
    gchar *name;
    gboolean ret;

    g_return_val_if_fail (DONNA_IS_PROVIDER_CONFIG (config), FALSE);
    g_return_val_if_fail (fmt != NULL, FALSE);
    priv = config->priv;

    name = g_strdup_vprintf (fmt, va_arg);

    g_rw_lock_reader_lock (&priv->lock);
    option = get_option (priv->root, name);
    if (option)
    {
        /* G_TYPE_INVALID means we want a category */
        if (type == G_TYPE_INVALID)
        {
            ret = option_is_category (option, priv->root);
            if (!ret && error)
                g_set_error (error, DONNA_CONFIG_ERROR,
                        DONNA_CONFIG_ERROR_INVALID_TYPE,
                        "Config: '%s' is an option",
                        (*name == '/') ? name + 1 : name);
        }
        else if (!option_is_category (option, priv->root))
        {
            ret = G_VALUE_HOLDS (&option->value, type);
            if (!ret && error)
                g_set_error (error, DONNA_CONFIG_ERROR,
                        DONNA_CONFIG_ERROR_INVALID_OPTION_TYPE,
                        "Config: '%s' is of type %s, (expected %s)",
                        (*name == '/') ? name + 1 : name,
                        G_VALUE_TYPE_NAME (&option->value),
                        g_type_name (type));
        }
        else
        {
            ret = FALSE;
            g_set_error (error, DONNA_CONFIG_ERROR,
                    DONNA_CONFIG_ERROR_INVALID_TYPE,
                    "Config: '%s' is a category",
                    (*name == '/') ? name + 1 : name);
        }
    }
    else
    {
        ret = FALSE;
        g_set_error (error, DONNA_CONFIG_ERROR,
                DONNA_CONFIG_ERROR_NOT_FOUND,
                "Config: '%s' doesn't exist",
                (*name == '/') ? name + 1 : name);
    }

    /* allows caller to get the option value, then unlock */
    if (!leave_lock_on)
        g_rw_lock_reader_unlock (&priv->lock);

    g_free (name);
    return (ret) ? option : NULL;
}

#define _has_opt(gtype)    do {             \
    struct option *option;                  \
    va_list va_arg;                         \
                                            \
    va_start (va_arg, fmt);                 \
    option = _get_option (config, error,    \
            gtype, FALSE, fmt, va_arg);     \
    va_end (va_arg);                        \
    return option != NULL;                  \
} while (0)

/**
 * donna_config_has_boolean:
 * @config: The #DonnaConfig
 * @error: (allow-none): Return location of a #GError, or %NULL
 * @fmt: <function>printf</function>-like format for the full/option/name
 * @...: <function>printf</function>-like arguments
 *
 * Returns whether option @fmt exists and is boolean or not
 *
 * Returns: %TRUE if option exists and is boolean, else %FALSE
 */
gboolean
donna_config_has_boolean (DonnaConfig *config,
                          GError      **error,
                          const gchar *fmt,
                          ...)
{
    _has_opt (G_TYPE_BOOLEAN);
}

/**
 * donna_config_has_int:
 * @config: The #DonnaConfig
 * @error: (allow-none): Return location of a #GError, or %NULL
 * @fmt: <function>printf</function>-like format for the full/option/name
 * @...: <function>printf</function>-like arguments
 *
 * Returns whether option @fmt exists and is integer or not
 *
 * Returns: %TRUE if option exists and is integer, else %FALSE
 */
gboolean
donna_config_has_int (DonnaConfig *config,
                      GError     **error,
                      const gchar *fmt,
                      ...)
{
    _has_opt (G_TYPE_INT);
}

/**
 * donna_config_has_double:
 * @config: The #DonnaConfig
 * @error: (allow-none): Return location of a #GError, or %NULL
 * @fmt: <function>printf</function>-like format for the full/option/name
 * @...: <function>printf</function>-like arguments
 *
 * Returns whether option @fmt exists and is double or not
 *
 * Returns: %TRUE if option exists and is double, else %FALSE
 */
gboolean
donna_config_has_double (DonnaConfig *config,
                         GError     **error,
                         const gchar *fmt,
                         ...)
{
    _has_opt (G_TYPE_DOUBLE);
}

/**
 * donna_config_has_string:
 * @config: The #DonnaConfig
 * @error: (allow-none): Return location of a #GError, or %NULL
 * @fmt: <function>printf</function>-like format for the full/option/name
 * @...: <function>printf</function>-like arguments
 *
 * Returns whether option @fmt exists and is string or not
 *
 * Returns: %TRUE if option exists and is string, else %FALSE
 */
gboolean
donna_config_has_string (DonnaConfig *config,
                         GError     **error,
                         const gchar *fmt,
                         ...)
{
    _has_opt (G_TYPE_STRING);
}

/**
 * donna_config_has_category:
 * @config: The #DonnaConfig
 * @error: (allow-none): Return location of a #GError, or %NULL
 * @fmt: <function>printf</function>-like format for the full/option/name
 * @...: <function>printf</function>-like arguments
 *
 * Returns whether option @fmt exists and is a category or not
 *
 * Returns: %TRUE if option exists and is a category, else %FALSE
 */
gboolean
donna_config_has_category (DonnaConfig *config,
                           GError     **error,
                           const gchar *fmt,
                           ...)
{
    _has_opt (G_TYPE_INVALID /* i.e. category */);
}

/**
 * donna_config_has_option:
 * @config: The #DonnaConfig
 * @error: (allow-none): Return location of a #GError, or %NULL
 * @type: (allow-none) (out): Return location for the %GType of the option, or
 * %NULL
 * @extra_name: (allow-none) (out): Return location for the extra of the option
 * (if any), or %NULL
 * @extra: (allow-none) (out): Return location for the %DonnaConfigExtra of the
 * option (if any), or %NULL
 * @fmt: <function>printf</function>-like format for the full/option/name
 * @...: <function>printf</function>-like arguments
 *
 * Returns %TRUE if the option exist (and isn't a category), and set @type,
 * @extra_name and/or @extra
 *
 * If set, both values in @extra_name and @extra are owned by @config and
 * shouldn't be freed.
 *
 * Returns: %TRUE if option exists and is not a category, else %FALSE
 */
gboolean
donna_config_has_option (DonnaConfig             *config,
                         GError                 **error,
                         GType                   *type,
                         const gchar            **extra_name,
                         const DonnaConfigExtra **extra,
                         const gchar             *fmt,
                         ...)
{
    DonnaProviderConfigPrivate *priv;
    struct option *option;
    va_list va_arg;
    gchar *name;
    gboolean ret = FALSE;

    g_return_val_if_fail (DONNA_IS_PROVIDER_CONFIG (config), FALSE);
    g_return_val_if_fail (fmt != NULL, FALSE);
    g_return_val_if_fail (extra_name == NULL || *extra_name == NULL, FALSE);
    g_return_val_if_fail (extra == NULL || *extra == NULL, FALSE);
    priv = config->priv;

    va_start (va_arg, fmt);
    name = g_strdup_vprintf (fmt, va_arg);
    va_end (va_arg);

    g_rw_lock_reader_lock (&priv->lock);
    option = get_option (priv->root, name);
    g_free (name);
    if (option)
    {
        if (option_is_category (option, priv->root))
        {
            g_set_error (error, DONNA_CONFIG_ERROR,
                    DONNA_CONFIG_ERROR_INVALID_TYPE,
                    "Config: '%s' is a category",
                    name);
        }
        else
        {
            ret = TRUE;

            if (type)
                *type = G_VALUE_TYPE (&option->value);

            if (extra_name && option->extra)
                *extra_name = option->extra;

            if (extra && option->extra)
                *extra = g_hash_table_lookup (priv->extras, option->extra);
        }
    }
    else
        g_set_error (error, DONNA_CONFIG_ERROR,
                DONNA_CONFIG_ERROR_NOT_FOUND,
                "Config: Option '%s' doesn't exist",
                name);

    g_rw_lock_reader_unlock (&priv->lock);
    return ret;
}

#define _get_opt(gtype, get_fn)  do {                   \
    struct option *option;                              \
    va_list va_arg;                                     \
                                                        \
    va_start (va_arg, fmt);                             \
    option = _get_option (config, error, gtype, TRUE,   \
            fmt, va_arg);                               \
    va_end (va_arg);                                    \
    if (option)                                         \
        *value = get_fn (&option->value);               \
    g_rw_lock_reader_unlock (&config->priv->lock);      \
    return option != NULL;                              \
} while (0)

/**
 * donna_config_get_boolean:
 * @config: The #DonnaConfig
 * @error: (allow-none): Return location of a #GError, or %NULL
 * @value: (out): Return location to put the value of the option
 * @fmt: <function>printf</function>-like format for the full/option/name
 * @...: <function>printf</function>-like arguments
 *
 * Get the value of boolean option @fmt and set it in @value
 *
 * Returns: %TRUE if @value was set, else %FALSE (option doesn't exist, or is a
 * another type)
 */
gboolean
donna_config_get_boolean (DonnaConfig    *config,
                          GError        **error,
                          gboolean       *value,
                          const gchar    *fmt,
                          ...)
{
    _get_opt (G_TYPE_BOOLEAN, g_value_get_boolean);
}

/**
 * donna_config_get_int:
 * @config: The #DonnaConfig
 * @error: (allow-none): Return location of a #GError, or %NULL
 * @value: (out): Return location to put the value of the option
 * @fmt: <function>printf</function>-like format for the full/option/name
 * @...: <function>printf</function>-like arguments
 *
 * Get the value of integer option @fmt and set it in @value
 *
 * Returns: %TRUE if @value was set, else %FALSE (option doesn't exist, or is a
 * another type)
 */
gboolean
donna_config_get_int (DonnaConfig    *config,
                      GError        **error,
                      gint           *value,
                      const gchar    *fmt,
                      ...)
{
    _get_opt (G_TYPE_INT, g_value_get_int);
}

/**
 * donna_config_get_double:
 * @config: The #DonnaConfig
 * @error: (allow-none): Return location of a #GError, or %NULL
 * @value: (out): Return location to put the value of the option
 * @fmt: <function>printf</function>-like format for the full/option/name
 * @...: <function>printf</function>-like arguments
 *
 * Get the value of double option @fmt and set it in @value
 *
 * Returns: %TRUE if @value was set, else %FALSE (option doesn't exist, or is a
 * another type)
 */
gboolean
donna_config_get_double (DonnaConfig    *config,
                         GError        **error,
                         gdouble        *value,
                         const gchar    *fmt,
                         ...)
{
    _get_opt (G_TYPE_DOUBLE, g_value_get_double);
}

/**
 * donna_config_get_string:
 * @config: The #DonnaConfig
 * @error: (allow-none): Return location of a #GError, or %NULL
 * @value: (out): Return location to put the value of the option
 * @fmt: <function>printf</function>-like format for the full/option/name
 * @...: <function>printf</function>-like arguments
 *
 * Get the value of string option @fmt and set it in @value
 *
 * Returns: %TRUE if @value was set, else %FALSE (option doesn't exist, or is a
 * another type)
 */
gboolean
donna_config_get_string (DonnaConfig          *config,
                         GError              **error,
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
    gchar *name;
    va_list va_arg;

    g_return_val_if_fail (DONNA_IS_PROVIDER_CONFIG (config), FALSE);
    g_return_val_if_fail (fmt != NULL, FALSE);
    g_return_val_if_fail (type != 0, FALSE);
    g_return_val_if_fail (options != NULL, FALSE);
    g_return_val_if_fail (*options == NULL, FALSE);

    priv = config->priv;

    va_start (va_arg, fmt);
    name = g_strdup_vprintf (fmt, va_arg);
    va_end (va_arg);

    g_rw_lock_reader_lock (&priv->lock);
    node = get_option_node (priv->root, name);
    g_free (name);
    if (node)
    {
        for (node = node->children; node; node = node->next)
        {
            if ((type & DONNA_CONFIG_OPTION_TYPE_BOTH) == DONNA_CONFIG_OPTION_TYPE_BOTH
                    || ((type & (DONNA_CONFIG_OPTION_TYPE_CATEGORY
                            | DONNA_CONFIG_OPTION_TYPE_NUMBERED))
                        && option_is_category (node->data, priv->root))
                    || ((type & DONNA_CONFIG_OPTION_TYPE_OPTION)
                        && !option_is_category (node->data, priv->root)))
            {
                if (type & DONNA_CONFIG_OPTION_TYPE_NUMBERED)
                {
                    const gchar *s = ((struct option *) node->data)->name;
                    if (*s < '1' || *s > '9')
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

static struct option *
__get_option (DonnaConfig   *config,
              GType          type,
              gboolean       leave_lock_on,
              const gchar   *fmt,
              ...)
{
    struct option *option;
    va_list va_arg;

    va_start (va_arg, fmt);
    option = _get_option (config, NULL, type, leave_lock_on, fmt, va_arg);
    va_end (va_arg);
    return option;
}

static gboolean
_get_option_column (DonnaConfig  *config,
                    GType         type,
                    GValue       *value,
                    const gchar  *col_name,
                    const gchar  *arr_name,
                    const gchar  *tv_name,
                    gboolean      is_tree,
                    const gchar  *def_cat,
                    const gchar  *opt_name,
                    guint        *from)
{
    DonnaProviderConfigPrivate *priv = config->priv;
    GNode *node;
    GNode *child;
    GValue *v;
    gsize len_col;
    gsize len_tv;
    gsize len_opt;
    struct option *option;

    /* there might not be a tv_name when used e.g. from filter */
    g_return_val_if_fail (col_name != NULL, FALSE);
    g_return_val_if_fail (opt_name != NULL, FALSE);

    len_col = strlen (col_name);
    len_tv = (tv_name) ? strlen (tv_name) : 0;
    len_opt = strlen (opt_name);

    g_rw_lock_reader_lock (&priv->lock);

    if (!arr_name)
        goto treeview;
    node = get_option_node (priv->root, arr_name);
    if (!node)
        goto treeview;
    get_child_cat ("columns_options", 15, treeview);
    get_child_cat (col_name, len_col, treeview);
    get_child_opt (opt_name, len_opt, type, treeview);
    if (from)
    {
        *from = _DONNA_CONFIG_COLUMN_FROM_ARRANGEMENT;
        g_rw_lock_reader_unlock (&priv->lock);
        return TRUE;
    }
    goto get_value;

treeview:
    if (!tv_name)
        goto def;
    node = priv->root;
    get_child_cat ("tree_views", 10, mode);
    get_child_cat (tv_name, len_tv, mode);
    get_child_cat ("columns", 7, mode);
    get_child_cat (col_name, len_col, mode);
    get_child_opt (opt_name, len_opt, type, mode);
    if (from)
    {
        *from = _DONNA_CONFIG_COLUMN_FROM_TREE;
        g_rw_lock_reader_unlock (&priv->lock);
        return TRUE;
    }
    goto get_value;

mode:
    node = priv->root;
    get_child_cat ("defaults", 8, def);
    get_child_cat ((is_tree) ? "trees" : "lists", 5, def);
    get_child_cat ("columns", 7, def);
    get_child_cat (col_name, len_col, def);
    get_child_opt (opt_name, len_opt, type, def);
    if (from)
    {
        *from = _DONNA_CONFIG_COLUMN_FROM_MODE;
        g_rw_lock_reader_unlock (&priv->lock);
        return TRUE;
    }
    goto get_value;

def:
    if (!def_cat)
        goto not_found;

    /* because def_cat might be a string/with/multiple/categories */
    g_rw_lock_reader_unlock (&priv->lock);
    option = __get_option (config, type, TRUE, "defaults/%s/%s", def_cat, opt_name);
    if (option)
    {
        if (from)
        {
            *from = _DONNA_CONFIG_COLUMN_FROM_DEFAULT;
            g_rw_lock_reader_unlock (&priv->lock);
            return TRUE;
        }
        g_value_copy (&option->value, value);
    }
    g_rw_lock_reader_unlock (&priv->lock);
    return option != NULL;

get_value:
    g_value_copy (v, value);
    g_rw_lock_reader_unlock (&priv->lock);
    return TRUE;

not_found:
    g_rw_lock_reader_unlock (&priv->lock);
    return FALSE;
}

#undef get_child_opt
#undef get_child_cat

#define _get_cfg_column(type, gtype, cfg_set, gvalue_get, dup_def) do { \
    GValue value = G_VALUE_INIT;                                    \
    type ret;                                                       \
                                                                    \
    g_value_init (&value, G_TYPE_##gtype);                          \
    if (!_get_option_column (config, G_TYPE_##gtype, &value,        \
                col_name, arr_name, tv_name, is_tree,               \
                def_cat, opt_name, NULL))                           \
    {                                                               \
        g_value_unset (&value);                                     \
        donna_config_set_##cfg_set (config, NULL, def_val,          \
                (def_cat) ? "defaults/%s/%s" : "defaults/%s/columns/%s/%s", \
                (def_cat) ? def_cat : (is_tree) ? "trees" : "lists",\
                (def_cat) ? opt_name : col_name,                    \
                opt_name);                                          \
        return dup_def (def_val);                                   \
    }                                                               \
    ret = g_value_##gvalue_get (&value);                            \
    g_value_unset (&value);                                         \
    return ret;                                                     \
} while (0)

gboolean
donna_config_get_boolean_column (DonnaConfig *config,
                                 const gchar *col_name,
                                 const gchar *arr_name,
                                 const gchar *tv_name,
                                 gboolean     is_tree,
                                 const gchar *def_cat,
                                 const gchar *opt_name,
                                 gboolean     def_val)
{
    _get_cfg_column (gboolean, BOOLEAN, boolean, get_boolean, );
}

gint
donna_config_get_int_column (DonnaConfig *config,
                             const gchar *col_name,
                             const gchar *arr_name,
                             const gchar *tv_name,
                             gboolean     is_tree,
                             const gchar *def_cat,
                             const gchar *opt_name,
                             gint         def_val)
{
    _get_cfg_column (gint, INT, int, get_int, );
}

gdouble
donna_config_get_double_column (DonnaConfig *config,
                                const gchar *col_name,
                                const gchar *arr_name,
                                const gchar *tv_name,
                                gboolean     is_tree,
                                const gchar *def_cat,
                                const gchar *opt_name,
                                gdouble      def_val)
{
    _get_cfg_column (gdouble, DOUBLE, double, get_double, );
}

gchar *
donna_config_get_string_column (DonnaConfig *config,
                                const gchar *col_name,
                                const gchar *arr_name,
                                const gchar *tv_name,
                                gboolean     is_tree,
                                const gchar *def_cat,
                                const gchar *opt_name,
                                const gchar *def_val)
{
    _get_cfg_column (gchar *, STRING, string, dup_string, g_strdup);
}

guint
_donna_config_get_from_column (DonnaConfig *config,
                               const gchar *col_name,
                               const gchar *arr_name,
                               const gchar *tv_name,
                               gboolean     is_tree,
                               const gchar *def_cat,
                               const gchar *opt_name,
                               GType        type)
{
    guint from;

    if (!_get_option_column (config, type, NULL,
                col_name, arr_name, tv_name, is_tree,
                def_cat, opt_name, &from))
        return (def_cat) ? _DONNA_CONFIG_COLUMN_FROM_DEFAULT
            : _DONNA_CONFIG_COLUMN_FROM_MODE;

    return from;
}

#define get_node()  do {                                            \
    va_start (va_arg, fmt);                                         \
    name = g_strdup_vprintf (fmt, va_arg);                          \
    va_end (va_arg);                                                \
                                                                    \
    g_rw_lock_reader_lock (&priv->lock);                            \
    node = get_option_node (priv->root, name);                      \
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
    gchar   *name;
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
    g_free (name);
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
    gchar   *name;
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
    g_free (name);
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
    gchar   *name;
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
    g_free (name);
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
    gchar   *name;
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
    arr->columns_options = name;
    name = NULL;

    get_child ("columns_options_always", 22, BOOLEAN, TRUE);
    if (g_value_get_boolean (value))
        arr->flags |= DONNA_ARRANGEMENT_COLUMNS_OPTIONS_ALWAYS;

done:
    g_rw_lock_reader_unlock (&priv->lock);
    g_free (name);
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
    gchar       *name;
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
            g_warning ("Invalid option 'type' for '%s/color_filters'", name);
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
    g_free (name);
    return ret;
}

#undef get_node
#undef get_child

/* assumes reader lock */
static gboolean
is_value_valid_for_extra (DonnaConfig   *config,
                          const gchar   *extra_name,
                          GValue        *value,
                          GError       **error)
{
    DonnaProviderConfigPrivate *priv = config->priv;
    DonnaConfigExtra *extra;
    gint i;

    extra = g_hash_table_lookup (priv->extras, extra_name);
    if (G_UNLIKELY (!extra))
    {
        g_set_error (error, DONNA_CONFIG_ERROR,
                DONNA_CONFIG_ERROR_INVALID_OPTION_TYPE,
                "Extra '%s' not found",
                extra_name);
        return FALSE;
    }

    if (extra->any.type == DONNA_CONFIG_EXTRA_TYPE_LIST_INT)
    {
        DonnaConfigExtraListInt *e = (DonnaConfigExtraListInt *) extra;
        gint v;

        if (!G_VALUE_HOLDS (value, G_TYPE_INT))
        {
            g_set_error (error, DONNA_CONFIG_ERROR,
                    DONNA_CONFIG_ERROR_OTHER,
                    "Invalid type for extra '%s': expects '%s', given '%s'",
                    extra_name,
                    g_type_name (G_TYPE_INT),
                    G_VALUE_TYPE_NAME (value));
            return FALSE;
        }

        v = g_value_get_int (value);

        for (i = 0; i < e->nb_items; ++i)
        {
            if (v == e->items[i].value)
                break;
        }
        if (i >= e->nb_items)
        {
            g_set_error (error, DONNA_CONFIG_ERROR,
                    DONNA_CONFIG_ERROR_OTHER,
                    "Value '%d' not found in extra '%s'",
                    v, extra_name);
            return FALSE;
        }
    }
    else if (extra->any.type == DONNA_CONFIG_EXTRA_TYPE_LIST_FLAGS)
    {
        DonnaConfigExtraListFlags *e = (DonnaConfigExtraListFlags *) extra;
        gint v = 0;

        if (!G_VALUE_HOLDS (value, G_TYPE_INT))
        {
            g_set_error (error, DONNA_CONFIG_ERROR,
                    DONNA_CONFIG_ERROR_OTHER,
                    "Invalid type for extra '%s': expects '%s', given '%s'",
                    extra_name,
                    g_type_name (G_TYPE_INT),
                    G_VALUE_TYPE_NAME (value));
            return FALSE;
        }

        for (i = 0; i < e->nb_items; ++i)
            v |= e->items[i].value;
        if (g_value_get_int (value) & ~v)
        {
            g_set_error (error, DONNA_CONFIG_ERROR,
                    DONNA_CONFIG_ERROR_OTHER,
                    "Value '%d' not matching extra '%s'",
                    g_value_get_int (value), extra_name);
            return FALSE;
        }
    }
    else /* DONNA_CONFIG_EXTRA_TYPE_LIST */
    {
        DonnaConfigExtraList *e = (DonnaConfigExtraList *) extra;
        const gchar *s;

        if (!G_VALUE_HOLDS (value, G_TYPE_STRING))
        {
            g_set_error (error, DONNA_CONFIG_ERROR,
                    DONNA_CONFIG_ERROR_OTHER,
                    "Invalid type for extra '%s': expects '%s', given '%s'",
                    extra_name,
                    g_type_name (G_TYPE_STRING),
                    G_VALUE_TYPE_NAME (value));
            return FALSE;
        }

        s = g_value_get_string (value);

        for (i = 0; i < e->nb_items; ++i)
        {
            if (streq (s, e->items[i].value))
                break;
        }
        if (i >= e->nb_items)
        {
            g_set_error (error, DONNA_CONFIG_ERROR,
                    DONNA_CONFIG_ERROR_OTHER,
                    "Value '%s' not found in extra '%s'",
                    s, extra_name);
            return FALSE;
        }
    }

    return TRUE;
}

static void
move_numbered_category (GNode *root, GNode *node)
{
    GNode *parent = node->parent;
    struct option *op = parent->data;
    struct option *option = node->data;
    GNode *n;
    gint index;
    gint num;

    /* internal stuff: there's a rule about how options (GNode-s) are stored in
     * the config, specifically for "numbered categories" : they must be in
     * order. There's also the fact hat the parent category has an index of the
     * next available number */

    num = (gint) g_ascii_strtoll (option->name, NULL, 10);
    index = g_value_get_int (&op->value);

    /* is this the first numbered category? */
    if (index == 1)
        goto skip;

    /* "remove" the node from the tree */
    g_node_unlink (node);
    /* and find the first numbered category with a higher number */
    for (n = parent->children; n; n = n->next)
    {
        struct option *o = n->data;
        gint idx;

        /* skip options & "regular" categories */
        if (!option_is_category (o, root)
                || *o->name < '1' || *o->name > '9')
            continue;

        idx = (gint) g_ascii_strtoll (o->name, NULL, 10);
        if (idx > num)
            break;
    }
    /* re-insert node before, or as last children if none found */
    g_node_insert_before (parent, n, node);
skip:
    if (num >= index)
        g_value_set_int (&op->value, num + 1);
}

static gboolean
_set_option_va (DonnaConfig    *config,
               GError        **error,
               DonnaNode     **new_node,
               GType           type,
               const gchar    *extra,
               GValue         *value,
               gboolean        allow_overwrite,
               const gchar    *fmt,
               va_list         va_arg)
{
    DonnaProviderConfigPrivate *priv;
    GNode *parent;
    GNode *node;
    struct option *option;
    DonnaNode *parent_node = NULL;
    DonnaNode *child_node  = NULL;
    DonnaNode *option_node = NULL;
    gchar *name;
    const gchar *s;
    gboolean ret = FALSE;

    g_return_val_if_fail (DONNA_IS_PROVIDER_CONFIG (config), FALSE);
    g_return_val_if_fail (fmt != NULL, FALSE);

    name = g_strdup_vprintf (fmt, va_arg);
    if (*name != '/')
    {
        gchar *n;
        n = g_strconcat ("/", name, NULL);
        g_free (name);
        name = n;
    }

    priv = config->priv;
    g_rw_lock_writer_lock (&priv->lock);
    s = strrchr (name + 1, '/');
    if (s)
    {
        parent = ensure_categories (config, name + 1, (gsize) (s - name - 1),
                &parent_node, &child_node);
        if (!parent)
        {
            g_set_error (error, DONNA_CONFIG_ERROR,
                    DONNA_CONFIG_ERROR_NOT_FOUND,
                    "Config: Parent of option '%s' not found",
                    name + 1);
            goto done;
        }
        ++s;
    }
    else
    {
        s = name + 1;
        parent = priv->root;
    }

    /* G_TYPE_INVALID means category */
    if (!is_valid_name ((gchar *) s,
                (type == G_TYPE_INVALID) ? VALID_CATEGORY_NAME : VALID_OPTION_NAME))
    {
        g_set_error (error, DONNA_CONFIG_ERROR,
                DONNA_CONFIG_ERROR_INVALID_NAME,
                (type == G_TYPE_INVALID)
                ? "Config: Cannot create category '%s': invalid name"
                : "Config: Cannot create option '%s': invalid name",
                name + 1);
        goto done;
    }

    node = get_child_node (parent, s, strlen (s));
    if (node)
    {
        option = node->data;
        if (type == G_TYPE_INVALID || !allow_overwrite)
        {
            g_set_error (error, DONNA_CONFIG_ERROR,
                    DONNA_CONFIG_ERROR_ALREADY_EXISTS,
                    "Config: Option '%s' already exists",
                    name + 1);
        }
        else if (option_is_category (option, priv->root))
        {
            g_set_error (error, DONNA_CONFIG_ERROR,
                    DONNA_CONFIG_ERROR_INVALID_TYPE,
                    "Config: Option '%s' is a category",
                    name + 1);
        }
        else if (!G_VALUE_HOLDS (&option->value, type)
                || (extra && !streq (extra, option->extra)))
        {
            g_set_error (error, DONNA_CONFIG_ERROR,
                    DONNA_CONFIG_ERROR_INVALID_OPTION_TYPE,
                    "Config: Option '%s' is of type '%s' (expected '%s')",
                    name + 1,
                    (option->extra)
                    ? (gchar *) option->extra : G_VALUE_TYPE_NAME (&option->value),
                    (extra) ? extra : g_type_name (type));
        }
        else
            ret = TRUE;
    }
    else
    {
        struct option *po = parent->data;

        option = g_slice_new0 (struct option);
        option->name = str_chunk (priv, s);
        if (type == G_TYPE_INVALID)
        {
            option->extra = priv->root;
            /* next index for auto-numbered categories */
            g_value_init (&option->value, G_TYPE_INT);
            g_value_set_int (&option->value, 1);
        }
        else
            g_value_init (&option->value, type);

        node = g_node_append_data (parent, option);
        ret = TRUE;

        if (extra)
            option->extra = str_chunk (priv, extra);

        /* numbered category special handling */
        if (type == G_TYPE_INVALID && *s >= '1' && *s <= '9')
            move_numbered_category (priv->root, node);

        /* see below re: option_node for more */
        if (!child_node && po->node && ((GObject *) po->node)->ref_count > 1)
            parent_node = g_object_ref (po->node);
    }

    if (ret)
    {
        if (type != G_TYPE_INVALID)
        {
            /* if extra is non NULL then we assume the check of value was done
             * prior to calling is. And since we know this is an option, so
             * option->extra can't be priv->root (for categories) */
            if (!extra && option->extra
                    && !is_value_valid_for_extra (config, option->extra, value, error))
            {
                g_prefix_error (error, "Config: Failed to set option '%s': ",
                        name + 1);
                goto done;
            }

            g_value_copy (value, &option->value);
        }

        if (!child_node)
        {
            if (parent_node)
            {
                /* if we have a parent node it means we created the option, so
                 * we should also create the node to emit node-new-child */
                ensure_option_has_node (config, name, option);
                /* a ref was added already for us */
                option_node = option->node;
            }
            else if (option->node)
            {
                /* update the value w/out emitting node-updated */
                donna_node_set_property_value_no_signal (option->node,
                        "option-value", &option->value);
                /* make sure we can ref the node (to emit signal) without
                 * problem.  That is, if ref_count == 1 it means we're holding
                 * the only ref left, i.e. we have a toggle_ref waiting for the
                 * lock to unref the node, and trying to add a ref would
                 * deadlock */
                if (((GObject *) option->node)->ref_count > 1)
                    option_node = g_object_ref (option->node);
            }
        }

        if (new_node)
        {
            /* new_node is used when creation an option/category, so there's
             * no risk of deadlock due to the node existing w/ a pending
             * toggle_ref.
             * However, option_node might stil already have been created, if
             * parent_node existed (for node-new-child) */
            if (!option_node)
            {
                ensure_option_has_node (config, name, option);
                *new_node = option->node;
            }
            else
                *new_node = g_object_ref (option_node);
        }
    }

done:
    g_rw_lock_writer_unlock (&priv->lock);

    /* signals after releasing the lock, to avoid any deadlocks */
    if (child_node)
    {
        donna_provider_node_new_child ((DonnaProvider *) config,
                parent_node, child_node);
        g_object_unref (parent_node);
        g_object_unref (child_node);
    }
    else if (ret)
    {
        config_option_set (config, name + 1);
        if (parent_node)
        {
            donna_provider_node_new_child ((DonnaProvider *) config,
                    parent_node, option_node);
            g_object_unref (parent_node);
            g_object_unref (option_node);
        }
        else if (option_node)
        {
            donna_provider_node_updated ((DonnaProvider *) config, option_node,
                    "option-value");
            g_object_unref (option_node);
        }
    }

    g_free (name);
    return ret;
}

static gboolean
_set_option (DonnaConfig    *config,
             GError        **error,
             DonnaNode     **new_node,
             GType           type,
             const gchar    *extra,
             GValue         *value,
             gboolean        allow_overwrite,
             const gchar    *fmt,
             ...)
{
    va_list va_args;
    gboolean ret;

    va_start (va_args, fmt);
    ret = _set_option_va (config, error, new_node, type, extra, value,
            allow_overwrite, fmt, va_args);
    va_end (va_args);
    return ret;
}

#define _set_opt(gtype, extra, over, set_fn, node) do { \
    va_list va_arg;                                     \
    GValue gvalue = G_VALUE_INIT;                       \
    gboolean ret;                                       \
                                                        \
    if (gtype != G_TYPE_INVALID)                        \
    {                                                   \
        g_value_init (&gvalue, gtype);                  \
        set_fn (&gvalue, value);                        \
        if (extra && !is_value_valid_for_extra (config, \
                    extra, &gvalue, error))             \
        {                                               \
            g_value_unset (&gvalue);                    \
            return FALSE;                               \
        }                                               \
    }                                                   \
    va_start (va_arg, fmt);                             \
    ret = _set_option_va (config, error, node, gtype,   \
            extra, &gvalue, over, fmt, va_arg);         \
    if (gtype != G_TYPE_INVALID)                        \
        g_value_unset (&gvalue);                        \
    va_end (va_arg);                                    \
    return ret;                                         \
} while (0)

/**
 * donna_config_new_boolean:
 * @config: The #DonnaConfig
 * @error: (allow-none): Return location of a #GError, or %NULL
 * @new_node: (allow-none) (out): Return location for the #DonnaNode of the
 * newly-created option; or %NULL
 * @value: Value for the new option
 * @fmt: <function>printf</function>-like format for the full/option/name
 * @...: <function>printf</function>-like arguments
 *
 * Creates a new boolean option @fmt and sets it to @value
 *
 * Will fail and return %FALSE if an option already exist, even if it is of the
 * same type (and same value).
 *
 * Returns: %TRUE if option was created & set, else %FALSE
 */
gboolean
donna_config_new_boolean (DonnaConfig            *config,
                          GError                **error,
                          DonnaNode             **new_node,
                          gboolean                value,
                          const gchar            *fmt,
                          ...)

{
    _set_opt (G_TYPE_BOOLEAN, NULL, FALSE, g_value_set_boolean, new_node);
}

/**
 * donna_config_new_int:
 * @config: The #DonnaConfig
 * @error: (allow-none): Return location of a #GError, or %NULL
 * @new_node: (allow-none) (out): Return location for the #DonnaNode of the
 * newly-created option; or %NULL
 * @extra: (allow-none): Name of the extra of the option to create
 * @value: Value for the new option
 * @fmt: <function>printf</function>-like format for the full/option/name
 * @...: <function>printf</function>-like arguments
 *
 * Creates a new integer option @fmt (of extra @extra, is specified) and sets it
 * to @value
 *
 * Will fail and return %FALSE if an option already exist, even if it is of the
 * same type (and same value).
 *
 * If you want to create an option with an extra, using the "in-file" values
 * instead of the actual value, see donna_config_set_option()
 *
 * Returns: %TRUE if option was created & set, else %FALSE
 */
gboolean
donna_config_new_int (DonnaConfig            *config,
                      GError                **error,
                      DonnaNode             **new_node,
                      const gchar            *extra,
                      gint                    value,
                      const gchar            *fmt,
                      ...)
{
    _set_opt (G_TYPE_INT, extra, FALSE, g_value_set_int, new_node);
}

/**
 * donna_config_new_double:
 * @config: The #DonnaConfig
 * @error: (allow-none): Return location of a #GError, or %NULL
 * @new_node: (allow-none) (out): Return location for the #DonnaNode of the
 * newly-created option; or %NULL
 * @value: Value for the new option
 * @fmt: <function>printf</function>-like format for the full/option/name
 * @...: <function>printf</function>-like arguments
 *
 * Creates a new double option @fmt and sets it to @value
 *
 * Will fail and return %FALSE if an option already exist, even if it is of the
 * same type (and same value).
 *
 * Returns: %TRUE if option was created & set, else %FALSE
 */
gboolean
donna_config_new_double (DonnaConfig            *config,
                         GError                **error,
                         DonnaNode             **new_node,
                         gdouble                 value,
                         const gchar            *fmt,
                         ...)
{
    _set_opt (G_TYPE_DOUBLE, NULL, FALSE, g_value_set_double, new_node);
}

/**
 * donna_config_new_string:
 * @config: The #DonnaConfig
 * @error: (allow-none): Return location of a #GError, or %NULL
 * @new_node: (allow-none) (out): Return location for the #DonnaNode of the
 * newly-created option; or %NULL
 * @extra: (allow-none): Name of the extra of the option to create
 * @value: Value for the new option
 * @fmt: <function>printf</function>-like format for the full/option/name
 * @...: <function>printf</function>-like arguments
 *
 * Creates a new string option @fmt (of extra @extra, is specified) and sets it
 * to (a copy of) @value
 * If you want @config to take owership of @value (and use g_free() when done
 * with it) use donna_config_new_string_take()
 *
 * Will fail and return %FALSE if an option already exist, even if it is of the
 * same type (and same value).
 *
 * If you want to create an option with an extra, using the "in-file" values
 * instead of the actual value, see donna_config_set_option()
 *
 * Returns: %TRUE if option was created & set, else %FALSE
 */
gboolean
donna_config_new_string (DonnaConfig            *config,
                         GError                **error,
                         DonnaNode             **new_node,
                         const gchar            *extra,
                         const gchar            *value,
                         const gchar            *fmt,
                         ...)
{
    _set_opt (G_TYPE_STRING, extra, FALSE, g_value_set_string, new_node);
}

/**
 * donna_config_new_string_take:
 * @config: The #DonnaConfig
 * @error: (allow-none): Return location of a #GError, or %NULL
 * @new_node: (allow-none) (out): Return location for the #DonnaNode of the
 * newly-created option; or %NULL
 * @extra: (allow-none): Name of the extra of the option to create
 * @value: Value for the new option
 * @fmt: <function>printf</function>-like format for the full/option/name
 * @...: <function>printf</function>-like arguments
 *
 * Creates a new string option @fmt (of extra @extra, is specified) and sets it
 * to @value, assuming ownerhsip (using g_free() when done with it)
 *
 * Will fail and return %FALSE if an option already exist, even if it is of the
 * same type (and same value).
 *
 * If you want to create an option with an extra, using the "in-file" values
 * instead of the actual value, see donna_config_set_option()
 *
 * Returns: %TRUE if option was created & set, else %FALSE
 */
gboolean
donna_config_new_string_take (DonnaConfig            *config,
                              GError                **error,
                              DonnaNode             **new_node,
                              const gchar            *extra,
                              gchar                  *value,
                              const gchar            *fmt,
                              ...)
{
    _set_opt (G_TYPE_STRING, extra, FALSE, g_value_take_string, new_node);
}

/**
 * donna_config_new_category:
 * @config: The #DonnaConfig
 * @error: (allow-none): Return location of a #GError, or %NULL
 * @new_node: (allow-none) (out): Return location for the #DonnaNode of the
 * newly-created option; or %NULL
 * @fmt: <function>printf</function>-like format for the full/option/name
 * @...: <function>printf</function>-like arguments
 *
 * Creates a new category
 *
 * Will fail and return %FALSE if an option already exist, even if it is a
 * category.
 *
 * Returns: %TRUE if category was created, else %FALSE
 */
gboolean
donna_config_new_category (DonnaConfig            *config,
                           GError                **error,
                           DonnaNode             **new_node,
                           const gchar            *fmt,
                           ...)
{
    gint value; /* unused, for the the macro to work */
    _set_opt (G_TYPE_INVALID, NULL, FALSE, g_value_set_int, new_node);
}

/**
 * donna_config_set_boolean:
 * @config: The #DonnaConfig
 * @error: (allow-none): Return location of a #GError, or %NULL
 * @value: Value for the new option
 * @fmt: <function>printf</function>-like format for the full/option/name
 * @...: <function>printf</function>-like arguments
 *
 * Sets boolean option @fmt to @value. Create the option if it doesn't exist.
 * If the option exists, its value is changed unless it isn't compatible
 * (different type).
 *
 * Returns: %TRUE if option was created & set, else %FALSE
 */
gboolean
donna_config_set_boolean (DonnaConfig   *config,
                          GError       **error,
                          gboolean       value,
                          const gchar   *fmt,
                          ...)
{
    _set_opt (G_TYPE_BOOLEAN, NULL, TRUE, g_value_set_boolean, NULL);
}

/**
 * donna_config_set_int:
 * @config: The #DonnaConfig
 * @error: (allow-none): Return location of a #GError, or %NULL
 * @value: Value for the new option
 * @fmt: <function>printf</function>-like format for the full/option/name
 * @...: <function>printf</function>-like arguments
 *
 * Sets integer option @fmt to @value. Create the option if it doesn't exist.
 * If the option exists, its value is changed unless it isn't compatible
 * (different type, or @value invalid with the option's extra).
 *
 * Returns: %TRUE if option was set to @value, else %FALSE
 */
gboolean
donna_config_set_int (DonnaConfig   *config,
                      GError       **error,
                      gint           value,
                      const gchar   *fmt,
                      ...)
{
    _set_opt (G_TYPE_INT, NULL, TRUE, g_value_set_int, NULL);
}

/**
 * donna_config_set_double:
 * @config: The #DonnaConfig
 * @error: (allow-none): Return location of a #GError, or %NULL
 * @value: Value for the new option
 * @fmt: <function>printf</function>-like format for the full/option/name
 * @...: <function>printf</function>-like arguments
 *
 * Sets double option @fmt to @value. Create the option if it doesn't exist.
 * If the option exists, its value is changed unless it isn't compatible
 * (different type).
 *
 * Returns: %TRUE if option was created & set, else %FALSE
 */
gboolean
donna_config_set_double (DonnaConfig    *config,
                         GError        **error,
                         gdouble         value,
                         const gchar    *fmt,
                         ...)
{
    _set_opt (G_TYPE_DOUBLE, NULL, TRUE, g_value_set_double, NULL);
}

/**
 * donna_config_set_string:
 * @config: The #DonnaConfig
 * @error: (allow-none): Return location of a #GError, or %NULL
 * @value: Value for the new option
 * @fmt: <function>printf</function>-like format for the full/option/name
 * @...: <function>printf</function>-like arguments
 *
 * Sets string option @fmt to (a copy of) @value. Create the option if it
 * doesn't exist.
 * If the option exists, its value is changed unless it isn't compatible
 * (different type, or @value invalid with the option's extra).
 *
 * For @config to take ownership of @value instead, use
 * donna_config_take_string()
 *
 * Returns: %TRUE if option was set to @value, else %FALSE
 */
gboolean
donna_config_set_string (DonnaConfig         *config,
                         GError             **error,
                         const gchar         *value,
                         const gchar         *fmt,
                         ...)
{
    _set_opt (G_TYPE_STRING, NULL, TRUE, g_value_set_string, NULL);
}

/**
 * donna_config_set_string:
 * @config: The #DonnaConfig
 * @error: (allow-none): Return location of a #GError, or %NULL
 * @value: Value for the new option
 * @fmt: <function>printf</function>-like format for the full/option/name
 * @...: <function>printf</function>-like arguments
 *
 * Sets string option @fmt to @value, taking owernship (will use g_free() when
 * done). Create the option if it doesn't exist.
 * If the option exists, its value is changed unless it isn't compatible
 * (different type, or @value invalid with the option's extra).
 *
 * For @config to make a copy of @value instead, use donna_config_set_string()
 *
 * Returns: %TRUE if option was set to @value, else %FALSE
 */
gboolean
donna_config_take_string (DonnaConfig        *config,
                          GError            **error,
                          gchar              *value,
                          const gchar        *fmt,
                          ...)
{
    _set_opt (G_TYPE_STRING, NULL, TRUE, g_value_take_string, NULL);
}

struct extra
{
    const gchar *name;
    DonnaConfigExtra *extra;
};

enum
{
    SO_INIT,
    SO_INIT_WITH_DEFAULT_VALUE,
    SO_RUNNING,
    SO_CONFIRMED
};

struct set_option
{
    GtkWidget *win;
    GtkInfoBar *infobar;
    GtkLabel *lblerr;
    GtkGrid *grid;
    GtkComboBox *combo;
    GtkEntry *entry;
    gint row_value;
    guint state;
    const gchar *type;
    gchar *name;
    GValue *value;
    GArray *extras;
};

static void
btn_clicked (struct set_option *so)
{
    GtkWidget *w;
    const gchar *type;

    w = gtk_grid_get_child_at (so->grid, 1, so->row_value);

    type = gtk_combo_box_get_active_id (so->combo);
    if (!is_valid_name (gtk_entry_get_text (so->entry),
                (streq (type, ":category")) ? VALID_CATEGORY_NAME : VALID_OPTION_NAME))
    {
        gtk_info_bar_set_message_type (so->infobar, GTK_MESSAGE_ERROR);
        gtk_label_set_text (so->lblerr, "Invalid name");
        gtk_widget_show ((GtkWidget *) so->infobar);
        return;
    }

    if (streq (type, ":category"))
    {
        /* done */
    }
    else if (streq (type, ":string"))
    {
        if (streq (gtk_entry_get_text ((GtkEntry *) w), ""))
        {
            gtk_info_bar_set_message_type (so->infobar, GTK_MESSAGE_ERROR);
            gtk_label_set_text (so->lblerr, "Value missing");
            gtk_widget_show ((GtkWidget *) so->infobar);
            return;
        }

        if (!G_VALUE_HOLDS (so->value, G_TYPE_STRING))
        {
            g_value_unset (so->value);
            g_value_init (so->value, G_TYPE_STRING);
        }
        g_value_set_string (so->value, gtk_entry_get_text ((GtkEntry *) w));
    }
    else if (streq (type, ":int"))
    {
        if (streq (gtk_entry_get_text ((GtkEntry *) w), ""))
        {
            gtk_info_bar_set_message_type (so->infobar, GTK_MESSAGE_ERROR);
            gtk_label_set_text (so->lblerr, "Value missing");
            gtk_widget_show ((GtkWidget *) so->infobar);
            return;
        }

        if (!G_VALUE_HOLDS (so->value, G_TYPE_INT))
        {
            g_value_unset (so->value);
            g_value_init (so->value, G_TYPE_INT);
        }
        g_value_set_int (so->value, (gint) g_ascii_strtoll (
                    gtk_entry_get_text ((GtkEntry *) w), NULL, 10));
    }
    else if (streq (type, ":boolean"))
    {
        if (!gtk_combo_box_get_active_id ((GtkComboBox *) w))
        {
            gtk_info_bar_set_message_type (so->infobar, GTK_MESSAGE_ERROR);
            gtk_label_set_text (so->lblerr, "Value missing");
            gtk_widget_show ((GtkWidget *) so->infobar);
            return;
        }

        if (!G_VALUE_HOLDS (so->value, G_TYPE_BOOLEAN))
        {
            g_value_unset (so->value);
            g_value_init (so->value, G_TYPE_BOOLEAN);
        }
        g_value_set_boolean (so->value, streq ("t",
                    gtk_combo_box_get_active_id ((GtkComboBox *) w)));
    }
    else
    {
        DonnaConfigExtra *_e;
        gint i;

        for (i = 0; (guint) i < so->extras->len; ++i)
        {
            struct extra *e = &g_array_index (so->extras, struct extra, i);
            if (streq (type, e->name))
            {
                _e = e->extra;
                break;
            }
        }
        if (G_UNLIKELY ((guint) i >= so->extras->len))
        {
            gtk_info_bar_set_message_type (so->infobar, GTK_MESSAGE_ERROR);
            gtk_label_set_text (so->lblerr, "Internal error");
            gtk_widget_show ((GtkWidget *) so->infobar);
            return;
        }

        if (_e->any.type == DONNA_CONFIG_EXTRA_TYPE_LIST)
        {
            const gchar *id = gtk_combo_box_get_active_id ((GtkComboBox *) w);
            if (!id)
            {
                gtk_info_bar_set_message_type (so->infobar, GTK_MESSAGE_ERROR);
                gtk_label_set_text (so->lblerr, "Value missing");
                gtk_widget_show ((GtkWidget *) so->infobar);
                return;
            }

            if (!G_VALUE_HOLDS (so->value, G_TYPE_STRING))
            {
                g_value_unset (so->value);
                g_value_init (so->value, G_TYPE_STRING);
            }
            g_value_set_string (so->value, id);
        }
        else if (_e->any.type == DONNA_CONFIG_EXTRA_TYPE_LIST_INT)
        {
            const gchar *id = gtk_combo_box_get_active_id ((GtkComboBox *) w);

            if (!id)
            {
                gtk_info_bar_set_message_type (so->infobar, GTK_MESSAGE_ERROR);
                gtk_label_set_text (so->lblerr, "Value missing");
                gtk_widget_show ((GtkWidget *) so->infobar);
                return;
            }

            if (!G_VALUE_HOLDS (so->value, G_TYPE_INT))
            {
                g_value_unset (so->value);
                g_value_init (so->value, G_TYPE_INT);
            }
            /* XXX if we made the model ourselves, we could get the active
             * iter and have the value on that row, no loop required */
            for (i = 0; i < _e->any.nb_items; ++i)
            {
                DonnaConfigItemExtraListInt *it;

                it = &_e->list_int.items[i];
                if (streq (id, it->in_file))
                {
                    g_value_set_int (so->value, it->value);
                    break;
                }
            }
        }
        else if (_e->any.type == DONNA_CONFIG_EXTRA_TYPE_LIST_FLAGS)
        {
            GList *list;
            GList *l;
            gint val = 0;

            if (!G_VALUE_HOLDS (so->value, G_TYPE_INT))
            {
                g_value_unset (so->value);
                g_value_init (so->value, G_TYPE_INT);
            }

            list = gtk_container_get_children ((GtkContainer *) w);
            for (l = list; l; l = l->next)
            {
                GtkToggleButton *tb = l->data;

                if (gtk_toggle_button_get_active (tb))
                    val |= GPOINTER_TO_INT (g_object_get_data ((GObject *) tb,
                                "flag-value"));
            }
            g_list_free (list);
            g_value_set_int (so->value, val);
        }
    }

    so->state = SO_CONFIRMED;
    so->type = type;
    so->name = g_strdup (gtk_entry_get_text (so->entry));
    gtk_widget_destroy (so->win);
}

static gboolean
combo_is_row_sep (GtkTreeModel *model, GtkTreeIter *iter, gpointer data)
{
    gchar *s;
    gboolean ret;

    gtk_tree_model_get (model, iter, 1, &s, -1);
    ret = streq (s, ":-");
    g_free (s);
    return ret;
}

static void
combo_changed (struct set_option *so)
{
    GtkWidget *w;
    const gchar *type;

    w = gtk_grid_get_child_at (so->grid, 1, so->row_value);
    if (w)
        gtk_widget_destroy (w);

    type = gtk_combo_box_get_active_id (so->combo);
    if (G_UNLIKELY (!type))
        return;

    if (streq (type, ":category"))
        w = NULL;
    else if (streq (type, ":string"))
    {
        w = gtk_entry_new ();
        if (so->state == SO_INIT_WITH_DEFAULT_VALUE
                && G_VALUE_HOLDS (so->value, G_TYPE_STRING)
                && g_value_get_string (so->value))
            gtk_entry_set_text ((GtkEntry *) w, g_value_get_string (so->value));
    }
    else if (streq (type, ":int"))
    {
        w = gtk_entry_new ();
        if (so->state == SO_INIT_WITH_DEFAULT_VALUE
                && G_VALUE_HOLDS (so->value, G_TYPE_INT))
        {
            gchar *s = g_strdup_printf ("%d", g_value_get_int (so->value));
            gtk_entry_set_text ((GtkEntry *) w, s);
            g_free (s);
        }
    }
    else if (streq (type, ":boolean"))
    {
        w = gtk_combo_box_text_new ();
        gtk_combo_box_text_append ((GtkComboBoxText *) w, "t", "True");
        gtk_combo_box_text_append ((GtkComboBoxText *) w, "f", "False");

        if (so->state == SO_INIT_WITH_DEFAULT_VALUE
                && G_VALUE_HOLDS (so->value, G_TYPE_BOOLEAN))
            gtk_combo_box_set_active_id ((GtkComboBox *) w,
                    (g_value_get_boolean (so->value)) ? "t" : "f");
        else
            gtk_combo_box_set_active_id ((GtkComboBox *) w, "t");
    }
    else
    {
        DonnaConfigExtra *_e = NULL;
        gint i;

        for (i = 0; (guint) i < so->extras->len; ++i)
        {
            struct extra *e = &g_array_index (so->extras, struct extra, i);
            if (streq (type, e->name))
            {
                _e = e->extra;
                break;
            }
        }

        if (_e->any.type == DONNA_CONFIG_EXTRA_TYPE_LIST)
        {
            w = gtk_combo_box_text_new ();
            for (i = 0; i < _e->any.nb_items; ++i)
            {
                DonnaConfigItemExtraList *it;

                it = &_e->list.items[i];
                gtk_combo_box_text_append ((GtkComboBoxText *) w,
                        it->value,
                        (it->label) ? it->label : it->value);
            }

            if (so->state == SO_INIT_WITH_DEFAULT_VALUE
                    && G_VALUE_HOLDS (so->value, G_TYPE_STRING))
                gtk_combo_box_set_active_id ((GtkComboBox *) w,
                        g_value_get_string (so->value));
            else
                gtk_combo_box_set_active ((GtkComboBox *) w, 0);
        }
        else if (_e->any.type == DONNA_CONFIG_EXTRA_TYPE_LIST_INT)
        {
            w = gtk_combo_box_text_new ();
            for (i = 0; i < _e->any.nb_items; ++i)
            {
                DonnaConfigItemExtraListInt *it;

                it = &_e->list_int.items[i];
                gtk_combo_box_text_append ((GtkComboBoxText *) w,
                        it->in_file,
                        (it->label) ? it->label : it->in_file);
                if (so->state == SO_INIT_WITH_DEFAULT_VALUE
                        && G_VALUE_HOLDS (so->value, G_TYPE_INT)
                        && it->value == g_value_get_int (so->value))
                    gtk_combo_box_set_active_id ((GtkComboBox *) w, it->in_file);
            }

            if (!(so->state == SO_INIT_WITH_DEFAULT_VALUE
                        && G_VALUE_HOLDS (so->value, G_TYPE_INT)))
                gtk_combo_box_set_active ((GtkComboBox *) w, 0);
        }
        else /* DONNA_CONFIG_EXTRA_TYPE_LIST_FLAGS */
        {
            w = gtk_box_new (GTK_ORIENTATION_VERTICAL, 0);

            for (i = 0; i < _e->any.nb_items; ++i)
            {
                DonnaConfigItemExtraListFlags *it;
                GtkWidget *cb;

                it = &_e->list_int.items[i];
                cb = gtk_check_button_new_with_label (
                        (it->label) ? it->label : it->in_file);
                g_object_set_data ((GObject *) cb, "flag-value",
                        GINT_TO_POINTER (it->value));
                gtk_box_pack_start ((GtkBox *) w, cb, 0, 0, FALSE);

                if (so->state == SO_INIT_WITH_DEFAULT_VALUE
                        && G_VALUE_HOLDS (so->value, G_TYPE_INT)
                        && (it->value & g_value_get_int (so->value)))
                    gtk_toggle_button_set_active ((GtkToggleButton *) cb, TRUE);
            }
        }
    }

    if (w)
    {
        gtk_grid_attach (so->grid, w, 1, so->row_value, 1, 1);
        gtk_widget_show_all (w);
    }

    if (so->state != SO_RUNNING)
        so->state = SO_RUNNING;

    /* the grid will grow when needed, but it won't shrink. Event in a box w/
     * both expand & fill, it'll always shrink if it can. So, because that
     * doesn't look good, let's have the window reduced to its minimum size as
     * well */
    gtk_window_resize ((GtkWindow *) so->win, 1, 1);
}

static gint
arr_extras_cmp (gconstpointer extra1, gconstpointer extra2)
{
    const struct extra *e1 = extra1;
    const struct extra *e2 = extra2;

    return strcmp (e1->extra->any.title, e2->extra->any.title);
}

/**
 * donna_config_set_option:
 * @config: The #DonnaConfig
 * @error: (allow-none): Return location of a #GError, or %NULL
 * @node: (allow-none) (out): Return location for the #DonnaNode of the option;
 * or %NULL
 * @create_only: %TRUE to only create option (i.e. fail if already exists, even
 * if of same type)
 * @ask_user: %TRUE if a window will be shown, for the user to see/change the
 * option name/type/value; possibly cancelling the operation
 * @type: (allow-none): Name of the type of the option
 * @name: (allow-none): Name of the option to set/create
 * @value: (allow-none): String representation of the value to set
 * @fmt: <function>printf</function>-like format for the full/option/name of he
 * parent to create the option in
 * @...: <function>printf</function>-like arguments
 *
 * Creates/sets an option to the value represented by @value
 *
 * If @create_only is %TRUE only option creation will be performed, i.e. if the
 * option exists it will fail (even if option is of the same type)
 *
 * If @ask_user is %TRUE then a window will be shown, allowing user to change
 * the name, type and value of the option to set/create. In that case, @value
 * will be used as default value, and if it wasn't specified and the option
 * exists, then the current value will be used as default.
 * The window will have the CSS id/name "config-set-option"
 *
 * @type must be either ":category", ":boolean", ":int", ":string" or the name
 * of an extra.
 * Note that this can therefore obviously be used to create a category.
 *
 * @value must be the string representation of the value to set. So for extras
 * you should use the "in-file" values (i.e. what's found/used in the conf
 * file).
 *
 * Returns: %TRUE if option was set, else %FALSE
 */
gboolean
donna_config_set_option (DonnaConfig            *config,
                         GError                **error,
                         DonnaNode             **node,
                         gboolean                create_only,
                         gboolean                ask_user,
                         const gchar            *type,
                         const gchar            *name,
                         const gchar            *value,
                         const gchar            *fmt,
                         ...)
{
    DonnaProviderConfigPrivate *priv;
    DonnaConfigExtra *_e = NULL;
    GNode *parent;
    GNode *gnode;
    GValue v = G_VALUE_INIT;
    va_list va_args;
    gchar *parent_name;
    gboolean value_imported = FALSE;
    gboolean ret;

    g_return_val_if_fail (DONNA_IS_PROVIDER_CONFIG (config), FALSE);
    g_return_val_if_fail (fmt != NULL, FALSE);
    priv = config->priv;

    va_start (va_args, fmt);
    parent_name = g_strdup_vprintf (fmt, va_args);
    va_end (va_args);

    g_rw_lock_reader_lock (&priv->lock);

    /* make sure the parent option exists & is a category */
    parent = get_option_node (priv->root, parent_name);
    if (!parent)
    {
        g_rw_lock_reader_unlock (&priv->lock);
        g_set_error (error, DONNA_CONFIG_ERROR,
                DONNA_CONFIG_ERROR_NOT_FOUND,
                "Config: Parent option '%s' not found",
                parent_name);
        g_free (parent_name);
        return FALSE;
    }
    else if (!option_is_category (parent->data, priv->root))
    {
        g_rw_lock_reader_unlock (&priv->lock);
        g_set_error (error, DONNA_CONFIG_ERROR,
                DONNA_CONFIG_ERROR_INVALID_TYPE,
                "Config: Parent option '%s' isn't a category",
                parent_name);
        g_free (parent_name);
        return FALSE;
    }

    if (type)
    {
        if (*type == ':')
        {
            if (streq (type + 1, "category"))
                /* we need v to be init */
                g_value_init (&v, G_TYPE_POINTER);
            else if (streq (type + 1, "int"))
                g_value_init (&v, G_TYPE_INT);
            else if (streq (type + 1, "string"))
                g_value_init (&v, G_TYPE_STRING);
            else if (streq (type + 1, "boolean"))
                g_value_init (&v, G_TYPE_BOOLEAN);
            else
            {
                g_rw_lock_reader_unlock (&priv->lock);
                g_set_error (error, DONNA_CONFIG_ERROR,
                        DONNA_CONFIG_ERROR_INVALID_TYPE,
                        "Config: Invalid type '%s'",
                        type);
                g_free (parent_name);
                return FALSE;
            }
        }
        else /* extra */
        {
            _e = g_hash_table_lookup (priv->extras, type);
            if (!_e)
            {
                g_rw_lock_reader_unlock (&priv->lock);
                g_set_error (error, DONNA_CONFIG_ERROR,
                        DONNA_CONFIG_ERROR_INVALID_TYPE,
                        "Config: Invalid type '%s'",
                        type);
                g_free (parent_name);
                return FALSE;
            }

            if (_e->any.type == DONNA_CONFIG_EXTRA_TYPE_LIST)
                g_value_init (&v, G_TYPE_STRING);
            else
                g_value_init (&v, G_TYPE_INT);
        }
    }

    /* get current option, if exists & we're not in "new" mode */
    if (!create_only && name && (is_valid_name (name, VALID_OPTION_NAME)
                || is_valid_name (name, VALID_CATEGORY_NAME)))
    {
        gnode = get_child_node (parent, name, strlen (name));
        if (gnode)
        {
            struct option *option = gnode->data;

            /* load default type from current option if none given */
            if (!type)
            {
                if (option_is_category (option, priv->root))
                {
                    type = ":category";
                    /* we need v to be init */
                    g_value_init (&v, G_TYPE_POINTER);
                }
                else
                {
                    if (option->extra)
                        type = option->extra;
                    else if (G_VALUE_HOLDS (&option->value, G_TYPE_STRING))
                        type = ":string";
                    else if (G_VALUE_HOLDS (&option->value, G_TYPE_INT))
                        type = ":int";
                    else if (G_VALUE_HOLDS (&option->value, G_TYPE_BOOLEAN))
                        type = ":boolean";
                    g_value_init (&v, G_VALUE_TYPE (&option->value));
                }
            }

            /* if ask_user & no value, we can load default from current option.
             * Only if it's an option of the same type, ofc */
            if (ask_user && !value && !option_is_category (option, priv->root)
                    && G_VALUE_TYPE (&v) == G_VALUE_TYPE (&option->value))
            {
                g_value_copy (&option->value, &v);
                value_imported = TRUE;
            }
        }
    }
    else
        gnode = NULL;

    /* is a value was provided, make sure it fits the type & convert it */
    if (value)
    {
        if (type && *type != ':')
        {
            gpointer ptr;
            gchar *s;
            gint i;

            if (!_e)
                _e = g_hash_table_lookup (priv->extras, type);

            if (_e->any.type == DONNA_CONFIG_EXTRA_TYPE_LIST)
                ptr = &s;
            else
                ptr = &i;

            if (!get_extra_value (_e, value, ptr))
            {
                g_rw_lock_reader_unlock (&priv->lock);
                g_set_error (error, DONNA_CONFIG_ERROR,
                        DONNA_CONFIG_ERROR_INVALID_NAME,
                        "Config: Cannot set option '%s', invalid value '%s' for extra '%s'",
                        name, value, type);
                g_value_unset (&v);
                g_free (parent_name);
                return FALSE;
            }

            if (_e->any.type == DONNA_CONFIG_EXTRA_TYPE_LIST)
                g_value_set_string (&v, s);
            else
                g_value_set_int (&v, i);
        }
        else if (G_VALUE_HOLDS (&v, G_TYPE_STRING))
            g_value_set_string (&v, value);
        else if (G_VALUE_HOLDS (&v, G_TYPE_INT))
            g_value_set_int (&v, (gint) g_ascii_strtoll (value, NULL, 10));
        else if (G_VALUE_HOLDS (&v, G_TYPE_BOOLEAN))
        {
            if (streq (value, "true") || streq (value, "1"))
                g_value_set_boolean (&v, TRUE);
            else if (streq (value, "false") || streq (value, "0"))
                g_value_set_boolean (&v, FALSE);
            else
            {
                g_rw_lock_reader_unlock (&priv->lock);
                g_set_error (error, DONNA_CONFIG_ERROR,
                        DONNA_CONFIG_ERROR_INVALID_NAME,
                        "Config: Cannot set option '%s', invalid value '%s'; "
                        "Expected 'true', '1', 'false' or '0'",
                        name, value);
                g_value_unset (&v);
                g_free (parent_name);
                return FALSE;
            }
        }
    }

    /* time to show the UI and ask user */
    if (ask_user)
    {
        GMainLoop *loop;
        GtkWidget *btn_box;
        GtkBox *box;
        GtkWidget *w;
        struct set_option so;
        struct extra so_extra;
        GHashTableIter iter;
        gint i = 0;
        guint nb;

        so.state = (value || value_imported) ? SO_INIT_WITH_DEFAULT_VALUE : SO_INIT;
        so.value = &v;
        so.extras = g_array_sized_new (FALSE, FALSE, sizeof (struct extra),
                g_hash_table_size (priv->extras));
        g_hash_table_iter_init (&iter, priv->extras);
        while (g_hash_table_iter_next (&iter,
                    (gpointer) &so_extra.name, (gpointer) &so_extra.extra))
            g_array_append_val (so.extras, so_extra);
        g_array_sort (so.extras, arr_extras_cmp);

        g_rw_lock_reader_unlock (&priv->lock);

        so.win = gtk_window_new (GTK_WINDOW_TOPLEVEL);
        gtk_widget_set_name (so.win, "config-set-option");
        donna_app_add_window (priv->app, (GtkWindow *) so.win, TRUE);
        gtk_window_set_default_size ((GtkWindow *) so.win, 230, -1);
        gtk_window_set_decorated ((GtkWindow *) so.win, FALSE);
        gtk_window_set_has_resize_grip ((GtkWindow *) so.win, FALSE);
        gtk_container_set_border_width ((GtkContainer *) so.win, 4);

        w = gtk_box_new (GTK_ORIENTATION_VERTICAL, 0);
        box = (GtkBox *) w;
        gtk_container_add ((GtkContainer *) so.win, w);

        so.infobar = (GtkInfoBar *) gtk_info_bar_new ();
        so.lblerr = (GtkLabel *) gtk_label_new (NULL);
        gtk_widget_show ((GtkWidget *) so.lblerr);
        gtk_container_add ((GtkContainer *) gtk_info_bar_get_content_area (so.infobar),
                (GtkWidget *) so.lblerr);
        gtk_widget_set_no_show_all ((GtkWidget *) so.infobar, TRUE);
        gtk_box_pack_start (box, (GtkWidget *) so.infobar, TRUE, TRUE, 0);

        so.grid = (GtkGrid *) gtk_grid_new ();
        g_object_set (so.grid, "expand", TRUE, NULL);
        gtk_grid_set_column_spacing (so.grid, 4);
        gtk_box_pack_start (box, (GtkWidget *) so.grid, TRUE, TRUE, 0);

        i = 0;

        w = gtk_label_new ((create_only) ? "New option" : "Set option");
        gtk_style_context_add_class (gtk_widget_get_style_context (w), "title");
        gtk_grid_attach (so.grid, w, 0, i, 2, 1);
        ++i;

        w = gtk_label_new ("Parent:");
        gtk_misc_set_alignment ((GtkMisc *) w, 1.0, 0.5);
        gtk_grid_attach (so.grid, w, 0, i, 1, 1);
        w = gtk_label_new (parent_name);
        gtk_misc_set_alignment ((GtkMisc *) w, 0.0, 0.5);
        gtk_grid_attach (so.grid, w, 1, i, 1, 1);
        ++i;

        w = gtk_label_new ("Type:");
        gtk_misc_set_alignment ((GtkMisc *) w, 1.0, 0.5);
        gtk_grid_attach (so.grid, w, 0, i, 1, 1);
        w = gtk_combo_box_text_new ();
        so.combo = (GtkComboBox *) w;
        gtk_combo_box_set_row_separator_func (so.combo, combo_is_row_sep, NULL, NULL);
        gtk_combo_box_text_append ((GtkComboBoxText *) w, ":category", "Category");
        gtk_combo_box_text_append ((GtkComboBoxText *) w, ":-", "-");
        gtk_combo_box_text_append ((GtkComboBoxText *) w, ":boolean", "Boolean");
        gtk_combo_box_text_append ((GtkComboBoxText *) w, ":int", "Integer");
        gtk_combo_box_text_append ((GtkComboBoxText *) w, ":string", "String");
        gtk_combo_box_text_append ((GtkComboBoxText *) w, ":-", "-");
        for (nb = 0; nb < so.extras->len; ++nb)
        {
            struct extra *e = &g_array_index (so.extras, struct extra, nb);
            gtk_combo_box_text_append ((GtkComboBoxText *) w,
                    e->name, e->extra->any.title);
        }
        gtk_grid_attach (so.grid, w, 1, i, 1, 1);
        ++i;

        w = gtk_label_new ("Name:");
        gtk_misc_set_alignment ((GtkMisc *) w, 1.0, 0.5);
        gtk_grid_attach (so.grid, w, 0, i, 1, 1);
        w = gtk_entry_new ();
        so.entry = (GtkEntry *) w;
        if (name)
            gtk_entry_set_text ((GtkEntry *) w, name);
        gtk_grid_attach (so.grid, w, 1, i, 1, 1);
        ++i;

        w = gtk_label_new ("Value:");
        gtk_misc_set_alignment ((GtkMisc *) w, 1.0, 0.0);
        gtk_misc_set_padding ((GtkMisc *) w, 0, 4);
        gtk_grid_attach (so.grid, w, 0, i, 1, 1);
        so.row_value = i;
        ++i;

        btn_box = gtk_box_new (GTK_ORIENTATION_HORIZONTAL, 0);
        gtk_box_pack_start (box, btn_box, FALSE, FALSE, 4);

        w = gtk_button_new_with_label ("Ok");
        gtk_button_set_image ((GtkButton *) w,
                gtk_image_new_from_icon_name ("gtk-ok", GTK_ICON_SIZE_MENU));
        g_signal_connect_swapped (w, "clicked", (GCallback) btn_clicked, &so);
        gtk_box_pack_end ((GtkBox *) btn_box, w, FALSE, FALSE, 2);

        w = gtk_button_new_with_label ("Cancel");
        gtk_button_set_image ((GtkButton *) w,
                gtk_image_new_from_icon_name ("gtk-cancel", GTK_ICON_SIZE_MENU));
        g_signal_connect_swapped (w, "clicked", (GCallback) gtk_widget_destroy, so.win);
        gtk_box_pack_end ((GtkBox *) btn_box, w, FALSE, FALSE, 2);

        g_signal_connect_swapped (so.combo, "changed",
                (GCallback) combo_changed, &so);
        gtk_combo_box_set_active_id (so.combo, (type) ? type : ":string");

        loop = g_main_loop_new (NULL, TRUE);
        g_signal_connect_swapped (so.win, "destroy", (GCallback) g_main_loop_quit, loop);

        gtk_widget_show_all (so.win);
        g_main_loop_run (loop);
        g_array_free (so.extras, TRUE);

        if (so.state != SO_CONFIRMED)
        {
            /* user cancelled */
            g_free (parent_name);
            return FALSE;
        }

        type = so.type;
        name = so.name;
    }
    else
    {
        g_rw_lock_reader_unlock (&priv->lock);

        if (!type)
        {
            g_set_error (error, DONNA_CONFIG_ERROR,
                    DONNA_CONFIG_ERROR_OTHER,
                    "Config: Cannot set option, no type specified");
            g_free (parent_name);
            return FALSE;
        }

        if (!name)
        {
            g_set_error (error, DONNA_CONFIG_ERROR,
                    DONNA_CONFIG_ERROR_OTHER,
                    "Config: Cannot set option, no name specified");
            g_value_unset (&v);
            g_free (parent_name);
            return FALSE;
        }
        else if (streq (type, ":category"))
        {
            if (!is_valid_name (name, VALID_CATEGORY_NAME))
            {
                g_set_error (error, DONNA_CONFIG_ERROR,
                        DONNA_CONFIG_ERROR_INVALID_NAME,
                        "Config: Cannot set category '%s', invalid name",
                        name);
                g_value_unset (&v);
                g_free (parent_name);
                return FALSE;
            }
        }
        else if (!is_valid_name (name, VALID_OPTION_NAME))
        {
            g_set_error (error, DONNA_CONFIG_ERROR,
                    DONNA_CONFIG_ERROR_INVALID_NAME,
                    "Config: Cannot set option '%s', invalid name",
                    name);
            g_value_unset (&v);
            g_free (parent_name);
            return FALSE;
        }

        if (!value)
        {
            g_set_error (error, DONNA_CONFIG_ERROR,
                    DONNA_CONFIG_ERROR_INVALID_NAME,
                    "Config: Cannot set option '%s', no value specified",
                    name);
            g_value_unset (&v);
            g_free (parent_name);
            return FALSE;
        }
    }

    /* except for category, type is the extra (if any) */
    if (type && *type == ':' && !streq (type + 1, "category"))
        type = NULL;

    ret = _set_option (config, error, node,
            /* G_TYPE_INVALID means category */
            (type && *type == ':') ? G_TYPE_INVALID : G_VALUE_TYPE (&v),
            (!type || *type == ':') ? NULL : type,
            &v,
            !create_only,
            "%s/%s", (streq (parent_name, "/")) ? "" : parent_name, name);

    if (ask_user)
        g_free ((gchar *) name);
    g_value_unset (&v);
    g_free (parent_name);
    return ret;
}

/**
 * donna_config_rename_option:
 * @config: The #DonnaConfig
 * @error: (allow-none): Return location of a #GError, or %NULL
 * @new_name: New name for the option
 * @fmt: <function>printf</function>-like format for the full/option/name
 * @...: <function>printf</function>-like arguments
 *
 * Rename option @fmt to @new_name
 *
 * Returns: %TRUE if option was renamed, else %FALSE
 */
gboolean
donna_config_rename_option (DonnaConfig            *config,
                            GError                **error,
                            const gchar            *new_name,
                            const gchar            *fmt,
                            ...)
{
    DonnaProviderConfigPrivate *priv;
    struct option *option;
    DonnaNode *parent_node = NULL;
    DonnaNode *child_node  = NULL;
    DonnaNode *option_node = NULL;
    gchar *new_location;
    GNode *node;
    GNode *parent;
    const gchar *s;
    gchar *name;
    va_list va_arg;
    gboolean ret = FALSE;

    g_return_val_if_fail (DONNA_IS_PROVIDER_CONFIG (config), FALSE);
    g_return_val_if_fail (new_name != NULL, FALSE);
    g_return_val_if_fail (fmt != NULL, FALSE);
    priv = config->priv;

    if (!is_valid_name (new_name, VALID_OPTION_NAME))
    {
        g_set_error (error, DONNA_CONFIG_ERROR,
                DONNA_CONFIG_ERROR_INVALID_NAME,
                "Config: Cannot rename to '%s': Invalid name",
                new_name);
        return FALSE;
    }

    va_start (va_arg, fmt);
    name = g_strdup_vprintf (fmt, va_arg);
    va_end (va_arg);

    g_rw_lock_writer_lock (&priv->lock);
    s = strrchr ((*name == '/') ? name + 1 : name, '/');
    if (s)
    {
        parent = ensure_categories (config, name, (gsize) (s - name),
                &parent_node, &child_node);
        if (!parent)
        {
            g_rw_lock_writer_unlock (&priv->lock);
            g_set_error (error, DONNA_CONFIG_ERROR,
                    DONNA_CONFIG_ERROR_NOT_FOUND,
                    "Config: Parent of option '%s' not found",
                    (*name == '/') ? name + 1 : name);
            g_free (name);
            goto done;
        }
        ++s;
    }
    else
    {
        s = (*name == '/') ? name + 1 : name;
        parent = priv->root;
    }

    /* make sure the option exists/isn't a category */
    node = get_child_node (parent, s, strlen (s));
    if (!node)
    {
        g_rw_lock_writer_unlock (&priv->lock);
        g_set_error (error, DONNA_CONFIG_ERROR,
                DONNA_CONFIG_ERROR_NOT_FOUND,
                "Config: Cannot rename option '%s': Doesn't exist",
                (*name == '/') ? name + 1 : name);
        g_free (name);
        goto done;
    }
    if (option_is_category (node->data, priv->root))
    {
        g_rw_lock_writer_unlock (&priv->lock);
        g_set_error (error, DONNA_CONFIG_ERROR,
                DONNA_CONFIG_ERROR_INVALID_TYPE,
                "Config: Cannot rename option '%s': It is a category",
                (*name == '/') ? name + 1 : name);
        g_free (name);
        goto done;
    }

    /* make sure the new name is available */
    if (get_child_node (parent, new_name, strlen (new_name)))
    {
        g_rw_lock_writer_unlock (&priv->lock);
        g_set_error (error, DONNA_CONFIG_ERROR,
                DONNA_CONFIG_ERROR_ALREADY_EXISTS,
                "Config: Cannot rename option '%s' to '%s': Option already exists",
                (*name == '/') ? name + 1 : name, new_name);
        g_free (name);
        goto done;
    }

    /* perform the rename */
    option = (struct option *) node->data;
    option->name = str_chunk (priv, new_name);

    /* get the new location of the node */
    new_location = get_option_full_name (priv->root, node);

    /* if there's a node, we need to update it now, but without any signals
     * emitted under the lock */
    if (option->node)
    {
        GValue v = G_VALUE_INIT;

        g_value_init (&v, G_TYPE_STRING);
        g_value_set_string (&v, new_location);
        donna_node_set_property_value_no_signal (option->node, "location", &v);
        g_value_unset (&v);

        g_value_init (&v, G_TYPE_STRING);
        g_value_set_static_string (&v, option->name);
        donna_node_set_property_value_no_signal (option->node, "name", &v);
        g_value_unset (&v);

        /* make sure we can ref the node (to emit signal) without problem.  That
         * is, if ref_count == 1 it means we're holding the only ref left, i.e.
         * we have a toggle_ref waiting for the lock to unref the node, and
         * trying to add a ref would deadlock */
        if (((GObject *) option->node)->ref_count > 1)
            option_node = g_object_ref (option->node);
    }

    /* we're done with the writer lock, signals go after to avoid deadlocks */
    g_rw_lock_writer_unlock (&priv->lock);

    /* old name/option was deleted */
    config_option_deleted (config, (*name == '/') ? name + 1 : name);
    g_free (name);

    /* new name/option was set */
    config_option_set (config, new_location + 1); /* +1 to skip leading slash */
    g_free (new_location);

    /* emit signals on node outside of lock */
    if (option_node)
    {
        donna_provider_node_updated ((DonnaProvider *) config, option_node,
                "location");
        donna_provider_node_updated ((DonnaProvider *) config, option_node,
                "name");
        g_object_unref (option_node);
    }

    ret = TRUE;

done:
    if (child_node)
    {
        donna_provider_node_new_child ((DonnaProvider *) config,
                parent_node, child_node);
        g_object_unref (parent_node);
        g_object_unref (child_node);
    }

    return ret;
}

struct renaming_category
{
    GNode     *gnode;
    gchar     *parent_name;
    GPtrArray *names;
    GPtrArray *nodes;
    GString   *str_prefix;
};

static gboolean
traverse_renaming_category (GNode *node, struct renaming_category *data)
{
    struct option *option = node->data;
    GString *str;

    /* skip the category we're renaming */
    if (node == data->gnode)
        /* keep iterating */
        return FALSE;

    str = g_string_new (option->name);
    for (node = node->parent; node && node != data->gnode; node = node->parent)
    {
        g_string_prepend_c (str, '/');
        g_string_prepend (str, ((struct option *) node->data)->name);
    }
    g_string_prepend_c (str, '/');
    /* takes ownership of str->str */
    g_ptr_array_add (data->names, str->str);

    if (option->node)
    {
        GValue v = G_VALUE_INIT;

        g_value_init (&v, G_TYPE_STRING);
        g_value_take_string (&v, g_strconcat ("/", data->str_prefix->str,
                    data->parent_name, str->str, NULL));
        donna_node_set_property_value_no_signal (option->node, "location", &v);
        g_value_unset (&v);

        /* add a ref to make sure it doesn't go away before we sent the signal */
        g_ptr_array_add (data->nodes, g_object_ref (option->node));
    }

    g_string_free (str, FALSE);

    /* keep iterating */
    return FALSE;
}

/**
 * donna_config_rename_category:
 * @config: The #DonnaConfig
 * @error: (allow-none): Return location of a #GError, or %NULL
 * @new_name: New name for the category
 * @fmt: <function>printf</function>-like format for the full/option/name
 * @...: <function>printf</function>-like arguments
 *
 * Rename category @fmt to @new_name
 *
 * Returns: %TRUE if category was renamed, else %FALSE
 */
gboolean
donna_config_rename_category (DonnaConfig            *config,
                              GError                **error,
                              const gchar            *new_name,
                              const gchar            *fmt,
                              ...)
{
    DonnaProviderConfigPrivate *priv;
    struct renaming_category data;
    const gchar *old_name;
    struct option *option;
    DonnaNode *parent_node = NULL;
    DonnaNode *child_node  = NULL;
    DonnaNode *option_node = NULL;
    GNode *node;
    GNode *parent;
    GNode *n;
    gchar *s;
    gchar *name;
    va_list va_arg;
    guint i;
    gboolean ret = FALSE;

    g_return_val_if_fail (DONNA_IS_PROVIDER_CONFIG (config), FALSE);
    g_return_val_if_fail (new_name != NULL, FALSE);
    g_return_val_if_fail (fmt != NULL, FALSE);
    priv = config->priv;

    if (!is_valid_name (new_name, VALID_CATEGORY_NAME))
    {
        g_set_error (error, DONNA_CONFIG_ERROR,
                DONNA_CONFIG_ERROR_INVALID_NAME,
                "Config: Cannot rename to '%s': Invalid name",
                new_name);
        return FALSE;
    }

    va_start (va_arg, fmt);
    name = g_strdup_vprintf (fmt, va_arg);
    va_end (va_arg);

    g_rw_lock_writer_lock (&priv->lock);
    s = strrchr ((*name == '/') ? name + 1 : name, '/');
    if (s)
    {
        parent = ensure_categories (config, name, (gsize) (s - name),
                &parent_node, &child_node);
        if (!parent)
        {
            g_rw_lock_writer_unlock (&priv->lock);
            g_set_error (error, DONNA_CONFIG_ERROR,
                    DONNA_CONFIG_ERROR_NOT_FOUND,
                    "Config: Parent of category '%s' not found",
                    (*name == '/') ? name + 1 : name);
            g_free (name);
            goto done;
        }
        ++s;
    }
    else
    {
        s = (*name == '/') ? name + 1 : name;
        parent = priv->root;
    }

    /* make sure the option exists/is a category */
    node = get_child_node (parent, s, strlen (s));
    if (!node)
    {
        g_rw_lock_writer_unlock (&priv->lock);
        g_set_error (error, DONNA_CONFIG_ERROR,
                DONNA_CONFIG_ERROR_NOT_FOUND,
                "Config: Cannot rename category '%s': Doesn't exist",
                (*name == '/') ? name + 1 : name);
        g_free (name);
        goto done;
    }
    if (!option_is_category (node->data, priv->root))
    {
        g_rw_lock_writer_unlock (&priv->lock);
        g_set_error (error, DONNA_CONFIG_ERROR,
                DONNA_CONFIG_ERROR_INVALID_TYPE,
                "Config: Cannot rename category '%s': It is an option",
                (*name == '/') ? name + 1 : name);
        g_free (name);
        goto done;
    }

    /* make sure the new name is available */
    if (get_child_node (parent, new_name, strlen (new_name)))
    {
        g_rw_lock_writer_unlock (&priv->lock);
        g_set_error (error, DONNA_CONFIG_ERROR,
                DONNA_CONFIG_ERROR_ALREADY_EXISTS,
                "Config: Cannot rename category '%s' to '%s': Option already exists",
                (*name == '/') ? name + 1 : name, new_name);
        g_free (name);
        goto done;
    }

    /* perform the rename */
    option = (struct option *) node->data;
    old_name = option->name;
    option->name = str_chunk (priv, new_name);

    /* numbered category special handling */
    if (*new_name >= '1' && *new_name <= '9')
        move_numbered_category (priv->root, node);

    data.str_prefix = g_string_new (NULL);
    for (n = node->parent; n && n != priv->root; n = n->parent)
    {
        g_string_prepend_c (data.str_prefix, '/');
        g_string_prepend (data.str_prefix, ((struct option *) n->data)->name);
    }

    /* if there's a node, we need to update it now, but without any signals
     * emitted under the lock */
    if (option->node)
    {
        GValue v = G_VALUE_INIT;

        g_value_init (&v, G_TYPE_STRING);
        g_value_take_string (&v, g_strconcat ("/", data.str_prefix->str, new_name, NULL));
        donna_node_set_property_value_no_signal (option->node, "location", &v);
        g_value_unset (&v);

        g_value_init (&v, G_TYPE_STRING);
        g_value_set_static_string (&v, option->name);
        donna_node_set_property_value_no_signal (option->node, "name", &v);
        g_value_unset (&v);

        /* make sure we can ref the node (to emit signal) without problem.  That
         * is, if ref_count == 1 it means we're holding the only ref left, i.e.
         * we have a toggle_ref waiting for the lock to unref the node, and
         * trying to add a ref would deadlock */
        if (((GObject *) option->node)->ref_count > 1)
            option_node = g_object_ref (option->node);
    }

    data.gnode = node;
    data.parent_name = option->name;
    data.names = g_ptr_array_new ();
    data.nodes = g_ptr_array_new_with_free_func (g_object_unref);
    g_node_traverse (node, G_IN_ORDER, G_TRAVERSE_ALL, -1,
            (GNodeTraverseFunc) traverse_renaming_category,
            &data);

    /* we're done with the writer lock, signals go after to avoid deadlocks */
    g_rw_lock_writer_unlock (&priv->lock);

    /* all option with the old name were deleted */
    for (i = data.names->len; i > 0; --i)
    {
        s = g_strconcat (data.str_prefix->str, old_name, data.names->pdata[i - 1], NULL);
        config_option_deleted (config, s);
        g_free (s);
    }
    /* as was the category itself */
    s = g_strconcat (data.str_prefix->str, old_name, NULL);
    config_option_deleted (config, s);
    g_free (s);

    /* the new name/category was set */
    s = g_strconcat (data.str_prefix->str, new_name, NULL);
    config_option_set (config, s);
    g_free (s);
    /* and so were every option with the new name */
    for (i = 1; i <= data.names->len; ++i)
    {
        s = g_strconcat (data.str_prefix->str, new_name, data.names->pdata[i - 1], NULL);
        config_option_set (config, s);
        g_free (s);
        g_free (data.names->pdata[i - 1]);
    }
    g_ptr_array_free (data.names, TRUE);

    /* emit signals on node outside of lock */
    for (i = data.nodes->len; i > 0; --i)
        donna_provider_node_updated ((DonnaProvider *) config,
                data.nodes->pdata[i - 1], "location");
    g_ptr_array_free (data.nodes, TRUE);

    if (option_node)
    {
        donna_provider_node_updated ((DonnaProvider *) config, option_node,
                "location");
        donna_provider_node_updated ((DonnaProvider *) config, option_node,
                "name");
        g_object_unref (option_node);
    }

    g_string_free (data.str_prefix, TRUE);
    ret = TRUE;

done:
    if (child_node)
    {
        donna_provider_node_new_child ((DonnaProvider *) config,
                parent_node, child_node);
        g_object_unref (parent_node);
        g_object_unref (child_node);
    }
    return ret;
}

static gboolean
_remove_option (DonnaProviderConfig *config,
                GError             **error,
                const gchar         *name,
                gboolean             category)
{
    DonnaProviderConfigPrivate *priv;
    DonnaNode *parent_node = NULL;
    DonnaNode *child_node  = NULL;
    GNode *parent;
    GNode *node;
    const gchar *s;
    struct removing_data data;
    guint i;
    gboolean ret = FALSE;

    g_return_val_if_fail (DONNA_IS_PROVIDER_CONFIG (config), FALSE);

    priv = config->priv;
    g_rw_lock_writer_lock (&priv->lock);
    s = strrchr (name, '/');
    if (s)
    {
        parent = ensure_categories (config, name, (gsize) (s - name),
                &parent_node, &child_node);
        if (!parent)
        {
            g_rw_lock_writer_unlock (&priv->lock);
            g_set_error (error, DONNA_CONFIG_ERROR,
                    DONNA_CONFIG_ERROR_NOT_FOUND,
                    "Config: Cannot remove '%s': %s not found",
                    name,
                    (category) ? "category" : "option");
            goto done;
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
        g_set_error (error, DONNA_CONFIG_ERROR,
                DONNA_CONFIG_ERROR_NOT_FOUND,
                "Config: Cannot remove '%s': %s not found",
                name,
                (category) ? "category" : "option");
        goto done;
    }
    if ((!category && option_is_category (node->data, priv->root))
            || (category && !option_is_category (node->data, priv->root)))
    {
        g_rw_lock_writer_unlock (&priv->lock);
        g_set_error (error, DONNA_CONFIG_ERROR,
                DONNA_CONFIG_ERROR_INVALID_OPTION_TYPE,
                (category)
                ? "Config: Cannot remove '%s': Not a category"
                : "Config: Cannot remove '%s': Not an option",
                name);
        goto done;
    }

    /* actually remove the nodes/options */
    data.config = config;
    data.nodes  = g_ptr_array_new_with_free_func (g_object_unref);
    g_node_traverse (node, G_IN_ORDER, G_TRAVERSE_ALL, -1,
            (GNodeTraverseFunc) free_node_data_removing,
            &data);
    g_node_destroy (node);

    g_rw_lock_writer_unlock (&priv->lock);

    /* signals after releasing the lock, to avoid dead locks */
    /* config: we only send one signal, e.g. only the category (no children) */
    config_option_deleted (DONNA_CONFIG (config), name);
    /* for provider: we must do it for all existing nodes, as it also serves as
     * a "destroy" i.e. to mean unref it, the node doesn't exist anymore */
    for (i = 0; i < data.nodes->len; ++i)
        donna_provider_node_deleted (DONNA_PROVIDER (config),
                data.nodes->pdata[i]);
    g_ptr_array_free (data.nodes, TRUE);
    ret = TRUE;

done:
    if (child_node)
    {
        donna_provider_node_new_child ((DonnaProvider *) config,
                parent_node, child_node);
        g_object_unref (parent_node);
        g_object_unref (child_node);
    }
    return ret;
}

#define __remove_option(is_category)    do {                    \
    gchar *name;                                                \
    va_list va_arg;                                             \
    gboolean ret;                                               \
                                                                \
    g_return_val_if_fail (fmt != NULL, FALSE);                  \
                                                                \
    va_start (va_arg, fmt);                                     \
    name = g_strdup_vprintf (fmt, va_arg);                      \
    va_end (va_arg);                                            \
                                                                \
    ret = _remove_option (config, error,                        \
            (*name == '/') ? name + 1 : name,                   \
            is_category);                                       \
                                                                \
    g_free (name);                                              \
    return ret;                                                 \
} while (0)

/**
 * donna_config_remove_option:
 * @config: The #DonnaConfig
 * @error: (allow-none): Return location of a #GError, or %NULL
 * @fmt: <function>printf</function>-like format for the full/option/name
 * @...: <function>printf</function>-like arguments
 *
 * Removes option @fmt
 *
 * Returns: %TRUE if option was removed, else %FALSE
 */
gboolean
donna_config_remove_option (DonnaConfig    *config,
                            GError        **error,
                            const gchar    *fmt,
                            ...)
{
    __remove_option (FALSE);
}

/**
 * donna_config_remove_category:
 * @config: The #DonnaConfig
 * @error: (allow-none): Return location of a #GError, or %NULL
 * @fmt: <function>printf</function>-like format for the full/option/name
 * @...: <function>printf</function>-like arguments
 *
 * Removes category @fmt
 *
 * Returns: %TRUE if category was removed, else %FALSE
 */
gboolean
donna_config_remove_category (DonnaConfig    *config,
                              GError        **error,
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
    GError *err = NULL;
    DonnaProvider *provider;
    DonnaProviderConfigPrivate *priv;
    GNode *gnode;
    struct option *option;
    gchar *location;
    gboolean is_set_value;

    is_set_value = streq (name, "option-value");
    if (is_set_value || streq (name, "name"))
    {
        gboolean is_category;

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
        is_category = option_is_category (option, priv->root);

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
        {
            /* set the new value */
            g_value_copy (value, &option->value);
            donna_node_set_property_value_no_signal (node, name, value);

            g_rw_lock_writer_unlock (&priv->lock);

            /* emit signal, +1 to skip leading slash */
            config_option_set ((DonnaConfig *) provider, location + 1);

            /* we can now emit the signal, outside or the lock */
            donna_provider_node_updated (provider, node, name);
        }
        else
        {
            /* rename */

            g_rw_lock_writer_unlock (&priv->lock);

            if (is_category)
            {
                if (!donna_config_rename_category ((DonnaConfig *) provider, &err,
                            g_value_get_string (value),
                            "%s", location))
                {
                    donna_task_take_error (task, err);
                    g_free (location);
                    return DONNA_TASK_FAILED;
                }
            }
            else
            {
                if (!donna_config_rename_option ((DonnaConfig *) provider, &err,
                            g_value_get_string (value),
                            "%s", location))
                {
                    donna_task_take_error (task, err);
                    g_free (location);
                    return DONNA_TASK_FAILED;
                }
            }

        }

        g_free (location);
        return DONNA_TASK_DONE;
    }

    /* should never happened, since the only WRITABLE properties on our nodes
     * are the ones dealt with above */
    g_return_val_if_reached (DONNA_TASK_FAILED);
}

static void
real_node_toggle_ref_cb (DonnaProviderConfig *config,
                         DonnaNode           *node,
                         gboolean             is_last,
                         gboolean             force)
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

        if (G_UNLIKELY (!force && ((GObject *) node)->ref_count > 1))
        {
            g_rec_mutex_unlock (&config->priv->nodes_mutex);
            g_rw_lock_reader_unlock (&config->priv->lock);
            return;
        }
        location = donna_node_get_location (node);
        gnode = get_option_node (config->priv->root, location);
        if (gnode)
            /* gnode might be NULL if the option has already been removed */
            ((struct option *) gnode->data)->node = NULL;
        g_object_unref (node);
        g_free (location);
    }

    g_rec_mutex_unlock (&config->priv->nodes_mutex);
    g_rw_lock_reader_unlock (&config->priv->lock);
}

static void
node_toggle_ref_cb (DonnaProviderConfig *config,
                    DonnaNode           *node,
                    gboolean             is_last)
{
    real_node_toggle_ref_cb (config, node, is_last, FALSE);
}

/* assumes a reader lock on config; will lock/unlock nodes_mutex as needed.
 * It will emit new_node under the reader lock, which means handlers should
 * never try to write on the config, since that would deadlock. */
static gboolean
ensure_option_has_node (DonnaProviderConfig *config,
                        gchar               *location,
                        struct option       *option)
{
    DonnaProviderConfigPrivate *priv = config->priv;

    g_rec_mutex_lock (&priv->nodes_mutex);
    if (!option->node)
    {
        GValue v = G_VALUE_INIT;

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
                g_value_init (&v, G_TYPE_STRING);
                g_value_set_string (&v, option->extra);
                donna_node_add_property (option->node,
                        "option-extra",
                        G_TYPE_STRING,
                        &v,
                        node_prop_refresher,
                        NULL /* no setter */,
                        NULL);
                g_value_unset (&v);
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
        {
            g_value_init (&v, G_TYPE_ICON);
            g_value_take_object (&v, g_themed_icon_new ((option->extra != priv->root)
                        ? "document-properties" : "preferences-desktop"));
            donna_node_set_property_value (option->node, "icon", &v);
            g_value_unset (&v);
        }

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
    /* real_node_toggle_ref_cb() will remove the node from our hashmap, and as
     * such remove our reference to the node (amongst other things), but since
     * we want to actually remove the toggle_ref, we need to add a ref to node
     * here (that will be removed right after, in node_toggle_ref_cb()) */
    real_node_toggle_ref_cb ((DonnaProviderConfig *) provider, g_object_ref (node),
            TRUE, TRUE);
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
    gsize len = 0;
    gboolean want_item;
    gboolean want_categories;
    GValue *value;
    gboolean match = FALSE;

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

    DONNA_DEBUG (TASK, NULL,
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

    DONNA_DEBUG (TASK, NULL,
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
            if (!donna_config_remove_option (pcfg, NULL, location))
                ret = FALSE;
        }
        else
        {
            if (!donna_config_remove_category (pcfg, NULL, location))
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

    DONNA_DEBUG (TASK, NULL,
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

struct cmp_extra
{
    const gchar *name;
    const gchar *title;
    gboolean is_int;
};

static gint
cmp_extra (struct cmp_extra **extra1, struct cmp_extra **extra2)
{
    return strcmp ((*extra1)->title, (*extra2)->title);
}

static gchar *
provider_config_get_context_alias_new_nodes (DonnaProvider      *provider,
                                             const gchar        *extra,
                                             DonnaNode          *location,
                                             const gchar        *prefix,
                                             GError            **error)
{
    DonnaProviderConfigPrivate *priv = ((DonnaProviderConfig *) provider)->priv;
    GString *str;
    GHashTableIter iter;
    const gchar *name;
    DonnaConfigExtra *_e;
    GPtrArray *arr;
    guint i;

    str = g_string_new (NULL);
    donna_g_string_append_concat (str,
            prefix, "new_category,",
            prefix, "new_string<",
                prefix, "new_boolean,",
                prefix, "new_int,",
                prefix, "new_string,-,",
            NULL);

    arr = g_ptr_array_new_with_free_func (g_free);

    g_rw_lock_reader_lock (&priv->lock);
    g_hash_table_iter_init (&iter, priv->extras);
    while (g_hash_table_iter_next (&iter, (gpointer) &name, (gpointer) &_e))
    {
        struct cmp_extra *cmp;

        cmp = g_new (struct cmp_extra, 1);
        cmp->name   = name;
        cmp->title  = _e->any.title;
        cmp->is_int = _e->any.type == DONNA_CONFIG_EXTRA_TYPE_LIST_INT
            || _e->any.type == DONNA_CONFIG_EXTRA_TYPE_LIST_FLAGS;
        g_ptr_array_add (arr, cmp);
    }

    g_ptr_array_sort (arr, (GCompareFunc) cmp_extra);
    for (i = 0; i < arr->len; ++i)
    {
        struct cmp_extra *cmp = arr->pdata[i];

        if (cmp->is_int)
            donna_g_string_append_concat (str, prefix, "new_int:", cmp->name, NULL);
        else
            donna_g_string_append_concat (str, prefix, "new_string:", cmp->name, NULL);
        g_string_append_c (str, ',');
    }
    g_rw_lock_reader_unlock (&priv->lock);

    /* remove trailing ',' */
    g_string_truncate (str, str->len - 1);

    g_ptr_array_free (arr, TRUE);
    return g_string_free (str, FALSE);
}

static gboolean
provider_config_get_context_item_info (DonnaProvider      *provider,
                                       const gchar        *item,
                                       const gchar        *extra,
                                       DonnaContextReference reference,
                                       DonnaNode          *node_ref,
                                       get_sel_fn          get_sel,
                                       gpointer            get_sel_data,
                                       DonnaContextInfo   *info,
                                       GError            **error)
{
    DonnaProviderConfigPrivate *priv = ((DonnaProviderConfig *) provider)->priv;
    DonnaConfigExtra *_e;

    if (streq (item, "new_category"))
    {
        info->is_visible = info->is_sensitive = TRUE;
        info->name = "New Category";
        info->icon_name = "folder-new";
        info->trigger = "command:tv_goto_line (%o, f+s, "
            "@config_set_option ("
            "@node_get_property (@tv_get_location (%o), location),"
            ":category,,,1,1))";
        return TRUE;
    }
    else if (streq (item, "new_boolean"))
    {
        info->is_visible = info->is_sensitive = TRUE;
        info->name = "New Boolean Option";
        info->icon_name = "document-new";
        info->trigger = "command:tv_goto_line (%o, f+s, "
            "@config_set_option ("
            "@node_get_property (@tv_get_location (%o), location),"
            ":boolean,,,1,1))";
        return TRUE;
    }
    else if (streq (item, "new_int"))
    {
        info->is_visible = info->is_sensitive = TRUE;
        info->icon_name = "document-new";
        if (extra)
        {
            g_rw_lock_reader_lock (&priv->lock);
            _e = g_hash_table_lookup (priv->extras, extra);
            g_rw_lock_reader_unlock (&priv->lock);
            if (!_e || !(_e->any.type == DONNA_CONFIG_EXTRA_TYPE_LIST_INT
                        || _e->any.type == DONNA_CONFIG_EXTRA_TYPE_LIST_FLAGS))
            {
                g_set_error (error, DONNA_CONTEXT_MENU_ERROR,
                        DONNA_CONTEXT_MENU_ERROR_UNKNOWN_ITEM,
                        "Provider 'config': Invalid extra '%s' for item '%s'",
                        extra, item);
                return FALSE;
            }
            info->name = g_strconcat ("New '", _e->any.title, "' Option", NULL);
            info->free_name = TRUE;
            info->trigger = g_strconcat ("command:tv_goto_line (%o, f+s, "
                    "@config_set_option ("
                    "@node_get_property (@tv_get_location (%o), location),",
                    extra, ",,,1,1))", NULL);
            info->free_trigger = TRUE;
        }
        else
        {
            info->name = "New Integer Option";
            info->trigger = "command:tv_goto_line (%o, f+s, "
                "@config_set_option ("
                "@node_get_property (@tv_get_location (%o), location),"
                ":int,,,1,1))";
        }

        return TRUE;
    }
    else if (streq (item, "new_string"))
    {
        info->is_visible = info->is_sensitive = TRUE;
        info->icon_name = "document-new";
        if (extra)
        {
            g_rw_lock_reader_lock (&priv->lock);
            _e = g_hash_table_lookup (priv->extras, extra);
            g_rw_lock_reader_unlock (&priv->lock);
            if (!_e || _e->any.type != DONNA_CONFIG_EXTRA_TYPE_LIST)
            {
                g_set_error (error, DONNA_CONTEXT_MENU_ERROR,
                        DONNA_CONTEXT_MENU_ERROR_UNKNOWN_ITEM,
                        "Provider 'config': Invalid extra '%s' for item '%s'",
                        extra, item);
                return FALSE;
            }
            info->name = g_strconcat ("New '", _e->any.title, "' Option", NULL);
            info->free_name = TRUE;
            info->trigger = g_strconcat ("command:tv_goto_line (%o, f+s, "
                    "@config_set_option ("
                    "@node_get_property (@tv_get_location (%o), location),",
                    extra, ",,,1,1))", NULL);
            info->free_trigger = TRUE;
        }
        else
        {
            info->name = "New String Option";
            info->trigger = "command:tv_goto_line (%o, f+s, "
                "@config_set_option ("
                "@node_get_property (@tv_get_location (%o), location),"
                ":string,,,1,1))";
        }

        return TRUE;
    }

    g_set_error (error, DONNA_CONTEXT_MENU_ERROR,
            DONNA_CONTEXT_MENU_ERROR_UNKNOWN_ITEM,
            "Provider 'config': No such context item: '%s'", item);
    return FALSE;
}
