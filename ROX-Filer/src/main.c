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

/* main.c - parses command-line options and parameters, plus some global
 * 	    housekeeping.
 *
 * New to the code and feeling lost? Read global.h now.
 */

#include "config.h"

#include <stdlib.h>
#include <stdio.h>
#include <sys/types.h>
#include <signal.h>
#include <string.h>
#include <sys/wait.h>
#include <unistd.h>
#include <fcntl.h>
#include <pwd.h>
#include <grp.h>
#include <libxml/parser.h>

#ifdef HAVE_GETOPT_LONG
#  include <getopt.h>
#endif

#include <gtk/gtk.h>

#include "global.h"

#include "main.h"
#include "support.h"
#include "gui_support.h"
#include "filer.h"
#include "display.h"
#include "mount.h"
#include "menu.h"
#include "dnd.h"
#include "options.h"
#include "choices.h"
#include "type.h"
#include "pixmaps.h"
#include "dir.h"
#include "diritem.h"
#include "action.h"
#include "i18n.h"
#include "remote.h"
#include "pinboard.h"
#include "run.h"
#include "toolbar.h"
#include "bind.h"
#include "icon.h"
#include "panel.h"
#include "session.h"
#include "minibuffer.h"

int number_of_windows = 0;	/* Quit when this reaches 0 again... */
static int to_wakeup_pipe = -1;	/* Write here to get noticed */

/* Information about the ROX-Filer process */
uid_t euid;
gid_t egid;
int ngroups;			/* Number of supplemental groups */
gid_t *supplemental_groups = NULL;

/* Message to display at the top of each filer window */
guchar *show_user_message = NULL;

int home_dir_len;
char *home_dir, *app_dir;

GtkTooltips *tooltips = NULL;

#define COPYING								\
	     N_("Copyright (C) 2002 Thomas Leonard.\n"			\
		"ROX-Filer comes with ABSOLUTELY NO WARRANTY,\n"	\
		"to the extent permitted by law.\n"			\
		"You may redistribute copies of ROX-Filer\n"		\
		"under the terms of the GNU General Public License.\n"	\
		"For more information about these matters, "		\
		"see the file named COPYING.\n")

#ifdef HAVE_GETOPT_LONG
#  define USAGE   N_("Try `ROX-Filer/AppRun --help' for more information.\n")
#  define SHORT_ONLY_WARNING ""
#else
#  define USAGE   N_("Try `ROX-Filer/AppRun -h' for more information.\n")
#  define SHORT_ONLY_WARNING	\
		_("NOTE: Your system does not support long options - \n" \
		"you must use the short versions instead.\n\n")
#endif

#define HELP N_("Usage: ROX-Filer/AppRun [OPTION]... [FILE]...\n"	\
       "Open each directory or file listed, or the current working\n"	\
       "directory if no arguments are given.\n\n"			\
       "  -b, --bottom=PANEL	open PAN as a bottom-edge panel\n"	\
       "  -c, --client-id=ID	used for session management\n"		\
       "  -d, --dir=DIR		open DIR as directory (not application)\n"  \
       "  -D, --close=DIR	close DIR and its subdirectories\n"     \
       "  -h, --help		display this help and exit\n"		\
       "  -l, --left=PANEL	open PAN as a left-edge panel\n"	\
       "  -m, --mime-type=FILE	print MIME type of FILE and exit\n" \
       "  -n, --new		start a new filer, even if already running\n"  \
       "  -o, --override	override window manager control of panels\n" \
       "  -p, --pinboard=PIN	use pinboard PIN as the pinboard\n"	\
       "  -r, --right=PANEL	open PAN as a right-edge panel\n"	\
       "  -R, --RPC		invoke method call read from stdin\n"	\
       "  -s, --show=FILE	open a directory showing FILE\n"	\
       "  -t, --top=PANEL	open PANEL as a top-edge panel\n"	\
       "  -u, --user		show user name in each window \n"	\
       "  -v, --version		display the version information and exit\n"   \
       "  -x, --examine=FILE	FILE has changed - re-examine it\n"	\
       "\nThe latest version can be found at:\n"			\
       "\thttp://rox.sourceforge.net\n"					\
       "\nReport bugs to <tal197@users.sourceforge.net>.\n")

#define SHORT_OPS "c:d:t:b:l:r:op:s:hvnux:m:D:R"

#ifdef HAVE_GETOPT_LONG
static struct option long_opts[] =
{
	{"dir", 1, NULL, 'd'},
	{"top", 1, NULL, 't'},
	{"bottom", 1, NULL, 'b'},
	{"left", 1, NULL, 'l'},
	{"override", 0, NULL, 'o'},
	{"pinboard", 1, NULL, 'p'},
	{"right", 1, NULL, 'r'},
	{"help", 0, NULL, 'h'},
	{"version", 0, NULL, 'v'},
	{"user", 0, NULL, 'u'},
	{"new", 0, NULL, 'n'},
	{"RPC", 0, NULL, 'R'},
	{"show", 1, NULL, 's'},
	{"examine", 1, NULL, 'x'},
	{"close", 1, NULL, 'D'},
	{"mime-type", 1, NULL, 'm'},
	{"client-id", 1, NULL, 'c'},
	{NULL, 0, NULL, 0},
};
#endif

