/*
 * $Id$
 *
 * ROX-Filer, filer for the ROX desktop project
 * Copyright (C) 2003, the ROX-Filer team.
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

/* menu.c - code for handling the popup menus */

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
#include "view_iface.h"
#include "display.h"
#include "bookmarks.h"

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
	FILE_OPEN_VFS_AVFS,
} FileOp;

typedef enum menu_icon_style {
  MIS_NONE, MIS_SMALL, MIS_LARGE,
  MIS_HUGE_UNUSED,
  MIS_CURRENT, /* As per current filer window */
  MIS_DEFAULT
} MenuIconStyle;

typedef void (*ActionFn)(GList *paths,
			 const char *dest_dir, const char *leaf, int quiet);
typedef void MenuCallback(GtkWidget *widget, gpointer data);

typedef gboolean (*SaveCb)(GObject *savebox,
			   const gchar *current, const gchar *new);

GtkAccelGroup	*filer_keys = NULL;
static gboolean filer_keys_need_init = TRUE;

static GtkWidget *popup_menu = NULL;	/* Currently open menu */

static gint updating_menu = 0;		/* Non-zero => ignore activations */
static GList *send_to_paths = NULL;

static Option o_menu_iconsize, o_menu_xterm;

/* Static prototypes */

static void save_menus(void);
static void menu_closed(GtkWidget *widget);
static void shade_file_menu_items(gboolean shaded);
static void savebox_show(const gchar *action, const gchar *path,
			 MaskedPixmap *image, SaveCb callback,
			 GdkDragAction dnd_action);
static gint save_to_file(GObject *savebox,
			 const gchar *pathname, gpointer data);
static gboolean action_with_leaf(ActionFn action,
				 const gchar *current, const gchar *new);
static gboolean link_cb(GObject *savebox,
			const gchar *initial, const gchar *path);
static void select_nth_item(GtkMenuShell *shell, int n);
static void new_file_type(gchar *templ);
static void do_send_to(gchar *templ);
static void show_send_to_menu(GList *paths, GdkEvent *event);
static GList *set_keys_button(Option *option, xmlNode *node, guchar *label);

/* Note that for most of these callbacks none of the arguments are used. */

static void view_type(gpointer data, guint action, GtkWidget *widget);

/* (action used in these three - DetailsType) */
static void change_size(gpointer data, guint action, GtkWidget *widget);
static void change_size_auto(gpointer data, guint action, GtkWidget *widget);
static void set_with(gpointer data, guint action, GtkWidget *widget);

static void set_sort(gpointer data, guint action, GtkWidget *widget);
static void reverse_sort(gpointer data, guint action, GtkWidget *widget);

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
static void show_bookmarks(gpointer data, guint action, GtkWidget *widget);
static void new_window(gpointer data, guint action, GtkWidget *widget);
/* static void new_user(gpointer data, guint action, GtkWidget *widget); */
static void close_window(gpointer data, guint action, GtkWidget *widget);
static void follow_symlinks(gpointer data, guint action, GtkWidget *widget);

/* (action used in this - MiniType) */
static void mini_buffer(gpointer data, guint action, GtkWidget *widget);
static void resize(gpointer data, guint action, GtkWidget *widget);

#define MENUS_NAME "menus2"

static GtkWidget	*filer_menu;		/* The popup filer menu */
static GtkWidget	*filer_file_item;	/* The File '' label */
static GtkWidget	*filer_file_menu;	/* The File '' menu */
static GtkWidget	*file_shift_item;	/* Shift Open label */
static GtkWidget	*filer_auto_size_menu;	/* The Automatic item */
static GtkWidget	*filer_hidden_menu;	/* The Show Hidden item */
static GtkWidget	*filer_reverse_menu;	/* The Reversed item */
static GtkWidget	*filer_thumb_menu;	/* The Show Thumbs item */
static GtkWidget	*filer_new_window;	/* The New Window item */
static GtkWidget        *filer_new_menu;        /* The New submenu */
static GtkWidget        *filer_follow_sym;      /* Follow symbolic links item */

#undef N_
#define N_(x) x

