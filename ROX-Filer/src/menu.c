/*
 * $Id$
 *
 * ROX-Filer, filer for the ROX desktop project
 * Copyright (C) 2002, the ROX-Filer team.
 *
 * This program is free software; you can redistribute it and/or modify it
 * under the terms of the GNU General Public License as published by the Free
 * Software Foundation; either version 2 of the License, or (at your option)
 * any later version.
 *
 * This program is distributed in the hope that it will be useful, but WITHOUT
 * ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or
 * FITNESS FOR A PARTICULAR PURPOSE.  See the GNU General Public License for
 * more details.
 *
 * You should have received a copy of the GNU General Public License along with
 * this program; if not, write to the Free Software Foundation, Inc., 59 Temple
 * Place, Suite 330, Boston, MA  02111-1307  USA
 */

/* menu.c - code for handling the popup menu */

#include "config.h"
#include <stdio.h>
#include <stdlib.h>
#include <sys/wait.h>
#include <sys/param.h>
#include <fcntl.h>
#include <errno.h>
#include <string.h>
#include <dirent.h>

#include <gtk/gtk.h>

#include "global.h"

#include "menu.h"
#include "run.h"
#include "action.h"
#include "filer.h"
#include "pixmaps.h"
#include "type.h"
#include "support.h"
#include "gui_support.h"
#include "options.h"
#include "choices.h"
#include "gtksavebox.h"
#include "mount.h"
#include "minibuffer.h"
#include "i18n.h"
#include "main.h"
#include "pinboard.h"
#include "dir.h"
#include "diritem.h"
#include "appmenu.h"
#include "usericons.h"
#include "infobox.h"
#include "collection.h"
#include "display.h"

#define C_ "<control>"

typedef enum {
	FILE_COPY_ITEM,
	FILE_RENAME_ITEM,
	FILE_LINK_ITEM,
	FILE_OPEN_FILE,
	FILE_HELP,
	FILE_SHOW_FILE_INFO,
	FILE_RUN_ACTION,
	FILE_SET_ICON,
	FILE_SEND_TO,
	FILE_DELETE,
	FILE_USAGE,
	FILE_CHMOD_ITEMS,
	FILE_FIND,
#ifdef HAVE_LIBVFS
	FILE_OPEN_VFS_RPM,
	FILE_OPEN_VFS_UTAR,
	FILE_OPEN_VFS_UZIP,
	FILE_OPEN_VFS_DEB,
#else
	FILE_OPEN_VFS_AVFS,
#endif
} FileOp;

typedef enum menu_icon_style {
  MIS_NONE, MIS_SMALL, MIS_LARGE, MIS_HUGE,
  MIS_CURRENT, /* As per current filer window */
  MIS_DEFAULT
} MenuIconStyle;

typedef void (*ActionFn)(GList *paths, char *dest_dir, char *leaf, int quiet);
typedef void MenuCallback(GtkWidget *widget, gpointer data);

GtkAccelGroup	*filer_keys;
GtkAccelGroup	*pinboard_keys;

GtkWidget *popup_menu = NULL;		/* Currently open menu */

static gint updating_menu = 0;		/* Non-zero => ignore activations */
static GList *send_to_paths = NULL;

/* Static prototypes */

static void save_menus(void);
static void menu_closed(GtkWidget *widget);
static void items_sensitive(gboolean state);
static void savebox_show(guchar *title, guchar *path, MaskedPixmap *image,
		gboolean (*callback)(guchar *current, guchar *new));
static gint save_to_file(GtkSavebox *savebox, guchar *pathname);
static gboolean action_with_leaf(ActionFn action, guchar *current, guchar *new);
static gboolean link_cb(guchar *initial, guchar *path);
static void select_nth_item(GtkMenuShell *shell, int n);
static void new_file_type(gchar *templ);
static void do_send_to(gchar *templ);
static void show_send_to_menu(GList *paths, GdkEvent *event);

/* Note that for most of these callbacks none of the arguments are used. */

/* (action used in these three - DetailsType) */
static void huge_with(gpointer data, guint action, GtkWidget *widget);
static void large_with(gpointer data, guint action, GtkWidget *widget);
static void small_with(gpointer data, guint action, GtkWidget *widget);

static void sort_name(gpointer data, guint action, GtkWidget *widget);
static void sort_type(gpointer data, guint action, GtkWidget *widget);
static void sort_size(gpointer data, guint action, GtkWidget *widget);
static void sort_date(gpointer data, guint action, GtkWidget *widget);

static void hidden(gpointer data, guint action, GtkWidget *widget);
static void show_thumbs(gpointer data, guint action, GtkWidget *widget);
static void refresh(gpointer data, guint action, GtkWidget *widget);

static void file_op(gpointer data, FileOp action, GtkWidget *widget);

static void select_all(gpointer data, guint action, GtkWidget *widget);
static void clear_selection(gpointer data, guint action, GtkWidget *widget);
static void invert_selection(gpointer data, guint action, GtkWidget *widget);
static void new_directory(gpointer data, guint action, GtkWidget *widget);
static void new_file(gpointer data, guint action, GtkWidget *widget);
static void xterm_here(gpointer data, guint action, GtkWidget *widget);

static void open_parent_same(gpointer data, guint action, GtkWidget *widget);
static void open_parent(gpointer data, guint action, GtkWidget *widget);
static void home_directory(gpointer data, guint action, GtkWidget *widget);
static void new_window(gpointer data, guint action, GtkWidget *widget);
/* static void new_user(gpointer data, guint action, GtkWidget *widget); */
static void close_window(gpointer data, guint action, GtkWidget *widget);

/* (action used in this - MiniType) */
static void mini_buffer(gpointer data, guint action, GtkWidget *widget);
static void resize(gpointer data, guint action, GtkWidget *widget);

#ifdef GTK2
#define MENUS_NAME "menus2"
static void keys_changed(gpointer data);
#else
# define MENUS_NAME "menus"
static void mark_menus_modified(gboolean mod);
#endif

static GtkWidget	*filer_menu;		/* The popup filer menu */
static GtkWidget	*filer_file_item;	/* The File '' label */
static GtkWidget	*filer_file_menu;	/* The File '' menu */
static GtkWidget	*file_shift_item;	/* Shift Open label */
GtkWidget	*display_large_menu;	/* Display->Large With... */
GtkWidget	*display_small_menu;	/* Display->Small With... */
#ifdef HAVE_LIBVFS
static GtkWidget	*filer_vfs_menu;	/* The Open VFS menu */
#endif
static GtkWidget	*filer_hidden_menu;	/* The Show Hidden item */
static GtkWidget	*filer_thumb_menu;	/* The Show Thumbs item */
static GtkWidget	*filer_new_window;	/* The New Window item */
static GtkWidget        *filer_new_menu;        /* The New submenu */

/* Used for Copy, etc */
static GtkWidget	*savebox = NULL;	
static GtkWidget	*check_relative = NULL;	
static guchar		*current_path = NULL;
static gboolean	(*current_savebox_callback)(guchar *current, guchar *new);

#undef N_
#define N_(x) x

