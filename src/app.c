/*
 * donnatella - Copyright (C) 2014 Olivier Brunel
 *
 * app.c
 * Copyright (C) 2014 Olivier Brunel <jjk@jjacky.com>
 *
 * This file is part of donnatella.
 *
 * donnatella is free software: you can redistribute it and/or modify it under
 * the terms of the GNU General Public License as published by the Free Software
 * Foundation, either version 3 of the License, or (at your option) any later
 * version.
 *
 * donnatella is distributed in the hope that it will be useful, but WITHOUT ANY
 * WARRANTY; without even the implied warranty of MERCHANTABILITY or FITNESS
 * FOR A PARTICULAR PURPOSE.
 * See the GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License along with
 * donnatella. If not, see http://www.gnu.org/licenses/
 */

#include "config.h"

#include <locale.h>
#include <stdlib.h>     /* free() */
#include <ctype.h>      /* isblank() */
#include <string.h>
#include <errno.h>
#ifdef DONNA_DEBUG_AUTOBREAK
#include <unistd.h>
#include <stdio.h>
#endif
#include "debug.h"
#include "app.h"
#include "provider.h"
#include "provider-fs.h"
#include "provider-command.h"
#include "provider-config.h"
#include "provider-task.h"
#include "provider-exec.h"
#include "provider-register.h"
#include "provider-internal.h"
#include "provider-mark.h"
#include "provider-invalid.h"
#include "columntype.h"
#include "columntype-name.h"
#include "columntype-size.h"
#include "columntype-time.h"
#include "columntype-perms.h"
#include "columntype-text.h"
#include "columntype-label.h"
#include "columntype-progress.h"
#include "columntype-value.h"
#include "node.h"
#include "filter.h"
#include "sort.h"
#include "command.h"
#include "statusbar.h"
#include "imagemenuitem.h"
#include "misc.h"
#include "util.h"
#include "macros.h"
#include "closures.h"

/**
 * SECTION:app
 * @Short_Description: Overview of your file manager: donnatella
 *
 * donnatella - donna for short - is a free, open-source GUI file manager for
 * GNU/Linux systems.
 *
 * <refsect2 id="installation">
 * <title>Installation: a patched GTK+3 for full GUI power</title>
 * <para>
 * donna is built upon GTK+3 and the underlying GLib/GIO libraries. However,
 * because some of the features of donna were not doable using GTK+ as it is,
 * especially when it comes to the treeview, a patchset is available.
 *
 * This set of patches for GTK+ will fix some bugs & add extra features, all the
 * while remaining 100% compatible with GTK+3. You can safely compile your
 * patched GTK+ and install it, replacing the vanilla GTK+.  It won't change
 * anything for other applications (unless they were victims of the few fixed
 * bugs), but will unleash the full power of donnatella.
 *
 * Obviously it would be better if this wasn't necessary, and I'd like to see
 * all patches merged upstream. This is a work in process, but unfortunately
 * upstream doesn't seem too eager to review those patches (Seems they don't
 * have much love for the treeview, because client-side decorations are so much
 * more useful... :p).
 * </para></refsect2>
 *
 * <refsect2 id="start">
 * <title>Start</title>
 * <para>
 * On start, donna will load its configuration (and possibly other data) from
 * its configuration directory, which is
 * <filename>$XDG_CONFIG_HOME/donnatella</filename> (and will default to
 * <filename>~/.config/donnatella</filename>).
 *
 * If you need to you can specify another directory to be used, using
 * command-line option <systemitem>--config-dir</systemitem>
 * </para></refsect2>
 *
 * <refsect2 id="concept">
 * <title>Concept</title>
 * <para>
 * Usually, a file manager shows you the files & directories of your file
 * system. Things are a little different in donna, though, as it uses a layer of
 * abstraction.
 *
 * Instead, donna is all about nodes (items & containers) of a domain. A domain
 * might be "fs" (which stands for file system), where nodes will be the files
 * (items) and directories (containers) as expected.
 *
 * But using this concept will allow donna to easily show other things exactly
 * the same way. That is, it will be used to show the content of "virtual
 * folders" or list search results; It also allows to show nodes that aren't
 * files or directories, like the categories & options of the configuration, or
 * provide interface to other features of donna, e.g. registers or marks.
 * </para></refsect2>
 *
 * <refsect2 id="features">
 * <title>Features</title>
 * <para>
 * <refsect3 id="window">
 * <title>Customize the main window</title>
 * <para>
 * On start, donna's main window will be created according to a few options, all
 * found under category <systemitem>donna</systemitem> :
 *
 * - <systemitem>width</systemitem> & <systemitem>height</systemitem> : to
 *   define the initial size of the main window
 * - <systemitem>maximized</systemitem> : A boolean, set to true to have the
 *   window maximized on start.
 *   In that case, the width & height will be used when unmaximizing the window.
 *   Note that it is also possible to start with a maximized window using
 *   command line option <systemitem>--maximized</systemitem>
 * - <systemitem>active_list</systemitem> : must be the name of a treeview in
 *   mode list, to be the active-list. If not set, the first list created will
 *   be the active-list.
 * - <systemitem>layout</systemitem> : the actual layout; see #layout for more
 * - <systemitem>tile</systemitem> : the title of the window
 *
 * The following variables are available to use in the window title :
 *
 * - <systemitem>\%a</systemitem> : treeview name of the active list
 * - <systemitem>\%d</systemitem> : current directory; See
 *   donna_app_get_current_dirname() for difference with current location
 * - <systemitem>\%l</systemitem> : full location of the active list's current
 *   location
 * - <systemitem>\%L</systemitem> : active's list current location. What is
 *   actually used depends on the domain of the current location. An option
 *   <systemitem>domain_&lt;DOMAIN&gt;</systemitem> (integer:title-domain) is
 *   looked up, and can be "full", "loc" or "custom"
 *   The first two will have the full location or location used, respectively.
 *   With the later a string option <systemitem>custom_&lt;DOMAIN&gt;</systemitem>
 *   will be looked. If it exists, it is used; else the name of the current
 *   location will be used.
 * - <systemitem>\%v</systemitem> : version number
 *
 * </para></refsect3>
 *
 * <refsect3 id="layout">
 * <title>Layout: Single pane, dual pane, hexapane...</title>
 * <para>
 * donna is made so you can fully customize it to your needs & improve your
 * workflow as best possible. By default, you will have a tree on the left, and
 * a list on the right. Simple, standard, efficient setup.
 *
 * However, you might want something different: no tree; a dual pane with one
 * tree and two lists, or one tree for each lists; maybe you want 4 panes, or
 * more?
 *
 * The actual layout of donna's window is entirely configurable. You can in fact
 * create as many layouts as you need. A layout is define under section
 * <systemitem>[layouts]</systemitem> in the configuration file
 * (<filename>donnatella.conf</filename>).
 *
 * The basic rule in donna is that most GUI component will have a name: each
 * treeview, each toolbar, etc will have a unique name to identify it. The main
 * window can only hold one single element.
 *
 * Worry not, there is a trick: special elements are available:
 *
 * - boxH() & boxV() : those aren't actually visible, but will allow to pack
 *   more than one component together. The former will put them next to each
 *   other horizontally, the later vertically.  Inside boxes should be a
 *   coma-separated list of children elements. You can add as many elements as
 *   you wish inside a box.
 * - paneH() & paneV() : those are quite similar to boxes, only they can and
 *   must contain only 2 children. There will also be possibility to resize both
 *   children by dragging a splitter put between the two.  Inside panes should
 *   always be a coma-separated list of two elements, though they are no
 *   restrictions as to which elements those are (i.e. you can put boxes or
 *   panes inside panes).  You can prefix one element with a '!' to indicate
 *   that it should be "fixed," meaning that when the window is resized it
 *   shouldn't (as much as possible) be automatically resized.  The first
 *   element can also be suffixed with the '@' symbol and a size, this will be
 *   the initial size/position of the splitter between the two children.
 *
 * And of course, there are actual GUI compenents. Those should always be
 * followed by a colon and the name of the component. This will be used to
 * identify it within donna, starting with loading its configuration.
 *
 * The following GUI compenents are available:
 *
 * - <systemitem>treeview</systemitem> : this is the main component in
 *   donnatella. A treeview will either be a list (by default) or a tree, using
 *   its boolean option <systemitem>is_tree</systemitem>.
 *
 * You can now create the layout you want for donna. For example, to make a
 * dual-pane with one tree on the left, and two lists on the right, you could
 * use:
 *
 * <programlisting>
 * [layouts]
 * dualpane=paneH(!treeview:tree@230,paneV(treeview:listTop,treeview:listBtm))
 * </programlisting>
 *
 * Once your layout set, you simply need to tell donna to load it up on start,
 * which is done by setting its name under option donna/layout, e.g:
 *
 * <programlisting>
 * [donna]
 * layout=dualpane
 * </programlisting>
 *
 * Voilà!
 *
 * The default configuration actually comes with a few layouts you can try, or
 * use as examples in order to create your very own.
 *
 * It is also possible to define which layout should be used from the command
 * line, thus "overriding" option donna/layout, using
 * <systemitem>--layout</systemitem>
 * Note that it is, however, not possible to change layout while donna is
 * running.
 *
 * </para></refsect3>
 *
 * <refsect3 id="configuration">
 * <title>Configuration</title>
 * <para>
 * donna's configuration is loaded from a single text file, and then handled via
 * the configuration manager, which is also providing domain "config" as an
 * interface. See #DonnaProviderConfig.description
 * </para></refsect3>
 *
 * <refsect3 id="treeviews">
 * <title>Advanced Treeviews</title>
 * <para>
 * As you could expect, the main GUI component of donna is the treeview,
 * especially since it will handle both trees & lists.
 *
 * See #DonnaTreeView.description for more about all the many unique options/features both
 * trees & lists offer.
 * </para></refsect3>
 *
 * <refsect3 id="dynamic-arrangements">
 * <title>Dynamic Arrangements (on Lists)</title>
 * <para>
 * donna allows dynamic arrangements to be used on lists, to have specific
 * column layout/options, sort orders or color filters based on the the list's
 * current location.
 * See #arrangements for more.
 * </para></refsect3>
 *
 * <refsect3 id="node-visuals">
 * <title>Node Visuals</title>
 * <para>
 * Trees support #tree-visuals, allowing you to set row-specific name, icon,
 * box or highlight effect. It is also possible not to define those as
 * tree-specific settings, but have them set on the node itself.
 *
 * This is done by simply creating numbered categories under category
 * <systemitem>visuals</systemitem> in the configuration. Each category
 * represents a node visual definition, and must at least contain a string
 * option <systemitem>node</systemitem> which must be the full location of the
 * node on which the following visuals can be set (all string options):
 *
 * - <systemitem>name</systemitem>: custom name to be used. Set as string
 *   property <systemitem>visual-name</systemitem> on nodes.
 * - <systemitem>icon</systemitem>: custom icon to be used. Can be the full path
 *   to a picture file, or the name of an icon to be loaded from the theme.
 *   Set as string property <systemitem>visual-icon</systemitem> on nodes.
 * - <systemitem>box</systemitem>: name of the class for the box effect. Set as
 *   string property <systemitem>visual-box</systemitem> on nodes.
 * - <systemitem>highlight</systemitem>: name of the class for the highlight
 *   effect. Set as string property <systemitem>visual-highlight</systemitem> on
 *   nodes.
 *
 * Which visuals will actually be loaded/used on trees will depend on their
 * option <systemitem>node_visuals</systemitem>.
 * </para></refsect3>
 *
 * <refsect3 id="user-parsing">
 * <title>Full Location: prefixes, aliases and more</title>
 * <para>
 * As you might know, donna uses the concept of nodes (#DonnaNode) to represent
 * both items (e.g. files) & containers (e.g. folders) everywhere in the
 * application, starting with treeviews or menus.
 *
 * A node belongs to a domain, for example "fs" represents the filesystem,
 * "config" donna's configuration, etc
 * As a result, every location in donna is identified via a "full location." A
 * full location is a string made of the domain & the location within the
 * domain, separated by a colon. For example, when in <filename>/tmp</filename>
 * donna will refer to this as <systemitem>fs:/tmp</systemitem>
 *
 * This can be cumbersome to type, and is why you some facilities are available
 * when dealing with full locations, known as "user parsing" of full locations.
 * Note that there might be places where an actual full location is required
 * (e.g. in list/tree files), but all user input support this user parsing.
 *
 * See donna_app_parse_fl() for more on user parsing.
 * </para></refsect3>
 *
 * <refsect3 id="statusbar">
 * <title>Custom statusbar</title>
 * <para>
 * The statusbar, automatically displayed at the bottom of the main window if
 * defined, is made of as many "areas" as needed. You define the content of the
 * statusbar simply via string option <systemitem>statusbar/areas</systemitem>
 * which must simply be a comma-separated list of area names.
 *
 * Said name being the name of a section in configuration, under
 * <systemitem>statusbar</systemitem>
 * Each area is defined in said section, with at least one required string
 * option, <systemitem>source</systemitem>. The source is the component which
 * will handle the area (drawing, etc), and can be one of the following:
 *
 * - <systemitem>:task</systemitem> : the task manager, see #taskmanager-status
 * - <systemitem>:active</systemitem> : the treeview currently active-list
 * - <systemitem>:focused</systemitem> : the treeview currently focused
 * - or the name of a treeview
 *
 * Integer option <systemitem>width</systemitem> can be used to set the
 * (minimum) size of the area, and boolean option
 * <systemitem>expand</systemitem> can bet set to false if you don't want the
 * area to automatically expand when more space is available. By default, all
 * remaining space in the statusbar is distributed amongst all areas; setting
 * <systemitem>expand</systemitem> to false excludes the area, so it remains at
 * the specified size.
 *
 * Other options that can be used in the section depend on its source. For
 * treeviews, refer to #treeview-status.
 * </para></refsect3>
 *
 * </para></refsect2>
 *
 * <refsect2 id="css">
 * <title>CSS Customizations</title>
 * <para>
 * Being a GTK3 application, donna's appearance can be customized the same way
 * any other GTK3 application can, using some CSS.
 *
 * Every UI component (treeview, etc) in donna will have its name set and
 * available via CSS, so for a treeview "foobar" you can use "\#foobar" as
 * selector.
 *
 * In some dialogs, such as those of commands ask() or ask_text(), a title and
 * optionally a details text are featured. The former has class
 * <systemitem>title</systemitem> applied, while the later has class
 * <systemitem>details</systemitem>.
 *
 * <refsect3 id="CSS-treeviews">
 * <title>Treeview-specific CSS</title>
 * <para>
 * Treeviews also offer some special classes:
 *
 * - <systemitem>second-arrow</systemitem>: used to draw the arrow for secondary
 *   sort order
 * - <systemitem>focused-row</systemitem>: used on the focused row. Unlike
 *   pseudo-class <systemitem>:focused</systemitem> this one is applied on the
 *   focused row, regardless of the whether the treeview is focused or not.
 * - <systemitem>select-row-underline</systemitem>: used on the row underline
 *   effect, when applicable based on option
 *   <systemitem>select_highlight</systemitem>
 *
 * </para></refsect3>
 *
 * <refsect3 id="css-trees">
 * <title>Tree-specific CSS</title>
 * <para>
 * Trees have the following additional classes:
 *
 * - <systemitem>minitree-unknown</systemitem>: used on rows which have never
 *   been expanded
 * - <systemitem>minitree-partial</systemitem>: used on rows in partial expanded
 *   state. See #minitree for more on the expand state, and not that those
 *   classes are used regardless of the value of the
 *   <systemitem>is_minitree</systemitem> option (i.e. on maxitree as well).
 *
 * In addition, trees have some specific CSS that are used to apply the boxed
 * branch & highlight effects from #tree-visuals.
 *
 * For the box effect, a region <systemitem>boxed</systemitem> is created in the
 * expander area, that is meant to always be of the boxed color even when
 * focused/selected. See <filename>donnatella.css</filename> for examples.
 *
 * For the highlight effect, you can use special option
 * <systemitem>-DonnaTreeView-highlighted-size</systemitem> to define the width
 * by which the highlight effect should extend, making sure it remains visible
 * even when selected.
 * This will be available in CSS as region
 * <systemitem>highlight-overflow</systemitem>; Again you can refer to
 * <filename>donnatella.css</filename> to see how it's done.
 *
 * For both effects, a set of effects/classes are provided, each with a
 * different color. All classes for the box effect are prefixed with "box-"
 * while the ones for the highlight effect are prefixed with "hl-"
 * Classes are available for the following colors: pink, violet, black, white,
 * red, orange, lime, green, yellow, cyan, and blue.
 *
 * </para></refsect3>
 *
 * <refsect3 id="css-lists">
 * <title>List-specific CSS</title>
 * <para>
 * Lists also have additional classes applied, based on the domain of their
 * current location. A class by the name of the domain, prefixed with "domain-",
 * will be applied.
 * So e.g. when in the configuration (domain "config"), the class
 * <systemitem>domain-config</systemitem> will be applied to the treeview. By
 * default this is used to have a special background color on certain domains,
 * e.g. orange in config, blue on exec (e.g. search results).
 * </para></refsect3>
 *
 * <refsect3 id="css-statusbar">
 * <title>Statusbar-specific CSS</title>
 * <para>
 * The statusbar will also have a class applied on each area/section, the name
 * of said section (no prefix). (So it's probably best to use
 * <systemitem>DonnaStatusBar.section</systemitem> as selector) It also makes
 * sure that any font properties are applied, so you can set specific font
 * properties on a per-area basis.
 * </para></refsect3>
 *
 * </para></refsect2>
 */

enum
{
    PROP_0,

    PROP_ACTIVE_LIST,
    PROP_JUST_FOCUSED,

    NB_PROPS
};

enum
{
    TREE_VIEW_LOADED,
    EVENT,
    NB_SIGNALS
};

enum
{
    COL_TYPE_NAME = 0,
    COL_TYPE_SIZE,
    COL_TYPE_TIME,
    COL_TYPE_PERMS,
    COL_TYPE_TEXT,
    COL_TYPE_LABEL,
    COL_TYPE_PROGRESS,
    COL_TYPE_VALUE,
    NB_COL_TYPES
};

enum rc
{
    RC_OK = 0,
    RC_PARSE_CMDLINE_FAILED,
    RC_PREPARE_FAILED,
    RC_LAYOUT_MISSING,
    RC_LAYOUT_INVALID,
    RC_ACTIVE_LIST_MISSING
};

enum
{
    TITLE_DOMAIN_LOCATION,
    TITLE_DOMAIN_FULL_LOCATION,
    TITLE_DOMAIN_CUSTOM
};

struct visuals
{
    gchar *name;
    gchar *icon;
    gchar *box;
    gchar *highlight;
};

struct filter
{
    DonnaFilter *filter;
    guint        toggle_count;
    guint        timeout;
};

struct intref
{
    DonnaArgType type;
    gpointer     ptr;
    gint64       last;
};

enum
{
    ST_SCE_DONNA,
    ST_SCE_ACTIVE,
    ST_SCE_FOCUSED,
    ST_SCE_TASK,
};

struct provider
{
    DonnaStatusProvider *sp;
    guint                id;
};

struct status
{
    gchar   *name;
    guint    source;
    GArray  *providers;
};

struct _DonnaAppPrivate
{
    GtkWindow       *window;
    GSList          *windows;
    GtkWidget       *floating_window;
    gboolean         just_focused;
    gboolean         exiting;
    DonnaConfig     *config;
    DonnaTaskManager*task_manager;
    DonnaStatusBar  *sb;
    GSList          *tree_views;
    GSList          *arrangements;
    GThreadPool     *pool;
    DonnaTreeView   *active_list;
    DonnaTreeView   *focused_tree;
    gulong           sid_active_location;
    GSList          *statuses;
    gchar           *config_dir;
    gchar           *cur_dirname;
    /* visuals are under a RW lock so everyone can read them at the same time
     * (e.g. creating nodes, get_children() & the likes). The write operation
     * should be quite rare. */
    GRWLock          lock;
    GHashTable      *visuals;
    /* ct, providers, filters, intrefs, etc are all under the same lock because
     * there shouldn't be a need to separate them all. We use a recursive mutex
     * because we need it for filters, to handle correctly the toggle_ref */
    GRecMutex        rec_mutex;
    struct col_type
    {
        const gchar     *name;
        const gchar     *desc; /* i.e. config extra label */
        GType            type;
        DonnaColumnType *ct;
        gpointer         ct_data;
    } column_types[NB_COL_TYPES];
    GSList          *providers;
    GHashTable      *filters;
    GHashTable      *intrefs;
    guint            intrefs_timeout;
};

struct argmt
{
    gchar        *name;
    GPatternSpec *pspec;
};

static GThread *main_thread;
static GLogLevelFlags show_log = G_LOG_LEVEL_WARNING;
guint donna_debug_flags = 0;

static GParamSpec * donna_app_props[NB_PROPS] = { NULL, };
static guint        donna_app_signals[NB_SIGNALS] = { 0 };
static GSList *     event_confirm = NULL;

/* internal from treeview.c */
gboolean
_donna_tree_view_register_extras (DonnaConfig *config, GError **error);

/* internal from contextmenu.c */
gboolean
_donna_context_register_extras (DonnaConfig *config, GError **error);


static gboolean event_accumulator (GSignalInvocationHint    *ihint,
                                   GValue                   *value_accu,
                                   const GValue             *value_handler,
                                   gpointer                  data);

/* internal; used from treeview.c with its own get_ct_data */
gboolean
_donna_app_filter_nodes (DonnaApp        *app,
                         GPtrArray       *nodes,
                         const gchar     *filter_str,
                         get_ct_data_fn   get_ct_data,
                         gpointer         data,
                         GError         **error);


static void             donna_app_log_handler       (const gchar    *domain,
                                                     GLogLevelFlags  log_level,
                                                     const gchar    *message,
                                                     gpointer        data);
static void             donna_app_set_property      (GObject        *object,
                                                     guint           prop_id,
                                                     const GValue   *value,
                                                     GParamSpec     *pspec);
static void             donna_app_get_property      (GObject        *object,
                                                     guint           prop_id,
                                                     GValue         *value,
                                                     GParamSpec     *pspec);
static void             donna_app_finalize          (GObject        *object);


static void             new_node_cb                 (DonnaProvider  *provider,
                                                     DonnaNode      *node,
                                                     DonnaApp       *app);
static GSList *         load_arrangements           (DonnaConfig    *config,
                                                     const gchar    *sce);
static inline void      set_active_list             (DonnaApp       *app,
                                                     DonnaTreeView  *list);


G_DEFINE_TYPE (DonnaApp, donna_app, G_TYPE_OBJECT);

static void
donna_app_class_init (DonnaAppClass *klass)
{
    GObjectClass *o_class;

    o_class = G_OBJECT_CLASS (klass);
    o_class->set_property   = donna_app_set_property;
    o_class->get_property   = donna_app_get_property;
    o_class->finalize       = donna_app_finalize;

    /**
     * DonnaApp:active-list:
     *
     * The #DonnaTreeView thatis the active list.
     *
     * In case you use a #layout with more than one list, there is always one
     * that will be the "active" one. This is the one defining the app/current
     * location, or the one treeview that is used by commands when using
     * ":active" as special name.
     */
    donna_app_props[PROP_ACTIVE_LIST] =
        g_param_spec_object ("active-list", "active-list",
                "Active list",
                DONNA_TYPE_TREE_VIEW,
                G_PARAM_READWRITE);

    /**
     * DonnaApp:just-focused:
     *
     * Will be %TRUE when the application's main window was just focused, and
     * (it it was done via click) the click hasn't been processed/consumed yet.
     *
     * It will be %TRUE on focus-in and for 42 ms, unless it is set to %FALSE
     * (which could happen e.g. in a treeview when processing (or ignoring) the
     * click)
     */
    donna_app_props[PROP_JUST_FOCUSED] =
        g_param_spec_boolean ("just-focused", "just-focused",
                "Whether or not the main window was just focused",
                FALSE,  /* default */
                G_PARAM_READWRITE);

    g_object_class_install_properties (o_class, NB_PROPS, donna_app_props);

    /**
     * DonnaApp::tree-view-loaded:
     * @app: The #DonnaApp
     * @tree: The #DonnaTreeView just loaded
     *
     * Emitted when a treeview was loaded into the layout. This happens on app
     * startup (since treeviews cannot be (un)loaded at will), but allows e.g.
     * for a tree to synchonize with a list as soon as said list is loaded.
     */
    donna_app_signals[TREE_VIEW_LOADED] =
        g_signal_new ("tree-view-loaded",
            DONNA_TYPE_APP,
            G_SIGNAL_RUN_LAST,
            G_STRUCT_OFFSET (DonnaAppClass, tree_view_loaded),
            NULL,
            NULL,
            g_cclosure_marshal_VOID__OBJECT,
            G_TYPE_NONE,
            1,
            DONNA_TYPE_TREE_VIEW);

    /**
     * DonnaApp::event:
     *
     * Emitted whenever an event occurs in donna, via calls to
     * donna_app_emit_event()
     */
    donna_app_signals[EVENT] =
        g_signal_new ("event",
            DONNA_TYPE_APP,
            G_SIGNAL_RUN_LAST | G_SIGNAL_DETAILED,
            G_STRUCT_OFFSET (DonnaAppClass, event),
            event_accumulator,
            &event_confirm,
            g_cclosure_user_marshal_BOOLEAN__STRING_STRING_STRING_POINTER_POINTER,
            G_TYPE_BOOLEAN,
            5,
            G_TYPE_STRING,
            G_TYPE_STRING,
            G_TYPE_STRING,
            G_TYPE_POINTER,
            G_TYPE_POINTER);

    g_type_class_add_private (klass, sizeof (DonnaAppPrivate));
}

