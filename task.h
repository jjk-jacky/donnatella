
#ifndef __DONNA_TASK_H__
#define __DONNA_TASK_H__

#include "common.h"

G_BEGIN_DECLS

#define DONNA_TASK_ERROR            g_quark_from_static_string ("DonnaTask-Error")
typedef enum
{
    DONNA_TASK_ERROR_NOMEM,
    DONNA_TASK_ERROR_OTHER
} DonnaTaskError;

typedef enum
{
    DONNA_TASK_WAITING,
    DONNA_TASK_RUNNING,
    DONNA_TASK_DONE,
    DONNA_TASK_CANCELLED,
    DONNA_TASK_FAILED
} DonnaTaskState;

typedef DonnaTaskState  (*task_fn)          (DonnaTask  *task,
                                             gpointer    data);
typedef void            (*task_timeout_fn)  (DonnaTask  *task,
                                             gpointer    data);
typedef void            (*task_callback_fn) (DonnaTask  *task,
                                             gboolean    timeout_called,
                                             gpointer    data);

struct _DonnaTask
{
    GObject parent;

    DonnaTaskPrivate *priv;
};

struct _DonnaTaskClass
{
    GObjectClass parent;
};

DonnaTask *     donna_task_new              (gchar              *desc,
                                             task_fn             func,
                                             gpointer            data,
                                             task_callback_fn    callback,
                                             gpointer            callback_data,
                                             guint               timeout_delay,
                                             task_timeout_fn     timeout_callback,
                                             gpointer            timeout_data);
void            donna_task_cancel           (DonnaTask          *task);
void            donna_task_pause            (DonnaTask          *task);
void            donna_task_resume           (DonnaTask          *task);
gboolean        donna_task_get_error        (DonnaTask          *task,
                                             GError             *error);
gboolean        donna_task_get_return_value (DonnaTask          *task,
                                             GValue             *value);
gboolean        donna_is_cancelled          (DonnaTask          *task);
void            donna_task_update           (DonnaTask          *task,
                                             gboolean            has_progress,
                                             gdouble             progress,
                                             gboolean            has_status,
                                             const gchar        *status_fmt,
                                             ...);
void            donna_task_set_error        (DonnaTask          *task,
                                             GQuark              domain,
                                             gint                code,
                                             const gchar        *format,
                                             ...);
void            donna_task_take_error       (DonnaTask          *task,
                                             GError            **error);
void            donna_task_set_return_value (DonnaTask          *task,
                                             const GValue       *value);

G_END_DECLS

#endif /* __DONNA_TASK_H__ */