static GtkItemFactoryEntry filer_menu_def[] = {
{N_("Display"),			NULL, NULL, 0, "<Branch>"},
{">" N_("Huge Icons"),   	NULL, huge_with, DETAILS_NONE, NULL},
{">" N_("Large Icons"),   	NULL, large_with, DETAILS_NONE, NULL},
{">" N_("Small Icons"),   	NULL, small_with, DETAILS_NONE, NULL},
{">" N_("Huge, With..."),	NULL, NULL, 0, "<Branch>"},
{">>" N_("Summary"),		NULL, huge_with, DETAILS_SUMMARY, NULL},
{">>" N_("Sizes"),		NULL, huge_with, DETAILS_SIZE, NULL},
{">>" N_("Permissions"),	NULL, huge_with, DETAILS_PERMISSIONS, NULL},
{">>" N_("Type"),		NULL, huge_with, DETAILS_TYPE, NULL},
{">>" N_("Times"),		NULL, huge_with, DETAILS_TIMES, NULL},
{">" N_("Large, With..."),	NULL, NULL, 0, "<Branch>"},
{">>" N_("Summary"),		NULL, large_with, DETAILS_SUMMARY, NULL},
{">>" N_("Sizes"),		NULL, large_with, DETAILS_SIZE, NULL},
{">>" N_("Permissions"),	NULL, large_with, DETAILS_PERMISSIONS, NULL},
{">>" N_("Type"),		NULL, large_with, DETAILS_TYPE, NULL},
{">>" N_("Times"),		NULL, large_with, DETAILS_TIMES, NULL},
{">" N_("Small, With..."),	NULL, NULL, 0, "<Branch>"},
{">>" N_("Summary"),		NULL, small_with, DETAILS_SUMMARY, NULL},
{">>" N_("Sizes"),		NULL, small_with, DETAILS_SIZE, NULL},
{">>" N_("Permissions"),	NULL, small_with, DETAILS_PERMISSIONS, NULL},
{">>" N_("Type"),		NULL, small_with, DETAILS_TYPE, NULL},
{">>" N_("Times"),		NULL, small_with, DETAILS_TIMES, NULL},
{">",				NULL, NULL, 0, "<Separator>"},
{">" N_("Sort by Name"),	NULL, sort_name, 0, NULL},
{">" N_("Sort by Type"),	NULL, sort_type, 0, NULL},
{">" N_("Sort by Date"),	NULL, sort_date, 0, NULL},
{">" N_("Sort by Size"),	NULL, sort_size, 0, NULL},
{">",				NULL, NULL, 0, "<Separator>"},
{">" N_("Show Hidden"),   	NULL, hidden, 0, "<ToggleItem>"},
{">" N_("Show Thumbnails"),	NULL, show_thumbs, 0, "<ToggleItem>"},
{">" N_("Refresh"),		NULL, refresh, 0, NULL},
{N_("File"),			NULL, NULL, 0, "<Branch>"},
{">" N_("Copy..."),		NULL, file_op, FILE_COPY_ITEM, NULL},
{">" N_("Rename..."),		NULL, file_op, FILE_RENAME_ITEM, NULL},
{">" N_("Link..."),		NULL, file_op, FILE_LINK_ITEM, NULL},
{">" N_("Shift Open"),   	NULL, file_op, FILE_OPEN_FILE, NULL},
{">" N_("Help"),		NULL, file_op, FILE_HELP, NULL},
{">" N_("Info"),		NULL, file_op, FILE_SHOW_FILE_INFO, NULL},
{">" N_("Set Run Action..."),	NULL, file_op, FILE_RUN_ACTION, NULL},
{">" N_("Set Icon..."),		NULL, file_op, FILE_SET_ICON, NULL},
#ifdef HAVE_LIBVFS
{">" N_("Open VFS"),		NULL, NULL, 0, "<Branch>"},
{">>" N_("Unzip"),		NULL, file_op, FILE_OPEN_VFS_UZIP, NULL},
{">>" N_("Untar"),		NULL, file_op, FILE_OPEN_VFS_UTAR, NULL},
{">>" N_("Deb"),              	NULL, file_op, FILE_OPEN_VFS_DEB, NULL},
{">>" N_("RPM"),		NULL, file_op, FILE_OPEN_VFS_RPM, NULL},
#else
{">" N_("Open AVFS"),		NULL, file_op, FILE_OPEN_VFS_AVFS, NULL},
#endif
{">",				NULL, NULL, 0, "<Separator>"},
{">" N_("Send To..."),		NULL, file_op, FILE_SEND_TO, NULL},
{">" N_("Delete"),	    	NULL, file_op, FILE_DELETE, NULL},
{">" N_("Disk Usage"),		NULL, file_op, FILE_USAGE, NULL},
{">" N_("Permissions"),		NULL, file_op, FILE_CHMOD_ITEMS, NULL},
{">" N_("Find"),		NULL, file_op, FILE_FIND, NULL},
{N_("Select"),	    		NULL, NULL, 0, "<Branch>"},
{">" N_("Select All"),	    	NULL, select_all, 0, NULL},
{">" N_("Clear Selection"),	NULL, clear_selection, 0, NULL},
{">" N_("Invert Selection"),	NULL, invert_selection, 0, NULL},
{">" N_("Select If..."),	NULL, mini_buffer, MINI_SELECT_IF, NULL},
{N_("Options..."),		NULL, menu_show_options, 0, NULL},
{N_("New"),			NULL, NULL, 0, "<Branch>"},
{">" N_("Directory"),		NULL, new_directory, 0, NULL},
{">" N_("Blank file"),		NULL, new_file, 0, NULL},
{N_("Xterm Here"),		NULL, xterm_here, 0, NULL},
{N_("Window"),			NULL, NULL, 0, "<Branch>"},
{">" N_("Parent, New Window"), 	NULL, open_parent, 0, NULL},
{">" N_("Parent, Same Window"), NULL, open_parent_same, 0, NULL},
{">" N_("New Window"),		NULL, new_window, 0, NULL},
{">" N_("Home Directory"),	NULL, home_directory, 0, NULL},
{">" N_("Resize Window"),	NULL, resize, 0, NULL},
/* {">" N_("New, As User..."),	NULL, new_user, 0, NULL}, */

{">" N_("Close Window"),	NULL, close_window, 0, NULL},
{">",				NULL, NULL, 0, "<Separator>"},
{">" N_("Enter Path..."),	"slash", mini_buffer, MINI_PATH, NULL},
{">" N_("Shell Command..."),	NULL, mini_buffer, MINI_SHELL, NULL},
{">",				NULL, NULL, 0, "<Separator>"},
{">" N_("Show ROX-Filer Help"), "F1", menu_rox_help, 0, NULL},
};


#define GET_MENU_ITEM(var, menu)	\
		var = gtk_item_factory_get_widget(item_factory,	"<" menu ">");

#define GET_SMENU_ITEM(var, menu, sub)	\
	do {				\
		tmp = g_strdup_printf("<" menu ">/%s", _(sub));		\
		var = gtk_item_factory_get_widget(item_factory,	tmp); 	\
		g_free(tmp);		\
	} while (0)

#define GET_SSMENU_ITEM(var, menu, sub, subsub)	\
	do {				\
		tmp = g_strdup_printf("<" menu ">/%s/%s", _(sub), _(subsub)); \
		var = gtk_item_factory_get_widget(item_factory,	tmp); 	\
		g_free(tmp);		\
	} while (0)

