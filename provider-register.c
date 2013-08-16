
#include <gtk/gtk.h>
#include "provider-register.h"
#include "provider-command.h"
#include "provider.h"
#include "node.h"
#include "app.h"
#include "command.h"
#include "macros.h"
#include "debug.h"

static const gchar const * reg_default   = "_";
static const gchar const * reg_clipboard = "+";

struct reg
{
    gchar *name;
    DonnaRegisterType type;
    GHashTable *hashtable; /* hashmap of full locations (keys == values) */
};

struct _DonnaProviderRegisterPrivate
{
    GMutex mutex;
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
                                                         GError            **error);
static DonnaTaskState  provider_register_io             (DonnaProviderBase  *provider,
                                                         DonnaTask          *task,
                                                         DonnaIoType         type,
                                                         gboolean            is_source,
                                                         GPtrArray          *sources,
                                                         DonnaNode          *dest);
static DonnaTaskState   provider_register_new_child     (DonnaProviderBase  *provider,
                                                         DonnaTask          *task,
                                                         DonnaNode          *parent,
                                                         DonnaNodeType       type,
                                                         const gchar        *name);

static void
provider_register_provider_init (DonnaProviderInterface *interface)
{
    interface->get_domain   = provider_register_get_domain;
    interface->get_flags    = provider_register_get_flags;
}

static void
donna_provider_register_class_init (DonnaProviderRegisterClass *klass)
{
    DonnaProviderBaseClass *pb_class;
    GObjectClass *o_class;

    o_class = (GObjectClass *) klass;
    o_class->constructed    = provider_register_contructed;

    pb_class = (DonnaProviderBaseClass *) klass;
    pb_class->new_node      = provider_register_new_node;
    pb_class->has_children  = provider_register_has_children;
    pb_class->get_children  = provider_register_get_children;
    pb_class->trigger_node  = provider_register_trigger_node;
    pb_class->support_io    = provider_register_support_io;
    pb_class->io            = provider_register_io;
    pb_class->new_child     = provider_register_new_child;

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
    g_mutex_init (&priv->mutex);
}

G_DEFINE_TYPE_WITH_CODE (DonnaProviderRegister, donna_provider_register,
        DONNA_TYPE_PROVIDER_BASE,
        G_IMPLEMENT_INTERFACE (DONNA_TYPE_PROVIDER, provider_register_provider_init)
        )

/* internals */

static void
free_register (struct reg *reg)
{
    if (reg->name != reg_default && reg->name != reg_clipboard)
        g_free (reg->name);
    g_hash_table_unref (reg->hashtable);
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

    return reg;
}

static gboolean
drop_register (DonnaProviderRegister *pr, const gchar *name, gboolean lock)
{
    DonnaProviderRegisterPrivate *priv = pr->priv;
    DonnaNode *node;
    struct reg *reg;
    GSList *l;
    GSList *prev;
    gboolean ret = FALSE;

    if (lock)
        g_mutex_lock (&priv->mutex);
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
    if (lock)
        g_mutex_unlock (&priv->mutex);

    if (ret)
    {
        if ((node = get_node_for (pr, name)))
        {
            /* default/clipboard should always exists */
            if (reg->name == reg_default || reg->name == reg_clipboard)
            {
                GPtrArray *arr = g_ptr_array_new ();
                donna_provider_node_children ((DonnaProvider *) pr, node,
                        DONNA_NODE_ITEM | DONNA_NODE_CONTAINER, arr);
                g_ptr_array_unref (arr);
            }
            else
                donna_provider_node_removed ((DonnaProvider *) pr, node);
            g_object_unref (node);
        }
        free_register (reg);
    }

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
    GHashTableIter iter;
    gpointer key;

    g_mutex_lock (&priv->mutex);
    reg = get_register (priv->registers, reg_clipboard);
    if (G_UNLIKELY (!reg))
    {
        g_mutex_unlock (&priv->mutex);
        g_warning ("Provider 'register': clipboard_get() for CLIPBOARD triggered while register '+' doesn't exist");
        return;
    }

    str = g_string_new (NULL);
    if (info < 3)
        g_string_append_printf (str, "%s\n",
                (reg->type == DONNA_REGISTER_CUT) ? "cut" : "copy");

    g_hash_table_iter_init (&iter, reg->hashtable);
    while (g_hash_table_iter_next (&iter, &key, NULL))
    {
        GError *err = NULL;
        gchar *s;

        s = g_filename_to_uri ((gchar *) key, NULL, &err);
        if (G_UNLIKELY (!s))
        {
            g_warning ("Provider 'register': clipboard_get() for CLIPBOARD: Failed to convert '%s' to URI: %s",
                    (gchar *) key, err->message);
            g_clear_error (&err);
        }
        else
        {
            g_string_append (str, s);
            g_string_append_c (str, '\n');
            g_free (s);
        }
    }
    g_mutex_unlock (&priv->mutex);

    gtk_selection_data_set (sd, (info == 1) ? atom_gnome
            : ((info == 2) ? atom_kde : atom_uris), 8, str->str, str->len);
    g_string_free (str, TRUE);
}

