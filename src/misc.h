
#ifndef __DONNA_MISC_H__
#define __DONNA_MISC_H__

#include "common.h"

G_BEGIN_DECLS

gboolean        _key_press_ctrl_a_cb                (GtkEntry       *entry,
                                                     GdkEventKey    *event);
gchar *         _resolve_path                       (DonnaNode      *node,
                                                     const gchar    *path);

G_END_DECLS

#endif /* __DONNA_MISC_H__ */
