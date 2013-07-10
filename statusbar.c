
#include <string.h>
#include "statusbar.h"
#include "macros.h"

#define DONNA_RENDERER_TEXT         't'
#define DONNA_RENDERER_PIXBUF       'p'
#define DONNA_RENDERER_PROGRESS     'P'
#define DONNA_RENDERER_COMBO        'c'
#define DONNA_RENDERER_TOGGLE       'T'
#define DONNA_RENDERER_SPINNER      'S'

enum
{
    RENDERER_TEXT,
    RENDERER_PIXBUF,
    RENDERER_PROGRESS,
    RENDERER_SPINNER,
    NB_RENDERERS
};

#define SPACING_BETWEEN_AREAS   4

struct area
{
    gchar               *name;
    DonnaStatusProvider *sp;
    guint                id;
    gulong               sid_status_changed;
    const gchar         *rend;
    GtkCellRenderer    **renderers;
    GtkCellAreaContext  *context;
    GtkCellArea         *area;
    /* user-specified natural width */
    gint                 nat_width;
    gboolean             expand;
    /* actual position & allocated width */
    gint                 x;
    gint                 width;
};

struct _DonnaStatusBarPrivate
{
    GtkCellRenderer *renderers[NB_RENDERERS];
    GArray  *areas;
};

static void         donna_status_bar_finalize               (GObject    *object);
static gboolean     donna_status_bar_query_tooltip          (GtkWidget  *widget,
                                                             gint        x,
                                                             gint        y,
                                                             gboolean    keyboard_tooltip,
                                                             GtkTooltip *tooltip);
static void         donna_status_bar_get_preferred_width    (GtkWidget  *widget,
                                                             gint       *minimum,
                                                             gint       *natural);
static void         donna_status_bar_get_preferred_height   (GtkWidget  *widget,
                                                             gint       *minimum,
                                                             gint       *natural);
static void         donna_status_bar_size_allocate          (GtkWidget  *widget,
                                                             GtkAllocation *allocation);
static gboolean     donna_status_bar_draw                   (GtkWidget  *widget,
                                                             cairo_t    *cr);

#define set_renderers(area, i, r)    do {                               \
    for (i = 1, r = area->renderers; *r; ++r, ++i)                      \
        if (area->sp)                                                   \
            donna_status_provider_render (area->sp, area->id, i, *r);   \
        else                                                            \
            g_object_set (*r, "visible", FALSE, NULL);                  \
} while (0)

G_DEFINE_TYPE (DonnaStatusBar, donna_status_bar, GTK_TYPE_WIDGET);

static void
donna_status_bar_class_init (DonnaStatusBarClass *klass)
{
    GObjectClass    *o_class;
    GtkWidgetClass  *w_class;

    o_class = G_OBJECT_CLASS (klass);
    o_class->finalize               = donna_status_bar_finalize;

    w_class = GTK_WIDGET_CLASS (klass);
    w_class->query_tooltip          = donna_status_bar_query_tooltip;
    w_class->get_preferred_width    = donna_status_bar_get_preferred_width;
    w_class->get_preferred_height   = donna_status_bar_get_preferred_height;
    w_class->size_allocate          = donna_status_bar_size_allocate;
    w_class->draw                   = donna_status_bar_draw;

    g_type_class_add_private (klass, sizeof (DonnaStatusBarPrivate));
}

static void
free_area (struct area *area)
{
    g_free (area->name);
    /* FIXME on finalize when closing app, it says there's no handler by that id
     * on the instance, even though we did register it... */
    if (0&&area->sid_status_changed > 0)
        g_signal_handler_disconnect (area->sp, area->sid_status_changed);
    g_free (area->renderers);
    g_object_unref (area->context);
    g_object_unref (area->area);
    if (area->sp)
    {
        donna_status_provider_free_status (area->sp, area->id);
        g_object_unref (area->sp);
    }
}