static void
clipboard_clear (GtkClipboard *clipboard, DonnaProviderRegister *pr)
{
    drop_register (pr, reg_clipboard, TRUE);
}

static DonnaTaskState
task_take_clipboard_ownership (DonnaTask *task, gpointer _data)
{
    struct {
        DonnaProviderRegister *pr;
        gboolean clear;
    } *data = _data;
    GtkClipboard *clipboard;
    GtkTargetEntry targets[] = {
        { _ATOM_GNOME, 0, 1 },
        { _ATOM_KDE, 0, 2 },
        { _ATOM_URIS, 0, 3 },
    };
    gboolean ret;

    clipboard = gtk_clipboard_get (GDK_SELECTION_CLIPBOARD);
    if (G_UNLIKELY (!clipboard))
        return DONNA_TASK_FAILED;
    ret = gtk_clipboard_set_with_owner (clipboard, targets, 3,
            (GtkClipboardGetFunc) clipboard_get,
            (GtkClipboardClearFunc) clipboard_clear,
            (GObject *) data->pr);
    if (ret && data->clear)
        gtk_clipboard_clear (clipboard);

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
    donna_task_set_can_block (g_object_ref_sink (task));
    donna_app_run_task (((DonnaProviderBase *) pr)->app, task);
    donna_task_wait_for_it (task);
    ret = donna_task_get_state (task) == DONNA_TASK_DONE;
    g_object_unref (task);
    return ret;
}

static DonnaTaskState
task_get_from_clipboard (DonnaTask *task, gpointer _data)
{
    struct {
        GHashTable **hashtable;
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
        g_set_error (data->error, DONNA_PROVIDER_REGISTER_ERROR,
                DONNA_PROVIDER_REGISTER_ERROR_EMPTY,
                "No files available on CLIPBOARD");
        return DONNA_TASK_FAILED;
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
        g_set_error (data->error, DONNA_PROVIDER_REGISTER_ERROR,
                DONNA_PROVIDER_REGISTER_ERROR_EMPTY,
                "No supported format for files available in CLIPBOARD");
        return DONNA_TASK_FAILED;
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

        g_hash_table_add (*data->hashtable, filename);
        s = e + 1;
    }
    gtk_selection_data_free (sd);
    return DONNA_TASK_DONE;
}

static gboolean
get_from_clipboard (DonnaApp             *app,
                    GHashTable          **hashtable,
                    DonnaRegisterType    *type,
                    GString             **str,
                    GError              **error)
{
    DonnaTask *task;
    struct {
        GHashTable **hashtable;
        DonnaRegisterType *type;
        GString **str;
        GError **error;
    } data = { hashtable, type, str, error };
    gboolean ret;

    g_assert (hashtable != NULL && *hashtable != NULL);
    g_assert (type != NULL);

    task = donna_task_new (task_get_from_clipboard, &data, NULL);
    donna_task_set_visibility (task, DONNA_TASK_VISIBILITY_INTERNAL_GUI);
    donna_task_set_can_block (g_object_ref_sink (task));
    donna_app_run_task (app, task);
    donna_task_wait_for_it (task);
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
        return strchr (*name, '/') == NULL;

    g_set_error (error, DONNA_PROVIDER_ERROR,
            DONNA_PROVIDER_ERROR_LOCATION_NOT_FOUND,
            "Invalid register name: '%s'", *name);
    return FALSE;
}

