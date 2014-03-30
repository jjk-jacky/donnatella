/*
 * donnatella - Copyright (C) 2014 Olivier Brunel
 *
 * columntype-perms.c
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
#include <unistd.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <pwd.h>
#include <grp.h>
#include <ctype.h>
#include <errno.h>
#include "columntype.h"
#include "columntype-perms.h"
#include "renderer.h"
#include "node.h"
#include "app.h"
#include "conf.h"
#include "macros.h"
#include "debug.h"

/**
 * SECTION:columntype-perms
 * @Short_description: To show user, group and/or permissions.
 *
 * Column type to show user, group and/or permissions.
 *
 * <refsect2 id="ct-perms-options">
 * <title>Options</title>
 * <para>
 * The following options are available :
 *
 * - <systemitem>format</systemitem> (string) : A format string defining what to
 *   show and how. Defaults to "&percnt;S"
 * - <systemitem>format_tooltip</systemitem> (string) : Same as format only for
 *   the tooltip.
 * - <systemitem>color_user</systemitem> (string) : Color used on user name
 *   (&percnt;V) and in &percnt;S
 * - <systemitem>color_group</systemitem> (string) : Color used on group name
 *   (&percnt;H) and in &percnt;S
 * - <systemitem>color_mixed</systemitem> (string) : Color used in &percnt;S
 * - <systemitem>sort</systemitem> (integer:perms) : Which criteria to sort by
 *   when sorting on the column.
 *   Can be one of "perms", "self", "uid", "user", "gid", and "group"
 *
 * The following format specifiers are supported in
 * <systemitem>format</systemitem> and <systemitem>format_tooltip</systemitem>:
 *
 * - &percnt;p: Permissions, as classic "rwxrwxrwx" string
 * - &percnt;s: Permissions owned ("self"), as "rwx" string. A letter indicates
 *   you have the permission (e.g. to read, regardless of where from), a dash
 *   that you don't.
 * - &percnt;S: Similar as above, but with colors. There will always be the
 *   three letters, lowercase when you don't have the permission, uppercase when
 *   you do. In addition, the color indicates why you have the permission, or
 *   what would be needed to have it (that is, the first/lower requirement) :
 *
 *   In <systemitem>color_user</systemitem> if you have the permission because
 *   you're the owner (and), or if only owner has the permission.
 *
 *   In <systemitem>color_group</systemitem> if you have the permission because
 *   you're member of the group, or if you'd need to be in the group to have it
 *   (Note that owner might also have it).
 *
 *   In <systemitem>color_mixed</systemitem> if you have the permission as
 *   owner, but the group also has it (i.e. you're not a member).
 * - &percnt;u : User ID
 * - &percnt;U : User name
 * - &percnt;V : Same as &percnt;U but with colors. It will be in
 *   <systemitem>color_user</systemitem> when it is you.
 * - &percnt;g : Group ID
 * - &percnt;G : Group name
 * - &percnt;H : Same as &percnt;G but with colors. It will be in
 *   <systemitem>color_group</systemitem> when you are in the group.
 * - &percnt;o : Permissions as octal number
 *
 * </para></refsect2>
 *
 * <refsect2 id="ct-perms-filtering">
 * <title>Filtering</title>
 * <para>
 * You can filter by using the following format:
 * [UNIT] [COMP] VALUE
 *
 * Where UNIT can be one of the following:
 * - u: user id
 * - U: user name
 * - g: group id
 * - G: group name
 * - p: permissions
 * - s: "self" (permissions owned)
 *
 * Defaults to 'p' if none specified.
 *
 * If UNIT is one of 'u', 'U', 'g' or 'G' then it must only be followed by VALUE
 * (no COMP), the user/group id/name.
 *
 * If UNIT is one of 'p' or 's', then COMP can be either '=' (equals), '-'
 * (requires) or '/' (any); And VALUE must represent permissions. This is
 * inspired by the syntax of find(1).
 *
 * If UNIT is 'p' then VALUE can be either a octal number (e.g. 644), or a
 * coma-separated list of 'u', 'g', 'o' or 'a', followed by '=' or '+', then one
 * or more of 'r', 'w' and 'x'.
 * For example, the following do the same: "p-644" and "p-u=rw,g=r,o=r"
 *
 * If UNIT is 's' then VALUE can be either a single octal number (e.g. 5), or
 * one or more of 'r', 'w' and 'x'.
 * For example, the following do the same: "s/rw" and "s/6"
 *
 * As you probably guessed, it will match if the node either has the exact
 * specified permissions ('='), if any of the specified permissions are present
 * ('/'), or if at least all the specifed permissions are present ('-').
 * </para></refsect2>
 */

#define SET_PERMS   (1 << 0)
#define SET_UID     (1 << 1)
#define SET_GID     (1 << 2)

enum
{
    PROP_0,

    PROP_APP,

    NB_PROPS
};

enum
{
    SORT_PERMS = 0,
    SORT_MY_PERMS,
    SORT_USER_ID,
    SORT_USER_NAME,
    SORT_GROUP_ID,
    SORT_GROUP_NAME,
    NB_SORT
};

struct tv_col_data
{
    gchar *format;
    gchar *format_tooltip;
    gchar *color_user;
    gchar *color_group;
    gchar *color_mixed;
    gint8  sort;
};

struct _user
{
    uid_t  id;
    gchar *name;
};

struct _group
{
    gid_t    id;
    gchar   *name;
    gboolean is_member;
};

struct _DonnaColumnTypePermsPrivate
{
    DonnaApp *app;
    uid_t     user_id;
    gint      nb_groups;
    gid_t    *group_ids;
    GSList   *users;
    GSList   *groups;
};

enum unit
{
    UNIT_UID        = 'u',
    UNIT_USER       = 'U', /* parsing only, will be turned into UNIT_UID */
    UNIT_GID        = 'g',
    UNIT_GROUP      = 'G', /* parsing only, will be turned into UNIT_GID */
    UNIT_PERMS      = 'p',
    UNIT_SELF       = 's'
};

enum comp
{
    COMP_EQUAL  = '=',
    COMP_REQ    = '-',
    COMP_ANY    = '/'
};

struct filter_data
{
    enum unit   unit;
    enum comp   comp;
    guint       ref;
};

/* internal, used by app.c */
gboolean
_donna_column_type_perms_register_extras (DonnaConfig *config, GError **error);

static struct _user *   get_user            (DonnaColumnTypePermsPrivate *priv,
                                             uid_t                        uid);
static struct _user *   get_user_from_name  (DonnaColumnTypePermsPrivate *priv,
                                             const gchar                 *name);
static struct _group *  get_group           (DonnaColumnTypePermsPrivate *priv,
                                             gid_t                        gid);
static struct _group *  get_group_from_name (DonnaColumnTypePermsPrivate *priv,
                                             const gchar                 *name);

static void             ct_perms_set_property       (GObject            *object,
                                                     guint               prop_id,
                                                     const GValue       *value,
                                                     GParamSpec         *pspec);
static void             ct_perms_get_property       (GObject            *object,
                                                     guint               prop_id,
                                                     GValue             *value,
                                                     GParamSpec         *pspec);
static void             ct_perms_finalize           (GObject            *object);

/* ColumnType */
static const gchar *    ct_perms_get_name           (DonnaColumnType    *ct);
static const gchar *    ct_perms_get_renderers      (DonnaColumnType    *ct);
static void             ct_perms_get_options        (DonnaColumnType    *ct,
                                                     DonnaColumnOptionInfo **options,
                                                     guint              *nb_options);
static DonnaColumnTypeNeed ct_perms_refresh_data    (DonnaColumnType    *ct,
                                                     const gchar        *col_name,
                                                     const gchar        *arr_name,
                                                     const gchar        *tv_name,
                                                     gboolean            is_tree,
                                                     gpointer           *data);
static void             ct_perms_free_data          (DonnaColumnType    *ct,
                                                     gpointer            data);
static GPtrArray *      ct_perms_get_props          (DonnaColumnType    *ct,
                                                     gpointer            data);
static GtkSortType      ct_perms_get_default_sort_order
                                                    (DonnaColumnType    *ct,
                                                     const gchar        *col_name,
                                                     const gchar        *arr_name,
                                                     const gchar        *tv_name,
                                                     gboolean            is_tree,
                                                     gpointer            data);
static gboolean         ct_perms_can_edit           (DonnaColumnType    *ct,
                                                     gpointer            data,
                                                     DonnaNode          *node,
                                                     GError            **error);
static gboolean         ct_perms_edit               (DonnaColumnType    *ct,
                                                     gpointer            data,
                                                     DonnaNode          *node,
                                                     GtkCellRenderer   **renderers,
                                                     renderer_edit_fn    renderer_edit,
                                                     gpointer            re_data,
                                                     DonnaTreeView      *treeview,
                                                     GError            **error);
static gboolean         ct_perms_set_value          (DonnaColumnType    *ct,
                                                     gpointer            data,
                                                     GPtrArray          *nodes,
                                                     const gchar        *value,
                                                     DonnaNode          *node_ref,
                                                     DonnaTreeView      *treeview,
                                                     GError            **error);
static GPtrArray *      ct_perms_render             (DonnaColumnType    *ct,
                                                     gpointer            data,
                                                     guint               index,
                                                     DonnaNode          *node,
                                                     GtkCellRenderer    *renderer);
static gint             ct_perms_node_cmp           (DonnaColumnType    *ct,
                                                     gpointer            data,
                                                     DonnaNode          *node1,
                                                     DonnaNode          *node2);
static gboolean         ct_perms_set_tooltip        (DonnaColumnType    *ct,
                                                     gpointer            data,
                                                     guint               index,
                                                     DonnaNode          *node,
                                                     GtkTooltip         *tooltip);
static gboolean         ct_perms_refresh_filter_data(DonnaColumnType    *ct,
                                                     const gchar        *filter,
                                                     gpointer           *filter_data,
                                                     GError            **error);
static gboolean         ct_perms_is_filter_match    (DonnaColumnType    *ct,
                                                     gpointer            data,
                                                     gpointer            filter_data,
                                                     DonnaNode          *node);
static void             ct_perms_free_filter_data   (DonnaColumnType    *ct,
                                                     gpointer            filter_data);
static DonnaColumnTypeNeed ct_perms_set_option      (DonnaColumnType    *ct,
                                                     const gchar        *col_name,
                                                     const gchar        *arr_name,
                                                     const gchar        *tv_name,
                                                     gboolean            is_tree,
                                                     gpointer            data,
                                                     const gchar        *option,
                                                     gpointer            value,
                                                     gboolean            toggle,
                                                     DonnaColumnOptionSaveLocation save_location,
                                                     GError            **error);
static gchar *          ct_perms_get_context_alias  (DonnaColumnType   *ct,
                                                     gpointer           data,
                                                     const gchar       *alias,
                                                     const gchar       *extra,
                                                     DonnaContextReference reference,
                                                     DonnaNode         *node_ref,
                                                     get_sel_fn         get_sel,
                                                     gpointer           get_sel_data,
                                                     const gchar       *prefix,
                                                     GError           **error);
static gboolean         ct_perms_get_context_item_info (
                                                     DonnaColumnType   *ct,
                                                     gpointer           data,
                                                     const gchar       *item,
                                                     const gchar       *extra,
                                                     DonnaContextReference reference,
                                                     DonnaNode         *node_ref,
                                                     get_sel_fn         get_sel,
                                                     gpointer           get_sel_data,
                                                     DonnaContextInfo  *info,
                                                     GError           **error);

static void
ct_perms_column_type_init (DonnaColumnTypeInterface *interface)
{
    interface->get_name                 = ct_perms_get_name;
    interface->get_renderers            = ct_perms_get_renderers;
    interface->get_options              = ct_perms_get_options;
    interface->refresh_data             = ct_perms_refresh_data;
    interface->free_data                = ct_perms_free_data;
    interface->get_props                = ct_perms_get_props;
    interface->get_default_sort_order   = ct_perms_get_default_sort_order;
    interface->can_edit                 = ct_perms_can_edit;
    interface->edit                     = ct_perms_edit;
    interface->set_value                = ct_perms_set_value;
    interface->render                   = ct_perms_render;
    interface->set_tooltip              = ct_perms_set_tooltip;
    interface->node_cmp                 = ct_perms_node_cmp;
    interface->refresh_filter_data      = ct_perms_refresh_filter_data;
    interface->is_filter_match          = ct_perms_is_filter_match;
    interface->free_filter_data         = ct_perms_free_filter_data;
    interface->set_option               = ct_perms_set_option;
    interface->get_context_alias        = ct_perms_get_context_alias;
    interface->get_context_item_info    = ct_perms_get_context_item_info;
}

G_DEFINE_TYPE_WITH_CODE (DonnaColumnTypePerms, donna_column_type_perms,
        G_TYPE_OBJECT,
        G_IMPLEMENT_INTERFACE (DONNA_TYPE_COLUMN_TYPE, ct_perms_column_type_init)
        )

static void
donna_column_type_perms_class_init (DonnaColumnTypePermsClass *klass)
{
    GObjectClass *o_class;

    o_class = (GObjectClass *) klass;
    o_class->set_property   = ct_perms_set_property;
    o_class->get_property   = ct_perms_get_property;
    o_class->finalize       = ct_perms_finalize;

    g_object_class_override_property (o_class, PROP_APP, "app");

    g_type_class_add_private (klass, sizeof (DonnaColumnTypePermsPrivate));
}