static GtkItemFactoryEntry filer_menu_def[] = {
{N_("Display"),			NULL, NULL, 0, "<Branch>"},
{">" N_("Icons View"),   	NULL, view_type, VIEW_TYPE_COLLECTION, NULL},
{">" N_("Icons, With..."),	NULL, NULL, 0, "<Branch>"},
{">>" N_("Sizes"),		NULL, set_with, DETAILS_SIZE, NULL},
{">>" N_("Permissions"),	NULL, set_with, DETAILS_PERMISSIONS, NULL},
{">>" N_("Types"),		NULL, set_with, DETAILS_TYPE, NULL},
{">>" N_("Times"),		NULL, set_with, DETAILS_TIMES, NULL},
{">" N_("List View"),   	NULL, view_type, VIEW_TYPE_DETAILS, "<StockItem>", ROX_STOCK_SHOW_DETAILS},
{">",				NULL, NULL, 0, "<Separator>"},
{">" N_("Bigger Icons"),   	NULL, change_size, 1, "<StockItem>", GTK_STOCK_ZOOM_IN},
{">" N_("Smaller Icons"),   	NULL, change_size, -1, "<StockItem>", GTK_STOCK_ZOOM_OUT},
{">" N_("Automatic"),   	NULL, change_size_auto, 0, "<ToggleItem>"},
{">",				NULL, NULL, 0, "<Separator>"},
{">" N_("Sort by Name"),	NULL, set_sort, SORT_NAME, NULL},
{">" N_("Sort by Type"),	NULL, set_sort, SORT_TYPE, NULL},
{">" N_("Sort by Date"),	NULL, set_sort, SORT_DATE, NULL},
{">" N_("Sort by Size"),	NULL, set_sort, SORT_SIZE, NULL},
{">" N_("Sort by Owner"),	NULL, set_sort, SORT_OWNER, NULL},
{">" N_("Sort by Group"),	NULL, set_sort, SORT_GROUP, NULL},
{">" N_("Reversed"),		NULL, reverse_sort, 0, "<ToggleItem>"},
{">",				NULL, NULL, 0, "<Separator>"},
{">" N_("Show Hidden"),   	NULL, hidden, 0, "<ToggleItem>"},
{">" N_("Show Thumbnails"),	NULL, show_thumbs, 0, "<ToggleItem>"},
{">" N_("Refresh"),		NULL, refresh, 0, "<StockItem>", GTK_STOCK_REFRESH},
{N_("File"),			NULL, NULL, 0, "<Branch>"},
{">" N_("Copy..."),		NULL, file_op, FILE_COPY_ITEM, "<StockItem>", GTK_STOCK_COPY},
{">" N_("Rename..."),		NULL, file_op, FILE_RENAME_ITEM, NULL},
{">" N_("Link..."),		NULL, file_op, FILE_LINK_ITEM, NULL},
{">" N_("Delete"),	    	NULL, file_op, FILE_DELETE, "<StockItem>", GTK_STOCK_DELETE},
{">",				NULL, NULL, 0, "<Separator>"},
{">" N_("Help"),		NULL, file_op, FILE_HELP, "<StockItem>", GTK_STOCK_HELP},
{">" N_("Shift Open"),   	NULL, file_op, FILE_OPEN_FILE},
{">" N_("Open AVFS"),		NULL, file_op, FILE_OPEN_VFS_AVFS, "<StockItem>", GTK_STOCK_OPEN},
{">" N_("Send To..."),		NULL, file_op, FILE_SEND_TO, NULL},
{">",				NULL, NULL, 0, "<Separator>"},
{">" N_("Set Run Action..."),	NULL, file_op, FILE_RUN_ACTION, "<StockItem>", GTK_STOCK_EXECUTE},
{">" N_("Set Icon..."),		NULL, file_op, FILE_SET_ICON, NULL},
{">" N_("Info"),		NULL, file_op, FILE_SHOW_FILE_INFO, "<StockItem>", GTK_STOCK_DIALOG_INFO},
{">" N_("Count"),		NULL, file_op, FILE_USAGE, NULL},
{">" N_("Permissions"),		NULL, file_op, FILE_CHMOD_ITEMS, NULL},
{">",				NULL, NULL, 0, "<Separator>"},
{">" N_("Find"),		NULL, file_op, FILE_FIND, "<StockItem>", GTK_STOCK_FIND},
{N_("Select"),	    		NULL, NULL, 0, "<Branch>"},
{">" N_("Select All"),	    	NULL, select_all, 0, NULL},
{">" N_("Clear Selection"),	NULL, clear_selection, 0, NULL},
{">" N_("Invert Selection"),	NULL, invert_selection, 0, NULL},
{">" N_("Select If..."),	NULL, mini_buffer, MINI_SELECT_IF, NULL},
{N_("Options..."),		NULL, menu_show_options, 0, "<StockItem>", GTK_STOCK_PREFERENCES},
{N_("New"),			NULL, NULL, 0, "<Branch>"},
{">" N_("Directory"),		NULL, new_directory, 0, NULL},
{">" N_("Blank file"),		NULL, new_file, 0, "<StockItem>", GTK_STOCK_NEW},
{N_("Window"),			NULL, NULL, 0, "<Branch>"},
{">" N_("Parent, New Window"), 	NULL, open_parent, 0, "<StockItem>", GTK_STOCK_GO_UP},
{">" N_("Parent, Same Window"), NULL, open_parent_same, 0, NULL},
{">" N_("New Window"),		NULL, new_window, 0, NULL},
{">" N_("Home Directory"),	NULL, home_directory, 0, "<StockItem>", GTK_STOCK_HOME},
{">" N_("Show Bookmarks"),	"<Ctrl>B", show_bookmarks, 0, "<StockItem>", ROX_STOCK_BOOKMARKS},
{">" N_("Follow Symbolic Links"),	NULL, follow_symlinks, 0, NULL},
{">" N_("Resize Window"),	NULL, resize, 0, NULL},
/* {">" N_("New, As User..."),	NULL, new_user, 0, NULL}, */

{">" N_("Close Window"),	NULL, close_window, 0, "<StockItem>", GTK_STOCK_CLOSE},
{">",				NULL, NULL, 0, "<Separator>"},
{">" N_("Enter Path..."),	"slash", mini_buffer, MINI_PATH, NULL},
{">" N_("Shell Command..."),	NULL, mini_buffer, MINI_SHELL, NULL},
{">" N_("Xterm Here"),		NULL, xterm_here, FALSE, NULL},
{">" N_("Switch to xterm"),	NULL, xterm_here, TRUE, NULL},
{N_("Help"),			NULL, NULL, 0, "<Branch>"},
{">" N_("About ROX-Filer..."),	NULL, menu_rox_help, HELP_ABOUT, NULL},
{">" N_("Show Help Files"),	"F1", menu_rox_help, HELP_DIR, "<StockItem>", GTK_STOCK_HELP},
{">" N_("Manual"),		NULL, menu_rox_help, HELP_MANUAL, NULL},
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

/* Returns TRUE if the keys were installed (first call only) */
gboolean ensure_filer_menu(void)
{
	GList			*items;
	guchar			*tmp;
	GtkWidget		*item;
	GtkItemFactory  	*item_factory;

	if (!filer_keys_need_init)
		return FALSE;
	filer_keys_need_init = FALSE;

	item_factory = menu_create(filer_menu_def,
		sizeof(filer_menu_def) / sizeof(*filer_menu_def),
		"<filer>", filer_keys);

	GET_MENU_ITEM(filer_menu, "filer");
	GET_SMENU_ITEM(filer_file_menu, "filer", "File");
	GET_SSMENU_ITEM(filer_hidden_menu, "filer", "Display", "Show Hidden");
	GET_SSMENU_ITEM(filer_reverse_menu, "filer", "Display", "Reversed");
	GET_SSMENU_ITEM(filer_auto_size_menu, "filer", "Display", "Automatic");
	GET_SSMENU_ITEM(filer_thumb_menu, "filer", "Display",
							"Show Thumbnails");

	GET_SMENU_ITEM(filer_new_menu, "filer", "New");
	GET_SSMENU_ITEM(item, "filer", "Window", "Follow Symbolic Links");
	filer_follow_sym = GTK_BIN(item)->child;

	/* File '' label... */
	items = gtk_container_get_children(GTK_CONTAINER(filer_menu));
	filer_file_item = GTK_BIN(g_list_nth(items, 1)->data)->child;
	g_list_free(items);

	/* Shift Open... label */
	items = gtk_container_get_children(GTK_CONTAINER(filer_file_menu));
	file_shift_item = GTK_BIN(g_list_nth(items, 6)->data)->child;
	g_list_free(items);

	GET_SSMENU_ITEM(item, "filer", "Window", "New Window");
	filer_new_window = GTK_BIN(item)->child;

	g_signal_connect(filer_menu, "unmap_event",
			G_CALLBACK(menu_closed), NULL);
	g_signal_connect(filer_file_menu, "unmap_event",
			G_CALLBACK(menu_closed), NULL);

	g_signal_connect(filer_keys, "accel_changed",
			G_CALLBACK(save_menus), NULL);

	return TRUE;
}

void menu_init(void)
{
	char			*menurc;

	menurc = choices_find_path_load(MENUS_NAME, PROJECT);
	if (menurc)
	{
		gtk_accel_map_load(menurc);
		g_free(menurc);
	}

	option_add_string(&o_menu_xterm, "menu_xterm", "xterm");
	option_add_int(&o_menu_iconsize, "menu_iconsize", MIS_SMALL);
	option_add_saver(save_menus);

	option_register_widget("menu-set-keys", set_keys_button);

	filer_keys = gtk_accel_group_new();
}

/* Name is in the form "<panel>" */
GtkItemFactory *menu_create(GtkItemFactoryEntry *def, int n_entries,
			    const gchar *name, GtkAccelGroup *keys)
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
static void menuitem_no_shortcuts(GtkWidget *item)
{
	/* XXX */
#if 0
	GtkMenuItem *menuitem = GTK_MENU_ITEM(item);

	_gtk_widget_set_accel_path(item, NULL, NULL);
	null_g_free(&menuitem->accel_path);
#endif
}

/* Shade items that only work on single files */
static void shade_file_menu_items(gboolean shaded)
{
	menu_set_items_shaded(filer_file_menu, shaded, 0, 3);
	menu_set_items_shaded(filer_file_menu, shaded, 5, 3);
	menu_set_items_shaded(filer_file_menu, shaded, 10, 2);
}

/* 'data' is an array of three ints:
 * [ pointer_x, pointer_y, item_under_pointer ]
 */
void position_menu(GtkMenu *menu, gint *x, gint *y,
		   gboolean  *push_in, gpointer data)
{
	int		*pos = (int *) data;
	GtkRequisition 	requisition;
	GList		*items, *next;
	int		y_shift = 0;
	int		item = pos[2];

	next = items = gtk_container_get_children(GTK_CONTAINER(menu));

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

	*push_in = FALSE;
}

/* Returns an array listing all the names in the directory 'path'.
 * The array is unsorted.
 * '.' and '..' are skipped.
 * On error, the error is reported with g_warning and NULL is returned.
 */
static GPtrArray *list_dir(const guchar *path)
{
	GDir *dir;
	GError *error = NULL;
	GPtrArray *names;
	const char *leaf;
	
	dir = g_dir_open(path, 0, &error);
	if (error)
	{
		g_warning("Can't list directory:\n%s", error->message);
		g_error_free(error);
		return NULL;
	}

	names = g_ptr_array_new();

	while ((leaf = g_dir_read_name(dir)))
		g_ptr_array_add(names, g_strdup(leaf));

	g_dir_close(dir);

	return names;
}

/* Used as the sort function for sorting GPtrArrays */
static gint strcmp2(gconstpointer a, gconstpointer b)
{
	const char *aa = *(char **) a;
	const char *bb = *(char **) b;

	return g_strcasecmp(aa, bb);
}

static GList *menu_from_dir(GtkWidget *menu, const gchar *dir_name,
			    MenuIconStyle style, CallbackFn func,
			    gboolean separator, gboolean strip_ext,
			    gboolean recurse)
{
	GList *widgets = NULL;
	DirItem *ditem;
	int i;
	GtkWidget *item;
	char *dname = NULL;
	GPtrArray *names;

	dname = pathdup(dir_name);

	names = list_dir(dname);
	if (!names)
		goto out;

	if (separator)
	{
		item = gtk_menu_item_new();
		widgets = g_list_append(widgets, item);
		gtk_menu_shell_append(GTK_MENU_SHELL(menu), item);
	}

	g_ptr_array_sort(names, strcmp2);

	for (i = 0; i < names->len; i++)
	{
		char	*leaf = names->pdata[i];
		gchar	*fname;

		fname = g_strconcat(dname, "/", leaf, NULL);

		/* Strip off extension, if any */
		if (strip_ext)
		{
			char	*dot;
			dot = strchr(leaf, '.');
			if (dot)
				*dot = '\0';
		}

		ditem = diritem_new("");
		diritem_restat(fname, ditem, NULL);

		if (ditem->image && style != MIS_NONE)
		{
			GdkPixbuf *pixbuf;

			switch (style)
			{
				case MIS_LARGE:
					pixbuf = ditem->image->pixbuf;
					break;
				default:
					if (!ditem->image->sm_pixbuf)
						pixmap_make_small(ditem->image);
					pixbuf = ditem->image->sm_pixbuf;
					break;
			}

			item = gtk_image_menu_item_new_with_label(leaf);
			/* TODO: Find a way to allow short-cuts */
			menuitem_no_shortcuts(item);

			gtk_image_menu_item_set_image(GTK_IMAGE_MENU_ITEM(item),
					gtk_image_new_from_pixbuf(pixbuf));
		}
		else
			item = gtk_menu_item_new_with_label(leaf);

		g_free(leaf);

		/* If it is a directory (but NOT an AppDir) and we are
		 * recursing then set up a sub menu.
		 */
		if (recurse && ditem->base_type == TYPE_DIRECTORY &&
			   !(ditem->flags & ITEM_FLAG_APPDIR))
		{
			GtkWidget *sub;
			GList *new_widgets;

			sub = gtk_menu_new();
			new_widgets = menu_from_dir(sub, fname, style, func,
						separator, strip_ext, FALSE);
			g_list_free(new_widgets);
			gtk_menu_item_set_submenu(GTK_MENU_ITEM(item), sub);
		}
		else
			g_signal_connect_swapped(item, "activate",
					G_CALLBACK(func), fname);

		diritem_free(ditem);

		gtk_menu_shell_append(GTK_MENU_SHELL(menu), item);
		g_signal_connect_swapped(item, "destroy",
				G_CALLBACK(g_free), fname);

		widgets = g_list_append(widgets, item);
	}

	g_ptr_array_free(names, TRUE);
out:
	g_free(dname);

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
					(CallbackFn) new_file_type, TRUE, TRUE,
					FALSE);
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

/* Hide the popup menu, if any */
void menu_popdown(void)
{
	if (popup_menu)
		gtk_menu_popdown(GTK_MENU(popup_menu));
}

static MenuIconStyle get_menu_icon_style(void)
{
	MenuIconStyle mis;
	int display;

	mis = o_menu_iconsize.int_value;

	switch (mis)
	{
		case MIS_NONE: case MIS_SMALL: case MIS_LARGE:
			return mis;
		default:
			break;
	}

	if (mis == MIS_CURRENT && window_with_focus)
	{
		switch (window_with_focus->display_style)
		{
			case HUGE_ICONS:
			case LARGE_ICONS:
				return MIS_LARGE;
			case SMALL_ICONS:
				return MIS_SMALL;
			default:
				break;
		}
	}

	display = o_display_size.int_value;
	switch (display)
	{
		case HUGE_ICONS:
		case LARGE_ICONS:
			return MIS_LARGE;
		case SMALL_ICONS:
			return MIS_SMALL;
		default:
			break;
	}

	return MIS_SMALL;
}

/* iter->peek() is the clicked item, or NULL if none */
void show_filer_menu(FilerWindow *filer_window, GdkEvent *event, ViewIter *iter)
{
	DirItem		*file_item = NULL;
	GdkModifierType	state = 0;
	int		n_selected;

	n_selected = view_count_selected(filer_window->view);

	ensure_filer_menu();

	updating_menu++;

	/* Remove previous AppMenu, if any */
	appmenu_remove();

	window_with_focus = filer_window;

	if (!event)
		event = gtk_get_current_event();

	if (event->type == GDK_BUTTON_PRESS)
		state = ((GdkEventButton *) event)->state;
	else if (event->type == GDK_KEY_PRESS)
		state = ((GdkEventKey *) event)->state;

	if (n_selected == 0 && iter && iter->peek(iter) != NULL)
	{
		filer_window->temp_item_selected = TRUE;
		view_set_selected(filer_window->view, iter, TRUE);
		n_selected = view_count_selected(filer_window->view);
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

		if (n_selected == 0)
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
		gtk_check_menu_item_set_active(
				GTK_CHECK_MENU_ITEM(filer_reverse_menu),
				filer_window->sort_order != GTK_SORT_ASCENDING);
		gtk_check_menu_item_set_active(
			GTK_CHECK_MENU_ITEM(filer_auto_size_menu),
			filer_window->display_style_wanted == AUTO_SIZE_ICONS);
		buffer = g_string_new(NULL);

		switch (n_selected)
		{
			case 0:
				g_string_assign(buffer, _("Next Click"));
				shade_file_menu_items(FALSE);
				break;
			case 1:
				item = filer_selected_item(filer_window);
				if (!item->image)
					dir_update_item(filer_window->directory,
							item->leafname);
				shade_file_menu_items(FALSE);
				file_item = filer_selected_item(filer_window);
				g_string_printf(buffer, "%s '%s'",
					basetype_name(file_item),
					g_utf8_validate(file_item->leafname,
							-1, NULL)
						? file_item->leafname
						: _("(bad utf-8)"));
				if (!can_set_run_action(file_item))
					menu_set_items_shaded(filer_file_menu,
							TRUE, 6, 1);
				break;
			default:
				shade_file_menu_items(TRUE);
				g_string_printf(buffer, _("%d items"),
						 n_selected);
				break;
		}
		gtk_label_set_text(GTK_LABEL(file_label), buffer->str);
		g_string_free(buffer, TRUE);

		menu_show_shift_action(file_shift_item, file_item,
					n_selected == 0);
		if (file_item)
			appmenu_add(make_path(filer_window->sym_path,
						file_item->leafname),
					file_item, filer_file_menu);
	}

	update_new_files_menu(get_menu_icon_style());

	gtk_widget_set_sensitive(filer_new_window,
			!o_unique_filer_windows.int_value);
	gtk_widget_set_sensitive(filer_follow_sym,
		strcmp(filer_window->sym_path, filer_window->real_path) != 0);

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
		view_clear_selection(window_with_focus->view);
		window_with_focus->temp_item_selected = FALSE;
	}
}

