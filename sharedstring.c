
#include <glib-object.h>
#include <gobject/gvaluecollector.h>
#include "sharedstring.h"

/**
 * SECTION:SharedString
 * @Short_description: A type to hold/share a string
 *
 * When using strings and needing to share them, especially in multi-threaded
 * app, strings are usually strdup-ed. To avoid doing lots of strdup/free all
 * over the place, we use #DonnaSharedString to store the strings.
 *
 * A #DonnaSharedString is just a string (gchar *) with a reference count. This
 * allows to simply take a ref when sharing it, and when the caller is done a
 * simple unref is enough. No need to strdup/free the string, and if it changes
 * meanwhile, the owner will just unref the shared string and make a new one.
 * Once the caller unrefs the shared string, the ref count drops to zero and
 * memory will be freed, as usual with ref-counted objects.
 *
 * This should be registered as a fundamental type - using
 * donna_shared_string_register() - and can then be used inside #GValue (so as
 * properties, in signals, etc) instead of G_TYPE_STRING
 *
 * A shared string can be created using donna_shared_string_new_take() to take
 * ownership of the strings, donna_shared_string_new_dup() to duplicate it, or
 * donna_shared_string_new_printf() to create a new string using a printf-like
 * format.
 *
 * References are added/removed using donna_shared_string_ref() and
 * donna_shared_string_unref() respectively.
 *
 * It is important to note that while shared strings are safe to use in a
 * multi-threaded application, they are not meant to be updated from multiple
 * threads. Changing the value of a shared string should only be done by the
 * sole "owner" of the string.
 *
 * This idea is to share a string as a read-only string only, this is why one
 * can easilly access the string using donna_shared_string() which will then
 * return the string, typecasted as a const gchar *
 *
 * To change the string you can use donna_shared_string_update_take(),
 * donna_shared_string_update_dup() or donna_shared_string_update_printf() that
 * will change the string in the #DonnaSharedString if we have the only
 * reference, else it will unref it and create/return a new one.
 * In multi-threaded application, you must ensure proper locking is done (much
 * like when using e.g. #GHashTable and such).
 *
 * DONNA_TYPE_SHARED_STRING can be used as type in e.g. #GValue, and there are a
 * few helper functions to use shared strings in GValues.
 *
 * donna_g_value_new_shared_string_take() and
 * donna_g_value_new_shared_string_dup() will create a new #GValue holding a new
 * #DonnaSharedString having taken resp. duplicated the given string.
 *
 * If the #GValue already exists, use donna_g_value_set_shared_string() and
 * donna_g_value_take_shared_string() to do the same.
 * To get the #DonnaSharedString from a #GValue use
 * donna_g_value_dup_shared_string() or donna_g_value_get_shared_string()
 * whether you want to take a reference on it or not, respectively.
 *
 * You can also directly access the string (as const gchar *) from the #GValue
 * using donna_g_value_get_shared_string_const_string()
 */

/**
 * donna_shared_string_ref:
 * @ss: The shared string to add a reference to
 *
 * Adds a reference to the shared string.
 *
 * Returns: The shared string
 */
DonnaSharedString *
donna_shared_string_ref (DonnaSharedString *ss)
{
    g_return_val_if_fail (ss != NULL, NULL);
    g_atomic_int_inc (&ss->ref_count);
    return ss;
}

/**
 * donna_shared_string_unref:
 * @ss: The shared string to remove a reference from
 *
 * Removes a reference from the shared string. If the reference count drops to
 * zero, memory will be freed.
 */
void
donna_shared_string_unref (DonnaSharedString *ss)
{
    g_return_if_fail (ss != NULL);

    if (g_atomic_int_dec_and_test (&ss->ref_count))
    {
        g_free (ss->string);
        g_slice_free (DonnaSharedString, ss);
    }
}