static void
donna_column_type_perms_init (DonnaColumnTypePerms *ct)
{
    DonnaColumnTypePermsPrivate *priv;

    priv = ct->priv = G_TYPE_INSTANCE_GET_PRIVATE (ct,
            DONNA_TYPE_COLUMN_TYPE_PERMS,
            DonnaColumnTypePermsPrivate);
    priv->user_id = getuid ();
    priv->nb_groups = getgroups (0, NULL);
    priv->group_ids = g_new (gid_t, (gsize) priv->nb_groups);
    if (G_UNLIKELY (getgroups (priv->nb_groups, priv->group_ids) == -1))
    {
        gint _errno = errno;
        g_warning ("ColumnType 'perms': Failed to get groups: %s",
                g_strerror (_errno));
        memset (priv->group_ids, 0, sizeof (gid_t) * (gsize) priv->nb_groups);
    }
}

gboolean
_donna_column_type_perms_register_extras (DonnaConfig *config, GError **error)
{
    DonnaConfigItemExtraListInt it[6];
    gint i;

    i = 0;
    it[i].value     = SORT_MY_PERMS;
    it[i].in_file   = "myperms";
    it[i].label     = "Own Permissions";
    ++i;
    it[i].value     = SORT_PERMS;
    it[i].in_file   = "perms";
    it[i].label     = "Permissions";
    ++i;
    it[i].value     = SORT_USER_ID;
    it[i].in_file   = "uid";
    it[i].label     = "User ID";
    ++i;
    it[i].value     = SORT_USER_NAME;
    it[i].in_file   = "user";
    it[i].label     = "User Name";
    ++i;
    it[i].value     = SORT_GROUP_ID;
    it[i].in_file   = "gid";
    it[i].label     = "Group ID";
    ++i;
    it[i].value     = SORT_GROUP_NAME;
    it[i].in_file   = "group";
    it[i].label     = "Group Name";
    ++i;
    if (G_UNLIKELY (!donna_config_add_extra (config,
                    DONNA_CONFIG_EXTRA_TYPE_LIST_INT, "sort-perms",
                    "Permissions Sorting Criteria",
                    i, it, error)))
        return FALSE;

    return TRUE;
}

static void
free_user (struct _user *u)
{
    g_free (u->name);
    g_slice_free (struct _user, u);
}

static void
free_group (struct _group *g)
{
    g_free (g->name);
    g_slice_free (struct _group, g);
}

static void
ct_perms_finalize (GObject *object)
{
    DonnaColumnTypePermsPrivate *priv;

    priv = DONNA_COLUMN_TYPE_PERMS (object)->priv;
    g_object_unref (priv->app);
    g_slist_free_full (priv->users, (GDestroyNotify) free_user);
    g_slist_free_full (priv->groups, (GDestroyNotify) free_group);
    g_free (priv->group_ids);

    /* chain up */
    G_OBJECT_CLASS (donna_column_type_perms_parent_class)->finalize (object);
}

static void
ct_perms_set_property (GObject            *object,
                       guint               prop_id,
                       const GValue       *value,
                       GParamSpec         *pspec)
{
    if (G_LIKELY (prop_id == PROP_APP))
        DONNA_COLUMN_TYPE_PERMS (object)->priv->app = g_value_dup_object (value);
    else
        G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
}

static void
ct_perms_get_property (GObject            *object,
                       guint               prop_id,
                       GValue             *value,
                       GParamSpec         *pspec)
{
    if (G_LIKELY (prop_id == PROP_APP))
        g_value_set_object (value, DONNA_COLUMN_TYPE_PERMS (object)->priv->app);
    else
        G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
}

static const gchar *
ct_perms_get_name (DonnaColumnType *ct)
{
    g_return_val_if_fail (DONNA_IS_COLUMN_TYPE_PERMS (ct), NULL);
    return "perms";
}

static const gchar *
ct_perms_get_renderers (DonnaColumnType   *ct)
{
    g_return_val_if_fail (DONNA_IS_COLUMN_TYPE_PERMS (ct), NULL);
    return "t";
}

static void
ct_perms_get_options (DonnaColumnType    *ct,
                      DonnaColumnOptionInfo **options,
                      guint              *nb_options)
{
    static DonnaColumnOptionInfo o[] = {
        { "format",             G_TYPE_STRING,      NULL },
        { "format_tooltip",     G_TYPE_STRING,      NULL },
        { "color_user",         G_TYPE_STRING,      NULL },
        { "color_group",        G_TYPE_STRING,      NULL },
        { "color_mixed",        G_TYPE_STRING,      NULL },
        { "sort",               G_TYPE_INT,         "sort-perms" }
    };

    *options = o;
    *nb_options = G_N_ELEMENTS (o);
}

static DonnaColumnTypeNeed
ct_perms_refresh_data (DonnaColumnType    *ct,
                       const gchar        *col_name,
                       const gchar        *arr_name,
                       const gchar        *tv_name,
                       gboolean            is_tree,
                       gpointer           *_data)
{
    DonnaColumnTypePerms *ctperms = DONNA_COLUMN_TYPE_PERMS (ct);
    DonnaConfig *config;
    struct tv_col_data *data;
    DonnaColumnTypeNeed need = DONNA_COLUMN_TYPE_NEED_NOTHING;
    gchar *s;
    gint i;

    config = donna_app_peek_config (ctperms->priv->app);

    if (!*_data)
        *_data = g_new0 (struct tv_col_data, 1);
    data = *_data;

    s = donna_config_get_string_column (config, col_name,
            arr_name, tv_name, is_tree, "column_types/perms",
            "format", "%S");
    if (!streq (data->format, s))
    {
        g_free (data->format);
        data->format = g_markup_escape_text (s, -1);
        need = DONNA_COLUMN_TYPE_NEED_REDRAW;
    }
    else
        g_free (s);

    s = donna_config_get_string_column (config, col_name,
            arr_name, tv_name, is_tree, "column_types/perms",
            "format_tooltip", "%p %V:%H");
    if (!streq(data->format_tooltip, s))
    {
        g_free (data->format_tooltip);
        /* empty string to disable tooltip */
        data->format_tooltip = (*s == '\0') ? NULL : g_markup_escape_text (s, -1);
        need = DONNA_COLUMN_TYPE_NEED_REDRAW;
    }
    else
        g_free (s);

    s = donna_config_get_string_column (config, col_name,
            arr_name, tv_name, is_tree, "column_types/perms",
            "color_user", "green");
    if (!streq (data->color_user, s))
    {
        g_free (data->color_user);
        data->color_user = s;
        need = DONNA_COLUMN_TYPE_NEED_REDRAW;
    }
    else
        g_free (s);

    s = donna_config_get_string_column (config, col_name,
            arr_name, tv_name, is_tree, "column_types/perms",
            "color_group", "blue");
    if (!streq (data->color_group, s))
    {
        g_free (data->color_group);
        data->color_group = s;
        need = DONNA_COLUMN_TYPE_NEED_REDRAW;
    }
    else
        g_free (s);

    s = donna_config_get_string_column (config, col_name,
            arr_name, tv_name, is_tree, "column_types/perms",
            "color_mixed", "#00aaaa");
    if (!streq (data->color_mixed, s))
    {
        g_free (data->color_mixed);
        data->color_mixed = s;
        need = DONNA_COLUMN_TYPE_NEED_REDRAW;
    }
    else
        g_free (s);

    i = donna_config_get_int_column (config, col_name,
            arr_name, tv_name, is_tree, "column_types/perms",
            "sort", SORT_MY_PERMS);
    if (i != data->sort)
    {
        data->sort = (gint8) i;
        need = DONNA_COLUMN_TYPE_NEED_RESORT;
    }

    return need;
}

static void
ct_perms_free_data (DonnaColumnType    *ct,
                    gpointer            _data)
{
    struct tv_col_data *data = _data;

    g_free (data->format);
    g_free (data->format_tooltip);
    g_free (data);
}

static GPtrArray *
ct_perms_get_props (DonnaColumnType  *ct,
                    gpointer          data)
{
    GPtrArray *props;
    gchar *s;
    guint set = 0;

    g_return_val_if_fail (DONNA_IS_COLUMN_TYPE_PERMS (ct), NULL);

    props = g_ptr_array_new_full (3, g_free);

    s = ((struct tv_col_data *) data)->format;
    while ((s = strchr (s, '%')))
    {
        switch (s[1])
        {
            case 'p':
            case 's':
            case 'S':
            case 'o':
                if (!(set & SET_PERMS))
                {
                    set |= SET_PERMS;
                    g_ptr_array_add (props, g_strdup ("mode"));
                }
                break;

            case 'u':
            case 'U':
            case 'V':
                if (!(set & SET_UID))
                {
                    set |= SET_UID;
                    g_ptr_array_add (props, g_strdup ("uid"));
                }
                break;

            case 'g':
            case 'G':
            case 'H':
                if (!(set & SET_GID))
                {
                    set |= SET_GID;
                    g_ptr_array_add (props, g_strdup ("gid"));
                }
                break;
        }
        if ((set & (SET_PERMS | SET_UID | SET_GID)) == (SET_PERMS | SET_UID | SET_GID))
            break;
        ++s;
    }

    return props;
}

static GtkSortType
ct_perms_get_default_sort_order (DonnaColumnType *ct,
                                 const gchar     *col_name,
                                 const gchar     *arr_name,
                                 const gchar     *tv_name,
                                 gboolean         is_tree,
                                 gpointer         _data)
{
    struct tv_col_data *data = _data;

    return (donna_config_get_boolean_column (donna_app_peek_config (
                    DONNA_COLUMN_TYPE_PERMS (ct)->priv->app),
                /* no default since it's based on option sort */
                col_name, arr_name, tv_name, is_tree, NULL, "desc_first",
                data->sort == SORT_PERMS || data->sort == SORT_MY_PERMS))
        ? GTK_SORT_DESCENDING : GTK_SORT_ASCENDING;
}

struct editing_data
{
    DonnaApp        *app;
    DonnaTreeView   *tree;
    DonnaNode       *node;
    GPtrArray       *arr;
    mode_t           mode;
    uid_t            uid;
    gid_t            gid;
    GtkWidget       *window;
    GtkToggleButton *rad_sel;
    GtkToggleButton *tgl_u[3];
    GtkToggleButton *tgl_g[3];
    GtkToggleButton *tgl_o[3];
    GtkSpinButton   *spn_u;
    GtkSpinButton   *spn_g;
    GtkSpinButton   *spn_o;
    GtkComboBox     *box_u;
    GtkComboBox     *box_g;
    GtkButton       *btn_set;
    GtkButton       *btn_cancel;
    GtkToggleButton *set_perms;
    gulong           sid_spn_u;
    gulong           sid_spn_g;
    gulong           sid_spn_o;
    /* struct box_changed */
    GtkToggleButton *set_uid;
    gulong           sid_uid;
    /* struct box_changed */
    GtkToggleButton *set_gid;
    gulong           sid_gid;
};

struct box_changed
{
    GtkToggleButton *toggle;
    gulong           sid;
};

static void
spin_cb (GtkSpinButton *spin, GtkToggleButton *tgl[])
{
    gchar c;

    c = (gchar) gtk_spin_button_get_value (spin);
    gtk_toggle_button_set_active (tgl[0], c & 4);
    gtk_toggle_button_set_active (tgl[1], c & 2);
    gtk_toggle_button_set_active (tgl[2], c & 1);
}

static void
perms_cb (struct editing_data *ed)
{
    g_signal_handler_disconnect (ed->spn_u, ed->sid_spn_u);
    g_signal_handler_disconnect (ed->spn_g, ed->sid_spn_g);
    g_signal_handler_disconnect (ed->spn_o, ed->sid_spn_o);
    gtk_toggle_button_set_active (ed->set_perms, TRUE);
}

static void
toggle_cb (GtkToggleButton *toggle, GtkSpinButton *spin)
{
    gint c;
    gint perm;

    c = (gchar) gtk_spin_button_get_value (spin);
    perm = GPOINTER_TO_INT (g_object_get_data ((GObject *) toggle, "perm"));

    if (gtk_toggle_button_get_active (toggle))
        c |= perm;
    else
        c &= ~perm;
    gtk_spin_button_set_value (spin, c);
}

static void
window_destroy_cb (struct editing_data *data)
{
    if (data->arr)
        g_ptr_array_unref (data->arr);
    g_free (data);
}

static void
toggle_set (struct editing_data *data)
{
    gchar lbl[128];

    strcpy (lbl, "Set ");
    if (gtk_toggle_button_get_active (data->set_perms))
        strcat (lbl, "Permissions/");
    if (gtk_toggle_button_get_active (data->set_uid))
        strcat (lbl, "User/");
    if (gtk_toggle_button_get_active (data->set_gid))
        strcat (lbl, "Group/");
    lbl[strlen (lbl) - 1] = '\0';

    gtk_button_set_label (data->btn_set, lbl);
    gtk_widget_set_sensitive ((GtkWidget *) data->btn_set, lbl[3] != '\0');
}

static void
box_changed (GtkComboBox *box, struct box_changed *bc)
{
    g_signal_handler_disconnect (box, bc->sid);
    gtk_toggle_button_set_active (bc->toggle, TRUE);
}

