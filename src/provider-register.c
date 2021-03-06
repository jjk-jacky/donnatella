/*
 * donnatella - Copyright (C) 2014 Olivier Brunel
 *
 * provider-register.c
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

#include <gtk/gtk.h>
#include "provider-register.h"
#include "provider-command.h"
#include "provider.h"
#include "node.h"
#include "app.h"
#include "command.h"
#include "util.h"
#include "macros.h"
#include "debug.h"

static const gchar const * reg_default   = "_";
static const gchar const * reg_clipboard = "+";

struct reg
{
    gchar *name;
    DonnaRegisterType type;
    GHashTable *hashtable; /* hashmap of full locations (value == GList*) */
    GList *list; /* to preserve order */
    GList *last; /* so we can append w/out traversing the whole list */
};

struct _DonnaProviderRegisterPrivate
{
    /* we need a recursive mutex because you can put registers inside registers,
     * and then getting the nodes from that register could require to create the
     * node for it, but since we've locked the provider as we're iterating over
     * a register's content, we'd deadlock. */
    GRecMutex rec_mutex;
    GSList *registers;
};

#define _ATOM_GNOME "x-special/gnome-copied-files"
#define _ATOM_KDE   "application/x-kde-cutselection"
#define _ATOM_URIS  "text/uri-list"

static GdkAtom atom_gnome;
static GdkAtom atom_kde;
static GdkAtom atom_uris;


static inline DonnaNode *   get_node_for            (DonnaProviderRegister  *pr,
                                                     const gchar            *name);
static DonnaNode *          new_node_for_reg        (DonnaProvider          *provider,
                                                     struct reg             *reg,
                                                     GError                **error);
static inline void          update_node_type        (DonnaProvider          *provider,
                                                     DonnaNode              *node,
                                                     guint                   type);

/* GObject */
static void             provider_register_contructed    (GObject            *object);
/* DonnaProvider */
static const gchar *    provider_register_get_domain    (DonnaProvider      *provider);
static DonnaProviderFlags provider_register_get_flags   (DonnaProvider      *provider);
/* DonnaProviderBase */
static DonnaTaskState   provider_register_new_node      (DonnaProviderBase  *provider,
                                                         DonnaTask          *task,
                                                         const gchar        *location);
static DonnaTaskState   provider_register_has_children  (DonnaProviderBase  *provider,
                                                         DonnaTask          *task,
                                                         DonnaNode          *node,
                                                         DonnaNodeType       node_types);
static DonnaTaskState   provider_register_get_children  (DonnaProviderBase  *provider,
                                                         DonnaTask          *task,
                                                         DonnaNode          *node,
                                                         DonnaNodeType       node_types);
static DonnaTaskState   provider_register_trigger_node  (DonnaProviderBase  *provider,
                                                         DonnaTask          *task,
                                                         DonnaNode          *node);
static gboolean         provider_register_support_io    (DonnaProviderBase  *provider,
                                                         DonnaIoType         type,
                                                         gboolean            is_source,
                                                         GPtrArray          *sources,
                                                         DonnaNode          *dest,
                                                         const gchar        *new_name,
                                                         GError            **error);
static DonnaTaskState  provider_register_io             (DonnaProviderBase  *provider,
                                                         DonnaTask          *task,
                                                         DonnaIoType         type,
                                                         gboolean            is_source,
                                                         GPtrArray          *sources,
                                                         DonnaNode          *dest,
                                                         const gchar        *new_name);
static DonnaTaskState   provider_register_new_child     (DonnaProviderBase  *provider,
                                                         DonnaTask          *task,
                                                         DonnaNode          *parent,
                                                         DonnaNodeType       type,
                                                         const gchar        *name);
static DonnaTaskState   provider_register_remove_from   (DonnaProviderBase  *provider,
                                                         DonnaTask          *task,
                                                         GPtrArray          *nodes,
                                                         DonnaNode          *source);

static void
provider_register_provider_init (DonnaProviderInterface *interface)
{
    interface->get_domain   = provider_register_get_domain;
    interface->get_flags    = provider_register_get_flags;
}

G_DEFINE_TYPE_WITH_CODE (DonnaProviderRegister, donna_provider_register,
        DONNA_TYPE_PROVIDER_BASE,
        G_IMPLEMENT_INTERFACE (DONNA_TYPE_PROVIDER, provider_register_provider_init)
        )

static void
donna_provider_register_class_init (DonnaProviderRegisterClass *klass)
{
    DonnaProviderBaseClass *pb_class;
    GObjectClass *o_class;

    o_class = (GObjectClass *) klass;
    o_class->constructed    = provider_register_contructed;

    pb_class = (DonnaProviderBaseClass *) klass;

    pb_class->task_visibility.new_node      = DONNA_TASK_VISIBILITY_INTERNAL_FAST;
    pb_class->task_visibility.has_children  = DONNA_TASK_VISIBILITY_INTERNAL_FAST;
    pb_class->task_visibility.get_children  = DONNA_TASK_VISIBILITY_INTERNAL_FAST;
    pb_class->task_visibility.trigger_node  = DONNA_TASK_VISIBILITY_INTERNAL_FAST;
    pb_class->task_visibility.io            = DONNA_TASK_VISIBILITY_INTERNAL_FAST;
    pb_class->task_visibility.new_child     = DONNA_TASK_VISIBILITY_INTERNAL_FAST;
    pb_class->task_visibility.remove_from   = DONNA_TASK_VISIBILITY_INTERNAL_FAST;

    pb_class->new_node      = provider_register_new_node;
    pb_class->has_children  = provider_register_has_children;
    pb_class->get_children  = provider_register_get_children;
    pb_class->trigger_node  = provider_register_trigger_node;
    pb_class->support_io    = provider_register_support_io;
    pb_class->io            = provider_register_io;
    pb_class->new_child     = provider_register_new_child;
    pb_class->remove_from   = provider_register_remove_from;

    g_type_class_add_private (klass, sizeof (DonnaProviderRegisterPrivate));

    atom_gnome = gdk_atom_intern_static_string (_ATOM_GNOME);
    atom_kde   = gdk_atom_intern_static_string (_ATOM_KDE);
    atom_uris  = gdk_atom_intern_static_string (_ATOM_URIS);
}

static void
donna_provider_register_init (DonnaProviderRegister *provider)
{
    DonnaProviderRegisterPrivate *priv;

    priv = provider->priv = G_TYPE_INSTANCE_GET_PRIVATE (provider,
            DONNA_TYPE_PROVIDER_REGISTER,
            DonnaProviderRegisterPrivate);
    g_rec_mutex_init (&priv->rec_mutex);
}

/* internals */

static void
free_register (struct reg *reg)
{
    if (reg->name != reg_default && reg->name != reg_clipboard)
        g_free (reg->name);
    g_hash_table_unref (reg->hashtable);
    g_list_free (reg->list);
    g_free (reg);
}

/* assumes lock */
static inline struct reg *
get_register (GSList *l, const gchar *name)
{
    for ( ; l; l = l->next)
        if (streq (name, ((struct reg *) l->data)->name))
            return l->data;
    return NULL;
}

static inline struct reg *
new_register (const gchar *name, DonnaRegisterType type)
{
    struct reg *reg;

    reg = g_new (struct reg, 1);
    if (streq (name, reg_default))
        reg->name = (gchar *) reg_default;
    else if (streq (name, reg_clipboard))
        reg->name = (gchar *) reg_clipboard;
    else
        reg->name = g_strdup (name);
    reg->type = type;
    reg->hashtable = g_hash_table_new_full (g_str_hash, g_str_equal, g_free, NULL);
    reg->list = NULL;
    reg->last = NULL;

    return reg;
}

static void
update_special_nodes (DonnaProviderRegister *pr, const gchar *name, gboolean exists)
{
    DonnaNode *node;
    gchar buf[48], *b = buf;
    GValue v = G_VALUE_INIT;
    const gchar *suffix[] = { "", "_copy", "_move", "_new_folder" };
    gint i;

    for (i = (sizeof (suffix) / sizeof (*suffix)) - 1; i >= 0; --i)
    {
        if (snprintf (buf, 48, "%s/paste%s", name, suffix[i]) >= 48)
            b = g_strdup_printf ("%s/paste%s", name, suffix[i]);
        node = get_node_for (pr, b);
        if (node)
        {
            g_value_init (&v, G_TYPE_BOOLEAN);
            g_value_set_boolean (&v, exists);
            donna_node_set_property_value (node, "menu-is-sensitive", &v);
            g_value_unset (&v);
            g_object_unref (node);
        }
        if (b != buf)
        {
            g_free (b);
            b = buf;
        }
    }
}

static inline void
emit_drop (DonnaProviderRegister *pr, DonnaNode *node, gboolean still_exists)
{
    if (still_exists)
    {
        GPtrArray *arr = g_ptr_array_new ();
        donna_provider_node_children ((DonnaProvider *) pr, node,
                DONNA_NODE_ITEM | DONNA_NODE_CONTAINER, arr);
        g_ptr_array_unref (arr);
    }
    else
        donna_provider_node_deleted ((DonnaProvider *) pr, node);
    g_object_unref (node);
}

static gboolean
drop_register (DonnaProviderRegister *pr, const gchar *name, DonnaNode **node)
{
    DonnaProviderRegisterPrivate *priv = pr->priv;
    DonnaNode *n;
    struct reg *reg;
    GSList *l;
    GSList *prev;
    gboolean ret = FALSE;

    if (!node)
        g_rec_mutex_lock (&priv->rec_mutex);
    l = priv->registers;
    prev = NULL;
    while (l)
    {
        reg = l->data;

        if (streq (reg->name, name))
        {
            if (prev)
                prev->next = l->next;
            else
                priv->registers = l->next;

            g_slist_free1 (l);
            ret = TRUE;
            break;
        }

        prev = l;
        l = l->next;
    }
    if (!node)
        g_rec_mutex_unlock (&priv->rec_mutex);

    if (ret)
    {
        n = get_node_for (pr, name);
        if (node)
            *node = n;
        else if (n)
            emit_drop (pr, n, reg->name == reg_default || reg->name == reg_clipboard);
        free_register (reg);
    }
    else if (node)
        *node = NULL;

    g_info ("Dropped register '%s'", name);

    return ret;
}

/* clipboard */

static void
clipboard_get (GtkClipboard             *clipboard,
               GtkSelectionData         *sd,
               guint                     info,
               DonnaProviderRegister    *pr)
{
    DonnaProviderRegisterPrivate *priv = pr->priv;
    struct reg *reg;
    GString *str;
    GList *l;

    g_rec_mutex_lock (&priv->rec_mutex);
    reg = get_register (priv->registers, reg_clipboard);
    if (!reg)
    {
        g_rec_mutex_unlock (&priv->rec_mutex);
        /* we don't have a reg means we dropped it. IOW we own the clipboard,
         * and it is empty */
        if (info == 99)
            /* because setting an empty string (len=0) doesn't seem to work
             * properly, and pasting gets "n " for some reason */
            gtk_selection_data_set_text (sd, "\0", 1);
        else if (info == 3)
            gtk_selection_data_set (sd, atom_uris, 8,
                    (const guchar *) "", 0);
        else
            gtk_selection_data_set (sd, (info == 1) ? atom_gnome : atom_kde,
                    8,
                    (const guchar *) "copy\n", 5);
        return;
    }

    str = g_string_new (NULL);
    if (info < 3)
        g_string_append_printf (str, "%s\n",
                (reg->type == DONNA_REGISTER_CUT) ? "cut" : "copy");

    for (l = reg->list; l; l = l->next)
    {
        GError *err = NULL;
        gchar *s;

        if (info < 99)
        {
            s = g_filename_to_uri ((gchar *) l->data, NULL, &err);
            if (G_UNLIKELY (!s))
            {
                g_warning ("Provider 'register': clipboard_get() for CLIPBOARD: "
                        "Failed to convert '%s' to URI: %s",
                        (gchar *) l->data, err->message);
                g_clear_error (&err);
            }
            else
            {
                g_string_append (str, s);
                g_string_append_c (str, '\n');
                g_free (s);
            }
        }
        else
        {
            g_string_append (str, l->data);
            g_string_append_c (str, '\n');
        }
    }
    g_rec_mutex_unlock (&priv->rec_mutex);

    if (info < 99)
        gtk_selection_data_set (sd, (info == 1) ? atom_gnome
                : ((info == 2) ? atom_kde : atom_uris),
                8,
                (const guchar *) str->str, (gint) str->len);
    else
        gtk_selection_data_set_text (sd, str->str, (gint) str->len);
    g_string_free (str, TRUE);
}

static void
clipboard_clear (GtkClipboard *clipboard, DonnaProviderRegister *pr)
{
    drop_register (pr, reg_clipboard, NULL);
}

static DonnaTaskState
task_take_clipboard_ownership (DonnaTask *task, gpointer _data)
{
    struct {
        DonnaProviderRegister *pr;
        gboolean clear;
    } *data = _data;
    GtkClipboard *clipboard;
    GtkTargetList *list;
    GtkTargetEntry targets[] = {
        { (gchar *) _ATOM_GNOME, 0, 1 },
        { (gchar *) _ATOM_KDE,   0, 2 },
        { (gchar *) _ATOM_URIS,  0, 3 },
    };
    GtkTargetEntry *entries;
    gint nb;
    gboolean ret;

    clipboard = gtk_clipboard_get (GDK_SELECTION_CLIPBOARD);
    if (G_UNLIKELY (!clipboard))
        return DONNA_TASK_FAILED;

    list = gtk_target_list_new (targets, 3);
    gtk_target_list_add_text_targets (list, 99);
    entries = gtk_target_table_new_from_list (list, &nb);

    ret = gtk_clipboard_set_with_owner (clipboard, entries, (guint) nb,
            (GtkClipboardGetFunc) clipboard_get,
            (GtkClipboardClearFunc) clipboard_clear,
            (GObject *) data->pr);
    /* so data can be stored when donna exits */
    gtk_clipboard_set_can_store (clipboard, NULL, 0);
    if (ret && data->clear)
    {
        gtk_clipboard_clear (clipboard);
        g_info ("Dropped register '+'");
    }

    gtk_target_table_free (entries, nb);
    gtk_target_list_unref (list);
    return (ret) ? DONNA_TASK_DONE : DONNA_TASK_FAILED;
}

static gboolean
take_clipboard_ownership (DonnaProviderRegister *pr, gboolean clear)
{
    DonnaTask *task;
    struct {
        DonnaProviderRegister *pr;
        gboolean clear;
    } data = { pr, clear };
    gboolean ret;

    task = donna_task_new (task_take_clipboard_ownership, &data, NULL);
    donna_task_set_visibility (task, DONNA_TASK_VISIBILITY_INTERNAL_GUI);
    donna_app_run_task (((DonnaProviderBase *) pr)->app, g_object_ref (task));
    donna_task_wait_for_it (task, NULL, NULL);
    ret = donna_task_get_state (task) == DONNA_TASK_DONE;
    g_object_unref (task);
    return ret;
}