void menu_init(void)
{
	char			*menurc;
	GList			*items;
	guchar			*tmp;
	GtkWidget		*item;
	GtkTooltips		*tips;
	GtkItemFactory  	*item_factory;

	filer_keys = gtk_accel_group_new();
	item_factory = menu_create(filer_menu_def,
		sizeof(filer_menu_def) / sizeof(*filer_menu_def),
		"<filer>", filer_keys);

	GET_MENU_ITEM(filer_menu, "filer");
	GET_SMENU_ITEM(filer_file_menu, "filer", "File");
#ifdef HAVE_LIBVFS
	GET_SSMENU_ITEM(filer_vfs_menu, "filer", "File", "Open VFS");
#endif
	GET_SSMENU_ITEM(filer_hidden_menu, "filer", "Display", "Show Hidden");
	GET_SSMENU_ITEM(filer_thumb_menu, "filer", "Display",
							"Show Thumbnails");

	GET_SSMENU_ITEM(display_large_menu, "filer",
			"Display", "Large, With...");
	GET_SSMENU_ITEM(display_small_menu, "filer",
			"Display", "Small, With...");

	GET_SMENU_ITEM(filer_new_menu, "filer", "New");

	/* File '' label... */
	items = gtk_container_children(GTK_CONTAINER(filer_menu));
	filer_file_item = GTK_BIN(g_list_nth(items, 1)->data)->child;
	g_list_free(items);

	/* Shift Open... label */
	items = gtk_container_children(GTK_CONTAINER(filer_file_menu));
	file_shift_item = GTK_BIN(g_list_nth(items, 3)->data)->child;
	g_list_free(items);

	GET_SSMENU_ITEM(item, "filer", "Window", "New Window");
	filer_new_window = GTK_BIN(item)->child;

	menurc = choices_find_path_load(MENUS_NAME, PROJECT);
	if (menurc)
	{
#ifdef GTK2
		gtk_accel_map_load(menurc);
#else
		gtk_item_factory_parse_rc(menurc);
		mark_menus_modified(FALSE);
#endif
		g_free(menurc);
	}

	gtk_signal_connect(GTK_OBJECT(filer_menu), "unmap_event",
			GTK_SIGNAL_FUNC(menu_closed), NULL);
	gtk_signal_connect(GTK_OBJECT(filer_file_menu), "unmap_event",
			GTK_SIGNAL_FUNC(menu_closed), NULL);

	option_add_string("menu_xterm", "xterm", NULL);
	option_add_int("menu_iconsize", MIS_SMALL, NULL);
	option_add_saver(save_menus);

	tips = gtk_tooltips_new();
	check_relative = gtk_check_button_new_with_label(_("Relative link"));
	gtk_toggle_button_set_active(GTK_TOGGLE_BUTTON(check_relative), TRUE);
	GTK_WIDGET_UNSET_FLAGS(check_relative, GTK_CAN_FOCUS);
	gtk_tooltips_set_tip(tips, check_relative,
			_("If on, the symlink will store the path from the "
			"symlink to the target file. Use this if the symlink "
			"and the target will be moved together.\n"
			"If off, the path from the root directory is stored - "
			"use this if the symlink may move but the target will "
			"stay put."), NULL);
			
	savebox = gtk_savebox_new();
	gtk_box_pack_start(GTK_BOX(GTK_SAVEBOX(savebox)->vbox),
			   check_relative, FALSE, TRUE, 0);
	gtk_widget_show(check_relative);

	gtk_signal_connect_object(GTK_OBJECT(savebox), "save_to_file",
				GTK_SIGNAL_FUNC(save_to_file), NULL);
	gtk_signal_connect_object(GTK_OBJECT(savebox), "save_done",
				GTK_SIGNAL_FUNC(gtk_widget_hide),
				GTK_OBJECT(savebox));

#ifdef GTK2
	g_signal_connect_object(G_OBJECT(filer_keys), "accel_changed",
				  (GCallback) keys_changed, NULL, 0);
#else
	atexit(save_menus);
#endif
}

/* Name is in the form "<panel>" */
GtkItemFactory *menu_create(GtkItemFactoryEntry *def, int n_entries,
			    guchar *name, GtkAccelGroup *keys)
{
	GtkItemFactory  	*item_factory;
	GtkItemFactoryEntry	*translated;

	if (!keys)
	{
		keys = gtk_accel_group_new();
		gtk_accel_group_lock(keys);
	}

	item_factory = gtk_item_factory_new(GTK_TYPE_MENU, name, keys);

	translated = translate_entries(def, n_entries);
	gtk_item_factory_create_items(item_factory, n_entries,
					translated, NULL);
	free_translated_entries(translated, n_entries);

	return item_factory;
}

/* Prevent the user from setting a short-cut on this item */
void menuitem_no_shortcuts(GtkWidget *item)
{
#ifdef GTK2
	/*
	GtkMenuItem *menuitem = GTK_MENU_ITEM(item);

	_gtk_widget_set_accel_path(item, NULL, NULL);
	g_free(menuitem->accel_path);
	menuitem->accel_path = NULL;
	*/
#else
	gtk_widget_lock_accelerators(item);
#endif
}
 
static void items_sensitive(gboolean state)
{
	int	n = 9;
	GList	*items, *item;

	items = item = gtk_container_children(GTK_CONTAINER(filer_file_menu));
	while (item && n--)
	{
		gtk_widget_set_sensitive(GTK_BIN(item->data)->child, state);
		item = item->next;
	}
	g_list_free(items);

#ifdef HAVE_LIBVFS
	items = item = gtk_container_children(GTK_CONTAINER(filer_vfs_menu));
	while (item)
	{
		gtk_widget_set_sensitive(GTK_BIN(item->data)->child, state);
		item = item->next;
	}
	g_list_free(items);
#endif
}

/* 'data' is an array of three ints:
 * [ pointer_x, pointer_y, item_under_pointer ]
 */
void position_menu(GtkMenu *menu, gint *x, gint *y,
#ifdef GTK2
		gboolean  *push_in,
#endif
		gpointer data)
{
	int		*pos = (int *) data;
	GtkRequisition 	requisition;
	GList		*items, *next;
	int		y_shift = 0;
	int		item = pos[2];

	next = items = gtk_container_children(GTK_CONTAINER(menu));

	while (item >= 0 && next)
	{
		int h = ((GtkWidget *) next->data)->requisition.height;

		if (item > 0)
			y_shift += h;
		else
			y_shift += h / 2;

		next = next->next;
		item--;
	}
	
	g_list_free(items);

	gtk_widget_size_request(GTK_WIDGET(menu), &requisition);

	*x = pos[0] - (requisition.width * 7 / 8);
	*y = pos[1] - y_shift;

	*x = CLAMP(*x, 0, screen_width - requisition.width);
	*y = CLAMP(*y, 0, screen_height - requisition.height);

#ifdef GTK2
	*push_in = FALSE;
#endif
}

#if 0
/* Used when you menu-click on the Large or Small toolbar tools */
void show_style_menu(FilerWindow *filer_window,
			GdkEventButton *event,
			GtkWidget *menu)
{
	int		pos[3];

	pos[0] = event->x_root;
	pos[1] = event->y_root;
	pos[2] = 0;

	window_with_focus = filer_window;

	popup_menu = menu;
	
	gtk_menu_popup(GTK_MENU(popup_menu), NULL, NULL, position_menu,
			(gpointer) pos, event->button, event->time);
}
#endif

