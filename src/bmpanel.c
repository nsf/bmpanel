/*
 * Copyright (C) 2008 nsf
 */

#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <string.h>
#include <signal.h>
#include <sys/time.h>
#include <sys/types.h>
#include <unistd.h>
#include <ev.h>
#include <X11/Xatom.h>
#include <X11/Xutil.h>
#include "logger.h"
#include "theme.h"
#include "render.h"
#include "bmpanel.h"

/**************************************************************************
  GLOBALS
**************************************************************************/

#ifndef PREFIX
#define PREFIX "/usr"
#endif

#define HOME_THEME_PATH "/.bmpanel/themes/"
#define SHARE_THEME_PATH PREFIX "/share/bmpanel/themes/"

#define TRAY_REQUEST_DOCK 0

enum atom_cnames {
	XATOM_WM_STATE,
	XATOM_NET_DESKTOP_NAMES,
	XATOM_NET_WM_STATE,
	XATOM_NET_ACTIVE_WINDOW,
	XATOM_NET_WM_NAME,
	XATOM_NET_WORKAREA,
	XATOM_NET_WM_ICON,
	XATOM_NET_WM_VISIBLE_NAME,
	XATOM_NET_WM_STATE_SKIP_TASKBAR,
	XATOM_NET_WM_STATE_SHADED,
	XATOM_NET_WM_DESKTOP,
	XATOM_NET_MOVERESIZE_WINDOW,
	XATOM_NET_WM_WINDOW_TYPE,
	XATOM_NET_WM_WINDOW_TYPE_DOCK,
	XATOM_NET_WM_STRUT,
	XATOM_NET_CLIENT_LIST,
	XATOM_NET_NUMBER_OF_DESKTOPS,
	XATOM_NET_CURRENT_DESKTOP,
	XATOM_NET_SYSTEM_TRAY_OPCODE,
	XATOM_UTF8_STRING,
	XATOM_COUNT
};

static char *atom_names[] = {
	"WM_STATE",
	"_NET_DESKTOP_NAMES",
	"_NET_WM_STATE",
	"_NET_ACTIVE_WINDOW",
	"_NET_WM_NAME",
	"_NET_WORKAREA",
	"_NET_WM_ICON",
	"_NET_WM_VISIBLE_NAME",
	"_NET_WM_STATE_SKIP_TASKBAR",
	"_NET_WM_STATE_SHADED",
	"_NET_WM_DESKTOP",
	"_NET_MOVERESIZE_WINDOW",
	"_NET_WM_WINDOW_TYPE",
	"_NET_WM_WINDOW_TYPE_DOCK",
	"_NET_WM_STRUT",
	"_NET_CLIENT_LIST",
	"_NET_NUMBER_OF_DESKTOPS",
	"_NET_CURRENT_DESKTOP",
	"_NET_SYSTEM_TRAY_OPCODE",
	"UTF8_STRING"
};

static struct {
	Display *display;
	int screen;
	int screen_width;
	int screen_height;
	
	int wa_x;
	int wa_y;
	int wa_w;
	int wa_h;

	Visual *visual;
	Window root;
	Atom atoms[XATOM_COUNT];
	Atom trayselatom;
} X;

static struct panel P;

static int commence_taskbar_redraw;
static int commence_panel_redraw;
static int commence_switcher_redraw;

static void cleanup();

/**************************************************************************
  X error handlers
**************************************************************************/

static int X_error_handler(Display *dpy, XErrorEvent *error)
{
	char buf[1024];
	XGetErrorText(dpy, error->error_code, buf, sizeof(buf));
	LOG_WARNING("X error: %s (resource id: %d)", buf, error->resourceid);
	return 0;
}

static int X_io_error_handler(Display *dpy)
{
	LOG_WARNING("fatal error, connection to X server lost? cleaning up");
	cleanup();
	return 0;
}

/**************************************************************************
  imlib2 font util
**************************************************************************/