static inline DonnaNode *
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

static gboolean
register_drop (DonnaProviderRegister *pr,
               const gchar           *name,
               GError               **error)
{
    gboolean ret = FALSE;

    if (*name == *reg_clipboard)
        ret = take_clipboard_ownership (pr, TRUE);
    else
        ret = drop_register (pr, name, TRUE);

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
    /* if already in there, replaces old key with s & frees the old key */
    g_hash_table_add (reg->hashtable, s);
    return added;
}

/* assumes lock */
static inline void
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
              GError               **error)
{
    DonnaProviderRegisterPrivate *priv = pr->priv;
    DonnaProvider *pfs;
    DonnaNode *node_root = NULL;
    DonnaNode *node;
    GPtrArray *arr;
    struct reg *reg;
    gboolean is_clipboard;
    guint reg_type = -1;
    guint i;

    is_clipboard = *name == *reg_clipboard;
    if (is_clipboard)
        pfs = donna_app_get_provider (((DonnaProviderBase *) pr)->app, "fs");

    g_mutex_lock (&priv->mutex);
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
    }

    arr = g_ptr_array_sized_new (nodes->len);
    for (i = 0; i < nodes->len; ++i)
        if (!is_clipboard || donna_node_peek_provider (nodes->pdata[i]) == pfs)
            if (add_node_to_reg (reg, nodes->pdata[i], is_clipboard))
                g_ptr_array_add (arr, nodes->pdata[i]);

    g_mutex_unlock (&priv->mutex);

    if (is_clipboard)
    {
        take_clipboard_ownership (pr, FALSE);
        g_object_unref (pfs);
    }

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
        donna_provider_node_children ((DonnaProvider *) pr, node,
                DONNA_NODE_ITEM | DONNA_NODE_CONTAINER, arr);
        g_object_unref (node);
    }
    g_ptr_array_unref (arr);

    return TRUE;
}

