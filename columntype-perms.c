
#include <glib-object.h>
#include <unistd.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <pwd.h>
#include <grp.h>
#include "columntype.h"
#include "columntype-perms.h"
#include "node.h"
#include "donna.h"
#include "conf.h"
#include "macros.h"

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
static DonnaColumnTypeNeed ct_perms_refresh_data    (DonnaColumnType    *ct,
                                                     const gchar        *tv_name,
                                                     const gchar        *col_name,
                                                     const gchar        *arr_name,
                                                     gpointer           *data);
static void             ct_perms_free_data          (DonnaColumnType    *ct,
                                                     gpointer            data);
static GPtrArray *      ct_perms_get_props          (DonnaColumnType    *ct,
                                                     gpointer            data);
static GtkSortType      ct_perms_get_default_sort_order
                                                    (DonnaColumnType    *ct,
                                                     const gchar        *tv_name,
                                                     const gchar        *col_name,
                                                     const gchar        *arr_name,
                                                     gpointer            data);
static GtkMenu *        ct_perms_get_options_menu   (DonnaColumnType    *ct,
                                                     gpointer            data);
static gboolean         ct_perms_handle_context     (DonnaColumnType    *ct,
                                                     gpointer            data,
                                                     DonnaNode          *node,
                                                     DonnaTreeView      *treeview);
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

static void
ct_perms_columntype_init (DonnaColumnTypeInterface *interface)
{
    interface->get_name                 = ct_perms_get_name;
    interface->get_renderers            = ct_perms_get_renderers;
    interface->refresh_data             = ct_perms_refresh_data;
    interface->free_data                = ct_perms_free_data;
    interface->get_props                = ct_perms_get_props;
    interface->get_default_sort_order   = ct_perms_get_default_sort_order;
    interface->get_options_menu         = ct_perms_get_options_menu;
    interface->handle_context           = ct_perms_handle_context;
    interface->render                   = ct_perms_render;
    interface->set_tooltip              = ct_perms_set_tooltip;
    interface->node_cmp                 = ct_perms_node_cmp;
}

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
            DONNA_TYPE_COLUMNTYPE_PERMS,
            DonnaColumnTypePermsPrivate);
    priv->user_id = getuid ();
    priv->nb_groups = getgroups (0, NULL);
    priv->group_ids = g_new (gid_t, priv->nb_groups);
    getgroups (priv->nb_groups, priv->group_ids);
}

G_DEFINE_TYPE_WITH_CODE (DonnaColumnTypePerms, donna_column_type_perms,
        G_TYPE_OBJECT,
        G_IMPLEMENT_INTERFACE (DONNA_TYPE_COLUMNTYPE, ct_perms_columntype_init)
        )

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

    priv = DONNA_COLUMNTYPE_PERMS (object)->priv;
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
        DONNA_COLUMNTYPE_PERMS (object)->priv->app = g_value_dup_object (value);
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
        g_value_set_object (value, DONNA_COLUMNTYPE_PERMS (object)->priv->app);
    else
        G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
}

static const gchar *
ct_perms_get_name (DonnaColumnType *ct)
{
    g_return_val_if_fail (DONNA_IS_COLUMNTYPE_PERMS (ct), NULL);
    return "perms";
}

static const gchar *
ct_perms_get_renderers (DonnaColumnType   *ct)
{
    g_return_val_if_fail (DONNA_IS_COLUMNTYPE_PERMS (ct), NULL);
    return "t";
}