static void append_font_path_to_imlib()
{
	int fonts, i;
	char **fontpaths = XGetFontPath(X.display, &fonts);
	
	for (i = 0; i < fonts; ++i)
		imlib_add_path_to_font_path(fontpaths[i]);

	XFreeFontPath(fontpaths);
}

/**************************************************************************
  window properties
**************************************************************************/

static void *get_prop_data(Window win, Atom prop, Atom type, int *items)
{
	Atom type_ret;
	int format_ret;
	unsigned long items_ret;
	unsigned long after_ret;
	uchar *prop_data;

	prop_data = 0;

	XGetWindowProperty(X.display, win, prop, 0, 0x7fffffff, False,
			type, &type_ret, &format_ret, &items_ret,
			&after_ret, &prop_data);
	if (items)
		*items = items_ret;

	return prop_data;
}

static int get_prop_int(Window win, Atom at)
{
	int num = 0;
	unsigned long *data;

	data = get_prop_data(win, at, XA_CARDINAL, 0);
	if (data) {
		num = *data;
		XFree(data);
	}
	return num;
}

static int get_window_desktop(Window win)
{
	return get_prop_int(win, X.atoms[XATOM_NET_WM_DESKTOP]);
}

static int is_window_hidden(Window win)
{
	unsigned long *data;
	int ret = 0;
	int num;

	data = get_prop_data(win, X.atoms[XATOM_NET_WM_STATE], XA_ATOM, &num);
	if (!data)
		return 0;

	while (num) {
		num--;
		if (data[num] == X.atoms[XATOM_NET_WM_STATE_SKIP_TASKBAR])
			ret = 1;
	}
	XFree(data);

	return ret;
}

static int is_window_iconified(Window win)
{
	unsigned long *data;
	int ret = 0;

	data = get_prop_data(win, X.atoms[XATOM_WM_STATE], 
	     		X.atoms[XATOM_WM_STATE], 0);
	if (data) {
		if (data[0] == IconicState) {
			ret = 1;
		}
		XFree(data);
	}
	return ret;
}

static Imlib_Image get_window_icon(Window win)
{
	if (P.theme->taskbar.icon_w == 0 &&
	    P.theme->taskbar.icon_h == 0)
	{
		return 0;
	}

	Imlib_Image ret = 0;

	int num = 0;
	uint32_t *data = get_prop_data(win, X.atoms[XATOM_NET_WM_ICON], 
					XA_CARDINAL, &num);
	if (data) {
		uint32_t *datal = data;
		uint32_t w,h;
		w = *datal++;
		h = *datal++;
		ret = imlib_create_image_using_copied_data(w,h,datal);
		imlib_context_set_image(ret);
		imlib_image_set_has_alpha(1);
		XFree(data);
	}

	if (!ret) {
	        XWMHints *hints = XGetWMHints(X.display, win);
		if (hints) {
			if (hints->flags & IconPixmapHint) {
				Pixmap pix;
				int x = 0, y = 0;
				uint w = 0, h = 0, d = 0, bw = 0;
				XGetGeometry(X.display, hints->icon_pixmap, &pix, 
						&x, &y, &w, &w, &bw, &d);
	
				imlib_context_set_drawable(hints->icon_pixmap);
				ret = imlib_create_image_from_drawable(hints->icon_mask, 
								x, y, w, h, 1);
			}
	        	XFree(hints);
		}
	}

	/* if we can't get icon, set default and return */
	if (!ret) {
		ret = P.theme->taskbar.default_icon_img;
		return ret;
	}

	/* well, we have our icon, lets resize it for faster rendering */
	int w,h;
	imlib_context_set_image(ret);
	w = imlib_image_get_width();
	h = imlib_image_get_height();
	Imlib_Image sizedicon = imlib_create_cropped_scaled_image(0, 0, w, h, 
			P.theme->taskbar.icon_w, P.theme->taskbar.icon_h);
	imlib_context_set_image(sizedicon);
	imlib_image_set_has_alpha(1);
	imlib_context_set_image(ret);
	imlib_free_image();

	return sizedicon;
}

