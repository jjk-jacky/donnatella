
#include <glib-object.h>
#include "sharedstring.h"

G_DEFINE_BOXED_TYPE (DonnaSharedString, donna_shared_string,
        (GBoxedCopyFunc) donna_shared_string_ref,
        (GBoxedFreeFunc) donna_shared_string_unref)

DonnaSharedString *
donna_shared_string_ref (DonnaSharedString *ss)
{
    g_return_val_if_fail (DONNA_IS_SHARED_STRING (ss), NULL);
    g_atomic_int_inc (&ss->ref_count);
    return ss;
}

void
donna_shared_string_unref (DonnaSharedString *ss)
{
    g_return_if_fail (DONNA_IS_SHARED_STRING (ss));

    if (g_atomic_int_dec_and_test (&ss->ref_count))
    {
        g_free (ss->string);
        g_slice_free (DonnaSharedString, ss);
    }
}

static inline DonnaSharedString *
update_or_new (DonnaSharedString *ss, gpointer string, gboolean need_dup)
{
    g_return_val_if_fail (!ss || DONNA_IS_SHARED_STRING (ss), NULL);

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