static gboolean
set_value (const gchar   *prop,
           guint          value,
           DonnaNode     *node,
           DonnaTreeView *tree,
           GError       **error)
{
    GValue v = G_VALUE_INIT;

    g_value_init (&v, G_TYPE_UINT);
    g_value_set_uint (&v, value);
    if (!donna_tree_view_set_node_property (tree, node, prop, &v, error))
    {
        gboolean is_mode = streq (prop, "mode");
        gchar *fl = donna_node_get_full_location (node);
        g_prefix_error (error, (is_mode)
                ? "ColumnType 'perms': Unable to set property '%s' for '%s' to %o"
                : "ColumnType 'perms': Unable to set property '%s' for '%s' to %d",
                prop, fl, value);
        g_free (fl);
        return FALSE;
    }
    g_value_unset (&v);
    return TRUE;
}

static inline void
set_prop (struct editing_data   *data,
          DonnaNode             *node,
          const gchar           *prop,
          guint                  value)
{
    GError *err = NULL;

    if (!set_value (prop, value, node, data->tree, &err))
    {
        donna_app_show_error (data->app, err, NULL);
        g_clear_error (&err);
    }
}

static void
apply_cb (struct editing_data *data)
{
    GtkTreeIter iter;
    gboolean use_arr;
    guint set = 0;
    mode_t mode = 0;
    uid_t uid = 0;
    gid_t gid = 0;
    guint i;

    gtk_widget_hide (data->window);
    use_arr = (data->arr && gtk_toggle_button_get_active (data->rad_sel));

    if (gtk_toggle_button_get_active (data->set_perms))
    {
        mode_t m;

        mode = 00;
        m = 00;
        if (gtk_toggle_button_get_active (data->tgl_u[0]))
            m |= 04;
        if (gtk_toggle_button_get_active (data->tgl_u[1]))
            m |= 02;
        if (gtk_toggle_button_get_active (data->tgl_u[2]))
            m |= 01;
        mode = 0100 * m;
        m = 00;
        if (gtk_toggle_button_get_active (data->tgl_g[0]))
            m |= 04;
        if (gtk_toggle_button_get_active (data->tgl_g[1]))
            m |= 02;
        if (gtk_toggle_button_get_active (data->tgl_g[2]))
            m |= 01;
        mode += 010 * m;
        m = 00;
        if (gtk_toggle_button_get_active (data->tgl_o[0]))
            m |= 04;
        if (gtk_toggle_button_get_active (data->tgl_o[1]))
            m |= 02;
        if (gtk_toggle_button_get_active (data->tgl_o[2]))
            m |= 01;
        mode += m;

        set |= SET_PERMS;
    }

    if (gtk_toggle_button_get_active (data->set_uid))
    {
        if (gtk_combo_box_get_active_iter (data->box_u, &iter))
            gtk_tree_model_get (gtk_combo_box_get_model (data->box_u), &iter,
                    0,  &uid,
                    -1);
        else
            uid = (uid_t) -1;

        if (uid != (uid_t) -1)
            set |= SET_UID;
    }

    if (gtk_toggle_button_get_active (data->set_gid))
    {
        if (gtk_combo_box_get_active_iter (data->box_g, &iter))
            gtk_tree_model_get (gtk_combo_box_get_model (data->box_g), &iter,
                    0,  &gid,
                    -1);
        else
            gid = (gid_t) -1;

        if (gid != (gid_t) -1)
            set |= SET_GID;
    }

    if (set != 0)
    {
        if (use_arr)
            for (i = 0; i < data->arr->len; ++i)
            {
                if (set & SET_PERMS)
                    set_prop (data, data->arr->pdata[i], "mode", (guint) mode);
                if (set & SET_UID)
                    set_prop (data, data->arr->pdata[i], "uid", (guint) uid);
                if (set & SET_GID)
                    set_prop (data, data->arr->pdata[i], "gid", (guint) gid);
            }
        else
        {
            if ((set & SET_PERMS) && data->mode != mode)
                set_prop (data, data->node, "mode", (guint) mode);
            if ((set & SET_UID) && data->uid != uid)
                set_prop (data, data->node, "uid", (guint) uid);
            if ((set & SET_GID) && data->gid != gid)
                set_prop (data, data->node, "gid", (guint) gid);
        }
    }

    gtk_widget_destroy (data->window);
}

static gboolean
key_pressed (struct editing_data *ed, GdkEventKey *event)
{
    if (event->keyval == GDK_KEY_Escape)
    {
        gtk_widget_activate ((GtkWidget *) ed->btn_cancel);
        return TRUE;
    }

    return FALSE;
}

static gboolean
ct_perms_can_edit (DonnaColumnType    *ct,
                   gpointer            _data,
                   DonnaNode          *node,
                   GError            **error)
{
    if (!DONNA_COLUMN_TYPE_GET_INTERFACE (ct)->helper_can_edit (ct,
            "mode", node, error))
        return FALSE;

    if (!DONNA_COLUMN_TYPE_GET_INTERFACE (ct)->helper_can_edit (ct,
            "uid", node, error))
        return FALSE;

    if (!DONNA_COLUMN_TYPE_GET_INTERFACE (ct)->helper_can_edit (ct,
            "gid", node, error))
        return FALSE;

    return TRUE;
}

