
#ifndef __DONNA_PROVIDER_TASK_H__
#define __DONNA_PROVIDER_TASK_H__

#include "provider-base.h"
#include "common.h"

G_BEGIN_DECLS

#define DONNA_TYPE_PROVIDER_TASK                (donna_provider_task_get_type ())
#define DONNA_PROVIDER_TASK(obj)                (G_TYPE_CHECK_INSTANCE_CAST ((obj), DONNA_TYPE_PROVIDER_TASK, DonnaProviderTask))
#define DONNA_PROVIDER_TASK_CLASS(klass)        (G_TYPE_CHECK_CLASS_CAST ((klass), DONNA_TYPE_PROVIDER_TASK, DonnaProviderTaskClass))
#define DONNA_IS_PROVIDER_TASK(obj)             (G_TYPE_CHECK_INSTANCE_TYPE ((obj), DONNA_TYPE_PROVIDER_TASK))
#define DONNA_IS_PROVIDER_TASK_CLASS(klass)     (G_TYPE_CHECK_CLASS_TYPE ((klass), DONNA_TYPE_PROVIDER_TASK))
#define DONNA_PROVIDER_TASK_GET_CLASS(obj)      (G_TYPE_INSTANCE_GET_CLASS ((obj), DONNA_TYPE_PROVIDER_TASK, DonnaProviderTaskClass))

typedef struct _DonnaProviderTask               DonnaProviderTask;
typedef struct _DonnaProviderTaskClass          DonnaProviderTaskClass;
typedef struct _DonnaProviderTaskPrivate        DonnaProviderTaskPrivate;


#define DONNA_TASK_MANAGER(obj)                 ((DonnaTaskManager *) (obj))
#define DONNA_IS_TASK_MANAGER(obj)              DONNA_IS_PROVIDER_TASK(obj)

typedef DonnaProviderTask                       DonnaTaskManager;

#define DONNA_TASK_MANAGER_ERROR                g_quark_from_static_string ("DonnaTaskManager-Error")
typedef enum
{
    DONNA_TASK_MANAGER_ERROR_INVALID_TASK_VISIBILITY,
    DONNA_TASK_MANAGER_ERROR_INVALID_TASK_STATE,
    DONNA_TASK_MANAGER_ERROR_OTHER
} DonnaTaskManagerError;


struct _DonnaProviderTask
{
    DonnaProviderBase parent;

    DonnaProviderTaskPrivate *priv;
};

struct _DonnaProviderTaskClass
{
    DonnaProviderBaseClass parent;
};

GType       donna_provider_task_get_type        (void) G_GNUC_CONST;
/* task manager */
gboolean    donna_task_manager_add_task         (DonnaTaskManager       *tm,
                                                 DonnaTask              *task,
                                                 GError                **error);
gboolean    donna_task_manager_set_state        (DonnaTaskManager       *tm,
                                                 DonnaNode              *node,
                                                 DonnaTaskState          state,
                                                 GError                **error);
gboolean    donna_task_manager_switch_tasks     (DonnaTaskManager       *tm,
                                                 GPtrArray              *nodes,
                                                 gboolean                switch_on,
                                                 gboolean                fail_on_failure,
                                                 GError                **error);
gboolean    donna_task_manager_cancel           (DonnaTaskManager       *tm,
                                                 DonnaNode              *node,
                                                 GError                **error);
gboolean    donna_task_manager_show_ui          (DonnaTaskManager       *tm,
                                                 DonnaNode              *node,
                                                 GError                **error);
void        donna_task_manager_cancel_all       (DonnaProviderTask      *tm);
gboolean    donna_task_manager_pre_exit         (DonnaProviderTask      *tm,
                                                 gboolean                always_confirm);

G_END_DECLS

#endif /* __DONNA_PROVIDER_TASK_H__ */