static DonnaTaskState
task_get_from_clipboard (DonnaTask *task, gpointer _data)
{
    struct {
        GHashTable **hashtable;
        GList **list;
        GList **last;
        DonnaRegisterType *type;
        GString **str;
        GError **error;
    } *data = _data;
    GtkClipboard *clipboard;
    GtkSelectionData *sd;
    GdkAtom *atoms;
    GdkAtom atom;
    const gchar *s_list;
    const gchar *s, *e;
    gint nb;
    gint i;

    clipboard = gtk_clipboard_get (GDK_SELECTION_CLIPBOARD);
    if (!gtk_clipboard_wait_for_targets (clipboard, &atoms, &nb))
    {
        *data->type = DONNA_REGISTER_UNKNOWN;
        return DONNA_TASK_DONE;
    }

    for (i = 0; i < nb; ++i)
    {
        if (atoms[i] == atom_gnome || atoms[i] == atom_kde
                || atoms[i] == atom_uris)
        {
            atom = atoms[i];
            break;
        }
    }
    if (i >= nb)
    {
        g_free (atoms);
        *data->type = DONNA_REGISTER_UNKNOWN;
        return DONNA_TASK_DONE;
    }

    sd = gtk_clipboard_wait_for_contents (clipboard, atom);
    if (!sd)
    {
        g_set_error (data->error, DONNA_PROVIDER_REGISTER_ERROR,
                DONNA_PROVIDER_REGISTER_ERROR_OTHER,
                "Failed to get content from CLIPBOARD");
        return DONNA_TASK_FAILED;
    }

    s = s_list = (const gchar *) gtk_selection_data_get_data (sd);
    if (atom != atom_uris)
    {
        e = strchr (s, '\n');
        if (streqn (s, "cut", 3))
            *data->type = DONNA_REGISTER_CUT;
        else if (streqn (s, "copy", 4))
            *data->type = DONNA_REGISTER_COPY;
        else
        {
            g_set_error (data->error, DONNA_PROVIDER_REGISTER_ERROR,
                    DONNA_PROVIDER_REGISTER_ERROR_OTHER,
                    "Invalid data from CLIPBOARD, unknown operation '%.*s'",
                    (gint) (e - s), s);
            gtk_selection_data_free (sd);
            return DONNA_TASK_FAILED;
        }
        s = e + 1;
    }
    else
        *data->type = DONNA_REGISTER_UNKNOWN;

    while ((e = strchr (s, '\n')))
    {
        GError *err = NULL;
        gchar buf[255], *b;
        gchar *filename;
        gint len;

        len = (gint) (e - s);
        if (len < 255)
        {
            b = buf;
            snprintf (b, 255, "%.*s", len, s);
        }
        else
            b = g_strdup_printf ("%.*s", len, s);

        filename = g_filename_from_uri (b, NULL, &err);
        if (G_UNLIKELY (!filename))
        {
            if (!data->str)
                continue;
            if (!*data->str)
                *data->str = g_string_new (NULL);

            g_string_append_printf (*data->str,
                    "\n- Failed to get filename from '%s': %s",
                    b, err->message);
            g_clear_error (&err);
            if (b != buf)
                g_free (b);
            s = e + 1;
            continue;
        }
        if (b != buf)
            g_free (b);

        if (*data->list)
        {
            /* append to last item directly */
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wunused-result"
            g_list_append (*data->last, filename);
#pragma GCC diagnostic pop
            *data->last = (*data->last)->next;
        }
        else
            *data->list = *data->last = g_list_append (*data->list, filename);
        g_hash_table_add (*data->hashtable, *data->last);
        s = e + 1;
    }
    gtk_selection_data_free (sd);
    return DONNA_TASK_DONE;
}

static gboolean
get_from_clipboard (DonnaApp             *app,
                    GHashTable          **hashtable,
                    GList               **list,
                    GList               **last,
                    DonnaRegisterType    *type,
                    GString             **str,
                    GError              **error)
{
    DonnaTask *task;
    struct {
        GHashTable **hashtable;
        GList **list;
        GList **last;
        DonnaRegisterType *type;
        GString **str;
        GError **error;
    } data = { hashtable, list, last, type, str, error };
    gboolean ret;

    g_assert (hashtable != NULL && *hashtable != NULL);
    g_assert (type != NULL);

    task = donna_task_new (task_get_from_clipboard, &data, NULL);
    donna_task_set_visibility (task, DONNA_TASK_VISIBILITY_INTERNAL_GUI);
    donna_app_run_task (app, g_object_ref (task));
    donna_task_wait_for_it (task, NULL, NULL);
    ret = donna_task_get_state (task) == DONNA_TASK_DONE;
    g_object_unref (task);
    return ret;
}

/* registers */

static inline gboolean
is_valid_register_name (const gchar **name, GError **error)
{
    /* if no name was given (i.e. NULL or empty string) we use reg_default */
    if (!*name || **name == '\0')
    {
        *name = reg_default;
        return TRUE;
    }

    /* register names must start with a letter. Only exceptions are the special
     * names:
     * reg_clipboard    is the name for CLIPBOARD (i.e. the system clipboard)
     * reg_default      is the name of our "default" register (see above)
     */
    if ((**name >= 'a' && **name <= 'z') || (**name >= 'A' && **name <= 'Z')
            || streq (*name, reg_default) || streq (*name, reg_clipboard))
        /* valid if there are no '/' in the name. That way we'll be able to have
         * special nodes like "<register>/cut" that can be triggered... */
        if (strchr (*name, '/') == NULL)
            return TRUE;

    g_set_error (error, DONNA_PROVIDER_ERROR,
            DONNA_PROVIDER_ERROR_LOCATION_NOT_FOUND,
            "Invalid register name: '%s'", *name);
    return FALSE;
}

static DonnaNode *
get_node_for (DonnaProviderRegister *pr, const gchar *name)
{
    DonnaProviderBase *pb = (DonnaProviderBase *) pr;
    DonnaProviderBaseClass *klass;
    DonnaNode *node;

    klass = DONNA_PROVIDER_BASE_GET_CLASS (pb);
    klass->lock_nodes (pb);
    node = klass->get_cached_node (pb, name);
    klass->unlock_nodes (pb);

    return node;
}

static DonnaNode *
create_node_for (DonnaProviderRegister *pr, const gchar *name, GError **error)
{
    DonnaProviderRegisterPrivate *priv = pr->priv;
    DonnaProviderBase *pb = (DonnaProviderBase *) pr;
    DonnaProviderBaseClass *klass;
    DonnaNode *node;
    DonnaNode *n;
    struct reg *reg;

    g_rec_mutex_lock (&priv->rec_mutex);
    reg = get_register (priv->registers, name);
    if (G_UNLIKELY (!reg))
    {
        g_rec_mutex_unlock (&priv->rec_mutex);
        g_set_error (error, DONNA_PROVIDER_ERROR,
                DONNA_PROVIDER_ERROR_LOCATION_NOT_FOUND,
                "Provider 'register': Register '%s' doesn't exist", name);
        return NULL;
    }

    node = new_node_for_reg ((DonnaProvider *) pr, reg, error);
    if (G_UNLIKELY (!node))
    {
        g_rec_mutex_unlock (&priv->rec_mutex);
        return NULL;
    }

    klass = DONNA_PROVIDER_BASE_GET_CLASS (pb);
    klass->lock_nodes (pb);
    n = klass->get_cached_node (pb, name);
    if (n)
    {
        /* already added while we were busy */
        g_object_unref (node);
        node = n;
    }
    else
        klass->add_node_to_cache (pb, node);
    klass->unlock_nodes (pb);

    g_rec_mutex_unlock (&priv->rec_mutex);
    return node;
}

static gboolean
register_drop (DonnaProviderRegister *pr,
               const gchar           *name,
               GError               **error)
{
    gboolean ret = FALSE;

    if (*name == *reg_clipboard)
        ret = take_clipboard_ownership (pr, TRUE);
    else
        ret = drop_register (pr, name, NULL);

    if (ret)
        update_special_nodes (pr, name, FALSE);

    return ret;
}

/* assumes lock */
static inline gboolean
add_node_to_reg (struct reg *reg, DonnaNode *node, gboolean is_clipboard)
{
    gchar *s;
    gboolean added;

    if (is_clipboard)
        s = donna_node_get_location (node);
    else
        s = donna_node_get_full_location (node);

    added = !g_hash_table_contains (reg->hashtable, s);
    if (added)
    {
        if (reg->list)
        {
            /* append to last item directly */
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wunused-result"
            g_list_append (reg->last, s);
#pragma GCC diagnostic pop
            reg->last = reg->last->next;
        }
        else
            /* create first item */
            reg->list = reg->last = g_list_append (reg->list, s);
        g_hash_table_insert (reg->hashtable, s, reg->last);
    }
    return added;
}

/* assumes lock */
static void
add_reg_to_registers (DonnaProviderRegister *pr,
                      struct reg            *reg,
                      gboolean               need_node,
                      DonnaNode            **node_root,
                      DonnaNode            **node,
                      GError               **error)
{
    DonnaProviderRegisterPrivate *priv = pr->priv;

    g_assert (node_root != NULL);
    g_assert (node != NULL);

    priv->registers = g_slist_prepend (priv->registers, reg);

    /* default/clipboard always exist */
    if (reg->name == reg_default || reg->name == reg_clipboard)
        return;

    /* since we created a new reg, check if there's a node for our root */
    if ((*node_root = get_node_for (pr, "/")) || need_node)
    {
        /* yep, means we need a node for the new reg (to emit node-new-child) */
        *node = new_node_for_reg ((DonnaProvider *) pr, reg, error);
        if (G_UNLIKELY (!*node))
        {
            if (*node_root)
            {
                g_object_unref (*node_root);
                *node_root = NULL;
            }
        }
        else
        {
            DonnaProviderBaseClass *klass;
            DonnaProviderBase *_provider = (DonnaProviderBase *) pr;

            klass = DONNA_PROVIDER_BASE_GET_CLASS (_provider);
            klass->lock_nodes (_provider);
            klass->add_node_to_cache (_provider, *node);
            klass->unlock_nodes (_provider);
        }
    }
}

static gboolean
register_set (DonnaProviderRegister *pr,
              const gchar           *name,
              DonnaRegisterType      type,
              GPtrArray             *nodes,
              DonnaNode            **reg_node,
              GError               **error)
{
    DonnaProviderRegisterPrivate *priv = pr->priv;
    DonnaProvider *pfs;
    DonnaNode *node_root = NULL;
    DonnaNode *node = NULL;
    GPtrArray *arr;
    struct reg *reg;
    gboolean is_clipboard;
    guint reg_type = (guint) -1;
    guint i;

    is_clipboard = *name == *reg_clipboard;
    if (is_clipboard)
        pfs = donna_app_get_provider (((DonnaProviderBase *) pr)->app, "fs");

    g_rec_mutex_lock (&priv->rec_mutex);
    reg = get_register (priv->registers, name);

    if (!reg)
    {
        reg = new_register (name, type);
        add_reg_to_registers (pr, reg, FALSE, &node_root, &node, NULL);
    }
    else
    {
        if (reg->type != type)
            reg_type = type;
        reg->type = type;
        g_hash_table_remove_all (reg->hashtable);
        g_list_free (reg->list);
        reg->list = reg->last = NULL;
    }

    arr = g_ptr_array_sized_new (nodes->len);
    for (i = 0; i < nodes->len; ++i)
        if (!is_clipboard || donna_node_peek_provider (nodes->pdata[i]) == pfs)
            if (add_node_to_reg (reg, nodes->pdata[i], is_clipboard))
                g_ptr_array_add (arr, nodes->pdata[i]);

    g_rec_mutex_unlock (&priv->rec_mutex);

    if (is_clipboard)
    {
        take_clipboard_ownership (pr, FALSE);
        g_object_unref (pfs);
    }

    if (node_root)
    {
        donna_provider_node_new_child ((DonnaProvider *) pr, node_root, node);
        g_object_unref (node_root);
    }
    else if ((node = get_node_for (pr, name)))
    {
        if (reg_type != (guint) -1)
            update_node_type ((DonnaProvider *) pr, node, reg_type);
        donna_provider_node_children ((DonnaProvider *) pr, node,
                DONNA_NODE_ITEM | DONNA_NODE_CONTAINER, arr);
    }
    g_info ("Set %d nodes in register '%s'", arr->len, name);
    g_ptr_array_unref (arr);

    update_special_nodes (pr, name, nodes->len > 0);

    if (reg_node)
        *reg_node = node;
    else if (node)
        g_object_unref (node);

    return TRUE;
}

static gboolean
register_add_nodes (DonnaProviderRegister   *pr,
                    const gchar             *name,
                    GPtrArray               *nodes,
                    DonnaNode              **reg_node,
                    GError                 **error)
{
    DonnaProviderRegisterPrivate *priv = pr->priv;
    DonnaApp *app = ((DonnaProviderBase *) pr)->app;
    DonnaProvider *pfs;
    DonnaNode *node_root = NULL;
    DonnaNode *node = NULL;
    GPtrArray *arr;
    struct reg *reg;
    gboolean is_clipboard;
    gboolean has_items;
    guint i;

    is_clipboard = *name == *reg_clipboard;
    if (is_clipboard)
        pfs = donna_app_get_provider (((DonnaProviderBase *) pr)->app, "fs");

    g_rec_mutex_lock (&priv->rec_mutex);
    reg = get_register (priv->registers, name);

    if (!reg)
    {
        reg = new_register (name, DONNA_REGISTER_UNKNOWN);
        if (is_clipboard)
        {
            GString *str = NULL;

            if (!get_from_clipboard (app, &reg->hashtable, &reg->list, &reg->last,
                        &reg->type, &str, error))
            {
                g_rec_mutex_unlock (&priv->rec_mutex);
                g_prefix_error (error, "Couldn't append files to CLIPBOARD: ");
                free_register (reg);
                return FALSE;
            }
            else if (str)
            {
                g_warning ("Failed to get some files from CLIPBOARD: %s", str->str);
                g_string_free (str, TRUE);
            }
            take_clipboard_ownership (pr, FALSE);
        }
        add_reg_to_registers (pr, reg, FALSE, &node_root, &node, NULL);
    }

    has_items = g_hash_table_size (reg->hashtable) > 0;
    arr = g_ptr_array_sized_new (nodes->len);
    for (i = 0; i < nodes->len; ++i)
        if (!is_clipboard || donna_node_peek_provider (nodes->pdata[i]) == pfs)
            if (add_node_to_reg (reg, nodes->pdata[i], is_clipboard))
                g_ptr_array_add (arr, nodes->pdata[i]);

    g_rec_mutex_unlock (&priv->rec_mutex);

    if (is_clipboard)
        g_object_unref (pfs);

    if (node_root)
    {
        donna_provider_node_new_child ((DonnaProvider *) pr, node_root, node);
        g_object_unref (node_root);
    }
    else if ((node = get_node_for (pr, name)))
    {
        for (i = 0; i < arr->len; ++i)
            donna_provider_node_new_child ((DonnaProvider *) pr, node, arr->pdata[i]);
    }
    g_info ("Added %d nodes to register '%s'", arr->len, name);
    g_ptr_array_unref (arr);

    update_special_nodes (pr, name, has_items || nodes->len > 0);

    if (reg_node)
        *reg_node = node;
    else if (node)
        g_object_unref (node);

    return TRUE;
}