static gboolean
ct_perms_edit (DonnaColumnType    *ct,
               gpointer            _data,
               DonnaNode          *node,
               GtkCellRenderer   **renderers,
               renderer_edit_fn    renderer_edit,
               gpointer            re_data,
               DonnaTreeView      *treeview,
               GError            **error)
{
    struct tv_col_data *data = _data;
    struct editing_data *ed;
    mode_t mode;
    uid_t uid;
    gid_t gid;
    GPtrArray *arr;
    GtkWidget *w;
    GtkWindow *win;
    gint row;
    gchar *s;
    gint c;

    GtkGrid *grid;
    GtkListStore *store_pwd, *store_grp;
    GtkCellRenderer *renderer;
    GtkTreeIter it_pwd, *iter_pwd = &it_pwd;
    GtkTreeIter it_grp, *iter_grp = &it_grp;
    struct passwd *pwd;
    struct group *grp;
    GtkBox *box;

    if (!ct_perms_can_edit (ct, data, node, error))
        return FALSE;

    /* get the perms */
    if (donna_node_get_mode (node, TRUE, &mode) != DONNA_NODE_VALUE_SET
            || donna_node_get_uid (node, TRUE, &uid) != DONNA_NODE_VALUE_SET
            || donna_node_get_gid (node, TRUE, &gid) != DONNA_NODE_VALUE_SET)
    {
        g_set_error (error, DONNA_COLUMN_TYPE_ERROR,
                DONNA_COLUMN_TYPE_ERROR_NODE_NO_PROP,
                "ColumnType 'perms': Failed to get properties");
        return FALSE;
    }
    /* get selected nodes (if any) */
    arr = donna_tree_view_get_selected_nodes (treeview, NULL);

    ed = g_new0 (struct editing_data, 1);
    ed->app  = ((DonnaColumnTypePerms *) ct)->priv->app;
    ed->tree = treeview;
    ed->node = node;
    ed->mode = (mode & (S_IRWXU | S_IRWXG | S_IRWXO));
    ed->uid  = uid;
    ed->gid  = gid;

    win = donna_column_type_new_floating_window (treeview, !!arr);
    ed->window = w = (GtkWidget *) win;
    g_signal_connect_swapped (win, "destroy",
            (GCallback) window_destroy_cb, ed);

    w = gtk_grid_new ();
    grid = (GtkGrid *) w;
    g_object_set (w, "column-spacing", 12, NULL);
    gtk_container_add ((GtkContainer *) win, w);

    row = 0;
    if (!arr || (arr->len == 1 && node == arr->pdata[0]))
    {
        PangoAttrList *attr_list;

        if (arr)
            g_ptr_array_unref (arr);

        s = donna_node_get_name (node);
        w = gtk_label_new (s);
        g_free (s);
        attr_list = pango_attr_list_new ();
        pango_attr_list_insert (attr_list,
                pango_attr_style_new (PANGO_STYLE_ITALIC));
        gtk_label_set_attributes ((GtkLabel *) w, attr_list);
        pango_attr_list_unref (attr_list);
        gtk_grid_attach (grid, w, 0, row, 4, 1);
    }
    else
    {
        ed->arr = arr;

        w = gtk_label_new (NULL);
        gtk_label_set_markup ((GtkLabel *) w, "<i>Apply to:</i>");
        gtk_grid_attach (grid, w, 0, row++, 4, 1);

        s = donna_node_get_name (node);
        w = gtk_radio_button_new_with_label (NULL, s);
        gtk_widget_set_tooltip_text (w, "Clicked item");
        g_free (s);
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
    }
    g_object_set (w, "margin-bottom", 9, NULL);

    ++row;
    w = gtk_label_new (NULL);
    gtk_label_set_markup ((GtkLabel *) w, "<b>User</b>");
    gtk_grid_attach (grid, w, 1, row, 1, 1);
    w = gtk_label_new (NULL);
    gtk_label_set_markup ((GtkLabel *) w, "<b>Group</b>");
    gtk_grid_attach (grid, w, 2, row, 1, 1);
    w = gtk_label_new (NULL);
    gtk_label_set_markup ((GtkLabel *) w, "<b>Other</b>");
    gtk_grid_attach (grid, w, 3, row, 1, 1);

    ++row;
    w = gtk_label_new ("Read");
    gtk_grid_attach (grid, w, 0, row, 1, 1);
    w = gtk_check_button_new ();
    g_object_set (w, "halign", GTK_ALIGN_CENTER, NULL);
    if (mode & S_IRUSR)
        gtk_toggle_button_set_active ((GtkToggleButton *) w, TRUE);
    gtk_grid_attach (grid, w, 1, row, 1, 1);
    ed->tgl_u[0] = (GtkToggleButton *) w;
    w = gtk_check_button_new ();
    g_object_set (w, "halign", GTK_ALIGN_CENTER, NULL);
    if (mode & S_IRGRP)
        gtk_toggle_button_set_active ((GtkToggleButton *) w, TRUE);
    gtk_grid_attach (grid, w, 2, row, 1, 1);
    ed->tgl_g[0] = (GtkToggleButton *) w;
    w = gtk_check_button_new ();
    g_object_set (w, "halign", GTK_ALIGN_CENTER, NULL);
    if (mode & S_IROTH)
        gtk_toggle_button_set_active ((GtkToggleButton *) w, TRUE);
    gtk_grid_attach (grid, w, 3, row, 1, 1);
    ed->tgl_o[0] = (GtkToggleButton *) w;

    ++row;
    w = gtk_label_new ("Write");
    gtk_grid_attach (grid, w, 0, row, 1, 1);
    w = gtk_check_button_new ();
    g_object_set (w, "halign", GTK_ALIGN_CENTER, NULL);
    if (mode & S_IWUSR)
        gtk_toggle_button_set_active ((GtkToggleButton *) w, TRUE);
    gtk_grid_attach (grid, w, 1, row, 1, 1);
    ed->tgl_u[1] = (GtkToggleButton *) w;
    w = gtk_check_button_new ();
    g_object_set (w, "halign", GTK_ALIGN_CENTER, NULL);
    if (mode & S_IWGRP)
        gtk_toggle_button_set_active ((GtkToggleButton *) w, TRUE);
    gtk_grid_attach (grid, w, 2, row, 1, 1);
    ed->tgl_g[1] = (GtkToggleButton *) w;
    w = gtk_check_button_new ();
    g_object_set (w, "halign", GTK_ALIGN_CENTER, NULL);
    if (mode & S_IWOTH)
        gtk_toggle_button_set_active ((GtkToggleButton *) w, TRUE);
    gtk_grid_attach (grid, w, 3, row, 1, 1);
    ed->tgl_o[1] = (GtkToggleButton *) w;

    ++row;
    w = gtk_label_new ("Execute");
    gtk_grid_attach (grid, w, 0, row, 1, 1);
    w = gtk_check_button_new ();
    g_object_set (w, "halign", GTK_ALIGN_CENTER, NULL);
    if (mode & S_IXUSR)
        gtk_toggle_button_set_active ((GtkToggleButton *) w, TRUE);
    gtk_grid_attach (grid, w, 1, row, 1, 1);
    ed->tgl_u[2] = (GtkToggleButton *) w;
    w = gtk_check_button_new ();
    g_object_set (w, "halign", GTK_ALIGN_CENTER, NULL);
    if (mode & S_IXGRP)
        gtk_toggle_button_set_active ((GtkToggleButton *) w, TRUE);
    gtk_grid_attach (grid, w, 2, row, 1, 1);
    ed->tgl_g[2] = (GtkToggleButton *) w;
    w = gtk_check_button_new ();
    g_object_set (w, "halign", GTK_ALIGN_CENTER, NULL);
    if (mode & S_IXOTH)
        gtk_toggle_button_set_active ((GtkToggleButton *) w, TRUE);
    gtk_grid_attach (grid, w, 3, row, 1, 1);
    ed->tgl_o[2] = (GtkToggleButton *) w;

    ++row;
    w = gtk_label_new ("Permission");
    gtk_grid_attach (grid, w, 0, row, 1, 1);
    w = gtk_spin_button_new_with_range (0, 7, 1);
    gtk_entry_set_activates_default ((GtkEntry *) w, TRUE);
    g_object_set (w, "halign", GTK_ALIGN_CENTER, NULL);
    gtk_entry_set_width_chars ((GtkEntry *) w, 1);
#if GTK_CHECK_VERSION (3, 12, 0)
    gtk_entry_set_max_width_chars ((GtkEntry *) w, 1);
#endif
    c = 0;
    if (mode & S_IRUSR)
        c += 4;
    if (mode & S_IWUSR)
        c += 2;
    if (mode & S_IXUSR)
        c += 1;
    gtk_spin_button_set_value ((GtkSpinButton *) w, c);
    gtk_grid_attach (grid, w, 1, row, 1, 1);
    ed->spn_u = (GtkSpinButton *) w;
    w = gtk_spin_button_new_with_range (0, 7, 1);
    gtk_entry_set_activates_default ((GtkEntry *) w, TRUE);
    g_object_set (w, "halign", GTK_ALIGN_CENTER, NULL);
    gtk_entry_set_width_chars ((GtkEntry *) w, 1);
#if GTK_CHECK_VERSION (3, 12, 0)
    gtk_entry_set_max_width_chars ((GtkEntry *) w, 1);
#endif
    c = 0;
    if (mode & S_IRGRP)
        c += 4;
    if (mode & S_IWGRP)
        c += 2;
    if (mode & S_IXGRP)
        c += 1;
    gtk_spin_button_set_value ((GtkSpinButton *) w, c);
    gtk_grid_attach (grid, w, 2, row, 1, 1);
    ed->spn_g = (GtkSpinButton *) w;
    w = gtk_spin_button_new_with_range (0, 7, 1);
    gtk_entry_set_activates_default ((GtkEntry *) w, TRUE);
    g_object_set (w, "halign", GTK_ALIGN_CENTER, NULL);
    gtk_entry_set_width_chars ((GtkEntry *) w, 1);
#if GTK_CHECK_VERSION (3, 12, 0)
    gtk_entry_set_max_width_chars ((GtkEntry *) w, 1);
#endif
    c = 0;
    if (mode & S_IROTH)
        c += 4;
    if (mode & S_IWOTH)
        c += 2;
    if (mode & S_IXOTH)
        c += 1;
    gtk_spin_button_set_value ((GtkSpinButton *) w, c);
    gtk_grid_attach (grid, w, 3, row, 1, 1);
    ed->spn_o = (GtkSpinButton *) w;

    g_signal_connect (ed->spn_u, "value-changed",
            (GCallback) spin_cb, &ed->tgl_u);
    g_signal_connect (ed->spn_g, "value-changed",
            (GCallback) spin_cb, &ed->tgl_g);
    g_signal_connect (ed->spn_o, "value-changed",
            (GCallback) spin_cb, &ed->tgl_o);

    ed->sid_spn_u = g_signal_connect_swapped (ed->spn_u, "value-changed",
            (GCallback) perms_cb, ed);
    ed->sid_spn_g = g_signal_connect_swapped (ed->spn_g, "value-changed",
            (GCallback) perms_cb, ed);
    ed->sid_spn_o = g_signal_connect_swapped (ed->spn_o, "value-changed",
            (GCallback) perms_cb, ed);

    g_object_set_data ((GObject *) ed->tgl_u[0], "perm", GINT_TO_POINTER (4));
    g_signal_connect (ed->tgl_u[0], "toggled",
            (GCallback) toggle_cb, ed->spn_u);
    g_object_set_data ((GObject *) ed->tgl_u[1], "perm", GINT_TO_POINTER (2));
    g_signal_connect (ed->tgl_u[1], "toggled",
            (GCallback) toggle_cb, ed->spn_u);
    g_object_set_data ((GObject *) ed->tgl_u[2], "perm", GINT_TO_POINTER (1));
    g_signal_connect (ed->tgl_u[2], "toggled",
            (GCallback) toggle_cb, ed->spn_u);

    g_object_set_data ((GObject *) ed->tgl_g[0], "perm", GINT_TO_POINTER (4));
    g_signal_connect (ed->tgl_g[0], "toggled",
            (GCallback) toggle_cb, ed->spn_g);
    g_object_set_data ((GObject *) ed->tgl_g[1], "perm", GINT_TO_POINTER (2));
    g_signal_connect (ed->tgl_g[1], "toggled",
            (GCallback) toggle_cb, ed->spn_g);
    g_object_set_data ((GObject *) ed->tgl_g[2], "perm", GINT_TO_POINTER (1));
    g_signal_connect (ed->tgl_g[2], "toggled",
            (GCallback) toggle_cb, ed->spn_g);

    g_object_set_data ((GObject *) ed->tgl_o[0], "perm", GINT_TO_POINTER (4));
    g_signal_connect (ed->tgl_o[0], "toggled",
            (GCallback) toggle_cb, ed->spn_o);
    g_object_set_data ((GObject *) ed->tgl_o[1], "perm", GINT_TO_POINTER (2));
    g_signal_connect (ed->tgl_o[1], "toggled",
            (GCallback) toggle_cb, ed->spn_o);
    g_object_set_data ((GObject *) ed->tgl_o[2], "perm", GINT_TO_POINTER (1));
    g_signal_connect (ed->tgl_o[2], "toggled",
            (GCallback) toggle_cb, ed->spn_o);

    store_pwd = gtk_list_store_new (2, G_TYPE_INT, G_TYPE_STRING);
    while ((pwd = getpwent ()))
    {
        gtk_list_store_insert_with_values (store_pwd, iter_pwd, -1,
                0,  pwd->pw_uid,
                1,  pwd->pw_name,
                -1);
        if (uid == pwd->pw_uid)
            iter_pwd = NULL;
    }
    endpwent ();

    store_grp = gtk_list_store_new (2, G_TYPE_INT, G_TYPE_STRING);
    while ((grp = getgrent ()))
    {
        gtk_list_store_insert_with_values (store_grp, iter_grp, -1,
                0,  grp->gr_gid,
                1,  grp->gr_name,
                -1);
        if (gid == grp->gr_gid)
            iter_grp = NULL;
    }
    endgrent ();

    renderer = gtk_cell_renderer_text_new ();

    ++row;
    w = gtk_combo_box_new_with_model ((GtkTreeModel *) store_pwd);
    ed->box_u = (GtkComboBox *) w;
    gtk_widget_set_tooltip_text (w, "User");
    gtk_combo_box_set_active_iter ((GtkComboBox *) w, &it_pwd);
    gtk_cell_layout_pack_start ((GtkCellLayout *) w, renderer, TRUE);
    gtk_cell_layout_set_attributes ((GtkCellLayout *) w, renderer,
            "text", 1, NULL);
    g_object_set (w, "margin-top", 9, NULL);
    gtk_grid_attach (grid, w, 0, row, 2, 1);
    w = gtk_combo_box_new_with_model ((GtkTreeModel *) store_grp);
    ed->box_g = (GtkComboBox *) w;
    gtk_widget_set_tooltip_text (w, "Group");
    gtk_combo_box_set_active_iter ((GtkComboBox *) w, &it_grp);
    gtk_cell_layout_pack_start ((GtkCellLayout *) w, renderer, TRUE);
    gtk_cell_layout_set_attributes ((GtkCellLayout *) w, renderer,
            "text", 1, NULL);
    g_object_set (w, "margin-top", 9, NULL);
    gtk_grid_attach (grid, w, 2, row, 2, 1);

    ++row;
    w = gtk_box_new (GTK_ORIENTATION_HORIZONTAL, FALSE);
    box = (GtkBox *) w;
    g_object_set (w, "margin-top", 15, NULL);
    gtk_grid_attach (grid, w, 0, row, 4, 1);

    w = gtk_button_new_with_label (NULL);
    ed->btn_set = (GtkButton *) w;
    gtk_widget_set_can_default (w, TRUE);
    gtk_window_set_default ((GtkWindow *) ed->window, w);
    gtk_button_set_image ((GtkButton *) w,
            gtk_image_new_from_icon_name ("gtk-ok", GTK_ICON_SIZE_MENU));
    g_signal_connect_swapped (w, "clicked", (GCallback) apply_cb, ed);
    gtk_box_pack_end (box, w, FALSE, FALSE, 3);
    w = gtk_button_new_with_label ("Cancel");
    ed->btn_cancel = (GtkButton *) w;
    gtk_button_set_image ((GtkButton *) w,
            gtk_image_new_from_icon_name ("gtk-cancel", GTK_ICON_SIZE_MENU));
    g_signal_connect_swapped (w, "clicked",
            (GCallback) gtk_widget_destroy, win);
    gtk_box_pack_end (box, w, FALSE, FALSE, 3);

    ++row;
    w = gtk_box_new (GTK_ORIENTATION_HORIZONTAL, FALSE);
    box = (GtkBox *) w;
    gtk_grid_attach (grid, w, 0, row, 4, 1);

    w = gtk_label_new ("Set: ");
    gtk_box_pack_start (box, w, FALSE, FALSE, 0);
    w = gtk_check_button_new_with_label ("Permissions");
    s = data->format;
    while ((s = strchr (s, '%')))
    {
        ++s;
        if (*s == 'p' || *s == 's' || *s == 'S' || *s == 'o')
        {
            gtk_toggle_button_set_active ((GtkToggleButton *) w, TRUE);
            break;
        }
    }
    g_signal_connect_swapped (w, "toggled", (GCallback) toggle_set, ed);
    ed->set_perms = (GtkToggleButton *) w;
    gtk_box_pack_start (box, w, FALSE, FALSE, 0);
    w = gtk_check_button_new_with_label ("User");
    s = data->format;
    while ((s = strchr (s, '%')))
    {
        ++s;
        if (*s == 'u' || *s == 'U' || *s == 'V')
        {
            gtk_toggle_button_set_active ((GtkToggleButton *) w, TRUE);
            break;
        }
    }
    g_signal_connect_swapped (w, "toggled", (GCallback) toggle_set, ed);
    ed->set_uid = (GtkToggleButton *) w;
    gtk_box_pack_start (box, w, FALSE, FALSE, 0);
    w = gtk_check_button_new_with_label ("Group");
    s = data->format;
    while ((s = strchr (s, '%')))
    {
        ++s;
        if (*s == 'g' || *s == 'G' || *s == 'H')
        {
            gtk_toggle_button_set_active ((GtkToggleButton *) w, TRUE);
            break;
        }
    }
    g_signal_connect_swapped (w, "toggled", (GCallback) toggle_set, ed);
    ed->set_gid = (GtkToggleButton *) w;
    gtk_box_pack_start (box, w, FALSE, FALSE, 0);

    ed->sid_uid = g_signal_connect (ed->box_u, "changed",
            (GCallback) box_changed, &ed->set_uid);
    ed->sid_gid = g_signal_connect (ed->box_g, "changed",
            (GCallback) box_changed, &ed->set_gid);

    /* set the button label/sensitivity */
    toggle_set (ed);

    g_signal_connect_swapped (ed->window, "key-press-event",
            (GCallback) key_pressed, ed);

    donna_app_set_floating_window (((DonnaColumnTypePerms *) ct)->priv->app, win);
    gtk_widget_show_all (ed->window);
    return TRUE;
}