static GList *menu_from_dir(GtkWidget *menu, const gchar *dname,
			    MenuIconStyle style, CallbackFn func,
			    gboolean separator, gboolean strip_ext)
{
	GList *widgets = NULL;
	DirItem *ditem;
	DIR	*dir;
	struct dirent *ent;
	GtkWidget *item;

	dir = opendir(dname);
	if (!dir)
		goto out;

	if (separator)
	{
		item = gtk_menu_item_new();
		widgets = g_list_append(widgets, item);
		gtk_menu_shell_append(GTK_MENU_SHELL(menu), item);
	}

	while ((ent = readdir(dir)))
	{
		char	*dot, *leaf;
		GtkWidget *hbox;
		GtkWidget *img;
		GtkWidget *label;
		gchar *fname;
		GdkPixmap *icon;
		GdkBitmap *mask;

		/* Ignore hidden files */
		if (ent->d_name[0] == '.')
			continue;

		/* Strip off extension, if any */
		dot = strchr(ent->d_name, '.');
		if (strip_ext && dot)
			leaf = g_strndup(ent->d_name, dot - ent->d_name);
		else
			leaf = g_strdup(ent->d_name);

		fname = g_strconcat(dname, "/", ent->d_name, NULL);
		ditem = diritem_new(NULL);
		diritem_restat(fname, ditem);

		if (ditem->image && style != MIS_NONE)
		{
			switch (style) {
				case MIS_HUGE:
					if (!ditem->image->huge_pixmap)
						pixmap_make_huge(ditem->image);
					icon = ditem->image->huge_pixmap;
					mask = ditem->image->huge_mask;
					break;
				case MIS_LARGE:
					icon = ditem->image->pixmap;
					mask = ditem->image->mask;
					break;

				case MIS_SMALL:
				default:
					if (!ditem->image->sm_pixmap)
						pixmap_make_small(ditem->image);
					icon = ditem->image->sm_pixmap;
					mask = ditem->image->sm_mask;
					break;
			}

			item = gtk_menu_item_new();
			/* TODO: Find a way to allow short-cuts */
			menuitem_no_shortcuts(item);

			hbox = gtk_hbox_new(FALSE, 2);
			gtk_container_add(GTK_CONTAINER(item), hbox);

			img = gtk_pixmap_new(icon, mask);
			gtk_box_pack_start(GTK_BOX(hbox), img, FALSE, FALSE, 2);

			label = gtk_label_new(leaf);
			gtk_misc_set_alignment(GTK_MISC(label), 0, 0.5);
			gtk_box_pack_start(GTK_BOX(hbox), label, TRUE, TRUE, 2);

			diritem_free(ditem);
		}
		else
			item = gtk_menu_item_new_with_label(leaf);

		g_free(leaf);

		gtk_signal_connect_object(GTK_OBJECT(item), "activate",
				GTK_SIGNAL_FUNC(func), (GtkObject *) fname);
		gtk_menu_shell_append(GTK_MENU_SHELL(menu), item);
		gtk_signal_connect_object(GTK_OBJECT(item), "destroy",
				GTK_SIGNAL_FUNC(g_free), (GtkObject *) fname);

		widgets = g_list_append(widgets, item);
	}

	closedir(dir);
out:

	return widgets;
}

/* Scan the templates dir and create entries for the New menu */
static void update_new_files_menu(MenuIconStyle style)
{
	static GList *widgets = NULL;

	gchar	*templ_dname = NULL;

	if (widgets)
	{
		GList	*next;
		
		for (next = widgets; next; next = next->next)
			gtk_widget_destroy((GtkWidget *) next->data);

		g_list_free(widgets);
		widgets = NULL;
	}

	templ_dname = choices_find_path_load("Templates", "");
	if (templ_dname)
	{
		widgets = menu_from_dir(filer_new_menu, templ_dname, style,
					(CallbackFn) new_file_type, TRUE, TRUE);
		g_free(templ_dname);
	}
	gtk_widget_show_all(filer_new_menu);
}

/* 'item' is the number of the item to appear under the pointer. */
void show_popup_menu(GtkWidget *menu, GdkEvent *event, int item)
{
	int		pos[3];
	int		button = 0;
	guint32		time = 0;

	if (event && (event->type == GDK_BUTTON_PRESS ||
			event->type == GDK_BUTTON_RELEASE))
	{
		GdkEventButton *bev = (GdkEventButton *) event;

		pos[0] = bev->x_root;
		pos[1] = bev->y_root;
		button = bev->button;
		time = bev->time;
	}
	else if (event && event->type == GDK_KEY_PRESS)
	{
		GdkEventKey *kev = (GdkEventKey *) event;

		get_pointer_xy(pos, pos + 1);
		time = kev->time;
	}
	else
		get_pointer_xy(pos, pos + 1);

	pos[2] = item;

	gtk_widget_show_all(menu);
	gtk_menu_popup(GTK_MENU(menu), NULL, NULL,
			position_menu, (gpointer) pos, button, time);
	select_nth_item(GTK_MENU_SHELL(menu), item);
}

static MenuIconStyle get_menu_icon_style(void)
{
	MenuIconStyle mis;
	int display;

	mis = option_get_int("menu_iconsize");

	switch (mis)
	{
		case MIS_NONE: case MIS_SMALL: case MIS_LARGE: case MIS_HUGE:
			return mis;
		default:
			break;
	}

	if (mis == MIS_CURRENT && window_with_focus)
	{
		switch (window_with_focus->display_style)
		{
			case HUGE_ICONS:
				return MIS_HUGE;
			case LARGE_ICONS:
				return MIS_LARGE;
			case SMALL_ICONS:
				return MIS_SMALL;
			default:
				break;
		}
	}

	display = option_get_int("display_size");
	switch (display)
	{
		case HUGE_ICONS:
			return MIS_HUGE;
		case LARGE_ICONS:
			return MIS_LARGE;
		case SMALL_ICONS:
			return MIS_SMALL;
		default:
			break;
	}

	return MIS_SMALL;
}

void show_filer_menu(FilerWindow *filer_window, GdkEvent *event, int item)
{
	DirItem		*file_item = NULL;
	GdkModifierType	state = 0;

	updating_menu++;

	/* Remove previous AppMenu, if any */
	appmenu_remove();

	window_with_focus = filer_window;

	if (event->type == GDK_BUTTON_PRESS)
		state = ((GdkEventButton *) event)->state;
	else if (event->type == GDK_KEY_PRESS)
		state = ((GdkEventKey *) event)->state;

	if (filer_window->collection->number_selected == 0 && item >= 0)
	{
		filer_window->temp_item_selected = TRUE;
		collection_select_item(filer_window->collection, item);
	}
	else
	{
		filer_window->temp_item_selected = FALSE;
	}

	/* Short-cut to the Send To menu */
	if (state & GDK_SHIFT_MASK)
	{
		GList *paths;

		updating_menu--;

		if (filer_window->collection->number_selected == 0)
		{
			report_error(
				_("You should Shift+Menu click over a file to "
				"send it somewhere"));
			return;
		}

		paths = filer_selected_items(filer_window);

		show_send_to_menu(paths, event); /* (paths eaten) */

		return;
	}

	{
		GtkWidget	*file_label, *file_menu;
		Collection 	*collection = filer_window->collection;
		GString		*buffer;
		DirItem		*item;

		file_label = filer_file_item;
		file_menu = filer_file_menu;
		gtk_check_menu_item_set_active(
				GTK_CHECK_MENU_ITEM(filer_thumb_menu),
				filer_window->show_thumbs);
		gtk_check_menu_item_set_active(
				GTK_CHECK_MENU_ITEM(filer_hidden_menu),
				filer_window->show_hidden);
		buffer = g_string_new(NULL);

		if (collection->number_selected == 1)
		{
			item = selected_item(filer_window->collection);
			if (!item->image)
				dir_update_item(filer_window->directory,
						item->leafname);
		}
		
		switch (collection->number_selected)
		{
			case 0:
				g_string_assign(buffer, _("Next Click"));
				items_sensitive(TRUE);
				break;
			case 1:
				items_sensitive(TRUE);
				file_item = selected_item(
						filer_window->collection);
				g_string_sprintf(buffer, "%s '%s'",
						basetype_name(file_item),
						file_item->leafname);
				if (!can_set_run_action(file_item))
					menu_set_items_shaded(filer_file_menu,
							TRUE, 6, 1);
				break;
			default:
				items_sensitive(FALSE);
				g_string_sprintf(buffer, _("%d items"),
						collection->number_selected);
				break;
		}
		gtk_label_set_text(GTK_LABEL(file_label), buffer->str);
		g_string_free(buffer, TRUE);

		menu_show_shift_action(file_shift_item, file_item,
				collection->number_selected == 0);
		if (file_item)
			appmenu_add(make_path(filer_window->path,
						file_item->leafname)->str,
					file_item, filer_file_menu);
	}

	update_new_files_menu(get_menu_icon_style());

	gtk_widget_set_sensitive(filer_new_window, !o_unique_filer_windows);

	popup_menu = (state & GDK_CONTROL_MASK)
				? filer_file_menu
				: filer_menu;

	updating_menu--;
	
	show_popup_menu(popup_menu, event,
			popup_menu == filer_file_menu ? 5 : 1);
}