static char *get_window_name(Window win)
{
	char *ret, *name = 0;
	name = get_prop_data(win, X.atoms[XATOM_NET_WM_VISIBLE_NAME], X.atoms[XATOM_UTF8_STRING], 0);
	if (!name)
		name = get_prop_data(win, X.atoms[XATOM_NET_WM_NAME], X.atoms[XATOM_UTF8_STRING], 0);
	if (!name)
		name = get_prop_data(win, XA_WM_NAME, XA_STRING, 0);
	if (name) {
		ret = xstrdup(name);
		XFree(name);
	} else
		ret = xstrdup("<unknown>");
	return ret;
}

/**************************************************************************
  creating panel window
**************************************************************************/

static Window create_panel_window(uint placement, int h)
{
	Window win;
	int y = 0;
	uint32_t strut[4] = {0,0,0,h};
	uint32_t tmp;

	if (placement == PLACE_TOP) {
		y = X.wa_y;
		strut[3] = 0;
		strut[2] = h;
	} else if (placement == PLACE_BOTTOM)
		y = X.wa_h - h;

	win = XCreateWindow(X.display, X.root, X.wa_x, y, X.wa_w, h, 0, 
			CopyFromParent, InputOutput, X.visual, 0, 0);
	XSelectInput(X.display, win, ButtonPressMask | ExposureMask | StructureNotifyMask);

	/* get our place on desktop */
	XChangeProperty(X.display, win, X.atoms[XATOM_NET_WM_STRUT], XA_CARDINAL, 32,
			PropModeReplace, (uchar*)&strut, 4);

	/* we want to be on all desktops */
	tmp = 0xFFFFFFFF;
	XChangeProperty(X.display, win, X.atoms[XATOM_NET_WM_DESKTOP], XA_CARDINAL, 32,
			PropModeReplace, (uchar*)&tmp, 1);

	/* we're panel! */
	tmp = X.atoms[XATOM_NET_WM_WINDOW_TYPE_DOCK];
	XChangeProperty(X.display, win, X.atoms[XATOM_NET_WM_WINDOW_TYPE], XA_ATOM, 32,
			PropModeReplace, (uchar*)&tmp, 1);
	
	/* place window on it's position */
	XSizeHints size_hints;
	size_hints.flags = PPosition;
	XSetWMNormalHints(X.display, win, &size_hints);
	/*
	XChangeProperty(X.display, win, XA_WM_NORMAL_HINTS, XA_WM_SIZE_HINTS, 32,
			PropModeReplace, (uchar *)&size_hints,
			sizeof(XSizeHints) / 4);
	*/
	
	XMapWindow(X.display, win);
	return win;
}


/**************************************************************************
  desktop management
**************************************************************************/

static int get_active_desktop()
{
	return get_prop_int(X.root, X.atoms[XATOM_NET_CURRENT_DESKTOP]);
}

static void set_active_desktop(int d)
{
	int i = 0;
	struct desktop *iter = P.desktops;
	while (iter) {
		iter->focused = (i == d);
		iter = iter->next;
		i++;
	}
}

static int get_number_of_desktops()
{
	return get_prop_int(X.root, X.atoms[XATOM_NET_NUMBER_OF_DESKTOPS]);
}

static void free_desktops()
{
	struct desktop *iter, *next;
	iter = P.desktops;
	while (iter) {
		next = iter->next;
		xfree(iter->name);
		xfree(iter);
		iter = next;
	}
	P.desktops = 0;
}