static gboolean
register_add_nodes (DonnaProviderRegister   *pr,
                    const gchar             *name,
                    GPtrArray               *nodes,
                    GError                 **error)
{
    DonnaProviderRegisterPrivate *priv = pr->priv;
    DonnaApp *app = ((DonnaProviderBase *) pr)->app;
    DonnaProvider *pfs;
    DonnaNode *node_root = NULL;
    DonnaNode *node;
    GPtrArray *arr;
    struct reg *reg;
    gboolean is_clipboard;
    guint i;

    is_clipboard = *name == *reg_clipboard;
    if (is_clipboard)
        pfs = donna_app_get_provider (((DonnaProviderBase *) pr)->app, "fs");

    g_mutex_lock (&priv->mutex);
    reg = get_register (priv->registers, name);

    if (!reg)
    {
        reg = new_register (name, DONNA_REGISTER_UNKNOWN);
        if (is_clipboard)
        {
            GString *str = NULL;

            if (!get_from_clipboard (app, &reg->hashtable, &reg->type, &str, error))
            {
                g_mutex_unlock (&priv->mutex);
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

    arr = g_ptr_array_sized_new (nodes->len);
    for (i = 0; i < nodes->len; ++i)
        if (!is_clipboard || donna_node_peek_provider (nodes->pdata[i]) == pfs)
            if (add_node_to_reg (reg, nodes->pdata[i], is_clipboard))
                g_ptr_array_add (arr, nodes->pdata[i]);

    g_mutex_unlock (&priv->mutex);

    if (is_clipboard)
        g_object_unref (pfs);

    if (node_root)
    {
        donna_provider_node_new_child ((DonnaProvider *) pr, node_root, node);
        g_object_unref (node_root);
        g_object_unref (node);
    }
    else if ((node = get_node_for (pr, name)))
    {
        for (i = 0; i < arr->len; ++i)
            donna_provider_node_new_child ((DonnaProvider *) pr, node, arr->pdata[i]);
        g_object_unref (node);
    }
    g_ptr_array_unref (arr);

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
    g_mutex_lock (&priv->mutex);
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
            if (!get_from_clipboard (app, &reg->hashtable, &reg->type, &str, error))
            {
                g_mutex_unlock (&priv->mutex);
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
            g_mutex_unlock (&priv->mutex);
            g_set_error (error, DONNA_PROVIDER_ERROR,
                    DONNA_PROVIDER_ERROR_LOCATION_NOT_FOUND,
                    "Cannot set type of register '%s', it doesn't exist.", name);
            return FALSE;
        }
    }

    reg->type = type;
    g_mutex_unlock (&priv->mutex);

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
    GHashTableIter iter;
    gpointer key;
    GString *str = NULL;
    gboolean do_drop;

    is_clipboard = *name == *reg_clipboard;
    g_mutex_lock (&priv->mutex);
    reg = get_register (priv->registers, name);
    if (!reg)
    {
        /* reg_default must always exists */
        if (*name == *reg_default)
        {
            g_mutex_unlock (&priv->mutex);
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
                || !get_from_clipboard (app, &hashtable, &reg_type, &str, error))
        {
            if (is_clipboard)
                g_hash_table_unref (hashtable);
            g_mutex_unlock (&priv->mutex);
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
        reg_type = reg->type;
    }

    if (type)
        *type = reg_type;

    if (!nodes)
    {
        g_mutex_unlock (&priv->mutex);
        if (!reg)
            g_hash_table_unref (hashtable);
        return TRUE;
    }

    if (is_clipboard)
        pfs = donna_app_get_provider (((DonnaProviderBase *) pr)->app, "fs");

    *nodes = g_ptr_array_new_full (g_hash_table_size (hashtable), g_object_unref);
    g_hash_table_iter_init (&iter, hashtable);
    while (g_hash_table_iter_next (&iter, &key, NULL))
    {
        DonnaTask *task;

        if (pfs)
            task = donna_provider_get_node_task (pfs, (gchar *) key, NULL);
        else
            task = donna_app_get_node_task (((DonnaProviderBase *) pr)->app,
                    (gchar *) key);

        if (!task)
        {
            if (!str)
                str = g_string_new (NULL);

            g_string_append_printf (str, "\n- Failed to get node for '%s' (couldn't get task)",
                    (gchar *) key);
            continue;
        }

        donna_task_set_can_block (g_object_ref_sink (task));
        donna_app_run_task (((DonnaProviderBase *) pr)->app, task);
        donna_task_wait_for_it (task);

        if (donna_task_get_state (task) == DONNA_TASK_DONE)
            g_ptr_array_add (*nodes,
                    g_value_dup_object (donna_task_get_return_value (task)));
        else if (error)
        {
            const GError *e = donna_task_get_error (task);

            if (!str)
                str = g_string_new (NULL);

            if (e)
                g_string_append_printf (str, "\n- Failed to get node for '%s': %s",
                        (gchar *) key, e->message);
            else
                g_string_append_printf (str, "\n- Failed to get node for '%s'",
                        (gchar *) key);
        }

        g_object_unref (task);
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
        if (do_drop)
            drop_register (pr, name, FALSE);
        g_mutex_unlock (&priv->mutex);
    }
    else
    {
        g_mutex_unlock (&priv->mutex);
        g_hash_table_unref (hashtable);

        if (do_drop)
            take_clipboard_ownership (pr, TRUE);
    }

    return TRUE;
}

static gboolean
register_load (DonnaProviderRegister    *pr,
               const gchar              *name,
               const gchar              *file,
               DonnaRegisterFile         file_type,
               GError                  **error)
{
    DonnaProviderRegisterPrivate *priv = pr->priv;
    DonnaNode *node_root = NULL;
    DonnaNode *node;
    gboolean is_clipboard;
    GPtrArray *arr;
    struct reg *new_reg;
    struct reg *reg;
    guint reg_type = -1;
    gchar *data;
    gchar *s;
    gchar *e;

    is_clipboard = *name == *reg_clipboard;

    if (!g_file_get_contents (file, &data, NULL, error))
    {
        g_prefix_error (error, "Failed to load register '%s' from '%s': ",
                name, file);
        return FALSE;
    }

    new_reg = new_register (name, DONNA_REGISTER_UNKNOWN);

    if (streqn (data, "cut\n", 4))
    {
        new_reg->type = DONNA_REGISTER_CUT;
        s = data + 4;
    }
    else if (streqn (data, "copy\n", 5))
    {
        new_reg->type = DONNA_REGISTER_COPY;
        s = data + 5;
    }
    else
    {
        g_set_error (error, DONNA_PROVIDER_REGISTER_ERROR,
                DONNA_PROVIDER_REGISTER_ERROR_INVALID_FORMAT,
                "Failed to load register '%s' from '%s': invalid file format",
                name, file);
        g_free (data);
        free_register (new_reg);
        return FALSE;
    }

    arr = g_ptr_array_new ();
    while ((e = strchr (s, '\n')))
    {
        gchar *new;
        *e = '\0';

        if (file_type == DONNA_REGISTER_FILE_NODES)
        {
            if (!is_clipboard)
                new = g_strdup (s);
            else if (streqn (s, "fs:", 3))
                new = g_strdup (s + 3);
        }
        else if (file_type == DONNA_REGISTER_FILE_FILE)
        {
            if (!is_clipboard)
                new =  g_strdup_printf ("fs:%s", s);
            else
                new = g_strdup (s);
        }
        else /* DONNA_REGISTER_FILE_URIS */
        {
            gchar *f = g_filename_from_uri (s, NULL, NULL);
            if (f)
            {
                if (!is_clipboard)
                {
                    new = g_strdup_printf ("fs:%s", f);
                    g_free (f);
                }
                else
                    new = f;
            }
        }

        g_hash_table_add (new_reg->hashtable, new);
        g_ptr_array_add (arr, new);

        s = e + 1;
    }
    g_free (data);

    g_mutex_lock (&priv->mutex);
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

    g_mutex_unlock (&priv->mutex);

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
        donna_provider_node_children ((DonnaProvider *) pr, node,
                DONNA_NODE_ITEM | DONNA_NODE_CONTAINER, arr);
        g_object_unref (node);
    }
    g_ptr_array_unref (arr);

    if (is_clipboard)
        take_clipboard_ownership (pr, FALSE);

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
    DonnaApp *app = ((DonnaProviderBase *) pr)->app;
    struct reg *reg = NULL;
    gboolean is_clipboard;
    DonnaRegisterType reg_type;
    GHashTable *hashtable;
    GHashTableIter iter;
    gpointer key;
    GString *str;

    is_clipboard = *name == *reg_clipboard;

    g_mutex_lock (&priv->mutex);
    reg = get_register (priv->registers, name);
    if (!reg)
    {
        /* reg_default must always exists */
        if (*name == *reg_default)
        {
            g_mutex_unlock (&priv->mutex);
            str = g_string_new ("copy\n");
            goto write;
        }
        /* same with reg_clipboard, but we import its content */
        else if (is_clipboard)
            hashtable = g_hash_table_new_full (g_str_hash, g_str_equal, g_free, NULL);
        if (!is_clipboard
                || !get_from_clipboard (app, &hashtable, &reg_type, &str, error))
        {
            if (is_clipboard)
                g_hash_table_unref (hashtable);
            g_mutex_unlock (&priv->mutex);
            if (*error)
                g_prefix_error (error, "Cannot save register '%s': ", name);
            else
                g_set_error (error, DONNA_PROVIDER_ERROR,
                        DONNA_PROVIDER_ERROR_LOCATION_NOT_FOUND,
                        "Cannot save register '%s', it doesn't exist.", name);
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
        reg_type = reg->type;
    }

    str = g_string_new ((reg_type == DONNA_REGISTER_CUT) ? "cut\n" : "copy\n");
    g_hash_table_iter_init (&iter, hashtable);
    while (g_hash_table_iter_next (&iter, &key, NULL))
    {
        if (reg)
        {
            if (file_type == DONNA_REGISTER_FILE_NODES)
            {
                g_string_append (str, (gchar *) key);
                g_string_append_c (str, '\n');
            }
            /* other file_types require nodes to be in fs */
            else if (streqn ((gchar *) key, "fs:", 3))
            {
                if (file_type == DONNA_REGISTER_FILE_FILE)
                    g_string_append (str, (gchar *) key + 3);
                else /* DONNA_REGISTER_FILE_URIS */
                {
                    gchar *s;

                    s = g_filename_to_uri ((gchar *) key + 3, NULL, NULL);
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
                g_string_append (str, (gchar *) key);
            }
            else if (file_type == DONNA_REGISTER_FILE_FILE)
                g_string_append (str, (gchar *) key);
            else /* DONNA_REGISTER_FILE_URIS */
            {
                gchar *s;

                s = g_filename_to_uri ((gchar *) key, NULL, NULL);
                if (!s)
                    continue;
                g_string_append (str, s);
                g_free (s);
            }
            g_string_append_c (str, '\n');
        }
    }

    g_mutex_unlock (&priv->mutex);
    if (!reg)
        g_hash_table_unref (hashtable);

write:
    if (!g_file_set_contents (file, str->str, str->len, error))
    {
        g_prefix_error (error, "Failed to save register '%s' to '%s': ",
                name, file);
        g_string_free (str, TRUE);
        return FALSE;
    }
    g_string_free (str, TRUE);

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
            (refresher_fn) gtk_true, NULL, name, 0);

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
                    G_TYPE_UINT, &v, (refresher_fn) gtk_true, NULL, error)))
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

static DonnaTaskState
provider_register_new_node (DonnaProviderBase  *_provider,
                            DonnaTask          *task,
                            const gchar        *location)
{
    DonnaProviderRegisterPrivate *priv = ((DonnaProviderRegister *) _provider)->priv;
    DonnaProviderBaseClass *klass;
    GError *err = NULL;
    struct reg *reg;
    struct reg r = { .type = DONNA_REGISTER_UNKNOWN };
    DonnaNode *node;
    DonnaNode *n;
    GValue *value;

    if (streq (location, "/"))
    {
        node = donna_node_new ((DonnaProvider *) _provider, location,
                DONNA_NODE_CONTAINER, NULL, (refresher_fn) gtk_true, NULL,
                "Registers", 0);
        if (G_UNLIKELY (!node))
        {
            donna_task_set_error (task, DONNA_PROVIDER_ERROR,
                    DONNA_PROVIDER_ERROR_OTHER,
                    "Provider 'register': Unable to create a new node");
            return DONNA_TASK_FAILED;
        }

        goto cache_and_return;
    }

    if (!is_valid_register_name (&location, &err))
    {
        donna_task_take_error (task, err);
        return DONNA_TASK_FAILED;
    }

    g_mutex_lock (&priv->mutex);
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
            g_mutex_unlock (&priv->mutex);
            donna_task_set_error (task, DONNA_PROVIDER_ERROR,
                    DONNA_PROVIDER_ERROR_LOCATION_NOT_FOUND,
                    "Register '%s' doesn't exist", location);
            return DONNA_TASK_FAILED;
        }
    }

    node = new_node_for_reg ((DonnaProvider *) _provider, reg, &err);
    if (G_UNLIKELY (!node))
    {
        g_mutex_unlock (&priv->mutex);
        donna_task_take_error (task, err);
        return DONNA_TASK_FAILED;
    }
    g_mutex_unlock (&priv->mutex);

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

    value = donna_task_grab_return_value (task);
    g_value_init (value, DONNA_TYPE_NODE);
    g_value_take_object (value, node);
    donna_task_release_return_value (task);

    return DONNA_TASK_DONE;
}