static gboolean
ct_perms_set_value (DonnaColumnType    *ct,
                    gpointer            data,
                    GPtrArray          *nodes,
                    const gchar        *value,
                    DonnaNode          *node_ref,
                    DonnaTreeView      *treeview,
                    GError            **error)
{
    GError *err = NULL;
    DonnaColumnTypePermsPrivate *priv;
    GString *str = NULL;
    const gchar *s;
    gchar unit;
    guint ref;
    guint ref_add; /* for perms that are added, not set */
    guint i;

    g_return_val_if_fail (DONNA_IS_COLUMN_TYPE_PERMS (ct), FALSE);
    priv = ((DonnaColumnTypePerms *) ct)->priv;

    s = value;

    skip_blank (s);
    switch (*s)
    {
        case UNIT_UID:
        case UNIT_USER:
        case UNIT_GID:
        case UNIT_GROUP:
        case UNIT_PERMS:
            unit = *s;
            break;

        case UNIT_SELF:
            g_set_error (error, DONNA_COLUMN_TYPE_ERROR,
                    DONNA_COLUMN_TYPE_ERROR_INVALID_SYNTAX,
                    "Cannot use unit SELF ('s') to set value");
            return FALSE;

        default:
            unit = UNIT_PERMS;
            break;
    }

    skip_blank (s);
    if (*s == '\0')
    {
        DonnaNodeHasValue has;

        if (!node_ref)
        {
            g_set_error (error, DONNA_COLUMN_TYPE_ERROR,
                    DONNA_COLUMN_TYPE_ERROR_INVALID_SYNTAX,
                    "Invalid syntax: no value given");
            return FALSE;
        }

        if (unit == UNIT_UID || unit == UNIT_USER)
        {
            has = donna_node_get_uid (node_ref, TRUE, &ref);
            if (has != DONNA_NODE_VALUE_SET)
            {
                gchar *fl = donna_node_get_full_location (node_ref);
                g_set_error (error, DONNA_COLUMN_TYPE_ERROR,
                        DONNA_COLUMN_TYPE_ERROR_OTHER,
                        "Failed to import UID from '%s'",
                        fl);
                g_free (fl);
                return FALSE;
            }
        }
        else if (unit == UNIT_GID || unit == UNIT_GROUP)
        {
            has = donna_node_get_gid (node_ref, TRUE, &ref);
            if (has != DONNA_NODE_VALUE_SET)
            {
                gchar *fl = donna_node_get_full_location (node_ref);
                g_set_error (error, DONNA_COLUMN_TYPE_ERROR,
                        DONNA_COLUMN_TYPE_ERROR_OTHER,
                        "Failed to import GID from '%s'",
                        fl);
                g_free (fl);
                return FALSE;
            }
        }
        else
        {
            has = donna_node_get_mode (node_ref, TRUE, &ref);
            if (has != DONNA_NODE_VALUE_SET)
            {
                gchar *fl = donna_node_get_full_location (node_ref);
                g_set_error (error, DONNA_COLUMN_TYPE_ERROR,
                        DONNA_COLUMN_TYPE_ERROR_OTHER,
                        "Failed to import permissions from '%s'",
                        fl);
                g_free (fl);
                return FALSE;
            }
            ref = (ref & (S_IRWXU | S_IRWXG | S_IRWXO));
        }

        goto ready;
    }

    switch (unit)
    {
        case UNIT_UID:
        case UNIT_GID:
            ref = (guint) g_ascii_strtoull (s, NULL, 10);
            goto ready;

        case UNIT_USER:
            {
                struct _user *u;

                u = get_user_from_name (priv, s);
                if (!u)
                {
                    g_set_error (error, DONNA_COLUMN_TYPE_ERROR,
                            DONNA_COLUMN_TYPE_ERROR_INVALID_SYNTAX,
                            "Unable to find user '%s'", s);
                    return FALSE;
                }
                unit = UNIT_UID;
                ref = u->id;
                goto ready;
            }

        case UNIT_GROUP:
            {
                struct _group *g;

                g = get_group_from_name (priv, s);
                if (!g)
                {
                    g_set_error (error, DONNA_COLUMN_TYPE_ERROR,
                            DONNA_COLUMN_TYPE_ERROR_INVALID_SYNTAX,
                            "Unable to find group '%s'", s);
                    return FALSE;
                }
                unit = UNIT_GID;
                ref = g->id;
                goto ready;
            }

        case UNIT_PERMS:
        case UNIT_SELF:
            /* silence warnings */
            break;
    }

    if (*s >= '0' && *s <= '9')
    {
        ref = (guint) g_ascii_strtoull (s, NULL, 8);
        goto ready;
    }

    ref_add = 0;
    for (;;)
    {
        guint m = 0;
        gboolean add = FALSE;

        if (*s == 'u')
            m = 0100;
        else if (*s == 'g')
            m = 010;
        else if (*s == 'o')
            m = 01;
        else if (*s == 'a')
            m = 0111;
        else if (*s == '\0')
            break;
        else
        {
            g_set_error (error, DONNA_COLUMN_TYPE_ERROR,
                    DONNA_COLUMN_TYPE_ERROR_INVALID_SYNTAX,
                    "Invalid syntax, expected 'u', 'g', 'o', 'a' or EOL: %s",
                    s);
            return FALSE;
        }
        ++s;
        if (*s == '+')
            add = TRUE;
        else if (*s != '=')
        {
            g_set_error (error, DONNA_COLUMN_TYPE_ERROR,
                    DONNA_COLUMN_TYPE_ERROR_INVALID_SYNTAX,
                    "Invalid syntax, expected '=' or '+': %s'",
                    s);
            return FALSE;
        }
        ++s;
        for (;;)
        {
            guint *r = (add) ? &ref_add : &ref;

            if (*s == 'r')
                *r += 04 * m;
            else if (*s == 'w')
                *r += 02 * m;
            else if (*s == 'x')
                *r += 01 * m;
            else if (*s == ',')
            {
                ++s;
                break;
            }
            else if (*s == '\0')
                break;
            else
            {
                g_set_error (error, DONNA_COLUMN_TYPE_ERROR,
                        DONNA_COLUMN_TYPE_ERROR_INVALID_SYNTAX,
                        "Invalid syntax, expected 'r', 'w', 'x', ',' or EOL: %s",
                        s);
                return FALSE;
            }
            ++s;
        }
    }

ready:
    for (i = 0; i < nodes->len; ++i)
    {

        if (!ct_perms_can_edit (ct, data, nodes->pdata[i], &err))
        {
            gchar *fl = donna_node_get_full_location (nodes->pdata[i]);

            if (!str)
                str = g_string_new (NULL);

            g_string_append_printf (str, "\n- Cannot set value on '%s': %s",
                    fl, (err) ? err->message : "(no error message)");
            g_free (fl);
            g_clear_error (&err);

            continue;
        }

        switch (unit)
        {
            case UNIT_UID:
                if (!set_value ("uid", ref, nodes->pdata[i], treeview, &err))
                {
                    gchar *fl = donna_node_get_full_location (nodes->pdata[i]);

                    if (!str)
                        str = g_string_new (NULL);

                    g_string_append_printf (str, "\n- Failed to set user on '%s': %s",
                            fl, (err) ? err->message : "(no error message)");
                    g_free (fl);
                    g_clear_error (&err);
                }
                break;

            case UNIT_GID:
                if (!set_value ("gid", ref, nodes->pdata[i], treeview, &err))
                {
                    gchar *fl = donna_node_get_full_location (nodes->pdata[i]);

                    if (!str)
                        str = g_string_new (NULL);

                    g_string_append_printf (str, "\n- Failed to set group on '%s': %s",
                            fl, (err) ? err->message : "(no error message)");
                    g_free (fl);
                    g_clear_error (&err);
                }
                break;

            default: /* UNIT_PERMS */
                if (ref_add > 0)
                {
                    mode_t m;

                    if (donna_node_get_mode (nodes->pdata[i], TRUE, &m)
                            != DONNA_NODE_VALUE_SET)
                    {
                        gchar *fl = donna_node_get_full_location (nodes->pdata[i]);
                        g_set_error (error, DONNA_COLUMN_TYPE_ERROR,
                                DONNA_COLUMN_TYPE_ERROR_OTHER,
                                "ColumnType 'perms': "
                                "Couldn't update permissions of '%s', "
                                "failed to get current value",
                                fl);
                        g_free (fl);
                        return FALSE;
                    }
                    m |= ref_add;
                    if (!set_value ("mode", m, nodes->pdata[i], treeview, error))
                    {
                        gchar *fl = donna_node_get_full_location (nodes->pdata[i]);

                        if (!str)
                            str = g_string_new (NULL);

                        g_string_append_printf (str,
                                "\n- Failed to set permissions on '%s': %s",
                                fl, (err) ? err->message : "(no error message)");
                        g_free (fl);
                        g_clear_error (&err);

                        continue;
                    }
                }
                if (!set_value ("mode", ref, nodes->pdata[i], treeview, &err))
                {
                    gchar *fl = donna_node_get_full_location (nodes->pdata[i]);

                    if (!str)
                        str = g_string_new (NULL);

                    g_string_append_printf (str,
                            "\n- Failed to set permissions on '%s': %s",
                            fl, (err) ? err->message : "(no error message)");
                    g_free (fl);
                    g_clear_error (&err);
                }
        }
    }

    if (!str)
        return TRUE;

    g_set_error (error, DONNA_COLUMN_TYPE_ERROR,
            DONNA_COLUMN_TYPE_ERROR_PARTIAL_COMPLETION,
            "Some operations failed :\n%s", str->str);
    g_string_free (str, TRUE);

    return FALSE;
}

static struct _user *
get_user (DonnaColumnTypePermsPrivate *priv, uid_t uid)
{
    GSList *list;
    struct _user *u;
    struct passwd *pwd;

    for (list = priv->users; list; list = list->next)
        if (((struct _user *) list->data)->id == uid)
            return list->data;

    pwd = getpwuid (uid);
    if (!pwd)
        return NULL;

    u = g_slice_new (struct _user);
    u->id = uid;
    u->name = g_strdup (pwd->pw_name);
    return u;
}

static struct _user *
get_user_from_name (DonnaColumnTypePermsPrivate *priv, const gchar *name)
{
    GSList *list;
    struct _user *u;
    struct passwd *pwd;

    for (list = priv->users; list; list = list->next)
        if (streq (((struct _user *) list->data)->name, name))
            return list->data;

    pwd = getpwnam (name);
    if (!pwd)
        return NULL;

    u = g_slice_new (struct _user);
    u->id = pwd->pw_uid;
    u->name = g_strdup (name);
    return u;
}

static struct _group *
get_group (DonnaColumnTypePermsPrivate *priv, gid_t gid)
{
    GSList *list;
    struct _group *g;
    struct group *grp;
    gint i;

    for (list = priv->groups; list; list = list->next)
        if (((struct _group *) list->data)->id == gid)
            return list->data;

    grp = getgrgid (gid);
    if (!grp)
        return NULL;

    g = g_slice_new (struct _group);
    g->id = gid;
    g->name = g_strdup (grp->gr_name);
    g->is_member = FALSE;
    for (i = 0; i < priv->nb_groups; ++i)
        if (priv->group_ids[i] == gid)
        {
            g->is_member = TRUE;
            break;
        }
    return g;
}

static struct _group *
get_group_from_name (DonnaColumnTypePermsPrivate *priv, const gchar *name)
{
    GSList *list;
    struct _group *g;
    struct group *grp;
    gint i;

    for (list = priv->groups; list; list = list->next)
        if (streq (((struct _group *) list->data)->name, name))
            return list->data;

    grp = getgrnam (name);
    if (!grp)
        return NULL;

    g = g_slice_new (struct _group);
    g->id = grp->gr_gid;
    g->name = g_strdup (name);
    g->is_member = FALSE;
    for (i = 0; i < priv->nb_groups; ++i)
        if (priv->group_ids[i] == g->id)
        {
            g->is_member = TRUE;
            break;
        }
    return g;
}

static void
add_colored_perm (DonnaColumnTypePermsPrivate   *priv,
                  struct tv_col_data            *data,
                  gchar                        **str,
                  gsize                         *max,
                  gsize                         *total,
                  mode_t                         mode,
                  uid_t                          uid,
                  gid_t                          gid,
                  gchar                          perm,
                  gboolean                       in_color)
{
    gchar u_perm = (in_color) ? (gchar) (perm + 'A' - 'a') : perm;
    mode_t S_OTH, S_GRP, S_USR;
    int group_has_perm = 0;
    gssize need;

    switch (perm)
    {
        case 'r':
            S_OTH = S_IROTH;
            S_GRP = S_IRGRP;
            S_USR = S_IRUSR;
            break;
        case 'w':
            S_OTH = S_IWOTH;
            S_GRP = S_IWGRP;
            S_USR = S_IWUSR;
            break;
        case 'x':
            S_OTH = S_IXOTH;
            S_GRP = S_IXGRP;
            S_USR = S_IXUSR;
            break;
        default:
            g_return_if_reached ();
    }

    if (mode & S_OTH)
    {
        need = snprintf (*str, *max, "%c", u_perm);
        goto done;
    }

    if (mode & S_GRP)
    {
        struct _group *g;

        g = get_group (priv, gid);
        if (!g)
        {
            need = snprintf (*str, *max, "?");
            goto done;
        }
        if (g->is_member)
        {
            if (in_color)
                need = snprintf (*str, *max,
                        "<span color=\"%s\">%c</span>", data->color_group, u_perm);
            else
                need = snprintf (*str, *max, "%c", perm);
            goto done;
        }
        group_has_perm = 1;
    }

    if (mode & S_USR)
    {
        if (uid == priv->user_id)
        {
            if (in_color)
                need = snprintf (*str, *max, "<span color=\"%s\">%c</span>",
                        (group_has_perm == 1) ? data->color_mixed : data->color_user,
                        u_perm);
            else
                need = snprintf (*str, *max, "%c", perm);
        }
        else
        {
            if (in_color)
                need = snprintf (*str, *max, "<span color=\"%s\">%c</span>",
                        (group_has_perm == 1) ? data->color_group : data->color_user,
                        perm);
            else
                need = snprintf (*str, *max, "-");
        }
        goto done;
    }

    if (in_color)
    {
        if (group_has_perm == 1)
            need = snprintf (*str, *max, "<span color=\"%s\">%c</span>",
                    data->color_group, perm);
        else
            need = snprintf (*str, *max, "%c", perm);
    }
    else
        need = snprintf (*str,*max, "-");

done:
    if (need >= 0)
    {
        if ((gsize) need < *max)
        {
            *max -= (gsize) need;
            *str += (gsize) need;
        }
        else
            *max = 0;
        *total += (gsize) need;
    }
}