static void
donna_app_set_property (GObject       *object,
                        guint          prop_id,
                        const GValue  *value,
                        GParamSpec    *pspec)
{
    DonnaAppPrivate *priv = DONNA_APP (object)->priv;

    if (prop_id == PROP_ACTIVE_LIST)
        set_active_list ((DonnaApp *) object, g_value_get_object (value));
    else if (prop_id == PROP_JUST_FOCUSED)
        priv->just_focused = g_value_get_boolean (value);
}

static void
donna_app_get_property (GObject       *object,
                        guint          prop_id,
                        GValue        *value,
                        GParamSpec    *pspec)
{
    DonnaAppPrivate *priv = DONNA_APP (object)->priv;

    if (prop_id == PROP_ACTIVE_LIST)
        g_value_set_object (value, priv->active_list);
    else if (prop_id == PROP_JUST_FOCUSED)
        g_value_set_boolean (value, priv->just_focused);
}

static void
free_arrangements (GSList *list)
{
    GSList *l;

    for (l = list; l; l = l->next)
    {
        struct argmt *argmt = l->data;

        g_free (argmt->name);
        g_pattern_spec_free (argmt->pspec);
        g_free (argmt);
    }
    g_slist_free (list);
}

static void
donna_app_finalize (GObject *object)
{
    DonnaAppPrivate *priv;
    guint i;

    priv = DONNA_APP (object)->priv;

    g_free (priv->config_dir);
    g_rw_lock_clear (&priv->lock);
    g_rec_mutex_clear (&priv->rec_mutex);
    g_object_unref (priv->config);
    free_arrangements (priv->arrangements);
    g_hash_table_destroy (priv->filters);
    g_hash_table_destroy (priv->visuals);
    g_hash_table_destroy (priv->intrefs);
    g_thread_pool_free (priv->pool, TRUE, FALSE);

    for (i = 0; i < NB_COL_TYPES; ++i)
    {
        if (priv->column_types[i].ct_data)
            donna_column_type_free_data (priv->column_types[i].ct,
                    priv->column_types[i].ct_data);
        if (priv->column_types[i].ct)
            g_object_unref (priv->column_types[i].ct);
    }

    G_OBJECT_CLASS (donna_app_parent_class)->finalize (object);
}

static gboolean
donna_app_task_run (DonnaTask *task)
{
    donna_task_run (task);
    g_object_unref (task);
    return FALSE;
}

static void
free_visuals (struct visuals *visuals)
{
    g_free (visuals->name);
    g_free (visuals->icon);
    g_free (visuals->box);
    g_free (visuals->highlight);
    g_slice_free (struct visuals, visuals);
}

static void
free_filter (struct filter *f)
{
    g_object_unref (f->filter);
    g_free (f);
}

static void
free_intref (struct intref *ir)
{
    if (ir->type & DONNA_ARG_IS_ARRAY)
        g_ptr_array_unref (ir->ptr);
    else if (ir->type == DONNA_ARG_TYPE_TREE_VIEW || ir->type == DONNA_ARG_TYPE_NODE)
        g_object_unref (ir->ptr);
    else
        g_warning ("free_intref(): Invalid type: %d", ir->type);

    g_free (ir);
}

static void
donna_app_init (DonnaApp *app)
{
    DonnaAppPrivate *priv;

    main_thread = g_thread_self ();
    g_log_set_default_handler (donna_app_log_handler, NULL);

    priv = app->priv = G_TYPE_INSTANCE_GET_PRIVATE (app, DONNA_TYPE_APP, DonnaAppPrivate);

    g_rw_lock_init (&priv->lock);
    g_rec_mutex_init (&priv->rec_mutex);

    priv->config = g_object_new (DONNA_TYPE_PROVIDER_CONFIG, "app", app, NULL);
    g_signal_connect (priv->config, "new-node", (GCallback) new_node_cb, app);
    priv->column_types[COL_TYPE_NAME].name = "name";
    priv->column_types[COL_TYPE_NAME].desc = "Name (and Icon)";
    priv->column_types[COL_TYPE_NAME].type = DONNA_TYPE_COLUMN_TYPE_NAME;
    priv->column_types[COL_TYPE_SIZE].name = "size";
    priv->column_types[COL_TYPE_SIZE].desc = "Size";
    priv->column_types[COL_TYPE_SIZE].type = DONNA_TYPE_COLUMN_TYPE_SIZE;
    priv->column_types[COL_TYPE_TIME].name = "time";
    priv->column_types[COL_TYPE_TIME].desc = "Date/Time";
    priv->column_types[COL_TYPE_TIME].type = DONNA_TYPE_COLUMN_TYPE_TIME;
    priv->column_types[COL_TYPE_PERMS].name = "perms";
    priv->column_types[COL_TYPE_PERMS].desc = "Permissions";
    priv->column_types[COL_TYPE_PERMS].type = DONNA_TYPE_COLUMN_TYPE_PERMS;
    priv->column_types[COL_TYPE_TEXT].name = "text";
    priv->column_types[COL_TYPE_TEXT].desc = "Text";
    priv->column_types[COL_TYPE_TEXT].type = DONNA_TYPE_COLUMN_TYPE_TEXT;
    priv->column_types[COL_TYPE_LABEL].name = "label";
    priv->column_types[COL_TYPE_LABEL].desc = "Label";
    priv->column_types[COL_TYPE_LABEL].type = DONNA_TYPE_COLUMN_TYPE_LABEL;
    priv->column_types[COL_TYPE_PROGRESS].name = "progress";
    priv->column_types[COL_TYPE_PROGRESS].desc = "Progress bar";
    priv->column_types[COL_TYPE_PROGRESS].type = DONNA_TYPE_COLUMN_TYPE_PROGRESS;
    priv->column_types[COL_TYPE_VALUE].name = "value";
    priv->column_types[COL_TYPE_VALUE].desc = "Value (of config option)";
    priv->column_types[COL_TYPE_VALUE].type = DONNA_TYPE_COLUMN_TYPE_VALUE;

    priv->task_manager = g_object_new (DONNA_TYPE_PROVIDER_TASK, "app", app, NULL);
    g_signal_connect (priv->task_manager, "new-node", (GCallback) new_node_cb, app);

    priv->pool = g_thread_pool_new ((GFunc) donna_app_task_run, NULL,
            5, FALSE, NULL);

    priv->filters = g_hash_table_new_full (g_str_hash, g_str_equal,
            g_free, (GDestroyNotify) free_filter);

    priv->visuals = g_hash_table_new_full (g_str_hash, g_str_equal,
            g_free, (GDestroyNotify) free_visuals);

    priv->intrefs = g_hash_table_new_full (g_str_hash, g_str_equal,
            g_free, (GDestroyNotify) free_intref);
}

static void
donna_app_log_handler (const gchar    *domain,
                       GLogLevelFlags  log_level,
                       const gchar    *message,
                       gpointer        data)
{
    GThread *thread = g_thread_self ();
    time_t now;
    struct tm *tm;
    gchar buf[12];
    gboolean colors;
    GString *str;

    if (log_level > show_log)
        return;

    colors = isatty (fileno (stdout));

    now = time (NULL);
    tm = localtime (&now);
    strftime (buf, 12, "[%H:%M:%S] ", tm);
    str = g_string_new (buf);

    if (g_main_context_is_owner (g_main_context_default ()))
        g_string_append (str, "[UI] ");

    if (thread != main_thread)
        g_string_append_printf (str, "[thread %p] ", thread);

    if (log_level & G_LOG_LEVEL_ERROR)
        donna_g_string_append_concat (str,
                (colors) ? "\x1b[31m" : "** ",
                "ERROR: ",
                (colors) ? "\x1b[0m" : "",
                NULL);
    if (log_level & G_LOG_LEVEL_CRITICAL)
        donna_g_string_append_concat (str,
                (colors) ? "\x1b[1;31m" : "** ",
                "CRITICAL: ",
                (colors) ? "\x1b[0m" : "",
                NULL);
    if (log_level & G_LOG_LEVEL_WARNING)
        donna_g_string_append_concat (str,
                (colors) ? "\x1b[33m" : "",
                "WARNING: ",
                (colors) ? "\x1b[0m" : "",
                NULL);
    if (log_level & G_LOG_LEVEL_MESSAGE)
        g_string_append (str, "MESSAGE: ");
    if (log_level & G_LOG_LEVEL_INFO)
        g_string_append (str, "INFO: ");
    if (log_level & G_LOG_LEVEL_DEBUG)
        g_string_append (str, "DEBUG: ");
    /* custom/user log levels, for extra debug verbosity */
    if (log_level & DONNA_LOG_LEVEL_DEBUG2)
        g_string_append (str, "DEBUG: ");
    if (log_level & DONNA_LOG_LEVEL_DEBUG3)
        g_string_append (str, "DEBUG: ");
    if (log_level & DONNA_LOG_LEVEL_DEBUG4)
        g_string_append (str, "DEBUG: ");

    if (domain)
        donna_g_string_append_concat (str, "[", domain, "] ", NULL);

    g_string_append (str, message);
    puts (str->str);
    g_string_free (str, TRUE);

#ifdef DONNA_DEBUG_AUTOBREAK
    if (log_level & G_LOG_LEVEL_CRITICAL)
    {
        gboolean under_gdb = FALSE;
        FILE *f;
        gchar buffer[64];

        /* try to determine if we're running under GDB or not, and if so we
         * break. This is done by reading our /proc/PID/status and checking if
         * TracerPid if non-zero or not.
         * This doesn't guarantee GDB, and we don't check the name of that PID,
         * because this is a dev thing and good enough for me.
         * We also don't cache this info so we can attach/detach without
         * worries, and when attached it will break automagically.
         */

        snprintf (buffer, 64, "/proc/%d/status", getpid ());
        f = fopen (buffer, "r");
        if (f)
        {
            while ((fgets (buffer, 64, f)))
            {
                if (streqn ("TracerPid:\t", buffer, 11))
                {
                    under_gdb = buffer[11] != '0';
                    break;
                }
            }
            fclose (f);
        }

        if (under_gdb)
            GDB (1);
    }
#endif
}


/* signals */

static gboolean
event_accumulator (GSignalInvocationHint    *ihint,
                   GValue                   *value_accu,
                   const GValue             *value_handler,
                   gpointer                  data)
{
    GSList *l = * (GSList **) data;
    gboolean is_confirm = FALSE;

    for ( ; l; l = l->next)
        if ((GQuark) GPOINTER_TO_UINT (l->data) == ihint->detail)
        {
            is_confirm = TRUE;
            break;
        }

    if (!is_confirm)
        return TRUE;

    if (g_value_get_boolean (value_handler))
    {
        g_value_set_boolean (value_accu, TRUE);
        return FALSE;
    }

    return TRUE;
}


/* API */

/**
 * donna_app_ensure_focused:
 * @app: The #DonnaApp
 *
 * Makes sure the main window (toplevel) is focused, and if not "present" (GTK
 * terminology) it
 */
void
donna_app_ensure_focused (DonnaApp       *app)
{
    DonnaAppPrivate *priv;

    g_return_if_fail (DONNA_IS_APP (app));
    priv = app->priv;

    if (!gtk_window_has_toplevel_focus (priv->window))
        gtk_window_present_with_time (priv->window, GDK_CURRENT_TIME);
}

/**
 * donna_app_move_focus:
 * @app: The #DonnaApp
 * @move: How many times to move focus
 *
 * Moves the focus @move times to the next (previous if negative) widget in main
 * window
 *
 * Typical use would be for handling [Shift+]Tab keys
 */
void
donna_app_move_focus (DonnaApp          *app,
                      gint               move)
{
    g_return_if_fail (DONNA_IS_APP (app));
    while (move != 0)
    {
        gtk_widget_child_focus ((GtkWidget *) app->priv->window,
                (move > 0) ? GTK_DIR_TAB_FORWARD : GTK_DIR_TAB_BACKWARD);
        if (move > 0)
            --move;
        else
            ++move;
    }
}

/**
 * donna_app_set_focus:
 * @app: The #DonnaApp
 * @type: The type of GUI element to focus
 * @name: The name of the element to focus
 * @error: Return location of a #GError, or %NULL
 *
 * Set the focus to the GUI element @name of type @type
 *
 * Returns: %TRUE on success, else %FALSE
 */
gboolean
donna_app_set_focus (DonnaApp       *app,
                     const gchar    *type,
                     const gchar    *name,
                     GError        **error)
{
    GtkWidget *w = NULL;

    g_return_val_if_fail (DONNA_IS_APP (app), FALSE);

    if (streq (type, "treeview"))
        w = (GtkWidget *) donna_app_get_tree_view (app, name);
    else
    {
        g_set_error (error, DONNA_APP_ERROR, DONNA_APP_ERROR_UNKNOWN_TYPE,
                "Cannot set focus, unknown type of GUI element: '%s'",
                type);
        return FALSE;
    }

    if (!w)
    {
        g_set_error (error, DONNA_APP_ERROR, DONNA_APP_ERROR_NOT_FOUND,
                "Cannot set focus to TreeView '%s': not found",
                name);
        return FALSE;
    }

    gtk_widget_grab_focus (w);
    return TRUE;
}

static void
window_destroyed (GtkWindow *window, DonnaApp *app)
{
    app->priv->windows = g_slist_remove (app->priv->windows, window);
}

/**
 * donna_app_add_window:
 * @app: The #DonnaApp
 * @window: The %GtkWindow to add
 * @destroy_with_parent: Whether to destroy @window with the main window or not
 *
 * This will make @window transient for the main window, and if
 * @destroy_with_parent is %TRUE will make sure it gets destroyed alongside the
 * main window.
 */
void
donna_app_add_window (DonnaApp       *app,
                      GtkWindow      *window,
                      gboolean        destroy_with_parent)
{
    DonnaAppPrivate *priv;

    g_return_if_fail (DONNA_IS_APP (app));
    g_return_if_fail (GTK_IS_WINDOW (window));
    priv = app->priv;

    gtk_window_set_transient_for (window, priv->window);
    if (destroy_with_parent)
    {
        g_signal_connect (window, "destroy", (GCallback) window_destroyed, app);
        priv->windows = g_slist_prepend (priv->windows, window);
    }
}

static void
floating_window_destroy_cb (GtkWidget *w, DonnaApp *app)
{
    app->priv->floating_window = NULL;
}

/**
 * donna_app_set_floating_window:
 * @app: The #DonnaApp
 * @window: The window to become the new app's floating window
 *
 * At any given time, the app can only have one floating window (e.g. window to
 * show/set permissions on a file). Once the window has been created, it should
 * be passed to this function to destroy any previous floating window, and set
 * @window as new floating window.
 *
 * Floating window will automatically be destroyed if the main window is focused
 * again. When @window is destroyed, the floating window internal pointer is
 * automacially reset (i.e. don't call this with %NULL as @window)
 */
void
donna_app_set_floating_window (DonnaApp       *app,
                               GtkWindow      *window)
{
    DonnaAppPrivate *priv;

    g_return_if_fail (DONNA_IS_APP (app));
    g_return_if_fail (window == NULL || GTK_IS_WINDOW (window));
    priv = app->priv;

    if (priv->floating_window)
    {
        gtk_widget_destroy (priv->floating_window);
        priv->floating_window = NULL;
    }

    /* make sure all events are processed before we switch to the new window,
     * otherwise this could lead to immediate destruction of said new floating
     * window */
    while (gtk_events_pending ())
        gtk_main_iteration ();

    priv->floating_window = (GtkWidget *) window;
    g_signal_connect (window, "destroy",
            (GCallback) floating_window_destroy_cb, app);
}

/**
 * donna_app_get_config:
 * @app: The #DonnaApp
 *
 * Returns the configuration manager with an added reference. If you don't need
 * it, use donna_app_peek_config()
 *
 * Returns: (transfer full): The configuration manager; call g_object_unref()
 * when done
 */
DonnaConfig *
donna_app_get_config (DonnaApp       *app)
{
    g_return_val_if_fail (DONNA_IS_APP (app), NULL);
    return g_object_ref (app->priv->config);
}

/**
 * donna_app_peek_config:
 * @app: The #DonnaApp
 *
 * Returns the configuration manager without adding a reference.
 *
 * Returns: (transfer none): The configuration manager
 */
DonnaConfig *
donna_app_peek_config (DonnaApp *app)
{
    g_return_val_if_fail (DONNA_IS_APP (app), NULL);
    return app->priv->config;
}

static gboolean
visual_refresher (DonnaTask *task, DonnaNode *node, const gchar *name)
{
    /* FIXME: should we do something here? */
    return TRUE;
}

static void
new_node_cb (DonnaProvider *provider, DonnaNode *node, DonnaApp *app)
{
    gchar *fl;
    struct visuals *visuals;

    fl = donna_node_get_full_location (node);
    g_rw_lock_reader_lock (&app->priv->lock);
    visuals = g_hash_table_lookup (app->priv->visuals, fl);
    if (visuals)
    {
        GValue value = G_VALUE_INIT;

        if (visuals->name)
        {
            g_value_init (&value, G_TYPE_STRING);
            g_value_set_string (&value, visuals->name);
            donna_node_add_property (node, "visual-name", G_TYPE_STRING, &value,
                    visual_refresher, NULL, NULL);
            g_value_unset (&value);
        }

        if (visuals->icon)
        {
            GIcon *icon;

            if (*visuals->icon == '/')
            {
                GFile *file;

                file = g_file_new_for_path (visuals->icon);
                icon = g_file_icon_new (file);
                g_object_unref (file);
            }
            else
                icon = g_themed_icon_new (visuals->icon);

            if (icon)
            {
                g_value_init (&value, G_TYPE_ICON);
                g_value_take_object (&value, icon);
                donna_node_add_property (node, "visual-icon", G_TYPE_ICON, &value,
                        visual_refresher, NULL, NULL);
                g_value_unset (&value);
            }
        }

        if (visuals->box)
        {
            g_value_init (&value, G_TYPE_STRING);
            g_value_set_string (&value, visuals->box);
            donna_node_add_property (node, "visual-box", G_TYPE_STRING, &value,
                    visual_refresher, NULL, NULL);
            g_value_unset (&value);
        }

        if (visuals->highlight)
        {
            g_value_init (&value, G_TYPE_STRING);
            g_value_set_string (&value, visuals->highlight);
            donna_node_add_property (node, "visual-highlight", G_TYPE_STRING, &value,
                    visual_refresher, NULL, NULL);
            g_value_unset (&value);
        }
    }
    g_rw_lock_reader_unlock (&app->priv->lock);
    g_free (fl);
}

/**
 * donna_app_get_provider:
 * @app: The #DonnaApp
 * @domain: The domain you want the provider of
 *
 * Returns the provider for @domain
 *
 * Returns: (transfer full): The provider of @domain, or %NULL
 */
DonnaProvider *
donna_app_get_provider (DonnaApp       *app,
                        const gchar    *domain)
{
    DonnaAppPrivate *priv;
    DonnaProvider *provider = NULL;
    GSList *l;

    g_return_val_if_fail (DONNA_IS_APP (app), NULL);
    g_return_val_if_fail (domain != NULL, NULL);
    priv = app->priv;

    if (streq (domain, "config"))
        return g_object_ref (priv->config);
    else if (streq (domain, "task"))
        return g_object_ref (priv->task_manager);

    g_rec_mutex_lock (&priv->rec_mutex);
    for (l = priv->providers; l; l = l->next)
        if (streq (domain, donna_provider_get_domain (l->data)))
        {
            g_rec_mutex_unlock (&priv->rec_mutex);
            return g_object_ref (l->data);
        }

    if (streq (domain, "fs"))
        provider = g_object_new (DONNA_TYPE_PROVIDER_FS, "app", app, NULL);
    else if (streq (domain, "command"))
        provider = g_object_new (DONNA_TYPE_PROVIDER_COMMAND, "app", app, NULL);
    else if (streq (domain, "exec"))
        provider = g_object_new (DONNA_TYPE_PROVIDER_EXEC, "app", app, NULL);
    else if (streq (domain, "register"))
        provider = g_object_new (DONNA_TYPE_PROVIDER_REGISTER, "app", app, NULL);
    else if (streq (domain, "internal"))
        provider = g_object_new (DONNA_TYPE_PROVIDER_INTERNAL, "app", app, NULL);
    else if (streq (domain, "mark"))
        provider = g_object_new (DONNA_TYPE_PROVIDER_MARK, "app", app, NULL);
    else if (streq (domain, "invalid"))
        provider = g_object_new (DONNA_TYPE_PROVIDER_INVALID, "app", app, NULL);
    else
    {
        g_rec_mutex_unlock (&priv->rec_mutex);
        return NULL;
    }

    g_signal_connect (provider, "new-node", (GCallback) new_node_cb, app);
    priv->providers = g_slist_prepend (priv->providers, provider);
    g_rec_mutex_unlock (&priv->rec_mutex);
    return g_object_ref (provider);
}

/**
 * donna_app_get_node:
 * @app: The #DonnaApp
 * @full_location: The full location of the wanted node
 * @do_user_parse: Whether to do user parsing of @full_location or not
 * @error: (allow-none): Return location of a #GError, or %NULL
 *
 * Helper function to get the node corresponding to @full_location, optionally
 * after having applied #user-parsing to @full_location
 *
 * Returns: (transfer full): The #DonnaNode for (user-parsed) @full_location
 */
DonnaNode *
donna_app_get_node (DonnaApp    *app,
                    const gchar *full_location,
                    gboolean     do_user_parse,
                    GError     **error)
{
    DonnaProvider *provider;
    DonnaNode *node;
    gchar buf[64], *b = buf;
    const gchar *location;
    gchar *fl = NULL;

    g_return_val_if_fail (DONNA_IS_APP (app), NULL);
    g_return_val_if_fail (full_location != NULL, NULL);

    if (do_user_parse)
    {
        fl = donna_app_parse_fl (app, (gchar *) full_location, FALSE,
                NULL, NULL, NULL, NULL);
        full_location = fl;
    }

    location = strchr (full_location, ':');
    if (!location)
    {
        g_set_error (error, DONNA_APP_ERROR, DONNA_APP_ERROR_OTHER,
                "Invalid full location: '%s'", full_location);
        g_free (fl);
        return NULL;
    }

    if (G_UNLIKELY (location - full_location >= 64))
        b = g_strdup_printf ("%.*s", (gint) (location - full_location),
                full_location);
    else
    {
        *buf = '\0';
        strncat (buf, full_location, (size_t) (location - full_location));
    }
    provider = donna_app_get_provider (app, buf);
    if (!provider)
    {
        g_set_error (error, DONNA_APP_ERROR, DONNA_APP_ERROR_OTHER,
                "Unknown provider: '%s'", b);
        if (b != buf)
            g_free (b);
        g_free (fl);
        return NULL;
    }
    if (b != buf)
        g_free (b);

    node = donna_provider_get_node (provider, ++location, error);
    g_object_unref (provider);
    g_free (fl);
    return node;
}

struct trigger_node
{
    DonnaApp *app;
    DonnaNode *node;
};

static void
free_tn (struct trigger_node *tn)
{
    g_object_unref (tn->node);
    g_slice_free (struct trigger_node, tn);
}

static void
trigger_node_cb (DonnaTask *task, gboolean timeout_called, struct trigger_node *tn)
{
    if (donna_task_get_state (task) == DONNA_TASK_FAILED)
    {
        gchar *fl = donna_node_get_full_location (tn->node);
        donna_app_show_error (tn->app, donna_task_get_error (task),
                "Failed to trigger node '%s'", fl);
        g_free (fl);
    }
    free_tn (tn);
}

/**
 * donna_app_trigger_node:
 * @app: The #DonnaApp
 * @full_location: The full location of the wanted node to trigger
 * @do_user_parse: Whether to do user parsing of @full_location or not
 * @error: (allow-none): Return location of a #GError, or %NULL
 *
 * Helper function to trigger the node corresponding to @full_location,
 * optionally after having applied #user-parsing to @full_location
 *
 * Returns: %TRUE if the corresponding task was run (doesn't mean it succedeed,
 * or even that it has started yet), else %FALSE
 */
gboolean
donna_app_trigger_node (DonnaApp       *app,
                        const gchar    *full_location,
                        gboolean        do_user_parse,
                        GError        **error)
{
    DonnaNode *node;
    DonnaTask *task;
    struct trigger_node *tn;

    g_return_val_if_fail (DONNA_IS_APP (app), FALSE);
    g_return_val_if_fail (full_location != NULL, FALSE);

    node = donna_app_get_node (app, full_location, do_user_parse, error);
    if (!node)
        return FALSE;

    task = donna_node_trigger_task (node, error);
    if (!task)
    {
        g_object_unref (node);
        return FALSE;
    }

    tn = g_slice_new (struct trigger_node);
    tn->app = app;
    tn->node = node;

    donna_task_set_callback (task, (task_callback_fn) trigger_node_cb, tn,
            (GDestroyNotify) free_tn);
    donna_app_run_task (app, task);
    return TRUE;
}