static DonnaTaskState
provider_register_has_children (DonnaProviderBase  *provider,
                                DonnaTask          *task,
                                DonnaNode          *node,
                                DonnaNodeType       node_types)
{
    GValue *value;

    value = donna_task_grab_return_value (task);
    g_value_init (value, G_TYPE_BOOLEAN);
    g_value_set_boolean (value, TRUE);
    donna_task_release_return_value (task);

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

        g_mutex_lock (&priv->mutex);
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
                    g_mutex_unlock (&priv->mutex);
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
            n = new_node_for_reg ((DonnaProvider *) _provider, &r, &err);
            if (G_UNLIKELY (!n))
            {
                klass->unlock_nodes (_provider);
                g_mutex_unlock (&priv->mutex);
                donna_task_take_error (task, err);
                g_ptr_array_unref (nodes);
                return DONNA_TASK_FAILED;
            }
            klass->add_node_to_cache (_provider, n);
            g_ptr_array_add (nodes, n);
        }
        if (!(has & HAS_CLIPBOARD))
        {
            struct reg r = {
                .name = (gchar *) reg_clipboard,
                .type = DONNA_REGISTER_UNKNOWN,
            };
            n = new_node_for_reg ((DonnaProvider *) _provider, &r, &err);
            if (G_UNLIKELY (!n))
            {
                klass->unlock_nodes (_provider);
                g_mutex_unlock (&priv->mutex);
                donna_task_take_error (task, err);
                g_ptr_array_unref (nodes);
                return DONNA_TASK_FAILED;
            }
            klass->add_node_to_cache (_provider, n);
            g_ptr_array_add (nodes, n);
        }

        klass->unlock_nodes (_provider);
        g_mutex_unlock (&priv->mutex);
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
provider_register_trigger_node (DonnaProviderBase  *provider,
                                DonnaTask          *task,
                                DonnaNode          *node)
{
    /* this should never be called, since all our nodes are CONTAINERs and thus
     * cannot get triggered */
    donna_task_set_error (task, DONNA_PROVIDER_ERROR,
            DONNA_PROVIDER_ERROR_NOT_SUPPORTED,
            "Provider 'register': trigger_node() not supported");
    gchar *fl = donna_node_get_full_location (node);
    g_warning ("Provider 'register': trigger_node() was called on '%s'", fl);
    g_free (fl);
    return DONNA_TASK_FAILED;
}

