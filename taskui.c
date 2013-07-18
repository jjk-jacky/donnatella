
#include <glib-object.h>
#include "taskui.h"

static void
donna_taskui_default_init (DonnaTaskUiInterface *klass)
{
}

G_DEFINE_INTERFACE (DonnaTaskUi, donna_taskui, G_TYPE_OBJECT)

void
donna_taskui_set_title (DonnaTaskUi    *taskui,
                       const gchar    *title)
{
    DonnaTaskUiInterface *interface;

    g_return_if_fail (DONNA_IS_TASKUI (taskui));

    interface = DONNA_TASKUI_GET_INTERFACE (taskui);

    g_return_if_fail (interface != NULL);
    g_return_if_fail (interface->set_title != NULL);

    return (*interface->set_title) (taskui, title);
}

void
donna_taskui_show (DonnaTaskUi *taskui)
{
    DonnaTaskUiInterface *interface;

    g_return_if_fail (DONNA_IS_TASKUI (taskui));

    interface = DONNA_TASKUI_GET_INTERFACE (taskui);

    g_return_if_fail (interface != NULL);
    g_return_if_fail (interface->show != NULL);

    return (*interface->show) (taskui);
}
