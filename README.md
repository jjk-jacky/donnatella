
# donnatella: your file manager

donnatella - donna for short - is a free software, file manager for GNU/Linux
systems.

Here's a list of cool features already available :

- **Registers**. vim users are familiar with the notion of having more than one
  "internal clipboard" (or register). donna uses a similar concept, and you can
  cut/copy files to any register you want, have as many as you need, and the
  clipboard is simply a special register (named '+').

  In addition to the regular cut/copy/paste operations, donna offers a little
  more: you can also add to a register, you can paste forcing the operation to
  be a copy or move, regardless of how items where put in the register (i.e.
  whether they were cut or copied), you can also paste into a new folder (to be
  created on the fly) or even delete the content of the register.

- **Key modes**. Unlike most (GUI) file managers, donna works with a system of
  key modes, inspired by what can be found in vim. This means that every key can
  be used to trigger an action. For instance, pressing `yy` will yank/copy the
  focused row to the default register, and `"+a2j` will add to the register '+'
  (i.e. clipboard) the focused row and the 2 below it. Then you can e.g. paste
  the clipboard into a new folder, using `"+N` You'll be prompted for the name
  of the new folder, with a few defaults taken from the first files to be
  pasted.

  Of course not everything is register-related, you can also edit the name (i.e.
  rename) via `en`, set the second sort order by size descending via `#dSs` and
  much more!

  Best of all is that you can define every key to your liking.

- **Marks**. There are lots of great ideas in vim, and marks are (also) one of
  those. Very much like you'd do in vim, pressing `md` will set mark 'd' to the
  current location. When you need to jump back there, simply press `'d`
  You can also use 'M' to set the mark to the focused row, to find that file
  again without needing to scroll and look for it.

  And in addition to such standard mark, donna also supports dynamic marks,
  where the destination isn't a location to jump to, but a command that will
  give said location. For example, `''` will jump back to the previous location
  in history (i.e. Go Back).

- **Click modes**. Because using the mouse can be pretty useful too, just like
  you can define your keys, you can define your clicks.

  Don't like that middle click on blank space scrolls to the focused row, you'd
  rather go up, or back?  It's all possible: just tell donna what to do, and
  it'll do it.

- **Commands**. As you've probably guessed by now, in donna, about everything
  that can be done is done by commands, and you can assign commands to about
  anything.

  Possibilities are limitless, so should you want/need to tweak something, it
  should be possible. Things will crazier with command `exec` allowing you to
  use external scripts (or binaries) to interact with donna...

- **Mini Tree**. As it happens, sometimes less is more. Browsing around in your
  file system might be something you do every single day, but you rarely visit
  every possible location every day. In fact, you probably have a limited set of
  locations you really need/use at a time, so why have to deal with a tree
  filled with all the useless noise?

  The minitree will only show locations you actually visited, hiding siblings
  until you actually go there, or ask to show them. Expanders will be red when
  no children is loaded in tree, orange when only some are (known as partially
  expanded), and black when all children are loaded (This, amongst other things,
  can be customized via CSS). A middle click will let you switch, and a right
  click will popup all the children to let you easily browse where you need to
  go.

- **Multi Root Tree**. Sure, by default your tree will have one root, that of
  your filesystem. But that's not always the most useful. Maybe you'd like to
  have your home folder as a root, or event just your project's main folder? You
  can do it!

  donna let's you use the location of your choice as root, and you can have as
  many roots as you need. Another great addition to the minitree to reduce the
  noise and focus on only what you need.

- **Multiple Trees**. So you can find your tree exactly as you left it, you can
  save it to a file, and load it later. Of course, you can also do the same for
  lists.

  This also allows you to have project-specific trees, and load them as needed.
  (This will in fact be even better/easier to use once tabs are added...)

- **Selection Highlight**. This might be a personal one, but I can't stand the
  "full row select" thing, which seems almost the only thing know in the world
  of Linux.

  So donna let's you decide how to highlight selected rows: full row highlight,
  column (cell) highlight, full row underline, or (personal favorite) a
  combination of column (cell) highlight & row underline. Of course this is a
  treeview option, so your tree & list can use different ones (e.g. no underline
  on tree).

- **Full Layout Control**. Single pane? Dual pane? Octopane? However you want
  things is how you'll get them.

  donna doesn't have a fixed layout, but let's you define it. Have as many
  trees/lists as you want, arranged how you want them to be...

- **(Custom) Context Menus**. Contextual menus in donna aren't hard-coded, but
  user-defined. Of couse it comes with sensible defaults, but it is easy to
  tweak them as you wish, adding, moving, or removing items and/or submenus.

  And since you control the keys & clicks, you can decide when to show which
  menus, e.g. making yourself custom context menus as you want.

- **Control you columns**. You can specify which columns to use and in what
  order of course, but also the format of each of them. However you want you
  date or size to be formatted is how they shall be. donna can even show the age
  of files (e.g. "42m ago") instead of a full date, for all files or only
  recently modified ones.

  donna also offers a colored ways of showing permissions, focusing on what you
  can do (and why). And of course, tooltips can be used for the more lengthy
  format.

- **Arrangements**. An arrangement is what donna will use to show a location on
  list. Every time the list changes current location, a new arrangement might be
  loaded (if needed) and used. It will define the columns to use, sort orders,
  color filters & can even include column options.

  Column options do not indeed come from a single location in config, instead
  multiple locations are tried, refered to as the option path. Using such option
  path allows you to use arrangement to have different columns, or column
  options, based on the current location.

  You can therefore have certain locations with a different default sort order,
  a different format to show time or size, specific color filters used instead -
  or or in addition -  to general ones, even a different set of columns.