static DonnaColumnTypeNeed
ct_perms_refresh_data (DonnaColumnType    *ct,
                       const gchar        *tv_name,
                       const gchar        *col_name,
                       const gchar        *arr_name,
                       gpointer           *_data)
{
    DonnaColumnTypePerms *ctperms = DONNA_COLUMNTYPE_PERMS (ct);
    DonnaConfig *config;
    struct tv_col_data *data;
    DonnaColumnTypeNeed need = DONNA_COLUMNTYPE_NEED_NOTHING;
    gchar *s;
    gint i;

    config = donna_app_peek_config (ctperms->priv->app);

    if (!*_data)
        *_data = g_new0 (struct tv_col_data, 1);
    data = *_data;

    s = donna_config_get_string_column (config, tv_name, col_name, arr_name,
            "columntypes/perms", "format", "%P");
    if (!streq (data->format, s))
    {
        g_free (data->format);
        data->format = g_markup_escape_text (s, -1);
        need = DONNA_COLUMNTYPE_NEED_REDRAW;
    }
    else
        g_free (s);

    s = donna_config_get_string_column (config, tv_name, col_name, arr_name,
            "columntypes/perms", "format_tooltip", "%p %U:%G");
    if (!streq(data->format_tooltip, s))
    {
        g_free (data->format_tooltip);
        /* empty string to disable tooltip */
        data->format_tooltip = (*s == '\0') ? NULL : g_markup_escape_text (s, -1);
        need = DONNA_COLUMNTYPE_NEED_REDRAW;
    }
    else
        g_free (s);

    s = donna_config_get_string_column (config, tv_name, col_name, arr_name,
            "columntypes/perms", "color_user", "green");
    if (!streq (data->color_user, s))
    {
        g_free (data->color_user);
        data->color_user = s;
        need = DONNA_COLUMNTYPE_NEED_REDRAW;
    }
    else
        g_free (s);

    s = donna_config_get_string_column (config, tv_name, col_name, arr_name,
            "columntypes/perms", "color_group", "blue");
    if (!streq (data->color_group, s))
    {
        g_free (data->color_group);
        data->color_group = s;
        need = DONNA_COLUMNTYPE_NEED_REDRAW;
    }
    else
        g_free (s);

    s = donna_config_get_string_column (config, tv_name, col_name, arr_name,
            "columntypes/perms", "color_mixed", "#00aaaa");
    if (!streq (data->color_mixed, s))
    {
        g_free (data->color_mixed);
        data->color_mixed = s;
        need = DONNA_COLUMNTYPE_NEED_REDRAW;
    }
    else
        g_free (s);

    i = donna_config_get_int_column (config, tv_name, col_name, arr_name,
            "columntypes/perms", "sort", SORT_MY_PERMS);
    if (i != data->sort)
    {
        data->sort = (gint8) i;
        need = DONNA_COLUMNTYPE_NEED_RESORT;
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

    g_return_val_if_fail (DONNA_IS_COLUMNTYPE_PERMS (ct), NULL);

    props = g_ptr_array_new_full (3, g_free);
    g_ptr_array_add (props, g_strdup ("mode"));
    g_ptr_array_add (props, g_strdup ("uid"));
    g_ptr_array_add (props, g_strdup ("gid"));

    return props;
}

static GtkSortType
ct_perms_get_default_sort_order (DonnaColumnType *ct,
                                 const gchar     *tv_name,
                                 const gchar     *col_name,
                                 const gchar     *arr_name,
                                 gpointer         _data)
{
    struct tv_col_data *data = _data;

    return (donna_config_get_boolean_column (donna_app_peek_config (
                    DONNA_COLUMNTYPE_PERMS (ct)->priv->app),
                /* no default since it's based on option sort */
                tv_name, col_name, arr_name, NULL, "desc_first",
                data->sort == SORT_PERMS || data->sort == SORT_MY_PERMS))
        ? GTK_SORT_DESCENDING : GTK_SORT_ASCENDING;
}

static GtkMenu *
ct_perms_get_options_menu (DonnaColumnType    *ct,
                           gpointer            data)
{
    /* FIXME */
    return NULL;
}

static gboolean
ct_perms_handle_context (DonnaColumnType    *ct,
                         gpointer            data,
                         DonnaNode          *node,
                         DonnaTreeView      *treeview)
{
    /* FIXME */
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

static void
add_colored_perm (DonnaColumnTypePermsPrivate   *priv,
                  struct tv_col_data            *data,
                  gchar                        **str,
                  gssize                        *max,
                  gssize                        *total,
                  mode_t                         mode,
                  uid_t                          uid,
                  gid_t                          gid,
                  gchar                          perm)
{
    gchar u_perm = perm + 'A' - 'a';
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
            need = snprintf (*str, *max,
                    "<span color=\"%s\">%c</span>", data->color_group, u_perm);
            goto done;
        }
        group_has_perm = 1;
    }

    if (mode & S_USR)
    {
        if (uid == priv->user_id)
            need = snprintf (*str, *max, "<span color=\"%s\">%c</span>",
                    (group_has_perm == 1) ? data->color_mixed : data->color_user,
                    u_perm);

        else
            need = snprintf (*str, *max, "<span color=\"%s\">%c</span>",
                (group_has_perm == 1) ? data->color_group : data->color_user,
                perm);
        goto done;
    }

    if (group_has_perm == 1)
        need = snprintf (*str, *max, "<span color=\"%s\">%c</span>",
                data->color_group, perm);
    else
        need = snprintf (*str, *max, "%c", perm);

done:
    if (need < *max)
    {
        *max -= need;
        *str += need;
    }
    else
        *max = 0;
    *total += need;
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
static gssize
print_perms (DonnaColumnTypePerms   *ctperms,
             struct tv_col_data     *data,
             gchar                  *str,
             gssize                  max,
             const gchar            *fmt,
             mode_t                  mode,
             uid_t                   uid,
             gid_t                   gid)
{
    DonnaColumnTypePermsPrivate *priv = ctperms->priv;
    gssize total = 0;

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
                case 'P':
                    add_colored_perm (priv, data, &str, &max, &total,
                            mode, uid, gid, 'r');
                    add_colored_perm (priv, data, &str, &max, &total,
                            mode, uid, gid, 'w');
                    add_colored_perm (priv, data, &str, &max, &total,
                            mode, uid, gid, 'x');
                    fmt += 2;
                    continue;
                case 'u':
                case 'U':
                    {
                        struct _user *u;

                        u = get_user (priv, uid);
                        if (!u)
                            s = "???";
                        else
                            s = u->name;

                        if (fmt[1] == 'u' || uid != priv->user_id)
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
                case 'G':
                    {
                        struct _group *g;

                        g = get_group (priv, gid);
                        if (!g)
                            s = "???";
                        else
                            s = g->name;

                        if (fmt[1] == 'g' || (g && !g->is_member))
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
                if (need < max)
                {
                    max -= need;
                    str += need;
                }
                else
                    max = 0;
                fmt += 2;
                total += need;
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
    mode_t mode;
    uid_t uid;
    gid_t gid;
    gchar buf[20], *b = buf;
    gssize len;

    g_return_val_if_fail (DONNA_IS_COLUMNTYPE_PERMS (ct), NULL);

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

    len = print_perms ((DonnaColumnTypePerms *) ct, data, b, 20, data->format,
            mode, uid, gid);
    if (len >= 20)
    {
        b = g_new (gchar, ++len);
        print_perms ((DonnaColumnTypePerms *) ct, data, b, len, data->format,
                mode, uid, gid);
    }
    g_object_set (renderer, "visible", TRUE, "markup", b, NULL);
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
    gssize len;
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

    len = print_perms ((DonnaColumnTypePerms *) ct, data, b, 20,
            data->format_tooltip, mode, uid, gid);
    if (len >= 20)
    {
        b = g_new (gchar, ++len);
        print_perms ((DonnaColumnTypePerms *) ct, data, b, len,
                data->format_tooltip, mode, uid, gid);
    }
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