static gboolean
provider_register_support_io (DonnaProviderBase  *_provider,
                              DonnaIoType         type,
                              gboolean            is_source,
                              GPtrArray          *sources,
                              DonnaNode          *dest,
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
                "Provider 'register': destination node '%s' isn't in domain 'rehister'",
                fl);
        g_free (fl);
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
                      DonnaNode          *dest)
{
    GError *err = NULL;
    gchar *name;

    name = donna_node_get_location (dest);
    if (!register_add_nodes ((DonnaProviderRegister *) _provider, name, sources, &err))
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
    DonnaNode *node;
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

    g_mutex_lock (&priv->mutex);
    reg = get_register (priv->registers, name);
    if (reg)
    {
        g_mutex_unlock (&priv->mutex);
        donna_task_set_error (task, DONNA_PROVIDER_ERROR,
                DONNA_PROVIDER_ERROR_ALREADY_EXIST,
                "Provider 'register': Cannot create register '%s'; it already exists",
                name);
        return DONNA_TASK_FAILED;
    }

    reg = new_register (name, DONNA_REGISTER_UNKNOWN);
    add_reg_to_registers ((DonnaProviderRegister *) _provider, reg, TRUE,
            &node_root, &node, &err);
    g_mutex_unlock (&priv->mutex);

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

/* commands */

static DonnaTaskState
cmd_register_add_nodes (DonnaTask               *task,
                        DonnaApp                *app,
                        gpointer                *args,
                        DonnaProviderRegister   *pr)
{
    GError *err = NULL;
    const gchar *name = args[0]; /* opt */
    GPtrArray *nodes = args[1];

    if (!is_valid_register_name (&name, &err))
    {
        donna_task_take_error (task, err);
        return DONNA_TASK_FAILED;
    }

    if (!register_add_nodes (pr, name, nodes, &err))
    {
        donna_task_take_error (task, err);
        return DONNA_TASK_FAILED;
    }
    return DONNA_TASK_DONE;
}

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

static DonnaTaskState
cmd_register_get_nodes (DonnaTask               *task,
                        DonnaApp                *app,
                        gpointer                *args,
                        DonnaProviderRegister   *pr)
{
    GError *err = NULL;
    const gchar *name = args[0]; /* opt */
    gchar *drop = args[1];

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

    if (!register_load (pr, name, file, file_types[c], &err))
    {
        donna_task_take_error (task, err);
        return DONNA_TASK_FAILED;
    }

    return DONNA_TASK_DONE;
}

static DonnaTaskState
cmd_register_nodes_io (DonnaTask               *task,
                       DonnaApp                *app,
                       gpointer                *args,
                       DonnaProviderRegister   *pr)
{
    GError *err = NULL;
    const gchar *name = args[0]; /* opt */
    gchar *io_type = args[1]; /* opt */
    DonnaNode *dest = args[2]; /* opt */

    const gchar *c_io_type[] = { "auto", "copy", "move", "delete" };
    DonnaIoType io_types[] = { DONNA_IO_UNKNOWN, DONNA_IO_COPY, DONNA_IO_MOVE,
        DONNA_IO_DELETE };
    DonnaDropRegister drop;
    DonnaRegisterType reg_type;
    gint c_io;
    GPtrArray *nodes;

    if (!is_valid_register_name (&name, &err))
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

    if (!register_get_nodes (pr, name, drop, &reg_type, &nodes, &err))
    {
        donna_task_take_error (task, err);
        return DONNA_TASK_FAILED;
    }

    if (c_io == 0)
        c_io = (reg_type == DONNA_REGISTER_CUT) ? 2 /* MOVE */ : 1 /* COPY */;

    if (!donna_app_nodes_io (app, nodes, io_types[c_io], dest, &err))
    {
        donna_task_take_error (task, err);
        g_ptr_array_unref (nodes);
        return DONNA_TASK_FAILED;
    }
    g_ptr_array_unref (nodes);

    return DONNA_TASK_DONE;
}

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

    if (!register_set (pr, name, types[c], nodes, &err))
    {
        donna_task_take_error (task, err);
        return DONNA_TASK_FAILED;
    }

    return DONNA_TASK_DONE;
}

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