/**
 * donna_app_get_column_type:
 * @app: The #DonnaApp
 * @type: Name of the wanted columntype
 *
 * Returns the columntype for @type
 *
 * Returns: (transfer full): The #DonnaColumnType for @type, or %NULL
 */
DonnaColumnType *
donna_app_get_column_type (DonnaApp      *app,
                           const gchar   *type)
{
    DonnaAppPrivate *priv;
    gint i;

    g_return_val_if_fail (DONNA_IS_APP (app), NULL);
    g_return_val_if_fail (type != NULL, NULL);
    priv = app->priv;

    g_rec_mutex_lock (&priv->rec_mutex);
    for (i = 0; i < NB_COL_TYPES; ++i)
    {
        if (streq (type, priv->column_types[i].name))
        {
            if (!priv->column_types[i].ct)
                priv->column_types[i].ct = g_object_new (
                        priv->column_types[i].type, "app", app, NULL);
            break;
        }
    }
    g_rec_mutex_unlock (&priv->rec_mutex);
    return (i < NB_COL_TYPES) ? g_object_ref (priv->column_types[i].ct) : NULL;
}

struct filter_toggle
{
    DonnaApp *app;
    gchar *filter_str;
};

static void
free_filter_toggle (struct filter_toggle *t)
{
    g_free (t->filter_str);
    g_free (t);
}

static gboolean
filter_remove (struct filter_toggle *t)
{
    DonnaAppPrivate *priv = t->app->priv;
    struct filter *f;

    g_rec_mutex_lock (&priv->rec_mutex);
    f = g_hash_table_lookup (priv->filters, t->filter_str);
    if (f->toggle_count > 0)
    {
        g_rec_mutex_unlock (&priv->rec_mutex);
        return FALSE;
    }

    /* will also unref filter */
    g_hash_table_remove (priv->filters, t->filter_str);
    g_rec_mutex_unlock (&priv->rec_mutex);
    return FALSE;
}

/* see node_toggle_ref_cb() in provider-base.c for more. Here we only add a
 * little extra: we don't unref/remove the filter (from hashtable) right away,
 * but after a little delay.
 * Mostly useful since on each location change/new arrangement, all color
 * filters are let go, then loaded again (assuming they stay active). */
static void
filter_toggle_ref_cb (DonnaApp *app, DonnaFilter *filter, gboolean is_last)
{
    DonnaAppPrivate *priv = app->priv;
    struct filter *f;
    gchar *filter_str;

    g_rec_mutex_lock (&priv->rec_mutex);
    /* can NOT use g_object_get, as it takes a ref on the object! */
    filter_str = donna_filter_get_filter (filter);
    f = g_hash_table_lookup (priv->filters, filter_str);
    if (is_last)
    {
        struct filter_toggle *t;

        if (f->timeout)
            g_source_remove (f->timeout);
        if (--f->toggle_count > 0)
        {
            g_rec_mutex_unlock (&priv->rec_mutex);
            g_free (filter_str);
            return;
        }
        t = g_new (struct filter_toggle, 1);
        t->app = app;
        t->filter_str = filter_str;
        f->timeout = g_timeout_add_seconds_full (G_PRIORITY_LOW,
                60 * 15, /* 15min */
                (GSourceFunc) filter_remove,
                t, (GDestroyNotify) free_filter_toggle);
    }
    else
    {
        ++f->toggle_count;
        if (f->timeout)
        {
            g_source_remove (f->timeout);
            f->timeout = 0;
        }
        g_free (filter_str);
    }
    g_rec_mutex_unlock (&priv->rec_mutex);
}

DonnaFilter *
donna_app_get_filter (DonnaApp       *app,
                      const gchar    *filter)
{
    DonnaAppPrivate *priv;
    struct filter *f;

    g_return_val_if_fail (DONNA_IS_APP (app), NULL);
    g_return_val_if_fail (filter != NULL, NULL);
    priv = app->priv;

    g_rec_mutex_lock (&priv->rec_mutex);
    f = g_hash_table_lookup (priv->filters, filter);
    if (!f)
    {
        f = g_new (struct filter, 1);
        f->filter = g_object_new (DONNA_TYPE_FILTER,
                "app",      app,
                "filter",   filter,
                NULL);
        f->toggle_count = 1;
        f->timeout = 0;
        /* add a toggle ref, which adds a strong ref to filter */
        g_object_add_toggle_ref ((GObject *) f->filter,
                (GToggleNotify) filter_toggle_ref_cb, app);
        g_hash_table_insert (priv->filters, g_strdup (filter), f);
    }
    else
        g_object_ref (f->filter);
    g_rec_mutex_unlock (&priv->rec_mutex);

    return f->filter;
}

/**
 * donna_app_run_task:
 * @app: The #DonnaApp
 * @task: The #DonnaTask to run
 *
 * This is how every #DonnaTask should always be run, regardless of what it
 * is/how it should run. It will run the task according to its visibility:
 *
 * - %DONNA_TASK_VISIBILITY_INTERNAL_GUI tasks are run in the main/UI thread
 * - %DONNA_TASK_VISIBILITY_INTERNAL_FAST tasks are run in the current thread
 * - %DONNA_TASK_VISIBILITY_INTERNAL tasks are run in a thread for the internal
 *   thread pool
 * - %DONNA_TASK_VISIBILITY_PULIC tasks are deferred to the task manager via
 *   donna_task_manager_add_task()
 *
 * If you need to run a task and wait for it to be done in a blocking manner
 * (i.e. you cant use a callback via donna_task_set_callback()), you might use
 * donna_task_wait_for_it()
 * When doing so from a task worker, see helper donna_app_run_task_and_wait()
 */
void
donna_app_run_task (DonnaApp       *app,
                    DonnaTask      *task)
{
    DonnaTaskVisibility visibility;

    g_return_if_fail (DONNA_IS_APP (app));
    g_return_if_fail (DONNA_IS_TASK (task));

    donna_task_prepare (task);
    g_object_get (task, "visibility", &visibility, NULL);
    if (visibility == DONNA_TASK_VISIBILITY_INTERNAL_GUI)
        g_main_context_invoke (NULL, (GSourceFunc) donna_app_task_run,
                g_object_ref_sink (task));
    else if (visibility == DONNA_TASK_VISIBILITY_INTERNAL_FAST)
        donna_app_task_run (g_object_ref_sink (task));
    else if (visibility == DONNA_TASK_VISIBILITY_PULIC)
        donna_task_manager_add_task (app->priv->task_manager, task, NULL);
    else
        g_thread_pool_push (app->priv->pool, g_object_ref_sink (task), NULL);
}

/**
 * donna_app_run_task_and_wait:
 * @app: The #DonnaApp
 * @task: The #DonnaTask to run
 * @current_task: The current #DonnaTask
 * @error: (allow-none): Return location of a #GError, or %NULL
 *
 * This is an helper meant to be used from a task worker, that of @current_task,
 * and it will run @task and block until it is done, using
 * donna_task_wait_for_it() It will also change the visibility of @task from
 * %DONNA_TASK_VISIBILITY_INTERNAL to %DONNA_TASK_VISIBILITY_INTERNAL_FAST so it
 * runs in the current thread instead of using another thread uselessly.
 *
 * Returns: See donna_task_wait_for_it()
 */
gboolean
donna_app_run_task_and_wait (DonnaApp       *app,
                             DonnaTask      *task,
                             DonnaTask      *current_task,
                             GError        **error)
{
    DonnaTaskVisibility visibility;

    g_return_val_if_fail (DONNA_IS_APP (app), FALSE);
    g_return_val_if_fail (DONNA_IS_TASK (task), FALSE);
    g_return_val_if_fail (DONNA_IS_TASK (current_task), FALSE);

    g_object_get (task, "visibility", &visibility, NULL);
    if (visibility == DONNA_TASK_VISIBILITY_INTERNAL)
        /* make it FAST so it runs inside the current thread instead of a new
         * one. This in intended to be used from a task worker, so no need to
         * "waste" an internal thread for no reason. */
        donna_task_set_visibility (task, DONNA_TASK_VISIBILITY_INTERNAL_FAST);

    donna_app_run_task (app, task);
    return donna_task_wait_for_it (task, current_task, error);
}

/**
 * donna_app_peek_task_manager:
 * @app: The #DonnaApp
 *
 * Returns the task manager
 *
 * Returns: (transfer none): The #DonnaTaskManager
 */
DonnaTaskManager *
donna_app_peek_task_manager (DonnaApp       *app)
{
    g_return_val_if_fail (DONNA_IS_APP (app), NULL);
    return app->priv->task_manager;
}

/**
 * donna_app_get_tree_view:
 * @app: The #DonnaApp
 * @name: The name of the wanted treeview
 *
 * Returns the treeview @name
 *
 * Note: On app startup, you might try to get a treeview that hasn't yet been
 * loaded. See signal #DonnaApp::tree-view-loaded for such cases.
 *
 * Returns: (transfer full): The #DonnaTreeView @name, or %NULL
 */
DonnaTreeView *
donna_app_get_tree_view (DonnaApp    *app,
                         const gchar *name)
{
    DonnaAppPrivate *priv;
    DonnaTreeView *tree = NULL;
    GSList *l;

    g_return_val_if_fail (DONNA_IS_APP (app), NULL);
    g_return_val_if_fail (name != NULL, NULL);
    priv = app->priv;

    for (l = priv->tree_views; l; l = l->next)
    {
        if (streq (name, donna_tree_view_get_name (l->data)))
        {
            tree = g_object_ref (l->data);
            break;
        }
    }
    return tree;
}

/**
 * donna_app_get_current_location:
 * @app: The #DonnaApp
 * @error: Return location of a #GError, or %NULL
 *
 * Helper to get the node of the current location (of the active list)
 *
 * Returns: (transfer full): The #DonnaNode of the current location of
 * #DonnaApp:active-list
 */
DonnaNode *
donna_app_get_current_location (DonnaApp       *app,
                                GError        **error)
{
    DonnaNode *node;

    g_return_val_if_fail (DONNA_IS_APP (app), NULL);

    if (!app->priv->active_list)
    {
        g_set_error (error, DONNA_APP_ERROR, DONNA_APP_ERROR_OTHER,
                "Cannot get current location: failed to get active-list");
        return NULL;
    }

    g_object_get (app->priv->active_list, "location", &node, NULL);
    if (!node)
    {
        g_set_error (error, DONNA_APP_ERROR, DONNA_APP_ERROR_OTHER,
                "Cannot get current location: failed to get it from treeview '%s'",
                donna_tree_view_get_name (app->priv->active_list));
        return NULL;
    }

    return node;
}

/**
 * donna_app_get_current_dirname:
 * @app: The #DonnaApp
 *
 * Returns the full path of the current directory. The current directory is the
 * last known location of the active list in domain "fs"
 * So if you changed active-list, or changed location of the active list, to a
 * location outside of "fs" (e.g. in "config") then this will still return the
 * last location in "fs" whereas donna_app_get_current_location() will return
 * the node of the current location (in "config")
 *
 * This is therefore useful to always get a valid path (in "fs"), e.g. when
 * running external scripts/applications needing a working directory.
 *
 * Returns: (transfer full): A newly allocated string of the path of the current
 * directory (free it with g_free() when done), or %NULL
 */
gchar *
donna_app_get_current_dirname (DonnaApp       *app)
{
    g_return_val_if_fail (DONNA_IS_APP (app), NULL);
    return g_strdup (app->priv->cur_dirname);
}

/**
 * donna_app_get_conf_filename:
 * @app: The #DonnaApp
 * @fmt: printf-like string for the filename
 * @...: %NULL-terminated list of printf-like arguments
 *
 * Returns the full path for a filename named according to @fmt and located in
 * the application's configuration directory.
 *
 * Returns: (transfer full): A newly allocated string in the filename encoding;
 * Free using g_free() when done
 */
gchar *
donna_app_get_conf_filename (DonnaApp       *app,
                             const gchar    *fmt,
                             ...)
{
    GString *str;
    va_list va_args;

    g_return_val_if_fail (DONNA_IS_APP (app), NULL);
    g_return_val_if_fail (fmt != NULL, NULL);

    va_start (va_args, fmt);
    str = g_string_new (app->priv->config_dir);
    g_string_append_c (str, '/');
    g_string_append_vprintf (str, fmt, va_args);
    va_end (va_args);

    if (!g_get_filename_charsets (NULL))
    {
        gchar *s;
        s = g_filename_from_utf8 (str->str, -1, NULL, NULL, NULL);
        if (s)
        {
            g_string_free (str, TRUE);
            return s;
        }
    }

    return g_string_free (str, FALSE);
}

static gboolean
intrefs_remove (gchar *key, struct intref *ir, gpointer data)
{
    /* remove after 15min */
    return ir->last + (G_USEC_PER_SEC * 60 * 15) - g_get_monotonic_time () <= 0;
}

static gboolean
intrefs_gc (DonnaApp *app)
{
    DonnaAppPrivate *priv = app->priv;
    gboolean keep_going;

    g_rec_mutex_lock (&priv->rec_mutex);
    g_hash_table_foreach_remove (priv->intrefs, (GHRFunc) intrefs_remove, NULL);
    keep_going = g_hash_table_size (priv->intrefs) > 0;
    if (!keep_going)
        priv->intrefs_timeout = 0;
    g_rec_mutex_unlock (&priv->rec_mutex);

    return keep_going;
}

/**
 * donna_app_new_int_ref:
 * @app: The #DonnaApp
 * @type: The type of the object to create an intref for
 * @ptr: The pointer to the object to store in the intref
 *
 * Creates a new intref (internal reference) for @ptr, object of type @type
 *
 * When a command returns an "object" (node, treeview, arrays, etc) it might
 * have to be represented as a string, e.g. in order to be used as argument in
 * another command (or via script).
 *
 * Sometimes it is possible to use a "direct" string representation, e.g.
 * strings (!) or treeviews, identified with their names. When it isn't
 * possible, by default intrefs will be used, to provide a typed "link" to the
 * object in memory.
 *
 * Once created the intref can now be accessed via the returned string, which is
 * a number in between inequality signs. This string can be used to then
 * accessed the object in memory via (other) commands.
 *
 * It should be noted that all intrefs should be freed after use, and that as a
 * "garbage collecting" process, all intrefs will be freed automatically after
 * 15 minutes of inactivity.
 *
 * Returns: (transfer full): A newly allocated string representing the intref;
 * Must be free-d using g_free()
 */
gchar *
donna_app_new_int_ref (DonnaApp       *app,
                       DonnaArgType    type,
                       gpointer        ptr)
{
    DonnaAppPrivate *priv;
    struct intref *ir;
    gchar *s;

    g_return_val_if_fail (DONNA_IS_APP (app), NULL);
    g_return_val_if_fail (ptr != NULL, NULL);
    g_return_val_if_fail (type == DONNA_ARG_TYPE_TREE_VIEW
            || type == DONNA_ARG_TYPE_NODE
            || (type & DONNA_ARG_IS_ARRAY), NULL);
    priv = app->priv;

    ir = g_new (struct intref, 1);
    ir->type = type;
    if (type & DONNA_ARG_IS_ARRAY)
        ir->ptr = g_ptr_array_ref (ptr);
    else if (type & (DONNA_ARG_TYPE_TREE_VIEW | DONNA_ARG_TYPE_NODE))
        ir->ptr = g_object_ref (ptr);
    else
        ir->ptr = ptr;
    ir->last = g_get_monotonic_time ();

    s = g_strdup_printf ("<%u%u>", rand (), (guint) (gintptr) ir);
    g_rec_mutex_lock (&priv->rec_mutex);
    g_hash_table_insert (priv->intrefs, g_strdup (s), ir);
    if (priv->intrefs_timeout == 0)
        priv->intrefs_timeout = g_timeout_add_seconds_full (G_PRIORITY_LOW,
                60 * 15, /* 15min */
                (GSourceFunc) intrefs_gc, app, NULL);
    g_rec_mutex_unlock (&priv->rec_mutex);
    return s;
}

/**
 * donna_app_get_int_ref:
 * @app: The #DonnaApp
 * @intref: The string representation of af intref, as returned by
 * donna_app_new_int_ref()
 * @type: The type of the requested intref
 *
 * Returns the intref identified by @intref if it is of type @type, else (or if
 * no sch intref exists) %NULL will be returned
 *
 * Returns: (transfer full): The object pointed to by @intref (of type @type),
 * with an added reference. Removing it depends on the type of the returned
 * object (e.g. g_object_unref() or g_ptr_array_unref())
 */
gpointer
donna_app_get_int_ref (DonnaApp       *app,
                       const gchar    *intref,
                       DonnaArgType    type)

{
    DonnaAppPrivate *priv;
    struct intref *ir;
    gpointer ptr = NULL;

    g_return_val_if_fail (DONNA_IS_APP (app), NULL);
    g_return_val_if_fail (intref != NULL, NULL);
    g_return_val_if_fail (type != DONNA_ARG_TYPE_NOTHING, NULL);
    priv = app->priv;

    g_rec_mutex_lock (&priv->rec_mutex);
    ir = g_hash_table_lookup (priv->intrefs, intref);
    if (ir && ir->type == type)
    {
        ir->last = g_get_monotonic_time ();
        ptr = ir->ptr;
        if (ir->type & DONNA_ARG_IS_ARRAY)
            ptr = g_ptr_array_ref (ptr);
        else if (ir->type & (DONNA_ARG_TYPE_TREE_VIEW | DONNA_ARG_TYPE_NODE))
            ptr = g_object_ref (ptr);
    }
    g_rec_mutex_unlock (&priv->rec_mutex);

    return ptr;
}

/**
 * donna_app_free_int_ref:
 * @app: The #DonnaApp
 * @intref: String representation of the intref to free
 *
 * Frees the memory of intref @intref and removes its reference of the linked
 * object (which might as a result be freed if it was the last reference)
 *
 * Note that intrefs are automatically freed after 15 minutes of inactivity, as
 * part of a garbage collecting process.
 *
 * Returns: %TRUE if the intref was freed, else (intref didn't exist) %FALSE
 */
gboolean
donna_app_free_int_ref (DonnaApp       *app,
                        const gchar    *intref)
{
    DonnaAppPrivate *priv;
    gboolean ret;

    g_return_val_if_fail (DONNA_IS_APP (app), NULL);
    g_return_val_if_fail (intref != NULL, NULL);
    priv = app->priv;

    g_rec_mutex_lock (&priv->rec_mutex);
    /* frees key & value */
    ret = g_hash_table_remove (priv->intrefs, intref);
    g_rec_mutex_unlock (&priv->rec_mutex);

    return ret;
}

enum
{
    DEREFERENCE_NONE = 0,
    DEREFERENCE_FULL,
    DEREFERENCE_FS
};

/**
 * donna_app_parse_fl:
 * @app: The #DonnaApp
 * @fl: The full location to parse
 * @must_free_fl: Whether @fl must be freed (using g_free()) or not
 * @conv_flags: context conv flags
 * @conv_fn: context conv fn
 * @conv_data: context conv data
 * @intrefs: (allow-none): Return location where a #GPtrArray of all created
 * intrefs will be created, if needed; Or %NULL
 *
 * Parse the full location @fl. There are 2 parsing that can be performed:
 * - #user-parsing which should be performed on all user-provided full
 *   locations, see below for more.
 * - contextual parsing, performed according the @conv_flags & co
 *
 * User parsing is a process of "extending" the given full location using
 * prefixes, aliases, etc
 *
 * First of all, prefixes can be defined. A prefix is a string of one or more
 * characters that cannot start with a letter. Defined under numbered categories
 * in <systemitem>donna/prefixes</systemitem> in the configuration, each
 * definition can be made of the following options:
 *
 * - <systemitem>prefix</systemitem> (string; required): the actual prefix to
 *   look for at the beginning of the full location.
 * - <systemitem>is_strict</systemitem> (boolean; optional): By default, a match
 *   will be whenever the full location starts with the prefix. When true, it
 *   will also required that the full location contains more than the prefix,
 *   and that the first character after the prefix isn't a space. This is to
 *   allow the use of the same string as alias, and use them all as needed.
 * - <systemitem>replacement</systemitem> (string; required): The string the
 *   prefix will be replaced with in the full location.
 * - <systemitem>is_home_dir</systemitem> (boolean; optional): A special mode,
 *   where if true option <systemitem>replacement</systemitem> will be ignored
 *   (and isn't even in fact needed) and instead the prefix will be replaced
 *   with the user's home dir (prefixed with "fs:").
 *
 * When a prefix match if found, replacement occurs and user parsing is
 * completed. (I.e. the result cannot include other prefixes or aliases.)
 *
 * If no prefix match occured, donna will look for the first character that is
 * either a colon, a slash or a space.
 *
 * - If a colon, assume a full location was given and be done.
 * - If a space (or nothing), look for the corresponding alias.
 * - If a slash, and the current location (of the active list) is in a
 *   non-flat domain (e.g. fs), then try to resolve the full location as a
 *   relative path of said location.
 *
 * An alias, like a prefix, will consist of replacing it with a replacement.
 * Said replacement will be looked for in
 * <systemitem>donna/aliases/&lt;ALIAS&gt;/replacement</systemitem>.
 * If the full location was nothing else than the alias (i.e. no space after it)
 * then the replacement will first be looked for in
 * <systemitem>donna/aliases/&lt;ALIAS&gt;/replacement_no_args</systemitem>.
 *
 *
 * Contextual parsing happens on actions, when certain variables (e.g. \%o, etc)
 * can be used in the full location/trigger, and need to be parsed before
 * processing.
 *
 * When processing such variables, it should be known that by default so-called
 * "intrefs" (for internal references) can be used; For example, if a variable
 * points to a node, an intref will be used. An intref is simply a string
 * referencing said node in memory.
 *
 * It is possible to "dereference" a variable, so that instead of using an
 * intref, the full location of the node will be used. This is done by using a
 * star after the percent sign, e.g. <systemitem>\%*n</systemitem>
 * This can be useful if it isn't meant to be used as a command argument, but
 * e.g. to be used as part of a string or something.
 * Additionally, you can also use a special dereferencing, using a colon
 * instead, e.g. <systemitem>\%:n</systemitem>
 * This will use the location for nodes in "fs", and skip/use empty string for
 * any node in another domain; Particularly useful for use in command line of
 * external process.
 *
 * If intrefs were created during said parsing (see donna_app_new_int_ref()) and
 * @intrefs is not %NULL, A #GPtrArray will be created and filled with string
 * representations of intrefs. This is intended to be then used with
 * donna_app_trigger_fl() so intrefs are freed afterwards.
 *
 * If no parsing/changes was done, @fl will be returned unless @must_free_fl
 * was %FALSE, in which case a g_strdup() is returned. If parsing happens, @fl
 * will be freed unless @must_free_fl was %FALSE.
 *
 * Returns: (transfer full): A newly allocated string to be freed with g_free(),
 * the new/parsed full location
 */