static void menu_closed(GtkWidget *widget)
{
	if (window_with_focus == NULL || widget != popup_menu)
		return;			/* Close panel item chosen? */

	popup_menu = NULL;

	if (window_with_focus->temp_item_selected)
	{
		collection_clear_selection(window_with_focus->collection);
		window_with_focus->temp_item_selected = FALSE;
	}
}

void target_callback(FilerWindow *filer_window,
			gint item,
			gpointer action)
{
	Collection	*collection;

	g_return_if_fail(filer_window != NULL);

	collection = filer_window->collection;

	window_with_focus = filer_window;
	
	/* Don't grab the primary selection */
	filer_window->temp_item_selected = TRUE;
	
	collection_wink_item(collection, item);
	collection_clear_except(collection, item);
	file_op(NULL, GPOINTER_TO_INT(action), GTK_WIDGET(collection));

	if (item < collection->number_of_items)
		collection_unselect_item(collection, item);
	filer_window->temp_item_selected = FALSE;
}

/* Set the text of the 'Shift Open...' menu item.
 * If icon is NULL, reset the text and also shade it, unless 'next'.
 */
void menu_show_shift_action(GtkWidget *menu_item, DirItem *item, gboolean next)
{
	guchar		*shift_action = NULL;

	if (item)
	{
		if (item->flags & ITEM_FLAG_MOUNT_POINT)
		{
			if (item->flags & ITEM_FLAG_MOUNTED)
				shift_action = N_("Unmount");
			else
				shift_action = N_("Mount");
		}
		else if (item->flags & ITEM_FLAG_SYMLINK)
			shift_action = N_("Show Target");
		else if (item->base_type == TYPE_DIRECTORY)
			shift_action = N_("Look Inside");
		else if (item->base_type == TYPE_FILE)
			shift_action = N_("Open As Text");
	}
	gtk_label_set_text(GTK_LABEL(menu_item),
			shift_action ? _(shift_action)
				     : _("Shift Open"));
	gtk_widget_set_sensitive(menu_item, shift_action != NULL || next);
}

/* Actions */

static void huge_with(gpointer data, guint action, GtkWidget *widget)
{
	display_set_layout(window_with_focus, HUGE_ICONS, action);
}

static void large_with(gpointer data, guint action, GtkWidget *widget)
{
	display_set_layout(window_with_focus, LARGE_ICONS, action);
}

static void small_with(gpointer data, guint action, GtkWidget *widget)
{
	display_set_layout(window_with_focus, SMALL_ICONS, action);
}

static void sort_name(gpointer data, guint action, GtkWidget *widget)
{
	g_return_if_fail(window_with_focus != NULL);

	display_set_sort_fn(window_with_focus, sort_by_name);
}

static void sort_type(gpointer data, guint action, GtkWidget *widget)
{
	g_return_if_fail(window_with_focus != NULL);

	display_set_sort_fn(window_with_focus, sort_by_type);
}

static void sort_date(gpointer data, guint action, GtkWidget *widget)
{
	g_return_if_fail(window_with_focus != NULL);

	display_set_sort_fn(window_with_focus, sort_by_date);
}

static void sort_size(gpointer data, guint action, GtkWidget *widget)
{
	g_return_if_fail(window_with_focus != NULL);

	display_set_sort_fn(window_with_focus, sort_by_size);
}

static void hidden(gpointer data, guint action, GtkWidget *widget)
{
	if (updating_menu)
		return;

	g_return_if_fail(window_with_focus != NULL);

	display_set_hidden(window_with_focus, !window_with_focus->show_hidden);
}

static void show_thumbs(gpointer data, guint action, GtkWidget *widget)
{
	if (updating_menu)
		return;

	g_return_if_fail(window_with_focus != NULL);

	display_set_thumbs(window_with_focus, !window_with_focus->show_thumbs);
}

static void refresh(gpointer data, guint action, GtkWidget *widget)
{
	g_return_if_fail(window_with_focus != NULL);

	full_refresh();
	filer_update_dir(window_with_focus, TRUE);
}

static void delete(FilerWindow *filer_window)
{
	GList *paths;
	paths = filer_selected_items(filer_window);
	action_delete(paths);
	g_list_foreach(paths, (GFunc) g_free, NULL);
	g_list_free(paths);
}

static void usage(FilerWindow *filer_window)
{
	GList *paths;
	paths = filer_selected_items(filer_window);
	action_usage(paths);
	g_list_foreach(paths, (GFunc) g_free, NULL);
	g_list_free(paths);
}

static void chmod_items(FilerWindow *filer_window)
{
	GList *paths;
	paths = filer_selected_items(filer_window);
	action_chmod(paths, FALSE, NULL);
	g_list_foreach(paths, (GFunc) g_free, NULL);
	g_list_free(paths);
}

static void find(FilerWindow *filer_window)
{
	GList *paths;
	paths = filer_selected_items(filer_window);
	action_find(paths);
	g_list_foreach(paths, (GFunc) g_free, NULL);
	g_list_free(paths);
}

/* This pops up our savebox widget, cancelling any currently open one,
 * and allows the user to pick a new path for it.
 * Once the new path has been picked, the callback will be called with
 * both the current and new paths.
 * NOTE: This function unrefs 'image'!
 */
static void savebox_show(guchar *title, guchar *path, MaskedPixmap *image,
		gboolean (*callback)(guchar *current, guchar *new))
{
	g_return_if_fail(image != NULL);
	
	if (GTK_WIDGET_VISIBLE(savebox))
		gtk_widget_hide(savebox);

	if (callback == link_cb)
		gtk_widget_show(check_relative);
	else
		gtk_widget_hide(check_relative);

	if (current_path)
		g_free(current_path);
	current_path = g_strdup(path);
	current_savebox_callback = callback;

	gtk_window_set_title(GTK_WINDOW(savebox), title);
	gtk_savebox_set_pathname(GTK_SAVEBOX(savebox), current_path);
	gtk_savebox_set_icon(GTK_SAVEBOX(savebox), image->pixmap, image->mask);
	pixmap_unref(image);
				
	gtk_widget_show(savebox);
}

static gint save_to_file(GtkSavebox *savebox, guchar *pathname)
{
	g_return_val_if_fail(current_savebox_callback != NULL,
			GTK_XDS_SAVE_ERROR);

	return current_savebox_callback(current_path, pathname)
			? GTK_XDS_SAVED : GTK_XDS_SAVE_ERROR;
}

static gboolean copy_cb(guchar *current, guchar *new)
{
	return action_with_leaf(action_copy, current, new);
}