#define add_perm(PERM, letter)  do {    \
    c = (mode & PERM) ? letter : '-';   \
    if (max >= 2)                       \
    {                                   \
        *str++ = c;                     \
        --max;                          \
    }                                   \
    ++total;                            \
} while (0)
static gsize
print_perms (DonnaColumnTypePerms   *ctperms,
             struct tv_col_data     *data,
             gchar                  *str,
             gsize                   max,
             const gchar            *fmt,
             mode_t                  mode,
             uid_t                   uid,
             gid_t                   gid)
{
    DonnaColumnTypePermsPrivate *priv = ctperms->priv;
    gsize total = 0;

    while (*fmt != '\0')
    {
        if (*fmt == '%')
        {
            gchar *s;
            gchar c;
            gssize need;

            switch (fmt[1])
            {
                case 'p':
                    add_perm (S_IRUSR, 'r');
                    add_perm (S_IWUSR, 'w');
                    add_perm (S_IXUSR, 'x');
                    add_perm (S_IRGRP, 'r');
                    add_perm (S_IWGRP, 'w');
                    add_perm (S_IXGRP, 'x');
                    add_perm (S_IROTH, 'r');
                    add_perm (S_IWOTH, 'w');
                    add_perm (S_IXOTH, 'x');
                    fmt += 2;
                    continue;
                case 's':
                case 'S':
                    add_colored_perm (priv, data, &str, &max, &total,
                            mode, uid, gid, 'r', fmt[1] == 'S');
                    add_colored_perm (priv, data, &str, &max, &total,
                            mode, uid, gid, 'w', fmt[1] == 'S');
                    add_colored_perm (priv, data, &str, &max, &total,
                            mode, uid, gid, 'x', fmt[1] == 'S');
                    fmt += 2;
                    continue;
                case 'u':
                    need = snprintf (str, max, "%d", uid);
                    break;
                case 'U':
                case 'V':
                    {
                        struct _user *u;

                        u = get_user (priv, uid);
                        if (!u)
                            s = (gchar *) "???";
                        else
                            s = u->name;

                        if (fmt[1] == 'U' || uid != priv->user_id)
                            need = snprintf (str, max, "%s", s);
                        else
                        {
                            s = g_markup_printf_escaped ("<span color=\"%s\">%s</span>",
                                    data->color_user, s);
                            need = snprintf (str, max, "%s", s);
                            g_free (s);
                        }
                        break;
                    }
                case 'g':
                    need = snprintf (str, max, "%d", gid);
                    break;
                case 'G':
                case 'H':
                    {
                        struct _group *g;

                        g = get_group (priv, gid);
                        if (!g)
                            s = (gchar *) "???";
                        else
                            s = g->name;

                        if (fmt[1] == 'G' || (g && !g->is_member))
                            need = snprintf (str, max, "%s", s);
                        else
                        {
                            s = g_markup_printf_escaped ("<span color=\"%s\">%s</span>",
                                    data->color_group, s);
                            need = snprintf (str, max, "%s", s);
                            g_free (s);
                        }
                        break;
                    }
                case 'o':
                    need = snprintf (str, max, "%o",
                            mode & (S_IRWXU | S_IRWXG | S_IRWXO));
                    break;
                default:
                    need = 0;
                    break;
            }
            /* it was a known modifier */
            if (need > 0)
            {
                if ((gsize) need < max)
                {
                    max -= (gsize) need;
                    str += (gsize) need;
                }
                else
                    max = 0;
                fmt += 2;
                total += (gsize) need;
                continue;
            }
        }

        /* we keep one more for NUL */
        if (max >= 2)
        {
            *str++ = *fmt++;
            --max;
        }
        else
            ++fmt;
        total += 1;
    }
    if (max > 0)
        *str = '\0';
    return total;
}

static gchar *
format_perms (DonnaColumnTypePerms  *ctperms,
              struct tv_col_data    *data,
              const gchar           *_fmt,
              mode_t                 mode,
              uid_t                  uid,
              gid_t                  gid,
              gchar                 *str,
              gsize                  max)
{
    gchar *fmt;
    gsize len;

    fmt = g_markup_escape_text (_fmt, -1);
    len = print_perms (ctperms, data, str, max, fmt, mode, uid, gid);
    if (len >= max)
    {
        str = g_new (gchar, ++len);
        print_perms (ctperms, data, str, len, fmt, mode, uid, gid);
    }
    g_free (fmt);
    return str;
}

static GPtrArray *
ct_perms_render (DonnaColumnType    *ct,
                 gpointer            _data,
                 guint               index,
                 DonnaNode          *node,
                 GtkCellRenderer    *renderer)
{
    struct tv_col_data *data = _data;
    DonnaNodeHasValue has;
    GPtrArray *arr = NULL;
    guint val;
    mode_t mode = 0;
    uid_t uid = 0;
    gid_t gid = 0;
    gchar buf[20], *b = buf;

    g_return_val_if_fail (DONNA_IS_COLUMN_TYPE_PERMS (ct), NULL);

    has = donna_node_get_mode (node, FALSE, &val);
    if (has == DONNA_NODE_VALUE_NONE)
    {
        g_object_set (renderer, "visible", FALSE, NULL);
        return NULL;
    }
    else if (has == DONNA_NODE_VALUE_NEED_REFRESH)
    {
        arr = g_ptr_array_new_full (3, g_free);
        g_ptr_array_add (arr, g_strdup ("mode"));
    }
    else
        mode = (mode_t) val;

    has = donna_node_get_uid (node, FALSE, &val);
    if (has == DONNA_NODE_VALUE_NONE)
    {
        g_object_set (renderer, "visible", FALSE, NULL);
        if (arr)
            g_ptr_array_unref (arr);
        return NULL;
    }
    else if (has == DONNA_NODE_VALUE_NEED_REFRESH)
    {
        if (!arr)
            arr = g_ptr_array_new_full (2, g_free);
        g_ptr_array_add (arr, g_strdup ("uid"));
    }
    else
        uid = (uid_t) val;

    has = donna_node_get_gid (node, FALSE, &val);
    if (has == DONNA_NODE_VALUE_NONE)
    {
        g_object_set (renderer, "visible", FALSE, NULL);
        if (arr)
            g_ptr_array_unref (arr);
        return NULL;
    }
    else if (has == DONNA_NODE_VALUE_NEED_REFRESH)
    {
        if (!arr)
            arr = g_ptr_array_new_full (1, g_free);
        g_ptr_array_add (arr, g_strdup ("gid"));
    }
    else
        gid = (gid_t) val;

    if (arr)
    {
        g_object_set (renderer, "visible", FALSE, NULL);
        return arr;
    }

    b = format_perms ((DonnaColumnTypePerms *) ct, data, data->format,
            mode, uid, gid, b, 20);
    g_object_set (renderer,
            "visible",      TRUE,
            "markup",       b,
            "ellipsize",    PANGO_ELLIPSIZE_END,
            "ellipsize-set",TRUE,
            NULL);
    donna_renderer_set (renderer, "ellipsize-set", NULL);
    if (b != buf)
        g_free (b);
    return NULL;
}

static gboolean
ct_perms_set_tooltip (DonnaColumnType    *ct,
                      gpointer            _data,
                      guint               index,
                      DonnaNode          *node,
                      GtkTooltip         *tooltip)
{
    struct tv_col_data *data = _data;
    guint val;
    mode_t mode;
    uid_t uid;
    gid_t gid;
    gchar buf[20], *b = buf;
    DonnaNodeHasValue has;

    if (!data->format_tooltip)
        return FALSE;

    has = donna_node_get_mode (node, FALSE, &val);
    if (has != DONNA_NODE_VALUE_SET)
        return FALSE;
    else
        mode = (mode_t) val;

    has = donna_node_get_uid (node, FALSE, &val);
    if (has != DONNA_NODE_VALUE_SET)
        return FALSE;
    else
        uid = (uid_t) val;

    has = donna_node_get_gid (node, FALSE, &val);
    if (has != DONNA_NODE_VALUE_SET)
        return FALSE;
    else
        gid = (gid_t) val;

    b = format_perms ((DonnaColumnTypePerms *) ct, data, data->format_tooltip,
            mode, uid, gid, b, 20);
    gtk_tooltip_set_markup (tooltip, b);
    if (b != buf)
        g_free (b);
    return TRUE;
}


#define check_has() do {                    \
    if (has1 != DONNA_NODE_VALUE_SET)       \
    {                                       \
        if (has2 == DONNA_NODE_VALUE_SET)   \
            return -1;                      \
        else                                \
            return 0;                       \
    }                                       \
    else if (has2 != DONNA_NODE_VALUE_SET)  \
        return 1;                           \
} while (0)
static gint
ct_perms_node_cmp (DonnaColumnType    *ct,
                   gpointer            _data,
                   DonnaNode          *node1,
                   DonnaNode          *node2)
{
    DonnaColumnTypePermsPrivate *priv = ((DonnaColumnTypePerms *) ct)->priv;
    struct tv_col_data *data = _data;
    DonnaNodeHasValue has1;
    DonnaNodeHasValue has2;
    guint val1;
    guint val2;
    gchar *s1 = NULL;
    gchar *s2 = NULL;

    switch (data->sort)
    {
        case SORT_PERMS:
        case SORT_MY_PERMS:
            has1 = donna_node_get_mode (node1, TRUE, (mode_t *) &val1);
            has2 = donna_node_get_mode (node2, TRUE, (mode_t *) &val2);
            break;
        case SORT_USER_ID:
        case SORT_USER_NAME:
            has1 = donna_node_get_uid (node1, TRUE, (uid_t *) &val1);
            has2 = donna_node_get_uid (node2, TRUE, (uid_t *) &val2);
            break;
        case SORT_GROUP_ID:
        case SORT_GROUP_NAME:
            has1 = donna_node_get_gid (node1, TRUE, (gid_t *) &val1);
            has2 = donna_node_get_gid (node2, TRUE, (gid_t *) &val2);
            break;
        default:
            return 0;
    }

    /* since we're blocking, has can only be SET, ERROR or NONE */

    check_has ();

    switch (data->sort)
    {
        case SORT_MY_PERMS:
            {
                struct _group *g;
                gint id1, id2;

                has1 = donna_node_get_uid (node1, TRUE, (uid_t *) &id1);
                has2 = donna_node_get_uid (node2, TRUE, (uid_t *) &id2);
                check_has ();

                if (priv->user_id == (uid_t) id1)
                {
                    if (val1 & S_IRUSR)
                        val1 = val1 | S_IROTH;
                    if (val1 & S_IWUSR)
                        val1 = val1 | S_IWOTH;
                    if (val1 & S_IXUSR)
                        val1 = val1 | S_IXOTH;
                }
                if (priv->user_id == (uid_t) id2)
                {
                    if (val2 & S_IRUSR)
                        val2 = val2 | S_IROTH;
                    if (val2 & S_IWUSR)
                        val2 = val2 | S_IWOTH;
                    if (val2 & S_IXUSR)
                        val2 = val2 | S_IXOTH;
                }

                has1 = donna_node_get_gid (node1, TRUE, (gid_t *) &id1);
                has2 = donna_node_get_gid (node2, TRUE, (gid_t *) &id2);
                check_has ();

                g = get_group (priv, (gid_t) id1);
                if (g && g->is_member)
                {
                    if (val1 & S_IRGRP)
                        val1 = val1 | S_IROTH;
                    if (val1 & S_IWGRP)
                        val1 = val1 | S_IWOTH;
                    if (val1 & S_IXGRP)
                        val1 = val1 | S_IXOTH;
                }
                g = get_group (priv, (gid_t) id2);
                if (g && g->is_member)
                {
                    if (val2 & S_IRGRP)
                        val2 = val2 | S_IROTH;
                    if (val2 & S_IWGRP)
                        val2 = val2 | S_IWOTH;
                    if (val2 & S_IXGRP)
                        val2 = val2 | S_IXOTH;
                }

                val1 = val1 & S_IRWXO;
                val2 = val2 & S_IRWXO;
                return (val1 > val2) ? 1 : (val1 < val2) ? -1 : 0;
            }
        case SORT_PERMS:
            val1 = val1 & (S_IRWXU | S_IRWXG | S_IRWXO);
            val2 = val2 & (S_IRWXU | S_IRWXG | S_IRWXO);
            /* fall through */
        case SORT_USER_ID:
        case SORT_GROUP_ID:
            return (val1 > val2) ? 1 : (val1 < val2) ? -1 : 0;
        case SORT_USER_NAME:
            {
                struct _user *u;

                u = get_user (priv, (uid_t) val1);
                if (!u)
                    has1 = DONNA_NODE_VALUE_ERROR;
                else
                    s1 = u->name;

                u = get_user (priv, (uid_t) val2);
                if (!u)
                    has2 = DONNA_NODE_VALUE_ERROR;
                else
                    s2 = u->name;

                break;
            }
        case SORT_GROUP_NAME:
            {
                struct _group *g;

                g = get_group (priv, (gid_t) val1);
                if (!g)
                    has1 = DONNA_NODE_VALUE_ERROR;
                else
                    s1 = g->name;

                g = get_group (priv, (gid_t) val2);
                if (!g)
                    has2 = DONNA_NODE_VALUE_ERROR;
                else
                    s2 = g->name;

                break;
            }
    }

    if (!s1 || !s2)
        check_has ();

    return strcmp (s1, s2);
}