gchar *
donna_app_parse_fl (DonnaApp       *app,
                    gchar          *_fl,
                    gboolean        must_free_fl,
                    const gchar    *conv_flags,
                    conv_flag_fn    conv_fn,
                    gpointer        conv_data,
                    GPtrArray     **intrefs)
{
    GError *err = NULL;
    DonnaConfig *config = donna_app_peek_config (app);
    GPtrArray *arr;
    GString *str = NULL;
    gchar *fl = _fl;
    gchar *s;
    guint i;

    g_return_val_if_fail (DONNA_IS_APP (app), NULL);
    g_return_val_if_fail (fl != NULL, NULL);

    /* prefixes (cannot not start with a letter) */
    if (!((*fl >= 'a' && *fl <= 'z') || (*fl >= 'A' && *fl <= 'Z')))
    {
        arr = NULL;
        if (!donna_config_list_options (config, &arr,
                    DONNA_CONFIG_OPTION_TYPE_NUMBERED, "donna/prefixes"))
            goto prefix_done;

        for (i = 0; i < arr->len; ++i)
        {
            gsize len;
            gboolean is_set;

            if (!donna_config_get_string (config, &err, &s,
                        "donna/prefixes/%s/prefix", arr->pdata[i]))
            {
                g_warning ("Skipping prefix 'donna/prefixes/%s': %s",
                        (gchar *) arr->pdata[i],
                        (err) ? err->message : "(no error message)");
                g_clear_error (&err);
                continue;
            }

            len = strlen (s);
            if (!streqn (fl, s, len))
            {
                g_free (s);
                continue;
            }
            g_free (s);

            /* strict matching means the prefix must be "used as such," i.e. it
             * needs to be followed by something, that doesn't start by a space.
             * This allows to have an alias of the same thing, and have the
             * possibility of treating all 3 cases: prefix, alias_no_args, alias
             */
            if (donna_config_get_boolean (config, NULL, &is_set,
                        "donna/prefixes/%s/is_strict", arr->pdata[i])
                    && is_set && (fl[len] == ' ' || fl[len] == '\0'))
                continue;

            if (donna_config_get_boolean (config, NULL, &is_set,
                        "donna/prefixes/%s/is_home_dir", arr->pdata[i])
                    && is_set)
            {
                str = g_string_new ("fs:");
                g_string_append (str, g_get_home_dir ());
                fl += len;
                i = (guint) -1;
                break;
            }

            if (!donna_config_get_string (config, &err, &s,
                        "donna/prefixes/%s/replacement", arr->pdata[i]))
            {
                g_warning ("Skipping prefix 'donna/prefixes/%s': No replacement: %s",
                        (gchar *) arr->pdata[i],
                        (err) ? err->message : "(no error message)");
                g_clear_error (&err);
                continue;
            }

            str = g_string_new (s);
            g_free (s);
            fl += len;
            i = (guint) -1;
            break;
        }
        g_ptr_array_unref (arr);

        /* if there was a match, don't go through aliases, etc */
        if (i == (guint) -1)
            goto context_parsing;
    }
prefix_done:


    /* aliases: look for the first possible "separator" */
    for (s = fl; *s != ' ' && *s != ':' && *s != '/' && *s != '\0'; ++s)
        ;

    /* space (or EOF): alias */
    if (*s == ' ' || *s == '\0')
    {
        gint len = (gint) (s - fl);

        if (*s == ' ' && donna_config_get_string (config, NULL, &s,
                    "donna/aliases/%.*s/replacement_no_args", len, fl))
        {
            str = g_string_new (s);
            g_free (s);
            fl += len;
        }
        else if (!donna_config_get_string (config, &err, &s,
                    "donna/aliases/%.*s/replacement", len, fl))
        {
            if (donna_config_has_category (config, NULL,
                        "donna/aliases/%.*s", len, fl))
                g_warning ("Skipping prefix 'donna/aliases/%.*s': No replacement: %s",
                        len, fl, (err) ? err->message : "(no error message)");
            g_clear_error (&err);
        }
        else
        {
            str = g_string_new (s);
            g_free (s);
            fl += len;
        }
    }
    /* slash: special handling of relative path (non-flat domains only) */
    else if (*s == '/')
    {
        DonnaNode *node;

        node = donna_app_get_current_location (app, &err);
        if (!node)
        {
            g_warning ("Failed to perform relative path handling: "
                    "Couldn't get current location: %s",
                    (err) ? err->message : "(no error message)");
            g_clear_error (&err);
            goto context_parsing;
        }

        if (donna_provider_get_flags (donna_node_peek_provider (node))
                & DONNA_PROVIDER_FLAG_FLAT)
        {
            g_warning ("Failed to perform relative path handling: "
                    "domain '%s' is flat",
                    donna_node_get_domain (node));
            g_object_unref (node);
            goto context_parsing;
        }

        if (*fl == '/')
        {
            str = g_string_new (donna_node_get_domain (node));
            g_string_append_c (str, ':');
            g_object_unref (node);
            goto context_parsing;
        }

        s = _resolve_path (node, fl);
        g_object_unref (node);
        if (s)
        {
            /* set up new fl */
            fl = s;
            if (must_free_fl)
                g_free (_fl);
            else
                must_free_fl = TRUE;
            _fl = fl;
        }
        else
        {
            gchar *ss = donna_node_get_full_location (node);
            str = g_string_new (ss);
            g_string_append_c (str, '/');
            g_free (ss);
        }
    }
    /* colon: regular full location; nothing to do */


context_parsing:
    /* context */
    if (!conv_flags || !conv_fn)
        goto done;

    s = fl;
    while ((s = strchr (s, '%')))
    {
        guint dereference;
        gboolean match;

        if (s[1] == '*')
            dereference = DEREFERENCE_FULL;
        else if (s[1] == ':')
            dereference = DEREFERENCE_FS;
        else
            dereference = DEREFERENCE_NONE;

        if (dereference == DEREFERENCE_NONE)
            match = s[1] != '\0' && strchr (conv_flags, s[1]) != NULL;
        else
            match = s[2] != '\0' && strchr (conv_flags, s[2]) != NULL;

        if (match)
        {
            DonnaArgType type;
            gpointer ptr;
            GDestroyNotify destroy = NULL;

            if (!str)
                str = g_string_new (NULL);
            g_string_append_len (str, fl, s - fl);
            if (dereference != DEREFERENCE_NONE)
                ++s;

            if (G_UNLIKELY (!conv_fn (s[1], &type, &ptr, &destroy, conv_data)))
            {
                fl = ++s;
                ++s;
                continue;
            }

            /* we don't need to test for all possible types, only those can make
             * sense. That is, it could be a ROW, but not a ROW_ID (or PATH)
             * since those only make sense the other way around (or as type of
             * ROW_ID) */

            if (type & DONNA_ARG_TYPE_TREE_VIEW)
                g_string_append (str, donna_tree_view_get_name ((DonnaTreeView *) ptr));
            else if (type & DONNA_ARG_TYPE_ROW)
            {
                DonnaRow *row = (DonnaRow *) ptr;
                if (dereference != DEREFERENCE_NONE)
                {
                    gchar *l = NULL;

                    if (dereference == DEREFERENCE_FULL)
                        /* FULL = full location */
                        l = donna_node_get_full_location (row->node);
                    else if (streq (donna_node_get_domain (row->node), "fs"))
                        /* FS && domain "fs" = location */
                        l = donna_node_get_location (row->node);
                    else
                    {
                        /* FS && another domain == empty string */
                        g_string_append_c (str, '"');
                        g_string_append_c (str, '"');
                    }

                    if (l)
                    {
                        donna_g_string_append_quoted (str, l, FALSE);
                        g_free (l);
                    }
                }
                else
                    g_string_append_printf (str, "[%p;%p]", row->node, row->iter);
            }
            /* this will do nodes, array of nodes, array of strings */
            else if (type & (DONNA_ARG_TYPE_NODE | DONNA_ARG_IS_ARRAY))
            {
                if (dereference != DEREFERENCE_NONE)
                {
                    if (type & DONNA_ARG_IS_ARRAY)
                    {
                        GString *string;
                        GString *str_arr;
                        gchar sep;

                        arr = (GPtrArray *) ptr;

                        if (dereference == DEREFERENCE_FS)
                        {
                            string = str;
                            sep = ' ';
                        }
                        else
                        {
                            string = str_arr = g_string_new (NULL);
                            sep = ',';
                        }

                        if (type & DONNA_ARG_TYPE_NODE)
                            for (i = 0; i < arr->len; ++i)
                            {
                                DonnaNode *node = arr->pdata[i];
                                gchar *l = NULL;

                                if (dereference == DEREFERENCE_FULL)
                                    l = donna_node_get_full_location (node);
                                else if (streq (donna_node_get_domain (node), "fs"))
                                    l = donna_node_get_location (node);
                                /* no need to add a bunch of empty strings here */

                                if (l)
                                {
                                    donna_g_string_append_quoted (string, l, FALSE);
                                    g_string_append_c (string, sep);
                                    g_free (l);
                                }
                            }
                        else
                            for (i = 0; i < arr->len; ++i)
                            {
                                donna_g_string_append_quoted (string,
                                        (gchar *) arr->pdata[i], FALSE);
                                g_string_append_c (string, sep);
                            }

                        /* remove last sep */
                        g_string_truncate (string, string->len - 1);
                        if (dereference != DEREFERENCE_FS)
                        {
                            /* str_arr is a list of quoted strings/FL, but we
                             * also need to quote the list itself */
                            donna_g_string_append_quoted (str, str_arr->str, FALSE);
                            g_string_free (str_arr, TRUE);
                        }
                    }
                    else
                    {
                        DonnaNode *node = ptr;
                        gchar *l = NULL;

                        if (dereference == DEREFERENCE_FULL)
                            l = donna_node_get_full_location (node);
                        else if (streq (donna_node_get_domain (node), "fs"))
                            l = donna_node_get_location (node);
                        else
                        {
                            g_string_append_c (str, '"');
                            g_string_append_c (str, '"');
                        }

                        if (l)
                        {
                            donna_g_string_append_quoted (str, l, FALSE);
                            g_free (l);
                        }
                    }
                }
                else
                {
                    gchar *ir = donna_app_new_int_ref (app, type, ptr);
                    g_string_append (str, ir);
                    if (intrefs)
                    {
                        if (!*intrefs)
                            *intrefs = g_ptr_array_new_with_free_func (g_free);
                        g_ptr_array_add (*intrefs, ir);
                    }
                    else
                        g_free (ir);
                }
            }
            else if (type & DONNA_ARG_TYPE_STRING)
                donna_g_string_append_quoted (str, (gchar *) ptr, FALSE);
            else if (type & DONNA_ARG_TYPE_INT)
                g_string_append_printf (str, "%d", * (gint *) ptr);

            if (destroy)
                destroy (ptr);

            s += 2;
            fl = s;
        }
        else if (s[1] != '\0')
        {
            if (!str)
                str = g_string_new (NULL);
            g_string_append_len (str, fl, s - fl);
            fl = ++s;
            ++s;
        }
        else
            break;
    }

done:
    if (!str)
        return (must_free_fl) ? _fl : g_strdup (_fl);

    g_string_append (str, fl);
    if (must_free_fl)
        g_free (_fl);
    return g_string_free (str, FALSE);
}

struct fir
{
    gboolean is_stack;
    DonnaApp *app;
    GPtrArray *intrefs;
};

static void
free_fir (struct fir *fir)
{
    guint i;

    if (fir->intrefs)
    {
        for (i = 0; i < fir->intrefs->len; ++i)
            donna_app_free_int_ref (fir->app, fir->intrefs->pdata[i]);
        g_ptr_array_unref (fir->intrefs);
    }
    if (!fir->is_stack)
        g_free (fir);
}

static void
trigger_cb (DonnaTask *task, gboolean timeout_called, struct fir *fir)
{
    if (donna_task_get_state (task) == DONNA_TASK_FAILED)
        donna_app_show_error (fir->app, donna_task_get_error (task),
                "Action trigger failed");
    free_fir (fir);
}

static gboolean
trigger_fl (DonnaApp    *app,
            const gchar *fl,
            GPtrArray   *intrefs,
            gboolean     blocking,
            gboolean    *ret,
            GError     **error)
{
    DonnaNode *node;
    DonnaTask *task;

    node = donna_app_get_node (app, fl, FALSE, error);
    if (!node)
        return FALSE;

    task = donna_node_trigger_task (node, error);
    if (G_UNLIKELY (!task))
    {
        g_prefix_error (error, "Failed to trigger '%s': ", fl);
        g_object_unref (node);
        return FALSE;
    }
    g_object_unref (node);

    if (blocking)
        g_object_ref (task);
    else
    {
        struct fir *fir;
        fir = g_new0 (struct fir, 1);
        fir->app = app;
        fir->intrefs = intrefs;
        donna_task_set_callback (task, (task_callback_fn) trigger_cb, fir, NULL);
    }

    donna_app_run_task (app, task);

    if (blocking)
    {
        struct fir fir = { TRUE, app, intrefs };
        gboolean r;

        donna_task_wait_for_it (task, NULL, NULL);
        r = donna_task_get_state (task) == DONNA_TASK_DONE;
        /* ret: for events, there can be a return value that means TRUE, i.e.
         * stop the event emission. */
        if (r && ret)
        {
            const GValue *v;

            v = donna_task_get_return_value (task);
            if (G_VALUE_TYPE (v) == G_TYPE_INT)
                *ret = g_value_get_int (v) != 0;
            else if (G_VALUE_TYPE (v) == G_TYPE_STRING)
            {
                const gchar *s = g_value_get_string (v);
                gchar *e = NULL;
                gint64 i;

                i = g_ascii_strtoll (s, &e, 10);
                if (!e || *e != '\0')
                    /* if the string wasn't just a number, we "ignore" it */
                    *ret = FALSE;
                else
                    *ret = i != 0;
            }
            else
                *ret = FALSE;
        }
        g_object_unref (task);
        free_fir (&fir);
        return r;
    }

    return TRUE;
}

/**
 * donna_app_trigger_fl:
 * @app: The #DonnaApp
 * @fl: The full location to trigger
 * @intrefs: (allow-none): A #GPtrArray of all intrefs to free afterwards
 * @blocking: Is %TRUE block until the task has run
 * @error: (allow-none): Return location of a #GError, or %NULL
 *
 * Helper that will get the node for @fl and trigger it.
 *
 * If @intrefs was specified, it must be an array of string representations of
 * all intrefs to be freed after the task has run. Usually this will have been
 * created by donna_app_parse_fl()
 *
 * If @blocking is %FALSE, it returns %FALSE if fails to get the node or its
 * trigger task, else returns %TRUE after calling donna_app_run_task()
 *
 * If @blocking is %TRUE then it will block until the task has run, using
 * donna_task_wait_for_it(). It will only then return %TRUE is the task was
 * successful (ended in %DONNA_TASK_DONE), else %FALSE.
 *
 * Returns: %TRUE on success, else %FALSE
 */
gboolean
donna_app_trigger_fl (DonnaApp       *app,
                      const gchar    *fl,
                      GPtrArray      *intrefs,
                      gboolean        blocking,
                      GError        **error)
{
    g_return_val_if_fail (DONNA_IS_APP (app), NULL);
    g_return_val_if_fail (fl != NULL, NULL);

    return trigger_fl (app, fl, intrefs, blocking, NULL, error);
}

static gint
arr_str_cmp (gconstpointer a, gconstpointer b)
{
    return strcmp (* (const gchar **) a, * (const gchar **) b);
}

static gboolean
trigger_event (DonnaApp     *app,
               const gchar  *event,
               gboolean      is_confirm,
               const gchar  *source,
               const gchar  *conv_flags,
               conv_flag_fn  conv_fn,
               gpointer      conv_data)
{
    DonnaAppPrivate *priv = app->priv;
    GPtrArray *arr = NULL;
    guint i;

    if (!donna_config_list_options (priv->config, &arr,
                DONNA_CONFIG_OPTION_TYPE_OPTION, "%s/events/%s", source, event))
        return FALSE;

    g_ptr_array_sort (arr, arr_str_cmp);
    for (i = 0; i < arr->len; ++i)
    {
        GError *err = NULL;
        GPtrArray *intrefs = NULL;
        gchar *fl;

        if (donna_config_get_string (priv->config, NULL, &fl,
                    "%s/events/%s/%s",
                    source, event, arr->pdata[i]))
        {
            gboolean ret;

            fl = donna_app_parse_fl (app, fl, TRUE,
                    conv_flags, conv_fn, conv_data, &intrefs);
            if (!trigger_fl (app, fl, intrefs, is_confirm, &ret, &err))
            {
                donna_app_show_error (app, err,
                        "Event '%s': Failed to trigger '%s'%s%s%s",
                        event, arr->pdata[i],
                        (*source != '\0') ? " from '" : "",
                        (*source != '\0') ? source : "",
                        (*source != '\0') ? "'" : "");
                g_clear_error (&err);
            }
            else if (is_confirm && ret)
            {
                g_free (fl);
                g_ptr_array_unref (arr);
                return TRUE;
            }
            g_free (fl);
        }
    }

    g_ptr_array_unref (arr);
    return FALSE;
}

/**
 * donna_app_emit_event:
 * @app: The #DonnaApp
 * @event: The name of the event
 * @is_confirm: Whether this is a "confirm event" or not
 * @conv_flags: context conv flags
 * @conv_fn: context conv fn
 * @conv_data: context conv data
 * @fmt_source: printf-like format for the source of the event
 * @...: %NULL terminated printf-like arguments
 *
 * Emit an event. TODO: document the whole event thing, but not before we've
 * redone it.
 *
 * Returns: %TRUE or %FALSE
 */
gboolean
donna_app_emit_event (DonnaApp       *app,
                      const gchar    *event,
                      gboolean        is_confirm,
                      const gchar    *conv_flags,
                      conv_flag_fn    conv_fn,
                      gpointer        conv_data,
                      const gchar    *fmt_source,
                      ...)
{
    GQuark q = 0;
    GSList *l;
    gchar *source;
    gboolean ret;

    g_return_val_if_fail (DONNA_IS_APP (app), NULL);
    g_return_val_if_fail (event != NULL, NULL);

    if (is_confirm)
    {
        gboolean in_list = FALSE;

        q = g_quark_from_string (event);
        for (l = event_confirm; l; l = l->next)
        {
            if ((GQuark) GPOINTER_TO_UINT (l->data) == q)
            {
                in_list = TRUE;
                break;
            }
        }

        if (!in_list)
            event_confirm = g_slist_prepend (event_confirm, GUINT_TO_POINTER (q));
    }

    if (fmt_source)
    {
        va_list va_args;
        va_start (va_args, fmt_source);
        source = g_strdup_vprintf (fmt_source, va_args);
        va_end (va_args);
    }
    else
        source = NULL;

    g_signal_emit (app, donna_app_signals[EVENT],
            g_quark_from_string (event),
            event, source, conv_flags, conv_fn, conv_data,
            &ret);

    if (!is_confirm || !ret)
    {
        if (source)
        {
            ret = trigger_event (app, event, is_confirm, source,
                    conv_flags, conv_fn, conv_data);
            if (is_confirm && ret)
            {
                g_free (source);
                return TRUE;
            }
        }

        ret = trigger_event (app, event, is_confirm, "",
                conv_flags, conv_fn, conv_data);
    }

    if (is_confirm)
        event_confirm = g_slist_remove (event_confirm, GUINT_TO_POINTER (q));
    g_free (source);
    return ret;
}

struct menu_click
{
    DonnaApp            *app;
    /* options are loaded, but this is used when processing clicks */
    gchar               *name;
    /* this is only used to hold references to the nodes for the menu */
    GPtrArray           *nodes;
    /* should icons be features on menuitems? */
    gboolean            show_icons;
    /* default to file/folder icon based on item/container if no icon set */
    gboolean             use_default_icons;
    /* are containers just items, submenus, or both combined? */
    DonnaEnabledTypes    submenus;
    /* can children override submenus */
    gboolean             can_children_submenus;
    /* can children override menu definition */
    gboolean             can_children_menu;
    /* type of nodes to load in submenus */
    DonnaNodeType        node_type;
    /* do we "show" dot files in submenus */
    gboolean             show_hidden;
    /* sort options */
    gboolean             is_sorted;
    gboolean             container_first;
    gboolean             is_locale_based;
    DonnaSortOptions     options;
    gboolean             sort_special_first;
};

static void
free_menu_click (struct menu_click *mc)
{
    if (mc->nodes)
        g_ptr_array_unref (mc->nodes);
    g_free (mc->name);
    g_slice_free (struct menu_click, mc);
}

static gboolean
menu_conv_flag (const gchar      c,
                DonnaArgType    *type,
                gpointer        *ptr,
                GDestroyNotify  *destroy,
                DonnaNode       *node)
{
    switch (c)
    {
        case 'N':
            *type = DONNA_ARG_TYPE_STRING;
            *ptr = donna_node_get_location (node);
            *destroy = g_free;
            return TRUE;

        case 'n':
            *type = DONNA_ARG_TYPE_NODE;
            *ptr = node;
            return TRUE;
    }
    return FALSE;
}

static gboolean menuitem_button_release_cb (GtkWidget           *item,
                                            GdkEventButton      *event,
                                            struct menu_click   *mc);

static void
menuitem_activate_cb (GtkWidget *item, struct menu_click *mc)
{
    GdkEventButton event;

    /* because GTK emit "activate" when selecting an item with a submenu, for
     * some reason */
    if (gtk_menu_item_get_submenu (((GtkMenuItem *) item)))
        return;

    event.state = 0;
    event.button = 1;

    menuitem_button_release_cb (item, &event, mc);
}

struct menu_trigger
{
    DonnaApp *app;
    gchar *fl;
    GPtrArray *intrefs;
};

static gboolean
menu_trigger (struct menu_trigger *mt)
{
    donna_app_trigger_fl (mt->app, mt->fl, mt->intrefs, FALSE, NULL);
    g_slice_free (struct menu_trigger, mt);
    return FALSE;
}

static gboolean
menuitem_button_release_cb (GtkWidget           *item,
                            GdkEventButton      *event,
                            struct menu_click   *mc)
{
    DonnaAppPrivate *priv = mc->app->priv;
    DonnaNode *node;
    struct menu_trigger *mt;
    GPtrArray *intrefs = NULL;
    gchar *fl = NULL;
    gboolean must_free_fl = TRUE;
    /* longest possible is "ctrl_shift_middle_click" (len=23) */
    gchar buf[24];
    gchar *b = buf;

    /* we process it now, let's make sure activate isn't triggered; It is there
     * for when user press Enter */
    g_signal_handlers_disconnect_by_func (item, menuitem_activate_cb, mc);

    node = g_object_get_data ((GObject *) item, "node");
    if (!node)
        return FALSE;

    if (event->state & GDK_CONTROL_MASK)
    {
        strcpy (b, "ctrl_");
        b += 5;
    }
    if (event->state & GDK_SHIFT_MASK)
    {
        strcpy (b, "shift_");
        b += 6;
    }
    if (event->button == 1)
    {
        strcpy (b, "left_");
        b += 5;
    }
    else if (event->button == 2)
    {
        strcpy (b, "middle_");
        b += 7;
    }
    else if (event->button == 3)
    {
        strcpy (b, "right_");
        b += 6;
    }
    else
        return FALSE;

    strcpy (b, "click");

    if (!donna_config_get_string (priv->config, NULL, &fl, "menus/%s/%s",
                mc->name, buf))
        donna_config_get_string (priv->config, NULL, &fl, "defaults/menus/%s", buf);

    if (!fl)
    {
        if (streq (buf, "left_click"))
        {
            /* hard-coded default for sanity */
            fl = (gchar *) "command:node_trigger (%n)";
            must_free_fl = FALSE;
        }
        else
            return FALSE;
    }

    fl = donna_app_parse_fl (mc->app, fl, must_free_fl, "nN",
            (conv_flag_fn) menu_conv_flag, node, &intrefs);

    /* we use an idle source to trigger it, because otherwise this could lead to
     * e.g. ask the user something (e.g. @ask_text) which would start its own
     * main loop, all that from this thread, so as a result the menu wouldn't be
     * closed (since the event hasn't finished being processed) */
    mt = g_slice_new (struct menu_trigger);
    mt->app = mc->app;
    mt->fl = fl;
    mt->intrefs = intrefs;
    g_idle_add ((GSourceFunc) menu_trigger, mt);

    return FALSE;
}

static gint
node_cmp (gconstpointer n1, gconstpointer n2, struct menu_click *mc)
{
    DonnaNode *node1 = * (DonnaNode **) n1;
    DonnaNode *node2 = * (DonnaNode **) n2;
    gchar *name1;
    gchar *name2;
    gint ret;

    if (!node1)
        return (node2) ? -1 : 0;
    else if (!node2)
        return 1;

    if (mc->container_first)
    {
        gboolean is_container1;
        gboolean is_container2;

        is_container1 = donna_node_get_node_type (node1) == DONNA_NODE_CONTAINER;
        is_container2 = donna_node_get_node_type (node2) == DONNA_NODE_CONTAINER;

        if (is_container1)
        {
            if (!is_container2)
            return -1;
        }
        else if (is_container2)
            return 1;
    }

    name1 = donna_node_get_name (node1);
    name2 = donna_node_get_name (node2);

    if (mc->is_locale_based)
    {
        gchar *key1;
        gchar *key2;

        key1 = donna_sort_get_utf8_collate_key (name1, -1,
                mc->options & DONNA_SORT_DOT_FIRST,
                mc->sort_special_first,
                mc->options & DONNA_SORT_NATURAL_ORDER);
        key2 = donna_sort_get_utf8_collate_key (name2, -1,
                mc->options & DONNA_SORT_DOT_FIRST,
                mc->sort_special_first,
                mc->options & DONNA_SORT_NATURAL_ORDER);

        ret = strcmp (key1, key2);

        g_free (key1);
        g_free (key2);
        g_free (name1);
        g_free (name2);
        return ret;
    }

    ret = donna_strcmp (name1, name2, mc->options);

    g_free (name1);
    g_free (name2);
    return ret;
}

struct load_submenu
{
    struct menu_click   *mc;
    /* whether we own the mc (newly allocated), or it's just a pointer to our
     * parent (therefore we need to make a copy when loading the submenu) */
    gboolean             own_mc;
    /* mc for submenu/children (if already allocated, else copy mc) */
    struct menu_click   *sub_mc;
    /* parent menu item */
    GtkMenuItem         *item;
    /* get_children task, to cancel on item's destroy */
    DonnaTask           *task;
    /* one for item, one for task */
    guint                ref_count;
    /* if not, must be free-d. Else it's on stack, also we block the task */
    gboolean             blocking;
    /* when item is destroyed, in case task is still running/being cancelled */
    gboolean             invalid;
};

static void
free_load_submenu (struct load_submenu *ls)
{
    if (g_atomic_int_dec_and_test (&ls->ref_count))
    {
        if (ls->own_mc)
            free_menu_click (ls->mc);
        if (!ls->blocking)
            /* if not blocking it was on stack */
            g_slice_free (struct load_submenu, ls);
    }
}