static void
donna_status_bar_init (DonnaStatusBar *sb)
{
    DonnaStatusBarPrivate *priv;

    priv = sb->priv = G_TYPE_INSTANCE_GET_PRIVATE (sb, DONNA_TYPE_STATUS_BAR,
            DonnaStatusBarPrivate);

    priv->areas = g_array_new (FALSE, FALSE, sizeof (struct area));
    g_array_set_clear_func (priv->areas, (GDestroyNotify) free_area);

    gtk_widget_set_has_window ((GtkWidget *) sb, FALSE);
    gtk_widget_set_has_tooltip ((GtkWidget *) sb, TRUE);
}

static void
donna_status_bar_finalize (GObject *object)
{
    DonnaStatusBarPrivate *priv = ((DonnaStatusBar *) object)->priv;
    guint i;

    g_array_free (priv->areas, TRUE);
    for (i = 0; i < NB_RENDERERS; ++i)
        if (priv->renderers[i])
            g_object_unref (priv->renderers[i]);

    G_OBJECT_CLASS (donna_status_bar_parent_class)->finalize (object);
}

static void
donna_status_bar_get_preferred_width (GtkWidget      *widget,
                                      gint           *minimum,
                                      gint           *natural)
{
    DonnaStatusBarPrivate *priv = ((DonnaStatusBar *) widget)->priv;
    guint i;

    *minimum = *natural = (priv->areas->len - 1) * SPACING_BETWEEN_AREAS;
    for (i = 0; i < priv->areas->len; ++i)
    {
        struct area *area = &g_array_index (priv->areas, struct area, i);
        GtkCellRenderer **r;
        guint j;
        gint min, nat;

        set_renderers (area, j, r);
        gtk_cell_area_get_preferred_width (area->area, area->context, widget,
                &min, &nat);
        *minimum += min;
        *natural += nat;
    }
}

static void
donna_status_bar_get_preferred_height (GtkWidget      *widget,
                                       gint           *minimum,
                                       gint           *natural)
{
    DonnaStatusBarPrivate *priv = ((DonnaStatusBar *) widget)->priv;
    guint i;

    *minimum = *natural = 0;
    for (i = 0; i < priv->areas->len; ++i)
    {
        struct area *area = &g_array_index (priv->areas, struct area, i);
        GtkCellRenderer **r;
        guint j;
        gint min, nat;

        set_renderers (area, j, r);
        gtk_cell_area_get_preferred_height (area->area, area->context, widget,
                &min, &nat);
        *minimum = MAX (*minimum, min);
        *natural = MAX (*natural, nat);
    }
}