static inline DonnaSharedString *
update_or_new (DonnaSharedString *ss, gpointer string, gboolean need_dup)
{
    if (!ss || g_atomic_int_get (&ss->ref_count) > 1)
    {
        if (ss)
            donna_shared_string_unref (ss);
        ss = g_slice_new0 (DonnaSharedString);
        ss->ref_count = 1;
    }

    g_free (ss->string);
    if (need_dup)
        ss->string = g_strdup (string);
    else
        ss->string = string;

    return ss;
}

/**
 * donna_shared_string_update_take:
 * @ss: (allow-none): The shared string to update, or %NULL to create a new one
 * @string: the string to take ownership in the shared string
 *
 * Updates the shared string or creates a new one, taking ownership of the given
 * string.
 * A new shared string will be returned if another reference is taken on @ss.
 *
 * Returns: The (new) shared string
 */
DonnaSharedString *
donna_shared_string_update_take (DonnaSharedString  *ss,
                                 gchar              *string)
{
    return update_or_new (ss, (gpointer) string, FALSE);
}

/**
 * donna_shared_string_update_dup:
 * @ss: (allow-none): The shared string to update, or %NULL to create a new one
 * @string: the string to duplicate in the shared string
 *
 * Updates the shared string or creates a new one, duplicating the given string.
 * A new shared string will be returned if another reference is taken on @ss.
 *
 * Returns: The (new) shared string
 */
DonnaSharedString *
donna_shared_string_update_dup (DonnaSharedString  *ss,
                                const gchar        *string)
{
    return update_or_new (ss, (gpointer) string, TRUE);
}

/**
 * donna_shared_string_update_printf:
 * @ss: (allow-none): The shared string to update, or %NULL to create a new one
 * @fmt: the format to create a new shared string
 *
 * Updates the shared string or creates a new one, using the printf-format and
 * parameters given.
 * A new shared string will be returned if another reference is taken on @ss.
 *
 * Returns: The (new) shared string
 */
DonnaSharedString *
donna_shared_string_update_printf (DonnaSharedString    *ss,
                                   const gchar          *fmt,
                                   ...)
{
    va_list va_args;
    gchar *s;

    va_start (va_args, fmt);
    ss = update_or_new (ss, (gpointer) g_strdup_vprintf (fmt, va_args), FALSE);
    va_end (va_args);
    return ss;
}

/* Fundamental type */

static void
shared_string_init (GValue *value)
{
    value->data[0].v_pointer = NULL;
}

static void
shared_string_free (GValue *value)
{
    if (value->data[0].v_pointer)
        donna_shared_string_unref (value->data[0].v_pointer);
}

static void
shared_string_copy (const GValue *src, GValue *dest)
{
    dest->data[0].v_pointer = donna_shared_string_ref (src->data[0].v_pointer);
}

static gchar *
shared_string_collect (GValue        *value,
                      guint          n_collect_values,
                      GTypeCValue   *collect_values,
                      guint          collect_flags)
{
    if (!collect_values[0].v_pointer)
        value->data[0].v_pointer = NULL;
    else
        value->data[0].v_pointer = donna_shared_string_ref (
                collect_values[0].v_pointer);
    return NULL;
}

static gchar *
shared_string_lcopy (const GValue    *value,
                    guint            n_collect_values,
                    GTypeCValue     *collect_values,
                    guint            collect_flags)
{
    DonnaSharedString **ss = collect_values[0].v_pointer;

    if (!ss)
        return g_strdup_printf ("value location for '%s' passed as NULL",
                G_VALUE_TYPE_NAME (value));

    if (!value->data[0].v_pointer)
        *ss = NULL;
    else if (collect_flags & G_VALUE_NOCOPY_CONTENTS)
        *ss = value->data[0].v_pointer;
    else
        *ss = donna_shared_string_ref (value->data[0].v_pointer);
    return NULL;
}

static gpointer
shared_string_peek (const GValue *value)
{
    return value->data[0].v_pointer;
}

GType
donna_shared_string_get_type (void)
{
    static volatile GType type = 0;
    if (g_once_init_enter (&type))
        g_once_init_leave (&type, g_type_fundamental_next ());
    return type;
}