static void target_callback(FilerWindow *filer_window,
			ViewIter *iter,
			gpointer action)
{
	g_return_if_fail(filer_window != NULL);

	window_with_focus = filer_window;
	
	/* Don't grab the primary selection */
	filer_window->temp_item_selected = TRUE;
	
	view_wink_item(filer_window->view, iter);
	view_select_only(filer_window->view, iter);
	file_op(NULL, GPOINTER_TO_INT(action), NULL);

	view_clear_selection(filer_window->view);
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
				shift_action = N_("Open unmounted");
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

static void view_type(gpointer data, guint action, GtkWidget *widget)
{
	ViewType view_type = (ViewType) action;

	g_return_if_fail(window_with_focus != NULL);

	if (view_type == VIEW_TYPE_COLLECTION)
		display_set_layout(window_with_focus,
				window_with_focus->display_style_wanted,
				DETAILS_NONE, FALSE);

	filer_set_view_type(window_with_focus, (ViewType) action);
}

static void change_size(gpointer data, guint action, GtkWidget *widget)
{
	g_return_if_fail(window_with_focus != NULL);

	display_change_size(window_with_focus, action == 1);
}

static void change_size_auto(gpointer data, guint action, GtkWidget *widget)
{
	g_return_if_fail(window_with_focus != NULL);

	if (updating_menu)
		return;

	if (window_with_focus->display_style_wanted == AUTO_SIZE_ICONS)
		display_set_layout(window_with_focus,
				   window_with_focus->display_style,
				   window_with_focus->details_type, FALSE);
	else
		display_set_layout(window_with_focus, AUTO_SIZE_ICONS,
				   window_with_focus->details_type, FALSE);
}

static void set_with(gpointer data, guint action, GtkWidget *widget)
{
	DisplayStyle size;

	g_return_if_fail(window_with_focus != NULL);

	size = window_with_focus->display_style_wanted;

	filer_set_view_type(window_with_focus, VIEW_TYPE_COLLECTION);
	display_set_layout(window_with_focus, size, action, FALSE);
}

static void set_sort(gpointer data, guint action, GtkWidget *widget)
{
	if (updating_menu)
		return;

	g_return_if_fail(window_with_focus != NULL);

	display_set_sort_type(window_with_focus, action, GTK_SORT_ASCENDING);
}

static void reverse_sort(gpointer data, guint action, GtkWidget *widget)
{
	GtkSortType order;

	if (updating_menu)
		return;
	
	g_return_if_fail(window_with_focus != NULL);

	order = window_with_focus->sort_order;
	if (order == GTK_SORT_ASCENDING)
		order = GTK_SORT_DESCENDING;
	else
		order = GTK_SORT_ASCENDING;

	display_set_sort_type(window_with_focus, window_with_focus->sort_type,
			      order);
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
}

static void delete(FilerWindow *filer_window)
{
	GList *paths;
	paths = filer_selected_items(filer_window);
	action_delete(paths);
	destroy_glist(&paths);
}

static void usage(FilerWindow *filer_window)
{
	GList *paths;
	paths = filer_selected_items(filer_window);
	action_usage(paths);
	destroy_glist(&paths);
}

static void chmod_items(FilerWindow *filer_window)
{
	GList *paths;
	paths = filer_selected_items(filer_window);
	action_chmod(paths, FALSE, NULL);
	destroy_glist(&paths);
}

static void find(FilerWindow *filer_window)
{
	GList *paths;
	paths = filer_selected_items(filer_window);
	action_find(paths);
	destroy_glist(&paths);
}

/* This creates a new savebox widget, and allows the user to pick a new path
 * for the file.
 * Once the new path has been picked, the callback will be called with
 * both the current and new paths.
 * NOTE: This function unrefs 'image'!
 */
static void savebox_show(const gchar *action, const gchar *path,
			 MaskedPixmap *image, SaveCb callback,
			 GdkDragAction dnd_action)
{
	GtkWidget	*savebox = NULL;	
	GtkWidget	*check_relative = NULL;	

	g_return_if_fail(image != NULL);
	
	savebox = gtk_savebox_new(action);
	gtk_savebox_set_action(GTK_SAVEBOX(savebox), dnd_action);

	if (callback == link_cb)
	{
		check_relative = gtk_check_button_new_with_mnemonic(
							_("_Relative link"));
		gtk_toggle_button_set_active(GTK_TOGGLE_BUTTON(check_relative),
					     TRUE);

		GTK_WIDGET_UNSET_FLAGS(check_relative, GTK_CAN_FOCUS);
		gtk_tooltips_set_tip(tooltips, check_relative,
			_("If on, the symlink will store the path from the "
			"symlink to the target file. Use this if the symlink "
			"and the target will be moved together.\n"
			"If off, the path from the root directory is stored - "
			"use this if the symlink may move but the target will "
			"stay put."), NULL);
		gtk_box_pack_start(GTK_BOX(GTK_DIALOG(savebox)->vbox),
				check_relative, FALSE, TRUE, 0);
		gtk_widget_show(check_relative);
	}

	g_signal_connect(savebox, "save_to_file",
				G_CALLBACK(save_to_file), NULL);

	g_object_set_data_full(G_OBJECT(savebox), "current_path",
				g_strdup(path), g_free);
	g_object_set_data(G_OBJECT(savebox), "action_callback", callback);
	g_object_set_data(G_OBJECT(savebox), "check_relative", check_relative);

	gtk_window_set_title(GTK_WINDOW(savebox), action);

	if (g_utf8_validate(path, -1, NULL))
		gtk_savebox_set_pathname(GTK_SAVEBOX(savebox), path);
	else
	{
		gchar *u8, *dir;
		dir = g_path_get_dirname(path);
		u8 = to_utf8(g_basename(path));
		gtk_savebox_set_pathname(GTK_SAVEBOX(savebox),
				make_path(dir, u8));
		g_free(u8);
		g_free(dir);
	}
	gtk_savebox_set_icon(GTK_SAVEBOX(savebox), image->pixbuf);
	g_object_unref(image);
				
	gtk_widget_show(savebox);
}

static gint save_to_file(GObject *savebox,
			 const gchar *pathname, gpointer data)
{
	SaveCb		callback;
	const gchar	*current_path;
	
	callback = g_object_get_data(savebox, "action_callback");
	current_path = g_object_get_data(savebox, "current_path");

	g_return_val_if_fail(callback != NULL, GTK_XDS_SAVE_ERROR);
	g_return_val_if_fail(current_path != NULL, GTK_XDS_SAVE_ERROR);

	return callback(savebox, current_path, pathname)
			? GTK_XDS_SAVED : GTK_XDS_SAVE_ERROR;
}

static gboolean copy_cb(GObject *savebox,
			const gchar *current, const gchar *new)
{
	return action_with_leaf(action_copy, current, new);
}

static gboolean action_with_leaf(ActionFn action,
				 const gchar *current, const gchar *new)
{
	const char	*leaf;
	char		*new_dir;
	GList		*local_paths;

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
		const gchar *slash;
		
		slash = strrchr(new, '/');
		new_dir = g_strndup(new, slash - new);
		leaf = slash + 1;
	}

	local_paths = g_list_append(NULL, (gchar *) current);
	action(local_paths, new_dir, leaf, -1);
	g_list_free(local_paths);

	g_free(new_dir);

	return TRUE;
}