static void
donna_status_bar_size_allocate (GtkWidget     *widget,
                                GtkAllocation *allocation)
{
    DonnaStatusBarPrivate *priv = ((DonnaStatusBar *) widget)->priv;
    guint i;
    gint alloc;
    gint min;
    gint nat;
    gint tot;
    gint x = 0;
    gint nb_expand = 0;


    GTK_WIDGET_CLASS (donna_status_bar_parent_class)->size_allocate (widget,
            allocation);

    min = nat = tot = (priv->areas->len - 1) * SPACING_BETWEEN_AREAS;
    for (i = 0; i < priv->areas->len; ++i)
    {
        struct area *area = &g_array_index (priv->areas, struct area, i);
        gint m, n;

        gtk_cell_area_context_get_preferred_width (area->context, &m, &n);
        area->x = x;
        area->width = MAX (n, area->nat_width);
        x += area->width + SPACING_BETWEEN_AREAS;

        min += m;
        nat += n;
        tot += area->width;
        if (area->expand)
            ++nb_expand;
    }

    alloc = gtk_widget_get_allocated_width (widget);
    if (alloc >= tot)
    {
        gint exp;

        if (nb_expand == 0)
            return;
        exp = (alloc - tot) / nb_expand;

        x = 0;
        for (i = 0; i < priv->areas->len; ++i)
        {
            struct area *area = &g_array_index (priv->areas, struct area, i);

            area->x = x;
            if (area->expand)
                area->width += exp;
            x += area->width + SPACING_BETWEEN_AREAS;
        }
    }
    else if (alloc >= nat)
    {
        gint exp;

        if (nb_expand > 0)
            exp = (alloc - nat) / nb_expand;

        x = 0;
        for (i = 0; i < priv->areas->len; ++i)
        {
            struct area *area = &g_array_index (priv->areas, struct area, i);

            area->x = x;
            gtk_cell_area_context_get_preferred_width (area->context,
                    NULL, &area->width);
            if (area->expand)
                area->width += exp;
            x += area->width + SPACING_BETWEEN_AREAS;
        }
    }
    else if (alloc >= min)
    {
        gint exp;

        if (nb_expand > 0)
            exp = (alloc - min) / nb_expand;

        x = 0;
        for (i = 0; i < priv->areas->len; ++i)
        {
            struct area *area = &g_array_index (priv->areas, struct area, i);

            area->x = x;
            gtk_cell_area_context_get_preferred_width (area->context,
                    &area->width, NULL);
            if (area->expand)
                area->width += exp;
            x += area->width + SPACING_BETWEEN_AREAS;
        }
    }
    else
    {
        x = 0;
        for (i = 0; i < priv->areas->len; ++i)
        {
            struct area *area = &g_array_index (priv->areas, struct area, i);

            area->x = x;
            gtk_cell_area_context_get_preferred_width (area->context,
                    &area->width, NULL);
            if (area->x + area->width > alloc)
            {
                area->width -= (area->x + area->width) - alloc;
                area->width = MAX (0, area->width);
            }
            else
                x += area->width + SPACING_BETWEEN_AREAS;
        }
    }
}

static gboolean
donna_status_bar_draw (GtkWidget          *widget,
                       cairo_t            *cr)
{
    DonnaStatusBarPrivate *priv = ((DonnaStatusBar *) widget)->priv;
    GdkRectangle clip;
    GdkRectangle cell;
    guint i;
    gint h;

    if (!gdk_cairo_get_clip_rectangle (cr, &clip))
        return TRUE;

    h = gtk_widget_get_allocated_height (widget);

    cell.x = 0;
    cell.y = 0;
    cell.height = h;

    for (i = 0; i < priv->areas->len; ++i)
    {
        struct area *area = &g_array_index (priv->areas, struct area, i);
        GtkCellRenderer **r;
        guint j;

        if (area->x + area->width < clip.x)
            continue;
        else if (area->x > clip.x + clip.width)
            break;

        set_renderers (area, j, r);
        cell.x = area->x;
        cell.width = area->width;
        gtk_cell_area_render (area->area, area->context, widget, cr,
                &cell, &cell, 0, FALSE);
    }

    return FALSE;
}

static gboolean
donna_status_bar_query_tooltip (GtkWidget  *widget,
                                gint        x,
                                gint        y,
                                gboolean    keyboard_tooltip,
                                GtkTooltip *tooltip)
{
    DonnaStatusBarPrivate *priv = ((DonnaStatusBar *) widget)->priv;
    guint i;

    for (i = 0; i < priv->areas->len; ++i)
    {
        struct area *area = &g_array_index (priv->areas, struct area, i);

        if (x >= area->x && x <= area->x + area->width)
        {
            GtkCellRenderer **r;
            GtkCellRenderer *renderer;
            GdkRectangle cell;
            const gchar *rend;
            gchar c;
            guint index;
            guint j;

            if (!area->sp)
                return FALSE;

            cell.x = area->x;
            cell.y = 0;
            cell.width = area->width;
            cell.height = gtk_widget_get_allocated_height (widget);

            set_renderers (area, j, r);
            renderer = gtk_cell_area_get_cell_at_position (area->area, area->context,
                    widget, &cell, x, y, NULL);
            if (!renderer)
                return FALSE;
            c = GPOINTER_TO_INT (g_object_get_data ((GObject *) renderer,
                        "donna-renderer"));

            rend = donna_status_provider_get_renderers (area->sp, area->id);
            for (index = 1; *rend && *rend != c; ++rend, ++index)
                ;
            return donna_status_provider_set_tooltip (area->sp, area->id, index,
                    tooltip);
        }
    }
    return FALSE;
}