static gboolean action_with_leaf(ActionFn action, guchar *current, guchar *new)
{
	char	*new_dir, *leaf;
	GList	*local_paths;

	if (new[0] != '/')
	{
		report_error(_("New pathname is not absolute"));
		return FALSE;
	}

	if (new[strlen(new) - 1] == '/')
	{
		new_dir = g_strdup(new);
		leaf = NULL;
	}
	else
	{
		guchar *slash;
		
		slash = strrchr(new, '/');
		new_dir = g_strndup(new, slash - new);
		leaf = slash + 1;
	}

	local_paths = g_list_append(NULL, current);
	action(local_paths, new_dir, leaf, -1);
	g_list_free(local_paths);

	g_free(new_dir);

	return TRUE;
}

/* Open a savebox to act on the selected file.
 * Call 'callback' later to perform the operation.
 */
static void src_dest_action_item(guchar *path, MaskedPixmap *image,
				 guchar *title,
				 gboolean (*callback)(guchar *, guchar *))
{
	pixmap_ref(image);
	savebox_show(title, path, image, callback);
}

static gboolean rename_cb(guchar *current, guchar *new)
{
	return action_with_leaf(action_move, current, new);
}

static gboolean link_cb(guchar *initial, guchar *path)
{
	int	err;

	if (gtk_toggle_button_get_active(GTK_TOGGLE_BUTTON(check_relative)))
	{
		guchar *rpath;
		
		rpath = get_relative_path(path, initial);
		err = symlink(rpath, path);
		
		g_free(rpath);
	}
	else
		err = symlink(initial, path);
			
	if (err)
	{
		report_error("symlink: %s", g_strerror(errno));
		return FALSE;
	}

	dir_check_this(path);

	return TRUE;
}

static void run_action(DirItem *item)
{
	if (can_set_run_action(item))
		type_set_handler_dialog(item->mime_type);
	else
		report_error(
			_("You can only set the run action for a "
			"regular file"));
}

void open_home(gpointer data, guint action, GtkWidget *widget)
{
	filer_opendir(home_dir, NULL);
}

#ifdef HAVE_LIBVFS
static void real_vfs_open(FilerWindow *filer_window, DirItem *item, char *fs)
{
	gchar		*path;

	path = g_strconcat(filer_window->path,
			"/",
			item->leafname,
			"#", fs, NULL);

	filer_change_to(filer_window, path, NULL);
	g_free(path);
}
#else
static void open_vfs_avfs(FilerWindow *filer_window, DirItem *item)
{
	gchar		*path;

	path = g_strconcat(filer_window->path,
			"/", item->leafname, "#", NULL);

	filer_change_to(filer_window, path, NULL);
	g_free(path);
}
#endif

static void select_all(gpointer data, guint action, GtkWidget *widget)
{
	g_return_if_fail(window_with_focus != NULL);

	window_with_focus->temp_item_selected = FALSE;
	collection_select_all(window_with_focus->collection);
}

static void clear_selection(gpointer data, guint action, GtkWidget *widget)
{
	g_return_if_fail(window_with_focus != NULL);

	window_with_focus->temp_item_selected = FALSE;
	collection_clear_selection(window_with_focus->collection);
}

static void invert_selection(gpointer data, guint action, GtkWidget *widget)
{
	g_return_if_fail(window_with_focus != NULL);

	window_with_focus->temp_item_selected = FALSE;
	collection_invert_selection(window_with_focus->collection);
}

void menu_show_options(gpointer data, guint action, GtkWidget *widget)
{
	options_show();
}

static gboolean new_directory_cb(guchar *initial, guchar *path)
{
	if (mkdir(path, S_IRWXU | S_IRWXG | S_IRWXO))
	{
		report_error("mkdir: %s", g_strerror(errno));
		return FALSE;
	}

	dir_check_this(path);

	if (filer_exists(window_with_focus))
	{
		guchar	*leaf;
		leaf = strrchr(path, '/');
		if (leaf)
			display_set_autoselect(window_with_focus, leaf + 1);
	}
	
	return TRUE;
}

static void new_directory(gpointer data, guint action, GtkWidget *widget)
{
	g_return_if_fail(window_with_focus != NULL);

	savebox_show(_("New Directory"),
			make_path(window_with_focus->path, _("NewDir"))->str,
			type_to_icon(special_directory),
			new_directory_cb);
}

static gboolean new_file_cb(guchar *initial, guchar *path)
{
	int fd;

	fd = open(path, O_CREAT | O_EXCL, 0666);

	if (fd == -1)
	{
		report_error(_("Error creating '%s': %s"),
				path, g_strerror(errno));
		return FALSE;
	}

	if (close(fd))
		report_error(_("Error creating '%s': %s"),
				path, g_strerror(errno));

	dir_check_this(path);

	if (filer_exists(window_with_focus))
	{
		guchar	*leaf;
		leaf = strrchr(path, '/');
		if (leaf)
			display_set_autoselect(window_with_focus, leaf + 1);
	}

	return TRUE;
}

static void new_file(gpointer data, guint action, GtkWidget *widget)
{
	g_return_if_fail(window_with_focus != NULL);
	
	savebox_show(_("New File"),
			make_path(window_with_focus->path, _("NewFile"))->str,
			type_to_icon(text_plain),
			new_file_cb);
}

static gboolean new_file_type_cb(guchar *initial, guchar *path)
{
	gchar *templ, *templ_dname, *oleaf, *dest, *leaf;
	GList *paths;

	/* We can work out the template path from the initial name */
	oleaf = g_basename(initial);
	templ_dname = choices_find_path_load("Templates", "");
	if (!templ_dname)
	{
		report_error(
		_("Error creating file: could not find the template for %s"),
				oleaf);
		return FALSE;
	}

	templ = g_strconcat(templ_dname, "/", oleaf, NULL);
	g_free(templ_dname);

	dest = g_dirname(path);
	leaf = g_basename(path);
	paths = g_list_append(NULL, templ);

	action_copy(paths, dest, leaf, -1);

	g_list_free(paths);
	g_free(dest);
	g_free(templ);

	if (filer_exists(window_with_focus))
		display_set_autoselect(window_with_focus, leaf);

	return TRUE;
}

static void do_send_to(gchar *templ)
{
	g_return_if_fail(send_to_paths != NULL);

	run_with_files(templ, send_to_paths);
}

static void new_file_type(gchar *templ)
{
	gchar *leaf;
	MIME_type *type;

	g_return_if_fail(window_with_focus != NULL);
	
	leaf = g_basename(templ);
	type = type_get_type(templ);

	savebox_show(_("New File"),
			make_path(window_with_focus->path, leaf)->str,
			type_to_icon(type),
			new_file_type_cb);
}

static void customise_send_to(gpointer data)
{
	GPtrArray	*path;
	guchar		*save;
	GString		*dirs;
	int		i;

	dirs = g_string_new(NULL);

	path = choices_list_dirs("");
	for (i = 0; i < path->len; i++)
	{
		guchar *old = (guchar *) path->pdata[i];

		g_string_append(dirs, old);
		g_string_append(dirs, "SendTo\n");
	}
	choices_free_list(path);

	save = choices_find_path_save("", "SendTo", TRUE);
	if (save)
		mkdir(save, 0777);

	report_error(
		_("The `Send To' menu provides a quick way to send some files "
		"to an application. The applications listed are those in "
		"the following directories:\n\n%s\n%s\n"
		"The `Send To' menu may be opened by Shift+Menu clicking "
		"over a file."),
		dirs->str,
		save ? _("I'll show you your SendTo directory now; you should "
			"symlink (Ctrl+Shift drag) any applications you want "
			"into it.")
		     : _("Your CHOICESPATH variable setting prevents "
			 "customisations - sorry."));

	g_string_free(dirs, TRUE);
	
	if (save)
		filer_opendir(save, NULL);
}

/* Scan the SendTo dir and create and show the Send To menu.
 * The 'paths' list and every path in it is claimed, and will be
 * freed later -- don't free it yourself!
 */
