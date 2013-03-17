
#ifndef __DONNA_SHARED_STRING__
#define __DONNA_SHARED_STRING__

#include <glib-object.h>

G_BEGIN_DECLS

/**
 * DONNA_TYPE_SHARED_STRING:
 *
 * The GType for #DonnaSharedString
 */
#define DONNA_TYPE_SHARED_STRING        (donna_shared_string_get_type ())
#define DONNA_G_VALUE_HOLDS_SHARED_STRING(value)    \
    (G_TYPE_CHECK_VALUE_TYPE ((value), DONNA_TYPE_SHARED_STRING))

GType param_type_shared_string;
#define DONNA_TYPE_PARAM_SHARED_STRING  (param_type_shared_string)

typedef struct _DonnaSharedString       DonnaSharedString;

struct _DonnaSharedString
{
    gchar   *string;
    gint     ref_count;
};

GType               donna_shared_string_get_type        (void) G_GNUC_CONST;
void                donna_shared_string_register        (void);

/**
 * donna_shared_string:
 * @ss: #DonnaSharedString to get string from
 *
 * Gets the string from @ss
 *
 * Returns: The string (as const gchar *) from @ss
 */
#define donna_shared_string(ss)         ((const gchar *) ss->string)

/**
 * donna_shared_string_new_take:
 * @string: the string to take ownership in the shared string
 *
 * Creates a new shared string, taking ownership of the given string.
 *
 * Returns: The (new) shared string
 */
#define donna_shared_string_new_take(string)    \
    donna_shared_string_update_take (NULL, string)
/**
 * donna_shared_string_new_dup:
 * @string: the string to duplicate in the shared string
 *
 * Creates a new shared string, duplicating @string.
 *
 * Returns: The (new) shared string
 */
#define donna_shared_string_new_dup(string)     \
    donna_shared_string_update_dup (NULL, string)
/**
 * donna_shared_string_new_printf:
 *
 * Creates a new shared string, using the printf-format and parameters given.
 *
 * Returns: The (new) shared string
 */
#define donna_shared_string_new_printf(...)     \
    donna_shared_string_update_printf (NULL, __VA_ARGS__)

DonnaSharedString * donna_shared_string_ref             (DonnaSharedString  *sv);
void                donna_shared_string_unref           (DonnaSharedString  *sv);
DonnaSharedString * donna_shared_string_update_take     (DonnaSharedString  *sv,
                                                         gchar              *string);
DonnaSharedString * donna_shared_string_update_dup      (DonnaSharedString  *sv,
                                                         const gchar        *string);
DonnaSharedString * donna_shared_string_update_printf   (DonnaSharedString    *ss,
                                                         const gchar          *fmt,
                                                         ...);

/**
 * donna_g_value_new_shared_string_take:
 * @value: #GValue to set the newly created shared string into
 * @string: String to set into the newly created #DonnaSharedString
 *
 * Creates a new #DonnaSharedString taking ownership of @string, and sets it
 * inside @value
 */
#define donna_g_value_new_shared_string_take(value, string) \
    donna_g_value_take_shared_string (value,                \
            donna_shared_string_new_take (string))
/**
 * donna_g_value_new_shared_string_dup:
 * @value: #GValue to set the newly created shared string into
 * @string: String to set into the newly created #DonnaSharedString
 *
 * Creates a new #DonnaSharedString duplicating @string, and sets it inside
 * @value
 */
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


GParamSpec *        donna_param_spec_shared_string (const gchar *name,
                                                    const gchar *nick,
                                                    const gchar *blurb,
                                                    GParamFlags  flags);
G_END_DECLS

#endif /* __DONNA_SHARED_STRING__ */