static void
status_changed (DonnaStatusProvider *sp, guint id, DonnaStatusBar *sb)
{
    DonnaStatusBarPrivate *priv = sb->priv;
    guint i;

    for (i = 0; i < priv->areas->len; ++i)
    {
        struct area *area = &g_array_index (priv->areas, struct area, i);
        if (area->sp == sp && area->id == id)
        {
            GtkCellRenderer **r;
            GtkAllocation alloc;
            guint j;
            gint nat;

            gtk_widget_get_allocation ((GtkWidget *) sb, &alloc);

            set_renderers (area, j, r);
            gtk_cell_area_get_preferred_width (area->area, area->context,
                    (GtkWidget *) sb, NULL, &nat);
            if (nat > area->width)
                /* the resize will take care of adjusting sizes for all areas
                 * (this one getting bigger might reduce an adjacent one in its
                 * expanded space) as well as queueing a redraw */
                gtk_widget_queue_resize ((GtkWidget *) sb);
            else
            {
                /* simply invalidate this area */
                alloc.x += area->x;
                alloc.width = area->width;

                /* resize is needed to adjust the minimum size of sb */
                gtk_widget_queue_resize_no_redraw ((GtkWidget *) sb);
                gtk_widget_queue_draw_area ((GtkWidget *) sb,
                        alloc.x, alloc.y, alloc.width, alloc.height);
            }

            return;
        }
    }
    g_warning ("StatusBar: signal 'status-changed' for %p (%d) found no match",
            sp, id);
}

gboolean
donna_status_bar_add_area (DonnaStatusBar       *sb,
                           const gchar          *name,
                           DonnaStatusProvider  *sp,
                           guint                 id,
                           gint                  nat_width,
                           gboolean              expand,
                           GError              **error)
{
    DonnaStatusBarPrivate *priv;
    struct area area;
    const gchar *rend;
    guint i;

    g_return_val_if_fail (DONNA_IS_STATUS_BAR (sb), FALSE);
    g_return_val_if_fail (name != NULL, FALSE);
    g_return_val_if_fail (DONNA_IS_STATUS_PROVIDER (sp), FALSE);
    g_return_val_if_fail (id > 0, FALSE);

    priv = sb->priv;

    for (i = 0; i < priv->areas->len; ++i)
    {
        struct area *area = &g_array_index (priv->areas, struct area, i);
        if (streq (name, area->name))
        {
            g_set_error (error, DONNA_STATUS_BAR_ERROR,
                    DONNA_STATUS_BAR_ERROR_AREA_ALREADY_EXISTS,
                    "Statusbar: Cannot added area '%s', one already exists",
                    name);
            return FALSE;
        }
    }

    rend = donna_status_provider_get_renderers (sp, id);
    area.name = g_strdup (name);
    area.sp = g_object_ref (sp);
    area.id = id;
    area.nat_width = nat_width;
    area.expand = expand;
    area.x = -1;
    area.rend = rend;
    area.renderers = g_new0 (GtkCellRenderer *, strlen (rend) + 1);
    area.area = g_object_ref_sink (gtk_cell_area_box_new ());
    area.context = gtk_cell_area_create_context (area.area);
    for (i = 0; *rend; ++rend, ++i)
    {
        GtkCellRenderer **r;
        GtkCellRenderer * (*load_renderer) (void);

        switch (*rend)
        {
            case DONNA_RENDERER_TEXT:
                r = &priv->renderers[RENDERER_TEXT];
                load_renderer = gtk_cell_renderer_text_new;
                break;
            case DONNA_RENDERER_PIXBUF:
                r = &priv->renderers[RENDERER_PIXBUF];
                load_renderer = gtk_cell_renderer_pixbuf_new;
                break;
            case DONNA_RENDERER_PROGRESS:
                r = &priv->renderers[RENDERER_PROGRESS];
                load_renderer = gtk_cell_renderer_progress_new;
                break;
            case DONNA_RENDERER_SPINNER:
                r = &priv->renderers[RENDERER_SPINNER];
                load_renderer = gtk_cell_renderer_spinner_new;
                break;
            default:
                g_warning ("StatusBar: Unknown renderer type '%c'", *rend);
                continue;
        }
        if (!*r)
        {
            *r = g_object_ref (load_renderer ());
            g_object_set_data ((GObject *) *r, "donna-renderer",
                    GINT_TO_POINTER (*rend));
        }

        area.renderers[i] = *r;
        gtk_cell_area_box_pack_start ((GtkCellAreaBox *) area.area, *r,
                area.expand && rend[1] == '\0', FALSE, FALSE);
    }
    area.sid_status_changed = g_signal_connect (sp, "status-changed",
            (GCallback) status_changed, sb);

    g_array_append_val (priv->areas, area);
    return TRUE;
}