static void
item_destroy_cb (struct load_submenu *ls)
{
    ls->invalid = TRUE;
    if (ls->task)
        donna_task_cancel (ls->task);
    free_load_submenu (ls);
}

static GtkWidget * load_menu (struct menu_click *mc);

static void
submenu_get_children_cb (DonnaTask           *task,
                         gboolean             timeout_called,
                         struct load_submenu *ls)
{
    GtkWidget *menu;
    GPtrArray *arr;
    struct menu_click *mc;
    gboolean is_selected;

    if (ls->invalid)
    {
        free_load_submenu (ls);
        return;
    }
    ls->task = NULL;

    if (donna_task_get_state (task) != DONNA_TASK_DONE)
    {
        const GError *error;
        GtkWidget *w;

        error = donna_task_get_error (task);
        menu = gtk_menu_new ();
        w = donna_image_menu_item_new_with_label (
                (error) ? error->message : "Failed to load children");
        gtk_widget_set_sensitive (w, FALSE);
        gtk_menu_attach ((GtkMenu *) menu, w, 0, 1, 0, 1);
        gtk_widget_show (w);
        goto set_menu;
    }

    arr = g_value_get_boxed (donna_task_get_return_value (task));
    if (!ls->mc->show_hidden)
    {
        GPtrArray *filtered = NULL;
        guint i;

        /* arr is owned by the task, we shouldn't modify it. (It could also be
         * used by e.g. a treeview to refresh its content) */
        filtered = g_ptr_array_new_with_free_func (g_object_unref);
        for (i = 0; i < arr->len; ++i)
        {
            gchar *name;

            name = donna_node_get_name (arr->pdata[i]);
            if (*name != '.')
                g_ptr_array_add (filtered, g_object_ref (arr->pdata[i]));
            g_free (name);
        }

        arr = filtered;
    }

    if (arr->len == 0)
    {
no_submenu:
        gtk_menu_item_set_submenu (ls->item, NULL);
        if (ls->mc->submenus == DONNA_ENABLED_TYPE_ENABLED)
            gtk_widget_set_sensitive ((GtkWidget *) ls->item, FALSE);
        else if (ls->mc->submenus == DONNA_ENABLED_TYPE_COMBINE)
        {
            donna_image_menu_item_set_is_combined ((DonnaImageMenuItem *) ls->item,
                    FALSE);
            if (!donna_image_menu_item_get_is_combined_sensitive (
                        (DonnaImageMenuItem *) ls->item))
            {
                gtk_widget_set_sensitive ((GtkWidget *) ls->item, FALSE);
                gtk_menu_item_deselect (ls->item);
            }
        }
        free_load_submenu (ls);
        return;
    }

    if (ls->sub_mc)
        mc = ls->sub_mc;
    else
    {
        mc = g_slice_new0 (struct menu_click);
        memcpy (mc, ls->mc, sizeof (struct menu_click));
        mc->name = g_strdup (ls->mc->name);
    }
    mc->nodes = (ls->mc->show_hidden) ? g_ptr_array_ref (arr) : arr;

    menu = load_menu (mc);
    if (G_UNLIKELY (!menu))
        goto no_submenu;
    else
        g_object_ref (menu);

set_menu:
    /* see if the item is selected (if we're not TYPE_COMBINE then it can't be,
     * since thje menu hasn't event been shown yet). If so, we need to unselect
     * it before we can add/change (if timeout_called) the submenu */
    is_selected = ls->mc->submenus == DONNA_ENABLED_TYPE_COMBINE
        && (GtkWidget *) ls->item == gtk_menu_shell_get_selected_item (
                (GtkMenuShell *) gtk_widget_get_parent ((GtkWidget *) ls->item));

    if (is_selected)
        gtk_menu_item_deselect (ls->item);
    gtk_menu_item_set_submenu (ls->item, menu);
    if (is_selected)
        gtk_menu_item_select (ls->item);
    free_load_submenu (ls);
}

static void
submenu_get_children_timeout (DonnaTask *task, struct load_submenu *ls)
{
    if (!ls->invalid)
        donna_image_menu_item_set_loading_submenu (
                (DonnaImageMenuItem *) ls->item, NULL);
}

static void
load_submenu (struct load_submenu *ls)
{
    DonnaNode *node;
    DonnaTask *task;

    if (!ls->blocking)
        g_signal_handlers_disconnect_by_func (ls->item, load_submenu, ls);

    node = g_object_get_data ((GObject *) ls->item, "node");
    if (!node)
        return;

    task = donna_node_get_children_task (node,
            (ls->sub_mc && ls->sub_mc->node_type) ? ls->sub_mc->node_type
            : ls->mc->node_type,
            NULL);

    if (ls->blocking)
        g_object_ref (task);
    else
    {
        donna_task_set_callback (task,
                (task_callback_fn) submenu_get_children_cb,
                ls, (GDestroyNotify) free_load_submenu);
        donna_task_set_timeout (task, /*FIXME*/ 800,
                (task_timeout_fn) submenu_get_children_timeout, ls, NULL);
    }

    g_atomic_int_inc (&ls->ref_count);
    ls->task = task;

    donna_app_run_task (ls->mc->app, task);
    if (ls->blocking)
    {
        donna_task_wait_for_it (task, NULL, NULL);
        submenu_get_children_cb (task, FALSE, ls);
        g_object_unref (task);
    }
}

#define get_boolean(var, option, def_val)   do {                \
    if (!donna_config_get_boolean (priv->config, NULL, &var,    \
                "/menus/%s/" option, name))                     \
        if (!donna_config_get_boolean (priv->config, NULL, &var,\
                    "/defaults/menus/" option))                 \
        {                                                       \
            var = def_val;                                      \
            donna_config_set_boolean (priv->config, NULL, var,  \
                    "/defaults/menus/" option);                 \
        }                                                       \
} while (0)

#define get_int(var, option, def_val)   do {                \
    if (!donna_config_get_int (priv->config, NULL, &var,    \
                "/menus/%s/" option, name))                 \
        if (!donna_config_get_int (priv->config, NULL, &var,\
                    "/defaults/menus/" option))             \
        {                                                   \
            var = def_val;                                  \
            donna_config_set_int (priv->config, NULL, var,  \
                    "/defaults/menus/" option);             \
        }                                                   \
} while (0)

static struct menu_click *
load_mc (DonnaApp *app, const gchar *name, GPtrArray *nodes)
{
    DonnaAppPrivate *priv = app->priv;
    struct menu_click *mc;
    gboolean b;
    gint i;

    mc = g_slice_new0 (struct menu_click);
    mc->app   = app;
    mc->name  = g_strdup (name);
    mc->nodes = nodes;
    if (nodes)
        /* because we allow to have NULL elements to be used as separator, we
         * can't just use g_object_unref() as GDestroyNotify since that will
         * cause warning when called on NULL */
        g_ptr_array_set_free_func (nodes, (GDestroyNotify) donna_g_object_unref);

    /* icon options */
    get_boolean (b, "show_icons", TRUE);
    mc->show_icons = b;
    get_boolean (b, "use_default_icons", TRUE);
    mc->use_default_icons = b;

    get_int (i, "submenus", DONNA_ENABLED_TYPE_DISABLED);
    if (i == DONNA_ENABLED_TYPE_ENABLED || i == DONNA_ENABLED_TYPE_COMBINE)
        mc->submenus = i;

    /* we could have made this option a list-flags, i.e. be exactly the
     * value we want, but we wanted it to be similar to what's used in
     * commands, where you say "all" not "item,container" (as would have
     * been the case using flags) */
    get_int (i, "children", 0);
    if (i == 1)
        mc->node_type = DONNA_NODE_ITEM;
    else if (i == 2)
        mc->node_type = DONNA_NODE_CONTAINER;
    else /* if (i == 0) */
        mc->node_type = DONNA_NODE_ITEM | DONNA_NODE_CONTAINER;

    get_boolean (b, "children_show_hidden", TRUE);
    mc->show_hidden = b;

    get_boolean (b, "can_children_submenus", TRUE);
    mc->can_children_submenus = b;
    get_boolean (b, "can_children_menu", TRUE);
    mc->can_children_menu = b;

    get_boolean (b, "sort", FALSE);
    mc->is_sorted = b;
    if (mc->is_sorted)
    {
        get_boolean (b, "container_first", TRUE);
        mc->container_first = b;

        get_boolean (b, "locale_based", FALSE);
        mc->is_locale_based = b;

        get_boolean (b, "natural_order", TRUE);
        if (b)
            mc->options |= DONNA_SORT_NATURAL_ORDER;

        get_boolean (b, "dot_first", TRUE);
        if (b)
            mc->options |= DONNA_SORT_DOT_FIRST;

        if (mc->is_locale_based)
        {
            get_boolean (b, "special_first", TRUE);
            mc->sort_special_first = b;
        }
        else
        {
            get_boolean (b, "dot_mixed", FALSE);
            if (b)
                mc->options |= DONNA_SORT_DOT_MIXED;

            get_boolean (b, "case_sensitive", FALSE);
            if (!b)
                mc->options |= DONNA_SORT_CASE_INSENSITIVE;

            get_boolean (b, "ignore_spunct", FALSE);
            if (b)
                mc->options |= DONNA_SORT_IGNORE_SPUNCT;
        }
    }

    return mc;
}

static GtkWidget *
load_menu (struct menu_click *mc)
{
    GtkIconTheme *theme;
    GtkWidget *menu;
    guint last_sep;
    guint i;
    gboolean has_items = FALSE;

    if (mc->is_sorted)
        g_ptr_array_sort_with_data (mc->nodes, (GCompareDataFunc) node_cmp, mc);

    menu = gtk_menu_new ();

    /* in case the last few "nodes" are all NULLs, make sure we don't feature
     * any separators */
    for (last_sep = mc->nodes->len - 1;
            last_sep > 0 && !mc->nodes->pdata[last_sep];
            --last_sep)
        ;

    theme = gtk_icon_theme_get_default ();
    for (i = 0; i < mc->nodes->len; ++i)
    {
        DonnaNode *node = mc->nodes->pdata[i];
        GtkWidget *item;

        if (!node)
        {
            /* no separator as first or last item.. */
            if (G_LIKELY (i > 0 && i < last_sep
                        /* ..and no separator after a separator */
                        && mc->nodes->pdata[i - 1]))
                item = gtk_separator_menu_item_new ();
            else
                continue;
        }
        else
        {
            DonnaImageMenuItem *imi;
            DonnaNodeHasValue has;
            GValue v = G_VALUE_INIT;
            gchar *s;

            s = donna_node_get_name (node);
            item = donna_image_menu_item_new_with_label (s);
            imi  = (DonnaImageMenuItem *) item;
            g_free (s);

            donna_node_get (node, TRUE, "menu-is-name-markup", &has, &v, NULL);
            if (has == DONNA_NODE_VALUE_SET)
            {
                if (G_VALUE_TYPE (&v) == G_TYPE_BOOLEAN && g_value_get_boolean (&v))
                    donna_image_menu_item_set_is_label_markup (imi, TRUE);
                g_value_unset (&v);
            }

            if (donna_node_get_desc (node, TRUE, &s) == DONNA_NODE_VALUE_SET)
            {
                gtk_widget_set_tooltip_text (item, s);
                g_free (s);
            }

            donna_node_get (node, TRUE, "menu-is-sensitive", &has, &v, NULL);
            if (has == DONNA_NODE_VALUE_SET)
            {
                if (G_VALUE_TYPE (&v) == G_TYPE_BOOLEAN && !g_value_get_boolean (&v))
                    gtk_widget_set_sensitive (item, FALSE);
                g_value_unset (&v);
            }

            donna_node_get (node, TRUE, "menu-is-combined-sensitive", &has, &v, NULL);
            if (has == DONNA_NODE_VALUE_SET)
            {
                if (G_VALUE_TYPE (&v) == G_TYPE_BOOLEAN)
                    donna_image_menu_item_set_is_combined_sensitive (imi,
                            g_value_get_boolean (&v));
                g_value_unset (&v);
            }

            donna_node_get (node, TRUE, "menu-is-label-bold", &has, &v, NULL);
            if (has == DONNA_NODE_VALUE_SET)
            {
                if (G_VALUE_TYPE (&v) == G_TYPE_BOOLEAN)
                    donna_image_menu_item_set_is_label_bold (imi,
                            g_value_get_boolean (&v));
                g_value_unset (&v);
            }

            g_object_set_data ((GObject *) item, "node", node);

            if (mc->show_icons)
            {
                GtkWidget *image = NULL;
                DonnaImageMenuItemImageSpecial img;

                donna_node_get (node, TRUE, "menu-image-special", &has, &v, NULL);
                if (has == DONNA_NODE_VALUE_SET)
                {
                    img = g_value_get_uint (&v);
                    g_value_unset (&v);
                }
                else
                    img = DONNA_IMAGE_MENU_ITEM_IS_IMAGE;

                if (img == DONNA_IMAGE_MENU_ITEM_IS_CHECK
                        || img == DONNA_IMAGE_MENU_ITEM_IS_RADIO)
                {
                    donna_image_menu_item_set_image_special (imi, img);

                    donna_node_get (node, TRUE, "menu-is-active", &has, &v, NULL);
                    if (has == DONNA_NODE_VALUE_SET)
                    {
                        donna_image_menu_item_set_is_active (imi,
                                g_value_get_boolean (&v));
                        g_value_unset (&v);
                    }

                    if (img == DONNA_IMAGE_MENU_ITEM_IS_CHECK)
                    {
                        donna_node_get (node, TRUE, "menu-is-inconsistent",
                                &has, &v, NULL);
                        if (has == DONNA_NODE_VALUE_SET)
                        {
                            donna_image_menu_item_set_is_inconsistent (imi,
                                    g_value_get_boolean (&v));
                            g_value_unset (&v);
                        }
                    }
                }
                else /* DONNA_IMAGE_MENU_ITEM_IS_IMAGE */
                {
                    GIcon *icon = NULL;

                    if (donna_node_get_icon (node, TRUE, &icon) == DONNA_NODE_VALUE_SET)
                    {
                        GtkIconInfo *info;

                        info = gtk_icon_theme_lookup_by_gicon (theme, icon, 16,
                                GTK_ICON_LOOKUP_GENERIC_FALLBACK);
                        g_object_unref (icon);
                        if (info)
                        {
                            g_object_unref (info);
                            image = gtk_image_new_from_gicon (icon,
                                    /*XXX*/GTK_ICON_SIZE_MENU);
                        }
                        else
                            icon = NULL;

                        /* if lookup failed, we'll default to the file/folder
                         * icon instead of the "broken" one, much like
                         * columntype-name does for treeview */
                    }
                    if (!icon && mc->use_default_icons)
                    {
                        if (donna_node_get_node_type (node) == DONNA_NODE_ITEM)
                            image = gtk_image_new_from_icon_name ("text-x-generic",
                                    GTK_ICON_SIZE_MENU);
                        else /* DONNA_NODE_CONTAINER */
                            image = gtk_image_new_from_icon_name ("folder",
                                    GTK_ICON_SIZE_MENU);
                    }
                    else if (!icon)
                        image = NULL;

                    if (image)
                        donna_image_menu_item_set_image (imi, image);

                    donna_node_get (node, TRUE, "menu-image-selected", &has, &v, NULL);
                    if (has == DONNA_NODE_VALUE_SET)
                    {
                        if (G_VALUE_TYPE (&v) == G_TYPE_ICON)
                        {
                            image = gtk_image_new_from_gicon (g_value_get_object (&v),
                                    GTK_ICON_SIZE_MENU /* FIXME */);
                            donna_image_menu_item_set_image_selected (imi, image);
                        }
                        g_value_unset (&v);
                    }
                }
            }

            if (donna_node_get_node_type (node) == DONNA_NODE_CONTAINER)
            {
                DonnaEnabledTypes submenus = mc->submenus;
                struct menu_click *sub_mc = NULL;

                if (mc->can_children_submenus)
                {
                    donna_node_get (node, TRUE, "menu-submenus", &has, &v, NULL);
                    if (has == DONNA_NODE_VALUE_SET)
                    {
                        if (G_VALUE_TYPE (&v) == G_TYPE_UINT)
                        {
                            submenus = g_value_get_uint (&v);
                            submenus = CLAMP (submenus, 0, 3);
                        }
                        g_value_unset (&v);
                    }
                }

                if (mc->can_children_menu
                        && (submenus == DONNA_ENABLED_TYPE_ENABLED
                            || submenus == DONNA_ENABLED_TYPE_COMBINE))
                {
                    donna_node_get (node, TRUE, "menu-menu", &has, &v, NULL);
                    if (has == DONNA_NODE_VALUE_SET)
                    {
                        if (G_VALUE_TYPE (&v) == G_TYPE_STRING)
                            sub_mc = load_mc (mc->app, g_value_get_string (&v), NULL);
                        g_value_unset (&v);
                    }
                }

                if (submenus == DONNA_ENABLED_TYPE_ENABLED)
                {
                    struct load_submenu ls = { 0, };

                    ls.blocking = TRUE;
                    ls.mc       = mc;
                    ls.sub_mc   = sub_mc;
                    ls.item     = (GtkMenuItem *) item;

                    if (submenus != mc->submenus)
                    {
                        if (!sub_mc)
                        {
                            /* load the sub_mc now, since we'll change option
                             * submenus in mc */
                            ls.sub_mc           = g_slice_new0 (struct menu_click);
                            memcpy (ls.sub_mc, mc, sizeof (struct menu_click));
                            ls.sub_mc->name     = g_strdup (mc->name);
                            ls.sub_mc->nodes    = NULL;
                        }

                        ls.own_mc       = TRUE;
                        ls.mc           = g_slice_new0 (struct menu_click);
                        memcpy (ls.mc, mc, sizeof (struct menu_click));
                        ls.mc->name     = NULL;
                        ls.mc->nodes    = NULL;
                        ls.mc->submenus = submenus;
                    }

                    load_submenu (&ls);
                }
                else if (submenus == DONNA_ENABLED_TYPE_COMBINE)
                {
                    struct load_submenu *ls;

                    ls = g_slice_new0 (struct load_submenu);
                    ls->mc          = mc;
                    ls->sub_mc      = sub_mc;
                    ls->item        = (GtkMenuItem *) item;
                    ls->ref_count = 1;

                    if (submenus != mc->submenus)
                    {
                        if (!sub_mc)
                        {
                            /* load the sub_mc now, since we'll change option
                             * submenus in mc */
                            ls->sub_mc           = g_slice_new0 (struct menu_click);
                            memcpy (ls->sub_mc, mc, sizeof (struct menu_click));
                            ls->sub_mc->name     = g_strdup (mc->name);
                            ls->sub_mc->nodes    = NULL;
                        }

                        ls->own_mc       = TRUE;
                        ls->mc           = g_slice_new0 (struct menu_click);
                        memcpy (ls->mc, mc, sizeof (struct menu_click));
                        ls->mc->name     = NULL;
                        ls->mc->nodes    = NULL;
                        ls->mc->submenus = submenus;
                    }

                    donna_image_menu_item_set_is_combined (imi, TRUE);
                    g_signal_connect_swapped (item, "load-submenu",
                            (GCallback) load_submenu, ls);
                    g_signal_connect_swapped (item, "destroy",
                            (GCallback) item_destroy_cb, ls);
                }
            }
        }

        /* we use button-release because that's what's handled by
         * DonnaImageMenuItem, as the button-press-event is used by GTK and
         * couldn't be blocked. */
        gtk_widget_add_events (item, GDK_BUTTON_RELEASE_MASK);
        g_signal_connect (item, "button-release-event",
                (GCallback) menuitem_button_release_cb, mc);
        g_signal_connect (item, "activate", (GCallback) menuitem_activate_cb, mc);

        gtk_widget_show (item);
        gtk_menu_attach ((GtkMenu *) menu, item, 0, 1, i, i + 1);
        has_items = TRUE;
    }

    if (G_UNLIKELY (!has_items))
    {
        g_object_unref (g_object_ref_sink (menu));
        return NULL;
    }

    g_signal_connect_swapped (menu, "destroy", (GCallback) free_menu_click, mc);
    return menu;
}

/**
 * donna_app_show_menu:
 * @app: The #DonnaApp
 * @nodes: (element-type DonnaNode): An array of nodes to show in a menu
 * @menu: Name of the menu definition to use
 * @error: (allow-none): Return location of a #GError, or %NULL
 *
 * Shows a menu consisting the all the #DonnaNode<!-- -->s in @nodes, using menu
 * definition @menu.
 *
 * As you probably know, donna uses nodes to represent about everything. Nodes
 * can be shown in a #DonnaTreeView, of course, but also in menus.
 * Nothing specfic needs to be done, and any node can be used. It is however
 * possible to extra special properties on nodes, to be used in menus.
 *
 * If you don't intend to sort the nodes on the menu (see below), you can also
 * include %NULL in the array @nodes, to indicate where to include separator.
 * donna will make sure there's no separator as first or last item, and that
 * there's no more than one in a row.
 *
 * When the menu is shown, it will use the "menu definition" @menu. This must
 * simply be the name of a category found under <systemitem>menus</systemitem> i
 * config, which will include options for the menu, as well as how to handle the
 * action on click.
 *
 * Available options are:
 *
 * - <systemitem>show_icons</systemitem> (boolean): Whether to show icons or
 *   not; Defaults to true
 * - <systemitem>use_default_icons</systemitem> (boolean): When showing icons
 *   and there's no icon set on the node, fallback to default file/folder icons
 *   (based on node type). Defaults to true
 * - <systemitem>submenus</systemitem> (integer:enabled): How to handle
 *   containers. If "enabled" they will be submenus (with their
 *   content/children); If "disabled" they will be menuitems (that can be
 *   clicked, same as items); If "combine" then menuitems will be both clickable
 *   and include a submenu.  Defaults to "disabled"
 * - <systemitem>children</systemitem> (integer:node-type): Define which node
 *   type to show on submenus: "item", "container", or "all" Defaults to "all"
 * - <systemitem>children_show_hidden</systemitem> (boolean): Whether or not to
 *   include "hidden"/dot files in submenus (similar to the
 *   <systemitem>show_hidden</systemitem> option of treeviews) Defaults to true
 * - <systemitem>can_children_submenus</systemitem> (boolean): Whether to use
 *   node's <systemitem>menu-submenus</systemitem> property to overwrite option
 *   <systemitem>submenus</systemitem> Defaults to true
 * - <systemitem>can_children_menu</systemitem> (boolean): Whether to use node's
 *   <systemitem>menu-menu</systemitem> property to overwrite @menu
 * - <systemitem>sort</systemitem> (boolean): Whether to sort nodes in menu. See
 *   #ct-name-options for sort-related options. Defaults to false
 *
 *
 * Node properties used in menus are:
 *
 * - <systemitem>name</systemitem>: The label of the menuitem
 * - <systemitem>menu-is-name-markup</systemitem> (boolean): Whether the label
 *   contains markup
 * - <systemitem>desc</systemitem>: The tooltip of the menuitem
 * - <systemitem>menu-is-sensitive</systemitem> (boolean): Whether the menuitem
 *   is sensitive or not
 * - <systemitem>menu-is-combined-sensitive</systemitem> (boolean): If the item
 *   is in "combine" mode (i.e. both a menuitem and submenu), whether only the
 *   item-part is sensitive or not
 * - <systemitem>menu-is-label-bold</systemitem> (boolean): Whether the label
 *   must be in bold or not
 * - <systemitem>menu-submenus</systemitem> (uint): Overwrite
 *   <systemitem>submenus</systemitem> if
 *   <systemitem>can_children_submenus</systemitem> is true
 * - <systemitem>menu-menu</systemitem> (string): Overwrite @menu if
 *   <systemitem>can_children_menu</systemitem> is true
 *
 * Additionally, if <systemitem>show_icons</systemitem> is true:
 *
 * - <systemitem>menu-image-special</systemitem> (uint): A
 *   %DonnaImageMenuItemImageSpecial if the menuitem is a check or radio option
 * - <systemitem>menu-is-active</systemitem>: If check or radio, whether it is
 *   active/checked or not
 * - <systemitem>menu-is-inconsistent</systemitem>: If check, whether it is
 *   inconsistent or not
 * - <systemitem>icon</systemitem>: If image, the actual icon to use
 * - <systemitem>menu-image-selected</systemitem> (icon): If image, the actual
 *   icon to use when the menuitem is selected
 *
 *
 * When a menuitem is clicked, processing said click happens very much like
 * on treeviews, except instead of using <systemitem>click_modes</systemitem>
 * the triggers are looked for in category @menu under
 * <systemitem>menus</systemitem> (alongside the options).
 *
 * Triggers will be parsed using the following variables:
 *
 * - <systemitem>\%N</systemitem>: Location of the clicked node
 * - <systemitem>\%n</systemitem>: The clicked node
 *
 * For both options & clicks/triggers, if nothing if found under @menu then
 * category <systemitem>defaults/menus</systemitem> is used.
 *
 * Returns: %TRUE is the menu was popped up, else %FALSE
 */