/* Open a savebox to act on the selected file.
 * Call 'callback' later to perform the operation.
 */
static void src_dest_action_item(const gchar *path, MaskedPixmap *image,
			 const gchar *action, SaveCb callback,
			 GdkDragAction dnd_action)
{
	g_object_ref(image);
	savebox_show(action, path, image, callback, dnd_action);
}

static gboolean rename_cb(GObject *savebox,
			  const gchar *current, const gchar *new)
{
	return action_with_leaf(action_move, current, new);
}

static gboolean link_cb(GObject *savebox,
			const gchar *initial, const gchar *path)
{
	GtkToggleButton *check_relative;
	struct stat info;
	int	err;
	gchar	*link_path;

	check_relative = g_object_get_data(savebox, "check_relative");

	if (gtk_toggle_button_get_active(check_relative))
		link_path = get_relative_path(path, initial);
	else
		link_path = g_strdup(initial);

	if (mc_lstat(path, &info) == 0 && S_ISLNK(info.st_mode))
	{
		GtkWidget *box, *button;
		gint ans;

		box = gtk_message_dialog_new(NULL, 0, GTK_MESSAGE_QUESTION,
				GTK_BUTTONS_CANCEL,
				_("Symlink from '%s' already exists. "
				"Replace it with a link to '%s'?"),
				path, link_path);

		gtk_window_set_position(GTK_WINDOW(box), GTK_WIN_POS_MOUSE);
		
		button = button_new_mixed(GTK_STOCK_YES, _("_Replace"));
		gtk_widget_show(button);
		GTK_WIDGET_SET_FLAGS(button, GTK_CAN_DEFAULT);
		gtk_dialog_add_action_widget(GTK_DIALOG(box),
					     button, GTK_RESPONSE_OK);
		gtk_dialog_set_default_response(GTK_DIALOG(box),
						GTK_RESPONSE_OK);

		ans = gtk_dialog_run(GTK_DIALOG(box));
		gtk_widget_destroy(box);
		
		if (ans != GTK_RESPONSE_OK)
		{
			g_free(link_path);
			return FALSE;
		}

		unlink(path);
	}

	err = symlink(link_path, path);
	g_free(link_path);
			
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
	filer_opendir(home_dir, NULL, NULL);
}