static gboolean
register_set_type (DonnaProviderRegister    *pr,
                   const gchar              *name,
                   DonnaRegisterType         type,
                   GError                  **error)
{
    DonnaProviderRegisterPrivate *priv = pr->priv;
    DonnaApp *app = ((DonnaProviderBase *) pr)->app;
    struct reg *reg;
    gboolean is_clipboard;
    DonnaNode *node_root = NULL;
    DonnaNode *node;

    is_clipboard = *name == *reg_clipboard;
    g_rec_mutex_lock (&priv->rec_mutex);
    reg = get_register (priv->registers, name);
    if (!reg)
    {
        /* reg_default must always exists */
        if (*name == *reg_default)
        {
            reg = new_register (name, DONNA_REGISTER_UNKNOWN);
            add_reg_to_registers (pr, reg, FALSE, &node_root, &node, NULL);
        }
        /* same with reg_clipboard, except we import */
        else if (is_clipboard)
        {
            GString *str = NULL;

            reg = new_register (reg_clipboard, DONNA_REGISTER_UNKNOWN);
            if (!get_from_clipboard (app, &reg->hashtable, &reg->list, &reg->last,
                        &reg->type, &str, error))
            {
                g_rec_mutex_unlock (&priv->rec_mutex);
                g_prefix_error (error, "Couldn't set register type of CLIPBOARD: ");
                free_register (reg);
                return FALSE;
            }
            else if (str)
            {
                g_warning ("Failed to get some files from CLIPBOARD: %s", str->str);
                g_string_free (str, TRUE);
            }
            take_clipboard_ownership (pr, FALSE);
            add_reg_to_registers (pr, reg, FALSE, &node_root, &node, NULL);
        }
        else
        {
            g_rec_mutex_unlock (&priv->rec_mutex);
            g_set_error (error, DONNA_PROVIDER_ERROR,
                    DONNA_PROVIDER_ERROR_LOCATION_NOT_FOUND,
                    "Cannot set type of register '%s', it doesn't exist.", name);
            return FALSE;
        }
    }

    reg->type = type;
    g_rec_mutex_unlock (&priv->rec_mutex);

    if (node_root)
    {
        donna_provider_node_new_child ((DonnaProvider *) pr, node_root, node);
        g_object_unref (node_root);
        g_object_unref (node);
    }
    else if ((node = get_node_for (pr, name)))
    {
        update_node_type ((DonnaProvider *) pr, node, type);
        g_object_unref (node);
    }

    return TRUE;
}

static gboolean
register_get_nodes (DonnaProviderRegister   *pr,
                    const gchar             *name,
                    DonnaDropRegister        drop,
                    DonnaRegisterType       *type,
                    GPtrArray              **nodes,
                    GError                 **error)
{
    DonnaProviderRegisterPrivate *priv = pr->priv;
    DonnaApp *app = ((DonnaProviderBase *) pr)->app;
    DonnaProvider *pfs = NULL;
    gboolean is_clipboard;
    struct reg *reg = NULL;
    DonnaRegisterType reg_type;
    GHashTable *hashtable;
    GList *list = NULL;
    GList *l = NULL;
    GString *str = NULL;
    gboolean do_drop;

    is_clipboard = *name == *reg_clipboard;
    g_rec_mutex_lock (&priv->rec_mutex);
    reg = get_register (priv->registers, name);
    if (!reg)
    {
        /* reg_default must always exists */
        if (*name == *reg_default)
        {
            g_rec_mutex_unlock (&priv->rec_mutex);
            if (type)
                *type = DONNA_REGISTER_UNKNOWN;
            if (nodes)
                *nodes = g_ptr_array_new ();
            return TRUE;
        }
        /* reg_clipboard as well, except we need to try and import from
         * CLIPBOARD since there might be something in there */
        if (is_clipboard)
            hashtable = g_hash_table_new_full (g_str_hash, g_str_equal, g_free, NULL);
        if (!is_clipboard
                || !get_from_clipboard (app, &hashtable, &list, &l,
                    &reg_type, &str, error))
        {
            if (is_clipboard)
            {
                g_hash_table_unref (hashtable);
                g_list_free (list);
            }
            g_rec_mutex_unlock (&priv->rec_mutex);
            if (*error)
                g_prefix_error (error, "Cannot get nodes from register '%s': ",
                        name);
            else
                g_set_error (error, DONNA_PROVIDER_ERROR,
                        DONNA_PROVIDER_ERROR_LOCATION_NOT_FOUND,
                        "Cannot get nodes from register '%s', it doesn't exist.",
                        name);
            return FALSE;
        }
        if (str)
        {
            g_warning ("Failed to get some files from CLIPBOARD: %s", str->str);
            g_string_free (str, TRUE);
        }
    }
    else
    {
        hashtable = reg->hashtable;
        list = reg->list;
        reg_type = reg->type;
    }

    if (type)
        *type = reg_type;

    if (!nodes)
    {
        g_rec_mutex_unlock (&priv->rec_mutex);
        if (!reg)
        {
            g_hash_table_unref (hashtable);
            g_list_free (list);
        }
        return TRUE;
    }

    if (is_clipboard)
        pfs = donna_app_get_provider (((DonnaProviderBase *) pr)->app, "fs");

    *nodes = g_ptr_array_new_full (g_hash_table_size (hashtable), g_object_unref);
    for (l = list; l; l = l->next)
    {
        GError *err = NULL;
        DonnaNode *node;

        if (pfs)
            node = donna_provider_get_node (pfs, (gchar *) l->data, &err);
        else
            node = donna_app_get_node (((DonnaProviderBase *) pr)->app,
                    (gchar *) l->data, FALSE, &err);

        if (!node)
        {
            if (!str)
                str = g_string_new (NULL);

            g_string_append_printf (str, "\n- Failed to get node for '%s': %s",
                    (gchar *) l->data,
                    (err) ? err->message : "(no error message)");
            g_clear_error (&err);
            continue;
        }

        g_ptr_array_add (*nodes, node);
    }

    if (str)
    {
        g_set_error (error, DONNA_PROVIDER_REGISTER_ERROR,
                DONNA_PROVIDER_REGISTER_ERROR_OTHER,
                "Not all nodes from register '%s' could be loaded:\n%s",
                name, str->str);
        g_string_free (str, TRUE);
    }

    do_drop = (drop == DONNA_DROP_REGISTER_ALWAYS
            || (drop == DONNA_DROP_REGISTER_ON_CUT && reg_type == DONNA_REGISTER_CUT));

    if (reg)
    {
        DonnaNode *node = NULL;

        if (do_drop)
            drop_register (pr, name, &node);
        g_rec_mutex_unlock (&priv->rec_mutex);
        if (node)
            emit_drop (pr, node, *name == *reg_default || is_clipboard);
    }
    else
    {
        g_rec_mutex_unlock (&priv->rec_mutex);
        g_hash_table_unref (hashtable);
        g_list_free (list);

        if (do_drop)
            take_clipboard_ownership (pr, TRUE);
    }

    if (do_drop)
        update_special_nodes (pr, name, FALSE);

    return TRUE;
}

static inline gchar *
get_filename (const gchar *file)
{
    if (!g_get_filename_charsets (NULL))
    {
        gchar *s;
        s = g_filename_from_utf8 (file, -1, NULL, NULL, NULL);
        return (s) ? s : (gchar *) file;
    }
    else
        return (gchar *) file;
}

static void
emit_node_children_from_arr (DonnaProviderRegister  *pr,
                             DonnaNode              *parent,
                             GPtrArray              *arr)
{
    DonnaApp *app = ((DonnaProviderBase *) pr)->app;
    GPtrArray *children;
    guint i;

    children = g_ptr_array_new_full (arr->len, g_object_unref);
    for (i = 0; i < arr->len; ++i)
    {
        GError *err = NULL;
        DonnaNode *node;

        node = donna_app_get_node (app, arr->pdata[i], FALSE, &err);
        if (!node)
        {
            gchar *fl = donna_node_get_full_location (parent);
            g_warning ("Provider 'register': Failed to get node for '%s' "
                    "(children of '%s'): %s",
                    (gchar *) arr->pdata[i],
                    fl,
                    (err) ? err->message : "(no error message)");
            g_free (fl);
            g_clear_error (&err);
            continue;
        }

        g_ptr_array_add (children, node);
    }
    donna_provider_node_children ((DonnaProvider *) pr, parent,
            DONNA_NODE_ITEM | DONNA_NODE_CONTAINER, children);
    g_ptr_array_unref (children);
}

struct emit_load
{
    DonnaNode   *node_root;
    DonnaNode   *node;
    guint        reg_type;
    GPtrArray   *arr;
};

static gboolean
register_import (DonnaProviderRegister  *pr,
                 const gchar            *name,
                 gchar                  *data,
                 DonnaRegisterFile       file_type,
                 struct emit_load       *el,
                 GError                **error)
{
    DonnaProviderRegisterPrivate *priv = pr->priv;
    DonnaNode *node_root = NULL;
    DonnaNode *node = NULL;
    GPtrArray *arr;
    struct reg *new_reg;
    struct reg *reg;
    guint reg_type = (guint) -1;
    gboolean is_clipboard;
    gchar *e;

    is_clipboard = *name == *reg_clipboard;
    new_reg = new_register (name, DONNA_REGISTER_UNKNOWN);

    if (streqn (data, "cut\n", 4))
    {
        new_reg->type = DONNA_REGISTER_CUT;
        data += 4;
    }
    else if (streqn (data, "copy\n", 5))
    {
        new_reg->type = DONNA_REGISTER_COPY;
        data += 5;
    }
    else
    {
        g_set_error (error, DONNA_PROVIDER_REGISTER_ERROR,
                DONNA_PROVIDER_REGISTER_ERROR_INVALID_FORMAT,
                "Failed to load register '%s': invalid file format",
                name);
        free_register (new_reg);
        return FALSE;
    }

    arr = g_ptr_array_new_with_free_func (g_free);
    while ((e = strchr (data, '\n')))
    {
        gchar *new = NULL;
        gchar *fl  = NULL;
        *e = '\0';

        if (file_type == DONNA_REGISTER_FILE_NODES)
        {
            fl = g_strdup (data);
            if (!is_clipboard)
                new = g_strdup (data);
            else if (streqn (data, "fs:", 3))
                new = g_strdup (data + 3);
        }
        else if (file_type == DONNA_REGISTER_FILE_FILE)
        {
            fl = g_strdup_printf ("fs:%s", data);
            if (!is_clipboard)
                new =  g_strdup_printf ("fs:%s", data);
            else
                new = g_strdup (data);
        }
        else /* DONNA_REGISTER_FILE_URIS */
        {
            gchar *f = g_filename_from_uri (data, NULL, NULL);
            if (f)
            {
                fl = g_strdup_printf ("fs:%s", f);
                if (!is_clipboard)
                {
                    new = g_strdup_printf ("fs:%s", f);
                    g_free (f);
                }
                else
                    new = f;
            }
        }

        if (new)
        {
            if (new_reg->list)
            {
                /* append to last item directly */
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wunused-result"
                g_list_append (new_reg->last, new);
#pragma GCC diagnostic pop
                new_reg->last = new_reg->last->next;
            }
            else
                new_reg->list = new_reg->last = g_list_append (new_reg->list, new);
            g_hash_table_insert (new_reg->hashtable, new, new_reg->last);
        }
        if (fl)
            g_ptr_array_add (arr, fl);

        data = e + 1;
    }


    if (!el)
        g_rec_mutex_lock (&priv->rec_mutex);
    reg = get_register (priv->registers, name);
    if (!reg)
        add_reg_to_registers (pr, new_reg, FALSE, &node_root, &node, NULL);
    else
    {
        if (new_reg->type != reg->type)
            reg_type = new_reg->type;
        priv->registers = g_slist_remove (priv->registers, reg);
        free_register (reg);
        priv->registers = g_slist_prepend (priv->registers, new_reg);
    }

    if (!el)
    {
        g_rec_mutex_unlock (&priv->rec_mutex);

        if (node_root)
        {
            donna_provider_node_new_child ((DonnaProvider *) pr, node_root, node);
            g_object_unref (node_root);
            g_object_unref (node);
        }
        else if ((node = get_node_for (pr, name)))
        {
            if (reg_type != (guint) -1)
                update_node_type ((DonnaProvider *) pr, node, reg_type);
            emit_node_children_from_arr (pr, node, arr);
            g_object_unref (node);
        }
        update_special_nodes (pr, name, arr->len > 0);
        g_ptr_array_unref (arr);
    }
    else
    {
        el->node_root = node_root;
        if (node_root)
            el->node = node;
        else
            el->node = get_node_for (pr, name);
        el->reg_type = reg_type;
        el->arr = arr;
    }

    if (is_clipboard)
        take_clipboard_ownership (pr, FALSE);

    return TRUE;
}

static gboolean
register_load (DonnaProviderRegister    *pr,
               const gchar              *name,
               const gchar              *file,
               DonnaRegisterFile         file_type,
               struct emit_load         *el,
               GError                  **error)
{
    gchar *data;
    gchar *filename;
    gchar *s = NULL;

    if (*file != '/')
    {
        s = donna_app_get_current_dirname (((DonnaProviderBase *) pr)->app);
        filename = g_strdup_printf ("%s/%s", s, file);
        g_free (s);
        s = filename;
        file = (const gchar *) s;
    }

    filename = get_filename (file);
    if (!g_file_get_contents (filename, &data, NULL, error))
    {
        g_prefix_error (error, "Failed to load register '%s' from '%s': ",
                name, file);
        if (filename != file)
            g_free (filename);
        g_free (s);
        return FALSE;
    }
    if (filename != file)
        g_free (filename);

    if (!register_import (pr, name, data, file_type, el, error))
    {
        g_free (data);
        g_free (s);
        return FALSE;
    }

    g_free (data);
    g_free (s);
    return TRUE;
}