gboolean
donna_app_show_menu (DonnaApp       *app,
                     GPtrArray      *nodes,
                     const gchar    *name,
                     GError        **error)
{
    struct menu_click *mc;
    GtkMenu *menu;

    g_return_val_if_fail (DONNA_IS_APP (app), NULL);
    g_return_val_if_fail (nodes != NULL, NULL);

    if (G_UNLIKELY (nodes->len == 0))
    {
        g_set_error (error, DONNA_APP_ERROR, DONNA_APP_ERROR_OTHER,
                "Unable to show menu, empty array of nodes given");
        g_ptr_array_unref (nodes);
        return FALSE;
    }

    mc = load_mc (app, name, nodes);
    /* menu will not be packed anywhere, so we need to take ownership and handle
     * it when done, i.e. on "unmap-event". It will trigger the widget's destroy
     * which is when we'll free mc */
    menu = (GtkMenu *) load_menu (mc);
    if (G_UNLIKELY (!menu))
    {
        g_set_error (error, DONNA_APP_ERROR, DONNA_APP_ERROR_OTHER,
                "Cannot show/popup an empty menu");
        free_menu_click (mc);
        return FALSE;
    }
    menu = g_object_ref_sink (menu);
    gtk_widget_add_events ((GtkWidget *) menu, GDK_STRUCTURE_MASK);
    g_signal_connect (menu, "unmap-event", (GCallback) g_object_unref, NULL);

    gtk_menu_popup (menu, NULL, NULL, NULL, NULL, 0,
            gtk_get_current_event_time ());
    return TRUE;
}

/**
 * donna_app_show_error:
 * @app: The #DonnaApp
 * @error: (allow-none): Related #GError to show the message of
 * @fmt: printf-like format of the error message
 * @...: printf-like arguments
 *
 * Show an error message.
 *
 * @fmt will be the main message/title shown on the window, while the error
 * message from @error (if any) will be used as secondary text below.
 */
void
donna_app_show_error (DonnaApp       *app,
                      const GError   *error,
                      const gchar    *fmt,
                      ...)
{
    DonnaAppPrivate *priv;
    GtkWidget *w;
    gchar *title;
    va_list va_args;

    g_return_if_fail (DONNA_IS_APP (app));
    g_return_if_fail (fmt != NULL);
    priv = app->priv;

    va_start (va_args, fmt);
    title = g_strdup_vprintf (fmt, va_args);
    va_end (va_args);

    w = gtk_message_dialog_new (priv->window,
            GTK_DIALOG_DESTROY_WITH_PARENT | ((priv->exiting) ? GTK_DIALOG_MODAL : 0),
            GTK_MESSAGE_ERROR,
            GTK_BUTTONS_CLOSE,
            "%s", title);
    g_free (title);
    gtk_message_dialog_format_secondary_text ((GtkMessageDialog *) w, "%s",
            (error) ? error->message : "");
    g_signal_connect_swapped (w, "response", (GCallback) gtk_widget_destroy, w);
    gtk_widget_show_all (w);
    if (G_UNLIKELY (priv->exiting))
        /* if this happens while exiting (i.e. after main window was closed
         * (hidden), e.g. during a task from event "exit") then we make sure the
         * user gets to see/read the error, by blocking until he's closed it */
        gtk_dialog_run ((GtkDialog *) w);
}

/**
 * donna_app_get_ct_data:
 * @col_name: Name of the column
 * @app: The #DonnaApp
 *
 * Returns the columntype data for @column
 *
 * <note><para>Note that the order of arguments is unlike other functions, so
 * this function can be used as #get_ct_data_fn</para></note>
 *
 * Returns: The columntype data (to be used e.g. by filters)
 */
gpointer
donna_app_get_ct_data (const gchar    *col_name,
                       DonnaApp       *app)
{
    DonnaAppPrivate *priv;
    gchar *type = NULL;
    guint i;

    g_return_val_if_fail (col_name != NULL, NULL);
    g_return_val_if_fail (DONNA_IS_APP (app), NULL);
    priv = app->priv;

    if (!donna_config_get_string (priv->config, NULL, &type,
            "columns/%s/type", col_name))
        /* fallback to its name */
        type = g_strdup (col_name);

    g_rec_mutex_lock (&priv->rec_mutex);
    for (i = 0; i < NB_COL_TYPES; ++i)
    {
        if (streq (type, priv->column_types[i].name))
        {
            /* should never be possible, since filter has the ct */
            if (G_UNLIKELY (!priv->column_types[i].ct))
                priv->column_types[i].ct = g_object_new (
                        priv->column_types[i].type, "app", app, NULL);
            if (!priv->column_types[i].ct_data)
                donna_column_type_refresh_data (priv->column_types[i].ct,
                        col_name, NULL, NULL, FALSE, &priv->column_types[i].ct_data);
            g_rec_mutex_unlock (&priv->rec_mutex);
            g_free (type);
            return priv->column_types[i].ct_data;
        }
    }
    /* Again: this cannot happen, since the filter has the ct */
    g_rec_mutex_unlock (&priv->rec_mutex);
    g_free (type);
    return NULL;
}

gboolean
_donna_app_filter_nodes (DonnaApp        *app,
                         GPtrArray       *nodes,
                         const gchar     *filter_str,
                         get_ct_data_fn   get_ct_data,
                         gpointer         data,
                         GError         **error)
{
    GError *err = NULL;
    DonnaFilter *filter;
    guint i;

    g_return_val_if_fail (get_ct_data != NULL, FALSE);
    g_return_val_if_fail (nodes != NULL, FALSE);
    g_return_val_if_fail (filter_str != NULL, FALSE);

    if (G_UNLIKELY (nodes->len == 0))
        return FALSE;

    filter = donna_app_get_filter (app, filter_str);
    if (G_UNLIKELY (!filter))
    {
        g_set_error (error, DONNA_APP_ERROR, DONNA_APP_ERROR_OTHER,
                "Failed to create a filter object for '%s'",
                filter_str);
        return FALSE;
    }

    for (i = 0; i < nodes->len; )
        if (!donna_filter_is_match (filter, nodes->pdata[i],
                    get_ct_data, data, &err))
        {
            if (err)
            {
                g_propagate_error (error, err);
                g_object_unref (filter);
                return FALSE;
            }
            /* last element comes here, hence no need to increment i */
            g_ptr_array_remove_index_fast (nodes, i);
        }
        else
            ++i;

    g_object_unref (filter);
    return TRUE;
}

/**
 * donna_app_filter_nodes:
 * @app: The #DonnaApp
 * @nodes: (element-type DonnaNode): Array of #DonnaNode<!-- -->s to filter
 * @filter: The actual filter to apply
 * @error: (allow-none): Return location of a #GError, or %NULL
 *
 * Filter @nodes using @filter
 *
 * Filters references columns, and might therefore be linked to a treeview (in
 * order to use treeview-specific column options). This will instead use
 * "generic" options, as the filtering happens via donna/app and not any
 * treeview (i.e. it uses donna_app_get_ct_data()).
 *
 * To filter nodes via a treeview, see donna_tree_view_filter_nodes()
 *
 * <note><para>Every node that doesn't match the filter will be removed from
 * @nodes. Make sure to own the array, since it will be changed (i.e. don't use
 * an array returned from a get-children task, as it could also be
 * referenced/used elsewhere)</para></note>
 *
 * Returns: %TRUE on success, else %FALSE
 */
gboolean
donna_app_filter_nodes (DonnaApp       *app,
                        GPtrArray      *nodes,
                        const gchar    *filter,
                        GError       **error)
{
    g_return_val_if_fail (DONNA_IS_APP (app), FALSE);

    return _donna_app_filter_nodes (app, nodes, filter,
            (get_ct_data_fn) donna_app_get_ct_data, app, error);
}

/**
 * donna_app_nodes_io_task:
 * @app: The #DonnaApp
 * @nodes: (element-type DonnaNode): Array of #DonnaNode<!-- -->s, source(s) of
 * the IO operation
 * @io_type: Type of IO operation
 * @dest: Container #DonnaNode, destination of the IO operation
 * @new_name: (allow-none): New name to be used in the IO operation, or %NULL
 * @error: (allow-none): Return location of a #GError, or %NULL
 *
 * Returns a task to perform the specified IO operation. For %DONNA_IO_COPY and
 * %DONNA_IO_MOVE it is possible to specify @new_name, a new name to be used in
 * the operation (e.g. to rename the item as it is copied/moved).
 * @dest can be omitted (and will be ignored) for %DONNA_IO_DELETE operations.
 *
 * All nodes in @nodes must be from the same provider. The provider of source
 * nodes in @nodes will be used first, and if it failed to provide a task then
 * the provider of @dest (if different) will be tried.
 *
 * Returns: (transfer floating): A floating #DonnaTask to perform the IO
 * operation, or %NULL
 */
DonnaTask *
donna_app_nodes_io_task (DonnaApp       *app,
                         GPtrArray      *nodes,
                         DonnaIoType     io_type,
                         DonnaNode      *dest,
                         const gchar    *new_name,
                         GError        **error)
{
    DonnaProvider *provider;
    DonnaTask *task;
    guint i;

    g_return_val_if_fail (DONNA_IS_APP (app), FALSE);
    g_return_val_if_fail (nodes != NULL, FALSE);
    g_return_val_if_fail (io_type == DONNA_IO_COPY || io_type == DONNA_IO_MOVE
            || io_type == DONNA_IO_DELETE, FALSE);
    if (io_type != DONNA_IO_DELETE)
        g_return_val_if_fail (DONNA_IS_NODE (dest), FALSE);

    if (G_UNLIKELY (nodes->len == 0))
    {
        g_set_error (error, DONNA_APP_ERROR, DONNA_APP_ERROR_EMPTY,
                "Cannot perform IO: no nodes given");
        return NULL;
    }

    /* make sure all nodes are from the same provider */
    provider = donna_node_peek_provider (nodes->pdata[0]);
    for (i = 1; i < nodes->len; ++i)
    {
        if (provider != donna_node_peek_provider (nodes->pdata[i]))
        {
            g_set_error (error, DONNA_APP_ERROR, DONNA_APP_ERROR_OTHER,
                    "Cannot perform IO: nodes are not all from the same provider/domain.");
            return NULL;
        }
    }

    task = donna_provider_io_task (provider, io_type, TRUE, nodes,
            dest, new_name, error);
    if (!task && dest && provider != donna_node_peek_provider (dest))
    {
        g_clear_error (error);
        /* maybe the IO can be done by dest's provider */
        task = donna_provider_io_task (donna_node_peek_provider (dest),
                io_type, FALSE, nodes, dest, new_name, error);
    }

    if (!task)
    {
        g_prefix_error (error, "Couldn't to perform IO operation: ");
        return NULL;
    }

    return task;
}

struct ask
{
    GMainLoop *loop;
    GtkWidget *win;
    gint response;
};

static void
btn_clicked (GObject *btn, struct ask *ask)
{
    ask->response = GPOINTER_TO_INT (g_object_get_data (btn, "response"));
    gtk_widget_destroy (ask->win);
}

/**
 * donna_app_ask:
 * @app: The #DonnaApp
 * @title: Title of the dialog
 * @details: (allow-none): Additional text to shown below the title
 * @btn1_icon: (allow-none): Name of the icon to use on button 1
 * @btn1_label: (allow-none): Label of button 1
 * @btn2_icon: (allow-none): Name of the icon to use on button 2
 * @btn2_label: (allow-none): Label of button 2
 * @...: %NULL terminated icon/label for other buttons
 *
 * Show a dialog asking the user to make a choice. This will consist of @title,
 * optionally a longer text in @details which can begin with prefix "markup:" to
 * indicate that it must be parsed using Pango markup.
 *
 * The dialog will then have at least 2 buttons, allowing the user to make a
 * choice. Buttons are numbered from 1, and will be placed from right to left.
 * All buttons will close the dialog, and the button number will be returned.
 *
 * If not specified, button 1 will default to "Yes" with "gtk-yes" as icon. If
 * not specified, button 2 will default to "No" with "gtk-no" as icon.
 *
 * Note that a new main loop will be started after showing the dialog, waiting
 * for a choice to be made.
 *
 * Returns: The number of the button pressed
 */
gint
donna_app_ask (DonnaApp       *app,
               const gchar    *title,
               const gchar    *details,
               const gchar    *btn1_icon,
               const gchar    *btn1_label,
               const gchar    *btn2_icon,
               const gchar    *btn2_label,
               ...)
{
    DonnaAppPrivate *priv;
    struct ask ask = { NULL, };
    GtkWidget *area;
    GtkBox *box;
    GtkWidget *btn;
    GtkWidget *w;
    gint i = 0;
    va_list va_args;

    g_return_val_if_fail (DONNA_IS_APP (app), 0);
    g_return_val_if_fail (title != NULL, 0);
    priv = app->priv;

    ask.win = gtk_message_dialog_new (priv->window,
            GTK_DIALOG_DESTROY_WITH_PARENT,
            GTK_MESSAGE_QUESTION,
            GTK_BUTTONS_NONE,
            "%s", title);

    if (details)
    {
        if (streqn (details, "markup:", 7))
            gtk_message_dialog_format_secondary_markup ((GtkMessageDialog *) ask.win,
                    "%s", details + 7);
        else
            gtk_message_dialog_format_secondary_text ((GtkMessageDialog *) ask.win,
                    "%s", details);
    }

    area = gtk_dialog_get_action_area ((GtkDialog *) ask.win);
    box = (GtkBox *) gtk_box_new (GTK_ORIENTATION_HORIZONTAL, 4);
    gtk_box_set_homogeneous (box, TRUE);
    gtk_container_add ((GtkContainer *) area, (GtkWidget *) box);

    btn = gtk_button_new_with_label ((btn1_label) ? btn1_label : "Yes");
    w = gtk_image_new_from_icon_name ((btn1_icon) ? btn1_icon : "gtk-yes",
            GTK_ICON_SIZE_MENU);
    if (w)
        gtk_button_set_image ((GtkButton *) btn, w);
    g_object_set_data ((GObject *) btn, "response", GINT_TO_POINTER (++i));
    g_signal_connect (btn, "clicked", (GCallback) btn_clicked, &ask);
    gtk_box_pack_end (box, btn, FALSE, TRUE, 0);

    btn = gtk_button_new_with_label ((btn2_label) ? btn2_label : "No");
    w = gtk_image_new_from_icon_name ((btn2_icon) ? btn2_icon : "gtk-no",
            GTK_ICON_SIZE_MENU);
    if (w)
        gtk_button_set_image ((GtkButton *) btn, w);
    g_object_set_data ((GObject *) btn, "response", GINT_TO_POINTER (++i));
    g_signal_connect (btn, "clicked", (GCallback) btn_clicked, &ask);
    gtk_box_pack_end (box, btn, FALSE, TRUE, 0);

    va_start (va_args, btn2_label);
    for (;;)
    {
        const gchar *s;

        s = va_arg (va_args, const gchar *);
        if (!s)
            break;
        btn = gtk_button_new_with_label (s);

        s = va_arg (va_args, const gchar *);
        if (s)
        {
            w = gtk_image_new_from_icon_name (s, GTK_ICON_SIZE_MENU);
            if (w)
                gtk_button_set_image ((GtkButton *) btn, w);
        }

        g_object_set_data ((GObject *) btn, "response", GINT_TO_POINTER (++i));
        g_signal_connect (btn, "clicked", (GCallback) btn_clicked, &ask);
        gtk_box_pack_end (box, btn, FALSE, TRUE, 0);
    }
    va_end (va_args);

    ask.loop = g_main_loop_new (NULL, TRUE);
    g_signal_connect_swapped (ask.win, "destroy",
            (GCallback) g_main_loop_quit, ask.loop);
    gtk_widget_show_all (ask.win);
    g_main_loop_run (ask.loop);

    return ask.response;
}

struct ask_text
{
    GtkWindow   *win;
    GtkEntry    *entry;
    gchar       *s;
};

static void
btn_ok_cb (struct ask_text *data)
{
    data->s = g_strdup (gtk_entry_get_text (data->entry));
    gtk_widget_destroy ((GtkWidget *) data->win);
}

static gboolean
key_press_cb (struct ask_text *data, GdkEventKey *event)
{
    if (event->keyval == GDK_KEY_Escape)
        gtk_widget_destroy ((GtkWidget *) data->win);
    return FALSE;
}

/**
 * donna_app_ask_text:
 * @app: The #DonnaApp
 * @title: Title of the dialog
 * @details: (allow-none): Additional text to shown below the title
 * @main_default: (allow-none): Default to use in the entry
 * @other_defaults: (allow-none): %NULL terminated array of strings, to use as
 * other defaults in the popdown menu of the entry
 * @error: (allow-none): Return location of a #GError, or %NULL
 *
 * Shows a dialog asking the user for input.
 *
 * The window will have the CSS id/name <systemitem>ask-text</systemitem> to
 * allow customization. @title will be shown on the dialog, with CSS class
 * <systemitem>title</systemitem>
 * If @details was specified, it will be shown below, using CSS class
 * <systemitem>details</systemitem> If it starts with prefix "markup:" then it
 * will be processed using Pango markup.
 *
 * If specified, @main_default will be featured in the entry. If @other_defaults
 * was specified, the entry will also feature a popdown menu including the given
 * strings, in the given order.
 *
 * Note that, as in other places in donna, using Ctrl+A will automatically allow
 * to select all if nothing is selected, if the basename only is selected (e.g.
 * everything before last dot) then it selects all, else select the basename
 * only.
 *
 * The window will include two buttons: Ok and Cancel. If the user presses Esc
 * or clicks Cancel then %NULL will be returned. If nothing was enterred, an
 * empty string will be returned.
 *
 * Note that a new main loop will be started after showing the dialog, until the
 * dialog is closed.
 *
 * Returns: (transfer full): Newly allocated string (use g_free() when done)
 * enterred by the user, or %NULL.
 */
gchar *
donna_app_ask_text (DonnaApp       *app,
                    const gchar    *title,
                    const gchar    *details,
                    const gchar    *main_default,
                    const gchar   **other_defaults,
                    GError        **error)
{
    DonnaAppPrivate *priv;
    GtkStyleContext *context;
    GMainLoop *loop;
    struct ask_text data = { NULL, };
    GtkBox *box;
    GtkBox *btn_box;
    GtkLabel *lbl;
    GtkWidget *w;

    g_return_val_if_fail (DONNA_IS_APP (app), FALSE);
    g_return_val_if_fail (title != NULL, FALSE);
    priv = app->priv;

    data.win = (GtkWindow *) gtk_window_new (GTK_WINDOW_TOPLEVEL);
    gtk_widget_set_name ((GtkWidget *) data.win, "ask-text");
    gtk_window_set_transient_for (data.win, priv->window);
    gtk_window_set_destroy_with_parent (data.win, TRUE);
    gtk_window_set_default_size (data.win, 230, -1);
    gtk_window_set_decorated (data.win, FALSE);
    gtk_container_set_border_width ((GtkContainer *) data.win, 4);

    box = (GtkBox *) gtk_box_new (GTK_ORIENTATION_VERTICAL, 0);
    gtk_container_add ((GtkContainer *) data.win, (GtkWidget *) box);

    w = gtk_label_new (title);
    lbl = (GtkLabel *) w;
    gtk_label_set_selectable ((GtkLabel *) w, TRUE);
    context = gtk_widget_get_style_context (w);
    gtk_style_context_add_class (context, "title");
    gtk_box_pack_start (box, w, FALSE, FALSE, 0);

    if (details)
    {
        if (streqn (details, "markup:", 7))
        {
            w = gtk_label_new (NULL);
            gtk_label_set_markup ((GtkLabel *) w, details + 7);
        }
        else
            w = gtk_label_new (details);
        gtk_label_set_selectable ((GtkLabel *) w, TRUE);
        gtk_misc_set_alignment ((GtkMisc *) w, 0, 0.5);
        context = gtk_widget_get_style_context (w);
        gtk_style_context_add_class (context, "details");
        gtk_box_pack_start (box, w, FALSE, FALSE, 0);
    }

    if (other_defaults)
    {
        w = gtk_combo_box_text_new_with_entry ();
        data.entry = (GtkEntry *) gtk_bin_get_child ((GtkBin *) w);
        for ( ; *other_defaults; ++other_defaults)
            gtk_combo_box_text_append_text ((GtkComboBoxText *) w, *other_defaults);
    }
    else
    {
        w = gtk_entry_new ();
        data.entry = (GtkEntry *) w;
    }
    g_signal_connect_swapped (data.entry, "activate",
            (GCallback) btn_ok_cb, &data);
    g_signal_connect (data.entry, "key-press-event",
            (GCallback) _key_press_ctrl_a_cb, NULL);
    g_signal_connect_swapped (data.entry, "key-press-event",
            (GCallback) key_press_cb, &data);

    if (main_default)
        gtk_entry_set_text (data.entry, main_default);
    gtk_box_pack_start (box, w, FALSE, FALSE, 0);


    btn_box = (GtkBox *) gtk_box_new (GTK_ORIENTATION_HORIZONTAL, 0);
    gtk_box_pack_end (box, (GtkWidget *) btn_box, FALSE, FALSE, 4);

    w = gtk_button_new_with_label ("Ok");
    gtk_button_set_image ((GtkButton *) w,
            gtk_image_new_from_icon_name ("gtk-ok", GTK_ICON_SIZE_MENU));
    g_signal_connect_swapped (w, "clicked", (GCallback) btn_ok_cb, &data);
    gtk_box_pack_end (btn_box, w, FALSE, FALSE, 2);

    w = gtk_button_new_with_label ("Cancel");
    gtk_button_set_image ((GtkButton *) w,
            gtk_image_new_from_icon_name ("gtk-cancel", GTK_ICON_SIZE_MENU));
    g_signal_connect_swapped (w, "clicked", (GCallback) gtk_widget_destroy, data.win);
    gtk_box_pack_end (btn_box, w, FALSE, FALSE, 2);


    loop = g_main_loop_new (NULL, TRUE);
    g_signal_connect_swapped (data.win, "destroy", (GCallback) g_main_loop_quit, loop);
    gtk_widget_show_all ((GtkWidget *) data.win);
    gtk_widget_grab_focus ((GtkWidget *) data.entry);
    gtk_label_select_region (lbl, 0, 0);
    g_main_loop_run (loop);

    return data.s;
}


/* DONNATELLA */

static DonnaArrangement *
tree_select_arrangement (DonnaTreeView  *tree,
                         const gchar    *tv_name,
                         DonnaNode      *node,
                         DonnaApp       *app)
{
    DonnaAppPrivate *priv = app->priv;
    DonnaArrangement *arr = NULL;
    GSList *list, *l;
    gchar _source[255];
    gchar *source[] = { _source, (gchar *) "arrangements" };
    guint i, max = sizeof (source) / sizeof (source[0]);
    DonnaEnabledTypes type;
    gboolean is_first = TRUE;
    gchar buf[255], *b = buf;
    gchar *location;

    if (!node)
        return NULL;

    if (snprintf (source[0], 255, "tree_views/%s/arrangements", tv_name) >= 255)
        source[0] = g_strdup_printf ("tree_views/%s/arrangements", tv_name);

    for (i = 0; i < max; ++i)
    {
        gchar *sce;

        sce = source[i];
        if (donna_config_has_category (priv->config, NULL, sce))
        {
            if (!donna_config_get_int (priv->config, NULL, (gint *) &type,
                        "%s/type", sce))
                type = DONNA_ENABLED_TYPE_ENABLED;
            switch (type)
            {
                case DONNA_ENABLED_TYPE_ENABLED:
                case DONNA_ENABLED_TYPE_COMBINE:
                    /* process */
                    break;

                case DONNA_ENABLED_TYPE_DISABLED:
                    /* flag to stop */
                    i = max;
                    break;

                case DONNA_ENABLED_TYPE_IGNORE:
                    /* next source */
                    continue;

                case DONNA_ENABLED_TYPE_UNKNOWN:
                default:
                    g_warning ("Unable to load arrangements: Invalid option '%s/type'",
                            sce);
                    /* flag to stop */
                    i = max;
                    break;
            }
        }
        else
            /* next source */
            continue;

        if (i == max)
            break;

        if (i == 0)
        {
            list = g_object_get_data ((GObject *) tree, "arrangements-masks");
            if (!list)
            {
                list = load_arrangements (priv->config, sce);
                g_object_set_data_full ((GObject *) tree, "arrangements-masks",
                        list, (GDestroyNotify) free_arrangements);
            }
        }
        else
            list = priv->arrangements;

        if (is_first)
        {
            /* get full location of node, with an added / at the end so mask can
             * easily be made for a folder & its subfodlers */
            location = donna_node_get_location (node);
            if (snprintf (buf, 255, "%s:%s/",
                        donna_node_get_domain (node), location) >= 255)
                b = g_strdup_printf ("%s:%s/",
                        donna_node_get_domain (node), location);
            g_free (location);
            is_first = FALSE;
        }

        for (l = list; l; l = l->next)
        {
            struct argmt *argmt = l->data;

            if (g_pattern_match_string (argmt->pspec, b))
            {
                if (!arr)
                {
                    arr = g_new0 (DonnaArrangement, 1);
                    arr->priority = DONNA_ARRANGEMENT_PRIORITY_NORMAL;
                }

                if (!(arr->flags & DONNA_ARRANGEMENT_HAS_COLUMNS))
                    donna_config_arr_load_columns (priv->config, arr,
                            "%s/%s", sce, argmt->name);

                if (!(arr->flags & DONNA_ARRANGEMENT_HAS_SORT))
                    donna_config_arr_load_sort (priv->config, arr,
                            "%s/%s", sce, argmt->name);

                if (!(arr->flags & DONNA_ARRANGEMENT_HAS_SECOND_SORT))
                    donna_config_arr_load_second_sort (priv->config, arr,
                            "%s/%s", sce, argmt->name);

                if (!(arr->flags & DONNA_ARRANGEMENT_HAS_COLUMNS_OPTIONS))
                    donna_config_arr_load_columns_options (priv->config, arr,
                            "%s/%s", sce, argmt->name);

                if (!(arr->flags & DONNA_ARRANGEMENT_HAS_COLOR_FILTERS))
                    donna_config_arr_load_color_filters (priv->config,
                            app, arr,
                            "%s/%s", sce, argmt->name);

                if ((arr->flags & DONNA_ARRANGEMENT_HAS_ALL) == DONNA_ARRANGEMENT_HAS_ALL)
                    break;
            }
        }
        /* at this point type can only be ENABLED or COMBINE */
        if (type == DONNA_ENABLED_TYPE_ENABLED || (arr /* could still be NULL */
                    /* even in COMBINE, if arr is "full" we're done */
                    && (arr->flags & DONNA_ARRANGEMENT_HAS_ALL) == DONNA_ARRANGEMENT_HAS_ALL))
            break;
    }

    /* special: color filters might have been loaded with a type COMBINE, which
     * resulted in them loaded but no flag set (in order to keep loading others
     * from other arrangements). We still don't set the flag, so that treeview
     * can keep combining with its own color filters */

    if (b != buf)
        g_free (b);
    if (source[0] != _source)
        g_free (source[0]);
    return arr;
}