static void show_send_to_menu(GList *paths, GdkEvent *event)
{
	GtkWidget	*menu, *item;
	GPtrArray	*path;
	int		i;

	menu = gtk_menu_new();

	path = choices_list_dirs("SendTo");

	for (i = 0; i < path->len; i++)
	{
		GList	*widgets = NULL;
		guchar	*dir = (guchar *) path->pdata[i];

		widgets = menu_from_dir(menu, dir, get_menu_icon_style(),
					(CallbackFn) do_send_to,
					FALSE, FALSE);

		if (widgets)
			gtk_menu_shell_append(GTK_MENU_SHELL(menu),
					gtk_menu_item_new());

		g_list_free(widgets);	/* TODO: Get rid of this */
	}

	choices_free_list(path);

	item = gtk_menu_item_new_with_label(_("Customise"));
	gtk_signal_connect_object(GTK_OBJECT(item), "activate",
				GTK_SIGNAL_FUNC(customise_send_to), NULL);
	gtk_menu_shell_append(GTK_MENU_SHELL(menu), item);

	if (send_to_paths)
	{
		g_list_foreach(send_to_paths, (GFunc) g_free, NULL);
		g_list_free(send_to_paths);
	}
	send_to_paths = paths;

	gtk_signal_connect(GTK_OBJECT(menu), "unmap_event",
			GTK_SIGNAL_FUNC(menu_closed), NULL);

	popup_menu = menu;
	show_popup_menu(menu, event, 0);
}

static void send_to(FilerWindow *filer_window)
{
	GList		*paths;
	GdkEvent	*event;

	paths = filer_selected_items(filer_window);
	event = gtk_get_current_event();

	/* Eats paths */
	show_send_to_menu(paths, event);

	gdk_event_free(event);
}

static void xterm_here(gpointer data, guint action, GtkWidget *widget)
{
	char	*argv[] = {"sh", "-c", NULL, NULL};

	argv[2] = option_get_static_string("menu_xterm");

	g_return_if_fail(window_with_focus != NULL);

	rox_spawn(window_with_focus->path, argv);
}

static void home_directory(gpointer data, guint action, GtkWidget *widget)
{
	g_return_if_fail(window_with_focus != NULL);

	filer_change_to(window_with_focus, home_dir, NULL);
}

static void open_parent(gpointer data, guint action, GtkWidget *widget)
{
	g_return_if_fail(window_with_focus != NULL);

	filer_open_parent(window_with_focus);
}

static void open_parent_same(gpointer data, guint action, GtkWidget *widget)
{
	g_return_if_fail(window_with_focus != NULL);

	change_to_parent(window_with_focus);
}

static void resize(gpointer data, guint action, GtkWidget *widget)
{
	g_return_if_fail(window_with_focus != NULL);

	filer_window_autosize(window_with_focus, TRUE);
}

static void new_window(gpointer data, guint action, GtkWidget *widget)
{
	g_return_if_fail(window_with_focus != NULL);

	if (o_unique_filer_windows)
	{
		report_error(_("You can't open a second view onto "
			"this directory because the `Unique Windows' option "
			"is turned on in the Options window."));
	}
	else
		filer_opendir(window_with_focus->path, window_with_focus);
}

#if 0
static void su_to_user(GtkWidget *dialog)
{
	char		*argv[] = {
		"xterm", "-e", "su_rox", "USER", "APP_RUN", "DIR", NULL};
	GtkEntry	*user;
	guchar  	*path;
	
	g_return_if_fail(dialog != NULL);

	path = gtk_object_get_data(GTK_OBJECT(dialog), "dir_path");
	user = gtk_object_get_data(GTK_OBJECT(dialog), "user_name");

	g_return_if_fail(user != NULL && path != NULL);

	argv[2] = g_strconcat(app_dir, "/su_rox", NULL);
	argv[3] = gtk_entry_get_text(user);
	argv[4] = g_strconcat(app_dir, "/AppRun", NULL);
	argv[5] = path;

	if (!spawn(argv))
		report_error(_("fork: %s"), g_strerror(errno));

	g_free(argv[2]);
	g_free(argv[4]);

	gtk_widget_destroy(dialog);
}

static void new_user(gpointer data, guint action, GtkWidget *widget)
{
	GtkWidget	*dialog, *vbox, *hbox, *entry, *button;
	
	g_return_if_fail(window_with_focus != NULL);

	dialog = gtk_window_new(GTK_WINDOW_DIALOG);
	gtk_window_set_title(GTK_WINDOW(dialog), _("New window, as user..."));
	gtk_container_set_border_width(GTK_CONTAINER(dialog), 4);
	gtk_object_set_data_full(GTK_OBJECT(dialog), "dir_path",
			g_strdup(window_with_focus->path), g_free);
	
	vbox = gtk_vbox_new(FALSE, 4);
	gtk_container_add(GTK_CONTAINER(dialog), vbox);
	gtk_box_pack_start(GTK_BOX(vbox),
			gtk_label_new(_("Browse as which user?")),
			TRUE, TRUE, 2);
	
	hbox = gtk_hbox_new(FALSE, 4);
	gtk_box_pack_start(GTK_BOX(vbox), hbox, FALSE, FALSE, 2);

	gtk_box_pack_start(GTK_BOX(hbox),
			gtk_label_new(_("User:")), FALSE, TRUE, 2);

	entry = gtk_entry_new();
	gtk_entry_set_text(GTK_ENTRY(entry), "root");
	gtk_editable_select_region(GTK_EDITABLE(entry), 0, -1);
	gtk_box_pack_start(GTK_BOX(hbox), entry, TRUE, TRUE, 2);
	gtk_widget_grab_focus(entry);
	gtk_object_set_data(GTK_OBJECT(dialog), "user_name", entry);
	gtk_signal_connect_object(GTK_OBJECT(entry), "activate",
			GTK_SIGNAL_FUNC(su_to_user), GTK_OBJECT(dialog));

	hbox = gtk_hbox_new(TRUE, 0);
	gtk_box_pack_start(GTK_BOX(vbox), hbox, FALSE, FALSE, 2);

	button = gtk_button_new_with_label(_("OK"));
	gtk_box_pack_start(GTK_BOX(hbox), button, TRUE, TRUE, 0);
	GTK_WIDGET_SET_FLAGS(button, GTK_CAN_DEFAULT);
	gtk_window_set_default(GTK_WINDOW(dialog), button);
	gtk_signal_connect_object(GTK_OBJECT(button), "clicked",
		GTK_SIGNAL_FUNC(su_to_user), GTK_OBJECT(dialog));
	
	button = gtk_button_new_with_label(_("Cancel"));
	GTK_WIDGET_SET_FLAGS(button, GTK_CAN_DEFAULT);
	gtk_box_pack_start(GTK_BOX(hbox), button, TRUE, TRUE, 0);
	gtk_signal_connect_object(GTK_OBJECT(button), "clicked",
			GTK_SIGNAL_FUNC(gtk_widget_destroy),
			GTK_OBJECT(dialog));

	gtk_widget_show_all(dialog);
}
#endif

static void close_window(gpointer data, guint action, GtkWidget *widget)
{
	g_return_if_fail(window_with_focus != NULL);

	gtk_widget_destroy(window_with_focus->window);
}

static void mini_buffer(gpointer data, guint action, GtkWidget *widget)
{
	MiniType type = (MiniType) action;

	g_return_if_fail(window_with_focus != NULL);

	/* Item needs to remain selected... */
	if (type == MINI_SHELL)
		window_with_focus->temp_item_selected = FALSE;

	minibuffer_show(window_with_focus, type);
}