/* assume lock */
static gboolean
register_export (DonnaProviderRegister  *pr,
                 const gchar            *name,
                 DonnaRegisterFile       file_type,
                 GString                *str,
                 GError                **error)
{
    DonnaProviderRegisterPrivate *priv = pr->priv;
    DonnaApp *app = ((DonnaProviderBase *) pr)->app;
    struct reg *reg = NULL;
    gboolean is_clipboard;
    DonnaRegisterType reg_type;
    GHashTable *hashtable;
    GList *list;
    GList *l;
    GString *str_err = NULL;

    is_clipboard = *name == *reg_clipboard;

    reg = get_register (priv->registers, name);
    if (!reg)
    {
        /* reg_default must always exists */
        if (*name == *reg_default)
        {
            g_string_append (str, "copy\n");
            return TRUE;
        }
        /* same with reg_clipboard, but we import its content */
        else if (is_clipboard)
            hashtable = g_hash_table_new_full (g_str_hash, g_str_equal, g_free, NULL);
        if (!is_clipboard
                || !get_from_clipboard (app, &hashtable, &list, &l,
                    &reg_type, &str_err, error))
        {
            if (is_clipboard)
                g_hash_table_unref (hashtable);
            if (*error)
                g_prefix_error (error, "Cannot save register '%s': ", name);
            else
                g_set_error (error, DONNA_PROVIDER_ERROR,
                        DONNA_PROVIDER_ERROR_LOCATION_NOT_FOUND,
                        "Cannot save register '%s', it doesn't exist.", name);
            return FALSE;
        }
        if (str_err)
        {
            g_warning ("Failed to get some files from CLIPBOARD: %s", str_err->str);
            g_string_free (str_err, TRUE);
        }
    }
    else
    {
        hashtable = reg->hashtable;
        list = reg->list;
        reg_type = reg->type;
    }

    g_string_append (str, (reg_type == DONNA_REGISTER_CUT) ? "cut\n" : "copy\n");
    for (l = list; l; l = l->next)
    {
        if (reg)
        {
            if (file_type == DONNA_REGISTER_FILE_NODES)
            {
                g_string_append (str, (gchar *) l->data);
                g_string_append_c (str, '\n');
            }
            /* other file_types require nodes to be in fs */
            else if (streqn ((gchar *) l->data, "fs:", 3))
            {
                if (file_type == DONNA_REGISTER_FILE_FILE)
                    g_string_append (str, (gchar *) l->data + 3);
                else /* DONNA_REGISTER_FILE_URIS */
                {
                    gchar *s;

                    s = g_filename_to_uri ((gchar *) l->data + 3, NULL, NULL);
                    if (!s)
                        continue;
                    g_string_append (str, s);
                    g_free (s);
                }
                g_string_append_c (str, '\n');
            }
        }
        else
        {
            if (file_type == DONNA_REGISTER_FILE_NODES)
            {
                g_string_append (str, "fs:");
                g_string_append (str, (gchar *) l->data);
            }
            else if (file_type == DONNA_REGISTER_FILE_FILE)
                g_string_append (str, (gchar *) l->data);
            else /* DONNA_REGISTER_FILE_URIS */
            {
                gchar *s;

                s = g_filename_to_uri ((gchar *) l->data, NULL, NULL);
                if (!s)
                    continue;
                g_string_append (str, s);
                g_free (s);
            }
            g_string_append_c (str, '\n');
        }
    }

    if (!reg)
    {
        g_hash_table_unref (hashtable);
        g_list_free (list);
    }

    return TRUE;
}

static gboolean
register_save (DonnaProviderRegister    *pr,
               const gchar              *name,
               const gchar              *file,
               DonnaRegisterFile         file_type,
               GError                  **error)
{
    DonnaProviderRegisterPrivate *priv = pr->priv;
    GString *str;
    gchar *filename;
    gchar *s = NULL;

    str = g_string_new (NULL);
    g_rec_mutex_lock (&priv->rec_mutex);
    if (!register_export (pr, name, file_type, str, error))
    {
        g_rec_mutex_unlock (&priv->rec_mutex);
        g_string_free (str, TRUE);
        return FALSE;
    }
    g_rec_mutex_unlock (&priv->rec_mutex);

    if (*file != '/')
    {
        s = donna_app_get_current_dirname (((DonnaProviderBase *) pr)->app);
        filename = g_strdup_printf ("%s/%s", s, file);
        g_free (s);
        s = filename;
        file = (const gchar *) s;
    }

    filename = get_filename (file);
    if (!g_file_set_contents (filename, str->str, (gssize) str->len, error))
    {
        g_prefix_error (error, "Failed to save register '%s' to '%s': ",
                name, file);
        g_string_free (str, TRUE);
        if (filename != file)
            g_free (filename);
        g_free (s);
        return FALSE;
    }
    g_string_free (str, TRUE);
    if (filename != file)
        g_free (filename);

    g_free (s);
    return TRUE;
}


/* DonnaProvider */

static DonnaProviderFlags
provider_register_get_flags (DonnaProvider *provider)
{
    g_return_val_if_fail (DONNA_IS_PROVIDER_REGISTER (provider),
            DONNA_PROVIDER_FLAG_INVALID);
    return DONNA_PROVIDER_FLAG_FLAT;
}

static const gchar *
provider_register_get_domain (DonnaProvider *provider)
{
    g_return_val_if_fail (DONNA_IS_PROVIDER_REGISTER (provider), NULL);
    return "register";
}

/* DonnaProviderBase */

static DonnaNode *
new_node_for_reg (DonnaProvider *provider, struct reg *reg, GError **error)
{
    DonnaNode *node;
    GValue v = G_VALUE_INIT;
    gchar *name;

    if (reg->name == reg_default)
        name = (gchar *) "Default register ('_')";
    else if (reg->name == reg_clipboard)
        name = (gchar *) "System clipboard ('+')";
    else
        name = g_strdup_printf ("Register '%s'", reg->name);

    node = donna_node_new (provider, reg->name, DONNA_NODE_CONTAINER, NULL,
            DONNA_TASK_VISIBILITY_INTERNAL_FAST,
            NULL, (refresher_fn) gtk_true,
            NULL,
            name,
            0);

    if (reg->name != reg_default && reg->name != reg_clipboard)
        g_free (name);

    if (G_UNLIKELY (!node))
    {
        g_set_error (error, DONNA_PROVIDER_ERROR, DONNA_PROVIDER_ERROR_OTHER,
                "Provider 'register': Unable to create a new node");
        return NULL;
    }

    g_value_init (&v, G_TYPE_UINT);
    g_value_set_uint (&v, reg->type);
    if (G_UNLIKELY (!donna_node_add_property (node, "register-type",
                    G_TYPE_UINT, &v,
                    DONNA_TASK_VISIBILITY_INTERNAL_FAST,
                    NULL, (refresher_fn) gtk_true,
                    NULL,
                    NULL, NULL,
                    error)))
    {
        g_prefix_error (error, "Provider 'register': Cannot create new node, "
                "failed to add property 'register-type': ");
        g_value_unset (&v);
        g_object_unref (node);
        return NULL;
    }

    return node;
}

static inline void
update_node_type (DonnaProvider *provider, DonnaNode *node, guint type)
{
    GValue v = G_VALUE_INIT;

    g_value_init (&v, G_TYPE_UINT);
    g_value_set_uint (&v, type);
    donna_node_set_property_value (node, "register-type", &v);
    g_value_unset (&v);
}

static DonnaNode *
new_action_node (DonnaProviderRegister  *pr,
                 const gchar            *location,
                 const gchar            *name,
                 const gchar            *action,
                 GError                **error)
{
    DonnaProviderRegisterPrivate *priv = pr->priv;
    DonnaNode *node;
    struct reg *reg;
    GValue v = G_VALUE_INIT;
    gchar buf[32], *b = buf;
    const gchar *lbl;
    const gchar *icon;
    enum {
        SENSITIVE_IF_REG = 0,   /* can be empty, that is default/clipboard */
        SENSITIVE_YES,
        SENSITIVE_NO,
        SENSITIVE_IF_REG_NOT_EMPTY
    } sensitive;

    if (streq (action, "cut"))
    {
        if (*name == *reg_default)
            lbl = "Cut to register";
        else if (*name == *reg_clipboard)
            lbl = "Cut";
        else
            lbl = "Cut to '%s'";
        icon = "edit-cut";
        sensitive = SENSITIVE_YES;
    }
    else if (streq (action, "copy"))
    {
        if (*name == *reg_default)
            lbl = "Copy to register";
        else if (*name == *reg_clipboard)
            lbl = "Copy";
        else
            lbl = "Copy to '%s'";
        icon = "edit-copy";
        sensitive = SENSITIVE_YES;
    }
    else if (streq (action, "append"))
    {
        if (*name == *reg_default)
            lbl = "Append to register";
        else if (*name == *reg_clipboard)
            lbl = "Append";
        else
            lbl = "Append to '%s'";
        icon = "edit-copy";
        sensitive = SENSITIVE_YES;
    }
    else if (streq (action, "paste"))
    {
        if (*name == *reg_default)
            lbl = "Paste from register";
        else if (*name == *reg_clipboard)
            lbl = "Paste";
        else
            lbl = "Paste from '%s'";
        icon = "edit-paste";
        sensitive = SENSITIVE_IF_REG_NOT_EMPTY;
    }
    else if (streq (action, "paste_copy"))
    {
        if (*name == *reg_default)
            lbl = "Paste (Copy) from register";
        else if (*name == *reg_clipboard)
            lbl = "Paste (Copy)";
        else
            lbl = "Paste (Copy) from '%s'";
        icon = "edit-paste";
        sensitive = SENSITIVE_IF_REG_NOT_EMPTY;
    }
    else if (streq (action, "paste_move"))
    {
        if (*name == *reg_default)
            lbl = "Paste (Move) from register";
        else if (*name == *reg_clipboard)
            lbl = "Paste (Move)";
        else
            lbl = "Paste (Move) from '%s'";
        icon = "edit-paste";
        sensitive = SENSITIVE_IF_REG_NOT_EMPTY;
    }
    else if (streq (action, "paste_new_folder"))
    {
        if (*name == *reg_default)
            lbl = "Paste Into New Folder from register";
        else if (*name == *reg_clipboard)
            lbl = "Paste Into New Folder";
        else
            lbl = "Paste Into New Folder from '%s'";
        icon = "edit-paste";
        sensitive = SENSITIVE_IF_REG_NOT_EMPTY;
    }
    else
    {
        g_set_error (error, DONNA_PROVIDER_ERROR,
                DONNA_PROVIDER_ERROR_LOCATION_NOT_FOUND,
                "Provider 'register': Invalid action '%s' for register '%s'; "
                "Supported actions are: 'cut', 'copy', 'append', 'paste', "
                "'paste_copy', 'paste_move', and 'paste_new_folder'",
                action, name);
        return NULL;
    }

#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wformat-nonliteral"
    if (snprintf (buf, 32, lbl, name) >= 32)
        b = g_strdup_printf (lbl, name);
#pragma GCC diagnostic pop

    if (sensitive == SENSITIVE_IF_REG || sensitive == SENSITIVE_IF_REG_NOT_EMPTY)
    {
        if (sensitive == SENSITIVE_IF_REG
                && (*name == *reg_default || *name == *reg_clipboard))
            sensitive = SENSITIVE_YES;
        else
        {
            g_rec_mutex_lock (&priv->rec_mutex);
            reg = get_register (priv->registers, name);
            if (reg && (sensitive == SENSITIVE_IF_REG
                        || g_hash_table_size (reg->hashtable) > 0))
                sensitive = SENSITIVE_YES;
            else
                sensitive = SENSITIVE_NO;
            g_rec_mutex_unlock (&priv->rec_mutex);
        }
    }

    node = donna_node_new ((DonnaProvider *) pr, location, DONNA_NODE_ITEM,
            NULL,
            DONNA_TASK_VISIBILITY_INTERNAL_FAST,
            NULL, (refresher_fn) gtk_true,
            NULL,
            b,
            DONNA_NODE_ICON_EXISTS);
    if (G_UNLIKELY (!node))
    {
        g_set_error (error, DONNA_PROVIDER_ERROR,
                DONNA_PROVIDER_ERROR_OTHER,
                "Provider 'register': Unable to create a new node");
        if (b != buf)
            g_free (b);
        return NULL;
    }

    g_value_init (&v, G_TYPE_ICON);
    g_value_take_object (&v, g_themed_icon_new (icon));
    donna_node_set_property_value (node, "icon", &v);
    g_value_unset (&v);

    g_value_init (&v, G_TYPE_BOOLEAN);
    g_value_set_boolean (&v, sensitive == SENSITIVE_YES);
    if (G_UNLIKELY (!donna_node_add_property (node, "menu-is-sensitive",
                    G_TYPE_BOOLEAN, &v,
                    DONNA_TASK_VISIBILITY_INTERNAL_FAST,
                    NULL, (refresher_fn) gtk_true,
                    NULL,
                    NULL, NULL,
                    error)))
    {
        g_prefix_error (error, "Provider 'register': Cannot create new node, "
                "failed to add property 'menu-is-sensitive': ");
        g_value_unset (&v);
        g_object_unref (node);
        if (b != buf)
            g_free (b);
        return NULL;
    }
    g_value_unset (&v);

    if (b != buf)
        g_free (b);
    return node;
}

static DonnaTaskState
provider_register_new_node (DonnaProviderBase  *_provider,
                            DonnaTask          *task,
                            const gchar        *location)
{
    DonnaProviderRegisterPrivate *priv = ((DonnaProviderRegister *) _provider)->priv;
    DonnaProviderBaseClass *klass;
    GError *err = NULL;
    struct reg *reg = NULL;
    struct reg r = { .type = DONNA_REGISTER_UNKNOWN };
    DonnaNode *node;
    DonnaNode *n;
    GValue *value;
    gchar *s;

    if (streq (location, "/"))
    {
        node = donna_node_new ((DonnaProvider *) _provider, location,
                DONNA_NODE_CONTAINER,
                NULL,
                DONNA_TASK_VISIBILITY_INTERNAL_FAST,
                NULL, (refresher_fn) gtk_true,
                NULL,
                "Registers",
                0);
        if (G_UNLIKELY (!node))
        {
            donna_task_set_error (task, DONNA_PROVIDER_ERROR,
                    DONNA_PROVIDER_ERROR_OTHER,
                    "Provider 'register': Unable to create a new node");
            return DONNA_TASK_FAILED;
        }

        goto cache_and_return;
    }

    s = strchr (location, '/');
    if (s)
    {
        gchar buf[32];
        gchar *name = buf;

        if (s - location < 32)
            sprintf (buf, "%.*s", (gint) (s - location), location);
        else
            name = g_strdup_printf ("%.*s", (gint) (s - location), location);
        ++s;

        if (!is_valid_register_name ((const gchar **) &name, &err))
        {
            g_prefix_error (&err, "Provider 'register': Cannot create node for '%s': ",
                    location);
            donna_task_take_error (task, err);
            if (name != buf)
                g_free (name);
            return DONNA_TASK_FAILED;
        }

        node = new_action_node ((DonnaProviderRegister *) _provider, location,
                name, s, &err);
        if (!node)
        {
            donna_task_take_error (task, err);
            if (name != buf && name != reg_default)
                g_free (name);
            return DONNA_TASK_FAILED;
        }

        if (name != buf && name != reg_default)
            g_free (name);
        goto cache_and_return;
    }

    if (!is_valid_register_name (&location, &err))
    {
        donna_task_take_error (task, err);
        return DONNA_TASK_FAILED;
    }

    g_rec_mutex_lock (&priv->rec_mutex);
    reg = get_register (priv->registers, location);
    if (!reg)
    {
        /* reg_default & reg_clipboard must always exists */
        if (*location == *reg_default)
        {
            r.name = (gchar *) reg_default;
            reg = &r;
        }
        else if (*location == *reg_clipboard)
        {
            /* we don't need to import anything just yet, it'll happen when an
             * actual register command/operation happens */
            r.name = (gchar *) reg_clipboard;
            reg = &r;
        }
        else
        {
            g_rec_mutex_unlock (&priv->rec_mutex);
            donna_task_set_error (task, DONNA_PROVIDER_ERROR,
                    DONNA_PROVIDER_ERROR_LOCATION_NOT_FOUND,
                    "Register '%s' doesn't exist", location);
            return DONNA_TASK_FAILED;
        }
    }

    node = new_node_for_reg ((DonnaProvider *) _provider, reg, &err);
    if (G_UNLIKELY (!node))
    {
        g_rec_mutex_unlock (&priv->rec_mutex);
        donna_task_take_error (task, err);
        return DONNA_TASK_FAILED;
    }

cache_and_return:
    klass = DONNA_PROVIDER_BASE_GET_CLASS (_provider);
    klass->lock_nodes (_provider);
    n = klass->get_cached_node (_provider, location);
    if (n)
    {
        /* already added while we were busy */
        g_object_unref (node);
        node = n;
    }
    else
        klass->add_node_to_cache (_provider, node);
    klass->unlock_nodes (_provider);
    if (reg)
        g_rec_mutex_unlock (&priv->rec_mutex);

    value = donna_task_grab_return_value (task);
    g_value_init (value, DONNA_TYPE_NODE);
    g_value_take_object (value, node);
    donna_task_release_return_value (task);

    return DONNA_TASK_DONE;
}

