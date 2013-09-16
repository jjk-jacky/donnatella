
#ifndef __DONNA_TASK_HELPERS_H__
#define __DONNA_TASK_HELPERS_H__

#include "task.h"

G_BEGIN_DECLS

typedef struct _DonnaTaskHelper             DonnaTaskHelper;

typedef enum
{
    DONNA_TASK_HELPER_RC_SUCCESS = 0,
    DONNA_TASK_HELPER_RC_CANCELLING,
    DONNA_TASK_HELPER_RC_ERROR
} DonnaTaskHelperRc;

typedef void        (*task_helper_ui)       (DonnaTaskHelper    *th,
                                             gpointer            data);

/* base functions for helpers */
void                donna_task_helper_done  (DonnaTaskHelper    *th);
DonnaTaskHelperRc   donna_task_helper       (DonnaTask          *task,
                                             task_helper_ui      show_ui,
                                             task_helper_ui      destroy_ui,
                                             gpointer            data);

/* actual helpers */

typedef enum
{
    DONNA_TASK_HELPER_ASK_RC_CANCELLING =  0,
    DONNA_TASK_HELPER_ASK_RC_ERROR      = -1,
    DONNA_TASK_HELPER_ASK_RC_NO_ANSWER  = -2
} DonnaTaskHelperAskRc;


gint        donna_task_helper_ask           (DonnaTask          *task,
                                             const gchar        *question,
                                             const gchar        *details,
                                             gboolean            details_markup,
                                             gint                btn_default,
                                             const gchar        *btn_label,
                                             ...);

G_END_DECLS

#endif /* __DONNA_TASK_HELPERS_H__ */
