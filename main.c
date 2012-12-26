
#include <gtk/gtk.h>
#include "fstree.h"

static void
window_destroy_cb (GtkWidget *window, gpointer data)
{
    gtk_main_quit ();
}

static void
tb_fill_tree_clicked_cb (GtkToolButton *tb_btn, FsTree *fstree)
{
    fstree_add_root (fstree, "/home/jjacky");
}

static void
tb_new_root_clicked_cb (GtkToolButton *tb_btn, FsTree *fstree)
{
    fstree_set_root (fstree, "/home/jjacky");
}

int
main (int argc, char *argv[])
{
    GtkWidget       *_window;
    GtkWindow       *window;
    GtkWidget       *_box;
    GtkBox          *box;
    GtkWidget       *_tb;
    GtkToolbar      *tb;
    GtkWidget       *_paned;
    GtkPaned        *paned;
    GtkWidget       *_scrolled_window;
    GtkWidget       *_tree;
    GtkTreeView     *tree;
    GtkTreeModel    *list_model;
    GtkListStore    *list_store;
    GtkWidget       *_list;
    GtkTreeView     *list;

    gtk_init (&argc, &argv);

    /* main window */
    _window = gtk_window_new (GTK_WINDOW_TOPLEVEL);
    window = GTK_WINDOW (_window);

    g_signal_connect (G_OBJECT (window), "destroy",
            G_CALLBACK (window_destroy_cb), NULL);

    gtk_window_set_title (window, "Donnatella");

    _box = gtk_box_new (GTK_ORIENTATION_VERTICAL, 0);
    box = GTK_BOX (_box);
    gtk_container_add (GTK_CONTAINER (window), _box);
    gtk_widget_show (_box);

    /* toolbar */
    _tb = gtk_toolbar_new ();
    tb = GTK_TOOLBAR (_tb);
    gtk_toolbar_set_icon_size (tb, GTK_ICON_SIZE_SMALL_TOOLBAR);
    gtk_box_pack_start (box, _tb, FALSE, FALSE, 0);
    gtk_widget_show (_tb);

    GtkToolItem *tb_btn;
    tb_btn = gtk_tool_button_new_from_stock (GTK_STOCK_APPLY);
    gtk_toolbar_insert (tb, tb_btn, -1);
    gtk_widget_show (GTK_WIDGET (tb_btn));

    GtkToolItem *tb_btn2;
    tb_btn2 = gtk_tool_button_new_from_stock (GTK_STOCK_REFRESH);
    gtk_toolbar_insert (tb, tb_btn2, -1);
    gtk_widget_show (GTK_WIDGET (tb_btn2));

    /* paned to host tree & list */
    _paned = gtk_paned_new (GTK_ORIENTATION_HORIZONTAL);
    paned = GTK_PANED (_paned);
    gtk_box_pack_start (box, _paned, TRUE, TRUE, 0);
    gtk_widget_show (_paned);

    /* tree */
    _tree = fstree_new (NULL);
    tree = GTK_TREE_VIEW (_tree);
    /* scrolled window */
    _scrolled_window = gtk_scrolled_window_new (NULL, NULL);
    gtk_paned_pack1 (paned, _scrolled_window, FALSE, TRUE);
    gtk_widget_show (_scrolled_window);
    /* size */
    gtk_container_add (GTK_CONTAINER (_scrolled_window), _tree);
    gtk_widget_show (_tree);

    /* list */
    list_store = gtk_list_store_new (1, G_TYPE_STRING);
    list_model = GTK_TREE_MODEL (list_store);
    _list = gtk_tree_view_new_with_model (list_model);
    list = GTK_TREE_VIEW (_list);
    gtk_paned_pack2 (paned, _list, TRUE, TRUE);
    gtk_widget_show (_list);

    /* tb signals */
    GtkTreeModel *model;
    model = gtk_tree_view_get_model (tree);
    g_signal_connect (G_OBJECT (tb_btn), "clicked",
            G_CALLBACK (tb_fill_tree_clicked_cb), tree);
    g_signal_connect (G_OBJECT (tb_btn2), "clicked",
            G_CALLBACK (tb_new_root_clicked_cb), tree);

    /* show everything */
    gtk_window_set_default_size (window, 420, 230);
    gtk_paned_set_position (paned, 230);
    gtk_widget_show (_window);
    gtk_main ();

    return 0;
}
