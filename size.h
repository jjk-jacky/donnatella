
#ifndef __DONNA_SIZE_H__
#define __DONNA_SIZE_H__

#include <glib.h>
#include <sys/types.h>  /* off_t */

G_BEGIN_DECLS

typedef enum
{
    DONNA_SIZE_FORMAT_RAW = 0,
    DONNA_SIZE_FORMAT_B_NO_UNIT,
    DONNA_SIZE_FORMAT_B,
    DONNA_SIZE_FORMAT_KB,
    DONNA_SIZE_FORMAT_MB,
    DONNA_SIZE_FORMAT_ROUNDED,
} DonnaSizeFormat;

void
donna_print_size (gchar         **str,
                  gssize          max,
                  off_t           size,
                  DonnaSizeFormat format,
                  gint            digits);

G_END_DECLS

#endif /* __DONNA_SIZE_H__ */