static DonnaTaskState
provider_register_has_children (DonnaProviderBase  *_provider,
                                DonnaTask          *task,
                                DonnaNode          *node,
                                DonnaNodeType       node_types)
{
    DonnaProviderRegisterPrivate *priv = ((DonnaProviderRegister *) _provider)->priv;
    struct reg *reg;
    gchar *location;
    GValue *value;

    location = donna_node_get_location (node);
    value = donna_task_grab_return_value (task);
    g_value_init (value, G_TYPE_BOOLEAN);
    if (streq (location, "/"))
        /* because default & clipboard always exist */
        g_value_set_boolean (value, TRUE);
    else
    {
        g_rec_mutex_lock (&priv->rec_mutex);
        reg = get_register (priv->registers, location);
        g_value_set_boolean (value, reg && g_hash_table_size (reg->hashtable) > 0);
        g_rec_mutex_unlock (&priv->rec_mutex);
    }
    donna_task_release_return_value (task);
    g_free (location);

    return DONNA_TASK_DONE;
}

static DonnaTaskState
provider_register_get_children (DonnaProviderBase  *_provider,
                                DonnaTask          *task,
                                DonnaNode          *node,
                                DonnaNodeType       node_types)
{
    DonnaProviderRegisterPrivate *priv = ((DonnaProviderRegister *) _provider)->priv;
    GError *err = NULL;
    GValue *value;
    gchar *name;
    GPtrArray *nodes;

    name = donna_node_get_location (node);

    if (streq (name, "/"))
    {
        DonnaProviderBaseClass *klass;
        DonnaNode *n;
        GSList *l;
        enum {
            HAS_DEFAULT     = (1 << 0),
            HAS_CLIPBOARD   = (1 << 1),
        } has = 0;

        if (!(node_types & DONNA_NODE_CONTAINER))
        {
            /* no containers == return an empty array */
            nodes = g_ptr_array_sized_new (0);
            goto done;
        }

        klass = DONNA_PROVIDER_BASE_GET_CLASS (_provider);
        nodes = g_ptr_array_new_with_free_func (g_object_unref);

        g_rec_mutex_lock (&priv->rec_mutex);
        klass->lock_nodes (_provider);
        for (l = priv->registers; l; l = l->next)
        {
            struct reg *reg = l->data;

            if (reg->name == reg_default)
                has |= HAS_DEFAULT;
            else if (reg->name == reg_clipboard)
                has |= HAS_CLIPBOARD;

            n = klass->get_cached_node (_provider, reg->name);
            if (!n)
            {
                n = new_node_for_reg ((DonnaProvider *) _provider, reg, &err);
                if (G_UNLIKELY (!n))
                {
                    klass->unlock_nodes (_provider);
                    g_rec_mutex_unlock (&priv->rec_mutex);
                    donna_task_take_error (task, err);
                    g_ptr_array_unref (nodes);
                    return DONNA_TASK_FAILED;
                }
                klass->add_node_to_cache (_provider, n);
            }
            g_ptr_array_add (nodes, n);
        }

        /* we force to have default & clipboard even when the actual registers
         * do not exist, because they should always be available */
        if (!(has & HAS_DEFAULT))
        {
            struct reg r = {
                .name = (gchar *) reg_default,
                .type = DONNA_REGISTER_UNKNOWN,
            };
            n = klass->get_cached_node (_provider, r.name);
            if (!n)
            {
                n = new_node_for_reg ((DonnaProvider *) _provider, &r, &err);
                if (G_UNLIKELY (!n))
                {
                    klass->unlock_nodes (_provider);
                    g_rec_mutex_unlock (&priv->rec_mutex);
                    donna_task_take_error (task, err);
                    g_ptr_array_unref (nodes);
                    return DONNA_TASK_FAILED;
                }
                klass->add_node_to_cache (_provider, n);
            }
            g_ptr_array_add (nodes, n);
        }
        if (!(has & HAS_CLIPBOARD))
        {
            struct reg r = {
                .name = (gchar *) reg_clipboard,
                .type = DONNA_REGISTER_UNKNOWN,
            };
            n = klass->get_cached_node (_provider, r.name);
            if (!n)
            {
                n = new_node_for_reg ((DonnaProvider *) _provider, &r, &err);
                if (G_UNLIKELY (!n))
                {
                    klass->unlock_nodes (_provider);
                    g_rec_mutex_unlock (&priv->rec_mutex);
                    donna_task_take_error (task, err);
                    g_ptr_array_unref (nodes);
                    return DONNA_TASK_FAILED;
                }
                klass->add_node_to_cache (_provider, n);
            }
            g_ptr_array_add (nodes, n);
        }

        klass->unlock_nodes (_provider);
        g_rec_mutex_unlock (&priv->rec_mutex);
        goto done;
    }

    if (!register_get_nodes ((DonnaProviderRegister *) _provider, name,
                DONNA_DROP_REGISTER_NOT, NULL, &nodes, &err))
    {
        g_free (name);
        g_prefix_error (&err, "Provider 'register': ");
        donna_task_take_error (task, err);
        return DONNA_TASK_FAILED;
    }
    g_free (name);

    /* we might have to filter out some results */
    if (node_types != (DONNA_NODE_ITEM | DONNA_NODE_CONTAINER))
    {
        guint i;

        for (i = 0; i < nodes->len; )
        {
            if (!(donna_node_get_node_type ((DonnaNode *) nodes->pdata[i])
                    & node_types))
                g_ptr_array_remove_index_fast (nodes, i);
            else
                ++i;
        }
    }

done:
    value = donna_task_grab_return_value (task);
    g_value_init (value, G_TYPE_PTR_ARRAY);
    g_value_take_boxed (value, nodes);
    donna_task_release_return_value (task);

    return DONNA_TASK_DONE;
}

static DonnaTaskState
provider_register_trigger_node (DonnaProviderBase  *_provider,
                                DonnaTask          *task,
                                DonnaNode          *node)
{
    GError *err = NULL;
    gchar *location;
    gchar *name;
    gchar *action;
    gchar *s;

    location = donna_node_get_location (node);
    s = strchr (location + 1, '/');
    if (!s)
    {
        donna_task_set_error (task, DONNA_PROVIDER_ERROR,
                DONNA_PROVIDER_ERROR_INVALID_CALL,
                "Provider 'register': Cannot trigger node 'register:%s'",
                location);
        g_free (location);
        return DONNA_TASK_FAILED;
    }

    *s = '\0';
    action = s + 1;

    /* in the off chance it needs quoting */
    if (strchr (location, ',') || strchr (location, '"'))
    {
        GString *str;

        str = g_string_new (NULL);
        donna_g_string_append_quoted (str, location, FALSE);
        name = g_string_free (str, FALSE);
    }
    else
        name = location;

    if (streq (action, "cut") || streq (action, "copy"))
        s = g_strdup_printf (
                "command:register_set (%s, %s, @tv_get_nodes (:active, :selected))",
                name, action);
    else if (streq (action, "append"))
        s = g_strdup_printf (
                "command:register_add_nodes (%s, @tv_get_nodes (:active, :selected))",
                name);
    else if (streq (action, "paste") || streq (action, "paste_copy")
            || streq (action, "paste_move") || streq (action, "paste_new_folder"))
        s = g_strdup_printf (
                "command:register_nodes_io (%s, %s, @tv_get_location (:active), %d)",
                name,
                (streq (action, "paste_copy")) ? "copy"
                 : (streq (action, "paste_move")) ? "move" : "auto",
                 (streq (action, "paste_new_folder")) ? 1 : 0);
    else
    {
        *s = '/';
        donna_task_set_error (task, DONNA_PROVIDER_ERROR,
                DONNA_PROVIDER_ERROR_INVALID_CALL,
                "Provider 'register': Cannot trigger node 'register:%s': Invalid action",
                location);
        g_free (location);
        if (name != location)
            g_free (name);
        return DONNA_TASK_FAILED;
    }

    if (!donna_app_trigger_node (_provider->app, s, FALSE, &err))
    {
        g_prefix_error (&err, "Provider 'register': Failed to trigger node 'register:%s': ",
                location);
        donna_task_take_error (task, err);
        g_free (s);
        g_free (location);
        if (name != location)
            g_free (name);
        return DONNA_TASK_FAILED;
    }

    g_free (s);
    g_free (location);
    if (name != location)
        g_free (name);
    return DONNA_TASK_DONE;
}

static gboolean
provider_register_support_io (DonnaProviderBase  *_provider,
                              DonnaIoType         type,
                              gboolean            is_source,
                              GPtrArray          *sources,
                              DonnaNode          *dest,
                              const gchar        *new_name,
                              GError            **error)
{
    if (is_source)
    {
        g_set_error (error, DONNA_PROVIDER_ERROR,
                DONNA_PROVIDER_ERROR_NOT_SUPPORTED,
                "Provider 'register': Doesn't support IO as source");
        return FALSE;
    }

    if (type != DONNA_IO_COPY)
    {
        g_set_error (error, DONNA_PROVIDER_ERROR,
                DONNA_PROVIDER_ERROR_NOT_SUPPORTED,
                "Provider 'register': Only support copying (adding) to a register");
        return FALSE;
    }

    if (G_UNLIKELY (donna_node_peek_provider (dest) != (DonnaProvider *) _provider))
    {
        gchar *fl = donna_node_get_full_location (dest);
        g_set_error (error, DONNA_PROVIDER_ERROR,
                DONNA_PROVIDER_ERROR_NOT_SUPPORTED,
                "Provider 'register': destination node '%s' isn't in domain 'register'",
                fl);
        g_free (fl);
        return FALSE;
    }

    if (new_name && sources->len == 1)
    {
        g_set_error (error, DONNA_PROVIDER_ERROR,
                DONNA_PROVIDER_ERROR_NOT_SUPPORTED,
                "Provider 'register': "
                "Doesn't support move/copy operation with renaming on-the-fly");
        return FALSE;
    }

    return TRUE;
}

static DonnaTaskState
provider_register_io (DonnaProviderBase  *_provider,
                      DonnaTask          *task,
                      DonnaIoType         type,
                      gboolean            is_source,
                      GPtrArray          *sources,
                      DonnaNode          *dest,
                      const gchar        *new_name)
{
    GError *err = NULL;
    gchar *name;

    name = donna_node_get_location (dest);
    if (!register_add_nodes ((DonnaProviderRegister *) _provider, name, sources,
                NULL, &err))
    {
        donna_task_take_error (task, err);
        g_free (name);
        return DONNA_TASK_FAILED;
    }

    g_free (name);
    return DONNA_TASK_DONE;
}

static DonnaTaskState
provider_register_new_child (DonnaProviderBase  *_provider,
                             DonnaTask          *task,
                             DonnaNode          *parent,
                             DonnaNodeType       type,
                             const gchar        *name)
{
    GError *err = NULL;
    DonnaProviderRegisterPrivate *priv = ((DonnaProviderRegister *) _provider)->priv;
    struct reg *reg;
    DonnaNode *node_root = NULL;
    DonnaNode *node = NULL;
    GValue *value;
    gchar *s;

    s = donna_node_get_location (parent);
    if (!streq (s, "/"))
    {
        donna_task_set_error (task, DONNA_PROVIDER_ERROR,
                DONNA_PROVIDER_ERROR_NOT_SUPPORTED,
                "Provider 'register': Cannot create nodes inside a register");
        g_free (s);
        return DONNA_TASK_FAILED;
    }
    g_free (s);

    if (type == DONNA_NODE_ITEM)
    {
        donna_task_set_error (task, DONNA_PROVIDER_ERROR,
                DONNA_PROVIDER_ERROR_NOT_SUPPORTED,
                "Provider 'register': Cannot create an ITEM (registers are CONTAINERs)");
        return DONNA_TASK_FAILED;
    }

    if (!is_valid_register_name (&name, &err))
    {
        donna_task_take_error (task, err);
        return DONNA_TASK_FAILED;
    }

    /* those always exists (even when they don't) */
    if (streq (name, reg_default) || streq (name, reg_clipboard))
    {
        donna_task_set_error (task, DONNA_PROVIDER_ERROR,
                DONNA_PROVIDER_ERROR_ALREADY_EXIST,
                "Provider 'register': Cannot create register '%s'; it already exists",
                name);
        return DONNA_TASK_FAILED;
    }

    g_rec_mutex_lock (&priv->rec_mutex);
    reg = get_register (priv->registers, name);
    if (reg)
    {
        g_rec_mutex_unlock (&priv->rec_mutex);
        donna_task_set_error (task, DONNA_PROVIDER_ERROR,
                DONNA_PROVIDER_ERROR_ALREADY_EXIST,
                "Provider 'register': Cannot create register '%s'; it already exists",
                name);
        return DONNA_TASK_FAILED;
    }

    reg = new_register (name, DONNA_REGISTER_UNKNOWN);
    add_reg_to_registers ((DonnaProviderRegister *) _provider, reg, TRUE,
            &node_root, &node, &err);
    g_rec_mutex_unlock (&priv->rec_mutex);

    if (G_UNLIKELY (!node))
    {
        donna_task_take_error (task, err);
        return DONNA_TASK_FAILED;
    }

    if (node_root)
    {
        donna_provider_node_new_child ((DonnaProvider *) _provider, node_root, node);
        g_object_unref (node_root);
    }

    value = donna_task_grab_return_value (task);
    g_value_init (value, DONNA_TYPE_NODE);
    g_value_take_object (value, node);
    donna_task_release_return_value (task);

    return DONNA_TASK_DONE;
}