static void open_vfs_avfs(FilerWindow *filer_window, DirItem *item)
{
	gchar		*path;

	path = g_strconcat(filer_window->sym_path,
			"/", item->leafname, "#", NULL);

	filer_change_to(filer_window, path, NULL);
	g_free(path);
}

static void select_all(gpointer data, guint action, GtkWidget *widget)
{
	g_return_if_fail(window_with_focus != NULL);

	window_with_focus->temp_item_selected = FALSE;
	view_select_all(window_with_focus->view);
}

static void clear_selection(gpointer data, guint action, GtkWidget *widget)
{
	g_return_if_fail(window_with_focus != NULL);

	window_with_focus->temp_item_selected = FALSE;
	view_clear_selection(window_with_focus->view);
}

static gboolean invert_cb(ViewIter *iter, gpointer data)
{
	return !view_get_selected((ViewIface *) data, iter);
}

static void invert_selection(gpointer data, guint action, GtkWidget *widget)
{
	g_return_if_fail(window_with_focus != NULL);

	window_with_focus->temp_item_selected = FALSE;

	view_select_if(window_with_focus->view, invert_cb,
		       window_with_focus->view);
}

void menu_show_options(gpointer data, guint action, GtkWidget *widget)
{
	GtkWidget *win;

	win = options_show();

	if (win)
	{
		number_of_windows++;
		g_signal_connect(win, "destroy",
				G_CALLBACK(one_less_window), NULL);
	}
}

