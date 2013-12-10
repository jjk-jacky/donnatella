
#include <string.h>     /* memmove() */
#include "history.h"
#include "macros.h"

struct _DonnaHistory
{
    GPtrArray *items;
    guint max;
    guint pos;
};

DonnaHistory *
donna_history_new (guint max)
{
    DonnaHistory *history;

    g_return_val_if_fail (max > 0, NULL);

    history = g_slice_new (DonnaHistory);
    history->items = g_ptr_array_sized_new (max);
    history->max = max;
    history->pos = 0;

    return history;
}

void
donna_history_set_max (DonnaHistory           *history,
                       guint                   max)
{
    g_return_if_fail (history != NULL);
    g_return_if_fail (max > 0);

    /* if we're gonna drop items, we need to make sure those are the oldest
     * ones, i.e. removed oldest ones & move others up */
    if (history->items->len > max)
    {
        guint diff;
        guint i;

        diff = history->items->len - max;
        for (i = diff; i > 0; --i)
            g_free (history->items->pdata[i - 1]);
        memmove (history->items->pdata, history->items->pdata + diff,
                sizeof (gpointer) * (history->items->len - diff));
    }

    history->max = max;
    g_ptr_array_set_size (history->items, max);
}

guint
donna_history_get_max (DonnaHistory *history)
{
    g_return_val_if_fail (history != NULL, 0);
    return history->max;
}

static void
add_item (DonnaHistory *history, const gchar *item, gboolean need_dup)
{
    /* if we're in the middle of the history, two things:
     * - first, check if the next item if the one we're adding; in which case we
     *   just move the position
     * - if not, remove all items in front of us, before adding the new one
     */
    if (history->items->len > 0 && history->pos < history->items->len - 1)
    {
        guint i;

        if (streq (history->items->pdata[history->pos + 1], item))
        {
            if (!need_dup)
                g_free ((gchar *) item);
            ++history->pos;
            return;
        }

        for (i = history->items->len - 1; i > history->pos; --i)
        {
            g_free (history->items->pdata[i]);
            --history->items->len;
        }
    }

    /* if history is full, remove oldest item & move others up */
    if (history->items->len == history->max)
    {
        g_free (history->items->pdata[0]);
        memmove (history->items->pdata, history->items->pdata + 1,
                sizeof (gpointer) * (history->max - 1));
        --history->items->len;
    }

    g_ptr_array_add (history->items, (need_dup) ? g_strdup (item) : (gpointer) item);
    /* if len==1 we've added the first item, pos stays at 0 */
    if (G_LIKELY (history->items->len > 1)
            /* if history is full, pos stays as last item */
            && history->pos < history->items->len - 1)
        ++history->pos;
}

gboolean
donna_history_add_items (DonnaHistory           *history,
                         const gchar           **items,
                         GError                **error)
{
    g_return_val_if_fail (history != NULL, FALSE);
    g_return_val_if_fail (items != NULL, FALSE);

    if (G_UNLIKELY (history->items->len > 0))
    {
        g_set_error (error, DONNA_HISTORY_ERROR, DONNA_HISTORY_ERROR_NOT_EMPTY,
                "Cannot add items to a non-empty history");
        return FALSE;
    }

    while (*items)
    {
        add_item (history, *items, TRUE);
        ++items;
    }

    return TRUE;
}

gboolean
donna_history_take_items (DonnaHistory           *history,
                          gchar                 **items,
                          GError                **error)
{
    gchar **s;
    guint i;

    g_return_val_if_fail (history != NULL, FALSE);
    g_return_val_if_fail (items != NULL, FALSE);

    if (G_UNLIKELY (history->items->len > 0))
    {
        g_set_error (error, DONNA_HISTORY_ERROR, DONNA_HISTORY_ERROR_NOT_EMPTY,
                "Cannot take items to a non-empty history");
        return FALSE;
    }

    for (i = 0, s = items; *s; ++s)
        ++i;
    memmove (history->items->pdata, items, sizeof (gpointer) * i);
    history->items->len = i;
    history->pos = i - 1;

    g_free (items);
    return TRUE;
}

void
donna_history_add_item (DonnaHistory           *history,
                        const gchar            *item)
{
    g_return_if_fail (history != NULL);
    g_return_if_fail (item != NULL);

    add_item (history, item, TRUE);
}

void
donna_history_take_item (DonnaHistory           *history,
                         gchar                  *item)
{
    g_return_if_fail (history != NULL);
    g_return_if_fail (item != NULL);

    add_item (history, item, FALSE);
}

