
#ifndef __DONNA_SHARED_STRING__
#define __DONNA_SHARED_STRING__

#include <glib-object.h>

G_BEGIN_DECLS

#define DONNA_TYPE_SHARED_STRING        (donna_shared_string_get_type ())
#define DONNA_G_VALUE_HOLDS_SHARED_STRING(value)    \
    (G_TYPE_CHECK_VALUE_TYPE ((value), DONNA_TYPE_SHARED_STRING))

typedef struct _DonnaSharedString       DonnaSharedString;

struct _DonnaSharedString
{
    gchar   *string;
    gint     ref_count;
};

GType               donna_shared_string_get_type        (void) G_GNUC_CONST;
void                donna_shared_string_register        (void);

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

#define donna_g_value_new_shared_string_take(value, string) \
    donna_g_value_take_shared_string (value,                \
            donna_shared_string_new_take (string))
#define donna_g_value_new_shared_string_dup(value, string)  \
    donna_g_value_take_shared_string (value,                \
            donna_shared_string_new_dup (string))

void                donna_g_value_set_shared_string     (GValue             *value,
                                                         DonnaSharedString  *ss);
void                donna_g_value_take_shared_string    (GValue             *value,
                                                         DonnaSharedString  *ss);
DonnaSharedString * donna_g_value_get_shared_string     (const GValue       *value);
DonnaSharedString * donna_g_value_dup_shared_string     (const GValue       *value);
const gchar *       donna_g_value_get_shared_string_const_string (const GValue *value);

G_END_DECLS

#endif /* __DONNA_SHARED_STRING__ */