static gboolean new_directory_cb(GObject *savebox,
				 const gchar *initial, const gchar *path)
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

	savebox_show(_("Create"),
		make_path(window_with_focus->sym_path, _("NewDir")),
		type_to_icon(inode_directory), new_directory_cb,
		GDK_ACTION_COPY);
}

static gboolean new_file_cb(GObject *savebox,
			    const gchar *initial, const gchar *path)
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
	
	savebox_show(_("Create"),
		make_path(window_with_focus->sym_path, _("NewFile")),
		type_to_icon(text_plain),
		new_file_cb, GDK_ACTION_COPY);
}

static gboolean new_file_type_cb(GObject *savebox,
			         const gchar *initial, const gchar *path)
{
	const gchar *oleaf, *leaf;
	gchar *templ, *templ_dname, *dest;
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

	dest = g_path_get_dirname(path);
	leaf = g_basename(path);
	paths = g_list_append(NULL, templ);

	action_copy(paths, dest, leaf, TRUE);

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
	const gchar *leaf;
	MIME_type *type;

	g_return_if_fail(window_with_focus != NULL);
	
	leaf = g_basename(templ);
	type = type_get_type(templ);

	savebox_show(_("Create"),
		make_path(window_with_focus->sym_path, leaf),
		type_to_icon(type),
		new_file_type_cb, GDK_ACTION_COPY);
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

	info_message(
		_("The `Send To' menu provides a quick way to send some files "
		"to an application. The applications listed are those in "
		"the following directories:\n\n%s\n%s\n"
		"The `Send To' menu may be opened by Shift+Menu clicking "
		"over a file.\n\n"
		"Advanced use:\n"
		"You can also create subdirectories called "
		"`.text_html', `.text', etc which will only be "
		"shown for files of that type. `.group' is shown "
		"only when multiple files are selected."),
		dirs->str,
		save ? _("I'll show you your SendTo directory now; you should "
			"symlink (Ctrl+Shift drag) any applications you want "
			"into it.")
		     : _("Your CHOICESPATH variable setting prevents "
			 "customisations - sorry."));

	g_string_free(dirs, TRUE);
	
	if (save)
		filer_opendir(save, NULL, NULL);
}