- **Filters**. A system of filters can be used against about all columns, as
  each column type comes with its own filtering capabilities, specific to its
  type: name will handle pattern/regex matching; time will allow to to compare
  by date, ranges, or filter files modified within the hour, or in the current
  week; etc

  You can even combine them using boolean logic. Filters allow powerful
  selections on names, times, permissions and more, and can be used in various
  forms (selection filters, color filters, visual filters, etc)

- **Colors**. Using colors can help a lot to figure out things without having to
  actually read and analyse information. Certain rows on tree can be "boxed" to
  have a specific background color (that extends to its children), to easily
  spot where you are. You can also use an highlight effect in the row itself
  only.

  And of course color filters can be used on list, based on any criteria, and
  applying color to the entire row of the column of your choice (thus allowing
  to "combine" filters).

- **Tree Visuals**. In addition to the box & highlight effect, you can also
  specify a name and/or icon to use for a specific row. The former can be
  especially useful for your own roots.

  You can even specify a row-specific click mode, pretty useful when using a
  tree to store bookmarks/favorites.

- **Little things**. Sometimes it's the little things that help the most. The
  ability to jump from files to folders, to quickly scroll to the focused row,
  have time/size columns sorted descendingly by default, to have the focus
  (and/or scroll) put on the folder you come from when going up, or have focusing
  click ignored (to not lose a selection when activating the window), use
  up/down arrows to move around during inline editing (e.g. file renaming) or
  Ctrl+A to select all/but the extension, ...

  Lots of attention is being put into donna and such "details."

- **Search Results**. As explained earlier, donna doesn't search, but parses the
  output of `find` and presents the results in its GUI. In fact, it can parse
  the output of anything outputting a list of full file names, so you can use
  which ever tool you need for the job required. Simply use prefix `<` to
  indicate that the output should be parsed; E.g: `<pacman -Qlq foobar` will
  list all the files owned by package foobar (on Arch Linux).

  Executing external processes can be done using other prefixes, whether you
  want to wait for the process to end or not, have it run in a terminal, ...

- **Everything is a node**. donna doesn't show you files & folders as much as
  so-called nodes, items & containers. Nodes belong to a domain, e.g. "fs" for
  the filesystem, but also "config" for donna's configuration, "mark" for its
  marks, etc

  This allows you to simply interact with donna & its configuration in a very
  simple/natural way, for example changing an option's value as you would rename
  a file, or going to "register:t" to browse the content of register 't', and
  simply select & remove files from there as you would from any regular folder.

- **Much More**. There's even more goodies in donna already, and an even longer
  list of things to add.

  Dive in!

## More to come...

donna is still early in development, and many things aren't done yet. It comes
with HTML documentation; Even though it's not quite complete, the main features
& all commands should be documented.

If you have ideas, questions or comments feel free to either open an issue on
[github](https://github.com/jjk-jacky/donnatella "donnatella @ GitHub") or email
me. And if you encounter any bugs, please let me know so I get a chance to try
and fix them.

## It's still early

Please note that the 0.x versions belong to the development branch, i.e. this
is an early release, and while the application is functional, there is lots
that hasn't been implemented yet, things will change, and bugs will be found
(and dealt with).

For instance, if you happen to be looking for such things as toolbars, tabs,
menubar, autorefresh or drag&drop, you're out of luck. All of these are examples
of items I haven't had a chance yet to implement. Worry not, it is (eventually)
coming...

So it's probably not a good idea to use it "in production," but feel free to
give it a try, and report what you thought.

## Patched GTK

donna is built upon GTK+3 (and the underlying GLib/GIO libraries). However,
because some of the features of donna were not doable using GTK+ as it is,
especially when it comes to the treeview, a patchset is available.

This [set of patches for
GTK+](http://jjacky.com/donnatella/gtk3-donnatella.1.tar.gz "Patchset for GTK+3")
will fix some bugs & add extra features, all the while remaining 100% compatible
with GTK+3. You can safely compile your patched GTK+ and install it, replacing
the vanilla GTK+. It won't change anything for other applications (unless they
were victims of the few fixed bugs), but will unleash the full power of
donnatella.

Obviously it would be better if this wasn't necessary, and I'd like to see all
patches merged upstream. This is a work in process, but unfortunately upstream
doesn't seem too eager to review those patches (Seems they don't have much love
for the treeview, because client-side decorations are so much more useful...
:p).

## Free Software

donnatella - Copyright (C) 2014 Olivier Brunel <jjk@jjacky.com>

donnatella is free software: you can redistribute it and/or modify it under the
terms of the GNU General Public License as published by the Free Software
Foundation, either version 3 of the License, or (at your option) any later
version.

donnatella is distributed in the hope that it will be useful, but WITHOUT ANY
WARRANTY; without even the implied warranty of MERCHANTABILITY or FITNESS FOR A
PARTICULAR PURPOSE.
See the GNU General Public License for more details.

You should have received a copy of the GNU General Public License along with
donnatella (COPYING). If not, see http://www.gnu.org/licenses/

## Want to know more?

Some useful links if you're looking for more info:

- [official site](http://jjacky.com/donnatella "donnatella @ jjacky.com")

- [source code & issue tracker](https://github.com/jjk-jacky/donnatella "donnatella @ GitHub.com")

- [blog post about where donna comes from](http://jjacky.com/2014-02-08-donnatella-a-brand-new-linux-file-manager "donnatella: A brand new Linux file manager @ jjacky.com")

- [PKGBUILD in AUR](https://aur.archlinux.org/packages/donnatella "AUR: donnatella")

- [PKGBUILD for git version in AUR](https://aur.archlinux.org/packages/donnatella-git "AUR: donnatella-git")

- [PKGBUILD for patched GTK+3 in AUR](https://aur.archlinux.org/packages/gtk3-donnatella "AUR: gtk3-donnatella")

Plus, donnatella comes with html documentation.