static DonnaTreeView *
load_tree_view (DonnaApp *app, const gchar *name)
{
    DonnaTreeView *tree;

    tree = donna_app_get_tree_view (app, name);
    if (!tree)
    {
        /* shall we load it indeed */
        tree = (DonnaTreeView *) donna_tree_view_new (app, name);
        if (tree)
        {
            g_signal_connect (tree, "select-arrangement",
                    G_CALLBACK (tree_select_arrangement), app);
            app->priv->tree_views = g_slist_prepend (app->priv->tree_views,
                    g_object_ref (tree));
            g_signal_emit (app, donna_app_signals[TREE_VIEW_LOADED], 0, tree);
        }
    }
    return tree;
}

static GtkWidget *
load_widget (DonnaApp    *app,
             gchar      **def,
             gchar      **active_list_name,
             GtkWidget  **active_list_widget)
{
    DonnaAppPrivate *priv = app->priv;
    GtkWidget *w;
    gchar *end;
    gchar *sep = NULL;

    skip_blank (*def);

    for (end = *def; end; ++end)
    {
        if (*end == '(')
        {
            if (end - *def == 4 && (streqn (*def, "boxH", 4)
                        || streqn (*def, "boxV", 4)))
            {
                GtkBox *box;

                box = (GtkBox *) gtk_box_new (((*def)[3] == 'H')
                        ? GTK_ORIENTATION_HORIZONTAL : GTK_ORIENTATION_VERTICAL,
                        0);
                *def = end + 1;
                for (;;)
                {
                    w = load_widget (app, def, active_list_name, active_list_widget);
                    if (!w)
                    {
                        g_object_unref (g_object_ref_sink (box));
                        return NULL;
                    }

                    gtk_box_pack_start (box, w, TRUE, TRUE, 0);

                    if (**def == ',')
                        ++*def;
                    else if (**def != ')')
                    {
                        g_debug("expected ',' or ')': %s", *def);
                        g_object_unref (g_object_ref_sink (box));
                        return NULL;
                    }
                    else
                        break;
                }
                ++*def;
                return (GtkWidget *) box;
            }
            else if (end - *def == 5 && (streqn (*def, "paneH", 5)
                        || streqn (*def, "paneV", 5)))
            {
                GtkPaned *paned;
                gboolean is_fixed;

                paned = (GtkPaned *) gtk_paned_new (((*def)[4] == 'H')
                        ? GTK_ORIENTATION_HORIZONTAL : GTK_ORIENTATION_VERTICAL);
                *def = end + 1;

                for ( ; isblank (**def); ++*def)
                    ;
                is_fixed = (**def == '!');
                if (is_fixed)
                    ++*def;
                w = load_widget (app, def, active_list_name, active_list_widget);
                if (!w)
                {
                    g_object_unref (g_object_ref_sink (paned));
                    return NULL;
                }

                gtk_paned_pack1 (paned, w, !is_fixed, TRUE);

                if (**def == '@')
                {
                    gint pos = 0;

                    for (++*def; **def >= '0' && **def <= '9'; ++*def)
                        pos = pos * 10 + **def - '0';
                    gtk_paned_set_position (paned, pos);
                }

                if (**def != ',')
                {
                    g_debug("missing second item in pane: %s", *def);
                    g_object_unref (g_object_ref_sink (paned));
                    return NULL;
                }

                ++*def;
                for ( ; isblank (**def); ++*def)
                    ;
                is_fixed = (**def == '!');
                if (is_fixed)
                    ++*def;
                w = load_widget (app, def, active_list_name, active_list_widget);
                if (!w)
                {
                    g_object_unref (g_object_ref_sink (paned));
                    return NULL;
                }

                gtk_paned_pack2 (paned, w, !is_fixed, TRUE);

                if (**def != ')')
                {
                    g_debug("only 2 items per pane: %s", *def);
                    g_object_unref (g_object_ref_sink (paned));
                    return NULL;
                }
                ++*def;
                return (GtkWidget *) paned;
            }
        }
        else if (*end == ':')
            sep = end;
        else if (*end == ',' || *end == '@' || *end == ')' || *end == '\0')
        {
            gchar e = *end;

            if (!sep)
            {
                g_debug("missing ':' with item name: %s", *def);
                return NULL;
            }

            *sep = '\0';
            if (streq (*def, "treeview"))
            {
                DonnaTreeView *tree;

                *def = sep + 1;
                *end = '\0';

                w = gtk_scrolled_window_new (NULL, NULL);
                tree = load_tree_view (app, *def);
                if (!donna_tree_view_is_tree (tree) && !priv->active_list)
                {
                    gboolean skip;
                    if (!donna_config_get_boolean (priv->config, NULL, &skip,
                                "tree_views/%s/not_active_list",
                                donna_tree_view_get_name (tree))
                            || !skip)
                    {
                        if (streq (*active_list_name,
                                    donna_tree_view_get_name (tree)))
                        {
                            priv->active_list = tree;
                            *active_list_widget = (GtkWidget *) tree;
                        }
                        else if (!*active_list_widget)
                            *active_list_widget = (GtkWidget *) tree;
                    }
                }
                gtk_container_add ((GtkContainer *) w, (GtkWidget *) tree);
                *end = e;
            }
            else if (streq (*def, "toolbar"))
                w = gtk_toolbar_new ();
            else
            {
                g_debug("invalid item type: %s", *def);
                *sep = ':';
                return NULL;
            }
            *sep = ':';

            *def = end;
            return w;
        }
    }
    return NULL;
}

static gchar *
parse_string (DonnaApp *app, gchar *fmt)
{
    DonnaAppPrivate *priv = app->priv;
    GString *str = NULL;
    gchar *s = fmt;
    DonnaNode *node;
    gchar *ss;

    while ((s = strchr (s, '%')))
    {
        switch (s[1])
        {
            case 'a':
                if (!str)
                    str = g_string_new (NULL);
                g_string_append_len (str, fmt, s - fmt);
                g_string_append (str, donna_tree_view_get_name (priv->active_list));
                s += 2;
                fmt = s;
                break;

            case 'd':
                if (!str)
                    str = g_string_new (NULL);
                g_string_append_len (str, fmt, s - fmt);
                if (priv->cur_dirname)
                    g_string_append (str, priv->cur_dirname);
                s += 2;
                fmt = s;
                break;

            case 'L':
                if (!str)
                    str = g_string_new (NULL);
                g_string_append_len (str, fmt, s - fmt);
                node = donna_tree_view_get_location (priv->active_list);
                if (G_LIKELY (node))
                {
                    const gchar *domain;
                    gint type;

                    domain = donna_node_get_domain (node);
                    if (!donna_config_get_int (priv->config, NULL, &type,
                                "donna/domain_%s", domain))
                        type = (streq ("fs", domain))
                            ? TITLE_DOMAIN_LOCATION : TITLE_DOMAIN_FULL_LOCATION;

                    if (type == TITLE_DOMAIN_LOCATION)
                        ss = donna_node_get_location (node);
                    else if (type == TITLE_DOMAIN_FULL_LOCATION)
                        ss = donna_node_get_full_location (node);
                    else if (type == TITLE_DOMAIN_CUSTOM)
                        if (!donna_config_get_string (priv->config, NULL, &ss,
                                    "donna/custom_%s", domain))
                            ss = donna_node_get_name (node);

                    g_string_append (str, ss);
                    g_free (ss);
                    g_object_unref (node);
                }
                s += 2;
                fmt = s;
                break;

            case 'l':
                if (!str)
                    str = g_string_new (NULL);
                g_string_append_len (str, fmt, s - fmt);
                node = donna_tree_view_get_location (priv->active_list);
                if (G_LIKELY (node))
                {
                    ss = donna_node_get_full_location (node);
                    g_string_append (str, ss);
                    g_free (ss);
                    g_object_unref (node);
                }
                s += 2;
                fmt = s;
                break;

            case 'v':
                if (!str)
                    str = g_string_new (NULL);
                g_string_append_len (str, fmt, s - fmt);
                g_string_append (str, PACKAGE_VERSION);
                s += 2;
                fmt = s;
                break;

            default:
                s += 2;
                break;
        }
    }

    if (!str)
        return NULL;

    g_string_append (str, fmt);
    return g_string_free (str, FALSE);
}

static void
refresh_window_title (DonnaApp *app)
{
    DonnaAppPrivate *priv = app->priv;
    gchar *def = (gchar *) "%L - Donnatella";
    gchar *fmt;
    gchar *str;

    if (!donna_config_get_string (priv->config, NULL, &fmt, "donna/title"))
        fmt = def;

    str = parse_string (app, fmt);
    gtk_window_set_title (priv->window, (str) ? str : fmt);
    g_free (str);
    if (fmt != def)
        g_free (fmt);
}

static gboolean
window_delete_event_cb (GtkWidget *window, GdkEvent *event, DonnaApp *app)
{
    static gboolean in_pre_exit = FALSE;

    /* because emitting event pre-exit could result in a new main loop started
     * while waiting for a trigger, there's a possibility of reentrancy that we
     * need to handle/avoid */
    if (in_pre_exit)
        return TRUE;
    in_pre_exit = TRUE;

    /* FALSE means it wasn't aborted */
    if (!donna_app_emit_event (app, "pre-exit", TRUE, NULL, NULL, NULL, NULL))
    {
        GSList *l;

        gtk_widget_hide (window);

        /* our version of destroy_with_parent */
        for (l = app->priv->windows; l; l = l->next)
            gtk_widget_destroy ((GtkWidget *) l->data);
        g_slist_free (app->priv->windows);
        app->priv->windows = NULL;

        gtk_main_quit ();
    }

    in_pre_exit = FALSE;
    return TRUE;
}

static void
switch_statuses_source (DonnaApp *app, guint source, DonnaStatusProvider *sp)
{
    DonnaAppPrivate *priv = app->priv;
    GSList *l;

    for (l = priv->statuses; l; l = l->next)
    {
        struct status *status = l->data;

        if (status->source == source)
        {
            GError *err = NULL;
            struct provider *provider;
            guint i;

            for (i = 0; i < status->providers->len; ++i)
            {
                provider = &g_array_index (status->providers, struct provider, i);
                if (provider->sp == sp)
                    break;
            }

            if (i >= status->providers->len)
            {
                struct provider p;

                p.sp = sp;
                p.id = donna_status_provider_create_status (p.sp,
                        status->name, &err);
                if (p.id == 0)
                {
                    g_warning ("Failed to connect statusbar area '%s' to new active-list: "
                            "create_status() failed: %s",
                            status->name, err->message);
                    g_clear_error (&err);
                    /* this simply makes sure the area is blank/not connected to
                     * any provider anymore */
                    donna_status_bar_update_area (priv->sb, status->name,
                            NULL, 0, NULL);
                    continue;
                }
                g_array_append_val (status->providers, p);
                provider = &g_array_index (status->providers, struct provider,
                        status->providers->len - 1);
            }

            if (!donna_status_bar_update_area (priv->sb, status->name,
                        provider->sp, provider->id, &err))
            {
                g_warning ("Failed to connect statusbar area '%s' to new active-list: "
                        "update_area() failed: %s",
                        status->name, err->message);
                g_clear_error (&err);
                /* this simply makes sure the area is blank/not connected to
                 * any provider anymore */
                donna_status_bar_update_area (priv->sb, status->name,
                        NULL, 0, NULL);
            }
        }
    }
}

static void
update_cur_dirname (DonnaApp *app)
{
    DonnaAppPrivate *priv = app->priv;
    DonnaNode *node;
    gchar *s;

    g_object_get (priv->active_list, "location", &node, NULL);
    if (G_UNLIKELY (!node))
        return;

    if (!streq ("fs", donna_node_get_domain (node)))
    {
        g_object_unref (node);
        return;
    }

    s = priv->cur_dirname;
    priv->cur_dirname = donna_node_get_location (node);
    g_free (s);
    g_object_unref (node);
}

static void
active_location_changed (GObject *object, GParamSpec *spec, DonnaApp *app)
{
    update_cur_dirname (app);
    refresh_window_title (app);
}

static inline void
set_active_list (DonnaApp *app, DonnaTreeView *list)
{
    DonnaAppPrivate *priv = app->priv;

    if (priv->sid_active_location > 0)
        g_signal_handler_disconnect (priv->active_list, priv->sid_active_location);
    priv->sid_active_location = g_signal_connect (list, "notify::location",
            (GCallback) active_location_changed, app);

    switch_statuses_source (app, ST_SCE_ACTIVE, (DonnaStatusProvider *) list);

    priv->active_list = g_object_ref (list);
    update_cur_dirname (app);
    refresh_window_title (app);
    g_object_notify ((GObject *) app, "active-list");
}


static void
window_set_focus_cb (GtkWindow *window, GtkWidget *widget, DonnaApp *app)
{
    DonnaAppPrivate *priv = app->priv;

    if (DONNA_IS_TREE_VIEW (widget))
    {
        priv->focused_tree = (DonnaTreeView *) widget;
        switch_statuses_source (app, ST_SCE_FOCUSED, (DonnaStatusProvider *) widget);

        if (!donna_tree_view_is_tree ((DonnaTreeView *) widget)
                && (DonnaTreeView *) widget != priv->active_list)
        {
            gboolean skip;
            if (donna_config_get_boolean (priv->config, NULL, &skip,
                        "tree_views/%s/not_active_list",
                        donna_tree_view_get_name ((DonnaTreeView *) widget))
                    && skip)
                return;

            set_active_list (app, (DonnaTreeView *) widget);
        }
    }
}

static gboolean
just_focused_expired (DonnaApp *app)
{
    app->priv->just_focused = FALSE;
    return G_SOURCE_REMOVE;
}

static gboolean
focus_in_event_cb (GtkWidget *w, GdkEvent *event, DonnaApp *app)
{
    app->priv->just_focused = TRUE;
    g_timeout_add (42, (GSourceFunc) just_focused_expired, app);
    if (app->priv->floating_window)
        gtk_widget_destroy (app->priv->floating_window);
    return FALSE;
}

static inline enum rc
create_gui (DonnaApp *app, gchar *layout, gboolean maximized)
{
    GError              *err = NULL;
    DonnaAppPrivate     *priv = app->priv;
    GtkWindow           *window;
    GtkWidget           *active_list_widget = NULL;
    GtkWidget           *w;
    gchar               *active_list_name;
    gchar               *s;
    gchar               *ss;
    gchar               *def;
    gchar               *areas;
    gint                 width;
    gint                 height;

    window = (GtkWindow *) gtk_window_new (GTK_WINDOW_TOPLEVEL);
    priv->window = g_object_ref (window);

    g_signal_connect (window, "focus-in-event",
            (GCallback) focus_in_event_cb, app);
    g_signal_connect (window, "delete-event",
            (GCallback) window_delete_event_cb, app);

    if (!layout
            && !donna_config_get_string (priv->config, &err, &layout, "donna/layout"))
    {
        w = gtk_message_dialog_new (NULL,
                GTK_DIALOG_MODAL,
                GTK_MESSAGE_ERROR,
                GTK_BUTTONS_CLOSE,
                "Unable to load interface: no layout set (%s)",
                (err) ? err->message : "no error message");
        g_clear_error (&err);
        gtk_dialog_run ((GtkDialog *) w);
        gtk_widget_destroy (w);
        return RC_LAYOUT_MISSING;
    }

    if (!donna_config_get_string (priv->config, &err, &ss, "layouts/%s", layout))
    {
        w = gtk_message_dialog_new (NULL,
                GTK_DIALOG_MODAL,
                GTK_MESSAGE_ERROR,
                GTK_BUTTONS_CLOSE,
                "Unable to load interface: layout '%s' not defined (%s)",
                layout, (err) ? err->message : "no error message");
        g_clear_error (&err);
        gtk_dialog_run ((GtkDialog *) w);
        gtk_widget_destroy (w);
        g_free (layout);
        return RC_LAYOUT_MISSING;
    }
    g_free (layout);
    s = ss;

    if (!donna_config_get_string (priv->config, NULL, &active_list_name,
                "donna/active_list"))
        active_list_name = NULL;

    def = s;
    w = load_widget (app, &def, &active_list_name, &active_list_widget);
    g_free (s);
    if (!w)
    {
        w = gtk_message_dialog_new (NULL,
                GTK_DIALOG_MODAL,
                GTK_MESSAGE_ERROR,
                GTK_BUTTONS_CLOSE,
                "Unable to load interface: invalid layout");
        gtk_dialog_run ((GtkDialog *) w);
        gtk_widget_destroy (w);
        return RC_LAYOUT_INVALID;
    }
    gtk_container_add ((GtkContainer *) window, w);

    g_free (active_list_name);
    if (!priv->active_list)
    {
        if (!active_list_widget)
        {
            w = gtk_message_dialog_new (NULL,
                    GTK_DIALOG_MODAL,
                    GTK_MESSAGE_ERROR,
                    GTK_BUTTONS_CLOSE,
                    "Unable to load interface: no active-list found");
            gtk_message_dialog_format_secondary_text ((GtkMessageDialog *) w,
                    "You need at least one treeview in mode List to be defined in your layout.");
            gtk_dialog_run ((GtkDialog *) w);
            gtk_widget_destroy (w);
            return RC_ACTIVE_LIST_MISSING;
        }
    }
    priv->active_list = NULL;
    set_active_list (app, (DonnaTreeView *) active_list_widget);
    priv->focused_tree = (DonnaTreeView *) active_list_widget;

    /* status bar */
    if (donna_config_get_string (priv->config, NULL, &areas, "statusbar/areas"))
    {
        GtkWidget *box;

        priv->sb = g_object_new (DONNA_TYPE_STATUS_BAR, NULL);
        s = areas;
        for (;;)
        {
            struct status *status;
            struct provider provider;
            gboolean expand;
            gchar *sce;
            gchar *e;

            e = strchr (s, ',');
            if (e)
                *e = '\0';

            if (!donna_config_get_string (priv->config, NULL, &sce,
                        "statusbar/%s/source", s))
            {
                g_warning ("Unable to load statusbar area '%s', no source specified",
                        s);
                goto next;
            }

            status = g_new0 (struct status, 1);
            status->name = g_strdup (s);
            status->providers = g_array_new (FALSE, FALSE, sizeof (struct provider));
            if (streq (sce, ":active"))
            {
                status->source = ST_SCE_ACTIVE;
                provider.sp = (DonnaStatusProvider *) priv->active_list;
            }
            else if (streq (sce, ":focused"))
            {
                status->source = ST_SCE_FOCUSED;
                provider.sp = (DonnaStatusProvider *) priv->focused_tree;
            }
            else if (streq (sce, ":task"))
            {
                status->source = ST_SCE_TASK;
                provider.sp = (DonnaStatusProvider *) priv->task_manager;
            }
#if 0
            else if (streq (s, "donna"))
                status->source = ST_SCE_DONNA;
#endif
            else
            {
                g_free (status->name);
                g_free (status);
                g_warning ("Unable to load statusbar area '%s', invalid source: '%s'",
                        s, sce);
                g_free (sce);
                goto next;
            }
            g_free (sce);

            provider.id = donna_status_provider_create_status (provider.sp,
                    status->name, &err);
            if (provider.id == 0)
            {
                g_free (status->name);
                g_free (status);
                g_warning ("Unable to load statusbar area '%s', failed to init provider: %s",
                        s, err->message);
                g_clear_error (&err);
                goto next;
            }
            g_array_append_val (status->providers, provider);

            if (!donna_config_get_int (priv->config, NULL, &width,
                        "statusbar/%s/width", status->name))
                width = -1;
            if (!donna_config_get_boolean (priv->config, NULL, &expand,
                        "statusbar/%s/expand", status->name))
                expand = TRUE;
            donna_status_bar_add_area (priv->sb, status->name,
                    provider.sp, provider.id, width, expand, &err);

            priv->statuses = g_slist_prepend (priv->statuses, status);
next:
            if (!e)
                break;
            else
                s = e + 1;
        }
        g_free (areas);

        w = g_object_ref (gtk_bin_get_child ((GtkBin *) window));
        gtk_container_remove ((GtkContainer *) window, w);
        box = gtk_box_new (GTK_ORIENTATION_VERTICAL, 0);
        gtk_container_add ((GtkContainer *) window, box);
        gtk_box_pack_start ((GtkBox *) box, w, TRUE, TRUE, 0);
        g_object_unref (w);
        gtk_box_pack_end ((GtkBox *) box, (GtkWidget *) priv->sb, FALSE, FALSE, 0);
    }

    /* sizing */
    if (!donna_config_get_int (priv->config, NULL, &width, "donna/width"))
        width = -1;
    if (!donna_config_get_int (priv->config, NULL, &height, "donna/height"))
        height = -1;
    gtk_window_set_default_size (window, width, height);

    if (maximized
            || (donna_config_get_boolean (priv->config, NULL, &maximized, "donna/maximized")
                && maximized))
        gtk_window_maximize (window);

    refresh_window_title (app);
    gtk_widget_show_all ((GtkWidget *) window);
    gtk_widget_grab_focus (active_list_widget);
    g_signal_connect (window, "set-focus",
            (GCallback) window_set_focus_cb, app);

    return RC_OK;
}

static GSList *
load_arrangements (DonnaConfig *config, const gchar *sce)
{
    GSList      *list   = NULL;
    GPtrArray   *arr    = NULL;
    guint        i;

    if (!donna_config_list_options (config, &arr,
                DONNA_CONFIG_OPTION_TYPE_NUMBERED, sce))
        return NULL;

    for (i = 0; i < arr->len; ++i)
    {
        struct argmt *argmt;
        gchar *mask;

        if (!donna_config_get_string (config, NULL, &mask,
                    "%s/%s/mask", sce, arr->pdata[i]))
        {
            g_warning ("Arrangement '%s/%s' has no mask set, skipping",
                    sce, (gchar *) arr->pdata[i]);
            continue;
        }
        argmt = g_new0 (struct argmt, 1);
        argmt->name  = g_strdup (arr->pdata[i]);
        argmt->pspec = g_pattern_spec_new (mask);
        list = g_slist_prepend (list, argmt);
        g_free (mask);
    }
    list = g_slist_reverse (list);
    g_ptr_array_free (arr, TRUE);

    return list;
}

static void
load_css (const gchar *dir, gboolean is_main)
{
    GtkCssProvider *css_provider;
    gchar buf[255], *b = buf;
    gchar *file = NULL;

    if (snprintf (buf, 255, "%s%s/donnatella.css",
                dir, (is_main) ? "" : "/donnatella") >= 255)
        b = g_strdup_printf ("%s%s/donnatella.css",
                dir, (is_main) ? "" : "/donnatella");

    if (!g_get_filename_charsets (NULL))
        file = g_filename_from_utf8 (b, -1, NULL, NULL, NULL);

    if (!g_file_test ((file) ? file : b, G_FILE_TEST_IS_REGULAR))
    {
        if (b != buf)
            g_free (b);
        g_free (file);
        return;
    }

    DONNA_DEBUG (APP, NULL,
            g_debug3 ("Load '%s'", b));
    css_provider = gtk_css_provider_new ();
    gtk_css_provider_load_from_path (css_provider, (file) ? file : b, NULL);
    gtk_style_context_add_provider_for_screen (gdk_screen_get_default (),
            (GtkStyleProvider *) css_provider,
            (is_main) ? GTK_STYLE_PROVIDER_PRIORITY_USER
            : GTK_STYLE_PROVIDER_PRIORITY_APPLICATION);

    if (b != buf)
        g_free (b);
    g_free (file);
}