/* Add everything in the directory <Choices>/SendTo/[.type[_subtype]] 
 * to the menu.
 */
static void add_sendto(GtkWidget *menu, const gchar *type, const gchar *subtype)
{
	gchar *searchdir;
	GPtrArray *paths;
	int i;

	if (subtype)
		searchdir = g_strdup_printf("SendTo/.%s_%s", type, subtype);
	else if (type)
		searchdir = g_strdup_printf("SendTo/.%s", type);
	else
		searchdir = g_strdup("SendTo");

	paths = choices_list_dirs(searchdir);
	g_free(searchdir);	

	for (i = 0; i < paths->len; i++)
	{
		GList	*widgets = NULL;
		guchar	*dir = (guchar *) paths->pdata[i];

		widgets = menu_from_dir(menu, dir, get_menu_icon_style(),
				(CallbackFn) do_send_to,
				FALSE, FALSE, TRUE);

		if (widgets)
			gtk_menu_shell_append(GTK_MENU_SHELL(menu),
					gtk_menu_item_new());

		g_list_free(widgets);	/* TODO: Get rid of this */
	}

	choices_free_list(paths);
}

/* Scan the SendTo dir and create and show the Send To menu.
 * The 'paths' list and every path in it is claimed, and will be
 * freed later -- don't free it yourself!
 */
static void show_send_to_menu(GList *paths, GdkEvent *event)
{
	GtkWidget	*menu, *item;

	menu = gtk_menu_new();
	
	if (g_list_length(paths) == 1)
	{
		DirItem	*item;
		
		item = diritem_new("");
		diritem_restat(paths->data, item, NULL);

		add_sendto(menu,
			   item->mime_type->media_type,
			   item->mime_type->subtype);

		add_sendto(menu, item->mime_type->media_type, NULL);
		
		diritem_free(item);
	}
	else
		add_sendto(menu, "group", NULL);
	
	add_sendto(menu, NULL, NULL);

	item = gtk_menu_item_new_with_label(_("Customise"));
	g_signal_connect_swapped(item, "activate",
				G_CALLBACK(customise_send_to), NULL);
	gtk_menu_shell_append(GTK_MENU_SHELL(menu), item);

	if (send_to_paths)
		destroy_glist(&send_to_paths);

	send_to_paths = paths;

	g_signal_connect(menu, "unmap_event", G_CALLBACK(menu_closed), NULL);

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
	const char *argv[] = {"sh", "-c", NULL, NULL};
	gboolean close = action;

	argv[2] = o_menu_xterm.value;

	g_return_if_fail(window_with_focus != NULL);

	if (rox_spawn(window_with_focus->sym_path, argv) && close)
		gtk_widget_destroy(window_with_focus->window);
}

static void home_directory(gpointer data, guint action, GtkWidget *widget)
{
	g_return_if_fail(window_with_focus != NULL);

	filer_change_to(window_with_focus, home_dir, NULL);
}

static void show_bookmarks(gpointer data, guint action, GtkWidget *widget)
{
	g_return_if_fail(window_with_focus != NULL);

	bookmarks_show_menu(window_with_focus);
}

static void follow_symlinks(gpointer data, guint action, GtkWidget *widget)
{
	g_return_if_fail(window_with_focus != NULL);

	if (strcmp(window_with_focus->real_path, window_with_focus->sym_path))
		filer_change_to(window_with_focus,
				window_with_focus->real_path, NULL);
	else
		delayed_error(_("This is already the canonical name "
				"for this directory."));
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

	view_autosize(window_with_focus->view);
}

static void new_window(gpointer data, guint action, GtkWidget *widget)
{
	g_return_if_fail(window_with_focus != NULL);

	if (o_unique_filer_windows.int_value)
	{
		report_error(_("You can't open a second view onto "
			"this directory because the `Unique Windows' option "
			"is turned on in the Options window."));
	}
	else
		filer_opendir(window_with_focus->sym_path, window_with_focus, NULL);
}