#define add_command(cmd_name, cmd_argc, cmd_visibility, cmd_return_value) \
if (G_UNLIKELY (!donna_provider_command_add_command (pc, #cmd_name, cmd_argc, \
            arg_type, cmd_return_value, cmd_visibility, \
            (command_fn) cmd_##cmd_name, object, NULL, &err))) \
{ \
    g_warning ("Provider 'register': Failed to add command '" #cmd_name "': %s", \
        err->message); \
    g_clear_error (&err); \
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
            DONNA_ARG_TYPE_NOTHING);

    i = -1;
    arg_type[++i] = DONNA_ARG_TYPE_STRING | DONNA_ARG_IS_OPTIONAL;
    add_command (register_drop, ++i, DONNA_TASK_VISIBILITY_INTERNAL_FAST,
            DONNA_ARG_TYPE_NOTHING);

    i = -1;
    arg_type[++i] = DONNA_ARG_TYPE_STRING | DONNA_ARG_IS_OPTIONAL;
    arg_type[++i] = DONNA_ARG_TYPE_STRING;
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
            DONNA_ARG_TYPE_NOTHING);

    i = -1;
    arg_type[++i] = DONNA_ARG_TYPE_STRING | DONNA_ARG_IS_OPTIONAL;
    arg_type[++i] = DONNA_ARG_TYPE_STRING | DONNA_ARG_IS_OPTIONAL;
    arg_type[++i] = DONNA_ARG_TYPE_NODE | DONNA_ARG_IS_OPTIONAL;
    add_command (register_nodes_io, ++i, DONNA_TASK_VISIBILITY_INTERNAL,
            DONNA_ARG_TYPE_NOTHING);

    i = -1;
    arg_type[++i] = DONNA_ARG_TYPE_STRING | DONNA_ARG_IS_OPTIONAL;
    arg_type[++i] = DONNA_ARG_TYPE_STRING;
    arg_type[++i] = DONNA_ARG_TYPE_STRING | DONNA_ARG_IS_OPTIONAL;
    add_command (register_save, ++i, DONNA_TASK_VISIBILITY_INTERNAL,
            DONNA_ARG_TYPE_NOTHING);

    i = -1;
    arg_type[++i] = DONNA_ARG_TYPE_STRING | DONNA_ARG_IS_OPTIONAL;
    arg_type[++i] = DONNA_ARG_TYPE_STRING;
    arg_type[++i] = DONNA_ARG_TYPE_NODE | DONNA_ARG_IS_ARRAY;
    add_command (register_set, ++i, DONNA_TASK_VISIBILITY_INTERNAL,
            DONNA_ARG_TYPE_NOTHING);

    i = -1;
    arg_type[++i] = DONNA_ARG_TYPE_STRING | DONNA_ARG_IS_OPTIONAL;
    arg_type[++i] = DONNA_ARG_TYPE_STRING;
    add_command (register_set_type, ++i, DONNA_TASK_VISIBILITY_INTERNAL_FAST,
            DONNA_ARG_TYPE_NOTHING);

    g_object_unref (pc);
}
#undef add_command