enum cfgdir
{
    CFGDIR_EXIST,
    CFGDIR_CREATED,
    CFGDIR_CREATION_FAILED
};

static enum cfgdir
create_and_init_config_dir (DonnaConfig *config,
                            const gchar *sce,
                            const gchar *dst,
                            gchar       *data)
{
    GError *err = NULL;
    GPtrArray *arr = NULL;
    gchar buf[255], *b = buf;
    gchar *file = NULL;
    gint cat;

    if (!g_get_filename_charsets (NULL))
        file = g_filename_from_utf8 (dst, -1, NULL, NULL, NULL);
    if (g_file_test ((file) ? file : dst, G_FILE_TEST_IS_DIR))
    {
        g_free (file);
        return CFGDIR_EXIST;
    }

    DONNA_DEBUG (APP, NULL,
            g_debug3 ("Create config dir '%s'", dst));
    if (g_mkdir_with_parents ((file) ? file : dst, 0700) == -1)
    {
        gint _errno = errno;
        g_warning ("Failed to create config dir '%s': %s",
                dst,
                g_strerror (_errno));
        g_free (file);
        g_free (data);
        return CFGDIR_CREATION_FAILED;
    }
    g_free (file);
    file = NULL;

    /* we're gonna load the config, make some runtime changes, then save it */
    donna_config_load_config (config, data); /* frees data */

    /* box (highlight if unpatched) the homedir */
    cat = 0;
    if (!donna_config_has_category (config, NULL, "visuals"))
        donna_config_new_category (config, NULL, NULL, "visuals");
    else if (donna_config_list_options (config, &arr,
                DONNA_CONFIG_OPTION_TYPE_NUMBERED, "visuals"))
    {
        cat = (gint) g_ascii_strtoll (arr->pdata[arr->len - 1], NULL, 10);
        g_ptr_array_unref (arr);
    }
    if (donna_config_new_category (config, NULL, NULL, "visuals/%d", ++cat))
    {
        donna_config_new_string_take (config, NULL, NULL, NULL,
                g_strconcat ("fs:", g_get_home_dir (), NULL),
                "visuals/%d/node", cat);
#ifdef GTK_IS_JJK
        donna_config_new_string (config, NULL, NULL, NULL, "box-yellow",
                "visuals/%d/box", cat);
#else
        donna_config_new_string (config, NULL, NULL, NULL, "hl-blue",
                "visuals/%d/highlight", cat);
#endif
    }

#ifndef GTK_IS_JJK
    /* change key/click mode to unpatched version */
    donna_config_set_string (config, NULL, "donna_unpatched",
            "defaults/trees/key_mode");
    donna_config_set_string (config, NULL, "tree_unpatched",
            "defaults/trees/click_mode");
    donna_config_set_string (config, NULL, "donna_unpatched",
            "defaults/lists/key_mode");
    donna_config_set_string (config, NULL, "list_unpatched",
            "defaults/lists/click_mode");
#endif

    /* save config */
    data = donna_config_export_config (config);

    if (snprintf (buf, 255, "%s/donnatella.conf-ref", dst) >= 255)
        b = g_strdup_printf ("%s/donnatella.conf-ref", dst);

    if (!g_get_filename_charsets (NULL))
        file = g_filename_from_utf8 (b, -1, NULL, NULL, NULL);

    DONNA_DEBUG (APP, NULL,
            g_debug3 ("Writing '%s'", b));
    if (!g_file_set_contents ((file) ? file : b, data, -1, &err))
    {
        g_warning ("Failed to import configuration to '%s': %s",
                dst, err->message);
        g_clear_error (&err);
        if (b != buf)
            g_free (b);
        g_free (file);
        g_free (data);
        return CFGDIR_CREATION_FAILED;
    }

    /* remove the "-ref" bit */
    b[strlen (b) - 4] = '\0';
    if (file)
        file[strlen (file) - 4] = '\0';

    DONNA_DEBUG (APP, NULL,
            g_debug3 ("Writing '%s'", b));
    if (!g_file_set_contents ((file) ? file : b, data, -1, &err))
    {
        g_warning ("Failed to write new configuration to '%s': %s",
                dst, err->message);
        g_clear_error (&err);
        if (b != buf)
            g_free (b);
        g_free (file);
        g_free (data);
        return CFGDIR_CREATION_FAILED;
    }
    if (b != buf)
        g_free (b);
    g_free (file);
    g_free (data);

    /* copy default marks.conf */
    DONNA_DEBUG (APP, NULL,
            g_debug3 ("Copy default 'marks.conf'"));

    if (snprintf (buf, 255, "%s/donnatella/marks.conf", sce) >= 255)
        b = g_strdup_printf ("%s/donnatella/marks.conf", sce);

    if (!g_get_filename_charsets (NULL))
        file = g_filename_from_utf8 (b, -1, NULL, NULL, NULL);

    if (!g_file_get_contents ((file) ? file : b, &data, NULL, &err))
    {
        g_warning ("Failed to read '%s': %s", b, err->message);
        g_clear_error (&err);
    }
    else
    {
        g_free (file);
        file = NULL;
        if (b != buf)
        {
            g_free (b);
            b = buf;
        }

        if (snprintf (buf, 255, "%s/marks.conf", dst) >= 255)
            b = g_strdup_printf ("%s/marks.conf", dst);

        if (!g_get_filename_charsets (NULL))
            file = g_filename_from_utf8 (b, -1, NULL, NULL, NULL);

        if (!g_file_set_contents ((file) ? file : b, data, -1, &err))
        {
            g_warning ("Failed to write '%s': %s", b, err->message);
            g_clear_error (&err);
        }
        g_free (data);
    }

    if (b != buf)
        g_free (b);
    g_free (file);
    return CFGDIR_CREATED;
}

static gboolean
copy_and_load_conf (DonnaConfig *config, const gchar *sce, const gchar *dst)
{
    GError *err = NULL;
    gchar buf[255], *b = buf;
    gchar *file = NULL;
    gchar *data;
    enum cfgdir cfgdir;

    if (snprintf (buf, 255, "%s/donnatella/donnatella.conf", sce) >= 255)
        b = g_strdup_printf ("%s/donnatella/donnatella.conf", sce);

    if (!g_get_filename_charsets (NULL))
        file = g_filename_from_utf8 (b, -1, NULL, NULL, NULL);

    DONNA_DEBUG (APP, NULL,
            g_debug3 ("Reading '%s'", b));
    if (!g_file_get_contents ((file) ? file : b, &data, NULL, &err))
    {
        g_warning ("Failed to copy configuration from '%s': %s",
                sce, err->message);
        g_clear_error (&err);
        if (b != buf)
            g_free (b);
        g_free (file);
        return FALSE;
    }
    if (b != buf)
    {
        g_free (b);
        b = buf;
    }
    g_free (file);
    file = NULL;

    /* if dst doesn't exist, we create it and do some extra init stuff */
    cfgdir = create_and_init_config_dir (config, sce, dst, data);
    if (cfgdir == CFGDIR_CREATED)
        return TRUE;
    else if (cfgdir == CFGDIR_CREATION_FAILED)
        return FALSE;
    /* CFGDIR_EXIST */

    if (snprintf (buf, 255, "%s/donnatella.conf-ref", dst) >= 255)
        b = g_strdup_printf ("%s/donnatella.conf-ref", dst);

    if (!g_get_filename_charsets (NULL))
        file = g_filename_from_utf8 (b, -1, NULL, NULL, NULL);

    DONNA_DEBUG (APP, NULL,
            g_debug3 ("Writing '%s'", b));
    if (!g_file_set_contents ((file) ? file : b, data, -1, &err))
    {
        g_warning ("Failed to import configuration to '%s': %s",
                dst, err->message);
        g_clear_error (&err);
        if (b != buf)
            g_free (b);
        g_free (file);
        g_free (data);
        return FALSE;
    }

    /* remove the "-ref" bit */
    b[strlen (b) - 4] = '\0';
    if (file)
        file[strlen (file) - 4] = '\0';

    DONNA_DEBUG (APP, NULL,
            g_debug3 ("Writing '%s'", b));
    if (!g_file_set_contents ((file) ? file : b, data, -1, &err))
    {
        g_warning ("Failed to write new configuration to '%s': %s",
                dst, err->message);
        g_clear_error (&err);
        if (b != buf)
            g_free (b);
        g_free (file);
        g_free (data);
        return FALSE;
    }
    if (b != buf)
        g_free (b);
    g_free (file);

    /* takes ownership/will free data */
    donna_config_load_config (config, data);
    return TRUE;
}

/* returns TRUE if file existed (even if loading failed), else FALSE */
static gboolean
load_conf (DonnaConfig *config, const gchar *dir)
{
    GError *err = NULL;
    gchar buf[255], *b = buf;
    gchar *file = NULL;
    gchar *data;
    gboolean file_exists = FALSE;

    if (snprintf (buf, 255, "%s/donnatella.conf", dir) >= 255)
        b = g_strdup_printf ("%s/donnatella.conf", dir);

    if (!g_get_filename_charsets (NULL))
        file = g_filename_from_utf8 (b, -1, NULL, NULL, NULL);

    DONNA_DEBUG (APP, NULL,
            g_debug3 ("Try loading '%s'", b));
    if (g_file_get_contents ((file) ? file : b, &data, NULL, &err))
    {
        file_exists = TRUE;
        donna_config_load_config (config, data);
    }
    else
    {
        file_exists = !g_error_matches (err, G_FILE_ERROR, G_FILE_ERROR_NOENT);
        if (file_exists)
            g_warning ("Unable to load configuration from '%s': %s",
                    b, err->message);
        g_clear_error (&err);
    }

    if (b != buf)
        g_free (b);
    g_free (file);
    return file_exists;
}

static inline void
init_app (DonnaApp *app)
{
    DonnaAppPrivate *priv = app->priv;
    GPtrArray *arr = NULL;
    const gchar *main_dir;
    const gchar * const *extra_dirs;
    const gchar * const *dir;
    const gchar * const *first;

    /* get config dirs */
    main_dir = priv->config_dir;
    extra_dirs = g_get_system_config_dirs ();

    /* load config: load user one. If there's none, copy the system one over,
     * and keep another copy as "reference" for future merging */
    if (!load_conf (priv->config, main_dir))
    {
        for (dir = extra_dirs; *dir; ++dir)
            if (copy_and_load_conf (priv->config, *dir, main_dir))
                break;
    }

    /* CSS - At same priority, the last one loaded takes precedence, so we need
     * to load system ones first (in reverse order), then the user one */
    first = extra_dirs;
    for (dir = extra_dirs; *dir; ++dir)
        ;
    if (dir != first)
    {
        for (--dir; dir != first; --dir)
            load_css (*dir, FALSE);
        load_css (*dir, FALSE);
    }
    load_css (main_dir, TRUE);

    /* compile patterns of arrangements' masks */
    priv->arrangements = load_arrangements (priv->config, "arrangements");

    if (donna_config_list_options (priv->config, &arr,
                DONNA_CONFIG_OPTION_TYPE_NUMBERED, "visuals"))
    {
        guint i;

        for (i = 0; i < arr->len; ++i)
        {
            struct visuals *visuals;
            gchar *s;

            if (!donna_config_get_string (priv->config, NULL, &s,
                        "visuals/%s/node",
                        arr->pdata[i]))
                continue;

            visuals = g_slice_new0 (struct visuals);
            donna_config_get_string (priv->config, NULL, &visuals->name,
                    "visuals/%s/name", arr->pdata[i]);
            donna_config_get_string (priv->config, NULL, &visuals->icon,
                    "visuals/%s/icon", arr->pdata[i]);
            donna_config_get_string (priv->config, NULL, &visuals->box,
                    "visuals/%s/box", arr->pdata[i]);
            donna_config_get_string (priv->config, NULL, &visuals->highlight,
                    "visuals/%s/highlight", arr->pdata[i]);
            g_hash_table_insert (priv->visuals, s, visuals);
        }
        g_ptr_array_unref (arr);
    }
}

static gboolean
prepare_app (DonnaApp *app, GError **error)
{
    DonnaConfig *config = app->priv->config;
    DonnaConfigItemExtraList    it_lst[NB_COL_TYPES + 1];
    DonnaConfigItemExtraListInt it_int[NB_COL_TYPES + 1];
    gint i;

    it_lst[0].value = "line-number";
    it_lst[0].label = "Line Numbers";
    for (i = 0; i < NB_COL_TYPES; ++i)
    {
        it_lst[i + 1].value = app->priv->column_types[i].name;
        it_lst[i + 1].label = app->priv->column_types[i].desc;
    }
    if (G_UNLIKELY (!donna_config_add_extra (config,
                    DONNA_CONFIG_EXTRA_TYPE_LIST, "ct", "Column Type",
                    i + 1, it_lst, error)))
        return FALSE;

    i = 0;
    it_int[i].value     = TITLE_DOMAIN_LOCATION;
    it_int[i].in_file   = "loc";
    it_int[i].label     = "Location";
    ++i;
    it_int[i].value     = TITLE_DOMAIN_FULL_LOCATION;
    it_int[i].in_file   = "full";
    it_int[i].label     = "Full Location (i.e. domain included)";
    ++i;
    it_int[i].value     = TITLE_DOMAIN_CUSTOM;
    it_int[i].in_file   = "custom";
    it_int[i].label     = "Custom string";
    ++i;
    if (G_UNLIKELY (!donna_config_add_extra (config,
                    DONNA_CONFIG_EXTRA_TYPE_LIST_INT, "title-domain",
                    "Type of title for a domain",
                    i, it_int, error)))
        return FALSE;

    i = 0;
    it_int[i].value     = DONNA_ENABLED_TYPE_ENABLED;
    it_int[i].in_file   = "enabled";
    it_int[i].label     = "Enabled";
    ++i;
    it_int[i].value     = DONNA_ENABLED_TYPE_DISABLED;
    it_int[i].in_file   = "disabled";
    it_int[i].label     = "Disabled";
    ++i;
    it_int[i].value     = DONNA_ENABLED_TYPE_COMBINE;
    it_int[i].in_file   = "combine";
    it_int[i].label     = "Combine";
    ++i;
    it_int[i].value     = DONNA_ENABLED_TYPE_IGNORE;
    it_int[i].in_file   = "ignore";
    it_int[i].label     = "Ignore";
    ++i;
    if (G_UNLIKELY (!donna_config_add_extra (config,
                    DONNA_CONFIG_EXTRA_TYPE_LIST_INT, "enabled",
                    "Enabled state",
                    i, it_int, error)))
        return FALSE;

    /* have treeview register its extras */
    if (G_UNLIKELY (!_donna_tree_view_register_extras (config, error)))
        return FALSE;

    /* have context register its extras */
    if (G_UNLIKELY (!_donna_context_register_extras (config, error)))
        return FALSE;

    /* "preload" mark & register so they can add their own extras/commands. We
     * could use a "prepare" function so they only do that without having to
     * load the providers, but since for the commands the providers need to be
     * there, and they'll probably be used often, let's do this (instead of
     * having to ref/unref on each command call) */
    g_object_unref (donna_app_get_provider (app, "register"));
    g_object_unref (donna_app_get_provider (app, "mark"));

    return TRUE;
}

struct cmdline_opt
{
    guint loglevel;
};

static gboolean
cmdline_cb (const gchar         *option,
            const gchar         *value,
            struct cmdline_opt  *data,
            GError             **error)
{
    if (streq (option, "-v") || streq (option, "--verbose"))
    {
        switch (data->loglevel)
        {
            case G_LOG_LEVEL_WARNING:
                data->loglevel = G_LOG_LEVEL_MESSAGE;
                break;
            case G_LOG_LEVEL_MESSAGE:
                data->loglevel = G_LOG_LEVEL_INFO;
                break;
            case G_LOG_LEVEL_INFO:
                data->loglevel = G_LOG_LEVEL_DEBUG;
                break;
            case G_LOG_LEVEL_DEBUG:
                data->loglevel = DONNA_LOG_LEVEL_DEBUG2;
                break;
            case DONNA_LOG_LEVEL_DEBUG2:
                data->loglevel = DONNA_LOG_LEVEL_DEBUG3;
                break;
            case DONNA_LOG_LEVEL_DEBUG3:
                data->loglevel = DONNA_LOG_LEVEL_DEBUG4;
                break;
        }
        return TRUE;
    }
    else if (streq (option, "-q") || streq (option, "--quiet"))
    {
        data->loglevel = G_LOG_LEVEL_ERROR;
        return TRUE;
    }
#ifdef DONNA_DEBUG_ENABLED
    else if (streq (option, "-d") || streq (option, "--debug"))
    {
        if (value && !donna_debug_set_valid (g_strdup (value), error))
            return FALSE;
        /* make sure the loglevel is at least debug */
        if (data->loglevel < G_LOG_LEVEL_DEBUG)
            data->loglevel = G_LOG_LEVEL_DEBUG;
        return TRUE;
    }
#endif

    g_set_error (error, DONNA_APP_ERROR, DONNA_APP_ERROR_OTHER,
            "Cannot parse unknown option '%s'", option);
    return FALSE;
}

static gboolean
parse_cmdline (DonnaApp      *app,
               gchar        **layout,
               gboolean      *maximized,
               int           *argc,
               char         **argv[],
               GError       **error)
{
    DonnaAppPrivate *priv = app->priv;
    struct cmdline_opt data = { G_LOG_LEVEL_WARNING, };
    gchar *config_dir = NULL;
    gchar *log_level = NULL;
    gboolean version = FALSE;
    GOptionContext *context;
    GOptionEntry entries[] =
    {
        { "config-dir", 'c', 0, G_OPTION_ARG_STRING, &config_dir,
            "Use DIR as configuration directory", "DIR" },
        { "log-level",  'L', 0, G_OPTION_ARG_STRING, &log_level,
            "Set LEVEL as the minimum log level to show", "LEVEL" },
        { "verbose",    'v', G_OPTION_FLAG_NO_ARG, G_OPTION_ARG_CALLBACK, cmdline_cb,
            "Increase verbosity of log; Repeat multiple times as needed.", NULL },
        { "quiet",      'q', G_OPTION_FLAG_NO_ARG, G_OPTION_ARG_CALLBACK, cmdline_cb,
            "Quiet mode (Same as --log-level=error)", NULL },
        { "layout",     'y', 0, G_OPTION_ARG_STRING, layout,
            "Use LAYOUT as layout (instead of option donna/layout)", "LAYOUT" },
        { "maximized",  'M', 0, G_OPTION_ARG_NONE, maximized,
            "Start with main window maximized", NULL },
#ifdef DONNA_DEBUG_ENABLED
        { "debug",      'd', G_OPTION_FLAG_OPTIONAL_ARG, G_OPTION_ARG_CALLBACK, cmdline_cb,
            "Define \"filters\" for the debug log messages", "FILTERS" },
#endif
        { "version",    'V', 0, G_OPTION_ARG_NONE, &version,
            "Show version and exit", NULL },
        { NULL }
    };
    GOptionGroup *group;

    context = g_option_context_new ("- your file manager");
    group = g_option_group_new ("donna", "Donnatella", "Donnatella options",
            &data, NULL);
    g_option_group_add_entries (group, entries);
    //g_option_group_set_translation_domain (group, "domain");
    g_option_context_set_main_group (context, group);
    g_option_context_add_group (context, gtk_get_option_group (TRUE));
    if (!g_option_context_parse (context, argc, argv, error))
        return FALSE;

    if (version)
    {
        puts (  "donnatella v" PACKAGE_VERSION
#ifdef GTK_IS_JJK
                " [GTK_IS_JJK]"
#endif
                "\n"
                "Copyright (C) 2014 Olivier Brunel - http://jjacky.com/donnatella\n"
                "License GPLv3+: GNU GPL version 3 or later <http://gnu.org/licenses/gpl.html>\n"
                "This is free software: you are free to change and redistribute it.\n"
                "There is NO WARRANTY, to the extent permitted by law."
                );
        exit (RC_OK);
    }

    /* set up config dir */
    if (!config_dir)
        priv->config_dir = g_strconcat (g_get_user_config_dir (), "/donnatella", NULL);
    else
    {
        char *s;
        s = realpath (config_dir, NULL);
        if (!s)
        {
            gint _errno = errno;
            g_set_error (error, DONNA_APP_ERROR, DONNA_APP_ERROR_OTHER,
                    "Failed to get realpath for config-dir '%s': %s",
                    config_dir, g_strerror (_errno));
            g_free (config_dir);
            return FALSE;
        }
        priv->config_dir = g_strdup (s);
        free (s);
        g_free (config_dir);
    }

    /* log level (default/init to G_LOG_LEVEL_WARNING) */
    if (log_level)
    {
        if (streq (log_level, "debug4"))
            show_log = DONNA_LOG_LEVEL_DEBUG4;
        else if (streq (log_level, "debug3"))
            show_log = DONNA_LOG_LEVEL_DEBUG3;
        else if (streq (log_level, "debug2"))
            show_log = DONNA_LOG_LEVEL_DEBUG2;
        else if (streq (log_level, "debug"))
            show_log = G_LOG_LEVEL_DEBUG;
        else if (streq (log_level, "info"))
            show_log = G_LOG_LEVEL_INFO;
        else if (streq (log_level, "message"))
            show_log = G_LOG_LEVEL_MESSAGE;
        else if (streq (log_level, "warning"))
            show_log = G_LOG_LEVEL_WARNING;
        else if (streq (log_level, "critical"))
            show_log = G_LOG_LEVEL_CRITICAL;
        else if (streq (log_level, "error"))
            show_log = G_LOG_LEVEL_ERROR;
        else
        {
            g_set_error (error, DONNA_APP_ERROR, DONNA_APP_ERROR_OTHER,
                    "Invalid minimum log level '%s': Must be one of "
                    "'debug4', 'debug3', 'debug2', 'debug', 'info', 'message' "
                    "'warning', 'critical' or 'error'",
                    log_level);
            g_free (log_level);
            return FALSE;
        }

        g_free (log_level);
    }
    else
        show_log = data.loglevel;

    return TRUE;
}

/**
 * donna_app_run:
 * @app: The #DonnaApp
 * @argc: argc
 * @argv: argv
 *
 * Runs donnatella
 *
 * Returns: Return code
 */
gint
donna_app_run (DonnaApp       *app,
               gint            argc,
               gchar          *argv[])
{
    GError *err = NULL;
    DonnaAppPrivate *priv;
    enum rc rc;
    gchar *layout = NULL;
    gboolean maximized = FALSE;

    g_return_if_fail (DONNA_IS_APP (app));
    priv = app->priv;

    g_main_context_acquire (g_main_context_default ());

    if (!parse_cmdline (app, &layout, &maximized, &argc, &argv, &err))
    {
        fputs (err->message, stderr);
        fputc ('\n', stderr);
        g_clear_error (&err);
        g_free (layout);
        return RC_PARSE_CMDLINE_FAILED;
    }

    /* load config extras, registers commands, etc */
    if (G_UNLIKELY (!prepare_app (app, &err)))
    {
        GtkWidget *w = gtk_message_dialog_new (NULL,
                GTK_DIALOG_MODAL,
                GTK_MESSAGE_ERROR,
                GTK_BUTTONS_CLOSE,
                "Failed to prepare application: %s",
                (err) ? err->message : "no error message");
        g_clear_error (&err);
        gtk_dialog_run ((GtkDialog *) w);
        gtk_widget_destroy (w);
        g_free (layout);
        return RC_PREPARE_FAILED;
    }

    /* load config, css arrangements, required providers, etc */
    init_app (app);

    /* create & show the main window */
    rc = create_gui (app, layout, maximized);
    if (G_UNLIKELY (rc != 0))
        return rc;

    donna_app_emit_event (app, "start", FALSE, NULL, NULL, NULL, NULL);

    /* in the off-chance something before already led to closing the app (could
     * happen e.g. if something had started its own mainloop (e.g. in event
     * "start" there was a command that does, like ask_text) and the user then
     * closed the main window */
    if (G_LIKELY (gtk_widget_get_realized ((GtkWidget *) priv->window)))
        gtk_main ();

    priv->exiting = TRUE;
    donna_app_emit_event (app, "exit", FALSE, NULL, NULL, NULL, NULL);

    /* let's make sure all (internal) tasks (e.g. triggered from event "exit")
     * are done before we die */
    g_thread_pool_stop_unused_threads ();
    while (g_thread_pool_get_num_threads (priv->pool) > 0)
    {
        if (gtk_events_pending ())
            gtk_main_iteration ();
        g_thread_pool_stop_unused_threads ();
    }
    gtk_widget_destroy ((GtkWidget *) priv->window);
    g_main_context_release (g_main_context_default ());

#ifdef DONNA_DEBUG_ENABLED
    donna_debug_reset_valid ();
#endif
    g_object_unref (app);
    return rc;
}