static DonnaTaskState
provider_register_remove_from (DonnaProviderBase  *_provider,
                               DonnaTask          *task,
                               GPtrArray          *nodes,
                               DonnaNode          *source)
{
    GError *err = NULL;
    DonnaProviderRegisterPrivate *priv = ((DonnaProviderRegister *) _provider)->priv;
    DonnaNode *node;
    GString *str = NULL;
    gchar *location;
    gchar *fl;
    guint i;

    location = donna_node_get_location (source);
    if (streq ("/", location))
    {
        /* IOW this shall be about deleting registers */

        for (i = 0; i < nodes->len; ++i)
        {
            node = nodes->pdata[i];
            fl = donna_node_get_full_location (node);

            if (G_UNLIKELY (donna_node_peek_provider (node) != (DonnaProvider *) _provider))
            {
                if (!str)
                    str = g_string_new (NULL);
                g_string_append_printf (str, "\n- Cannot remove '%s': "
                        "node isn't in 'register:/'",
                        fl);
                g_free (fl);
                continue;
            }

            /* 9 == strlen ("register:"); IOW: fl + 9 == location */
            if (G_UNLIKELY (!drop_register ((DonnaProviderRegister *) _provider,
                        fl + 9, NULL)))
            {
                if (!streq (fl + 9, reg_default) && !streq (fl + 9, reg_clipboard))
                {
                    if (!str)
                        str = g_string_new (NULL);
                    g_string_append_printf (str, "\n- Failed to drop register '%s', "
                            "it doesn't exist",
                            fl + 9);
                }
                g_free (fl);
                continue;
            }

            update_special_nodes ((DonnaProviderRegister *) _provider, fl + 9, FALSE);
            g_free (fl);
        }
    }
    else
    {
        GPtrArray *nodes_removed_from;
        struct reg *reg;
        gboolean has_items;

        nodes_removed_from = g_ptr_array_sized_new (nodes->len);
        g_rec_mutex_lock (&priv->rec_mutex);
        reg = get_register (priv->registers, location);
        if (G_UNLIKELY (!reg))
        {
            /* reg_default must always exists */
            if (*location == *reg_default)
            {
                g_rec_mutex_unlock (&priv->rec_mutex);
                donna_task_set_error (task, DONNA_PROVIDER_ERROR,
                        DONNA_PROVIDER_ERROR_LOCATION_NOT_FOUND,
                        "Provider 'register': Cannot remove nodes from 'register:%s', "
                        "register is empty",
                        location);
                g_ptr_array_unref (nodes_removed_from);
                g_free (location);
                return DONNA_TASK_FAILED;
            }
            /* same with reg_clipboard, except we import */
            else if (*location == *reg_clipboard)
            {
                DonnaNode *nr = NULL, *n = NULL;
                GString *string = NULL;

                reg = new_register (reg_clipboard, DONNA_REGISTER_UNKNOWN);
                if (!get_from_clipboard (_provider->app, &reg->hashtable,
                            &reg->list, &reg->last, &reg->type, &string, &err))
                {
                    g_rec_mutex_unlock (&priv->rec_mutex);
                    g_prefix_error (&err, "Provider 'register': "
                            "Cannot remove nodes from 'register:%s', "
                            "failed to get CLIPBOARD content: ",
                            location);
                    donna_task_take_error (task, err);
                    free_register (reg);
                    g_ptr_array_unref (nodes_removed_from);
                    g_free (location);
                    return DONNA_TASK_FAILED;;
                }
                else if (string)
                {
                    g_warning ("Failed to get some files from CLIPBOARD: %s",
                            string->str);
                    g_string_free (string, TRUE);
                }
                take_clipboard_ownership ((DonnaProviderRegister *) _provider, FALSE);
                add_reg_to_registers ((DonnaProviderRegister *) _provider, reg,
                        FALSE, &nr, &n, NULL);
                g_object_unref (nr);
                g_object_unref (n);
            }
            else
            {
                g_rec_mutex_unlock (&priv->rec_mutex);
                donna_task_set_error (task, DONNA_PROVIDER_ERROR,
                        DONNA_PROVIDER_ERROR_LOCATION_NOT_FOUND,
                        "Provider 'register': Cannot remove nodes from 'register:%s', "
                        "register doesn't exist",
                        location);
                g_ptr_array_unref (nodes_removed_from);
                g_free (location);
                return DONNA_TASK_FAILED;
            }
        }

        for (i = 0; i < nodes->len; ++i)
        {
            GList *l;
            gchar *s;

            node = nodes->pdata[i];
            if (reg->name == reg_clipboard)
                s = donna_node_get_location (node);
            else
                s = donna_node_get_full_location (node);

            l = g_hash_table_lookup (reg->hashtable, s);
            if (l)
            {
                if (l == reg->last)
                    reg->last = reg->last->prev;
                reg->list = g_list_delete_link (reg->list, l);
                g_hash_table_remove (reg->hashtable, s);
            }
            else
            {
                if (!str)
                    str = g_string_new (NULL);
                g_string_append_printf (str, "\n- Cannot remove '%s' from register '%s', "
                        "it's not in there",
                        s, location);
                g_free (s);
                continue;
            }

            g_ptr_array_add (nodes_removed_from, node);
            g_free (s);
        }
        has_items = g_hash_table_size (reg->hashtable) > 0;
        g_rec_mutex_unlock (&priv->rec_mutex);

        for (i = 0; i < nodes_removed_from->len; ++i)
            donna_provider_node_removed_from ((DonnaProvider *) _provider,
                    nodes_removed_from->pdata[i], source);
        g_ptr_array_unref (nodes_removed_from);
        update_special_nodes ((DonnaProviderRegister *) _provider, location, has_items);
    }

    if (str)
    {
        donna_task_set_error (task, DONNA_PROVIDER_ERROR,
                DONNA_PROVIDER_ERROR_OTHER,
                "Provider 'register': Couldn't remove all nodes from 'register:%s':\n%s",
                location, str->str);
        g_string_free (str, TRUE);
        g_free (location);
        return DONNA_TASK_FAILED;
    }

    g_free (location);
    return DONNA_TASK_DONE;
}

/* commands */

/**
 * register_add_nodes:
 * @name: (allow-none): Name of the register
 * @nodes: (array): Nodes to add
 *
 * Add @nodes to register @name. If @name isn't specified, use default register
 *
 * Returns: The node of the register @name
 */
static DonnaTaskState
cmd_register_add_nodes (DonnaTask               *task,
                        DonnaApp                *app,
                        gpointer                *args,
                        DonnaProviderRegister   *pr)
{
    GError *err = NULL;
    const gchar *name = args[0]; /* opt */
    GPtrArray *nodes = args[1];

    DonnaNode *node;
    GValue *value;

    if (!is_valid_register_name (&name, &err))
    {
        donna_task_take_error (task, err);
        return DONNA_TASK_FAILED;
    }

    if (!register_add_nodes (pr, name, nodes, &node, &err))
    {
        donna_task_take_error (task, err);
        return DONNA_TASK_FAILED;
    }

    if (!node)
        node = create_node_for (pr, name, &err);
    if (G_UNLIKELY (!node))
    {
        g_prefix_error (&err, "Command 'register_add_nodes': "
                "Failed to get node for register: ");
        donna_task_take_error (task, err);
        return DONNA_TASK_FAILED;
    }

    value = donna_task_grab_return_value (task);
    g_value_init (value, DONNA_TYPE_NODE);
    g_value_take_object (value, node);
    donna_task_release_return_value (task);

    return DONNA_TASK_DONE;
}

/**
 * register_drop:
 * @name: (allow-none): Name of the register
 *
 * Drop register @name, or default register if @name not specified
 *
 * Dropping a non-existing register does nothing. Dropping default register or
 * register "+" (clipboard) will simply reset/empty them, since they always
 * exist.
 */
static DonnaTaskState
cmd_register_drop (DonnaTask               *task,
                   DonnaApp                *app,
                   gpointer                *args,
                   DonnaProviderRegister   *pr)
{
    GError *err = NULL;
    const gchar *name = args[0]; /* opt */

    if (!is_valid_register_name (&name, &err))
    {
        donna_task_take_error (task, err);
        return DONNA_TASK_FAILED;
    }

    if (!register_drop (pr, name, &err))
    {
        donna_task_take_error (task, err);
        return DONNA_TASK_FAILED;
    }
    return DONNA_TASK_DONE;
}

/**
 * register_get_nodes:
 * @name: (allow-none): Name of the register
 * @drop: (allow-none): How to drop (or not) the register afterwards
 *
 * Get the nodes from register @name (or default if not specified) and drops it
 * (or not), according to @drop
 *
 * @drop can be one of "not" (do not drop register), "always" (drop register),
 * or "on-cut" (drop register only if it was of type "cut"). Defaults to
 * "on-cut"
 *
 * Returns: (array): The nodes from the register
 */
static DonnaTaskState
cmd_register_get_nodes (DonnaTask               *task,
                        DonnaApp                *app,
                        gpointer                *args,
                        DonnaProviderRegister   *pr)
{
    GError *err = NULL;
    const gchar *name = args[0]; /* opt */
    gchar *drop = args[1]; /* opt */

    const gchar *c_drop[] = { "not", "always", "on-cut" };
    DonnaDropRegister drops[] = { DONNA_DROP_REGISTER_NOT,
        DONNA_DROP_REGISTER_ALWAYS, DONNA_DROP_REGISTER_ON_CUT };
    gint c;
    GPtrArray *arr;
    GValue *value;

    if (!is_valid_register_name (&name, &err))
    {
        donna_task_take_error (task, err);
        return DONNA_TASK_FAILED;
    }

    if (drop)
    {
        c = _get_choice (c_drop, drop);
        if (c < 0)
        {
            donna_task_set_error (task, DONNA_COMMAND_ERROR,
                    DONNA_COMMAND_ERROR_SYNTAX,
                    "Command 'register_get_nodes': Invalid drop option: '%s'; "
                    "Must be 'not', 'always' or 'on-cut'",
                    drop);
            return DONNA_TASK_FAILED;
        }
    }
    else
        c = 2; /* DONNA_DROP_REGISTER_ON_CUT */

    if (!register_get_nodes (pr, name, drops[c],
                NULL, &arr, &err))
    {
        donna_task_take_error (task, err);
        return DONNA_TASK_FAILED;
    }

    value = donna_task_grab_return_value (task);
    g_value_init (value, G_TYPE_PTR_ARRAY);
    g_value_take_boxed (value, arr);
    donna_task_release_return_value (task);

    return DONNA_TASK_DONE;
}

/**
 * register_get_type:
 * @name: (allow-none): The name of the register
 *
 * Get the type of the register. Can be one of "unknown", "cut" or "copy"
 *
 * Returns: The type of the register @name, or default one if not specified
 */
static DonnaTaskState
cmd_register_get_type (DonnaTask               *task,
                       DonnaApp                *app,
                       gpointer                *args,
                       DonnaProviderRegister   *pr)
{
    GError *err = NULL;
    const gchar *name = args[0]; /* opt */

    DonnaRegisterType type;
    GValue *value;
    const gchar *s_type[3];
    s_type[DONNA_REGISTER_UNKNOWN]  = "unknown";
    s_type[DONNA_REGISTER_CUT]      = "cut";
    s_type[DONNA_REGISTER_COPY]     = "copy";

    if (!is_valid_register_name (&name, &err))
    {
        donna_task_take_error (task, err);
        return DONNA_TASK_FAILED;
    }

    if (!register_get_nodes (pr, name, DONNA_DROP_REGISTER_NOT, &type, NULL, &err))
    {
        donna_task_take_error (task, err);
        return DONNA_TASK_FAILED;
    }

    value = donna_task_grab_return_value (task);
    g_value_init (value, G_TYPE_STRING);
    g_value_set_static_string (value, s_type[type]);
    donna_task_release_return_value (task);

    return DONNA_TASK_DONE;
}

/**
 * register_load:
 * @name: (allow-none): The name of the register
 * @filename: Name of the file to load from
 * @file_type: (allow-none): Type/format of @filename
 *
 * Load content of register @name (or default one) from @filename, usually saved
 * using command register_save()
 *
 * @filename can be either a full path to a file, or the filename in the current
 * location (according to donna_app_get_current_dirname())
 *
 * @file_type can be one of "nodes", "files" or "uris" See register_save() for
 * more. Defaults to "nodes"
 *
 * If the register already existed, it will basically be dropped first.
 *
 * Returns: The node of the register
 */
static DonnaTaskState
cmd_register_load (DonnaTask               *task,
                   DonnaApp                *app,
                   gpointer                *args,
                   DonnaProviderRegister   *pr)
{
    GError *err = NULL;
    const gchar *name = args[0]; /* opt */
    gchar *file = args[1];
    gchar *file_type = args[2]; /* opt */

    const gchar *c_file_type[] = { "nodes", "files", "uris" };
    DonnaRegisterFile file_types[] = { DONNA_REGISTER_FILE_NODES,
        DONNA_REGISTER_FILE_FILE, DONNA_REGISTER_FILE_URIS };
    gint c;

    DonnaNode *node;
    GValue *value;

    if (!is_valid_register_name (&name, &err))
    {
        donna_task_take_error (task, err);
        return DONNA_TASK_FAILED;
    }

    if (file_type)
    {
        c = _get_choice (c_file_type, file_type);
        if (c < 0)
        {
            donna_task_set_error (task, DONNA_COMMAND_ERROR,
                    DONNA_COMMAND_ERROR_SYNTAX,
                    "Command 'register_load': Invalid register file type: '%s'; "
                    "Must be 'nodes', 'files' or 'uris'",
                    file_type);
            return DONNA_TASK_FAILED;
        }
    }
    else
        c = 0;

    if (!register_load (pr, name, file, file_types[c], NULL, &err))
    {
        donna_task_take_error (task, err);
        return DONNA_TASK_FAILED;
    }

    node = create_node_for (pr, name, &err);
    if (G_UNLIKELY (!node))
    {
        g_prefix_error (&err, "Command 'register_load': "
                "Failed to get node for register: ");
        donna_task_take_error (task, err);
        return DONNA_TASK_FAILED;
    }

    value = donna_task_grab_return_value (task);
    g_value_init (value, DONNA_TYPE_NODE);
    g_value_take_object (value, node);
    donna_task_release_return_value (task);

    return DONNA_TASK_DONE;
}

/**
 * register_load_all:
 * @filename: (allow-none): Name of the file to load from
 * @ignore_no_file: (allow-none): Set to 1 to ignore if @filename doesn't exist
 * @reset: (allow-none): Set to 1 to drop all existing registers first
 *
 * Load all registers from @filename, usually created via command
 * register_save_all()
 *
 * @filename can be either a full path to a file, or it will be processed
 * through donna_app_get_conf_filename() (Defaulting to "registers" if not
 * specified)
 *
 * If @ignore_no_file is set and @file doesn't exist, this will do nothing, else
 * it would fail.
 *
 * If @reset is set, all registers will be dropped prior to loading new ones
 * (but after reading @file's content, though no syntax check has been done yet).
 */