/* Take control of panels away from WM? */
gboolean override_redirect = FALSE;

/* Always start a new filer, even if one seems to be already running */
gboolean new_copy = FALSE;

/* Maps child PIDs to Callback pointers */
static GHashTable *death_callbacks = NULL;
static gboolean child_died_flag = FALSE;

/* Static prototypes */
static void show_features(void);
static void soap_add(xmlNodePtr body,
			   xmlChar *function,
			   xmlChar *arg1_name, xmlChar *arg1_value,
			   xmlChar *arg2_name, xmlChar *arg2_value);
static void child_died(int signum);
static void child_died_callback(void);
static void wake_up_cb(gpointer data, gint source, GdkInputCondition condition);

/****************************************************************
 *			EXTERNAL INTERFACE			*
 ****************************************************************/

/* The value that goes with an option */
#define VALUE (*optarg == '=' ? optarg + 1 : optarg)

/* Parses the command-line to work out what the user wants to do.
 * Tries to send the request to an already-running copy of the filer.
 * If that fails, it initialises all the other modules and executes the
 * request itself.
 */
int main(int argc, char **argv)
{
	int		 wakeup_pipe[2];
	int		 i;
	struct sigaction act;
	guchar		*tmp, *dir, *slash;
	gchar *client_id = NULL;
	gboolean	show_user = FALSE;
	xmlDocPtr	rpc, soap_rpc = NULL, reply;
	xmlNodePtr	body;

	home_dir = g_get_home_dir();
	home_dir_len = strlen(home_dir);
	app_dir = g_strdup(getenv("APP_DIR"));

	/* Get internationalisation up and running. This requires the
	 * choices system, to discover the user's preferred language.
	 */
	choices_init();
	options_init();
	i18n_init();

	if (!app_dir)
	{
		g_warning("APP_DIR environment variable was unset!\n"
			"Use the AppRun script to invoke ROX-Filer...\n");
		app_dir = g_get_current_dir();
	}
#ifdef HAVE_UNSETENV 
	else
	{
		/* Don't pass it on to our child processes... */
		unsetenv("APP_DIR");
	}
#endif

	/* Sometimes we want to take special action when a child
	 * process exits. This hash table is used to convert the
	 * child's PID to the callback function.
	 */
	death_callbacks = g_hash_table_new(NULL, NULL);

#ifdef HAVE_LIBVFS
	mc_vfs_init();
#endif

	/* Find out some information about ourself */
	euid = geteuid();
	egid = getegid();
	ngroups = getgroups(0, NULL);
	if (ngroups < 0)
		ngroups = 0;
	else if (ngroups > 0)
	{
		supplemental_groups = g_malloc(sizeof(gid_t) * ngroups);
		getgroups(ngroups, supplemental_groups);
	}

	if (argc == 2 && strcmp(argv[1], "-v") == 0)
	{
		/* This is used by install.sh to test if the filer
		 * compiled OK. Do this test before gtk_init so that
		 * we don't need an X server to install.
		 */
		fprintf(stderr, "ROX-Filer %s\n", VERSION);
		fprintf(stderr, _(COPYING));
		show_features();
		return EXIT_SUCCESS;
	}

	/* The idea here is to convert the command-line arguments
	 * into a SOAP RPC.
	 * We attempt to invoke the call on an already-running copy of
	 * the filer if possible, or execute it ourselves if not.
	 */
	rpc = soap_new(&body);

	/* Note: must do this before checking our options,
	 * otherwise we report an error for Gtk's options.
	 */
	add_default_styles();
	gtk_init(&argc, &argv);

	/* Process each option in turn */
	while (1)
	{
		int	c;
#ifdef HAVE_GETOPT_LONG
		int	long_index;
		c = getopt_long(argc, argv, SHORT_OPS,
				long_opts, &long_index);
#else
		c = getopt(argc, argv, SHORT_OPS);
#endif

		if (c == EOF)
			break;		/* No more options */
		
		switch (c)
		{
			case 'n':
				new_copy = TRUE;
				break;
			case 'o':
				override_redirect = TRUE;
				break;
			case 'v':
				fprintf(stderr, "ROX-Filer %s\n", VERSION);
				fprintf(stderr, _(COPYING));
				show_features();
				return EXIT_SUCCESS;
			case 'h':
				fprintf(stderr, _(HELP));
				fprintf(stderr, _(SHORT_ONLY_WARNING));
				return EXIT_SUCCESS;
			case 'D':
			case 'd':
		        case 'x':
				/* Argument is a path */
				tmp = pathdup(VALUE);
				soap_add(body,
					c == 'D' ? "CloseDir" :
					c == 'd' ? "OpenDir" :
					c == 'x' ? "Examine" : "Unknown",
					"Filename", tmp,
					NULL, NULL);
				g_free(tmp);
				break;
			case 's':
				tmp = g_strdup(VALUE);
				slash = strrchr(tmp, '/');
				if (slash)
				{
					*slash = '\0';
					slash++;
					dir = pathdup(tmp);
				}
				else
				{
					slash = tmp;
					dir = pathdup(".");
				}

				soap_add(body, "Show",
					"Directory", dir,
					"Leafname", slash);
				g_free(tmp);
				g_free(dir);
				break;
			case 'l':
			case 'r':
			case 't':
			case 'b':
				/* Argument is a leaf (or starts with /) */
				soap_add(body, "Panel", "Name", VALUE,
					 "Side", c == 'l' ? "Left" :
						 c == 'r' ? "Right" :
						 c == 't' ? "Top" :
						 c == 'b' ? "Bottom" :
						 "Unkown");
				break;
			case 'p':
				soap_add(body, "Pinboard",
						"Name", VALUE, NULL, NULL);
				break;
			case 'u':
				show_user = TRUE;
				break;
		        case 'm':
				{
					MIME_type *type;
					type_init();
					type = type_get_type(VALUE);
					printf("%s/%s\n", type->media_type,
							  type->subtype);
				}
				return EXIT_SUCCESS;
			case 'c':
				client_id = g_strdup(VALUE);
				break;
			case 'R':
				soap_rpc = xmlParseFile("-");
				if (!soap_rpc)
					g_error("Invalid XML in RPC");
				break;
			default:
				printf(_(USAGE));
				return EXIT_FAILURE;
		}
	}

	tooltips = gtk_tooltips_new();

	if (euid == 0 || show_user)
		show_user_message = g_strdup_printf( _("Running as user '%s'"), 
				user_name(euid));
	
	/* Add each remaining (non-option) argument to the list of files
	 * to run.
	 */
	i = optind;
	while (i < argc)
	{
		tmp = pathdup(argv[i++]);

		soap_add(body, "Run", "Filename", tmp, NULL, NULL);

		g_free(tmp);
	}

	if (soap_rpc)
	{
		if (body->xmlChildrenNode)
			g_error("Can't use -R with other options - sorry!");
		xmlFreeDoc(rpc);
		body = NULL;
		rpc = soap_rpc;
	}
	else if (!body->xmlChildrenNode)
	{
		/* The user didn't request any action. Open the current
		 * directory.
		 */
		guchar	*dir;

		dir = g_get_current_dir();
		soap_add(body, "OpenDir", "Filename", dir, NULL, NULL);
		g_free(dir);
	}

	option_add_int("dnd_no_hostnames", 1, NULL);

	/* Try to send the request to an already-running copy of the filer */
	gui_support_init();
	if (remote_init(rpc, new_copy))
		return EXIT_SUCCESS;	/* It worked - exit */

	/* Put ourselves into the background (so 'rox' always works the
	 * same, whether we're already running or not).
	 * Not for -n, though (helps when debugging).
	 */
	if (!new_copy)
	{
		pid_t child;

		child = fork();
		if (child > 0)
			_exit(0);	/* Parent exits */
		/* Otherwise we're the child (or an error occurred - ignore
		 * it!).
		 */
	}

	/* Close stdin. We don't need it, and it can cause problems if
	 * a child process wants a password, etc...
	 */
	{
		int fd;
		fd = open("/dev/null", O_RDONLY);
		if (fd > 0)
		{
			close(0);
			dup2(fd, 0);
			close(fd);
		}
	}
	
	/* Initialize the rest of the filer... */
	
	pixmaps_init();

	dnd_init();
	bind_init();
	dir_init();
	diritem_init();
	menu_init();
	minibuffer_init();
	filer_init();
	toolbar_init();
	display_init();
	mount_init();
	type_init();
	action_init();

	icon_init();
	pinboard_init();
	panel_init();

	/* Let everyone update */
	options_notify();

	/* When we get a signal, we can't do much right then. Instead,
	 * we send a char down this pipe, which causes the main loop to
	 * deal with the event next time we're idle.
	 */
	pipe(wakeup_pipe);
	close_on_exec(wakeup_pipe[0], TRUE);
	close_on_exec(wakeup_pipe[1], TRUE);
	gdk_input_add(wakeup_pipe[0], GDK_INPUT_READ, wake_up_cb, NULL);
	to_wakeup_pipe = wakeup_pipe[1];

	/* If the pipe is full then we're going to get woken up anyway... */
	set_blocking(to_wakeup_pipe, FALSE);

	/* Let child processes die */
	act.sa_handler = child_died;
	sigemptyset(&act.sa_mask);
	act.sa_flags = SA_NOCLDSTOP;
	sigaction(SIGCHLD, &act, NULL);

	/* Ignore SIGPIPE - check for EPIPE errors instead */
	act.sa_handler = SIG_IGN;
	sigemptyset(&act.sa_mask);
	act.sa_flags = 0;
	sigaction(SIGPIPE, &act, NULL);

	/* Set up session managament if available */
	session_init(client_id);
	g_free(client_id);
		
	/* Finally, execute the request */
	reply = run_soap(rpc);
	xmlFreeDoc(rpc);
	if (reply)
	{
		/* Write the result, if any, to stdout */
		save_xml_file(reply, "-");
		xmlFreeDoc(reply);
	}

	/* Enter the main loop, processing events until all our windows
	 * are closed.
	 */
	if (number_of_windows > 0)
		gtk_main();

	return EXIT_SUCCESS;
}

