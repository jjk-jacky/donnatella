
#include <glib-object.h>
#include "taskui.h"

static void
donna_taskui_default_init (DonnaTaskUiInterface *klass)
{
}

G_DEFINE_INTERFACE (DonnaTaskUi, donna_taskui, G_TYPE_OBJECT)

void
donna_taskui_set_title (DonnaTaskUi     *taskui,
                        const gchar     *title)
{
    gchar *dup;

    g_return_if_fail (DONNA_IS_TASKUI (taskui));

    dup = g_strdup (title);
    donna_taskui_take_title (taskui, dup);
}

void
donna_taskui_take_title (DonnaTaskUi    *taskui,
                         gchar          *title)
{
    DonnaTaskUiInterface *interface;

    g_return_if_fail (DONNA_IS_TASKUI (taskui));

    interface = DONNA_TASKUI_GET_INTERFACE (taskui);

    g_return_if_fail (interface != NULL);
    g_return_if_fail (interface->take_title != NULL);

    return (*interface->take_title) (taskui, title);
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