void menu_rox_help(gpointer data, guint action, GtkWidget *widget)
{
	filer_opendir(make_path(app_dir, "Help")->str, NULL);
}

/* Set n items from position 'from' in 'menu' to the 'shaded' state */
void menu_set_items_shaded(GtkWidget *menu, gboolean shaded, int from, int n)
{
	GList	*items, *item;

	items = gtk_container_children(GTK_CONTAINER(menu));

	item = g_list_nth(items, from);
	while (item && n--)
	{
		gtk_widget_set_sensitive(GTK_BIN(item->data)->child, !shaded);
		item = item->next;
	}
	g_list_free(items);
}

#ifndef GTK2
/* This is called for every modified menu entry. We just use it to
 * find out if the menu has changed at all.
 */
static void set_mod(gboolean *mod, guchar *str)
{
	if (str && str[0] == '(')
		*mod = TRUE;
}
#endif

static void save_menus(void)
{
	char	*menurc;

#ifdef GTK2
	menurc = choices_find_path_save(MENUS_NAME, PROJECT, TRUE);
	if (menurc)
	{
		gtk_accel_map_save(menurc);
		g_free(menurc);
	}
#else
	menurc = choices_find_path_save(MENUS_NAME, PROJECT, FALSE);
	if (menurc)
	{
		gboolean	mod = FALSE;

		g_free(menurc);

		/* Find out if anything changed... */
		gtk_item_factory_dump_items(NULL, TRUE,
				(GtkPrintFunc) set_mod, &mod);

		/* Dump out if so... */
		if (mod)
		{
			menurc = choices_find_path_save(MENUS_NAME,
							PROJECT, TRUE);
			g_return_if_fail(menurc != NULL);
			mark_menus_modified(TRUE);
			gtk_item_factory_dump_rc(menurc, NULL, TRUE);
			mark_menus_modified(FALSE);
			g_free(menurc);
		}
	}
#endif
}

#ifdef GTK2
static void keys_changed(gpointer data)
{
	save_menus();
}
#else
static void mark_modified(gpointer hash_key,
			  gpointer value,
			  gpointer user_data)
{
	GtkItemFactoryItem *item = (GtkItemFactoryItem *) value;

	item->modified = (gboolean) GPOINTER_TO_INT(user_data);
}

/* Set or clear the 'modified' flag in all menu items. Messy... */
static void mark_menus_modified(gboolean mod)
{
	GtkItemFactoryClass	*class;

	class = gtk_type_class(GTK_TYPE_ITEM_FACTORY);

	g_hash_table_foreach(class->item_ht, mark_modified,
			GINT_TO_POINTER(mod));
}
#endif

static void select_nth_item(GtkMenuShell *shell, int n)
{
	GList	  *items, *nth;
	GtkWidget *item = NULL;

	items = gtk_container_children(GTK_CONTAINER(shell));
	nth = g_list_nth(items, n);

	g_return_if_fail(nth != NULL);

	item = (GtkWidget *) (nth->data);
	g_list_free(items);

	gtk_menu_shell_select_item(shell, item);
}

static void file_op(gpointer data, FileOp action, GtkWidget *widget)
{
	Collection *collection;
	DirItem	*item;
	gchar	*path;

	g_return_if_fail(window_with_focus != NULL);

	collection = window_with_focus->collection;

	if (collection->number_selected < 1)
	{
		char *prompt;

		switch (action)
		{
			case FILE_COPY_ITEM:
				prompt = _("Copy ... ?");
				break;
			case FILE_RENAME_ITEM:
				prompt = _("Rename ... ?");
				break;
			case FILE_LINK_ITEM:
				prompt = _("Symlink ... ?");
				break;
			case FILE_OPEN_FILE:
				prompt = _("Shift Open ... ?");
				break;
			case FILE_HELP:
				prompt = _("Help about ... ?");
				break;
			case FILE_SHOW_FILE_INFO:
				prompt = _("Examine ... ?");
				break;
			case FILE_RUN_ACTION:
				prompt = _("Set run action for ... ?");
				break;
			case FILE_SET_ICON:
				prompt = _("Set icon for ... ?");
				break;
			case FILE_SEND_TO:
				prompt = _("Send ... to ... ?");
				break;
			case FILE_DELETE:
				prompt = _("DELETE ... ?");
				break;
			case FILE_USAGE:
				prompt = _("Count the size of ... ?");
				break;
			case FILE_CHMOD_ITEMS:
				prompt = _("Set permissions on ... ?");
				break;
			case FILE_FIND:
				prompt = _("Search inside ... ?");
				break;
#ifdef HAVE_LIBVFS
			case FILE_OPEN_VFS_RPM:
			case FILE_OPEN_VFS_UTAR:
			case FILE_OPEN_VFS_UZIP:
			case FILE_OPEN_VFS_DEB:
#else
			case FILE_OPEN_VFS_AVFS:
#endif
				prompt = _("Look inside ... ?");
				break;
			default:
				g_warning("Unknown action!");
				return;
		}
		filer_target_mode(window_with_focus, target_callback,
					GINT_TO_POINTER(action), prompt);
		return;
	}

	switch (action)
	{
		case FILE_SEND_TO:
			send_to(window_with_focus);
			return;
		case FILE_DELETE:
			delete(window_with_focus);
			return;
		case FILE_USAGE:
			usage(window_with_focus);
			return;
		case FILE_CHMOD_ITEMS:
			chmod_items(window_with_focus);
			return;
		case FILE_FIND:
			find(window_with_focus);
			return;
		default:
			break;
	}

	/* All the following actions require exactly one file selected */

	if (collection->number_selected > 1)
	{
		report_error(_("You cannot do this to more than "
				"one item at a time"));
		return;
	}

	item = selected_item(collection);
	g_return_if_fail(item != NULL);
	if (!item->image)
		item = dir_update_item(window_with_focus->directory,
					item->leafname);

	if (!item)
	{
		report_error(_("Item no longer exists!"));
		return;
	}

	path = make_path(window_with_focus->path, item->leafname)->str;

	switch (action)
	{
		case FILE_COPY_ITEM:
			src_dest_action_item(path, item->image,
						_("Copy"), copy_cb);
			break;
		case FILE_RENAME_ITEM:
			src_dest_action_item(path, item->image,
					_("Rename"), rename_cb);
			break;
		case FILE_LINK_ITEM:
			src_dest_action_item(path, item->image,
					_("Symlink"), link_cb);
			break;
		case FILE_OPEN_FILE:
			filer_openitem(window_with_focus,
					selected_item_number(collection),
					OPEN_SAME_WINDOW | OPEN_SHIFT);
			break;
		case FILE_HELP:
			show_item_help(path, item);
			break;
		case FILE_SHOW_FILE_INFO:
			infobox_new(path);
			break;
		case FILE_RUN_ACTION:
			run_action(item);
			break;
		case FILE_SET_ICON:
			icon_set_handler_dialog(item, path);
			break;
#ifdef HAVE_LIBVFS
		case FILE_OPEN_VFS_RPM:
			real_vfs_open(window_with_focus, item, "rpm");
			break;
		case FILE_OPEN_VFS_UTAR:
			real_vfs_open(window_with_focus, item, "utar");
			break;
		case FILE_OPEN_VFS_UZIP:
			real_vfs_open(window_with_focus, item, "uzip");
			break;
		case FILE_OPEN_VFS_DEB:
			real_vfs_open(window_with_focus, item, "deb");
			break;
#else
		case FILE_OPEN_VFS_AVFS:
			open_vfs_avfs(window_with_focus, item);
			break;
#endif
		default:
			g_warning("Unknown action!");
			return;
	}
}