/* Register a function to be called when process number 'child' dies. */
void on_child_death(gint child, CallbackFn callback, gpointer data)
{
	Callback	*cb;

	g_return_if_fail(callback != NULL);

	cb = g_new(Callback, 1);

	cb->callback = callback;
	cb->data = data;

	g_hash_table_insert(death_callbacks, GINT_TO_POINTER(child), cb);
}

/****************************************************************
 *			INTERNAL FUNCTIONS			*
 ****************************************************************/

static void show_features(void)
{
	g_printerr("\n-- %s --\n\n", _("features set at compile time"));
	g_printerr("%s... %s\n", _("Large File Support"),
#ifdef LARGE_FILE_SUPPORT
		_("Yes")
#else
		_("No")
#endif
		);
	g_printerr("%s... %s\n", _("Old VFS support"),
#ifdef HAVE_LIBVFS
		_("Yes")
#else
# ifdef LARGE_FILE_SUPPORT
		_("No (incompatible with Large File Support)")
# else
		_("No (couldn't find a valid libvfs)")
# endif
#endif
		);
	g_printerr("%s... %s\n", _("Gtk+-2.0 support"),
#ifdef GTK2
		_("Yes")
#else
		_("No (using Gtk+-1.2 instead)")
#endif
		);
	g_printerr("%s... %s\n", _("Save thumbnails"),
#ifdef GTK2
		_("Yes (using Gtk+-2.0)")
#elif defined(HAVE_LIBPNG)
		_("Yes (using libpng)")
#else
		_("No (needs libpng or Gtk+-2.0)")
#endif
		);
	g_printerr("%s... %s\n", _("Character set translations"),
#ifdef GTK2
			_("Yes (using Gtk+-2.0)")
#else
# ifdef HAVE_ICONV_H
			_("Yes")
# else
			_("No (needs libiconv or Gtk+-2.0)")
# endif
#endif
		  );
}