static inline guint
get_index (DonnaHistory          *history,
           DonnaHistoryDirection  direction,
           guint                  nb,
           GError               **error)
{
    guint i;

    if (G_UNLIKELY (history->items->len == 0))
    {
        g_set_error (error, DONNA_HISTORY_ERROR, DONNA_HISTORY_ERROR_OUT_OF_RANGE,
                "History is empty");
        return (guint) -1;
    }

    if (direction == DONNA_HISTORY_BACKWARD)
    {
        if (history->pos >= nb)
            i = history->pos - nb;
        else
        {
            g_set_error (error, DONNA_HISTORY_ERROR, DONNA_HISTORY_ERROR_OUT_OF_RANGE,
                    "History: given position out of range");
            return (guint) -1;
        }
    }
    else if (direction == DONNA_HISTORY_FORWARD)
    {
        if (history->pos + nb < history->items->len)
            i = history->pos + nb;
        else
        {
            g_set_error (error, DONNA_HISTORY_ERROR, DONNA_HISTORY_ERROR_OUT_OF_RANGE,
                    "History: given position out of range");
            return (guint) -1;
        }
    }
    else
    {
        g_set_error (error, DONNA_HISTORY_ERROR, DONNA_HISTORY_ERROR_INVALID_DIRECTION,
                "History: invalid direction given");
            return (guint) -1;
    }
    return i;
}

const gchar *
donna_history_get_item (DonnaHistory           *history,
                        DonnaHistoryDirection   direction,
                        guint                   nb,
                        GError                **error)
{
    guint i;

    g_return_val_if_fail (history != NULL, NULL);

    i = get_index (history, direction, nb, error);
    if (i == (guint) -1)
        return NULL;
    else
        return history->items->pdata[i];
}

const gchar *
donna_history_move (DonnaHistory           *history,
                    DonnaHistoryDirection   direction,
                    guint                   nb,
                    GError                **error)
{
    guint i;

    g_return_val_if_fail (history != NULL, NULL);

    i = get_index (history, direction, nb, error);
    if (i == (guint) -1)
        return NULL;

    history->pos = i;
    return history->items->pdata[i];
}

gchar **
donna_history_get_items (DonnaHistory           *history,
                         DonnaHistoryDirection   direction,
                         guint                   nb,
                         GError                **error)
{
    gchar **r;
    guint i = 0;
    guint from = history->pos + 1;
    guint to = (history->pos > 0) ? history->pos - 1 : (guint) -1;

    g_return_val_if_fail (history != NULL, NULL);

    if (G_UNLIKELY (history->items->len == 0))
    {
        g_set_error (error, DONNA_HISTORY_ERROR, DONNA_HISTORY_ERROR_OUT_OF_RANGE,
                "History is empty");
        return NULL;
    }
    if (!(direction & (DONNA_HISTORY_BACKWARD | DONNA_HISTORY_FORWARD)))
    {
        g_set_error (error, DONNA_HISTORY_ERROR, DONNA_HISTORY_ERROR_INVALID_DIRECTION,
                "Getting history requires a valid direction");
        return NULL;
    }

    if (direction & DONNA_HISTORY_BACKWARD)
    {
        if (nb > 0)
            from = (history->pos >= nb) ? history->pos - nb : 0;
        else
            from = 0;
    }
    if (direction & DONNA_HISTORY_FORWARD)
    {
        if (nb > 0)
            to = MIN (history->items->len - 1, history->pos + nb);
        else
            to = history->items->len - 1;
    }

    r = g_new (gchar *, to - from + 2);
    /* to was set to -1 is we couldn't go back. If it's still there, we're
     * asking for BACKWARD when there's nothing available */
    if (to != (guint) -1)
        for (i = 0; from <= to; ++i, ++from)
            r[i] = g_strdup (history->items->pdata[from]);
    r[i] = NULL;

    return r;
}

void
donna_history_clear (DonnaHistory           *history,
                     DonnaHistoryDirection   direction)
{
    guint i;

    g_return_if_fail (history != NULL);

    if ((direction & (DONNA_HISTORY_BACKWARD | DONNA_HISTORY_FORWARD))
            == (DONNA_HISTORY_BACKWARD | DONNA_HISTORY_FORWARD))
    {
        for (i = 0; i < history->items->len; ++i)
            g_free (history->items->pdata[i]);
        history->items->len = 0;
        history->pos = 0;
    }
    else if (direction == DONNA_HISTORY_BACKWARD)
    {
        if (history->pos == 0)
            return;

        for (i = 0; i < history->pos; ++i)
            g_free (history->items->pdata[i]);
        memmove (history->items->pdata, history->items->pdata + history->pos,
                sizeof (gpointer) * (history->items->len - history->pos));
        history->items->len -= history->pos;
        history->pos = 0;
    }
    else if (direction == DONNA_HISTORY_FORWARD)
    {
        for (i = history->pos + 1; i < history->items->len; ++i)
            g_free (history->items->pdata[i]);
        history->items->len = history->pos + 1;
    }
}

void
donna_history_free (DonnaHistory           *history)
{
    guint i;

    g_return_if_fail (history != NULL);

    for (i = 0; i < history->items->len; ++i)
        g_free (history->items->pdata[i]);
    g_ptr_array_unref (history->items);
    g_slice_free (DonnaHistory, history);
}
