/*
 * donnatella - Copyright (C) 2014 Olivier Brunel
 *
 * treeview.c
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

#include <gtk/gtk.h>
#include <string.h>             /* strchr(), strncmp() */
#include "treeview.h"
#include "common.h"
#include "provider.h"
#include "node.h"
#include "task.h"
#include "statusprovider.h"
#include "macros.h"
#include "renderer.h"
#include "columntype-name.h"    /* DONNA_TYPE_COLUMN_TYPE_NAME */
#include "cellrenderertext.h"
#include "colorfilter.h"
#include "filter-private.h"
#include "provider-internal.h"
#include "contextmenu.h"
#include "util.h"
#include "closures.h"
#include "debug.h"

#ifdef GTK_IS_JJK
#define GTK_TREEVIEW_REMOVE_COLUMN_FIXED
#if GTK_CHECK_VERSION (3, 14, 0)
#define JJK_RUBBER_SIGNAL
#else
#define JJK_RUBBER_START
#endif
#endif

#if GTK_CHECK_VERSION (3, 12, 0)
#define GTK_TREEVIEW_REMOVE_COLUMN_FIXED
#endif

/**
 * SECTION:treeview
 * @Short_description: The TreeView componement for trees and lists
 *
 * The treeview is the main GUI component of donna; It can be used as a tree, or
 * as a list. Either way, it comes with advanced features you might not yet be
 * familiar with (but might soon get addicted to).
 *
 * It should however be noted that quite a few of those were not possible to do
 * using the treeview widget from GTK+ As such, they will only be available if
 * you're using a patched version of GTK+3 which adds support for all of those,
 * while remaining 100% compatible with GTK+3.
 *
 * # Tree or List # {#tree-or-list}
 *
 * The first thing to do for a treeview, is set whether it should be a list or a
 * tree. This is done via boolean option
 * `tree_views/&lt;TREEVIEW-NAME&gt;/is_tree`.
 *
 * For example, to have treeview "foobar" be a tree, you'd set:
 * <programlisting>
 * [tree_views/foobar]
 * is_tree=true
 * </programlisting>
 *
 * This option is obviously required, and if missing it will default to `false`.
 *
 * Some of the other treeview options apply to both modes, while each mode has
 * its own specific set of options.
 *
 *
 * # Tree and List options # {#tree-and-list-options}
 *
 * The following treeview options apply to both trees and lists, using the
 * treeview option paths, as described in #option-paths.
 *
 * - `show_hidden` &lpar;boolean&rpar; : Whether or not to show "hidden"
 *   files/folders, i.e.  beginning with a dot
 * - `node_types` &lpar;integer:node-type&rpar; : Which types of nodes to show:
 *   "items" &lpar;e.g.  files&rpar;, "containers" &lpar;e.g. folders&rpar;, or
 *   "all" for both
 * - `sort_groups` &lpar;integer:sg&rpar; : How to sort containers: "first" to
 *   have them listed first when sorting ascendingly, and last when sorting
 *   descendingly; "first-always" to have them always listed first; or "mixed"
 *   to have them mixed with items
 * - `select_highlight` &lpar;integer:highlight&rpar; : How to draw rows that
 *   are selected; Note that this requires a patched GTK to work.  "fullrow" for
 *   a full row highlight &lpar;GTK default&rpar;; "column" for an highlight on
 *   the column &lpar;cell&rpar; only; "underline" for a simple underline of the
 *   full row; or "column-underline" to combine both the cell highlight and the
 *   full row underline.
 * - `key_mode` &lpar;string&rpar; : Default key mode for the treeview. See
 *   #key-modes for more.
 * - `click_mode` &lpar;string&rpar; : Click mode for the treeview.  See
 *   #click-modes for more.
 * - `default_save_location` &lpar;integer:save-location&rpar; : Default
 *   location to save options to (See commands tv_set_option() and
 *   tv_column_set_option())
 *
 *
 * # Know (how) your GUI (works) # {#how-gui-works}
 *
 * As with most graphical file manager, donna's main window is composed of a few
 * GUI elements, at the center of it a list, showing you the content of a
 * folder. It usually comes along with a tree, where the folder
 * structure/hierarchy of the file system is represented.
 *
 * In donna, both list and tree are done by the same component, a treeview.
 * Obviously, though they have similarities, lists and trees are quite different
 * in their behaviors. Option `is_tree` will determine which it is, and then
 * list and tree share common options, as well as having their own.
 *
 * Either way, a treeview is made up of columns, columns being defined similarly
 * first by their type - commonly referred to as columntype - then a mix of
 * common (to all columns/column-types) and and type-specific options.
 *
 * Although tree views have options, those are only for "general"
 * features/behaviors of the treeview, but you don't configure columns directly.
 *
 * Instead, donna uses arrangements. An arrangement defines which columns are
 * features on the treeview, the main and secondary sort orders, column options
 * as well as color filters.
 *
 *
 * # Arranging your columns # {#arrangements}
 *
 * Tree views, both as list and tree, uses arrangements. Arrangements are
 * defined like regular treeview options, with an extra twist.
 *
 * First of all, what is found in an arrangement definition? It can contain the
 * following:
 * - columns; Option `columns` is a coma-separated list of columns to load in
 *   the treeview. Option `main_column` allows to set the main column, i.e.
 *   used for the selection highlight effect (and expanders on trees).
 * - sort order; Options `sort_column` and `sort_order` define the (main) sort
 *   order
 * - secondary sort order; Options `second_sort_column`, `second_sort_order` and
 *   `second_sort_sticky` define the secondary sort order. If sticky, it means
 *   when the main sort order is set to the current second sort order, the later
 *   remains set and will be "restored" back when the main sort order is set
 *   elsewhere.
 * - column options; Category `column_options` can contain column options, which
 *   will be first in the option paths for column options.
 * - color filter; Category `color_filters` can contain color filters.
 *
 * Each of those elements will be looked for and loaded into the current
 * arrangement. Unless all elements have been loaded, loading continues for
 * other arrangements down the option path.
 *
 * Note that color filters are special, in that even when loaded into the
 * current arrangement, they will not be considered loaded when their option
 * type was set to combine, to keep loading color filters.
 *
 * Trees simply load an arrangement on start, using the typical option path for
 * treeview options:
 * - `tree_views/&lt;TREEVIEW-NAME&gt;/arrangement`
 * - `defaults/&lt;TREEVIEW-MODE&gt;s/arrangement`
 *
 * For lists however, there's a little more to it. First off, donna allows
 * "dynamic arrangements" which must contain an option "mask" a #DonnaPattern
 * that, when matched against the list's current location, will be loaded. See
 * donna_pattern_new() for more.
 *
 * Those dynamic arrangements will be looked for using typical option path:
 * - `tree_views/&lt;TREEVIEW-NAME&gt;/arrangements` (Note that final 's')
 * - `defaults/lists/arrangements` (Again, plural)
 *
 * Each of those can have an option type set to either :
 * - `disabled` to stop loading dynamic arrangements. Loading will continue for
 *   "fixed" arrangements however.
 * - `enabled` to load dynamic arrangements. In this case, no other dynamic
 *   arrangements will be loaded (though "fixed" one are still processed).
 * - `combine` to load arrangements, but continue loading dynamic arrangements
 *   if possible
 * - `ignore` to simply ignore them (as if they didn't exist) and look for the
 *   next ones
 *
 * If the current arrangement isn't complete (i.e. not all elements have been
 * loaded yet) then the regular option paths are traversed as well (the
 * so-called "fixed" arrangements).
 *
 * Note that, should the current arrangement have no columns set in the end, it
 * would try and default to one column "name" in an attempt not to remain empty.
 *
 * Dynamic arrangements will allow you to have e.g. different columns based on
 * the current domain, or have different columns in certain locations; You can
 * also simply change column options based on the current location, e.g. have
 * one default format used to show the modified date, and another one used only
 * in a (few) selected location(s); Or simply set some different sort orders for
 * some locations.
 *
 *
 * # Defining your columns # {#define-columns}
 *
 * The arrangement will define the list of columns to be featured in the
 * treeview.  Columns are defined first by their type, from option
 * `defaults/&lt;TREEVIEW-MODE&gt;s/columns/&lt;COLUMN-NAME&gt;/type`
 *
 * Then, a mix of general and type-specific column options are available. The
 * option paths for those options are:
 * - `&lt;ARRANGEMENT&gt;/columns_options/&lt;COLUMN-NAME&gt;`
 * - `tree_views/&lt;TREEVIEW-NAME&gt;/columns/&lt;COLUMN-NAME&gt;`
 * - `defaults/&lt;TREEVIEW-MODE&gt;s/columns/&lt;COLUMN-NAME&gt;`
 * - `defaults/&lt;DEFAULT&gt;`
 *
 * Obviously the first one is optional, since current arrangement might not
 * feature columns options. The later is as well, as certain options might have
 * a default path (e.g. for date format, `defaults/date/format`) while others
 * may not.
 *
 * Generic options, common to all column types, are:
 * - `width` (integer) : Width of the column
 * - `title` (string) : Title of the column (shown in column header)
 * - `desc_first` (boolean) : When sorting on this column, whether to default to
 *   descending (true) or ascending (false).  Default value depends on the
 *   columntype
 * - `refresh_properties` (integer:col-rp) : How the treeview will handle
 *   properties whose value hasn't been loaded yet, and needs a refresh. This
 *   will most likely be the case for #custom-properties where executing an
 *   external process is required to get the value.
 *   By default, `visible`, only properties for visible rows will be refreshed.
 *   So when e.g. scrolling down, the columns will be empty at first, while
 *   properties are refreshed and then columns updated.
 *   With `preload` visible rows will be handled first, but then a background
 *   task will be started to refresh properties on all rows, thus "preloading"
 *   the values.
 *   Finally, using `on_demand` no property will be loaded automatically,
 *   instead a refresh icon will be shown on the column. A manual click on said
 *   icon is required to trigger the refresh. This can be useful for properties
 *   that are slow to refreshn, e.g.  calculating the hash (MD5, SHA1, etc) of a
 *   file, especially on large files. When used, sorting by this column will
 *   sort all rows with a value, groupping all rows without one afterwards.
 *   Trying to filter via this column will work as usual when the value is set,
 *   and simply not match when it doesn't. See command tv_column_refresh_nodes()
 *   to trigger the refresh of multiple/all rows.
 *
 * Other options depend on their column types, each having its own options (or
 * none). See #DonnaColumnType.description
 *
 *
 * # Treeview: Lists # {#list}
 *
 * A list is where content of the current location will be listed. Lists usually
 * have multiple columns (name, size, etc), and you can easilly filter/sort by
 * the column(s) of your choice. Lists support multiple selection, i.e. more
 * than one row can be selected at a time.
 *
 * Lists are "flat" (i.e. no expanders), but have column headers visible.
 *
 * See #tree-and-list-options for common options to trees & lists. In addition,
 * the following list-specific options, using treeview's #option-paths as well,
 * are available :
 *
 * - `focusing_click` (boolean) : When donna isn't focused, any left-click will
 *   be treated as a focusing click, and therefore otherwise ignored. That way
 *   you can safely click to activate/focus donna without worrying that said
 *   click might do something you didn't want (e.g. lose selection, focus row,
 *   etc).  With this option set to true (the default), it will do the same when
 *   the focus wasn't on the list itself, even though donna was focused (e.g.
 *   another treeview was focused).
 * - `goto_item_set` (integer:tree-set) : Defines what happens when an item is
 *   given as new location. Obviously, the current location will be set to its
 *   parent, but you can then use "scroll" to scroll to the row of item itself;
 *   "focus" to focus the row; or "cursor" to set the cursor to that row
 *   (Setting the cursor means unselect all, select, focus and scroll to the
 *   row).  You can also combine those, even though combining anything with
 *   "cursor" makes little sense. The default is "focus,scroll"
 * - `history_max` (integer) : Defines the maximum number of items stored in the
 *   history; Defaults to 100.
 * - `vf_items_only` (boolean) : When true any visual filter will only be
 *   applied to items (e.g. files), i.e. containers (e.g. folders) will remain
 *   visible regardless of the VF. Defaults to false
 *
 *
 * # Treeview: Trees # {#tree}
 *
 * A tree shows one or more user-specifed rows, and lists its children in a
 * hierarchy. Trees will usually have only one column (name), so no column
 * header will be shown. It can only have one row selected at a time.
 *
 * Trees can be synchronized with another treeview, a list. In such a case the
 * tree's selection might be automatically adjusted to follow the list's current
 * location, according to option `sync_mode` (see below).
 *
 * In most file managers, the root of the tree is the root of the file system.
 * Sometimes another root can be available, your home directory. In donna, you
 * can have as many roots as you want, in the order you want, each pointing to
 * any node of your choice.
 *
 * Trees also support #tree-visuals: those are row-specific properties that you
 * can define manually on rows of your choice. donna also supports node visuals,
 * where visuals can be automatically applied based on the node behind the row.
 * See #node-visuals for more, as well as option
 * `node_visuals` below.
 *
 * See #tree-and-list-options for common options to trees & lists. In addition,
 * the following tree-specific options, using treeview's #option-paths as well,
 * are available :
 *
 * - `is_minitree` (boolean): Defines whether the tree is a mini-tree or not.
 *   Minitrees are awesome, see #minitree for why. Defaults to false.
 * - `sync_with` (string): Name of the list to be synchronized with. This means
 *   both that on change of selection/current location, the list will be set to
 *   the same location, as well as have the tree adjust its selection/current
 *   location based on the list's. You can use ":active" to syncronized with the
 *   active list (auto-adjusting when the active list changes). Defaults to
 *   nothing.
 * - `sync_mode` (integer:sync): Defines the level of synchonization; Defaults
 *   to "full". Can be one of "none", "nodes", "known-children", "children" or
 *   "full". With "none" the tree won't react on list's change of location.
 *   With "nodes" it will only sync if a row for the location already exists on
 *   tree and is accessible (i.e. not a children with an ancestor collapsed).
 *   With "known-children" a row must exists on tree, but expansion might happen
 *   if needed. With "children" there might be first-time expansion
 *   loading/adding children to the tree. And with "full" if no parent row to be
 *   expanded is found, a new root will be automatically added.
 * - `sync_scroll` (boolean): Whether to scroll to the new selection/current
 *   location or not; Defaults to true.
 * - `auto_focus_sync` (boolean): When true, on a change of location the focus
 *   will automatically be sent to the list. This means after e.g. click on a
 *   row to change the list's location, the focus will automatically be sent to
 *   the list. Defaults to true.
 * - `node_visuals` (integer:visuals): Defines which visuals can be loaded from
 *   the node and applied on tree.
 *
 *
 * # More than a tree: a Mini-Tree # {#minitree}
 *
 * A mini-tree is basically your regular tree (also referred to a maxi-tree)
 * only with a simple twist: only show you what you need. Or, more specifically,
 * only show rows for location you've actually visited.
 *
 * A simple example: you start on the root of your filesystem, so only one
 * single row on the tree: <filename>/</filename>
 * Now you go into <filename>/etc</filename> and the tree synchonizes, thus
 * expanding its only row.  And now on tree appear all the children of said
 * row, the <filename>bin</filename>, <filename>boot</filename>,
 * <filename>dev</filename>, <filename>home</filename>,
 * <filename>lib</filename>, <filename>proc</filename>,
 * <filename>run</filename>, <filename>var</filename> & all the others. And of
 * course, the one you actually are interested in, <filename>etc</filename>.
 *
 * Then you go into <filename>nginx</filename>, and again: lots of other rows
 * are added to your tree, the <filename>audit</filename>,
 * <filename>cron.d</filename>, <filename>dbus-1</filename>,
 * <filename>modprobe.d</filename> & other <filename>udev</filename>. Your tree
 * is now already quite crowed, just because you went into
 * <filename>/etc/nginx</filename> Now imagine you also go into a few other
 * places - e.g. <filename>/etc/php/conf.d</filename>,
 * <filename>/var/log/nginx</filename>, <filename>/tmp</filename> &
 * <filename>/home/user/projects/www</filename> - can you imagine what your tree
 * would look like?
 *
 * It's a mess that has you scrolling every so often while you're only working
 * on a few places. Now with a minitree, only the places you actually visited
 * would have been added to the tree. So upon going into
 * <filename>/etc</filename>, only one single row would have been added:
 * <filename>etc</filename>.
 * Same thing going down into <filename>nginx</filename>.
 *
 * And going to <filename>/var/log/nginx</filename> would have resulted in no
 * more than 3 rows added to the tree: <filename>var</filename>,
 * <filename>log</filename>, and <filename>nginx</filename>. A much more compact
 * tree, with only the minimum, what you actually need to see, what you'll use,
 * nothing else.
 *
 * In such a scenario, parent rows such as <filename>/etc</filename> would be in
 * a state of partial expansion, i.e. there are children loaded, but not all of
 * them. Of course you can easilly have them maxi expanded, i.e. load all
 * missing children. You can also remove any row from your minitree, when you
 * don't need it anymore.
 *
 * It's a simple idea, but one that will change your perception of the tree,
 * making it much more useable, and useful; One you might get addicted to once
 * you're used to it. And as an added bonus, because donna only needs to add
 * rows for the locations visited, it can skip the scanning of each expanded
 * location (to list all children to add), so navigation is that much faster.
 *
 * By default (it can be customized via #css) expanders in donna can be of one
 * of three colors:
 * - red: when the row has never been expanded, and no children are currently
 *   loaded on tree.
 * - orange: (minitree only): row is partially expanded, i.e. not all children
 *   are loaded on tree. Note that in fact all children might be there, since
 *   donna doesn't check after adding children that there are more left.
 * - black: row is maxi expanded, i.e. all children are loaded on tree.
 *
 * Note that those also apply on maxitree, thus you can tell is expanding a row
 * might result in scanning the location for children to add on tree, or not.
 * (Might, because the list of children might be obtained by other means, e.g.
 * from the list.)
 * Also note that the color of the expander only refer to its expand state, not
 * whether or not the row is actually expanded or collapsed.
 * And finally, if option `show_hidden` is false, the
 * tree completely ignores dotted items, and thus will consider rows to be
 * maxi-expanded if all non-dotted children are loaded.
 *
 *
 * # Tree Visuals # {#tree-visuals}
 *
 * Tree visuals are row-specific properties that you can define manually on rows
 * of your choice. Note donna also supports #node-visuals.
 *
 * There are 5 tree visuals you can use, though one isn't really a "visual"
 * per-se:
 *
 * - name: Define a custom name to be used instead of the name property of the
 *   node. This is meant to be used (mostly) on tree roots. For example, you can
 *   want to work on a project, adding
 *   <filename>/home/user/projects/foobar</filename> as new root on your tree,
 *   yet have it show as "Project foobar" instead of just "foobar", or actually
 *   show the full path, or even "~/projects/foobar".
 * - icon: Define a custom icon to be used intead of the one from the property
 *   icon of the node. This can be either the full path to an image file, or
 *   name of an icon to load from the theme.
 * - box: Define the name of the class to be used for the "boxed branch" effect.
 *   This will have the node and all its children under a set background color.
 *   It is meant as a visual indicator, to very easily/quickly identifies when
 *   you are under a certain location. Boxed branch can be nested of course, in
 *   which case all colors will be visible on each row in the expander area. Box
 *   effect will also remain visible in that expander area even when selected
 *   with full row highlight effect (see option
 *   `select_highlight`).
 *   Note that this is only available with a patched GTK.
 * - highlight: Define the name of the class to be used for the highlight
 *   effect. This will have the row's name under a special set of colors,
 *   providing an highlight effect. This will extend a bit more to the right, so
 *   that it remains visible even when selected.
 * - click_mode: Define the name of the click mode to use for that row. While
 *   not a visual it works exactly the same, and is therefore found under Tree
 *   Visuals. Note that it is not, however, part of the visuals available as
 *   #node-visuals. Click modes define how to process clicks, see #click-modes
 *   for more.
 *
 * Note that tree visuals take precedence over node visuals, if both are
 * specified.
 *
 *
 * # Key Modes, or how keys are handled # {#key-modes}
 *
 * donna works in a different way than most file managers, in that it uses
 * vim-inspired key modes to handle key presses. While in most file managers if
 * you start typing something this is processed as some sort of "find as you
 * type" search, things are quite different in donna (and you certainly
 * shouldn't start typing things like that!).
 *
 * Indeed, each key can be assign an action of sorts. Almost every key can be
 * set, with exceptions like numbers (0-9) or modifiers (Ctrl, Shift, Alt...)
 * Obviously when you press keys, you want some kind of trigger to happen as
 * response. The most simple way being that the action happens when the key is
 * pressed; This is the type "direct" As is usual in donna, the action will be a
 * full location, parsed contextually and triggered; See #treeview-context for
 * more.
 *
 * The reason numbers aren't usable is that they're automatically used to handle
 * the "modifier," a number you can enter before pressing the key to trigger the
 * action.  Commonly, this is used to have the action be triggered a certain
 * number of times, e.g. to specify by how many rows to move the focus. However,
 * the way multiplier works in donna isn't by repeating the action as many times
 * as specified via the multiplier (for multiple reasons, where it wouldn't work
 * (as expected)).
 * Instead, the multiplier will be available as variable `%m` on the
 * trigger/full location. It does mean that if not used, the multiplier is
 * simply ignored, however commands such as tv_goto_line() support an argument
 * especially made for the multipier.
 *
 * In addition to "direct" keys, you can use type "spec" A "spec" key is very
 * much like a direct one except that after pressing it nothing happens, as it
 * waits for another key to be pressed, the so-called "spec."
 * This spec can be restricted (e.g. only numbers, and/or letters, etc) and will
 * be available on trigger as variable `%k` (think key).
 *
 * This allows to quickly specify an argument to e.g. the triggered command. In
 * addition, it is possible to have keys use a spec motion, which means that the
 * key pressed should be a "motion."
 *
 * A motion key is simply any key (direct or spec) that was flagged as such, to
 * indicate that it will move the focus on treeview. The idea is that two
 * actions will then take place:
 *
 * - first, the motion key is triggered, so the focus can move.
 * - then, the original (spec) key is triggered, but it will now have the
 *   originally focused row available through variables `%r` and `%n` (instead
 *   of the focused row).
 *
 * This pretty much allows to do the same kind of operation that are possible
 * via mouse (where it is possible to click on a row other than the focused
 * row), but using the keyboard only.
 *
 * Note that donna doesn't ensure that a key marked as motion actually is/moves
 * the focus.
 *
 * Any spec key with a spec motion will also support to be pressed again, to
 * indicate that no motion should occur and instead the action can be triggered.
 * This is common to what one can find in vim, so it is possible to do similar
 * things: press 'y' twice to yank/copy the focused row, or press 'y' then a
 * motion to yank the whole range of the rows; E.g. "y2j" would yank the focused
 * row and the two below, ending with the focus moved two row below, assuming
 * 'j' is a motion key to move focus down obvisouly. As you can see, it is
 * possible to use multipliers on a spec motion. It is also possible that the
 * motion key is of type "spec" thus requiring yet another key to be pressed.
 *
 * Another type of key supported is "combine" which works just the like spec, in
 * that it requires another key to be pressed just the same (expect you can't
 * use a spec motion), and it will be available as variable `%c`
 * However, a combine is meant to be used as an optional argument, one that can
 * be used on more than one key. For example, when dealing with registers you
 * could have a few keys set, to yank/paste/etc to/from a register. But to avoid
 * having to define all keys as spec, and thus losing the ability to make those
 * keys spec (e.g. with a spec motion), you would use a combine.
 *
 * The combine key will set the name of combine it is (which is simply a string
 * used to identify the combine), and for other (direct/spec) keys to
 * accept/work with such combine, the same name must have been defined there as
 * accepted/compatible combine.
 *
 * Lastly, a type "alias" is available to define a key as alias of another one,
 * to have multiple keys behave exactly the same. E.g. set key 'j' an alias of
 * key 'Down' (or the other way around).
 *
 * While much less powerfull than what you find in vim, it clearly takes
 * inspiration in how things can be done in vim, and should allow to do quite a
 * lot from the keyboard only. See #default-key-modes for what is available by
 * default.
 *
 * Now that we've seen how keys are handled by donna, let's have a look at how
 * all of this work/is configured.
 *
 *
 * ## How keys are defined ## {#key-modes-config}
 *
 * First of all, treeviews have an option `key_mode` which defines the default
 * key mode for the treeview. This is simply the name of a category under
 * `key_modes`.
 *
 * Much like with #click-modes, a key mode can have an option `fallback` which
 * is simply the name of another key mode that will be used as fallback if the
 * option isn't found.
 * This is useful to preserve a key mode as it is, and only make a small set of
 * changes. It will be refered to as &lt;FALLBACK&gt;.
 *
 * (Note that when looking for options in a fallback key mode, its own option
 * `fallback` (if any) will be ignored.)
 *
 * When a key is pressed, donna determines which key it is, using
 * gdk_keyval_name(), referred to as &lt;KEY&gt;
 *
 * Then it looks for category `key_modes/&lt;KEY-MODE&gt;/key_&lt;KEY&gt;`
 * If not found, then `key_modes/&lt;FALLBACK&gt;/key_&lt;KEY&gt;` is tried.
 * If not found, the key is not defined.
 *
 * When a category is found, a few options are available, defining the key:
 *
 * - `type` &lpar;integer:key&rpar;: The type of key, one of "disabled" (as if
 *   the key wasn't defined, except without fallback lookup), "combine",
 *   "direct", "spec" or "alias" Defaults to "direct"
 *
 * For "alias":
 *
 * - `key` &lpar;string&rpar;: Name of the key (definition) to use
 *
 * For "direct" and "spec":
 *
 * - `trigger` &lpar;string&rpar; : Full location to trigger
 * - `is_motion` &lpar;optional; boolean&rpar; : Whether this is a motion or not
*   (Defaults to false)
 * - `combine` &lpar;optional; string&rpar; : Name of the combine that can be
 *   used with this key
 *
 * For "spec":
 *
 * - `spec` &lpar;integer:spec&rpar; : Type of keys allowed as spec; One or more
 *   of "lower", "upper", "digits", "extra" or "custom" (see option
 *   `custom_chars`) Can also be "motion" Defaults to "lower,upper"
 * - `custom_chars` &lpar;optional; string&rpar; : The list of allowed
 *   characters when using "custom" in `spec`
 *
 * For "combine":
 *
 * - `combine` &lpar;string&rpar; : Name of the combine
 * - `spec` &lpar;integer:spec&rpar; : Same as `spec` above, except that
 *   "motion" cannot be used.
 *
 *
 * # Click Modes, or how clicks are processed # {#click-modes}
 *
 * Commonly in applications, while you might be able to assign keyboard
 * shortcuts to map certains keys to operations of your choosing (see #key-modes
 * to see how donna gives you full control over your keys), things are different
 * when it comes to mouse clicks.
 *
 * In donna, however, you also get full control for clicks. Whenever you click
 * somewhere, a string identifying the type of click will be computed, and be
 * used as an option name to get the full location to trigger.
 *
 * First of all, which click was this? We're talking here about something like
 * "left_click" or "ctrl_middle_double_click"
 *
 * First off, there might have been modifiers held. Supported are Control
 * ("ctrl") and Shift ("shift"), checked and added in that order (i.e. if both
 * were held, prefix "ctrl_shift_" will be used).
 * Then the button: "left", "middle" or "right" is added.
 * Except for column headers, there might then be a "specifier" for the type of
 * click: "double" or "slow".
 * After that comes the string "click"
 *
 * A double click is just that, using the GTK settings "gtk-double-click-time"
 * as expected. A slow click is a "slow double click" that happened after said
 * delay (else it'd be a double click) and before that delay expired again (then
 * it's simple a new click).
 *
 * It should be noted that donna will wait for the delay to expire before
 * processing a click, thus allowing to hav a double click triggered without a
 * click being triggered first. This is, however, not true for simple left click
 * (i.e. for middle/right clicks, or left click with Control and/or Shift held)
 * because otherwise the application feels a bit slow/unresponsive, and
 * expectations are usually that the first click is processed before the double
 * click is (e.g. left_click sets focus, left_double_click acts on focused row).
 *
 * That's for the type of click. Certain areas, when clicked, will result in a
 * prefix be used. Those are "colheader_" for column headers, "blankrow_" when
 * clicking on the blank space after the last row, "blankcol_" when clicking on
 * the blank space to the right of the last column, "blank_" when clicking on
 * blank space within a column (on a row), and "expander_" when clicking on an
 * expander. There can, of course, only be one of those used at a time (they are
 * evaluated in the specified order, so "blankcol_" takes precedence over
 * "blankrow_").
 *
 * This gives an option name, whose value must be the full location to trigger.
 * This full location will first be parsed contextually of course.
 *
 * Now where should this option be? This is where the notion of "click modes"
 * will becomes apparent: treeview option "click_mode" defines the name of the
 * click mode used in tree view (Note that it can be changed used command
 * tv_set_key_mode()).
 *
 * A click mode is defined under `click_modes/&lt;CLICK-MODE&gt;`
 * Before describing the full option path used, it should be noted that a click
 * mode can have an option `fallback` set, name of another click mode that will
 * be used as fallback if the option isn't found.
 * This is useful to preserve a click mode as it is, and only make a small set
 * of changes. It will be refered to as &lt;FALLBACK&gt; in the option path
 * below.
 *
 * (Note that when looking for options in a fallback click mode, its own option
 * `fallback` (if any) will be ignored.)
 *
 * So, the full option path goes as follow (using &lt;CLICK&gt; to refer to the
 * option name as described above):
 *
 * If the click was on a selected row:
 *
 * - `click_modes/&lt;CLICK-MODE&gt;/columns/&lt;COLUMN&gt;/selected/&lt;CLICK&gt;`
 *   if the click was on a column
 * - `click_modes/&lt;FALLBACK&gt;/columns/&lt;COLUMN&gt;/selected/&lt;CLICK&gt;`
 *   if the click was on a column
 * - `click_modes/&lt;CLICK-MODE&gt;/selected/&lt;CLICK&gt;`
 * - `click_modes/&lt;FALLBACK&gt;/selected/&lt;CLICK&gt;`
 *
 * If nothing was found, or the click wasn't on a selected row:
 *
 * - `click_modes/&lt;CLICK-MODE&gt;/columns/&lt;COLUMN&gt;/&lt;CLICK&gt;`
 *   if the click was on a column
 * - `click_modes/&lt;FALLBACK&gt;/columns/&lt;COLUMN&gt;/&lt;CLICK&gt;`
 *   if the click was on a column
 * - `click_modes/&lt;CLICK-MODE&gt;/&lt;CLICK&gt;`
 * - `click_modes/&lt;FALLBACK&gt;/&lt;CLICK&gt;`
 *
 * If nothing was found, nothing happens. Else the trigger (option value) is
 * contextually parsed and the associated node triggered; Also see
 * #treeview-context
 *
 * As a result, you can control absolutely any & every clicks made. See
 * #default-click-modes for description of how to use donna with default
 * options.
 *
 *
 * # Context variables from treeviews # {#treeview-context}
 *
 * On multiple occasions in donna, you can use a full location, also refered to
 * as trigger, that will be contextually parsed before the corresponding node is
 * triggered. For instance this happens from keys, clicks or context menus.
 *
 * The whole point of the context, is that you can use variables in said
 * trigger, to refer e.g. to the clicked row, or simply know which treeview to
 * use.
 *
 * It is possible for a row and/or a column to be available in the context. For
 * example, on a click the reference row and the column will be those clicked,
 * if applicable.
 * For context menu, it will be what was specified on tv_context_get_nodes()
 *
 * The following variables are available:
 *
 * - `%o`: treeview
 * - `%l`: node of the current location (if any)
 * - `%R`: name of the column (if any)
 * - `%r`: reference row (if any)
 * - `%n`: node of the reference row (if any)
 * - `%f`: node of the focused row (if any)
 * - `%s`: array of selected nodes (might be empty)
 * - `%S`: array of selected nodes if any, else node of the focused row (same as
 *   `%f`)
 * - `%m`: current multiplier (if any)
 * - `%k`: current key pressed &amp; waiting for its specifier (spec) (if any)
 * - `%c`: current combine (if any)
 *
 * See also donna_app_parse_fl() about dereferencing such variables.
 *
 *
 * # Referencing rows (in commands) # {#rowid}
 *
 * When interacting with treeviews via commands, it is required to reference
 * rows. This is done using "row ids" which allow different ways to identify a
 * row:
 *
 * - Using a row; this will be used when commands return a rowid.  This is an
 *   internal reference that will allow to identify one specific row.
 * - Using a node, or its full location; this allows to identify the node
 *   represented by that row. On list, it's obviously enough, but on trees you
 *   can have the same node represented by more than one rows. In such a case,
 *   there's no rule as to which row will be returned.
 * - Using a path; this is a string used to reference one or more rows. You
 *   can simply use a line number, starting at 1.
 *   You can also prefix it with the percent sign, to reference the visible row
 *   at the specified percent. So, "\%23" is the row at 23% in the visible area
 *   of the treeview (i.e. there will never be scrolling needed to reach the
 *   row).
 *   Adding another percent sign at the end will then refer to the specified
 *   percent in the entire tree, not only the visible area. In other words,
 *   "\%100%" is the last row on the treeview, whereas "\%100" is the last
 *   visible row.
 *   Using a colon as prefix allows for special row identifiers, as detailled
 *   below.
 *
 * The following row identifiers can be used in (rowid's) path, prefixed with a
 * colon:
 * - all: special one, to reference all rows
 * - selected: special one, to reference all selected rows
 * - focused: to reference the focused row
 * - prev: to reference the previous row (above focused one)
 * - next: to reference the next row (below focused one)
 * - last: to reference the last row (e.g. same as "\%100%")
 * - up: to reference the parent of the focused row (tree only)
 * - down: to reference the first child of the focused row (tree only)
 * - top: to reference the first visible row (at least 2/3rd of the row must be
 *   visible to be considered)
 * - bottom: to reference the last visible row (again, 2/3rd visibility required)
 * - prev-same-depth: next row at the same level (tree only)
 * - next-same-depth: previous row at the same level (tree only)
 * - item: to reference the first next row to be an item
 * - container: to reference the first next row to be a container
 * - other: to reference the first next row to be of different type (than
 *   focused row)
 *
 * Note that when using ":all" or ":selected" the first row in the list will be
 * the focused row, then all (selected) rows as scanning the treeview to the
 * end, and finally going back to the first (selected) row from the top and back
 * to the focused row. This allows to give you a little control over the order
 * in which rows/nodes are listed, which might matter in certain cases (e.g.
 * when the first (few) names are used as templates, etc).
 * This same principle is used for ":item", ":container" and ":other"
 *
 *
 * # Treeview as status provider # {#treeview-status}
 *
 * You can use a treeview as source of a statusbar area (See #statusbar for
 * more). In that case, you must specify string option `format`, defining the
 * format of what to show in the area. The following variable are available:
 *
 * - `%o` : treeview name
 * - `%l` : full location of current location; a dash (-) if no current location
 *   is set
 * - `%L` : location of current location if in "fs", else full location; a dash
 *   (-) if no current location is set
 * - `%f` : name of focused row
 * - `%F` : current visual filter (if any)
 * - `%K` : current key mode
 * - `%k` : current key status; that is the current combine if any, the current
 *   combine spec if any, the current multiplier if any, the current key pressed
 *   if any (waiting for its spec), and the current multiplier of the spec
 *   motion if any. Can be an empty string.
 * - `%a` : number of all rows (i.e. including hidden ones for lists)
 * - `%v` : number of visible rows
 * - `%h` : number of hidden rows
 * - `%s` : number of selected rows
 * - `%A` : total size of all rows
 * - `%V` : total size of visible rows
 * - `%H` : total size of hidden rows
 * - `%S` : total size of selected rows
 * - `%n` : name of focused row, if any
 * - `%N` : name of selected item if there's only one, string "n items selected"
 *   (with n the number of selected items) if more than one, else nothing
 *
 * Note that `%A`, `%V`, `%H` and `%S` will use the format as specified in
 * option `size_format` (defaulting to that of `defaults/size/format` to
 * format the size, alongside options `digits` and `long_unit` (defaulting to
 * whatever is set under `defaults/size`).
 * You can however specify the format to use, by putting it in between brackets
 * right after the percent sign, e.g. `%{%b}S`
 *
 * `%a`, `%v`, `%h`, `%s` and `%F` also supports an extra (in between brackets),
 * which is itself a string parsed the same way and supporting the same
 * variables (If you need recursion, any backslash or closing braquet must be
 * escaped via backslash).
 *
 * The parsed string will then be shown instead of what the variable usually
 * resolves to. However, there's a twist:
 *
 * - for `%F` it will only be shown when a VF is set (else resolves to
 *   nothing/empty string)
 * - for the others, the string will also be splitted using comma as separator
 *   (if you need to use commas, you then need to use `%,`), so you can specify
 *   up to 3 strings.
 *
 * With only 1 string, it will be shown unless the reference (i.e. the number
 * of items the variable refers to) is zero.
 * With 2 strings, nothing is shown if the reference is zero, the first one is
 * shown if it is one, else the second one is shown.
 * Lastly, with 3 strings, the first one is shown is reference is zero, the
 * second one if it's one, else the last one is shown.
 *
 * `%a` behaves a little differently, in that instead of not showing anything
 * (or the first of the 3 specified strings) when the reference (i.e. number of
 * all rows) is zero, it does so when it is the same as the number of visible
 * rows.
 * This is to make it easy to show e.g. "23 rows" when all are visible, and
 * "23/42 rows" when some are hidden, using: \%{\%v/}a\%a rows
 *
 * Additionally, you can use option `colors` (integer:tree-set-colors) to enable
 * the use of (background/foreground) colors.
 *
 * When set to `keys` color will be based on the current key mode: if string
 * option `key_mode_&lt;KEY-MODE&gt;_foreground` is set, it will be used as
 * foreground color. If not, string option
 * `key_mode_&lt;KEY-MODE&gt;_foreground-rgba` is tried, which can be a string
 * as per gdk_rgba_parse()
 * Similarly named options for the background color are also used in the same
 * way.
 *
 * When set to `vf` options `foreground` or `foreground-rgba` (and similarly for
 * background) are then used when a VF is applied.
 *
 * Finally, the same variables are supported in option `format_tooltip`, used
 * for the tooltip of the statusbar area (no color support there).
 */

enum
{
    PROP_0,

    PROP_APP,
    PROP_LOCATION,

    NB_PROPS
};

enum
{
    SIGNAL_SELECT_ARRANGEMENT,
    NB_SIGNALS
};

enum
{
    TREE_COL_NODE = 0,
    TREE_COL_EXPAND_STATE,
    /* TRUE when expanded, back to FALSE only when manually collapsed, as
     * opposed to GTK default including collapsing a parent. This will allow to
     * preserve expansion when collapsing a parent */
    TREE_COL_EXPAND_FLAG,
    TREE_COL_ROW_CLASS,
    TREE_COL_NAME,
    TREE_COL_ICON,
    TREE_COL_BOX,
    TREE_COL_HIGHLIGHT,
    TREE_COL_CLICK_MODE,
    /* which of name, icon, box and/or highlight are locals (else from node).
     * Also includes click_mode even though it's not a visual/can't come from
     * node */
    TREE_COL_VISUALS,
    TREE_NB_COLS
};

enum
{
    LIST_COL_NODE = 0,
    LIST_NB_COLS
};

/* this column exists in both modes, and must have the same id */
#define TREE_VIEW_COL_NODE     0

enum tree_expand
{
    TREE_EXPAND_UNKNOWN = 0,    /* not known if node has children */
    TREE_EXPAND_NONE,           /* node doesn't have children */
    TREE_EXPAND_NEVER,          /* never expanded, children unknown */
    TREE_EXPAND_WIP,            /* we have a running task getting children */
    TREE_EXPAND_PARTIAL,        /* minitree: only some children are listed */
    TREE_EXPAND_MAXI,           /* (was) expanded, children are there */
};

#define ROW_CLASS_MINITREE          "minitree-unknown"
#define ROW_CLASS_PARTIAL           "minitree-partial"

#define CONTEXT_FLAGS               "olrnfsS"
#define CONTEXT_COLUMN_FLAGS        "R"
#define CONTEXT_KEYS_FLAGS          "kcm"

#define ST_CONTEXT_FLAGS            "olLfFkKaAvVhHsSnN"

#define DATA_PRELOAD_TASK           "donna-preload-props-task"

enum tree_sync
{
    TREE_SYNC_NONE = 0,
    TREE_SYNC_NODES,
    TREE_SYNC_NODES_KNOWN_CHILDREN,
    TREE_SYNC_NODES_CHILDREN,
    TREE_SYNC_FULL
};

enum
{
    RENDERER_TEXT,
    RENDERER_PIXBUF,
    RENDERER_PROGRESS,
    RENDERER_COMBO,
    RENDERER_TOGGLE,
    RENDERER_SPINNER,
    NB_RENDERERS
};

enum sort_container
{
    SORT_CONTAINER_FIRST = 0,
    SORT_CONTAINER_FIRST_ALWAYS,
    SORT_CONTAINER_MIXED
};

enum draw
{
    DRAW_NOTHING = 0,
    DRAW_WAIT,
    DRAW_EMPTY,
    DRAW_NO_VISIBLE
};

enum select_highlight
{
    SELECT_HIGHLIGHT_FULL_ROW = 0,
    SELECT_HIGHLIGHT_COLUMN,
    SELECT_HIGHLIGHT_UNDERLINE,
    SELECT_HIGHLIGHT_COLUMN_UNDERLINE
};

enum
{
    CLICK_REGULAR = 0,
    CLICK_ON_BLANK,
    CLICK_ON_EXPANDER,
    CLICK_ON_COLHEADER
};

enum removal
{
    /* tree: just remove the row -- list: filter out the row */
    RR_NOT_REMOVAL,
    /* node if deleted */
    RR_IS_REMOVAL,
    /* tree-only: just remove the row, but stay MAXI (don't go PARTIAL). This is
     * used when toggling show_hidden */
    RR_NOT_REMOVAL_STAY_MAXI
};

/* colors in statusprovider */
enum st_colors
{
    ST_COLORS_OFF = 0,
    ST_COLORS_KEYS,     /* key_mode based */
    ST_COLORS_VF        /* when there's a VF */
};

enum spec_type
{
    SPEC_NONE        = 0,
    /* a-z */
    SPEC_LOWER      = (1 << 0),
    /* A-Z */
    SPEC_UPPER      = (1 << 1),
    /* 0-9 */
    SPEC_DIGITS     = (1 << 2),
    /* anything translating to a character in SPEC_EXTRA_CHARS (below) */
    SPEC_EXTRA      = (1 << 3),
    /* custom set of characters, set in option custom_chars */
    SPEC_CUSTOM     = (1 << 4),

    /* key of type motion (can obviously not be combined w/ anything else) */
    SPEC_MOTION     = (1 << 9),
};
#define SPEC_EXTRA_CHARS "*+=-[](){}<>'\"|&~@$_"

enum key_type
{
    /* key does nothing */
    KEY_DISABLED = 0,
    /* gets an extra spec (can't be MOTION) for following action */
    KEY_COMBINE,
    /* direct trigger */
    KEY_DIRECT,
    /* key takes a spec */
    KEY_SPEC,
    /* key is "aliased" to another one */
    KEY_ALIAS,
};

enum changed_on
{
    STATUS_CHANGED_ON_KEY_MODE  = (1 << 0),
    STATUS_CHANGED_ON_KEYS      = (1 << 1),
    STATUS_CHANGED_ON_CONTENT   = (1 << 2),
    STATUS_CHANGED_ON_VF        = (1 << 3),
};

/* because changing location for List is a multi-step process */
enum cl
{
    /* we're not changing location */
    CHANGING_LOCATION_NOT = 0,
    /* the get_children() task has been started */
    CHANGING_LOCATION_ASKED,
    /* the timeout was triggered (DRAW_WAIT) */
    CHANGING_LOCATION_SLOW,
    /* we've received nodes from new-child signal (e.g. search results) */
    CHANGING_LOCATION_GOT_CHILD
};

typedef void (*node_children_extra_cb) (DonnaTreeView *tree, GtkTreeIter *iter);

struct node_children_data
{
    DonnaTreeView           *tree;
    GtkTreeIter              iter;
    DonnaNodeType            node_types;
    gboolean                 expand_row;
    gboolean                 scroll_to_current;
    node_children_extra_cb   extra_callback;
};

struct visuals
{
    /* iter of the root, or an invalid iter (stamp==0) and user_data if the
     * number of the root, e.g. same as path_to_string */
    GtkTreeIter  root;
    gchar       *name;
    GIcon       *icon;
    gchar       *box;
    gchar       *highlight;
    /* not a visual, but treated the same */
    gchar       *click_mode;
};

struct col_prop
{
    gchar             *prop;
    GtkTreeViewColumn *column;
};

struct as_col
{
    GtkTreeViewColumn *column;
    GPtrArray *tasks;
    guint nb;
};

struct active_spinners
{
    DonnaNode   *node;
    GArray      *as_cols;   /* struct as_col[] */
};

struct provider_signals
{
    DonnaProvider   *provider;
    guint            nb_nodes;
    gulong           sid_node_updated;
    gulong           sid_node_deleted;
    gulong           sid_node_removed_from;
    gulong           sid_node_children;
    gulong           sid_node_new_child;
};

struct column
{
    /* required when passed as data to handle Ctrl+click on column header */
    DonnaTreeView       *tree;
    gchar               *name;
    GtkTreeViewColumn   *column;
    /* renderers used in columns, indexed as per columntype */
    GPtrArray           *renderers;
    /* label in the header (for title, since we handle it ourself) */
    GtkWidget           *label;
    /* our arrow for secondary sort order */
    GtkWidget           *second_arrow;
    gint                 sort_id;
    DonnaColumnType     *ct;
    gpointer             ct_data;
    /* column option handled by treeview (like title or width) */
    enum rp              refresh_properties;
};

/* when filters use columns not loaded/used in tree */
struct column_filter
{
    gchar           *name;
    enum rp          refresh_properties;
    DonnaColumnType *ct;
    gpointer         ct_data;
};

/* status in statusbar */
struct status
{
    guint            id;
    enum changed_on  changed_on;
    gchar           *fmt;
    /* keep the name, so we can load key_modes_colors options. We don't
     * "preload" them because we don't know which key modes exists, so it's
     * simpler that way */
    gchar           *name;
    /* color options */
    enum st_colors   colors;
    /* size options */
    gint             digits;
    gboolean         long_unit;
};

/* for conv_flag_fn() used in actions/context menus */
struct conv
{
    DonnaTreeView   *tree;
    DonnaRow        *row;
    gchar           *col_name;
    gchar            key_spec;
    guint            key_m;
    /* context menus: selected nodes, if asked by a provider */
    GPtrArray       *selection;
};


struct _DonnaTreeViewPrivate
{
    DonnaApp            *app;
    gulong               option_set_sid;
    gulong               option_deleted_sid;

    /* tree name */
    gchar               *name;

    /* tree store */
    GtkTreeStore        *store;
    /* list of struct column */
    GSList              *columns;
    /* not in list above
     * list: empty column on the right
     * tree: non-visible column used as select-highlight-column when UNDERLINE */
    GtkTreeViewColumn   *blank_column;
    /* list of struct column_filter */
    GSList              *columns_filter;

    /* so we re-use the same renderer for all columns */
    GtkCellRenderer     *renderers[NB_RENDERERS];

    /* main column is the one where the SELECT_HIGHLIGHT_COLUMN effect is
     * applied to. In mode tree it's also the expander one (in list expander is
     * hidden) */
    GtkTreeViewColumn   *main_column;

    /* main/second sort columns */
    GtkTreeViewColumn   *sort_column;
    GtkTreeViewColumn   *second_sort_column;
    /* since it's not part of GtkTreeSortable */
    GtkSortType          second_sort_order;

    /* current arrangement */
    DonnaArrangement    *arrangement;

    /* properties used by our columns */
    GArray              *col_props;

    /* handling of spinners on columns (when setting node properties) */
    GPtrArray           *active_spinners;
    guint                active_spinners_id;
    guint                active_spinners_pulse;

    /* current location */
    DonnaNode           *location;

    /* Tree: iter of current location */
    GtkTreeIter          location_iter;

    /* List: last get_children task, if we need to cancel it. This is also used
     * in callbacks/timeouts, to know if we outta do something or not (i.e. task
     * has been replaced by another one, or cancelled).
     * This is list-only because in tree we don't abort the last get_children
     * when we start a new one, plus aborting one would require a lot more
     * (remove any already added child, reset expand state, etc) */
    DonnaTask           *get_children_task;
    /* List: future location (task get_children running) */
    DonnaNode           *future_location;
    /* List: extra info if the change_location if a move inside our history */
    DonnaHistoryDirection future_history_direction;
    guint                 future_history_nb;
    /* duplicatable task to get_children -- better than get doing a get_children
     * for e.g. search results, to keep the same workdir, etc */
    DonnaTask           *location_task;
    /* which step are we in the changing of location */
    enum cl              cl;

    /* List: history */
    DonnaHistory        *history;

    /* tree: list of iters for roots, in order */
    GSList              *roots;
    /* hashtable of nodes (w/ ref) & their iters on TV:
     * - list: can be NULL (not visible/in model) or an iter
     * - tree: always a GSList of iters (at least one) */
    GHashTable          *hashtable;
    /* list: current visual filter */
    DonnaFilter         *filter;

    /* list: nodes to be added. To avoid being "spammed" with node-new-child
     * signals (e.g. during a search) we only add a few, then add them to this
     * array, which is added to the list every few seconds */
    GPtrArray           *nodes_to_add;
    gint                 nodes_to_add_level;

    /* list of iters to be used by callbacks. Because we use iters in cb's data,
     * we need to ensure they stay valid. We only use iters from the store, and
     * they are persistent. However, the row could be removed, thus the iter
     * wouldn't be valid anymore.
     * To handle this, whenver an iter is used in a cb's data, a pointer is
     * added in this list. When a row is removed, any iter pointing to that row
     * is removed, that way in the cb we can check if the iter is still there or
     * not. If not, it means it's invalid/the row was removed. */
    GSList              *watched_iters;

    /* providers we're connected to */
    GPtrArray           *providers;

    /* list of props on nodes being refreshed (see refresh_node_prop_cb) */
    GMutex               refresh_node_props_mutex;
    GSList              *refresh_node_props;

    /* Tree: list we're synching with */
    DonnaTreeView       *sync_with;
    gulong               sid_sw_location_changed;
    gulong               sid_active_list_changed;
    gulong               sid_tree_view_loaded;

    /* to handle clicks */
    gchar               *click_mode;
    /* info about last event, used to handle single, double & slow-dbl clicks */
    GdkEventButton      *last_event;
    guint                last_event_timeout; /* it was a single-click */
    gboolean             last_event_expired; /* after sgl-clk, could get a slow-dbl */
    /* in case the trigger must happen on button-release instead */
    DonnaClick           on_release_click;
    /* used to make sure the release is within distance of the press */
    gdouble              on_release_x;
    gdouble              on_release_y;
    /* because middle/right click have a delay, and release could happen before
     * the timeout for the click is triggered */
    gboolean             on_release_triggered;
    /* info to handle the keys */
    gchar               *key_mode;          /* current key mode */
    gchar               *key_combine_name;  /* combine that was used */
    guint                key_combine_val;   /* combine key that was pressed */
    gchar                key_combine_spec;  /* the spec from the combine */
    enum spec_type       key_spec_type;     /* spec we're waiting for */
    guint                key_m;             /* key modifier */
    guint                key_val;           /* (main) key pressed */
    guint                key_motion_m;      /* motion modifier */
    guint                key_motion;        /* motion's key */
    /* when a renderer goes edit-mode, we need the editing-started signal to get
     * the editable */
    gulong               renderer_editing_started_sid;
    /* editable is kept so we can make it abort editing when the user clicks
     * away (e.g. blank space, another row, etc) */
    GtkCellEditable     *renderer_editable;
    /* this one is needed to clear/disconnect when editing is done */
    gulong               renderer_editable_remove_widget_sid;

    /* Tree: keys are ful locations, values are GSList of struct visuals. The
     * idea is that the list of loaded when loading for a tree file, so we can
     * load visuals only when adding the nodes (e.g. on expanding).
     * In minitree, we also put them back in there when nodes are removed. */
    GHashTable          *tree_visuals;
    /* Tree: which visuals to load from node */
    DonnaTreeVisual      node_visuals;

    /* statuses for statusbar */
    GArray              *statuses;
    guint                last_status_id;

    /* See donna_tree_view_save_to_config() for more */
    gboolean             saving_config;

    /* "cached" options */

    /* tree + list */
    gboolean                 is_tree;
    DonnaNodeType            node_types;
    gboolean                 show_hidden;
    enum sort_container      sort_groups; /* containers (always) first/mixed */
    enum select_highlight    select_highlight; /* only used if GTK_IS_JJK */
    DonnaColumnOptionSaveLocation default_save_location;
    /* mode Tree */
    gboolean                 is_minitree;
    enum tree_sync           sync_mode;
    gboolean                 sync_scroll;
    gboolean                 auto_focus_sync;
    /* mode List */
    gboolean                 focusing_click;
    DonnaTreeViewSet         goto_item_set;
    gboolean                 vf_items_only;
    /* DonnaColumnType (line number) */
    gboolean                 ln_relative; /* relative number */
    gboolean                 ln_relative_focused; /* relative only when focused */
    /* from current arrangement */
    DonnaSecondSortSticky    second_sort_sticky;

    /* internal flags */

    /* whether to draw "Please wait"/"Location empty" messages */
    guint                    draw_state         : 2;
    /* ignore any & all node-updated signals */
    guint                    refresh_on_hold    : 1;
    /* when filling list, some things can be disabled; e.g. check_statuses()
     * will not be triggered when adding nodes, etc */
    guint                    filling_list       : 1;
    /* tree is switching selection mode (see selection_changed_cb()) */
    guint                    changing_sel_mode  : 1;
};

static GParamSpec * donna_tree_view_props[NB_PROPS] = { NULL, };
static guint        donna_tree_view_signals[NB_SIGNALS] = { 0, };

/* our internal renderers */
enum
{
    INTERNAL_RENDERER_SPINNER = 0,
    INTERNAL_RENDERER_PIXBUF,
    NB_INTERNAL_RENDERERS
};
static GtkCellRenderer *int_renderers[NB_INTERNAL_RENDERERS] = { NULL, };

/* iters only uses stamp & user_data */
#define itereq(i1, i2)      \
    ((i1)->stamp == (i2)->stamp && (i1)->user_data == (i2)->user_data)

#define watch_iter(tree, iter)  \
    tree->priv->watched_iters = g_slist_prepend (tree->priv->watched_iters, iter)
#define remove_watch_iter(tree, iter)   \
    tree->priv->watched_iters = g_slist_remove (tree->priv->watched_iters, iter)

#define set_es(priv, iter, es)                          \
    gtk_tree_store_set ((priv)->store, iter,            \
            TREE_COL_EXPAND_STATE,  es,                 \
            TREE_COL_ROW_CLASS,                         \
            (((es) == TREE_EXPAND_PARTIAL)              \
             ? ROW_CLASS_PARTIAL                        \
             : ((es) == TREE_EXPAND_NONE                \
                 || (es) == TREE_EXPAND_MAXI)           \
             ? NULL : ROW_CLASS_MINITREE),              \
            -1)

/* internal from columntype.c */
DonnaColumnOptionSaveLocation
_donna_column_type_ask_save_location (DonnaApp    *app,
                                      const gchar *col_name,
                                      const gchar *arr_name,
                                      const gchar *tv_name,
                                      gboolean     is_tree,
                                      const gchar *def_cat,
                                      const gchar *option,
                                      guint        from);
gboolean
_donna_context_add_items_for_extra (GString              *str,
                                   DonnaConfig          *config,
                                   const gchar          *name,
                                   DonnaConfigExtraType  type,
                                   const gchar          *prefix,
                                   const gchar           *item,
                                   const gchar          *save_location,
                                   GError              **error);
gboolean
_donna_context_set_item_from_extra (DonnaContextInfo     *info,
                                   DonnaConfig          *config,
                                   const gchar          *name,
                                   DonnaConfigExtraType  type,
                                   gboolean              is_column_option,
                                   const gchar          *option,
                                   const gchar          *item,
                                   gintptr               current,
                                   const gchar          *save_location,
                                   GError              **error);

/* internal for provider-config.c */
gboolean
_donna_config_get_extra_value (DonnaConfigExtra *extra, const gchar *str, gpointer value);

/* internal; used by app.c */
gboolean _donna_tree_view_register_extras (DonnaConfig *config, GError **error);

static inline struct column *
                    get_column_by_column                (DonnaTreeView  *tree,
                                                         GtkTreeViewColumn *column);
static inline struct column *
                    get_column_by_name                  (DonnaTreeView  *tree,
                                                         const gchar    *name);
static inline void load_node_visuals                    (DonnaTreeView  *tree,
                                                         GtkTreeIter    *iter,
                                                         DonnaNode      *node,
                                                         gboolean        allow_refresh);
static void refilter_list                               (DonnaTreeView  *tree);
static inline void set_draw_state                       (DonnaTreeView  *tree,
                                                         enum draw       draw);
static void refresh_draw_state                          (DonnaTreeView  *tree);
static void preload_props_columns                       (DonnaTreeView  *tree);
static inline GtkTreeIter * get_child_iter_for_node     (DonnaTreeView  *tree,
                                                         GtkTreeIter    *parent,
                                                         DonnaNode      *node);
static void add_node_to_list                            (DonnaTreeView  *tree,
                                                         DonnaNode      *node,
                                                         gboolean        checked);
static gboolean add_node_to_tree_filtered               (DonnaTreeView  *tree,
                                                         GtkTreeIter    *parent,
                                                         DonnaNode      *node,
                                                         GtkTreeIter    *row);
static gboolean add_node_to_tree                        (DonnaTreeView  *tree,
                                                         GtkTreeIter    *parent,
                                                         DonnaNode      *node,
                                                         GtkTreeIter    *row);
static void remove_node_from_list                       (DonnaTreeView  *tree,
                                                         DonnaNode      *node,
                                                         GtkTreeIter    *iter);
static gboolean remove_row_from_tree                    (DonnaTreeView  *tree,
                                                         GtkTreeIter    *iter,
                                                         enum removal    removal);
static GtkTreeIter *get_current_root_iter               (DonnaTreeView  *tree);
static GtkTreeIter *get_closest_iter_for_node           (DonnaTreeView  *tree,
                                                         DonnaNode      *node,
                                                         DonnaProvider  *provider,
                                                         const gchar    *location,
                                                         gboolean        skip_current_root,
                                                         gboolean       *is_match);
static GtkTreeIter *get_best_existing_iter_for_node     (DonnaTreeView  *tree,
                                                         DonnaNode      *node,
                                                         gboolean        even_collapsed);
static GtkTreeIter *get_iter_expanding_if_needed        (DonnaTreeView  *tree,
                                                         GtkTreeIter    *iter_root,
                                                         DonnaNode      *node,
                                                         gboolean        only_accessible,
                                                         gboolean        ignore_show_hidden,
                                                         gboolean       *was_match);
static GtkTreeIter *get_best_iter_for_node              (DonnaTreeView  *tree,
                                                         DonnaNode      *node,
                                                         gboolean        add_root_if_needed,
                                                         gboolean        ignore_show_hidden,
                                                         GError        **error);
static GtkTreeIter * get_root_iter                      (DonnaTreeView   *tree,
                                                         GtkTreeIter     *iter);
static gboolean is_row_accessible                       (DonnaTreeView   *tree,
                                                         GtkTreeIter     *iter);
static struct active_spinners * get_as_for_node         (DonnaTreeView   *tree,
                                                         DonnaNode       *node,
                                                         guint           *index,
                                                         gboolean         create);
static void set_children                                (DonnaTreeView *tree,
                                                         GtkTreeIter   *iter,
                                                         DonnaNodeType  node_types,
                                                         GPtrArray     *children,
                                                         gboolean       expand,
                                                         gboolean       refresh);
static gboolean change_location                         (DonnaTreeView  *tree,
                                                         enum cl         cl,
                                                         DonnaNode      *node,
                                                         gpointer        data,
                                                         GError        **error);
static inline void scroll_to_iter                       (DonnaTreeView  *tree,
                                                         GtkTreeIter    *iter);
static gboolean scroll_to_current                       (DonnaTreeView  *tree);
static void check_children_post_expand                  (DonnaTreeView  *tree,
                                                         GtkTreeIter    *iter);
static gboolean maxi_expand_row                         (DonnaTreeView  *tree,
                                                         GtkTreeIter    *iter);
static gboolean maxi_collapse_row                       (DonnaTreeView  *tree,
                                                         GtkTreeIter    *iter);
static inline void resort_tree                          (DonnaTreeView  *tree);
static gboolean select_arrangement_accumulator      (GSignalInvocationHint  *hint,
                                                     GValue                 *return_accu,
                                                     const GValue           *return_handler,
                                                     gpointer                data);
static void check_statuses (DonnaTreeView *tree, enum changed_on changed);
static gboolean tree_conv_flag                          (const gchar      c,
                                                         gchar           *extra,
                                                         DonnaArgType    *type,
                                                         gpointer        *ptr,
                                                         GDestroyNotify  *destroy,
                                                         struct conv     *conv);

static void free_col_prop (struct col_prop *cp);
static void free_provider_signals (struct provider_signals *ps);
static void free_active_spinners (struct active_spinners *as);
static inline void free_arrangement (DonnaArrangement *arr);
static void selection_changed_cb (GtkTreeSelection *selection, DonnaTreeView *tree);

static void     donna_tree_view_destroy             (GtkWidget      *widget);
static gboolean donna_tree_view_button_press_event  (GtkWidget      *widget,
                                                     GdkEventButton *event);
static gboolean donna_tree_view_button_release_event(GtkWidget      *widget,
                                                     GdkEventButton *event);
static gboolean donna_tree_view_key_press_event     (GtkWidget      *widget,
                                                     GdkEventKey    *event);
static gboolean donna_tree_view_focus               (GtkWidget      *widget,
                                                     GtkDirectionType direction);
#ifdef GTK_IS_JJK
static void     donna_tree_view_rubber_banding_active (
                                                     GtkTreeView    *treev);
#endif
static void     donna_tree_view_row_activated       (GtkTreeView    *treev,
                                                     GtkTreePath    *path,
                                                     GtkTreeViewColumn *column);
static gboolean donna_tree_view_test_collapse_row   (GtkTreeView    *treev,
                                                     GtkTreeIter    *iter,
                                                     GtkTreePath    *path);
static gboolean donna_tree_view_test_expand_row     (GtkTreeView    *treev,
                                                     GtkTreeIter    *iter,
                                                     GtkTreePath    *path);
static void     donna_tree_view_row_collapsed       (GtkTreeView    *treev,
                                                     GtkTreeIter    *iter,
                                                     GtkTreePath    *path);
static void     donna_tree_view_row_expanded        (GtkTreeView    *treev,
                                                     GtkTreeIter    *iter,
                                                     GtkTreePath    *path);
static void     donna_tree_view_cursor_changed      (GtkTreeView    *treev);
#ifdef JJK_RUBBER_SIGNAL
static gboolean donna_tree_view_test_rubber_banding (GtkTreeView    *treev,
                                                     gint            button,
                                                     gint            bin_x,
                                                     gint            bin_y);
#endif
static void     donna_tree_view_get_property        (GObject        *object,
                                                     guint           prop_id,
                                                     GValue         *value,
                                                     GParamSpec     *pspec);
static void     donna_tree_view_set_property        (GObject        *object,
                                                     guint           prop_id,
                                                     const GValue   *value,
                                                     GParamSpec     *pspec);
static gboolean donna_tree_view_draw                (GtkWidget      *widget,
                                                     cairo_t        *cr);
static void     donna_tree_view_finalize            (GObject        *object);


/* DonnaStatusProvider */
static guint    status_provider_create_status       (DonnaStatusProvider    *sp,
                                                     gpointer                config,
                                                     GError                **error);
static void     status_provider_free_status         (DonnaStatusProvider    *sp,
                                                     guint                   id);
static const gchar * status_provider_get_renderers  (DonnaStatusProvider    *sp,
                                                     guint                   id);
static void     status_provider_render              (DonnaStatusProvider    *sp,
                                                     guint                   id,
                                                     guint                   index,
                                                     GtkCellRenderer        *renderer);
static gboolean status_provider_set_tooltip         (DonnaStatusProvider    *sp,
                                                     guint                   id,
                                                     guint                   index,
                                                     GtkTooltip             *tooltip);

/* DonnaColumnType */
static const gchar * columntype_get_name            (DonnaColumnType    *ct);
static const gchar * columntype_get_renderers       (DonnaColumnType    *ct);
static void         columntype_get_options          (DonnaColumnType    *ct,
                                                     DonnaColumnOptionInfo **options,
                                                     guint              *nb_options);
static DonnaColumnTypeNeed columntype_refresh_data  (DonnaColumnType    *ct,
                                                     const gchar        *col_name,
                                                     const gchar        *arr_name,
                                                     const gchar        *tv_name,
                                                     gboolean            is_tree,
                                                     gpointer           *data);
static void         columntype_free_data            (DonnaColumnType    *ct,
                                                     gpointer            data);
static GPtrArray *  columntype_get_props            (DonnaColumnType    *ct,
                                                     gpointer            data);
static DonnaColumnTypeNeed columntype_set_option    (DonnaColumnType    *ct,
                                                     const gchar        *col_name,
                                                     const gchar        *arr_name,
                                                     const gchar        *tv_name,
                                                     gboolean            is_tree,
                                                     gpointer            data,
                                                     const gchar        *option,
                                                     gpointer            value,
                                                     gboolean            toggle,
                                                     DonnaColumnOptionSaveLocation save_location,
                                                     GError            **error);
static gchar *      columntype_get_context_alias    (DonnaColumnType   *ct,
                                                     gpointer           data,
                                                     const gchar       *alias,
                                                     const gchar       *extra,
                                                     DonnaContextReference reference,
                                                     DonnaNode         *node_ref,
                                                     get_sel_fn         get_sel,
                                                     gpointer           get_sel_data,
                                                     const gchar       *prefix,
                                                     GError           **error);
static gboolean     columntype_get_context_item_info (
                                                     DonnaColumnType   *ct,
                                                     gpointer           data,
                                                     const gchar       *item,
                                                     const gchar       *extra,
                                                     DonnaContextReference reference,
                                                     DonnaNode         *node_ref,
                                                     get_sel_fn         get_sel,
                                                     gpointer           get_sel_data,
                                                     DonnaContextInfo  *info,
                                                     GError           **error);

#ifdef DONNA_DEBUG
/* this is a hack to "silence" warings, because otherwise we get lots of warning
 * about how inlining failed for g_string_append_c() because call is unlikely &
 * code size would grow */
#undef g_string_append_c
GString *
g_string_append_c (GString *str, gchar c)
{
    if (str->len + 1 < str->allocated_len)
    {
        str->str[str->len++] = c;
        str->str[str->len] = 0;
    }
    else
        g_string_insert_c (str, -1, c);
    return str;
}
#endif

#ifndef GTK_IS_JJK
/* this isn't really the same at all, because the patched version in GTK allows
 * to set the focus without affecting the selection or scroll. Here we have to
 * use set_cursor() to set the focus, and that can trigger some minimum
 * scrolling.
 * We try to "undo" it, but let's be clear: the patched version is obviously
 * much better.
 * Also, gtk_tree_view_set_cursor() is a focus grabber, which could have an
 * impact since the patched version is not. For instance, in
 * donna_tree_view_column_edit() & renderer_edit() we need to work around this,
 * because otherwise setting the focused after the inline editing started would
 * cancel it right away. */
static void
gtk_tree_view_set_focused_row (GtkTreeView *treev, GtkTreePath *path)
{
    DonnaTreeViewPrivate *priv = ((DonnaTreeView *) treev)->priv;
    GtkTreeSelection *sel;
    GtkTreePath *p;
    gint y;
    gboolean scroll;

    sel = gtk_tree_view_get_selection (treev);
    scroll = gtk_tree_view_get_path_at_pos (treev, 0, 0, &p, NULL, NULL, &y);

    if (priv->is_tree)
    {
        GtkSelectionMode mode;
        GtkTreeIter iter;

        if (gtk_tree_selection_get_selected (sel, NULL, &iter))
            g_signal_handlers_block_by_func (sel, selection_changed_cb, treev);
        else
            iter.stamp = 0;

        mode = gtk_tree_selection_get_mode (sel);
        priv->changing_sel_mode = TRUE;
        gtk_tree_selection_set_mode (sel, GTK_SELECTION_NONE);
        gtk_tree_view_set_cursor (treev, path, NULL, FALSE);
        gtk_tree_selection_set_mode (sel, mode);
        priv->changing_sel_mode = FALSE;
        if (iter.stamp != 0)
        {
            gtk_tree_selection_select_iter (sel, &iter);
            g_signal_handlers_unblock_by_func (sel, selection_changed_cb, treev);
        }
    }
    else
    {
        GList *list, *l;

        list = gtk_tree_selection_get_selected_rows (sel, NULL);
        priv->changing_sel_mode = TRUE;
        gtk_tree_selection_set_mode (sel, GTK_SELECTION_NONE);
        gtk_tree_view_set_cursor (treev, path, NULL, FALSE);
        gtk_tree_selection_set_mode (sel, GTK_SELECTION_MULTIPLE);
        priv->changing_sel_mode = FALSE;
        for (l = list; l; l = l->next)
            gtk_tree_selection_select_path (sel, l->data);
        g_list_free_full (list, (GDestroyNotify) gtk_tree_path_free);
    }

    if (scroll)
    {
        gtk_tree_view_scroll_to_cell (treev, p, NULL, TRUE, 0.0, 0.0);
        if (y != 0)
        {
            gint x; /* useless, but we need to send another gint* */
            gint new_y;

            gtk_tree_view_convert_bin_window_to_tree_coords (treev, 0, 0,
                    &x, &new_y);
            gtk_tree_view_scroll_to_point (treev, -1, new_y + y);
        }
        gtk_tree_path_free (p);
    }
}
#endif

static void
donna_tree_view_status_provider_init (DonnaStatusProviderInterface *interface)
{
    interface->create_status    = status_provider_create_status;
    interface->free_status      = status_provider_free_status;
    interface->get_renderers    = status_provider_get_renderers;
    interface->render           = status_provider_render;
    interface->set_tooltip      = status_provider_set_tooltip;
}

static void
donna_tree_view_column_type_init (DonnaColumnTypeInterface *interface)
{
    interface->get_name                 = columntype_get_name;
    interface->get_renderers            = columntype_get_renderers;
    interface->get_options              = columntype_get_options;
    interface->refresh_data             = columntype_refresh_data;
    interface->free_data                = columntype_free_data;
    interface->get_props                = columntype_get_props;
    interface->set_option               = columntype_set_option;
    interface->get_context_alias        = columntype_get_context_alias;
    interface->get_context_item_info    = columntype_get_context_item_info;
}


G_DEFINE_TYPE_WITH_CODE (DonnaTreeView, donna_tree_view, GTK_TYPE_TREE_VIEW,
        G_IMPLEMENT_INTERFACE (DONNA_TYPE_STATUS_PROVIDER,
            donna_tree_view_status_provider_init)
        G_IMPLEMENT_INTERFACE (DONNA_TYPE_COLUMN_TYPE,
            donna_tree_view_column_type_init));

static void
donna_tree_view_class_init (DonnaTreeViewClass *klass)
{
    GtkTreeViewClass *tv_class;
    GtkWidgetClass *w_class;
    GObjectClass *o_class;

    tv_class = GTK_TREE_VIEW_CLASS (klass);
#ifdef GTK_IS_JJK
    tv_class->rubber_banding_active = donna_tree_view_rubber_banding_active;
#endif
    tv_class->row_activated         = donna_tree_view_row_activated;
    tv_class->row_expanded          = donna_tree_view_row_expanded;
    tv_class->row_collapsed         = donna_tree_view_row_collapsed;
    tv_class->test_collapse_row     = donna_tree_view_test_collapse_row;
    tv_class->test_expand_row       = donna_tree_view_test_expand_row;
    tv_class->cursor_changed        = donna_tree_view_cursor_changed;
#ifdef JJK_RUBBER_SIGNAL
    tv_class->test_rubber_banding   = donna_tree_view_test_rubber_banding;
#endif

    w_class = GTK_WIDGET_CLASS (klass);
    w_class->destroy                = donna_tree_view_destroy;
    w_class->draw                   = donna_tree_view_draw;
    w_class->button_press_event     = donna_tree_view_button_press_event;
    w_class->button_release_event   = donna_tree_view_button_release_event;
    w_class->key_press_event        = donna_tree_view_key_press_event;
    w_class->focus                  = donna_tree_view_focus;

    o_class = G_OBJECT_CLASS (klass);
    o_class->get_property   = donna_tree_view_get_property;
    o_class->set_property   = donna_tree_view_set_property;
    o_class->finalize       = donna_tree_view_finalize;

    donna_tree_view_props[PROP_APP] =
        g_param_spec_object ("app", "app",
                "Application",
                DONNA_TYPE_APP,
                G_PARAM_READWRITE | G_PARAM_CONSTRUCT_ONLY);

    donna_tree_view_props[PROP_LOCATION] =
        g_param_spec_object ("location", "location",
                "Current location of the tree view",
                DONNA_TYPE_NODE,
                G_PARAM_READABLE);

    g_object_class_install_properties (o_class, NB_PROPS, donna_tree_view_props);

    donna_tree_view_signals[SIGNAL_SELECT_ARRANGEMENT] =
        g_signal_new ("select-arrangement",
                DONNA_TYPE_TREE_VIEW,
                G_SIGNAL_RUN_LAST,
                G_STRUCT_OFFSET (DonnaTreeViewClass, select_arrangement),
                select_arrangement_accumulator,
                NULL,
                g_cclosure_user_marshal_POINTER__STRING_OBJECT,
                G_TYPE_POINTER,
                2,
                G_TYPE_STRING,
                DONNA_TYPE_NODE);

    gtk_widget_class_install_style_property (w_class,
            g_param_spec_int ("highlighted-size", "Highlighted size",
                "Size of extra highlighted bit on the right",
                0,  /* minimum */
                8,  /* maximum */
                3,  /* default */
                G_PARAM_READABLE));

    g_type_class_add_private (klass, sizeof (DonnaTreeViewPrivate));

    if (!int_renderers[INTERNAL_RENDERER_SPINNER])
        int_renderers[INTERNAL_RENDERER_SPINNER] =
            gtk_cell_renderer_spinner_new ();
    if (!int_renderers[INTERNAL_RENDERER_PIXBUF])
        int_renderers[INTERNAL_RENDERER_PIXBUF] =
            gtk_cell_renderer_pixbuf_new ();
}

static void
free_status (struct status *status)
{
    g_free (status->fmt);
    g_free (status->name);
}

static void
donna_tree_view_init (DonnaTreeView *tv)
{
    DonnaTreeViewPrivate *priv;

    priv = tv->priv = G_TYPE_INSTANCE_GET_PRIVATE (tv, DONNA_TYPE_TREE_VIEW,
            DonnaTreeViewPrivate);
    /* we can't use g_hash_table_new_full and set a destroy, because we will
     * be replacing values often (since head of GSList can change) but don't
     * want the old value to be free-d, obviously */
    priv->hashtable = g_hash_table_new (g_direct_hash, g_direct_equal);

    priv->providers = g_ptr_array_new_with_free_func (
            (GDestroyNotify) free_provider_signals);
    g_mutex_init (&priv->refresh_node_props_mutex);
    priv->col_props = g_array_new (FALSE, FALSE, sizeof (struct col_prop));
    g_array_set_clear_func (priv->col_props, (GDestroyNotify) free_col_prop);
    priv->active_spinners = g_ptr_array_new_with_free_func (
            (GDestroyNotify) free_active_spinners);
    priv->statuses = g_array_new (FALSE, FALSE, sizeof (struct status));
    g_array_set_clear_func (priv->statuses, (GDestroyNotify) free_status);
}

static void
free_col_prop (struct col_prop *cp)
{
    g_free (cp->prop);
}

static void
free_provider_signals (struct provider_signals *ps)
{
    if (ps->sid_node_updated)
        g_signal_handler_disconnect (ps->provider, ps->sid_node_updated);
    if (ps->sid_node_deleted)
        g_signal_handler_disconnect (ps->provider, ps->sid_node_deleted);
    if (ps->sid_node_removed_from)
        g_signal_handler_disconnect (ps->provider, ps->sid_node_removed_from);
    if (ps->sid_node_children)
        g_signal_handler_disconnect (ps->provider, ps->sid_node_children);
    if (ps->sid_node_new_child)
        g_signal_handler_disconnect (ps->provider, ps->sid_node_new_child);
    g_object_unref (ps->provider);
    g_free (ps);
}

static void
free_active_spinners (struct active_spinners *as)
{
    g_object_unref (as->node);
    g_array_free (as->as_cols, TRUE);
    g_free (as);
}

static void
free_hashtable (DonnaNode *node, gpointer data, DonnaTreeView *tree)
{
    if (tree->priv->is_tree)
        g_slist_free_full ((GSList *) data, (GDestroyNotify) gtk_tree_iter_free);
    else if (data)
        gtk_tree_iter_free ((GtkTreeIter *) data);

    g_object_unref (node);
}

static void
free_visuals (struct visuals *visuals)
{
    g_free (visuals->name);
    if (visuals->icon)
        g_object_unref (visuals->icon);
    g_free (visuals->box);
    g_free (visuals->highlight);
    g_free (visuals->click_mode);
    g_slice_free (struct visuals, visuals);
}

static void
donna_tree_view_get_property (GObject        *object,
                              guint           prop_id,
                              GValue         *value,
                              GParamSpec     *pspec)
{
    DonnaTreeViewPrivate *priv = DONNA_TREE_VIEW (object)->priv;

    if (prop_id == PROP_LOCATION)
        g_value_set_object (value, priv->location);
    else if (prop_id == PROP_APP)
        g_value_set_object (value, priv->app);
    else
        G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
}

static void
donna_tree_view_set_property (GObject        *object,
                              guint           prop_id,
                              const GValue   *value,
                              GParamSpec     *pspec)
{
    DonnaTreeViewPrivate *priv = DONNA_TREE_VIEW (object)->priv;

    if (prop_id == PROP_APP)
    {
        donna_g_object_unref (priv->app);
        priv->app = g_value_dup_object (value);
    }
    else
        G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
}

static void
free_column (struct column *_col)
{
    g_free (_col->name);
    g_ptr_array_unref (_col->renderers);
    donna_column_type_free_data (_col->ct, _col->ct_data);
    g_object_unref (_col->ct);
    g_slice_free (struct column, _col);
}

static void
free_column_filter (struct column_filter *col)
{
    g_free (col->name);
    donna_column_type_free_data (col->ct, col->ct_data);
    g_object_unref (col->ct);
    g_free (col);
}

/* for use from finalize only */
static gboolean
free_tree_visuals (gpointer key, GSList *l)
{
    /* this frees the data */
    g_slist_free_full (l, (GDestroyNotify) free_visuals);
    /* this will take care of free-ing the key */
    return TRUE;
}

/* we should remove all refs on columntypes, mostly because one of those might
 * be the treeview itself (line-number) and if we don't unref it, it never gets
 * finalized.
 * So we need to remove the columns, so the arrangement, so we clear the store
 * first.
 * Note that it might be called multiple times, as the signal destroy could be
 * emitted more than once, or it could be called afterwards from finalize()*/
static void
donna_tree_view_destroy (GtkWidget      *widget)
{
    DonnaTreeViewPrivate *priv = ((DonnaTreeView *) widget)->priv;

    if (priv->hashtable)
    {
        DonnaRowId rid = { DONNA_ARG_TYPE_PATH, (gpointer) ":last" };

        /* to avoid warning about lost selection in BROWSE mode or trying to
         * sync on change location */
        g_signal_handlers_disconnect_by_func (
                gtk_tree_view_get_selection ((GtkTreeView *) widget),
                selection_changed_cb, widget);
        /* clear the list (see selection_changed_cb() for why filling_list) */
        priv->filling_list = TRUE;
        /* speed up -- see change_location() for why */
        donna_tree_view_set_focus ((DonnaTreeView *) widget, &rid, NULL);
        gtk_tree_store_clear (priv->store);
        priv->filling_list = FALSE;

        g_hash_table_foreach (priv->hashtable, (GHFunc) free_hashtable, widget);
        g_hash_table_destroy (priv->hashtable);
        priv->hashtable = NULL;
    }

    if (priv->location)
    {
        g_object_unref (priv->location);
        priv->location = NULL;
    }
    priv->location_iter.stamp = 0;

    /* remove all columns */
    while (priv->columns)
    {
        struct column *_col = priv->columns->data;

        /* no need to remove the GtkTreeViewColumn, that will be handled by
         * GtkTreeView automatically */
        g_free (_col->name);
        donna_column_type_free_data (_col->ct, _col->ct_data);
        g_object_unref (_col->ct);
        g_ptr_array_unref (_col->renderers);
        g_slice_free (struct column, _col);
        priv->columns = g_slist_delete_link (priv->columns, priv->columns);
    }

    priv->main_column = NULL;
    priv->second_sort_column = NULL;
    priv->sort_column = NULL;

    if (priv->arrangement)
    {
        free_arrangement (priv->arrangement);
        priv->arrangement = NULL;
    }

    ((GtkWidgetClass *) donna_tree_view_parent_class)->destroy (widget);
}

static void
donna_tree_view_finalize (GObject *object)
{
    DonnaTreeViewPrivate *priv;

    priv = DONNA_TREE_VIEW (object)->priv;
    DONNA_DEBUG (MEMORY, NULL,
            g_debug ("TreeView '%s' finalizing", priv->name));
    donna_tree_view_destroy ((GtkWidget *) object);
    donna_g_object_unref (priv->sync_with);
    g_ptr_array_free (priv->providers, TRUE);
    g_mutex_clear (&priv->refresh_node_props_mutex);
    g_array_free (priv->col_props, TRUE);
    g_ptr_array_free (priv->active_spinners, TRUE);
    g_slist_free_full (priv->columns, (GDestroyNotify) free_column);
    g_slist_free_full (priv->columns_filter, (GDestroyNotify) free_column_filter);
    if (priv->tree_visuals)
        g_hash_table_foreach_remove (priv->tree_visuals,
                (GHRFunc) free_tree_visuals, NULL);
    donna_g_object_unref (priv->filter);
    g_array_free (priv->statuses, TRUE);
    g_free (priv->click_mode);
    g_free (priv->key_mode);
    g_free (priv->name);
    g_object_unref (priv->app);

    G_OBJECT_CLASS (donna_tree_view_parent_class)->finalize (object);
}

gboolean
_donna_tree_view_register_extras (DonnaConfig *config, GError **error)
{
    DonnaConfigItemExtraListInt it_int[8];
    gint i;

    i = 0;
    it_int[i].value     = DONNA_SORT_ASC;
    it_int[i].in_file   = "asc";
    it_int[i].label     = "Ascendingly";
    ++i;
    it_int[i].value     = DONNA_SORT_DESC;
    it_int[i].in_file   = "desc";
    it_int[i].label     = "Descendingly";
    ++i;
    if (G_UNLIKELY (!donna_config_add_extra (config,
                    DONNA_CONFIG_EXTRA_TYPE_LIST_INT, "order", "Sort Order",
                    i, it_int, error)))
        return FALSE;

    i = 0;
    it_int[i].value     = SORT_CONTAINER_FIRST;
    it_int[i].in_file   = "first";
    it_int[i].label     = "First (Last when sorting descendingly)";
    ++i;
    it_int[i].value     = SORT_CONTAINER_FIRST_ALWAYS;
    it_int[i].in_file   = "first-always";
    it_int[i].label     = "Always First";
    ++i;
    it_int[i].value     = SORT_CONTAINER_MIXED;
    it_int[i].in_file   = "mixed";
    it_int[i].label     = "Mixed with Items";
    ++i;
    if (G_UNLIKELY (!donna_config_add_extra (config,
                    DONNA_CONFIG_EXTRA_TYPE_LIST_INT, "sg", "Sort Groups",
                    i, it_int, error)))
        return FALSE;

    i = 0;
    it_int[i].value     = SELECT_HIGHLIGHT_FULL_ROW;
    it_int[i].in_file   = "fullrow";
    it_int[i].label     = "Full Row Highlight";
    ++i;
    it_int[i].value     = SELECT_HIGHLIGHT_COLUMN;
    it_int[i].in_file   = "column";
    it_int[i].label     = "Column (Cell) Highlight";
    ++i;
    it_int[i].value     = SELECT_HIGHLIGHT_UNDERLINE;
    it_int[i].in_file   = "underline";
    it_int[i].label     = "Full Row Underline";
    ++i;
    it_int[i].value     = SELECT_HIGHLIGHT_COLUMN_UNDERLINE;
    it_int[i].in_file   = "column-underline";
    it_int[i].label     = "Column (Cell) Highlight + Full Row Underline";
    ++i;
    if (G_UNLIKELY (!donna_config_add_extra (config,
                    DONNA_CONFIG_EXTRA_TYPE_LIST_INT, "highlight", "Selection Highlight",
                    i, it_int, error)))
        return FALSE;

    i = 0;
    it_int[i].value     = DONNA_TREE_VISUAL_NAME;
    it_int[i].in_file   = "name";
    it_int[i].label     = "Custom Names";
    ++i;
    it_int[i].value     = DONNA_TREE_VISUAL_ICON;
    it_int[i].in_file   = "icon";
    it_int[i].label     = "Custom Icons";
    ++i;
    it_int[i].value     = DONNA_TREE_VISUAL_BOX;
    it_int[i].in_file   = "box";
    it_int[i].label     = "Boxed Branches";
    ++i;
    it_int[i].value     = DONNA_TREE_VISUAL_HIGHLIGHT;
    it_int[i].in_file   = "highlight";
    it_int[i].label     = "Highlighted Folders";
    ++i;
    /* we don't add click_mode because the only option of this type is
     * node_visual, which don't support it */
    if (G_UNLIKELY (!donna_config_add_extra (config,
                    DONNA_CONFIG_EXTRA_TYPE_LIST_FLAGS, "visuals", "Tree Visuals",
                    i, it_int, error)))
        return FALSE;

    i = 0;
    it_int[i].value     = TREE_SYNC_NONE;
    it_int[i].in_file   = "none";
    it_int[i].label     = "None";
    ++i;
    it_int[i].value     = TREE_SYNC_NODES;
    it_int[i].in_file   = "nodes";
    it_int[i].label     = "Only with accessible nodes";
    ++i;
    it_int[i].value     = TREE_SYNC_NODES_KNOWN_CHILDREN;
    it_int[i].in_file   = "known-children";
    it_int[i].label     = "Expand nodes only if children are known";
    ++i;
    it_int[i].value     = TREE_SYNC_NODES_CHILDREN;
    it_int[i].in_file   = "children";
    it_int[i].label     = "Expand nodes";
    ++i;
    it_int[i].value     = TREE_SYNC_FULL;
    it_int[i].in_file   = "full";
    it_int[i].label     = "Full";
    ++i;
    if (G_UNLIKELY (!donna_config_add_extra (config,
                    DONNA_CONFIG_EXTRA_TYPE_LIST_INT, "sync", "Synchronization Mode",
                    i, it_int, error)))
        return FALSE;

    i = 0;
    it_int[i].value     = DONNA_TREE_VIEW_SET_SCROLL;
    it_int[i].in_file   = "scroll";
    it_int[i].label     = "Scroll";
    ++i;
    it_int[i].value     = DONNA_TREE_VIEW_SET_FOCUS;
    it_int[i].in_file   = "focus";
    it_int[i].label     = "Focus";
    ++i;
    it_int[i].value     = DONNA_TREE_VIEW_SET_CURSOR;
    it_int[i].in_file   = "cursor";
    it_int[i].label     = "Cursor";
    ++i;
    if (G_UNLIKELY (!donna_config_add_extra (config,
                    DONNA_CONFIG_EXTRA_TYPE_LIST_FLAGS, "tree-set", "Tree Set",
                    i, it_int, error)))
        return FALSE;

    i = 0;
    it_int[i].value     = KEY_DISABLED;
    it_int[i].in_file   = "disabled";
    it_int[i].label     = "Disabled";
    ++i;
    it_int[i].value     = KEY_COMBINE;
    it_int[i].in_file   = "combine";
    it_int[i].label     = "Combine";
    ++i;
    it_int[i].value     = KEY_DIRECT;
    it_int[i].in_file   = "direct";
    it_int[i].label     = "Direct";
    ++i;
    it_int[i].value     = KEY_SPEC;
    it_int[i].in_file   = "spec";
    it_int[i].label     = "Spec";
    ++i;
    it_int[i].value     = KEY_ALIAS;
    it_int[i].in_file   = "alias";
    it_int[i].label     = "Alias";
    ++i;
    if (G_UNLIKELY (!donna_config_add_extra (config,
                    DONNA_CONFIG_EXTRA_TYPE_LIST_INT, "key", "Key",
                    i, it_int, error)))
        return FALSE;

    i = 0;
    it_int[i].value     = SPEC_LOWER;
    it_int[i].in_file   = "lower";
    it_int[i].label     = "Lowercase letter (a-z)";
    ++i;
    it_int[i].value     = SPEC_UPPER;
    it_int[i].in_file   = "upper";
    it_int[i].label     = "Uppercase latter (A-Z)";
    ++i;
    it_int[i].value     = SPEC_DIGITS;
    it_int[i].in_file   = "digits";
    it_int[i].label     = "Digit (0-9)";
    ++i;
    it_int[i].value     = SPEC_EXTRA;
    it_int[i].in_file   = "extra";
    it_int[i].label     = "Extra chars (see doc)";
    ++i;
    it_int[i].value     = SPEC_CUSTOM;
    it_int[i].in_file   = "custom";
    it_int[i].label     = "Custom chars (option custom_chars)";
    ++i;
    it_int[i].value     = SPEC_MOTION;
    it_int[i].in_file   = "motion";
    it_int[i].label     = "Motion Key";
    ++i;
    if (G_UNLIKELY (!donna_config_add_extra (config,
                    DONNA_CONFIG_EXTRA_TYPE_LIST_FLAGS, "spec", "Spec Type",
                    i, it_int, error)))
        return FALSE;

    /* this looks like it should be FLAGS, but we use INT so we can have three
     * options (items, containes, all) instead of two that can be added, and
     * would allow the invalid "nothing" */
    i = 0;
    it_int[i].value     = DONNA_NODE_ITEM;
    it_int[i].in_file   = "items";
    it_int[i].label     = "Items";
    ++i;
    it_int[i].value     = DONNA_NODE_CONTAINER;
    it_int[i].in_file   = "containers";
    it_int[i].label     = "Containers";
    ++i;
    it_int[i].value     = DONNA_NODE_ITEM | DONNA_NODE_CONTAINER;
    it_int[i].in_file   = "all";
    it_int[i].label     = "All (Items & Containers)";
    ++i;
    if (G_UNLIKELY (!donna_config_add_extra (config,
                    DONNA_CONFIG_EXTRA_TYPE_LIST_INT, "node-type", "Type of node",
                    i, it_int, error)))
        return FALSE;

    i = 0;
    it_int[i].value     = DONNA_COLUMN_OPTION_SAVE_IN_MEMORY;
    it_int[i].in_file   = "memory";
    it_int[i].label     = "In Memory";
    ++i;
    it_int[i].value     = DONNA_COLUMN_OPTION_SAVE_IN_CURRENT;
    it_int[i].in_file   = "current";
    it_int[i].label     = "Same As Current";
    ++i;
    it_int[i].value     = DONNA_COLUMN_OPTION_SAVE_IN_ASK;
    it_int[i].in_file   = "ask";
    it_int[i].label     = "Ask";
    ++i;
    if (G_UNLIKELY (!donna_config_add_extra (config,
                    DONNA_CONFIG_EXTRA_TYPE_LIST_INT, "save-location", "Save Location",
                    i, it_int, error)))
        return FALSE;

    i = 0;
    it_int[i].value     = ST_COLORS_OFF;
    it_int[i].in_file   = "off";
    it_int[i].label     = "Off";
    ++i;
    it_int[i].value     = ST_COLORS_KEYS;
    it_int[i].in_file   = "keys";
    it_int[i].label     = "Based on current key mode";
    ++i;
    it_int[i].value     = ST_COLORS_VF;
    it_int[i].in_file   = "vf";
    it_int[i].label     = "When a visual filter is applied";
    ++i;
    if (G_UNLIKELY (!donna_config_add_extra (config,
                    DONNA_CONFIG_EXTRA_TYPE_LIST_INT, "tree-st-colors",
                    "Change colors (treeview status)",
                    i, it_int, error)))
        return FALSE;

    i = 0;
    it_int[i].value     = RP_VISIBLE;
    it_int[i].in_file   = "visible";
    it_int[i].label     = "When row is visible";
    it_int[i].label     = "Off";
    ++i;
    it_int[i].value     = RP_PRELOAD;
    it_int[i].in_file   = "preload";
    it_int[i].label     = "When visible, preloading other rows";
    ++i;
    it_int[i].value     = RP_ON_DEMAND;
    it_int[i].in_file   = "on_demand";
    it_int[i].label     = "On Demand (e.g. when clicking the refresh icon)";
    ++i;
    if (G_UNLIKELY (!donna_config_add_extra (config,
                    DONNA_CONFIG_EXTRA_TYPE_LIST_INT, "col-rp",
                    "Columns' Properties Refresh Time",
                    i, it_int, error)))
        return FALSE;

    return TRUE;
}

/* some extensions to the GtkTreeModel interface. next/previous perform a
 * "natural" version, as in instead of being stuck to the same level, it does
 * what the user would expect from keys up/down.
 * Also adds last & get_count.
 */

static gboolean
_gtk_tree_model_iter_next (GtkTreeModel *model,
                           GtkTreeIter  *iter)
{
    GtkTreeIter it;

    g_return_val_if_fail (GTK_IS_TREE_MODEL (model), FALSE);

    /* get first child if any */
    if (gtk_tree_model_iter_children (model, &it, iter))
    {
        *iter = it;
        return TRUE;
    }
    /* then look for sibling */
    it = *iter;
    if (gtk_tree_model_iter_next (model, &it))
    {
        *iter = it;
        return TRUE;
    }
    /* then we need the parent's sibling */
    while (1)
    {
        if (!gtk_tree_model_iter_parent (model, &it, iter))
        {
            iter->stamp = 0;
            return FALSE;
        }
        *iter = it;
        if (gtk_tree_model_iter_next (model, &it))
        {
            *iter = it;
            return TRUE;
        }
    }
}

static gboolean
_get_last_child (GtkTreeModel *model, GtkTreeIter *iter)
{
    GtkTreeIter it = *iter;
    if (!gtk_tree_model_iter_children (model, &it, (iter->stamp == 0) ? NULL : iter))
        return FALSE;
    *iter = it;
    while (gtk_tree_model_iter_next (model, &it))
        *iter = it;
    return TRUE;
}

#define get_last_child(model, iter) for (;;) {  \
    if (!_get_last_child (model, iter))         \
        break;                                  \
}

static gboolean
_gtk_tree_model_iter_previous (GtkTreeModel *model,
                               GtkTreeIter  *iter)
{
    GtkTreeIter it;

    g_return_val_if_fail (GTK_IS_TREE_MODEL (model), FALSE);

    /* get previous sibbling if any */
    it = *iter;
    if (gtk_tree_model_iter_previous (model, &it))
    {
        *iter = it;
        /* and go down to its last child */
        get_last_child (model, iter);
        return TRUE;
    }
    /* else we get the parent */
    if (gtk_tree_model_iter_parent (model, &it, iter))
    {
        *iter = it;
        return TRUE;
    }
    iter->stamp = 0;
    return FALSE;
}

static gboolean
_gtk_tree_model_iter_last (GtkTreeModel *model,
                            GtkTreeIter *iter)
{
    g_return_val_if_fail (GTK_IS_TREE_MODEL (model), FALSE);
    g_return_val_if_fail (iter != NULL, FALSE);

    iter->stamp = 0;
    get_last_child (model, iter);
    return iter->stamp != 0;
}

struct count
{
    gint count;
    gint max;
};

static gboolean
_get_count (GtkTreeModel    *model,
            GtkTreePath     *path,
            GtkTreeIter     *iter,
            struct count    *count)
{
    ++count->count;
    if (count->max > 0 && count->count >= count->max)
        /* done */
        return TRUE;
    else
        /* keep iterating */
        return FALSE;
}

static gint
_gtk_tree_model_get_count (GtkTreeModel *model)
{
    struct count count = { 0, 0 };

    g_return_val_if_fail (GTK_IS_TREE_MODEL (model), -1);

    gtk_tree_model_foreach (model, (GtkTreeModelForeachFunc) _get_count, &count);
    return count.count;
}

static gboolean
has_model_at_least_n_rows (GtkTreeModel *model, gint max)
{
    struct count count = { 0, max };

    g_return_val_if_fail (GTK_IS_TREE_MODEL (model), -1);
    g_return_val_if_fail (max > 0, -1);

    gtk_tree_model_foreach (model, (GtkTreeModelForeachFunc) _get_count, &count);
    return count.count >= count.max;
}


/* tree synchronisation */

struct scroll_data
{
    DonnaTreeView   *tree;
    GtkTreeIter     *iter;
};

static gboolean
idle_scroll_to_iter (struct scroll_data *data)
{
    scroll_to_iter (data->tree, data->iter);
    g_slice_free (struct scroll_data, data);
    return FALSE;
}

/* this is obviously called when sync_with changes location, but also from
 * donna_tree_view_set_location() when in tree mode. Because in mode tree, a
 * set_location() is really just the following, only with FULL sync mode forced.
 */
static gboolean
perform_sync_location (DonnaTreeView    *tree,
                       DonnaNode        *node,
                       enum tree_sync    sync_mode,
                       gboolean          ignore_show_hidden)
{
    DonnaTreeViewPrivate *priv = tree->priv;
    GtkTreeView *treev;
    GtkTreeSelection *sel;
    GtkTreeIter *iter = NULL;
    static DonnaNode *node_ref;

    node_ref = node;

    switch (sync_mode)
    {
        case TREE_SYNC_NODES:
            iter = get_best_existing_iter_for_node (tree, node, FALSE);
            break;

        case TREE_SYNC_NODES_KNOWN_CHILDREN:
            iter = get_best_existing_iter_for_node (tree, node, TRUE);
            break;

        case TREE_SYNC_NODES_CHILDREN:
            iter = get_best_iter_for_node (tree, node, FALSE, FALSE, NULL);
            break;

        case TREE_SYNC_FULL:
            iter = get_best_iter_for_node (tree, node, TRUE, ignore_show_hidden, NULL);
            break;

        case TREE_SYNC_NONE:
            /* silence warning */
            break;
    }

    /* Here's the thing: those functions probably had to call some get_node()
     * which in turn could have led to running a new main loop while the task
     * (to get the node) was running.
     * It is technically possible that, during that main loop, something
     * happened that changed location again. If that's the case, we shall not
     * keep working anymore and just abort.
     * We could have done that check after each get_node() in the functions, but
     * that's a lot more of a PITA to do, and also it could be argued that in
     * such a case any expansion shall still happened, so this is a better
     * handling of it.
     * (Anyhow, it should be pretty rare to occur, since usually the nodes we
     * need might already be in the provider's cache (i.e. no main loop), the
     * expansion bit happens "à la" minitree even in non-minitree so it's very
     * fast; IOW it shouldn't be easy to trigger it.)
     */
    if (node_ref != node)
        /* TRUE because this shouldn't be seen as an error */
        return TRUE;

    treev = (GtkTreeView *) tree;
    sel = gtk_tree_view_get_selection (treev);
    if (iter)
    {
        GtkTreeModel *model = (GtkTreeModel *) priv->store;
        enum tree_expand es;
        GtkTreePath *path;

        gtk_tree_selection_set_mode (sel, GTK_SELECTION_BROWSE);
        /* we select the new row and put the cursor on it (required to get
         * things working when collapsing the parent) */
        path = gtk_tree_model_get_path ((GtkTreeModel *) priv->store, iter);
        if (priv->sync_mode == TREE_SYNC_NODES_KNOWN_CHILDREN)
        {
            GtkTreePath *p;
            gint i, depth, *indices;

            /* we're doing here the same as gtk_tree_view_expand_to_path() only
             * without expanding the row at path itself (only parents to it) */

            indices = gtk_tree_path_get_indices_with_depth (path, &depth);
            --depth;
            p = gtk_tree_path_new ();
            for (i = 0; i < depth; ++i)
            {
                gtk_tree_path_append_index (p, indices[i]);
                gtk_tree_view_expand_row (treev, p, FALSE);
            }
            gtk_tree_path_free (p);
        }

        /* this beauty will put focus & select the row, without doing any
         * scrolling whatsoever. What a wonderful thing! :) */
        /* Note: that's true when GTK_IS_JJK; if not we do provide a replacement
         * for set_focused_row() that should get the same results, though much
         * less efficiently. */
        gtk_tree_view_set_focused_row (treev, path);
        gtk_tree_selection_select_path (sel, path);
        gtk_tree_path_free (path);

        /* if we're in EXPAND_MAXI let's try and refresh our children */
        gtk_tree_model_get (model, iter, TREE_COL_EXPAND_STATE, &es, -1);
        if (es == TREE_EXPAND_MAXI)
        {
            GPtrArray *arr;

            arr = donna_tree_view_get_children (priv->sync_with,
                    priv->location, priv->node_types);
            if (arr)
            {
                set_children (tree, iter, priv->node_types, arr, FALSE, FALSE);
                g_ptr_array_unref (arr);
            }
        }

        if (priv->sync_scroll)
        {
            struct scroll_data *data;
            data = g_slice_new (struct scroll_data);
            data->tree = tree;
            data->iter = iter;

            /* the reason we use a timeout here w/ a magic number, is that
             * expanding rows had GTK install some triggers
             * (presize/validate_rows) that are required to be processed for
             * things to work, i.e. if we try to call get_background_area now
             * (which scroll_to_iter does to calculate visibility) we get BS
             * values.  I couldn't find a proper way around it, idle w/ low
             * priority doesn't do it, only a timeout seems to work. About 15
             * should be enough to do the trick, so we're hoping that 42 will
             * always work */
            g_timeout_add (42, (GSourceFunc) idle_scroll_to_iter, data);
        }
    }
    else
    {
        /* in non-flat domain we try to move the focus on closest matching row.
         * We do this before unselecting so the current location/iter are still
         * set, so we know/can give precedence to the current root */
        if (!(donna_provider_get_flags (donna_node_peek_provider (node))
                    & DONNA_PROVIDER_FLAG_FLAT))
        {
            gchar *location;

            location = donna_node_get_location (node);
            iter = get_closest_iter_for_node (tree, node,
                    donna_node_peek_provider (node), location,
                    FALSE, NULL);
            g_free (location);

            /* see comment for same stuff above */
            if (node_ref != node)
                /* TRUE because this shouldn't be seen as an error */
                return TRUE;

            if (iter)
            {
                GtkTreePath *path;

                /* we don't want to select anything here, just put focus on the
                 * closest accessible parent we just found, also put that iter
                 * into view */

                path = gtk_tree_model_get_path (GTK_TREE_MODEL (priv->store), iter);
                gtk_tree_view_set_focused_row (treev, path);
                gtk_tree_path_free (path);

                if (priv->sync_scroll)
                    scroll_to_iter (tree, iter);
            }
        }

        /* unselect, but allow a new selection to be made (will then switch
         * automatically back to SELECTION_BROWSE) */
        priv->changing_sel_mode = TRUE;
        gtk_tree_selection_set_mode (sel, GTK_SELECTION_SINGLE);
        priv->changing_sel_mode = FALSE;
        gtk_tree_selection_unselect_all (sel);
    }

    /* it might have already happened on selection change, but this might have
     * not changed the selection, only the focus (if anything), so: */
    check_statuses (tree, STATUS_CHANGED_ON_CONTENT);

    return !!iter;
}

static void
sync_with_location_changed_cb (GObject       *object,
                               GParamSpec    *pspec,
                               DonnaTreeView *tree)
{
    DonnaTreeViewPrivate *priv = tree->priv;
    DonnaNode *node;

    g_object_get (object, "location", &node, NULL);
    if (node == priv->location)
    {
        donna_g_object_unref (node);
        return;
    }

    if (node)
    {
        perform_sync_location (tree, node, priv->sync_mode, FALSE);
        g_object_unref (node);
    }
}

static void
active_list_changed_cb (GObject         *object,
                        GParamSpec      *pspec,
                        DonnaTreeView   *tree)
{
    DonnaTreeViewPrivate *priv = tree->priv;

    if (priv->sync_with)
    {
        if (priv->sid_sw_location_changed)
            g_signal_handler_disconnect (priv->sync_with,
                    priv->sid_sw_location_changed);
        g_object_unref (priv->sync_with);
    }
    g_object_get (object, "active-list", &priv->sync_with, NULL);
    priv->sid_sw_location_changed = g_signal_connect (priv->sync_with,
            "notify::location",
            G_CALLBACK (sync_with_location_changed_cb), tree);

    sync_with_location_changed_cb ((GObject *) priv->sync_with, NULL, tree);
}

/* mode list only */
static inline void
set_get_children_task (DonnaTreeView *tree, DonnaTask *task)
{
    if (tree->priv->get_children_task)
    {
        DonnaTask *t = tree->priv->get_children_task;

        /* we need to set it to NULL *before* we cancel it, in case the task was
         * e.g. not yet started (WAITING), as it would then be set to CANCELLED
         * right away and therefore had its callback called (right now, since we
         * are in the main/UI thread), and we don't want said callback to do
         * anything obviously */
        tree->priv->get_children_task = NULL;
        if (!(donna_task_get_state (t) & DONNA_TASK_POST_RUN))
            donna_task_cancel (t);
        g_object_unref (t);
    }
    tree->priv->get_children_task = (task) ? g_object_ref (task): NULL;
}

enum
{
    OPT_NONE = 0,
    OPT_DEFAULT,
    OPT_TREE_VIEW,
    OPT_TREE_VIEW_COLUMN,
    OPT_IN_MEMORY,  /* from set_option() when value in changed in memory */
    _OPT_NB
};

struct option_data
{
    DonnaTreeView *tree;
    gchar *option;
    guint opt;
    gpointer val;
};

static gboolean
reset_node_visuals (GtkTreeModel    *model,
                    GtkTreePath     *path,
                    GtkTreeIter     *iter,
                    DonnaTreeView   *tree)
{
    DonnaTreeViewPrivate *priv = tree->priv;
    DonnaTreeVisual visual;
    DonnaNode *node;

    gtk_tree_model_get (model, iter,
            TREE_COL_VISUALS,   &visual,
            TREE_COL_NODE,      &node,
            -1);

    if (!node)
        /* keep iterating */
        return FALSE;

    if (!(priv->node_visuals & DONNA_TREE_VISUAL_NAME)
            && !(visual & DONNA_TREE_VISUAL_NAME))
        gtk_tree_store_set (tree->priv->store, iter,
                TREE_COL_NAME,          NULL,
                -1);

    if (!(priv->node_visuals & DONNA_TREE_VISUAL_ICON)
            && !(visual & DONNA_TREE_VISUAL_ICON))
        gtk_tree_store_set (tree->priv->store, iter,
                TREE_COL_ICON,          NULL,
                -1);

    if (!(priv->node_visuals & DONNA_TREE_VISUAL_BOX)
            && !(visual & DONNA_TREE_VISUAL_BOX))
        gtk_tree_store_set (tree->priv->store, iter,
                TREE_COL_BOX,           NULL,
                -1);

    if (!(priv->node_visuals & DONNA_TREE_VISUAL_HIGHLIGHT)
            && !(visual & DONNA_TREE_VISUAL_HIGHLIGHT))
        gtk_tree_store_set (tree->priv->store, iter,
                TREE_COL_HIGHLIGHT,     NULL,
                -1);

    load_node_visuals (tree, iter, node, TRUE);
    g_object_unref (node);

    /* keep iterating */
    return FALSE;
}

static gboolean
switch_minitree_off (GtkTreeModel    *model,
                     GtkTreePath     *path,
                     GtkTreeIter     *iter,
                     DonnaTreeView   *tree)
{
    enum tree_expand es;

    gtk_tree_model_get (model, iter, TREE_COL_EXPAND_STATE, &es, -1);
    if (es == TREE_EXPAND_PARTIAL)
    {
        if (gtk_tree_view_row_expanded ((GtkTreeView *) tree, path))
            maxi_expand_row (tree, iter);
        else
            maxi_collapse_row (tree, iter);
    }

    /* keep iterating */
    return FALSE;
}

struct node_children_refresh_data
{
    DonnaTreeView *tree;
    GtkTreeIter iter;
    DonnaNodeType node_types;
    gboolean from_show_hidden;
};

static void node_get_children_refresh_tree_cb (DonnaTask *task,
                                               gboolean timeout_called,
                                               struct node_children_refresh_data *data);
static void node_has_children_cb (DonnaTask *task,
                                  gboolean timeout_called,
                                  struct node_children_data *data);
static void free_node_children_data (struct node_children_data *data);

static void
refresh_tree_show_hidden (DonnaTreeView *tree)
{
    DonnaTreeViewPrivate *priv = tree->priv;
    GtkTreeModel *model = (GtkTreeModel *) priv->store;
    GtkTreeIter it;

    if (priv->show_hidden)
    {
        if (!gtk_tree_model_iter_children (model, &it, NULL))
            return;
        for (;;)
        {
            enum tree_expand es;

            gtk_tree_model_get (model, &it, TREE_COL_EXPAND_STATE, &es, -1);
            if (es == TREE_EXPAND_MAXI)
            {
                DonnaNode *node;
                DonnaTask *task;

                gtk_tree_model_get (model, &it, TREE_COL_NODE, &node, -1);
                task = donna_node_get_children_task (node, priv->node_types, NULL);
                if (!task)
                {
                    gchar *fl = donna_node_get_full_location (node);
                    g_warning ("TreeView '%s': Failed to create task get_children() "
                            "for node '%s' (from refresh_tree_show_hidden())",
                            priv->name, fl);
                    g_free (fl);
                }
                else
                {
                    struct node_children_refresh_data *data;

                    data = g_new0 (struct node_children_refresh_data, 1);
                    data->tree = tree;
                    data->iter = it;
                    watch_iter (tree, &data->iter);
                    data->node_types = priv->node_types;
                    data->from_show_hidden = TRUE;

                    donna_task_set_callback (task,
                            (task_callback_fn) node_get_children_refresh_tree_cb,
                            data, g_free);
                    donna_app_run_task (priv->app, task);
                }
                g_object_unref (node);
            }
            else if (es == TREE_EXPAND_NONE)
            {
                DonnaNode *node;
                DonnaTask *task;

                gtk_tree_model_get (model, &it, TREE_COL_NODE, &node, -1);
                /* add fake node */
                gtk_tree_store_insert_with_values (priv->store, NULL, &it, 0,
                        TREE_COL_NODE,  NULL,
                        -1);
                /* update expand state */
                set_es (priv, &it, TREE_EXPAND_UNKNOWN);
                /* trigger has_children */
                task = donna_node_has_children_task (node, priv->node_types, NULL);
                if (task)
                {
                    struct node_children_data *data;

                    data = g_slice_new0 (struct node_children_data);
                    data->tree = tree;
                    data->iter = it;
                    watch_iter (tree, &data->iter);

                    donna_task_set_callback (task,
                            (task_callback_fn) node_has_children_cb,
                            data,
                            (GDestroyNotify) free_node_children_data);
                    donna_app_run_task (priv->app, task);
                }
            }

            if (!_gtk_tree_model_iter_next (model, &it))
                break;
        }
    }
    else
    {
        GtkTreeIter it_root;

        if (!gtk_tree_model_iter_children (model, &it, NULL))
            return;
        it_root = it;
        for (;;)
        {
            DonnaNode *node;
            gchar *name;
            gboolean keep = TRUE;

            gtk_tree_model_get (model, &it, TREE_COL_NODE, &node, -1);
            if (!node)
                keep = _gtk_tree_model_iter_next (model, &it);
            else
            {
                name = donna_node_get_name (node);
                g_object_unref (node);

                if (*name == '.')
                {
                    GtkTreeIter it_parent;

                    /* get the parent, in case there are no more siblings */
                    gtk_tree_model_iter_parent (model, &it_parent, &it);
                    if (remove_row_from_tree (tree, &it, RR_NOT_REMOVAL_STAY_MAXI))
                        keep = TRUE;
                    else if (it_parent.stamp != 0)
                    {
                        /* no siblings, trying the sibling of the parent */
                        it = it_parent;
                        keep = gtk_tree_model_iter_next (model, &it);
                        if (!keep)
                        {
                            /* going to the next root */
                            it = it_root;
                            keep = _gtk_tree_model_iter_next (model, &it);
                        }
                    }
                    else
                    {
                        /* going to the next root */
                        it = it_root;
                        keep = _gtk_tree_model_iter_next (model, &it);
                    }
                }
                else
                    keep = _gtk_tree_model_iter_next (model, &it);
                g_free (name);
            }

            if (!keep)
                break;
        }
    }
}

static void
add_col_props (DonnaTreeView *tree, struct column *_col)
{
    DonnaTreeViewPrivate *priv = tree->priv;
    GPtrArray *props;

    props = donna_column_type_get_props (_col->ct, _col->ct_data);
    if (props)
    {
        guint i;

        for (i = 0; i < props->len; ++i)
        {
            struct col_prop cp;

            cp.prop = g_strdup (props->pdata[i]);
            cp.column = _col->column;
            g_array_append_val (priv->col_props, cp);
        }
        g_ptr_array_unref (props);
    }
    else
        g_critical ("TreeView '%s': column '%s' reports no properties to watch for refresh",
                priv->name, _col->name);
}

static inline void
refresh_col_props (DonnaTreeView *tree)
{
    DonnaTreeViewPrivate *priv = tree->priv;
    GSList *l;

    if (priv->col_props->len > 0)
        g_array_set_size (priv->col_props, 0);

    for (l = tree->priv->columns; l; l = l->next)
        /* ignore treeview as ct (line-number) */
        if (((struct column *) l->data)->ct != (DonnaColumnType *) tree)
            add_col_props (tree, (struct column *) l->data);
}

static gint
config_get_int (DonnaTreeView   *tree,
                DonnaConfig     *config,
                const gchar     *option,
                gint             def)
{
    gint val;

    if (donna_config_get_int (config, NULL, &val, "tree_views/%s/%s",
            tree->priv->name, option))
        return val;
    if (donna_config_get_int (config, NULL, &val, "defaults/%s/%s",
                (tree->priv->is_tree) ? "trees" : "lists", option))
        return val;
    g_warning ("TreeView '%s': option 'defaults/%s/%s' not found, "
            "setting default (%d)",
            tree->priv->name,
            (tree->priv->is_tree) ? "trees" : "lists",
            option,
            def);
    donna_config_set_int (config, NULL, def, "defaults/%s/%s",
            (tree->priv->is_tree) ? "trees" : "lists", option);
    return def;
}

static gboolean
config_get_boolean (DonnaTreeView   *tree,
                    DonnaConfig     *config,
                    const gchar     *option,
                    gboolean         def)
{
    gboolean val;

    if (donna_config_get_boolean (config, NULL, &val, "tree_views/%s/%s",
            tree->priv->name, option))
        return val;
    if (donna_config_get_boolean (config, NULL, &val, "defaults/%s/%s",
                (tree->priv->is_tree) ? "trees" : "lists", option))
        return val;
    g_warning ("TreeView '%s': option 'defaults/%s/%s' not found, "
            "setting default (%d)",
            tree->priv->name,
            (tree->priv->is_tree) ? "trees" : "lists",
            option,
            def);
    donna_config_set_boolean (config, NULL, def, "defaults/%s/%s",
            (tree->priv->is_tree) ? "trees" : "lists", option);
    return def;
}

static gchar *
config_get_string (DonnaTreeView   *tree,
                   DonnaConfig     *config,
                   const gchar     *option,
                   const gchar     *def)
{
    gchar *val;

    if (donna_config_get_string (config, NULL, &val, "tree_views/%s/%s",
            tree->priv->name, option))
        return val;
    if (donna_config_get_string (config, NULL, &val, "defaults/%s/%s",
                (tree->priv->is_tree) ? "trees" : "lists", option))
        return val;
    if (!def)
        return NULL;
    g_warning ("TreeView '%s': option 'defaults/%s/%s' not found, "
            "setting default (%s)",
            tree->priv->name,
            (tree->priv->is_tree) ? "trees" : "lists",
            option,
            def);
    donna_config_set_string (config, NULL, def, "defaults/%s/%s",
            (tree->priv->is_tree) ? "trees" : "lists", option);
    return g_strdup (def);
}

static DonnaColumnOptionInfo _tv_options[] = {
    { "is_tree",            G_TYPE_BOOLEAN,     NULL },
    { "show_hidden",        G_TYPE_BOOLEAN,     NULL },
    { "node_types",         G_TYPE_INT,         "node-type" },
    { "sort_groups",        G_TYPE_INT,         "sg" },
    { "select_highlight",   G_TYPE_INT,         "highlight" },
    { "key_mode",           G_TYPE_STRING,      NULL },
    { "click_mode",         G_TYPE_STRING,      NULL },
    { "default_save_location", G_TYPE_INT,      "save-location" }
};
static DonnaColumnOptionInfo _tree_options[] = {
    { "node_visuals",       G_TYPE_INT,         "visuals" },
    { "is_minitree",        G_TYPE_BOOLEAN,     NULL },
    { "sync_mode",          G_TYPE_INT,         "sync" },
    { "sync_with",          G_TYPE_STRING,      NULL },
    { "sync_scroll",        G_TYPE_BOOLEAN,     NULL },
    { "auto_focus_sync",    G_TYPE_BOOLEAN,     NULL }
};
static DonnaColumnOptionInfo _list_options[] = {
    { "vf_items_only",      G_TYPE_BOOLEAN,     NULL },
    { "focusing_click",     G_TYPE_BOOLEAN,     NULL },
    { "goto_item_set",      G_TYPE_INT,         "tree-set" },
    { "history_max",        G_TYPE_INT,         NULL }
};

#define cfg_get_is_tree(t,c) \
    config_get_boolean (t, c, "is_tree", FALSE)
#define cfg_get_show_hidden(t,c) \
    config_get_boolean (t, c, "show_hidden", TRUE)
#define cfg_get_node_types(t,c) \
    CLAMP (config_get_int (t, c, "node_types", \
                ((t)->priv->is_tree) ? DONNA_NODE_CONTAINER \
                : DONNA_NODE_CONTAINER | DONNA_NODE_ITEM), \
                0, 3)
#define cfg_get_sort_groups(t,c) \
    CLAMP (config_get_int (t, c, "sort_groups", SORT_CONTAINER_FIRST), 0, 2)
#ifdef GTK_IS_JJK
#define cfg_get_select_highlight(t,c) \
    CLAMP (config_get_int (t, c, "select_highlight", \
                ((t)->priv->is_tree) ? SELECT_HIGHLIGHT_COLUMN \
                : SELECT_HIGHLIGHT_COLUMN_UNDERLINE), 0, 3)
#else
#define cfg_get_select_highlight(t,c) SELECT_HIGHLIGHT_FULL_ROW
#endif
#define cfg_get_node_visuals(t,c) \
    CLAMP (config_get_int (t, c, "node_visuals", DONNA_TREE_VISUAL_NOTHING), 0, 31)
#define cfg_get_is_minitree(t,c) \
    config_get_boolean (t, c, "is_minitree", FALSE)
#define cfg_get_sync_mode(t,c) \
    CLAMP (config_get_int (t, c, "sync_mode", TREE_SYNC_FULL), 0, 4)
#define cfg_get_sync_with(t,c) \
    config_get_string (t, c, "sync_with", NULL)
#define cfg_get_sync_scroll(t,c) \
    config_get_boolean (t, c, "sync_scroll", TRUE)
#define cfg_get_auto_focus_sync(t,c) \
    config_get_boolean (t, c, "auto_focus_sync", TRUE)
#define cfg_get_focusing_click(t,c) \
    config_get_boolean (t, c, "focusing_click", TRUE)
#define cfg_get_goto_item_set(t,c) \
    CLAMP (config_get_int (t, c, "goto_item_set", \
            DONNA_TREE_VIEW_SET_SCROLL | DONNA_TREE_VIEW_SET_FOCUS), 0, 7)
#define cfg_get_vf_items_only(t,c) \
    config_get_boolean (t, c, "vf_items_only", FALSE)
#define cfg_get_history_max(t,c) \
    config_get_int (t, c, "history_max", 100)
#define cfg_get_key_mode(t,c) \
    config_get_string (t, c, "key_mode", "donna")
#define cfg_get_click_mode(t,c) \
    config_get_string (t, c, "click_mode", "donna")
#define cfg_get_default_save_location(t,c) \
    config_get_int (t, c, "default_save_location", DONNA_COLUMN_OPTION_SAVE_IN_ASK)

static gboolean
real_option_cb (struct option_data *od)
{
    DonnaTreeView *tree = od->tree;
    DonnaTreeViewPrivate *priv = tree->priv;
    DonnaConfig *config;
    gchar buf[255], *b = buf;
    gchar *opt;
    gsize len = 0;
    gchar *s;

    /* could be OPT_IN_MEMORY from donna_tree_view_set_option() */
    if (od->opt == OPT_NONE)
    {
        /* options we care about are ones for the tree (in "tree_views/<NAME>"
         * or "defaults/<MODE>s") or for one of our columns:
         * tree_views/<NAME>/columns/<NAME>
         * defaults/<MODE>s/columns/<NAME>
         * We also care about columns_options from current arrangement, and
         * should refresh all columns' ctdata on changes in defaults that aren't
         * in either modes.
         *
         * We don't follow other sources from arrangement (columns layout, sort
         * orders...) because (a) they're mostly set in-memory, then maybe saved
         * somewhere, and because of that it would feel odd than a change
         * in config "overwrites" your current (possibly non-saved) settings.
         * And (b), unlike options, which can be set & saved somewhere via one
         * command, and you'd expect the new value to be applied, there are no
         * commands to set a column layout/sort order with a save location,
         * related commands are all in-memory only, so a change in config is
         * done via changing the config, and then a "reload" of the arrangement
         * seems normal/right.
         */

        /* start w/ arrangement, since it can be located anywhere (including
         * categories we would otherwise wrongly match) */
        if (G_LIKELY (priv->arrangement) && priv->arrangement->columns_options)
        {
            s = priv->arrangement->columns_options;
            len = strlen (s);
            if (streqn (od->option, s, len)
                    && streqn (od->option + len, "/columns_options/", 17))
            {
                od->opt = OPT_TREE_VIEW_COLUMN;
                len += 17;
                goto process;
            }
        }

        len = (gsize) snprintf (buf, 255, "tree_views/%s/", priv->name);
        if (len >= 255)
            b = g_strdup_printf ("tree_views/%s/", priv->name);

        if (streqn (od->option, b, len))
        {
            od->opt = OPT_TREE_VIEW;
            if (streqn (od->option + len, "columns/", 8))
            {
                od->opt = OPT_TREE_VIEW_COLUMN;
                len += 8;
            }
        }

        if (b != buf)
            g_free (b);

        if (od->opt == OPT_NONE && streqn (od->option, "defaults/", 9))
        {
            len = 9;
            if (streqn (od->option + len, (priv->is_tree) ? "trees/" : "lists/", 6))
            {
                len += 6;
                if (streqn (od->option + len, "columns/", 8))
                {
                    od->opt = OPT_TREE_VIEW_COLUMN;
                    len += 8;
                }
                else
                    od->opt = OPT_DEFAULT;
            }
            else
            {
                /* it's not our mode, is it the other one? if not, we'll need to
                 * refresh all columns (in case they use "generic" defaults) */
                if (!streqn (od->option + len, (priv->is_tree) ? "lists/" : "trees/", 6))
                {
                    GSList *l;
                    DonnaColumnTypeNeed need = 0;

                    DONNA_DEBUG (TREE_VIEW, priv->name,
                            g_debug ("TreeView '%s': Config change in defaults (%s)",
                                priv->name, od->option));

                    for (l = priv->columns; l; l = l->next)
                    {
                        struct column *_col = l->data;

                        need |= donna_column_type_refresh_data (_col->ct, _col->name,
                                (priv->arrangement) ? priv->arrangement->columns_options : NULL,
                                priv->name,
                                priv->is_tree,
                                &_col->ct_data);
                    }
                    refresh_col_props (tree);
                    if (need & DONNA_COLUMN_TYPE_NEED_RESORT)
                        resort_tree (tree);
                    if (need & DONNA_COLUMN_TYPE_NEED_REDRAW)
                        gtk_widget_queue_draw ((GtkWidget *) tree);
                }
                goto done;
            }
        }
    }

    if (od->opt == OPT_NONE)
        goto done;

process:
    config = donna_app_peek_config (priv->app);
    opt = od->option + len;

    DONNA_DEBUG (TREE_VIEW, priv->name,
            const gchar *opt_type[_OPT_NB];
            opt_type[OPT_DEFAULT] = "treeview (default)";
            opt_type[OPT_TREE_VIEW] = "treeview";
            opt_type[OPT_TREE_VIEW_COLUMN] = "column";
            opt_type[OPT_IN_MEMORY] = "treeview (in-memory)";
            g_debug ("TreeView '%s': Config change for %s option '%s' (%s)",
                priv->name, opt_type[od->opt], opt, od->option));

    if (od->opt == OPT_TREE_VIEW || od->opt == OPT_DEFAULT
            || od->opt == OPT_IN_MEMORY)
    {
        gint val;

        /* tree view option */

        if (streq (opt, "is_tree"))
        {
            /* cannot be OPT_IN_MEMORY */
            val = cfg_get_is_tree (tree, config);
            if (priv->is_tree != val)
            {
                donna_app_show_error (priv->app, NULL,
                        "TreeView '%s': option 'is_tree' was changed; "
                        "Please restart the application to have it applied.",
                        priv->name);
            }
        }
        else if (streq (opt, "show_hidden"))
        {
            if (od->opt == OPT_IN_MEMORY)
                val = (* (gboolean *) od->val) ? TRUE : FALSE;
            else
                val = cfg_get_show_hidden (tree, config);

            if (od->opt == OPT_IN_MEMORY || priv->show_hidden != val)
            {
                priv->show_hidden = val;
                if (priv->is_tree)
                    refresh_tree_show_hidden (tree);
                else
                    refilter_list (tree);
            }
        }
        else if (streq (opt, "node_types"))
        {
            if (od->opt == OPT_IN_MEMORY)
                val = CLAMP (* (gint *) od->val, 0, 3);
            else
                val = cfg_get_node_types (tree, config);

            if (od->opt == OPT_IN_MEMORY || priv->node_types != (guint) val)
            {
                priv->node_types = val;
                donna_tree_view_refresh (tree, DONNA_TREE_VIEW_REFRESH_RELOAD, NULL);
            }
        }
        else if (streq (opt, "sort_groups"))
        {
            if (od->opt == OPT_IN_MEMORY)
                val = CLAMP (* (gint *) od->val, 0, 2);
            else
                val = cfg_get_sort_groups (tree, config);

            if (od->opt == OPT_IN_MEMORY || priv->sort_groups != (guint) val)
            {
                priv->sort_groups = val;
                resort_tree (od->tree);
            }
        }
        else if (streq (opt, "select_highlight"))
        {
            if (od->opt == OPT_IN_MEMORY)
                val = CLAMP (* (gint *) od->val, 0, 3);
            else
                val = cfg_get_select_highlight (tree, config);

#ifdef GTK_IS_JJK
            if (od->opt == OPT_IN_MEMORY || priv->select_highlight != (guint) val)
            {
                GtkTreeView *treev = (GtkTreeView *) tree;

                priv->select_highlight = val;
                if (priv->select_highlight == SELECT_HIGHLIGHT_COLUMN
                        || priv->select_highlight == SELECT_HIGHLIGHT_COLUMN_UNDERLINE)
                    gtk_tree_view_set_select_highlight_column (treev, priv->main_column);
                else if (priv->select_highlight == SELECT_HIGHLIGHT_UNDERLINE)
                {
                    /* since we only want an underline, we must set the select highlight
                     * column to a non-visible one */
                    if (priv->is_tree)
                    {
                        /* tree never uses an empty column on the right, so we store the
                         * extra non-visible column used for this */
                        if (!priv->blank_column)
                        {
                            priv->blank_column = gtk_tree_view_column_new ();
                            gtk_tree_view_column_set_sizing (priv->blank_column,
                                    GTK_TREE_VIEW_COLUMN_FIXED);
                            gtk_tree_view_insert_column (treev, priv->blank_column, -1);
                        }
                        gtk_tree_view_set_select_highlight_column (treev, priv->blank_column);
                    }
                    else
                        /* list: expander_column is always set to a non-visible one */
                        gtk_tree_view_set_select_highlight_column (treev,
                                gtk_tree_view_get_expander_column (treev));
                }
                else
                    gtk_tree_view_set_select_highlight_column (treev, NULL);

                gtk_tree_view_set_select_row_underline (treev,
                        priv->select_highlight == SELECT_HIGHLIGHT_UNDERLINE
                        || priv->select_highlight == SELECT_HIGHLIGHT_COLUMN_UNDERLINE);

                gtk_widget_queue_draw ((GtkWidget *) treev);
            }
#endif
        }
        else if (streq (opt, "key_mode"))
        {
            if (od->opt == OPT_IN_MEMORY)
                s = * (gchar **) od->val;
            else
                s = cfg_get_key_mode (tree, config);

            if (od->opt == OPT_IN_MEMORY || !streq (priv->key_mode, s))
                donna_tree_view_set_key_mode (tree, s);

            if (od->opt != OPT_IN_MEMORY)
                g_free (s);
        }
        else if (streq (opt, "click_mode"))
        {
            if (od->opt == OPT_IN_MEMORY)
                s = * (gchar **) od->val;
            else
                s = cfg_get_click_mode (tree, config);

            if (od->opt == OPT_IN_MEMORY || !streq (priv->click_mode, s))
            {
                g_free (priv->click_mode);
                priv->click_mode = (od->opt == OPT_IN_MEMORY)
                    ? g_strdup (s) : s;
            }
            else if (od->opt != OPT_IN_MEMORY)
                g_free (s);
        }
        else if (streq (opt, "default_save_location"))
        {
            if (od->opt == OPT_IN_MEMORY)
                val = * (gint *) od->val;
            else
                val = cfg_get_default_save_location (tree, config);

            if (od->opt == OPT_IN_MEMORY || priv->default_save_location != (guint) val)
                priv->default_save_location = val;
        }
        else if (priv->is_tree)
        {
            if (streq (opt, "node_visuals"))
            {
                if (od->opt == OPT_IN_MEMORY)
                    val = CLAMP (* (gint *) od->val, 0, 31);
                else
                    val = cfg_get_node_visuals (tree, config);

                if (od->opt == OPT_IN_MEMORY || priv->node_visuals != (guint) val)
                {
                    priv->node_visuals = (guint) val;
                    gtk_tree_model_foreach ((GtkTreeModel *) priv->store,
                            (GtkTreeModelForeachFunc) reset_node_visuals,
                            tree);
                }
            }
            else if (streq (opt, "is_minitree"))
            {
                if (od->opt == OPT_IN_MEMORY)
                    val = (* (gboolean *) od->val) ? TRUE : FALSE;
                else
                    val = cfg_get_is_minitree (tree, config);

                if (od->opt == OPT_IN_MEMORY || priv->is_minitree != val)
                {
                    priv->is_minitree = val;
                    if (!val)
                    {
                        gtk_tree_model_foreach ((GtkTreeModel *) priv->store,
                                (GtkTreeModelForeachFunc) switch_minitree_off,
                                tree);
                        g_idle_add ((GSourceFunc) scroll_to_current, tree);
                    }
                }
            }
            else if (streq (opt, "sync_mode"))
            {
                if (od->opt == OPT_IN_MEMORY)
                    val = CLAMP (* (gint *) od->val, 0, 4);
                else
                    val = cfg_get_sync_mode (tree, config);

                if (od->opt == OPT_IN_MEMORY || priv->sync_mode != (guint) val)
                {
                    priv->sync_mode = val;
                    if (priv->sync_with)
                        sync_with_location_changed_cb ((GObject *) priv->sync_with,
                                NULL, tree);
                }
            }
            else if (streq (opt, "sync_with"))
            {
                DonnaTreeView *sw;

                if (od->opt == OPT_IN_MEMORY)
                    s = * (gchar **) od->val;
                else
                    s = cfg_get_sync_with (tree, config);

                if (streq (s, ":active"))
                    g_object_get (priv->app, "active-list", &sw, NULL);
                else if (s)
                    sw = donna_app_get_tree_view (priv->app, s);
                else
                    sw = NULL;

                /* if the tree view isn't a list, ignore */
                if (sw && sw->priv->is_tree)
                {
                    g_warning ("TreeView '%s': Option 'sync_with' set to '%s' "
                            "which is a tree -- Can only sync with lists",
                            priv->name, s);
                    sw = NULL;
                }

                if (priv->sync_with != sw)
                {
                    if (priv->sid_active_list_changed)
                    {
                        g_signal_handler_disconnect (priv->app,
                                priv->sid_active_list_changed);
                        priv->sid_active_list_changed = 0;
                    }
                    else if (streq (s, ":active"))
                        priv->sid_active_list_changed = g_signal_connect (priv->app,
                                "notify::active-list",
                                (GCallback) active_list_changed_cb, tree);

                    if (priv->sync_with)
                    {
                        g_signal_handler_disconnect (priv->sync_with,
                                priv->sid_sw_location_changed);
                        g_object_unref (priv->sync_with);
                    }
                    priv->sync_with = sw;
                    priv->sid_sw_location_changed = (sw)
                        ? g_signal_connect (priv->sync_with,
                                "notify::location",
                                (GCallback) sync_with_location_changed_cb,
                                tree)
                        : 0;

                    if (priv->sid_tree_view_loaded)
                    {
                        g_signal_handler_disconnect (priv->app,
                                priv->sid_tree_view_loaded);
                        priv->sid_tree_view_loaded = 0;
                    }
                }
                /* the same tree view could be set, but with a switch between
                 * the tree view itself and the active list */
                else if ((priv->sid_active_list_changed) ? TRUE : FALSE
                        != (streq (s, ":active")) ? TRUE : FALSE)
                {
                    if (priv->sid_active_list_changed)
                    {
                        g_signal_handler_disconnect (priv->app,
                                priv->sid_active_list_changed);
                        priv->sid_active_list_changed = 0;
                    }
                    else
                        priv->sid_active_list_changed = g_signal_connect (priv->app,
                                "notify::active-list",
                                (GCallback) active_list_changed_cb, tree);
                    donna_g_object_unref (sw);
                }
                else
                    donna_g_object_unref (sw);

                if (od->opt != OPT_IN_MEMORY)
                    g_free (s);
            }
            else if (streq (opt, "sync_scroll"))
            {
                if (od->opt == OPT_IN_MEMORY)
                    val = (* (gboolean *) od->val) ? TRUE : FALSE;
                else
                    val = cfg_get_sync_scroll (tree, config);

                if (od->opt == OPT_IN_MEMORY || priv->sync_scroll != val)
                    priv->sync_scroll = val;
            }
            else if (streq (opt, "auto_focus_sync"))
            {
                if (od->opt == OPT_IN_MEMORY)
                    val = (* (gboolean *) od->val) ? TRUE : FALSE;
                else
                    val = cfg_get_auto_focus_sync (tree, config);

                if (od->opt == OPT_IN_MEMORY || priv->auto_focus_sync != val)
                    priv->auto_focus_sync = val;
            }
        }
        else /* list */
        {
            if (streq (opt, "focusing_click"))
            {
                if (od->opt == OPT_IN_MEMORY)
                    val = (* (gboolean *) od->val) ? TRUE : FALSE;
                else
                    val = cfg_get_focusing_click (tree, config);

                if (od->opt == OPT_IN_MEMORY || priv->focusing_click != val)
                    priv->focusing_click = val;
            }
            else if (streq (opt, "goto_item_set"))
            {
                if (od->opt == OPT_IN_MEMORY)
                    val = * (gint *) od->val;
                else
                    val = cfg_get_goto_item_set (tree, config);

                if (od->opt == OPT_IN_MEMORY || priv->goto_item_set != (guint) val)
                    priv->goto_item_set = val;
            }
            if (streq (opt, "vf_items_only"))
            {
                if (od->opt == OPT_IN_MEMORY)
                    val = (* (gboolean *) od->val) ? TRUE : FALSE;
                else
                    val = cfg_get_vf_items_only (tree, config);

                if (od->opt == OPT_IN_MEMORY || priv->vf_items_only != val)
                {
                    priv->vf_items_only = val;
                    refilter_list (tree);
                }
            }
            else if (streq (opt, "history_max"))
            {
                if (od->opt == OPT_IN_MEMORY)
                    val = * (gint *) od->val;
                else
                    val = cfg_get_history_max (tree, config);

                if (od->opt == OPT_IN_MEMORY
                        || donna_history_get_max (priv->history) != (guint) val)
                    donna_history_set_max (priv->history, (guint) val);
            }
        }
    }
    /* columns option */
    else
    {
        struct column *_col;

        s = strchr (opt, '/');
        if (!s)
            goto done;

        /* is this change about a column we are using right now? */
        *s = '\0';
        _col = get_column_by_name (tree, opt);
        *s = '/';
        if (!_col)
            goto done;

        if (streq (s + 1, "title"))
        {
            gchar *ss = NULL;

            /* we know we will get a value, but it might not be from the
             * config changed that occured, since the value might be
             * overridden by current arrangement, etc */
            ss = donna_config_get_string_column (config, _col->name,
                    (priv->arrangement) ? priv->arrangement->columns_options : NULL,
                    priv->name,
                    priv->is_tree,
                    NULL,
                    "title", ss);
            gtk_tree_view_column_set_title (_col->column, ss);
            gtk_label_set_text ((GtkLabel *) _col->label, ss);
            g_free (ss);
        }
        else if (streq (s + 1, "width"))
        {
            gint w = 0;

            /* we know we will get a value, but it might not be from the
             * config changed that occured, since the value might be
             * overridden by current arrangement, etc */
            w = donna_config_get_int_column (config, _col->name,
                    (priv->arrangement) ? priv->arrangement->columns_options : NULL,
                    priv->name,
                    priv->is_tree,
                    NULL,
                    "width", w);
            gtk_tree_view_column_set_fixed_width (_col->column, w);
        }
        else if (streq (s + 1, "refresh_properties"))
        {
            guint rp;
            guint old = _col->refresh_properties;

            /* we know we will get a value, but it might not be from the
             * config changed that occured, since the value might be
             * overridden by current arrangement, etc */
            rp = (guint) donna_config_get_int_column (config, _col->name,
                    (priv->arrangement) ? priv->arrangement->columns_options : NULL,
                    priv->name,
                    priv->is_tree,
                    NULL,
                    "refresh_properties", RP_VISIBLE);
            if (rp < _MAX_RP && rp != _col->refresh_properties)
            {
                _col->refresh_properties = rp;
                if (old == RP_ON_DEMAND)
                    gtk_widget_queue_draw ((GtkWidget *) tree);
                if (rp == RP_PRELOAD)
                    preload_props_columns (tree);
            }
        }
        else
        {
            DonnaColumnTypeNeed need;

            /* ask the ct if something needs to happen */
            need = donna_column_type_refresh_data (_col->ct, _col->name,
                    (priv->arrangement) ? priv->arrangement->columns_options : NULL,
                    priv->name,
                    priv->is_tree,
                    &_col->ct_data);
            refresh_col_props (tree);
            if (need & DONNA_COLUMN_TYPE_NEED_RESORT)
                resort_tree (tree);
            if (need & DONNA_COLUMN_TYPE_NEED_REDRAW)
                gtk_widget_queue_draw ((GtkWidget *) tree);
        }
    }

done:
    if (od->opt != OPT_IN_MEMORY)
    {
        g_free (od->option);
        g_free (od);
    }
    return G_SOURCE_REMOVE;
}

static void
option_cb (DonnaConfig *config, const gchar *option, DonnaTreeView *tree)
{
    struct option_data *od;

    /* see donna_tree_view_save_to_config() */
    if (tree->priv->saving_config)
        return;

    od = g_new (struct option_data, 1);
    od->tree = tree;
    od->option = g_strdup (option);
    od->opt = OPT_NONE;
    g_main_context_invoke (NULL, (GSourceFunc) real_option_cb, od);
}

static void
tree_view_loaded_cb (DonnaApp *app, DonnaTreeView *loaded_tree, DonnaTreeView *tree)
{
    DonnaTreeViewPrivate *priv = tree->priv;
    gchar *s;

    s = cfg_get_sync_with (tree, donna_app_peek_config (priv->app));
    if (!priv->sync_with && streq (s, loaded_tree->priv->name))
    {
        g_signal_handler_disconnect (priv->app, priv->sid_tree_view_loaded);
        priv->sid_tree_view_loaded = 0;
        priv->sync_with = g_object_ref (loaded_tree);
        priv->sid_sw_location_changed = g_signal_connect (priv->sync_with,
                "notify::location",
                (GCallback) sync_with_location_changed_cb, tree);
    }
    g_free (s);
}

static void
load_config (DonnaTreeView *tree)
{
    DonnaTreeViewPrivate *priv;
    DonnaConfig *config;

    /* we load/cache some options, because usually we can't just get those when
     * needed, but they need to trigger some refresh or something. So we need to
     * listen on the option_{set,deleted} signals of the config manager anyways.
     * Might as well save a few function calls... */

    priv = tree->priv;
    config = donna_app_peek_config (priv->app);

    priv->is_tree = cfg_get_is_tree (tree, config);
    priv->show_hidden = cfg_get_show_hidden (tree, config);
    priv->node_types = cfg_get_node_types (tree, config);
    priv->sort_groups = cfg_get_sort_groups (tree, config);
    priv->select_highlight = cfg_get_select_highlight (tree, config);
    priv->key_mode = cfg_get_key_mode (tree, config);
    priv->click_mode = cfg_get_click_mode (tree, config);
    priv->default_save_location = cfg_get_default_save_location (tree, config);

    if (priv->is_tree)
    {
        gchar *s;

        priv->node_visuals = cfg_get_node_visuals (tree, config);
        priv->is_minitree = cfg_get_is_minitree (tree, config);
        priv->sync_mode = cfg_get_sync_mode (tree, config);

        s = cfg_get_sync_with (tree, config);
        if (streq (s, ":active"))
        {
            g_object_get (priv->app, "active-list", &priv->sync_with, NULL);
            priv->sid_active_list_changed = g_signal_connect (priv->app,
                    "notify::active-list",
                    (GCallback) active_list_changed_cb, tree);
        }
        else if (s)
            priv->sync_with = donna_app_get_tree_view (priv->app, s);
        g_free (s);
        if (priv->sync_with)
            priv->sid_sw_location_changed = g_signal_connect (priv->sync_with,
                    "notify::location",
                    (GCallback) sync_with_location_changed_cb, tree);
        else if (s)
            priv->sid_tree_view_loaded = g_signal_connect (priv->app,
                    "tree_view_loaded",
                    (GCallback) tree_view_loaded_cb, tree);

        priv->sync_scroll = cfg_get_sync_scroll (tree, config);
        priv->auto_focus_sync = cfg_get_auto_focus_sync (tree, config);
    }
    else
    {
        guint max;

        priv->focusing_click = cfg_get_focusing_click (tree, config);
        priv->goto_item_set = cfg_get_goto_item_set (tree, config);
        priv->vf_items_only = cfg_get_vf_items_only (tree, config);

        max = (guint) cfg_get_history_max (tree, config);
        priv->history = donna_history_new (max);
    }

    /* listen to config changes */
    priv->option_set_sid = g_signal_connect (config, "option-set",
            (GCallback) option_cb, tree);
    priv->option_deleted_sid = g_signal_connect (config, "option-deleted",
            (GCallback) option_cb, tree);
}

static gboolean
is_watched_iter_valid (DonnaTreeView *tree, GtkTreeIter *iter, gboolean remove)
{
    DonnaTreeViewPrivate *priv = tree->priv;
    GSList *l, *prev = NULL;

    l = priv->watched_iters;
    while (l)
    {
        if (l->data == iter)
        {
            if (remove)
            {
                if (prev)
                    prev->next = l->next;
                else
                    priv->watched_iters = l->next;

                g_slist_free_1 (l);
            }
            return TRUE;
        }
        else
        {
            prev = l;
            l = prev->next;
        }
    }
    return FALSE;
}

static inline struct column *
get_column_by_column (DonnaTreeView *tree, GtkTreeViewColumn *column)
{
    GSList *l;

    for (l = tree->priv->columns; l; l = l->next)
        if (((struct column *) l->data)->column == column)
            return l->data;
    return NULL;
}

static inline struct column *
get_column_by_name (DonnaTreeView *tree, const gchar *name)
{
    GSList *l;

    for (l = tree->priv->columns; l; l = l->next)
        if (streq (((struct column *) l->data)->name, name))
            return l->data;
    return NULL;
}

/* used from functions wrapped in commands, to get a column from a possibly
 * incomplete name. Useful so commands can be used from keys via spec, where
 * only one letter can be specified.
 * This is also why, as a special bonus, we support using a number to get the
 * nth column, in the order they are on treeview */
static struct column *
get_column_from_name (DonnaTreeView *tree, const gchar *name, GError **error)
{
    struct column *ret = NULL;
    GSList *l;
    gsize len;

    if (!name)
    {
        g_set_error (error, DONNA_TREE_VIEW_ERROR,
                DONNA_TREE_VIEW_ERROR_NOT_FOUND,
                "TreeView '%s': Unable to find column: no name specified",
                tree->priv->name);
        return NULL;
    }

    if (*name >= '0' && *name <= '9')
    {
        GList *list, *ll;
        gint nb;
        gchar *s;

        nb = (gint) g_ascii_strtoll (name, &s, 10);
        if (!s || *s != '\0' || nb <= 0)
        {
            g_set_error (error, DONNA_TREE_VIEW_ERROR,
                    DONNA_TREE_VIEW_ERROR_NOT_FOUND,
                    "TreeView '%s': Unable to find column: Invalid name '%s'",
                    tree->priv->name, name);
            return NULL;
        }

        list = gtk_tree_view_get_columns ((GtkTreeView *) tree);
        for (ll = list; ll; ll = ll->next)
        {
            struct column *_col = get_column_by_column (tree, ll->data);

            /* blankcol */
            if (!_col)
                continue;

            if (--nb == 0)
            {
                ret = _col;
                break;
            }
        }
        g_list_free (list);

        if (!ret)
            g_set_error (error, DONNA_TREE_VIEW_ERROR,
                    DONNA_TREE_VIEW_ERROR_NOT_FOUND,
                    "TreeView '%s': Unable to find column '%s': "
                    "Not that many columns in treeview",
                    tree->priv->name, name);
        return ret;
    }

    len = strlen (name);
    for (l = tree->priv->columns; l; l = l->next)
    {
        struct column *_col = l->data;
        gsize _len = strlen (_col->name);

        /* name is too long, skip */
        if (len > _len)
            continue;
        /* same length, can be an exact match */
        else if (len == _len)
        {
            /* we make this a special case so "foo" will match column "foo" even
             * if there's a column "foobar" */
            if (streq (name, _col->name))
            {
                ret = _col;
                break;
            }
        }

        if (!streqn (name, _col->name, len))
            continue;
        else if (ret)
        {
            g_set_error (error, DONNA_TREE_VIEW_ERROR,
                    DONNA_TREE_VIEW_ERROR_COLUMN_NAME_TOO_BROAD,
                    "TreeView '%s': Unable to find column '%s': More than one match",
                    tree->priv->name, name);
            return NULL;
        }

        ret = _col;
    }

    if (!ret)
        g_set_error (error, DONNA_TREE_VIEW_ERROR,
                DONNA_TREE_VIEW_ERROR_NOT_FOUND,
                "TreeView '%s': Unable to find column '%s'",
                tree->priv->name, name);
    return ret;
}

static void
show_err_on_task_failed (DonnaTask      *task,
                         gboolean        timeout_called,
                         DonnaTreeView  *tree)
{
    if (donna_task_get_state (task) != DONNA_TASK_FAILED)
        return;

    donna_app_show_error (tree->priv->app, donna_task_get_error (task),
            "TreeView '%s': Failed to trigger node", tree->priv->name);
}

static void
free_node_children_data (struct node_children_data *data)
{
    remove_watch_iter (data->tree, &data->iter);
    g_slice_free (struct node_children_data, data);
}

/* mode tree only */
static void
node_get_children_tree_timeout (DonnaTask                   *task,
                                struct node_children_data   *data)
{
    GtkTreePath *path;

    /* we're slow to get the children, let's just show the fake node ("please
     * wait...") */
    if (!is_watched_iter_valid (data->tree, &data->iter, FALSE))
        return;
    path = gtk_tree_model_get_path (GTK_TREE_MODEL (data->tree->priv->store),
            &data->iter);
    gtk_tree_view_expand_row (GTK_TREE_VIEW (data->tree), path, FALSE);
    gtk_tree_path_free (path);
}

struct ctv_data
{
    DonnaTreeView *tree;
    GtkTreeIter   *iter;
};

static gboolean
clean_tree_visuals (gchar *fl, GSList *list, struct ctv_data *data)
{
    GSList *l, *next;

    for (l = list; l; l = next)
    {
        struct visuals *visuals = l->data;
        next = l->next;

        if (itereq (data->iter, &visuals->root))
        {
            free_visuals (visuals);
            list = g_slist_delete_link (list, l);
        }
    }

    return list == NULL;
}

static void
handle_removing_row (DonnaTreeView *tree, GtkTreeIter *iter, gboolean is_focus)
{
    GtkTreeModel *model = (GtkTreeModel *) tree->priv->store;
    GtkTreeIter it = *iter;
    gboolean found = FALSE;

    /* we will move the focus/selection (current row in tree) to the next item
     * (or prev if there's no next).
     * In list, it's a simple next/prev; on tree it's the same (to try to stay
     * on the same level), then we go up. This is obviously the natural choice,
     * especially for the current location. */

    if (gtk_tree_model_iter_next (model, &it))
        found = TRUE;
    else
    {
        it = *iter;
        if (gtk_tree_model_iter_previous (model, &it))
            found= TRUE;
    }

    if (!found && tree->priv->is_tree)
        found = gtk_tree_model_iter_parent (model, &it, iter);

    if (!is_focus)
    {
        GtkTreeSelection *sel = gtk_tree_view_get_selection ((GtkTreeView *) tree);

        if (found)
            gtk_tree_selection_select_iter (sel, &it);
        else
        {
            if (!gtk_tree_model_iter_children (model, &it, NULL))
            {
                /* if there's no more rows on tree, let's make sure we don't
                 * have an old (invalid) current location */
                if (tree->priv->location)
                {
                    g_object_unref (tree->priv->location);
                    tree->priv->location = NULL;
                    tree->priv->location_iter.stamp = 0;
                }
                return;
            }

            /* then move to the first root, but make sure this isn't the row
             * we're moving away from (might be a row about to be removed) */
            while (itereq (&it, iter))
            {
                if (!gtk_tree_model_iter_next (model, &it))
                    break;
            }
            if (it.stamp != 0)
                gtk_tree_selection_select_iter (sel, &it);
            else
            {
                /* nowhere to go, no more current location: unselect, but allow
                 * a new selection to be made (will then switch automatically
                 * back to SELECTION_BROWSE) */
                tree->priv->changing_sel_mode = TRUE;
                gtk_tree_selection_set_mode (sel, GTK_SELECTION_SINGLE);
                tree->priv->changing_sel_mode = FALSE;
                gtk_tree_selection_unselect_all (sel);
            }
        }
    }
    else if (found)
    {
        GtkTreePath *path;

        path = gtk_tree_model_get_path (model, &it);
        gtk_tree_view_set_focused_row ((GtkTreeView *) tree, path);
        gtk_tree_path_free (path);
    }
}

static void
remove_node_from_list (DonnaTreeView    *tree,
                       DonnaNode        *node,
                       GtkTreeIter      *iter)
{
    DonnaTreeViewPrivate *priv = tree->priv;
    DonnaProvider *provider;
    guint i;

    if (iter)
    {
        remove_row_from_tree (tree, iter, RR_IS_REMOVAL);
        return;
    }

    DONNA_DEBUG (TREE_VIEW, priv->name,
            gchar *fl = donna_node_get_full_location (node);
            g_debug2 ("TreeView '%s': remove node '%s' from hashtable",
                priv->name, fl);
            g_free (fl));

    /* get its provider */
    provider = donna_node_peek_provider (node);
    /* and update the nb of nodes we have for this provider */
    for (i = 0; i < priv->providers->len; ++i)
    {
        struct provider_signals *ps = priv->providers->pdata[i];

        if (ps->provider == provider)
        {
            if (--ps->nb_nodes == 0)
            {
                /* this will disconnect handlers from provider & free memory */
                g_ptr_array_remove_index_fast (priv->providers, i);
                break;
            }
        }
    }

    g_hash_table_remove (priv->hashtable, node);
    g_object_unref (node);
}

/* similar to gtk_tree_store_remove() this will set iter to next row at that
 * level, or invalid it if it pointer to the last one.
 * Returns TRUE if iter is still valid, else FALSE */
/* Note: the reason we don't put this as handler for the store's row-deleted
 * signal is that that signal happens *after* the row has been deleted, and
 * therefore there are no iter. But we *need* an iter here, to take care of our
 * hashlist of, well, iters. This is also why we also have special handling of
 * removing an iter w/ children. */
static gboolean
remove_row_from_tree (DonnaTreeView *tree,
                      GtkTreeIter   *iter,
                      enum removal   removal)
{
    DonnaTreeViewPrivate *priv = tree->priv;
    GtkTreeModel *model = (GtkTreeModel *) priv->store;
    DonnaNode *node;
    DonnaProvider *provider;
    guint i;
    GSList *l, *prev;
    GtkTreeIter parent;
    GtkTreeIter it;
    gboolean is_root = FALSE;
    gboolean ret;

    /* get the node */
    gtk_tree_model_get (model, iter,
            TREE_VIEW_COL_NODE, &node,
            -1);
    if (node)
    {
        if (priv->is_tree || removal == RR_IS_REMOVAL)
        {
            /* get its provider */
            provider = donna_node_peek_provider (node);
            /* and update the nb of nodes we have for this provider */
            for (i = 0; i < priv->providers->len; ++i)
            {
                struct provider_signals *ps = priv->providers->pdata[i];

                if (ps->provider == provider)
                {
                    if (--ps->nb_nodes == 0)
                    {
                        /* this will disconnect handlers from provider & free
                         * memory */
                        g_ptr_array_remove_index_fast (priv->providers, i);
                        break;
                    }
                }
            }
        }

        if (priv->is_tree)
        {
            /* we'll need that info post_removal, i.e. once iter isn't valid
             * anymore */
            is_root = gtk_tree_store_iter_depth (priv->store, iter) == 0;

            if (removal != RR_IS_REMOVAL)
            {
                DonnaTreeVisual v;

                /* place any tree_visuals back there to remember them when the
                 * node comes back */

                gtk_tree_model_get (model, iter,
                        TREE_COL_VISUALS,   &v,
                        -1);
                if (v > 0)
                {
                    struct visuals *visuals;
                    GtkTreeIter *root;
                    gchar *fl;

                    visuals = g_slice_new0 (struct visuals);
                    root = get_root_iter (tree, iter);
                    visuals->root = *root;

                    /* we can't just get everything, since there might be
                     * node_visuals applied */
                    if (v & DONNA_TREE_VISUAL_NAME)
                        gtk_tree_model_get (model, iter,
                                TREE_COL_NAME,          &visuals->name,
                                -1);
                    if (v & DONNA_TREE_VISUAL_ICON)
                        gtk_tree_model_get (model, iter,
                                TREE_COL_ICON,          &visuals->icon,
                                -1);
                    if (v & DONNA_TREE_VISUAL_BOX)
                        gtk_tree_model_get (model, iter,
                                TREE_COL_BOX,           &visuals->box,
                                -1);
                    if (v & DONNA_TREE_VISUAL_HIGHLIGHT)
                        gtk_tree_model_get (model, iter,
                                TREE_COL_HIGHLIGHT,     &visuals->highlight,
                                -1);
                    /* not a visual, but treated the same */
                    if (v & DONNA_TREE_VISUAL_CLICK_MODE)
                        gtk_tree_model_get (model, iter,
                                TREE_COL_CLICK_MODE,    &visuals->click_mode,
                                -1);

                    fl = donna_node_get_full_location (node);

                    l = NULL;
                    if (priv->tree_visuals)
                        l = g_hash_table_lookup (priv->tree_visuals, fl);
                    else
                        priv->tree_visuals = g_hash_table_new_full (
                                g_str_hash, g_str_equal,
                                g_free, NULL);

                    l = g_slist_prepend (l, visuals);
                    g_hash_table_insert (priv->tree_visuals, fl, l);
                }
            }
        }

        g_object_unref (node);
    }

    /* removing the row with the focus will have GTK do a set_cursor(), this
     * isn't the best of behaviors, so let's see if we can do "better" */
    if (has_model_at_least_n_rows (model, 2))
    {
        GtkTreePath *path_cursor;

        gtk_tree_view_get_cursor ((GtkTreeView *) tree, &path_cursor, NULL);
        if (path_cursor)
        {
            GtkTreeIter  iter_cursor;

            gtk_tree_model_get_iter (model, &iter_cursor, path_cursor);
            if (itereq (iter, &iter_cursor)
                    /* if the cursor is on a children, same deal */
                    || gtk_tree_store_is_ancestor (priv->store, iter, &iter_cursor))
                handle_removing_row (tree, iter, TRUE);
            gtk_tree_path_free (path_cursor);
        }
    }

    /* tree: if removing the current location, let's move it */
    if (priv->is_tree && gtk_tree_selection_get_selected (
                gtk_tree_view_get_selection ((GtkTreeView *) tree), NULL, &it)
            && (itereq (iter, &it)
                /* also handles when location is on a (soon to be gone) children */
                || gtk_tree_store_is_ancestor (priv->store, iter, &it)))
        handle_removing_row (tree, iter, FALSE);

    /* now we can remove all children */
    if (priv->is_tree)
    {
        enum tree_expand es;
        GtkTreeIter child;

        /* if we were PARTIAL, set it to none so that removing children doesn't
         * result in adding a fake node */
        gtk_tree_model_get (model, iter,
                TREE_COL_EXPAND_STATE,  &es,
                -1);
        if (es == TREE_EXPAND_PARTIAL)
            set_es (priv, iter, TREE_EXPAND_NONE);

        /* get the parent, in case we're removing its last child */
        gtk_tree_model_iter_parent (model, &parent, iter);
        /* we need to remove all children before we remove the row, so we can
         * have said children processed correctly (through here) as well */
        if (gtk_tree_model_iter_children (model, &child, iter))
            while (remove_row_from_tree (tree, &child,
                        (removal == RR_IS_REMOVAL
                        /* we pretend it's a removal (node's item (e.g. file)
                         * deleted) when removing a root, so tree visuals are
                         * skipped.
                         * Makes sure it doesn't save them only so we can drop
                         * them right after */
                        || gtk_tree_store_iter_depth (priv->store, iter) == 0)
                        ? RR_IS_REMOVAL : removal))
                ;
    }

    /* remove all watched_iters to this row */
    prev = NULL;
    l = priv->watched_iters;
    while (l)
    {
        if (itereq (iter, (GtkTreeIter *) l->data))
        {
            GSList *next = l->next;

            if (prev)
                prev->next = next;
            else
                priv->watched_iters = next;

            g_slist_free_1 (l);
            l = next;
        }
        else
        {
            prev = l;
            l = l->next;
        }
    }

    /* for post-removal processing */
    it = *iter;

    /* now we can remove the row */
    DONNA_DEBUG (TREE_VIEW, priv->name,
            gchar *fl = (node) ? donna_node_get_full_location (node) : (gchar *) "-";
            g_debug2 ("TreeView '%s': remove row for '%s' (removal=%d)",
                priv->name, fl, removal);
            if (node)
                g_free (fl));
    ret = gtk_tree_store_remove (priv->store, iter);

    /* if there was a node, we have some extra work to do. We must do it now,
     * after removal, because otherwise there are all kinds of issues, since
     * we'll free & remove the iter from our hashtable & list of roots, but it's
     * needed e.g. for sorting reasons and whatnot.
     * Just remember: iter is either invalid or pointed to the next row, hence a
     * local copy in it. And that one doesn't link to a, actual row anymore */
    if (node)
    {
        GSList *list;

        if (is_root)
        {
            /* need to be prior removal, to ensure sorting remains valid until
             * the end */
            for (l = priv->roots; l; l = l->next)
                if (itereq (&it, (GtkTreeIter *) l->data))
                {
                    priv->roots = g_slist_delete_link (priv->roots, l);
                    break;
                }

            /* also means we need to clean tree_visuals for anything that
             * was under that root. Removing a root means forgetting any and
             * all tree visuals under there. */

            if (priv->tree_visuals)
            {
                struct ctv_data data = { .tree = tree, .iter = &it };

                g_hash_table_foreach_remove (priv->tree_visuals,
                        (GHRFunc) clean_tree_visuals, &data);
                if (g_hash_table_size (priv->tree_visuals) == 0)
                {
                    g_hash_table_unref (priv->tree_visuals);
                    priv->tree_visuals = NULL;
                }
            }
        }

        /* remove iter for that row from hashtable -- must be done after
         * everything needing the iter (from hashtable, which is also used in
         * priv->roots) is done, since it will be free-d */
        prev = NULL;
        if (priv->is_tree)
        {
            /* since we know there's at least one iter for this one, a lookup is
             * enough */
            l = list = g_hash_table_lookup (priv->hashtable, node);
            while (l)
            {
                if (itereq (&it, (GtkTreeIter *) l->data))
                {
                    if (prev)
                        prev->next = l->next;
                    else
                        list = l->next;

                    gtk_tree_iter_free (l->data);
                    g_slist_free_1 (l);
                    break;
                }
                else
                {
                    prev = l;
                    l = l->next;
                }
            }
            if (list)
                g_hash_table_insert (priv->hashtable, node, list);
            else
            {
                g_hash_table_remove (priv->hashtable, node);
                /* remove the ref from hashtable */
                g_object_unref (node);
            }
        }
        else
        {
            /* since we know there's an iter for this one, a lookup is enough */
            gtk_tree_iter_free (g_hash_table_lookup (priv->hashtable, node));
            if (removal == RR_IS_REMOVAL)
            {
                DONNA_DEBUG (TREE_VIEW, (tree)->priv->name,
                        gchar *fl = donna_node_get_full_location (node);
                        g_debug2 ("TreeView '%s': remove node '%s' from hashtable",
                            priv->name, fl);
                        g_free (fl));
                g_hash_table_remove (priv->hashtable, node);
                /* remove the ref from hashtable */
                g_object_unref (node);
            }
            else
                /* not visible anymore */
                g_hash_table_insert (priv->hashtable, node, NULL);
        }
    }

    /* we have a parent on tree, let's check/update its expand state */
    if (priv->is_tree && parent.stamp != 0)
    {
        enum tree_expand es;

        gtk_tree_model_get (model, &parent,
                TREE_COL_EXPAND_STATE,  &es,
                -1);

        if (!gtk_tree_model_iter_has_child (model, &parent))
        {
            if (es == TREE_EXPAND_PARTIAL || removal != RR_IS_REMOVAL)
            {
                es = TREE_EXPAND_UNKNOWN;
                /* add a fake row */
                gtk_tree_store_insert_with_values (priv->store, NULL, &parent, 0,
                        TREE_COL_NODE,  NULL,
                        -1);
            }
            else
                es = TREE_EXPAND_NONE;
            set_es (priv, &parent, es);
        }
        else
        {
            if (es == TREE_EXPAND_MAXI && removal == RR_NOT_REMOVAL)
            {
                es = TREE_EXPAND_PARTIAL;
                set_es (priv, &parent, es);
            }
        }
    }
    else if (!priv->is_tree
            && (!has_model_at_least_n_rows ((GtkTreeModel *) priv->store, 1)))
        set_draw_state (tree, (g_hash_table_size (priv->hashtable) == 0)
            ? DRAW_EMPTY : DRAW_NO_VISIBLE);

    if (!priv->filling_list)
        check_statuses (tree, STATUS_CHANGED_ON_CONTENT);
    return ret;
}

struct refresh_data
{
    DonnaTreeView   *tree;
    guint            started;
    guint            finished;
    gboolean         done;
};

/* when doing a refresh, we ask every node on tree (or every visible node for
 * DONNA_TREE_VIEW_REFRESH_VISIBLE) to refresh its set properties, and we then
 * get flooded by node-updated signals.
 * In a tree w/ 800 rows/nodes, that's 800 * nb_props, so even with only 6
 * properties (name, size, time, uid, gid, mode) that's 4 800 callbacks, which
 * is a lot.
 * And apparently the slow bit that might make the UI a bit unresponsive or make
 * it slow until the refresh "appears on screen" comes from the thousands of
 * calls to gtk_tree_model_get_path() (the path being needed to call
 * gtk_tree_model_row_changed).
 * So to try and make this a bit better/feel faster, we put refresh_on_hold
 * (i.e. all signals node-updated are no-op. We don't actually block them just
 * because I'm lazy, and in tree there can be plenty of providers/handlers to
 * block/unblock. Could be better though...) and simply trigger a redraw when
 * done, to refresh only the visible rows. Much better.
 * This is done using a refresh_data with the number of tasks started, all of
 * which having this callback to decrement the counter (under lock, ofc). After
 * all tasks have been started, this function is called with no task, to set the
 * flag done to TRUE. When done is TRUE & count == 0, it means everything has
 * been processed, we can trigger the refresh & free memory */
static void
refresh_node_cb (DonnaTask              *task,
                 gboolean                timeout_called,
                 struct refresh_data    *data)
{
    if (task)
        ++data->finished;
    else
        data->done = TRUE;
    if (data->done && data->finished == data->started)
    {
        data->tree->priv->refresh_on_hold = FALSE;
        resort_tree (data->tree);
        /* in case any name or size changed, since it was refresh_on_hold */
        check_statuses (data->tree, STATUS_CHANGED_ON_CONTENT);
        g_free (data);
    }
}

struct preload_props
{
    DonnaTreeView *tree;
    GPtrArray *props;
    GPtrArray *nodes;
};

static void
free_preload_props (gpointer data)
{
    struct preload_props *pp = data;

    g_ptr_array_unref (pp->props);
    g_ptr_array_unref (pp->nodes);
    g_free (pp);
}

static DonnaTaskState
preload_props_worker (DonnaTask *task, struct preload_props *pp)
{
    GPtrArray *tasks = NULL;
    guint i;

    for (i = 0; i < pp->nodes->len; ++i)
    {
        DonnaNode *node = pp->nodes->pdata[i];
        GPtrArray *props = NULL;
        guint j;

        if (donna_task_is_cancelling (task))
            /* XXX should we remember all started/running tasks, and cancel them
             * as well? */
            break;

        for (j = 0; j < pp->props->len; ++j)
        {
            gchar *prop = pp->props->pdata[j];
            DonnaNodeHasProp has;

            has = donna_node_has_property (node, prop);
            if ((has & DONNA_NODE_PROP_EXISTS) && !(has & DONNA_NODE_PROP_HAS_VALUE))
            {
                if (!props)
                    props = g_ptr_array_new ();
                g_ptr_array_add (props, prop);
            }
        }

        if (props)
        {
            GPtrArray *arr;

            arr = donna_node_refresh_arr_tasks_arr (node, tasks, props, NULL);
            if (G_UNLIKELY (!arr))
                continue;
            else if (!tasks)
                tasks = arr;

            for (j = 0; j < tasks->len; ++j)
                donna_app_run_task (pp->tree->priv->app, (DonnaTask *) tasks->pdata[j]);
            if (tasks->len > 0)
                g_ptr_array_remove_range (tasks, 0, tasks->len);
        }
    }

    g_object_set_data ((GObject *) pp->tree, DATA_PRELOAD_TASK, NULL);
    if (tasks)
        g_ptr_array_unref (tasks);
    free_preload_props (pp);
    return DONNA_TASK_DONE;
}

/* mode list only */
static void
preload_props_columns (DonnaTreeView *tree)
{
    GError *err = NULL;
    DonnaTreeViewPrivate *priv = tree->priv;
    DonnaRowId rid = { DONNA_ARG_TYPE_PATH, (gpointer) ":all" };
    DonnaTask *task;
    struct preload_props *pp;
    GPtrArray *props = NULL;
    GSList *l;

    if (G_UNLIKELY (g_object_get_data ((GObject *) tree, DATA_PRELOAD_TASK)))
        /* already a preloading task running */
        return;

    for (l = priv->columns; l; l = l->next)
    {
        struct column *_col = l->data;

        if (_col->refresh_properties == RP_PRELOAD)
        {
            guint i;

            if (!props)
                props = g_ptr_array_new_with_free_func (g_free);

            for (i = 0; i < priv->col_props->len; ++i)
            {
                struct col_prop *cp;

                cp = &g_array_index (priv->col_props, struct col_prop, i);
                if (cp->column != _col->column)
                    continue;

                g_ptr_array_add (props, g_strdup (cp->prop));
            }
        }
    }

    if (!props || props->len == 0)
    {
        if (props)
            g_ptr_array_unref (props);
        return;
    }

    pp = g_new (struct preload_props, 1);
    pp->tree  = tree;
    pp->props = props;
    /* this actually returns all nodes (not just non-visible ones), but it's
     * easier to do that way, and since their properties will be loaded already,
     * no refreshing will be triggered anyways */
    pp->nodes = donna_tree_view_get_nodes (tree, &rid, FALSE, &err);
    if (G_UNLIKELY (!pp->nodes))
    {
        g_warning ("TreeView '%s': Failed to preload ON_DEMAND columns, "
                "couldn't get nodes: %s",
                priv->name, err->message);
        g_clear_error (&err);
        g_ptr_array_unref (pp->props);
        g_free (pp);
        return;
    }

    task = donna_task_new ((task_fn) preload_props_worker, pp, free_preload_props);
    if (G_UNLIKELY (!task))
    {
        g_warning ("TreeView '%s': Failed to create task to preload ON_DEMAND columns",
                priv->name);
        free_preload_props (pp);
        return;
    }
    DONNA_DEBUG (TASK, NULL,
            donna_task_take_desc (task, g_strdup_printf ("TreeView '%s': "
                    "Preload %d properties for preload columns",
                    priv->name, pp->props->len)));
    DONNA_DEBUG (TREE_VIEW, priv->name,
            g_debug ("TreeView '%s': Starting task to preload %d properties on %d nodes",
                priv->name, pp->props->len, pp->nodes->len));

    g_object_set_data ((GObject *) tree, DATA_PRELOAD_TASK, task);
    donna_app_run_task (priv->app, task);
}

/* mode list only -- node *MUST* be in hashtable */
static gboolean
refilter_node (DonnaTreeView *tree, DonnaNode *node, GtkTreeIter *iter)
{
    DonnaTreeViewPrivate *priv = tree->priv;
    GtkTreeModel *model = (GtkTreeModel *) priv->store;
    gboolean is_visible;

    /* should it be visible */
    if (priv->show_hidden)
        is_visible = TRUE;
    else
    {
        gchar *name;

        name = donna_node_get_name (node);
        is_visible = (name && *name != '.');
        g_free (name);
    }

    if (is_visible && priv->filter
            && (!priv->vf_items_only || donna_node_get_node_type (node) == DONNA_NODE_ITEM))
        is_visible = donna_filter_is_match (priv->filter, node, tree);

    DONNA_DEBUG (TREE_VIEW, priv->name,
            gchar *fl = donna_node_get_full_location (node);
            g_debug3 ("TreeView '%s': refilter node '%s': %d -> %d",
                priv->name, fl, !!iter, is_visible);
            g_free (fl));

    if (!is_visible)
    {
        if (iter)
            /* will free the iter & set NULL in the hashtable */
            remove_row_from_tree (tree, iter, RR_NOT_REMOVAL);
    }
    else
    {
        if (!iter)
        {
            GtkTreeIter it;
            gboolean was_empty = FALSE;

            if (!priv->filling_list)
                was_empty = !gtk_tree_model_iter_children (model, &it, NULL);

            DONNA_DEBUG (TREE_VIEW, priv->name,
                    gchar *fl = donna_node_get_full_location (node);
                    g_debug2 ("TreeView '%s': add row for '%s'",
                        priv->name, fl);
                    g_free (fl));

            gtk_tree_store_insert_with_values (priv->store, &it, NULL, 0,
                    LIST_COL_NODE,  node,
                    -1);
            /* update hashtable */
            g_hash_table_insert (priv->hashtable, node, gtk_tree_iter_copy (&it));

            if (was_empty)
            {
                GtkTreePath *path;

                set_draw_state (tree, DRAW_NOTHING);
                path = gtk_tree_path_new_from_indices (0, -1);
                gtk_tree_view_set_focused_row ((GtkTreeView *) tree, path);
                gtk_tree_path_free (path);
            }
            if (!priv->filling_list)
                check_statuses (tree, STATUS_CHANGED_ON_CONTENT);
        }
        else
            return TRUE;
    }

    return FALSE;
}

/* mode list only */
static void
refilter_list (DonnaTreeView *tree)
{
    DonnaTreeViewPrivate *priv = tree->priv;
    GHashTableIter ht_it;
    DonnaNode *node;
    GtkTreeIter *iter;
    GtkTreeSortable *sortable = (GtkTreeSortable *) priv->store;
    gint sort_col_id;
    GtkSortType order;

    DONNA_DEBUG (TREE_VIEW, priv->name,
            g_debug2 ("TreeView '%s': refiltering list", priv->name));

    /* adding items to a sorted store is quite slow; and since we might be
     * adding/removing lots of items here (e.g. applying/removing a VF) we'll
     * get much better performance by adding all items to an unsorted store, and
     * then sorting it */
    gtk_tree_sortable_get_sort_column_id (sortable, &sort_col_id, &order);
    gtk_tree_sortable_set_sort_column_id (sortable,
            GTK_TREE_SORTABLE_UNSORTED_SORT_COLUMN_ID, order);

    /* filling_list to avoid update of statuses on each add/remove of row */
    priv->filling_list = TRUE;
    g_hash_table_iter_init (&ht_it, priv->hashtable);
    while (g_hash_table_iter_next (&ht_it, (gpointer) &node, (gpointer) &iter))
        refilter_node (tree, node, iter);
    priv->filling_list = FALSE;

    gtk_tree_sortable_set_sort_column_id (sortable, sort_col_id, order);

    refresh_draw_state (tree);
    check_statuses (tree, STATUS_CHANGED_ON_CONTENT);
    preload_props_columns (tree);
}

static gboolean may_get_children_refresh (DonnaTreeView *tree, GtkTreeIter *iter);

static void
set_children (DonnaTreeView *tree,
              GtkTreeIter   *iter,
              DonnaNodeType  node_types,
              GPtrArray     *children,
              gboolean       expand,
              gboolean       refresh)
{
    DonnaTreeViewPrivate *priv = tree->priv;
    GtkTreeModel *model = (GtkTreeModel *) priv->store;
    gboolean is_match;

    is_match = (node_types & priv->node_types) == priv->node_types;

#ifdef GTK_IS_JJK
    /* list: make sure we don't try to perform a rubber band on two different
     * content, as that would be very likely to segfault in GTK, in addition to
     * be quite unexpected at best */
    if (!priv->is_tree
            && gtk_tree_view_is_rubber_banding_pending ((GtkTreeView *) tree, TRUE))
        gtk_tree_view_stop_rubber_banding ((GtkTreeView *) tree, FALSE);
#endif

    if (children->len == 0)
    {
        GtkTreeIter child;

        if (priv->is_tree)
        {
            /* set new expand state */
            set_es (priv, iter, TREE_EXPAND_NONE);
            if (gtk_tree_model_iter_children (model, &child, iter))
            {
                if (is_match)
                    while (remove_row_from_tree (tree, &child, RR_IS_REMOVAL))
                        ;
                else
                {
                    for (;;)
                    {
                        DonnaNode *node;
                        gboolean ret;

                        gtk_tree_model_get (model, &child,
                                TREE_COL_NODE,  &node,
                                -1);
                        if (node && donna_node_get_node_type (node) & node_types)
                            ret = remove_row_from_tree (tree, &child, RR_IS_REMOVAL);
                        else
                            ret = gtk_tree_model_iter_next (model, &child);
                        g_object_unref (node);

                        if (!ret)
                            break;
                    }
                }
            }
        }
        else
        {
            GHashTableIter ht_it;
            GtkTreeIter *it;
            DonnaNode *node;

            if (is_match)
            {
                DonnaRowId rid = { DONNA_ARG_TYPE_PATH, (gpointer) ":last" };
                /* clear the list (see selection_changed_cb() for why
                 * filling_list) */
                priv->filling_list = TRUE;
                /* speed up -- see change_location() for why */
                donna_tree_view_set_focus (tree, &rid, NULL);
                gtk_tree_store_clear (priv->store);
                priv->filling_list = FALSE;
                /* also the hashtable. First we need to free the iters & unref
                 * the nodes, then actually clear it */
                g_hash_table_iter_init (&ht_it, priv->hashtable);
                while (g_hash_table_iter_next (&ht_it, (gpointer) &node, (gpointer) &it))
                {
                    if (it)
                        gtk_tree_iter_free (it);
                    g_object_unref (node);
                }
                g_hash_table_remove_all (priv->hashtable);

                /* show the "location empty" message */
                set_draw_state (tree, DRAW_EMPTY);
            }
            else if (gtk_tree_model_iter_children (model, &child, NULL))
            {
                for (;;)
                {
                    gboolean ret;

                    gtk_tree_model_get (model, &child,
                            TREE_COL_NODE,  &node,
                            -1);
                    if (donna_node_get_node_type (node) & node_types)
                        ret = remove_row_from_tree (tree, &child, RR_IS_REMOVAL);
                    else
                        ret = gtk_tree_model_iter_next (model, &child);
                    g_object_unref (node);

                    if (!ret)
                        break;
                }
            }
        }
    }
    else if (priv->is_tree)
    {
        GSList *list = NULL;
        enum tree_expand es;
        guint i;
        gboolean has_children = FALSE;

        /* for trees, this is only called if either we want to become MAXI (from
         * NEVER, UNKNOWN, PARTIAL or MAXI) or from a node-children signal, to
         * refresh a MAXI. In the later case, it might not be a match. */

        gtk_tree_model_get (model, iter,
                TREE_COL_EXPAND_STATE,  &es,
                -1);

        if (es == TREE_EXPAND_MAXI || es == TREE_EXPAND_PARTIAL)
        {
            GtkTreeIter it;

            if (is_match
                    && gtk_tree_model_iter_children (model, &it, iter))
            {
                do
                {
                    list = g_slist_prepend (list, gtk_tree_iter_copy (&it));
                } while (gtk_tree_model_iter_next (model, &it));
                list = g_slist_reverse (list);
            }
        }
        else
            es = TREE_EXPAND_UNKNOWN; /* == 0 */

        /* set new es now, so any call to remove_row_from_tree() can do things
         * properly should we remove the last row */
        set_es (priv, iter, TREE_EXPAND_MAXI);

        for (i = 0; i < children->len; ++i)
        {
            GtkTreeIter row;
            DonnaNode *node = children->pdata[i];
            gboolean skip = FALSE;

            /* in case we got children from a node_children signal, and there's
             * more types that we care for */
            if (!(donna_node_get_node_type (node) & priv->node_types))
                continue;

            if (!priv->show_hidden)
            {
                GtkTreeIter *_iter;
                gchar *name;

                name = donna_node_get_name (node);
                skip = (name && *name == '.');
                g_free (name);

                /* we still need to fill row in case it was in the tree (added
                 * manually despite the show_hidden option) */
                _iter = get_child_iter_for_node (tree, iter, node);
                if (_iter)
                    row = *_iter;
                else
                    row.stamp = 0;
            }

            /* add_node_to_tree_filtered() will return FALSE on error (should
             * really never happened) or if we don't show it (show_hidden) */
            if (skip || !add_node_to_tree_filtered (tree, iter, node, &row))
                continue;

            if (es && row.stamp != 0)
            {
                GSList *l, *prev = NULL;

                if (refresh)
                    may_get_children_refresh (tree, &row);

                /* remove the iter for that row */
                l = list;
                while (l)
                {
                    if (itereq ((GtkTreeIter *) l->data, &row))
                    {
                        if (prev)
                            prev->next = l->next;
                        else
                            list = l->next;

                        gtk_tree_iter_free (l->data);
                        g_slist_free_1 (l);
                        break;
                    }
                    prev = l;
                    l = prev->next;
                }
            }
            has_children = TRUE;
        }
        /* remove rows not in children */
        while (list)
        {
            GSList *l;

            l = list;
            remove_row_from_tree (tree, l->data, RR_IS_REMOVAL);
            gtk_tree_iter_free (l->data);
            list = l->next;
            g_slist_free_1 (l);
        }

        if (has_children && expand)
        {
            GtkTreePath *path;

            /* and make sure the row gets expanded (since we "blocked" it
             * when clicked */
            path = gtk_tree_model_get_path (model, iter);
            gtk_tree_view_expand_row ((GtkTreeView *) tree, path, FALSE);
            gtk_tree_path_free (path);
        }
    }
    else
    {
        GtkTreeSortable *sortable = (GtkTreeSortable *) priv->store;
        gint sort_col_id;
        GtkSortType order;

        struct refresh_data *rd = NULL;
        GPtrArray *tasks = NULL;
        GPtrArray *props = NULL;
        GHashTableIter ht_it;
        GSList *list = NULL;
        DonnaNode *node;
        guint i;

        if (is_match)
        {
            g_hash_table_iter_init (&ht_it, priv->hashtable);
            while (g_hash_table_iter_next (&ht_it, (gpointer) &node, NULL))
                list = g_slist_prepend (list, node);
        }

        if (refresh)
        {
            /* see refresh_node_cb() for more about this */
            rd = g_new0 (struct refresh_data, 1);
            rd->tree = tree;
            priv->refresh_on_hold = TRUE;

            props = g_ptr_array_sized_new (priv->col_props->len);
            for (i = 0; i < priv->col_props->len; ++i)
            {
                struct col_prop *cp;

                cp = &g_array_index (priv->col_props, struct col_prop, i);
                if (get_column_by_column (tree, cp->column)->refresh_properties
                        != RP_ON_DEMAND)
                    /* do not refresh properties for ON_DEMAND columns, to
                     * refresh/load them see
                     * donna_tree_view_column_refresh_nodes() */
                    g_ptr_array_add (props, cp->prop);
            }
        }

        /* adding items to a sorted store is quite slow; we get much better
         * performance by adding all items to an unsorted store, and then
         * sorting it */
        gtk_tree_sortable_get_sort_column_id (sortable, &sort_col_id, &order);
        gtk_tree_sortable_set_sort_column_id (sortable,
                GTK_TREE_SORTABLE_UNSORTED_SORT_COLUMN_ID, order);
        priv->filling_list = TRUE;

        for (i = 0; i < children->len; ++i)
        {
            node = children->pdata[i];

            /* in case we got children from a node_children signal, and there's
             * more types that we care for */
            if (!(donna_node_get_node_type (node) & priv->node_types))
                continue;

            /* make sure it's in the hashmap (adding it if not) & get the iter
             * (if row is visible) */
            if (g_hash_table_lookup_extended (priv->hashtable, node,
                        NULL, (gpointer) &iter))
            {
                GSList *l, *prev = NULL;

                if (refresh && refilter_node (tree, node, iter))
                {
                    GPtrArray *arr;

                    /* donna_node_refresh_arr_tasks_arr() will unref props, but
                     * we want to keep it alive */
                    g_ptr_array_ref (props);
                    arr = donna_node_refresh_arr_tasks_arr (node, tasks, props, NULL);
                    if (G_LIKELY (arr))
                    {
                        guint j;

                        if (!tasks)
                            /* in case the array was created */
                            tasks = arr;

                        rd->started += tasks->len;
                        for (j = 0; j < tasks->len; ++j)
                        {
                            DonnaTask *task = tasks->pdata[j];
                            donna_task_set_callback (task,
                                    (task_callback_fn) refresh_node_cb, rd, NULL);
                            donna_app_run_task (priv->app, task);
                        }
                        if (tasks->len > 0)
                            g_ptr_array_remove_range (tasks, 0, tasks->len);
                    }
                }

                l = list;
                while (l)
                {
                    if ((DonnaNode *) l->data == node)
                    {
                        if (prev)
                            prev->next = l->next;
                        else
                            list = l->next;

                        g_slist_free_1 (l);
                        break;
                    }
                    prev = l;
                    l = prev->next;
                }
            }
            else
                add_node_to_list (tree, node, TRUE);
        }
        /* remove nodes not in children */
        while (list)
        {
            GSList *l;

            l = list;
            iter = g_hash_table_lookup (priv->hashtable, l->data);
            remove_node_from_list (tree, l->data, iter);
            list = l->next;
            g_slist_free_1 (l);
        }

        /* restore sort */
        gtk_tree_sortable_set_sort_column_id (sortable, sort_col_id, order);
        priv->filling_list = FALSE;
        /* do it ourself because we prevented it w/ priv->filling_list */
        check_statuses (tree, STATUS_CHANGED_ON_CONTENT);

        if (refresh)
        {
            if (tasks)
                g_ptr_array_unref (tasks);
            g_ptr_array_unref (props);
            refresh_node_cb (NULL, FALSE, rd);
        }

        refresh_draw_state (tree);
        preload_props_columns (tree);
    }
}

/* mode tree only */
static void
node_get_children_tree_cb (DonnaTask                   *task,
                           gboolean                     timeout_called,
                           struct node_children_data   *data)
{
    if (!is_watched_iter_valid (data->tree, &data->iter, TRUE))
    {
        free_node_children_data (data);
        return;
    }

    if (donna_task_get_state (task) != DONNA_TASK_DONE)
    {
        GtkTreeModel      *model;
        GtkTreePath       *path;
        DonnaNode         *node;
        gchar             *location;
        const GError      *error;

        /* collapse the node & set it to UNKNOWN (it might have been NEVER
         * before, but we don't know) so if the user tries an expansion again,
         * it is tried again. */
        model = GTK_TREE_MODEL (data->tree->priv->store);
        path = gtk_tree_model_get_path (GTK_TREE_MODEL (data->tree->priv->store),
                &data->iter);
        gtk_tree_view_collapse_row (GTK_TREE_VIEW (data->tree), path);
        gtk_tree_path_free (path);
        set_es (data->tree->priv, &data->iter, TREE_EXPAND_UNKNOWN);

        /* explain ourself */
        gtk_tree_model_get (model, &data->iter,
                TREE_COL_NODE,  &node,
                -1);
        error = donna_task_get_error (task);
        location = donna_node_get_location (node);
        donna_app_show_error (data->tree->priv->app, error,
                "TreeView '%s': Failed to get children for node '%s:%s'",
                data->tree->priv->name,
                donna_node_get_domain (node),
                location);
        g_free (location);
        g_object_unref (node);

        free_node_children_data (data);
        return;
    }

    set_children (data->tree, &data->iter, data->node_types,
            g_value_get_boxed (donna_task_get_return_value (task)),
            /* expand row: only if asked, and the timeout hasn't been called. If
             * it has, either the row is already expanded (so we're good) or the
             * user closed it (when it had the fake/"please wait" node) and we
             * shoudln't force it back open */
            data->expand_row && !timeout_called,
            FALSE /* no refresh */);

    if (data->scroll_to_current)
        scroll_to_current (data->tree);

    /* for check_children_post_expand() or full_expand_children() */
    if (data->extra_callback)
        data->extra_callback (data->tree, &data->iter);

    free_node_children_data (data);
}

/* mode tree only */
static gboolean
expand_row (DonnaTreeView           *tree,
            GtkTreeIter             *iter,
            gboolean                 expand_row,
            gboolean                 scroll_current,
            node_children_extra_cb   extra_callback)
{
    DonnaTreeViewPrivate *priv = tree->priv;
    GtkTreeModel *model = GTK_TREE_MODEL (priv->store);
    GPtrArray *arr = NULL;
    DonnaNode *node;
    DonnaTask *task;
    struct node_children_data *data;
    GSList *list;

    gtk_tree_model_get (model, iter,
            TREE_COL_NODE,  &node,
            -1);
    if (!node)
    {
        g_warning ("TreeView '%s': expand_row() failed to get node from model",
                priv->name);
        return FALSE;
    }

    /* is there another tree node for this node? */
    list = g_hash_table_lookup (priv->hashtable, node);
    if (list)
    {
        for ( ; list; list = list->next)
        {
            GtkTreeIter *i = list->data;
            enum tree_expand es;

            /* skip ourself */
            if (itereq (iter, i))
                continue;

            gtk_tree_model_get (model, i,
                    TREE_COL_EXPAND_STATE,  &es,
                    -1);
            if (es == TREE_EXPAND_MAXI)
            {
                GtkTreeIter child;

                /* let's import the children */

                if (!gtk_tree_model_iter_children (model, &child, i))
                {
                    g_critical ("TreeView '%s': Inconsistency detected",
                            priv->name);
                    continue;
                }

                arr = g_ptr_array_new_with_free_func (g_object_unref);
                do
                {
                    DonnaNode *n;

                    gtk_tree_model_get (model, &child,
                            TREE_COL_NODE,  &n,
                            -1);
                    g_ptr_array_add (arr, n);
                } while (gtk_tree_model_iter_next (model, &child));

                break;
            }
        }
    }

    /* can we get them from our sync_with list ? */
    if (!arr && node == priv->location && priv->sync_with)
        arr = donna_tree_view_get_children (priv->sync_with, node, priv->node_types);

    if (arr)
    {
        set_children (tree, iter, priv->node_types, arr, expand_row, FALSE);
        g_ptr_array_unref (arr);

        if (scroll_current)
            scroll_to_current (tree);

        if (extra_callback)
            extra_callback (tree, iter);

        return TRUE;
    }

    task = donna_node_get_children_task (node, priv->node_types, NULL);

    data = g_slice_new0 (struct node_children_data);
    data->tree = tree;
    data->node_types = priv->node_types;
    data->expand_row = expand_row;
    data->scroll_to_current = scroll_current;
    data->iter = *iter;
    watch_iter (tree, &data->iter);
    data->extra_callback = extra_callback;

    if (expand_row)
        /* FIXME: timeout_delay must be an option */
        donna_task_set_timeout (task, 800,
                (task_timeout_fn) node_get_children_tree_timeout,
                data,
                NULL);
    donna_task_set_callback (task,
            (task_callback_fn) node_get_children_tree_cb,
            data,
            (GDestroyNotify) free_node_children_data);

    set_es (priv, &data->iter, TREE_EXPAND_WIP);

    donna_app_run_task (priv->app, task);
    g_object_unref (node);
    return FALSE;
}

/* mini-tree only */
static gboolean
maxi_expand_row (DonnaTreeView  *tree,
                 GtkTreeIter    *iter)
{
    DonnaTreeViewPrivate *priv = tree->priv;
    GtkTreeModel *model = (GtkTreeModel *) priv->store;
    enum tree_expand es;

    gtk_tree_model_get (model, iter,
            TREE_COL_EXPAND_STATE,  &es,
            -1);
    if (es != TREE_EXPAND_PARTIAL)
    {
        GtkTreePath *path;
        gboolean ret;

        path = gtk_tree_model_get_path (model, iter);
        ret = !gtk_tree_view_row_expanded ((GtkTreeView *) tree, path);
        if (ret)
            gtk_tree_view_expand_row ((GtkTreeView *) tree, path, FALSE);
        gtk_tree_path_free (path);
        return ret;
    }

    expand_row (tree, iter, TRUE /* expand */, FALSE /* scroll to current */,
            /* if we're not "in sync" with our list (i.e. there's no row
             * for it) we attach the extra callback to check for it once
             * children will have been added.
             * We also have the check run on every row-expanded, but
             * this is still needed because the row could be expanded to
             * only show the "fake/please wait" node... */
            (!priv->location && priv->sync_with)
            ? (node_children_extra_cb) check_children_post_expand
            : NULL);
    return TRUE;
}

/* tree only */
static gboolean
maxi_collapse_row (DonnaTreeView    *tree,
                   GtkTreeIter      *iter)
{
    DonnaTreeViewPrivate *priv = tree->priv;
    GtkTreeModel *model = (GtkTreeModel *) priv->store;
    enum tree_expand es;
    GtkTreePath *path;
    gboolean ret;

    path = gtk_tree_model_get_path (model, iter);
    ret = gtk_tree_view_row_expanded ((GtkTreeView *) tree, path);
    if (ret)
        gtk_tree_view_collapse_row ((GtkTreeView *) tree, path);
    gtk_tree_path_free (path);

    gtk_tree_model_get (model, iter,
            TREE_COL_EXPAND_STATE,  &es,
            -1);
    if (es == TREE_EXPAND_PARTIAL || es == TREE_EXPAND_MAXI)
    {
        GtkTreeIter it;

        /* remove all children */
        if (gtk_tree_model_iter_children (model, &it, iter))
            while (remove_row_from_tree (tree, &it, RR_NOT_REMOVAL))
                ;
    }

    return ret;
}

static gboolean
donna_tree_view_test_collapse_row (GtkTreeView    *treev,
                                   GtkTreeIter    *iter,
                                   GtkTreePath    *path)
{
    DonnaTreeView *tree = DONNA_TREE_VIEW (treev);
    DonnaTreeViewPrivate *priv = tree->priv;
    GtkTreePath *p;
    GtkTreeSelection *sel;
    GtkTreeIter sel_iter;

    if (!priv->is_tree)
        /* no collapse */
        return TRUE;

    /* if the focused row is somewhere down, we need to move it up before the
     * collapse, to avoid GTK's set_cursor() */
    gtk_tree_view_get_cursor (treev, &p, NULL);
    if (p && gtk_tree_path_is_ancestor (path, p))
        gtk_tree_view_set_focused_row (treev, path);
    if (p)
        gtk_tree_path_free (p);

    /* if the current row (i.e. selected path) is somewhere down, let's change
     * the selection now so we can change the selection, without changing the
     * focus */
    sel = gtk_tree_view_get_selection (treev);
    if (gtk_tree_selection_get_selected (sel, NULL, &sel_iter))
    {
        p = gtk_tree_model_get_path ((GtkTreeModel *) priv->store, &sel_iter);
        if (p)
        {
            if (gtk_tree_path_is_ancestor (path, p))
                gtk_tree_selection_select_path (sel, path);
            gtk_tree_path_free (p);
        }
    }

    /* collapse */
    return FALSE;
}

static gboolean
donna_tree_view_test_expand_row (GtkTreeView    *treev,
                                 GtkTreeIter    *iter,
                                 GtkTreePath    *path)
{
    DonnaTreeView *tree = DONNA_TREE_VIEW (treev);
    DonnaTreeViewPrivate *priv = tree->priv;
    enum tree_expand expand_state;

    if (!priv->is_tree)
        /* no expansion */
        return TRUE;

    gtk_tree_model_get (GTK_TREE_MODEL (priv->store), iter,
            TREE_COL_EXPAND_STATE,  &expand_state,
            -1);
    switch (expand_state)
    {
        /* allow expansion */
        case TREE_EXPAND_WIP:
        case TREE_EXPAND_PARTIAL:
        case TREE_EXPAND_MAXI:
            return FALSE;

        /* refuse expansion, import_children or get_children */
        case TREE_EXPAND_UNKNOWN:
        case TREE_EXPAND_NEVER:
            /* this will add an idle source import_children, or start a new task
             * get_children */
            expand_row (tree, iter,
                    /* expand row */
                    TRUE,
                    /* scroll to current */
                    FALSE,
                    /* if we're not "in sync" with our list (i.e. there's no row
                     * for it) we attach the extra callback to check for it once
                     * children will have been added.
                     * We also have the check run on every row-expanded, but
                     * this is still needed because the row could be expanded to
                     * only show the "fake/please wait" node... */
                    (!priv->location && priv->sync_with)
                    ? (node_children_extra_cb) check_children_post_expand
                    : NULL);
            return TRUE;

        /* refuse expansion. This case should never happen */
        case TREE_EXPAND_NONE:
            g_critical ("TreeView '%s' wanted to expand a node without children",
                    priv->name);
            return TRUE;
    }
    /* should never be reached */
    g_critical ("TreeView '%s': invalid expand state: %d",
            priv->name,
            expand_state);
    return FALSE;
}

/* mode tree only -- assumes that list don't have expander */
static void
donna_tree_view_row_collapsed (GtkTreeView   *treev,
                               GtkTreeIter   *iter,
                               GtkTreePath   *path)
{
    /* this node was collapsed, update the flag */
    gtk_tree_store_set (((DonnaTreeView *) treev)->priv->store, iter,
            TREE_COL_EXPAND_FLAG,   FALSE,
            -1);
    /* After row is collapsed, there might still be an horizontal scrollbar,
     * because the column has been enlarged due to a long-ass children, and
     * it hasn't been resized since. So even though there's no need for the
     * scrollbar anymore, it remains there.
     * Since we only have one column, we trigger an autosize to get rid of the
     * horizontal scrollbar (or adjust its size) */
    if (((DonnaTreeView *) treev)->priv->is_tree)
        gtk_tree_view_columns_autosize (treev);
}

static void
donna_tree_view_row_expanded (GtkTreeView   *treev,
                              GtkTreeIter   *iter,
                              GtkTreePath   *path)
{
    DonnaTreeView *tree = (DonnaTreeView *) treev;
    DonnaTreeViewPrivate * priv = tree->priv;
    GtkTreeIter child;

    /* this node was expanded, update the flag */
    gtk_tree_store_set (((DonnaTreeView *) treev)->priv->store, iter,
            TREE_COL_EXPAND_FLAG,   TRUE,
            -1);
    /* also go through all its children and expand them if the flag is set, tjus
     * restoring the previous expand state. This expansion will trigger a new
     * call to this very function, thus taking care of the recursion */
    if (gtk_tree_model_iter_children ((GtkTreeModel *) priv->store, &child, iter))
    {
        GtkTreeModel *model = (GtkTreeModel *) priv->store;
        do
        {
            gboolean expand_flag;

            gtk_tree_model_get (model, &child,
                    TREE_COL_EXPAND_FLAG,   &expand_flag,
                    -1);
            if (expand_flag)
            {
                GtkTreePath *p = gtk_tree_model_get_path (model, &child);
                gtk_tree_view_expand_row (treev, p, FALSE);
                gtk_tree_path_free (p);
            }
        } while (gtk_tree_model_iter_next (model, &child));
    }

    if (priv->is_tree && !priv->location && priv->sync_with)
        check_children_post_expand (tree, iter);
}

struct refresh_node_props_data
{
    DonnaTreeView *tree;
    DonnaNode     *node;
    GPtrArray     *props;
};

static void
free_refresh_node_props_data (struct refresh_node_props_data *data)
{
    DonnaTreeViewPrivate *priv = data->tree->priv;

    g_mutex_lock (&priv->refresh_node_props_mutex);
    priv->refresh_node_props = g_slist_remove (priv->refresh_node_props, data);
    g_mutex_unlock (&priv->refresh_node_props_mutex);

    g_object_unref (data->node);
    g_ptr_array_unref (data->props);
    g_free (data);
}

/* Usually, upon a provider's node-updated signal, we check if the node is in
 * the tree, and if the property is one that our columns use; If so, we trigger
 * a refresh of that row (i.e. trigger a row-updated on store)
 * However, there's an exception: a columntype can, on render, give a list of
 * properties to be refreshed. We then store those properties on
 * priv->refresh_node_props as we run a task to refresh them. During that time,
 * those properties (on that node) will *not* trigger a refresh, as they usually
 * would. Instead, it's only when this callback is triggered that, if *all*
 * properties were refreshed, the refresh will be triggered (on the tree) */
static void
refresh_node_prop_cb (DonnaTask                      *task,
                      gboolean                        timeout_called,
                      struct refresh_node_props_data *data)
{
    if (donna_task_get_state (task) == DONNA_TASK_DONE)
    {
        /* no return value means all props were refreshed, i.e. full success */
        if (!donna_task_get_return_value (task))
        {
            DonnaTreeViewPrivate *priv = data->tree->priv;
            GtkTreeModel *model;

            model = (GtkTreeModel *) data->tree->priv->store;
            if (priv->is_tree)
            {
                GSList *list;

                list = g_hash_table_lookup (priv->hashtable, data->node);
                if (!list)
                    goto bail;

                for ( ; list; list = list->next)
                {
                    GtkTreeIter *iter = list->data;
                    GtkTreePath *path;

                    path = gtk_tree_model_get_path (model, iter);
                    gtk_tree_model_row_changed (model, path, iter);
                    gtk_tree_path_free (path);
                }
            }
            else
            {
                GtkTreeIter *iter;

                if (!g_hash_table_lookup_extended (priv->hashtable, data->node,
                            NULL, (gpointer) &iter))
                    goto bail;
                if (refilter_node (data->tree, data->node, iter))
                {
                    GtkTreePath *path;
                    path = gtk_tree_model_get_path (model, iter);
                    gtk_tree_model_row_changed (model, path, iter);
                    gtk_tree_path_free (path);
                }
            }
        }
    }
bail:
    free_refresh_node_props_data (data);
}

static gboolean
spinner_fn (DonnaTreeView *tree)
{
    DonnaTreeViewPrivate *priv = tree->priv;
    GtkTreeModel *model;
    guint i;
    gboolean active = FALSE;

    if (!priv->active_spinners_id)
        return FALSE;

    if (!priv->active_spinners->len)
    {
        g_source_remove (priv->active_spinners_id);
        priv->active_spinners_id = 0;
        priv->active_spinners_pulse = 0;
        return FALSE;
    }

    model = GTK_TREE_MODEL (priv->store);
    for (i = 0; i < priv->active_spinners->len; ++i)
    {
        struct active_spinners *as = priv->active_spinners->pdata[i];
        GSList *list;
        guint j;
        gboolean refresh;

        refresh = FALSE;
        for (j = 0; j < as->as_cols->len; ++j)
        {
            struct as_col *as_col;

            as_col = &g_array_index (as->as_cols, struct as_col, j);
            if (as_col->nb > 0)
            {
                active = refresh = TRUE;
                break;
            }
        }
        if (!refresh)
            continue;

        if (priv->is_tree)
        {
            list = g_hash_table_lookup (priv->hashtable, as->node);
            for ( ; list; list = list->next)
            {
                GtkTreeIter *iter = list->data;
                GtkTreePath *path;

                path = gtk_tree_model_get_path (model, iter);
                gtk_tree_model_row_changed (model, path, iter);
                gtk_tree_path_free (path);
            }
        }
        else
        {
            GtkTreeIter *iter;
            GtkTreePath *path;

            /* since it's an as we can assume that (a) it is in hashtable, and
             * (b) it has an iter */
            iter = g_hash_table_lookup (priv->hashtable, as->node);
            if (iter)
            {
                path = gtk_tree_model_get_path (model, iter);
                gtk_tree_model_row_changed (model, path, iter);
                gtk_tree_path_free (path);
            }
        }
    }

    if (!active)
    {
        /* there are active spinners only for error messages */
        g_source_remove (priv->active_spinners_id);
        priv->active_spinners_id = 0;
        priv->active_spinners_pulse = 0;
        return FALSE;
    }

    ++priv->active_spinners_pulse;
    return TRUE;
}

static gboolean
is_col_node_need_refresh (DonnaTreeView *tree,
                          struct column *_col,
                          DonnaNode     *node)
{
    DonnaTreeViewPrivate *priv = tree->priv;
    guint i;

    for (i = 0; i < priv->col_props->len; ++i)
    {
        struct col_prop *cp;

        cp = &g_array_index (priv->col_props, struct col_prop, i);
        if (cp->column == _col->column)
        {
            DonnaNodeHasProp has;

            has = donna_node_has_property (node, cp->prop);
            if ((has & DONNA_NODE_PROP_EXISTS) && !(has & DONNA_NODE_PROP_HAS_VALUE))
                return TRUE;
        }
    }
    return FALSE;
}

gboolean
_donna_tree_view_get_ct_data (const gchar   *col_name,
                              DonnaNode     *node,
                              gpointer      *ctdata,
                              DonnaTreeView *tree)
{
    struct column *_col;
    struct column_filter *cf;
    GSList *l;

    /* since the col_name comes from user input, we could fail to find the
     * column in this case */
    _col = get_column_by_name (tree, col_name);
    if (_col)
    {
        if (_col->refresh_properties == RP_ON_DEMAND
                && is_col_node_need_refresh (tree, _col, node))
            return FALSE;
        *ctdata = _col->ct_data;
        return TRUE;
    }
    /* this means it's a column not loaded/used in tree. But, we know it does
     * exist (because filter has the ct) so we need to get it & load a ctdata,
     * if we haven't already */
    for (l = tree->priv->columns_filter; l; l = l->next)
        if (streq (((struct column_filter *) l->data)->name, col_name))
            break;

    if (l)
        cf = l->data;
    else
    {
        DonnaTreeViewPrivate *priv = tree->priv;
        DonnaConfig *config = donna_app_peek_config (priv->app);
        gchar *col_type = NULL;

        cf = g_new0 (struct column_filter, 1);
        cf->name = g_strdup (col_name);
        cf->refresh_properties = (guint) donna_config_get_int_column (config,
                col_name,
                (priv->arrangement) ? priv->arrangement->columns_options : NULL,
                priv->name,
                priv->is_tree,
                NULL,
                "refresh_properties", RP_VISIBLE);
        donna_config_get_string (donna_app_peek_config (priv->app), NULL,
                &col_type, "defaults/%s/columns/%s/type",
                (priv->is_tree) ? "trees" : "lists", col_name);
        cf->ct   = donna_app_get_column_type (priv->app,
                (col_type) ? col_type : col_name);
        g_free (col_type);
        donna_column_type_refresh_data (cf->ct, col_name,
                priv->arrangement->columns_options, priv->name, priv->is_tree,
                &cf->ct_data);
        priv->columns_filter = g_slist_prepend (priv->columns_filter, cf);
    }

    if (cf->refresh_properties == RP_ON_DEMAND)
    {
        GPtrArray *props;
        guint i;

        props = donna_column_type_get_props (cf->ct, cf->ct_data);
        if (G_UNLIKELY (!props))
            return FALSE;

        for (i = 0; i < props->len; ++i)
        {
            DonnaNodeHasProp has;

            has = donna_node_has_property (node, (gchar *) props->pdata[i]);
            if ((has & DONNA_NODE_PROP_EXISTS) && !(has & DONNA_NODE_PROP_HAS_VALUE))
            {
                g_ptr_array_unref (props);
                return FALSE;
            }
        }
        g_ptr_array_unref (props);
    }

    *ctdata = cf->ct_data;
    return TRUE;
}

static void
apply_color_filters (DonnaTreeView      *tree,
                     GtkTreeViewColumn  *column,
                     GtkCellRenderer    *renderer,
                     DonnaNode          *node)
{
    DonnaTreeViewPrivate *priv = tree->priv;
    GError *err = NULL;
    gboolean visible;
    GSList *l;

    if (!g_type_is_a (G_TYPE_FROM_INSTANCE (renderer), GTK_TYPE_CELL_RENDERER_TEXT))
        return;

    g_object_get (renderer, "visible", &visible, NULL);
    if (!visible)
        return;

    if (!(priv->arrangement->flags & DONNA_ARRANGEMENT_HAS_COLOR_FILTERS))
        return;

    for (l = priv->arrangement->color_filters; l; )
    {
        DonnaColorFilter *cf = l->data;
        gboolean keep_going;

        if (donna_color_filter_apply_if_match (cf, (GObject *) renderer,
                    get_column_by_column (tree, column)->name,
                    node, tree, &keep_going, &err))
        {
            if (!keep_going)
                break;
        }
        else if (err)
        {
            GSList *ll;
            gchar *filter;

            /* remove color filter */
            g_object_get (cf, "filter", &filter, NULL);
            donna_app_show_error (priv->app, err,
                    "Ignoring color filter '%s'", filter);
            g_free (filter);
            g_clear_error (&err);

            ll = l;
            l = l->next;
            priv->arrangement->color_filters = g_slist_delete_link (
                    priv->arrangement->color_filters, ll);
            continue;
        }

        l = l->next;
    }
}

/* Because the same renderers are used on all columns, we need to reset their
 * properties so they don't "leak" to other columns. If we used a model, every
 * row would have a foobar-set to TRUE or FALSE accordingly.
 * But we don't, and not all column types will set the same properties, also we
 * have things like color filters that also may set some.
 * So we need to reset whatever what set last time a renderer was used. An easy
 * way would be to connect to notify beforehand, have the ct & color filters do
 * their things, w/ our handler keep track of what needs to be reset next time.
 * Unfortunately, this can't be done because by the time we're done in rend_func
 * and therfore disconnect, no signal has been emitted yet. And since we
 * disconnect, we won't get to process anything.
 * The way we deal with all this is, we ask anything that sets a property
 * xalign, highlight and *-set on a renderer to also call this function, with
 * names of properties that shall be reset before next use. */
void
donna_renderer_set (GtkCellRenderer *renderer,
                    const gchar     *first_prop,
                    ...)
{
    GPtrArray *arr = NULL;
    va_list va_args;
    const gchar *prop;

    arr = g_object_get_data ((GObject *) renderer, "renderer-props");
    prop = first_prop;
    va_start (va_args, first_prop);
    while (prop)
    {
        g_ptr_array_add (arr, g_strdup (prop));
        prop = va_arg (va_args, const gchar *);
    }
    va_end (va_args);
}

static void
rend_on_demand (DonnaTreeView   *tree,
                GtkTreeModel    *model,
                GtkTreeIter     *iter,
                struct column   *_col,
                GtkCellRenderer *renderer,
                DonnaNode       *node)
{
    gboolean unref_node = FALSE;

    if (_col->refresh_properties != RP_ON_DEMAND)
        goto bail;
    if (!node)
    {
        gtk_tree_model_get (model, iter, TREE_VIEW_COL_NODE, &node, -1);
        if (!node)
            goto bail;
        unref_node = TRUE;
    }

    if (is_col_node_need_refresh (tree, _col, node))
    {
        g_object_set (renderer,
                "visible",      TRUE,
                "icon-name",    "view-refresh",
                "follow-state", TRUE,
                "xalign",       0.5,
                NULL);
    }
    else
        g_object_set (renderer, "visible", FALSE, NULL);

done:
    if (unref_node)
        g_object_unref (node);
    return;

bail:
    g_object_set (renderer, "visible", FALSE, NULL);
    goto done;
}

static void
rend_func (GtkTreeViewColumn  *column,
           GtkCellRenderer    *renderer,
           GtkTreeModel       *model,
           GtkTreeIter        *iter,
           gpointer            data)
{
    DonnaTreeView *tree;
    DonnaTreeViewPrivate *priv;
    struct column *_col;
    DonnaNode *node;
    guint index = GPOINTER_TO_UINT (data);
    GPtrArray *arr;
    guint i;

    tree = DONNA_TREE_VIEW (gtk_tree_view_column_get_tree_view (column));
    priv = tree->priv;

    /* spinner */
    if (index == INTERNAL_RENDERER_SPINNER || index == INTERNAL_RENDERER_PIXBUF)
    {
        struct active_spinners *as;

        if (!priv->active_spinners->len)
        {
            if (index == INTERNAL_RENDERER_PIXBUF)
                rend_on_demand (tree, model, iter, get_column_by_column (tree, column),
                        renderer, NULL);
            else
                g_object_set (renderer,
                        "visible",  FALSE,
                        NULL);
            return;
        }

        gtk_tree_model_get (model, iter, TREE_VIEW_COL_NODE, &node, -1);
        if (!node)
            return;

        as = get_as_for_node (tree, node, NULL, FALSE);
        if (as)
        {
            for (i = 0; i < as->as_cols->len; ++i)
            {
                struct as_col *as_col;

                as_col = &g_array_index (as->as_cols, struct as_col, i);
                if (as_col->column != column)
                    continue;

                if (index == INTERNAL_RENDERER_SPINNER)
                {
                    if (as_col->nb > 0)
                    {
                        g_object_set (renderer,
                                "visible",  TRUE,
                                "active",   TRUE,
                                "pulse",    priv->active_spinners_pulse,
                                NULL);
                        g_object_unref (node);
                        return;
                    }
                }
                else /* INTERNAL_RENDERER_PIXBUF */
                {
                    for (i = 0; i < as_col->tasks->len; ++i)
                    {
                        DonnaTask *task = as_col->tasks->pdata[i];

                        if (donna_task_get_state (task) == DONNA_TASK_FAILED)
                        {
                            g_object_set (renderer,
                                    "visible",      TRUE,
                                    "icon-name",    "dialog-warning",
                                    "follow-state", TRUE,
                                    "xalign",       0.0,
                                    NULL);
                            g_object_unref (node);
                            return;
                        }
                    }
                }
                break;
            }
        }

        if (index == INTERNAL_RENDERER_PIXBUF)
            rend_on_demand (tree, model, iter, get_column_by_column (tree, column),
                    renderer, node);
        else
            g_object_set (renderer,
                    "visible",  FALSE,
                    NULL);
        g_object_unref (node);
        return;
    }

    /* reset any properties that was used last time on this renderer. See
     * donna_renderer_set() for more */
    arr = g_object_get_data ((GObject *) renderer, "renderer-props");
    for (i = 0; i < arr->len; )
    {
        if (streq (arr->pdata[i], "xalign"))
            g_object_set ((GObject *) renderer, "xalign", 0.0, NULL);
        else if (streq (arr->pdata[i], "highlight"))
            g_object_set ((GObject *) renderer, "highlight", NULL, NULL);
        else
            g_object_set ((GObject *) renderer, arr->pdata[i], FALSE, NULL);
        /* brings the last item to index i, hence no need to increment i */
        g_ptr_array_remove_index_fast (arr, i);
    }

    index -= NB_INTERNAL_RENDERERS - 1; /* -1 to start with index 1 */

    _col = get_column_by_column (tree, column);
    /* special case: in mode list we can be our own ct, for the column showing
     * the line number. This is obviously has nothing to do w/ nodes, we handle
     * the rendering here instead of going through the ct interface */
    if (_col->ct == (DonnaColumnType *) tree)
    {
        GtkTreePath *path;
        gchar buf[10];
        gint ln = 0;

        path = gtk_tree_model_get_path (model, iter);
        if (priv->ln_relative && (!priv->ln_relative_focused
                    || gtk_widget_has_focus ((GtkWidget *) tree)))
        {
            GtkTreePath *path_focus;

            gtk_tree_view_get_cursor ((GtkTreeView *) tree, &path_focus, NULL);
            if (path_focus)
            {
                gint _ln;

                /* get the focus number */
                ln = gtk_tree_path_get_indices (path_focus)[0];

                /* calculate the relative number. For current line that falls to
                 * 0, which will then be turned to the current line number */
                /* we could do:
                 *
                 * ln -= gtk_tree_path_get_indices (path)[0];
                 * ln = ABS (ln);
                 *
                 * but then we get a warning: assuming signed overflow does not
                 * occur when simplifying conditional to constant
                 * [-Wstrict-overflow]
                 * if (ln == 0)
                 *     ^
                 *
                 * so this is our way to avoid/silence it.
                 */
                _ln = gtk_tree_path_get_indices (path)[0];
                if (ln > _ln)
                    ln -= _ln;
                else
                    ln = _ln - ln;

                if (ln > 0)
                {
                    /* align relative numbers to the right */
                    g_object_set (renderer, "xalign", 1.0, NULL);
                    donna_renderer_set (renderer, "xalign", NULL);
                }

                gtk_tree_path_free (path_focus);
            }
        }
        if (ln == 0)
            ln = 1 + gtk_tree_path_get_indices (path)[0];

        snprintf (buf, 10, "%d", ln);
        g_object_set (renderer, "visible", TRUE, "text", buf, NULL);
        gtk_tree_path_free (path);

        return;
    }
    gtk_tree_model_get (model, iter, TREE_VIEW_COL_NODE, &node, -1);

    if (priv->is_tree)
    {
        if (!node)
        {
            /* this is a "fake" node, shown as a "Please Wait..." */
            /* we can only do that for a column of type "name" */
            if (G_TYPE_FROM_INSTANCE (_col->ct) != DONNA_TYPE_COLUMN_TYPE_NAME)
                return;

            if (index == 1)
                /* GtkRendererPixbuf */
                g_object_set (renderer, "visible", FALSE, NULL);
            else /* index == 2 */
                /* GtkRendererText */
                g_object_set (renderer,
                        "visible",  TRUE,
                        "text",     "Please Wait...",
                        NULL);

            return;
        }
    }
    else if (!node)
        return;

    arr = donna_column_type_render (_col->ct, _col->ct_data, index, node, renderer);

    /* visuals */
    if (priv->is_tree
            && G_TYPE_FROM_INSTANCE (_col->ct) == DONNA_TYPE_COLUMN_TYPE_NAME)
    {
        if (index == 1)
        {
            /* GtkRendererPixbuf */
            GIcon *icon;

            gtk_tree_model_get (model, iter,
                    TREE_COL_ICON,      &icon,
                    -1);
            if (icon)
            {
                g_object_set (renderer, "gicon", icon, NULL);
                g_object_unref (icon);
            }
        }
        else /* index == 2 */
        {
            /* DonnaRendererText */
            gchar *name, *highlight;

            gtk_tree_model_get (model, iter,
                    TREE_COL_NAME,      &name,
                    TREE_COL_HIGHLIGHT, &highlight,
                    -1);
            if (name)
            {
                g_object_set (renderer, "text", name, NULL);
                g_free (name);
            }
            if (highlight)
            {
                g_object_set (renderer, "highlight", highlight, NULL);
                donna_renderer_set (renderer, "highlight", NULL);
                g_free (highlight);
            }
        }
    }

    if (arr)
    {
        GdkRectangle rect_visible, rect;
        GtkTreePath *path;
        DonnaTask *task;
        GSList *list;
        gboolean match = FALSE;

        /* ct wants some properties refreshed on node. See refresh_node_prop_cb */

        if (_col->refresh_properties == RP_ON_DEMAND)
        {
            /* assume our INTERNAL_RENDERER_PIXBUF was drawn, simply do nothing
             * (the columntype should have made renderer invisible) */
            g_ptr_array_unref (arr);
            g_object_unref (node);
            return;
        }

        /* get visible area, so we can determine if the row is visible. if not,
         * we don't trigger any refresh. This is a small "optimization" for
         * cases such as: go to a location where nodes have 2 custom props set,
         * one is preload the other not.
         * Every node-updated for the preloading CP will have the treeview
         * redraw the row (even if not visible) which would in turn have us here
         * trigger a refresh of the other CP. */
        gtk_tree_view_get_visible_rect ((GtkTreeView *) tree, &rect_visible);
        path = gtk_tree_model_get_path (model, iter);
        gtk_tree_view_get_background_area ((GtkTreeView *) tree, path, NULL, &rect);
        gtk_tree_path_free (path);
        if (rect.y + rect.height < 0 || rect.y > rect_visible.height)
        {
            g_ptr_array_unref (arr);
            g_object_unref (node);
            return;
        }

        g_mutex_lock (&priv->refresh_node_props_mutex);
        /* in case we've already a task running for this exact same cell, which
         * could happen if a second draw operation was triggered before the
         * refreshing completed, which is possible (esp. w/ custom properties
         * maybe) */
        for (list = priv->refresh_node_props; list; list = list->next)
        {
            struct refresh_node_props_data *d = list->data;

            if (d->node == node)
            {
                if (arr->len != d->props->len)
                    continue;
                match = TRUE;
                for (i = 0; i < arr->len; ++i)
                {
                    guint j;

                    for (j = 0; j < d->props->len; ++j)
                    {
                        if (streq (arr->pdata[i], d->props->pdata[j]))
                            break;
                    }
                    if (j >= d->props->len)
                    {
                        match = FALSE;
                        break;
                    }
                }
                if (match)
                    break;
            }
        }
        if (match)
        {
            g_mutex_unlock (&priv->refresh_node_props_mutex);
            g_ptr_array_unref (arr);
        }
        else
        {
            struct refresh_node_props_data *rnpd;

            rnpd = g_new0 (struct refresh_node_props_data, 1);
            rnpd->tree  = tree;
            rnpd->node  = g_object_ref (node);
            rnpd->props = g_ptr_array_ref (arr);

            priv->refresh_node_props = g_slist_append (priv->refresh_node_props, rnpd);
            g_mutex_unlock (&priv->refresh_node_props_mutex);

            task = donna_node_refresh_arr_task (node, arr, NULL);
            donna_task_set_callback (task,
                    (task_callback_fn) refresh_node_prop_cb,
                    rnpd,
                    (GDestroyNotify) free_refresh_node_props_data);
            donna_app_run_task (priv->app, task);
        }
    }
    else
        apply_color_filters (tree, column, renderer, node);
    g_object_unref (node);
}

static gint
sort_func (GtkTreeModel      *model,
           GtkTreeIter       *iter1,
           GtkTreeIter       *iter2,
           GtkTreeViewColumn *column)
{
    DonnaTreeView *tree;
    DonnaTreeViewPrivate *priv;
    GtkSortType sort_order;
    struct column *_col;
    DonnaNode *node1;
    DonnaNode *node2;
#define RET_UNKNOWN     42
    gint ret = RET_UNKNOWN;

    tree = DONNA_TREE_VIEW (gtk_tree_view_column_get_tree_view (column));
    priv = tree->priv;
    _col = get_column_by_column (tree, column);

    g_return_val_if_fail (_col != NULL, 0);

    /* special case: in mode list we can be our own ct, for the column showing
     * the line number. There's no sorting on that column obviously. */
    if (_col->ct == (DonnaColumnType *) tree)
        return 0;

    gtk_tree_model_get (model, iter1, TREE_COL_NODE, &node1, -1);
    /* one node could be a "fake" one, i.e. node is a NULL pointer */
    if (!node1)
        return -1;

    gtk_tree_model_get (model, iter2, TREE_COL_NODE, &node2, -1);
    if (!node2)
    {
        g_object_unref (node1);
        return 1;
    }

    /* are iters roots? */
    if (priv->is_tree && gtk_tree_store_iter_depth (priv->store, iter1) == 0
        && gtk_tree_store_iter_depth (priv->store, iter2) == 0)
    {
        GSList *l;

        /* so we decide the order. First one on our (ordered) list is first */
        for (l = priv->roots; l; l = l->next)
            if (itereq (iter1, (GtkTreeIter *) l->data))
                return -1;
            else if (itereq (iter2, (GtkTreeIter *) l->data))
                return 1;
        g_critical ("TreeView '%s': Failed to find order of roots", priv->name);
    }

    sort_order = gtk_tree_view_column_get_sort_order (column);

    if (priv->sort_groups != SORT_CONTAINER_MIXED)
    {
        DonnaNodeType type1, type2;

        type1 = donna_node_get_node_type (node1);
        type2 = donna_node_get_node_type (node2);

        if (type1 == DONNA_NODE_CONTAINER)
        {
            if (type2 != DONNA_NODE_CONTAINER)
            {
                if (priv->sort_groups == SORT_CONTAINER_FIRST)
                    ret = -1;
                else /* SORT_CONTAINER_FIRST_ALWAYS */
                    ret = (sort_order == GTK_SORT_ASCENDING) ? -1 : 1;
                goto done;
            }
        }
        else if (type2 == DONNA_NODE_CONTAINER)
        {
            if (priv->sort_groups == SORT_CONTAINER_FIRST)
                ret = 1;
            else /* SORT_CONTAINER_FIRST_ALWAYS */
                ret = (sort_order == GTK_SORT_ASCENDING) ? 1 : -1;
            goto done;
        }
    }

    if (_col->refresh_properties == RP_ON_DEMAND)
    {
        gboolean need_refresh1, need_refresh2;

        need_refresh1 = is_col_node_need_refresh (tree, _col, node1);
        need_refresh2 = is_col_node_need_refresh (tree, _col, node2);
        if (need_refresh1)
        {
            if (need_refresh2)
                /* don't goto done to go through secondary sort */
                ret = 0;
            else
            {
                /* reverse in DESC because the model will then reverse the
                 * return value of this function, and we want nodes w/ a value
                 * to always be listed before those w/out */
                ret = (sort_order == GTK_SORT_ASCENDING) ? 1 : -1;
                goto done;
            }
        }
        else if (need_refresh2)
        {
            ret = (sort_order == GTK_SORT_ASCENDING) ? -1 : 1;
            goto done;
        }
    }

    if (ret == RET_UNKNOWN)
        ret = donna_column_type_node_cmp (_col->ct, _col->ct_data, node1, node2);

    /* second sort order */
    if (ret == 0 && priv->second_sort_column
            /* could be the same column with second_sort_sticky */
            && priv->second_sort_column != column)
    {
        column = priv->second_sort_column;
        _col = get_column_by_column (tree, column);

        if (_col->refresh_properties == RP_ON_DEMAND)
        {
            gboolean need_refresh1, need_refresh2;

            need_refresh1 = is_col_node_need_refresh (tree, _col, node1);
            need_refresh2 = is_col_node_need_refresh (tree, _col, node2);
            if (need_refresh1)
            {
                if (need_refresh2)
                    ret = 0;
                else
                    ret = (priv->second_sort_order == GTK_SORT_ASCENDING) ? 1 : -1;
            }
            else if (need_refresh2)
                ret = (priv->second_sort_order == GTK_SORT_ASCENDING) ? -1 : 1;
            else
                ret = RET_UNKNOWN;
        }
        else
            ret = RET_UNKNOWN;

        if (ret == RET_UNKNOWN)
            ret = donna_column_type_node_cmp (_col->ct, _col->ct_data, node1, node2);
        if (ret != 0)
        {
            /* if second order is DESC, we should invert ret. But, if the
             * main order is DESC, the store will already invert the return
             * value of this function. */
            if (priv->second_sort_order == GTK_SORT_DESCENDING)
                ret *= -1;
            if (sort_order == GTK_SORT_DESCENDING)
                ret *= -1;
        }
    }

done:
    g_object_unref (node1);
    g_object_unref (node2);
    return ret;
}

static inline void
resort_tree (DonnaTreeView *tree)
{
    DonnaTreeViewPrivate *priv = tree->priv;

    /* trigger a resort */
    DONNA_DEBUG (TREE_VIEW, priv->name,
            g_debug ("TreeView '%s': Resort tree", priv->name));

    /* if there is no sorting needed (less than 2 rows) simply redraw */
    if (has_model_at_least_n_rows ((GtkTreeModel *) priv->store, 2))
    {
        GtkTreeSortable *sortable = (GtkTreeSortable *) priv->store;
        gint cur_sort_id;
        GtkSortType cur_sort_order;

        gtk_tree_sortable_get_sort_column_id (sortable,
                &cur_sort_id, &cur_sort_order);
        gtk_tree_sortable_set_sort_column_id (sortable,
                GTK_TREE_SORTABLE_UNSORTED_SORT_COLUMN_ID, cur_sort_order);
        gtk_tree_sortable_set_sort_column_id (sortable,
                cur_sort_id, cur_sort_order);
    }
    else
        gtk_widget_queue_draw ((GtkWidget *) tree);
}

static void
donna_tree_view_cursor_changed (GtkTreeView    *treev)
{
    DonnaTreeView *tree = (DonnaTreeView *) treev;
    DonnaTreeViewPrivate *priv = tree->priv;
    GSList *l;

    for (l = priv->columns; l; l = l->next)
    {
        struct column *_col = l->data;

        /* if we are the ct, it means it's a line-numbers column */
        if (_col->ct == (DonnaColumnType *) tree)
        {
            /* and if it shows relative numbers, we need to refresh the entire
             * column. Maybe emitting row-changed on all rows in the model would
             * be the "right thing to do" but it feels easier/faster to simply
             * redraw the column. */
            if (priv->ln_relative && (!priv->ln_relative_focused
                        || gtk_widget_has_focus ((GtkWidget *) tree)))
            {
                gint x, y;

                gtk_tree_view_convert_tree_to_widget_coords (treev,
                        gtk_tree_view_column_get_x_offset (_col->column), 0,
                        &x, &y);
                gtk_widget_queue_draw_area ((GtkWidget *) tree, x, 0,
                        gtk_tree_view_column_get_width (_col->column),
                        gtk_widget_get_allocated_height ((GtkWidget*) tree));
            }
        }
    }
}

static void
row_changed_cb (GtkTreeModel    *model,
                GtkTreePath     *path,
                GtkTreeIter     *iter,
                DonnaTreeView   *tree)
{
    DonnaTreeViewPrivate *priv = tree->priv;
    GtkTreeIter it;
    gboolean resort = FALSE;
    gint wrong;

    /* row was updated, refresh was done, but there's no auto-resort. So let's
     * do it ourself */
    if (!priv->sort_column)
        return;

    wrong = (gtk_tree_view_column_get_sort_order (priv->sort_column)
            == GTK_SORT_DESCENDING) ? -1 : 1;

    it = *iter;
    if (gtk_tree_model_iter_previous (model, &it))
        /* should previous iter be switched? */
        if (sort_func (model, &it, iter, priv->sort_column) == wrong)
            resort = TRUE;

    it = *iter;
    if (!resort && gtk_tree_model_iter_next (model, &it))
        /* should next iter be switched? */
        if (sort_func (model, iter, &it, priv->sort_column) == wrong)
            resort = TRUE;

    if (resort)
        resort_tree (tree);
}

static void
node_has_children_cb (DonnaTask                 *task,
                      gboolean                   timeout_called,
                      struct node_children_data *data)
{
    GtkTreeStore *store = data->tree->priv->store;
    GtkTreeModel *model = GTK_TREE_MODEL (store);
    DonnaTaskState state;
    const GValue *value;
    gboolean has_children;
    enum tree_expand es;

    if (!is_watched_iter_valid (data->tree, &data->iter, TRUE))
        goto free;

    state = donna_task_get_state (task);
    if (state != DONNA_TASK_DONE)
        /* we don't know if the node has children, so we'll keep the fake node
         * in, with expand state to UNKNOWN as it is. That way the user can ask
         * for expansion, which could simply have the expander removed if it
         * wasn't needed after all... */
        goto free;

    value = donna_task_get_return_value (task);
    has_children = g_value_get_boolean (value);

    gtk_tree_model_get (model, &data->iter,
            TREE_COL_EXPAND_STATE,  &es,
            -1);
    switch (es)
    {
        case TREE_EXPAND_UNKNOWN:
        case TREE_EXPAND_NEVER:
        case TREE_EXPAND_WIP:
            if (!has_children)
            {
                GtkTreeIter iter;

                /* remove fake node */
                if (gtk_tree_model_iter_children (model, &iter, &data->iter))
                {
                    DonnaNode *node;

                    gtk_tree_model_get (model, &iter,
                            TREE_VIEW_COL_NODE, &node,
                            -1);
                    if (!node)
                        gtk_tree_store_remove (store, &iter);
                }
                /* update expand state */
                set_es (data->tree->priv, &data->iter, TREE_EXPAND_NONE);
            }
            else
            {
                /* fake node already there, we just update the expand state,
                 * unless we're WIP then we'll let get_children set it right
                 * once the children have been added */
                if (es == TREE_EXPAND_UNKNOWN)
                    set_es (data->tree->priv, &data->iter, TREE_EXPAND_NEVER);
            }
            break;

        case TREE_EXPAND_PARTIAL:
        case TREE_EXPAND_MAXI:
            if (!has_children)
            {
                GtkTreeIter iter;

                /* update expand state */
                set_es (data->tree->priv, &data->iter, TREE_EXPAND_NONE);
                /* remove all children */
                if (gtk_tree_model_iter_children (model, &iter, &data->iter))
                    while (remove_row_from_tree (data->tree, &iter, RR_IS_REMOVAL))
                        ;
            }
            /* else: children and expand state obviously already good */
            break;

        case TREE_EXPAND_NONE:
            if (has_children)
            {
                /* add fake node */
                gtk_tree_store_insert_with_values (store,
                        NULL, &data->iter, 0,
                        TREE_COL_NODE,  NULL,
                        -1);
                /* update expand state */
                set_es (data->tree->priv, &data->iter, TREE_EXPAND_NEVER);
            }
            /* else: already no fake node */
            break;
    }

free:
    free_node_children_data (data);
}

struct node_updated_data
{
    DonnaTreeView   *tree;
    DonnaNode       *node;
    gchar           *name;
};

static gboolean
real_node_updated_cb (struct node_updated_data *data)
{
    DonnaTreeView *tree = data->tree;
    DonnaTreeViewPrivate *priv = tree->priv;
    GtkTreeModel *model = (GtkTreeModel *) priv->store;
    GSList *list, *l;
    guint i;

    /* do we have this node on tree? */
    if (!g_hash_table_lookup_extended (priv->hashtable, data->node,
                NULL, (gpointer) &l))
        goto done;

    /* list: we might need to bypass the properties from column: if name, or
     * there's a VF applied FIXME */
    if (priv->is_tree || !streq (data->name, "name"))
    {
        /* should that property cause a refresh? */
        for (i = 0; i < priv->col_props->len; ++i)
        {
            struct col_prop *cp;

            cp = &g_array_index (priv->col_props, struct col_prop, i);
            if (streq (data->name, cp->prop))
                break;
        }
        if (i >= priv->col_props->len)
            goto done;
    }

    /* should we ignore this prop/node combo ? See refresh_node_prop_cb */
    g_mutex_lock (&priv->refresh_node_props_mutex);
    for (list = priv->refresh_node_props; list; list = list->next)
    {
        struct refresh_node_props_data *d = list->data;

        if (d->node == data->node)
        {
            for (i = 0; i < d->props->len; ++i)
            {
                if (streq (data->name, d->props->pdata[i]))
                    break;
            }
            if (i < d->props->len)
                break;
        }
    }
    g_mutex_unlock (&priv->refresh_node_props_mutex);
    if (list)
        goto done;

    /* trigger refresh */
    if (priv->is_tree)
    {
        /* on all rows for that node */
        for ( ; l; l = l->next)
        {
            GtkTreeIter *iter = l->data;
            GtkTreePath *path;

            path = gtk_tree_model_get_path (model, iter);
            gtk_tree_model_row_changed (model, path, iter);
            gtk_tree_path_free (path);
        }
    }
    else
    {
        GtkTreeIter *iter = (GtkTreeIter *) l;

        if (refilter_node (tree, data->node, iter))
        {
            GtkTreePath *path;

            path = gtk_tree_model_get_path (model, iter);
            gtk_tree_model_row_changed (model, path, iter);
            gtk_tree_path_free (path);
        }
    }

done:
    if (streq (data->name, "name") || streq (data->name, "size"))
        check_statuses (tree, STATUS_CHANGED_ON_CONTENT);
    g_object_unref (data->node);
    g_free (data->name);
    g_free (data);
    return FALSE;
}

static void
node_updated_cb (DonnaProvider  *provider,
                 DonnaNode      *node,
                 const gchar    *name,
                 DonnaTreeView  *tree)
{
    struct node_updated_data *data;

    if (tree->priv->refresh_on_hold)
        return;

    /* we might not be in the main thread, but we need to be */

    data = g_new (struct node_updated_data, 1);
    data->tree = tree;
    data->node = g_object_ref (node);
    data->name = g_strdup (name);
    g_main_context_invoke (NULL, (GSourceFunc) real_node_updated_cb, data);
}

struct node_deleted_data
{
    DonnaTreeView   *tree;
    DonnaNode       *node;
};

static void
free_node_deleted_data (struct node_deleted_data *data)
{
    g_object_unref (data->node);
    g_free (data);
}

static gboolean
real_node_deleted_cb (struct node_deleted_data *data)
{
    DonnaTreeViewPrivate *priv = data->tree->priv;
    GSList *list;
    GSList *next;

    if (!priv->is_tree && priv->location == data->node)
    {
        GError *err = NULL;
        DonnaNode *n;
        gchar *location;
        gchar *s;

        if (donna_provider_get_flags (donna_node_peek_provider (data->node))
                & DONNA_PROVIDER_FLAG_FLAT)
        {
            gchar *fl = donna_node_get_full_location (data->node);
            donna_app_show_error (priv->app, NULL,
                    "TreeView '%s': Current location (%s) has been deleted",
                    priv->name, fl);
            g_free (fl);
            /*FIXME DRAW_ERROR*/
            goto free;
        }

        /* try to go up */
        location = donna_node_get_location (data->node);

        for (;;)
        {
            /* location can't be "/" since root can't be deleted */
            s = strrchr (location, '/');
            if (s == location)
                ++s;
            *s = '\0';

            n = donna_provider_get_node (donna_node_peek_provider (data->node),
                    location, &err);
            if (!n)
            {
                if (g_error_matches (err, DONNA_PROVIDER_ERROR,
                        DONNA_PROVIDER_ERROR_LOCATION_NOT_FOUND))
                {
                    g_clear_error (&err);
                    continue;
                }

                /*FIXME DRAW_ERROR*/
                g_clear_error (&err);
                break;
            }

            if (!donna_tree_view_set_location (data->tree, n, &err))
            {
                gchar *fl = donna_node_get_full_location (data->node);
                donna_app_show_error (data->tree->priv->app, err,
                        "TreeView '%s': Failed to go to '%s' (as parent of '%s')",
                        data->tree->priv->name, location, fl);
                g_free (fl);
            }
            g_object_unref (n);
            break;
        }

        goto free;
    }

    if (!g_hash_table_lookup_extended (priv->hashtable, data->node,
                NULL, (gpointer) &list))
        goto free;

    if (priv->is_tree)
    {
        for ( ; list; list = next)
        {
            GtkTreeIter it;

            next = list->next;
            it = * (GtkTreeIter *) list->data;
            /* this will remove the row from the list in hashtable. IOW, it will
             * remove the current list element (list); which is why we took the
             * next element ahead of time. Because it also assumes we own iter
             * (to set it to the next children) we need to use a local one */
            remove_row_from_tree (data->tree, &it, RR_IS_REMOVAL);
        }
    }
    else
        remove_node_from_list (data->tree, data->node, (GtkTreeIter *) list);

free:
    free_node_deleted_data (data);
    /* don't repeat */
    return FALSE;
}

static void
node_deleted_cb (DonnaProvider  *provider,
                 DonnaNode      *node,
                 DonnaTreeView  *tree)
{
    struct node_deleted_data *data;

    /* we might not be in the main thread, but we need to be */
    data = g_new0 (struct node_deleted_data, 1);
    data->tree       = tree;
    data->node       = g_object_ref (node);
    g_main_context_invoke (NULL, (GSourceFunc) real_node_deleted_cb, data);
}

struct node_removed_from
{
    DonnaTreeView *tree;
    DonnaNode *node;
    DonnaNode *parent;
};

static gboolean
real_node_removed_from_cb (struct node_removed_from *nrf)
{
    DonnaTreeViewPrivate *priv = nrf->tree->priv;
    GSList *list;
    GSList *next;

    if (!priv->is_tree && priv->location != nrf->parent)
        goto finish;

    if (!g_hash_table_lookup_extended (priv->hashtable, nrf->node,
                NULL, (gpointer) &list))
        goto finish;

    if (priv->is_tree)
    {
        for ( ; list; list = next)
        {
            GtkTreeIter it;
            GtkTreeIter parent;
            DonnaNode *node;

            next = list->next;
            it = * (GtkTreeIter *) list->data;

            /* we should only remove nodes for which the parent matches */
            if (!gtk_tree_model_iter_parent ((GtkTreeModel *) priv->store,
                        &parent, &it))
                continue;

            gtk_tree_model_get ((GtkTreeModel *) priv->store, &parent,
                    TREE_COL_NODE, &node,
                    -1);
            g_object_unref (node);
            if (node != nrf->parent)
                continue;

            /* this will remove the row from the list in hashtable. IOW, it will
             * remove the current list element (list); which is why we took the
             * next element ahead of time. Because it also assumes we own iter
             * (to set it to the next children) we need to use a local one */
            remove_row_from_tree (nrf->tree, &it, RR_IS_REMOVAL);
        }
    }
    else
        remove_node_from_list (nrf->tree, nrf->node, (GtkTreeIter *) list);

finish:
    g_object_unref (nrf->node);
    g_object_unref (nrf->parent);
    g_free (nrf);
    return FALSE;
}

static void
node_removed_from_cb (DonnaProvider *provider,
                      DonnaNode     *node,
                      DonnaNode     *parent,
                      DonnaTreeView *tree)
{
    struct node_removed_from *nrf;

    /* we might not be in the main thread, but we need to be */
    nrf = g_new0 (struct node_removed_from, 1);
    nrf->tree   = tree;
    nrf->node   = g_object_ref (node);
    nrf->parent = g_object_ref (parent);
    g_main_context_invoke (NULL, (GSourceFunc) real_node_removed_from_cb, nrf);
}

struct node_children_cb_data
{
    DonnaTreeView   *tree;
    DonnaNode       *node;
    DonnaNodeType    node_types;
    GPtrArray       *children;
};

/* mode tree only */
static gboolean
real_node_children_cb (struct node_children_cb_data *data)
{
    DonnaTreeViewPrivate *priv = data->tree->priv;
    enum tree_expand es;

    if (priv->location != data->node)
        goto free;

    if (!(data->node_types & priv->node_types))
        goto free;

    gtk_tree_model_get (GTK_TREE_MODEL (priv->store), &priv->location_iter,
            TREE_COL_EXPAND_STATE,  &es,
            -1);
    if (es == TREE_EXPAND_MAXI)
    {
        DONNA_DEBUG (TREE_VIEW, priv->name,
                g_debug ("TreeView '%s': updating children for current location",
                    priv->name));
        set_children (data->tree, &priv->location_iter,
                data->node_types, data->children, FALSE, FALSE);
    }

free:
    g_object_unref (data->node);
    g_ptr_array_unref (data->children);
    g_free (data);

    /* don't repeat */
    return FALSE;
}

/* mode tree only */
static void
node_children_cb (DonnaProvider  *provider,
                  DonnaNode      *node,
                  DonnaNodeType   node_types,
                  GPtrArray      *children,
                  DonnaTreeView  *tree)
{
    struct node_children_cb_data *data;

    /* we might not be in the main thread, but we need to be */
    data = g_new (struct node_children_cb_data, 1);
    data->tree       = tree;
    data->node       = g_object_ref (node);
    data->node_types = node_types;
    data->children   = g_ptr_array_ref (children);
    g_main_context_invoke (NULL, (GSourceFunc) real_node_children_cb, data);
}

/* mode list only */
static gboolean
add_pending_nodes (DonnaTreeView *tree)
{
    DonnaTreeViewPrivate *priv = tree->priv;
    GtkTreeSortable *sortable = (GtkTreeSortable *) priv->store;
    gint sort_col_id;
    GtkSortType order;
    gint64 t;
    guint max;
    guint i;

    if (G_UNLIKELY (!priv->nodes_to_add))
        return G_SOURCE_REMOVE;

    /* the level determines how many rows to add at the most. This was set by
     * the last iteration, based on how long it took to add rows and then sort
     * the model. */
    if (priv->nodes_to_add_level == 0)
        max = 100;
    else if (priv->nodes_to_add_level == 1)
        max = 1000;
    else
        /* add all rows */
        max = 0;

    t = g_get_monotonic_time ();
    /* adding items to a sorted store is quite slow; we get much better
     * performance by adding all items to an unsorted store, and then
     * sorting it */
    gtk_tree_sortable_get_sort_column_id (sortable, &sort_col_id, &order);
    gtk_tree_sortable_set_sort_column_id (sortable,
            GTK_TREE_SORTABLE_UNSORTED_SORT_COLUMN_ID, order);
    priv->filling_list = TRUE;

    i = 0;
    while (priv->nodes_to_add->len > 0)
    {
        add_node_to_list (tree, priv->nodes_to_add->pdata[0], FALSE);
        g_ptr_array_remove_index (priv->nodes_to_add, 0);
        if (++i == max)
            break;
    }
    if (priv->nodes_to_add->len == 0)
    {
        g_ptr_array_unref (priv->nodes_to_add);
        priv->nodes_to_add = NULL;
        priv->nodes_to_add_level = 0;
    }
    else
        /* we've stopped before processing all rows, so we'll come back to it
         * after all other events have been processed, including idle sources
         * (hence a slightly lower priority) */
        g_idle_add_full (G_PRIORITY_DEFAULT_IDLE + 10,
                (GSourceFunc) add_pending_nodes, tree, NULL);

    /* restore sort */
    gtk_tree_sortable_set_sort_column_id (sortable, sort_col_id, order);
    /* for next iteration: see how long it took to add those rows & resort the
     * model, and based on that determine how many rows we should add max next,
     * to try and not block the UI too much */
    /* TODO: later on, we might want to make our own list store, with special
     * support so that the sorting can be done in another thread, and in the UI
     * (task's callback) we just "apply it."
     * That way, the UI would never block and we'll always add all rows as fast
     * as possible. */
    t = g_get_monotonic_time () - t;
    if (t <= G_USEC_PER_SEC)
        priv->nodes_to_add_level = 0;
    else if (t <= 2 * G_USEC_PER_SEC)
        priv->nodes_to_add_level = 1;
    else
        priv->nodes_to_add_level = 2;
    /* do it ourself because we prevented it w/ priv->filling_list */
    priv->filling_list = FALSE;
    check_statuses (tree, STATUS_CHANGED_ON_CONTENT);

    return G_SOURCE_REMOVE;
}

struct new_child_data
{
    DonnaTreeView   *tree;
    DonnaNode       *node;
    DonnaNode       *child;
};

static gboolean
real_new_child_cb (struct new_child_data *data)
{
    DonnaTreeViewPrivate *priv = data->tree->priv;

    if (!priv->is_tree)
    {
        /* nodes_to_add_level is at -1 when processing events from the callback
         * of a get_children task. We then want to ignore any pending
         * node-new-child since we've just filled the list with all children.
         * IOW, while there could be possibility of such an event being a
         * legitimate signal generated right after the get_children task, most
         * likely this is - from exec/search results - some signals that were
         * emitted but haven't yet been processed while the task is done, since
         * the callback is being processed */
        if (priv->nodes_to_add_level == -1)
            goto free;

        if (priv->cl == CHANGING_LOCATION_ASKED
                || priv->cl == CHANGING_LOCATION_SLOW)
        {
            if (!change_location (data->tree, CHANGING_LOCATION_GOT_CHILD,
                    data->node, NULL, NULL))
                goto free;
            /* emit signal */
            g_object_notify_by_pspec ((GObject *) data->tree,
                    donna_tree_view_props[PROP_LOCATION]);

        }
        else if (priv->cl == CHANGING_LOCATION_GOT_CHILD)
        {
            if (priv->future_location != data->node)
                goto free;
        }
        else if (priv->location != data->node)
            goto free;

        /* until we have 100 rows, we just add right away */
        if (!has_model_at_least_n_rows ((GtkTreeModel *) priv->store, 100))
            add_node_to_list (data->tree, data->child, FALSE);
        else
        {
            /* then, we'll store them in an array, and wait one second
             * (literally) in case we're getting a bunch of signals (e.g. a few
             * search results coming in at once), so we can add them all and
             * sort the model once, instead of once per new row */
            if (!priv->nodes_to_add)
            {
                priv->nodes_to_add_level = 0;
                priv->nodes_to_add = g_ptr_array_new_with_free_func (g_object_unref);
                /* keep it as idle priority, as timeout are usually much higher,
                 * namely G_PRIORITY_DEFAULT (same as GDK events) */
                g_timeout_add_full (G_PRIORITY_DEFAULT_IDLE, 1000,
                        (GSourceFunc) add_pending_nodes, data->tree, NULL);
            }
            g_ptr_array_add (priv->nodes_to_add, g_object_ref (data->child));
        }
        goto free;
    }
    else
    {
        GtkTreeModel *model = (GtkTreeModel *) priv->store;
        GSList *list;

        list = g_hash_table_lookup (priv->hashtable, data->node);
        for ( ; list; list = list->next)
        {
            enum tree_expand es;

            gtk_tree_model_get (model, list->data,
                    TREE_COL_EXPAND_STATE,  &es,
                    -1);
            /* MAXI: add the node */
            if (es == TREE_EXPAND_MAXI)
                add_node_to_tree_filtered (data->tree, list->data, data->child, NULL);
            /* NONE: now there's one, update es but we don't add it */
            else if (es == TREE_EXPAND_NONE)
            {
                /* add fake node */
                gtk_tree_store_insert_with_values (priv->store, NULL, list->data, 0,
                        TREE_COL_NODE,  NULL,
                        -1);
                set_es (priv, list->data, TREE_EXPAND_NEVER);
            }
            /* anything else (PARTIAL, etc) stays as is */
        }
    }

free:
    g_object_unref (data->node);
    g_object_unref (data->child);
    g_free (data);
    /* no repeat */
    return FALSE;
}

static void
node_new_child_cb (DonnaProvider *provider,
                   DonnaNode     *node,
                   DonnaNode     *child,
                   DonnaTreeView *tree)
{
    DonnaTreeViewPrivate *priv = tree->priv;
    struct new_child_data *data;
    DonnaNodeType type;

    type = donna_node_get_node_type (child);
    /* if we don't care for this type of nodes, nothing to do.
     * XXX technically this is bad, since we shouldn't access priv from possibly
     * another thread. But really, everything we look at exists, and is very
     * unlikely to change/cause issues, so this saves one alloc + 2 ref */
    if (!(type & priv->node_types))
        return;

    /* we can't check if node is in the tree though, because there's no lock,
     * and we might not be in the main thread, and so we need to be */
    data = g_new (struct new_child_data, 1);
    data->tree  = tree;
    data->node  = g_object_ref (node);
    data->child = g_object_ref (child);
    g_main_context_invoke (NULL, (GSourceFunc) real_new_child_cb, data);
}

/* mode tree only */
static inline GtkTreeIter *
get_child_iter_for_node (DonnaTreeView  *tree,
                         GtkTreeIter    *parent,
                         DonnaNode      *node)
{
    DonnaTreeViewPrivate *priv = tree->priv;
    GtkTreeModel *model = (GtkTreeModel *) priv->store;
    GSList *list;

    list = g_hash_table_lookup (priv->hashtable, node);
    for ( ; list; list = list->next)
    {
        GtkTreeIter *i = list->data;
        GtkTreeIter  p;

        /* get the parent and compare with our parent iter */
        if (gtk_tree_model_iter_parent (model, &p, i)
                && itereq (&p, parent))
            return i;
    }
    return NULL;
}

struct node_visuals_data
{
    DonnaTreeView   *tree;
    GtkTreeIter      iter;
    DonnaNode       *node;
};

static void
free_node_visuals_data (struct node_visuals_data *data)
{
    g_slice_free (struct node_visuals_data, data);
}

static void
node_refresh_visuals_cb (DonnaTask                  *task,
                         gboolean                    timeout_called,
                         struct node_visuals_data   *data)
{
    if (donna_task_get_state (task) == DONNA_TASK_FAILED)
    {
        free_node_visuals_data (data);
        return;
    }

    load_node_visuals (data->tree, &data->iter, data->node, FALSE);

    free_node_visuals_data (data);
}

#define add_prop(arr, prop) do {            \
    if (!arr)                               \
        arr = g_ptr_array_new ();           \
    g_ptr_array_add (arr, (gpointer) prop); \
} while (0)
#define load_node_visual(UPPER, lower, GTYPE, get_fn, COLUMN)   do {                \
    if ((priv->node_visuals & DONNA_TREE_VISUAL_##UPPER)                            \
            && !(visuals & DONNA_TREE_VISUAL_##UPPER))                              \
    {                                                                               \
        donna_node_get (node, FALSE, "visual-" lower, &has, &value, NULL);          \
        switch (has)                                                                \
        {                                                                           \
            case DONNA_NODE_VALUE_NONE:                                             \
            case DONNA_NODE_VALUE_ERROR: /* not possible, avoids warning */         \
                break;                                                              \
            case DONNA_NODE_VALUE_NEED_REFRESH:                                     \
                if (allow_refresh)                                                  \
                    add_prop (arr, "visual-" lower);                                \
                break;                                                              \
            case DONNA_NODE_VALUE_SET:                                              \
                if (G_VALUE_TYPE (&value) != GTYPE)                                 \
                {                                                                   \
                    gchar *location = donna_node_get_location (node);               \
                    g_warning ("TreeView '%s': "                                    \
                            "Unable to load visual-" lower " from node '%s:%s', "   \
                            "property isn't of expected type (%s instead of %s)",   \
                            priv->name,                                             \
                            donna_node_get_domain (node),                           \
                            location,                                               \
                            G_VALUE_TYPE_NAME (&value),                             \
                            g_type_name (GTYPE));                                   \
                    g_free (location);                                              \
                }                                                                   \
                else                                                                \
                    gtk_tree_store_set (priv->store, iter,                          \
                            TREE_COL_##COLUMN,  g_value_##get_fn (&value),          \
                            -1);                                                    \
                g_value_unset (&value);                                             \
                break;                                                              \
        }                                                                           \
    }                                                                               \
} while (0)
/* mode tree only */
static inline void
load_node_visuals (DonnaTreeView    *tree,
                   GtkTreeIter      *iter,
                   DonnaNode        *node,
                   gboolean          allow_refresh)
{
    DonnaTreeViewPrivate *priv = tree->priv;
    DonnaTreeVisual visuals;
    DonnaNodeHasValue has;
    GValue value = G_VALUE_INIT;
    GPtrArray *arr = NULL;

    gtk_tree_model_get ((GtkTreeModel *) priv->store, iter,
            TREE_COL_VISUALS,   &visuals,
            -1);

    load_node_visual (NAME,      "name",      G_TYPE_STRING,   get_string, NAME);
    load_node_visual (ICON,      "icon",      G_TYPE_ICON,     get_object, ICON);
    load_node_visual (BOX,       "box",       G_TYPE_STRING,   get_string, BOX);
    load_node_visual (HIGHLIGHT, "highlight", G_TYPE_STRING,   get_string, HIGHLIGHT);

    if (arr)
    {
        GError *err = NULL;
        DonnaTask *task;

        task = donna_node_refresh_arr_task (node, arr, &err);
        if (!task)
        {
            gchar *location = donna_node_get_location (node);
            donna_app_show_error (priv->app, err,
                    "Unable to refresh visuals on node '%s:%s'",
                    donna_node_get_domain (node),
                    location);
            g_free (location);
            g_clear_error (&err);
        }
        else
        {
            struct node_visuals_data *data;

            data = g_slice_new (struct node_visuals_data);
            data->tree = tree;
            data->iter = *iter;
            data->node = node;

            donna_task_set_callback (task,
                    (task_callback_fn) node_refresh_visuals_cb,
                    data,
                    (GDestroyNotify) free_node_visuals_data);
            donna_app_run_task (priv->app, task);
        }

        g_ptr_array_unref (arr);
    }
}
#undef load_node_visual
#undef add_prop

/* mode tree only */
static inline void
load_tree_visuals (DonnaTreeView    *tree,
                   GtkTreeIter      *iter,
                   DonnaNode        *node)
{
    DonnaTreeViewPrivate *priv = tree->priv;
    gchar *fl;
    GSList *list, *l;
    GtkTreeIter *root;

    if (!priv->tree_visuals)
        return;

    fl = donna_node_get_full_location (node);
    list = g_hash_table_lookup (priv->tree_visuals, fl);
    if (!list)
    {
        g_free (fl);
        return;
    }

    root = get_root_iter (tree, iter);

    for (l = list; l; l = l->next)
    {
        struct visuals *visuals = l->data;

        if (visuals->root.stamp == 0)
        {
            GtkTreeIter it;

            /* invalid iter means user_data holds the "path" element, i.e.
             * number of root to use (starting at 0) */
            if (!gtk_tree_model_iter_nth_child ((GtkTreeModel *) priv->store,
                        &it, NULL, GPOINTER_TO_INT (visuals->root.user_data)))
                /* we don't (yet) have that root */
                continue;

            /* make it a valid iter pointing to the row */
            visuals->root = it;
        }

        if (itereq (root, &visuals->root))
        {
            DonnaTreeVisual v = 0;

            if (visuals->name)
            {
                v |= DONNA_TREE_VISUAL_NAME;
                gtk_tree_store_set (priv->store, iter,
                        TREE_COL_NAME,          visuals->name,
                        -1);
            }
            if (visuals->icon)
            {
                v |= DONNA_TREE_VISUAL_ICON;
                gtk_tree_store_set (priv->store, iter,
                        TREE_COL_ICON,          visuals->icon,
                        -1);
            }
            if (visuals->box)
            {
                v |= DONNA_TREE_VISUAL_BOX;
                gtk_tree_store_set (priv->store, iter,
                        TREE_COL_BOX,           visuals->box,
                        -1);
            }
            if (visuals->highlight)
            {
                v |= DONNA_TREE_VISUAL_HIGHLIGHT;
                gtk_tree_store_set (priv->store, iter,
                        TREE_COL_HIGHLIGHT,     visuals->highlight,
                        -1);
            }
            /* not a visual, but treated the same */
            if (visuals->click_mode)
            {
                v |= DONNA_TREE_VISUAL_CLICK_MODE;
                gtk_tree_store_set (priv->store, iter,
                        TREE_COL_CLICK_MODE,    visuals->click_mode,
                        -1);
            }
            gtk_tree_store_set (priv->store, iter,
                    TREE_COL_VISUALS,           v,
                    -1);

            /* now that it's loaded, remove from the list */
            free_visuals (visuals);
            list = g_slist_delete_link (list, l);
            if (list)
                /* will free fl */
                g_hash_table_insert (priv->tree_visuals, fl, list);
            else
            {
                g_hash_table_remove (priv->tree_visuals, fl);
                g_free (fl);
                if (g_hash_table_size (priv->tree_visuals) == 0)
                {
                    g_hash_table_unref (priv->tree_visuals);
                    priv->tree_visuals = NULL;
                }
            }

            return;
        }
    }
    g_free (fl);
}

/* mode list only */
static void
add_node_to_list (DonnaTreeView *tree, DonnaNode *node, gboolean checked)
{
    DonnaTreeViewPrivate *priv = tree->priv;
    DonnaProvider *provider;
    guint i;

    if (!checked)
    {
        GtkTreeIter *_iter_row;

        if (g_hash_table_lookup_extended (priv->hashtable, node,
                    NULL, (gpointer) &_iter_row))
        {
            refilter_node (tree, node, _iter_row);
            return;
        }
    }

    DONNA_DEBUG (TREE_VIEW, priv->name,
            gchar *fl = donna_node_get_full_location (node);
            g_debug2 ("TreeView '%s': add row for '%s' to hashtable",
                priv->name, fl);
            g_free (fl));

    provider = donna_node_peek_provider (node);
    for (i = 0; i < priv->providers->len; ++i)
    {
        struct provider_signals *ps = priv->providers->pdata[i];

        if (ps->provider == provider)
        {
            ps->nb_nodes++;
            break;
        }
    }
    if (i >= priv->providers->len)
    {
        struct provider_signals *ps;

        ps = g_new0 (struct provider_signals, 1);
        ps->provider = g_object_ref (provider);
        ps->nb_nodes = 1;
        ps->sid_node_updated = g_signal_connect (provider, "node-updated",
                (GCallback) node_updated_cb, tree);
        ps->sid_node_deleted = g_signal_connect (provider, "node-deleted",
                (GCallback) node_deleted_cb, tree);
        ps->sid_node_removed_from = g_signal_connect (provider, "node-removed-from",
                (GCallback) node_removed_from_cb, tree);

        g_ptr_array_add (priv->providers, ps);
    }

    g_hash_table_insert (priv->hashtable, g_object_ref (node), NULL);
    refilter_node (tree, node, NULL);
}

/* mode tree only */
static gboolean
add_node_to_tree_filtered (DonnaTreeView *tree,
                           GtkTreeIter   *iter,
                           DonnaNode     *node,
                           GtkTreeIter   *iter_row)
{
    DonnaTreeViewPrivate *priv = tree->priv;
    gchar *name;
    gboolean skip = FALSE;

    if (priv->show_hidden)
        return add_node_to_tree (tree, iter, node, iter_row);

    name = donna_node_get_name (node);
    skip = (name && *name == '.');
    g_free (name);

    if (!skip)
        return add_node_to_tree (tree, iter, node, iter_row);

    return FALSE;
}

/* mode tree only */
static gboolean
add_node_to_tree (DonnaTreeView *tree,
                  GtkTreeIter   *parent,
                  DonnaNode     *node,
                  GtkTreeIter   *iter_row)
{
    const gchar             *domain;
    DonnaTreeViewPrivate    *priv;
    GtkTreeModel            *model;
    GtkTreeIter              iter;
    GtkTreeIter             *it;
    GSList                  *list;
    GSList                  *l;
    DonnaProvider           *provider;
    DonnaNodeType            node_type;
    DonnaTask               *task;
    gboolean                 added;
    guint                    i;
    GError                  *err = NULL;

    g_return_val_if_fail (DONNA_IS_TREE_VIEW (tree), FALSE);
    g_return_val_if_fail (DONNA_IS_NODE (node), FALSE);
    g_return_val_if_fail (tree->priv->is_tree, FALSE);

    priv  = tree->priv;
    model = (GtkTreeModel *) priv->store;

    /* is there already a row for this node at that level? */
    if (parent)
    {
        GtkTreeIter *_it;

        _it = get_child_iter_for_node (tree, parent, node);
        if (_it)
        {
            /* already exists under the same parent, nothing to do */
            if (iter_row)
                *iter_row = *_it;
            return TRUE;
        }
    }

    DONNA_DEBUG (TREE_VIEW, priv->name,
            gchar *fl = donna_node_get_full_location (node);
            g_debug2 ("TreeView '%s': add row for '%s'",
                priv->name, fl);
            g_free (fl));

    /* check if the parent has a "fake" node as child, in which case we'll
     * re-use it instead of adding a new node */
    added = FALSE;
    if (parent && gtk_tree_model_iter_children (model, &iter, parent))
    {
        DonnaNode *n;

        gtk_tree_model_get (model, &iter,
                TREE_COL_NODE,  &n,
                -1);
        if (!n)
        {
            gtk_tree_store_set (priv->store, &iter,
                    TREE_COL_NODE,          node,
                    TREE_COL_EXPAND_STATE,  TREE_EXPAND_UNKNOWN,
                    -1);
            set_es (priv, &iter, TREE_EXPAND_UNKNOWN);
            added = TRUE;
        }
        else
            g_object_unref (n);
    }
    if (!added)
    {
        gtk_tree_store_insert_with_values (priv->store, &iter, parent, -1,
                TREE_COL_NODE,          node,
                TREE_COL_EXPAND_STATE,  TREE_EXPAND_UNKNOWN,
                -1);
        set_es (priv, &iter, TREE_EXPAND_UNKNOWN);
    }
    if (iter_row)
        *iter_row = iter;
    /* add it to our hashtable */
    it   = gtk_tree_iter_copy (&iter);
    list = g_hash_table_lookup (priv->hashtable, node);
    if (!list)
        /* we're adding a new node, take a ref on it */
        g_object_ref (node);
    list = g_slist_prepend (list, it);
    g_hash_table_insert (priv->hashtable, node, list);
    /* new root? */
    if (!parent)
        priv->roots = g_slist_append (priv->roots, it);
    /* visuals */
    load_tree_visuals (tree, &iter, node);
    load_node_visuals (tree, &iter, node, TRUE);
    /* check the list in case we have another tree node for that node, in which
     * case we might get the has_children info from there */
    added = FALSE;
    for (l = list; l; l = l->next)
    {
        GtkTreeIter *_iter = l->data;
        enum tree_expand es;

        if (itereq (&iter, _iter))
            continue;

        gtk_tree_model_get (model, _iter,
                TREE_COL_EXPAND_STATE,  &es,
                -1);
        switch (es)
        {
            /* node has children */
            case TREE_EXPAND_NEVER:
            case TREE_EXPAND_PARTIAL:
            case TREE_EXPAND_MAXI:
                es = TREE_EXPAND_NEVER;
                break;

            /* node doesn't have children */
            case TREE_EXPAND_NONE:
                break;

            /* anything else is inconclusive */
            default:
                es = TREE_EXPAND_UNKNOWN; /* == 0 */
        }

        if (es)
        {
            set_es (priv, &iter, es);
            if (es == TREE_EXPAND_NEVER)
                /* insert a fake node so the user can ask for expansion */
                gtk_tree_store_insert_with_values (priv->store, NULL, &iter, 0,
                        TREE_COL_NODE,  NULL,
                        -1);
            added = TRUE;
            break;
        }
    }
    /* get provider to get task to know if it has children */
    provider = donna_node_peek_provider (node);
    node_type = donna_node_get_node_type (node);
    for (i = 0; i < priv->providers->len; ++i)
    {
        struct provider_signals *ps = priv->providers->pdata[i];

        if (ps->provider == provider)
        {
            ps->nb_nodes++;
            break;
        }
    }
    if (i >= priv->providers->len)
    {
        struct provider_signals *ps;

        ps = g_new0 (struct provider_signals, 1);
        ps->provider = g_object_ref (provider);
        ps->nb_nodes = 1;
        ps->sid_node_updated = g_signal_connect (provider, "node-updated",
                G_CALLBACK (node_updated_cb), tree);
        ps->sid_node_deleted = g_signal_connect (provider, "node-deleted",
                G_CALLBACK (node_deleted_cb), tree);
        ps->sid_node_removed_from = g_signal_connect (provider, "node-removed-from",
                G_CALLBACK (node_removed_from_cb), tree);
        if (node_type != DONNA_NODE_ITEM)
        {
            ps->sid_node_children = g_signal_connect (provider, "node-children",
                    G_CALLBACK (node_children_cb), tree);
            ps->sid_node_new_child = g_signal_connect (provider, "node-new-child",
                    G_CALLBACK (node_new_child_cb), tree);
        }

        g_ptr_array_add (priv->providers, ps);
    }

    if (added || node_type == DONNA_NODE_ITEM)
    {
        if (node_type == DONNA_NODE_ITEM)
            set_es (priv, &iter, TREE_EXPAND_NONE);
        /* fix some weird glitch sometimes, when adding row/root on top and
         * scrollbar is updated */
        gtk_widget_queue_draw (GTK_WIDGET (tree));
        if (!priv->filling_list)
            check_statuses (tree, STATUS_CHANGED_ON_CONTENT);
        return TRUE;
    }

    task = donna_provider_has_node_children_task (provider, node,
            priv->node_types, &err);
    if (task)
    {
        struct node_children_data *data;

        data = g_slice_new0 (struct node_children_data);
        data->tree  = tree;
        data->iter  = iter;
        watch_iter (tree, &data->iter);

        /* insert a fake node so the user can ask for expansion right away (the
         * node will disappear if needed asap) */
        gtk_tree_store_insert_with_values (priv->store,
                NULL, &data->iter, 0,
                TREE_COL_NODE,  NULL,
                -1);

        donna_task_set_callback (task,
                (task_callback_fn) node_has_children_cb,
                data,
                (GDestroyNotify) free_node_children_data);
        donna_app_run_task (priv->app, task);
    }
    else
    {
        gchar *location;

        /* insert a fake node, so user can try again by asking to expand it */
        gtk_tree_store_insert_with_values (priv->store, NULL, &iter, 0,
            TREE_COL_NODE,  NULL,
            -1);

        location = donna_node_get_location (node);
        g_warning ("TreeView '%s': Unable to create a task to determine "
                "if the node '%s:%s' has children: %s",
                priv->name, domain, location, err->message);
        g_free (location);
        g_clear_error (&err);
    }

    if (!priv->filling_list)
    {
        check_statuses (tree, STATUS_CHANGED_ON_CONTENT);
        /* fix some weird glitch sometimes, when adding row/root on top and
         * scrollbar is updated */
        gtk_widget_queue_draw ((GtkWidget *) tree);
    }

    return TRUE;
}

/**
 * donna_tree_view_add_root:
 * @tree: A #DonnaTreeView
 * @node: The #DonnaNode to add as root
 * @error: (allow-none): Return location of a #GError, or %NULL
 *
 * Adds a new root @node in @tree
 *
 * This obviously only works on trees.
 *
 * Returns: %TRUE if the new root was added, else %FALSE
 */
gboolean
donna_tree_view_add_root (DonnaTreeView *tree, DonnaNode *node, GError **error)
{
    gboolean ret;

    g_return_val_if_fail (DONNA_IS_TREE_VIEW (tree), FALSE);

    if (!tree->priv->is_tree)
    {
        g_set_error (error, DONNA_TREE_VIEW_ERROR,
                DONNA_TREE_VIEW_ERROR_INVALID_MODE,
                "TreeView '%s': Cannot add root in mode List", tree->priv->name);
        return FALSE;
    }

    /* always add root, so we don't filter/care for show_hidden */
    ret = add_node_to_tree (tree, NULL, node, NULL);
    if (!tree->priv->arrangement)
        donna_tree_view_build_arrangement (tree, FALSE);
    else
        check_children_post_expand (tree, NULL);
    return ret;
}

/* mode list only -- this is used to disallow dropping a column to the right of
 * the empty column (to make blank space there) */
static gboolean
col_drag_func (GtkTreeView          *treev,
               GtkTreeViewColumn    *col,
               GtkTreeViewColumn    *prev_col,
               GtkTreeViewColumn    *next_col,
               gpointer              data)
{
    if (!next_col && !get_column_by_column ((DonnaTreeView *) treev, prev_col))
        return FALSE;
    else
        return TRUE;
}

static void handle_click (DonnaTreeView     *tree,
                          DonnaClick         click,
                          GdkEventButton    *event,
                          GtkTreeIter       *iter,
                          GtkTreeViewColumn *column,
                          GtkCellRenderer   *renderer,
                          guint              click_on);

static gboolean
column_button_press_event_cb (GtkWidget         *btn,
                              GdkEventButton    *event,
                              struct column     *column)
{
    DonnaTreeViewPrivate *priv = column->tree->priv;
    DonnaClick click = DONNA_CLICK_SINGLE;
    gboolean just_focused;

    /* if app's main window just got focused, we ignore this click */
    g_object_get (priv->app, "just-focused", &just_focused, NULL);
    if (just_focused)
    {
        g_object_set (priv->app, "just-focused", FALSE, NULL);
        gtk_widget_grab_focus ((GtkWidget *) column->tree);
        return TRUE;
    }

    if (event->button == 1)
        click |= DONNA_CLICK_LEFT;
    else if (event->button == 2)
        click |= DONNA_CLICK_MIDDLE;
    else if (event->button == 3)
        click |= DONNA_CLICK_RIGHT;

    priv->on_release_triggered = FALSE;
    handle_click (column->tree, click, event, NULL, column->column, NULL,
            CLICK_ON_COLHEADER);

    return FALSE;
}

/* we have a "special" handling of clicks on column headers. First off, we
 * don't use gtk_tree_view_column_set_sort_column_id() to handle the sorting
 * because we want control to do things like have a default order (ASC/DESC)
 * based on the type, etc
 * Then, we also don't use the signal clicked because we want to provide
 * support for a second sort order, which is why instead we're connecting to
 * signals of the button making the column header.
 * - clicks are processed like any other, so for things to work as expected when
 *   it comes to dragging, colheader_left_click_on_rls should be true (it is by
 *   default, but that's still a .conf thing)
 * - we only validate/trigger on rls if within dbl-click distance of press event
 */
static gboolean
column_button_release_event_cb (GtkWidget       *btn,
                                GdkEventButton  *event,
                                struct column   *column)
{
    DonnaTreeViewPrivate *priv = column->tree->priv;

    if (priv->on_release_click)
    {
        gint distance;

        g_object_get (gtk_settings_get_default (),
                "gtk-double-click-distance",    &distance,
                NULL);

        /* only validate/trigger the click on release if it's within dbl-click
         * distance of the press event */
        if ((ABS (event->x - priv->on_release_x) <= distance)
                && ABS (event->y - priv->on_release_y) <= distance)
            handle_click (column->tree, priv->on_release_click, event,
                    NULL, column->column, NULL, CLICK_ON_COLHEADER);

        priv->on_release_click = 0;
    }
    else
        priv->on_release_triggered = TRUE;

    return FALSE;
}

static inline void
set_second_arrow (DonnaTreeView *tree)
{
    DonnaTreeViewPrivate *priv = tree->priv;
    struct column *_col;
    gboolean alt;
    const gchar *icon_name;

    /* GTK settings whether to use sane/alternative arrows or not */
    g_object_get (gtk_widget_get_settings (GTK_WIDGET (tree)),
            "gtk-alternative-sort-arrows", &alt, NULL);

    if (priv->second_sort_order == GTK_SORT_ASCENDING)
        icon_name = (alt) ? "pan-up-symbolic" : "pan-down-symbolic";
    else
        icon_name = (alt) ? "pan-down-symbolic" : "pan-up-symbolic";

    /* show/update the second arrow */
    _col = get_column_by_column (tree, priv->second_sort_column);
    gtk_image_set_from_icon_name ((GtkImage *) _col->second_arrow,
            icon_name, GTK_ICON_SIZE_MENU);
    /* visible unless main & second sort are the same */
    gtk_widget_set_visible (_col->second_arrow,
            priv->second_sort_column != priv->sort_column);

    DONNA_DEBUG (TREE_VIEW, priv->name,
            g_debug4 ("TreeView '%s': set second arrow to %s on %s (%d)",
                priv->name,
                icon_name,
                _col->name,
                priv->second_sort_column != priv->sort_column));
}

static void
set_sort_column (DonnaTreeView      *tree,
                 GtkTreeViewColumn  *column,
                 DonnaSortOrder      order,
                 gboolean            preserve_order)
{
    DonnaTreeViewPrivate *priv = tree->priv;
    struct column *_col;
    GtkTreeSortable *sortable;
    gint cur_sort_id;
    GtkSortType cur_sort_order;
    GtkSortType sort_order;

    _col = get_column_by_column (tree, column);
    DONNA_DEBUG (TREE_VIEW, priv->name,
            g_debug ("TreeView '%s': set sort on %s (%s)",
                priv->name,
                (_col) ? _col->name : "-",
                (order == DONNA_SORT_ASC) ? "asc" :
                    (order == DONNA_SORT_DESC) ? "desc" :
                        (preserve_order) ? "preserve" : "reverse"));

    sortable = GTK_TREE_SORTABLE (priv->store);
    gtk_tree_sortable_get_sort_column_id (sortable, &cur_sort_id, &cur_sort_order);

    if (priv->sort_column != column)
    {
        gboolean refresh_second_arrow = FALSE;

        /* new main sort on second sort column, remove the arrow */
        if (priv->second_sort_column == column)
            gtk_widget_set_visible (_col->second_arrow, FALSE);
        /* if not sticky, also remove the second sort */
        if (!priv->second_sort_sticky)
        {
            if (priv->second_sort_column && priv->second_sort_column != column)
                gtk_widget_set_visible (get_column_by_column (tree,
                            priv->second_sort_column)->second_arrow,
                        FALSE);
            priv->second_sort_column = NULL;
        }
        /* if sticky, and the old main sort is the second sort, bring back
         * the arrow (second sort is automatic, i.e. done when the second
         * sort column is set and isn't the main sort column, of course) */
        else if (priv->second_sort_column == priv->sort_column && priv->sort_column)
        {
            gtk_widget_set_visible (get_column_by_column (tree,
                        priv->second_sort_column)->second_arrow,
                    TRUE);
            /* we need to call set_second_arrow() after we've updated
             * priv->sort_colmun, else since it's the same as second_sort_column
             * it won't make the arrow visible */
            refresh_second_arrow = TRUE;
        }

        /* handle the change of main sort column */
        if (priv->sort_column)
            gtk_tree_view_column_set_sort_indicator (priv->sort_column, FALSE);
        priv->sort_column = column;
        if (order != DONNA_SORT_UNKNOWN)
            sort_order = (order == DONNA_SORT_ASC)
                ? GTK_SORT_ASCENDING : GTK_SORT_DESCENDING;
        else
            sort_order = donna_column_type_get_default_sort_order (_col->ct,
                    _col->name,
                    (priv->arrangement) ? priv->arrangement->columns_options : NULL,
                    priv->name,
                    priv->is_tree,
                    _col->ct_data);
        if (refresh_second_arrow)
            set_second_arrow (tree);
    }
    else if (order != DONNA_SORT_UNKNOWN)
    {
        sort_order = (order == DONNA_SORT_ASC)
            ? GTK_SORT_ASCENDING : GTK_SORT_DESCENDING;
        if (sort_order == cur_sort_order)
            return;
    }
    else if (preserve_order)
        return;
    else
        /* revert order */
        sort_order = (cur_sort_order == GTK_SORT_ASCENDING)
            ? GTK_SORT_DESCENDING : GTK_SORT_ASCENDING;

    /* important to set the sort order on column before the sort_id on sortable,
     * since sort_func might use the column's sort_order (when putting container
     * always first) */
    gtk_tree_view_column_set_sort_indicator (column, TRUE);
    gtk_tree_view_column_set_sort_order (column, sort_order);
    gtk_tree_sortable_set_sort_column_id (sortable, _col->sort_id, sort_order);
}


static void
set_second_sort_column (DonnaTreeView       *tree,
                        GtkTreeViewColumn   *column,
                        DonnaSortOrder       order,
                        gboolean             preserve_order)
{
    DonnaTreeViewPrivate *priv = tree->priv;
    struct column *_col;

    _col = get_column_by_column (tree, column);
    DONNA_DEBUG (TREE_VIEW, priv->name,
            g_debug ("TreeView '%s': set second sort on %s (%s)",
                priv->name,
                (_col) ? _col->name : "-",
                (order == DONNA_SORT_ASC) ? "asc" :
                    (order == DONNA_SORT_DESC) ? "desc" :
                        (preserve_order) ? "preserve" : "reverse"));

    if (!column || priv->sort_column == column)
    {
        if (priv->second_sort_column)
            gtk_widget_set_visible (get_column_by_column (tree,
                        priv->second_sort_column)->second_arrow,
                    FALSE);
        priv->second_sort_column = (priv->second_sort_sticky) ? column : NULL;
        return;
    }

    if (priv->second_sort_column != column)
    {
        if (priv->second_sort_column)
            gtk_widget_set_visible (get_column_by_column (tree,
                        priv->second_sort_column)->second_arrow,
                    FALSE);
        priv->second_sort_column = column;
        if (order != DONNA_SORT_UNKNOWN)
            priv->second_sort_order = (order == DONNA_SORT_ASC)
                ? GTK_SORT_ASCENDING : GTK_SORT_DESCENDING;
        else
            priv->second_sort_order = donna_column_type_get_default_sort_order (
                    _col->ct,
                    _col->name,
                    (priv->arrangement) ? priv->arrangement->columns_options : NULL,
                    priv->name,
                    priv->is_tree,
                    _col->ct_data);
    }
    else if (order != DONNA_SORT_UNKNOWN)
    {
        GtkSortType sort_order;

        sort_order = (order == DONNA_SORT_ASC)
            ? GTK_SORT_ASCENDING : GTK_SORT_DESCENDING;
        if (sort_order == priv->second_sort_order)
            return;
        priv->second_sort_order = sort_order;
    }
    else if (preserve_order)
        return;
    else
        /* revert order */
        priv->second_sort_order =
            (priv->second_sort_order == GTK_SORT_ASCENDING)
            ? GTK_SORT_DESCENDING : GTK_SORT_ASCENDING;

    /* show/update the second arrow */
    set_second_arrow (tree);

    /* trigger a resort */
    resort_tree (tree);
}

static inline void
free_arrangement (DonnaArrangement *arr)
{
    if (!arr)
        return;
    g_free (arr->columns);
    g_free (arr->main_column);
    g_free (arr->columns_source);
    g_free (arr->sort_column);
    g_free (arr->sort_source);
    g_free (arr->second_sort_column);
    g_free (arr->second_sort_source);
    g_free (arr->columns_options);
    if (arr->color_filters)
        g_slist_free_full (arr->color_filters, g_object_unref);
    g_free (arr);
}

static gint
no_sort (GtkTreeModel *model, GtkTreeIter *i1, GtkTreeIter *i2, gpointer data)
{
    g_critical ("TreeView '%s': Invalid sorting function called",
            ((DonnaTreeView *) data)->priv->name);
    return 0;
}

/* those must only be used on arrangement from select_arrangement(), i.e. they
 * always have all elements (except maybe second_sort). Hence why we don't check
 * for that (again, except second_sort) */
#define must_load_columns(arr, cur_arr, force)                              \
     (force || !cur_arr || arr->flags & DONNA_ARRANGEMENT_COLUMNS_ALWAYS    \
         || !streq (cur_arr->columns, arr->columns))
#define must_load_sort(arr, cur_arr, force)                                 \
     (force || !cur_arr || arr->flags & DONNA_ARRANGEMENT_SORT_ALWAYS       \
         || !(cur_arr->sort_order == arr->sort_order                        \
             && streq (cur_arr->sort_column, arr->sort_column)))
#define must_load_second_sort(arr, cur_arr, force)                          \
    (arr->flags & DONNA_ARRANGEMENT_HAS_SECOND_SORT                         \
     && (force || !cur_arr                                                  \
         || arr->flags & DONNA_ARRANGEMENT_SECOND_SORT_ALWAYS               \
         || !(cur_arr->second_sort_order == arr->second_sort_order          \
             && cur_arr->second_sort_sticky == arr->second_sort_sticky      \
             && streq (cur_arr->second_sort_column,                         \
                 arr->second_sort_column))))
#define must_load_columns_options(arr, cur_arr, force)                      \
     (force || !cur_arr                                                     \
         || arr->flags & DONNA_ARRANGEMENT_COLUMNS_OPTIONS_ALWAYS           \
         || !streq (cur_arr->columns_options, arr->columns_options))

static void
load_arrangement (DonnaTreeView     *tree,
                  DonnaArrangement  *arrangement,
                  gboolean           force)
{
    DonnaTreeViewPrivate *priv  = tree->priv;
    DonnaConfig          *config;
    GtkTreeView          *treev = (GtkTreeView *) tree;
    GtkTreeSortable      *sortable;
    GSList               *list;
    gboolean              free_sort_column = FALSE;
    gchar                *sort_column = NULL;
    DonnaSortOrder        sort_order = DONNA_SORT_UNKNOWN;
    gboolean              free_second_sort_column = FALSE;
    gchar                *second_sort_column = NULL;
    DonnaSortOrder        second_sort_order = DONNA_SORT_UNKNOWN;
    DonnaSecondSortSticky second_sort_sticky = DONNA_SECOND_SORT_STICKY_UNKNOWN;
    gchar                *col;
    GtkTreeViewColumn    *first_column = NULL;
    GtkTreeViewColumn    *last_column = NULL;
    GtkTreeViewColumn    *expander_column = NULL;
    GtkTreeViewColumn    *ctname_column = NULL;
    DonnaColumnType      *ctname;
    gint                  sort_id = 0;

    config = donna_app_peek_config (priv->app);
    sortable = (GtkTreeSortable *) priv->store;

    /* clear list of props we're watching to refresh tree */
    if (priv->col_props->len > 0)
        g_array_set_size (priv->col_props, 0);

    if (!priv->is_tree)
    {
        /* because setting it to NULL means the first visible column will be
         * used. If we don't want an expander to show (and just eat space), we
         * need to add an invisible column and set it as expander column */
        expander_column = gtk_tree_view_get_expander_column (treev);
        if (!expander_column)
        {
            expander_column = gtk_tree_view_column_new ();
            gtk_tree_view_column_set_sizing (expander_column,
                    GTK_TREE_VIEW_COLUMN_FIXED);
            gtk_tree_view_insert_column (treev, expander_column, 0);
            gtk_tree_view_column_set_visible (expander_column, FALSE);
        }
        last_column = expander_column;
    }
    /* to set default for main (tree: & expander) column */
    ctname = donna_app_get_column_type (priv->app, "name");

    col = arrangement->columns;
    /* just to be safe, but this function should only be called with arrangement
     * having (at least) columns */
    if (G_UNLIKELY (!col))
    {
        g_critical ("TreeView '%s': load_arrangement() called on an arrangement "
                "without columns",
                priv->name);
        col = (gchar *) "name";
    }

    if (must_load_sort (arrangement, priv->arrangement, force))
    {
        sort_column = arrangement->sort_column;
        sort_order  = arrangement->sort_order;
    }
    else if (priv->sort_column)
    {
        sort_column = g_strdup (get_column_by_column (tree,
                    priv->sort_column)->name);
        free_sort_column = TRUE;
        /* also preserve sort order */
        sort_order  = (gtk_tree_view_column_get_sort_order (priv->sort_column)
                == GTK_SORT_ASCENDING) ? DONNA_SORT_ASC : DONNA_SORT_DESC;
    }

    if (must_load_second_sort (arrangement, priv->arrangement, force))
    {
        second_sort_column = arrangement->second_sort_column;
        second_sort_order  = arrangement->second_sort_order;
        second_sort_sticky = arrangement->second_sort_sticky;
    }
    else if (priv->second_sort_column)
    {
        second_sort_column = g_strdup (get_column_by_column (tree,
                    priv->second_sort_column)->name);
        free_second_sort_column = TRUE;
        /* also preserve sort order */
        second_sort_order  = (gtk_tree_view_column_get_sort_order (priv->second_sort_column)
                == GTK_SORT_ASCENDING) ? DONNA_SORT_ASC : DONNA_SORT_DESC;
    }

    /* because we'll "re-fill" priv->columns, we can't keep the sort columns set
     * up, as calling set_sort_column() or set_second_sort_column() would risk
     * segfaulting, when get_column_by_column() would return NULL (because the
     * old/current columns aren't in priv->columns anymore).
     * So, we unset them both, so they can be set properly */
    if (priv->second_sort_column)
    {
        gtk_widget_set_visible (get_column_by_column (tree,
                    priv->second_sort_column)->second_arrow,
                FALSE);
        priv->second_sort_column = NULL;
    }
    if (priv->sort_column)
    {
        gtk_tree_view_column_set_sort_indicator (priv->sort_column, FALSE);
        priv->sort_column = NULL;
    }

    list = priv->columns;
    priv->columns = NULL;
    priv->main_column = NULL;

    for (;;)
    {
        struct column     *_col;
        gchar             *col_type;
        gchar             *e;
        gboolean           is_last_col;
        DonnaColumnType   *ct;
        DonnaColumnType   *col_ct;
        gboolean           force_load_options = FALSE;
        gint               width;
        gchar             *title;
        GSList            *l;
        GSList            *ll;
        GtkTreeViewColumn *column;
        GtkCellRenderer   *renderer;
        const gchar       *rend;
        gint               index;
        gchar              buf[64];
        gchar             *b = buf;

        e = strchrnul (col, ',');
        is_last_col = (*e == '\0');
        if (!is_last_col)
            *e = '\0';

        if (!donna_config_get_string (config, NULL, &col_type,
                    "defaults/%s/columns/%s/type",
                    (priv->is_tree) ? "trees" : "lists", col))
        {
            g_warning ("TreeView '%s': No type defined for column '%s', "
                    "fallback to its name",
                    priv->name, col);
            col_type = NULL;
        }

        /* ct "line-number" is a special one, which is handled by the treeview
         * itself (only supported in mode list) to show line numbers */
        if (!priv->is_tree && streq (col_type, "line-number"))
            ct = (DonnaColumnType *) g_object_ref (tree);
        else
        {
            ct = donna_app_get_column_type (priv->app, (col_type) ? col_type : col);
            if (!ct)
            {
                g_critical ("TreeView '%s': Unable to load column-type '%s' for column '%s'",
                        priv->name, (col_type) ? col_type : col, col);
                goto next;
            }
        }
        /* look to re-user the same column if possible; if not, look for an
         * existing column of the same type (that won't be re-used) */
        column = NULL;
        for (ll = NULL, l = list; l; l = l->next)
        {
            _col = l->data;

            /* must be the same columntype */
            if (_col->ct != ct)
                continue;

            /* if the same column, re-use */
            if (streq (_col->name, col))
            {
                ll = l;

                col_ct = _col->ct;
                column = _col->column;
                /* column has a ref already, we can release ours */
                g_object_unref (ct);

                if (must_load_columns_options (arrangement, priv->arrangement, force))
                    /* refresh data to load new options */
                    donna_column_type_refresh_data (ct, col,
                            arrangement->columns_options,
                            priv->name,
                            priv->is_tree,
                            &_col->ct_data);
                break;
            }

            /* if we already have a ct to use, we're just looking the for same
             * column to re-use if possible */
            if (ll)
                continue;

            /* will it be used for another column? */
            if (!is_last_col)
            {
                gchar *s;

                s = strstr (e + 1, _col->name);
                if (s)
                {
                    s += strlen (_col->name);
                    if (*s == '\0' || *s == ',')
                        continue;
                }
            }

            /* mark it -- we'll use it if we don't find a match */
            ll = l;
        }

        if (!column && ll)
        {
            _col = ll->data;

            col_ct = _col->ct;
            column = _col->column;
            /* column has a ref already, we can release ours */
            g_object_unref (ct);

            g_free (_col->name);
            _col->name = g_strdup (col);
            donna_column_type_free_data (ct, _col->ct_data);
            _col->ct_data = NULL;
            donna_column_type_refresh_data (ct, col,
                    arrangement->columns_options,
                    priv->name,
                    priv->is_tree,
                    &_col->ct_data);
            /* so width & title are reloaded */
            force_load_options = TRUE;
        }

        if (column)
        {
            /* move column */
            gtk_tree_view_move_column_after (treev, column, last_column);

            list = g_slist_delete_link (list, ll);
            priv->columns = g_slist_prepend (priv->columns, _col);
        }
        else
        {
            GtkWidget *btn;
            GtkWidget *hbox;
            GtkWidget *label;
            GtkWidget *arrow;

            _col = g_slice_new0 (struct column);
            _col->tree = tree;
            /* create renderer(s) & column */
            _col->column = column = gtk_tree_view_column_new ();
            _col->name = g_strdup (col);
            /* data for use in render, node_cmp, etc */
            donna_column_type_refresh_data (ct, col,
                    arrangement->columns_options,
                    priv->name,
                    priv->is_tree,
                    &_col->ct_data);
            /* give our ref on the ct to the column */
            _col->ct = ct;
            /* add to our list of columns (order doesn't matter) */
            priv->columns = g_slist_prepend (priv->columns, _col);
            /* to test for expander column */
            col_ct = ct;
            /* so width & title are reloaded */
            force_load_options = TRUE;
            /* sizing stuff */
            gtk_tree_view_column_set_sizing (column, GTK_TREE_VIEW_COLUMN_FIXED);
            if (!priv->is_tree)
            {
                gtk_tree_view_column_set_resizable (column, TRUE);
                gtk_tree_view_column_set_reorderable (column, TRUE);
            }
            /* put our internal renderers */
            for (index = 0; index < NB_INTERNAL_RENDERERS; ++index)
            {
                gtk_tree_view_column_set_cell_data_func (column,
                        int_renderers[index],
                        rend_func,
                        GINT_TO_POINTER (index),
                        NULL);
                gtk_tree_view_column_pack_start (column, int_renderers[index],
                        FALSE);
            }
            /* load renderers */
            rend = donna_column_type_get_renderers (ct);
            _col->renderers = g_ptr_array_sized_new ((guint) strlen (rend));
            for ( ; *rend; ++index, ++rend)
            {
                GtkCellRenderer **r;
                GtkCellRenderer * (*load_renderer) (void);
                /* TODO: use an external (app-global) renderer loader? */
                switch (*rend)
                {
                    case DONNA_COLUMN_TYPE_RENDERER_TEXT:
                        r = &priv->renderers[RENDERER_TEXT];
                        load_renderer = donna_cell_renderer_text_new;
                        break;
                    case DONNA_COLUMN_TYPE_RENDERER_PIXBUF:
                        r = &priv->renderers[RENDERER_PIXBUF];
                        load_renderer = gtk_cell_renderer_pixbuf_new;
                        break;
                    case DONNA_COLUMN_TYPE_RENDERER_PROGRESS:
                        r = &priv->renderers[RENDERER_PROGRESS];
                        load_renderer = gtk_cell_renderer_progress_new;
                        break;
                    case DONNA_COLUMN_TYPE_RENDERER_COMBO:
                        r = &priv->renderers[RENDERER_COMBO];
                        load_renderer = gtk_cell_renderer_combo_new;
                        break;
                    case DONNA_COLUMN_TYPE_RENDERER_TOGGLE:
                        r = &priv->renderers[RENDERER_TOGGLE];
                        load_renderer = gtk_cell_renderer_toggle_new;
                        break;
                    case DONNA_COLUMN_TYPE_RENDERER_SPINNER:
                        r = &priv->renderers[RENDERER_SPINNER];
                        load_renderer = gtk_cell_renderer_spinner_new;
                        break;
                    default:
                        g_critical ("TreeView '%s': Unknown renderer type '%c' for column '%s'",
                                priv->name, *rend, col);
                        continue;
                }
                if (!*r)
                {
                    /* FIXME use a weakref instead? */
                    renderer = *r = g_object_ref (load_renderer ());
                    g_object_set_data ((GObject * ) renderer, "renderer-type",
                            GINT_TO_POINTER (*rend));
                    /* an array where we'll store properties that have been set
                     * by the ct, so we can reset them before next use.
                     * See donna_renderer_set() for more */
                    g_object_set_data_full ((GObject *) renderer, "renderer-props",
                            /* 4: random. There probably won't be more than 4
                             * properties per renderer, is a guess */
                            g_ptr_array_new_full (4, g_free),
                            (GDestroyNotify) g_ptr_array_unref);
                }
                else
                    renderer = *r;
                g_ptr_array_add (_col->renderers, renderer);
                gtk_tree_view_column_set_cell_data_func (column, renderer,
                        rend_func, GINT_TO_POINTER (index), NULL);
                gtk_tree_view_column_pack_start (column, renderer, FALSE);
            }
            /* add it (we add now because we can't get the button (to connect)
             * until it's been added to the treev) */
            gtk_tree_view_append_column (treev, column);
            gtk_tree_view_move_column_after (treev, column, last_column);
            /* click on column header stuff -- see
             * column_button_release_event_cb() for more about this */
            btn = gtk_tree_view_column_get_button (column);
            g_signal_connect (btn, "button-press-event",
                    G_CALLBACK (column_button_press_event_cb), _col);
            g_signal_connect (btn, "button-release-event",
                    G_CALLBACK (column_button_release_event_cb), _col);
            /* we handle the header stuff so we can add our own arrow (for
             * second sort) */
            hbox = gtk_box_new (GTK_ORIENTATION_HORIZONTAL, 0);
            label = gtk_label_new (NULL);
            arrow = gtk_image_new_from_icon_name ("pan-down-symbolic", GTK_ICON_SIZE_MENU);
            gtk_style_context_add_class (gtk_widget_get_style_context (arrow),
                    "second-arrow");
            gtk_box_pack_start (GTK_BOX (hbox), label, TRUE, TRUE, 0);
            gtk_box_pack_end (GTK_BOX (hbox), arrow, FALSE, FALSE, 0);
            gtk_tree_view_column_set_widget (column, hbox);
            gtk_widget_show (hbox);
            gtk_widget_show (label);
            /* so we can access/update things */
            _col->label = label;
            _col->second_arrow = arrow;
            /* lastly */
            gtk_tree_view_column_set_clickable (column, TRUE);
        }

        if (!first_column && col_ct != (DonnaColumnType *) tree)
            first_column = column;

        if (!ctname_column && col_ct == ctname)
            ctname_column = column;

        if (!priv->main_column && arrangement->main_column
                && streq (col, arrangement->main_column))
            priv->main_column = column;

        if (force_load_options
                || must_load_columns_options (arrangement, priv->arrangement, force))
        {
            /* size */
            if (snprintf (buf, 64, "column_types/%s", col_type) >= 64)
                b = g_strdup_printf ("column_types/%s", col_type);
            width = donna_config_get_int_column (config, col,
                    arrangement->columns_options, priv->name, priv->is_tree, b,
                    "width", 230);
            gtk_tree_view_column_set_min_width (column, 23);
            gtk_tree_view_column_set_fixed_width (column, width);
            if (b != buf)
            {
                g_free (b);
                b = buf;
            }

            /* title */
            title = donna_config_get_string_column (config, col,
                    arrangement->columns_options, priv->name, priv->is_tree, NULL,
                    "title", col);
            gtk_tree_view_column_set_title (column, title);
            gtk_label_set_text (GTK_LABEL (_col->label), title);
            g_free (title);

            /* refresh_properties */
            _col->refresh_properties = (guint) donna_config_get_int_column (config, col,
                    arrangement->columns_options, priv->name, priv->is_tree, NULL,
                    "refresh_properties", RP_VISIBLE);
            if (_col->refresh_properties >= _MAX_RP)
                _col->refresh_properties = RP_VISIBLE;
        }

        /* for line-number columns, there's no properties to watch, and this
         * shouldn't trigger a warning, obviously. Sorting also doesn't apply
         * there. */
        if (ct != (DonnaColumnType *) tree)
        {
            /* props to watch for refresh */
            add_col_props (tree, _col);

            /* sort -- (see column_button_release_event_cb() for more) */
            _col->sort_id = sort_id;
            /* FIXME this causes a re-sort of the treeview when toggling a
             * column. With lots of rows, it's useless slowness... */
            gtk_tree_sortable_set_sort_func (sortable, sort_id,
                    (GtkTreeIterCompareFunc) sort_func, column, NULL);
            if (sort_column && streq (sort_column, col))
            {
                if (free_sort_column)
                {
                    g_free (sort_column);
                    free_sort_column = FALSE;
                }
                sort_column = NULL;
                set_sort_column (tree, column, sort_order, TRUE);
            }
            ++sort_id;

            /* second sort order -- only if main sort has been set (else we'd
             * end up trying to sort by invalid main sort, and segfault or
             * something) */
            if (priv->sort_column
                    && second_sort_column && streq (second_sort_column, col))
            {
                if (free_second_sort_column)
                {
                    g_free (second_sort_column);
                    free_second_sort_column = FALSE;
                }
                second_sort_column = NULL;

                if (second_sort_sticky != DONNA_SECOND_SORT_STICKY_UNKNOWN)
                    priv->second_sort_sticky =
                        second_sort_sticky == DONNA_SECOND_SORT_STICKY_ENABLED;

                set_second_sort_column (tree, column, second_sort_order, TRUE);
            }
        }

        last_column = column;

next:
        g_free (col_type);
        if (is_last_col)
            break;
        *e = ',';
        col = e + 1;
    }
    g_object_unref (ctname);

    /* have our columns in their actual order */
    priv->columns = g_slist_reverse (priv->columns);

    /* ensure we have an expander column */
    if (!expander_column)
        expander_column = (ctname_column) ? ctname_column : first_column;

    /* ensure we have a main column */
    if (!priv->main_column)
        priv->main_column = (ctname_column) ? ctname_column : first_column;

    if (!priv->is_tree && !priv->blank_column)
    {
        /* we add an extra (empty) column, so we can have some
         * free/blank space on the right, instead of having the last
         * column to be used to fill the space and whatnot */
        priv->blank_column = gtk_tree_view_column_new ();
        gtk_tree_view_column_set_sizing (priv->blank_column, GTK_TREE_VIEW_COLUMN_FIXED);
        g_object_set (priv->blank_column, "expand", TRUE, NULL);
        gtk_tree_view_insert_column (treev, priv->blank_column, -1);
    }

    /* set expander column */
    gtk_tree_view_set_expander_column (treev, expander_column);

#ifdef GTK_IS_JJK
    if (priv->select_highlight == SELECT_HIGHLIGHT_COLUMN
            || priv->select_highlight == SELECT_HIGHLIGHT_COLUMN_UNDERLINE)
        gtk_tree_view_set_select_highlight_column (treev, priv->main_column);
    else if (priv->select_highlight == SELECT_HIGHLIGHT_UNDERLINE)
    {
        /* since we only want an underline, we must set the select highlight
         * column to a non-visible one */
        if (priv->is_tree)
        {
            /* tree never uses an empty column on the right, so we store the
             * extra non-visible column used for this */
            if (!priv->blank_column)
            {
                priv->blank_column = gtk_tree_view_column_new ();
                gtk_tree_view_column_set_sizing (priv->blank_column,
                        GTK_TREE_VIEW_COLUMN_FIXED);
                gtk_tree_view_insert_column (treev, priv->blank_column, -1);
            }
            gtk_tree_view_set_select_highlight_column (treev, priv->blank_column);
        }
        else
            /* list: expander_column is always set to a non-visible one */
            gtk_tree_view_set_select_highlight_column (treev, expander_column);
    }
    gtk_tree_view_set_select_row_underline (treev,
            priv->select_highlight == SELECT_HIGHLIGHT_UNDERLINE
            || priv->select_highlight == SELECT_HIGHLIGHT_COLUMN_UNDERLINE);
#endif

    /* failed to set sort order */
    if (free_sort_column)
        g_free (sort_column);
    if (sort_column || !priv->sort_column)
        set_sort_column (tree, first_column, DONNA_SORT_UNKNOWN, TRUE);

    /* failed to set second sort order */
    if (second_sort_column)
    {
        struct column *_col;

        /* try to get the column, as this might not have been set only because
         * we hadn't set the main sort first (which is required) */
        _col = get_column_by_name (tree, second_sort_column);
        if (_col)
            set_second_sort_column (tree, _col->column, second_sort_order, TRUE);
        else
            set_second_sort_column (tree, first_column, DONNA_SORT_UNKNOWN, TRUE);

        if (free_second_sort_column)
            g_free (second_sort_column);
    }

    /* remove all columns left unused */
    while (list)
    {
        struct column *_col = list->data;

        /* though we should never try to sort by a sort_id not used by a column,
         * let's make sure if that happens, we just get a warning (instead of
         * dereferencing a pointer pointing nowhere) */
        gtk_tree_sortable_set_sort_func (sortable, sort_id++, no_sort, tree, NULL);
        /* free associated data */
        g_free (_col->name);
        donna_column_type_free_data (_col->ct, _col->ct_data);
        g_object_unref (_col->ct);
#ifndef GTK_TREEVIEW_REMOVE_COLUMN_FIXED
        /* "Fix" a bug in GTK that doesn't take care of the button properly.
         * This is a memory leak (button doesn't get unref-d/finalized), but
         * could also cause a few issues for us:
         * - it could lead to a column header (the button) not having its hover
         *   effect done on the right area, still using info from an old
         *   button. Could look bad, could also make it impossible to click a
         *   column!
         * - On button-release-event the wrong signal handler would get called,
         *   leading to use of a free-d memory, and segfault.
         */
        GtkWidget *btn;
        btn = gtk_tree_view_column_get_button (_col->column);
#endif
        gtk_tree_view_remove_column (treev, _col->column);
        g_ptr_array_unref (_col->renderers);
#ifndef GTK_TREEVIEW_REMOVE_COLUMN_FIXED
        gtk_widget_unparent (btn);
#endif
        g_slice_free (struct column, _col);
        list = g_slist_delete_link (list, list);
    }

    /* remove any column_filter we had loaded */
    g_slist_free_full (priv->columns_filter, (GDestroyNotify) free_column_filter);
    priv->columns_filter = NULL;
}

static gboolean
select_arrangement_accumulator (GSignalInvocationHint  *hint,
                                GValue                 *return_accu,
                                const GValue           *return_handler,
                                gpointer                data)
{
    DonnaArrangement *arr_accu;
    DonnaArrangement *arr_handler;
    gboolean keep_emission = TRUE;

    arr_accu    = g_value_get_pointer (return_accu);
    arr_handler = g_value_get_pointer (return_handler);

    /* nothing in accu but something in handler, probably the first handler */
    if (!arr_accu && arr_handler)
    {
        g_value_set_pointer (return_accu, arr_handler);
        if (arr_handler->priority == DONNA_ARRANGEMENT_PRIORITY_OVERRIDE)
            keep_emission = FALSE;
    }
    /* something in accu & in handler */
    else if (arr_handler)
    {
        if (arr_handler->priority > arr_accu->priority)
        {
            free_arrangement (arr_accu);
            g_value_set_pointer (return_accu, arr_handler);
            if (arr_handler->priority == DONNA_ARRANGEMENT_PRIORITY_OVERRIDE)
                keep_emission = FALSE;
        }
        else
            free_arrangement (arr_handler);
    }

    return keep_emission;
}

static inline DonnaArrangement *
select_arrangement (DonnaTreeView *tree, DonnaNode *location)
{
    DonnaTreeViewPrivate *priv = tree->priv;
    DonnaConfig *config;
    DonnaArrangement *arr = NULL;
    gchar *s;

    /* list only: emit select-arrangement */
    if (!priv->is_tree)
        g_signal_emit (tree, donna_tree_view_signals[SIGNAL_SELECT_ARRANGEMENT], 0,
                priv->name, location, &arr);

    if (!arr)
        arr = g_new0 (DonnaArrangement, 1);

    config = donna_app_peek_config (priv->app);

    if (!(arr->flags & DONNA_ARRANGEMENT_HAS_COLUMNS))
        /* try loading our from our own arrangement */
        if (!donna_config_arr_load_columns (config, arr,
                    "tree_views/%s/arrangement", priv->name))
            /* fallback on default for our mode */
            if (!donna_config_arr_load_columns (config, arr,
                        "defaults/%s/arrangement",
                        (priv->is_tree) ? "trees" : "lists"))
            {
                /* if all else fails, use a column "name" */
                arr->columns = g_strdup ("name");
                arr->flags |= DONNA_ARRANGEMENT_HAS_COLUMNS;
            }

    if (!(arr->flags & DONNA_ARRANGEMENT_HAS_SORT))
        if (!donna_config_arr_load_sort (config, arr,
                    "tree_views/%s/arrangement", priv->name)
                && !donna_config_arr_load_sort (config, arr,
                    "defaults/%s/arrangement",
                    (priv->is_tree) ? "trees" : "lists"))
        {
            /* we can't find anything, default to first column */
            s = strchr (arr->columns, ',');
            if (s)
                arr->sort_column = g_strndup (arr->columns,
                        (gsize) (s - arr->columns));
            else
                arr->sort_column = g_strdup (arr->columns);
            arr->flags |= DONNA_ARRANGEMENT_HAS_SORT;
        }

    /* Note: even here, this one is optional */
    if (!(arr->flags & DONNA_ARRANGEMENT_HAS_SECOND_SORT))
        if (!donna_config_arr_load_second_sort (config, arr,
                    "tree_views/%s/arrangement",
                    priv->name))
            donna_config_arr_load_second_sort (config, arr,
                    "defaults/%s/arrangement",
                    (priv->is_tree) ? "trees" : "lists");

    if (!(arr->flags & DONNA_ARRANGEMENT_HAS_COLUMNS_OPTIONS))
    {
        if (!donna_config_arr_load_columns_options (config, arr,
                    "tree_views/%s/arrangement",
                    priv->name)
                && !donna_config_arr_load_columns_options (config, arr,
                    "defaults/%s/arrangement",
                    (priv->is_tree) ? "trees" : "lists"))
            /* else: we say we have something, it is NULL. This will force
             * updating the columntype-data without using an arr_name */
            arr->flags |= DONNA_ARRANGEMENT_HAS_COLUMNS_OPTIONS;
    }

    if (!(arr->flags & DONNA_ARRANGEMENT_HAS_COLOR_FILTERS))
    {
        if (!donna_config_arr_load_color_filters (config, priv->app, arr,
                    "tree_views/%s/arrangement", priv->name))
            donna_config_arr_load_color_filters (config, priv->app, arr,
                    "defaults/%s/arrangement",
                    (priv->is_tree) ? "trees" : "lists");

        /* special: color filters might have been loaded with a type COMBINE,
         * which resulted in them loaded but no flag set (in order to keep
         * loading others from other arrangements). In such a case, we now need
         * to set the flag */
        if (!(arr->flags & DONNA_ARRANGEMENT_HAS_COLOR_FILTERS)
                && arr->color_filters)
            arr->flags |= DONNA_ARRANGEMENT_HAS_COLOR_FILTERS;
    }

    return arr;
}

void
donna_tree_view_build_arrangement (DonnaTreeView *tree, gboolean force)
{
    DonnaTreeViewPrivate *priv;
    DonnaArrangement *arr = NULL;

    g_return_if_fail (DONNA_IS_TREE_VIEW (tree));

    priv = tree->priv;
    DONNA_DEBUG (TREE_VIEW, priv->name,
            gchar *fl = NULL;
            if (priv->location)
                fl = donna_node_get_full_location (priv->location);
            g_debug2 ("TreeView '%s': build arrangement for '%s' (force=%d)",
                priv->name, (fl) ? fl : "-", force);
            g_free (fl));

    arr = select_arrangement (tree, priv->location);

    if (must_load_columns (arr, priv->arrangement, force))
        load_arrangement (tree, arr, force);
    else
    {
        DonnaConfig *config;
        GSList *l;
        gboolean need_sort;
        gboolean need_second_sort;
        gboolean need_columns_options;

        config = donna_app_peek_config (priv->app);
        need_sort = must_load_sort (arr, priv->arrangement, force);
        need_second_sort = must_load_second_sort (arr, priv->arrangement, force);
        need_columns_options = must_load_columns_options (arr, priv->arrangement, force);

        for (l = priv->columns; l; l = l->next)
        {
            struct column *_col = l->data;

            if (need_sort && streq (_col->name, arr->sort_column))
            {
                set_sort_column (tree, _col->column, arr->sort_order, TRUE);
                need_sort = FALSE;
            }
            if (need_second_sort && streq (_col->name, arr->second_sort_column))
            {
                set_second_sort_column (tree, _col->column,
                        arr->second_sort_order, TRUE);
                if (arr->second_sort_sticky != DONNA_SECOND_SORT_STICKY_UNKNOWN)
                    priv->second_sort_sticky =
                        arr->second_sort_sticky == DONNA_SECOND_SORT_STICKY_ENABLED;
                need_second_sort = FALSE;
            }
            if (!need_sort && !need_second_sort && !need_columns_options)
                break;

            if (need_columns_options)
            {
                gchar buf[64], *b = buf;
                gint width;
                gchar *title;
                enum rp rp;

                /* ctdata */
                donna_column_type_refresh_data (_col->ct, _col->name,
                        arr->columns_options, priv->name, priv->is_tree,
                        &_col->ct_data);

                /* size */
                if (snprintf (buf, 64, "column_types/%s",
                            donna_column_type_get_name (_col->ct)) >= 64)
                    b = g_strdup_printf ("column_types/%s",
                            donna_column_type_get_renderers (_col->ct));
                width = donna_config_get_int_column (config, _col->name,
                        arr->columns_options, priv->name, priv->is_tree, b,
                        "width", 230);
                gtk_tree_view_column_set_fixed_width (_col->column, width);
                if (b != buf)
                {
                    g_free (b);
                    b = buf;
                }

                /* title */
                title = donna_config_get_string_column (config, _col->name,
                        arr->columns_options, priv->name, priv->is_tree, NULL,
                        "title", _col->name);
                gtk_tree_view_column_set_title (_col->column, title);
                gtk_label_set_text (GTK_LABEL (_col->label), title);
                g_free (title);

                /* refresh_properties */
                rp = (guint) donna_config_get_int_column (config, _col->name,
                        arr->columns_options, priv->name, priv->is_tree, NULL,
                        "refresh_properties", RP_VISIBLE);
                if (rp < _MAX_RP)
                    _col->refresh_properties = rp;
            }
        }
    }

    free_arrangement (priv->arrangement);
    priv->arrangement = arr;
}

struct set_node_prop_data
{
    DonnaTreeView   *tree;
    DonnaNode       *node;
    gchar           *prop;
};

static void
free_set_node_prop_data (struct set_node_prop_data *data)
{
    g_free (data->prop);
    g_free (data);
}

static struct active_spinners *
get_as_for_node (DonnaTreeView  *tree,
                 DonnaNode      *node,
                 guint          *index,
                 gboolean        create)
{
    DonnaTreeViewPrivate *priv = tree->priv;
    struct active_spinners *as;
    guint i;

    /* is there already an as for this node? */
    for (i = 0; i < priv->active_spinners->len; ++i)
    {
        as = priv->active_spinners->pdata[i];
        if (as->node == node)
            break;
    }

    if (i >= priv->active_spinners->len)
    {
        if (create)
        {
            as = g_new0 (struct active_spinners, 1);
            as->node = g_object_ref (node);
            as->as_cols = g_array_new (FALSE, FALSE, sizeof (struct as_col));

            g_ptr_array_add (priv->active_spinners, as);
        }
        else
            as = NULL;
    }

    if (index)
        *index = i;

    return as;
}

static void
set_node_prop_callbak (DonnaTask                 *task,
                       gboolean                   timeout_called,
                       struct set_node_prop_data *data)
{
    DonnaTreeViewPrivate *priv = data->tree->priv;
    gboolean task_failed;
    guint i;
    GPtrArray *arr;

    task_failed = donna_task_get_state (task) == DONNA_TASK_FAILED;

    /* search column(s) linked to that prop */
    arr = g_ptr_array_sized_new (1);
    for (i = 0; i < priv->col_props->len; ++i)
    {
        struct col_prop *cp;

        cp = &g_array_index (priv->col_props, struct col_prop, i);
        if (streq (data->prop, cp->prop))
            g_ptr_array_add (arr, cp->column);
    }
    /* on the off chance there's no columns linked to that prop */
    if (arr->len == 0)
    {
        if (task_failed)
        {
            const GError *error;
            gchar *location;

            error = donna_task_get_error (task);
            location = donna_node_get_location (data->node);
            donna_app_show_error (priv->app, error,
                    "Setting property %s on '%s:%s' failed",
                    data->prop,
                    donna_node_get_domain (data->node),
                    location);
            g_free (location);
        }

        g_ptr_array_free (arr, TRUE);
        free_set_node_prop_data (data);
        return;
    }

    /* timeout called == spinners set; task failed == error message */
    if (timeout_called || task_failed)
    {
        struct active_spinners *as;
        guint as_idx; /* in case we need to remove this as */
        gboolean refresh = FALSE;

        as = get_as_for_node (data->tree, data->node, &as_idx, task_failed);
        if (!as)
            goto free;

        /* for every column using that property */
        for (i = 0; i < arr->len; ++i)
        {
            GtkTreeViewColumn *column = arr->pdata[i];
            struct as_col *as_col;
            guint j;

            /* does this as have a spinner for this column? */
            for (j = 0; j < as->as_cols->len; ++j)
            {
                as_col = &g_array_index (as->as_cols, struct as_col, j);
                if (as_col->column == column)
                    break;
            }
            if (j >= as->as_cols->len)
            {
                if (task_failed)
                {
                    struct as_col as_col_new;

                    as_col_new.column = column;
                    /* no as_col means no timeout called, so we can safely set
                     * nb to 0 */
                    as_col_new.nb = 0;
                    as_col_new.tasks = g_ptr_array_new_full (1, g_object_unref);
                    g_ptr_array_add (as_col_new.tasks, g_object_ref (task));
                    g_array_append_val (as->as_cols, as_col_new);
                    as_col = &g_array_index (as->as_cols, struct as_col, j);
                }
                else
                    continue;
            }
            else if (!timeout_called) /* implies task_failed */
                g_ptr_array_add (as_col->tasks, g_object_ref (task));

            if (!task_failed)
                g_ptr_array_remove_fast (as_col->tasks, task);

            if (timeout_called)
                --as_col->nb;

            if (as_col->nb == 0)
            {
                refresh = TRUE;
#ifndef GTK_IS_JJK
                if (task_failed)
                    /* a bug in GTK means that because when the size of renderer
                     * is first computed and renderer if not visible, it has a
                     * natural size of 0 and therefore even when it becomes
                     * visible it isn't actually drawn.
                     * This is a hack to workaround this, by enforcing the
                     * column to re-compute its size now that we'll have the
                     * renderer visible, so it should have a natural size and
                     * actually be drawn */
                    gtk_tree_view_column_queue_resize (column);
#endif
                /* can we remove this as_col? */
                if (as_col->tasks->len == 0)
                {
                    /* can we remove the whole as? */
                    if (as->as_cols->len == 1)
                        g_ptr_array_remove_index_fast (priv->active_spinners,
                                as_idx);
                    else
                        g_array_remove_index_fast (as->as_cols, j);
                }
            }
        }

        if (refresh)
        {
            GtkTreeModel *model = (GtkTreeModel *) priv->store;

            /* make sure a redraw will be done for this row, else the last
             * spinner frame stays there until a redraw happens */
            if (priv->is_tree)
            {
                GSList *list;

                /* for every row of this node */
                list = g_hash_table_lookup (priv->hashtable, data->node);
                for ( ; list; list = list->next)
                {
                    GtkTreeIter *iter = list->data;
                    GtkTreePath *path;

                    path = gtk_tree_model_get_path (model, iter);
                    gtk_tree_model_row_changed (model, path, iter);
                    gtk_tree_path_free (path);
                }
            }
            else
            {
                GtkTreeIter *iter;

                /* it should be on list; and whether it's not or not visible
                 * makes no difference here, hence a simple lookup is fine */
                iter = g_hash_table_lookup (priv->hashtable, data->node);
                if (iter)
                {
                    GtkTreePath *path;

                    path = gtk_tree_model_get_path (model, iter);
                    gtk_tree_model_row_changed (model, path, iter);
                    gtk_tree_path_free (path);
                }
            }
        }

        /* no more as == we can stop spinner_fn. If there's still one (or more)
         * but only for error messages, on its next call spinner_fn will see it
         * and stop itself */
        if (!priv->active_spinners->len && priv->active_spinners_id)
        {
            g_source_remove (priv->active_spinners_id);
            priv->active_spinners_id = 0;
            priv->active_spinners_pulse = 0;
        }
    }

free:
    g_ptr_array_free (arr, TRUE);
    free_set_node_prop_data (data);
}

static void
set_node_prop_timeout (DonnaTask *task, struct set_node_prop_data *data)
{
    DonnaTreeViewPrivate *priv = data->tree->priv;
#ifdef GTK_IS_JJK
    GtkTreeModel *model;
    GSList *list;
#endif
    guint i;
    GPtrArray *arr;
    struct active_spinners *as;

    /* search column(s) linked to that prop */
    arr = g_ptr_array_sized_new (1);
    for (i = 0; i < priv->col_props->len; ++i)
    {
        struct col_prop *cp;

        cp = &g_array_index (priv->col_props, struct col_prop, i);
        if (streq (data->prop, cp->prop))
            g_ptr_array_add (arr, cp->column);
    }
    /* on the off chance there's no columns linked to that prop */
    if (arr->len == 0)
    {
        g_ptr_array_free (arr, TRUE);
        return;
    }

    as = get_as_for_node (data->tree, data->node, NULL, TRUE);
    /* for every column using that property */
    for (i = 0; i < arr->len; ++i)
    {
        GtkTreeViewColumn *column = arr->pdata[i];
        struct as_col *as_col;
        guint j;

        /* does this as already have a spinner for this column? */
        for (j = 0; j < as->as_cols->len; ++j)
        {
            as_col = &g_array_index (as->as_cols, struct as_col, j);
            if (as_col->column == column)
                break;
        }
        if (j >= as->as_cols->len)
        {
            struct as_col as_col_new;

            as_col_new.column = column;
            as_col_new.nb = 1;
            as_col_new.tasks = g_ptr_array_new_full (1, g_object_unref);
            g_array_append_val (as->as_cols, as_col_new);
            as_col = &g_array_index (as->as_cols, struct as_col, j);

#ifndef GTK_IS_JJK
            /* a bug in GTK means that because when the size of renderer is
             * first computed and spinner if not visible, it has a natural
             * size of 0 and therefore even when it becomes visible it isn't
             * actually drawn.
             * This is a hack to workaround this, by enforcing the column to
             * re-compute its size now that we'll have the spinner visible,
             * so it should have a natural size and actually be drawn */
            gtk_tree_view_column_queue_resize (column);
#endif
        }
        else
            ++as_col->nb;

        g_ptr_array_add (as_col->tasks, g_object_ref (task));
    }

#ifdef GTK_IS_JJK
    model = (GtkTreeModel *) priv->store;
    if (priv->is_tree)
    {
        /* for every row of this node */
        list = g_hash_table_lookup (priv->hashtable, data->node);
        for ( ; list; list = list->next)
        {
            GtkTreeIter *iter = list->data;
            GtkTreePath *path;

            path = gtk_tree_model_get_path (model, iter);
            gtk_tree_model_row_changed (model, path, iter);
            gtk_tree_path_free (path);
        }
    }
    else
    {
        GtkTreeIter *iter = g_hash_table_lookup (priv->hashtable, data->node);
        if (iter)
        {
            GtkTreePath *path;

            path = gtk_tree_model_get_path (model, iter);
            gtk_tree_model_row_changed (model, path, iter);
            gtk_tree_path_free (path);
        }
    }
#endif

    if (!priv->active_spinners_id)
        priv->active_spinners_id = g_timeout_add (42,
                (GSourceFunc) spinner_fn, data->tree);

    g_ptr_array_free (arr, TRUE);
}

gboolean
donna_tree_view_set_node_property (DonnaTreeView      *tree,
                                   DonnaNode          *node,
                                   const gchar        *prop,
                                   const GValue       *value,
                                   GError           **error)
{
    DonnaTreeViewPrivate *priv;
    GError *err = NULL;
    DonnaTask *task;
    struct set_node_prop_data *data;

    g_return_val_if_fail (DONNA_IS_TREE_VIEW (tree), FALSE);
    g_return_val_if_fail (DONNA_IS_NODE (node), FALSE);
    g_return_val_if_fail (prop != NULL, FALSE);
    g_return_val_if_fail (G_IS_VALUE (value), FALSE);

    priv = tree->priv;

    /* make sure the node is on the tree. We use lookup() and not contains()
     * because we don't want nodes in the hashtable but with a NULL value (i.e.
     * filtered out on list) to be a match. If the node isn't visible, one
     * should be allowed to set a property on it.
     * Reasonning is that there can't be no GUI for it, not to trigger it nor to
     * provide feedback (spinner/error) */
    if (!g_hash_table_lookup (priv->hashtable, node))
    {
        gchar *location = donna_node_get_location (node);
        g_set_error (error, DONNA_TREE_VIEW_ERROR, DONNA_TREE_VIEW_ERROR_NOT_FOUND,
                "TreeView '%s': Cannot set property '%s' on node '%s:%s', "
                "the node is not represented in the tree view",
                priv->name,
                prop,
                donna_node_get_domain (node),
                location);
        g_free (location);
        return FALSE;
    }

    task = donna_node_set_property_task (node, prop, value, &err);
    if (!task)
    {
        gchar *fl = donna_node_get_full_location (node);
        if (err)
            g_propagate_prefixed_error (error, err,
                    "TreeView '%s': Cannot set property '%s' on node '%s': ",
                    priv->name, prop, fl);
        else
            g_set_error (error, DONNA_TREE_VIEW_ERROR,
                    DONNA_TREE_VIEW_ERROR_OTHER,
                    "TreeView '%s': Failed to create task to set property '%s' on node '%s'",
                    priv->name, prop, fl);
        g_free (fl);
        return FALSE;
    }

    data = g_new0 (struct set_node_prop_data, 1);
    data->tree = tree;
    /* don't need to take a ref on node for timeout or cb, since task has one */
    data->node = node;
    data->prop = g_strdup (prop);

    donna_task_set_timeout (task, 800 /* FIXME an option */,
            (task_timeout_fn) set_node_prop_timeout,
            data,
            NULL);
    donna_task_set_callback (task,
            (task_callback_fn) set_node_prop_callbak,
            data,
            (GDestroyNotify) free_set_node_prop_data);
    donna_app_run_task (priv->app, task);
    return TRUE;
}

static DonnaRow *
get_row_for_iter (DonnaTreeView *tree, GtkTreeIter *iter)
{
    DonnaTreeViewPrivate *priv = tree->priv;
    DonnaRow *row;
    DonnaNode *node;

    gtk_tree_model_get ((GtkTreeModel *) priv->store, iter,
            TREE_VIEW_COL_NODE, &node,
            -1);
    g_object_unref (node);

    if (priv->is_tree)
    {
        GSList *l;

        for (l = g_hash_table_lookup (priv->hashtable, node); l; l = l->next)
            if (itereq (iter, (GtkTreeIter *) l->data))
            {
                iter = l->data;
                break;
            }
        if (G_UNLIKELY (!l))
            g_return_val_if_reached (NULL);
    }
    else
        /* we know there's an iter in the hashtable (since we were given one),
         * we do a lookup to be sure to use the one we own (from hashtable) */
        iter = g_hash_table_lookup (priv->hashtable, node);

    row = g_new (DonnaRow, 1);
    row->node = node;
    row->iter = iter;
    return row;
}

/* mode tree only */
static GtkTreeIter *
get_root_iter (DonnaTreeView *tree, GtkTreeIter *iter)
{
    DonnaTreeViewPrivate *priv = tree->priv;
    GtkTreeModel *model = (GtkTreeModel *) priv->store;
    GtkTreeIter root;
    DonnaNode *node;
    GSList *list;

    if (iter->stamp == 0)
        return NULL;

    if (gtk_tree_store_iter_depth (priv->store, iter) > 0)
    {
        gchar *str;

        str = gtk_tree_model_get_string_from_iter (model, iter);
        /* there is at least one ':' since it's not a root */
        *strchr (str, ':') = '\0';
        gtk_tree_model_get_iter_from_string (model, &root, str);
        g_free (str);
    }
    else
        /* current location is a root */
        root = *iter;

    /* get the iter from the hashtable */
    gtk_tree_model_get (model, &root, TREE_COL_NODE, &node, -1);
    list = g_hash_table_lookup (priv->hashtable, node);
    g_object_unref (node);
    for ( ; list; list = list->next)
        if (itereq (&root, (GtkTreeIter *) list->data))
            return list->data;
    return NULL;
}

/* mode tree only */
static inline GtkTreeIter *
get_current_root_iter (DonnaTreeView *tree)
{
    return get_root_iter (tree, &tree->priv->location_iter);
}

/* mode tree only */
static gboolean
is_row_accessible (DonnaTreeView *tree, GtkTreeIter *iter)
{
    DonnaTreeViewPrivate *priv = tree->priv;
    GtkTreeModel *model = GTK_TREE_MODEL (priv->store);
    GtkTreeView *treev = GTK_TREE_VIEW (tree);
    GtkTreeIter parent;
    GtkTreeIter child;
    GtkTreePath *path;

    child = *iter;
    while (gtk_tree_model_iter_parent (model, &parent, &child))
    {
        gboolean is_expanded;

        path = gtk_tree_model_get_path (model, &parent);
        is_expanded = gtk_tree_view_row_expanded (treev, path);
        gtk_tree_path_free (path);
        if (!is_expanded)
            return FALSE;
        /* go up */
        child = parent;
    }
    return TRUE;
}

/* mode tree only */
/* return the best iter for the given node. Iter must exists on tree, and must
 * be expanded unless even_collapsed is TRUE.
 * This is how we get the new current location in TREE_SYNC_NODES and
 * TREE_SYNC_NODES_KNOWN_CHILDREN */
static GtkTreeIter *
get_best_existing_iter_for_node (DonnaTreeView  *tree,
                                 DonnaNode      *node,
                                 gboolean        even_collapsed)
{
    DonnaTreeViewPrivate *priv = tree->priv;
    GtkTreeView *treev;
    GSList *list;
    GtkTreeModel *model;
    GtkTreeIter *iter_cur_root;
    GtkTreeIter *iter_vis = NULL;
    GtkTreeIter *iter_non_vis = NULL;
    GdkRectangle rect_visible;

    /* we only want iters on tree */
    list = g_hash_table_lookup (priv->hashtable, node);
    if (!list)
        return NULL;

    treev = (GtkTreeView *) tree;
    model = (GtkTreeModel *) priv->store;

    /* just the one? */
    if (!list->next)
    {
        if (even_collapsed || is_row_accessible (tree, list->data))
            return list->data;
        return NULL;
    }

    iter_cur_root = get_current_root_iter (tree);
    if (!iter_cur_root)
    {
        GtkTreePath *path;
        GtkTreeIter iter;

        /* no current root, let's consider the root of the focused row to be the
         * current one, as far as precedence goes */
        gtk_tree_view_get_cursor (treev, &path, NULL);
        if (path)
        {
            if (gtk_tree_model_get_iter (model, &iter, path))
                iter_cur_root = get_root_iter (tree, &iter);
            gtk_tree_path_free (path);
        }
    }

    /* get visible area, so we can determine which iters are visible */
    gtk_tree_view_get_visible_rect (treev, &rect_visible);
    gtk_tree_view_convert_tree_to_bin_window_coords (treev, 0, rect_visible.y,
            &rect_visible.x, &rect_visible.y);

    for ( ; list; list = list->next)
    {
        GtkTreeIter *iter = list->data;
        GdkRectangle rect;

        /* not "accessible" w/out expanding, we skip */
        if (!even_collapsed && !is_row_accessible (tree, iter))
            continue;

        /* if in the current location's root branch, it's the one */
        if (iter_cur_root && (itereq (iter_cur_root, iter)
                    || gtk_tree_store_is_ancestor (priv->store,
                        iter_cur_root, iter)))
            return iter;

        /* if we haven't found a visible match yet... */
        if (!iter_vis)
        {
            GtkTreePath *path;

            /* determine if it is visible or not */
            path = gtk_tree_model_get_path (model, iter);
            gtk_tree_view_get_background_area (treev, path, NULL, &rect);
            gtk_tree_path_free (path);
            if (rect.y >= rect_visible.y
                    && rect.y + rect.height <= rect_visible.y + rect_visible.height)
                iter_vis = iter;
            else if (!iter_non_vis)
                iter_non_vis = iter;
        }
    }

    return (iter_vis) ? iter_vis : iter_non_vis;
}

/* mode tree only -- node must be in a non-flat domain */
static gboolean
is_node_ancestor (DonnaNode         *node,
                  DonnaNode         *descendant,
                  DonnaProvider     *descendant_provider,
                  const gchar       *descendant_location)
{
    gchar *location;
    size_t len;
    gboolean ret;

    if (descendant_provider != donna_node_peek_provider (node))
        return FALSE;

    /* descandant is in the same domain as node, and we know node's domain isn't
     * flat, so we can assume that if descendant is a child, its location starts
     * with its parent's location and a slash */
    location = donna_node_get_location (node);
    len = strlen (location);
    ret = streq (location, "/") /* node is the root */
        || (streqn (location, descendant_location, len)
                && descendant_location[len] == '/');
    g_free (location);
    return ret;
}

/* mode tree only -- node must have its iter ending up under iter_root, and must
 * be in a non-flat domain */
/* get an iter (under iter_root) for the node. If only_accessible we don't want
 * any collapsed row, but the first accessible one. We can then provider the
 * address of a gboolean that will indicate of the iter is for the node asked,
 * or just the closest accessible ancestor. */
static GtkTreeIter *
get_iter_expanding_if_needed (DonnaTreeView *tree,
                              GtkTreeIter   *iter_root,
                              DonnaNode     *node,
                              gboolean       only_accessible,
                              gboolean       ignore_show_hidden,
                              gboolean      *was_match)
{
    GtkTreeView *treev = (GtkTreeView *) tree;
    DonnaTreeViewPrivate *priv = tree->priv;
    GtkTreeModel *model;
    GtkTreeIter *last_iter = NULL;
    GtkTreeIter *iter;
    DonnaProvider *provider;
    DonnaNode *n;
    gchar *location;
    size_t len;
    gchar *s;
    gchar *ss;

    model = (GtkTreeModel *) priv->store;
    iter = iter_root;
    provider = donna_node_peek_provider (node);
    location = donna_node_get_location (node);
    if (was_match)
        *was_match = FALSE;

    /* get the node for the given iter_root, our starting point */
    gtk_tree_model_get (model, iter,
            TREE_COL_NODE,  &n,
            -1);
    for (;;)
    {
        GtkTreeIter *prev_iter;
        GtkTreePath *path;
        enum tree_expand es;

        if (n == node)
        {
            /* this _is_ the iter we're looking for */
            g_free (location);
            g_object_unref (n);
            if (was_match)
                *was_match = TRUE;
            return iter;
        }

        /* get the node's location, and obtain the location of the next child */
        ss = donna_node_get_location (n);
        len = strlen (ss);
        g_free (ss);
        g_object_unref (n);
        s = strchr (location + len + 1, '/');
        if (s)
            s = g_strndup (location, (gsize) (s - location));
        else
            s = (gchar *) location;

        /* get the corresponding node */
        n = donna_provider_get_node (provider, (const gchar *) s, NULL);
        if (s != location)
            g_free (s);

        if (only_accessible)
        {
            /* we only remember this iter as last possible match if expanded */
            if (is_row_accessible (tree, iter))
                last_iter = iter;
        }
        else
            last_iter = iter;

        /* now get the child iter for that node */
        prev_iter = iter;
        iter = get_child_iter_for_node (tree, prev_iter, n);
        if (!iter)
        {
            if (!only_accessible)
            {
                GtkTreeIter it;
                GSList *list;

                /* we need to add a new row */
                if (ignore_show_hidden)
                {
                    if (!add_node_to_tree (tree, prev_iter, n, &it))
                    {
                        g_object_unref (n);
                        g_free (location);
                        return NULL;
                    }
                }
                else if (!add_node_to_tree_filtered (tree, prev_iter, n, &it))
                {
                    g_object_unref (n);
                    g_free (location);
                    return NULL;
                }

                /* get the iter from the hashtable for the row we added (we
                 * cannot end up return the pointer to a local iter) */
                list = g_hash_table_lookup (priv->hashtable, n);
                for ( ; list; list = list->next)
                    if (itereq (&it, (GtkTreeIter *) list->data))
                    {
                        iter = list->data;
                        break;
                    }
            }
            else
            {
                g_object_unref (n);
                g_free (location);
                return last_iter;
            }
        }
        else if (only_accessible && !is_row_accessible (tree, iter))
        {
            g_object_unref (n);
            g_free (location);
            return last_iter;
        }

        /* check if the parent (prev_iter) is expanded */
        path = gtk_tree_model_get_path (model, prev_iter);
        if (!gtk_tree_view_row_expanded (treev, path))
        {
            gtk_tree_model_get (model, prev_iter,
                    TREE_COL_EXPAND_STATE,  &es,
                    -1);
            if (es == TREE_EXPAND_MAXI
                    || es == TREE_EXPAND_PARTIAL)
                gtk_tree_view_expand_row (treev, path, FALSE);
            else
            {
                es = (priv->is_minitree)
                    ? TREE_EXPAND_PARTIAL : TREE_EXPAND_UNKNOWN;
                set_es (priv, prev_iter, es);

                if (priv->is_minitree)
                    gtk_tree_view_expand_row (treev, path, FALSE);
                else
                {
                    /* this will take care of the import/get-children, we'll
                     * scroll (if sync_scroll) to make sure to scroll to current
                     * once children are added */
                    expand_row (tree, prev_iter, /* expand */ TRUE,
                            priv->sync_scroll, NULL);
                    /* now that the thread is started, we need to trigger it
                     * again, so the row actually gets expanded this time, which
                     * we require to be able to continue adding children &
                     * expanding them */
                    gtk_tree_view_expand_row (treev, path, FALSE);
                }
            }
        }
        gtk_tree_path_free (path);
    }
}

static gint
_get_level (GtkTreeModel *model, GtkTreeIter *iter, DonnaNode *node)
{
    DonnaNode *n;
    gint level = 0;
    gchar *s;

    if (iter)
        gtk_tree_model_get (model, iter,
                TREE_COL_NODE,  &n,
                -1);
    else
        n = node;

    s = donna_node_get_location (n);

    if (iter)
        g_object_unref (n);

    if (!streq (s, "/"))
    {
        gchar *ss;

        for (ss = s; *ss != '\0'; ++ss)
            if (*ss == '/')
                ++level;
    }
    g_free (s);

    return level;
}

static GtkTreeIter *
get_closest_iter_for_node (DonnaTreeView *tree,
                           DonnaNode     *node,
                           DonnaProvider *provider,
                           const gchar   *location,
                           gboolean       skip_current_root,
                           gboolean      *is_match)
{
    DonnaTreeViewPrivate *priv = tree->priv;
    GtkTreeView *treev = (GtkTreeView *) tree;
    GtkTreeModel *model;
    GtkTreeIter iter;
    DonnaNode *n;
    GdkRectangle rect_visible;
    GdkRectangle rect;
    GtkTreeIter *cur_root;
    GtkTreeIter *last_iter = NULL;
    guint last_match = 0;
#define LM_MATCH    (1 << 0)
#define LM_VISIBLE  (1 << 1)
    gboolean last_is_in_cur_root = FALSE;
    gint last_level = -1;

    model  = (GtkTreeModel *) priv->store;

    cur_root = get_current_root_iter (tree);
    if (!cur_root)
    {
        GtkTreePath *path;

        /* no current root, nothing to skip */
        skip_current_root = FALSE;

        /* however, we'll consider the root of the focused row to be the current
         * one, as far as precedence goes for results below */
        gtk_tree_view_get_cursor (treev, &path, NULL);
        if (path)
        {
            if (gtk_tree_model_get_iter (model, &iter, path))
                cur_root = get_root_iter (tree, &iter);
            gtk_tree_path_free (path);
        }
    }

    /* get visible area, so we can determine which iters are visible */
    gtk_tree_view_get_visible_rect (treev, &rect_visible);
    gtk_tree_view_convert_tree_to_bin_window_coords (treev,
            0, rect_visible.y, &rect_visible.x, &rect_visible.y);

    /* try all existing tree roots (if any) */
    if (gtk_tree_model_iter_children (model, &iter, NULL))
        do
        {
            /* we might have to skip current root (probably already processed
             * before calling this */
            if (skip_current_root && itereq (&iter, cur_root))
                continue;

            gtk_tree_model_get (model, &iter, TREE_COL_NODE, &n, -1);
            if (n == node || is_node_ancestor (n, node, provider, location))
            {
                GSList *list;
                GtkTreeIter *i = NULL;
                gboolean match;

                /* get the iter from the hashtable (we cannot end up return the
                 * pointer to a local iter) */
                if (priv->is_tree)
                {
                    list = g_hash_table_lookup (priv->hashtable, n);
                    for ( ; list; list = list->next)
                        if (itereq (&iter, (GtkTreeIter *) list->data))
                        {
                            i = list->data;
                            break;
                        }
                }
                else
                    i = g_hash_table_lookup (priv->hashtable, n);

                /* find the closest "accessible" iter for node under i */
                i = get_iter_expanding_if_needed (tree, i, node, TRUE, FALSE, &match);
                if (i)
                {
                    GtkTreePath *path;

                    /* determine if it is visible or not */
                    path = gtk_tree_model_get_path (model, i);
                    gtk_tree_view_get_background_area (treev, path, NULL, &rect);
                    gtk_tree_path_free (path);
                    if (rect.y >= rect_visible.y
                            && rect.y + rect.height <= rect_visible.y +
                            rect_visible.height)
                    {
                        if (match)
                        {
                            /* visible match, this is it */
                            if (is_match)
                                *is_match = match;
                            return get_iter_expanding_if_needed (tree, i, node,
                                    FALSE, FALSE, NULL);
                        }
                        else if (last_match == LM_VISIBLE)
                        {
                            /* we already have a visible non-match... */

                            if (cur_root && itereq (&iter, cur_root))
                            {
                                /* ...but this one is in the current root, so
                                 * takes precedence */
                                last_level = -1;
                                last_match = LM_VISIBLE;
                                last_iter = i;
                                last_is_in_cur_root = TRUE;
                            }
                            else if (!last_is_in_cur_root)
                            {
                                gint level;

                                /* ...neither are in current root, check the
                                 * "level" to use the closest one */

                                if (last_level < 0)
                                    last_level = _get_level (model, last_iter, NULL);
                                level = _get_level (NULL, NULL, n);

                                if (level > last_level)
                                {
                                    last_level = level;
                                    last_match = LM_VISIBLE;
                                    last_iter = i;
                                    last_is_in_cur_root = FALSE;
                                }
                            }
                        }
                        else if (last_match == 0)
                        {
                            /* first result, or we alreayd had a non-match, but
                             * it was not visible */
                            last_level = -1;
                            last_match = LM_VISIBLE;
                            last_iter = i;
                            last_is_in_cur_root = cur_root && itereq (&iter, cur_root);
                        }
                    }
                    else
                    {
                        if (match)
                        {
                            if (last_match != LM_MATCH)
                            {
                                /* we didn't have a match (i.e. we had nothing,
                                 * or a visible non-match) */
                                last_level = -1;
                                last_match = LM_MATCH;
                                last_iter = i;
                                last_is_in_cur_root = itereq (&iter, cur_root);
                            }
                            else if (cur_root && itereq (&iter, cur_root))
                            {
                                /* we already have a non-visible match, but this
                                 * one is in the current root */
                                last_level = -1;
                                last_match = LM_MATCH;
                                last_iter = i;
                                last_is_in_cur_root = TRUE;
                            }
                        }
                        else if (!last_iter)
                        {
                            /* first result */
                            last_level = -1;
                            last_match = 0;
                            last_iter = i;
                            last_is_in_cur_root = cur_root && itereq (&iter, cur_root);
                        }
                        else if (last_match == 0)
                        {
                            /* we already had a non-visible non-match... */

                            if (cur_root && itereq (&iter, cur_root))
                            {
                                /* ...but this one is in the current root */
                                last_level = -1;
                                last_match = 0;
                                last_iter = i;
                                last_is_in_cur_root = TRUE;
                            }
                            else if (!last_is_in_cur_root)
                            {
                                gint level;

                                /* ...neither are in current root, check the
                                 * "level" to use the closest one */

                                if (last_level < 0)
                                    last_level = _get_level (model, last_iter, NULL);
                                level = _get_level (NULL, NULL, n);

                                if (level > last_level)
                                {
                                    last_level = level;
                                    last_match = LM_VISIBLE;
                                    last_iter = i;
                                    last_is_in_cur_root = FALSE;
                                }
                            }
                        }
                    }
                }
            }
        }
        while (gtk_tree_model_iter_next (model, &iter));

    if (is_match)
        *is_match = (last_match & LM_MATCH) ? TRUE : FALSE;
    return last_iter;
}

/* mode tree only */
/* this will get the best iter for new location in TREE_SYNC_NODES_CHILDREN, as
 * well as TREE_SYNC_FULL with add_root_if_needed set to TRUE */
static GtkTreeIter *
get_best_iter_for_node (DonnaTreeView   *tree,
                        DonnaNode       *node,
                        gboolean         add_root_if_needed,
                        gboolean         ignore_show_hidden,
                        GError         **error)
{
    DonnaTreeViewPrivate *priv = tree->priv;
    GtkTreeModel *model;
    DonnaProvider *provider;
    DonnaProviderFlags flags;
    gchar *location;
    GtkTreeIter *iter_cur_root;
    DonnaNode *n;
    GtkTreeIter *last_iter = NULL;
    gboolean match;

    provider = donna_node_peek_provider (node);
    flags = donna_provider_get_flags (provider);
    if (G_UNLIKELY (flags & DONNA_PROVIDER_FLAG_INVALID))
    {
        g_set_error (error, DONNA_TREE_VIEW_ERROR, DONNA_TREE_VIEW_ERROR_OTHER,
                "TreeView '%s': Unable to get flags for provider '%s'",
                priv->name,
                donna_provider_get_domain (provider));
        return NULL;
    }
    /* w/ flat provider we can't do anything else but rely on existing rows */
    else if (flags & DONNA_PROVIDER_FLAG_FLAT)
        /* TRUE not to ignore non-"accesible" (collapsed) ones */
        return get_best_existing_iter_for_node (tree, node, TRUE);

    model  = GTK_TREE_MODEL (priv->store);
    location = donna_node_get_location (node);

    /* try inside the current branch first */
    iter_cur_root = get_current_root_iter (tree);
    if (iter_cur_root)
    {
        gtk_tree_model_get (model, iter_cur_root, TREE_COL_NODE, &n, -1);
        if (n == node || is_node_ancestor (n, node, provider, location))
        {
            g_free (location);
            g_object_unref (n);
            return get_iter_expanding_if_needed (tree, iter_cur_root, node,
                    FALSE, ignore_show_hidden, NULL);
        }
    }

    last_iter = get_closest_iter_for_node (tree, node,
            provider, location, TRUE, &match);
    if (last_iter)
    {
        g_free (location);
        if (match)
            return last_iter;
        else
            return get_iter_expanding_if_needed (tree, last_iter, node,
                    FALSE, ignore_show_hidden, NULL);
    }
    else if (add_root_if_needed)
    {
        gchar *s;
        GSList *list;
        GtkTreeIter it;
        GtkTreeIter *i = NULL;

        /* the tree is empty, we need to add the first root */
        s = strchr (location, '/');
        if (s)
            *++s = '\0';

        n = donna_provider_get_node (provider, location, NULL);
        g_free (location);

        /* since it's a root, we always add (regardless of show_hidden) */
        add_node_to_tree (tree, NULL, n, &it);
        /* first root added, so we might need to load an arrangement */
        if (!priv->arrangement)
            donna_tree_view_build_arrangement (tree, FALSE);
        /* get the iter from the hashtable for the row we added (we
         * cannot end up return the pointer to a local iter) */
        list = g_hash_table_lookup (priv->hashtable, n);
        for ( ; list; list = list->next)
            if (itereq (&it, (GtkTreeIter *) list->data))
            {
                i = list->data;
                break;
            }

        g_object_unref (n);
        return get_iter_expanding_if_needed (tree, i, node,
                FALSE, ignore_show_hidden, NULL);
    }
    else
    {
        g_free (location);
        return NULL;
    }
}

static inline void
scroll_to_iter (DonnaTreeView *tree, GtkTreeIter *iter)
{
    GtkTreeView *treev = (GtkTreeView *) tree;
    GdkRectangle rect_visible, rect;
    GtkTreePath *path;

    /* get visible area, so we can determine if it is already visible */
    gtk_tree_view_get_visible_rect (treev, &rect_visible);

    path = gtk_tree_model_get_path ((GtkTreeModel *) tree->priv->store, iter);
    gtk_tree_view_get_background_area (treev, path, NULL, &rect);
    if (rect.y < 0 || rect.y > rect_visible.height - rect.height)
        /* only scroll if not visible */
        gtk_tree_view_scroll_to_cell (treev, path, NULL, TRUE, 0.5, 0.0);

    gtk_tree_path_free (path);
}

/* mode tree only */
static gboolean
scroll_to_current (DonnaTreeView *tree)
{
    GtkTreeIter iter;

    if (!gtk_tree_selection_get_selected (
                gtk_tree_view_get_selection ((GtkTreeView *) tree),
                NULL,
                &iter))
        return FALSE;

    scroll_to_iter (tree, &iter);
    return FALSE;
}

typedef void (*change_location_callback_fn) (DonnaTreeView *tree, gpointer data);
struct node_get_children_list_data
{
    DonnaTreeView *tree;
    DonnaNode     *node;
    DonnaNode     *child; /* item to goto_item_set */
    change_location_callback_fn callback;
    gpointer cb_data;
    GDestroyNotify cb_destroy;
};

static inline void
free_node_get_children_list_data (struct node_get_children_list_data *data)
{
    g_object_unref (data->node);
    if (data->child)
        g_object_unref (data->child);
    if (data->cb_destroy && data->cb_data)
        data->cb_destroy (data->cb_data);
    g_slice_free (struct node_get_children_list_data, data);
}

/* mode list only */
static void
node_get_children_list_timeout (DonnaTask                           *task,
                                struct node_get_children_list_data  *data)
{
    if (data->tree->priv->get_children_task == task)
        change_location (data->tree, CHANGING_LOCATION_SLOW, NULL, data, NULL);
}

static void switch_provider (DonnaTreeView *tree,
                             DonnaProvider *provider_current,
                             DonnaProvider *provider_future);

/* mode list only */
static void
node_get_children_list_cb (DonnaTask                            *task,
                           gboolean                              timeout_called,
                           struct node_get_children_list_data   *data)
{
    DonnaTreeViewPrivate *priv = data->tree->priv;
    gboolean changed_location;
    GtkTreeIter iter, *it = &iter;
    const GValue *value;
    GPtrArray *arr;
    gboolean check_dupes;
    guint i;

    if (priv->get_children_task != task)
        goto free;

    g_object_unref (priv->get_children_task);
    priv->get_children_task = NULL;

    if (priv->nodes_to_add)
    {
        g_ptr_array_unref (priv->nodes_to_add);
        priv->nodes_to_add = NULL;
    }

    if (donna_task_get_state (task) != DONNA_TASK_DONE)
    {
        if (donna_task_get_state (task) == DONNA_TASK_FAILED)
        {
            gchar *fl = donna_node_get_full_location (data->node);

            donna_app_show_error (priv->app, donna_task_get_error (task),
                    "TreeView '%s': Failed to get children for node '%s'",
                    priv->name, fl);
            g_free (fl);
        }

        if (priv->cl == CHANGING_LOCATION_GOT_CHILD)
        {
            /* GOT_CHILD means that we've already switch our current location,
             * and don't remember what the old one was. It also means we got
             * some children listed, so we should stay there (e.g. search
             * results but the search failed/got cancelled halfway through).
             * We keep priv->cl there, so donna_tree_view_get_children() will
             * still not send anything (since we only have an incomplete list),
             * but we reset priv->future_location */
            priv->future_location = NULL;

            /* Also update the location_task */
            if (priv->location_task)
                g_object_unref (priv->location_task);
            priv->location_task = (donna_task_can_be_duplicated (task))
                ? g_object_ref (task) : NULL;
        }
        else
        {
            GError *err = NULL;
            gchar *fl;

            /* go back -- this is needed to maybe switch back providers,
             * also we might have gone SLOW/DRAW_WAIT and need to
             * re-fill/ask for children again */

            /* first let's make sure any tree sync-ed with us knows where we
             * really are (else they could try to get us to change location
             * back to where we tried & failed) */
            g_object_notify_by_pspec ((GObject *) data->tree,
                    donna_tree_view_props[PROP_LOCATION]);

            /* we hadn't done anything else yet, so all we need is switched
             * back to listen to the right provider */
            if (priv->cl == CHANGING_LOCATION_ASKED)
            {
                switch_provider (data->tree,
                        donna_node_peek_provider (priv->future_location),
                        donna_node_peek_provider (priv->location));
                priv->cl = CHANGING_LOCATION_NOT;
                priv->future_location = NULL;
                priv->future_history_direction = 0;
                priv->future_history_nb = 0;
                goto free;
            }

            /* we actually need to get_children again */
            if (priv->location_task)
            {
                struct node_get_children_list_data *d;
                DonnaTask *t;

                t = donna_task_get_duplicate (priv->location_task, &err);
                if (!t)
                    goto no_task;
                set_get_children_task (data->tree, t);

                d = g_slice_new0 (struct node_get_children_list_data);
                d->tree = data->tree;
                d->node = g_object_ref (priv->location);

                donna_task_set_callback (t,
                        (task_callback_fn) node_get_children_list_cb,
                        d,
                        (GDestroyNotify) free_node_get_children_list_data);
                donna_app_run_task (priv->app, t);
            }
            else if (!change_location (data->tree, CHANGING_LOCATION_ASKED,
                        priv->location, NULL, &err))
                goto no_task;

            check_statuses (data->tree, STATUS_CHANGED_ON_CONTENT);
            goto free;
no_task:
            fl = donna_node_get_full_location (priv->location);
            donna_app_show_error (priv->app, err,
                    "TreeView '%s': Failed to go back to '%s'",
                    priv->name, fl);
            g_free (fl);
            g_clear_error (&err);

            check_statuses (data->tree, STATUS_CHANGED_ON_CONTENT);
        }

        goto free;
    }

    changed_location = priv->location && priv->location != data->node;
    check_dupes = priv->cl == CHANGING_LOCATION_GOT_CHILD;

    if (!change_location (data->tree, CHANGING_LOCATION_NOT, data->node, NULL, NULL))
        goto free;

    value = donna_task_get_return_value (task);
    arr = g_value_get_boxed (value);
    if (arr->len > 0)
    {
        GtkTreeSortable *sortable = (GtkTreeSortable *) data->tree->priv->store;
        gint sort_col_id;
        GtkSortType order;

        /* adding items to a sorted store is quite slow; we get much better
         * performance by adding all items to an unsorted store, and then
         * sorting it */
        gtk_tree_sortable_get_sort_column_id (sortable, &sort_col_id, &order);
        gtk_tree_sortable_set_sort_column_id (sortable,
                GTK_TREE_SORTABLE_UNSORTED_SORT_COLUMN_ID, order);

        priv->filling_list = TRUE;
        for (i = 0; i < arr->len; ++i)
            add_node_to_list (data->tree, arr->pdata[i], !check_dupes);
        priv->filling_list = FALSE;

        gtk_tree_sortable_set_sort_column_id (sortable, sort_col_id, order);

        /* do it now (before processing event) so the request happens now and
         * the size is correct for the first drawing */
        check_statuses (data->tree, STATUS_CHANGED_ON_CONTENT);

        /* in order to scroll properly, we need to have the tree sorted &
         * everything done; i.e. we need to have all pending events processed.
         * Note: here this should be fine, as there shouldn't be any pending
         * events updating the list. See sync_with_location_changed_cb for more
         * about this. */
        priv->nodes_to_add_level = -1; /* see real_new_child_cb() */
        while (gtk_events_pending ())
            gtk_main_iteration ();
        priv->nodes_to_add_level = 0;

        /* do we have a child to focus/scroll to? */
        if (data->child)
            it = g_hash_table_lookup (priv->hashtable, data->child);
        else
            it = NULL;
        if (it)
        {
            GtkTreePath *path;

            path = gtk_tree_model_get_path ((GtkTreeModel *) priv->store, it);

            if (priv->goto_item_set & DONNA_TREE_VIEW_SET_SCROLL)
            {
                if (changed_location)
                    scroll_to_iter (data->tree, it);
                else
                    gtk_tree_view_scroll_to_cell ((GtkTreeView *) data->tree,
                            path, NULL, FALSE, 0.0, 0.0);
            }

            if (priv->goto_item_set & DONNA_TREE_VIEW_SET_FOCUS)
                gtk_tree_view_set_focused_row ((GtkTreeView *) data->tree, path);

            if (priv->goto_item_set & DONNA_TREE_VIEW_SET_CURSOR)
            {
                if (!(priv->goto_item_set & DONNA_TREE_VIEW_SET_FOCUS))
                    gtk_tree_view_set_focused_row ((GtkTreeView *) data->tree, path);
                gtk_tree_selection_select_path (
                        gtk_tree_view_get_selection ((GtkTreeView *) data->tree),
                        path);
            }

            gtk_tree_path_free (path);
        }
        if (!(priv->goto_item_set & DONNA_TREE_VIEW_SET_SCROLL) || !it)
            /* scroll to top-left */
            gtk_tree_view_scroll_to_point ((GtkTreeView *) data->tree, 0, 0);

        /* we need to ensure the tree gets focused so the class is applied and
         * the cursor set. This is done in set_draw_state() when switching to
         * NOTHING, so we need to ensure it is a swicth */
        priv->draw_state = DRAW_WAIT;
        /* because when relative number are used and the tree was cleared, there
         * was no cursor, and so relative number couldn't be calculated (so it
         * fell back to "regular" line number), we need to queue a redraw if
         * ln_relative is used, but this will (always) queue one */
        set_draw_state (data->tree,
                /* could be that everything got filtered out */
                (has_model_at_least_n_rows ((GtkTreeModel *) priv->store, 1))
                ? DRAW_NOTHING : DRAW_NO_VISIBLE);
        /* we need to trigger it again, because the focused item might have
         * changed/been set */
        check_statuses (data->tree, STATUS_CHANGED_ON_CONTENT);
        preload_props_columns (data->tree);
    }
    else
    {
        check_statuses (data->tree, STATUS_CHANGED_ON_CONTENT);
        /* show the "location empty" message */
        set_draw_state (data->tree, DRAW_EMPTY);
    }

    if (priv->location_task)
        g_object_unref (priv->location_task);
    priv->location_task = (donna_task_can_be_duplicated (task))
        ? g_object_ref (task) : NULL;

    /* if there's a post-CL callback, trigger it */
    if (data->callback)
    {
        data->callback (data->tree, data->cb_data);
        /* no need to free cb_data anymore (callback had to do it) */
        data->cb_destroy = NULL;
    }

    /* emit signal */
    g_object_notify_by_pspec ((GObject *) data->tree,
            donna_tree_view_props[PROP_LOCATION]);

free:
    free_node_get_children_list_data (data);
}

/* mode list only */
static void
switch_provider (DonnaTreeView *tree,
                 DonnaProvider *provider_current,
                 DonnaProvider *provider_future)
{
    DonnaTreeViewPrivate *priv = tree->priv;

    if (provider_current != provider_future)
    {
        struct provider_signals *ps;
        guint i;
        gint done = (provider_current) ? 0 : 1;
        gint found = -1;

        for (i = 0; i < priv->providers->len; ++i)
        {
            ps = priv->providers->pdata[i];

            if (ps->provider == provider_future)
            {
                found = (gint) i;
                ps->nb_nodes++;
                ++done;
            }
            else if (ps->provider == provider_current)
            {
                if (--ps->nb_nodes == 0)
                    /* this will disconnect handlers from provider & free memory */
                    g_ptr_array_remove_index_fast (priv->providers, i--);
                else
                {
                    /* still connected for children listed on list, but not the
                     * current location. So, we can disconnect from new_child */
                    g_signal_handler_disconnect (ps->provider,
                            ps->sid_node_new_child);
                    ps->sid_node_new_child = 0;
                }
                ++done;
            }

            if (done == 2)
                break;
        }
        if (found < 0)
        {
            ps = g_new0 (struct provider_signals, 1);
            ps->provider = g_object_ref (provider_future);
            ps->nb_nodes = 1;
            ps->sid_node_updated = g_signal_connect (provider_future,
                    "node-updated", G_CALLBACK (node_updated_cb), tree);
            ps->sid_node_deleted = g_signal_connect (provider_future,
                    "node-deleted", G_CALLBACK (node_deleted_cb), tree);
            ps->sid_node_removed_from = g_signal_connect (provider_future,
                    "node-removed-from", G_CALLBACK (node_removed_from_cb), tree);

            g_ptr_array_add (priv->providers, ps);
        }
        else
            ps = priv->providers->pdata[found];
        /* whether or not we created ps, we need to connect to new_child, since
         * it's only useful for current location */
        ps->sid_node_new_child = g_signal_connect (provider_future,
                "node-new-child", G_CALLBACK (node_new_child_cb), tree);
    }
}

enum cl_extra
{
    CL_EXTRA_NONE = 0,
    CL_EXTRA_HISTORY_MOVE,
    CL_EXTRA_CALLBACK
};

struct change_location_extra
{
    enum cl_extra type;
};

struct history_move
{
    enum cl_extra type;
    DonnaHistoryDirection direction;
    guint nb;
};

struct cl_cb
{
    enum cl_extra type;
    change_location_callback_fn callback;
    gpointer data;
    GDestroyNotify destroy;
};

struct cl_go_up
{
    enum cl_extra type;
    change_location_callback_fn callback;
    gpointer data;
    GDestroyNotify destroy;

    DonnaNode *node;
    DonnaTreeViewSet set;
};

static inline gboolean
handle_history_move (DonnaTreeView *tree, DonnaNode *node)
{
    GValue v = G_VALUE_INIT;
    DonnaTask *task;
    DonnaNodeHasValue has;

    if (!streq ("internal", donna_node_get_domain (node)))
        return FALSE;

    donna_node_get (node, FALSE, "history-tree", &has, &v, NULL);
    if (has != DONNA_NODE_VALUE_SET)
        return FALSE;

    if (tree != (DonnaTreeView *) g_value_get_object (&v))
    {
        g_value_unset (&v);
        return FALSE;
    }
    g_value_unset (&v);

    task = donna_node_trigger_task (node, NULL);
    if (!task)
        return FALSE;

    donna_app_run_task (tree->priv->app, task);
    return TRUE;
}

/* mode list only */
static gboolean
change_location (DonnaTreeView *tree,
                 enum cl        cl,
                 DonnaNode     *node,
                 gpointer       _data,
                 GError       **error)
{
    DonnaTreeViewPrivate *priv = tree->priv;
    DonnaProvider *provider_current;
    DonnaProvider *provider_future;

    if (cl > CHANGING_LOCATION_ASKED && priv->cl > cl)
        /* this is ignoring e.g. CHANGING_LOCATION_SLOW if we're already at
         * CHANGING_LOCATION_GOT_CHILD */
        return FALSE;

    if (cl == CHANGING_LOCATION_ASKED)
    {
        DonnaTask *task;
        DonnaNode *child = NULL;
        struct node_get_children_list_data *data;

        /* if that's already happening, nothing needs to be done. This can
         * happen sometimes when multiple selection-changed in a tree occur,
         * thus leading to multiple call to set_location() if they happen before
         * list completed the change :
         *
         * - first selection-changed, call to set_location()
         * - another selection-changed, list is still changing location, tree
         *   calls set_location() again. This would cancel the first one, and
         *   by the time the second one would end the first one has set
         *   future_location to NULL thus it gets ignored (this is all because
         *   they both point to the same location).
         *
         * Why would multiple selection-changed occur? Besides the fact that it
         * can happen be design, this would most likely happen because the
         * selection mode wasn't BROWSE, and setting it to BROWSE (in idle)
         * might emit the signal again.
         *
         * A reproducible way to get this is:
         * - go to a flat domain, e.g. register:/ [1]
         * - click on tree, there you go.
         *
         * [1] tree gets out of sync; See selection_changed_cb() for more
         */
        if (priv->future_location == node)
            return TRUE;

        provider_future = donna_node_peek_provider (node);

        if (donna_node_get_node_type (node) == DONNA_NODE_ITEM)
        {
            if (donna_provider_get_flags (provider_future) == DONNA_PROVIDER_FLAG_FLAT)
            {
                gchar *fl;

                /* special case: if this is a node from history_get_node() we
                 * will process it as a move in history. This will allow e.g.
                 * dynamic marks to move backward/forward/etc */
                if (handle_history_move (tree, node))
                    return TRUE;

                fl = donna_node_get_full_location (node);
                g_set_error (error, DONNA_TREE_VIEW_ERROR,
                        DONNA_TREE_VIEW_ERROR_FLAT_PROVIDER,
                        "TreeView '%s': Cannot set node '%s' as current location, "
                        "provider is flat (i.e. no parent to go to)",
                        priv->name, fl);
                g_free (fl);
                return FALSE;
            }

            child = node;
            node = donna_node_get_parent (node, error);
            if (!node)
                return FALSE;
            if (node == priv->future_location)
            {
                g_object_unref (node);
                return TRUE;
            }
        }

        /* abort any preloading of properties */
        task = g_object_get_data ((GObject *) tree, DATA_PRELOAD_TASK);
        if (task)
        {
            donna_task_cancel (task);
            g_object_set_data ((GObject *) tree, DATA_PRELOAD_TASK, NULL);
        }

        task = donna_node_get_children_task (node, priv->node_types, error);
        if (!task)
            return FALSE;
        set_get_children_task (tree, task);

        data = g_slice_new0 (struct node_get_children_list_data);
        data->tree = tree;
        if (child)
        {
            data->node = node;
            data->child = g_object_ref (child);
        }
        else
            data->node = g_object_ref (node);

        donna_task_set_timeout (task, 800, /* FIXME */
                (task_timeout_fn) node_get_children_list_timeout,
                data,
                NULL);
        donna_task_set_callback (task,
                (task_callback_fn) node_get_children_list_cb,
                data,
                (GDestroyNotify) free_node_get_children_list_data);

        /* if we're not or already switched, current location is as expected */
        if (priv->cl == CHANGING_LOCATION_NOT
                || priv->cl == CHANGING_LOCATION_GOT_CHILD)
        {
            if (G_LIKELY (priv->location))
                provider_current = donna_node_peek_provider (priv->location);
            else
                provider_current = NULL;
        }
        else
            /* but for ASKED and SLOW we've already switched to future provider,
             * so we should consider it as our current one */
            provider_current = donna_node_peek_provider (priv->future_location);

        /* we don't ref this node, since we should only have it for a short
         * period of time, and will only use it to compare (the pointer) in the
         * task's timeout/cb, to make sure the new location is still valid */
        priv->future_location = node;
        /* we might have gotten extra info */
        if (_data)
        {
            struct change_location_extra *cle = _data;

            /* is this a move in history? */
            if (cle->type == CL_EXTRA_HISTORY_MOVE)
            {
                struct history_move *hm = _data;
                priv->future_history_direction = hm->direction;
                priv->future_history_nb = hm->nb;
            }
            /* is this a callback? */
            else if (cle->type == CL_EXTRA_CALLBACK)
            {
                struct cl_cb *cb = _data;
                data->callback = cb->callback;
                data->cb_data = cb->data;
                data->cb_destroy = cb->destroy;
            }
        }
        else
        {
            priv->future_history_direction = 0;
            priv->future_history_nb = 0;
        }

        /* connect to provider's signals of future location (if needed) */
        switch_provider (tree, provider_current, provider_future);

        /* update cl now to make sure we don't overwrite the task we're about to
         * run or something. That is, said task could be an INTERNAL_FAST one
         * (e.g. in config) and therefore run right now blockingly. And once
         * done its callback will set priv->cl to NOT, which we would then
         * overwrite with our ASKED, leading to troubles. */
        priv->cl = cl;

        /* now that we're ready, let's get those children */
        donna_app_run_task (priv->app, task);

        /* return now, since we've handled cl already */
        return TRUE;
    }
    else if (cl == CHANGING_LOCATION_SLOW)
    {
        DonnaRowId rid = { DONNA_ARG_TYPE_PATH, (gpointer) ":last" };
        struct node_get_children_list_data *data = _data;
        GHashTableIter ht_it;
        GtkTreeIter *iter;

        /* is this still valid (or did the user click away already) ? */
        if (data->node)
        {
            if (G_UNLIKELY (priv->future_location != data->node))
            {
                gchar *fl_task = donna_node_get_full_location (data->node);
                gchar *fl_list = NULL;

                if (priv->location)
                    fl_list = donna_node_get_full_location (priv->location);

                g_critical ("TreeView '%s': change_location (SLOW) triggered "
                        "yet future location differs.\n"
                        "Task: %s\nList: %s",
                        priv->name, fl_task, fl_list);

                g_free (fl_task);
                g_free (fl_list);
                return FALSE;
            }
        }
        else if (G_UNLIKELY (priv->future_location != data->child))
        {
            gchar *fl_task = donna_node_get_full_location (data->child);
            gchar *fl_list = NULL;

            if (priv->location)
                fl_list = donna_node_get_full_location (priv->location);

            g_critical ("TreeView '%s': change_location (SLOW) triggered "
                    "yet future location differs.\n"
                    "Task: %s\nList: %s",
                    priv->name, fl_task, fl_list);

            g_free (fl_task);
            g_free (fl_list);
            return FALSE;
        }

#ifdef GTK_IS_JJK
        /* make sure we don't try to perform a rubber band on two different
         * content, as that would be very likely to segfault in GTK, in
         * addition to be quite unexpected at best */
        if (gtk_tree_view_is_rubber_banding_pending ((GtkTreeView *) tree, TRUE))
            gtk_tree_view_stop_rubber_banding ((GtkTreeView *) tree, FALSE);
#endif
        /* clear the list (see selection_changed_cb() for why filling_list) */
        priv->filling_list = TRUE;
        /* speed up -- see below for why */
        donna_tree_view_set_focus (tree, &rid, NULL);
        gtk_tree_store_clear (priv->store);
        priv->filling_list = FALSE;
        /* also the hashtable. First we need to free the iters & unref the
         * nodes, then actually clear it */
        g_hash_table_iter_init (&ht_it, priv->hashtable);
        while (g_hash_table_iter_next (&ht_it, (gpointer) &node, (gpointer) &iter))
        {
            if (iter)
                gtk_tree_iter_free (iter);
            g_object_unref (node);
        }
        g_hash_table_remove_all (priv->hashtable);
        /* and show the "please wait" message */
        set_draw_state (tree, DRAW_WAIT);
        /* no more files on list */
        check_statuses (tree, STATUS_CHANGED_ON_CONTENT);
    }
    else /* CHANGING_LOCATION_GOT_CHILD || CHANGING_LOCATION_NOT */
    {
        if (node != priv->future_location)
            return FALSE;

        if (priv->cl < CHANGING_LOCATION_GOT_CHILD)
        {
            DonnaRowId rid = { DONNA_ARG_TYPE_PATH, (gpointer) ":last" };
            GHashTableIter ht_it;
            GtkTreeIter *iter;
            DonnaNode *n;

#ifdef GTK_IS_JJK
            /* make sure we don't try to perform a rubber band on two different
             * content, as that would be very likely to segfault in GTK, in
             * addition to be quite unexpected at best */
            if (gtk_tree_view_is_rubber_banding_pending ((GtkTreeView *) tree, TRUE))
                gtk_tree_view_stop_rubber_banding ((GtkTreeView *) tree, FALSE);
#endif
            /* clear the list (see selection_changed_cb() for why filling_list) */
            priv->filling_list = TRUE;
            /* set focus to last row, to speed things up. Because when clearing
             * the store the treeview will react to each and every signal
             * row-deleted, and figure out where to and move the focus, which
             * when there's thousands of rows and the focus was on the first,
             * slows thigns down quite a bit */
            donna_tree_view_set_focus (tree, &rid, NULL);
            gtk_tree_store_clear (priv->store);
            priv->filling_list = FALSE;
            /* also the hashtable. First we need to free the iters & unref the
             * nodes, then actually clear it */
            g_hash_table_iter_init (&ht_it, priv->hashtable);
            while (g_hash_table_iter_next (&ht_it, (gpointer) &n, (gpointer) &iter))
            {
                if (iter)
                    gtk_tree_iter_free (iter);
                g_object_unref (n);
            }
            g_hash_table_remove_all (priv->hashtable);
            /* no special drawing */
            set_draw_state (tree, DRAW_NOTHING);
        }

        /* GOT_CHILD, or NOT which means finalizing the switch, in which case we
         * also need to do the switch if it hasn't been done before */
        if (cl == CHANGING_LOCATION_GOT_CHILD
                || priv->cl < CHANGING_LOCATION_GOT_CHILD)
        {
            GtkStyleContext *context;
            const gchar *domain;
            gchar buf[64];

            context = gtk_widget_get_style_context ((GtkWidget *) tree);
            /* update current location (now because we need this done to build
             * arrangement) */
            if (priv->location)
            {
                domain = donna_node_get_domain (priv->location);
                /* 56 == 64 (buf) - 8 (strlen ("domain-") + NUL) */
                if (strlen (domain) <= 56)
                {
                    strcpy (buf, "domain-");
                    strcpy (buf + 7, domain);
                    gtk_style_context_remove_class (context, buf);
                }
                else
                {
                    gchar *b = g_strdup_printf ("domain-%s", domain);
                    gtk_style_context_remove_class (context, buf);
                    g_free (b);
                }
                g_object_unref (priv->location);
            }
            priv->location = g_object_ref (node);
            domain = donna_node_get_domain (priv->location);
            /* 56 == 64 (buf) - 8 (strlen ("domain-") + NUL) */
            if (strlen (domain) <= 56)
            {
                strcpy (buf, "domain-");
                strcpy (buf + 7, domain);
                gtk_style_context_add_class (context, buf);
            }
            else
            {
                gchar *b = g_strdup_printf ("domain-%s", domain);
                gtk_style_context_add_class (context, buf);
                g_free (b);
            }
            /* update arrangement for new location if needed */
            donna_tree_view_build_arrangement (tree, FALSE);

            /* update history */
            if (priv->future_history_direction > 0)
            {
                const gchar *h;

                /* this is a move in history */
                if (G_LIKELY ((h = donna_history_move (priv->history,
                        priv->future_history_direction,
                        priv->future_history_nb,
                        NULL))))
                {
                    gchar *fl = donna_node_get_full_location (priv->location);

                    if (G_LIKELY (streq (fl, h)))
                        g_free (fl);
                    else
                    {
                        /* this means the history changed during the change of
                         * location, and e.g. change of history_max option
                         * (could have resulted in he needed items to be lost) */
                        g_warning ("TreeView '%s': History move couldn't be validated, "
                                "adding current location as new one instead",
                                priv->name);
                        donna_history_take_item (priv->history, fl);
                    }

                    priv->future_history_direction = 0;
                    priv->future_history_nb = 0;
                }
                else
                {
                    /* this means the history changed during the change of
                     * location, and e.g. was cleared. */
                    g_warning ("TreeView '%s': History move couldn't be validated, "
                            "adding current location as new one instead",
                            priv->name);
                    donna_history_take_item (priv->history,
                            donna_node_get_full_location (priv->location));
                }
            }
            else
                /* add new location to history */
                donna_history_take_item (priv->history,
                        donna_node_get_full_location (priv->location));

            /* we don't emit the notify signal from here, because it should be
             * emitted AFTER the list has been updated, in case e.g. another
             * treeview ask us for the children (e.g. a tree to update its
             * chidren) */
        }

        if (cl == CHANGING_LOCATION_NOT)
            priv->future_location = NULL;
    }

    priv->cl = cl;
    return TRUE;
}

gboolean
donna_tree_view_set_location (DonnaTreeView  *tree,
                              DonnaNode      *node,
                              GError        **error)
{
    DonnaTreeViewPrivate *priv;

    g_return_val_if_fail (DONNA_IS_TREE_VIEW (tree), FALSE);
    g_return_val_if_fail (DONNA_IS_NODE (node), FALSE);

    priv = tree->priv;

    if (priv->is_tree)
    {
        if (!(priv->node_types & donna_node_get_node_type (node)))
        {
            gchar *location = donna_node_get_location (node);
            g_set_error (error, DONNA_TREE_VIEW_ERROR, DONNA_TREE_VIEW_ERROR_OTHER,
                    "TreeView '%s': Cannot go to '%s:%s', invalid type",
                    priv->name, donna_node_get_domain (node), location);
            g_free (location);
            return FALSE;
        }

        return perform_sync_location (tree, node, TREE_SYNC_FULL, TRUE);
    }
    else
        return change_location (tree, CHANGING_LOCATION_ASKED, node, NULL, error);
}

/**
 * donna_tree_view_get_location:
 * @tree: A #DonnaTreeView
 *
 * Returns the current location of @tree
 *
 * Returns: (transfer full): The #DonnaNode of the current location, or %NULL
 */
DonnaNode *
donna_tree_view_get_location (DonnaTreeView      *tree)
{
    g_return_val_if_fail (DONNA_IS_TREE_VIEW (tree), NULL);

    if (tree->priv->location)
        return g_object_ref (tree->priv->location);
    else
        return NULL;
}

static inline gboolean
init_getting_nodes (GtkTreeView    *treev,
                    GtkTreeModel   *model,
                    GtkTreeIter    *iter_focus,
                    GtkTreeIter    *iter)
{
    GtkTreePath *path;

    /* we start on the focused row, then loop back from start to it. This allows
     * user to have the ability to set some order/which item is the first, which
     * could be useful when those nodes are then used. */
    gtk_tree_view_get_cursor (treev, &path, NULL);
    if (path && gtk_tree_model_get_iter (model, iter_focus, path))
    {
        gtk_tree_path_free (path);
        *iter = *iter_focus;
    }
    else
    {
        if (path)
            gtk_tree_path_free (path);
        if (!gtk_tree_model_iter_children (model, iter, NULL))
            /* no (first) row, no selection */
            return FALSE;
        iter_focus->stamp = 0;
    }
    return TRUE;
}

/* Note: Returns NULL on error or no selection. Check error to know */
GPtrArray *
donna_tree_view_get_selected_nodes (DonnaTreeView   *tree,
                                    GError         **error)
{
    DonnaTreeViewPrivate *priv;
    GtkTreeModel *model;
    GtkTreeIter iter_focus;
    GtkTreeIter iter;
    GtkTreeSelection *sel;
    GPtrArray *arr = NULL;
    gboolean second_pass = FALSE;

    g_return_val_if_fail (DONNA_IS_TREE_VIEW (tree), NULL);
    priv  = tree->priv;
    model = (GtkTreeModel *) priv->store;

    if (priv->is_tree)
    {
        g_set_error (error, DONNA_TREE_VIEW_ERROR,
                DONNA_TREE_VIEW_ERROR_INVALID_MODE,
                "TreeView '%s': No selection support in mode Tree"
                " (use get_location() to get the current/selected node)",
                priv->name);
        return NULL;
    }

    sel = gtk_tree_view_get_selection ((GtkTreeView *) tree);
    if (!init_getting_nodes ((GtkTreeView *) tree, model, &iter_focus, &iter))
        return NULL;

again:
    do
    {
        if (second_pass && itereq (&iter, &iter_focus))
        {
            iter_focus.stamp = 0;
            break;
        }
        else if (gtk_tree_selection_iter_is_selected (sel, &iter))
        {
            DonnaNode *node;

            gtk_tree_model_get (model, &iter,
                    TREE_VIEW_COL_NODE, &node,
                    -1);
            if (G_UNLIKELY (!arr))
                arr = g_ptr_array_new_with_free_func (g_object_unref);
            g_ptr_array_add (arr, node);
        }
    } while (gtk_tree_model_iter_next (model, &iter));

    /* if we started at focus, let's start back from top to focus */
    if (iter_focus.stamp != 0)
    {
        gtk_tree_model_iter_children (model, &iter, NULL);
        second_pass = TRUE;
        goto again;
    }

    return arr;
}

typedef enum
{
    ROW_ID_INVALID = 0,
    ROW_ID_ROW,
    ROW_ID_SELECTION,
    ROW_ID_ALL,
} row_id_type;

static row_id_type
convert_row_id_to_iter (DonnaTreeView   *tree,
                        DonnaRowId      *rowid,
                        GtkTreeIter     *iter)
{
    DonnaTreeViewPrivate *priv = tree->priv;
    GSList *list;

    /* we can do simple lookups here, because in list a non-visible node is the
     * same as a non-existing one: invalid rowid (since there *is* no row for
     * that node) */

    if (rowid->type == DONNA_ARG_TYPE_ROW)
    {
        DonnaRow *row = rowid->ptr;

        list = g_hash_table_lookup (priv->hashtable, row->node);
        if (!list)
            return ROW_ID_INVALID;
        if (priv->is_tree)
        {
            for ( ; list; list = list->next)
                if ((GtkTreeIter *) list->data == row->iter)
                {
                    if (!is_row_accessible (tree, row->iter))
                        return ROW_ID_INVALID;
                    *iter = *row->iter;
                    return ROW_ID_ROW;
                }
        }
        else
        {
            if ((GtkTreeIter *) list == row->iter)
            {
                *iter = *row->iter;
                return ROW_ID_ROW;
            }
        }

        return ROW_ID_INVALID;
    }
    else if (rowid->type == DONNA_ARG_TYPE_NODE)
    {
        list = g_hash_table_lookup (priv->hashtable, rowid->ptr);
        if (!list)
            return ROW_ID_INVALID;
        if (priv->is_tree)
        {
            for ( ; list; list = list->next)
                if (is_row_accessible (tree, list->data))
                {
                    *iter = * (GtkTreeIter *) list->data;
                    return ROW_ID_ROW;
                }
        }
        else
        {
            *iter = * (GtkTreeIter *) list;
            return ROW_ID_ROW;
        }

        return ROW_ID_INVALID;
    }
    else if (rowid->type == DONNA_ARG_TYPE_PATH)
    {
        GtkTreeView *treev  = (GtkTreeView *) tree;
        GtkTreeModel *model = (GtkTreeModel *) priv->store;
        gchar *s = rowid->ptr;

        if (*s == ':')
        {
            ++s;
            if (streq ("all", s))
                return ROW_ID_ALL;
            else if (streq ("selected", s))
                return ROW_ID_SELECTION;
            else if (streq ("focused", s))
            {
                GtkTreePath *path;

                gtk_tree_view_get_cursor (treev, &path, NULL);
                if (path && gtk_tree_model_get_iter (model, iter, path))
                {
                    gtk_tree_path_free (path);
                    return ROW_ID_ROW;
                }
                if (path)
                    gtk_tree_path_free (path);
                return ROW_ID_INVALID;
            }
            else if (streq ("prev", s))
            {
                GtkTreePath *path;
                GtkTreeIter it;

                gtk_tree_view_get_cursor (treev, &path, NULL);
                if (!path)
                    return ROW_ID_INVALID;

                if (!gtk_tree_model_get_iter (model, iter, path))
                    return ROW_ID_INVALID;
                it = *iter;

                for (;;)
                {
                    if (!_gtk_tree_model_iter_previous (model, iter))
                    {
                        /* no previous row, simply return the current one.
                         * Avoids getting "invalid row-id" error message just
                         * because you press Up while on the first row, or
                         * similarly breaking "v3k" when there's only 2 rows
                         * above, etc */
                        *iter = it;
                        return ROW_ID_ROW;
                    }
                    if (!priv->is_tree || is_row_accessible (tree, iter))
                        break;
                }
                return ROW_ID_ROW;
            }
            else if (streq ("next", s))
            {
                GtkTreePath *path;
                GtkTreeIter it;

                gtk_tree_view_get_cursor (treev, &path, NULL);
                if (!path)
                    return ROW_ID_INVALID;

                if (!gtk_tree_model_get_iter (model, iter, path))
                    return ROW_ID_INVALID;
                it = *iter;

                for (;;)
                {
                    if (!_gtk_tree_model_iter_next (model, iter))
                    {
                        /* same as prev */
                        *iter = it;
                        return ROW_ID_ROW;
                    }
                    if (!priv->is_tree || is_row_accessible (tree, iter))
                        break;
                }
                return ROW_ID_ROW;
            }
            else if (streq ("last", s))
            {
                if (!_gtk_tree_model_iter_last (model, iter))
                    return ROW_ID_INVALID;
                if (priv->is_tree)
                {
                    while (!is_row_accessible (tree, iter))
                        if (!_gtk_tree_model_iter_previous (model, iter))
                            return ROW_ID_INVALID;
                }
                return ROW_ID_ROW;
            }
            else if (streq ("up", s))
            {
                GtkTreePath *path;

                gtk_tree_view_get_cursor (treev, &path, NULL);
                if (!path)
                    return ROW_ID_INVALID;

                if (!gtk_tree_path_up (path))
                    return ROW_ID_INVALID;

                if (gtk_tree_model_get_iter (model, iter, path))
                {
                    gtk_tree_path_free (path);
                    return ROW_ID_ROW;
                }
                gtk_tree_path_free (path);
                return ROW_ID_INVALID;
            }
            else if (streq ("down", s))
            {
                GtkTreePath *path;

                gtk_tree_view_get_cursor (treev, &path, NULL);
                if (!path)
                    return ROW_ID_INVALID;

                gtk_tree_path_down (path);

                if (gtk_tree_model_get_iter (model, iter, path)
                        && is_row_accessible (tree, iter))
                {
                    gtk_tree_path_free (path);
                    return ROW_ID_ROW;
                }
                gtk_tree_path_free (path);
                return ROW_ID_INVALID;
            }
            else if (streq ("top", s))
            {
                GtkTreePath *path;

                if (!gtk_tree_view_get_visible_range (treev, &path, NULL))
                    return ROW_ID_INVALID;

                if (gtk_tree_model_get_iter (model, iter, path))
                {
                    GdkRectangle rect;

                    gtk_tree_view_get_background_area (treev, path, NULL, &rect);
                    if (rect.y < -(rect.height / 3))
                    {
                        for (;;)
                        {
                            if (!_gtk_tree_model_iter_next (model, iter))
                                return ROW_ID_INVALID;
                            if (!priv->is_tree || is_row_accessible (tree, iter))
                                break;
                        }
                    }
                    gtk_tree_path_free (path);
                    return ROW_ID_ROW;
                }
                gtk_tree_path_free (path);
                return ROW_ID_INVALID;
            }
            else if (streq ("bottom", s))
            {
                GtkTreePath *path;

                if (!gtk_tree_view_get_visible_range (treev, NULL, &path))
                    return ROW_ID_INVALID;

                if (gtk_tree_model_get_iter (model, iter, path))
                {
                    GdkRectangle rect_visible;
                    GdkRectangle rect;

                    gtk_tree_view_get_visible_rect (treev, &rect_visible);

                    gtk_tree_view_get_background_area (treev, path, NULL, &rect);
                    if (rect.y + 2 * (rect.height / 3) > rect_visible.height)
                    {
                        for (;;)
                        {
                            if (!_gtk_tree_model_iter_previous (model, iter))
                                return ROW_ID_INVALID;
                            if (!priv->is_tree || is_row_accessible (tree, iter))
                                break;
                        }
                    }
                    gtk_tree_path_free (path);
                    return ROW_ID_ROW;
                }
                gtk_tree_path_free (path);
                return ROW_ID_INVALID;
            }
            else if (streq ("prev-same-depth", s))
            {
                GtkTreePath *path;

                gtk_tree_view_get_cursor (treev, &path, NULL);
                if (!path)
                    return ROW_ID_INVALID;

                if (!gtk_tree_path_prev (path))
                    return ROW_ID_INVALID;

                if (gtk_tree_model_get_iter (model, iter, path))
                {
                    gtk_tree_path_free (path);
                    return ROW_ID_ROW;
                }
                gtk_tree_path_free (path);
                return ROW_ID_INVALID;
            }
            else if (streq ("next-same-depth", s))
            {
                GtkTreePath *path;

                gtk_tree_view_get_cursor (treev, &path, NULL);
                if (!path)
                    return ROW_ID_INVALID;

                gtk_tree_path_next (path);

                if (gtk_tree_model_get_iter (model, iter, path))
                {
                    gtk_tree_path_free (path);
                    return ROW_ID_ROW;
                }
                gtk_tree_path_free (path);
                return ROW_ID_INVALID;
            }
            else if (streq ("other", s) || streq ("item", s) || streq ("container", s))
            {
                GtkTreeIter focused;
                GtkTreeIter it;
                GtkTreePath *path;
                DonnaNodeType nt;

                /* first we need to get the focused row */
                gtk_tree_view_get_cursor (treev, &path, NULL);
                if (!path)
                    return ROW_ID_INVALID;
                else if (!gtk_tree_model_get_iter (model, &focused, path))
                {
                    gtk_tree_path_free (path);
                    return ROW_ID_INVALID;
                }
                gtk_tree_path_free (path);

                /* what are we looking for */
                if (*s == 'i')
                    nt = DONNA_NODE_ITEM;
                else if (*s == 'c')
                    nt = DONNA_NODE_CONTAINER;
                else /* 'o' */
                {
                    DonnaNode *n;
                    gtk_tree_model_get (model, &focused,
                            TREE_VIEW_COL_NODE, &n,
                            -1);
                    if (G_UNLIKELY (!n))
                        return ROW_ID_INVALID;
                    if (donna_node_get_node_type (n) == DONNA_NODE_ITEM)
                        nt = DONNA_NODE_CONTAINER;
                    else
                        nt = DONNA_NODE_ITEM;
                    g_object_unref (n);
                }

                /* now moving next */
                it = focused;
                for (;;)
                {
                    if (!_gtk_tree_model_iter_next (model, &it))
                    {
                        /* reached bottom, go back from the top */
                        if (!gtk_tree_model_iter_children (model, &it, NULL))
                            return ROW_ID_INVALID;
                    }

                    if (itereq (&it, &focused))
                        /* we looped back to the focus, i.e. no match */
                        return ROW_ID_INVALID;
                    else if (!priv->is_tree || is_row_accessible (tree, &it))
                    {
                        DonnaNode *n;

                        gtk_tree_model_get (model, &it,
                                TREE_VIEW_COL_NODE, &n,
                                -1);
                        if (n)
                        {
                            /* it's okay, since the tree obviously has one */
                            g_object_unref (n);
                            if (donna_node_get_node_type (n) == nt)
                            {
                                *iter = it;
                                return ROW_ID_ROW;
                            }
                        }
                    }
                }
                return ROW_ID_INVALID;
            }
            else
                return ROW_ID_INVALID;
        }
        else
        {
            GtkTreePath *path;
            GtkTreeIter iter_top;
            gchar *end;
            gint i;
            enum
            {
                LINE,
                PCTG_TREE,
                PCTG_VISIBLE
            } flg = LINE;

            if (*s == '%')
            {
                flg = PCTG_VISIBLE;
                ++s;
            }

            i = (gint) g_ascii_strtoll (s, &end, 10);
            if (i < 0)
                return ROW_ID_INVALID;

            if (end[0] == '%' && end[1] == '\0')
                flg = PCTG_TREE;
            else if (*end == '\0')
                i = MAX (1, i);
            else
                return ROW_ID_INVALID;

            if (flg != LINE)
            {
                DonnaRowId rid = { DONNA_ARG_TYPE_PATH, NULL };
                GdkRectangle rect;
                gint height;
                gint rows;
                gint top;

                /* locate first/top row */
                if (flg == PCTG_TREE)
                    path = gtk_tree_path_new_from_indices (0, -1);
                else
                {
                    rid.ptr = (gpointer) ":top";
                    if (convert_row_id_to_iter (tree, &rid, &iter_top) == ROW_ID_INVALID)
                        return ROW_ID_INVALID;
                    path = gtk_tree_model_get_path (model, &iter_top);
                    if (!priv->is_tree)
                        top = gtk_tree_path_get_indices (path)[0];
                }
                gtk_tree_view_get_background_area (treev, path, NULL, &rect);
                gtk_tree_path_free (path);
                height = ABS (rect.y);

                /* locate last/bottom row */
                if (flg == PCTG_TREE)
                {
                    if (!_gtk_tree_model_iter_last (model, iter))
                        return ROW_ID_INVALID;
                }
                else
                {
                    rid.ptr = (gpointer) ":bottom";
                    if (convert_row_id_to_iter (tree, &rid, iter) == ROW_ID_INVALID)
                            return ROW_ID_INVALID;
                }
                path = gtk_tree_model_get_path (model, iter);
                if (!path)
                    return ROW_ID_INVALID;
                gtk_tree_view_get_background_area (treev, path, NULL, &rect);
                gtk_tree_path_free (path);
                height += ABS (rect.y) + rect.height;

                /* nb of rows accessible/visible on tree */
                rows = height / rect.height;

                /* get the one at specified percent */
                i = (gint) ((gdouble) rows * ((gdouble) i / 100.0)) + 1;
                i = CLAMP (i, 1, rows);

                if (flg == PCTG_VISIBLE && !priv->is_tree)
                    i += top;
            }

            if (priv->is_tree)
            {
                /* we can't just get a path, so we'll go to the first/top row
                 * and move down */
                if (flg == PCTG_VISIBLE)
                    *iter = iter_top;
                else
                    if (!gtk_tree_model_iter_children (model, iter, NULL))
                        return ROW_ID_INVALID;

                --i;
                while (i > 0)
                {
                    if (!_gtk_tree_model_iter_next (model, iter))
                        return ROW_ID_INVALID;
                    if (is_row_accessible (tree, iter))
                        --i;
                }
                path = gtk_tree_model_get_path (model, iter);
            }
            else
                path = gtk_tree_path_new_from_indices (i - 1, -1);

            if (gtk_tree_model_get_iter (model, iter, path))
            {
                gtk_tree_path_free (path);
                return ROW_ID_ROW;
            }
            gtk_tree_path_free (path);
            return ROW_ID_INVALID;
        }
    }
    else
        return ROW_ID_INVALID;
}

/* special case for "root_on_child" functions (e.g. root_get_child_visual) where
 * the rowid must be that of a root. This means is we have a node and there are
 * more than one rows on tree for said node, let's ignore non-root ones to try
 * to find a valid match */
/* mode tree only */
static row_id_type
convert_row_id_to_root_iter (DonnaTreeView  *tree,
                             DonnaRowId     *rowid,
                             GtkTreeIter    *iter)
{
    DonnaTreeViewPrivate *priv = tree->priv;
    GSList *list;

    if (rowid->type != DONNA_ARG_TYPE_NODE)
        return convert_row_id_to_iter (tree, rowid, iter);

    list = g_hash_table_lookup (priv->hashtable, rowid->ptr);
    if (!list)
        return ROW_ID_INVALID;
    for ( ; list; list = list->next)
        if (gtk_tree_store_iter_depth (priv->store, list->data) == 0)
        {
            *iter = * (GtkTreeIter *) list->data;
            return ROW_ID_ROW;
        }

    return ROW_ID_INVALID;
}


static void
unselect_path (gpointer p, gpointer s)
{
    gtk_tree_selection_unselect_path ((GtkTreeSelection *) s, (GtkTreePath *) p);
}

/**
 * donna_tree_view_selection:
 * @tree: A #DonnaTreeView
 * @action: Which action to perform on the selection
 * @rowid: Identifier of a row; See #rowid for more
 * @to_focused: When %TRUE rows affected will be the range from @rowid to the
 * focused row
 * @error: (allow-none): Return location of a #GError, or %NULL
 *
 * Affects the selection on @tree. For trees, the only supported action is
 * %DONNA_SEL_SELECT to set the selection on one row. This is pretty much the
 * same as using donna_tree_view_set_location() but allows to specify the row,
 * in case the same location exists for more than one rows on tree.
 *
 * For lists, you can select, unselect, invert or define the selection.
 *
 * If @to_focused is %TRUE then @rowid must point to a row which will be used as
 * other boundary (with focused row) to make the range of rows to be used.
 *
 * Note that inverting a range (using @to_focused) is only supported with a
 * patched GTK.
 *
 * Returns: %TRUE on success, else %FALSE
 */
gboolean
donna_tree_view_selection (DonnaTreeView        *tree,
                           DonnaSelAction        action,
                           DonnaRowId           *rowid,
                           gboolean              to_focused,
                           GError              **error)
{
    DonnaTreeViewPrivate *priv;
    GtkTreeSelection *sel;
    GtkTreeIter iter;
    row_id_type type;

    g_return_val_if_fail (DONNA_IS_TREE_VIEW (tree), FALSE);
    g_return_val_if_fail (action == DONNA_SEL_SELECT
            || action == DONNA_SEL_UNSELECT
            || action == DONNA_SEL_INVERT
            || action == DONNA_SEL_DEFINE, FALSE);
    g_return_val_if_fail (rowid != NULL, FALSE);

    priv = tree->priv;
    sel = gtk_tree_view_get_selection ((GtkTreeView *) tree);

    type = convert_row_id_to_iter (tree, rowid, &iter);
    if (type == ROW_ID_INVALID)
    {
        g_set_error (error, DONNA_TREE_VIEW_ERROR,
                DONNA_TREE_VIEW_ERROR_INVALID_ROW_ID,
                "TreeView '%s': Cannot update selection, invalid row-id",
                priv->name);
        return FALSE;
    }

    /* tree is limited in its selection capabilities */
    if (priv->is_tree && !(type == ROW_ID_ROW && !to_focused
                && action == DONNA_SEL_SELECT))
    {
        g_set_error (error, DONNA_TREE_VIEW_ERROR,
                DONNA_TREE_VIEW_ERROR_INCOMPATIBLE_OPTION,
                "TreeView '%s': Cannot update selection, incompatible with mode tree",
                priv->name);
        return FALSE;
    }

    if (type == ROW_ID_ALL)
    {
        if (action == DONNA_SEL_SELECT || action == DONNA_SEL_DEFINE)
            gtk_tree_selection_select_all (sel);
        else if (action == DONNA_SEL_UNSELECT)
            gtk_tree_selection_unselect_all (sel);
        else /* DONNA_SEL_INVERT */
        {
            gint nb, count;
            GList *list;

            nb = gtk_tree_selection_count_selected_rows (sel);
            if (nb == 0)
            {
                gtk_tree_selection_select_all (sel);
                return TRUE;
            }

            count = _gtk_tree_model_get_count ((GtkTreeModel *) priv->store);
            if (nb == count)
            {
                gtk_tree_selection_unselect_all (sel);
                return TRUE;
            }

            list = gtk_tree_selection_get_selected_rows (sel, NULL);
            gtk_tree_selection_select_all (sel);
            g_list_foreach (list, unselect_path, sel);
            g_list_free_full (list, (GDestroyNotify) gtk_tree_path_free);
        }
        return TRUE;
    }
    else if (type == ROW_ID_SELECTION)
    {
        /* SELECT/DEFINE the selection means do nothing; UNSELECT & INVERT both
         * means unselect (all) */
        if (action == DONNA_SEL_UNSELECT || action == DONNA_SEL_INVERT)
            gtk_tree_selection_unselect_all (sel);
        return TRUE;
    }
    else /* ROW_ID_ROW */
    {
        if (to_focused)
        {
            GtkTreePath *path, *path_focus;

            gtk_tree_view_get_cursor ((GtkTreeView *) tree, &path_focus, NULL);
            if (!path_focus)
            {
                g_set_error (error, DONNA_TREE_VIEW_ERROR,
                        DONNA_TREE_VIEW_ERROR_OTHER,
                        "TreeView '%s': Cannot update selection, failed to get focused row",
                        priv->name);
                return FALSE;
            }
            path = gtk_tree_model_get_path ((GtkTreeModel *) priv->store, &iter);
            if (!path)
            {
                g_set_error (error, DONNA_TREE_VIEW_ERROR,
                        DONNA_TREE_VIEW_ERROR_OTHER,
                        "TreeView '%s': Cannot update selection, failed to get path",
                        priv->name);
                gtk_tree_path_free (path_focus);
                return FALSE;
            }

            if (action == DONNA_SEL_DEFINE)
            {
                gtk_tree_selection_unselect_all (sel);
                action = DONNA_SEL_SELECT;
            }

            if (action == DONNA_SEL_SELECT)
                gtk_tree_selection_select_range (sel, path, path_focus);
            else if (action == DONNA_SEL_UNSELECT)
                gtk_tree_selection_unselect_range (sel, path, path_focus);
            else /* DONNA_SEL_INVERT */
#ifdef GTK_IS_JJK
                gtk_tree_selection_invert_range (sel, path, path_focus);
#else
            {
                g_set_error (error, DONNA_TREE_VIEW_ERROR,
                        DONNA_TREE_VIEW_ERROR_OTHER,
                        "TreeView '%s': Cannot invert selection on a range (Vanilla GTK+ limitation)",
                        priv->name);
                gtk_tree_path_free (path);
                gtk_tree_path_free (path_focus);
                return FALSE;
            }
#endif

            gtk_tree_path_free (path);
            gtk_tree_path_free (path_focus);
            return TRUE;
        }
        else
        {
            if (action == DONNA_SEL_DEFINE)
            {
                gtk_tree_selection_unselect_all (sel);
                action = DONNA_SEL_SELECT;
            }

            if (action == DONNA_SEL_SELECT)
                gtk_tree_selection_select_iter (sel, &iter);
            else if (action == DONNA_SEL_UNSELECT)
                gtk_tree_selection_unselect_iter (sel, &iter);
            else /* DONNA_SEL_INVERT */
            {
                if (gtk_tree_selection_iter_is_selected (sel, &iter))
                    gtk_tree_selection_unselect_iter (sel, &iter);
                else
                    gtk_tree_selection_select_iter (sel, &iter);
            }
            return TRUE;
        }
    }
}

/* mode list only */
/**
 * donna_tree_view_selection_nodes:
 * @tree: A #DonnaTreeView
 * @action: Which action to perform on the selection
 * @nodes: (element-type DonnaNode): An array of nodes
 * @error: (allow-none): Return location of a #GError, or %NULL
 *
 * Similar to donna_tree_view_selection() only using the given nodes instead of
 * a rowid. Any node not found if the treeview will be
 * ignored/skipped.
 *
 * Note that this is only supported in lists.
 *
 * Returns: %TRUE on success, else %FALSE
 */
gboolean
donna_tree_view_selection_nodes (DonnaTreeView      *tree,
                                 DonnaSelAction      action,
                                 GPtrArray          *nodes,
                                 GError            **error)
{
    DonnaTreeViewPrivate *priv;
    GtkTreeSelection *sel;
    guint i;

    g_return_val_if_fail (DONNA_IS_TREE_VIEW (tree), FALSE);
    g_return_val_if_fail (action == DONNA_SEL_SELECT
            || action == DONNA_SEL_UNSELECT
            || action == DONNA_SEL_INVERT
            || action == DONNA_SEL_DEFINE, FALSE);
    g_return_val_if_fail (nodes != NULL, FALSE);

    priv = tree->priv;
    sel = gtk_tree_view_get_selection ((GtkTreeView *) tree);

    if (nodes->len == 0)
    {
        if (action == DONNA_SEL_DEFINE)
            gtk_tree_selection_unselect_all (sel);
        return TRUE;
    }

    /* tree is limited in its selection capabilities */
    if (priv->is_tree)
    {
        g_set_error (error, DONNA_TREE_VIEW_ERROR,
                DONNA_TREE_VIEW_ERROR_INVALID_MODE,
                "TreeView '%s': Cannot update selection, incompatible with mode tree",
                priv->name);
        return FALSE;
    }

    if (action == DONNA_SEL_DEFINE)
    {
        gtk_tree_selection_unselect_all (sel);
        action = DONNA_SEL_SELECT;
    }

    /* we set this so all the selection-changed signals that will be emitted
     * after each (un)select_iter() call will be noop (otherwise it slows things
     * down a bit */
    priv->filling_list = TRUE;
    for (i = 0; i < nodes->len; ++i)
    {
        GtkTreeIter *iter;

        iter = g_hash_table_lookup (priv->hashtable, nodes->pdata[i]);
        if (!iter)
            continue;

        if (action == DONNA_SEL_SELECT)
            gtk_tree_selection_select_iter (sel, iter);
        else if (action == DONNA_SEL_UNSELECT)
            gtk_tree_selection_unselect_iter (sel, iter);
        else /* DONNA_SEL_INVERT */
        {
            if (gtk_tree_selection_iter_is_selected (sel, iter))
                gtk_tree_selection_unselect_iter (sel, iter);
            else
                gtk_tree_selection_select_iter (sel, iter);
        }
    }
    /* restore things, and also make sure the status are updated (since we
     * prevented that from happening) */
    priv->filling_list = FALSE;
    check_statuses (tree, STATUS_CHANGED_ON_CONTENT);

    return TRUE;
}

/**
 * donna_tree_view_set_focus:
 * @tree: A #DonnaTreeView
 * @rowid: Identifier of a row; See #rowid for more
 * @error: (allow-none): Return location of a #GError, or %NULL
 *
 * Make @rowid the focused row.
 *
 * Note that this only set the focused row, nothing else. Specifically,
 * selection will not be affected, nor will any scrolling take place. If you
 * want to e.g. also scroll to the row, see donna_tree_view_goto_line()
 *
 * Returns: %TRUE on success, else %FALSE
 */
gboolean
donna_tree_view_set_focus (DonnaTreeView        *tree,
                           DonnaRowId           *rowid,
                           GError              **error)
{
    DonnaTreeViewPrivate *priv;
    GtkTreeIter  iter;
    row_id_type  type;
    GtkTreePath *path;

    g_return_val_if_fail (DONNA_IS_TREE_VIEW (tree), FALSE);
    g_return_val_if_fail (rowid != NULL, FALSE);
    priv = tree->priv;

    type = convert_row_id_to_iter (tree, rowid, &iter);
    if (type != ROW_ID_ROW)
    {
        g_set_error (error, DONNA_TREE_VIEW_ERROR,
                DONNA_TREE_VIEW_ERROR_INVALID_ROW_ID,
                "TreeView '%s': Cannot set focus, invalid row-id",
                priv->name);
        return FALSE;
    }

    path = gtk_tree_model_get_path ((GtkTreeModel *) priv->store, &iter);
    gtk_tree_view_set_focused_row ((GtkTreeView *) tree, path);
    gtk_tree_path_free (path);
    check_statuses (tree, STATUS_CHANGED_ON_CONTENT);
    return TRUE;
}

/**
 * donna_tree_view_set_cursor:
 * @tree: A #DonnaTreeView
 * @rowid: Identifier of a row; See #rowid for more
 * @no_scroll: %TRUE not to scroll to @rowid
 * @error: (allow-none): Return location of a #GError, or %NULL
 *
 * Set the cursor on @rowid. This is the GTK terminology for the following:
 * - unselect all
 * - make @rowid the focused row
 * - scroll to @rowid (unless @no_scroll is %TRUE)
 *
 * Note that you can have more control over what to do (select, scroll, etc)
 * using donna_tree_view_goto_line()
 *
 * Returns: %TRUE on success, else %FALSE
 */
gboolean
donna_tree_view_set_cursor (DonnaTreeView        *tree,
                            DonnaRowId           *rowid,
                            gboolean              no_scroll,
                            GError              **error)
{
    DonnaTreeViewPrivate *priv;
    GtkTreeSelection *sel;
    GtkTreeIter  iter;
    row_id_type  type;
    GtkTreePath *path;

    g_return_val_if_fail (DONNA_IS_TREE_VIEW (tree), FALSE);
    g_return_val_if_fail (rowid != NULL, FALSE);
    priv = tree->priv;

    type = convert_row_id_to_iter (tree, rowid, &iter);
    if (type != ROW_ID_ROW)
    {
        g_set_error (error, DONNA_TREE_VIEW_ERROR,
                DONNA_TREE_VIEW_ERROR_INVALID_ROW_ID,
                "TreeView '%s': Cannot set cursor, invalid row-id",
                priv->name);
        return FALSE;
    }

    /* more about this can be read in sync_with_location_changed_cb() */

    path = gtk_tree_model_get_path ((GtkTreeModel *) priv->store, &iter);
    gtk_tree_view_set_focused_row ((GtkTreeView *) tree, path);
    sel = gtk_tree_view_get_selection ((GtkTreeView *) tree);
    if (!priv->is_tree)
        gtk_tree_selection_unselect_all (sel);
    gtk_tree_selection_select_path (sel, path);
    gtk_tree_path_free (path);
    /* no_scroll instead of scroll so in command (which mimics the params, but
     * where that one is optional) the default is FALSE, i.e. scroll, but it can
     * be set to TRUE to disable scrolling. */
    if (!no_scroll)
        scroll_to_iter (tree, &iter);
    return TRUE;
}

/**
 * donna_tree_view_activate_row:
 * @tree: A #DonnaTreeView
 * @rowid: Identifier of a row; See #rowid for more
 * @error: (allow-none): Return location of a #GError, or %NULL
 *
 * "Activates" @rowid; Activating a row means:
 * - for container, make it the new current location
 * - for items, trigger it (e.g. open the file w/ associated application)
 *
 * Note that if @rowid refers to more than one row (i.e. all/selected rows) then
 * containers will wimply be skipped.
 *
 * Returns: %TRUE on success, else %FALSE. Note that success here means all rows
 * were succesfully activated (or skipped), which only means the task was
 * started, regardless of whether or not it succeeded.
 */
gboolean
donna_tree_view_activate_row (DonnaTreeView      *tree,
                              DonnaRowId         *rowid,
                              GError            **error)
{
    DonnaTreeViewPrivate *priv;
    GtkTreeSelection *sel;
    GtkTreeModel *model;
    GtkTreeIter iter_focus;
    GtkTreeIter iter;
    row_id_type type;
    gboolean second_pass = FALSE;
    gboolean ret = TRUE;

    g_return_val_if_fail (DONNA_IS_TREE_VIEW (tree), FALSE);
    g_return_val_if_fail (rowid != NULL, FALSE);
    priv  = tree->priv;
    model = (GtkTreeModel *) priv->store;

    type = convert_row_id_to_iter (tree, rowid, &iter);
    if (type == ROW_ID_INVALID)
    {
        g_set_error (error, DONNA_TREE_VIEW_ERROR,
                DONNA_TREE_VIEW_ERROR_INVALID_ROW_ID,
                "TreeView '%s': Cannot activate row, invalid row-id",
                priv->name);
        return FALSE;
    }

    if (type == ROW_ID_SELECTION)
        sel = gtk_tree_view_get_selection ((GtkTreeView *) tree);
    if (type == ROW_ID_SELECTION || type == ROW_ID_ALL)
        /* for SELECTION we'll also iter through each row, and check whether or
         * not they're selected. Might not seem like the best of choices, but
         * this is what gtk_tree_selection_get_selected_rows() actually does,
         * so this makes this code simpler (and avoids GList "overhead").
         * Also, we don't loop from top to bottom, but focus to bottom & then
         * top to focus, to allow user to set "order"/the first item. */
        if (!init_getting_nodes ((GtkTreeView *) tree, model, &iter_focus, &iter))
            /* empty tree. I consider this a success */
            return TRUE;

again:
    for (;;)
    {
        DonnaNode *node;

        if (second_pass && itereq (&iter, &iter_focus))
        {
            iter_focus.stamp = 0;
            break;
        }

        if (type == ROW_ID_SELECTION
                && !gtk_tree_selection_iter_is_selected (sel, &iter))
            goto next;

        gtk_tree_model_get (model, &iter,
                TREE_VIEW_COL_NODE, &node,
                -1);
        if (G_UNLIKELY (!node))
            goto next;

        /* since the node is in the tree, we already have a ref on it */
        g_object_unref (node);

        if (donna_node_get_node_type (node) == DONNA_NODE_CONTAINER)
        {
            /* only for single row; else we'd risk having to go into multiple
             * locations, so we just don't support it/ignore them */
            if (type == ROW_ID_ROW)
                ret = (donna_tree_view_set_location (tree, node, error)) ? ret : FALSE;
        }
        else /* DONNA_NODE_ITEM */
        {
            DonnaTask *task;

            task = donna_node_trigger_task (node, error);
            if (G_UNLIKELY (!task))
            {
                ret = FALSE;
                goto next;
            }

            donna_task_set_callback (task,
                    (task_callback_fn) show_err_on_task_failed, tree, NULL);
            donna_app_run_task (priv->app, task);
        }

next:
        if (type == ROW_ID_ROW || !_gtk_tree_model_iter_next (model, &iter))
            break;
    }

    /* if we started at focus, let's start back from top to focus */
    if (type != ROW_ID_ROW && iter_focus.stamp != 0)
    {
        gtk_tree_model_iter_children (model, &iter, NULL);
        second_pass = TRUE;
        goto again;
    }

    return ret;
}

/* mode tree only */
/**
 * donna_tree_view_toggle_row:
 * @tree: A #DonnaTreeView
 * @rowid: Identifier of a row; See #rowid for more
 * @toggle: Which type of toggling to perform
 * @error: (allow-none): Return location of a #GError, or %NULL
 *
 * Toggle @rowid, i.e. collapse or expand the row.
 *
 * Maxi toggle (%DONNA_TREE_TOGGLE_MAXI) is a special operation:
 * - if @rowid is expanded, but only partially (i.e. in #minitree) then it will
 *   remain expanded, but switch to maxi expand. Else it is maxi collapsed.
 * - if @rowid is collapsed and has never been expanded, it is maxi expanded. If
 *   it had been expanded before, it is maxi collapsed.
 *
 * Note that this obviously is only supported on trees.
 *
 * Returns: %TRUE on success, else %FALSE
 */
gboolean
donna_tree_view_toggle_row (DonnaTreeView      *tree,
                            DonnaRowId         *rowid,
                            DonnaTreeToggle     toggle,
                            GError            **error)
{
    DonnaTreeViewPrivate *priv;
    GtkTreeView *treev = (GtkTreeView *) tree;
    GtkTreeIter  iter;
    row_id_type  type;
    enum tree_expand es;
    GtkTreePath *path;
    gboolean     ret = TRUE;

    g_return_val_if_fail (DONNA_IS_TREE_VIEW (tree), FALSE);
    g_return_val_if_fail (rowid != NULL, FALSE);
    priv = tree->priv;

    if (G_UNLIKELY (!priv->is_tree))
    {
        g_set_error (error, DONNA_TREE_VIEW_ERROR,
                DONNA_TREE_VIEW_ERROR_INVALID_MODE,
                "TreeView '%s': toggle_row() doesn't apply in mode list",
                priv->name);
        return FALSE;
    }

    type = convert_row_id_to_iter (tree, rowid, &iter);
    if (type != ROW_ID_ROW)
    {
        g_set_error (error, DONNA_TREE_VIEW_ERROR,
                DONNA_TREE_VIEW_ERROR_INVALID_ROW_ID,
                "TreeView '%s': Cannot toggle row, invalid row-id",
                priv->name);
        return FALSE;
    }

    gtk_tree_model_get ((GtkTreeModel *) priv->store, &iter,
            TREE_COL_EXPAND_STATE,  &es,
            -1);
    if (es == TREE_EXPAND_NONE)
        return TRUE;

    path = gtk_tree_model_get_path ((GtkTreeModel *) priv->store, &iter);
    if (G_UNLIKELY (!path))
    {
        g_set_error (error, DONNA_TREE_VIEW_ERROR, DONNA_TREE_VIEW_ERROR_OTHER,
                "TreeView '%s': Failed to obtain path for iter", priv->name);
        return FALSE;
    }

    if (gtk_tree_view_row_expanded (treev, path))
    {
        if (toggle == DONNA_TREE_TOGGLE_STANDARD)
            gtk_tree_view_collapse_row (treev, path);
        else if (toggle == DONNA_TREE_TOGGLE_FULL)
            ret = donna_tree_view_full_collapse (tree, rowid, error);
        else /* DONNA_TREE_TOGGLE_MAXI */
        {
            /* maxi is a special kind of toggle: if partially expanded, we
             * maxi-expand; Else, we maxi collapse */
            if (es == TREE_EXPAND_PARTIAL)
                ret = donna_tree_view_maxi_expand (tree, rowid, error);
            else
                ret = donna_tree_view_maxi_collapse (tree, rowid, error);
        }
    }
    else
    {
        if (toggle == DONNA_TREE_TOGGLE_STANDARD)
            gtk_tree_view_expand_row (treev, path, FALSE);
        else if (toggle == DONNA_TREE_TOGGLE_FULL)
            ret = donna_tree_view_full_expand (tree, rowid, error);
        else /* DONNA_TREE_TOGGLE_MAXI */
        {
            /* maxi is a special kind of toggle: if never expanded, we (maxi)
             * expand; Else we maxi collapse */
            if (es == TREE_EXPAND_NEVER || es == TREE_EXPAND_UNKNOWN)
                gtk_tree_view_expand_row (treev, path, FALSE);
            else
                ret = donna_tree_view_maxi_collapse (tree, rowid, error);
        }
    }

    gtk_tree_path_free (path);
    return ret;
}

static void full_expand_children (DonnaTreeView *tree, GtkTreeIter *iter);

static inline void
full_expand (DonnaTreeView *tree, GtkTreeIter *iter)
{
    DonnaTreeViewPrivate *priv = tree->priv;
    GtkTreeModel *model = (GtkTreeModel *) priv->store;
    enum tree_expand es;

    gtk_tree_model_get (model, iter,
            TREE_COL_EXPAND_STATE,  &es,
            -1);
    switch (es)
    {
        case TREE_EXPAND_UNKNOWN:
        case TREE_EXPAND_NEVER:
            /* will import/create get_children task. Will also call ourself
             * on iter, or make sure it gets called from the task's cb */
            expand_row (tree, iter, /* expand */ TRUE,
                    /* scroll to current */ FALSE, full_expand_children);
            break;

        case TREE_EXPAND_PARTIAL:
        case TREE_EXPAND_MAXI:
            {
                GtkTreePath *path;
                path = gtk_tree_model_get_path (model, iter);
                gtk_tree_view_expand_row ((GtkTreeView *) tree, path, FALSE);
                gtk_tree_path_free (path);
                /* recursion */
                full_expand_children (tree, iter);
            }
            break;

        case TREE_EXPAND_NONE:
        case TREE_EXPAND_WIP:
            break;
    }
}

static void
full_expand_children (DonnaTreeView *tree, GtkTreeIter *iter)
{
    DonnaTreeViewPrivate *priv = tree->priv;
    GtkTreeModel *model = (GtkTreeModel *) priv->store;
    GtkTreeIter child;

    if (G_UNLIKELY (!gtk_tree_model_iter_children (model, &child, iter)))
        return;

    do
    {
        full_expand (tree, &child);
    } while (gtk_tree_model_iter_next (model, &child));
}

/**
 * donna_tree_view_full_expand:
 * @tree: A #DonnaTreeView
 * @rowid: Identifier of a row; See #rowid for more
 * @error: (allow-none): Return location of a #GError, or %NULL
 *
 * Perform a full expand of @rowid. Full expand means that it will expand the
 * row itself, as well as every one of its children (and so on recursively).
 *
 * Note that this is obviously only supported on trees.
 *
 * Returns: %TRUE on success, else %FALSE
 */
gboolean
donna_tree_view_full_expand (DonnaTreeView      *tree,
                             DonnaRowId         *rowid,
                             GError            **error)
{
    DonnaTreeViewPrivate *priv;
    GtkTreeIter iter;
    row_id_type type;

    g_return_val_if_fail (DONNA_IS_TREE_VIEW (tree), FALSE);
    g_return_val_if_fail (rowid != NULL, FALSE);
    priv = tree->priv;

    if (G_UNLIKELY (!priv->is_tree))
    {
        g_set_error (error, DONNA_TREE_VIEW_ERROR,
                DONNA_TREE_VIEW_ERROR_INVALID_MODE,
                "TreeView '%s': full_expand() doesn't apply in mode list",
                priv->name);
        return FALSE;
    }

    type = convert_row_id_to_iter (tree, rowid, &iter);
    if (type != ROW_ID_ROW)
    {
        g_set_error (error, DONNA_TREE_VIEW_ERROR,
                DONNA_TREE_VIEW_ERROR_INVALID_ROW_ID,
                "TreeView '%s': Cannot full-expand row, invalid row-id",
                priv->name);
        return FALSE;
    }

    full_expand (tree, &iter);
    return TRUE;
}

static void
reset_expand_flag (GtkTreeModel *model, GtkTreeIter *iter)
{
    GtkTreeIter child;

    if (G_UNLIKELY (!gtk_tree_model_iter_children (model, &child, iter)))
        return;

    do
    {
        gtk_tree_store_set ((GtkTreeStore *) model, &child,
                TREE_COL_EXPAND_FLAG,   FALSE,
                -1);
        reset_expand_flag (model, &child);
    } while (gtk_tree_model_iter_next (model, &child));
}

/**
 * donna_tree_view_full_collapse:
 * @tree: A #DonnaTreeView
 * @rowid: Identifier of a row; See #rowid for more
 * @error: (allow-none): Return location of a #GError, or %NULL
 *
 * Perform a full collapse of @rowid. Full collapse means that it will collapse
 * the row itself, as well as every one of its children (and so on recursively).
 *
 * Note that this is obviously only supported on trees.
 *
 * Returns: %TRUE on success, else %FALSE
 */
gboolean
donna_tree_view_full_collapse (DonnaTreeView      *tree,
                               DonnaRowId         *rowid,
                               GError            **error)
{
    DonnaTreeViewPrivate *priv;
    GtkTreeIter   iter;
    row_id_type   type;
    GtkTreePath  *path;

    g_return_val_if_fail (DONNA_IS_TREE_VIEW (tree), FALSE);
    g_return_val_if_fail (rowid != NULL, FALSE);
    priv = tree->priv;

    if (G_UNLIKELY (!priv->is_tree))
    {
        g_set_error (error, DONNA_TREE_VIEW_ERROR,
                DONNA_TREE_VIEW_ERROR_INVALID_MODE,
                "TreeView '%s': full_collapse() doesn't apply in mode list",
                priv->name);
        return FALSE;
    }

    type = convert_row_id_to_iter (tree, rowid, &iter);
    if (type != ROW_ID_ROW)
    {
        g_set_error (error, DONNA_TREE_VIEW_ERROR,
                DONNA_TREE_VIEW_ERROR_INVALID_ROW_ID,
                "TreeView '%s': Cannot full-collapse row, invalid row-id",
                priv->name);
        return FALSE;
    }

    path = gtk_tree_model_get_path ((GtkTreeModel *) priv->store, &iter);
    gtk_tree_view_collapse_row ((GtkTreeView *) tree, path);
    gtk_tree_path_free (path);

    /* we also need to recursively set the EXPAND_FLAG to FALSE */
    reset_expand_flag ((GtkTreeModel *) priv->store, &iter);

    return TRUE;
}

/**
 * donna_tree_view_maxi_expand:
 * @tree: A #DonnaTreeView
 * @rowid: Identifier of a row; See #rowid for more
 * @error: (allow-none): Return location of a #GError, or %NULL
 *
 * Perform a maxi expand of @rowid. Maxi expand means that it will expand
 * the row, but also make sure the row is maxi expanded, i.e. that all of its
 * children are loaded on tree.
 *
 * Note that this is obviously only supported on #minitree<!-- -->s.
 *
 * Returns: %TRUE on success, else %FALSE
 */
gboolean
donna_tree_view_maxi_expand (DonnaTreeView      *tree,
                             DonnaRowId         *rowid,
                             GError            **error)
{
    DonnaTreeViewPrivate *priv;
    GtkTreeIter   iter;
    row_id_type   type;

    g_return_val_if_fail (DONNA_IS_TREE_VIEW (tree), FALSE);
    g_return_val_if_fail (rowid != NULL, FALSE);
    priv = tree->priv;

    if (G_UNLIKELY (!priv->is_tree))
    {
        g_set_error (error, DONNA_TREE_VIEW_ERROR,
                DONNA_TREE_VIEW_ERROR_INVALID_MODE,
                "TreeView '%s': maxi_expand() doesn't apply in mode list",
                priv->name);
        return FALSE;
    }
    if (G_UNLIKELY (!priv->is_minitree))
    {
        g_set_error (error, DONNA_TREE_VIEW_ERROR, DONNA_TREE_VIEW_ERROR_OTHER,
                "TreeView '%s': maxi_expand() only works in mini-tree",
                priv->name);
        return FALSE;
    }

    type = convert_row_id_to_iter (tree, rowid, &iter);
    if (type != ROW_ID_ROW)
    {
        g_set_error (error, DONNA_TREE_VIEW_ERROR,
                DONNA_TREE_VIEW_ERROR_INVALID_ROW_ID,
                "TreeView '%s': Cannot maxi-expand row, invalid row-id",
                priv->name);
        return FALSE;
    }

    maxi_expand_row (tree, &iter);
    return TRUE;
}

/**
 * donna_tree_view_maxi_collapse:
 * @tree: A #DonnaTreeView
 * @rowid: Identifier of a row; See #rowid for more
 * @error: (allow-none): Return location of a #GError, or %NULL
 *
 * Perform a maxi collapse of @rowid. Maxi collapse means that it will collapse
 * the row, but also remove all its children from the tree.
 *
 * Note that this is obviously only supported on trees, even "maxi-tree" (i.e.
 * not a #minitree).
 *
 * Returns: %TRUE on success, else %FALSE
 */
gboolean
donna_tree_view_maxi_collapse (DonnaTreeView      *tree,
                               DonnaRowId         *rowid,
                               GError            **error)
{
    DonnaTreeViewPrivate *priv;
    GtkTreeIter   iter;
    row_id_type   type;

    g_return_val_if_fail (DONNA_IS_TREE_VIEW (tree), FALSE);
    g_return_val_if_fail (rowid != NULL, FALSE);
    priv = tree->priv;

    if (G_UNLIKELY (!priv->is_tree))
    {
        g_set_error (error, DONNA_TREE_VIEW_ERROR,
                DONNA_TREE_VIEW_ERROR_INVALID_MODE,
                "TreeView '%s': maxi_collapse() doesn't apply in mode list",
                priv->name);
        return FALSE;
    }

    type = convert_row_id_to_iter (tree, rowid, &iter);
    if (type != ROW_ID_ROW)
    {
        g_set_error (error, DONNA_TREE_VIEW_ERROR,
                DONNA_TREE_VIEW_ERROR_INVALID_ROW_ID,
                "TreeView '%s': Cannot maxi-collapse row, invalid row-id",
                priv->name);
        return FALSE;
    }

    maxi_collapse_row (tree, &iter);
    return TRUE;
}

/* mode tree only */
static gboolean
set_tree_visual (DonnaTreeView  *tree,
                 GtkTreeIter    *iter,
                 DonnaTreeVisual visual,
                 const gchar    *_value,
                 GError        **error)
{
    DonnaTreeViewPrivate *priv = tree->priv;
    DonnaTreeVisual v;
    gpointer value = (gpointer) _value;
    guint col;

    if (visual == DONNA_TREE_VISUAL_NAME)
        col = TREE_COL_NAME;
    else if (visual == DONNA_TREE_VISUAL_ICON)
    {
        col = TREE_COL_ICON;
        if (!_value)
            value = NULL;
        else
        {
            if (*_value == '/')
            {
                GFile *file;

                file = g_file_new_for_path (_value);
                value = g_file_icon_new (file);
                g_object_unref (file);
            }
            else
                value = g_themed_icon_new (_value);

            if (!value)
            {
                g_set_error (error, DONNA_TREE_VIEW_ERROR,
                        DONNA_TREE_VIEW_ERROR_OTHER,
                        "TreeView '%s': Cannot set visual 'icon', "
                        "unable to get icon from '%s'",
                        priv->name, _value);
                return FALSE;
            }
        }
    }
    else if (visual == DONNA_TREE_VISUAL_BOX)
        col = TREE_COL_BOX;
    else if (visual == DONNA_TREE_VISUAL_HIGHLIGHT)
        col = TREE_COL_HIGHLIGHT;
    else if (visual == DONNA_TREE_VISUAL_CLICK_MODE)
        col = TREE_COL_CLICK_MODE;
    else
    {
        g_set_error (error, DONNA_TREE_VIEW_ERROR,
                DONNA_TREE_VIEW_ERROR_OTHER,
                "TreeView '%s': Cannot set visual, invalid visual type",
                priv->name);
        return FALSE;
    }

    gtk_tree_model_get ((GtkTreeModel *) priv->store, iter,
            TREE_COL_VISUALS,   &v,
            -1);
    if (value)
        v |= visual;
    else
        v &= ~visual;

    gtk_tree_store_set (priv->store, iter,
            TREE_COL_VISUALS,   v,
            col,                value,
            -1);

    if (visual == DONNA_TREE_VISUAL_ICON)
        donna_g_object_unref (value);

    if (!value && (priv->node_visuals & visual))
    {
        DonnaNode *node;

        /* if we show the node visual and there's one, restore it */

        gtk_tree_model_get ((GtkTreeModel *) priv->store, iter,
                TREE_COL_NODE,  &node,
                -1);
        load_node_visuals (tree, iter, node, FALSE);
        g_object_unref (node);
    }

    return TRUE;
}

/**
 * donna_tree_view_set_visual:
 * @tree: A #DonnaTreeView
 * @rowid: Identifier of a row; See #rowid for more
 * @visual: Which visual to set
 * @value: New value to set for @visual
 * @error: (allow-none): Return location of a #GError, or %NULL
 *
 * Set the value of the specified #tree-visuals.
 *
 * Note that this is obviously only supported on trees.
 *
 * Returns: %TRUE on success, else %FALSE
 */
gboolean
donna_tree_view_set_visual (DonnaTreeView      *tree,
                            DonnaRowId         *rowid,
                            DonnaTreeVisual     visual,
                            const gchar        *value,
                            GError            **error)
{
    DonnaTreeViewPrivate *priv;
    GtkTreeIter  iter;
    row_id_type  type;

    g_return_val_if_fail (DONNA_IS_TREE_VIEW (tree), FALSE);
    g_return_val_if_fail (rowid != NULL, FALSE);
    g_return_val_if_fail (visual != 0, FALSE);
    priv = tree->priv;

    if (G_UNLIKELY (!priv->is_tree))
    {
        g_set_error (error, DONNA_TREE_VIEW_ERROR, DONNA_TREE_VIEW_ERROR_OTHER,
                "TreeView '%s': set_visual() doesn't apply in mode list",
                priv->name);
        return FALSE;
    }

    type = convert_row_id_to_iter (tree, rowid, &iter);
    if (type != ROW_ID_ROW)
    {
        g_set_error (error, DONNA_TREE_VIEW_ERROR,
                DONNA_TREE_VIEW_ERROR_INVALID_ROW_ID,
                "TreeView '%s': Cannot set visual, invalid row-id",
                priv->name);
        return FALSE;
    }

    return set_tree_visual (tree, &iter, visual, value, error);
}

static gboolean
_set_visual_value (struct visuals   *visuals,
                   DonnaTreeVisual   visual,
                   const gchar      *value,
                   GError          **error)
{
    if (visual == DONNA_TREE_VISUAL_NAME)
    {
        g_free (visuals->name);
        visuals->name = g_strdup (value);
    }
    else if (visual == DONNA_TREE_VISUAL_ICON)
    {
        GIcon *icon;

        if (value)
        {
            if (*value == '/')
            {
                GFile *file;

                file = g_file_new_for_path (value);
                icon = g_file_icon_new (file);
                g_object_unref (file);
            }
            else
                icon = g_themed_icon_new (value);

            if (!icon)
            {
                g_set_error (error, DONNA_TREE_VIEW_ERROR,
                        DONNA_TREE_VIEW_ERROR_OTHER,
                        "Cannot set visual 'icon', "
                        "unable to get icon from '%s'",
                        value);
                return FALSE;
            }
        }
        else
            icon = NULL;

        if (visuals->icon)
            g_object_unref (visuals->icon);
        visuals->icon = icon;
    }
    else if (visual == DONNA_TREE_VISUAL_BOX)
    {
        g_free (visuals->box);
        visuals->box = g_strdup (value);
    }
    else if (visual == DONNA_TREE_VISUAL_HIGHLIGHT)
    {
        g_free (visuals->highlight);
        visuals->highlight = g_strdup (value);
    }
    else if (visual == DONNA_TREE_VISUAL_CLICK_MODE)
    {
        g_free (visuals->click_mode);
        visuals->click_mode = g_strdup (value);
    }
    else
    {
        g_set_error (error, DONNA_TREE_VIEW_ERROR,
                DONNA_TREE_VIEW_ERROR_OTHER,
                "Cannot set visual, invalid visual type");
        return FALSE;
    }

    return TRUE;
}

/**
 * donna_tree_view_root_set_child_visual:
 * @tree: A #DonnaTreeView
 * @rowid: Identifier of a row; See #rowid for more
 * @node: The node to get visual for
 * @visual: Which visual to set
 * @value: New value to set for @visual
 * @error: (allow-none): Return location of a #GError, or %NULL
 *
 * Set the value of the specified #tree-visuals.
 *
 * This is similar to donna_tree_view_set_visual() except that you give a root
 * and a node which must have a row as descendant of said root. The idea is that
 * usually (when giving a rowid) you can only set the visual of an actual row,
 * i.e. it won't work for any child which hasn't yet been loaded on tree, or
 * with an ansendant collapsed.
 *
 * Otherwise, see donna_tree_view_set_visual() for more, since it works very
 * much the same.
 *
 * Note that this is obviously only supported on trees.
 *
 * Returns: %TRUE on success, else %FALSE
 */
gboolean
donna_tree_view_root_set_child_visual (DonnaTreeView      *tree,
                                       DonnaRowId         *rowid,
                                       DonnaNode          *node,
                                       DonnaTreeVisual     visual,
                                       const gchar        *value,
                                       GError            **error)
{
    DonnaTreeViewPrivate *priv;
    GtkTreeIter  iter;
    row_id_type  type;
    GSList      *list;
    struct visuals *visuals;
    GSList *l;
    gchar *fl;

    g_return_val_if_fail (DONNA_IS_TREE_VIEW (tree), FALSE);
    g_return_val_if_fail (rowid != NULL, FALSE);
    g_return_val_if_fail (DONNA_IS_NODE (node), FALSE);
    g_return_val_if_fail (visual != 0, FALSE);
    priv = tree->priv;

    if (G_UNLIKELY (!priv->is_tree))
    {
        g_set_error (error, DONNA_TREE_VIEW_ERROR, DONNA_TREE_VIEW_ERROR_OTHER,
                "TreeView '%s': set_visual() doesn't apply in mode list",
                priv->name);
        return FALSE;
    }

    type = convert_row_id_to_root_iter (tree, rowid, &iter);
    if (type != ROW_ID_ROW)
    {
        g_set_error (error, DONNA_TREE_VIEW_ERROR,
                DONNA_TREE_VIEW_ERROR_INVALID_ROW_ID,
                "TreeView '%s': Cannot set visual, invalid root row-id",
                priv->name);
        return FALSE;
    }

    /* try to find a row (even non-accessible) for the given node */
    list = g_hash_table_lookup (priv->hashtable, node);
    if (list)
    {
        GtkTreePath *path_root;
        GtkTreePath *path_node;

        path_root = gtk_tree_model_get_path ((GtkTreeModel *) priv->store, &iter);
        for ( ; list; list = list->next)
        {
            path_node = gtk_tree_model_get_path ((GtkTreeModel *) priv->store, list->data);
            if (gtk_tree_path_is_descendant (path_node, path_root))
            {
                /* switch iter to the actual row we're getting the visual of */
                iter = * (GtkTreeIter *) list->data;

                gtk_tree_path_free (path_node);
                break;
            }
            gtk_tree_path_free (path_node);
        }
        gtk_tree_path_free (path_root);
    }
    if (list)
        /* found a row/an iter */
        return set_tree_visual (tree, &iter, visual, value, error);

    /* this is an unloaded visuals */

    fl = donna_node_get_full_location (node);
    if (!priv->tree_visuals)
    {
        priv->tree_visuals = g_hash_table_new_full (
                g_str_hash, g_str_equal, g_free, NULL);
        list = NULL;
    }
    else
        list = g_hash_table_lookup (priv->tree_visuals, fl);

    for (l = list; l ; l = l->next)
    {
        visuals = l->data;

        if (itereq (&iter, &visuals->root))
        {
            if (!_set_visual_value (visuals, visual, value, error))
            {
                g_prefix_error (error, "Treeview '%s': ", priv->name);
                g_free (fl);
                return FALSE;
            }

            g_free (fl);
            return TRUE;
        }
    }

    /* unsetting a value when there's none == noop */
    if (!value)
        return TRUE;

    /* add new visual */
    visuals = g_slice_new0 (struct visuals);
    visuals->root = iter;
    if (!_set_visual_value (visuals, visual, value, error))
    {
        g_prefix_error (error, "Treeview '%s': ", priv->name);
        g_free (fl);
        return FALSE;
    }

    list = g_slist_prepend (list, visuals);
    g_hash_table_insert (priv->tree_visuals, fl, list);

    return TRUE;
}

/**
 * donna_tree_view_get_visual:
 * @tree: A #DonnaTreeView
 * @rowid: Identifier of a row; See #rowid for more
 * @visual: Which visual to set
 * @source: From where to get the value of @visual
 * @error: (allow-none): Return location of a #GError, or %NULL
 *
 * Return the value of the specified visual.
 *
 * Trees support #tree-visuals, which can be applied either as tree-specific, or
 * via #node-visuals. @source determines where the value will come from. Using
 * %DONNA_TREE_VISUAL_SOURCE_ANY will return the tree-specific value if any,
 * else the value from the node if any, else an empty string.
 *
 * Note that when getting the value from the node, it will only return a value
 * if the visual is actually used/shown on @tree (via option
 * `node_visuals`).
 * IOW when e.g. custom icons are not shown as part of node visuals, an empty
 * string will be returned even if a custom icon is defined on the node as
 * visual, because it isn't used on @tree. If you're looking for the value of
 * the node visual regardless, see #node-visuals to see which property to get
 * value of from the node directly.
 *
 * Note that this is obviously only supported on trees.
 *
 * Returns: (transfer full): A newly-allocated string, value of the visual. Free
 * it using g_free() when done.
 */
gchar *
donna_tree_view_get_visual (DonnaTreeView           *tree,
                            DonnaRowId              *rowid,
                            DonnaTreeVisual          visual,
                            DonnaTreeVisualSource    source,
                            GError                 **error)
{
    DonnaTreeViewPrivate *priv;
    GtkTreeIter  iter;
    row_id_type  type;
    DonnaTreeVisual v;
    guint col;
    gchar *value;

    g_return_val_if_fail (DONNA_IS_TREE_VIEW (tree), NULL);
    g_return_val_if_fail (rowid != NULL, NULL);
    g_return_val_if_fail (visual != 0, NULL);
    g_return_val_if_fail (source == DONNA_TREE_VISUAL_SOURCE_TREE
            || source == DONNA_TREE_VISUAL_SOURCE_NODE
            || source == DONNA_TREE_VISUAL_SOURCE_ANY, NULL);
    priv = tree->priv;

    if (G_UNLIKELY (!priv->is_tree))
    {
        g_set_error (error, DONNA_TREE_VIEW_ERROR, DONNA_TREE_VIEW_ERROR_OTHER,
                "TreeView '%s': get_visual() doesn't apply in mode list",
                priv->name);
        return NULL;
    }

    type = convert_row_id_to_iter (tree, rowid, &iter);
    if (type != ROW_ID_ROW)
    {
        g_set_error (error, DONNA_TREE_VIEW_ERROR,
                DONNA_TREE_VIEW_ERROR_INVALID_ROW_ID,
                "TreeView '%s': Cannot get visual, invalid row-id",
                priv->name);
        return NULL;
    }

    if (visual == DONNA_TREE_VISUAL_NAME)
        col = TREE_COL_NAME;
    else if (visual == DONNA_TREE_VISUAL_ICON)
        col = TREE_COL_ICON;
    else if (visual == DONNA_TREE_VISUAL_BOX)
        col = TREE_COL_BOX;
    else if (visual == DONNA_TREE_VISUAL_HIGHLIGHT)
        col = TREE_COL_HIGHLIGHT;
    else if (visual == DONNA_TREE_VISUAL_CLICK_MODE)
        col = TREE_COL_CLICK_MODE;
    else
    {
        g_set_error (error, DONNA_TREE_VIEW_ERROR,
                DONNA_TREE_VIEW_ERROR_OTHER,
                "TreeView '%s': Cannot get visual, invalid visual type",
                priv->name);
        return NULL;
    }

    gtk_tree_model_get ((GtkTreeModel *) priv->store, &iter,
            TREE_COL_VISUALS,   &v,
            -1);

    if (source == DONNA_TREE_VISUAL_SOURCE_TREE)
    {
        if (!(v & visual))
            return g_strdup ("");
    }
    else if (source == DONNA_TREE_VISUAL_SOURCE_NODE)
    {
        if (v & visual)
            return g_strdup ("");
    }

    gtk_tree_model_get ((GtkTreeModel *) priv->store, &iter,
            col,    &value,
            -1);

    if (col == TREE_COL_ICON)
    {
        GIcon *icon = (GIcon *) value;

        value = g_icon_to_string (icon);
        g_object_unref (icon);
        if (G_UNLIKELY (!value || *value == '.'))
        {
            /* since a visual is a user-set icon, it should always be either
             * a /path/to/file.png or an icon-name, and never something like
             * ". GThemedIcon icon-name1 icon-name2" */
            g_free (value);
            g_set_error (error, DONNA_TREE_VIEW_ERROR,
                    DONNA_TREE_VIEW_ERROR_OTHER,
                    "TreeView '%s': Cannot return visual 'icon', "
                    "it doesn't have a straight-forward string value",
                    priv->name);
            return NULL;
        }
    }

    return (value) ? value : g_strdup ("");
}

/**
 * donna_tree_view_root_get_child_visual:
 * @tree: A #DonnaTreeView
 * @rowid: Identifier of a row; See #rowid for more
 * @node: The node to get visual for
 * @visual: Which visual to set
 * @source: From where to get the value of @visual
 * @error: (allow-none): Return location of a #GError, or %NULL
 *
 * Return the value of the specified visual.
 *
 * This is similar to donna_tree_view_get_visual() except that you give a root
 * and a node which must have a row as descendant of said root. The idea is that
 * usually (when giving a rowid) you can only get the visual of an actual row,
 * i.e. it won't work for any child which hasn't yet been loaded on tree, or
 * with an ansendant collapsed.
 *
 * Otherwise, see donna_tree_view_get_visual() for more, since it works very
 * much the same.
 *
 * Returns: (transfer full): A newly-allocated string, value of the visual. Free
 * it using g_free() when done.
 */
gchar *
donna_tree_view_root_get_child_visual (DonnaTreeView           *tree,
                                       DonnaRowId              *rowid,
                                       DonnaNode               *node,
                                       DonnaTreeVisual          visual,
                                       DonnaTreeVisualSource    source,
                                       GError                 **error)
{
    DonnaTreeViewPrivate *priv;
    GtkTreeIter  iter;
    row_id_type  type;
    GSList *list;
    DonnaTreeVisual v;
    guint col;
    const gchar *prop_name = NULL; /* silence warning */
    gchar *value;

    g_return_val_if_fail (DONNA_IS_TREE_VIEW (tree), NULL);
    g_return_val_if_fail (rowid != NULL, NULL);
    g_return_val_if_fail (DONNA_IS_NODE (node), NULL);
    g_return_val_if_fail (visual != 0, NULL);
    g_return_val_if_fail (source == DONNA_TREE_VISUAL_SOURCE_TREE
            || source == DONNA_TREE_VISUAL_SOURCE_NODE
            || source == DONNA_TREE_VISUAL_SOURCE_ANY, NULL);
    priv = tree->priv;

    if (G_UNLIKELY (!priv->is_tree))
    {
        g_set_error (error, DONNA_TREE_VIEW_ERROR, DONNA_TREE_VIEW_ERROR_OTHER,
                "TreeView '%s': root_get_child_visual() doesn't apply in mode list",
                priv->name);
        return NULL;
    }

    type = convert_row_id_to_root_iter (tree, rowid, &iter);
    if (type != ROW_ID_ROW)
    {
        g_set_error (error, DONNA_TREE_VIEW_ERROR,
                DONNA_TREE_VIEW_ERROR_INVALID_ROW_ID,
                "TreeView '%s': Cannot get visual, invalid root row-id",
                priv->name);
        return NULL;
    }

    if (visual == DONNA_TREE_VISUAL_NAME)
    {
        col = TREE_COL_NAME;
        prop_name = "visual-name";
    }
    else if (visual == DONNA_TREE_VISUAL_ICON)
    {
        col = TREE_COL_ICON;
        prop_name = "visual-icon";
    }
    else if (visual == DONNA_TREE_VISUAL_BOX)
    {
        col = TREE_COL_BOX;
        prop_name = "visual-box";
    }
    else if (visual == DONNA_TREE_VISUAL_HIGHLIGHT)
    {
        col = TREE_COL_HIGHLIGHT;
        prop_name = "visual-highlight";
    }
    else if (visual == DONNA_TREE_VISUAL_CLICK_MODE)
        col = TREE_COL_CLICK_MODE;
    else
    {
        g_set_error (error, DONNA_TREE_VIEW_ERROR,
                DONNA_TREE_VIEW_ERROR_OTHER,
                "TreeView '%s': Cannot get visual, invalid visual type",
                priv->name);
        return NULL;
    }

    /* try to find a row (even non-accessible) for the given node */
    list = g_hash_table_lookup (priv->hashtable, node);
    if (list)
    {
        GtkTreePath *path_root;
        GtkTreePath *path_node;

        path_root = gtk_tree_model_get_path ((GtkTreeModel *) priv->store, &iter);
        for ( ; list; list = list->next)
        {
            path_node = gtk_tree_model_get_path ((GtkTreeModel *) priv->store, list->data);
            if (gtk_tree_path_is_descendant (path_node, path_root))
            {
                /* switch iter to the actual row we're getting the visual of */
                iter = * (GtkTreeIter *) list->data;

                gtk_tree_path_free (path_node);
                break;
            }
            gtk_tree_path_free (path_node);
        }
        gtk_tree_path_free (path_root);
    }
    /* no row found (under root), let's check "unloaded" visuals */
    if (!list)
    {
        gchar *fl;

        if (!priv->tree_visuals)
            /* we don't check that node could be a descendant of rowid, because
             * we couldn't tell when provider is flat, and so we'd have to
             * assume either way, and it would work to possible inconsistent
             * behavior whether the child has a row in model or not.
             * Same goes when setting, we just add an "unloaded" visual and
             * assume the user knows what he's doing. (If not, no biggie. And to
             * clean it up, a simple editing of the treefile will do it.) */
            return g_strdup ("");

        fl = donna_node_get_full_location (node);
        list = g_hash_table_lookup (priv->tree_visuals, fl);
        g_free (fl);
        for ( ; list ; list = list->next)
        {
            struct visuals *visuals = list->data;

            if (itereq (&iter, &visuals->root))
            {
                GValue node_value = G_VALUE_INIT;
                DonnaNodeHasValue has;

                if (visual == DONNA_TREE_VISUAL_NAME)
                    value = visuals->name;
                else if (visual == DONNA_TREE_VISUAL_ICON)
                {
                    value = g_icon_to_string (visuals->icon);
                    if (G_UNLIKELY (!value || *value == '.'))
                    {
                        /* since a visual is a user-set icon, it should always be either
                         * a /path/to/file.png or an icon-name, and never something like
                         * ". GThemedIcon icon-name1 icon-name2" */
                        g_free (value);
                        g_set_error (error, DONNA_TREE_VIEW_ERROR,
                                DONNA_TREE_VIEW_ERROR_OTHER,
                                "TreeView '%s': Cannot return visual 'icon', "
                                "it doesn't have a straight-forward string value",
                                priv->name);
                        return NULL;
                    }
                }
                else if (visual == DONNA_TREE_VISUAL_BOX)
                    value = visuals->box;
                else if (visual == DONNA_TREE_VISUAL_HIGHLIGHT)
                    value = visuals->highlight;
                else /* DONNA_TREE_VISUAL_CLICK_MODE */
                    value = visuals->click_mode;

                if (source == DONNA_TREE_VISUAL_SOURCE_TREE)
                {
                    if (!value)
                        return g_strdup ("");
                    else if (visual == DONNA_TREE_VISUAL_ICON)
                        return value;
                    else
                        return g_strdup (value);
                }
                else if (source == DONNA_TREE_VISUAL_SOURCE_NODE)
                {
                    if (value)
                    {
                        if (visual == DONNA_TREE_VISUAL_ICON)
                            g_free (value);
                        return g_strdup ("");
                    }
                }
                else /* DONNA_TREE_VISUAL_SOURCE_ANY */
                {
                    if (value)
                    {
                        if (visual == DONNA_TREE_VISUAL_ICON)
                            return value;
                        else
                            return g_strdup (value);
                    }
                }
                /* return value from node */

                /* if we don't shown those node visuals (or it isn' one) then
                 * there's no value */
                if (!(priv->node_visuals & visual)
                        || visual == DONNA_TREE_VISUAL_CLICK_MODE)
                    return g_strdup ("");

                donna_node_get (node, TRUE, prop_name, &has, &node_value, NULL);
                if (has != DONNA_NODE_VALUE_SET)
                    return g_strdup ("");

                if (visual == DONNA_TREE_VISUAL_ICON)
                {
                    value = g_icon_to_string (g_value_get_boxed (&node_value));
                    if (G_UNLIKELY (!value || *value == '.'))
                    {
                        /* since a visual is a user-set icon, it should always be either
                         * a /path/to/file.png or an icon-name, and never something like
                         * ". GThemedIcon icon-name1 icon-name2" */
                        g_free (value);
                        g_set_error (error, DONNA_TREE_VIEW_ERROR,
                                DONNA_TREE_VIEW_ERROR_OTHER,
                                "TreeView '%s': Cannot return visual 'icon', "
                                "it doesn't have a straight-forward string value",
                                priv->name);
                        g_value_unset (&node_value);
                        return NULL;
                    }
                }
                else
                    value = g_value_dup_string (&node_value);

                g_value_unset (&node_value);
                return value;
            }
        }
        return g_strdup ("");
    }

    gtk_tree_model_get ((GtkTreeModel *) priv->store, &iter,
            TREE_COL_VISUALS,   &v,
            -1);

    if (source == DONNA_TREE_VISUAL_SOURCE_TREE)
    {
        if (!(v & visual))
            return g_strdup ("");
    }
    else if (source == DONNA_TREE_VISUAL_SOURCE_NODE)
    {
        if (v & visual)
            return g_strdup ("");
    }

    gtk_tree_model_get ((GtkTreeModel *) priv->store, &iter,
            col,    &value,
            -1);

    if (col == TREE_COL_ICON)
    {
        GIcon *icon = (GIcon *) value;

        value = g_icon_to_string (icon);
        g_object_unref (icon);
        if (G_UNLIKELY (!value || *value == '.'))
        {
            /* since a visual is a user-set icon, it should always be either
             * a /path/to/file.png or an icon-name, and never something like
             * ". GThemedIcon icon-name1 icon-name2" */
            g_free (value);
            g_set_error (error, DONNA_TREE_VIEW_ERROR,
                    DONNA_TREE_VIEW_ERROR_OTHER,
                    "TreeView '%s': Cannot return visual 'icon', "
                    "it doesn't have a straight-forward string value",
                    priv->name);
            return NULL;
        }
    }

    return (value) ? value : g_strdup ("");
}

struct re_data
{
    DonnaTreeView       *tree;
    GtkTreeViewColumn   *column;
    GtkTreeIter         *iter;
    GtkTreePath         *path;
};

enum
{
    INLINE_EDIT_DONE = 0,
    INLINE_EDIT_PREV,
    INLINE_EDIT_NEXT
};

struct inline_edit
{
    DonnaTreeView       *tree;
    GtkTreeViewColumn   *column;
    DonnaRow            *row;
    guint                move;
};

static gboolean
move_inline_edit (struct inline_edit *ie)
{
    DonnaRowId rid;
    struct column *_col;

    rid.type = DONNA_ARG_TYPE_ROW;
    rid.ptr  = ie->row;

    _col = get_column_by_column (ie->tree, ie->column);
    donna_tree_view_column_edit (ie->tree, &rid, _col->name, NULL);
    g_object_unref (ie->row->node);
    g_slice_free (DonnaRow, ie->row);
    g_slice_free (struct inline_edit, ie);
    return FALSE;
}

static void
editable_remove_widget_cb (GtkCellEditable *editable, struct inline_edit *ie)
{
    DonnaTreeViewPrivate *priv = ie->tree->priv;

    g_signal_handler_disconnect (editable,
            priv->renderer_editable_remove_widget_sid);
    priv->renderer_editable_remove_widget_sid = 0;
    priv->renderer_editable = NULL;
    if (ie->move != INLINE_EDIT_DONE)
    {
        DonnaRowId rid;
        row_id_type type;
        GtkTreeIter iter;

        /* we need to call move_inline_edit() via an idle source, because
         * otherwise the entry doesn't get properly destroyed, etc
         * But, we need to get the prev/next row right now, because after events
         * have been processed and since there might have been a property
         * update, the sort order could be affected and it would look odd that
         * e.g. pressing Up goes to the row above *after* the resort has been
         * triggered.
         * This is why we get the iter of the prev/next row now, and store it
         * (as a DonnaRow) for move_inline_edit() to use */

        rid.type = DONNA_ARG_TYPE_PATH;
        rid.ptr  = (gpointer) ((ie->move == INLINE_EDIT_PREV) ? ":prev" : ":next");
        type = convert_row_id_to_iter (ie->tree, &rid, &iter);
        if (type == ROW_ID_ROW)
        {
            GSList *list;

            ie->row = g_slice_new (DonnaRow);

            gtk_tree_model_get ((GtkTreeModel *) priv->store, &iter,
                    TREE_VIEW_COL_NODE, &ie->row->node,
                    -1);

            /* we want the iter from the hashtable */
            /* FIXME actually, we should have the iter inside the struct, and
             * make it a watched iter to be safe */
            if (priv->is_tree)
            {
                list = g_hash_table_lookup (priv->hashtable, ie->row->node);
                for ( ; list; list = list->next)
                    if (itereq (&iter, (GtkTreeIter *) list->data))
                    {
                        ie->row->iter = list->data;
                        break;
                    }
            }
            else
                ie->row->iter = g_hash_table_lookup (priv->hashtable, ie->row->node);

            /* we use a HIGH priority to avoid unneeded drawing & related
             * slowness. That is, with a line numbers column w/ related number
             * only if focused, we would see the treeview drawn as it gets the
             * focus (relative numbers) when the widget is destroyed, then again
             * as the new editing takes place (line numbers); which was not
             * really nice and slow.
             * With a HIGH priority we don't get those "glitches" and things are
             * much better. There might still be some little glitches/slowness,
             * but nothing unbearable. */
            g_idle_add_full (G_PRIORITY_HIGH_IDLE,
                    (GSourceFunc) move_inline_edit, ie, NULL);
            return;
        }
    }
    g_slice_free (struct inline_edit, ie);
}

static gboolean
kp_up_down_cb (GtkEntry *entry, GdkEventKey *event, struct inline_edit *ie)
{
    if (event->keyval == GDK_KEY_Up)
        ie->move = INLINE_EDIT_PREV;
    else if (event->keyval == GDK_KEY_Down)
        ie->move = INLINE_EDIT_NEXT;
    return FALSE;
}

static void
editing_started_cb (GtkCellRenderer     *renderer,
                    GtkCellEditable     *editable,
                    gchar               *path,
                    struct inline_edit  *ie)
{
    DonnaTreeViewPrivate *priv = ie->tree->priv;

    g_signal_handler_disconnect (renderer, priv->renderer_editing_started_sid);
    priv->renderer_editing_started_sid = 0;

    donna_app_ensure_focused (priv->app);

    if (GTK_IS_ENTRY (editable))
        /* handle using Up/Down to move the editing to the prev/next row */
        g_signal_connect (editable, "key-press-event",
                (GCallback) kp_up_down_cb, ie);

    /* in case we need to abort the editing */
    priv->renderer_editable = editable;
    /* when the editing will be done */
    priv->renderer_editable_remove_widget_sid = g_signal_connect (
            editable, "remove-widget",
            (GCallback) editable_remove_widget_cb, ie);
}

static gboolean
renderer_edit (GtkCellRenderer *renderer, struct re_data *data)
{
    DonnaTreeViewPrivate *priv = data->tree->priv;
    struct inline_edit *ie;
    GdkEventAny event = { .type = GDK_NOTHING };
    GdkRectangle cell_area;
    gint offset;
    gboolean ret;

    /* shouldn't happen, but to be safe */
    if (G_UNLIKELY (priv->renderer_editable))
        return FALSE;

    /* this is needed to set the renderer to our cell, since it might have been
     * used for another cell/row and that would cause confusion (e.g. wrong
     * text used in the entry, etc) */
    gtk_tree_view_column_cell_set_cell_data (data->column,
            (GtkTreeModel *) priv->store,
            data->iter, FALSE, FALSE);
    /* get the cell_area (i.e. where editable will be placed */
    gtk_tree_view_get_cell_area ((GtkTreeView *) data->tree,
            data->path, data->column, &cell_area);
    /* in case there are other renderers in that column */
    if (gtk_tree_view_column_cell_get_position (data->column, renderer,
            &offset, &cell_area.width))
        cell_area.x += offset;

    ie = g_slice_new (struct inline_edit);
    ie->tree = data->tree;
    ie->column = data->column;
    ie->move = INLINE_EDIT_DONE;

    /* so we can get the editable to be able to abort if needed */
    priv->renderer_editing_started_sid = g_signal_connect (
            renderer, "editing-started",
            (GCallback) editing_started_cb, ie);

    ret = gtk_cell_area_activate_cell (
            gtk_cell_layout_get_area ((GtkCellLayout *) data->column),
            (GtkWidget *) data->tree,
            renderer,
            (GdkEvent *) &event,
            &cell_area,
            0);

    if (G_UNLIKELY (!ret))
    {
        g_signal_handler_disconnect (renderer, priv->renderer_editing_started_sid);
        priv->renderer_editing_started_sid = 0;
        g_slice_free (struct inline_edit, ie);
    }

    return ret;
}

/**
 * donna_tree_view_column_edit:
 * @tree: A #DonnaTreeView
 * @rowid: Identifier of a row; See #rowid for more
 * @column: Name of the column
 * @error: (allow-none): Return location of a #GError, or %NULL
 *
 * Enable editing mode for @column on @rowid. Note that in case of success,
 * @rowid will automatically become the focused row.
 *
 * Valid values for @column are the column's full name, simply as many of the
 * first characters as needed to identify it, or a number to get the nth column
 * on @tree.
 *
 * How the editing is actually done (e.g. inline or via a new dialog) depends on
 * the columntype. (Some columntypes could also have different behavior
 * depending on whether there is a selection on @tree or not, etc)
 *
 * Returns: %TRUE on success, else %FALSE
 */
gboolean
donna_tree_view_column_edit (DonnaTreeView      *tree,
                             DonnaRowId         *rowid,
                             const gchar        *column,
                             GError            **error)
{
    DonnaTreeViewPrivate *priv;
    GtkTreeIter  iter;
    row_id_type  type;
    struct column *_col;
    DonnaNode *node;

    g_return_val_if_fail (DONNA_IS_TREE_VIEW (tree), FALSE);
    g_return_val_if_fail (rowid != NULL, FALSE);
    g_return_val_if_fail (column != NULL, FALSE);
    priv = tree->priv;

    _col = get_column_from_name (tree, column, error);
    if (!_col)
        return FALSE;

    type = convert_row_id_to_iter (tree, rowid, &iter);
    if (type != ROW_ID_ROW)
    {
        g_set_error (error, DONNA_TREE_VIEW_ERROR,
                DONNA_TREE_VIEW_ERROR_INVALID_ROW_ID,
                "TreeView '%s': Cannot edit column, invalid row-id",
                priv->name);
        return FALSE;
    }

    struct re_data re_data = {
        .tree   = tree,
        .column = _col->column,
        .iter   = &iter,
        .path   = gtk_tree_model_get_path ((GtkTreeModel *) priv->store, &iter),
    };

    gtk_tree_model_get ((GtkTreeModel *) priv->store, &iter,
            TREE_VIEW_COL_NODE, &node,
            -1);

#ifndef GTK_IS_JJK
    /* if not patched, a call to gtk_tree_view_set_focused_row() is actually a
     * wrapper around gtk_tree_view_set_cursor() which is a focus grabber, and
     * doing so would then cancel any inline editing that barely started. So to
     * avoid this, we need to do it prior */
    gtk_tree_view_set_focused_row ((GtkTreeView *) tree, re_data.path);
    check_statuses (tree, STATUS_CHANGED_ON_CONTENT);
#endif

    if (!donna_column_type_edit (_col->ct, _col->ct_data, node,
                (GtkCellRenderer **) _col->renderers->pdata,
                (renderer_edit_fn) renderer_edit, &re_data, tree, error))
    {
        g_object_unref (node);
        return FALSE;
    }

#ifdef GTK_IS_JJK
    gtk_tree_view_set_focused_row ((GtkTreeView *) tree, re_data.path);
    check_statuses (tree, STATUS_CHANGED_ON_CONTENT);
#endif

    gtk_tree_path_free (re_data.path);
    g_object_unref (node);
    return TRUE;
}

static gboolean
parse_option_value (DonnaConfig             *config,
                    DonnaColumnOptionInfo   *oi,
                    const gchar             *value,
                    const gchar            **s_val,
                    gint                    *val,
                    gboolean                *toggle,
                    GError                 **error)
{
    /* now get the value from its string representation (using extra) */
    if (oi->type == G_TYPE_STRING)
        *s_val = value;
    else if (oi->type == G_TYPE_BOOLEAN)
    {
        if (streq (value, "1") || streq (value, "true"))
            *val = TRUE;
        else if (streq (value, "0") || streq (value, "false"))
            *val = FALSE;
        else
        {
            g_set_error (error, DONNA_TREE_VIEW_ERROR,
                    DONNA_TREE_VIEW_ERROR_OTHER,
                    "Invalid value '%s' (must be '1', 'true', '0' or 'false')",
                    value);
            return FALSE;
        }
    }
    else if (!oi->extra) /* G_TYPE_INT */
    {
        *val = (gint) g_ascii_strtoll (value, (gchar **) s_val, 10);
        if (!*s_val || **s_val != '\0')
        {
            g_set_error (error, DONNA_TREE_VIEW_ERROR,
                    DONNA_TREE_VIEW_ERROR_OTHER,
                    "Invalid integer value: '%s'", value);
            return FALSE;
        }
    }

    if (oi->extra)
    {
        const DonnaConfigExtra *extra;

        extra = donna_config_get_extra (config, oi->extra, error);
        if (!extra)
        {
            g_prefix_error (error, "Unable to get definition of extra '%s': ",
                    oi->extra);
            return FALSE;
        }

        /* for FLAGS if it starts with a comma, it means toggle what's specified
         * from current value. Else it *is* the new value (allows to easily
         * toggle one flag from menus, etc) */
        if (extra->any.type == DONNA_CONFIG_EXTRA_TYPE_LIST_FLAGS && *value == ',')
        {
            *toggle = TRUE;
            ++value;
        }

        /* this gets the actual value (e.g. int) from the string representation,
         * based on the extra. Combines flags for comma-separated lists */
        if (!_donna_config_get_extra_value ((DonnaConfigExtra *) extra, value,
                    (oi->type == G_TYPE_STRING) ? (gpointer) s_val : (gpointer) val))
        {
            /* were we given the string of an actual number for an INT option? */
            if (oi->type == G_TYPE_INT && *value >= '0' && *value <= '9')
            {
                *val = (gint) g_ascii_strtoll (value, (gchar **) s_val, 10);
                if (!*s_val || **s_val != '\0')
                {
                    g_set_error (error, DONNA_TREE_VIEW_ERROR,
                            DONNA_TREE_VIEW_ERROR_OTHER,
                            "Invalid integer value '%s'", value);
                    return FALSE;
                }
                else
                {
                    GValue v = G_VALUE_INIT;

                    /* make sure the value is accepted by the extra */
                    g_value_init (&v, G_TYPE_INT);
                    g_value_set_int (&v, *val);
                    if (!donna_config_is_value_valid_for_extra (config, oi->extra,
                                &v, error))
                    {
                        g_value_unset (&v);
                        g_prefix_error (error, "Invalid value '%s' "
                                "(not matching allowed values from extra '%s'",
                                value, oi->extra);
                        return FALSE;
                    }
                    g_value_unset (&v);
                }
            }
            else
            {
                g_set_error (error, DONNA_TREE_VIEW_ERROR,
                        DONNA_TREE_VIEW_ERROR_OTHER,
                        "Invalid value '%s' (not in extra '%s')",
                        value, oi->extra);
                return FALSE;
            }
        }
    }

    return TRUE;
}

/**
 * donna_tree_view_column_set_option:
 * @tree: A #DonnaTreeView
 * @column: Name of the column
 * @option: Name of the option
 * @value: (allow-none): String representation of the value to set, or %NULL
 * @save_location: Where to save the option
 * @error: (allow-none): Return location of a #GError, or %NULL
 *
 * Set a new value derived from @value for the column option @option of column
 * @column, saving it in the location indicated by @save_location.
 * See #option-paths and #define-columns for more.
 *
 * Valid values for @column are the column's full name, simply as many of the
 * first characters as needed to identify it, or a number to get the nth column
 * on @tree.
 *
 * The same rules apply for @value as for treeview options, refer to
 * donna_tree_view_set_option() for more.
 *
 * Returns: %TRUE on success, else %FALSE
 */
gboolean
donna_tree_view_column_set_option (DonnaTreeView      *tree,
                                   const gchar        *column,
                                   const gchar        *option,
                                   const gchar        *value,
                                   DonnaTreeViewOptionSaveLocation save_location,
                                   GError            **error)
{
    GError *err = NULL;
    DonnaTreeViewPrivate *priv;
    struct column *_col;
    DonnaColumnTypeNeed need = DONNA_COLUMN_TYPE_NEED_NOTHING;
    DonnaColumnOptionInfo *oi;
    guint nb_options;
    const gchar *s_val;
    gint val;
    gboolean toggle = FALSE;

    g_return_val_if_fail (DONNA_IS_TREE_VIEW (tree), FALSE);
    g_return_val_if_fail (column != NULL, FALSE);
    g_return_val_if_fail (option != NULL, FALSE);
    g_return_val_if_fail (save_location == DONNA_TREE_VIEW_OPTION_SAVE_IN_MEMORY
            || save_location == DONNA_TREE_VIEW_OPTION_SAVE_IN_CURRENT
            || save_location == DONNA_TREE_VIEW_OPTION_SAVE_IN_ARRANGEMENT
            || save_location == DONNA_TREE_VIEW_OPTION_SAVE_IN_TREE
            || save_location == DONNA_TREE_VIEW_OPTION_SAVE_IN_MODE
            || save_location == DONNA_TREE_VIEW_OPTION_SAVE_IN_DEFAULT
            || save_location == DONNA_TREE_VIEW_OPTION_SAVE_IN_ASK
            || save_location == DONNA_TREE_VIEW_OPTION_SAVE_IN_SAVE_LOCATION, FALSE);
    priv = tree->priv;

    _col = get_column_from_name (tree, column, error);
    if (!_col)
        return FALSE;

    if (save_location == DONNA_TREE_VIEW_OPTION_SAVE_IN_SAVE_LOCATION)
        save_location = priv->default_save_location;

    if (streq (option, "title"))
    {
        const gchar *current = gtk_tree_view_column_get_title (_col->column);

        /* we "abuse" the fact that we are a columntype as well */
        if (!DONNA_COLUMN_TYPE_GET_INTERFACE (tree)->helper_set_option (_col->ct,
                    _col->name,
                    priv->arrangement->columns_options,
                    priv->name,
                    priv->is_tree,
                    NULL, /* no default */
                    (DonnaColumnOptionSaveLocation *) &save_location,
                    option,
                    G_TYPE_STRING,
                    &current,
                    (value) ? (gpointer) &value : &current,
                    &err))
            goto done;

        if (save_location != DONNA_TREE_VIEW_OPTION_SAVE_IN_MEMORY)
            return TRUE;

        if (value)
        {
            gtk_tree_view_column_set_title (_col->column, value);
            gtk_label_set_text ((GtkLabel *) _col->label, value);
        }
        return TRUE;
    }
    else if (streq (option, "width"))
    {
        gint current = gtk_tree_view_column_get_fixed_width (_col->column);
        gint new;

        if (value)
            new = (gint) g_ascii_strtoll (value, NULL, 10);
        else
            new = current;

        /* we "abuse" the fact that we are a columntype as well */
        if (!DONNA_COLUMN_TYPE_GET_INTERFACE (tree)->helper_set_option (_col->ct,
                    _col->name,
                    priv->arrangement->columns_options,
                    priv->name,
                    priv->is_tree,
                    NULL, /* no default */
                    (DonnaColumnOptionSaveLocation *) &save_location,
                    option,
                    G_TYPE_INT,
                    &current,
                    &new,
                    &err))
            goto done;

        if (save_location != DONNA_TREE_VIEW_OPTION_SAVE_IN_MEMORY)
            return TRUE;

        gtk_tree_view_column_set_fixed_width (_col->column, new);
        return TRUE;
    }
    else if (streq (option, "refresh_properties"))
    {
        enum rp current = _col->refresh_properties;
        enum rp new;

        if (value)
        {
            new = (guint) g_ascii_strtoll (value, NULL, 10);
            if (new >= _MAX_RP)
            {
                g_set_error (error, DONNA_TREE_VIEW_ERROR,
                        DONNA_TREE_VIEW_ERROR_OTHER,
                        "TreeView '%s': Cannot set option '%s' on column '%s': invalid value",
                        priv->name, option, _col->name);
                return FALSE;
            }
        }
        else
            new = current;

        /* we "abuse" the fact that we are a columntype as well */
        if (!DONNA_COLUMN_TYPE_GET_INTERFACE (tree)->helper_set_option (_col->ct,
                    _col->name,
                    priv->arrangement->columns_options,
                    priv->name,
                    priv->is_tree,
                    NULL, /* no default */
                    (DonnaColumnOptionSaveLocation *) &save_location,
                    option,
                    G_TYPE_INT,
                    &current,
                    &new,
                    &err))
            goto done;

        if (save_location != DONNA_TREE_VIEW_OPTION_SAVE_IN_MEMORY)
            return TRUE;

        _col->refresh_properties = new;
        return TRUE;
    }

    donna_column_type_get_options (_col->ct, &oi, &nb_options);
    for ( ; nb_options > 0; --nb_options, ++oi)
    {
        if (streq (option, oi->name))
            break;
    }

    if (nb_options == 0)
    {
        g_set_error (error, DONNA_TREE_VIEW_ERROR,
                DONNA_TREE_VIEW_ERROR_NOT_FOUND,
                "TreeView '%s': Cannot set option '%s' on column '%s': No such option",
                priv->name, option, _col->name);
        return FALSE;
    }

    if (value && !parse_option_value (donna_app_peek_config (priv->app), oi,
                value, &s_val, &val, &toggle, error))
    {
        g_prefix_error (error, "TreeView '%s': Cannot set option '%s' on column '%s': ",
                priv->name, option, _col->name);
        return FALSE;
    }

    need = donna_column_type_set_option (_col->ct, _col->name,
            priv->arrangement->columns_options,
            priv->name,
            priv->is_tree,
            _col->ct_data,
            option,
            (value)
            ? (oi->type == G_TYPE_STRING) ? (gpointer) &s_val : (gpointer) &val
            : NULL,
            toggle,
            save_location,
            &err);

    if (save_location == DONNA_TREE_VIEW_OPTION_SAVE_IN_MEMORY
            || save_location == DONNA_TREE_VIEW_OPTION_SAVE_IN_ASK)
        /* ASK might have not been IN_MEMORY and therefore gone through the
         * option_cb, but in case it was IN_MEMORY we should trigger the refresh
         * */
        refresh_col_props (tree);

done:
    if (err)
    {
        g_propagate_prefixed_error (error, err, "TreeView '%s': "
                "Failed to set option '%s' on column '%s': ",
                priv->name, option, column);
        return FALSE;
    }

    if (need & DONNA_COLUMN_TYPE_NEED_RESORT)
        resort_tree (tree);
    else if (need & DONNA_COLUMN_TYPE_NEED_REDRAW)
        gtk_widget_queue_draw ((GtkWidget *) tree);

    return TRUE;
}

/**
 * donna_tree_view_column_set_value:
 * @tree: A #DonnaTreeView
 * @rowid: Identifier of a row; See #rowid for more
 * @to_focused: When %TRUE rows affected will be the range from @rowid to the
 * focused row
 * @column: Name of the column
 * @value: String representation of the value to set
 * @rowid_ref: (allow-none): Identifier of the row used as reference, or %NULL
 * @error: (allow-none): Return location of a #GError, or %NULL
 *
 * Set a new value derived from @value for the property handled by @column on
 * the node(s) represented by @rowid
 *
 * See donna_tree_view_get_nodes() for more on which nodes will be affected.
 *
 * The node behind @rowid_ref will be given to the columntype as a "reference"
 * which can then be used. For example, it could be used to set the value of the
 * property on the node(s) behind @rowid to that of the one behind @rowid_ref
 *
 * If @column handles more than one property, it's up to the columntype to
 * determine which property to update. This might be defined via e.g. switches
 * in @value.
 *
 * Columntypes are expected, after validating @value (and converting it to the
 * actual value to set) to use donna_tree_view_set_node_property() for the
 * actual change to be applied, thus providing user with feedback if needed.
 *
 * Valid values for @column are the column's full name, simply as many of the
 * first characters as needed to identify it, or a number to get the nth column
 * on @tree.
 *
 * Returns: %TRUE on success, else %FALSE
 */
gboolean
donna_tree_view_column_set_value (DonnaTreeView      *tree,
                                  DonnaRowId         *rowid,
                                  gboolean            to_focused,
                                  const gchar        *column,
                                  const gchar        *value,
                                  DonnaRowId         *rowid_ref,
                                  GError            **error)
{
    DonnaTreeViewPrivate *priv;
    GPtrArray *nodes;
    struct column *_col;
    DonnaNode *node_ref;
    gboolean ret;

    g_return_val_if_fail (DONNA_IS_TREE_VIEW (tree), FALSE);
    g_return_val_if_fail (rowid != NULL, FALSE);
    g_return_val_if_fail (column != NULL, FALSE);
    priv = tree->priv;

    _col = get_column_from_name (tree, column, error);
    if (!_col)
        return FALSE;

    nodes = donna_tree_view_get_nodes (tree, rowid, to_focused, error);

    if (rowid_ref)
    {
        row_id_type type;
        GtkTreeIter iter;

        type = convert_row_id_to_iter (tree, rowid_ref, &iter);
        if (type != ROW_ID_ROW)
        {
            g_set_error (error, DONNA_TREE_VIEW_ERROR,
                    DONNA_TREE_VIEW_ERROR_INVALID_ROW_ID,
                    "TreeView '%s': Cannot set column value, invalid reference row-id",
                    priv->name);
            g_ptr_array_unref (nodes);
            return FALSE;
        }

        gtk_tree_model_get ((GtkTreeModel *) priv->store, &iter,
                TREE_VIEW_COL_NODE, &node_ref,
                -1);
    }
    else
        node_ref = NULL;

    ret = donna_column_type_set_value (_col->ct, _col->ct_data, nodes, value,
            node_ref, tree, error);
    if (!ret)
        g_prefix_error (error, "TreeView '%s': Failed to set column value: ",
                priv->name);

    g_ptr_array_unref (nodes);
    donna_g_object_unref (node_ref);
    return ret;
}

/**
 * donna_tree_view_set_option:
 * @tree: A #DonnaTreeView
 * @option: Name of the option
 * @value: (allow-none): String representation of the value to set, or %NULL
 * @save_location: Where to save the option
 * @error: (allow-none): Return location of a #GError, or %NULL
 *
 * Set a new value derived from @value for the tree option @option of @tree,
 * saving it in the location indicated by @save_location.
 * See #option-paths for more.
 *
 * For boolean options, accepted values are "0" or "false" for %FALSE, and "1"
 * or "true" for %TRUE.
 *
 * For interger options, a string of a number (e.g. "42") can be used. When the
 * option is of an extra type, it is also possible to use the infile values
 * (i.e. same as in the conf file).
 * Additionally, for FLAGS one can specify a comma-separated list of flags,
 * again as in in the conf file. It is also possible to prefix it with a comma,
 * to indicate to toggle the specified flags from the current value.
 *
 * So for example, using "icon,box" for option "node_visuals" would set it to
 * "icon,box" whereas using ",icon,box" would toggle those from the current
 * value. E.g. if current value was "box,highlight" it would then be set to
 * "icon,highlight"
 *
 * If @value is %NULL then the current (i.e. in memory) value will be used.
 *
 * Returns: %TRUE on success, else %FALSE
 */
gboolean
donna_tree_view_set_option (DonnaTreeView      *tree,
                            const gchar        *option,
                            const gchar        *value,
                            DonnaTreeViewOptionSaveLocation save_location,
                            GError            **error)
{
    DonnaTreeViewPrivate *priv;
    DonnaConfig *config;
    DonnaColumnOptionInfo *oi;
    guint i;
    gboolean toggle = FALSE;
    struct option_data od;
    guint from = 0;
    gint val;
    const gchar *s_val;
    /* there's a special case for string options when no value was given.
     * Because we'll set s_val to the current value, if then we use a save
     * location IN_MEMORY (direct or from IN_ASK) we would be usng our current
     * value as new value to set; And because it goes something like this:
     * g_free (current);
     * current = g_strdup (new);
     * Knowing that new == current; obviously we're using/dupping some free-d
     * memory...
     * So, to avoid this when using current value of a string option, we dup it
     * and use free_me to free it when done */
    gchar *free_me = NULL;
    gchar *loc;

    g_return_val_if_fail (DONNA_IS_TREE_VIEW (tree), FALSE);
    g_return_val_if_fail (option != NULL, FALSE);
    g_return_val_if_fail (save_location == DONNA_TREE_VIEW_OPTION_SAVE_IN_MEMORY
            || save_location == DONNA_TREE_VIEW_OPTION_SAVE_IN_CURRENT
            || save_location == DONNA_TREE_VIEW_OPTION_SAVE_IN_ARRANGEMENT
            || save_location == DONNA_TREE_VIEW_OPTION_SAVE_IN_TREE
            || save_location == DONNA_TREE_VIEW_OPTION_SAVE_IN_MODE
            || save_location == DONNA_TREE_VIEW_OPTION_SAVE_IN_DEFAULT
            || save_location == DONNA_TREE_VIEW_OPTION_SAVE_IN_ASK
            || save_location == DONNA_TREE_VIEW_OPTION_SAVE_IN_SAVE_LOCATION, FALSE);
    priv = tree->priv;
    config = donna_app_peek_config (priv->app);

    if (save_location == DONNA_TREE_VIEW_OPTION_SAVE_IN_ARRANGEMENT
            || save_location == DONNA_TREE_VIEW_OPTION_SAVE_IN_DEFAULT)
    {
        g_set_error (error, DONNA_TREE_VIEW_ERROR,
                DONNA_TREE_VIEW_ERROR_INCOMPATIBLE_OPTION,
                "TreeView '%s': Cannot use IN_ARRANGEMENT or IN_DEFAULT "
                "as save_location for tree view options", priv->name);
        return FALSE;
    }

    /* first make sure this is a known option. It will also give us its type &
     * extra (if any) */
    i = G_N_ELEMENTS (_tv_options);
    for (oi = _tv_options; i > 0; --i, ++oi)
        if (streq (option, oi->name))
        {
            if (!value)
            {
                switch (i)
                {
                    case 1:
                        val = priv->default_save_location;
                        break;
                    case 2:
                        s_val = free_me = g_strdup (priv->click_mode);
                        break;
                    case 3:
                        s_val = free_me = g_strdup (priv->key_mode);
                        break;
                    case 4:
                        val = priv->select_highlight;
                        break;
                    case 5:
                        val = priv->sort_groups;
                        break;
                    case 6:
                        val = priv->node_types;
                        break;
                    case 7:
                        val = priv->show_hidden;
                        break;
                    case 8:
                        val = priv->is_tree;
                        break;
                }
            }
            break;
        }
    if (i == 0)
    {
        if (priv->is_tree)
        {
            i = G_N_ELEMENTS (_tree_options);
            oi = _tree_options;
        }
        else
        {
            i = G_N_ELEMENTS (_list_options);
            oi = _list_options;
        }

        for ( ; i > 0; --i, ++oi)
            if (streq (option, oi->name))
            {
                if (!value)
                {
                    if (priv->is_tree)
                    {
                        switch (i)
                        {
                            case 1:
                                val = priv->auto_focus_sync;
                                break;
                            case 2:
                                val = priv->sync_scroll;
                                break;
                            case 3:
                                if (priv->sync_with)
                                {
                                    /* those aren't affected since we don't own
                                     * them, hence no free_me required here */
                                    if (priv->sid_active_list_changed)
                                        s_val = ":active";
                                    else
                                        s_val = priv->sync_with->priv->name;
                                }
                                else
                                    s_val = NULL;
                                break;
                            case 4:
                                val = priv->sync_mode;
                                break;
                            case 5:
                                val = priv->is_minitree;
                                break;
                            case 6:
                                val = priv->node_visuals;
                                break;
                        }
                    }
                    else
                    {
                        switch (i)
                        {
                            case 1:
                                val = (gint) donna_history_get_max (priv->history);
                                break;
                            case 2:
                                val = priv->goto_item_set;
                                break;
                            case 3:
                                val = priv->focusing_click;
                                break;
                            case 4:
                                val = priv->vf_items_only;
                                break;
                        }
                    }
                }
                break;
            }
    }
    if (i == 0)
    {
        g_set_error (error, DONNA_TREE_VIEW_ERROR,
                DONNA_TREE_VIEW_ERROR_NOT_FOUND,
                "TreeView '%s': Cannot set option '%s': No such option",
                priv->name, option);
        return FALSE;
    }

    if (value)
    {
        if (!parse_option_value (config, oi, value, &s_val, &val, &toggle, error))
        {
            g_prefix_error (error, "TreeView '%s': Cannot set option '%s': ",
                    priv->name, option);
            return FALSE;
        }

        if (toggle)
        {
            if (streq (option, "node_visuals"))
                val = (gint) (priv->node_visuals ^ (guint) val);
            else if (streq (option, "goto_item_set"))
                val = (gint) (priv->goto_item_set ^ (guint) val);
            else
            {
                g_set_error (error, DONNA_TREE_VIEW_ERROR,
                        DONNA_TREE_VIEW_ERROR_OTHER,
                        "TreeView '%s': Cannot set option '%s': "
                        "Internal error, toggle mode used for extra '%s' "
                        "on unknown LIST-FLAGS option",
                        priv->name, option, oi->extra);
                return FALSE;
            }
        }
    }

    od.tree = tree;
    od.option = (gchar *) option;
    od.opt = OPT_IN_MEMORY;

    if (save_location == DONNA_TREE_VIEW_OPTION_SAVE_IN_SAVE_LOCATION)
        save_location = priv->default_save_location;

    if (save_location == DONNA_TREE_VIEW_OPTION_SAVE_IN_CURRENT
            || save_location == DONNA_TREE_VIEW_OPTION_SAVE_IN_ASK)
    {
        if (oi->type == G_TYPE_INT)
        {
            if (donna_config_has_int (config, NULL, "tree_views/%s/%s",
                        priv->name, option))
                from = _DONNA_CONFIG_COLUMN_FROM_TREE;
            else
                from = _DONNA_CONFIG_COLUMN_FROM_MODE;
        }
        else if (oi->type == G_TYPE_BOOLEAN)
        {
            if (donna_config_has_boolean (config, NULL, "tree_views/%s/%s",
                        priv->name, option))
                from = _DONNA_CONFIG_COLUMN_FROM_TREE;
            else
                from = _DONNA_CONFIG_COLUMN_FROM_MODE;
        }
        else /* G_TYPE_STRING */
        {
            if (donna_config_has_string (config, NULL, "tree_views/%s/%s",
                        priv->name, option))
                from = _DONNA_CONFIG_COLUMN_FROM_TREE;
            else
                from = _DONNA_CONFIG_COLUMN_FROM_MODE;
        }
    }
    else if (save_location == DONNA_TREE_VIEW_OPTION_SAVE_IN_MEMORY)
    {
        if (oi->type == G_TYPE_STRING)
            od.val = &s_val;
        else
            od.val = &val;
        real_option_cb (&od);
        g_free (free_me);
        return TRUE;
    }

    if (save_location == DONNA_TREE_VIEW_OPTION_SAVE_IN_CURRENT)
    {
        if (from == _DONNA_CONFIG_COLUMN_FROM_TREE)
            save_location = DONNA_TREE_VIEW_OPTION_SAVE_IN_TREE;
        else /* _DONNA_CONFIG_COLUMN_FROM_MODE */
            save_location = DONNA_TREE_VIEW_OPTION_SAVE_IN_MODE;
    }
    else if (save_location == DONNA_TREE_VIEW_OPTION_SAVE_IN_ASK)
    {
        save_location = _donna_column_type_ask_save_location (priv->app, NULL,
                NULL, priv->name, priv->is_tree,
                (priv->is_tree) ? "trees" : "lists",
                option, from);
        if (save_location == (guint) -1)
        {
            /* user cancelled, not an error */
            g_free (free_me);
            return TRUE;
        }
    }

    if (save_location == DONNA_TREE_VIEW_OPTION_SAVE_IN_TREE)
        loc = g_strconcat ("tree_views/", priv->name, "/", option, NULL);
    else if (save_location == DONNA_TREE_VIEW_OPTION_SAVE_IN_MODE)
        loc = g_strconcat ("defaults/",
                (priv->is_tree) ? "trees/" : "lists/", option, NULL);
    else /* IN_MEMORY from IN_ASK */
    {
        if (oi->type == G_TYPE_STRING)
            od.val = &s_val;
        else
            od.val = &val;
        real_option_cb (&od);
        g_free (free_me);
        return TRUE;
    }

    if (oi->type == G_TYPE_INT)
    {
        if (!donna_config_set_int (config, error, val, loc))
        {
            g_prefix_error (error, "TreeView '%s': Failed to save option '%s': ",
                    priv->name, option);
            g_free (loc);
            return FALSE;
        }
    }
    else if (oi->type == G_TYPE_BOOLEAN)
    {
        if (!donna_config_set_boolean (config, error, val, loc))
        {
            g_prefix_error (error, "TreeView '%s': Failed to save option '%s'",
                    priv->name, option);
            g_free (loc);
            return FALSE;
        }
    }
    else if (oi->type == G_TYPE_STRING)
    {
        if (!donna_config_set_string (config, error, s_val, loc))
        {
            g_prefix_error (error, "TreeView '%s': Failed to save option '%s'",
                    priv->name, option);
            g_free (loc);
            g_free (free_me);
            return FALSE;
        }
        g_free (free_me);
    }
    g_free (loc);

    /* we don't "apply" anything, if if should be done it'll happen on the
     * option-set signal handler from config */

    return TRUE;
}

/**
 * donna_tree_view_move_root:
 * @tree: A #DonnaTreeView
 * @rowid: Identifier of a row; See #rowid for more
 * @move: Number indication how to move the row
 * @error: (allow-none): Return location of a #GError, or %NULL
 *
 * Moves @rowid (which must be a root) by @move. If @move is negative, the row
 * will move up, else it'll move down.
 *
 * Note that this is obviously only supported on trees.
 *
 * Returns: %TRUE on success, else %FALSE
 */
gboolean
donna_tree_view_move_root (DonnaTreeView     *tree,
                           DonnaRowId        *rowid,
                           gint               move,
                           GError           **error)
{
    DonnaTreeViewPrivate *priv;
    row_id_type type;
    GtkTreeIter iter;
    GSList *l, *prev, *ll;
    gint pos;

    g_return_val_if_fail (DONNA_IS_TREE_VIEW (tree), FALSE);
    g_return_val_if_fail (rowid != NULL, FALSE);
    priv = tree->priv;

    if (!priv->is_tree)
    {
        g_set_error (error, DONNA_TREE_VIEW_ERROR,
                DONNA_TREE_VIEW_ERROR_INVALID_MODE,
                "TreeView '%s': Cannot move rows in List mode",
                priv->name);
        return FALSE;
    }

    type = convert_row_id_to_iter (tree, rowid, &iter);
    if (type != ROW_ID_ROW)
    {
        g_set_error (error, DONNA_TREE_VIEW_ERROR,
                DONNA_TREE_VIEW_ERROR_INVALID_ROW_ID,
                "TreeView '%s': Cannot move row, invalid row-id",
                priv->name);
        return FALSE;
    }

    if (gtk_tree_store_iter_depth (priv->store, &iter) != 0)
    {
        g_set_error (error, DONNA_TREE_VIEW_ERROR,
                DONNA_TREE_VIEW_ERROR_INVALID_ROW_ID,
                "TreeView '%s': Cannot move row, not a root",
                priv->name);
        return FALSE;
    }

    prev = NULL;
    for (pos = 0, l = priv->roots; l; ++pos, prev = l, l = l->next)
        if (itereq ((GtkTreeIter *) l->data, &iter))
            break;
    if (G_UNLIKELY (!l))
    {
        g_warning ("TreeView '%s': Failed to find a root iter in list of roots",
                priv->name);
        g_set_error (error, DONNA_TREE_VIEW_ERROR,
                DONNA_TREE_VIEW_ERROR_OTHER,
                "TreeView '%s': Row not found in internal list of roots. This is a bug!",
                priv->name);
        return FALSE;
    }

    /* remove it from the list */
    if (prev)
        prev->next = l->next;
    else
        priv->roots = l->next;

    if (move < 0)
    {
        pos += move - 1;
        if (pos < 0)
        {
            /* new first element */
            l->next = priv->roots;
            priv->roots = l;
        }
        else
        {
            /* find element before where we need to insert l */
            for (ll = priv->roots; pos > 0; --pos, ll = ll->next)
                ;
            l->next  = ll->next;
            ll->next = l;
        }
    }
    else
    {
        /* -1 because we're removed l from the list */
        pos += move - 1;
        /* find element before where we need to insert l */
        for (ll = priv->roots; pos > 0; --pos, ll = ll->next)
            if (!ll->next)
                break;
        l->next  = ll->next;
        ll->next = l;
    }

    resort_tree (tree);
    return TRUE;
}

/* assumes ownership of str */
static gboolean
save_to_file (DonnaTreeView *tree,
              const gchar   *filename,
              GString       *str,
              GError       **error)
{
    gchar *file;

    if (*filename == '/')
    {
        if (!g_get_filename_charsets (NULL))
            file = g_filename_from_utf8 (filename, -1, NULL, NULL, NULL);
        else
            file = (gchar *) filename;
    }
    else
        file = donna_app_get_conf_filename (tree->priv->app, filename);

    if (!g_file_set_contents (file, str->str, (gssize) str->len, error))
    {
        g_prefix_error (error, "TreeView '%s': Failed to save to file '%s': ",
                tree->priv->name, filename);
        g_string_free (str, TRUE);
        if (file != filename)
            g_free (file);
        return FALSE;
    }

    g_string_free (str, TRUE);
    if (file != filename)
        g_free (file);

    g_info ("TreeView '%s': Saved to file '%s'", tree->priv->name, filename);

    return TRUE;
}

/**
 * donna_tree_view_save_list_file:
 * @tree: A #DonnaTreeView
 * @filename: Name of the file to save to
 * @elements: Which elements to include in the saved list file
 * @error: (allow-none): Return location of a #GError, or %NULL
 *
 * Saves the state of list @tree into a list file, so it can be loaded back
 * later using donna_tree_view_load_list_file()
 *
 * @filename can be either a full path to a file, or it will be processed
 * through donna_app_get_conf_filename()
 *
 * A list file is a simple text file with different information stored on each
 * line.
 *
 * 1: full location of the current location
 * 2: full location of the focused row
 * 3: sort orders, in the form: column:a,second_column:d
 * 4: scroll position (as a floating number)
 * 5: selection; one full location per line, for as many lines as selected rows
 *
 * Of all those, only the first line is required, everything else will depend on
 * @elements (Of course empty lines are then still required, since position/line
 * number is part of the format.)
 *
 * Usually you'll want to save every information you might need, since you can
 * decide which one(s) to load via donna_tree_view_load_list_file()
 *
 * Note that this is obviously only supported on lists. For trees, see
 * donna_tree_view_save_tree_file()
 *
 * Returns: %TRUE on success, else %FALSE
 */
gboolean
donna_tree_view_save_list_file (DonnaTreeView      *tree,
                                const gchar        *filename,
                                DonnaListFileElements elements,
                                GError            **error)
{
    DonnaTreeViewPrivate *priv;
    DonnaRowId rid = { DONNA_ARG_TYPE_PATH, NULL };
    GtkTreeModel *model;
    GtkTreeIter iter;
    DonnaNode *node;
    GString *str;
    gchar *s;

    g_return_val_if_fail (DONNA_IS_TREE_VIEW (tree), FALSE);
    g_return_val_if_fail (filename != NULL, FALSE);
    priv = tree->priv;

    if (priv->is_tree)
    {
        g_set_error (error, DONNA_TREE_VIEW_ERROR,
                DONNA_TREE_VIEW_ERROR_INVALID_MODE,
                "TreeView '%s': Cannot save list file in mode Tree",
                priv->name);
        return FALSE;
    }

    if (G_UNLIKELY (!priv->location))
    {
        g_set_error (error, DONNA_TREE_VIEW_ERROR,
                DONNA_TREE_VIEW_ERROR_NOT_FOUND,
                "TreeView '%s': Cannot save to file, no current location",
                priv->name);
        return FALSE;
    }

    model = (GtkTreeModel *) priv->store;

    /* 1. current location */
    s = donna_node_get_full_location (priv->location);
    str = g_string_new (s);
    g_string_append_c (str, '\n');
    g_free (s);

    /* 2. focused row */
    if (elements & DONNA_LIST_FILE_FOCUS)
    {
        rid.ptr = (gpointer) ":focused";
        if (convert_row_id_to_iter (tree, &rid, &iter) == ROW_ID_ROW)
        {
            gtk_tree_model_get (model, &iter,
                    TREE_VIEW_COL_NODE, &node,
                    -1);
            if (node)
            {
                s = donna_node_get_full_location (node);
                g_string_append (str, s);
                g_free (s);
                g_object_unref (node);
            }
        }
    }
    g_string_append_c (str, '\n');

    /* 3. sort */
    if (elements & DONNA_LIST_FILE_SORT)
    {
        struct column *_col;

        _col = get_column_by_column (tree, priv->sort_column);
        if (G_LIKELY (_col))
        {
            g_string_append (str, _col->name);
            g_string_append_c (str, ':');
            g_string_append_c (str,
                    (gtk_tree_view_column_get_sort_order (priv->sort_column)
                     == GTK_SORT_ASCENDING) ? 'a' : 'd');
        }

        _col = (priv->second_sort_column)
            ? get_column_by_column (tree, priv->second_sort_column) : NULL;
        if (_col)
        {
            g_string_append_c (str, ',');
            g_string_append (str, _col->name);
            g_string_append_c (str, ':');
            g_string_append_c (str, (priv->second_sort_order == GTK_SORT_ASCENDING)
                    ? 'a' : 'd');
        }
    }
    g_string_append_c (str, '\n');

    /* 4. scroll */
    if (elements & DONNA_LIST_FILE_SCROLL)
    {
        gdouble lower;
        gdouble upper;
        gdouble value;

        g_object_get (gtk_scrollable_get_vadjustment ((GtkScrollable *) tree),
                "lower", &lower, "upper", &upper, "value", &value, NULL);
        g_string_append_printf (str, "%f", value / (upper - lower));
    }
    g_string_append_c (str, '\n');

    /* 5. selection (if any) */
    if (elements & DONNA_LIST_FILE_SELECTION)
    {
        if (gtk_tree_model_iter_children (model, &iter, NULL))
        {
            GtkTreeSelection *sel;

            sel = gtk_tree_view_get_selection ((GtkTreeView *) tree);
            do
            {
                if (gtk_tree_selection_iter_is_selected (sel, &iter))
                {
                    gtk_tree_model_get (model, &iter,
                            TREE_VIEW_COL_NODE, &node,
                            -1);
                    if (node)
                    {
                        s = donna_node_get_full_location (node);
                        g_string_append (str, s);
                        g_string_append_c (str, '\n');
                        g_free (s);
                        g_object_unref (node);
                    }
                }
            } while (gtk_tree_model_iter_next (model, &iter));
        }
    }

    return save_to_file (tree, filename, str, error);
}

static gboolean
load_from_file (DonnaTreeView   *tree,
                const gchar     *filename,
                gchar          **data,
                GError         **error)
{
    DonnaTreeViewPrivate *priv = tree->priv;
    gchar *file;

    if (*filename == '/')
    {
        if (!g_get_filename_charsets (NULL))
            file = g_filename_from_utf8 (filename, -1, NULL, NULL, NULL);
        else
            file = (gchar *) filename;
    }
    else
        file = donna_app_get_conf_filename (priv->app, filename);

    if (!g_file_get_contents (file, data, NULL, error))
    {
        g_prefix_error (error, "TreeView '%s': Failed to load from file; "
                "Error reading '%s': ",
                priv->name, filename);
        if (file != filename)
            g_free (file);
        return FALSE;
    }
    if (file != filename)
        g_free (file);
    return TRUE;
}

struct load_list
{
    enum cl_extra type;
    change_location_callback_fn callback;
    gpointer data;
    GDestroyNotify destroy;

    gchar *content;
    DonnaListFileElements elements;
};

static void
free_load_list (struct load_list *ll)
{
    g_free (ll->content);
    g_free (ll);
}

static void
load_list (DonnaTreeView *tree, struct load_list *ll)
{
    GError *err = NULL;
    DonnaTreeViewPrivate *priv = tree->priv;
    DonnaNode *node;
    GString *errmsg = NULL;
    GtkTreeIter *iter;
    gchar *data;
    gchar *s;

    /* simple g_hash_table_lookup()s will be done since we're expecting to find
     * an iter (and if not, not in list or not visible is the same) */

    /* move past the location */
    data = strchr (ll->content, '\0') + 1;

    s = strchr (data, '\n');
    if (!s)
    {
        donna_app_show_error (priv->app, NULL,
                "TreeView '%s': Failed to finish loading list from file: "
                "Invalid data",
                priv->name);
        goto free;
    }
    if (ll->elements & DONNA_LIST_FILE_FOCUS)
    {
        GtkTreePath *path;

        *s = '\0';
        node = donna_app_get_node (priv->app, data, FALSE, &err);
        if (!node)
        {
            if (!errmsg)
                errmsg = g_string_new (NULL);
            g_string_append (errmsg, "- Failed to get node to focus: ");
            g_string_append (errmsg, (err) ? err->message : "(no error message)");
            g_string_append_c (errmsg, '\n');
            g_clear_error (&err);
            goto sort;
        }

        /* we don't need the node, only its address to find the iter (besides,
         * if we find it, then the tree view has a ref on it anyways) */
        g_object_unref (node);
        iter = g_hash_table_lookup (priv->hashtable, node);
        if (G_UNLIKELY (!iter))
        {
            if (!errmsg)
                errmsg = g_string_new (NULL);
            g_string_append_printf (errmsg, "- Failed to get node to focus: "
                    "'%s' not found in tree view", data);
            g_string_append_c (errmsg, '\n');
            g_clear_error (&err);
            goto sort;
        }

        path = gtk_tree_model_get_path ((GtkTreeModel *) priv->store, iter);
        gtk_tree_view_set_focused_row ((GtkTreeView *) tree, path);
        gtk_tree_path_free (path);
    }

sort:
    data = s + 1;
    s = strchr (data, '\n');
    if (!s)
    {
        if (!errmsg)
            errmsg = g_string_new (NULL);
        g_string_append_printf (errmsg, "- Failed to get sort order: "
                "Invalid data");
        g_string_append_c (errmsg, '\n');
        g_clear_error (&err);
        goto scroll;
    }
    if (ll->elements & DONNA_LIST_FILE_SORT)
    {
        struct column *_col;

        *s = '\0';

        s = strchr (data, ':');
        if (!s)
        {
            if (!errmsg)
                errmsg = g_string_new (NULL);
            g_string_append_printf (errmsg, "- Failed to get sort order: "
                    "Invalid data");
            g_string_append_c (errmsg, '\n');
            g_clear_error (&err);
            goto scroll;
        }
        *s = '\0';
        _col = get_column_by_name (tree, data);
        if (_col)
            set_sort_column (tree, _col->column,
                    (s[1] == 'd') ? DONNA_SORT_DESC : DONNA_SORT_ASC, FALSE);

        data = s + 2;
        s = strchr (data, ':');
        if (!s)
        {
            if (!errmsg)
                errmsg = g_string_new (NULL);
            g_string_append_printf (errmsg, "- Failed to get secondary sort order: "
                    "Invalid data");
            g_string_append_c (errmsg, '\n');
            g_clear_error (&err);
            goto scroll;
        }
        *s = '\0';
        _col = get_column_by_name (tree, data);
        if (_col)
            set_second_sort_column (tree, _col->column,
                    (s[1] == 'd') ? DONNA_SORT_DESC : DONNA_SORT_ASC, FALSE);
        s = strchr (data, '\0');
    }

scroll:
    data = s + 1;
    s = strchr (data, '\n');
    if (!s)
    {
        if (!errmsg)
            errmsg = g_string_new (NULL);
        g_string_append_printf (errmsg, "- Failed to get scroll position: "
                "Invalid data");
        g_string_append_c (errmsg, '\n');
        g_clear_error (&err);
        goto selection;
    }
    if (ll->elements & DONNA_LIST_FILE_SCROLL)
    {
        GtkAdjustment *adj;
        gdouble lower;
        gdouble upper;

        *s = '\0';

        adj = gtk_scrollable_get_vadjustment ((GtkScrollable *) tree);
        g_object_get (adj, "lower", &lower, "upper", &upper, NULL);
        g_object_set (adj, "value", g_strtod (data, NULL) * (upper - lower), NULL);
    }

selection:
    data = s + 1;
    if (ll->elements & DONNA_LIST_FILE_SELECTION)
    {
        GtkTreeSelection *sel;

        sel = gtk_tree_view_get_selection ((GtkTreeView *) tree);
        while ((s = strchr (data, '\n')))
        {
            *s = '\0';
            node = donna_app_get_node (priv->app, data, FALSE, &err);
            if (!node)
            {
                if (!errmsg)
                    errmsg = g_string_new (NULL);
                g_string_append (errmsg, "- Failed to get node to select: ");
                g_string_append (errmsg, (err) ? err->message : "(no error message)");
                g_string_append_c (errmsg, '\n');
                g_clear_error (&err);
                goto next;
            }

            /* we don't need the node, only its address to find the iter
             * (besides, if we find it, then the treeview has a ref on it
             * anyways) */
            g_object_unref (node);
            iter = g_hash_table_lookup (priv->hashtable, node);
            if (G_UNLIKELY (!iter))
            {
                if (!errmsg)
                    errmsg = g_string_new (NULL);
                g_string_append_printf (errmsg, "- Failed to get node to select: "
                        "'%s' not found in tree view", data);
                g_string_append_c (errmsg, '\n');
                g_clear_error (&err);
                goto next;
            }

            gtk_tree_selection_select_iter (sel, iter);
next:
            data = s + 1;
        }
    }

free:
    if (errmsg)
    {
        donna_app_show_error (priv->app, NULL,
                "TreeView '%s': Failed to finish loading list from file:\n\n%s",
                priv->name, errmsg->str);
        g_string_free (errmsg, TRUE);
    }
    free_load_list (ll);
}

/**
 * donna_tree_view_load_list_file:
 * @tree: A #DonnaTreeView
 * @filename: Name of the list file to load from
 * @elements: Which elements to load from the list file
 * @error: (allow-none): Return location of a #GError, or %NULL
 *
 * Loads the state of list @tree from list file @filename, usually a file
 * previously saved using donna_tree_view_save_list_file()
 *
 * @filename can be either a full path to a file, or it will be processed
 * through donna_app_get_conf_filename()
 *
 * For more information on the format of the file, see
 * donna_tree_view_save_list_file()
 *
 * Different information can be saved in a list file, of which only the current
 * location is always loaded. @elements allows to define which other information
 * will be loaded, assuming they were saved/are present in the file of course
 * (if not, this won't be an error).
 *
 * Much like donna_tree_view_set_location() this function returns %TRUE when the
 * change of location has been initiated, not when it has actually been done.
 * Meaning it might take a little while to happen (if e.g. the location is on a
 * slow device needing to be waked, etc) or even fail (though in case e.g. the
 * location doesn't exist, this function will fail).
 *
 * Note that this is obviously only supported on lists. For trees, see
 * donna_tree_view_load_tree_file()
 *
 * Returns: %TRUE on success, else %FALSE
 */
gboolean
donna_tree_view_load_list_file (DonnaTreeView      *tree,
                                const gchar        *filename,
                                DonnaListFileElements elements,
                                GError            **error)
{
    DonnaTreeViewPrivate *priv;
    struct load_list *ll;
    DonnaNode *node;
    gchar *data;
    gchar *s;
    gboolean ret;

    g_return_val_if_fail (DONNA_IS_TREE_VIEW (tree), FALSE);
    g_return_val_if_fail (filename != NULL, FALSE);
    priv = tree->priv;

    if (priv->is_tree)
    {
        g_set_error (error, DONNA_TREE_VIEW_ERROR,
                DONNA_TREE_VIEW_ERROR_INVALID_MODE,
                "TreeView '%s': Cannot load list file in mode Tree",
                priv->name);
        return FALSE;
    }

    if (!load_from_file (tree, filename, &data, error))
        return FALSE;

    s = strchr (data, '\n');
    if (!s)
    {
        g_set_error (error, DONNA_TREE_VIEW_ERROR,
                DONNA_TREE_VIEW_ERROR_OTHER,
                "TreeView '%s': Failed to load from file; "
                "Invalid data in '%s' (no current location)",
                priv->name, filename);
        g_free (data);
        return FALSE;
    }

    *s = '\0';
    node = donna_app_get_node (priv->app, data, FALSE, error);
    if (!node)
    {
        g_prefix_error (error, "TreeView '%s': Failed to load from file; "
                "Unable to get node of current location: ",
                priv->name);
        g_free (data);
        return FALSE;
    }

    if (elements == 0)
    {
        g_free (data);
        ll = NULL;
    }
    else
    {
        ll = g_new0 (struct load_list, 1);

        ll->type        = CL_EXTRA_CALLBACK;
        ll->callback    = (change_location_callback_fn) load_list;
        ll->data        = ll;
        ll->destroy     = (GDestroyNotify) free_load_list;

        ll->content     = data;
        ll->elements    = elements;
    }

    ret = change_location (tree, CHANGING_LOCATION_ASKED, node, ll, error);
    g_object_unref (node);

    if (ret)
        g_info ("TreeView '%s': Loaded from file '%s'", priv->name, filename);
    return ret;
}

static void
save_row (DonnaTreeView     *tree,
          GString           *str,
          GtkTreeIter       *iter,
          guint              level,
          gboolean           is_in_tree,
          DonnaTreeVisual    visuals)
{
    DonnaTreeViewPrivate *priv = tree->priv;
    DonnaNode *node;
    DonnaTreeVisual v;
    enum tree_expand es;
    gboolean expand_flag;
    GtkTreeModel *model = (GtkTreeModel *) priv->store;
    GtkTreeIter child;
    guint i;
    gboolean need_space = level > 0;
    gchar *s;

    gtk_tree_model_get (model, iter,
            TREE_COL_NODE,          &node,
            TREE_COL_VISUALS,       &v,
            TREE_COL_EXPAND_STATE,  &es,
            TREE_COL_EXPAND_FLAG,   &expand_flag,
            -1);

    /* fake node, nothing to do */
    if (!node)
        return;

    /* override is_in_tree if there are visuals, it's in partial/maxi expand,
     * or is expanded, as we need to include such rows even when they're
     * children on a non-partially expanded parent */
    is_in_tree = is_in_tree || expand_flag
        || es == TREE_EXPAND_PARTIAL || es == TREE_EXPAND_MAXI
        || (visuals & v);

    if (is_in_tree)
    {
        /* level indentation */
        for (i = level; i > 0; --i)
            g_string_append_c (str, '-');

        /* visuals */
        if ((visuals & DONNA_TREE_VISUAL_NAME) && (v & DONNA_TREE_VISUAL_NAME))
        {
            gtk_tree_model_get (model, iter, TREE_COL_NAME, &s, -1);
            if (need_space)
                g_string_append_c (str, ' ');
            g_string_append_c (str, '"');
            g_string_append (str, s);
            g_string_append_c (str, '"');
            g_free (s);
            need_space = TRUE;
        }
        if ((visuals & DONNA_TREE_VISUAL_ICON) && (v & DONNA_TREE_VISUAL_ICON))
        {
            GIcon *icon;

            gtk_tree_model_get (model, iter, TREE_COL_ICON, &icon, -1);
            s = g_icon_to_string (icon);
            if (G_LIKELY (s && *s != '.'))
            {
                if (need_space)
                    g_string_append_c (str, ' ');
                g_string_append_c (str, '@');
                g_string_append (str, s);
                g_string_append_c (str, '@');
                need_space = TRUE;
            }
            g_free (s);
            g_object_unref (icon);
        }
        if ((visuals & DONNA_TREE_VISUAL_BOX) && (v & DONNA_TREE_VISUAL_BOX))
        {
            gtk_tree_model_get (model, iter, TREE_COL_BOX, &s, -1);
            if (need_space)
                g_string_append_c (str, ' ');
            g_string_append_c (str, '{');
            g_string_append (str, s);
            g_string_append_c (str, '}');
            g_free (s);
            need_space = TRUE;
        }
        if ((visuals & DONNA_TREE_VISUAL_HIGHLIGHT) && (v & DONNA_TREE_VISUAL_HIGHLIGHT))
        {
            gtk_tree_model_get (model, iter, TREE_COL_HIGHLIGHT, &s, -1);
            if (need_space)
                g_string_append_c (str, ' ');
            g_string_append_c (str, '[');
            g_string_append (str, s);
            g_string_append_c (str, ']');
            g_free (s);
            need_space = TRUE;
        }
        if ((visuals & DONNA_TREE_VISUAL_CLICK_MODE) && (v & DONNA_TREE_VISUAL_CLICK_MODE))
        {
            gtk_tree_model_get (model, iter, TREE_COL_CLICK_MODE, &s, -1);
            if (need_space)
                g_string_append_c (str, ' ');
            g_string_append_c (str, '(');
            g_string_append (str, s);
            g_string_append_c (str, ')');
            g_free (s);
            need_space = TRUE;
        }

        /* current location, prefixed with some flags */
        if (need_space)
            g_string_append_c (str, ' ');
        /* flag current location */
        if (itereq (iter, &priv->location_iter))
            g_string_append_c (str, '!');
        /* flag expanded (else collapsed) */
        if (expand_flag)
            g_string_append_c (str, '<');
        /* flag expand state */
        if (es == TREE_EXPAND_PARTIAL)
            g_string_append_c (str, '+');
        else if (es == TREE_EXPAND_MAXI)
            g_string_append_c (str, '*');
        /* actual FL */
        s = donna_node_get_full_location (node);
        g_string_append (str, s);
        g_free (s);

        /* done */
        g_string_append_c (str, '\n');

        /* process children */
        if (gtk_tree_model_iter_children (model, &child, iter))
            do save_row (tree, str, &child, level + 1,
                    es == TREE_EXPAND_PARTIAL,
                    visuals);
            while (gtk_tree_model_iter_next (model, &child));
    }
    /* Note that there cannot be any children if !is_in_tree, since the only
     * ways for children to exists (PARTIAL/MAXI) are covered */

    g_object_unref (node);
}

/**
 * donna_tree_view_save_tree_file:
 * @tree: A #DonnaTreeView
 * @filename: Name of the file to save to
 * @visuals: Which #tree-visuals to include in the tree file
 * @error: (allow-none): Return location of a #GError, or %NULL
 *
 * Saves the tree @tree into a tree file, so it can be loaded back later using
 * donna_tree_view_load_tree_file()
 *
 * @filename can be either a full path to a file, or it will be processed
 * through donna_app_get_conf_filename()
 *
 * A tree file is a simple text file where every line represents a row, and is
 * formatted as such:
 * `&lt;LEVEL-INDICATOR&gt;&lt;TREE-VISUALS&gt;&lt;FLAGS&gt;&lt;FULL-LOCATION&gt;`;
 * Where:
 *
 * - `LEVEL-INDICATOR` is nothing for roots, or as many dash as needed to
 *   represent the row's level, followed by a space. So children of a root will
 *   have "- " as indicator, while their children have "-- " and so on.
 * - `FLAGS` can be one or more of the flags described below
 * - `TREE-VISUALS` are the row's tree visuals (if any) as set in @visuals,
 *   which also includes custom click_mode. See below for syntax.
 *
 * Supported flags are:
 *
 * - Star (`*`) to indicate the row was in maxi expand state (i.e. all children
 *   were loaded; doesn't mean it was expanded)
 * - Plus sign (`+`) to indicate the row was in partial expand state (i.e. only
 *   some children were loaded; doesn't indicate it was expanded). See #minitree
 *   for more.
 * - Less than sign (`&lt;`) to indicate the row was expanded
 * - Exclamation point (`!`) to indicate the current location
 *
 * Any tree visual is saved as a string enclosed with signs indicating which
 * tree visual it is, followed by a space:
 *
 * - Custom names are put in between quotes (`"..."`)
 * - Custom icons are put in between at-sign (`@...@`). As usual this will be
 *   the full path/name of the file to use, or the name of an icon to load from
 *   the theme
 * - Box class names are put in between brackets (`{...}`)
 * - Highlight class names are put in between square brackets (`[...]`)
 * - Custom click_mode names are put in between parenthesis (`(...)`)
 *
 * Lastly, a tree can have tree visuals in memory that aren't (yet) applied,
 * because the row isn't loaded on tree (e.g. a parent hasn't been expanded
 * yet), in which case such tree visuals will be features on lines using the
 * same syntax, only with the equal sign (`=`) as prefix instead of level
 * indicator.
 * Such tree visuals will then belong in/be linked to the last defined root.
 *
 * Usually you'll want to save every information you might need, since you can
 * decide which one(s) to load via donna_tree_view_load_tree_file()
 *
 * Note that this is obviously only supported on trees. For lists, see
 * donna_tree_view_save_list_file()
 *
 * Returns: %TRUE on success, else %FALSE
 */
gboolean
donna_tree_view_save_tree_file (DonnaTreeView      *tree,
                                const gchar        *filename,
                                DonnaTreeVisual     visuals,
                                GError            **error)
{
    DonnaTreeViewPrivate *priv;
    GtkTreeModel *model;
    GtkTreeIter iter;
    GString *str;

    g_return_val_if_fail (DONNA_IS_TREE_VIEW (tree), FALSE);
    g_return_val_if_fail (filename != NULL, FALSE);
    priv = tree->priv;
    model = (GtkTreeModel *) priv->store;

    if (!priv->is_tree)
    {
        g_set_error (error, DONNA_TREE_VIEW_ERROR,
                DONNA_TREE_VIEW_ERROR_INVALID_MODE,
                "TreeView '%s': Cannot save tree file in mode List",
                priv->name);
        return FALSE;
    }

    if (G_UNLIKELY (!gtk_tree_model_iter_children (model, &iter, NULL)))
    {
        g_set_error (error, DONNA_TREE_VIEW_ERROR,
                DONNA_TREE_VIEW_ERROR_NOT_FOUND,
                "TreeView '%s': Cannot save to file, nothing in tree",
                priv->name);
        return FALSE;
    }

    str = g_string_new (NULL);

    do
    {
        /* export the root, and all its children (recursively) */
        save_row (tree, str, &iter, 0, TRUE, visuals);
        /* export visuals not (yet) loaded */
        if (priv->tree_visuals)
        {
            GHashTableIter it;
            const gchar *fl;
            GSList *l;

            g_hash_table_iter_init (&it, priv->tree_visuals);
            while (g_hash_table_iter_next (&it, (gpointer) &fl, (gpointer) &l))
            {
                for ( ; l; l = l->next)
                {
                    struct visuals *visual = l->data;
                    gboolean added = FALSE;

                    if (!itereq (&visual->root, &iter))
                        continue;

                    if ((visuals & DONNA_TREE_VISUAL_NAME) && visual->name)
                    {
                        if (!added)
                        {
                            g_string_append_c (str, '=');
                            added = TRUE;
                        }
                        g_string_append_c (str, ' ');
                        g_string_append_c (str, '"');
                        g_string_append (str, visual->name);
                        g_string_append_c (str, '"');
                    }

                    if ((visuals & DONNA_TREE_VISUAL_ICON) && visual->icon)
                    {
                        gchar *s = g_icon_to_string (visual->icon);
                        if (G_LIKELY (s && *s != '.'))
                        {
                            /* since a visual is a user-set icon, it should
                             * always be either a /path/to/file.png or an
                             * icon-name. In the off chance it's not, e.g. a
                             * ". GThemedIcon icon-name1 icon-name2" kinda
                             * string, we just ignore it. */
                            if (!added)
                            {
                                g_string_append_c (str, '=');
                                added = TRUE;
                            }
                            g_string_append_c (str, ' ');
                            g_string_append_c (str, '@');
                            g_string_append (str, s);
                            g_string_append_c (str, '@');
                        }
                        g_free (s);
                    }

                    if ((visuals & DONNA_TREE_VISUAL_BOX) && visual->box)
                    {
                        if (!added)
                        {
                            g_string_append_c (str, '=');
                            added = TRUE;
                        }
                        g_string_append_c (str, ' ');
                        g_string_append_c (str, '{');
                        g_string_append (str, visual->box);
                        g_string_append_c (str, '}');
                    }

                    if ((visuals & DONNA_TREE_VISUAL_HIGHLIGHT) && visual->highlight)
                    {
                        if (!added)
                        {
                            g_string_append_c (str, '=');
                            added = TRUE;
                        }
                        g_string_append_c (str, ' ');
                        g_string_append_c (str, '[');
                        g_string_append (str, visual->highlight);
                        g_string_append_c (str, ']');
                    }

                    if ((visuals & DONNA_TREE_VISUAL_CLICK_MODE) && visual->click_mode)
                    {
                        if (!added)
                        {
                            g_string_append_c (str, '=');
                            added = TRUE;
                        }
                        g_string_append_c (str, ' ');
                        g_string_append_c (str, '(');
                        g_string_append (str, visual->click_mode);
                        g_string_append_c (str, ')');
                    }

                    if (added)
                    {
                        g_string_append_c (str, ' ');
                        g_string_append (str, fl);
                        g_string_append_c (str, '\n');
                    }
                }
            }
        }
    }
    while (gtk_tree_model_iter_next (model, &iter));

    return save_to_file (tree, filename, str, error);
}

/**
 * donna_tree_view_load_tree_file:
 * @tree: A #DonnaTreeView
 * @filename: Name of the tree file to load from
 * @visuals: Which #tree-visuals to load from the tree file
 * @error: (allow-none): Return location of a #GError, or %NULL
 *
 * Loads the content of @tree from tree file @filename, usually a file
 * previously saved using donna_tree_view_save_tree_file()
 *
 * @filename can be either a full path to a file, or it will be processed
 * through donna_app_get_conf_filename()
 *
 * For more information on the format of the file, see
 * donna_tree_view_save_tree_file()
 *
 * Tree files can include #tree-visuals, only those specified in @visuals will
 * be loaded into @tree.
 *
 * Note that this is obviously only supported on trees. For lists, see
 * donna_tree_view_load_list_file()
 *
 * Returns: %TRUE on success, else %FALSE
 */
gboolean
donna_tree_view_load_tree_file (DonnaTreeView      *tree,
                                const gchar        *filename,
                                DonnaTreeVisual     visuals,
                                GError            **error)
{
    DonnaTreeViewPrivate *priv;
    GtkTreeModel *model;
    GtkTreeIter *root;
    GtkTreeIter it;
    gchar *data;
    gchar *e;
    gchar *s;
    GArray *array;
    gint last_level = -1;

    g_return_val_if_fail (DONNA_IS_TREE_VIEW (tree), FALSE);
    g_return_val_if_fail (filename != NULL, FALSE);
    priv = tree->priv;
    model = (GtkTreeModel *) priv->store;

    if (!priv->is_tree)
    {
        g_set_error (error, DONNA_TREE_VIEW_ERROR,
                DONNA_TREE_VIEW_ERROR_INVALID_MODE,
                "TreeView '%s': Cannot load tree file in mode List",
                priv->name);
        return FALSE;
    }

    if (!load_from_file (tree, filename, &data, error))
        return FALSE;

    e = strchr (data, '\n');
    if (!e)
    {
        g_set_error (error, DONNA_TREE_VIEW_ERROR,
                DONNA_TREE_VIEW_ERROR_OTHER,
                "TreeView '%s': Failed to load from file; "
                "Invalid data in '%s'",
                priv->name, filename);
        g_free (data);
        return FALSE;
    }

    /* first off, let's clear the tree. We get the current root iter to make
     * sure we remove the current branch (if any) last, to avoid having the
     * location be changed for no reason (which would affect sync_with, and also
     * therefore (via sync) could lead to the root being re-added. */
    if (priv->location_iter.stamp != 0)
        root = get_root_iter (tree, &priv->location_iter);
    else
        root = NULL;

    if (gtk_tree_model_iter_children (model, &it, NULL))
        for (;;)
        {
            if (root && itereq (&it, root))
            {
                if (!gtk_tree_model_iter_next (model, &it))
                    break;
            }
            else
            {
                if (!remove_row_from_tree (tree, &it, RR_NOT_REMOVAL))
                    break;
            }
        }
    if (root)
    {
        it = *root; /* we must own the iter */
        remove_row_from_tree (tree, &it, RR_NOT_REMOVAL);
    }

#define load_visual(c_open, c_close, UPPER, var)    \
    if (*s == c_open)                               \
    {                                               \
        gchar *d;                                   \
        d = s + 1;                                  \
        s = strchr (d, c_close);                    \
        if (!s)                                     \
        {                                           \
        }                                           \
        if (visuals & DONNA_TREE_VISUAL_##UPPER)    \
        {                                           \
            *s = '\0';                              \
            var = d;                                \
        }                                           \
        if (*++s == ' ')                            \
            ++s;                                    \
    }

    /* if the tree was fresh, we might need to load an arrangement */
    if (!priv->arrangement)
        donna_tree_view_build_arrangement (tree, FALSE);

    array = g_array_sized_new (FALSE, TRUE, sizeof (GtkTreeIter), 8);
    /* so we can actually use those directly */
    g_array_set_size (array, 8);

    s = data;
    do
    {
        GError *err = NULL;
        GtkTreeIter parent;
        DonnaNode *node;
        gint level          = 0;
        gchar *name         = NULL;
        gchar *icon         = NULL;
        gchar *box          = NULL;
        gchar *highlight    = NULL;
        gchar *click_mode   = NULL;
        gboolean is_in_tree;
        gboolean is_future_location = FALSE;
        gboolean expand = FALSE;
        enum tree_expand es = TREE_EXPAND_UNKNOWN;

        *e = '\0';

        /* visuals only? */
        if (*s == '=')
        {
            is_in_tree = FALSE;
            ++s;
            if (*s == ' ')
                ++s;
        }
        else
        {
            is_in_tree = TRUE;
            /* get the level */
            for ( ; *s == '-'; ++s, ++level)
                ;
            if (level > 0 && *s == ' ')
                ++s;
        }

        /* visuals */
        load_visual ('"', '"', NAME, name)
        load_visual ('@', '@', ICON, icon)
        load_visual ('{', '}', BOX, box)
        load_visual ('[', ']', HIGHLIGHT, highlight)
        load_visual ('(', ')', CLICK_MODE, click_mode)

        /* flags */
        if (*s == '!')
        {
            is_future_location = TRUE;
            ++s;
        }
        if (*s == '<')
        {
            expand = TRUE;
            ++s;
        }
        if (*s == '+')
        {
            es = TREE_EXPAND_PARTIAL;
            ++s;
        }
        else if (*s == '*')
        {
            es = TREE_EXPAND_MAXI;
            ++s;
        }

        /* last_level was same/deeper down */
        if (last_level >= level && last_level >= 0)
        {
            GtkTreeIter *iter;
            iter = &g_array_index (array, GtkTreeIter, level);
            iter->stamp = 0;
        }

        /* make sure we want to add it */
        if (is_in_tree && !priv->show_hidden)
        {

            /* get node */
            node = donna_app_get_node (priv->app, s, FALSE, &err);
            if (!node)
            {
                g_warning ("TreeView '%s': Failed to get node for '%s': %s",
                        priv->name, s, (err) ? err->message : "(no error message)");
                is_in_tree = FALSE;
            }
            else
            {
                gchar *s_name = donna_node_get_name (node);
                if (s_name && *s_name == '.')
                    is_in_tree = FALSE;
                g_free (s_name);
            }
        }
        else
            node = NULL;

        if (is_in_tree)
        {
            GtkTreeIter *iter;

            if (!node)
            {
                node = donna_app_get_node (priv->app, s, FALSE, &err);
                if (!node)
                {
                    g_warning ("TreeView '%s': Failed to get node for '%s': %s",
                            priv->name, s, (err) ? err->message : "(no error message)");
                    goto next;
                }
            }

            /* get parent iter */
            if (level > 0)
            {
                iter = &g_array_index (array, GtkTreeIter, level - 1);
                parent = *iter;
            }
            else
            {
                iter = NULL;
                parent.stamp = 0;
            }

            /* add to tree */
            add_node_to_tree (tree, (parent.stamp != 0) ? &parent : NULL, node, &it);

            /* set up the iter for this level (so we can add children) */
            if ((guint) level >= array->len)
                g_array_set_size (array, (guint) level + 2);
            iter = &g_array_index (array, GtkTreeIter, level);
            *iter = it;
            last_level = level;

            /* set visuals */
            if (name)
                set_tree_visual (tree, &it,
                        DONNA_TREE_VISUAL_NAME, name, NULL);
            if (icon)
                set_tree_visual (tree, &it,
                        DONNA_TREE_VISUAL_ICON, icon, NULL);
            if (box)
                set_tree_visual (tree, &it,
                        DONNA_TREE_VISUAL_BOX, box, NULL);
            if (highlight)
                set_tree_visual (tree, &it,
                        DONNA_TREE_VISUAL_HIGHLIGHT, highlight, NULL);
            if (click_mode)
                set_tree_visual (tree, &it,
                        DONNA_TREE_VISUAL_CLICK_MODE, click_mode, NULL);

            if (es == TREE_EXPAND_PARTIAL && priv->is_minitree)
                set_es (priv, &it, es);
            else if (es == TREE_EXPAND_MAXI && !expand)
                /* only get the children & load them, no expansion */
                expand_row (tree, &it, FALSE, FALSE, NULL);

            if (expand)
            {
                GtkTreePath *path;

                path = gtk_tree_model_get_path ((GtkTreeModel*) priv->store, &it);
                gtk_tree_view_expand_row ((GtkTreeView *) tree, path, FALSE);
                if (is_future_location)
                    gtk_tree_view_set_focused_row ((GtkTreeView *) tree, path);
                gtk_tree_path_free (path);
            }

            if (is_future_location)
            {
                if (!expand)
                {
                    GtkTreePath *path;

                    path = gtk_tree_model_get_path ((GtkTreeModel*) priv->store, &it);
                    gtk_tree_view_set_focused_row ((GtkTreeView *) tree, path);
                    gtk_tree_path_free (path);
                }
                gtk_tree_selection_select_iter (
                        gtk_tree_view_get_selection ((GtkTreeView *) tree),
                        &it);
            }
        }
        /* add visuals for non-loaded row */
        else if (name || icon || box || highlight || click_mode)
        {
            GSList *l = NULL;
            struct visuals *visual;

            /* get current root */
            if (!gtk_tree_model_iter_nth_child (model, &it, NULL,
                        gtk_tree_model_iter_n_children (model, NULL)))
            {
            }

            visual = g_slice_new0 (struct visuals);
            visual->root = it;
            visual->name = g_strdup (name);
            if (icon)
            {
                if (*icon == '/')
                {
                    GFile *file;

                    file = g_file_new_for_path (icon);
                    visual->icon = g_file_icon_new (file);
                    g_object_unref (file);
                }
                else
                    visual->icon = g_themed_icon_new (icon);
            }
            visual->box = g_strdup (box);
            visual->highlight = g_strdup (highlight);
            visual->click_mode = g_strdup (click_mode);

            if (priv->tree_visuals)
                l = g_hash_table_lookup (priv->tree_visuals, s);
            else
                priv->tree_visuals = g_hash_table_new_full (
                        g_str_hash, g_str_equal,
                        g_free, NULL);

            l = g_slist_prepend (l, visual);
            g_hash_table_insert (priv->tree_visuals, g_strdup (s), l);
        }

        donna_g_object_unref (node);

next:
        s = e + 1;
    } while ((e = strchr (s, '\n')));

#undef load_visual

    g_array_free (array, TRUE);
    g_free (data);

    g_info ("TreeView '%s': Loaded from file '%s'", priv->name, filename);

    return TRUE;
}

/**
 * donna_tree_view_toggle_column:
 * @tree: A #DonnaTreeView
 * @column: Name of the column
 * @error: (allow-none): Return location of a #GError, or %NULL
 *
 * Toggle column @column
 *
 * Valid values for @column are the column's full name, simply as many of the
 * first characters as needed to identify it, or a number to get the nth column
 * on @tree.
 *
 * Returns: %TRUE on success, else %FALSE
 */
gboolean
donna_tree_view_toggle_column (DonnaTreeView      *tree,
                               const gchar        *column,
                               GError            **error)
{
    DonnaTreeViewPrivate *priv;
    DonnaArrangement arr;
    struct column *_col;
    GString *str;
    GList *list, *l;

    g_return_val_if_fail (DONNA_IS_TREE_VIEW (tree), FALSE);
    g_return_val_if_fail (column != NULL, FALSE);
    priv = tree->priv;

    _col = get_column_from_name (tree, column, NULL);
    if (_col)
    {
        /* toggle off -- for sanity reason, let's not allow to remove the
         * last/only column */
        if ((struct column *) priv->columns->data == _col && !priv->columns->next)
        {
            g_set_error (error, DONNA_TREE_VIEW_ERROR,
                    DONNA_TREE_VIEW_ERROR_OTHER,
                    "TreeView '%s': Cannot remove the only column in tree view",
                    priv->name);
            return FALSE;
        }
    }

    if (G_UNLIKELY (!priv->arrangement) || !priv->arrangement->columns)
    {
        g_set_error (error, DONNA_TREE_VIEW_ERROR,
                DONNA_TREE_VIEW_ERROR_OTHER,
                "TreeView '%s': Internal error: no arrangement/columns set",
                priv->name);
        return FALSE;
    }

    str = g_string_new (NULL);
    /* get them from treeview to preserve the current order */
    list = gtk_tree_view_get_columns ((GtkTreeView *) tree);
    for (l = list; l; l = l->next)
    {
        struct column *_c = get_column_by_column (tree, l->data);
        if (!_c)
            /* blankcol */
            continue;
        if (!_col || !streq (_c->name, column))
        {
            g_string_append (str, _c->name);
            g_string_append_c (str, ',');
        }
    }
    g_list_free (list);

    if (_col)
        g_string_truncate (str, str->len - 1);
    else
        g_string_append (str, column);

    memcpy (&arr, priv->arrangement, sizeof (DonnaArrangement));
    arr.columns = g_string_free (str, FALSE);
    load_arrangement (tree, &arr, FALSE);
    g_free (arr.columns);

    return TRUE;
}

/**
 * donna_tree_view_set_columns:
 * @tree: A #DonnaTreeView
 * @columns: Comma-separated list of columns
 * @error: (allow-none): Return location of a #GError, or %NULL
 *
 * Sets the columns of @tree to those specified in @columns, in the given order
 *
 * Returns: %TRUE
 */
gboolean
donna_tree_view_set_columns (DonnaTreeView      *tree,
                             const gchar        *columns,
                             GError            **error)
{
    DonnaTreeViewPrivate *priv;
    DonnaArrangement arr;

    g_return_val_if_fail (DONNA_IS_TREE_VIEW (tree), FALSE);
    g_return_val_if_fail (columns != NULL, FALSE);
    priv = tree->priv;

    if (G_UNLIKELY (!priv->arrangement))
        memset (&arr, 0, sizeof (DonnaArrangement));
    else
        memcpy (&arr, priv->arrangement, sizeof (DonnaArrangement));
    arr.columns = g_strdup (columns);
    load_arrangement (tree, &arr, FALSE);
    g_free (arr.columns);

    return TRUE;
}

struct refresh_list
{
    DonnaTreeView *tree;
    DonnaNode *node;
    DonnaNodeType node_types;
};

/* list only */
static void
node_get_children_refresh_list_cb (DonnaTask            *task,
                                   gboolean              timeout_called,
                                   struct refresh_list  *data)
{
    DonnaTreeViewPrivate *priv = data->tree->priv;

    if (priv->get_children_task != task)
        goto free;

    g_object_unref (priv->get_children_task);
    priv->get_children_task = NULL;

    if (G_UNLIKELY (data->node != priv->location))
    {
        gchar *fl_task = donna_node_get_full_location (data->node);
        gchar *fl_list = NULL;

        if (priv->location)
            fl_list = donna_node_get_full_location (priv->location);

        g_critical ("TreeView '%s': node_get_children_refresh_list_cb() triggered "
                "as the get_children_task yet current location differs.\n"
                "Task: %s\nList: %s",
                priv->name, fl_task, fl_list);

        g_free (fl_task);
        g_free (fl_list);
        goto free;
    }

    if (donna_task_get_state (task) != DONNA_TASK_DONE)
    {
        if (donna_task_get_state (task) == DONNA_TASK_FAILED)
            donna_app_show_error (priv->app, donna_task_get_error (task),
                    "TreeView '%s': Failed to refresh", priv->name);
        return;
    }

    set_children (data->tree, NULL, data->node_types,
            g_value_get_boxed (donna_task_get_return_value (task)),
            FALSE, TRUE);
free:
    g_free (data);
}

/* tree only */
static void
node_get_children_refresh_tree_cb (DonnaTask                         *task,
                                   gboolean                           timeout_called,
                                   struct node_children_refresh_data *data)
{
    DonnaTreeViewPrivate *priv = data->tree->priv;

    if (!is_watched_iter_valid (data->tree, &data->iter, TRUE))
        goto free;

    if (donna_task_get_state (task) != DONNA_TASK_DONE)
    {
        if (data->from_show_hidden)
        {
            const GError *err = donna_task_get_error (task);
            g_warning ("TreeView '%s': Failed to refresh children for show_hidden: %s",
                    priv->name, (err) ? err->message : "(no error message)");
        }
        else
            donna_app_show_error (priv->app, donna_task_get_error (task),
                    "TreeView '%s': Failed to refresh", priv->name);
        goto free;
    }

    set_children (data->tree, &data->iter, data->node_types,
            g_value_get_boxed (donna_task_get_return_value (task)),
            FALSE, TRUE);

free:
    g_free (data);
}

/* tree only */
static gboolean
may_get_children_refresh (DonnaTreeView *tree, GtkTreeIter *iter)
{
    DonnaTreeViewPrivate *priv = tree->priv;
    GError *err = NULL;
    GtkTreeModel *model = (GtkTreeModel *) priv->store;
    GtkTreePath *path;
    DonnaTask *task;
    DonnaNode *node;
    enum tree_expand es;
    gboolean ret = FALSE;

    path = gtk_tree_model_get_path (model, iter);

    /* refresh the node */
    gtk_tree_model_get (model, iter,
            TREE_COL_NODE,          &node,
            TREE_COL_EXPAND_STATE,  &es,
            -1);
    task = donna_node_refresh_task (node, &err, DONNA_NODE_REFRESH_SET_VALUES);
    if (!task)
    {
        gchar *fl = donna_node_get_full_location (node);
        g_warning ("TreeView '%s': Failed to refresh '%s': %s",
                priv->name, fl, err->message);
        g_clear_error (&err);
        g_free (fl);
        g_object_unref (node);
        gtk_tree_path_free (path);
        return FALSE;
    }
    donna_app_run_task (priv->app, task);

    /* if EXPAND_MAXI, update children */
    if (es == TREE_EXPAND_MAXI)
    {
        struct node_children_refresh_data *data;

        ret = TRUE;
        task = donna_node_get_children_task (node,
                priv->node_types, &err);
        if (!task)
        {
            gchar *fl = donna_node_get_full_location (node);
            g_warning ("TreeView '%s': Failed to trigger children update for '%s': %s",
                    priv->name, fl, err->message);
            g_clear_error (&err);
            g_free (fl);
            g_object_unref (node);
            gtk_tree_path_free (path);
            return FALSE;
        }

        data = g_new0 (struct node_children_refresh_data, 1);
        data->tree = tree;
        data->iter = *iter;
        watch_iter (tree, &data->iter);
        data->node_types = priv->node_types;

        donna_task_set_callback (task,
                (task_callback_fn) node_get_children_refresh_tree_cb,
                data, NULL);
        donna_app_run_task (priv->app, task);
    }
    gtk_tree_path_free (path);
    g_object_unref (node);
    return ret;
}

/**
 * donna_tree_view_refresh:
 * @tree: A #DonnaTreeView
 * @mode: The refresh mode
 * @error: (allow-none): Return location of a #GError, or %NULL
 *
 * Triggers a refresh of @tree
 *
 * Treeviews support different kinds of refresh operations, defined by @mode.
 *
 * %DONNA_TREE_VIEW_REFRESH_VISIBLE and %DONNA_TREE_VIEW_REFRESH_SIMPLE will
 * both simply ask each node to refrsh all its properties, but the former will
 * only do so on visible nodes while the later will do it on all nodes in the
 * treeview.
 *
 * %DONNA_TREE_VIEW_REFRESH_NORMAL will perform the "standard" refresh
 * operation, which also includes refreshing the list of children. For trees,
 * that is children of all nodes in maxi expand state; for lists children of the
 * current location.
 *
 * %DONNA_TREE_VIEW_REFRESH_RELOAD is intended to perform a full reloading of
 * the treeview. For lists, it will also include clearing the treeview and
 * re-filling it, as well as updating the current arrangement if needed.
 *
 * For trees, this isn't yet implement and is a no-op.
 *
 * Returns: %TRUE if refresh was initiated, else %FALSE
 */
gboolean
donna_tree_view_refresh (DonnaTreeView          *tree,
                         DonnaTreeViewRefreshMode mode,
                         GError                **error)
{
    DonnaTreeViewPrivate *priv;
    GtkTreeView *treev = (GtkTreeView *) tree;
    GtkTreeModel *model;

    g_return_val_if_fail (DONNA_IS_TREE_VIEW (tree), FALSE);
    g_return_val_if_fail (mode == DONNA_TREE_VIEW_REFRESH_VISIBLE
            || mode == DONNA_TREE_VIEW_REFRESH_SIMPLE
            || mode == DONNA_TREE_VIEW_REFRESH_NORMAL
            || mode == DONNA_TREE_VIEW_REFRESH_RELOAD, FALSE);
    priv = tree->priv;
    model = (GtkTreeModel *) priv->store;

    if (G_UNLIKELY (!priv->is_tree && !priv->location))
            return TRUE;

    if (mode == DONNA_TREE_VIEW_REFRESH_VISIBLE
            || mode == DONNA_TREE_VIEW_REFRESH_SIMPLE)
    {
        struct refresh_data *data;
        GPtrArray *tasks;
        GPtrArray *props;
        GtkTreePath *start = NULL;
        GtkTreePath *end = NULL;
        GtkTreeIter  it_end;
        GtkTreeIter  it;
        guint i;

        if (!has_model_at_least_n_rows (model, 1))
            return TRUE;

        if (mode == DONNA_TREE_VIEW_REFRESH_VISIBLE)
        {
            if (!gtk_tree_view_get_visible_range (treev, &start, &end)
                    || !gtk_tree_model_get_iter (model, &it, start)
                    || !gtk_tree_model_get_iter (model, &it_end, end))
            {
                if (start)
                    gtk_tree_path_free (start);
                if (end)
                    gtk_tree_path_free (end);
                g_set_error (error, DONNA_TREE_VIEW_ERROR,
                        DONNA_TREE_VIEW_ERROR_OTHER,
                        "TreeView '%s': Failed to get visible range of rows",
                        priv->name);
                return FALSE;
            }
            gtk_tree_path_free (start);
            gtk_tree_path_free (end);
        }
        else /* DONNA_TREE_VIEW_REFRESH_SIMPLE */
        {
            if (!gtk_tree_model_iter_children (model, &it, NULL))
            {
                g_set_error (error, DONNA_TREE_VIEW_ERROR,
                        DONNA_TREE_VIEW_ERROR_OTHER,
                        "TreeView '%s': Failed to get first row",
                        priv->name);
                return FALSE;
            }
        }

        /* see refresh_node_cb() for more about this */
        data = g_new0 (struct refresh_data, 1);
        data->tree = tree;
        priv->refresh_on_hold = TRUE;

        props = g_ptr_array_sized_new (priv->col_props->len);
        for (i = 0; i < priv->col_props->len; ++i)
        {
            struct col_prop *cp;

            cp = &g_array_index (priv->col_props, struct col_prop, i);
            if (get_column_by_column (tree, cp->column)->refresh_properties
                    != RP_ON_DEMAND)
                /* do not refresh properties for ON_DEMAND columns, to
                 * refresh/load them see
                 * donna_tree_view_column_refresh_nodes() */
                g_ptr_array_add (props, cp->prop);
        }

        tasks = g_ptr_array_new ();
        do
        {
            GError *err = NULL;
            DonnaNode *node;

            if (!is_row_accessible (tree, &it))
                continue;

            gtk_tree_model_get (model, &it,
                    TREE_COL_NODE,  &node,
                    -1);
            /* donna_node_refresh_arr_tasks_arr() will unref props, but we want
             * to keep it alive */
            g_ptr_array_ref (props);
            if (G_UNLIKELY (!donna_node_refresh_arr_tasks_arr (node, tasks, props, &err)))
            {
                gchar *fl = donna_node_get_full_location (node);
                g_warning ("TreeView '%s': Failed to refresh '%s': %s",
                        priv->name, fl, err->message);
                g_clear_error (&err);
                g_free (fl);
                continue;
            }
            g_object_unref (node);

            data->started += tasks->len;
            for (i = 0; i < tasks->len; ++i)
            {
                DonnaTask *task = tasks->pdata[i];
                donna_task_set_callback (task,
                        (task_callback_fn) refresh_node_cb, data, NULL);
                donna_app_run_task (priv->app, task);
            }
            if (tasks->len > 0)
                g_ptr_array_remove_range (tasks, 0, tasks->len);
        } while ((mode == DONNA_TREE_VIEW_REFRESH_SIMPLE || !itereq (&it, &it_end))
                && _gtk_tree_model_iter_next (model, &it));
        g_ptr_array_unref (tasks);
        g_ptr_array_unref (props);

        if (G_UNLIKELY (data->started == 0))
        {
            g_free (data);
            g_set_error (error, DONNA_TREE_VIEW_ERROR,
                    DONNA_TREE_VIEW_ERROR_OTHER,
                    "TreeView '%s': Failed to get any task to perform refresh",
                    priv->name);
            return FALSE;
        }

        /* set flag done to TRUE and handles things in off chance all tasks are
         * already done */
        refresh_node_cb (NULL, FALSE, data);

        return TRUE;
    }
    else if (mode == DONNA_TREE_VIEW_REFRESH_NORMAL)
    {
        if (priv->is_tree)
        {
            GtkTreeIter it;
            gboolean (*next_fn) (GtkTreeModel *model, GtkTreeIter *iter);

            if (!has_model_at_least_n_rows (model, 1))
                return TRUE;

            if (!gtk_tree_model_iter_children (model, &it, NULL))
            {
                g_set_error (error, DONNA_TREE_VIEW_ERROR,
                        DONNA_TREE_VIEW_ERROR_OTHER,
                        "TreeView '%s': Failed to get first root",
                        priv->name);
                return FALSE;
            }

            /* to silence warnings, since really the first one will always be
             * accessible (a root) hence next_fn will always be set anyways */
            next_fn = _gtk_tree_model_iter_next;
            do
            {
                if (!is_row_accessible (tree, &it))
                    continue;

                if (may_get_children_refresh (tree, &it))
                    next_fn = gtk_tree_model_iter_next;
                else
                    next_fn = _gtk_tree_model_iter_next;
            } while (next_fn (model, &it));

            return TRUE;
        }
        else
        {
            DonnaTask *task;
            struct refresh_list *data;

            if (priv->location_task)
                task = donna_task_get_duplicate (priv->location_task, error);
            else
                task = donna_node_get_children_task (priv->location,
                        priv->node_types, error);
            if (!task)
                return FALSE;
            set_get_children_task (tree, task);

            data = g_new (struct refresh_list, 1);
            data->tree = tree;
            data->node = priv->location;
            data->node_types = priv->node_types;

            donna_task_set_callback (task,
                    (task_callback_fn) node_get_children_refresh_list_cb,
                    data, g_free);
            donna_app_run_task (priv->app, task);
            return TRUE;
        }
    }
    /* DONNA_TREE_VIEW_REFRESH_RELOAD */

    if (priv->is_tree)
    {
        /* TODO save to file; clear; load arr; load from file... or something */
    }
    else
    {
        if (priv->location_task)
        {
            struct node_get_children_list_data *data;
            DonnaTask *task;

            task = donna_task_get_duplicate (priv->location_task, error);
            if (!task)
                return FALSE;
            set_get_children_task (tree, task);

            data = g_slice_new0 (struct node_get_children_list_data);
            data->tree = tree;
            data->node = g_object_ref (priv->location);

            donna_task_set_callback (task,
                    (task_callback_fn) node_get_children_list_cb,
                    data,
                    (GDestroyNotify) free_node_get_children_list_data);
            donna_app_run_task (priv->app, task);
        }
        else
            return change_location (tree, CHANGING_LOCATION_ASKED, priv->location,
                    NULL, error);
    }

    return TRUE;
}

/**
 * donna_tree_view_goto_line:
 * @tree: A #DonnaTreeView
 * @set: Which element to set on @rowid
 * @rowid: Identifier of a row; See #rowid for more
 * @nb: Number of line/times to repeat the move
 * @nb_type: Define how to interpret @nb
 * @action: Action to perform on the selection
 * @to_focused: When %TRUE rows will be the range from @rowid to the focused row
 * @error: (allow-none): Return location of a #GError, or %NULL
 *
 * "Goes" to the row behind @rowid. What is actually done depends on @set,
 * defining which of the scroll, focus and/or cursor will be put on the final
 * destination row.
 *
 * @nb_type defines how to process @nb: it can be a line number or a percentage
 * (of the entire tree, or just the visible area) to go to, in which case @rowid
 * will be ignored. (Note however that if @nb is 0 and @nb_type is
 * %DONNA_TREE_VIEW_GOTO_LINE then @rowid is used, since line numbers start at
 * 1.)
 *
 * If it is %DONNA_TREE_VIEW_GOTO_REPEAT (and @nb > 0) then the operation will
 * be repeated @nb times.
 * Obviously this only makes sense with certain rowids, specifically ":top",
 * ":bottom" which, if already there, will then move one screen up/down; and
 * ":prev", ":next", ":up", ":down", ":prev-same-depth" and ":next-same-depth"
 * which will simply be repeated. For any other rowid, the operation isn't
 * repeated and @nb is simply ignored.
 *
 * In addition, the selection can also be affected, according to @action and
 * @to_focused. See donna_tree_view_selection() for more, which will be called
 * with the final destination row as rowid, before scroll, focus or cursor have
 * been set.
 *
 * Returns: %TRUE on success, else %FALSE
 */
gboolean
donna_tree_view_goto_line (DonnaTreeView      *tree,
                           DonnaTreeViewSet    set,
                           DonnaRowId         *rowid,
                           guint               nb,
                           DonnaTreeViewGoto   nb_type,
                           DonnaSelAction      action,
                           gboolean            to_focused,
                           GError            **error)
{
    DonnaTreeViewPrivate *priv;
    GtkTreeView *treev = (GtkTreeView *) tree;
    GtkTreeModel *model;
    GtkTreeIter   iter;
    row_id_type   type;
    GtkTreePath  *path  = NULL;
    GtkTreeIter   tb_iter;
    guint         is_tb = 0;
    guint         rows  = 0;
    guint         max   = 0;
    GdkRectangle  rect_visible;
    GdkRectangle  rect;

    g_return_val_if_fail (DONNA_IS_TREE_VIEW (tree), FALSE);
    g_return_val_if_fail (rowid != NULL, FALSE);
    priv  = tree->priv;
    model = (GtkTreeModel *) priv->store;

    if (nb_type == DONNA_TREE_VIEW_GOTO_PERCENT
            || nb_type == DONNA_TREE_VIEW_GOTO_VISIBLE)
    {
        GtkTreeIter iter_top;
        guint top = 0;
        gint height;

        /* locate first/top row */
        if (nb_type == DONNA_TREE_VIEW_GOTO_PERCENT)
            path = gtk_tree_path_new_from_indices (0, -1);
        else
        {
            DonnaRowId rid = { DONNA_ARG_TYPE_PATH, (gpointer) ":top" };
            if (convert_row_id_to_iter (tree, &rid, &iter_top) == ROW_ID_INVALID)
            {
                g_set_error (error, DONNA_TREE_VIEW_ERROR,
                        DONNA_TREE_VIEW_ERROR_OTHER,
                        "TreeView '%s': Failed getting the top row",
                        priv->name);
                return FALSE;
            }
            path = gtk_tree_model_get_path (model, &iter_top);
            if (!priv->is_tree)
                top = (guint) gtk_tree_path_get_indices (path)[0];
        }
        gtk_tree_view_get_background_area (treev, path, NULL, &rect);
        gtk_tree_path_free (path);
        height = ABS (rect.y);

        /* locate last/bottom row */
        if (nb_type == DONNA_TREE_VIEW_GOTO_PERCENT)
        {
            if (!_gtk_tree_model_iter_last (model, &iter))
            {
                g_set_error (error, DONNA_TREE_VIEW_ERROR,
                        DONNA_TREE_VIEW_ERROR_OTHER,
                        "TreeView '%s': Failed getting the last row",
                        priv->name);
                return FALSE;
            }
        }
        else
        {
            DonnaRowId rid = { DONNA_ARG_TYPE_PATH, (gpointer) ":bottom" };
            if (convert_row_id_to_iter (tree, &rid, &iter) == ROW_ID_INVALID)
            {
                g_set_error (error, DONNA_TREE_VIEW_ERROR,
                        DONNA_TREE_VIEW_ERROR_OTHER,
                        "TreeView '%s': Failed getting the bottom row",
                        priv->name);
                return FALSE;
            }
        }
        path = gtk_tree_model_get_path (model, &iter);
        if (!path)
        {
            g_set_error (error, DONNA_TREE_VIEW_ERROR,
                    DONNA_TREE_VIEW_ERROR_OTHER,
                    "TreeView '%s': Failed getting path to the last row",
                    priv->name);
            return FALSE;
        }
        gtk_tree_view_get_background_area (treev, path, NULL, &rect);
        gtk_tree_path_free (path);
        height += ABS (rect.y) + rect.height;

        /* nb of rows accessible on tree */
        rows = (guint) (height / rect.height);

        /* get the one at specified percent */
        nb = (guint) ((gdouble) rows * ((gdouble) nb / 100.0)) + 1;
        nb = CLAMP (nb, 1, rows);
        if (nb_type == DONNA_TREE_VIEW_GOTO_VISIBLE)
        {
            if (priv->is_tree)
                iter = iter_top;
            else
            {
                nb += top;
                /* this can now be treated as LINE */
                nb_type = DONNA_TREE_VIEW_GOTO_LINE;
            }
        }
        else
            /* this can now be treated as LINE */
            nb_type = DONNA_TREE_VIEW_GOTO_LINE;
    }

    if (nb > 0 && (nb_type == DONNA_TREE_VIEW_GOTO_LINE
                || nb_type == DONNA_TREE_VIEW_GOTO_VISIBLE))
    {
        if (!priv->is_tree)
        {
            /* list, so line n is path n-1 */
            path = gtk_tree_path_new_from_indices ((gint) nb - 1, -1);
            if (!gtk_tree_model_get_iter (model, &iter, path))
            {
                gtk_tree_path_free (path);
                /* row doesn't exist, i.e. number is too high, let's just go to
                 * the last one */
                if (!_gtk_tree_model_iter_last (model, &iter))
                {
                    g_set_error (error, DONNA_TREE_VIEW_ERROR,
                            DONNA_TREE_VIEW_ERROR_OTHER,
                            "TreeView '%s': Failed getting the last row (<%d)",
                            priv->name, nb);
                    return FALSE;
                }
                path = gtk_tree_model_get_path (model, &iter);
            }
        }
        else
        {
            GtkTreeIter it;
            guint i;

            /* tree: we can't just get a path, so we'll go to the first/top row
             * and move down. If nb_type is still VISIBLE it means iter has
             * already been set to the top row */
            if (nb_type == DONNA_TREE_VIEW_GOTO_LINE)
                if (!gtk_tree_model_iter_children (model, &iter, NULL))
                {
                    g_set_error (error, DONNA_TREE_VIEW_ERROR,
                            DONNA_TREE_VIEW_ERROR_OTHER,
                            "TreeView '%s': Failed getting the first row (going to %d)",
                            priv->name, nb);
                    return FALSE;
                }

            it = iter;
            for (i = 1; i < nb; )
            {
                if (!_gtk_tree_model_iter_next (model, &iter))
                {
                    /* row doesn't exist, i.e. number is too high, let's just go
                     * to the last one */
                    iter = it;
                    break;
                }
                if (is_row_accessible (tree, &iter))
                {
                    it = iter;
                    ++i;
                }
            }
            path = gtk_tree_model_get_path (model, &iter);
        }
        nb = 1;
        goto move;
    }

    /* those are special cases, where if the focus is already there, we want to
     * go one up/down more screen */
    if (rowid->type == DONNA_ARG_TYPE_PATH
            && (streq (rowid->ptr, ":top") || streq (rowid->ptr, ":bottom")))
    {
        is_tb = 1;
        gtk_tree_view_get_cursor (treev, &path, NULL);
        if (!path)
        {
            g_set_error (error, DONNA_TREE_VIEW_ERROR,
                    DONNA_TREE_VIEW_ERROR_INVALID_ROW_ID,
                    "TreeView '%s': Cannot go to line, failed to get cursor",
                    priv->name);
            return FALSE;
        }
        if (!gtk_tree_model_get_iter (model, &tb_iter, path))
        {
            gtk_tree_path_free (path);
            g_set_error (error, DONNA_TREE_VIEW_ERROR,
                    DONNA_TREE_VIEW_ERROR_INVALID_ROW_ID,
                    "TreeView '%s': Cannot go to line, failed to get cursor",
                    priv->name);
            return FALSE;
        }
        gtk_tree_path_free (path);
        path = NULL;
    }

    if (nb > 1 && nb_type == DONNA_TREE_VIEW_GOTO_REPEAT)
    {
        /* only those make sense to be repeated */
        if (rowid->type != DONNA_ARG_TYPE_PATH
                || !(is_tb || streq (rowid->ptr, ":prev")
                    || streq (rowid->ptr, ":next")
                    || streq (rowid->ptr, ":up")
                    || streq (rowid->ptr, ":down")
                    || streq (rowid->ptr, ":prev-same-depth")
                    || streq (rowid->ptr, ":next-same-depth")))
            nb = 1;
    }
    else
        nb = 1;

    for ( ; nb > 0; --nb)
    {
        if (is_tb < 2)
        {
            if (path)
                gtk_tree_path_free (path);

            type = convert_row_id_to_iter (tree, rowid, &iter);
            if (type != ROW_ID_ROW)
            {
                g_set_error (error, DONNA_TREE_VIEW_ERROR,
                        DONNA_TREE_VIEW_ERROR_INVALID_ROW_ID,
                        "TreeView '%s': Cannot go to line, invalid row-id",
                        priv->name);
                return FALSE;
            }

            path = gtk_tree_model_get_path (model, &iter);
        }

        if (is_tb)
        {
            if (is_tb == 1 && itereq (&iter, &tb_iter))
                is_tb = 2;

            /* scroll only; or we're already there: let's go beyond */
            if (set == DONNA_TREE_VIEW_SET_SCROLL || is_tb == 2)
            {
                if (!rows)
                {
                    gint count;

                    gtk_tree_view_get_visible_rect (treev, &rect_visible);
                    gtk_tree_view_get_background_area (treev, path, NULL, &rect);
                    rows  = (guint) (rect_visible.height / rect.height);
                    count = _gtk_tree_model_get_count (model) - 1;
                    if (count >= 0)
                        max = (guint) count;
                    else
                        max = 0;
                }

                if (!priv->is_tree)
                {
                    gint *indices;
                    guint i;

                    indices = gtk_tree_path_get_indices (path);
                    i = (guint) indices[0];
                    if (((gchar *) rowid->ptr)[1] == 't')
                    {
                        if (rows > i)
                            i = 0;
                        else
                            i -= rows;
                    }
                    else
                    {
                        i += rows;
                        i = MIN (i, max);
                    }

                    gtk_tree_path_free (path);
                    path = gtk_tree_path_new_from_indices ((gint) i, -1);
                    gtk_tree_model_get_iter (model, &iter, path);
                }
                else
                {
                    guint i;
                    gboolean (*move_fn) (GtkTreeModel *, GtkTreeIter *);
                    GtkTreeIter prev_it = iter;

                    if (((gchar *) rowid->ptr)[1] == 't')
                        move_fn = _gtk_tree_model_iter_previous;
                    else
                        move_fn = _gtk_tree_model_iter_next;

                    for (i = 1; i < rows; )
                    {
                        if (!move_fn (model, &iter))
                        {
                            iter = prev_it;
                            break;
                        }
                        if (is_row_accessible (tree, &iter))
                        {
                            prev_it = iter;
                            ++i;
                        }
                    }
                    path = gtk_tree_model_get_path (model, &iter);
                }

            }
            is_tb = 2;
        }
move:
        if (action == DONNA_SEL_SELECT || action == DONNA_SEL_UNSELECT
                || action == DONNA_SEL_INVERT || action == DONNA_SEL_DEFINE)
        {
            DonnaRowId rid;
            DonnaRow r;

            rid.type = DONNA_ARG_TYPE_ROW;
            rid.ptr  = &r;

            gtk_tree_model_get (model, &iter,
                    TREE_VIEW_COL_NODE, &r.node,
                    -1);
            /* get iter from hashtable */
            if (priv->is_tree)
            {
                GSList *l = g_hash_table_lookup (priv->hashtable, r.node);
                r.iter = l->data;
            }
            else
                r.iter = g_hash_table_lookup (priv->hashtable, r.node);

            donna_tree_view_selection (tree, action, &rid, to_focused, NULL);
            g_object_unref (r.node);
        }

        if (set & DONNA_TREE_VIEW_SET_FOCUS)
            gtk_tree_view_set_focused_row (treev, path);
        if (set & DONNA_TREE_VIEW_SET_CURSOR)
        {
            if (!(set & DONNA_TREE_VIEW_SET_FOCUS))
                    gtk_tree_view_set_focused_row (treev, path);
            gtk_tree_selection_select_path (
                    gtk_tree_view_get_selection (treev), path);
        }
    }

    if (set & DONNA_TREE_VIEW_SET_SCROLL)
    {
        /* get visible area, so we can determine if it is already visible */
        gtk_tree_view_get_visible_rect (treev, &rect_visible);

        gtk_tree_view_get_background_area (treev, path, NULL, &rect);
        if (nb_type == DONNA_TREE_VIEW_GOTO_LINE)
        {
            /* when going to a specific line, let's center it */
            if (rect.y < 0 || rect.y > rect_visible.height - rect.height)
                gtk_tree_view_scroll_to_cell (treev, path, NULL, TRUE, 0.5, 0.0);
        }
        else
        {
            /* only scroll if not visible. Using FALSE is supposed to get the tree
             * to do the minimum of scrolling, but that's apparently prety bugged,
             * and sometimes for a row only half visible on the bottom, GTK feels
             * that minimum scrolling means putting it on top (!!).
             * So, this is why we force it ourself as such. */
            if (rect.y < 0)
                gtk_tree_view_scroll_to_cell (treev, path, NULL, TRUE, 0.0, 0.0);
            if (rect.y > rect_visible.height - rect.height)
                gtk_tree_view_scroll_to_cell (treev, path, NULL, TRUE, 1.0, 0.0);
        }
    }

    gtk_tree_path_free (path);
    check_statuses (tree, STATUS_CHANGED_ON_CONTENT);
    return TRUE;
}

/**
 * donna_tree_view_get_node_at_row:
 * @tree: A #DonnaTreeView
 * @rowid: Identifier of a row; See #rowid for more
 * @error: (allow-none): Return location of a #GError, or %NULL
 *
 * Returns the #DonnaNode behind the row at @rowid
 *
 * Returns: (transfer full): The #DonnaNode behind the row at @rowid
 */
DonnaNode *
donna_tree_view_get_node_at_row (DonnaTreeView  *tree,
                                 DonnaRowId     *rowid,
                                 GError        **error)
{
    DonnaTreeViewPrivate *priv;
    GtkTreeIter   iter;
    row_id_type   type;
    DonnaNode    *node;

    g_return_val_if_fail (DONNA_IS_TREE_VIEW (tree), FALSE);
    g_return_val_if_fail (rowid != NULL, FALSE);
    priv = tree->priv;

    type = convert_row_id_to_iter (tree, rowid, &iter);
    if (type != ROW_ID_ROW)
    {
        g_set_error (error, DONNA_TREE_VIEW_ERROR,
                DONNA_TREE_VIEW_ERROR_INVALID_ROW_ID,
                "TreeView '%s': Cannot get node, invalid row-id",
                priv->name);
        return FALSE;
    }

    gtk_tree_model_get ((GtkTreeModel *) priv->store, &iter,
            TREE_VIEW_COL_NODE, &node,
            -1);

    return node;
}

/**
 * donna_tree_view_set_key_mode:
 * @tree: A #DonnaTreeView
 * @key_mode: The name of the key mode to set
 *
 * Sets the current key mode for @tree to @key_mode (See #key-modes for more)
 *
 * Will also reset the current key status, much like
 * donna_tree_view_reset_keys() would.
 */
void
donna_tree_view_set_key_mode (DonnaTreeView *tree, const gchar *key_mode)
{
    DonnaTreeViewPrivate *priv;

    g_return_if_fail (DONNA_IS_TREE_VIEW (tree));
    priv = tree->priv;

    g_free (priv->key_mode);
    priv->key_mode = g_strdup (key_mode);

    /* wrong_key */
    g_free (priv->key_combine_name);
    priv->key_combine_name = NULL;
    priv->key_combine_val = 0;
    priv->key_combine_spec = 0;
    priv->key_spec_type = SPEC_NONE;
    priv->key_m = 0;
    priv->key_val = 0;
    priv->key_motion_m = 0;
    priv->key_motion = 0;

    check_statuses (tree, STATUS_CHANGED_ON_KEYS | STATUS_CHANGED_ON_KEY_MODE);
}

/**
 * donna_tree_view_remove_row:
 * @tree: A #DonnaTreeView
 * @rowid: Identifier of a row; See #rowid for more
 * @error: (allow-none): Return location of a #GError, or %NULL
 *
 * Removes the row behind @rowid from the @tree
 *
 * Removing a row from the tree doesn't affect the actual node behind it, i.e.
 * no folder will be deleted from the file system.
 *
 * This only make sense for roots, unless you're using a #minitree of course, in
 * which case any row can be removed at will.
 *
 * Obviously this is only supported on trees.
 *
 * Returns: %TRUE on success, else %FALSE
 */
gboolean
donna_tree_view_remove_row (DonnaTreeView   *tree,
                            DonnaRowId      *rowid,
                            GError         **error)
{
    DonnaTreeViewPrivate *priv;
    GtkTreeIter iter;
    row_id_type type;

    g_return_val_if_fail (DONNA_IS_TREE_VIEW (tree), FALSE);
    g_return_val_if_fail (rowid != NULL, FALSE);
    priv = tree->priv;

    if (!priv->is_tree)
    {
        g_set_error (error, DONNA_TREE_VIEW_ERROR,
                DONNA_TREE_VIEW_ERROR_INVALID_MODE,
                "TreeView '%s': Cannot remove row in mode List",
                priv->name);
        return FALSE;
    }

    type = convert_row_id_to_iter (tree, rowid, &iter);
    if (type != ROW_ID_ROW)
    {
        g_set_error (error, DONNA_TREE_VIEW_ERROR,
                DONNA_TREE_VIEW_ERROR_INVALID_ROW_ID,
                "TreeView '%s': Cannot remove row, invalid row-id",
                priv->name);
        return FALSE;
    }

    if (!priv->is_minitree)
    {
        GtkTreePath *path;

        /* on non-minitree we can only remove roots */
        path = gtk_tree_model_get_path ((GtkTreeModel *) priv->store, &iter);
        if (gtk_tree_path_get_depth (path) > 1)
        {
            g_set_error (error, DONNA_TREE_VIEW_ERROR,
                    DONNA_TREE_VIEW_ERROR_INVALID_ROW_ID,
                    "TreeView '%s': Cannot remove row, option is_minitree not enabled",
                    priv->name);
            gtk_tree_path_free (path);
            return FALSE;
        }
        gtk_tree_path_free (path);
    }

    remove_row_from_tree (tree, &iter, RR_NOT_REMOVAL);
    return TRUE;
}

/**
 * donna_tree_view_reset_keys:
 * @tree: A #DonnaTreeView
 *
 * Resets the current key status. That is, if any keys had been typed and not
 * yet fully processed (e.g. a combine was typed, waiting for the association
 * key/command to be pressed) they will be dropped.
 *
 * Additionally, the current key mode will be reloaded from configuration. (See
 * #key-modes for more)
 */
void
donna_tree_view_reset_keys (DonnaTreeView *tree)
{
    DonnaTreeViewPrivate *priv;

    g_return_if_fail (DONNA_IS_TREE_VIEW (tree));
    priv = tree->priv;

    g_free (priv->key_mode);
    priv->key_mode = cfg_get_key_mode (tree, donna_app_peek_config (priv->app));

    /* wrong_key */
    g_free (priv->key_combine_name);
    priv->key_combine_name = NULL;
    priv->key_combine_val = 0;
    priv->key_combine_spec = 0;
    priv->key_spec_type = SPEC_NONE;
    priv->key_m = 0;
    priv->key_val = 0;
    priv->key_motion_m = 0;
    priv->key_motion = 0;

    check_statuses (tree, STATUS_CHANGED_ON_KEYS | STATUS_CHANGED_ON_KEY_MODE);
}

/* mode list only */
/**
 * donna_tree_view_abort:
 * @tree: A #DonnaTreeView
 *
 * Abort any running task changing @tree's current location as well as task to
 * refresh properties (from columns preloading properties)
 *
 * Note that this obviously only works on lists (i.e. is a no-op on trees).
 */
void
donna_tree_view_abort (DonnaTreeView *tree)
{
    DonnaTask *task;

    g_return_if_fail (DONNA_IS_TREE_VIEW (tree));

    set_get_children_task (tree, NULL);
    task = g_object_get_data ((GObject *) tree, DATA_PRELOAD_TASK);
    if (task)
    {
        donna_task_cancel (task);
        g_object_set_data ((GObject *) tree, DATA_PRELOAD_TASK, NULL);
    }
}

/**
 * donna_tree_view_get_nodes:
 * @tree: A #DonnaTreeView
 * @rowid: Identifier of a row; See #rowid for more
 * @to_focused: When %TRUE rows will be the range from @rowid to the focused row
 * @error: (allow-none): Return location of a #GError, or %NULL
 *
 * Returns the nodes behind the row, or rows, pointed by @rowid
 *
 * If @to_focused is %TRUE then @rowid must point to a row which will be used as
 * other boundary (with focused row) to make the range of rows to be used.
 * @to_focused can only be used on lists.
 *
 * Returns: (transfer full) (element-type DonnaNode): The #DonnaNode<!-- -->s
 * behind specified rows, or %NULL
 */
GPtrArray *
donna_tree_view_get_nodes (DonnaTreeView      *tree,
                           DonnaRowId         *rowid,
                           gboolean            to_focused,
                           GError            **error)
{
    DonnaTreeViewPrivate *priv;
    GtkTreeModel *model;
    GtkTreeSelection *sel;
    GtkTreeIter iter_focus;
    GtkTreeIter iter_last;
    GtkTreeIter iter;
    row_id_type type;
    GPtrArray *arr;
    gboolean second_pass = FALSE;

    g_return_val_if_fail (DONNA_IS_TREE_VIEW (tree), NULL);
    g_return_val_if_fail (rowid != NULL, NULL);
    priv  = tree->priv;
    model = (GtkTreeModel *) priv->store;

    type = convert_row_id_to_iter (tree, rowid, &iter);
    if (type == ROW_ID_INVALID)
    {
        g_set_error (error, DONNA_TREE_VIEW_ERROR,
                DONNA_TREE_VIEW_ERROR_INVALID_ROW_ID,
                "TreeView '%s': Cannot get nodes, invalid row-id",
                priv->name);
        return NULL;
    }

    if (priv->is_tree && type == ROW_ID_ROW && to_focused)
    {
        g_set_error (error, DONNA_TREE_VIEW_ERROR,
                DONNA_TREE_VIEW_ERROR_INCOMPATIBLE_OPTION,
                "TreeView '%s': Cannot get nodes using 'to_focused' flag in mode tree",
                priv->name);
        return NULL;
    }

    if (type == ROW_ID_ROW)
    {
        if (to_focused)
        {
            GtkTreePath *path_focus;
            GtkTreePath *path;

            gtk_tree_view_get_cursor ((GtkTreeView *) tree, &path_focus, NULL);
            if (!path_focus)
            {
                g_set_error (error, DONNA_TREE_VIEW_ERROR,
                        DONNA_TREE_VIEW_ERROR_OTHER,
                        "TreeView '%s': Cannot get nodes, failed to get focused row",
                        priv->name);
                return NULL;
            }
            path = gtk_tree_model_get_path (model, &iter);
            if (!path)
            {
                g_set_error (error, DONNA_TREE_VIEW_ERROR,
                        DONNA_TREE_VIEW_ERROR_OTHER,
                        "TreeView '%s': Cannot get nodes, failed to get path",
                        priv->name);
                gtk_tree_path_free (path_focus);
                return NULL;
            }

            if (gtk_tree_path_compare (path, path_focus) > 0)
            {
                gtk_tree_model_get_iter (model, &iter, path_focus);
                gtk_tree_model_get_iter (model, &iter_last, path);
            }
            else
                gtk_tree_model_get_iter (model, &iter_last, path_focus);

            gtk_tree_path_free (path_focus);
            gtk_tree_path_free (path);
        }
        else
            iter_last = iter;
    }
    else
        if (!init_getting_nodes ((GtkTreeView *) tree, model, &iter_focus, &iter))
            /* empty tree, let's just return "nothing" then */
            return g_ptr_array_new ();

    sel = gtk_tree_view_get_selection ((GtkTreeView *) tree);
    arr = g_ptr_array_new_with_free_func (g_object_unref);
again:
    for (;;)
    {
        if (second_pass && itereq (&iter, &iter_focus))
        {
            iter_focus.stamp = 0;
            break;
        }
        else if (type != ROW_ID_SELECTION
                || gtk_tree_selection_iter_is_selected (sel, &iter))
        {
            DonnaNode *node;

            gtk_tree_model_get (model, &iter,
                    TREE_VIEW_COL_NODE, &node,
                    -1);
            if (G_LIKELY (node))
                g_ptr_array_add (arr, node);
        }

        if ((type == ROW_ID_ROW && itereq (&iter, &iter_last))
                || !_gtk_tree_model_iter_next (model, &iter))
            break;
    }

    /* if we started at focus, let's start back from top to focus */
    if (type != ROW_ID_ROW && iter_focus.stamp != 0)
    {
        gtk_tree_model_iter_children (model, &iter, NULL);
        second_pass = TRUE;
        goto again;
    }

    return arr;
}

/* mode list only */
static DonnaTaskState
history_goto (DonnaTask *task, DonnaNode *node)
{
    GError *err = NULL;
    GValue v = G_VALUE_INIT;
    DonnaNodeHasValue has;
    DonnaTreeView *tree;
    DonnaHistoryDirection direction;
    DonnaTaskState ret = DONNA_TASK_DONE;

    donna_node_get (node, FALSE, "history-tree", &has, &v, NULL);
    tree = g_value_get_object (&v);
    g_value_unset (&v);

    donna_node_get (node, FALSE, "history-direction", &has, &v, NULL);
    if (G_UNLIKELY (has != DONNA_NODE_VALUE_SET))
    {
        /* current location: refresh */
        donna_tree_view_refresh (tree, DONNA_TREE_VIEW_REFRESH_NORMAL, NULL);
        return DONNA_TASK_DONE;
    }
    direction = g_value_get_uint (&v);
    g_value_unset (&v);

    donna_node_get (node, FALSE, "history-pos", &has, &v, NULL);

    if (!donna_tree_view_history_move (tree, direction, g_value_get_uint (&v), &err))
    {
        ret = DONNA_TASK_FAILED;
        donna_task_take_error (task, err);
    }
    g_value_unset (&v);

    return ret;
}

/* mode list only */
static DonnaNode *
get_node_for_history (DonnaTreeView         *tree,
                      DonnaProviderInternal *pi,
                      const gchar           *name,
                      DonnaHistoryDirection  direction,
                      guint                  nb,
                      GError               **error)
{
    DonnaTreeViewPrivate *priv = tree->priv;
    DonnaNode *node;
    GValue v = G_VALUE_INIT;

    node = donna_provider_internal_new_node (pi, name, FALSE, NULL, NULL,
            DONNA_NODE_ITEM, TRUE, DONNA_TASK_VISIBILITY_INTERNAL_GUI,
            (internal_fn) history_goto, NULL, NULL, error);
    if (G_UNLIKELY (!node))
    {
        g_prefix_error (error, "TreeView '%s': Failed to get history; "
                "couldn't create node: ",
                priv->name);
        return NULL;
    }

    g_value_init (&v, DONNA_TYPE_TREE_VIEW);
    g_value_set_object (&v, tree);
    if (G_UNLIKELY (!donna_node_add_property (node, "history-tree",
                    DONNA_TYPE_TREE_VIEW, &v,
                    DONNA_TASK_VISIBILITY_INTERNAL_FAST,
                    NULL, (refresher_fn) gtk_true,
                    NULL,
                    NULL, NULL,
                    error)))
    {
        g_prefix_error (error, "TreeView '%s': Failed to get history; "
                "couldn't add property 'history-tree': ",
                priv->name);
        g_value_unset (&v);
        g_object_unref (node);
        return NULL;
    }
    g_value_unset (&v);

    /* no direction == node for current location */
    if (direction == 0)
    {
        g_value_init (&v, G_TYPE_ICON);
        g_value_take_object (&v, g_themed_icon_new ("view-refresh"));
        donna_node_add_property (node, "menu-image-selected",
                G_TYPE_ICON, &v,
                DONNA_TASK_VISIBILITY_INTERNAL_FAST,
                NULL, (refresher_fn) gtk_true,
                NULL,
                NULL, NULL,
                NULL);
        g_value_unset (&v);

        g_value_init (&v, G_TYPE_BOOLEAN);
        g_value_set_boolean (&v, TRUE);
        donna_node_add_property (node, "menu-is-label-bold",
                G_TYPE_BOOLEAN, &v,
                DONNA_TASK_VISIBILITY_INTERNAL_FAST,
                NULL, (refresher_fn) gtk_true,
                NULL,
                NULL, NULL,
                NULL);
        g_value_unset (&v);

        return node;
    }

    g_value_init (&v, G_TYPE_UINT);
    g_value_set_uint (&v, direction);
    if (G_UNLIKELY (!donna_node_add_property (node, "history-direction",
                    G_TYPE_UINT, &v,
                    DONNA_TASK_VISIBILITY_INTERNAL_FAST,
                    NULL, (refresher_fn) gtk_true,
                    NULL,
                    NULL, NULL,
                    error)))
    {
        g_prefix_error (error, "TreeView '%s': Failed to get history; "
                "couldn't add property 'history-direction': ",
                priv->name);
        g_value_unset (&v);
        g_object_unref (node);
        return NULL;
    }

    g_value_set_uint (&v, nb);
    if (G_UNLIKELY (!donna_node_add_property (node, "history-pos",
                    G_TYPE_UINT, &v,
                    DONNA_TASK_VISIBILITY_INTERNAL_FAST,
                    NULL, (refresher_fn) gtk_true,
                    NULL,
                    NULL, NULL,
                    error)))
    {
        g_prefix_error (error, "TreeView '%s': Failed to get history; "
                "couldn't add property 'history-pos': ",
                priv->name);
        g_value_unset (&v);
        g_object_unref (node);
        return NULL;
    }
    g_value_unset (&v);

    g_value_init (&v, G_TYPE_ICON);
    g_value_take_object (&v, g_themed_icon_new ((direction == DONNA_HISTORY_BACKWARD)
                ? "go-previous" : "go-next"));
    donna_node_add_property (node, "menu-image-selected",
            G_TYPE_ICON, &v,
            DONNA_TASK_VISIBILITY_INTERNAL_FAST,
            NULL, (refresher_fn) gtk_true,
            NULL,
            NULL, NULL,
            NULL);
    g_value_unset (&v);

    return node;
}

/* mode list only */
/**
 * donna_tree_view_history_get:
 * @tree: A #DonnaTreeView
 * @direction: Direction(s) to look into @tree's history
 * @nb: How many items to return
 * @error: (allow-none): Return location of a #GError, or %NULL
 *
 * Returns nodes representing items from @tree's history, e.g. to show in a
 * popup menu.
 *
 * This items returned will be ordered from oldest to most recent, unless
 * @direction was set to only %DONNA_HISTORY_BACKWARD in which case they'll be
 * ordered from most recent to oldest.
 *
 * If @direction was set to both direction, then the current location will also
 * be included in between the two, as one might expect.
 *
 * This is obviously only supported on lists, as trees don't have an history.
 *
 * Returns: (transfer container) (element-type DonnaNode): Array of nodes from
 * @tree's history, or %NULL
 */
GPtrArray *
donna_tree_view_history_get (DonnaTreeView          *tree,
                             DonnaHistoryDirection   direction,
                             guint                   nb,
                             GError                **error)
{
    DonnaTreeViewPrivate *priv;
    DonnaProviderInternal *pi;
    DonnaNode *node;
    GPtrArray *arr;
    gchar **items, **s;
    gchar *name;
    guint pos;

    g_return_val_if_fail (DONNA_IS_TREE_VIEW (tree), NULL);
    priv = tree->priv;

    if (priv->is_tree)
    {
        g_set_error (error, DONNA_TREE_VIEW_ERROR,
                DONNA_TREE_VIEW_ERROR_INVALID_MODE,
                "TreeView '%s': No history in mode Tree",
                priv->name);
        return FALSE;
    }

    if (!(direction & (DONNA_HISTORY_BACKWARD | DONNA_HISTORY_FORWARD)))
    {
        g_set_error (error, DONNA_TREE_VIEW_ERROR,
                DONNA_TREE_VIEW_ERROR_OTHER,
                "TreeView '%s': Cannot get history, no valid direction(s) given",
                priv->name);
        return NULL;
    }

    pi = (DonnaProviderInternal *) donna_app_get_provider (priv->app, "internal");
    if (G_UNLIKELY (!pi))
    {
        g_set_error (error, DONNA_TREE_VIEW_ERROR,
                DONNA_TREE_VIEW_ERROR_OTHER,
                "TreeView '%s': Cannot get history, failed to get provider 'internal'",
                priv->name);
        return NULL;
    }

    arr = g_ptr_array_new_with_free_func (g_object_unref);

    if (direction & DONNA_HISTORY_BACKWARD)
    {
        items = donna_history_get_items (priv->history, DONNA_HISTORY_BACKWARD,
                nb, error);
        if (G_UNLIKELY (!items))
        {
            g_prefix_error (error, "TreeView '%s': Failed to get history: ",
                    priv->name);
            g_ptr_array_unref (arr);
            g_object_unref (pi);
            return NULL;
        }

        for (s = items, pos = 0; *s; ++s)
            ++pos;

        /* we got items from oldest to most recent. This is the order we want to
         * preserve if we're also showing FORWARD, so it all makes sense.
         * However, if only showing BACKWARD we shall reverse them, as then the
         * expectation would be to have the most recent first */
        if (direction & DONNA_HISTORY_FORWARD)
            /* reset to first */
            s = items;
        else if (s > items)
            /* back to last (unless first == last == NULL, i.e. no items) */
            --s;

        while (*s)
        {
            if (streqn (*s, "fs:", 3))
                name = *s + 3;
            else
                name = *s;

            node = get_node_for_history (tree, pi, name,
                    DONNA_HISTORY_BACKWARD, pos--, error);
            if (G_UNLIKELY (!node))
            {
                g_strfreev (items);
                g_ptr_array_unref (arr);
                g_object_unref (pi);
                return NULL;
            }
            g_ptr_array_add (arr, node);

            if (direction & DONNA_HISTORY_FORWARD)
                ++s;
            /* got back to the first item? */
            else if (s == items)
                break;
            else
                --s;
        }
        g_strfreev (items);

        /* if there's also forward, we add the current location on the list */
        if (direction & DONNA_HISTORY_FORWARD)
        {
            name = (gchar *) donna_history_get_item (priv->history,
                    DONNA_HISTORY_BACKWARD, 0, error);
            if (G_UNLIKELY (!name))
            {
                g_prefix_error (error, "TreeView '%s': Failed to get history; "
                        "couldn't get item: ",
                        priv->name);
                g_ptr_array_unref (arr);
                g_object_unref (pi);
                return NULL;
            }

            if (streqn (name, "fs:", 3))
                name += 3;

            node = get_node_for_history (tree, pi, name, 0, 0, error);
            if (G_UNLIKELY (!node))
            {
                g_ptr_array_unref (arr);
                g_object_unref (pi);
                return NULL;
            }
            g_ptr_array_add (arr, node);
        }
    }

    if (direction & DONNA_HISTORY_FORWARD)
    {
        items = donna_history_get_items (priv->history, DONNA_HISTORY_FORWARD,
                nb, error);
        if (G_UNLIKELY (!items))
        {
            g_prefix_error (error, "TreeView '%s': Failed to get history: ",
                    priv->name);
            g_ptr_array_unref (arr);
            g_object_unref (pi);
            return NULL;
        }

        pos = 0;
        for (s = items; *s; ++s)
        {
            if (streqn (*s, "fs:", 3))
                name = *s + 3;
            else
                name = *s;

            node = get_node_for_history (tree, pi, name,
                    DONNA_HISTORY_FORWARD, ++pos, error);
            if (G_UNLIKELY (!node))
            {
                g_strfreev (items);
                g_ptr_array_unref (arr);
                g_object_unref (pi);
                return NULL;
            }
            g_ptr_array_add (arr, node);
        }
        g_strfreev (items);
    }

    g_object_unref (pi);
    return arr;
}

/* mode list only */
/**
 * donna_tree_view_history_get_node:
 * @tree: A #DonnaTreeView
 * @direction: Direction to look into @tree's history
 * @nb: How many steps to go into history
 * @error: (allow-none): Return location of a #GError, or %NULL
 *
 * Returns the node of the @nb-th item from @tree's history going @direction
 *
 * Note that the returned node isn't that of the location, but an internal node
 * from @tree's history. You can however still use it via
 * donna_tree_view_set_location() to change location & history accordingly.
 *
 * This is obviously only supported on lists, as trees don't have an history.
 *
 * Returns: (transfer full): The #DonnaNode for the corresponding history item,
 * or %NULL
 */
DonnaNode *
donna_tree_view_history_get_node (DonnaTreeView          *tree,
                                  DonnaHistoryDirection   direction,
                                  guint                   nb,
                                  GError                **error)
{
    DonnaTreeViewPrivate *priv;
    DonnaProviderInternal *pi;
    DonnaNode *node;
    const gchar *item;

    g_return_val_if_fail (DONNA_IS_TREE_VIEW (tree), NULL);
    priv = tree->priv;

    if (priv->is_tree)
    {
        g_set_error (error, DONNA_TREE_VIEW_ERROR,
                DONNA_TREE_VIEW_ERROR_INVALID_MODE,
                "TreeView '%s': No history in mode Tree",
                priv->name);
        return FALSE;
    }

    pi = (DonnaProviderInternal *) donna_app_get_provider (priv->app, "internal");
    if (G_UNLIKELY (!pi))
    {
        g_set_error (error, DONNA_TREE_VIEW_ERROR,
                DONNA_TREE_VIEW_ERROR_OTHER,
                "TreeView '%s': Cannot get history node, "
                "failed to get provider 'internal'",
                priv->name);
        return NULL;
    }

    item = donna_history_get_item (priv->history, direction, nb, error);
    if (!item)
    {
        g_prefix_error (error, "TreeView '%s': Failed getting history node: ",
                priv->name);
        g_object_unref (pi);
        return NULL;
    }

    node = get_node_for_history (tree, pi,
            (streqn ("fs:", item, 3)) ? item + 3 : item,
            direction, nb, error);
    g_object_unref (pi);
    return node;
}

/* mode list only */
/**
 * donna_tree_view_history_move:
 * @tree: A #DonnaTreeView
 * @direction: Direction to look into @tree's history
 * @nb: How many steps to go into history
 * @error: (allow-none): Return location of a #GError, or %NULL
 *
 * Set current location by moving @nb steps into @tree's history going
 * @direction
 *
 * This is obviously only supported on lists, as trees don't have an history.
 *
 * Returns: %TRUE is location change was initiated; else %FALSE
 */
gboolean
donna_tree_view_history_move (DonnaTreeView         *tree,
                              DonnaHistoryDirection  direction,
                              guint                  nb,
                              GError               **error)
{
    DonnaTreeViewPrivate *priv;
    DonnaNode *node;
    const gchar *fl;
    struct history_move hm = { CL_EXTRA_HISTORY_MOVE, direction, nb };

    g_return_val_if_fail (DONNA_IS_TREE_VIEW (tree), FALSE);
    priv = tree->priv;

    if (priv->is_tree)
    {
        g_set_error (error, DONNA_TREE_VIEW_ERROR,
                DONNA_TREE_VIEW_ERROR_INVALID_MODE,
                "TreeView '%s': No history in mode Tree",
                priv->name);
        return FALSE;
    }

    fl = donna_history_get_item (priv->history, direction, nb, error);
    if (!fl)
    {
        g_prefix_error (error, "TreeView '%s': Failed to move in history: ",
                priv->name);
        return FALSE;
    }

    node = donna_app_get_node (priv->app, fl, FALSE, error);
    if (!node)
    {
        g_prefix_error (error, "TreeView '%s': Failed to move in history: ",
                priv->name);
        return FALSE;
    }

    if (!change_location (tree, CHANGING_LOCATION_ASKED, node, &hm, error))
    {
        g_prefix_error (error, "TreeView '%s': Failed to move in history: ",
                priv->name);
        return FALSE;
    }
    return TRUE;
}

/* mode list only */
/**
 * donna_tree_view_history_clear:
 * @tree: A #DonnaTreeView
 * @direction: Direction(s) to look into @tree's history
 * @error: (allow-none): Return location of a #GError, or %NULL
 *
 * Clears @tree's history going @direction
 *
 * Set @direction to %DONNA_HISTORY_BOTH to clear the entire history.
 *
 * This is obviously only supported on lists, as trees don't have an history.
 *
 * Returns: %TRUE on success, else %FALSE
 */
gboolean
donna_tree_view_history_clear (DonnaTreeView        *tree,
                               DonnaHistoryDirection direction,
                               GError              **error)
{
    DonnaTreeViewPrivate *priv;

    g_return_val_if_fail (DONNA_IS_TREE_VIEW (tree), FALSE);
    priv = tree->priv;

    if (priv->is_tree)
    {
        g_set_error (error, DONNA_TREE_VIEW_ERROR,
                DONNA_TREE_VIEW_ERROR_INVALID_MODE,
                "TreeView '%s': No history in mode Tree",
                priv->name);
        return FALSE;
    }

    donna_history_clear (priv->history, direction);
    return TRUE;
}

/**
 * donna_tree_view_get_node_up:
 * @tree: A #DonnaTreeView
 * @level: Level wanted
 * @error: (allow-none): Return location of a #GError, or %NULL
 *
 * Returns a node that is the @level-nth ascendant of current location
 *
 * This is obviously only possible if there is a current location in a non-flat
 * domain (e.g. "fs" or "config"). If @level is too high or negative, the node
 * of the root of the domain will be returned.
 *
 * Returns: (transfer full): A #DonnaNode, @level-nth ascendant of current
 * location, or %NULL
 */
DonnaNode *
donna_tree_view_get_node_up (DonnaTreeView      *tree,
                             gint                level,
                             GError            **error)
{
    DonnaTreeViewPrivate *priv;
    DonnaNode *node;
    gchar *fl = NULL;
    gchar *location;
    gchar *s;

    g_return_val_if_fail (DONNA_IS_TREE_VIEW (tree), NULL);
    priv = tree->priv;

    if (!priv->location)
    {
        g_set_error (error, DONNA_TREE_VIEW_ERROR,
                DONNA_TREE_VIEW_ERROR_NOT_FOUND,
                "TreeView '%s': Can't get node 'up', no current location set",
                priv->name);
        return NULL;
    }

    if (donna_provider_get_flags (donna_node_peek_provider (priv->location))
            & DONNA_PROVIDER_FLAG_FLAT)
    {
        g_set_error (error, DONNA_TREE_VIEW_ERROR,
                DONNA_TREE_VIEW_ERROR_FLAT_PROVIDER,
                "TreeView '%s': Can't get node 'up', current location is in flat provider",
                priv->name);
        return NULL;
    }

    /* if we're on root, trying to go up is a no-op, not an error */
    fl = donna_node_get_full_location (priv->location);
    location = strchr (fl, ':') + 1;
    if (streq (location, "/"))
    {
        g_free (fl);
        g_set_error (error, DONNA_TREE_VIEW_ERROR,
                DONNA_TREE_VIEW_ERROR_NOT_FOUND,
                "TreeView '%s': Can't get node 'up', we're in root already",
                priv->name);
        return NULL;
    }

    if (level > 0)
    {
        gint nb;

        /* turn into the parent location */
        for (s = location + 1, nb = 1; *s != '\0'; ++s)
            if (*s == '/')
                ++nb;
        if (level >= nb)
            /* go to root */
            *++location = '\0';
        else
        {
            for ( ; s > location; --s)
            {
                if (*s == '/' && --level <= 0)
                {
                    *s = '\0';
                    break;
                }
            }
        }
    }
    else
        /* go to root */
        *++location = '\0';

    node = donna_app_get_node (priv->app, fl, FALSE, error);
    g_free (fl);
    if (!node)
    {
        g_prefix_error (error, "TreeView '%s': Can't get node to go up: ",
                priv->name);
        return NULL;
    }

    return node;
}

static void
free_go_up (struct cl_go_up *data)
{
    g_object_unref (data->node);
    g_free (data);
}

static void
go_up_cb (DonnaTreeView *tree, struct cl_go_up *data)
{
    DonnaTreeViewPrivate *priv = tree->priv;
    GtkTreeIter *iter;

    iter = g_hash_table_lookup (priv->hashtable, data->node);
    if (G_LIKELY (iter))
    {
        GtkTreePath *path;

        path = gtk_tree_model_get_path ((GtkTreeModel *) priv->store, iter);

        if (data->set & DONNA_TREE_VIEW_SET_FOCUS)
            gtk_tree_view_set_focused_row ((GtkTreeView *) tree, path);
        if (data->set & DONNA_TREE_VIEW_SET_SCROLL)
            scroll_to_iter (tree, iter);
        if (data->set & DONNA_TREE_VIEW_SET_CURSOR)
        {
            if (!(data->set & DONNA_TREE_VIEW_SET_FOCUS))
                    gtk_tree_view_set_focused_row ((GtkTreeView *) tree, path);
            gtk_tree_selection_select_path (
                    gtk_tree_view_get_selection ((GtkTreeView *) tree), path);
        }

        gtk_tree_path_free (path);
    }
    free_go_up (data);
}

/**
 * donna_tree_view_go_up:
 * @tree: A #DonnaTreeView
 * @level: Level wanted
 * @set: For lists only: What to set on child
 * @error: (allow-none): Return location of a #GError, or %NULL
 *
 * Changes location to the @level-nth ascendant of current location
 *
 * See donna_tree_view_get_node_up() for how the node (for new current
 * location) is obtained
 *
 * For lists only, @set defines what will be set after location change on the
 * origin child. That is, once in the new location, the focus, scroll and/or
 * cursor can be set of the child that was the previous location (or its
 * ascendant).
 *
 * Returns: %TRUE if the location change was initiated, or if no nodes to go to
 * were found; else %FALSE
 */
gboolean
donna_tree_view_go_up (DonnaTreeView      *tree,
                       gint                level,
                       DonnaTreeViewSet    set,
                       GError            **error)
{
    GError *err = NULL;
    DonnaNode *node;
    gboolean ret;

    node = donna_tree_view_get_node_up (tree, level, &err);
    if (!node)
    {
        if (g_error_matches (err, DONNA_TREE_VIEW_ERROR,
                    DONNA_TREE_VIEW_ERROR_NOT_FOUND))
        {
            /* even though there's no location to go up to (in root already),
             * this is a no-op, not an error */
            g_clear_error (&err);
            return TRUE;
        }
        else
            g_propagate_error (error, err);

        return FALSE;
    }

    if (!tree->priv->is_tree)
    {
        struct cl_go_up *data;

        data = g_new0 (struct cl_go_up, 1);
        data->type      = CL_EXTRA_CALLBACK;
        data->callback  = (change_location_callback_fn) go_up_cb;
        data->data      = data;
        data->destroy   = (GDestroyNotify) free_go_up;

        data->set       = set;
        if (level > 1)
            data->node  = donna_tree_view_get_node_up (tree, level - 1, NULL);
        else
            data->node  = g_object_ref (tree->priv->location);

        /* same as donna_tree_view_set_location() only with our callback */
        ret = change_location (tree, CHANGING_LOCATION_ASKED, node, data, error);
    }
    else
        ret = donna_tree_view_set_location (tree, node, error);

    g_object_unref (node);
    return ret;
}

/* mode list only (history based) */
/**
 * donna_tree_view_get_node_down:
 * @tree: A #DonnaTreeView
 * @level: Level wanted
 * @error: (allow-none): Return location of a #GError, or %NULL
 *
 * Returns a node from @tree's history that is a descendant of current location.
 *
 * History is looked first in forward direction, then backward, always from most
 * to less recent. If a descendant is found, but isn't @level down, then the
 * search continues. Either a matching descendant will be found and returned,
 * else the closest (to @level) will be returned. (If more than one descendant
 * at the same level were found, the first match will be returned.)
 *
 * This is only supported for lists, as this is history-based (and trees don't
 * have history). It is also only possible if the current location is in a
 * non-flat domain (e.g. "fs" or "config").
 *
 * Returns: (transfer full): A #DonnaNode descendant of current location, or
 * %NULL
 */
DonnaNode *
donna_tree_view_get_node_down (DonnaTreeView      *tree,
                               gint                level,
                               GError            **error)
{
    DonnaTreeViewPrivate *priv;
    DonnaHistoryDirection direction;
    gchar **items_f = NULL;
    gchar **items;
    gchar **item;
    gchar *fl;
    gsize len;
    gint is_root;
    gchar *best = NULL;
    gint lvl = 0;

    g_return_val_if_fail (DONNA_IS_TREE_VIEW (tree), NULL);
    priv = tree->priv;

    if (priv->is_tree)
    {
        g_set_error (error, DONNA_TREE_VIEW_ERROR,
                DONNA_TREE_VIEW_ERROR_INVALID_MODE,
                "TreeView '%s': Can't get node 'down' in mode Tree (requires history)",
                priv->name);
        return NULL;
    }

    if (G_UNLIKELY (!priv->location))
    {
        g_set_error (error, DONNA_TREE_VIEW_ERROR,
                DONNA_TREE_VIEW_ERROR_NOT_FOUND,
                "TreeView '%s': Can't get node 'down', no current location set",
                priv->name);
        return NULL;
    }

    if (donna_provider_get_flags (donna_node_peek_provider (priv->location))
            & DONNA_PROVIDER_FLAG_FLAT)
    {
        g_set_error (error, DONNA_TREE_VIEW_ERROR,
                DONNA_TREE_VIEW_ERROR_FLAT_PROVIDER,
                "TreeView '%s': Can't get node 'down', current location is in flat provider",
                priv->name);
        return NULL;
    }

    fl = donna_node_get_full_location (priv->location);
    len = strlen (fl);
    if (*(strchr (fl, '/') + 1) == '\0')
        is_root = 1;
    else
        is_root = 0;

    direction = DONNA_HISTORY_FORWARD;
    for (;;)
    {
        items = donna_history_get_items (priv->history, direction, 0, error);
        if (!items)
        {
            g_prefix_error (error, "TreeView '%s': Can't get node 'down': ", priv->name);
            g_free (fl);
            g_strfreev (items_f);
            return NULL;
        }

        if (direction == DONNA_HISTORY_BACKWARD)
        {
            /* go to last item, since we want to process them from most recent
             * to oldest, i.e. in reverse order */
            for (item = items; *item; ++item)
                ;
        }
        else /* DONNA_HISTORY_FORWARD */
            item = items - 1;

        for (;;)
        {
            if (direction == DONNA_HISTORY_BACKWARD)
            {
                if (--item < items)
                    break;
            }
            else /* DONNA_HISTORY_FORWARD */
                if (!*++item)
                    break;

            /* item starts list current location */
            if (streqn (*item, fl, len)
                    /* if we're root, make sure it is not (i.e. is a child) */
                    && ((is_root && (*items)[len] != '\0')
                        /* if we're not, we sure it is a child */
                        || (!is_root && (*item)[len] == '/')))
            {
                gchar *s;
                gint i = 0;

                /* the '/' in item that's after our location */
                s = *item + len - is_root;
                for (;;)
                {
                    s = strchr (s + 1, '/');
                    if (++i >= level || !s)
                        break;
                }
                if (s)
                    *s = '\0';

                if (i > lvl)
                {
                    lvl = i;
                    best = *item;
                }

                if (lvl >= level)
                    goto found;
            }
        }

        if (direction == DONNA_HISTORY_FORWARD)
        {
            if (best)
                items_f = items;
            else
                g_strfreev (items);
            direction = DONNA_HISTORY_BACKWARD;
        }
        else
            break;
    }

    if (best)
    {
        DonnaNode *node;

found:
        node = donna_app_get_node (priv->app, best, FALSE, error);
        g_strfreev (items);
        g_strfreev (items_f);
        g_free (fl);
        if (!node)
        {
            g_prefix_error (error, "TreeView '%s': Can't get node to go down: ",
                    priv->name);
            return NULL;
        }

        return node;
    }

    g_strfreev (items);
    g_strfreev (items_f);
    g_free (fl);

    g_set_error (error, DONNA_TREE_VIEW_ERROR,
            DONNA_TREE_VIEW_ERROR_NOT_FOUND,
            "TreeView '%s': No node 'down' could be found",
            priv->name);
    return NULL;
}

/* mode list only (history based) */
/**
 * donna_tree_view_go_down:
 * @tree: A #DonnaTreeView
 * @level: Level wanted
 * @error: (allow-none): Return location of a #GError, or %NULL
 *
 * Changes location to that from @tree's history that is a descendant of current
 * location.
 *
 * See donna_tree_view_get_node_down() for how the node (for new current
 * location) is obtained
 *
 * This is only supported for lists, as this is history-based (and trees don't
 * have history).
 *
 * Returns: %TRUE if the location change was initiated, or if no nodes to go to
 * were found; else %FALSE
 */
gboolean
donna_tree_view_go_down (DonnaTreeView      *tree,
                         gint                level,
                         GError            **error)
{
    GError *err = NULL;
    DonnaNode *node;
    gboolean ret;

    node = donna_tree_view_get_node_down (tree, level, &err);
    if (!node)
    {
        if (g_error_matches (err, DONNA_TREE_VIEW_ERROR,
                    DONNA_TREE_VIEW_ERROR_NOT_FOUND))
        {
            /* even though there's no location to go down to, this is a no-op,
             * not an error */
            g_clear_error (&err);
            return TRUE;
        }
        else
            g_propagate_error (error, err);

        return FALSE;
    }

    ret = donna_tree_view_set_location (tree, node, error);
    g_object_unref (node);
    return ret;
}

static GPtrArray *
context_get_selection (struct conv *conv, GError **error)
{
    DonnaTreeView *tree = conv->tree;

    if (!conv->selection)
    {
        GError *err = NULL;

        conv->selection = donna_tree_view_get_selected_nodes (tree, &err);
        if (!conv->selection)
        {
            if (err)
                g_propagate_error (error, err);
            else
                /* it returns NULL if there's no selection, but sets an error.
                 * Neither means no selection, which here is an error */
                g_set_error (error, DONNA_TREE_VIEW_ERROR,
                        DONNA_TREE_VIEW_ERROR_OTHER,
                        "TreeView '%s': No selection", tree->priv->name);
        }
    }

    return conv->selection;
}

static gchar *
tree_context_get_alias (const gchar             *alias,
                        const gchar             *extra,
                        DonnaContextReference    reference,
                        DonnaContext            *context,
                        GError                 **error)
{
    struct conv *conv = context->data;
    DonnaTreeView *tree = conv->tree;
    DonnaTreeViewPrivate *priv = tree->priv;

    if (streqn (alias, "column.", 7))
    {
        struct column *_col;
        gchar buf[255], *b = buf;
        gchar *s;
        gchar *ret;

        alias += 7;
        s = strchr (alias, '.');
        if (!s)
        {
            g_set_error (error, DONNA_CONTEXT_MENU_ERROR,
                    DONNA_CONTEXT_MENU_ERROR_UNKNOWN_ALIAS,
                    "TreeView '%s': No such alias: '%s'",
                    priv->name, alias - 4);
            return NULL;
        }

        *s = '\0';
        _col = get_column_by_name (tree, alias);
        *s = '.';
        if (!_col)
        {
            g_set_error (error, DONNA_CONTEXT_MENU_ERROR,
                    DONNA_CONTEXT_MENU_ERROR_UNKNOWN_ALIAS,
                    "TreeView '%s': No such alias: '%s' (no such column)",
                    priv->name, alias - 7);
            return NULL;
        }

        if (G_UNLIKELY (snprintf (buf, 255, ":column.%s.", _col->name) >= 255))
            b = g_strdup_printf (":column.%s.", _col->name);

        ret = donna_column_type_get_context_alias (_col->ct,
                _col->ct_data,
                s + 1,
                extra,
                reference,
                (conv->row) ? conv->row->node : NULL,
                (get_sel_fn) context_get_selection,
                (reference & DONNA_CONTEXT_HAS_SELECTION) ? conv : NULL,
                b,
                error);

        if (G_UNLIKELY (b != buf))
            g_free (b);
        return ret;
    }
    else if (streq (alias, "column_options"))
    {
        struct column *_col;
        gchar buf[255], *b = buf;
        gchar *ret;
        gchar *s;

        if (!conv->col_name)
            return (gchar *) "";

        _col = get_column_by_name (tree, conv->col_name);
        if (G_UNLIKELY (!_col))
        {
            g_set_error (error, DONNA_CONTEXT_MENU_ERROR,
                    DONNA_CONTEXT_MENU_ERROR_UNKNOWN_ALIAS,
                    "TreeView '%s': Can't resolve alias 'column_options': "
                    "Failed to get column '%s' -- This is not supposed to happen!",
                    priv->name, conv->col_name);
            return NULL;
        }

        if (G_UNLIKELY (snprintf (buf, 255, ":column.%s.", _col->name) >= 255))
            b = g_strdup_printf (":column.%s.", _col->name);

        ret = donna_column_type_get_context_alias (_col->ct,
                _col->ct_data,
                "options",
                extra,
                reference,
                (conv->row) ? conv->row->node : NULL,
                (get_sel_fn) context_get_selection,
                (reference & DONNA_CONTEXT_HAS_SELECTION) ? conv : NULL,
                b,
                error);

        if (G_UNLIKELY (b != buf))
            g_free (b);

        s = g_strconcat (":column.", _col->name, ".title,"
                ":column.", _col->name, ".width,"
                ":column.", _col->name, ".refresh_properties<"
                    ":column.", _col->name, ".refresh_properties:visible,",
                    ":column.", _col->name, ".refresh_properties:preload,",
                    ":column.", _col->name, ".refresh_properties:on_demand>",
                ",-,", ret, NULL);
        g_free (ret);
        return s;
    }
    else if (streq (alias, "column_edit"))
    {
        if (priv->columns)
        {
            GString *str = g_string_new (NULL);
            GSList *l;

            for (l = priv->columns; l; l = l->next)
            {
                g_string_append (str, ":column_edit.");
                g_string_append (str, ((struct column *) l->data)->name);
                g_string_append_c (str, ',');
            }
            g_string_truncate (str, str->len - 1);
            return g_string_free (str, FALSE);
        }
        else
            return (gchar *) "";
    }
    else if (streq (alias, "columns"))
    {
        GString *str = g_string_new (NULL);
        GPtrArray *arr = NULL;
        GSList *l;
        guint i;

        if (donna_config_list_options (donna_app_peek_config (priv->app), &arr,
                    DONNA_CONFIG_OPTION_TYPE_CATEGORY, "defaults/%s/columns",
                    (priv->is_tree) ? "trees" : "lists"))
            for (i = 0; i < arr->len; ++i)
                donna_g_string_append_concat (str, ":columns:", arr->pdata[i], ",", NULL);

        /* make sure all columns used are listed. In case some columns aren't
         * defined at all in defaults */
        for (l = priv->columns; l; l = l->next)
        {
            struct column *_col = l->data;
            if (!arr || !donna_g_ptr_array_contains (arr,
                        _col->name, (GCompareFunc) strcmp))
                donna_g_string_append_concat (str, ":columns:", _col->name, ",", NULL);
        }
        g_string_truncate (str, str->len - 1);

        if (arr)
            g_ptr_array_unref (arr);
        return g_string_free (str, FALSE);
    }
    else if (streq (alias, "new_nodes"))
    {
        gchar buf[255], *b = buf;
        gchar *ret;

        if (!priv->location)
            return (gchar *) "";

        if (G_UNLIKELY (snprintf (buf, 255, ":domain.%s.",
                        donna_node_get_domain (priv->location)) >= 255))
            b = g_strdup_printf (":domain.%s.", donna_node_get_domain (priv->location));

        ret = donna_provider_get_context_alias_new_nodes (
                donna_node_peek_provider (priv->location), extra,
                priv->location, buf, error);

        if (G_UNLIKELY (b != buf))
            g_free (b);
        return ret;
    }
    else if (streq (alias, "sort_order") || streq (alias, "second_sort_order"))
    {
        GString *str;
        GList *list, *l;

        if (!priv->columns)
            return (gchar *) "";

        str = g_string_new (NULL);
        /* get them from treeview to preserve the current order */
        list = gtk_tree_view_get_columns ((GtkTreeView *) tree);
        for (l = list; l; l = l->next)
        {
            struct column *_col;

            _col = get_column_by_column (tree, l->data);
            /* blankcol */
            if (!_col)
                continue;
            /* skip line-number -- can't really sort by that one */
            if (_col->ct == (DonnaColumnType *) tree)
                continue;

            g_string_append_c (str, ':');
            g_string_append (str, alias);
            g_string_append_c (str, ':');
            g_string_append (str, _col->name);
            g_string_append_c (str, ',');
        }
        g_list_free (list);
        g_string_truncate (str, str->len - 1);
        return g_string_free (str, FALSE);
    }
    else if (streq (alias, "tv_options"))
    {
        DonnaConfig *config = donna_app_peek_config (priv->app);
        GString *str;
        GPtrArray *arr; /* to get list of key/click modes */

        if (!extra)
            extra = "";
        else if (!(streq (extra, "memory") || streq (extra, "current")
                || streq (extra, "ask") || streq (extra, "tree")
                || streq (extra, "default") || streq (extra, "save-location")))
        {
            g_set_error (error, DONNA_CONTEXT_MENU_ERROR,
                    DONNA_CONTEXT_MENU_ERROR_OTHER,
                    "TreeView '%s': Invalid extra (save_location) '%s' for alias '%s'",
                    priv->name, extra, alias);
            return FALSE;
        }

        str = g_string_new (NULL);
        donna_g_string_append_concat (str,
                ":tv_options.show_hidden:@", extra, ",", NULL);
        if (!_donna_context_add_items_for_extra (str, config,
                    "sg", DONNA_CONFIG_EXTRA_TYPE_LIST_INT,
                    ":tv_options.", "sort_groups", extra, error))
        {
            g_prefix_error (error, "TreeView '%s': Failed to process item '%s': ",
                    priv->name, "tv_options.sort_groups");
            g_string_free (str, TRUE);
            return FALSE;
        }
#ifdef GTK_IS_JJK
        g_string_append_c (str, ',');
        if (!_donna_context_add_items_for_extra (str, config,
                    "highlight", DONNA_CONFIG_EXTRA_TYPE_LIST_INT,
                    ":tv_options.", "select_highlight", extra, error))
        {
            g_prefix_error (error, "TreeView '%s': Failed to process item '%s': ",
                    priv->name, "tv_options.select_highlight");
            g_string_free (str, TRUE);
            return FALSE;
        }
#endif
        if (priv->is_tree)
        {
            donna_g_string_append_concat (str, ",-,"
                    ":tv_options.is_minitree:@", extra, ","
                    ":tv_options.sync<"
                        ":tv_options.sync_with<"
                            ":tv_options.sync_with.active:@", extra, ","
                            ":tv_options.sync_with.custom:@", extra, ",-,"
                            ":tv_options.auto_focus_sync:@", extra, ">,",
                            NULL);
            if (!_donna_context_add_items_for_extra (str, config,
                        "sync", DONNA_CONFIG_EXTRA_TYPE_LIST_INT,
                        ":tv_options.", "sync_mode", extra, error))
            {
                g_prefix_error (error, "TreeView '%s': Failed to process item '%s': ",
                        priv->name, "tv_options.sync_mode");
                g_string_free (str, TRUE);
                return FALSE;
            }
            /* add sync_scroll *inside* the submenu */
            g_string_truncate (str, str->len - 1);
            donna_g_string_append_concat (str, ",-,"
                    ":tv_options.sync_scroll:@", extra, ">>", NULL);

            g_string_append_c (str, ',');
            if (!_donna_context_add_items_for_extra (str, config,
                        "visuals", DONNA_CONFIG_EXTRA_TYPE_LIST_FLAGS,
                        ":tv_options.", "node_visuals", extra, error))
            {
                g_prefix_error (error, "TreeView '%s': Failed to process item '%s': ",
                        priv->name, "tv_options.node_visuals");
                g_string_free (str, TRUE);
                return FALSE;
            }
        }
        else
            donna_g_string_append_concat (str, ",-,"
                    ":tv_options.vf_items_only:@", extra, ",",
                    ":tv_options.focusing_click:@", extra, ",",
                    ":tv_options.goto_item_set<"
                        ":tv_options.goto_item_set.scroll:@", extra, ",",
                        ":tv_options.goto_item_set.focus:@", extra, ",",
                        ":tv_options.goto_item_set.cursor:@", extra, ">",
                        NULL);

        g_string_append (str, ",-,");
        if (!_donna_context_add_items_for_extra (str, config,
                    "node-type", DONNA_CONFIG_EXTRA_TYPE_LIST_INT,
                    ":tv_options.", "node_types", extra, error))
        {
            g_prefix_error (error, "TreeView '%s': Failed to process item '%s': ",
                    priv->name, "tv_options.node_types");
            g_string_free (str, TRUE);
            return FALSE;
        }

        arr = NULL;
        donna_g_string_append_concat (str, ",-,:tv_options.key_mode:@", extra, NULL);
        if (donna_config_list_options (donna_app_peek_config (priv->app),
                    &arr, DONNA_CONFIG_OPTION_TYPE_CATEGORY, "key_modes"))
        {
            guint i;

            g_string_append_c (str, '<');
            for (i = 0; i < arr->len; ++i)
                donna_g_string_append_concat (str, (i > 0) ? "," : "",
                        ":tv_options.key_mode:@", extra, ":", arr->pdata[i], NULL);
            g_string_append_c (str, '>');
            g_ptr_array_unref (arr);
        }

        arr = NULL;
        donna_g_string_append_concat (str, ",:tv_options.click_mode:@", extra, NULL);
        if (donna_config_list_options (donna_app_peek_config (priv->app),
                    &arr, DONNA_CONFIG_OPTION_TYPE_CATEGORY, "click_modes"))
        {
            guint i;

            g_string_append_c (str, '<');
            for (i = 0; i < arr->len; ++i)
                donna_g_string_append_concat (str, (i > 0) ? "," : "",
                        ":tv_options.click_mode:@", extra, ":", arr->pdata[i], NULL);
            g_string_append_c (str, '>');
            g_ptr_array_unref (arr);
        }

        g_string_append (str, ",-,");
        if (!_donna_context_add_items_for_extra (str, config,
                    "save-location", DONNA_CONFIG_EXTRA_TYPE_LIST_INT,
                    ":tv_options.", "default_save_location", extra, error))
        {
            g_prefix_error (error, "TreeView '%s': Failed to process item '%s': ",
                    priv->name, "tv_options.default_save_location");
            g_string_free (str, TRUE);
            return FALSE;
        }

        return g_string_free (str, FALSE);
    }

    g_set_error (error, DONNA_CONTEXT_MENU_ERROR,
            DONNA_CONTEXT_MENU_ERROR_UNKNOWN_ALIAS,
            "Unknown internal alias '%s'", alias);
    return NULL;
}

static gboolean
tree_context_get_item_info (const gchar             *item,
                            const gchar             *extra,
                            DonnaContextReference    reference,
                            DonnaContext            *context,
                            DonnaContextInfo        *info,
                            GError                 **error)
{
    struct conv *conv = context->data;
    DonnaTreeView *tree = conv->tree;
    DonnaTreeViewPrivate *priv = tree->priv;

    if (streqn (item, "column.", 7))
    {
        struct column *_col;
        gchar *s;

        item += 7;
        s = strchr (item, '.');
        if (!s)
        {
            g_set_error (error, DONNA_CONTEXT_MENU_ERROR,
                    DONNA_CONTEXT_MENU_ERROR_UNKNOWN_ITEM,
                    "TreeView '%s': No such item: '%s'",
                    priv->name, item - 4);
            return FALSE;
        }

        *s = '\0';
        _col = get_column_by_name (tree, item);
        *s = '.';
        if (!_col)
        {
            g_set_error (error, DONNA_CONTEXT_MENU_ERROR,
                    DONNA_CONTEXT_MENU_ERROR_UNKNOWN_ITEM,
                    "TreeView '%s': No such item: '%s' (no such column)",
                    priv->name, item - 7);
            return FALSE;
        }

        if (streq (s + 1, "title"))
        {
            const gchar *title = gtk_tree_view_column_get_title (_col->column);

            info->is_visible = info->is_sensitive = TRUE;
            info->name = g_strconcat ("Title: ", title, NULL);
            info->free_name = TRUE;
            info->trigger = g_strconcat ("command:tv_column_set_option (%o,",
                    _col->name, ",title,@ask_text(Enter the new column title,,",
                    title,"))", NULL);
            info->free_trigger = TRUE;
            return TRUE;
        }
        else if (streq (s + 1, "width"))
        {
            gint width = gtk_tree_view_column_get_fixed_width (_col->column);

            info->is_visible = info->is_sensitive = TRUE;
            info->name = g_strdup_printf ("Width: %d", width);
            info->free_name = TRUE;
            info->trigger = g_strdup_printf ("command:tv_column_set_option (%%o,"
                    "%s,width,@ask_text(Enter the new column width,,%d))",
                    _col->name, width);
            info->free_trigger = TRUE;
            return TRUE;
        }
        else if (streq (s + 1, "refresh_properties"))
        {
            info->is_visible = info->is_sensitive = TRUE;
            if (extra)
            {
                if (streq (extra, "visible"))
                {
                    info->icon_special = DONNA_CONTEXT_ICON_IS_CHECK;
                    info->is_active = _col->refresh_properties == RP_VISIBLE;
                    info->name = "When row is visible";
                    info->trigger = g_strdup_printf ("command:tv_column_set_option (%%o,"
                            "%s,refresh_properties,%d)", _col->name, RP_VISIBLE);
                    info->free_trigger = TRUE;
                }
                else if (streq (extra, "preload"))
                {
                    info->icon_special = DONNA_CONTEXT_ICON_IS_CHECK;
                    info->is_active = _col->refresh_properties == RP_PRELOAD;
                    info->name = "When visible, preloading other rows";
                    info->trigger = g_strdup_printf ("command:tv_column_set_option (%%o,"
                            "%s,refresh_properties,%d)", _col->name, RP_PRELOAD);
                    info->free_trigger = TRUE;
                }
                else if (streq (extra, "on_demand"))
                {
                    info->icon_special = DONNA_CONTEXT_ICON_IS_CHECK;
                    info->is_active = _col->refresh_properties == RP_ON_DEMAND;
                    info->name = "On Demand (e.g. when clicking the refresh icon)";
                    info->trigger = g_strdup_printf ("command:tv_column_set_option (%%o,"
                            "%s,refresh_properties,%d)", _col->name, RP_ON_DEMAND);
                    info->free_trigger = TRUE;
                }
                else
                {
                    g_set_error (error, DONNA_CONTEXT_MENU_ERROR,
                            DONNA_CONTEXT_MENU_ERROR_OTHER,
                            "TreeView '%s': item '%s': invalid value in extra: '%s'",
                            priv->name, s + 1, extra);
                    return FALSE;
                }
            }
            else
            {
                info->name = "Refresh Needed Properties...";
                info->desc = "When to refresh unloaded properties needed to render the column";
                info->submenus = 1;
            }
            return TRUE;
        }

        if (!donna_column_type_get_context_item_info (_col->ct,
                    _col->ct_data,
                    s + 1,
                    extra,
                    reference,
                    (conv->row) ? conv->row->node : NULL,
                    (get_sel_fn) context_get_selection,
                    (reference & DONNA_CONTEXT_HAS_SELECTION) ? conv : NULL,
                    info,
                    error))
        {
            g_prefix_error (error, "TreeView '%s': Failed to get item '%s': ",
                    priv->name, item - 7);
            return FALSE;
        }

        return TRUE;
    }
    else if (streqn (item, "column_edit", 11))
    {
        GError *err = NULL;
        struct column *_col;

        if (reference & DONNA_CONTEXT_HAS_REF)
            info->is_visible = TRUE;

        if (item[11] == '\0')
        {
            if (info->is_visible)
            {
                gchar *s = donna_node_get_name (conv->row->node);
                info->name = g_strdup_printf ("Edit %s", s);
                g_free (s);
                info->free_name = TRUE;
                info->is_sensitive = TRUE;
            }
            else
                info->name = "Edit...";
            info->icon_name = "gtk-edit";
            return TRUE;
        }
        else if (item[11] != '.')
            goto err;

        item += 12;
        _col = get_column_by_name (tree, item);
        if (!_col)
        {
            /* no error, so when a column isn't used on tree, it just silently
             * isn't featured on the menu */
            info->is_visible = FALSE;
            return TRUE;
        }

        if (info->is_visible)
        {
            if (!donna_column_type_can_edit (_col->ct, _col->ct_data,
                    conv->row->node, &err))
            {
                if (!g_error_matches (err, DONNA_COLUMN_TYPE_ERROR,
                            DONNA_COLUMN_TYPE_ERROR_NODE_NOT_WRITABLE))
                    info->is_visible = FALSE;
                g_clear_error (&err);
            }
            else
                info->is_sensitive = TRUE;
        }

        info->name = g_strdup_printf ("Edit %s...",
                gtk_tree_view_column_get_title (_col->column));
        info->free_name = TRUE;

        if (info->is_visible && info->is_sensitive)
            info->trigger = g_strconcat (
                    "command:tv_column_edit (%o,%r,", _col->name, ")", NULL);

        return TRUE;
    }
    else if (streq (item, "columns"))
    {
        info->is_visible = info->is_sensitive = TRUE;

        if (extra)
        {
            struct column *_col;

            _col = get_column_by_name (tree, extra);
            info->icon_special = DONNA_CONTEXT_ICON_IS_CHECK;
            if (_col)
            {
                info->is_active = TRUE;
                info->name = gtk_tree_view_column_get_title (_col->column);
            }
            else
            {
                info->name = donna_config_get_string_column (
                        donna_app_peek_config (priv->app),
                        extra,
                        (priv->arrangement) ? priv->arrangement->columns_options : NULL,
                        priv->name,
                        priv->is_tree,
                        NULL,
                        "title", extra);
                info->free_name = TRUE;
            }
            info->trigger = g_strconcat ("command:tv_toggle_column(%o,",
                    extra, ")", NULL);
            info->free_trigger = TRUE;
        }
        else
        {
            info->name = "Columns";
            info->submenus = 1;
        }

        return TRUE;
    }
    else if (streqn (item, "go.", 3))
    {
        item += 3;

        if (streq (item, "tree_root"))
        {
            DonnaTreeView *t;
            GtkTreeIter iter;

            info->name = "Go Up to Tree Root";
            info->icon_name = "go-up";

            /* no tree given & we're not a tree, not visible */
            if (!extra && !priv->is_tree)
                return TRUE;

            /* if we're not a tree, get the given one */
            if (!priv->is_tree)
            {
                t = donna_app_get_tree_view (priv->app, extra);
                if (!t)
                    return TRUE;

                /* must be a tree & in the same location as we are */
                if (!t->priv->is_tree || t->priv->location != priv->location)
                {
                    g_object_unref (t);
                    return TRUE;
                }
            }
            else
            {
                t = tree;
                extra = priv->name;
            }

            /* no location or flat provider means no going up */
            info->is_visible = (priv->location && !(donna_provider_get_flags (
                            donna_node_peek_provider (priv->location))
                        & DONNA_PROVIDER_FLAG_FLAT));

            if (info->is_visible)
            {
                /* sensitive only if there's at least one parent to go up to */
                info->is_sensitive = gtk_tree_model_iter_parent (
                        (GtkTreeModel *) t->priv->store, &iter, &t->priv->location_iter);

                info->trigger = g_strconcat (
                        "command:tv_go_root (", extra, ")", NULL);
                info->free_trigger = TRUE;
            }
            if (t != tree)
                g_object_unref (t);
        }
        else if (streq (item, "up"))
        {
            gchar *s;

            /* no location or flat provider means no going up */
            info->is_visible = (priv->location && !(donna_provider_get_flags (
                            donna_node_peek_provider (priv->location))
                        & DONNA_PROVIDER_FLAG_FLAT));

            if (info->is_visible && priv->location)
            {
                s = donna_node_get_location (priv->location);
                info->is_sensitive = !streq (s, "/");
                g_free (s);
            }

            info->name = "Go Up";
            info->icon_name = "go-up";
            if (extra)
                info->trigger = g_strconcat (
                        "command:tv_go_up (%o,,", extra, ")", NULL);
            else
                info->trigger = "command:tv_go_up (%o)";
        }
        else if (streq (item, "down"))
        {
            /* no location or flat provider means no going down */
            info->is_visible = info->is_sensitive =
                (!priv->is_tree && priv->location
                 && !(donna_provider_get_flags (
                         donna_node_peek_provider (priv->location))
                     & DONNA_PROVIDER_FLAG_FLAT));

            info->name = "Go Down";
            info->icon_name = "go-down";
            info->trigger = "command:tv_go_down (%o)";
        }
        else if (streq (item, "back"))
        {
            if (priv->is_tree)
                return TRUE;

            info->is_visible = TRUE;
            info->name = "Go Back";
            info->icon_name = "go-previous";
            info->is_sensitive = donna_history_get_item (priv->history,
                    DONNA_HISTORY_BACKWARD, 1, NULL) != NULL;
            info->trigger = "command:tv_history_move (%o)";
        }
        else if (streq (item, "forward"))
        {
            if (priv->is_tree)
                return TRUE;

            info->is_visible = TRUE;
            info->name = "Go Forward";
            info->icon_name = "go-next";
            info->is_sensitive = donna_history_get_item (priv->history,
                    DONNA_HISTORY_FORWARD, 1, NULL) != NULL;
            info->trigger = "command:tv_history_move (%o, forward)";
        }
        else
            goto err;

        return TRUE;
    }
    else if (streqn (item, "domain.", 7))
    {
        DonnaProvider *provider;
        gchar *s;

        item += 7;
        s = strchr (item, '.');
        if (!s)
        {
            g_set_error (error, DONNA_CONTEXT_MENU_ERROR,
                    DONNA_CONTEXT_MENU_ERROR_UNKNOWN_ITEM,
                    "TreeView '%s': No such item: '%s'",
                    priv->name, item - 4);
            return FALSE;
        }

        *s = '\0';
        provider = donna_app_get_provider (priv->app, item);
        *s = '.';
        if (!provider)
        {
            g_set_error (error, DONNA_CONTEXT_MENU_ERROR,
                    DONNA_CONTEXT_MENU_ERROR_UNKNOWN_ITEM,
                    "TreeView '%s': No such item: '%s' (provider not found)",
                    priv->name, item - 7);
            return FALSE;
        }

        if (!donna_provider_get_context_item_info (provider, s + 1, extra,
                reference, (conv->row) ? conv->row->node : NULL,
                (get_sel_fn) context_get_selection,
                (reference & DONNA_CONTEXT_HAS_SELECTION) ? conv : NULL,
                info, error))
        {
            g_prefix_error (error, "TreeView '%s': Failed to get item '%s': ",
                    priv->name, item - 7);
            g_object_unref (provider);
            return FALSE;
        }

        g_object_unref (provider);
        return TRUE;
    }
    else if (streq (item, "move_root"))
    {
        GSList *l;
        gint n;

        if (!priv->is_tree)
        {
            /* minimum required, but it's not visible or anything */
            info->name = "Move Root";
            return TRUE;
        }

        info->is_visible = TRUE;
        info->is_sensitive = (reference & DONNA_CONTEXT_HAS_REF)
            /* only for roots */
            && gtk_tree_store_iter_depth (priv->store, conv->row->iter) == 0;

        /* we can compare pointers from priv->roots & conv because both use
         * iters from our hashtable */

        if (!extra || streq (extra, "up"))
        {
            info->name = "Move Root Up";
            info->trigger = "command:tv_move_root (%o,%r,-1)";
            /* not sensitive if first */
            if (info->is_sensitive
                    && (GtkTreeIter *) priv->roots->data == conv->row->iter)
                info->is_sensitive = FALSE;
        }
        else if (streq (extra, "down"))
        {
            info->name = "Move Root Down";
            info->trigger = "command:tv_move_root (%o,%r,1)";
            /* not sensitive if last */
            if (info->is_sensitive)
            {
                for (l = priv->roots; l; l = l->next)
                    if (!l->next
                            && (GtkTreeIter *) l->data == conv->row->iter)
                    {
                        info->is_sensitive = FALSE;
                        break;
                    }
            }
        }
        else if (streq (extra, "first"))
        {
            info->name = "Move Root First";
            /* not sensitive if first */
            if (info->is_sensitive
                    && (GtkTreeIter *) priv->roots->data == conv->row->iter)
                info->is_sensitive = FALSE;
            if (info->is_sensitive)
            {
                n = 0;
                for (l = priv->roots; l; --n, l = l->next)
                    if ((GtkTreeIter *) l->data == conv->row->iter)
                        break;
                info->trigger = g_strdup_printf (
                        "command:tv_move_root (%%o,%%r,%d)", n);
                info->free_trigger = TRUE;
            }
        }
        else if (streq (extra, "last"))
        {
            info->name = "Move Root Last";
            if (info->is_sensitive)
            {
                n = 0;
                for (l = priv->roots; l; ++n, l = l->next)
                {
                    /* not sensitive if last */
                    if (!l->next
                            && (GtkTreeIter *) l->data == conv->row->iter)
                    {
                        info->is_sensitive = FALSE;
                        break;
                    }
                    if ((GtkTreeIter *) l->data == conv->row->iter)
                        n = 0;
                }
                if (info->is_sensitive)
                {
                    info->trigger = g_strdup_printf (
                            "command:tv_move_root (%%o,%%r,%d)", n);
                    info->free_trigger = TRUE;
                }
            }
        }
        else
        {
            g_set_error (error, DONNA_CONTEXT_MENU_ERROR,
                    DONNA_CONTEXT_MENU_ERROR_OTHER,
                    "TreeView '%s': Invalid extra '%s' for item '%s'",
                    priv->name, extra, item);
            return FALSE;
        }

        return TRUE;
    }
    else if (streqn (item, "refresh", 7))
    {
        info->is_visible = info->is_sensitive = priv->is_tree || priv->location != NULL;
        info->icon_name = "view-refresh";

        if (item[7] == '\0')
        {
            info->name = "Refresh";
            info->trigger = "command:tv_refresh (%o, normal)";
            return TRUE;
        }
        else if (item[7] != '.')
            goto err;

        item += 8;
        if (streq (item, "visible"))
        {
            info->name = "Refresh (Visible)";
            info->trigger = "command:tv_refresh (%o, visible)";
        }
        else if (streq (item, "simple"))
        {
            info->name = "Refresh (Simple)";
            info->trigger = "command:tv_refresh (%o, simple)";
        }
        else if (streq (item, "reload"))
        {
            info->name = "Refresh (Reload)";
            info->trigger = "command:tv_refresh (%o, reload)";
        }
        else
            goto err;

        return TRUE;
    }
    else if (streqn (item, "register", 8))
    {
        DonnaNode *node;
        DonnaNodeHasValue has;
        gchar buf[64], *b = buf;
        GValue v = G_VALUE_INIT;
        enum {
            ON_NOTHING = 0,
            ON_SEL,
            ON_REF,
            ON_REG,
        } on = ON_NOTHING;

        if (!extra)
            extra = "";

        if (item[8] == '\0')
        {
            /* generic container for a submenu */

            info->is_sensitive = TRUE;
            if (!extra || streq (extra, "_"))
                info->name = "Register";
            else if (streq (extra, "+"))
                info->name = "Clipboard";
            else
            {
                info->name = g_strdup_printf ("Register '%s'", extra);
                info->free_name = TRUE;
            }
            info->icon_name = "edit-copy";
            info->submenus = DONNA_ENABLED_TYPE_ENABLED;
            return TRUE;
        }
        else if (item[8] != '.')
            goto err;

        item += 9;
        info->is_visible = TRUE;
        if ((reference & DONNA_CONTEXT_REF_SELECTED)
                || (!(reference & DONNA_CONTEXT_REF_NOT_SELECTED)
                    && (reference & DONNA_CONTEXT_HAS_SELECTION)))
            on = ON_SEL;
        else if (reference & DONNA_CONTEXT_REF_NOT_SELECTED)
            on = ON_REF;

        if (streq (item, "cut"))
        {
            info->is_sensitive = on != ON_NOTHING;
            if (on == ON_SEL)
                info->trigger = g_strconcat (
                        "command:register_set (", extra, ",cut,"
                        "@tv_get_nodes (%o, :selected))", NULL);
            else if (on == ON_REF)
                info->trigger = g_strconcat (
                        "command:register_set (", extra, ",cut,"
                        "@tv_get_nodes (%o, %r))", NULL);
        }
        else if (streq (item, "copy"))
        {
            info->is_sensitive = on != ON_NOTHING;
            if (on == ON_SEL)
                info->trigger = g_strconcat (
                        "command:register_set (", extra, ",copy,"
                        "@tv_get_nodes (%o, :selected))", NULL);
            else if (on == ON_REF)
                info->trigger = g_strconcat (
                        "command:register_set (", extra, ",copy,"
                        "@tv_get_nodes (%o, %r))", NULL);
        }
        else if (streq (item, "append"))
        {
            info->is_sensitive = on != ON_NOTHING;
            if (on == ON_SEL)
                info->trigger = g_strconcat (
                        "command:register_add_nodes (", extra, ","
                        "@tv_get_nodes (%o, :selected))", NULL);
            else if (on == ON_REF)
                info->trigger = g_strconcat (
                        "command:register_add_nodes (", extra, ","
                        "@tv_get_nodes (%o, %r))", NULL);
        }
        else if (streq (item, "paste") || streq (item, "paste_copy")
                || streq (item, "paste_move") || streq (item, "paste_new_folder"))
        {
            gchar *dest = NULL;

            on = ON_REG;

            if ((reference & DONNA_CONTEXT_REF_NOT_SELECTED)
                    && donna_node_get_node_type (conv->row->node)
                    == DONNA_NODE_CONTAINER)
                dest = donna_node_get_full_location (conv->row->node);
            else if (G_LIKELY (priv->location))
                dest = donna_node_get_full_location (priv->location);

            if (dest)
            {
                info->trigger = g_strconcat (
                        "command:register_nodes_io (", extra, ",",
                        (streq (item, "paste_copy")) ? "copy"
                        : (streq (item, "paste_move")) ? "move" : "auto",
                        ",", dest, ",",
                        (streq (item, "paste_new_folder")) ? "1)" : "0)", NULL);

                g_free (dest);
            }
        }
        else
            goto err;

        if (snprintf (buf, 64, "register:%s/%s", extra, item) >= 64)
            b = g_strdup_printf ("register:%s/%s", extra, item);

        node = donna_app_get_node (priv->app, b, FALSE, error);
        if (!node)
        {
            g_prefix_error (error, "TreeView '%s': "
                    "Cannot create item '%s' for register '%s', failed to get node '%s': ",
                    priv->name, item, extra, b);
            if (b != buf)
                g_free (b);
            g_free ((gchar *) info->trigger);
            return FALSE;
        }

        if (b != buf)
            g_free (b);

        info->name = donna_node_get_name (node);
        info->free_name = TRUE;

        donna_node_get (node, FALSE, "icon", &has, &v, NULL);
        if (has == DONNA_NODE_VALUE_SET)
        {
            info->icon_is_gicon = TRUE;
            info->icon = g_value_dup_object (&v);
            info->free_icon = TRUE;
            g_value_unset (&v);
        }

        if (on == ON_REG)
        {
            donna_node_get (node, FALSE, "menu-is-sensitive", &has, &v, NULL);
            if (has == DONNA_NODE_VALUE_SET)
            {
                info->is_sensitive = g_value_get_boolean (&v);
                g_value_unset (&v);
            }
        }

        g_object_unref (node);
        info->free_trigger = TRUE;
        return TRUE;
    }
    else if (streq (item, "remove_row"))
    {
        info->is_visible = priv->is_tree && (reference & DONNA_CONTEXT_HAS_REF);
        if (priv->is_minitree)
            info->is_sensitive = TRUE;
        else if (info->is_visible)
        {
            GtkTreePath *path;

            /* on a non minitree we can remove roots */
            path = gtk_tree_model_get_path ((GtkTreeModel *) priv->store,
                    conv->row->iter);
            if (gtk_tree_path_get_depth (path) == 1)
                info->is_sensitive = TRUE;
            gtk_tree_path_free (path);
        }
        info->name = "Remove Row From Tree";
        info->icon_name = "list-remove";
        info->trigger = "command:tv_remove_row (%o,%r)";
        return TRUE;
    }
    else if (streqn (item, "sort_order", 10)
            || streqn (item, "second_sort_order", 17))
    {
        struct column *_col;
        gboolean is_second = item[1] == 'e';

        info->is_visible = TRUE;
        info->is_sensitive = TRUE;

        if (!extra)
        {
            info->name = (is_second) ? "Secondary Sort By..." : "Sort By...";
            info->submenus = 1;
            return TRUE;
        }

        _col = get_column_by_name (tree, extra);
        if (!_col)
        {
            g_set_error (error, DONNA_CONTEXT_MENU_ERROR,
                    DONNA_CONTEXT_MENU_ERROR_UNKNOWN_ITEM,
                    "TreeView '%s': No such item: '%s' (no such column)",
                    priv->name, item);
            return FALSE;
        }

        info->name = gtk_tree_view_column_get_title (_col->column);
        info->icon_special = DONNA_CONTEXT_ICON_IS_RADIO;
        if (is_second)
            info->is_active = _col->column == priv->second_sort_column
                && _col->column != priv->sort_column;
        else
            info->is_active = _col->column == priv->sort_column;
        info->trigger = g_strconcat ("command:tv_set_",
                (is_second) ? "second_" : "", "sort (%o,",
                _col->name, ")", NULL);
        info->free_trigger = TRUE;

        return TRUE;
    }
    else if (streqn (item, "tv_options.", 11))
    {
        DonnaConfig *config = donna_app_peek_config (priv->app);
        const gchar *save = "";

        if (extra && *extra == '@')
        {
            gchar *s;
            gsize len;

            ++extra;
            s = strchr (extra, ':');
            if (s)
                len = (gsize) (s - extra);
            else
                len = strlen (extra);

            if (len == 0)
                save = "";
            else if (streqn (extra, "memory", len))
                save = "memory";
            else if (streqn (extra, "current", len))
                save = "current";
            else if (streqn (extra, "ask", len))
                save = "ask";
            else if (streqn (extra, "arr", len))
                save = "arr";
            else if (streqn (extra, "tree", len))
                save = "tree";
            else if (streqn (extra, "mode", len))
                save = "mode";
            else if (streqn (extra, "default", len))
                save = "default";
            else if (streqn (extra, "save-location", len))
                save = "save-location";
            else
            {
                g_set_error (error, DONNA_CONTEXT_MENU_ERROR,
                        DONNA_CONTEXT_MENU_ERROR_OTHER,
                        "TreeView '%s': Invalid save_location '%s' in extra for item '%s'",
                        priv->name, extra, item);
                return FALSE;
            }

            if (!s || s[1] == '\0')
                extra = NULL;
            else
                extra = s + 1;
        }

        /* only key_mode & click_mode can also have some extra */
        if (extra && !(streq (item + 11, "key_mode")
                    || streq (item + 11, "click_mode")))
        {
            g_set_error (error, DONNA_CONTEXT_MENU_ERROR,
                    DONNA_CONTEXT_MENU_ERROR_OTHER,
                    "TreeView '%s': Invalid extra '%s' for item '%s'",
                    priv->name, extra, item);
            return FALSE;
        }

        item += 11;
#define set_trigger(option,value) do {                                      \
        if (save)                                                           \
        {                                                                   \
            info->trigger = g_strconcat (                                   \
                    "command:tv_set_option (%o," option "," value ",",    \
                    save, ")", NULL);                                       \
            info->free_trigger = TRUE;                                      \
        }                                                                   \
        else                                                                \
            info->trigger = "command:tv_set_option (%o," option "," value ")"; \
} while (0)

        if (streqn (item, "node_types", 10))
        {
            if (item[10] == '\0')
            {
                info->is_visible = info->is_sensitive = TRUE;
                info->name = "Nodes Shown...";
                info->submenus = 1;
                return TRUE;
            }
            else if (item[10] == '.')
            {
                if (!_donna_context_set_item_from_extra (info, config,
                            "node-type", DONNA_CONFIG_EXTRA_TYPE_LIST_INT,
                            FALSE, "node_types",
                            item + 11, priv->node_types, save, error))
                {
                    g_prefix_error (error,
                            "TreeView '%s': Failed to get item 'tv_options.%s': ",
                            priv->name, item);
                    return FALSE;
                }

                info->is_visible = info->is_sensitive = TRUE;
                return TRUE;
            }
        }
        else if (streq (item, "show_hidden"))
        {
            info->is_visible = info->is_sensitive = TRUE;
            info->name = "Show \"dot\" files";
            info->icon_special = DONNA_CONTEXT_ICON_IS_CHECK;
            info->is_active = priv->show_hidden;
            if (info->is_active)
                set_trigger ("show_hidden", "0");
            else
                set_trigger ("show_hidden", "1");
            return TRUE;
        }
        else if (streqn (item, "sort_groups", 11))
        {
            if (item[11] == '\0')
            {
                info->is_visible = info->is_sensitive = TRUE;
                info->name = "Sort Containers...";
                info->submenus = 1;
                return TRUE;
            }
            else if (item[11] == '.')
            {
                if (!_donna_context_set_item_from_extra (info, config,
                            "sg", DONNA_CONFIG_EXTRA_TYPE_LIST_INT,
                            FALSE, "sort_groups",
                            item + 12, priv->sort_groups, save, error))
                {
                    g_prefix_error (error,
                            "TreeView '%s': Failed to get item 'tv_options.%s': ",
                            priv->name, item);
                    return FALSE;
                }

                info->is_visible = info->is_sensitive = TRUE;
                return TRUE;
            }
        }
#ifdef GTK_IS_JJK
        else if (streqn (item, "select_highlight", 16))
        {
            if (item[16] == '\0')
            {
                info->is_visible = info->is_sensitive = TRUE;
                info->name = "Selection Highlight...";
                info->submenus = 1;
                return TRUE;
            }
            else if (item[16] == '.')
            {
                if (!_donna_context_set_item_from_extra (info, config,
                            "highlight", DONNA_CONFIG_EXTRA_TYPE_LIST_INT,
                            FALSE, "select_highlight",
                            item + 17, priv->select_highlight, save, error))
                {
                    g_prefix_error (error,
                            "TreeView '%s': Failed to get item 'tv_options.%s': ",
                            priv->name, item);
                    return FALSE;
                }

                info->is_visible = info->is_sensitive = TRUE;
                return TRUE;
            }
        }
#endif
        else if (streq (item, "key_mode"))
        {
            info->is_visible = info->is_sensitive = TRUE;
            if (extra)
            {
                info->icon_special = DONNA_CONTEXT_ICON_IS_RADIO;
                info->is_active = streq (extra, priv->key_mode);
                info->name = g_strdup (extra);
                info->free_name = TRUE;
                info->trigger = g_strconcat ("command:tv_set_option (%o,key_mode,",
                        extra, ",", save, ")", NULL);
                info->free_trigger = TRUE;
            }
            else
            {
                info->name = g_strconcat ("Key Mode: ", priv->key_mode, NULL);
                info->free_name = TRUE;
                info->trigger = g_strconcat ("command:tv_set_option (%o,key_mode,"
                        "@ask_text(Enter the new default key mode,,",
                        priv->key_mode, "),", save, ")", NULL);
                info->free_trigger = TRUE;
            }
            return TRUE;
        }
        else if (streq (item, "click_mode"))
        {
            info->is_visible = info->is_sensitive = TRUE;
            if (extra)
            {
                info->icon_special = DONNA_CONTEXT_ICON_IS_RADIO;
                info->is_active = streq (extra, priv->click_mode);
                info->name = g_strdup (extra);
                info->free_name = TRUE;
                info->trigger = g_strconcat ("command:tv_set_option (%o,click_mode,",
                        extra, ",", save, ")", NULL);
                info->free_trigger = TRUE;
            }
            else
            {
                info->name = g_strconcat ("Click Mode: ", priv->click_mode, NULL);
                info->free_name = TRUE;
                info->trigger = g_strconcat ("command:tv_set_option (%o,click_mode,"
                        "@ask_text(Enter the click key mode,,",
                        priv->click_mode, "),", save, ")", NULL);
                info->free_trigger = TRUE;
            }
            return TRUE;
        }
        else if (streqn (item, "default_save_location", 21))
        {
            if (item[21] == '\0')
            {
                info->is_visible = info->is_sensitive = TRUE;
                info->name = "Default Save Location...";
                info->desc = "Default save location for treeview/column options";
                info->submenus = 1;
                return TRUE;
            }
            else if (item[21] == '.')
            {
                if (!_donna_context_set_item_from_extra (info, config,
                            "save-location", DONNA_CONFIG_EXTRA_TYPE_LIST_INT,
                            FALSE, "default_save_location",
                            item + 22, priv->default_save_location, save, error))
                {
                    g_prefix_error (error,
                            "TreeView '%s': Failed to get item 'tv_options.%s': ",
                            priv->name, item);
                    return FALSE;
                }

                info->is_visible = info->is_sensitive = TRUE;
                return TRUE;
            }
        }
        else if (priv->is_tree)
        {
            if (streq (item, "is_minitree"))
            {
                info->is_visible = info->is_sensitive = TRUE;
                info->name = "Mini Tree";
                info->icon_special = DONNA_CONTEXT_ICON_IS_CHECK;
                info->is_active = priv->is_minitree;
                if (info->is_active)
                    set_trigger ("is_minitree", "0");
                else
                    set_trigger ("is_minitree", "1");
                return TRUE;
            }
            else if (streq (item, "sync"))
            {
                info->is_visible = info->is_sensitive = TRUE;
                info->name = "Synchonization...";
                info->submenus = 1;
                return TRUE;
            }
            else if (streq (item, "sync_with"))
            {
                info->is_visible = info->is_sensitive = TRUE;
                info->name = "Sync With...";
                info->submenus = 1;
                return TRUE;
            }
            else if (streq (item, "sync_with.active"))
            {
                info->is_visible = info->is_sensitive = TRUE;
                info->name = "Active List";
                info->icon_special = DONNA_CONTEXT_ICON_IS_RADIO;
                if (priv->sid_active_list_changed)
                    info->is_active = TRUE;
                set_trigger ("sync_with", ":active");
                return TRUE;
            }
            else if (streq (item, "sync_with.custom"))
            {
                info->is_visible = info->is_sensitive = TRUE;
                if (priv->sid_active_list_changed)
                    info->name = "Custom...";
                else if (priv->sync_with)
                {
                    info->name = g_strconcat ("Custom: ",
                            donna_tree_view_get_name (priv->sync_with), NULL);
                    info->free_name = TRUE;
                }
                else
                    info->name = "Custom: <none>";
                info->icon_special = DONNA_CONTEXT_ICON_IS_RADIO;
                if (priv->sid_active_list_changed == 0)
                    info->is_active = TRUE;

                info->trigger = g_strconcat ("command:tv_set_option (%o,sync_with,"
                        "@ask_text(Enter the name of the list to sync with,,",
                        (!priv->sid_active_list_changed && priv->sync_with)
                        ? donna_tree_view_get_name (priv->sync_with) : "",
                        "),", save, ")", NULL);
                info->free_trigger = TRUE;

                return TRUE;
            }
            else if (streqn (item, "sync_mode", 9))
            {
                if (item[9] == '\0')
                {
                    info->is_visible = info->is_sensitive = TRUE;
                    info->name = "Sync Mode...";
                    info->submenus = 1;
                    return TRUE;
                }
                else if (item[9] == '.')
                {
                    if (!_donna_context_set_item_from_extra (info, config,
                                "sync", DONNA_CONFIG_EXTRA_TYPE_LIST_INT,
                                FALSE, "sync_mode",
                                item + 10, priv->sync_mode, save, error))
                    {
                        g_prefix_error (error,
                                "TreeView '%s': Failed to get item 'tv_options.%s': ",
                                priv->name, item);
                        return FALSE;
                    }

                    item += 10;
                    if (streq (item, "nodes"))
                        info->desc = "Node must exists in tree and be accessible";
                    else if (streq (item, "known-children"))
                        info->desc = "Node must exists on tree (re-expand might occur)";
                    else if (streq (item, "children"))
                        info->desc = "Parent must exist on tree (fresh expand might occur)";
                    else if (streq (item, "full"))
                        info->desc = "New root might be added";

                    info->is_visible = info->is_sensitive = TRUE;
                    return TRUE;
                }
            }
            else if (streq (item, "sync_scroll"))
            {
                info->is_visible = info->is_sensitive = TRUE;
                info->name = "Scroll To Node";
                info->desc = "Scrolling will occur only if needed";
                info->icon_special = DONNA_CONTEXT_ICON_IS_CHECK;
                if (priv->sync_scroll)
                    info->is_active = TRUE;
                if (info->is_active)
                    set_trigger ("sync_scroll", "0");
                else
                    set_trigger ("sync_scroll", "1");
                return TRUE;
            }
            else if (streqn (item, "node_visuals", 12))
            {
                if (item[12] == '\0')
                {
                    info->is_visible = info->is_sensitive = TRUE;
                    info->name = "Node Visuals...";
                    info->submenus = 1;
                    return TRUE;
                }
                else if (item[12] == '.')
                {
                    if (!_donna_context_set_item_from_extra (info, config,
                                "visuals", DONNA_CONFIG_EXTRA_TYPE_LIST_FLAGS,
                                FALSE, "node_visuals",
                                item + 13, priv->node_visuals, save, error))
                    {
                        g_prefix_error (error,
                                "TreeView '%s': Failed to get item 'tv_options.%s': ",
                                priv->name, item);
                        return FALSE;
                    }

                    info->is_visible = info->is_sensitive = TRUE;
                    return TRUE;
                }
            }
            else if (streq (item, "auto_focus_sync"))
            {
                info->is_visible = info->is_sensitive = TRUE;
                info->name = "Autofocus Sync List";
                info->desc = "Send focus to sync list on selection/location change";
                info->icon_special = DONNA_CONTEXT_ICON_IS_CHECK;
                if (priv->auto_focus_sync)
                    info->is_active = TRUE;
                if (info->is_active)
                    set_trigger ("auto_focus_sync", "0");
                else
                    set_trigger ("auto_focus_sync", "1");
                return TRUE;
            }
        }
        else /* list */
        {
            if (streq (item, "focusing_click"))
            {
                info->is_visible = info->is_sensitive = TRUE;
                info->name = "Ignore Focusing Left Click";
                info->desc = "Single left click bringing the focus to the list will not be processed further";
                info->icon_special = DONNA_CONTEXT_ICON_IS_CHECK;
                if (priv->focusing_click)
                    info->is_active = TRUE;
                if (info->is_active)
                    set_trigger ("focusing_click", "0");
                else
                    set_trigger ("focusing_click", "1");
                return TRUE;
            }
            else if (streqn (item, "goto_item_set", 13))
            {
                if (item[13] == '\0')
                {
                    info->is_visible = info->is_sensitive = TRUE;
                    info->name = "On Item As Location...";
                    info->desc = "When an item is set as location, after going "
                        "to its parent, what should be set on the item...";
                    info->submenus = 1;
                    return TRUE;
                }
                else if (item[13] == '.')
                {
                    if (!_donna_context_set_item_from_extra (info, config,
                                "tree-set", DONNA_CONFIG_EXTRA_TYPE_LIST_FLAGS,
                                FALSE, "goto_item_set",
                                item + 14, priv->goto_item_set, save, error))
                    {
                        g_prefix_error (error,
                                "TreeView '%s': Failed to get item 'tv_options.%s': ",
                                priv->name, item);
                        return FALSE;
                    }

                    info->is_visible = info->is_sensitive = TRUE;
                    return TRUE;
                }
            }
            else if (streq (item, "vf_items_only"))
            {
                info->is_visible = info->is_sensitive = TRUE;
                info->name = "Apply Visual Filter to Items Only";
                info->desc = "Containers (e.g. folders) will always be visible regardless of the VF";
                info->icon_special = DONNA_CONTEXT_ICON_IS_CHECK;
                if (priv->vf_items_only)
                    info->is_active = TRUE;
                if (info->is_active)
                    set_trigger ("vf_items_only", "0");
                else
                    set_trigger ("vf_items_only", "1");
                return TRUE;
            }
        }

#undef set_trigger
        /* for error message */
        item -= 11;
    }
    else if (streqn (item, "tree_visuals.", 13))
    {
        DonnaTreeVisual v, visuals;
        GString *str;
        guint col;
        gchar *s;
        const gchar *name;

        item += 13;

        if (streq (item, "name"))
        {
            name = "Name";
            v = DONNA_TREE_VISUAL_NAME;
            col = TREE_COL_NAME;
        }
        else if (streq (item, "icon"))
        {
            name = "Icon";
            v = DONNA_TREE_VISUAL_ICON;
            col = TREE_COL_ICON;
        }
        else if (streq (item, "box"))
        {
            name = "Box";
            v = DONNA_TREE_VISUAL_BOX;
            col = TREE_COL_BOX;
        }
        else if (streq (item, "highlight"))
        {
            name = "Highlight";
            v = DONNA_TREE_VISUAL_HIGHLIGHT;
            col = TREE_COL_HIGHLIGHT;
        }
        else if (streq (item, "click_mode"))
        {
            name = "Click Mode";
            v = DONNA_TREE_VISUAL_CLICK_MODE;
            col = TREE_COL_CLICK_MODE;
        }
        else
        {
            /* for error message */
            item -= 13;
            goto err;
        }

        if (!conv->row)
        {
            info->name = (gchar *) name;
            return TRUE;
        }

        gtk_tree_model_get ((GtkTreeModel *) priv->store, conv->row->iter,
                TREE_COL_VISUALS,   &visuals,
                col,                &s,
                -1);

        if (col == TREE_COL_ICON && s)
        {
            GIcon *icon = (GIcon *) s;
            s = g_icon_to_string (icon);
            if (G_UNLIKELY (!s || *s == '.'))
            {
                /* since a visual is a user-set icon, it should always be either
                 * a /path/to/file.png or an icon-name. In the off chance it's
                 * not, e.g. a ". GThemedIcon icon-name1 icon-name2" kinda
                 * string, we just ignore it. */
                g_free (s);
                s = NULL;
            }
            g_object_unref (icon);
        }

        info->is_visible = info->is_sensitive = TRUE;
        if (visuals & v)
            info->name = g_strconcat (name, ": ", (s) ? s : "<icon>", NULL);
        else
            info->name = g_strconcat (name, "...", NULL);
        info->free_name = TRUE;

        str = g_string_new ("command:tv_set_visual (%o,%r,");
        g_string_append (str, name);
        g_string_append (str, ",@ask_text (");
        g_string_append_printf (str, "Enter the new '%s' value", name);
        if ((visuals & v) && s)
        {
            g_string_append_c (str, ',');
            g_string_append_c (str, ',');
            donna_g_string_append_quoted (str, s, FALSE);
        }
        g_string_append_c (str, ')');
        g_string_append_c (str, ')');

        info->trigger = g_string_free (str, FALSE);
        info->free_trigger = TRUE;

        g_free (s);
        return TRUE;
    }

err:
    g_set_error (error, DONNA_CONTEXT_MENU_ERROR,
            DONNA_CONTEXT_MENU_ERROR_UNKNOWN_ITEM,
            "TreeView '%s': No such item: '%s'",
                priv->name, item);
    return FALSE;
}

/**
 * donna_tree_view_context_get_nodes:
 * @tree: A #DonnaTreeView
 * @rowid: (allow-none): Identifier of a row; See #rowid for more
 * @column: (allow-none): Name of a column
 * @items: (allow-none): The items to load as context nodes
 * @error: (allow-none): Return location of a #GError, or %NULL
 *
 * Returns the nodes to be used in context menu, as defined on @items
 *
 * If @items is not specified, a few options will be tried to get the items to
 * use. First option
 * `tree_views/&lt;TREE-NAME&gt;/context_menu_&lt;DOMAIN&gt;`
 * is tried (where DOMAIN is the domain of the current location).
 * If it doesn't exist, option
 * `tree_views/&lt;TREE-NAME&gt;/context_menu` is tried.
 *
 * Next defaults are tried using the same logic:
 * `defaults/&lt;TREE-MODE&gt;/context_menu_&lt;DOMAIN&gt;`
 * first, and if it doesn't exist
 * `defaults/&lt;TREE-MODE&gt;/context_menu` is tried.
 *
 * If none of those options exist, an error is returned.
 *
 * #DonnaNode<!-- -->s from the specified items are then loaded (see
 * donna_context_menu_get_nodes()) and returned.
 * The context used will have the row behind @rowid (if any) set as reference,
 * and the column @column (if specified) will also be use/available in
 * contextual parsing.
 *
 * Valid values for @column are the column's full name, simply as many of the
 * first characters as needed to identify it, or a number to get the nth column
 * on @tree.
 *
 * TODO: document internal aliases & items
 *
 * Returns: (transfer full) (element-type DonnaNode): The contextual nodes (to
 * show in a popup menu), or %NULL
 */
GPtrArray *
donna_tree_view_context_get_nodes (DonnaTreeView      *tree,
                                   DonnaRowId         *rowid,
                                   const gchar        *column,
                                   gchar              *items,
                                   GError            **error)
{
    DonnaTreeViewPrivate *priv;
    DonnaContextReference reference = 0;
    struct conv conv = { NULL, };
    DonnaContext context = { CONTEXT_FLAGS CONTEXT_COLUMN_FLAGS, FALSE,
        (conv_flag_fn) tree_conv_flag, &conv };
    GtkTreeSelection *sel;
    GPtrArray *nodes;
    row_id_type type;
    GtkTreeIter iter;

    g_return_val_if_fail (DONNA_IS_TREE_VIEW (tree), NULL);
    priv = tree->priv;

    sel = gtk_tree_view_get_selection ((GtkTreeView *) tree);
    conv.tree = tree;

    if (column)
    {
        struct column *_col;

        _col = get_column_from_name (tree, column, error);
        if (!_col)
            return NULL;
        conv.col_name = _col->name;
    }
    else
        conv.col_name = (gchar *) "";

    if (rowid)
    {
        type = convert_row_id_to_iter (tree, rowid, &iter);
        if (type != ROW_ID_ROW)
        {
            g_set_error (error, DONNA_TREE_VIEW_ERROR,
                    DONNA_TREE_VIEW_ERROR_INVALID_ROW_ID,
                    "TreeView '%s': Cannot get context nodes, invalid reference row-id",
                    priv->name);
            return NULL;
        }
        conv.row = get_row_for_iter (tree, &iter);
        if (gtk_tree_selection_iter_is_selected (sel, &iter))
            reference |= DONNA_CONTEXT_REF_SELECTED;
        else
            reference |= DONNA_CONTEXT_REF_NOT_SELECTED;
    }

    if ((reference & DONNA_CONTEXT_REF_SELECTED)
            || gtk_tree_selection_count_selected_rows (sel))
        reference |= DONNA_CONTEXT_HAS_SELECTION;

    if (!items)
    {
        DonnaConfig *config = donna_app_peek_config (priv->app);
        const gchar *domain;

        domain = (priv->location) ? donna_node_get_domain (priv->location) : NULL;

        /* if no domain or no domain-specific, try basic definition */
        if (!domain || !donna_config_get_string (config, NULL, &items,
                    "tree_views/%s/context_menu_%s",
                    priv->name, domain))
            donna_config_get_string (config, NULL, &items,
                    "tree_views/%s/context_menu", priv->name);

        /* still nothing, use defaults */
        if (!items)
        {
            if (!domain || !donna_config_get_string (config, NULL, &items,
                        "defaults/%s/context_menu_%s",
                        (priv->is_tree) ? "trees": "lists",
                        domain))
                donna_config_get_string (config, NULL, &items,
                        "defaults/%s/context_menu",
                        (priv->is_tree) ? "trees": "lists");
        }

        if (!items)
        {
            g_set_error (error, DONNA_TREE_VIEW_ERROR,
                    DONNA_TREE_VIEW_ERROR_NOT_FOUND,
                    "TreeView '%s': No items for context menu found",
                    priv->name);
            return NULL;
        }
    }

    nodes = donna_context_menu_get_nodes (priv->app, items, reference, "tree_views",
                (get_alias_fn) tree_context_get_alias,
                (get_item_info_fn) tree_context_get_item_info,
                &context, error);

    if (conv.row)
        g_free (conv.row);
    if (conv.selection)
        g_ptr_array_unref (conv.selection);

    if (!nodes)
    {
        g_prefix_error (error, "TreeView '%s': Failed to get context nodes: ",
                priv->name);
        return NULL;
    }

    return nodes;
}

/**
 * donna_tree_view_context_popup:
 * @tree: A #DonnaTreeView
 * @rowid: (allow-none): Identifier of a row; See #rowid for more
 * @column: (allow-none): Name of a column
 * @items: (allow-none): The items to load as context nodes
 * @menus: (allow-none): Menu definition to use
 * @no_focus_grab: If %TRUE @tree won't grab focus before showing the menu
 * @error: (allow-none): Return location of a #GError, or %NULL
 *
 * Gets the nodes to be used in context menu, as defined on @items, and show
 * them in a popup menu.
 *
 * See donna_context_menu_get_nodes() for more on getting the nodes, and how
 * @rowid, @column and @items are used.
 *
 * If @menus isn't specified, value of treeview option
 * `context_menu_menus` will be used, following the usual option path.
 *
 * See donna_app_show_menu() for more on how the menu is shown.
 *
 * Returns: %TRUE is the menu was shown, else %FALSE
 */
gboolean
donna_tree_view_context_popup (DonnaTreeView      *tree,
                               DonnaRowId         *rowid,
                               const gchar        *column,
                               gchar              *items,
                               const gchar        *_menus,
                               gboolean            no_focus_grab,
                               GError            **error)
{
    DonnaTreeViewPrivate *priv;
    DonnaConfig *config;
    GPtrArray *nodes;
    gchar *menus = NULL;

    nodes = donna_tree_view_context_get_nodes (tree, rowid, column, items, error);
    if (!nodes)
        return FALSE;

    priv = tree->priv;
    config = donna_app_peek_config (priv->app);

    if (_menus)
        menus = (gchar *) _menus;
    else
    {
        if (!donna_config_get_string (config, NULL, &menus,
                    "tree_views/%s/context_menu_menus",
                    priv->name))
            donna_config_get_string (config, NULL, &menus,
                    "defaults/%s/context_menu_menus",
                    (priv->is_tree) ? "trees" : "lists");
    }

    if (!no_focus_grab)
        gtk_widget_grab_focus ((GtkWidget *) tree);

    if (!donna_app_show_menu (priv->app, nodes, menus, error))
    {
        g_prefix_error (error, "TreeView '%s': Failed to show context menu: ",
                priv->name);
        if (menus != _menus)
            g_free (menus);
        return FALSE;
    }

    if (menus != _menus)
        g_free (menus);
    return TRUE;
}

/* mode tree only */
/**
 * donna_tree_view_get_node_root:
 * @tree: A #DonnaTreeView
 * @error: (allow-none): Return location of a #GError, or %NULL
 *
 * Returns the node of the root of the current branch, that is the one of the
 * current location.
 *
 * This obviously only works on tree, and only if the current location is in a
 * non-flat domain (e.g. "fs" or "config").
 *
 * Returns: (transfer full): The #DonnaNode of the root of current branch
 */
DonnaNode *
donna_tree_view_get_node_root   (DonnaTreeView      *tree,
                                 GError            **error)
{
    DonnaTreeViewPrivate *priv;
    GtkTreeModel *model;
    GtkTreeIter parent;
    GtkTreeIter iter;
    DonnaNode *node;

    g_return_val_if_fail (DONNA_IS_TREE_VIEW (tree), NULL);
    priv = tree->priv;
    model = (GtkTreeModel *) priv->store;

    if (!priv->location)
    {
        g_set_error (error, DONNA_TREE_VIEW_ERROR,
                DONNA_TREE_VIEW_ERROR_NOT_FOUND,
                "TreeView '%s': Can't get root node, no current location",
                priv->name);
        return NULL;
    }

    if (!priv->is_tree)
    {
        g_set_error (error, DONNA_TREE_VIEW_ERROR,
                DONNA_TREE_VIEW_ERROR_INVALID_MODE,
                "TreeView '%s': Can't get root node in mode List",
                priv->name);
        return NULL;
    }

    if (donna_provider_get_flags (donna_node_peek_provider (priv->location))
            & DONNA_PROVIDER_FLAG_FLAT)
    {
        g_set_error (error, DONNA_TREE_VIEW_ERROR,
                DONNA_TREE_VIEW_ERROR_FLAT_PROVIDER,
                "TreeView '%s': Can't get root node, current location is in flat provider",
                priv->name);
        return NULL;
    }

    iter = priv->location_iter;
    while (gtk_tree_model_iter_parent (model, &parent, &iter))
        iter = parent;

    gtk_tree_model_get (model, &iter,
            TREE_COL_NODE,  &node,
            -1);

    return node;
}

/* mode tree only */
/**
 * donna_tree_view_go_root:
 * @tree: A #DonnaTreeView
 * @error: (allow-none): Return location of a #GError, or %NULL
 *
 * Change location to the root of the current branch
 *
 * See donna_tree_view_get_node_root() for more
 *
 * Returns: %TRUE if current location was changed, or no destination was found
 * (e.g. there's no current location); else %FALSE
 */
gboolean
donna_tree_view_go_root (DonnaTreeView      *tree,
                         GError            **error)
{
    GError *err = NULL;
    DonnaNode *node;
    gboolean ret;

    node = donna_tree_view_get_node_root (tree, &err);
    if (!node)
    {
        if (g_error_matches (err, DONNA_TREE_VIEW_ERROR,
                DONNA_TREE_VIEW_ERROR_NOT_FOUND))
        {
            /* even though there's no location to go to, this is a no-op, not an
             * error */
            g_clear_error (&err);
            return TRUE;
        }
        else
            g_propagate_error (error, err);

        return FALSE;
    }

    ret = donna_tree_view_set_location (tree, node, error);
    g_object_unref (node);
    return ret;
}

/**
 * donna_tree_view_set_sort_order:
 * @tree: A #DonnaTreeView
 * @column: Name of the column to sort by
 * @order: Order to sort
 * @error: (allow-none): Return location of a #GError, or %NULL
 *
 * Set @tree's sort order on @column (using order @order)
 *
 * If @order if %DONNA_SORT_UNKNOWN the default sort order for @column will be
 * used, unless sort order was already there in which case the order is
 * reversed.
 *
 * Returns: %TRUE on success, else %FALSE
 */
gboolean
donna_tree_view_set_sort_order (DonnaTreeView      *tree,
                                const gchar        *column,
                                DonnaSortOrder      order,
                                GError            **error)
{
    struct column *_col;

    g_return_val_if_fail (DONNA_IS_TREE_VIEW (tree), FALSE);
    g_return_val_if_fail (column != NULL, FALSE);

    _col = get_column_from_name (tree, column, error);
    if (!_col)
        return FALSE;

    set_sort_column (tree, _col->column, order, FALSE);
    return TRUE;
}

/**
 * donna_tree_view_set_second_sort_order:
 * @tree: A #DonnaTreeView
 * @column: Name of the column to second sort by
 * @order: Order to sort
 * @error: (allow-none): Return location of a #GError, or %NULL
 *
 * Set @tree's second sort order on @column (using order @order)
 *
 * If @order if %DONNA_SORT_UNKNOWN the default sort order for @column will be
 * used, unless second sort order was already there in which case the order is
 * reversed.
 *
 * Returns: %TRUE on success, else %FALSE
 */
gboolean
donna_tree_view_set_second_sort_order (DonnaTreeView      *tree,
                                       const gchar        *column,
                                       DonnaSortOrder      order,
                                       GError            **error)
{
    struct column *_col;

    g_return_val_if_fail (DONNA_IS_TREE_VIEW (tree), FALSE);
    g_return_val_if_fail (column != NULL, FALSE);

    _col = get_column_from_name (tree, column, error);
    if (!_col)
        return FALSE;

    set_second_sort_column (tree, _col->column, order, FALSE);
    return TRUE;
}

static gboolean
interactive_search (GtkTreeModel    *model,
                    gint             column,
                    const gchar     *key,
                    GtkTreeIter     *iter,
                    DonnaTreeView   *tree)
{
    DonnaNode *node;
    gchar *name;
    gchar *norm1 = NULL;
    gchar *norm2 = NULL;
    gboolean match = FALSE;
    gboolean from_start;

    gtk_tree_model_get (model, iter, TREE_VIEW_COL_NODE, &node, -1);
    if (!node)
        return TRUE;
    name = donna_node_get_name (node);
    g_object_unref (node);

    norm1 = g_utf8_normalize (name, -1, G_NORMALIZE_ALL);
    g_free (name);
    from_start = *key == '^';
    if (from_start)
        ++key;
    norm2 = g_utf8_normalize (key, -1, G_NORMALIZE_ALL);

    if (norm1 && norm2)
    {
        gchar *s1, *s2;

        s1 = g_utf8_casefold (norm1, -1);
        s2 = g_utf8_casefold (norm2, -1);
        if (from_start)
            match = streqn (s1, s2, strlen (s2));
        else if (strstr (s1, s2))
            match = TRUE;
        g_free (s1);
        g_free (s2);
    }
    g_free (norm1);
    g_free (norm2);

    return !match;
}

/**
 * donna_tree_view_start_interactive_search:
 * @tree: A #DonnaTreeView
 *
 * Starts the interactive search, i.e. show the entry to perform the search
 *
 * An entry will show up (at the bottom right of the treeview) allowing to enter
 * the text to search. Said text will be searched in the name of all (visible)
 * rows, case insensitively.
 *
 * If you prefix your criteria with a caret (^) then the text must match the
 * beginning of the name, else it can be contained anywhere in the name.
 *
 * When triggered, any selection is lost. The first match will then be scrolled
 * to, focused & selected. If you continue typing, the search is instantly
 * adjusted. If not match is found, there will be no selection.
 *
 * When a match is found/selected, you can use keys Up & Down to move to the
 * previous/next match.
 *
 * You can press Esc to exit interactive search mode, or simply wait as it will
 * automatically expire after a little while (5s) without searching (i.e. moving
 * or changing the text to search for).
 */
void
donna_tree_view_start_interactive_search (DonnaTreeView      *tree)
{
    gboolean r;
    g_return_if_fail (DONNA_IS_TREE_VIEW (tree));
    g_signal_emit_by_name (tree, "start-interactive-search", &r);
}

enum save
{
    SAVE_OPTIONS        = (1 << 0),
    SAVE_COLUMNS        = (1 << 1),
    SAVE_SORT           = (1 << 2),
    SAVE_SECOND_SORT    = (1 << 3),
    SAVE_COLUMN_OPTIONS = (1 << 4) /* i.e. options of all columns */
};

struct save_data
{
    GtkWidget *window;
    GtkContainer *c_box;
    GtkWidget *save_btn;
    GtkWidget *cancel_btn;
    enum save *save;
    GPtrArray **arr;
};

static void
save_cb (GtkButton *btn, struct save_data *data)
{
    GList *list, *l;

    list = gtk_container_get_children (data->c_box);
    for (l = list; l; l = l->next)
    {
        enum save save;

        if (!gtk_toggle_button_get_active ((GtkToggleButton *) l->data))
            continue;

        save = GPOINTER_TO_UINT (g_object_get_data (l->data, "_save"));
        if (save)
            *data->save |= save;
        else
        {
            struct column *_col;

            _col = g_object_get_data (l->data, "_save_column");
            if (!*data->arr)
                *data->arr = g_ptr_array_new ();
            g_ptr_array_add (*data->arr, _col);
        }
    }
    g_list_free (list);

    gtk_widget_destroy (data->window);
}

static gboolean
key_pressed (struct save_data *data, GdkEventKey *event)
{
    if (event->keyval == GDK_KEY_Escape)
    {
        gtk_widget_activate (data->cancel_btn);
        return TRUE;
    }

    return FALSE;
}

static void
save_toggled (GtkToggleButton *btn, struct save_data *data)
{
    GList *list, *l;
    gboolean active = FALSE;

    list = gtk_container_get_children (data->c_box);
    for (l = list; l; l = l->next)
    {
        if (!gtk_toggle_button_get_active ((GtkToggleButton *) l->data))
            continue;

        active = TRUE;
        break;
    }
    g_list_free (list);

    gtk_widget_set_sensitive (data->save_btn, active);
}

/**
 * donna_tree_view_save_to_config:
 * @tree: A #DonnaTreeView
 * @elements: (allow-none): What to save to configuration, or %NULL
 * @error: (allow-none): Return location of a #GError; or %NULL
 *
 * Saves to configuration the elements indicated in @elements
 *
 * For each element, the current value (i.e. in memory) will be saved to the
 * configuration, at the same place values would be loaded from. For treeview
 * and column options, this is the same as calling donna_tree_view_set_option()
 * and donna_tree_view_column_set_option() for each supported options, %NULL as
 * value and %DONNA_TREE_VIEW_OPTION_SAVE_IN_CURRENT as save location.
 *
 * @elements can be a string, comma-separated list of one or more of the
 * following:
 *
 * - `:options` : treeview options
 * - `:columns` : columns (layout); i.e. which columns are visible, and the
 *   order
 * - `:sort` : (main) sort order
 * - `:second_sort` : second sort order
 * - `:column_options` : all column options of all columns
 * - or the name of a column, to save all of its options
 *
 * @elements can also be %NULL to ask which elements to save.
 *
 * Returns: %TRUE if everything was saved without error, else %FALSE
 */
gboolean
donna_tree_view_save_to_config (DonnaTreeView      *tree,
                                const gchar        *elements,
                                GError            **error)
{
    GError *err = NULL;
    DonnaTreeViewPrivate *priv;
    DonnaConfig *config;
    DonnaColumnOptionInfo *oi;
    guint i;
    GString *str = NULL;
    enum save save = 0;
    GPtrArray *arr = NULL;
    gchar buf[32], *b = buf;
    const gchar *s;

    g_return_val_if_fail (DONNA_IS_TREE_VIEW (tree), FALSE);
    priv = tree->priv;

    /* first, determine what needs to be saved */
    if (!elements)
    {
        /* ask user */
        GMainLoop *loop;
        struct save_data data = { NULL, NULL, NULL, NULL, &save, &arr };
        GtkWindow *win;
        GtkBox *main_box;
        GtkBox *box;
        GtkWidget *w;
        GSList *l;

        data.window = gtk_window_new (GTK_WINDOW_TOPLEVEL);
        g_signal_connect_swapped (data.window, "key-press-event",
                (GCallback) key_pressed, &data);
        gtk_widget_set_name (data.window, "save-to-config");
        win = (GtkWindow *) data.window;
        donna_app_add_window (priv->app, win, TRUE);
        /* modal to prevent user for possibly doing things that would "mess
         * things up" such as toggling columns or changing location/arrangement.
         * Because otherwise we'd be referencing columns that don't exist
         * anymore and likely segfault...
         * XXX with socket, we'll have to figure out how to handle this */
        gtk_window_set_modal (win, TRUE);
        gtk_window_set_decorated (win, FALSE);
        gtk_window_set_position (win, GTK_WIN_POS_CENTER_ON_PARENT);
        gtk_window_set_resizable (win, FALSE);
        g_object_set (win, "border-width", 6, NULL);
        g_object_ref_sink (win);

        main_box = (GtkBox *) gtk_box_new (GTK_ORIENTATION_VERTICAL, 0);
        gtk_container_add ((GtkContainer *) win, (GtkWidget *) main_box);
        w = gtk_label_new ("Select the elements to save to configuration:");
        gtk_box_pack_start (main_box, w, FALSE, FALSE, 4);

        box = (GtkBox *) gtk_box_new (GTK_ORIENTATION_VERTICAL, 0);
        data.c_box = (GtkContainer *) box;
        gtk_box_pack_start (main_box, (GtkWidget *) box, FALSE, FALSE, 0);

        w = gtk_check_button_new_with_label ("TreeView Options");
        g_object_set_data ((GObject *) w, "_save",
                GUINT_TO_POINTER (SAVE_OPTIONS));
        g_object_set (w, "active", TRUE, NULL);
        g_signal_connect (w, "toggled", (GCallback) save_toggled, &data);
        gtk_box_pack_start (box, w, FALSE, FALSE, 0);

        w = gtk_check_button_new_with_label ("Columns (Layout)");
        g_object_set_data ((GObject *) w, "_save",
                GUINT_TO_POINTER (SAVE_COLUMNS));
        g_object_set (w, "active", TRUE, NULL);
        g_signal_connect (w, "toggled", (GCallback) save_toggled, &data);
        gtk_box_pack_start (box, w, FALSE, FALSE, 0);

        w = gtk_check_button_new_with_label ("Sort");
        g_object_set_data ((GObject *) w, "_save",
                GUINT_TO_POINTER (SAVE_SORT));
        g_object_set (w, "active", TRUE, NULL);
        g_signal_connect (w, "toggled", (GCallback) save_toggled, &data);
        gtk_box_pack_start (box, w, FALSE, FALSE, 0);

        w = gtk_check_button_new_with_label ("Second Sort");
        g_object_set_data ((GObject *) w, "_save",
                GUINT_TO_POINTER (SAVE_SECOND_SORT));
        g_object_set (w, "active", TRUE, NULL);
        g_signal_connect (w, "toggled", (GCallback) save_toggled, &data);
        gtk_box_pack_start (box, w, FALSE, FALSE, 0);

        for (l = priv->columns; l; l = l->next)
        {
            struct column *_col = l->data;
            gchar *lbl;

            lbl = g_strdup_printf ("Column '%s' Options",
                    gtk_tree_view_column_get_title (_col->column));
            w = gtk_check_button_new_with_label (lbl);
            g_free (lbl);
            g_object_set_data ((GObject *) w, "_save_column", _col);
            g_object_set (w, "active", TRUE, NULL);
            g_signal_connect (w, "toggled", (GCallback) save_toggled, &data);
            gtk_box_pack_start (box, w, FALSE, FALSE, 0);
        }

        w = gtk_box_new (GTK_ORIENTATION_HORIZONTAL, 0);
        g_object_set (w, "margin-top", 10, NULL);
        gtk_box_pack_start (main_box, w, FALSE, FALSE, 0);
        box = (GtkBox *) w;

        w = data.save_btn = gtk_button_new_with_label ("Save to Configuration");
        gtk_widget_set_can_default (w, TRUE);
        gtk_window_set_default ((GtkWindow *) data.window, w);
        gtk_button_set_image ((GtkButton *) w,
                gtk_image_new_from_icon_name ("gtk-apply", GTK_ICON_SIZE_MENU));
        g_signal_connect (w, "clicked", (GCallback) save_cb, &data);
        gtk_box_pack_end (box, w, FALSE, FALSE, 3);

        w = data.cancel_btn = gtk_button_new_with_label ("Cancel");
        gtk_button_set_image ((GtkButton *) w,
                gtk_image_new_from_icon_name ("gtk-cancel", GTK_ICON_SIZE_MENU));
        g_signal_connect_swapped (w, "clicked",
                (GCallback) gtk_widget_destroy, win);
        gtk_box_pack_end (box, w, FALSE, FALSE, 3);

        loop = g_main_loop_new (NULL, TRUE);
        g_signal_connect_swapped (win, "destroy", (GCallback) g_main_loop_quit, loop);
        gtk_widget_show_all ((GtkWidget *) win);
        g_main_loop_run (loop);

        if (save == 0 && !arr)
            /* user cancel */
            return TRUE;
    }
    else
        for (;;)
        {
            skip_blank (elements);
            s = elements;
            for ( ; !(isblank (*s) || *s == ',' || *s == '\0'); ++s)
                ;
            if (s - elements >= 32)
                b = g_strndup (elements, (gsize) (s - elements));
            else
            {
                memcpy (buf, elements, (gsize) (s - elements) * sizeof (gchar));
                buf[s - elements] = '\0';
            }

            if (streq (b, ":options"))
                save |= SAVE_OPTIONS;
            else if (streq (b, ":columns"))
                save |= SAVE_COLUMNS;
            else if (streq (b, ":sort"))
                save |= SAVE_SORT;
            else if (streq (b, ":second_sort"))
                save |= SAVE_SECOND_SORT;
            else if (streq (b, ":column_options"))
                save |= SAVE_COLUMN_OPTIONS;
            else
            {
                struct column *_col;

                _col = get_column_by_name (tree, b);
                if (!_col)
                {
                    g_set_error (error, DONNA_TREE_VIEW_ERROR,
                            DONNA_TREE_VIEW_ERROR_NOT_FOUND,
                            "TreeView '%s': Cannot save to config, unknown column '%s'",
                            priv->name, b);
                    if (arr)
                        g_ptr_array_unref (arr);
                    if (b != buf)
                        g_free (b);
                    return FALSE;
                }

                if (!arr)
                    arr = g_ptr_array_new ();
                g_ptr_array_add (arr, _col);
            }

            if (b != buf)
            {
                g_free (b);
                b = buf;
            }

            skip_blank (s);
            if (*s == '\0')
                break;
        }

    /* now we can actually save things */
    config = donna_app_peek_config (priv->app);
    /* now we're gonna save some options to the config. Set a flag so we ignore
     * any signal from the config, otherwise we would trigger refresh of ct-data
     * and lose the very in-memory settings we're supposed to save...
     * Yes, we could also miss some legit signals, if another thread happens to
     * also change relevant config options at the same time. Very unlikely
     * though, and wouldn't cause that much issue. Too much trouble addressing
     * it, so we don't. */
    priv->saving_config = TRUE;

    if (save & SAVE_OPTIONS)
    {
        gboolean go_again = TRUE;

        i = G_N_ELEMENTS (_tv_options);
        oi = _tv_options;

again:
        for ( ; i > 0; --i, ++oi)
        {
            if (!donna_tree_view_set_option (tree, oi->name, NULL,
                        DONNA_TREE_VIEW_OPTION_SAVE_IN_CURRENT, &err))
            {
                if (!str)
                    str = g_string_new (NULL);
                g_string_append_printf (str, "\n- Failed to set treeview option '%s': %s",
                        oi->name, (err) ? err->message : "(no error message)");
                g_clear_error (&err);
            }
        }

        if (go_again)
        {
            if (priv->is_tree)
            {
                i = G_N_ELEMENTS (_tree_options);
                oi = _tree_options;
            }
            else
            {
                i = G_N_ELEMENTS (_list_options);
                oi = _list_options;
            }
            go_again = FALSE;
            goto again;
        }
    }

    if (save & SAVE_COLUMNS)
    {
        GString *str_col;
        GList *list, *l;

        str_col = g_string_new (NULL);
        /* get them from treeview to preserve the current order */
        list = gtk_tree_view_get_columns ((GtkTreeView *) tree);
        for (l = list; l; l = l->next)
        {
            struct column *_c = get_column_by_column (tree, l->data);
            if (!_c)
                /* blankcol */
                continue;
            g_string_append (str_col, _c->name);
            g_string_append_c (str_col, ',');
        }
        g_list_free (list);
        g_string_truncate (str_col, str_col->len - 1);

        if (G_UNLIKELY (!priv->arrangement))
        {
            if (!str)
                str = g_string_new (NULL);
            g_string_append_printf (str, "\n- Failed to save columns: "
                    "No arrangement loaded");
        }
        else if (G_UNLIKELY (!priv->arrangement->columns_source))
        {
            if (!str)
                str = g_string_new (NULL);
            g_string_append_printf (str, "\n- Failed to save columns: "
                    "No columns source in arrangement");
        }
        else if (!donna_config_set_string (config, &err, str_col->str,
                "%s/columns", priv->arrangement->columns_source))
        {
            if (!str)
                str = g_string_new (NULL);
            g_string_append_printf (str, "\n- Failed to save columns: %s",
                    (err) ? err->message : "(no error message)");
            g_clear_error (&err);
        }

        g_string_free (str_col, TRUE);
    }

    if (save & SAVE_SORT)
    {
        if (G_UNLIKELY (!priv->arrangement))
        {
            if (!str)
                str = g_string_new (NULL);
            g_string_append_printf (str, "\n- Failed to save sort: "
                    "No arrangement loaded");
        }
        else if (G_UNLIKELY (!priv->arrangement->sort_source))
        {
            if (!str)
                str = g_string_new (NULL);
            g_string_append_printf (str, "\n- Failed to save sort: "
                    "No sort source in arrangement");
        }
        else if (G_UNLIKELY (!priv->sort_column))
        {
            if (!str)
                str = g_string_new (NULL);
            g_string_append_printf (str, "\n- Failed to save sort: "
                    "No sort defined");
        }
        else
        {
            if (!donna_config_set_string (config, &err,
                        get_column_by_column (tree, priv->sort_column)->name,
                        "%s/sort_column", priv->arrangement->sort_source))
            {
                if (!str)
                    str = g_string_new (NULL);
                g_string_append_printf (str, "\n- Failed to save sort column: %s",
                        (err) ? err->message : "(no error message)");
                g_clear_error (&err);
            }
            else if (priv->arrangement->sort_order != DONNA_SORT_UNKNOWN)
            {
                GtkSortType order;
                gint id;

                gtk_tree_sortable_get_sort_column_id ((GtkTreeSortable *) priv->store,
                        &id, &order);
                if (!donna_config_set_int (config, &err,
                            (order == GTK_SORT_ASCENDING) ? DONNA_SORT_ASC : DONNA_SORT_DESC,
                            "%s/sort_order", priv->arrangement->sort_source))
                {
                    if (!str)
                        str = g_string_new (NULL);
                    g_string_append_printf (str, "\n- Failed to save sort order: %s",
                            (err) ? err->message : "(no error message)");
                    g_clear_error (&err);
                }
            }
        }
    }

    if (save & SAVE_SECOND_SORT)
    {
        if (G_UNLIKELY (!priv->arrangement))
        {
            if (!str)
                str = g_string_new (NULL);
            g_string_append_printf (str, "\n- Failed to save second sort: "
                    "No arrangement loaded");
        }

        if (priv->second_sort_column)
        {
            gchar *source;

            if (priv->arrangement->second_sort_source)
                source = priv->arrangement->second_sort_source;
            else
                source = priv->arrangement->sort_source;

            if (G_UNLIKELY (!source))
            {
                if (!str)
                    str = g_string_new (NULL);
                g_string_append_printf (str, "\n- Failed to save second sort: "
                        "No sort source in arrangement");
            }
            else if (!donna_config_set_string (config, &err,
                        get_column_by_column (tree, priv->second_sort_column)->name,
                        "%s/second_sort_column", source))
            {
                if (!str)
                    str = g_string_new (NULL);
                g_string_append_printf (str, "\n- Failed to save second sort column: %s",
                        (err) ? err->message : "(no error message)");
                g_clear_error (&err);
            }
            else if (priv->arrangement->second_sort_order != DONNA_SORT_UNKNOWN)
            {
                if (!donna_config_set_int (config, &err,
                            (priv->second_sort_order == GTK_SORT_ASCENDING)
                            ? DONNA_SORT_ASC : DONNA_SORT_DESC,
                            "%s/second_sort_order", source))
                {
                    if (!str)
                        str = g_string_new (NULL);
                    g_string_append_printf (str, "\n- Failed to save second sort order: %s",
                            (err) ? err->message : "(no error message)");
                    g_clear_error (&err);
                }
            }
        }
        else if (priv->arrangement->second_sort_column
                && priv->arrangement->second_sort_source)
        {
            if (!donna_config_remove_option (config, &err, "%s/second_sort_column",
                        priv->arrangement->second_sort_source))
            {
                if (!str)
                    str = g_string_new (NULL);
                g_string_append_printf (str, "\n- Failed to remove second sort: %s",
                        (err) ? err->message : "(no error message)");
                g_clear_error (&err);
            }
        }
    }

    if (save & SAVE_COLUMN_OPTIONS || arr)
    {
        GSList *l = NULL;

        if (arr)
            i = 0;
        else
            l = priv->columns;

        for (;;)
        {
            struct column *_col;
            guint nb_options;

            if (arr)
            {
                if (i >= arr->len)
                    break;
                _col = arr->pdata[i];
            }
            else
            {
                if (!l)
                    break;
                _col= l->data;
            }

            if (!donna_tree_view_column_set_option (tree, _col->name, "title",
                        NULL, DONNA_TREE_VIEW_OPTION_SAVE_IN_CURRENT, &err))
            {
                if (!str)
                    str = g_string_new (NULL);
                g_string_append_printf (str,
                        "\n- Failed to set option '%s' for column '%s': %s",
                        "title",
                        _col->name,
                        (err) ? err->message : "(no error message)");
                g_clear_error (&err);
            }
            if (!donna_tree_view_column_set_option (tree, _col->name, "width",
                        NULL, DONNA_TREE_VIEW_OPTION_SAVE_IN_CURRENT, &err))
            {
                if (!str)
                    str = g_string_new (NULL);
                g_string_append_printf (str,
                        "\n- Failed to set option '%s' for column '%s': %s",
                        "width",
                        _col->name,
                        (err) ? err->message : "(no error message)");
                g_clear_error (&err);
            }
            if (!donna_tree_view_column_set_option (tree, _col->name, "refresh_properties",
                        NULL, DONNA_TREE_VIEW_OPTION_SAVE_IN_CURRENT, &err))
            {
                if (!str)
                    str = g_string_new (NULL);
                g_string_append_printf (str,
                        "\n- Failed to set option '%s' for column '%s': %s",
                        "refresh_properties",
                        _col->name,
                        (err) ? err->message : "(no error message)");
                g_clear_error (&err);
            }
            donna_column_type_get_options (_col->ct, &oi, &nb_options);
            for ( ; nb_options > 0; --nb_options, ++oi)
            {
                if (!donna_tree_view_column_set_option (tree, _col->name, oi->name,
                            NULL, DONNA_TREE_VIEW_OPTION_SAVE_IN_CURRENT, &err))
                {
                    if (!str)
                        str = g_string_new (NULL);
                    g_string_append_printf (str,
                            "\n- Failed to set option '%s' for column '%s': %s",
                            oi->name,
                            _col->name,
                            (err) ? err->message : "(no error message)");
                    g_clear_error (&err);
                }
            }

            if (arr)
                ++i;
            else
                l = l->next;
        }
        if (arr)
            g_ptr_array_unref (arr);
    }

    priv->saving_config = FALSE;

    if (str)
    {
        g_set_error (error, DONNA_TREE_VIEW_ERROR,
                DONNA_TREE_VIEW_ERROR_OTHER,
                "TreeView '%s': Failed to save the following to configuration:\n%s",
                priv->name, str->str);
        g_string_free (str, TRUE);
        return FALSE;
    }

    str = g_string_new (NULL);
    g_string_append_printf (str, "TreeView '%s': ", priv->name);
    if (save & SAVE_OPTIONS)
        g_string_append (str, "treeview options, ");
    if (save & SAVE_COLUMNS)
        g_string_append (str, "columns, ");
    if (save & SAVE_SORT)
        g_string_append (str, "sort, ");
    if (save & SAVE_SECOND_SORT)
        g_string_append (str, "second sort, ");
    if (save & SAVE_COLUMN_OPTIONS || arr)
        g_string_append (str, "columns options, ");
    g_string_truncate (str, str->len - 2);
    g_string_append (str, " saved to config");
    g_info ("%s", str->str);
    g_string_free (str, TRUE);

    return TRUE;
}

/* mode list only */
/**
 * donna_tree_view_set_visual_filter:
 * @tree: A #DonnaTreeView
 * @filter: (allow-none): The filter to use as visual filter, or %NULL
 * @toggle: If %TRUE and @filter is the same as current VF, then unsets VF
 * @error: (allow-none): Return location of a #GError, or %NULL
 *
 * Sets @filter as current visual filter on @tree. If @filter is %NULL then
 * unsets any currently applied VF.
 *
 * This only applies to lists (VF aren't supported on trees).
 *
 * See #DonnaFilter for more about filters.
 *
 * Returns: %TRUE if the filter was set, else %FALSE
 */
gboolean
donna_tree_view_set_visual_filter (DonnaTreeView      *tree,
                                   DonnaFilter        *filter,
                                   gboolean            toggle,
                                   GError            **error)
{
    DonnaTreeViewPrivate *priv;

    g_return_val_if_fail (DONNA_IS_TREE_VIEW (tree), FALSE);
    g_return_val_if_fail (filter == NULL || DONNA_IS_FILTER (filter), FALSE);
    priv = tree->priv;

    if (priv->is_tree)
    {
        g_set_error (error, DONNA_TREE_VIEW_ERROR,
                DONNA_TREE_VIEW_ERROR_INVALID_MODE,
                "TreeView '%s': Cannot set visual filter on tree",
                priv->name);
        return FALSE;
    }

    if (!filter && !priv->filter)
        return TRUE;

    if (filter)
    {
        if (priv->filter && toggle && priv->filter == filter)
            filter = NULL;

        if (filter && !donna_filter_is_compiled (filter)
                && !donna_filter_compile (filter, error))
        {
            g_prefix_error (error, "TreeView '%s': Failed to set current visual filter: ",
                    priv->name);
            return FALSE;
        }
    }

    donna_g_object_unref (priv->filter);
    priv->filter = (filter) ? g_object_ref (filter) : NULL;
    refilter_list (tree);
    check_statuses (tree, STATUS_CHANGED_ON_VF);
    return TRUE;
}

/**
 * donna_tree_view_get_visual_filter:
 * @tree: A #DonnaTreeView
 * @error: (allow-none): Return location of a #GError, or %NULL
 *
 * Returns the current visual filter, or %NULL
 *
 * Note that you need to use @error to determine if there's no filter, or an
 * error occured. Also note that the only possible error is a
 * %DONNA_TREE_VIEW_ERROR_INVALID_MODE is used on a tree, as VFs are only
 * supported on lists.
 *
 * Returns: (transfer full): The #DonnaFilter currently set as VF, or %NULL if
 * none or on error
 */
DonnaFilter *
donna_tree_view_get_visual_filter (DonnaTreeView      *tree,
                                   GError            **error)
{
    DonnaTreeViewPrivate *priv;

    g_return_val_if_fail (DONNA_IS_TREE_VIEW (tree), NULL);
    priv = tree->priv;

    if (priv->is_tree)
    {
        g_set_error (error, DONNA_TREE_VIEW_ERROR,
                DONNA_TREE_VIEW_ERROR_INVALID_MODE,
                "TreeView '%s': Cannot set visual filter on tree",
                priv->name);
        return NULL;
    }

    if (priv->filter)
        return g_object_ref (priv->filter);
    else
        return NULL;
}

/**
 * donna_tree_view_column_refresh_nodes:
 * @tree: A #DonnaTreeView
 * @rowid: Identifier of a row; See #rowid for more
 * @to_focused: When %TRUE rows affected will be the range from @rowid to the
 * focused row
 * @column: Name of the column
 * @error: (allow-none): Return location of a #GError, or %NULL
 *
 * Refreshes the properties used by @column on the specified rows.
 *
 * This is especially useful with #custom-peroperties and columns with option
 * `refresh_properties` set to "on_demand" i.e.  properties aren't refreshed
 * automatically.
 *
 * Returns: %TRUE on success, else %FALSE. Note that success means refreshing
 * tasks has been started, regardless of their success or not (as they might
 * still be running when the function returns)
 */
gboolean
donna_tree_view_column_refresh_nodes (DonnaTreeView      *tree,
                                      DonnaRowId         *rowid,
                                      gboolean            to_focused,
                                      const gchar        *column,
                                      GError            **error)
{
    DonnaTreeViewPrivate *priv;
    struct column *_col;
    GPtrArray *tasks = NULL;
    GPtrArray *nodes;
    GPtrArray *props;
    guint i;

    g_return_val_if_fail (DONNA_IS_TREE_VIEW (tree), FALSE);
    g_return_val_if_fail (column != NULL, FALSE);
    g_return_val_if_fail (rowid != NULL, FALSE);
    priv = tree->priv;

    if (priv->is_tree)
    {
        g_set_error (error, DONNA_TREE_VIEW_ERROR,
                DONNA_TREE_VIEW_ERROR_INVALID_MODE,
                "TreeView '%s': Cannot refresh column properties on tree",
                priv->name);
        return FALSE;
    }

    _col = get_column_from_name (tree, column, error);
    if (!_col)
        return FALSE;

    nodes = donna_tree_view_get_nodes (tree, rowid, to_focused, error);
    if (!nodes)
        return FALSE;

    props = donna_column_type_get_props (_col->ct, _col->ct_data);
    if (G_UNLIKELY (!props))
    {
        g_ptr_array_unref (nodes);
        return TRUE;
    }

    for (i = 0; i < nodes->len; ++i)
    {
        DonnaNode *node = nodes->pdata[i];
        GPtrArray *arr;
        guint j;

        /* donna_node_refresh_arr_tasks_arr() will unref props, but we want to
         * keep it alive */
        g_ptr_array_ref (props);
        arr = donna_node_refresh_arr_tasks_arr (node, tasks, props, NULL);
        if (G_UNLIKELY (!arr))
            continue;
        else if (!tasks)
            tasks = arr;

        for (j = 0; j < tasks->len; ++j)
            donna_app_run_task (priv->app, (DonnaTask *) tasks->pdata[j]);
        if (tasks->len > 0)
            g_ptr_array_remove_range (tasks, 0, tasks->len);
    }

    if (tasks)
        g_ptr_array_unref (tasks);
    g_ptr_array_unref (props);
    g_ptr_array_unref (nodes);
    return TRUE;
}


/* mode list only */
GPtrArray *
donna_tree_view_get_children (DonnaTreeView      *tree,
                              DonnaNode          *node,
                              DonnaNodeType       node_types)
{
    DonnaTreeViewPrivate *priv;
    GList *list, *l;
    GPtrArray *arr;

    g_return_val_if_fail (DONNA_IS_TREE_VIEW (tree), NULL);
    g_return_val_if_fail (!tree->priv->is_tree, NULL);

    priv = tree->priv;

    if (node != priv->location)
        return NULL;

    if (!(node_types & priv->node_types))
        return NULL;

    /* list changing location, already cleared the children */
    if (!priv->is_tree && tree->priv->cl >= CHANGING_LOCATION_SLOW)
        return NULL;

    /* get list of nodes we have in tree */
    list = g_hash_table_get_keys (priv->hashtable);
    /* create an array that could hold them all */
    arr = g_ptr_array_new_full (g_hash_table_size (priv->hashtable),
            g_object_unref);
    /* fill array based on requested node_types */
    for (l = list ; l; l = l->next)
    {
        if (donna_node_get_node_type (l->data) & node_types)
            g_ptr_array_add (arr, g_object_ref (l->data));
    }
    g_list_free (list);

    return arr;
}

static gboolean
query_tooltip_cb (GtkTreeView   *treev,
                  gint           x,
                  gint           y,
                  gboolean       keyboard_mode,
                  GtkTooltip    *tooltip)
{
    DonnaTreeView *tree = DONNA_TREE_VIEW (treev);
    GtkTreeViewColumn *column;
#ifdef GTK_IS_JJK
    GtkCellRenderer *renderer;
#endif
    GtkTreeModel *model;
    GtkTreeIter iter;
    gboolean ret = FALSE;

    /* x & y are widget coords, converted to bin_window coords */
    if (gtk_tree_view_get_tooltip_context (treev, &x, &y, keyboard_mode,
                &model, NULL, &iter))
    {
#ifdef GTK_IS_JJK
        if (!gtk_tree_view_is_blank_at_pos_full (treev, x, y, NULL, &column,
                    &renderer, NULL, NULL))
#else
        if (!gtk_tree_view_is_blank_at_pos (treev, x, y, NULL, &column, NULL, NULL))
#endif
        {
            DonnaNode *node;
            struct column *_col;
            guint index = 0;

            gtk_tree_model_get (model, &iter,
                    TREE_VIEW_COL_NODE, &node,
                    -1);
            if (!node)
                return FALSE;

#ifdef GTK_IS_JJK
            if (renderer == int_renderers[INTERNAL_RENDERER_SPINNER])
                return FALSE;
            else if (renderer == int_renderers[INTERNAL_RENDERER_PIXBUF])
            {
                struct active_spinners *as;
                guint i;

                as = get_as_for_node (tree, node, NULL, FALSE);
                if (!as)
                {
                    /* no as and a visible renderer == RP_ON_DEMAND */
                    gtk_tooltip_set_text (tooltip, "Click to refresh needed properties");
                    g_object_unref (node);
                    return TRUE;
                }

                for (i = 0; i < as->as_cols->len; ++i)
                {
                    struct as_col *as_col;
                    GString *str;

                    as_col = &g_array_index (as->as_cols, struct as_col, i);
                    if (as_col->column != column)
                        continue;

                    str = g_string_new (NULL);
                    for (i = 0; i < as_col->tasks->len; ++i)
                    {
                        DonnaTask *task = as_col->tasks->pdata[i];

                        if (donna_task_get_state (task) == DONNA_TASK_FAILED)
                        {
                            const GError *err;

                            if (str->len > 0)
                                g_string_append_c (str, '\n');

                            err = donna_task_get_error (task);
                            if (err)
                                g_string_append (str, err->message);
                            else
                                g_string_append (str, "Task failed, no error message");
                        }
                    }

                    i = str->len > 0;
                    if (i)
                        gtk_tooltip_set_text (tooltip, str->str);
                    g_string_free (str, TRUE);
                    return (gboolean) i;
                }
                return FALSE;
            }
#endif
            _col = get_column_by_column (tree, column);

#ifdef GTK_IS_JJK
            if (renderer)
            {
                const gchar *rend;

                rend = donna_column_type_get_renderers (_col->ct);
                if (rend[1] == '\0')
                    /* only one renderer in this column */
                    index = 1;
                else
                {
                    gchar r;

                    r = (gchar) GPOINTER_TO_INT (g_object_get_data (
                                G_OBJECT (renderer), "renderer-type"));
                    for (index = 1; *rend && *rend != r; ++index, ++rend)
                        ;
                }
            }
#else
            /* because (only) in vanilla, we could be there for the blank
             * column, which isn't in our internal list of columns */
            if (_col)
            {
                if (is_col_node_need_refresh (tree, _col, node))
                {
                    gtk_tooltip_set_text (tooltip, "Click to refresh needed properties");
                    g_object_unref (node);
                    return TRUE;
                }
                else
                {
#endif
            ret = donna_column_type_set_tooltip (_col->ct, _col->ct_data,
                    index, node, tooltip);
#ifndef GTK_IS_JJK
                }
            }
#endif

            g_object_unref (node);
        }
    }
    return ret;
}

static void
donna_tree_view_row_activated (GtkTreeView    *treev,
                               GtkTreePath    *path,
                               GtkTreeViewColumn *column)
{
    DonnaTreeView *tree = (DonnaTreeView *) treev;
    DonnaRowId rowid;

    /* warning because this shouldn't happen, as we're doing things our own way.
     * If this happens, it's probably an oversight somewhere that should be
     * fixed. So warning, and then we just do our ativating */
    g_warning ("TreeView '%s': row-activated signal was emitted", tree->priv->name);

    rowid.type = DONNA_ARG_TYPE_PATH;
    rowid.ptr  = gtk_tree_path_to_string (path);
    donna_tree_view_activate_row (tree, &rowid, NULL);
    g_free (rowid.ptr);
}

static void
check_children_post_expand (DonnaTreeView *tree, GtkTreeIter *iter)
{
    DonnaTreeViewPrivate *priv = tree->priv;
    GtkTreeModel *model = (GtkTreeModel *) priv->store;
    DonnaNode *loc_node;
    DonnaProvider *loc_provider;
    gchar *loc_location;
    GtkTreeIter child;

    /* don't do this when we're not sync, otherwise collapsing a row where we
     * put the focus would trigger a selection which we don't want */
    if (priv->sync_mode == TREE_SYNC_NONE)
        return;

    /* no need to do anything if we do have a current location */
    if (priv->location_iter.stamp != 0)
        return;

    if (G_UNLIKELY (!gtk_tree_model_iter_children (model, &child, iter)))
        return;

    loc_node = donna_tree_view_get_location (priv->sync_with);
    if (G_UNLIKELY (!loc_node))
        return;
    loc_provider = donna_node_peek_provider (loc_node);
    loc_location = donna_node_get_location (loc_node);
    do
    {
        DonnaNode *n;

        gtk_tree_model_get (model, &child,
                TREE_COL_NODE,  &n,
                -1);
        if (G_UNLIKELY (!n))
            continue;

        /* did we just revealed the node or one of its parent? */
        if (n == loc_node || is_node_ancestor (n, loc_node,
                    loc_provider, loc_location))
        {
            GtkTreeView *treev = (GtkTreeView *) tree;
            GtkTreeSelection *sel;
            GtkTreePath *loc_path;

            loc_path = gtk_tree_model_get_path (model, &child);
            gtk_tree_view_set_focused_row (treev, loc_path);
            if (n == loc_node)
            {
                sel = gtk_tree_view_get_selection (treev);
                gtk_tree_selection_select_path (sel, loc_path);
            }
            gtk_tree_path_free (loc_path);

            if (priv->sync_scroll)
                scroll_to_iter (tree, &child);

            g_object_unref (n);
            break;
        }
        g_object_unref (n);
    } while (gtk_tree_model_iter_next (model, &child));

    g_free (loc_location);
    g_object_unref (loc_node);
}

#define is_regular_left_click(click, event)             \
    ((click & (DONNA_CLICK_SINGLE | DONNA_CLICK_LEFT))  \
     == (DONNA_CLICK_SINGLE | DONNA_CLICK_LEFT)         \
     && !(event->state & (GDK_CONTROL_MASK | GDK_SHIFT_MASK)))

static void
tree_conv_custom (const gchar         c,
                  gchar              *extra,
                  DonnaContextOptions options,
                  GString            *str,
                  struct conv        *conv)
{
    if (c == 'k')
    {
        if (conv->key_spec > 0)
            g_string_append_c (str, conv->key_spec);
    }
    else /* 'c' */
    {
        if (conv->tree->priv->key_combine_spec > 0)
            g_string_append_c (str, conv->tree->priv->key_combine_spec);
    }
}

static gboolean
tree_conv_flag (const gchar      c,
                gchar           *extra,
                DonnaArgType    *type,
                gpointer        *ptr,
                GDestroyNotify  *destroy,
                struct conv     *conv)
{
    DonnaTreeViewPrivate *priv = conv->tree->priv;

    switch (c)
    {
        case 'o':
            *type = DONNA_ARG_TYPE_TREE_VIEW;
            *ptr = conv->tree;
            return TRUE;

        case 'l':
            if (G_UNLIKELY (!priv->location))
                return FALSE;
            *type = DONNA_ARG_TYPE_NODE;
            *ptr = priv->location;
            return TRUE;

        case 'R':
            if (G_UNLIKELY (!conv->col_name))
                return FALSE;
            *type = DONNA_ARG_TYPE_STRING;
            *ptr = conv->col_name;
            return TRUE;

        case 'r':
            if (G_UNLIKELY (!conv->row))
                return FALSE;
            *type = DONNA_ARG_TYPE_ROW;
            *ptr = conv->row;
            return TRUE;

        case 'n':
            if (G_UNLIKELY (!conv->row))
                return FALSE;
            *type = DONNA_ARG_TYPE_NODE;
            *ptr = conv->row->node;
            return TRUE;

        case 'f':
            {
                GtkTreePath *path;
                GtkTreeIter iter;

focused:
                gtk_tree_view_get_cursor ((GtkTreeView *) conv->tree, &path, NULL);
                if (G_UNLIKELY (!path))
                    return FALSE;
                if (!gtk_tree_model_get_iter ((GtkTreeModel *) priv->store,
                            &iter, path))
                {
                    gtk_tree_path_free (path);
                    return FALSE;
                }
                gtk_tree_path_free (path);
                gtk_tree_model_get ((GtkTreeModel *) priv->store, &iter,
                        TREE_VIEW_COL_NODE, ptr,
                        -1);
                if (G_UNLIKELY (!*ptr))
                    return FALSE;
                *type = DONNA_ARG_TYPE_NODE;
                *destroy = g_object_unref;
                return TRUE;
            }

        /* mode list only */
        case 's':
        case 'S':
            if (priv->is_tree)
                return FALSE;
            *ptr = donna_tree_view_get_selected_nodes (conv->tree, NULL);
            if (!*ptr)
            {
                if (c == 'S')
                    /* fallback to focused node */
                    goto focused;
                else
                    /* empty array */
                    *ptr = g_ptr_array_sized_new (0);
            }
            *type = DONNA_ARG_TYPE_NODE | DONNA_ARG_IS_ARRAY;
            *destroy = (GDestroyNotify) g_ptr_array_unref;
            return TRUE;

        case 'm':
            *type = DONNA_ARG_TYPE_INT;
            *ptr = &conv->key_m;
            return TRUE;

        case 'k':
        case 'c':
            /* CUSTOM because as STRING they would get quoted and we don't want
             * that, so here we can add nothing/only the char */
            *type = _DONNA_ARG_TYPE_CUSTOM;
            *ptr = tree_conv_custom;
            return TRUE;
    }

    return FALSE;
}

static gboolean
get_click (DonnaConfig  *config,
           const gchar  *click_mode,
           gboolean      is_selected,
           const gchar  *col_name,
           const gchar  *click,
           gboolean      is_on_rls,
           gpointer      ret)
{
    gchar *fallback = NULL;
    typedef gboolean (*config_get_fn) (DonnaConfig   *config,
                                       GError       **error,
                                       gpointer       retval,
                                       const gchar   *fmt,
                                       ...);
    config_get_fn config_get;

    if (is_on_rls)
        config_get = (config_get_fn) donna_config_get_boolean;
    else
        config_get = (config_get_fn) donna_config_get_string;

again:

    /* first we look for column-specific value */
    if (col_name)
    {
        if (config_get (config, NULL, ret,
                    "click_modes/%s/columns/%s/%s%s",
                    click_mode,
                    col_name,
                    (is_selected) ? "selected/" : "",
                    click))
            return TRUE;

        /* nothing, maybe we have a fallback click_mode */
        donna_config_get_string (config, NULL, &fallback,
                "click_modes/%s/fallback",
                click_mode);

        /* try column-specific fallback */
        if (fallback && config_get (config, NULL, ret,
                    "click_modes/%s/columns/%s/%s%s",
                    fallback,
                    col_name,
                    (is_selected) ? "selected/" : "",
                    click))
            return TRUE;
    }

    /* then general/treeview value */
    if (config_get (config, NULL, ret, "click_modes/%s/%s%s",
                click_mode,
                (is_selected) ? "selected/" : "",
                click))
        return TRUE;

    /* if we haven't yet, get fallback name */
    if (!col_name)
        donna_config_get_string (config, NULL, &fallback,
                "click_modes/%s/fallback",
                click_mode);

    /* try general/treeview fallback */
    if (fallback && config_get (config, NULL, ret,
                "click_modes/%s/%s%s",
                fallback,
                (is_selected) ? "selected/" : "",
                click))
        return TRUE;

    /* if we find nothing under selected, try without */
    if (is_selected)
    {
        is_selected = FALSE;
        goto again;
    }

    return FALSE;
}

static gboolean
grab_focus (GtkWidget *wtree)
{
    gtk_widget_grab_focus (wtree);
    return G_SOURCE_REMOVE;
}

static void
handle_click (DonnaTreeView     *tree,
              DonnaClick         click,
              GdkEventButton    *event,
              GtkTreeIter       *iter,
              GtkTreeViewColumn *column,
              GtkCellRenderer   *renderer,
              guint              click_on)
{
    DonnaTreeViewPrivate *priv = tree->priv;
    DonnaConfig *config;
    struct conv conv = { NULL, };
    DonnaContext context = { CONTEXT_FLAGS CONTEXT_COLUMN_FLAGS, FALSE,
        (conv_flag_fn) tree_conv_flag, &conv };
    struct column *_col;
    GPtrArray *intrefs = NULL;
    gchar *fl = NULL;
    gboolean is_selected;
    gchar *click_mode = NULL;
    /* longest possible is "blankcol_ctrl_shift_middle_double_click_on_rls"
     * (len=46); But longest prefix is "colheader_" (len=10) and even though it
     * can't be slow/double and therefore would fit inside 46, we move to 47 (48
     * with NUL) because we need a prefix space of 10 */
    gchar buf[48];
    gchar *b = buf + 10; /* leave space for "blankcol_" prefix */

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
    if (click & DONNA_CLICK_LEFT)
    {
        strcpy (b, "left_");
        b += 5;
    }
    else if (click & DONNA_CLICK_MIDDLE)
    {
        strcpy (b, "middle_");
        b += 7;
    }
    else /* DONNA_CLICK_RIGHT */
    {
        strcpy (b, "right_");
        b += 6;
    }
    /* COLHEADER doesn't do (slow) double clicks */
    if (click_on != CLICK_ON_COLHEADER)
    {
        if (click & DONNA_CLICK_DOUBLE)
        {
            strcpy (b, "double_");
            b += 7;
        }
        else if (click & DONNA_CLICK_SLOW_DOUBLE)
        {
            strcpy (b, "slow_");
            b += 5;
        }
        /* else DONNA_CLICK_SINGLE; we don't print anything for it */
    }
    strcpy (b, "click");

    _col = get_column_by_column (tree, column);
    conv.tree = tree;
    if (_col)
        conv.col_name = _col->name;

    /* test this first, because if also doesn't have an iter */
    if (click_on == CLICK_ON_COLHEADER)
    {
        memcpy (buf, "colheader_", 10 * sizeof (gchar));
        b = buf;
    }
    else if (!iter)
    {
        memcpy (buf + 1, "blankrow_", 9 * sizeof (gchar));
        b = buf + 1;
    }
    else if (!_col)
    {
        memcpy (buf + 1, "blankcol_", 9 * sizeof (gchar));
        b = buf + 1;
    }
    else if (click_on == CLICK_ON_BLANK)
    {
        memcpy (buf + 4, "blank_", 6 * sizeof (gchar));
        b = buf + 4;
    }
    else if (click_on == CLICK_ON_EXPANDER)
    {
        memcpy (buf + 1, "expander_", 9 * sizeof (gchar));
        b = buf + 1;
    }
    else
        b = buf + 10;

    DONNA_DEBUG (TREE_VIEW, priv->name,
            g_debug ("TreeView '%s': handle_click '%s'",
                priv->name, b));

    if (priv->is_tree && iter)
        gtk_tree_model_get ((GtkTreeModel *) priv->store, iter,
                TREE_COL_CLICK_MODE,    &click_mode,
                -1);

    /* list only: different source when the clicked item is selected */
    is_selected = !priv->is_tree && iter && gtk_tree_selection_iter_is_selected (
            gtk_tree_view_get_selection ((GtkTreeView *) tree), iter);

    config = donna_app_peek_config (priv->app);

    if (event->type == GDK_BUTTON_PRESS && !priv->on_release_triggered)
    {
        gboolean on_rls = FALSE;
        gchar *e;

        e = b + strlen (b);
        memcpy (e, "_on_rls", 8 * sizeof (gchar)); /* 8 to include NUL */

        /* should we delay the trigger to button-release ? */
        if (get_click (config, (click_mode) ? click_mode : priv->click_mode,
                    is_selected, conv.col_name, b, TRUE, &on_rls)
                && on_rls)
        {
            priv->on_release_click  = click;
            priv->on_release_x      = event->x;
            priv->on_release_y      = event->y;
            return;
        }
        *e = '\0';
    }

    /* get the trigger */
    get_click (config, (click_mode) ? click_mode : priv->click_mode,
            is_selected, conv.col_name, b, FALSE, &fl);
    g_free (click_mode);

    if (!fl)
        goto done;

    if (iter)
        conv.row = get_row_for_iter (tree, iter);

    fl = donna_app_parse_fl (priv->app, fl, TRUE, &context, &intrefs);

    if (iter)
        g_free (conv.row);

    DONNA_DEBUG (TREE_VIEW, priv->name,
            g_debug ("TreeView '%s': handle_click '%s': trigger=%s",
                priv->name, b, fl));
    donna_app_trigger_fl (priv->app, fl, intrefs, FALSE, NULL);
    g_free (fl);

done:
    if (click_on == CLICK_ON_COLHEADER)
        /* not sure why, but it doesn't work for middle click if called
         * directly, so we use an idle source */
        g_idle_add ((GSourceFunc) grab_focus, tree);
}

/* for obvious reason (grabbing the focus happens here) this can only be called
 * once per click. However, we might call this twice, first checking if a rubber
 * banding operation can start or not, and then when the trigger_click() occurs.
 * The way we handle this is that if tree_might_grab_focus is NULL there will be
 * no focus grabbed, since the rubber banding is list-only and there we don't
 * care about this (i.e. it's NULL) */
static gboolean
skip_focusing_click (DonnaTreeView  *tree,
                     DonnaClick      click,
                     GdkEventButton *event,
                     gboolean       *tree_might_grab_focus)
{
    gboolean might_grab_focus = FALSE;

    /* a click will grab the focus if:
     * - tree: it's a regular left click (i.e. no Ctrl/Shift held) unless
     *   click was on expander
     * - list: it's a left click (event w/ Ctrl/Shift)
     * and, ofc, focus isn't on treeview already. */
    if (tree->priv->is_tree)
        /* might, as we don't know if this was on expander or not yet */
        might_grab_focus = is_regular_left_click (click, event)
            && !gtk_widget_is_focus ((GtkWidget *) tree);
    else if ((click & (DONNA_CLICK_SINGLE | DONNA_CLICK_LEFT))
            == (DONNA_CLICK_SINGLE | DONNA_CLICK_LEFT)
            && !gtk_widget_is_focus ((GtkWidget *) tree))
    {
        GtkWidget *w = NULL;

        if (tree->priv->focusing_click)
        {
            /* get the widget that currently has the focus */
            w = gtk_window_get_focus ((GtkWindow *) gtk_widget_get_toplevel (
                        (GtkWidget *) tree));
            /* We'll "skip" the click if focusing_click is set, unless the
             * widget is a child of ours, e.g. a column header.
             * Call this now because the call to gtk_widget_grab_focus() below
             * could get this widget finalized */
            if (w && gtk_widget_get_ancestor (w, DONNA_TYPE_TREE_VIEW)
                    == (GtkWidget *) tree)
                w = NULL;
        }

        /* see notes above for why */
        if (tree_might_grab_focus)
            gtk_widget_grab_focus ((GtkWidget *) tree);

        if (tree->priv->focusing_click && w)
            return TRUE;
    }

    if (tree_might_grab_focus)
        *tree_might_grab_focus = might_grab_focus;
    return FALSE;
}

static void
refresh_props_for_col (DonnaTreeView    *tree,
                       struct column    *_col,
                       DonnaNode        *node)
{
    DonnaTreeViewPrivate *priv = tree->priv;
    GPtrArray *props = NULL;
    GPtrArray *tasks;
    guint i;

    for (i = 0; i < priv->col_props->len; ++i)
    {
        struct col_prop *cp;

        cp = &g_array_index (priv->col_props, struct col_prop, i);
        if (cp->column == _col->column)
        {
            DonnaNodeHasProp has;

            has = donna_node_has_property (node, cp->prop);
            if ((has & DONNA_NODE_PROP_EXISTS) && !(has & DONNA_NODE_PROP_HAS_VALUE))
            {
                if (!props)
                    props = g_ptr_array_new_with_free_func (g_free);
                g_ptr_array_add (props, g_strdup (cp->prop));
            }
        }
    }

    if (!props)
        return;

    tasks = donna_node_refresh_arr_tasks_arr (node, NULL, props, NULL);
    if (G_UNLIKELY (!tasks))
        return;

    for (i = 0; i < tasks->len;++i)
        donna_app_run_task (priv->app, (DonnaTask *) tasks->pdata[i]);
    g_ptr_array_unref (tasks);
}

static gboolean
trigger_click (DonnaTreeView *tree, DonnaClick click, GdkEventButton *event)
{
    GtkTreeView *treev = (GtkTreeView *) tree;
    DonnaTreeViewPrivate *priv = tree->priv;
    GtkTreeViewColumn *column;
    GtkCellRenderer *renderer = NULL;
    GtkTreeModel *model;
    GtkTreeIter iter;
    gint x, y;
    gboolean tree_might_grab_focus = FALSE;

    if (event->button == 1)
        click |= DONNA_CLICK_LEFT;
    else if (event->button == 2)
        click |= DONNA_CLICK_MIDDLE;
    else if (event->button == 3)
        click |= DONNA_CLICK_RIGHT;

    DONNA_DEBUG (TREE_VIEW, priv->name,
            g_debug ("TreeView '%s': trigger click %d", priv->name, click));

    /* the focus thing only matters on the actual click (i.e. on press), so we
     * ignore it when triggering a click on release */
    if (event->type == GDK_BUTTON_PRESS
            && skip_focusing_click (tree, click, event, &tree_might_grab_focus))
        return FALSE;

    x = (gint) event->x;
    y = (gint) event->y;

    /* event->window == bin_window, so ready for use with the is_blank()
     * functions. For get_context() however we need widget coords */
    gtk_tree_view_convert_bin_window_to_widget_coords (treev, x, y, &x, &y);

    /* it will also convert x & y (back) to bin_window coords */
    if (gtk_tree_view_get_tooltip_context (treev, &x, &y, 0,
                &model, NULL, &iter))
    {
#ifdef GTK_IS_JJK
        if (gtk_tree_view_is_blank_at_pos_full (treev, x, y, NULL, &column,
                    &renderer, NULL, NULL))
#else
        if (gtk_tree_view_is_blank_at_pos (treev, x, y, NULL, &column, NULL, NULL))
#endif
        {
            if (tree_might_grab_focus)
                gtk_widget_grab_focus ((GtkWidget *) tree);
            handle_click (tree, click, event, &iter, column, renderer, CLICK_ON_BLANK);
        }
        else
        {
            DonnaNode *node;
            struct active_spinners *as = NULL;
            guint as_idx;
            guint i;

            gtk_tree_model_get (model, &iter,
                    TREE_VIEW_COL_NODE, &node,
                    -1);
            if (!node)
                /* prevent clicking/selecting a fake node */
                return TRUE;
            /* tree already has a ref on it */
            g_object_unref (node);

#ifdef GTK_IS_JJK
            if (!renderer)
            {
                /* i.e. clicked on an expander (never grab focus) */
                handle_click (tree, click, event, &iter, column, renderer,
                        CLICK_ON_EXPANDER);
                return TRUE;
            }
            else if (renderer == int_renderers[INTERNAL_RENDERER_PIXBUF])
#endif
                as = get_as_for_node (tree, node, &as_idx, FALSE);

            if (!as)
            {
#ifdef GTK_IS_JJK
                if (renderer == int_renderers[INTERNAL_RENDERER_PIXBUF])
#else
                if (is_col_node_need_refresh (tree,
                            get_column_by_column (tree, column), node))
#endif
                    refresh_props_for_col (tree, get_column_by_column (tree, column), node);
                else
                {
                    if (tree_might_grab_focus)
                        gtk_widget_grab_focus ((GtkWidget *) tree);
                    handle_click (tree, click, event, &iter, column, renderer,
                            CLICK_REGULAR);
                }
                return TRUE;
            }

            for (i = 0; i < as->as_cols->len; ++i)
            {
                struct as_col *as_col;
                GSList *list;
                GString *str;
                guint j;

                as_col = &g_array_index (as->as_cols, struct as_col, i);
                if (as_col->column != column)
                    continue;

                str = g_string_new (NULL);
                for (j = 0; j < as_col->tasks->len; )
                {
                    DonnaTask *task = as_col->tasks->pdata[j];

                    if (donna_task_get_state (task) == DONNA_TASK_FAILED)
                    {
                        const GError *err;

                        if (str->len > 0)
                            g_string_append_c (str, '\n');

                        err = donna_task_get_error (task);
                        if (err)
                            g_string_append (str, err->message);
                        else
                            g_string_append (str, "Task failed, no error message");

                        /* this will get the last task in the array to j,
                         * hence no need to move/increment j */
                        g_ptr_array_remove_index_fast (as_col->tasks, j);

                        /* can we remove the as_col? */
                        if (as_col->nb == 0 && as_col->tasks->len == 0)
                        {
                            /* can we remove the whole as? */
                            if (as->as_cols->len == 1)
                                g_ptr_array_remove_index_fast (priv->active_spinners,
                                        as_idx);
                            else
                                g_array_remove_index_fast (as->as_cols, i);

                            break;
                        }
                    }
                    else
                        /* move to next task */
                        ++j;
                }

                if (str->len > 0)
                {
                    GError *err = NULL;
                    gchar *fl = donna_node_get_full_location (node);

                    g_set_error (&err, DONNA_TREE_VIEW_ERROR, DONNA_TREE_VIEW_ERROR_OTHER,
                            "%s", str->str);
                    donna_app_show_error (priv->app, err,
                            "TreeView '%s': Error occured on '%s'",
                            priv->name, fl);
                    g_free (fl);
                    g_error_free (err);
                }
                g_string_free (str, TRUE);

                /* we can safely assume we found/removed a task, so a refresh
                 * is in order for every row of this node */
                if (priv->is_tree)
                {
                    list = g_hash_table_lookup (priv->hashtable, node);
                    for ( ; list; list = list->next)
                    {
                        GtkTreeIter *it = list->data;
                        GtkTreePath *path;

                        path = gtk_tree_model_get_path (model, it);
                        gtk_tree_model_row_changed (model, path, it);
                        gtk_tree_path_free (path);
                    }
                }
                else
                {
                    GtkTreePath *path;

                    path = gtk_tree_model_get_path (model, &iter);
                    gtk_tree_model_row_changed (model, path, &iter);
                    gtk_tree_path_free (path);
                }

                return TRUE;
            }
            if (tree_might_grab_focus)
                gtk_widget_grab_focus ((GtkWidget *) tree);
            /* there was no as for this column */
            handle_click (tree, click, event, &iter, column, renderer, CLICK_REGULAR);
        }
    }
    else
    {
        if (tree_might_grab_focus)
            gtk_widget_grab_focus ((GtkWidget *) tree);
        handle_click (tree, click, event, NULL, NULL, NULL, CLICK_ON_BLANK);
    }
    return TRUE;
}

static gboolean
slow_expired_cb (DonnaTreeView *tree)
{
    DonnaTreeViewPrivate *priv = tree->priv;

    g_source_remove (priv->last_event_timeout);
    priv->last_event_timeout = 0;
    gdk_event_free ((GdkEvent *) priv->last_event);
    priv->last_event = NULL;
    priv->last_event_expired = FALSE;

    return FALSE;
}

static gboolean
single_click_cb (DonnaTreeView *tree)
{
    DonnaTreeViewPrivate *priv = tree->priv;
    guint delay;

    /* single click it is */

    g_source_remove (priv->last_event_timeout);
    priv->last_event_expired = TRUE;
    /* timeout for slow dbl click. If triggered, we can free last_event */
    g_object_get (gtk_settings_get_default (),
            "gtk-double-click-time",    &delay,
            NULL);
    priv->last_event_timeout = g_timeout_add (delay,
            (GSourceFunc) slow_expired_cb, tree);

    /* see button_press_event below for more about this */
    if (priv->last_event->button != 1
            || (priv->last_event->state & (GDK_CONTROL_MASK | GDK_SHIFT_MASK)))
        trigger_click (tree, DONNA_CLICK_SINGLE, priv->last_event);

    return FALSE;
}

static gboolean
donna_tree_view_button_press_event (GtkWidget      *widget,
                                    GdkEventButton *event)
{
    DonnaTreeView *tree = (DonnaTreeView *) widget;
    DonnaTreeViewPrivate *priv = tree->priv;
    gboolean set_up_as_last = FALSE;
    gboolean just_focused;

    /* if app's main window just got focused, we ignore this click */
    g_object_get (priv->app, "just-focused", &just_focused, NULL);
    if (just_focused)
    {
        g_object_set (priv->app, "just-focused", FALSE, NULL);
        return TRUE;
    }

    if (priv->renderer_editable)
    {
        /* we abort the editing -- we just do this, because our signal handlers
         * for remove-widget will take care of removing handlers and whatnot */
        g_object_set (priv->renderer_editable, "editing-canceled", TRUE, NULL);
        gtk_cell_editable_editing_done (priv->renderer_editable);
        gtk_cell_editable_remove_widget (priv->renderer_editable);
        if (priv->focusing_click && event->button == 1
                && !(event->state & (GDK_CONTROL_MASK | GDK_SHIFT_MASK)))
            /* this is a focusing click, don't process it further */
            return TRUE;
    }

    if (event->window != gtk_tree_view_get_bin_window ((GtkTreeView *) widget)
            || event->type != GDK_BUTTON_PRESS)
        return GTK_WIDGET_CLASS (donna_tree_view_parent_class)
            ->button_press_event (widget, event);

#ifdef GTK_IS_JJK
    /* rubber band only happens on left click. We also only "start" it if there
     * was no last_event (unless it expired already) to avoid starting a rubber
     * band during a dbl-click.
     * It might not be wrong in itself, but for some reason we/GTK would get a
     * motion between this start and the corresponding btn_rls, thus resulting
     * in some unwanted rubber band. This seems to be quite common/easy to get
     * (just move the mouse quickly after the dbl-click) and would get the
     * selection be way off (e.g. in a long scrolled list, go back up to the
     * first rows or something). This isn't a fix, but "fixes" the issue. */
    if (!priv->is_tree && event->button == 1
            && (!priv->last_event || priv->last_event_expired))
    {
        gint x, y;

        /* make sure we're over a row, i.e. not on blank space after the last
         * row, because that would cause trouble/is unsupported by GTK */
        gtk_tree_view_convert_bin_window_to_widget_coords ((GtkTreeView *) tree,
                (gint) event->x, (gint) event->y, &x, &y);
        if (gtk_tree_view_get_tooltip_context ((GtkTreeView *) tree, &x, &y, 0,
                    NULL, NULL, NULL)
                /* don't start if this is a focusing click to be skipped. We
                 * know it's LEFT, it might not be SINGLE but pretending should
                 * be fine, since anything else wouldn't be a focusing click */
                && !skip_focusing_click (tree, DONNA_CLICK_SINGLE | DONNA_CLICK_LEFT,
                    event, NULL))
        {
            /* this will only "prepare", the actual operation starts if there's
             * a drag/motion. If/when that happens, signal rubber-banding-active
             * will be emitted. Either way, the click will be processed as
             * usual. */
#ifdef JJK_RUBBER_START
            gtk_tree_view_start_rubber_banding ((GtkTreeView *) tree, event);
#else /* JJK_RUBBER_SIGNAL */
            /* this will have the button-press processing skipped, i.e. no
             * set_cursor or anything such thing */
            gtk_tree_view_skip_next_button_press ((GtkTreeView *) tree);
            /* we still need to chain up though, because this button-press-event
             * needs to chain up to GtkWidget, as that's where the GtkGesture-s
             * will be triggered, and the GtkGestureDrag is what's handling the
             * whole rubber band thing. */
            GTK_WIDGET_CLASS (donna_tree_view_parent_class)
                ->button_press_event (widget, event);
        }
#endif
    }
#endif

    priv->on_release_triggered = FALSE;

    if (!priv->last_event)
        set_up_as_last = TRUE;
    else if (priv->last_event_expired)
    {
        priv->last_event_expired = FALSE;
        /* since it's expired, there is a timeout, and we should remove it no
         * matter if it is a slow-double click or not */
        g_source_remove (priv->last_event_timeout);
        priv->last_event_timeout = 0;

        if (priv->last_event->button == event->button)
        {
            gint distance;

            /* slow-double click? */

            g_object_get (gtk_settings_get_default (),
                    "gtk-double-click-distance",    &distance,
                    NULL);

            if ((ABS (event->x - priv->last_event->x) <= distance)
                    && ABS (event->y - priv->last_event->y) <= distance)
                /* slow-double click it is */
                trigger_click (tree, DONNA_CLICK_SLOW_DOUBLE, event);
            else
                /* just a new click */
                set_up_as_last = TRUE;
        }
        else
            /* new click */
            set_up_as_last = TRUE;

        gdk_event_free ((GdkEvent *) priv->last_event);
        priv->last_event = NULL;
    }
    else
    {
        /* since it's not expired, there is a timeout (for single-click), and we
         * should remove it no matter if it is a double click or not */
        g_source_remove (priv->last_event_timeout);
        priv->last_event_timeout = 0;

        if (priv->last_event->button == event->button)
        {
            gint distance;

            /* double click? */

            g_object_get (gtk_settings_get_default (),
                    "gtk-double-click-distance",    &distance,
                    NULL);

            if ((ABS (event->x - priv->last_event->x) <= distance)
                    && ABS (event->y - priv->last_event->y) <= distance)
                /* trigger event as double click */
                trigger_click (tree, DONNA_CLICK_DOUBLE, event);
            else
            {
                /* trigger last_event as single click */
                trigger_click (tree, DONNA_CLICK_SINGLE, priv->last_event);
                /* and set up a new click */
                set_up_as_last = TRUE;
            }
        }
        else
        {
            /* trigger last_event as single click */
            trigger_click (tree, DONNA_CLICK_SINGLE, priv->last_event);
            /* and set up new click */
            set_up_as_last = TRUE;
        }

        gdk_event_free ((GdkEvent *) priv->last_event);
        priv->last_event = NULL;
    }

    if (set_up_as_last)
    {
        guint delay;

        /* left click are processed right away, unless Ctrl and/or Shift was
         * held. This is because:
         * - the delay could give the impression of things being "slow"(er than
         *   expected)
         * - usual behavior when dbl-clicking an item is to have it selected
         *   (from the click) and then dbl-clicked
         * This way we get that, yet other (middle, right) clicks, as well as
         * when Ctrl and/or Shift is held, can have a dbl-click registered w/out
         * a click before, so e.g. one could Ctrl+dbl-click an item without
         * having the selection being affected.
         * We still set up as last event after we triggered the click, so we can
         * still handle (slow) double clicks */
        if (event->button == 1 && !(event->state & (GDK_CONTROL_MASK | GDK_SHIFT_MASK)))
            if (!trigger_click (tree, DONNA_CLICK_SINGLE, event))
                /* click wasn't processed, i.e. focusing click */
                return TRUE;


        /* first timer. store it, and wait to see what happens next */

        g_object_get (gtk_settings_get_default (),
                "gtk-double-click-time",    &delay,
                NULL);

        priv->last_event = (GdkEventButton *) gdk_event_copy ((GdkEvent *) event);
        /* Here's why we use a special priority, a little lower than default. In
         * case of a double click, if there was e.g. a repaint of the treeview
         * that was to take place after this processing of the click, and before
         * that of the second click, it might take a little while. If it took
         * something around the dbl-click-time delay, the timeout will expire/be
         * triggered, and then we would have two events in the main loop queue:
         * the timeout callback, and the event for the second click.
         * In such a case, because both would have the same priority (GDK events
         * for X stuff are G_PRIORITY_DEFAULT) which one goes in first is
         * random, and when it's the timeout, that means the dbl-click isn't
         * seen as such, leading to either the wrong click being processed, or
         * seeming like the dbl-click was ignored (if e.g. nothing was set for
         * slow_click).
         * Using a lower priority, we ensure the second click is always
         * processed first, which solves the issue.
         * This might seem like a rare case (usually whichever event is first
         * gets processed before the second one is added to the queue) but it
         * might actually happen in case of a left click, since the click was
         * processed right away (before we set up the timeout) and could have
         * resulted in a redraw being queued (e.g. if focus/selection was
         * affected).
         */
        priv->last_event_timeout = g_timeout_add_full (G_PRIORITY_DEFAULT + 10,
                delay,
                (GSourceFunc) single_click_cb, tree, NULL);
        priv->last_event_expired = FALSE;
    }

    /* handled */
    return TRUE;
}

#ifdef JJK_RUBBER_SIGNAL
static gboolean
donna_tree_view_test_rubber_banding (GtkTreeView    *treev,
                                     gint            button,
                                     gint            bin_x,
                                     gint            bin_y)
{
    DonnaTreeView *tree = (DonnaTreeView *) treev;
    DonnaTreeViewPrivate *priv = tree->priv;

    if (!priv->is_tree && (!priv->last_event || priv->last_event_expired))
    {
        gint x, y;

        /* this is all to be safe, since we've already done this before chaining
         * up to "allow" this signal to be emitted */
        gtk_tree_view_convert_bin_window_to_widget_coords (treev,
                bin_x, bin_y, &x, &y);
        if (gtk_tree_view_get_tooltip_context (treev, &x, &y, 0,
                    NULL, NULL, NULL))
            /* allow rubber band to maybe start */
            return FALSE;
    }

    /* no rubber banding allowed */
    return TRUE;
}
#endif

static gboolean
donna_tree_view_button_release_event (GtkWidget      *widget,
                                      GdkEventButton *event)
{
    DonnaTreeViewPrivate *priv = ((DonnaTreeView *) widget)->priv;
    gboolean ret;
    GSList *l;

#ifdef GTK_IS_JJK
    if (gtk_tree_view_is_rubber_banding_pending ((GtkTreeView *) widget, TRUE))
        /* this ensures stopping rubber banding will not move the focus */
        gtk_tree_view_stop_rubber_banding ((GtkTreeView *) widget, FALSE);
#endif

    /* Note: this call will have GTK toggle (expand/collapse) a row when it was
     * double-left-clicked on an expander. It would be a PITA to avoid w/out
     * breaking other things (column resize/drag, rubber band, etc) so we leave
     * it as is.
     * After all, the left click will probably do that already, so no one in
     * their right might would really use expander-dbl-left-click for anything
     * really. (Middle/right dbl-click are fine.) */
    ret = GTK_WIDGET_CLASS (donna_tree_view_parent_class)->button_release_event (
            widget, event);

    /* because after a user resize of a column, GTK might have set the expand
     * property to TRUE which will then cause it to auto-expand on following
     * resize (of other columns or entire window), something we don't want.
     * So, to ensure our columns stay the size they are, and because there's no
     * event "release-post-column-resize" or something, we do the following:
     * After a button release on tree (which handles the column resize) we check
     * all our columns (i.e. skip our non-visible expander or blank space on the
     * right) and, if the property expand is TRUE, we set it back to FALSE. We
     * also set the fixed-width to the current width otherwise the column
     * shrinks unexpectedly. */
    for (l = priv->columns; l; l = l->next)
    {
        struct column *_col = l->data;
        gboolean expand;

        g_object_get (_col->column, "expand", &expand, NULL);
        if (expand)
            g_object_set (_col->column,
                    "expand",       FALSE,
                    "fixed-width",  gtk_tree_view_column_get_width (_col->column),
                    NULL);
    }

    if (priv->on_release_click)
    {
        gint distance;

        g_object_get (gtk_settings_get_default (),
                "gtk-double-click-distance",    &distance,
                NULL);

        /* only validate/trigger the click on release if it's within dbl-click
         * distance of the press event */
        if ((ABS (event->x - priv->on_release_x) <= distance)
                && ABS (event->y - priv->on_release_y) <= distance)
            trigger_click ((DonnaTreeView *) widget, priv->on_release_click, event);

        priv->on_release_click = 0;
    }
    else
        priv->on_release_triggered = TRUE;

    return ret;
}

#ifdef GTK_IS_JJK
static void
donna_tree_view_rubber_banding_active (GtkTreeView *treev)
{
    /* by default GTK will here toggle the row if Ctrl was held, to undo the
     * toggle it does when starting the rubebr band, since it assumes there was
     * one on button-press.
     * Since that assumption isn't valid for us, we simply do nothing (no chain
     * up) to not have this behavior.
     *
     * Of course, if our click w/ Ctrl did do a toggle, then when it gets
     * processes it will undo the toggle of the rubber band, thus creating a
     * "glitch."
     * TODO: The way we'll deal with this is by having a event of ours, where a
     * script could run, check if a toggle is needed and if so do it. That might
     * leave a "visual glitch" since the click is processed after a delay, but
     * it's only a small thing, and at least we'll get expected results.
     */
}
#endif

static inline gchar *
find_key_config (DonnaTreeView *tree, DonnaConfig *config, gchar *key)
{
    gchar *fallback;

    if (donna_config_has_category (config, NULL,
                "key_modes/%s/key_%s",
                tree->priv->key_mode, key))
        return g_strdup_printf ("key_modes/%s/key_%s", tree->priv->key_mode, key);

    if (donna_config_get_string (config, NULL, &fallback,
                "key_modes/%s/fallback", tree->priv->key_mode))
    {
        gchar *s = NULL;
        if (donna_config_has_category (config, NULL,
                    "key_modes/%s/key_%s", fallback, key))
            s = g_strdup_printf ("key_modes/%s/key_%s", fallback, key);
        g_free (fallback);
        return s;
    }

    return NULL;
}

static gint
find_key_from (DonnaTreeView *tree,
               DonnaConfig   *config,
               gchar        **key,
               gchar        **alias,
               gchar        **from)
{
    DonnaTreeViewPrivate *priv = tree->priv;
    gint level = 0;
    gint type;

    *from = find_key_config (tree, config, *key);
    if (!*from)
        return -1;

repeat:
    if (!donna_config_get_int (config, NULL, &type, "%s/type", *from))
        /* default */
        type = KEY_DIRECT;

    if (type == KEY_DISABLED)
        return -1;
    else if (type == KEY_ALIAS)
    {
        if (!donna_config_get_string (config, NULL, alias, "%s/key", *from))
        {
            g_warning ("TreeView '%s': Key '%s' of type ALIAS without alias set",
                    priv->name, *key);
            return -1;
        }
        g_free (*from);
        *from = find_key_config (tree, config, *alias);
        if (!*from)
            return -1;
        *key = *alias;
        if (++level > 10)
        {
            g_warning ("TreeView '%s': There might be an infinite loop in key aliasing, "
                    "bailing out on key '%s' reaching level %d",
                    priv->name, *key, level);
            return -1;
        }
        goto repeat;
    }
    return type;
}

#define wrong_key(beep) do {            \
    if (beep)                           \
        gtk_widget_error_bell (         \
                (GtkWidget *) tree);    \
    g_free (from);                      \
    g_free (alias);                     \
    g_free (priv->key_combine_name);    \
    priv->key_combine_name = NULL;      \
    priv->key_combine_val = 0;          \
    priv->key_combine_spec = 0;         \
    priv->key_spec_type = SPEC_NONE;    \
    priv->key_m = 0;                    \
    priv->key_val = 0;                  \
    priv->key_motion_m = 0;             \
    priv->key_motion = 0;               \
    check_statuses (tree,               \
            STATUS_CHANGED_ON_KEYS);    \
    return TRUE;                        \
} while (0)

static gboolean
trigger_key (DonnaTreeView *tree, gchar spec)
{
    DonnaTreeViewPrivate *priv = tree->priv;
    DonnaConfig *config;
    GtkTreePath *path;
    GtkTreeIter iter;
    gchar *key;
    gchar *alias = NULL;
    gchar *from  = NULL;
    gchar *fl;
    struct conv conv = { NULL, };
    DonnaContext context = { CONTEXT_FLAGS CONTEXT_KEYS_FLAGS, FALSE,
        (conv_flag_fn) tree_conv_flag, &conv };
    GPtrArray *intrefs = NULL;

    config = donna_app_peek_config (priv->app);
    conv.tree = tree;
    conv.key_spec = spec;

    /* is there a motion? */
    if (priv->key_motion)
    {
        gchar combine_spec;

        gtk_tree_view_get_cursor ((GtkTreeView *) tree, &path, NULL);
        if (!path)
            wrong_key (TRUE);
        gtk_tree_model_get_iter ((GtkTreeModel *) priv->store, &iter, path);
        gtk_tree_path_free (path);

        key = gdk_keyval_name (priv->key_motion);
        if (G_UNLIKELY (find_key_from (tree, config, &key, &alias, &from) == -1))
            wrong_key (TRUE);
        if (!donna_config_get_string (config, NULL, &fl, "%s/trigger", from))
            wrong_key (TRUE);

        conv.key_m = priv->key_motion_m;
        conv.row = get_row_for_iter (tree, &iter);

        /* "disable" combine_spec for now */
        combine_spec = priv->key_combine_spec;
        priv->key_combine_spec = 0;
        fl = donna_app_parse_fl (priv->app, fl, TRUE, &context, &intrefs);
        priv->key_combine_spec = combine_spec;
        if (!donna_app_trigger_fl (priv->app, fl, intrefs, TRUE, NULL))
        {
            g_free (fl);
            wrong_key (TRUE);
        }
        g_free (fl);
        intrefs = NULL;
        g_free (from);
        g_free (alias);
        from = alias = NULL;
    }

    key = gdk_keyval_name (priv->key_val);
    if (G_UNLIKELY (find_key_from (tree, config, &key, &alias, &from) == -1))
        wrong_key (TRUE);
    if (!donna_config_get_string (config, NULL, &fl, "%s/trigger", from))
        wrong_key (TRUE);

    if (!conv.row)
    {
        gtk_tree_view_get_cursor ((GtkTreeView *) tree, &path, NULL);
        if (path)
        {
            gtk_tree_model_get_iter ((GtkTreeModel *) priv->store, &iter, path);
            gtk_tree_path_free (path);
            conv.row = get_row_for_iter (tree, &iter);
        }
    }

    conv.key_m = priv->key_m;
    fl = donna_app_parse_fl (priv->app, fl, TRUE, &context, &intrefs);
    g_free (conv.row);

    g_free (from);
    g_free (alias);
    g_free (priv->key_combine_name);
    priv->key_combine_name = NULL;
    priv->key_combine_val = 0;
    priv->key_combine_spec = 0;
    priv->key_spec_type = SPEC_NONE;
    priv->key_m = 0;
    priv->key_val = 0;
    priv->key_motion_m = 0;
    priv->key_motion = 0;
    check_statuses (tree, STATUS_CHANGED_ON_KEYS);

    /* we need to trigger *after* we reset the keys, because trigger_fl() could
     * start a new main loop (for its get_node()) or even have e.g. the command
     * processed right away (e.g. if INTERNAL_GUI) and that could process
     * events, e.g. if using set_floating_window() as can be the case in
     * column_edit() */
    donna_app_trigger_fl (priv->app, fl, intrefs, FALSE, NULL);
    g_free (fl);
    return FALSE;
}

static gboolean
donna_tree_view_key_press_event (GtkWidget *widget, GdkEventKey *event)
{
    DonnaTreeView *tree = (DonnaTreeView *) widget;
    DonnaTreeViewPrivate *priv = tree->priv;
    DonnaConfig *config;
    gchar *key;
    gchar *alias = NULL;
    gchar *from  = NULL;
    enum key_type type = -1;
    gint i;

    /* ignore modifier or AltGr */
    if (event->is_modifier || event->keyval == GDK_KEY_ISO_Level3_Shift)
        return FALSE;

    config = donna_app_peek_config (priv->app);
    key = gdk_keyval_name (event->keyval);
    if (!key)
        return FALSE;

    g_debug("key=%s",key);

    if (priv->key_spec_type != SPEC_NONE)
    {
        if (priv->key_spec_type & SPEC_LOWER)
            if (event->keyval >= GDK_KEY_a && event->keyval <= GDK_KEY_z)
                goto next;
        if (priv->key_spec_type & SPEC_UPPER)
            if (event->keyval >= GDK_KEY_A && event->keyval <= GDK_KEY_Z)
                goto next;
        if (priv->key_spec_type & SPEC_DIGITS)
            if ((event->keyval >= GDK_KEY_0 && event->keyval <= GDK_KEY_9)
                    || (event->keyval >= GDK_KEY_KP_0 && event->keyval <= GDK_KEY_KP_9))
                goto next;
        if ((priv->key_spec_type & SPEC_EXTRA)
                && strchr (SPEC_EXTRA_CHARS, (gint) gdk_keyval_to_unicode (event->keyval)))
            goto next;
        if (priv->key_spec_type & SPEC_CUSTOM)
        {
            gchar *key_spec_owner;
            gchar *chars;
            gchar *found = NULL;

            if (priv->key_motion)
                key_spec_owner = gdk_keyval_name (priv->key_motion);
            else if (priv->key_combine_val && priv->key_combine_spec == 0)
                key_spec_owner = gdk_keyval_name (priv->key_combine_val);
            else
                key_spec_owner = gdk_keyval_name (priv->key_val);

            if (find_key_from (tree, config, &key_spec_owner, &alias, &from) == -1)
                wrong_key (TRUE);
            if (!donna_config_get_string (config, NULL, &chars,
                        "%s/custom_chars", from))
                wrong_key (TRUE);
            found = strchr (chars, (gint) gdk_keyval_to_unicode (event->keyval));
            g_free (chars);
            g_free (from);
            g_free (alias);
            from = alias = NULL;
            if (found)
                goto next;
        }
        if (priv->key_spec_type & SPEC_MOTION)
        {
            gboolean is_motion = FALSE;

            if (priv->key_motion_m == 0 && event->keyval == priv->key_val)
            {
                priv->key_spec_type = SPEC_NONE;
                goto next;
            }

            if (event->keyval >= GDK_KEY_0 && event->keyval <= GDK_KEY_9)
            {
                /* modifier */
                priv->key_motion_m *= 10;
                priv->key_motion_m += event->keyval - GDK_KEY_0;
                check_statuses (tree, STATUS_CHANGED_ON_KEYS);
                return TRUE;
            }
            else if (event->keyval >= GDK_KEY_KP_0 && event->keyval <= GDK_KEY_KP_9)
            {
                /* modifier */
                priv->key_motion_m *= 10;
                priv->key_motion_m += event->keyval - GDK_KEY_KP_0;
                check_statuses (tree, STATUS_CHANGED_ON_KEYS);
                return TRUE;
            }

            type = find_key_from (tree, config, &key, &alias, &from);
            if ((gint) type == -1)
                wrong_key (TRUE);

            donna_config_get_boolean (config, NULL, &is_motion,
                    "%s/is_motion", from);
            if (is_motion)
                goto next;
        }
        wrong_key (TRUE);
        /* not reached */
next:
        if (priv->key_combine_name && priv->key_combine_spec == 0)
        {
            priv->key_combine_spec = (gchar) gdk_keyval_to_unicode (event->keyval);
            priv->key_spec_type = SPEC_NONE;
            g_free (from);
            g_free (alias);
            check_statuses (tree, STATUS_CHANGED_ON_KEYS);
            return TRUE;
        }
    }

    if (priv->key_val)
    {
        /* means the spec was just specified */

        if (priv->key_spec_type & SPEC_MOTION)
        {
            priv->key_spec_type = SPEC_NONE;
            priv->key_motion = event->keyval;

            if (type == KEY_DIRECT)
                trigger_key (tree, 0);
            else if (type == KEY_SPEC)
            {
                if (!donna_config_get_int (config, NULL, &i, "%s/spec", from))
                    /* defaults */
                    i = SPEC_LOWER | SPEC_UPPER;
                if (i & SPEC_MOTION)
                    /* a motion can't ask for a motion */
                    wrong_key (TRUE);
                priv->key_spec_type = CLAMP (i, 1, 512);
            }
            else
                wrong_key (TRUE);
        }
        else
            trigger_key (tree, (gchar) gdk_keyval_to_unicode (event->keyval));
    }
    else if (event->keyval >= GDK_KEY_0 && event->keyval <= GDK_KEY_9)
    {
        /* modifier */
        priv->key_m *= 10;
        priv->key_m += event->keyval - GDK_KEY_0;
    }
    else if (event->keyval >= GDK_KEY_KP_0 && event->keyval <= GDK_KEY_KP_9)
    {
        /* modifier */
        priv->key_m *= 10;
        priv->key_m += event->keyval - GDK_KEY_KP_0;
    }
    else
    {
        type = find_key_from (tree, config, &key, &alias, &from);
        if (G_UNLIKELY ((gint) type == -1 && event->keyval == GDK_KEY_Escape))
        {
            /* special case: GDK_KEY_Escape will always default to
             * tree_reset_keys if not defined. This is to ensure that if you set
             * a key mode, you can always get back to normal mode (even if you
             * forgot to define the key Escape to do so. Of course if you define
             * it to nothing/something else, it's on you. */
            donna_tree_view_reset_keys (tree);
            return TRUE;
        }

        switch (type)
        {
            case KEY_COMBINE:
                if (priv->key_m > 0 || priv->key_combine_name)
                    /* no COMBINE with a modifier; only one at a time */
                    wrong_key (TRUE);
                if (!donna_config_get_string (config, NULL, &priv->key_combine_name,
                            "%s/combine", from))
                {
                    g_warning ("TreeView '%s': Key '%s' missing its name as COMBINE",
                            priv->name, key);
                    wrong_key (TRUE);
                }
                if (!donna_config_get_int (config, NULL, &i, "%s/spec", from))
                    /* defaults */
                    i = SPEC_LOWER | SPEC_UPPER;
                if (i & SPEC_MOTION)
                {
                    g_warning ("TreeView '%s': Key '%s' cannot be COMBINE with spec MOTION",
                            priv->name, key);
                    wrong_key (TRUE);
                }
                priv->key_combine_val = event->keyval;
                priv->key_spec_type = CLAMP (i, 1, 512);
                break;

            case KEY_DIRECT:
                priv->key_val = event->keyval;
                /* don't trigger now, to check combine */
                break;

            case KEY_SPEC:
                priv->key_val = event->keyval;
                if (!donna_config_get_int (config, NULL, &i, "%s/spec", from))
                    /* defaults */
                    i = SPEC_LOWER | SPEC_UPPER;
                if (i & SPEC_MOTION)
                    /* make sure there's no BS like SPEC_LOWER | SPEC_MOTION */
                    i = SPEC_MOTION;
                priv->key_spec_type = CLAMP (i, 1, 512);
                break;

            case KEY_ALIAS:
                /* to silence warning, but it can't happen since find_key_from()
                 * will "resolve" aliases */
                /* fall through */
            case KEY_DISABLED:
                /* to silence warning, but it can't happen since find_key_from()
                 * will return -1 (error) in such case */
                /* fall through */
            default:
                /* i.e. key isn't defined or disabled */
                if (priv->key_m || priv->key_combine_name)
                    wrong_key (TRUE);
                else
                {
                    /* didn't handle this. This will allow GTK to process it,
                     * e.g. for key bindgins such as [Ctrl/Shift]Tab to move
                     * focus around the widgets in main window */
                    g_free (from);
                    g_free (alias);
                    return FALSE;
                }
        }
        if (type != KEY_COMBINE && priv->key_combine_name)
        {
            gchar *s;

            if (!donna_config_get_string (config, NULL, &s, "%s/combine", from))
                wrong_key (TRUE);
            if (!streq (s, priv->key_combine_name))
            {
                g_free (s);
                wrong_key (TRUE);
            }
            g_free (s);
        }
        if (type == KEY_DIRECT)
            trigger_key (tree, 0);
    }

    g_free (from);
    g_free (alias);
    check_statuses (tree, STATUS_CHANGED_ON_KEYS);
    return TRUE;
}

#undef wrong_key

static gboolean
set_selection_browse (GtkTreeSelection *selection)
{
    gtk_tree_selection_set_mode (selection, GTK_SELECTION_BROWSE);
    return FALSE;
}

static gboolean
check_focus_widget (DonnaTreeView *tree)
{
    if (gtk_widget_is_focus ((GtkWidget *) tree))
        gtk_widget_grab_focus ((GtkWidget *) tree->priv->sync_with);
    return FALSE;
}

static void
selection_changed_cb (GtkTreeSelection *selection, DonnaTreeView *tree)
{
    DonnaTreeViewPrivate *priv = tree->priv;
    GtkTreeIter iter;

    /* filling_list is also set when clearing the store, because that has GTK
     * trigger *a lot* of selection-changed (even when there's no selection)
     * which in turn would trigger lots of status refresh, which would be a
     * little slow (when there was lots of items).
     * It is also set from selection_nodes() since on each (un)select_iter()
     * call there's a signal emitted, which slows things down a bit. */
    if (!priv->filling_list)
        check_statuses (tree, STATUS_CHANGED_ON_CONTENT);
    if (!priv->is_tree)
        return;

    if (gtk_tree_selection_get_selected (selection, NULL, &iter))
    {
        GtkTreeIter location_iter;
        DonnaNode *node;

        /* might have been to SELECTION_SINGLE if there was no selection, due to
         * unsync with the list (or minitree mode) */
        if (priv->sync_mode != TREE_SYNC_NONE
                && gtk_tree_selection_get_mode (selection) != GTK_SELECTION_BROWSE)
            /* trying to change it now causes a segfault in GTK */
            g_idle_add ((GSourceFunc) set_selection_browse, selection);

        location_iter = priv->location_iter;
        priv->location_iter = iter;

        gtk_tree_model_get (GTK_TREE_MODEL (priv->store), &iter,
                TREE_COL_NODE,  &node,
                -1);
        if (priv->location != node)
        {
            GError *err = NULL;
            gboolean triggered;

            if (priv->location)
                g_object_unref (priv->location);
            priv->location = node;

            triggered = (donna_node_get_node_type (node) == DONNA_NODE_ITEM
                    && (donna_provider_get_flags (donna_node_peek_provider (node))
                        & DONNA_PROVIDER_FLAG_FLAT));
            if (triggered)
            {
                DonnaTask *task;

                task = donna_node_trigger_task (node, &err);
                if (!task)
                {
                    gchar *fl = donna_node_get_full_location (node);
                    donna_app_show_error (priv->app, err,
                            "TreeView '%s': Failed to trigger '%s'",
                            priv->name, fl);
                    g_free (fl);
                    g_clear_error (&err);
                }
                else
                    donna_app_run_task (priv->app, task);

                /* restore selection to previous row */
                if (location_iter.stamp != 0)
                    gtk_tree_selection_select_iter (selection, &location_iter);
            }

            if (priv->sync_with)
            {
                DonnaNode *n;

                if (!triggered)
                {
                    /* should we ask the list to change its location? */
                    n = donna_tree_view_get_location (priv->sync_with);
                    if (n)
                        g_object_unref (n);
                    if (n == node)
                        return;

                    donna_tree_view_set_location (priv->sync_with, node, &err);
                    if (err)
                    {
                        donna_app_show_error (priv->app, err,
                                "TreeView '%s': Failed to set location on '%s'",
                                priv->name, priv->sync_with->priv->name);
                        g_clear_error (&err);
                    }
                }

                if (priv->auto_focus_sync)
                    /* auto_focus_sync means if we have the focus, we send it to
                     * sync_with. We need to do this in a new idle source
                     * because we might be getting the focus with the selection
                     * change (i.e.  user clicked on tree while focus was
                     * elsewhere) and is_focus() is only gonna take this into
                     * account *after* this signal is processed. */
                    g_idle_add ((GSourceFunc) check_focus_widget, tree);
            }
        }
        else if (node)
            g_object_unref (node);
    }
    else if (gtk_tree_selection_get_mode (selection) != GTK_SELECTION_BROWSE)
    {
        /* if we're not in BROWSE mode anymore, it means this is the result of
         * being out of sync with our list, resulting in a temporary switch to
         * SINGLE. So, we just don't have a current location for the moment */
        if (priv->location)
        {
            g_object_unref (priv->location);
            priv->location = NULL;
            priv->location_iter.stamp = 0;
        }
    }
    else
    {
        GtkTreePath *path;

        /* ideally this wouldn't happen. There are ways, though, for this to
         * occur. Known ways are:
         *
         * - Moving the focus up/outside the branch, then collapsing the parent
         *   of the selected node. No more selection!
         *   This is handled in donna_tree_view_test_collapse_row()
         *
         * - In minitree, removing the row of current location.
         *   This is handled in remove_row_from_tree()/handle_removing_row()
         *
         * - Then there's the case of the tree going out of sync. When no node
         *   was found, we switch to SINGLE and then unselect. However, the
         *   switch to SIGNLE will apparently emit selection-changed 3 times,
         *   the first one with nothing selected at all, but the mode is still
         *   BROWSE, thus leading here.
         *   This is handled via setting priv->changed_location prior, and
         *   ignoring below when it's set, ignoring the first/problematic
         *   signal.
         *
         *   It should be noted that because multiple signals can still emitted
         *   in other circustances, and a tree can end up doing multiple
         *   set_location() to its sync_with (if the change hasn't completed on
         *   the second signal). Since we can't avoid that, changed_location()
         *   has a special handling for that (ignoring request to change for the
         *   same future_location), see changing_sel_mode() for more.
         *
         * - There might be other ways GTK allows to get the selection removed
         *   in BROWSE, which ideally we should then learn and handle as well;
         *   Meanwhile, let's select the focused row. */

        if (priv->changing_sel_mode)
            return;

        g_warning ("TreeView '%s': the selection was lost in BROWSE mode",
                priv->name);

        gtk_tree_view_get_cursor ((GtkTreeView *) tree, &path, NULL);
        if (!path)
        {
            if (!has_model_at_least_n_rows ((GtkTreeModel *) priv->store, 1))
            {
                /* if there's no more rows on tree, let's make sure we don't
                 * have an old (invalid) current location */
                if (priv->location)
                {
                    g_object_unref (priv->location);
                    priv->location = NULL;
                    priv->location_iter.stamp = 0;
                }
                return;
            }
            path = gtk_tree_path_new_from_string ("0");
        }
        gtk_tree_selection_select_path (selection, path);
        gtk_tree_path_free (path);
    }
}

/* mode list only */
static inline void
set_draw_state (DonnaTreeView *tree, enum draw draw)
{
    DonnaTreeViewPrivate *priv = tree->priv;

    if (priv->draw_state == draw)
        return;

    priv->draw_state = draw;
    if (draw == DRAW_NOTHING)
    {
        GtkWidget *w;

        /* we give the tree view the focus, to ensure the focused row is set,
         * hence the class focused-row applied */
        w = gtk_widget_get_toplevel ((GtkWidget *) tree);
        w = gtk_window_get_focus ((GtkWindow *) w);
        gtk_widget_grab_focus ((GtkWidget *) tree);
        gtk_widget_grab_focus ((w) ? w : (GtkWidget *) tree);
    }
    gtk_widget_queue_draw ((GtkWidget *) tree);
}

/* mode list only */
static void
refresh_draw_state (DonnaTreeView *tree)
{
    DonnaTreeViewPrivate *priv = tree->priv;
    enum draw draw;
    GtkTreeIter it;

    if (!gtk_tree_model_iter_children ((GtkTreeModel *) priv->store, &it, NULL))
    {
        if (g_hash_table_size (priv->hashtable) == 0)
            draw = DRAW_EMPTY;
        else
            draw = DRAW_NO_VISIBLE;
    }
    else
        draw = DRAW_NOTHING;

    set_draw_state (tree, draw);
}

static gboolean
donna_tree_view_draw (GtkWidget *w, cairo_t *cr)
{
    DonnaTreeView *tree = DONNA_TREE_VIEW (w);
    DonnaTreeViewPrivate *priv = tree->priv;
    GtkTreeView *treev = GTK_TREE_VIEW (w);
    gint x, y, width;
    GtkStyleContext *context;
    PangoLayout *layout;

    /* chain up, so the drawing actually gets done */
    GTK_WIDGET_CLASS (donna_tree_view_parent_class)->draw (w, cr);

    if (priv->is_tree || priv->draw_state == DRAW_NOTHING)
        return FALSE;

    gtk_tree_view_convert_tree_to_widget_coords (treev, 0, 0, &x, &y);
    width = gtk_widget_get_allocated_width (w);
    context = gtk_widget_get_style_context (w);

    if (priv->draw_state == DRAW_EMPTY)
    {
        gtk_style_context_save (context);
        gtk_style_context_set_state (context, GTK_STATE_FLAG_INSENSITIVE);
    }

    layout = gtk_widget_create_pango_layout (w,
            (priv->draw_state == DRAW_WAIT) ? "Please wait..." :
            (priv->draw_state == DRAW_EMPTY) ? "(Location is empty)"
            : "(Nothing to show; There are hidden/filtered nodes)");
    pango_layout_set_width (layout, width * PANGO_SCALE);
    pango_layout_set_alignment (layout, PANGO_ALIGN_CENTER);
    gtk_render_layout (context, cr, x, y, layout);

    if (priv->draw_state == DRAW_EMPTY)
        gtk_style_context_restore (context);
    g_object_unref (layout);
    return FALSE;
}

/* this is *very* similar to that of GtkTreeView, only with minor changes.
 * Mainly, we don't want GTK_DIR_TAB_BACKWARD to put the focus on the colum
 * headers, instead we jump to another widget.
 * This is because I like it better that way, also goes along the fact that upon
 * clicking a column header (e.g. set sort order) we send the focus back to the
 * treeview.
 */
static gboolean
donna_tree_view_focus (GtkWidget        *widget,
                       GtkDirectionType  direction)
{
    DonnaTreeViewPrivate *priv = ((DonnaTreeView *) widget)->priv;

    if (!gtk_widget_is_sensitive (widget) || !gtk_widget_get_can_focus (widget))
        return FALSE;

    /* we need to stop editing if there was any. Luckily, we can do this. We
     * also return FALSE then so the focus moves to another widget */
    if (priv->renderer_editable)
    {
        /* we abort the editing -- we just do this, because our signal handlers
         * for remove-widget will take care of removing handlers and whatnot */
        g_object_set (priv->renderer_editable, "editing-canceled", TRUE, NULL);
        gtk_cell_editable_editing_done (priv->renderer_editable);
        gtk_cell_editable_remove_widget (priv->renderer_editable);
        return FALSE;
    }

    /* Case 1.  Headers currently have focus. */
    if (gtk_container_get_focus_child ((GtkContainer *) widget))
        /* we let GTK handle this, so the LEFT/RIGHT can work. It'll also handle
         * BACKWARD/FORWARD ofc */
        return GTK_WIDGET_CLASS (donna_tree_view_parent_class)->focus (widget,
                direction);

    /* Case 2. We don't have focus at all. */
    if (!gtk_widget_has_focus (widget))
    {
        /* same as GTK */
        gtk_widget_grab_focus (widget);
        return TRUE;
    }

    /* Case 3. We have focus already. */
    if (direction == GTK_DIR_TAB_BACKWARD || direction == GTK_DIR_TAB_FORWARD)
        /* both case we want to jump to another widget */
        return FALSE;

    /* Other directions caught by the keybindings (same as GTK) */
    gtk_widget_grab_focus (widget);
    return TRUE;
}

const gchar *
donna_tree_view_get_name (DonnaTreeView *tree)
{
    g_return_val_if_fail (DONNA_IS_TREE_VIEW (tree), NULL);
    return tree->priv->name;
}

gboolean
donna_tree_view_is_tree (DonnaTreeView      *tree)
{
    g_return_val_if_fail (DONNA_IS_TREE_VIEW (tree), FALSE);
    return tree->priv->is_tree;
}

static void
check_statuses (DonnaTreeView *tree, enum changed_on changed)
{
    DonnaTreeViewPrivate *priv = tree->priv;
    guint i;

    for (i = 0; i < priv->statuses->len; ++i)
    {
        struct status *status = &g_array_index (priv->statuses, struct status, i);
        if (status->changed_on & changed)
            donna_status_provider_status_changed ((DonnaStatusProvider *) tree,
                    status->id);
    }
}

/* DonnaStatusProvider */

static guint
status_provider_create_status (DonnaStatusProvider    *sp,
                               gpointer                _name,
                               GError                **error)
{
    DonnaTreeViewPrivate *priv = ((DonnaTreeView *) sp)->priv;
    DonnaConfig *config;
    struct status status;
    const gchar *name = _name;
    gchar *s;

    config = donna_app_peek_config (priv->app);
    if (!donna_config_get_string (config, error, &s, "statusbar/%s/format", name))
    {
        g_prefix_error (error, "TreeView '%s': Status '%s': No format: ",
                priv->name, name);
        return 0;
    }

    status.id   = ++priv->last_status_id;
    status.name = g_strdup (name);
    status.fmt  = s;
    status.changed_on = 0;

    if (!donna_config_get_int (config, NULL, &status.digits,
                "statusbar/%s/digits", name))
        if (!donna_config_get_int (config, NULL, &status.digits,
                    "defaults/size/digits"))
            status.digits = 1;
    if (!donna_config_get_boolean (config, NULL, &status.long_unit,
                "statusbar/%s/long_unit", name))
        if (!donna_config_get_boolean (config, NULL, &status.long_unit,
                    "defaults/size/long_unit"))
            status.long_unit = FALSE;

    if (!donna_config_get_int (config, NULL, (gint *) &status.colors,
                "statusbar/%s/colors", name)
            || (status.colors != ST_COLORS_OFF && status.colors != ST_COLORS_KEYS
                && status.colors != ST_COLORS_VF))
        status.colors = ST_COLORS_OFF;
    if (status.colors == ST_COLORS_KEYS)
        status.changed_on |= STATUS_CHANGED_ON_KEY_MODE;
    else if (status.colors == ST_COLORS_VF)
        status.changed_on |= STATUS_CHANGED_ON_VF;

    while ((s = strchr (s, '%')))
    {
        switch (s[1])
        {
            case 'K':
                status.changed_on |= STATUS_CHANGED_ON_KEY_MODE;
                break;

            case 'k':
                status.changed_on |= STATUS_CHANGED_ON_KEYS;
                break;

            case 'F':
                status.changed_on |= STATUS_CHANGED_ON_VF;
                break;

            case 'l':
            case 'L':
            case 'f':
            case 's':
            case 'S':
            case 'h':
            case 'H':
            case 'v':
            case 'V':
            case 'a':
            case 'A':
            case 'n':
            case 'N':
                status.changed_on |= STATUS_CHANGED_ON_CONTENT;
                break;
        }
        s += 2;
    }

    g_array_append_val (priv->statuses, status);
    return status.id;
}

static void
status_provider_free_status (DonnaStatusProvider    *sp,
                             guint                   id)
{
    DonnaTreeViewPrivate *priv = ((DonnaTreeView *) sp)->priv;
    guint i;

    for (i = 0; i < priv->statuses->len; ++i)
    {
        struct status *status = &g_array_index (priv->statuses, struct status, i);

        if (status->id == id)
        {
            g_array_remove_index_fast (priv->statuses, i);
            break;
        }
    }
}

static const gchar *
status_provider_get_renderers (DonnaStatusProvider    *sp,
                               guint                   id)
{
    DonnaTreeViewPrivate *priv = ((DonnaTreeView *) sp)->priv;
    guint i;

    for (i = 0; i < priv->statuses->len; ++i)
    {
        struct status *status = &g_array_index (priv->statuses, struct status, i);

        if (status->id == id)
            return "t";
    }
    return NULL;
}

enum cs
{
    CS_VISIBLE  = (1 << 0),
    CS_HIDDEN   = (1 << 1),
    CS_ALL      = CS_VISIBLE | CS_HIDDEN
};

struct calc_size
{
    enum cs cs;
    guint64 size;
};

static void
calculate_size (DonnaNode *node, gpointer value, struct calc_size *cs)
{
    guint64 size;

    if (value)
    {
        if (!(cs->cs & CS_VISIBLE))
            return;
    }
    else if (!(cs->cs & CS_HIDDEN))
        return;

    if (donna_node_get_node_type (node) == DONNA_NODE_ITEM
            && donna_node_get_size (node, TRUE, &size) == DONNA_NODE_VALUE_SET)
        cs->size += size;
}

static gboolean
calculate_size_selected (GtkTreeModel    *model,
                         GtkTreePath     *path,
                         GtkTreeIter     *iter,
                         guint64         *total)
{
    DonnaNode *node;
    guint64 size;

    gtk_tree_model_get (model, iter, TREE_VIEW_COL_NODE, &node, -1);
    if (!node)
        return FALSE;
    if (donna_node_get_node_type (node) == DONNA_NODE_ITEM
            && donna_node_get_size (node, TRUE, &size) == DONNA_NODE_VALUE_SET)
        *total += size;
    g_object_unref (node);
    return FALSE; /* keep iterating */
}

struct sp_conv
{
    DonnaTreeView *tree;
    struct status *status;
    GtkTreeSelection *sel;
    gint nb_a;
    gint nb_v;
    gint nb_h;
    gint nb_s;
};

static gboolean status_provider_conv (const gchar     c,
                                      gchar          *extra,
                                      DonnaArgType   *type,
                                      gpointer       *ptr,
                                      GDestroyNotify *destroy,
                                      struct sp_conv *sp_conv);

static void
sp_custom_conv (const gchar          c,
                gchar               *extra,
                DonnaContextOptions options,
                GString            *str,
                struct sp_conv     *sp_conv)
{
    DonnaTreeViewPrivate *priv = sp_conv->tree->priv;
    gchar *fmt = extra;
    struct calc_size cs = { CS_ALL, 0 };
    gchar buf[20], *b = buf;
    gsize len;

    /* we know that options == DONNA_CONTEXT_NO_QUOTES */

    if (c == 'k')
    {
        if (priv->key_combine_val)
            g_string_append_c (str,
                    (gchar) gdk_keyval_to_unicode (priv->key_combine_val));
        if (priv->key_combine_spec)
            g_string_append_c (str, priv->key_combine_spec);
        if (priv->key_m)
            g_string_append_printf (str, "%d", priv->key_m);
        if (priv->key_val)
            g_string_append_c (str,
                    (gchar) gdk_keyval_to_unicode (priv->key_val));
        if (priv->key_motion_m)
            g_string_append_printf (str, "%d", priv->key_motion_m);

        return;
    }

    /* only gets here if there was an extra */
    if (c == 'a' || c == 'v' || c == 'h' || c == 's' || c == 'F')
    {
        DonnaContext context = { ST_CONTEXT_FLAGS ",", TRUE,
            (conv_flag_fn) status_provider_conv, sp_conv };
        gint nb;
        gint ref = 0;
        gchar *_fmt;
        gchar *sep[2];
        gchar *s;
        gint n = 0;

        if (c == 'a')
        {
            nb = sp_conv->nb_a;
            /* 'a' is a special case: where all others use 0 as reference, i.e.
             * we don't show anything (or first one when 2 seps/3 strings are
             * specified), with 'a' it is when everything is visible that we
             * don't. Allows to do "2 items" and "2/3 items" easily */
            if (sp_conv->nb_v == -1)
                sp_conv->nb_v = _gtk_tree_model_get_count ((GtkTreeModel *) priv->store);
            ref = sp_conv->nb_v;
        }
        else if (c == 'v')
            nb = sp_conv->nb_v;
        else if (c == 'h' || c == 'F')
            nb = sp_conv->nb_h;
        else /* c == 's' */
            nb = sp_conv->nb_s;

        if (c != 'F')
            for (s = fmt; *s; ++s)
            {
                /* skip variables, specifically allows to skip "%," */
                if (*s == '%')
                {
                    ++s;
                    continue;
                }
                else if (n < 2 && *s == ',')
                    sep[n++] = s;
            }

        /* VF-based, so always show, there's no separator or nothing */
        if (c == 'F')
        {
            context.flags = ST_CONTEXT_FLAGS; /* no added ',' */
            _fmt = fmt;
        }
        /* 0 sep: show extra unless nb == ref */
        else if (n == 0)
        {
            if (nb == ref)
                return;
            _fmt = fmt;
        }
        /* 1 sep: show nothing if nb == ref; first if nb == 1; second if nb > 1 */
        else if (n == 1)
        {

            if (nb == ref)
                return;
            else if (nb == 1)
            {
                _fmt = fmt;
                *sep[0] = '\0';
            }
            else /* nb > 1 */
                _fmt = sep[0] + 1;
        }
        /* 2 sep: show first if nb == ref; second if nb == 1; third if nb > 1 */
        else /* n == 2 */
        {
            if (nb == ref)
            {
                _fmt = fmt;
                *sep[0] = '\0';
            }
            else if (nb == 1)
            {
                _fmt = sep[0] + 1;
                *sep[1] = '\0';
            }
            else /* nb > 1 */
                _fmt = sep[1] + 1;
        }

        /* direct "recursive" parsing, with an added ',' so one can use commas
         * by using "%," */
        donna_context_parse (&context, DONNA_CONTEXT_NO_QUOTES, priv->app,
                _fmt, &str, NULL);
        return;
    }
    else if (c == ',')
    {
        g_string_append_c (str, ',');
        return;
    }

    if (!fmt && !donna_config_get_string (donna_app_peek_config (priv->app),
                NULL, &fmt, "statusbar/%s/size_format", sp_conv->status->name))
        donna_config_get_string (donna_app_peek_config (priv->app),
                NULL, &fmt, "defaults/size/format");

    switch (c)
    {
        case 'A':
            g_hash_table_foreach (priv->hashtable, (GHFunc) calculate_size, &cs);
            break;

        case 'V':
            cs.cs = CS_VISIBLE;
            g_hash_table_foreach (priv->hashtable, (GHFunc) calculate_size, &cs);
            break;

        case 'H':
            cs.cs = CS_HIDDEN;
            g_hash_table_foreach (priv->hashtable, (GHFunc) calculate_size, &cs);
            break;

        case 'S':
            if (!sp_conv->sel)
                sp_conv->sel = gtk_tree_view_get_selection ((GtkTreeView *) sp_conv->tree);
            gtk_tree_selection_selected_foreach (sp_conv->sel,
                    (GtkTreeSelectionForeachFunc) calculate_size_selected, &cs.size);
            break;
    }

    b = buf;
    len = donna_print_size (b, 20, (fmt) ? fmt : "%R", cs.size,
            sp_conv->status->digits, sp_conv->status->long_unit);
    if (len >= 20)
    {
        b = g_new (gchar, ++len);
        donna_print_size (b, len, (fmt) ? fmt : "%R", cs.size,
                sp_conv->status->digits, sp_conv->status->long_unit);
    }

    g_string_append (str, b);
    if (b != buf)
        g_free (b);
    if (fmt && fmt != extra)
        g_free (fmt);
}

static gboolean
status_provider_conv (const gchar     c,
                      gchar          *extra,
                      DonnaArgType   *type,
                      gpointer       *ptr,
                      GDestroyNotify *destroy,
                      struct sp_conv *sp_conv)
{
    DonnaTreeViewPrivate *priv = sp_conv->tree->priv;

    switch (c)
    {
        case 'o':
            *type = DONNA_ARG_TYPE_STRING;
            *ptr = priv->name;
            return TRUE;

        case 'l':
        case 'L':
            *type = DONNA_ARG_TYPE_STRING;
            if (G_LIKELY (priv->location))
            {
                if (c == 'L'
                        && streq ("fs", donna_node_get_domain (priv->location)))
                    *ptr = donna_node_get_location (priv->location);
                else
                    *ptr = donna_node_get_full_location (priv->location);
                *destroy = g_free;
            }
            else
                *ptr = (gpointer) "-";
            return TRUE;

        case 'f':
            {
                GtkTreePath *path;
                GtkTreeIter iter;
                DonnaNode *node;

                gtk_tree_view_get_cursor ((GtkTreeView *) sp_conv->tree, &path, NULL);
                if (G_UNLIKELY (!path))
                    return FALSE;
                if (!gtk_tree_model_get_iter ((GtkTreeModel *) priv->store,
                            &iter, path))
                {
                    gtk_tree_path_free (path);
                    return FALSE;
                }
                gtk_tree_path_free (path);
                gtk_tree_model_get ((GtkTreeModel *) priv->store, &iter,
                        TREE_VIEW_COL_NODE, &node,
                        -1);
                if (G_UNLIKELY (!node))
                    return FALSE;
                *type = DONNA_ARG_TYPE_STRING;
                *ptr = donna_node_get_name (node);
                *destroy = g_free;
                g_object_unref (node);
                return TRUE;
            }

        case 'F':
            if (priv->filter)
            {
                if (extra)
                {
                    *type = _DONNA_ARG_TYPE_CUSTOM;
                    *ptr = sp_custom_conv;
                }
                else
                {
                    *type = DONNA_ARG_TYPE_STRING;
                    *ptr = donna_filter_get_filter (priv->filter);
                    *destroy = g_free;
                }
                return TRUE;
            }
            else
                return FALSE;

        case 'K':
            *type = DONNA_ARG_TYPE_STRING;
            *ptr = priv->key_mode;
            return TRUE;

        case 'k':
            *type = _DONNA_ARG_TYPE_CUSTOM;
            *ptr = sp_custom_conv;
            return TRUE;

        case 'a':
            *type = DONNA_ARG_TYPE_INT;
            if (sp_conv->nb_a == -1)
                sp_conv->nb_a = (gint) g_hash_table_size (priv->hashtable);
            if (extra)
            {
                *type = _DONNA_ARG_TYPE_CUSTOM;
                *ptr = sp_custom_conv;
            }
            else
            {
                *type = DONNA_ARG_TYPE_INT;
                *ptr = &sp_conv->nb_a;
            }
            return TRUE;

        case 'v':
            *type = DONNA_ARG_TYPE_INT;
            if (sp_conv->nb_v == -1)
                sp_conv->nb_v = _gtk_tree_model_get_count ((GtkTreeModel *) priv->store);
            if (extra)
            {
                *type = _DONNA_ARG_TYPE_CUSTOM;
                *ptr = sp_custom_conv;
            }
            else
            {
                *type = DONNA_ARG_TYPE_INT;
                *ptr = &sp_conv->nb_v;
            }
            return TRUE;

        case 'h':
            *type = DONNA_ARG_TYPE_INT;
            if (sp_conv->nb_a == -1)
                sp_conv->nb_a = (gint) g_hash_table_size (priv->hashtable);
            if (sp_conv->nb_v == -1)
                sp_conv->nb_v = _gtk_tree_model_get_count ((GtkTreeModel *) priv->store);
            if (sp_conv->nb_h == -1)
                sp_conv->nb_h = sp_conv->nb_a - sp_conv->nb_v;
            if (extra)
            {
                *type = _DONNA_ARG_TYPE_CUSTOM;
                *ptr = sp_custom_conv;
            }
            else
            {
                *type = DONNA_ARG_TYPE_INT;
                *ptr = &sp_conv->nb_h;
            }
            return TRUE;

        case 's':
            if (!sp_conv->sel)
                sp_conv->sel = gtk_tree_view_get_selection ((GtkTreeView *) sp_conv->tree);
            if (sp_conv->nb_s == -1)
                sp_conv->nb_s = gtk_tree_selection_count_selected_rows (sp_conv->sel);
            if (extra)
            {
                *type = _DONNA_ARG_TYPE_CUSTOM;
                *ptr = sp_custom_conv;
            }
            else
            {
                *type = DONNA_ARG_TYPE_INT;
                *ptr = &sp_conv->nb_s;
            }
            return TRUE;

        /* see sp_custom_conv for "avhs" */
        case ',':
            *type = _DONNA_ARG_TYPE_CUSTOM;
            *ptr = sp_custom_conv;
            return TRUE;

        case 'A':
        case 'V':
        case 'H':
        case 'S':
            *type = _DONNA_ARG_TYPE_CUSTOM;
            *ptr = sp_custom_conv;
            return TRUE;

        case 'n':
            {
                GtkTreePath *path;
                GtkTreeIter iter;
                gint nb;

                nb = 0;
                gtk_tree_view_get_cursor ((GtkTreeView *) sp_conv->tree, &path, NULL);
                if (path)
                {
                    if (gtk_tree_model_get_iter ((GtkTreeModel *) priv->store,
                                &iter, path))
                    {
                        DonnaNode *node;

                        gtk_tree_model_get ((GtkTreeModel *) priv->store, &iter,
                                TREE_VIEW_COL_NODE, &node,
                                -1);
                        if (node)
                        {
                            nb = 1;
                            *type = DONNA_ARG_TYPE_STRING;
                            *ptr = donna_node_get_name (node);
                            *destroy = g_free;
                            g_object_unref (node);
                        }
                    }
                    gtk_tree_path_free (path);
                }
                /* returning FALSE will simply not resolve to anything; and
                 * since we know the is used with NO_QUOTES we don't have to
                 * return an empty string (to ba added/quoted) */
                return nb == 1;
            }

        case 'N':
            {
                gint nb;

                *type = DONNA_ARG_TYPE_STRING;
                *ptr = (gpointer) "";

                if (!sp_conv->sel)
                    sp_conv->sel = gtk_tree_view_get_selection (
                            (GtkTreeView *) sp_conv->tree);
                nb = gtk_tree_selection_count_selected_rows (sp_conv->sel);
                if (nb == 1)
                {
                    GtkTreeIter iter;
                    GList *list;

                    list = gtk_tree_selection_get_selected_rows (sp_conv->sel, NULL);
                    if (gtk_tree_model_get_iter ((GtkTreeModel *) priv->store,
                                &iter, list->data))
                    {
                        DonnaNode *node;

                        gtk_tree_model_get ((GtkTreeModel *) priv->store, &iter,
                                TREE_VIEW_COL_NODE, &node,
                                -1);
                        if (node)
                        {
                            *ptr = donna_node_get_name (node);
                            *destroy = g_free;
                            g_object_unref (node);
                        }
                    }
                    g_list_free_full (list, (GDestroyNotify) gtk_tree_path_free);
                }
                else if (nb > 1)
                {
                    *ptr = g_strdup_printf ("%d items selected", nb);
                    *destroy = g_free;
                }

                return TRUE;
            }
    }
    return FALSE;
}

static void
status_provider_render (DonnaStatusProvider    *sp,
                        guint                   id,
                        guint                   index,
                        GtkCellRenderer        *renderer)
{
    DonnaTreeViewPrivate *priv = ((DonnaTreeView *) sp)->priv;
    struct status *status;
    struct sp_conv sp_conv = { (DonnaTreeView *) sp, NULL, NULL, -1, -1, -1, -1 };
    DonnaContext context = { ST_CONTEXT_FLAGS, TRUE,
        (conv_flag_fn) status_provider_conv, &sp_conv };
    GString *str = NULL;
    guint i;

    for (i = 0; i < priv->statuses->len; ++i)
    {
        status = &g_array_index (priv->statuses, struct status, i);
        if (status->id == id)
            break;
    }
    if (G_UNLIKELY (i >= priv->statuses->len))
    {
        g_warning ("TreeView '%s': Asked to render unknown status #%d",
                priv->name, id);
        return;
    }

    sp_conv.status = status;
    donna_context_parse (&context, DONNA_CONTEXT_NO_QUOTES, priv->app,
            status->fmt, &str, NULL);

    if ((status->colors == ST_COLORS_KEYS && priv->key_mode)
            || (status->colors == ST_COLORS_VF && priv->filter))
    {
        DonnaConfig *config;
        gchar *prefix;
        gchar *s;

        if (status->colors == ST_COLORS_KEYS)
            prefix = g_strconcat ("key_mode_", priv->key_mode, "_", NULL);
        else
            prefix = (gchar *) "";

        config = donna_app_peek_config (priv->app);

        if (donna_config_get_string (config, NULL, &s,
                    "statusbar/%s/%sbackground",
                    status->name, prefix))
        {
            g_object_set (renderer,
                    "background-set",   TRUE,
                    "background",       s,
                    NULL);
            donna_renderer_set (renderer, "background-set", NULL);
            g_free (s);
        }
        else if (donna_config_get_string (config, NULL, &s,
                    "statusbar/%s/%sbackground-rgba",
                    status->name, prefix))
        {
            GdkRGBA rgba;

            if (gdk_rgba_parse (&rgba, s))
            {
                g_object_set (renderer,
                        "background-set",   TRUE,
                        "background-rgba",  &rgba,
                        NULL);
                donna_renderer_set (renderer, "background-set", NULL);
            }
            g_free (s);
        }

        if (donna_config_get_string (config, NULL, &s,
                    "statusbar/%s/%sforeground",
                    status->name, prefix))
        {
            g_object_set (renderer,
                    "foreground-set",   TRUE,
                    "foreground",       s,
                    NULL);
            donna_renderer_set (renderer, "foreground-set", NULL);
            g_free (s);
        }
        else if (donna_config_get_string (config, NULL, &s,
                    "statusbar/%s/%sforeground-rgba",
                    status->name, prefix))
        {
            GdkRGBA rgba;

            if (gdk_rgba_parse (&rgba, s))
            {
                g_object_set (renderer,
                        "foreground-set",   TRUE,
                        "foreground-rgba",  &rgba,
                        NULL);
                donna_renderer_set (renderer, "foreground-set", NULL);
            }
            g_free (s);
        }

        if (status->colors == ST_COLORS_KEYS)
            g_free (prefix);
    }
    g_object_set (renderer,
            "visible",  TRUE,
            "text",     (str) ? str->str : status->fmt,
            NULL);
    if (str)
        g_string_free (str, TRUE);
}

static gboolean
status_provider_set_tooltip (DonnaStatusProvider    *sp,
                             guint                   id,
                             guint                   index,
                             GtkTooltip             *tooltip)
{
    DonnaTreeViewPrivate *priv = ((DonnaTreeView *) sp)->priv;
    struct status *status;
    struct sp_conv sp_conv = { (DonnaTreeView *) sp, NULL, NULL, -1, -1, -1, -1 };
    DonnaContext context = { ST_CONTEXT_FLAGS, TRUE,
        (conv_flag_fn) status_provider_conv, &sp_conv };
    GString *str = NULL;
    gchar *fmt;
    guint i;

    for (i = 0; i < priv->statuses->len; ++i)
    {
        status = &g_array_index (priv->statuses, struct status, i);
        if (status->id == id)
            break;
    }
    if (G_UNLIKELY (i >= priv->statuses->len))
    {
        g_warning ("TreeView '%s': Asked for tooltip of unknown status #%d",
                priv->name, id);
        return FALSE;
    }

    if (!donna_config_get_string (donna_app_peek_config (priv->app), NULL,
                &fmt, "statusbar/%s/format_tooltip", status->name))
        return FALSE;

    sp_conv.status = status;
    donna_context_parse (&context, DONNA_CONTEXT_NO_QUOTES, priv->app,
            fmt, &str, NULL);

    if ((str && str->len == 0) || *fmt == '\0')
    {
        if (str)
            g_string_free (str, TRUE);
        g_free (fmt);
        return FALSE;
    }

    gtk_tooltip_set_text (tooltip, (str) ? str->str : fmt);
    if (str)
        g_string_free (str, TRUE);
    g_free (fmt);
    return TRUE;
}

/* DonnaColumnType */

static const gchar *
columntype_get_name (DonnaColumnType    *ct)
{
    return "line-numbers";
}

static const gchar *
columntype_get_renderers (DonnaColumnType    *ct)
{
    return "t";
}

static void
columntype_get_options (DonnaColumnType    *ct,
                        DonnaColumnOptionInfo **options,
                        guint              *nb_options)
{
    static DonnaColumnOptionInfo o[] = {
        { "relative",           G_TYPE_BOOLEAN,     NULL },
        { "relative_focused",   G_TYPE_BOOLEAN,     NULL }
    };

    *options = o;
    *nb_options = G_N_ELEMENTS (o);
}

static DonnaColumnTypeNeed
columntype_refresh_data (DonnaColumnType  *ct,
                         const gchar        *col_name,
                         const gchar        *arr_name,
                         const gchar        *tv_name,
                         gboolean            is_tree,
                         gpointer           *data)
{
    DonnaTreeViewPrivate *priv = ((DonnaTreeView *) ct)->priv;
    DonnaConfig *config;
    DonnaColumnTypeNeed need = DONNA_COLUMN_TYPE_NEED_NOTHING;

    config = donna_app_peek_config (priv->app);

    if (priv->ln_relative != donna_config_get_boolean_column (config, col_name,
                arr_name, tv_name, is_tree, "column_types/line-numbers",
                "relative", FALSE))
    {
        need |= DONNA_COLUMN_TYPE_NEED_REDRAW;
        priv->ln_relative = !priv->ln_relative;
    }

    if (priv->ln_relative_focused != donna_config_get_boolean_column (config, col_name,
                arr_name, tv_name, is_tree, "column_types/line-numbers",
                "relative_focused", TRUE))
    {
        if (priv->ln_relative)
            need |= DONNA_COLUMN_TYPE_NEED_REDRAW;
        priv->ln_relative_focused = !priv->ln_relative_focused;
    }

    return need;
}

static void
columntype_free_data (DonnaColumnType    *ct,
                      gpointer            data)
{
    /* void */
}

static GPtrArray *
columntype_get_props (DonnaColumnType    *ct,
                      gpointer            data)
{
    return NULL;
}

static DonnaColumnTypeNeed
columntype_set_option (DonnaColumnType    *ct,
                       const gchar        *col_name,
                       const gchar        *arr_name,
                       const gchar        *tv_name,
                       gboolean            is_tree,
                       gpointer            data,
                       const gchar        *option,
                       gpointer            value,
                       gboolean            toggle,
                       DonnaColumnOptionSaveLocation save_location,
                       GError            **error)
{
    DonnaTreeViewPrivate *priv = ((DonnaTreeView *) ct)->priv;
    gpointer v;

    if (streq (option, "relative"))
    {
        v = (value) ? value : &priv->ln_relative;
        if (!DONNA_COLUMN_TYPE_GET_INTERFACE (ct)->helper_set_option (ct, col_name,
                    arr_name, tv_name, is_tree, "column_types/line-numbers",
                    &save_location,
                    option, G_TYPE_BOOLEAN, &priv->ln_relative, v, error))
            return DONNA_COLUMN_TYPE_NEED_NOTHING;

        if (save_location != DONNA_COLUMN_OPTION_SAVE_IN_MEMORY)
            return DONNA_COLUMN_TYPE_NEED_NOTHING;

        if (value)
            priv->ln_relative = * (gboolean *) value;
        return DONNA_COLUMN_TYPE_NEED_REDRAW;
    }
    else if (streq (option, "relative_focused"))
    {
        v = (value) ? value : &priv->ln_relative_focused;
        if (!DONNA_COLUMN_TYPE_GET_INTERFACE (ct)->helper_set_option (ct, col_name,
                    arr_name, tv_name, is_tree, "column_types/line-numbers",
                    &save_location,
                    option, G_TYPE_BOOLEAN, &priv->ln_relative_focused, v, error))
            return DONNA_COLUMN_TYPE_NEED_NOTHING;

        if (save_location != DONNA_COLUMN_OPTION_SAVE_IN_MEMORY)
            return DONNA_COLUMN_TYPE_NEED_NOTHING;

        if (value)
            priv->ln_relative_focused = * (gboolean *) value;
        return DONNA_COLUMN_TYPE_NEED_REDRAW;
    }

    g_set_error (error, DONNA_COLUMN_TYPE_ERROR,
            DONNA_COLUMN_TYPE_ERROR_OTHER,
            "ColumnType 'line-numbers': Unknown option '%s'",
            option);
    return DONNA_COLUMN_TYPE_NEED_NOTHING;
}

static gchar *
columntype_get_context_alias (DonnaColumnType   *ct,
                              gpointer           data,
                              const gchar       *alias,
                              const gchar       *extra,
                              DonnaContextReference reference,
                              DonnaNode         *node_ref,
                              get_sel_fn         get_sel,
                              gpointer           get_sel_data,
                              const gchar       *prefix,
                              GError           **error)
{
    const gchar *save_location;

    if (!streq (alias, "options"))
    {
        g_set_error (error, DONNA_CONTEXT_MENU_ERROR,
                DONNA_CONTEXT_MENU_ERROR_UNKNOWN_ALIAS,
                "ColumnType 'line-numbers': Unknown alias '%s'",
                alias);
        return NULL;
    }

    save_location = DONNA_COLUMN_TYPE_GET_INTERFACE (ct)->
        helper_get_save_location (ct, &extra, TRUE, error);
    if (!save_location)
        return NULL;

    if (extra)
    {
        g_set_error (error, DONNA_CONTEXT_MENU_ERROR,
                DONNA_CONTEXT_MENU_ERROR_OTHER,
                "ColumnType 'line-numbers': Invalid extra '%s' for alias '%s'",
                extra, alias);
        return NULL;
    }

    return g_strconcat (
            prefix, "relative:@", save_location, ",",
            prefix, "relative_focused:@", save_location,
            NULL);
}

static gboolean
columntype_get_context_item_info (DonnaColumnType   *ct,
                                  gpointer           data,
                                  const gchar       *item,
                                  const gchar       *extra,
                                  DonnaContextReference reference,
                                  DonnaNode         *node_ref,
                                  get_sel_fn         get_sel,
                                  gpointer           get_sel_data,
                                  DonnaContextInfo  *info,
                                  GError           **error)
{
    DonnaTreeViewPrivate *priv = ((DonnaTreeView *) ct)->priv;
    const gchar *save_location;

    save_location = DONNA_COLUMN_TYPE_GET_INTERFACE (ct)->
        helper_get_save_location (ct, &extra, FALSE, error);
    if (!save_location)
        return FALSE;

    if (streq (item, "relative"))
    {
        info->is_visible = TRUE;
        info->is_sensitive = TRUE;

        info->name = "Show Relative Line Numbers";
        info->icon_special = DONNA_CONTEXT_ICON_IS_CHECK;
        info->is_active = priv->ln_relative;
    }
    else if (streq (item, "relative_focused"))
    {
        info->is_visible = TRUE;
        info->is_sensitive = priv->ln_relative;

        info->name = "Show Relative Line Numbers Only When Focused";
        info->icon_special = DONNA_CONTEXT_ICON_IS_CHECK;
        info->is_active = priv->ln_relative_focused;
    }
    else
    {
        g_set_error (error, DONNA_CONTEXT_MENU_ERROR,
                DONNA_CONTEXT_MENU_ERROR_UNKNOWN_ITEM,
                "ColumnType 'line-numbers': Unknown item '%s'",
                item);
        return FALSE;
    }

    info->trigger = DONNA_COLUMN_TYPE_GET_INTERFACE (ct)->
        helper_get_set_option_trigger (item,
                (info->is_active) ? "0" : "1", FALSE,
                NULL, NULL, NULL, save_location);
    info->free_trigger = TRUE;

    return TRUE;
}


GtkWidget *
donna_tree_view_new (DonnaApp    *app,
                     const gchar *name)
{
    DonnaTreeViewPrivate *priv;
    DonnaTreeView        *tree;
    GtkWidget            *w;
    GtkTreeView          *treev;
    GtkTreeModel         *model;
    GtkTreeSelection     *sel;

    g_return_val_if_fail (DONNA_IS_APP (app), NULL);
    g_return_val_if_fail (name != NULL, NULL);

    w = g_object_new (DONNA_TYPE_TREE_VIEW, NULL);
    treev = GTK_TREE_VIEW (w);
    gtk_widget_set_name (w, name);
    gtk_tree_view_set_fixed_height_mode (treev, TRUE);

    /* tooltip */
    g_signal_connect (G_OBJECT (w), "query-tooltip",
            G_CALLBACK (query_tooltip_cb), NULL);
    gtk_widget_set_has_tooltip (w, TRUE);

    tree        = DONNA_TREE_VIEW (w);
    priv        = tree->priv;
    priv->app   = g_object_ref (app);
    priv->name  = g_strdup (name);

    load_config (tree);

    DONNA_DEBUG (TREE_VIEW, priv->name,
            g_debug ("TreeView '%s': setting up as %s",
                priv->name, (priv->is_tree) ? "tree" : "list"));

    if (priv->is_tree)
    {
        /* store */
        priv->store = gtk_tree_store_new (TREE_NB_COLS,
                DONNA_TYPE_NODE,/* TREE_COL_NODE */
                G_TYPE_INT,     /* TREE_COL_EXPAND_STATE */
                G_TYPE_BOOLEAN, /* TREE_COL_EXPAND_FLAG */
                G_TYPE_STRING,  /* TREE_COL_ROW_CLASS */
                G_TYPE_STRING,  /* TREE_COL_NAME */
                G_TYPE_ICON,    /* TREE_COL_ICON */
                G_TYPE_STRING,  /* TREE_COL_BOX */
                G_TYPE_STRING,  /* TREE_COL_HIGHLIGHT */
                G_TYPE_STRING,  /* TREE_COL_CLICK_MODE */
                G_TYPE_UINT);   /* TREE_COL_VISUALS */
        model = GTK_TREE_MODEL (priv->store);
        /* some stylling */
        gtk_tree_view_set_enable_tree_lines (treev, TRUE);
        G_GNUC_BEGIN_IGNORE_DEPRECATIONS
        gtk_tree_view_set_rules_hint (treev, FALSE);
        G_GNUC_END_IGNORE_DEPRECATIONS
        gtk_tree_view_set_headers_visible (treev, FALSE);
    }
    else
    {
        /* store */
        priv->store = gtk_tree_store_new (LIST_NB_COLS,
                DONNA_TYPE_NODE); /* LIST_COL_NODE */
        model = GTK_TREE_MODEL (priv->store);
        /* some stylling */
        G_GNUC_BEGIN_IGNORE_DEPRECATIONS
        gtk_tree_view_set_rules_hint (treev, TRUE);
        G_GNUC_END_IGNORE_DEPRECATIONS
        gtk_tree_view_set_headers_visible (treev, TRUE);
        /* to refuse reordering column past the blank column on the right */
        gtk_tree_view_set_column_drag_function (treev, col_drag_func, NULL, NULL);
#ifdef JJK_RUBBER_SIGNAL
        gtk_tree_view_set_rubber_banding (treev, TRUE);
#endif
    }

    /* because on property update the refesh does only that, i.e. there's no
     * auto-resort */
    g_signal_connect (model, "row-changed", (GCallback) row_changed_cb, tree);
    /* add to tree */
    gtk_tree_view_set_model (treev, model);
#ifdef GTK_IS_JJK
    if (priv->is_tree)
    {
        gtk_tree_view_set_row_class_column (treev, TREE_COL_ROW_CLASS);
        gtk_tree_boxable_set_box_column ((GtkTreeBoxable *) priv->store, TREE_COL_BOX);
    }
#endif

    /* selection mode */
    sel = gtk_tree_view_get_selection (treev);
    gtk_tree_selection_set_mode (sel, (priv->is_tree)
            ? GTK_SELECTION_BROWSE : GTK_SELECTION_MULTIPLE);

    g_signal_connect (G_OBJECT (sel), "changed",
            G_CALLBACK (selection_changed_cb), tree);

    /* interactive search */
    gtk_tree_view_set_search_equal_func (treev,
            (GtkTreeViewSearchEqualFunc) interactive_search, tree, NULL);
    gtk_tree_view_set_search_column (treev, 0);

    return w;
}