static void soap_add(xmlNodePtr body,
			   xmlChar *function,
			   xmlChar *arg1_name, xmlChar *arg1_value,
			   xmlChar *arg2_name, xmlChar *arg2_value)
{
	xmlNodePtr node;
	xmlNs *rox;

	rox = xmlSearchNsByHref(body->doc, body, ROX_NS);
	
	node = xmlNewChild(body, rox, function, NULL);

	if (arg1_name)
	{
		xmlNewTextChild(node, rox, arg1_name, arg1_value);
		if (arg2_name)
			xmlNewTextChild(node, rox, arg2_name, arg2_value);
	}
}

/* This is called as a signal handler; simply ensures that
 * child_died_callback() will get called later.
 */
static void child_died(int signum)
{
	child_died_flag = TRUE;
	write(to_wakeup_pipe, "\0", 1);	/* Wake up! */
}

static void child_died_callback(void)
{
	int	    	status;
	gint	    	child;

	child_died_flag = FALSE;

	/* Find out which children exited and allow them to die */
	do
	{
		Callback	*cb;

		child = waitpid(-1, &status, WNOHANG);

		if (child == 0 || child == -1)
			return;

		cb = g_hash_table_lookup(death_callbacks,
				GINT_TO_POINTER(child));
		if (cb)
		{
			cb->callback(cb->data);
			g_hash_table_remove(death_callbacks,
					GINT_TO_POINTER(child));
		}

	} while (1);
}

#define BUFLEN 40
/* When data is written to_wakeup_pipe, this gets called from the event
 * loop some time later. Useful for getting out of signal handlers, etc.
 */
static void wake_up_cb(gpointer data, gint source, GdkInputCondition condition)
{
	char buf[BUFLEN];

	read(source, buf, BUFLEN);
	
	if (child_died_flag)
		child_died_callback();
}