static void rebuild_desktops()
{
	/* 
	 * This function is not optimal. It frees all the desktops and create them again 
	 * Anyway, if you change number of your desktops or desktop names in real time, you are
	 * probably using wrong software, or maybe you were born in wrong world. 
	 */
	free_desktops();

	struct desktop *last = P.desktops, *d = 0;
	int desktopsnum = get_number_of_desktops();
	int activedesktop = get_active_desktop();
	int i, len;

	char *name, *names;
	names = name = get_prop_data(X.root, X.atoms[XATOM_NET_DESKTOP_NAMES], 
			X.atoms[XATOM_UTF8_STRING], 0);

	for (i = 0; i < desktopsnum; ++i) {
		d = XMALLOCZ(struct desktop, 1);
		if (names)
			d->name = xstrdup(name);
		else {
			char buf[16];
			snprintf(buf, sizeof(buf), "%d", i+1);
			d->name = xstrdup(buf);
		}
		d->focused = (i == activedesktop);
		if (!last) {
			P.desktops = d;
			last = d;
		} else {
			last->next = d;
			last = d;
		}

		if (names) {
			len = strlen(name);
			name += len + 1;
		}
	}

	if (names)
		XFree(names);
}

static void switch_desktop(int d)
{
	XClientMessageEvent e;

	if (d >= get_number_of_desktops())
		return;

	e.type = ClientMessage;
	e.window = X.root;
	e.message_type = X.atoms[XATOM_NET_CURRENT_DESKTOP];
	e.format = 32;
	e.data.l[0] = d;
	e.data.l[1] = 0;
	e.data.l[2] = 0;
	e.data.l[3] = 0;
	e.data.l[4] = 0;
	
	XSendEvent(X.display, X.root, False, SubstructureNotifyMask | 
			SubstructureRedirectMask, (XEvent*)&e);
}


/**************************************************************************
  task management
**************************************************************************/

/*
static void activate_task(struct task *t)
{
	XClientMessageEvent e;

	e.window = t->win;
	e.type = X.atoms[XATOM_NET_ACTIVE_WINDOW];
	e.format = 32;
	e.data.l[0] = 2;
	e.data.l[1] = CurrentTime;
	e.data.l[2] = 0;
	e.data.l[3] = 0;
	e.data.l[4] = 0;

	XSendEvent(X.display, X.root, False, SubstructureNotifyMask |
			SubstructureRedirectMask, (XEvent*)&e);
}
*/

static void free_tasks()
{
	struct task *iter, *next;
	iter = P.tasks;
	while (iter) {
		next = iter->next;
		if (iter->icon && iter->icon != P.theme->taskbar.default_icon_img) {
			imlib_context_set_image(iter->icon);
			imlib_free_image();
		}
		xfree(iter->name);
		xfree(iter);
		iter = next;
	}
}

static void add_task(Window win, uint focused)
{
	if (is_window_hidden(win))
		return;

	struct task *t = XMALLOCZ(struct task, 1);
	t->win = win;
	t->name = get_window_name(win); 
	t->desktop = get_window_desktop(win);
	t->iconified = is_window_iconified(win); 
	t->focused = focused;
	t->icon = get_window_icon(win);

	XSelectInput(X.display, win, PropertyChangeMask | 
			FocusChangeMask | StructureNotifyMask);

	/* put task in list */
	struct task *iter = P.tasks;
	if (!iter || iter->desktop > t->desktop) {
		t->next = P.tasks;
		P.tasks = t;
		return;
	}

	for (;;) {
		if (!iter->next || iter->next->desktop > t->desktop) {
			t->next = iter->next;
			iter->next = t;
			return;
		}
		iter = iter->next;
	}
}

static void del_task(Window win)
{
	struct task *prev = 0, *next, *iter = P.tasks;
	while (iter) {
		next = iter->next;
		if (iter->win == win) {
			/* TODO: generic icon */
			if (iter->icon && iter->icon != P.theme->taskbar.default_icon_img) {
				imlib_context_set_image(iter->icon);
				imlib_free_image();
			}
			xfree(iter->name);
			xfree(iter);
			if (!prev)
				P.tasks = next;
			else
				prev->next = next;
			return;
		}
		prev = iter;
		iter = next;
	}
}

static struct task *find_task(Window win)
{
	struct task *iter = P.tasks;
	while (iter) {
		if (iter->win == win)
			return iter;
		iter = iter->next;
	}
	return 0;
}