static DonnaTaskState
cmd_register_load_all (DonnaTask                *task,
                       DonnaApp                 *app,
                       gpointer                 *args,
                       DonnaProviderRegister    *pr)
{
    GError *err = NULL;
    DonnaProviderRegisterPrivate *priv = pr->priv;

    const gchar *file = args[0]; /* opt */
    gboolean ignore_no_file = GPOINTER_TO_INT (args[1]); /* opt */
    gboolean reset = GPOINTER_TO_INT (args[2]); /* opt */

    DonnaTaskState ret = DONNA_TASK_FAILED;
    gchar *filename;
    gchar *data;
    gchar *d;
    gchar *s;
    DonnaNode *node;
    GPtrArray *nodes_drop = NULL;
    GPtrArray *emit_load;

    if (!file)
        filename = donna_app_get_conf_filename (app, "registers");
    else
        filename = get_filename (file);

    if (!g_file_get_contents (filename, &data, NULL, &err))
    {
        if (filename != file)
            g_free (filename);
        if (ignore_no_file && g_error_matches (err, G_FILE_ERROR, G_FILE_ERROR_NOENT))
        {
            g_clear_error (&err);
            return DONNA_TASK_DONE;
        }
        else
        {
            g_prefix_error (&err, "Command 'register_load_all': Failed to load registers: ");
            donna_task_take_error (task, err);
            return DONNA_TASK_FAILED;
        }
    }
    if (filename != file)
        g_free (filename);

    g_rec_mutex_lock (&priv->rec_mutex);
    if (reset)
    {
        GSList *l;

        nodes_drop = g_ptr_array_new ();
        for (l = priv->registers; l; l = l->next)
        {
            drop_register (pr, ((struct reg *) l->data)->name, &node);
            if (node)
                g_ptr_array_add (nodes_drop, node);
        }
    }

    emit_load = g_ptr_array_new ();
    d = data;
    while ((s = strchr (d, '\n')))
    {
        struct emit_load *el;
        gchar *name;

        name = d;
        *s = '\0';
        if (!is_valid_register_name ((const gchar **) &name, &err))
        {
            g_prefix_error (&err, "Command 'register_load_all': Failed to load registers, "
                    "invalid file format for '%s': ",
                    (file) ? file : "registers");
            donna_task_take_error (task, err);
            goto finish;
        }

        d = s + 1;
        s = strstr (d, "\n\n");
        if (!s)
        {
            donna_task_set_error (task, DONNA_PROVIDER_ERROR,
                    DONNA_PROVIDER_ERROR_OTHER,
                    "Command 'register_load_all': Failed to load registers, "
                    "invalid file format for '%s'",
                    (file) ? file : "registers");
            goto finish;
        }
        *++s = '\0';

        el = g_new0 (struct emit_load, 1);
        if (!register_import (pr, name, d, DONNA_REGISTER_FILE_NODES, el, &err))
        {
            g_free (el);
            g_prefix_error (&err, "Command 'register_load_all': Failed to load registers: ");
            donna_task_take_error (task, err);
            goto finish;
        }
        g_ptr_array_add (emit_load, el);

        d = s + 1;
    }

    ret = DONNA_TASK_DONE;
finish:
    g_rec_mutex_unlock (&priv->rec_mutex);
    g_free (data);

    if (nodes_drop)
    {
        guint i;

        for (i = 0; i < nodes_drop->len; ++i)
        {
            s = donna_node_get_location (nodes_drop->pdata[i]);
            emit_drop (pr, nodes_drop->pdata[i],
                    *s == *reg_default || *s == *reg_clipboard);
            update_special_nodes (pr, s, FALSE);
            g_free (s);
        }
        g_ptr_array_unref (nodes_drop);
    }

    if (emit_load)
    {
        guint i;

        for (i = 0; i < emit_load->len; ++i)
        {
            struct emit_load *el = emit_load->pdata[i];

            if (el->node)
            {
                s = donna_node_get_location (el->node);
                update_special_nodes (pr, s, el->arr->len > 0);
                g_free (s);
            }

            if (el->node_root)
            {
                donna_provider_node_new_child ((DonnaProvider *) pr,
                        el->node_root, el->node);
                g_object_unref (el->node_root);
                g_object_unref (el->node);
            }
            else if (el->node)
            {
                if (el->reg_type != (guint) -1)
                    update_node_type ((DonnaProvider *) pr, el->node, el->reg_type);
                emit_node_children_from_arr (pr, el->node, el->arr);
                g_object_unref (el->node);
            }
            g_ptr_array_unref (el->arr);
            g_free (el);
        }
        g_ptr_array_unref (emit_load);
    }

    return ret;
}

/**
 * register_nodes_io:
 * @name: (allow-none): Name of the register
 * @io_type: (allow-none): Type of IO operation to perform
 * @dest: (allow-none): Destination for the IO operation
 * @in_new_folder: (allow-none): Set to 1 to set destination in a newly created
 * folder
 *
 * Perform the IO operation requested on register @name, or the default one if
 * not specified.
 *
 * @io_type canbe one of "auto" (based on the type of the register), "copy"
 * (force copy even for register "cut"), "move" (force move even for register
 * "copy") or "delete" (to delete the content of the register, instead of
 * copying/moving it) Defaults to "auto"
 *
 * Note that dropping register if done when getting the nodes and before
 * starting the actual IO operation so register might be dropped even though the
 * IO operation failed.
 *
 * @in_new_folder cannot be used with @io_type "delete" When set, the user will
 * be asked to enter the name of a new folder to create inside @dest, and to be
 * used as new destination of the IO operation.
 * IOW this allows to copy/move inside a new folder, created on the fly. Default
 * names for the new folder will be offered, being the names (with & w/out
 * extension) of the 3 first nodes in the register.
 *
 * Returns: (array) (allow-none): For copy/move operations, the resulting nodes
 * will be returned. For delete operation, there won't be no return value.
 */
static DonnaTaskState
cmd_register_nodes_io (DonnaTask               *task,
                       DonnaApp                *app,
                       gpointer                *args,
                       DonnaProviderRegister   *pr)
{
    GError *err = NULL;
    const gchar *reg_name = args[0]; /* opt */
    gchar *io_type = args[1]; /* opt */
    DonnaNode *dest = args[2]; /* opt */
    gboolean in_new_folder = GPOINTER_TO_INT (args[3]); /* opt */

    const gchar *c_io_type[] = { "auto", "copy", "move", "delete" };
    DonnaIoType io_types[] = { DONNA_IO_UNKNOWN, DONNA_IO_COPY, DONNA_IO_MOVE,
        DONNA_IO_DELETE };
    DonnaDropRegister drop;
    DonnaRegisterType reg_type;
    gint c_io;
    GPtrArray *nodes;
    DonnaTask *t;
    DonnaTaskState state;

    if (!is_valid_register_name (&reg_name, &err))
    {
        donna_task_take_error (task, err);
        return DONNA_TASK_FAILED;
    }

    if (io_type)
    {
        c_io = _get_choice (c_io_type, io_type);
        if (c_io < 0)
        {
            donna_task_set_error (task, DONNA_COMMAND_ERROR,
                    DONNA_COMMAND_ERROR_SYNTAX,
                    "Command 'register_nodes_io': Invalid type of IO operation: '%s'; "
                    "Must be 'auto', 'copy', 'move' or 'delete'",
                    io_type);
            return DONNA_TASK_FAILED;
        }
    }
    else
        /* default to 'auto' */
        c_io = 0;

    switch (c_io)
    {
        default:
        case 0:
            drop = DONNA_DROP_REGISTER_ON_CUT;
            break;
        case 1:
            drop = DONNA_DROP_REGISTER_NOT;
            break;
        case 2:
        case 3:
            drop = DONNA_DROP_REGISTER_ALWAYS;
            break;
    }

    if (in_new_folder && io_types[c_io] == DONNA_IO_DELETE)
    {
        donna_task_set_error (task, DONNA_PROVIDER_ERROR,
                DONNA_PROVIDER_ERROR_NOT_SUPPORTED,
                "Command 'register_nodes_io': Cannot use option 'in_new_folder' "
                "with IO 'delete'");
        return DONNA_TASK_FAILED;
    }

    if (!register_get_nodes (pr, reg_name, drop, &reg_type, &nodes, &err))
    {
        donna_task_take_error (task, err);
        return DONNA_TASK_FAILED;
    }

    if (in_new_folder && nodes->len > 0)
    {
        DonnaNode *n;
        GString *str;
        GString *str_defs;
        gchar *name;
        guint i;

        /* create command ask_text() to ask user the name of the new folder */
        str = g_string_new ("command:ask_text (");
        donna_g_string_append_quoted (str, "Paste Into New Folder", FALSE);
        g_string_append_c (str, ',');
        donna_g_string_append_quoted (str,
                "Please enter the name of the new folder to paste into", FALSE);
        g_string_append_c (str, ',');

        /* other defaults: name & name w/out extension, for first 3 items */
        str_defs = g_string_new (NULL);
        for (i = 0; i < 3 && i < nodes->len; ++i)
        {
            gchar *s;

            if (i > 0)
                g_string_append_c (str_defs, ',');
            name = donna_node_get_name (nodes->pdata[i]);
            donna_g_string_append_quoted (str_defs, name, FALSE);

            s = strrchr (name, '.');
            if (s)
            {
                *s = '\0';
                g_string_append_c (str_defs, ',');
                donna_g_string_append_quoted (str_defs, name, FALSE);
            }
            /* main default */
            if (i == 0)
            {
                donna_g_string_append_quoted (str, name, FALSE);
                g_string_append_c (str, ',');
            }

            g_free (name);
        }

        if (str_defs->len > 0)
            donna_g_string_append_quoted (str, str_defs->str, FALSE);
        g_string_free (str_defs, TRUE);

        g_string_append_c (str, ')');

        /* get the node for ask_text() */
        n = donna_app_get_node (app, str->str, FALSE, &err);
        g_string_free (str, TRUE);
        if (G_UNLIKELY (!n))
        {
            g_prefix_error (&err, "Command 'register_nodes_io': "
                    "Failed to get node for ask_text command: ");
            donna_task_take_error (task, err);
            g_ptr_array_unref (nodes);
            return DONNA_TASK_FAILED;
        }

        /* trigger ask_text() */
        t = donna_node_trigger_task (n, &err);
        g_object_unref (n);
        if (!t)
        {
            g_prefix_error (&err, "Command 'register_nodes_io': "
                    "Failed to get task to trigger ask_text: ");
            donna_task_take_error (task, err);
            g_ptr_array_unref (nodes);
            return DONNA_TASK_FAILED;
        }

        if (!donna_app_run_task_and_wait (app, g_object_ref (t), task, &err))
        {
            g_prefix_error (&err, "Command 'register_nodes_io': "
                    "Failed to run trigger task: ");
            donna_task_take_error (task, err);
            g_object_unref (t);
            g_ptr_array_unref (nodes);
            return DONNA_TASK_FAILED;
        }

        state = donna_task_get_state (t);
        if (state != DONNA_TASK_DONE)
        {
            if (state == DONNA_TASK_FAILED)
            {
                err = g_error_copy (donna_task_get_error (t));
                g_prefix_error (&err, "Command 'register_nodes_io': "
                        "Failed during command ask_text: ");
                donna_task_take_error (task, err);
            }
            g_ptr_array_unref (nodes);
            return state;
        }

        /* get name of new folder to create & paste into */
        name = g_value_dup_string (donna_task_get_return_value (t));
        g_object_unref (t);

        t = donna_node_new_child_task (dest, DONNA_NODE_CONTAINER, name, &err);
        if (G_UNLIKELY (!t))
        {
            g_prefix_error (&err, "Command 'register_nodes_io': "
                    "Failed to create new folder '%s': ", name);
            donna_task_take_error (task, err);
            g_ptr_array_unref (nodes);
            g_free (name);
            return DONNA_TASK_FAILED;
        }
        g_free (name);

        if (!donna_app_run_task_and_wait (app, g_object_ref (t), task, &err))
        {
            g_prefix_error (&err, "Command 'register_nodes_io': "
                    "Failed to run new_child task: ");
            donna_task_take_error (task, err);
            g_object_unref (t);
            g_ptr_array_unref (nodes);
            return DONNA_TASK_FAILED;
        }

        state = donna_task_get_state (t);
        if (state != DONNA_TASK_DONE)
        {
            if (state == DONNA_TASK_FAILED)
            {
                err = g_error_copy (donna_task_get_error (t));
                g_prefix_error (&err, "Command 'register_nodes_io': "
                        "Failed creating new folder: ");
                donna_task_take_error (task, err);
            }
            g_ptr_array_unref (nodes);
            g_object_unref (t);
            return state;
        }

        /* switch dest to the newly created folder */
        dest = g_value_dup_object (donna_task_get_return_value (t));
        g_object_unref (t);
    }

    if (c_io == 0)
        c_io = (reg_type == DONNA_REGISTER_CUT) ? 2 /* MOVE */ : 1 /* COPY */;

    t = donna_app_nodes_io_task (app, nodes, io_types[c_io], dest, NULL, &err);
    if (!t)
    {
        donna_task_take_error (task, err);
        g_ptr_array_unref (nodes);
        if (in_new_folder && nodes->len > 0)
            g_object_unref (dest);
        return DONNA_TASK_FAILED;
    }
    g_ptr_array_unref (nodes);

    if (!donna_app_run_task_and_wait (app, g_object_ref (t), task, &err))
    {
        g_prefix_error (&err, "Command 'register_nodes_io': "
                "Failed to run nodes_io task: ");
        donna_task_take_error (task, err);
        g_object_unref (t);
        if (in_new_folder && nodes->len > 0)
            g_object_unref (dest);
        return DONNA_TASK_FAILED;
    }

    state = donna_task_get_state (t);
    if (state == DONNA_TASK_DONE)
    {
        if (io_types[c_io] != DONNA_IO_DELETE)
        {
            GValue *value;

            value = donna_task_grab_return_value (task);
            g_value_init (value, G_TYPE_PTR_ARRAY);
            g_value_set_boxed (value,
                    g_value_get_boxed (donna_task_get_return_value (t)));
            donna_task_release_return_value (task);
        }
    }
    else
    {
        err = (GError *) donna_task_get_error (t);
        if (err)
        {
            err = g_error_copy (err);
            g_prefix_error (&err, "Command 'register_nodes_io': IO task failed: ");
            donna_task_take_error (task, err);
        }
        else
            donna_task_set_error (task, DONNA_COMMAND_ERROR,
                    DONNA_COMMAND_ERROR_OTHER,
                    "Command 'register_nodes_io': IO task failed without error message");
    }

    g_object_unref (t);
    if (in_new_folder && nodes->len > 0)
        g_object_unref (dest);
    return state;
}