gboolean
donna_status_bar_update_area (DonnaStatusBar       *sb,
                              const gchar          *name,
                              DonnaStatusProvider  *sp,
                              guint                 id,
                              GError              **error)
{
    DonnaStatusBarPrivate *priv;
    guint i;

    g_return_val_if_fail (DONNA_IS_STATUS_BAR (sb), FALSE);
    g_return_val_if_fail (name != NULL, FALSE);
    g_return_val_if_fail (sp == NULL || DONNA_IS_STATUS_PROVIDER (sp), FALSE);
    g_return_val_if_fail (sp == NULL || id > 0, FALSE);

    priv = sb->priv;

    for (i = 0; i < priv->areas->len; ++i)
    {
        struct area *area = &g_array_index (priv->areas, struct area, i);
        if (streq (name, area->name))
        {
            if (area->sp != sp)
            {
                if (sp && !streq (area->rend,
                            donna_status_provider_get_renderers (sp, id)))
                {
                    g_set_error (error, DONNA_STATUS_BAR_ERROR,
                            DONNA_STATUS_BAR_ERROR_OTHER,
                            "StatusBar: Cannot update area '%s', renderers aren't consistent",
                            name);
                    return FALSE;
                }

                if (area->sid_status_changed > 0)
                    g_signal_handler_disconnect (area->sp, area->sid_status_changed);
                if (sp)
                    area->sid_status_changed = g_signal_connect (sp, "status-changed",
                            (GCallback) status_changed, sb);
                else
                    area->sid_status_changed = 0;
                if (area->sp)
                    g_object_unref (area->sp);
                area->sp = (sp) ? g_object_ref (sp) : NULL;
            }
            area->id = id;
            status_changed (area->sp, area->id, sb);
            return TRUE;
        }
    }

    g_set_error (error, DONNA_STATUS_BAR_ERROR,
            DONNA_STATUS_BAR_ERROR_AREA_NOT_FOUND,
            "Statusbar: Cannot update area '%s', not found", name);
    return FALSE;
}

const gchar *
donna_status_bar_get_area_at_pos (DonnaStatusBar    *sb,
                                  gint               x,
                                  gint               y)
{
    DonnaStatusBarPrivate *priv;
    guint i;

    g_return_val_if_fail (DONNA_IS_STATUS_BAR (sb), NULL);
    g_return_val_if_fail (x >= 0, NULL);
    g_return_val_if_fail (y >= 0, NULL);

    priv = sb->priv;

    for (i = 0; i < priv->areas->len; ++i)
    {
        struct area *area = &g_array_index (priv->areas, struct area, i);

        if (x >= area->x && x <= area->x + area->width)
            return (const gchar *) area->name;
    }
    return NULL;
}