static void update_tasks()
{
	Window *wins, focuswin;
	int num, i, j, rev;

	XGetInputFocus(X.display, &focuswin, &rev);

	wins = get_prop_data(X.root, X.atoms[XATOM_NET_CLIENT_LIST], XA_WINDOW, &num);

	/* if there are no client list? we are in not NETWM compliant desktop? */
	/* if (!wins) return; */

	/* if one or more windows in my list are not in _NET_CLIENT_LIST, delete them */
	struct task *next, *iter = P.tasks;
	while (iter) {
		iter->focused = (focuswin == iter->win);
		next = iter->next;
		for (j = 0; j < num; ++j) {
			if (iter->win == wins[j])
				goto nodelete;
		}
		del_task(iter->win);
		commence_taskbar_redraw = 1;
nodelete:
		iter = next;
	}

	/* for each window in _NET_CLIENT_LIST, check if it is in out list, if
	   it's not, add it */
	for (i = 0; i < num; ++i) {
		/* skip panel */
		if (wins[i] == P.win)
			continue;

		if (!find_task(wins[i])) {
			add_task(wins[i], (wins[i] == focuswin));
			commence_taskbar_redraw = 1;
		}
	}
	XFree(wins);
}

/**************************************************************************
  systray functions
**************************************************************************/

static void init_tray()
{
	char systray_atom[32];
	snprintf(systray_atom, sizeof(systray_atom), "_NET_SYSTEM_TRAY_S%u", X.screen);

	X.trayselatom = XInternAtom(X.display, systray_atom, False);
	Window old_owner = XGetSelectionOwner(X.display, X.trayselatom);
	if (old_owner != 0) {
		LOG_WARNING("selection already have it's owner, disabling tray");
		theme_remove_element(P.theme, 't');
		return;
	}

	P.trayselowner = XCreateWindow(X.display, P.win, 0, 0, 1, 1, 0, 
			CopyFromParent, InputOnly, X.visual, 0, 0);
	XSetSelectionOwner(X.display, X.trayselatom, P.trayselowner, CurrentTime);

	/* done with selection, now we will inform clients */

	XEvent e;
	e.xclient.type = ClientMessage;
	e.xclient.message_type = XInternAtom(X.display, "MANAGER", False);
	e.xclient.display = X.display;
	e.xclient.window = X.root;
	e.xclient.format = 32;
	e.xclient.data.l[0] = CurrentTime; /* timestamp */
	e.xclient.data.l[1] = X.trayselatom; /* manager selection atom */
	e.xclient.data.l[2] = P.trayselowner; /* the window owning the selection */
	e.xclient.data.l[3] = 0l; /* selection specific data */
	e.xclient.data.l[4] = 0l; /* selection specific data */

	XSendEvent(X.display, X.root, False, StructureNotifyMask, &e);

	/* that's it */
}

static void shutdown_tray()
{
	XSetSelectionOwner(X.display, X.trayselatom, None, CurrentTime);
}

static void add_tray_icon(Window win)
{
	/* reparent, do all embed stuff */
	struct tray *t = XMALLOCZ(struct tray, 1);
	t->win = win;
	
	/* listen necessary events */
	XSelectInput(X.display, t->win, StructureNotifyMask | PropertyChangeMask);

	XSetWindowBorderWidth(X.display, t->win, 0);
	XReparentWindow(X.display, t->win, P.win, 0, 0); 
	XMapRaised(X.display, t->win);

	struct tray *iter = P.trayicons;
	if (!iter) {
		P.trayicons = t;
		return;
	}

	for (;;) {
		if (!iter->next) {
			iter->next = t;
			return;
		}
		iter = iter->next;
	}
}

static struct tray *find_tray_icon(Window win)
{
	struct tray *iter = P.trayicons;
	while (iter) {
		if (iter->win == win)
			return iter;
		iter = iter->next;
	}
	return 0;
}