static gboolean
ct_perms_refresh_filter_data (DonnaColumnType    *ct,
                              const gchar        *filter,
                              gpointer           *filter_data,
                              GError            **error)
{
    DonnaColumnTypePermsPrivate *priv = ((DonnaColumnTypePerms *) ct)->priv;
    struct filter_data *fd;

    if (*filter_data)
        fd = *filter_data;
    else
        fd = *filter_data = g_new0 (struct filter_data, 1);

    while (isblank (*filter))
        ++filter;

    switch (*filter)
    {
        case UNIT_UID:
        case UNIT_USER:
        case UNIT_GID:
        case UNIT_GROUP:
        case UNIT_PERMS:
        case UNIT_SELF:
            fd->unit = *filter++;
            break;
    }
    if (fd->unit == 0)
        fd->unit = UNIT_PERMS;

    while (isblank (*filter))
        ++filter;

    switch (fd->unit)
    {
        case UNIT_UID:
        case UNIT_GID:
            fd->ref = (guint) g_ascii_strtoull (filter, NULL, 10);
            return TRUE;

        case UNIT_USER:
            {
                struct _user *u;

                u = get_user_from_name (priv, filter);
                if (!u)
                {
                    g_set_error (error, DONNA_FILTER_ERROR,
                            DONNA_FILTER_ERROR_INVALID_SYNTAX,
                            "Unable to find user '%s'", filter);
                    g_free (fd);
                    *filter_data = NULL;
                    return FALSE;
                }
                fd->unit = UNIT_UID;
                fd->ref = u->id;
                return TRUE;
            }

        case UNIT_GROUP:
            {
                struct _group *g;

                g = get_group_from_name (priv, filter);
                if (!g)
                {
                    g_set_error (error, DONNA_FILTER_ERROR,
                            DONNA_FILTER_ERROR_INVALID_SYNTAX,
                            "Unable to find group '%s'", filter);
                    g_free (fd);
                    *filter_data = NULL;
                    return FALSE;
                }
                fd->unit = UNIT_GID;
                fd->ref = g->id;
                return TRUE;
            }

        case UNIT_PERMS:
        case UNIT_SELF:
            /* silence warnings */
            break;
    }

    /* PERMS || SELF */

    switch (*filter)
    {
        case COMP_EQUAL:
        case COMP_REQ:
        case COMP_ANY:
            fd->comp = *filter++;
            break;
    }
    if (fd->comp == 0)
        fd->comp = COMP_EQUAL;

    while (isblank (*filter))
        ++filter;

    if (fd->unit == UNIT_PERMS)
    {
        if (*filter >= '0' && *filter <= '9')
        {
            fd->ref = (guint) g_ascii_strtoull (filter, NULL, 8);
            return TRUE;
        }

        for (;;)
        {
            guint m = 0;

            if (*filter == 'u')
                m = 0100;
            else if (*filter == 'g')
                m = 010;
            else if (*filter == 'o')
                m = 01;
            else if (*filter == 'a')
                m = 0111;
            else if (*filter == '\0')
                break;
            else
            {
                g_set_error (error, DONNA_FILTER_ERROR,
                        DONNA_FILTER_ERROR_INVALID_SYNTAX,
                        "Invalid syntax, expected 'u', 'g', 'o', 'a' or EOL: %s",
                        filter);
                g_free (fd);
                *filter_data = NULL;
                return FALSE;
            }
            ++filter;
            if (*filter != '=' && *filter != '+')
            {
                g_set_error (error, DONNA_FILTER_ERROR,
                        DONNA_FILTER_ERROR_INVALID_SYNTAX,
                        "Invalid syntax, expected '=' or '+': %s'",
                        filter);
                g_free (fd);
                *filter_data = NULL;
                return FALSE;
            }
            ++filter;
            for (;;)
            {
                if (*filter == 'r')
                    fd->ref += 04 * m;
                else if (*filter == 'w')
                    fd->ref += 02 * m;
                else if (*filter == 'x')
                    fd->ref += 01 * m;
                else if (*filter == ',')
                {
                    ++filter;
                    break;
                }
                else if (*filter == '\0')
                    break;
                else
                {
                    g_set_error (error, DONNA_FILTER_ERROR,
                            DONNA_FILTER_ERROR_INVALID_SYNTAX,
                            "Invalid syntax, expected 'r', 'w', 'x', ',' or EOL: %s",
                            filter);
                    g_free (fd);
                    *filter_data = NULL;
                    return FALSE;
                }
                ++filter;
            }
        }
    }
    else /* UNIT_SELF */
    {
        if (*filter >= '0' && *filter <= '9')
            fd->ref = (guint) (*filter - '0');
        else
        {
            for (;;)
            {
                if (*filter == 'r')
                    fd->ref += 04;
                else if (*filter == 'w')
                    fd->ref += 02;
                else if (*filter == 'x')
                    fd->ref += 01;
                else if (*filter == '\0')
                    break;
                else
                {
                    g_set_error (error, DONNA_FILTER_ERROR,
                            DONNA_FILTER_ERROR_INVALID_SYNTAX,
                            "Invalid syntax, expected 'r', 'w', 'x' or EOL: %s",
                            filter);
                    g_free (fd);
                    *filter_data = NULL;
                    return FALSE;
                }
                ++filter;
            }
        }
    }

    return TRUE;
}

static gboolean
ct_perms_is_filter_match (DonnaColumnType    *ct,
                          gpointer            _data,
                          gpointer            filter_data,
                          DonnaNode          *node)
{
    DonnaColumnTypePermsPrivate *priv = ((DonnaColumnTypePerms *) ct)->priv;
    struct filter_data *fd = filter_data;
    DonnaNodeHasValue has;
    guint val = 0;
    mode_t mode;
    uid_t uid;
    gid_t gid;

    if (fd->unit == UNIT_UID || fd->unit == UNIT_GID)
    {
        if (fd->unit == UNIT_UID)
            has = donna_node_get_uid (node, TRUE, (uid_t *) &val);
        else
            has = donna_node_get_gid (node, TRUE, (gid_t *) &val);

        return (has == DONNA_NODE_VALUE_SET && val == fd->ref);
    }

    has = donna_node_get_mode (node, TRUE, (mode_t *) &val);
    if (has != DONNA_NODE_VALUE_SET)
        return FALSE;

    val = val & (S_IRWXU | S_IRWXG | S_IRWXO);
    if (fd->unit == UNIT_PERMS)
    {
        if (fd->comp == COMP_EQUAL)
            return val == fd->ref;
        else if (fd->ref == 0)
            /* as with find, "-000" and "/000" should match everything. The
             * former would (at least no perms), the later not so much, but
             * since it does (now) in find, it's probably expected behavior */
            return TRUE;
        else if (fd->comp == COMP_REQ)
            return (val & fd->ref) == fd->ref;
        else /* COMP_ANY */
            return (val & fd->ref) != 0;
    }
    /* UNIT_SELF */

    mode = (mode_t) fd->ref;

    /* remove permissions that are in OTH */
    mode &= ~((val & S_IRWXO) & mode);
    if (mode == 0)
        return TRUE;

    /* check USR first (quicker than GRP) */
    has = donna_node_get_uid (node, TRUE, &uid);
    if (has != DONNA_NODE_VALUE_SET)
        return FALSE;
    if (priv->user_id == uid)
    {
        /* remove permissions that are in USR */
        mode &= ~(((val & S_IRWXU) / 0100) & mode);
        if (mode == 0)
            return TRUE;
    }

    /* check GRP */
    has = donna_node_get_gid (node, TRUE, &gid);
    if (has != DONNA_NODE_VALUE_SET)
        return FALSE;
    {
        struct _group *g;

        g = get_group (priv, gid);
        if (g && g->is_member)
        {
            /* remove permissions that are in GRP */
            mode &= ~(((val & S_IRWXG) / 010) & mode);
            return mode == 0;
        }
    }

    return FALSE;
}

static void
ct_perms_free_filter_data (DonnaColumnType    *ct,
                           gpointer            filter_data)
{
    g_free (filter_data);
}

static DonnaColumnTypeNeed
ct_perms_set_option (DonnaColumnType    *ct,
                     const gchar        *col_name,
                     const gchar        *arr_name,
                     const gchar        *tv_name,
                     gboolean            is_tree,
                     gpointer            _data,
                     const gchar        *option,
                     gpointer            value,
                     gboolean            toggle,
                     DonnaColumnOptionSaveLocation save_location,
                     GError            **error)
{
    struct tv_col_data *data = _data;
    gpointer v;

    if (streq (option, "format"))
    {
        v = (value) ? value : &data->format;
        if (!DONNA_COLUMN_TYPE_GET_INTERFACE (ct)->helper_set_option (ct,
                    col_name, arr_name, tv_name, is_tree, "column_types/perms",
                    &save_location,
                    option, G_TYPE_STRING, &data->format, v, error))
            return DONNA_COLUMN_TYPE_NEED_NOTHING;

        if (save_location != DONNA_COLUMN_OPTION_SAVE_IN_MEMORY)
            return DONNA_COLUMN_TYPE_NEED_NOTHING;

        if (value)
        {
            g_free (data->format);
            data->format = g_strdup (* (gchar **) value);
        }
        return DONNA_COLUMN_TYPE_NEED_REDRAW;
    }
    else if (streq (option, "format_tooltip"))
    {
        if (value)
        {
            if (** (gchar **) value == '\0')
                v = NULL;
            else
                v = value;
        }
        else
            v = &data->format_tooltip;

        if (!DONNA_COLUMN_TYPE_GET_INTERFACE (ct)->helper_set_option (ct,
                    col_name, arr_name, tv_name, is_tree, "column_types/perms",
                    &save_location,
                    option, G_TYPE_STRING, &data->format_tooltip, v, error))
            return DONNA_COLUMN_TYPE_NEED_NOTHING;

        if (save_location != DONNA_COLUMN_OPTION_SAVE_IN_MEMORY)
            return DONNA_COLUMN_TYPE_NEED_NOTHING;

        if (value)
        {
            g_free (data->format_tooltip);
            if (v)
                data->format_tooltip = g_strdup (* (gchar **) value);
            else
                data->format_tooltip = NULL;
        }
        return DONNA_COLUMN_TYPE_NEED_NOTHING;
    }
    else if (streq (option, "color_user"))
    {
        v = (value) ? value : &data->color_user;
        if (!DONNA_COLUMN_TYPE_GET_INTERFACE (ct)->helper_set_option (ct,
                    col_name, arr_name, tv_name, is_tree, "column_types/perms",
                    &save_location,
                    option, G_TYPE_STRING, &data->color_user, v, error))
            return DONNA_COLUMN_TYPE_NEED_NOTHING;

        if (save_location != DONNA_COLUMN_OPTION_SAVE_IN_MEMORY)
            return DONNA_COLUMN_TYPE_NEED_NOTHING;

        if (value)
        {
            g_free (data->color_user);
            data->color_user = g_strdup (* (gchar **) value);
        }
        return DONNA_COLUMN_TYPE_NEED_REDRAW;
    }
    else if (streq (option, "color_group"))
    {
        v = (value) ? value : &data->color_group;
        if (!DONNA_COLUMN_TYPE_GET_INTERFACE (ct)->helper_set_option (ct,
                    col_name, arr_name, tv_name, is_tree, "column_types/perms",
                    &save_location,
                    option, G_TYPE_STRING, &data->color_group, v, error))
            return DONNA_COLUMN_TYPE_NEED_NOTHING;

        if (save_location != DONNA_COLUMN_OPTION_SAVE_IN_MEMORY)
            return DONNA_COLUMN_TYPE_NEED_NOTHING;

        if (value)
        {
            g_free (data->color_group);
            data->color_group = g_strdup (* (gchar **) value);
        }
        return DONNA_COLUMN_TYPE_NEED_REDRAW;
    }
    else if (streq (option, "color_mixed"))
    {
        v = (value) ? value : &data->color_mixed;
        if (!DONNA_COLUMN_TYPE_GET_INTERFACE (ct)->helper_set_option (ct,
                    col_name, arr_name, tv_name, is_tree, "column_types/perms",
                    &save_location,
                    option, G_TYPE_STRING, &data->color_mixed, v, error))
            return DONNA_COLUMN_TYPE_NEED_NOTHING;

        if (save_location != DONNA_COLUMN_OPTION_SAVE_IN_MEMORY)
            return DONNA_COLUMN_TYPE_NEED_NOTHING;

        if (value)
        {
            g_free (data->color_mixed);
            data->color_mixed = g_strdup (* (gchar **) value);
        }
        return DONNA_COLUMN_TYPE_NEED_REDRAW;
    }
    else if (streq (option, "sort"))
    {
        gint c = data->sort;
        v = (value) ? value : &c;

        if (!DONNA_COLUMN_TYPE_GET_INTERFACE (ct)->helper_set_option (ct,
                    col_name, arr_name, tv_name, is_tree, "column_types/perms",
                    &save_location,
                    option, G_TYPE_INT, &c, v, error))
            return DONNA_COLUMN_TYPE_NEED_NOTHING;

        if (save_location != DONNA_COLUMN_OPTION_SAVE_IN_MEMORY)
            return DONNA_COLUMN_TYPE_NEED_NOTHING;

        if (value)
            data->sort = (gint8) * (gint *) value;
        return DONNA_COLUMN_TYPE_NEED_RESORT;
    }

    g_set_error (error, DONNA_COLUMN_TYPE_ERROR,
            DONNA_COLUMN_TYPE_ERROR_OTHER,
            "ColumnType 'perms': Unknown option '%s'",
            option);
    return DONNA_COLUMN_TYPE_NEED_NOTHING;
}

