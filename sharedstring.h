
#ifndef __DONNA_SHARED_STRING__
#define __DONNA_SHARED_STRING__

#include <glib-object.h>

G_BEGIN_DECLS

#define DONNA_TYPE_SHARED_STRING        (donna_shared_string_get_type ())
#define DONNA_SHARED_STRING(obj)        (G_TYPE_CHECK_INSTANCE_CAST (obj(), DONNA_TYPE_SHARED_STRING, DonnaSharedString))
#define DONNA_IS_SHARED_STRING(obj)     (G_TYPE_CHECK_INSTANCE_TYPE ((obj), DONNA_TYPE_SHARED_STRING))

typedef struct _DonnaSharedString       DonnaSharedString;

struct _DonnaSharedString
{
    gchar   *string;
    gint     ref_count;
};

GType               donna_shared_string_get_type        (void) G_GNUC_CONST;

#define donna_shared_string(ss)         ((const gchar *) ss->string)

#define donna_shared_string_new_take(string)    \
    donna_shared_string_update_take (NULL, string)
#define donna_shared_string_new_dup(string)     \
    donna_shared_string_update_dup (NULL, string)

DonnaSharedString * donna_shared_string_ref             (DonnaSharedString  *sv);
void                donna_shared_string_unref           (DonnaSharedString  *sv);
DonnaSharedString * donna_shared_string_update_take     (DonnaSharedString  *sv,
                                                         gchar              *string);
DonnaSharedString * donna_shared_string_update_dup      (DonnaSharedString  *sv,
                                                         const gchar        *string);

G_END_DECLS

#endif /* __DONNA_SHARED_STRING__ */