static void del_tray_icon(Window win)
{
	struct tray *prev = 0, *next, *t = P.trayicons;
	while (t) {
		next = t->next;
		if (t->win == win)
			break;
		prev = t;
		t = t->next;
	}
	if (!t)
		return;

	xfree(t);
	if (!prev)
		P.trayicons = next;
	else 
		prev->next = next;
}

static void free_tray_icons()
{
	struct tray *iter, *next;
	iter = P.trayicons;
	while (iter) {
		next = iter->next;
		XReparentWindow(X.display, X.root, iter->win, 0, 0);
		xfree(iter);
		iter = next;
	}
}

/**************************************************************************
  X message handlers
**************************************************************************/

static void handle_client_message(XClientMessageEvent *e)
{
	if (e->message_type == X.atoms[XATOM_NET_SYSTEM_TRAY_OPCODE] &&
	    e->data.l[1] == TRAY_REQUEST_DOCK) 
	{
		add_tray_icon(e->data.l[2]);		
		render_update_panel_positions(&P);
		commence_panel_redraw = 1;
	}
}

static void handle_reparent_notify(Window win, Window parent)
{
	struct tray *t = find_tray_icon(win);
	if (!t)
		return;
	if (P.win != parent) {
		del_tray_icon(win);
		render_update_panel_positions(&P);
		commence_panel_redraw = 1;
	}
}

static void handle_property_notify(Window win, Atom a)
{
	/* global changes */
	if (win == X.root) {
		/* user or WM reconfigured it's desktops */
		if (a == X.atoms[XATOM_NET_NUMBER_OF_DESKTOPS] ||
		    a == X.atoms[XATOM_NET_DESKTOP_NAMES])
		{
			rebuild_desktops();
			render_update_panel_positions(&P);
			commence_panel_redraw = 1;
		}

		/* user or WM switched desktop */
		if (a == X.atoms[XATOM_NET_CURRENT_DESKTOP]) 
		{
			set_active_desktop(get_active_desktop());
			render_update_panel_positions(&P);
			commence_switcher_redraw = 1;
			commence_taskbar_redraw = 1;
		}

		/* updates in client list */
		if (a == X.atoms[XATOM_NET_CLIENT_LIST])
		{
			update_tasks();
			if (commence_taskbar_redraw)
				render_update_panel_positions(&P);
		}
	}

	/* now it's time for per-window changes */
	struct task *t = find_task(win);
	if (!t) {
		render_present();
		return;
	}

	/* widow changed it's desktop */
	if (a == X.atoms[XATOM_NET_WM_DESKTOP]) {
		t->desktop = get_window_desktop(win);
		render_update_panel_positions(&P);
		commence_switcher_redraw = 1;
		commence_taskbar_redraw = 1;
	}
	
	/* window changed it's visible name or name */
	if (a == X.atoms[XATOM_NET_WM_NAME] || 
	    a == X.atoms[XATOM_NET_WM_VISIBLE_NAME]) 
	{
		xfree(t->name);
		t->name = get_window_name(t->win);
		commence_taskbar_redraw = 1;
	}

	if (a == X.atoms[XATOM_WM_STATE]) {
		t->iconified = is_window_iconified(t->win);
		commence_taskbar_redraw = 1;
	}
}