static void close_window(gpointer data, guint action, GtkWidget *widget)
{
	g_return_if_fail(window_with_focus != NULL);

	if (!filer_window_delete(window_with_focus->window, NULL,
				 window_with_focus))
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
	if (action == HELP_ABOUT)
		infobox_new(app_dir);
	else if (action == HELP_DIR)
		filer_opendir(make_path(app_dir, "Help"), NULL, NULL);
	else if (action == HELP_MANUAL)
	{
		gchar *manual = NULL;

		if (current_lang)
		{
			manual = g_strconcat(app_dir, "/Help/Manual-",
					     current_lang, ".html", NULL);
			if (access(manual, F_OK))
				null_g_free(&manual);
		}

		if (!manual)
			manual = g_strconcat(app_dir,
						"/Help/Manual.html", NULL);
		
		run_by_path(manual);

		g_free(manual);
	}
	else
		g_warning("Unknown help action %d\n", action);
}

/* Set n items from position 'from' in 'menu' to the 'shaded' state */
void menu_set_items_shaded(GtkWidget *menu, gboolean shaded, int from, int n)
{
	GList	*items, *item;

	items = gtk_container_get_children(GTK_CONTAINER(menu));

	item = g_list_nth(items, from);
	while (item && n--)
	{
		gtk_widget_set_sensitive(GTK_BIN(item->data)->child, !shaded);
		item = item->next;
	}
	g_list_free(items);
}

static void save_menus(void)
{
	char	*menurc;

	menurc = choices_find_path_save(MENUS_NAME, PROJECT, TRUE);
	if (menurc)
	{
		gtk_accel_map_save(menurc);
		g_free(menurc);
	}
}

static void select_nth_item(GtkMenuShell *shell, int n)
{
	GList	  *items;
	GtkWidget *item;

	items = gtk_container_get_children(GTK_CONTAINER(shell));
	item = g_list_nth_data(items, n);

	g_return_if_fail(item != NULL);

	g_list_free(items);

	gtk_menu_shell_select_item(shell, item);
}

static void file_op(gpointer data, FileOp action, GtkWidget *unused)
{
	DirItem	*item;
	const guchar *path;
	int	n_selected;
	ViewIter iter;

	g_return_if_fail(window_with_focus != NULL);

	n_selected = view_count_selected(window_with_focus->view);

	if (n_selected < 1)
	{
		const char *prompt;

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
			case FILE_OPEN_VFS_AVFS:
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
		case FILE_SHOW_FILE_INFO:
		{
			GList *items;

			items = filer_selected_items(window_with_focus);
			infobox_show_list(items);
			destroy_glist(&items);
			return;
		}
		default:
			break;
	}

	/* All the following actions require exactly one file selected */

	if (n_selected > 1)
	{
		report_error(_("You cannot do this to more than "
				"one item at a time"));
		return;
	}

	view_get_iter(window_with_focus->view, &iter, VIEW_ITER_SELECTED);
		
	item = iter.next(&iter);
	g_return_if_fail(item != NULL);
	/* iter may be passed to filer_openitem... */

	if (!item->image)
		item = dir_update_item(window_with_focus->directory,
					item->leafname);

	if (!item)
	{
		report_error(_("Item no longer exists!"));
		return;
	}

	path = make_path(window_with_focus->sym_path, item->leafname);

	switch (action)
	{
		case FILE_COPY_ITEM:
			src_dest_action_item(path, item->image,
					_("Copy"), copy_cb,
					GDK_ACTION_COPY);
			break;
		case FILE_RENAME_ITEM:
			src_dest_action_item(path, item->image,
					_("Rename"), rename_cb,
					GDK_ACTION_MOVE);
			break;
		case FILE_LINK_ITEM:
			src_dest_action_item(path, item->image,
					_("Symlink"), link_cb,
					GDK_ACTION_LINK);
			break;
		case FILE_OPEN_FILE:
			filer_openitem(window_with_focus, &iter,
				OPEN_SAME_WINDOW | OPEN_SHIFT);
			break;
		case FILE_HELP:
			show_item_help(path, item);
			break;
		case FILE_RUN_ACTION:
			run_action(item);
			break;
		case FILE_SET_ICON:
			icon_set_handler_dialog(item, path);
			break;
		case FILE_OPEN_VFS_AVFS:
			open_vfs_avfs(window_with_focus, item);
			break;
		default:
			g_warning("Unknown action!");
			return;
	}
}

static void show_key_help(GtkWidget *button, gpointer data)
{
	gboolean can_change_accels;
	
	g_object_get(G_OBJECT(gtk_settings_get_default()),
		     "gtk-can-change-accels", &can_change_accels,
		     NULL);

	if (!can_change_accels)
	{
		info_message(_("User-definable shortcuts are disabled by "
			"default in Gtk2, and you have not enabled "
			"them. You can turn this feature on by:\n"
			"1) using an XSettings manager, such as ROX-Session\n"
			"or\n"
			"2) adding this line to ~/.gtkrc-2.0:\n"
			"\tgtk-can-change-accels = 1"));
		return;
	}

	info_message(_("To set a keyboard short-cut for a menu item:\n\n"
	"- Open the menu over a filer window,\n"
	"- Move the pointer over the item you want to use,\n"
	"- Press the key you want attached to it.\n\n"
	"The key will appear next to the menu item and you can just press "
	"that key without opening the menu in future."));
}

static GList *set_keys_button(Option *option, xmlNode *node, guchar *label)
{
	GtkWidget *button, *align;

	g_return_val_if_fail(option == NULL, NULL);
	
	align = gtk_alignment_new(0, 0.5, 0, 0);
	button = gtk_button_new_with_label(_("Set keyboard shortcuts"));
	gtk_container_add(GTK_CONTAINER(align), button);
	g_signal_connect(button, "clicked", G_CALLBACK(show_key_help), NULL);

	return g_list_append(NULL, align);
}