/**
 * register_save:
 * @name: (allow-none): Name of the register
 * @filename: Name of the file to save to
 * @file_type: (allow-none): Type/format of @filename
 *
 * Save content of register @name (or default one) to @filename, so it can later
 * be loaded using register_load()
 *
 * @filename can be either a full path to a file, or the filename in the current
 * location (according to donna_app_get_current_dirname())
 *
 * @file_type can be one of "nodes", "files" or "uris" Defaults to "nodes"
 *
 * Every file type uses the same format: the first line contains either "cut" or
 * "copy", indicating the type of register (i.e. how items were put in the
 * register).
 * Then every line contains an identifier:
 *
 * - "nodes": the full location of the node
 * - "files": the location, only for nodes in "fs" (others are skipped)
 * - "uris": the location encoded as URI, only for nodes in "fs" (others are
 *   skipped)
 *
 * Using "nodes" allows to use any nodes from donna and reload the full register
 * later. The other two follow comon standard (including that of the clipboard)
 * and are likely to be supported by other applications.
 */
static DonnaTaskState
cmd_register_save (DonnaTask               *task,
                   DonnaApp                *app,
                   gpointer                *args,
                   DonnaProviderRegister   *pr)
{
    GError *err = NULL;
    const gchar *name = args[0]; /* opt */
    gchar *file = args[1];
    gchar *file_type = args[2]; /* opt */

    const gchar *c_file_type[] = { "nodes", "files", "uris" };
    DonnaRegisterFile file_types[] = { DONNA_REGISTER_FILE_NODES,
        DONNA_REGISTER_FILE_FILE, DONNA_REGISTER_FILE_URIS };
    gint c;

    if (!is_valid_register_name (&name, &err))
    {
        donna_task_take_error (task, err);
        return DONNA_TASK_FAILED;
    }

    if (file_type)
    {
        c = _get_choice (c_file_type, file_type);
        if (c < 0)
        {
            donna_task_set_error (task, DONNA_COMMAND_ERROR,
                    DONNA_COMMAND_ERROR_SYNTAX,
                    "Command 'register_save': Invalid register file type: '%s'; "
                    "Must be 'nodes', 'files' or 'uris'",
                    file_type);
            return DONNA_TASK_FAILED;
        }
    }
    else
        c = 0;

    if (!register_save (pr, name, file, file_types[c], &err))
    {
        donna_task_take_error (task, err);
        return DONNA_TASK_FAILED;
    }

    return DONNA_TASK_DONE;
}

/**
 * register_save_all:
 * @filename: (allow-none): Name of the file to save to
 *
 * Saves all registers into @filename, so they can be loaded later using
 * register_load_all()
 *
 * @filename can be either a full path to a file, or it will be processed
 * through donna_app_get_conf_filename() (Defaulting to "registers" if not
 * specified)
 *
 * The format of the file is a simple text file with the name of the register on
 * one line, then the content of the register as done using command
 * register_save() with file type "nodes", and an empty line to indidcate the
 * end. Repeat with as many registers as there are.
 *
 * <note><para>Note that the clipboard (register "+") is not included in the
 * file, as it is a special case and its content can be defined by any other
 * application.</para></note>
 */
static DonnaTaskState
cmd_register_save_all (DonnaTask                *task,
                       DonnaApp                 *app,
                       gpointer                 *args,
                       DonnaProviderRegister    *pr)
{
    GError *err = NULL;
    DonnaProviderRegisterPrivate *priv = pr->priv;

    const gchar *file = args[0]; /* opt */

    GString *str_err = NULL;
    GString *str;
    GSList *l;
    gchar *filename;
    gboolean got_default = FALSE;

    str = g_string_new (NULL);
    g_rec_mutex_lock (&priv->rec_mutex);
    for (l = priv->registers; l; l = l->next)
    {
        struct reg *reg = l->data;

        if (reg->name == reg_clipboard)
            continue;

        g_string_append (str, reg->name);
        g_string_append_c (str, '\n');
        if (!register_export (pr, reg->name, DONNA_REGISTER_FILE_NODES, str, &err))
        {
            g_string_truncate (str, strlen (reg->name) + 1);
            if (!str_err)
                str_err = g_string_new (NULL);
            g_string_append_printf (str_err, "- Couldn't save register '%s', "
                    "failed to export its content: %s\n",
                    reg->name, err->message);
            g_clear_error (&err);
            continue;
        }
        g_string_append_c (str, '\n');
        if (reg->name == reg_default)
            got_default = TRUE;
    }
    g_rec_mutex_unlock (&priv->rec_mutex);

    if (!got_default)
        g_string_append (str, "_\ncopy\n\n");

    if (!file)
        filename = donna_app_get_conf_filename (app, "registers");
    else
        filename = get_filename (file);

    if (!g_file_set_contents (filename, str->str, (gssize) str->len, &err))
    {
        g_prefix_error (&err, "Command 'register_save_all': Failed to save registers: ");
        donna_task_take_error (task, err);
        if (str_err)
            g_string_free (str_err, TRUE);
        if (filename != file)
            g_free (filename);
        g_string_free (str, TRUE);
        return DONNA_TASK_FAILED;
    }
    if (filename != file)
        g_free (filename);
    g_string_free (str, TRUE);

    if (str_err)
    {
        donna_task_set_error (task, DONNA_PROVIDER_ERROR,
                DONNA_PROVIDER_ERROR_OTHER,
                "Command 'register_save_all': Some registers could not be saved:\n%s",
                str_err->str);
        g_string_free (str_err, TRUE);
        return DONNA_TASK_FAILED;
    }

    return DONNA_TASK_DONE;
}

/**
 * register_set:
 * @name: (allow-none): Name of the register
 * @type: Type to set
 * @nodes: (array): Nodes to set in the register
 *
 * Set @nodes in register @name (or default one if not specified) as type @type
 *
 * @type must be one of "cut" or "copy"
 *
 * Returns: The node of the register
 */
static DonnaTaskState
cmd_register_set (DonnaTask               *task,
                  DonnaApp                *app,
                  gpointer                *args,
                  DonnaProviderRegister   *pr)
{
    GError *err = NULL;
    const gchar *name = args[0]; /* opt */
    gchar *type = args[1];
    GPtrArray *nodes = args[2];

    const gchar *c_type[] = { "cut", "copy" };
    DonnaRegisterType types[] = { DONNA_REGISTER_CUT, DONNA_REGISTER_COPY };
    gint c;

    DonnaNode *node;
    GValue *value;

    if (!is_valid_register_name (&name, &err))
    {
        donna_task_take_error (task, err);
        return DONNA_TASK_FAILED;
    }

    c = _get_choice (c_type, type);
    if (c < 0)
    {
        donna_task_set_error (task, DONNA_COMMAND_ERROR,
                DONNA_COMMAND_ERROR_SYNTAX,
                "Command 'register_set': Invalid register type: '%s'; "
                "Must be 'cut' or 'copy'",
                type);
        return DONNA_TASK_FAILED;
    }

    if (!register_set (pr, name, types[c], nodes, &node, &err))
    {
        donna_task_take_error (task, err);
        return DONNA_TASK_FAILED;
    }

    if (!node)
        node = create_node_for (pr, name, &err);
    if (G_UNLIKELY (!node))
    {
        g_prefix_error (&err, "Command 'register_set': "
                "Failed to get node for register: ");
        donna_task_take_error (task, err);
        return DONNA_TASK_FAILED;
    }

    value = donna_task_grab_return_value (task);
    g_value_init (value, DONNA_TYPE_NODE);
    g_value_take_object (value, node);
    donna_task_release_return_value (task);

    return DONNA_TASK_DONE;
}

/**
 * register_set_type:
 * @name: (allow-none): Name of the register
 * @type: Type to set for the register
 *
 * Sets register @name (or default name if not specified) to @type
 *
 * @type must be one of "cut" or "copy"
 *
 * Returns: The node of the register
 */
static DonnaTaskState
cmd_register_set_type (DonnaTask               *task,
                       DonnaApp                *app,
                       gpointer                *args,
                       DonnaProviderRegister   *pr)
{
    GError *err = NULL;
    const gchar *name = args[0]; /* opt */
    gchar *type = args[1];

    const gchar *c_type[] = { "cut", "copy" };
    DonnaRegisterType types[] = { DONNA_REGISTER_CUT, DONNA_REGISTER_COPY };
    gint c;

    if (!is_valid_register_name (&name, &err))
    {
        donna_task_take_error (task, err);
        return DONNA_TASK_FAILED;
    }

    c = _get_choice (c_type, type);
    if (c < 0)
    {
        donna_task_set_error (task, DONNA_COMMAND_ERROR,
                DONNA_COMMAND_ERROR_SYNTAX,
                "Command 'register_set_type': Invalid register type: '%s'; "
                "Must be 'cut' or 'copy'",
                type);
        return DONNA_TASK_FAILED;
    }

    if (!register_set_type (pr, name, types[c], &err))
    {
        donna_task_take_error (task, err);
        return DONNA_TASK_FAILED;
    }

    return DONNA_TASK_DONE;
}

#define add_command(cmd_name, cmd_argc, cmd_visibility, cmd_return_value)       \
if (G_UNLIKELY (!donna_provider_command_add_command (pc, #cmd_name,             \
                (guint) cmd_argc, arg_type, cmd_return_value, cmd_visibility,   \
                (command_fn) cmd_##cmd_name, object, NULL, &err)))              \
{                                                                               \
    g_warning ("Provider 'register': Failed to add command '" #cmd_name "': %s",\
        err->message);                                                          \
    g_clear_error (&err);                                                       \
}
static void
provider_register_contructed (GObject *object)
{
    GError *err = NULL;
    DonnaProviderCommand *pc;
    DonnaArgType arg_type[8];
    gint i;

    G_OBJECT_CLASS (donna_provider_register_parent_class)->constructed (object);

    pc = (DonnaProviderCommand *) donna_app_get_provider (
            ((DonnaProviderBase *) object)->app, "command");
    if (G_UNLIKELY (!pc))
    {
        g_warning ("Provider 'register': Failed to add commands, "
                "couldn't get provider 'command'");
        return;
    }

    i = -1;
    arg_type[++i] = DONNA_ARG_TYPE_STRING | DONNA_ARG_IS_OPTIONAL;
    arg_type[++i] = DONNA_ARG_TYPE_NODE | DONNA_ARG_IS_ARRAY;
    add_command (register_add_nodes, ++i, DONNA_TASK_VISIBILITY_INTERNAL_FAST,
            DONNA_ARG_TYPE_NODE);

    i = -1;
    arg_type[++i] = DONNA_ARG_TYPE_STRING | DONNA_ARG_IS_OPTIONAL;
    add_command (register_drop, ++i, DONNA_TASK_VISIBILITY_INTERNAL_FAST,
            DONNA_ARG_TYPE_NOTHING);

    i = -1;
    arg_type[++i] = DONNA_ARG_TYPE_STRING | DONNA_ARG_IS_OPTIONAL;
    arg_type[++i] = DONNA_ARG_TYPE_STRING | DONNA_ARG_IS_OPTIONAL;
    add_command (register_get_nodes, ++i, DONNA_TASK_VISIBILITY_INTERNAL,
            DONNA_ARG_TYPE_NODE | DONNA_ARG_IS_ARRAY);

    i = -1;
    arg_type[++i] = DONNA_ARG_TYPE_STRING | DONNA_ARG_IS_OPTIONAL;
    add_command (register_get_type, ++i, DONNA_TASK_VISIBILITY_INTERNAL_FAST,
            DONNA_ARG_TYPE_STRING);

    i = -1;
    arg_type[++i] = DONNA_ARG_TYPE_STRING | DONNA_ARG_IS_OPTIONAL;
    arg_type[++i] = DONNA_ARG_TYPE_STRING;
    arg_type[++i] = DONNA_ARG_TYPE_STRING | DONNA_ARG_IS_OPTIONAL;
    add_command (register_load, ++i, DONNA_TASK_VISIBILITY_INTERNAL,
            DONNA_ARG_TYPE_NODE);

    i = -1;
    arg_type[++i] = DONNA_ARG_TYPE_STRING | DONNA_ARG_IS_OPTIONAL;
    arg_type[++i] = DONNA_ARG_TYPE_INT | DONNA_ARG_IS_OPTIONAL;
    arg_type[++i] = DONNA_ARG_TYPE_INT | DONNA_ARG_IS_OPTIONAL;
    add_command (register_load_all, ++i, DONNA_TASK_VISIBILITY_INTERNAL,
            DONNA_ARG_TYPE_NOTHING);

    i = -1;
    arg_type[++i] = DONNA_ARG_TYPE_STRING | DONNA_ARG_IS_OPTIONAL;
    arg_type[++i] = DONNA_ARG_TYPE_STRING | DONNA_ARG_IS_OPTIONAL;
    arg_type[++i] = DONNA_ARG_TYPE_NODE | DONNA_ARG_IS_OPTIONAL;
    arg_type[++i] = DONNA_ARG_TYPE_INT | DONNA_ARG_IS_OPTIONAL;
    add_command (register_nodes_io, ++i, DONNA_TASK_VISIBILITY_INTERNAL,
            DONNA_ARG_TYPE_NODE | DONNA_ARG_IS_ARRAY);

    i = -1;
    arg_type[++i] = DONNA_ARG_TYPE_STRING | DONNA_ARG_IS_OPTIONAL;
    arg_type[++i] = DONNA_ARG_TYPE_STRING;
    arg_type[++i] = DONNA_ARG_TYPE_STRING | DONNA_ARG_IS_OPTIONAL;
    add_command (register_save, ++i, DONNA_TASK_VISIBILITY_INTERNAL,
            DONNA_ARG_TYPE_NOTHING);

    i = -1;
    arg_type[++i] = DONNA_ARG_TYPE_STRING | DONNA_ARG_IS_OPTIONAL;
    add_command (register_save_all, ++i, DONNA_TASK_VISIBILITY_INTERNAL,
            DONNA_ARG_TYPE_NOTHING);

    i = -1;
    arg_type[++i] = DONNA_ARG_TYPE_STRING | DONNA_ARG_IS_OPTIONAL;
    arg_type[++i] = DONNA_ARG_TYPE_STRING;
    arg_type[++i] = DONNA_ARG_TYPE_NODE | DONNA_ARG_IS_ARRAY;
    add_command (register_set, ++i, DONNA_TASK_VISIBILITY_INTERNAL,
            DONNA_ARG_TYPE_NODE);

    i = -1;
    arg_type[++i] = DONNA_ARG_TYPE_STRING | DONNA_ARG_IS_OPTIONAL;
    arg_type[++i] = DONNA_ARG_TYPE_STRING;
    add_command (register_set_type, ++i, DONNA_TASK_VISIBILITY_INTERNAL_FAST,
            DONNA_ARG_TYPE_NOTHING);

    g_object_unref (pc);
}
#undef add_command