static gchar *
ct_perms_get_context_alias (DonnaColumnType   *ct,
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
                "ColumnType 'perms': Unknown alias '%s'",
                alias);
        return NULL;
    }

    save_location = DONNA_COLUMN_TYPE_GET_INTERFACE (ct)->
        helper_get_save_location (ct, &extra, TRUE, error);
    if (!save_location)
        return NULL;

    if (extra)
    {
        g_set_error (error, DONNA_CONTEXT_MENU_ERROR,
                DONNA_CONTEXT_MENU_ERROR_OTHER,
                "ColumnType 'perms': Invalid extra '%s' for alias '%s'",
                extra, alias);
        return NULL;
    }

    return g_strconcat (
            prefix, "format:@", save_location, "<",
                prefix, "format:@", save_location, ":%S %V:%H,",
                prefix, "format:@", save_location, ":%p %V:%H,",
                prefix, "format:@", save_location, ":%S,",
                prefix, "format:@", save_location, ":%p,",
                prefix, "format:@", save_location, ":%o,",
                prefix, "format:@", save_location, ":%V:%H,",
                prefix, "format:@", save_location, ":%U:%G,",
                prefix, "format:@", save_location, ":%U,",
                prefix, "format:@", save_location, ":%V,",
                prefix, "format:@", save_location, ":%G,",
                prefix, "format:@", save_location, ":%H,-,",
                prefix, "format:@", save_location, ":=>,",
            prefix, "format_tooltip:@", save_location, "<",
                prefix, "format_tooltip:@", save_location, ":%S %V:%H,",
                prefix, "format_tooltip:@", save_location, ":%p %V:%H,",
                prefix, "format_tooltip:@", save_location, ":%S,",
                prefix, "format_tooltip:@", save_location, ":%p,",
                prefix, "format_tooltip:@", save_location, ":%o,",
                prefix, "format_tooltip:@", save_location, ":%V:%H,",
                prefix, "format_tooltip:@", save_location, ":%U:%G,",
                prefix, "format_tooltip:@", save_location, ":%U,",
                prefix, "format_tooltip:@", save_location, ":%V,",
                prefix, "format_tooltip:@", save_location, ":%G,",
                prefix, "format_tooltip:@", save_location, ":%H,-,",
                prefix, "format_tooltip:@", save_location, ":=>,",
            prefix, "color_user:@", save_location, "<",
                prefix, "color_user:@", save_location, ":blue,",
                prefix, "color_user:@", save_location, ":green,",
                prefix, "color_user:@", save_location, ":red,-,",
                prefix, "color_user:@", save_location, ":=>,",
            prefix, "color_group:@", save_location, "<",
                prefix, "color_group:@", save_location, ":blue,",
                prefix, "color_group:@", save_location, ":green,",
                prefix, "color_group:@", save_location, ":red,-,",
                prefix, "color_group:@", save_location, ":=>,",
            prefix, "color_mixed:@", save_location, "<",
                prefix, "color_mixed:@", save_location, ":#00aaaa,",
                prefix, "color_mixed:@", save_location, ":orange,-,",
                prefix, "color_mixed:@", save_location, ":=>,-,",
            prefix, "sort:@", save_location, "<",
                prefix, "sort:@", save_location, ":perms,",
                prefix, "sort:@", save_location, ":myperms,",
                prefix, "sort:@", save_location, ":uid,",
                prefix, "sort:@", save_location, ":user,",
                prefix, "sort:@", save_location, ":gid,",
                prefix, "sort:@", save_location, ":group>",
            NULL);
}

static void
node_add_prop (DonnaNode *node)
{
    GError *err = NULL;
    GValue v = G_VALUE_INIT;

    g_value_init (&v, G_TYPE_BOOLEAN);
    g_value_set_boolean (&v, TRUE);
    if (G_UNLIKELY (!donna_node_add_property (node, "menu-is-name-markup",
                    G_TYPE_BOOLEAN, &v, (refresher_fn) gtk_true, NULL, &err)))
    {
        gchar *fl = donna_node_get_full_location (node);
        g_warning ("ColumnType 'perms': Failed to set is-name-markup "
                "on node '%s': %s",
                fl,
                (err) ? err->message : "(no error message)");
        g_free (fl);
        g_clear_error (&err);
    }
    g_value_unset (&v);
}

static gboolean
ct_perms_get_context_item_info (DonnaColumnType   *ct,
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
    DonnaColumnTypePermsPrivate *priv = ((DonnaColumnTypePerms *) ct)->priv;
    struct tv_col_data *data = _data;
    const gchar *value = NULL;
    const gchar *ask_title = NULL;
    const gchar *ask_current = NULL;
    const gchar *save_location = NULL;
    gboolean quote_value = FALSE;

    save_location = DONNA_COLUMN_TYPE_GET_INTERFACE (ct)->
        helper_get_save_location (ct, &extra, FALSE, error);
    if (!save_location)
        return FALSE;

    if (streq (item, "format"))
    {
        gchar buf[20], *b = buf;
        mode_t mode = 0640;
        uid_t uid = priv->user_id;
        gid_t gid = getgid ();

        info->is_visible = TRUE;
        info->is_sensitive = TRUE;
        if (!extra)
        {
            b = format_perms ((DonnaColumnTypePerms *) ct, data, data->format,
                    mode, uid, gid, b, 20);
            info->name = g_strconcat ("Column: ", b, NULL);
            info->free_name = TRUE;
            info->new_node_fn = (context_new_node_fn) node_add_prop;
            info->desc = g_strconcat ("Format: ", data->format, NULL);
            info->free_desc = TRUE;
            value = NULL;
            ask_title = "Enter the format for the column";
            ask_current = data->format;
            if (b != buf)
                g_free (b);
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
            info->desc = g_strconcat ("Current format: ", data->format, NULL);
            info->free_desc = TRUE;
            value = NULL;
            ask_title = "Enter the format for the column";
            ask_current = data->format;
        }
        else
        {
            if (*extra == ':')
                ++extra;
            info->icon_special = DONNA_CONTEXT_ICON_IS_RADIO;
            info->is_active = streq (extra, data->format);
            b = format_perms ((DonnaColumnTypePerms *) ct, data, extra,
                    mode, uid, gid, b, 20);
            if (b == buf)
                info->name = g_strdup (b);
            else
                info->name = b;
            info->free_name = TRUE;
            info->new_node_fn = (context_new_node_fn) node_add_prop;
            info->desc = g_strconcat ("Format: ", extra, NULL);
            info->free_desc = TRUE;
            value = extra;
            quote_value = TRUE;
        }
    }
    else if (streq (item, "format_tooltip"))
    {
        gchar buf[20], *b = buf;
        mode_t mode = 0640;
        uid_t uid = priv->user_id;
        gid_t gid = getgid ();

        info->is_visible = TRUE;
        info->is_sensitive = TRUE;

        if (!extra)
        {
            if (data->format_tooltip)
                b = format_perms ((DonnaColumnTypePerms *) ct, data,
                        data->format_tooltip, mode, uid, gid, b, 20);
            else
                sprintf (b, "&lt;no tooltip&gt;");
            info->name = g_strconcat ("Tooltip: ", b, NULL);
            info->free_name = TRUE;
            info->new_node_fn = (context_new_node_fn) node_add_prop;
            info->desc = g_strconcat ("Format: ", data->format_tooltip, NULL);
            info->free_desc = TRUE;
            value = NULL;
            ask_title = "Enter the format for the tooltip";
            ask_current = data->format_tooltip;
            if (b != buf)
                g_free (b);
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
            info->desc = g_strconcat ("Current format: ", data->format_tooltip, NULL);
            info->free_desc = TRUE;
            value = NULL;
            ask_title = "Enter the format for the tooltip";
            ask_current = data->format_tooltip;
        }
        else
        {
            if (*extra == ':')
                ++extra;
            info->icon_special = DONNA_CONTEXT_ICON_IS_RADIO;
            info->is_active = streq (extra, data->format_tooltip);
            b = format_perms ((DonnaColumnTypePerms *) ct, data, extra,
                    mode, uid, gid, b, 20);
            if (b == buf)
                info->name = g_strdup (b);
            else
                info->name = b;
            info->free_name = TRUE;
            info->new_node_fn = (context_new_node_fn) node_add_prop;
            info->desc = g_strconcat ("Format: ", extra, NULL);
            info->free_desc = TRUE;
            value = extra;
            quote_value = TRUE;
        }
    }
    else if (streq (item, "color_user"))
    {
        info->is_visible = TRUE;
        info->is_sensitive = TRUE;
        if (!extra)
        {
            info->name = g_markup_printf_escaped ("User Color: "
                    "<span color=\"%s\">%s</span>",
                    data->color_user, data->color_user);
            info->free_name = TRUE;
            info->new_node_fn = (context_new_node_fn) node_add_prop;
            value = NULL;
            ask_title = "Enter the color for the current user";
            ask_current = data->color_user;
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
            info->desc = g_strconcat ("Current color: ", data->color_user, NULL);
            info->free_desc = TRUE;
            value = NULL;
            ask_title = "Enter the color for the current user";
            ask_current = data->color_user;
        }
        else
        {
            if (*extra == ':')
                ++extra;
            info->icon_special = DONNA_CONTEXT_ICON_IS_RADIO;
            info->is_active = streq (extra, data->color_user);
            info->name = g_markup_printf_escaped ("<span color=\"%s\">%s</span>",
                    extra, extra);
            info->free_name = TRUE;
            info->new_node_fn = (context_new_node_fn) node_add_prop;
            value = extra;
        }
    }
    else if (streq (item, "color_group"))
    {
        info->is_visible = TRUE;
        info->is_sensitive = TRUE;
        if (!extra)
        {
            info->name = g_markup_printf_escaped ("Group Color: "
                    "<span color=\"%s\">%s</span>",
                    data->color_group, data->color_group);
            info->free_name = TRUE;
            info->new_node_fn = (context_new_node_fn) node_add_prop;
            value = NULL;
            ask_title = "Enter the color for a current group";
            ask_current = data->color_group;
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
            info->desc = g_strconcat ("Current color: ", data->color_group, NULL);
            info->free_desc = TRUE;
            value = NULL;
            ask_title = "Enter the color for a current group";
            ask_current = data->color_group;
        }
        else
        {
            if (*extra == ':')
                ++extra;
            info->icon_special = DONNA_CONTEXT_ICON_IS_RADIO;
            info->is_active = streq (extra, data->color_group);
            info->name = g_markup_printf_escaped ("<span color=\"%s\">%s</span>",
                    extra, extra);
            info->free_name = TRUE;
            info->new_node_fn = (context_new_node_fn) node_add_prop;
            value = extra;
        }
    }
    else if (streq (item, "color_mixed"))
    {
        info->is_visible = TRUE;
        info->is_sensitive = TRUE;
        if (!extra)
        {
            info->name = g_markup_printf_escaped ("Mixed Color: "
                    "<span color=\"%s\">%s</span>",
                    data->color_mixed, data->color_mixed);
            info->free_name = TRUE;
            info->new_node_fn = (context_new_node_fn) node_add_prop;
            value = NULL;
            ask_title = "Enter the color for mixed user & group";
            ask_current = data->color_mixed;
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
            info->desc = g_strconcat ("Current color: ", data->color_mixed, NULL);
            info->free_desc = TRUE;
            value = NULL;
            ask_title = "Enter the color for mixed user & group";
            ask_current = data->color_mixed;
        }
        else
        {
            if (*extra == ':')
                ++extra;
            info->icon_special = DONNA_CONTEXT_ICON_IS_RADIO;
            info->is_active = streq (extra, data->color_mixed);
            info->name = g_markup_printf_escaped ("<span color=\"%s\">%s</span>",
                    extra, extra);
            info->free_name = TRUE;
            info->new_node_fn = (context_new_node_fn) node_add_prop;
            value = extra;
        }
    }
    else if (streq (item, "sort"))
    {
        info->is_visible = TRUE;
        info->is_sensitive = TRUE;
        if (!extra)
        {
            info->name = "Sorting Criteria";
            info->submenus = 1;
            return TRUE;
        }
        else if (streq (extra, "perms"))
        {
            info->icon_special = DONNA_CONTEXT_ICON_IS_RADIO;
            info->is_active = data->sort == SORT_PERMS;
            info->name = "Permissions";
            value = extra;
        }
        else if (streq (extra, "myperms"))
        {
            info->icon_special = DONNA_CONTEXT_ICON_IS_RADIO;
            info->is_active = data->sort == SORT_MY_PERMS;
            info->name = "Own Permissions";
            value = extra;
        }
        else if (streq (extra, "uid"))
        {
            info->icon_special = DONNA_CONTEXT_ICON_IS_RADIO;
            info->is_active = data->sort == SORT_USER_ID;
            info->name = "User ID";
            value = extra;
        }
        else if (streq (extra, "user"))
        {
            info->icon_special = DONNA_CONTEXT_ICON_IS_RADIO;
            info->is_active = data->sort == SORT_USER_NAME;
            info->name = "User Name";
            value = extra;
        }
        else if (streq (extra, "gid"))
        {
            info->icon_special = DONNA_CONTEXT_ICON_IS_RADIO;
            info->is_active = data->sort == SORT_GROUP_ID;
            info->name = "Group ID";
            value = extra;
        }
        else if (streq (extra, "group"))
        {
            info->icon_special = DONNA_CONTEXT_ICON_IS_RADIO;
            info->is_active = data->sort == SORT_GROUP_NAME;
            info->name = "Group Name";
            value = extra;
        }
        else
        {
            g_set_error (error, DONNA_CONTEXT_MENU_ERROR,
                    DONNA_CONTEXT_MENU_ERROR_OTHER,
                    "ColumnType 'perms': Invalid extra '%s' for item '%s'",
                    extra, item);
            return FALSE;
        }
    }
    else
    {
        g_set_error (error, DONNA_CONTEXT_MENU_ERROR,
                DONNA_CONTEXT_MENU_ERROR_UNKNOWN_ITEM,
                "ColumnType 'perms': Unknown item '%s'",
                item);
        return FALSE;
    }

    info->trigger = DONNA_COLUMN_TYPE_GET_INTERFACE (ct)->
        helper_get_set_option_trigger (item, value, quote_value,
                ask_title, NULL, ask_current, save_location);
    info->free_trigger = TRUE;

    return TRUE;
}
