
#include <glib-object.h>
#include <gobject/gvaluecollector.h>
#include "sharedstring.h"

DonnaSharedString *
donna_shared_string_ref (DonnaSharedString *ss)
{
    g_return_val_if_fail (ss != NULL, NULL);
    g_atomic_int_inc (&ss->ref_count);
    return ss;
}

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

DonnaSharedString *
donna_shared_string_update_take (DonnaSharedString  *ss,
                                 gchar              *string)
{
    return update_or_new (ss, (gpointer) string, FALSE);
}

DonnaSharedString *
donna_shared_string_update_dup (DonnaSharedString  *ss,
                                const gchar        *string)
{
    return update_or_new (ss, (gpointer) string, TRUE);
}

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

void
donna_g_value_set_shared_string (GValue             *value,
                                 DonnaSharedString  *ss)
{
    g_return_if_fail (DONNA_G_VALUE_HOLDS_SHARED_STRING (value));
    value->data[0].v_pointer = donna_shared_string_ref (ss);
}

void
donna_g_value_take_shared_string (GValue             *value,
                                  DonnaSharedString  *ss)
{
    g_return_if_fail (DONNA_G_VALUE_HOLDS_SHARED_STRING (value));
    value->data[0].v_pointer = ss;
}

DonnaSharedString *
donna_g_value_get_shared_string (const GValue       *value)
{
    g_return_val_if_fail (DONNA_G_VALUE_HOLDS_SHARED_STRING (value), NULL);
    return value->data[0].v_pointer;
}

DonnaSharedString *
donna_g_value_dup_shared_string (const GValue       *value)
{
    g_return_val_if_fail (DONNA_G_VALUE_HOLDS_SHARED_STRING (value), NULL);
    return donna_shared_string_ref (value->data[0].v_pointer);
}

const gchar *
donna_g_value_get_shared_string_const_string (const GValue *value)
{
    g_return_val_if_fail (DONNA_G_VALUE_HOLDS_SHARED_STRING (value), NULL);
    return (const gchar *) ((DonnaSharedString *) value->data[0].v_pointer)->string;
}