/**
 * donna_shared_string_register:
 *
 * Registers #DonnaSharedString as a new fundamental type:
 * DONNA_TYPE_SHARED_STRING
 */
void
donna_shared_string_register (void)
{
    GType type;
    GTypeInfo info =
    {
        0,              /* class_size */
        NULL,           /* base_init */
        NULL,           /* base_destroy */
        NULL,           /* class_init */
        NULL,           /* class_destroy */
        NULL,           /* class_data */
        0,              /* instance_size */
        0,              /* n_preallocs */
        NULL,           /* instance_init */
        NULL,           /* value_table */
    };
    const GTypeFundamentalInfo finfo = { G_TYPE_FLAG_DERIVABLE, };
    static const GTypeValueTable value_table =
    {
        shared_string_init,     /* value_init */
        shared_string_free,     /* value_free */
        shared_string_copy,     /* value_copy */
        shared_string_peek,     /* value_peek_pointer */
        "p",                    /* collect_format */
        shared_string_collect,  /* collect_value */
        "p",                    /* lcopy_format */
        shared_string_lcopy,    /* lcopy_value */
    };
    info.value_table = &value_table;
    type = g_type_register_fundamental (DONNA_TYPE_SHARED_STRING,
            g_intern_static_string ("SharedString"), &info, &finfo, 0);
    g_assert (type == DONNA_TYPE_SHARED_STRING);
}

/**
 * donna_g_value_set_shared_string:
 * @value: #GValue to set the shared string into
 * @ss: Shared string to set into @value
 *
 * Sets the shared string in @value (must be properly initialized), taking a new
 * reference on it.
 */
void
donna_g_value_set_shared_string (GValue             *value,
                                 DonnaSharedString  *ss)
{
    g_return_if_fail (DONNA_G_VALUE_HOLDS_SHARED_STRING (value));
    value->data[0].v_pointer = donna_shared_string_ref (ss);
}

/**
 * donna_g_value_take_shared_string:
 * @value: #GValue to set shared string into
 * @ss: Shared string to set into @value
 *
 * Sets the shared string in @value taking ownership of the caller's reference
 * (i.e. no new reference is taken).
 */
void
donna_g_value_take_shared_string (GValue             *value,
                                  DonnaSharedString  *ss)
{
    g_return_if_fail (DONNA_G_VALUE_HOLDS_SHARED_STRING (value));
    value->data[0].v_pointer = ss;
}

/**
 * donna_g_value_get_shared_string:
 * @value: The #GValue to get the shared string from
 *
 * Gets the #DonnaSharedString from @value (no reference is taken)
 *
 * Returns: (transfer none): The #DonnaSharedString from @value
 */
DonnaSharedString *
donna_g_value_get_shared_string (const GValue       *value)
{
    g_return_val_if_fail (DONNA_G_VALUE_HOLDS_SHARED_STRING (value), NULL);
    return value->data[0].v_pointer;
}

/**
 * donna_g_value_dup_shared_string:
 * @value: The #GValue to get the shared string from
 *
 * Gets the #DonnaSharedString from @value, taking a new reference on it
 *
 * Returns: (transfer full): The #DonnaSharedString from @value
 */
DonnaSharedString *
donna_g_value_dup_shared_string (const GValue       *value)
{
    g_return_val_if_fail (DONNA_G_VALUE_HOLDS_SHARED_STRING (value), NULL);
    return donna_shared_string_ref (value->data[0].v_pointer);
}

/*
 * donna_g_value_get_shared_string_const_string:
 * @value: #GValue to get the string from the contained #DonnaSharedString
 *
 * Gets the string from the #DonnaSharedString in @value
 *
 * Returns: The string from the #DonnaSharedString in @value
 */
const gchar *
donna_g_value_get_shared_string_const_string (const GValue *value)
{
    g_return_val_if_fail (DONNA_G_VALUE_HOLDS_SHARED_STRING (value), NULL);
    return (const gchar *) ((DonnaSharedString *) value->data[0].v_pointer)->string;
}