static void handle_button(int x, int y, int button)
{
	int adesk = get_active_desktop();

	/* second button iconize all windows, we want to see our desktop */
	if (button == 3) {
		struct task *iter = P.tasks;
		while (iter) {
			if (iter->desktop == adesk) {
				iter->iconified = 1;
				iter->focused = 0;
				XIconifyWindow(X.display, iter->win, X.screen);
			}
			iter = iter->next;
		}
		commence_taskbar_redraw = 1;
		return;
	}

	/* check desktops */
	int desk = 0;
	struct desktop *diter = P.desktops;
	while (diter) {
		if (x > diter->posx && x < diter->posx + diter->width && !diter->focused) {
			if (desk != adesk)
				switch_desktop(desk);
			/* redraw will be in property notify */
			break;
		}
		diter = diter->next;
		desk++;
	}

	/* check taskbar */
	struct task *iter = P.tasks;
	while (iter) {
		if (iter->desktop == adesk && 
		    x > iter->posx && 
		    x < iter->posx + iter->width) 
		{
			if (iter->iconified) {
				iter->iconified = 0;
				iter->focused = 1;
				XMapWindow(X.display, iter->win);
			} else {
				if (iter->focused) {
					iter->iconified = 1;
					iter->focused = 0;
					XIconifyWindow(X.display, iter->win, X.screen);
				} else {
					iter->focused = 1;
					XRaiseWindow(X.display, iter->win);
					XSetInputFocus(X.display, iter->win, RevertToNone, CurrentTime);

					/* send _NET_ACTIVE_WINDOW */
					/* activate_task(iter); */
				}
			}
			commence_taskbar_redraw = 1;
		} else if (iter->desktop == adesk) {
			iter->focused = 0;
		}
		iter = iter->next;
	}
}

static void handle_focusin(Window win)
{
	struct task *iter = P.tasks;
	while (iter) {
		iter->focused = (iter->win == win);
		iter = iter->next;
	}
}

/**************************************************************************
  initialization
**************************************************************************/

static void initX()
{
	/* open connection to X server */
	X.display = XOpenDisplay(0);
	if (!X.display)
		LOG_ERROR("failed connect to X server");
	XSetErrorHandler(X_error_handler);
	XSetIOErrorHandler(X_io_error_handler);

	/* useful variables */
	X.screen 	= DefaultScreen(X.display);
	X.screen_width 	= DisplayWidth(X.display, X.screen);
	X.screen_height	= DisplayHeight(X.display, X.screen);
	X.visual 	= DefaultVisual(X.display, X.screen);
	X.root 		= RootWindow(X.display, X.screen);
	X.wa_x 		= 0;
	X.wa_y 		= 0;
	X.wa_w 		= X.screen_width;
	X.wa_h 		= X.screen_height;
	
	/* get internal atoms */
	XInternAtoms(X.display, atom_names, XATOM_COUNT, False, X.atoms);
	XSelectInput(X.display, X.root, PropertyChangeMask);

	append_font_path_to_imlib();

	/* get workarea */
	int32_t *workarea = get_prop_data(X.root, X.atoms[XATOM_NET_WORKAREA], XA_CARDINAL, 0);
	if (workarea) {
		X.wa_x = workarea[0];
		X.wa_y = workarea[1];
		X.wa_w = workarea[2];
		X.wa_h = workarea[3];
		XFree(workarea);	
	}
}

static void initP(const char *theme)
{
	char dirbuf[4096];
	/* first try to find theme in user home dir */
	snprintf(dirbuf, sizeof(dirbuf), "%s%s%s", 
			getenv("HOME"), 
			HOME_THEME_PATH,
			theme);
	P.theme = load_theme(dirbuf);
	if (P.theme) goto validation;

	/* now try share dir */
	snprintf(dirbuf, sizeof(dirbuf), "%s%s",
			SHARE_THEME_PATH,
			theme);
	P.theme = load_theme(dirbuf);
	if (P.theme) goto validation;

	/* and last try is absolute dir */
	P.theme = load_theme(theme);
	if (!P.theme)
		LOG_ERROR("failed to load theme: %s", theme);

validation:
	/* validate theme */
	if (!theme_is_valid(P.theme))
		LOG_ERROR("invalid theme: %s", theme);

	/* create panel window */
	P.win = create_panel_window(P.theme->placement, P.theme->height);
	
	/* set window bg */
	Pixmap tile, mask;
	imlib_context_set_display(X.display);
	imlib_context_set_visual(X.visual);
	imlib_context_set_drawable(P.win);
	imlib_context_set_image(P.theme->tile_img);
	imlib_render_pixmaps_for_whole_image(&tile, &mask);
	P.bgpix = tile;
	XSetWindowBackgroundPixmap(X.display, P.win, P.bgpix);

	/* init tray if needed */
	if (is_element_in_theme(P.theme, 't'))
		init_tray();
}

/**************************************************************************
  cleanup
**************************************************************************/

static void freeP()
{
	free_tray_icons();
	if (is_element_in_theme(P.theme, 't'))
		shutdown_tray();
	free_theme(P.theme);
	free_tasks();
	free_desktops();
	imlib_free_pixmap_and_mask(P.bgpix);
}

static void cleanup()
{
	shutdown_render();
	freeP();
	LOG_MESSAGE("cleanup");
}

/**************************************************************************
  libev callbacks
**************************************************************************/

static void xconnection_cb(EV_P_ struct ev_io *w, int revents)
{
	XEvent e;
	while (XPending(X.display)) {
		XNextEvent(X.display, &e);
		switch (e.type) {
		case Expose:
			commence_panel_redraw = 1;
			break;
		case ButtonPress:
			handle_button(e.xbutton.x, e.xbutton.y, e.xbutton.button);
			break;
		case PropertyNotify:
			handle_property_notify(e.xproperty.window, e.xproperty.atom);	
			break;
		case FocusIn:
			handle_focusin(e.xfocus.window);			
			render_update_panel_positions(&P);
			commence_taskbar_redraw = 1;
			break;
		case ClientMessage:
			handle_client_message(&e.xclient);
			break;
		case ReparentNotify:
			handle_reparent_notify(e.xreparent.window, e.xreparent.parent);
			break;
		case DestroyNotify:
			del_tray_icon(e.xdestroywindow.window);
			render_update_panel_positions(&P);
			commence_panel_redraw = 1;
			break;
		default:
			break;
		}
		XSync(X.display, 0);
		
		if (commence_panel_redraw) {
			render_panel(&P);
		} else if (commence_switcher_redraw || commence_taskbar_redraw) {
			if (commence_switcher_redraw) {
				render_switcher(P.desktops);
			}
			if (commence_taskbar_redraw) {
				render_taskbar(P.tasks, P.desktops);
			}
			render_present();
		}
		commence_panel_redraw = 0;
		commence_switcher_redraw = 0;
		commence_taskbar_redraw = 0;
	}
}

static void clock_redraw_cb(EV_P_ struct ev_timer *w, int revents)
{
	render_clock();
	render_present();
}

/**************************************************************************
  signal handlers
**************************************************************************/

static void sighup_handler(int xxx)
{
	LOG_MESSAGE("sighup signal received");
	cleanup();
	xmemleaks();
	exit(0);
}

static void sigint_handler(int xxx)
{
	LOG_MESSAGE("sigint signal received");
	cleanup();
	xmemleaks();
	exit(0);
}

/**************************************************************************
  libev loop and main
**************************************************************************/

static void init_and_start_ev_loop(int xfd)
{
	struct ev_loop *el = ev_default_loop(0);
	ev_timer clock_redraw;
	ev_io xconnection;

	ev_io_init(&xconnection, xconnection_cb, xfd, EV_READ);
	ev_io_start(el, &xconnection);

    	ev_timer_init(&clock_redraw, clock_redraw_cb, 1.0, 1.0);
 	ev_timer_start(el, &clock_redraw);

    	ev_loop(el, 0);
}

int main(int argc, char **argv)
{
	log_attach_callback(log_console_callback);

	if (argc != 2)
		LOG_ERROR("usage: bmpanel <theme>");
	LOG_MESSAGE("starting bmpanel with theme: %s", argv[1]);

	initX();
	initP(argv[1]);
	init_render(P.theme, X.display, P.win);

	signal(SIGHUP, sighup_handler);
	signal(SIGINT, sigint_handler);

	rebuild_desktops();
	update_tasks();

	render_update_panel_positions(&P);
	render_panel(&P);

	init_and_start_ev_loop(ConnectionNumber(X.display));

	cleanup();
	return 0;
}

