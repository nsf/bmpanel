/*
 * Copyright (C) 2008 nsf
 */

#ifndef BMPANEL_BMPANEL_H
#define BMPANEL_BMPANEL_H

#include <X11/Xlib.h>
#include <X11/extensions/Xrender.h>
#include <Imlib2.h>
#include "common.h"

struct task {
	struct task *next;
	char *name;
	Window win;
	Imlib_Image icon;
	int posx;
	int width;
	int desktop;
	uint focused;
	uint iconified;
};

struct desktop {
	struct desktop *next;
	char *name;
	int posx;
	int width;
	uint focused;
};

struct tray {
	struct tray *next;
	int posx;
	int width;
	Window win;
};

struct panel {
	Window win;
	struct task *tasks;
	struct desktop *desktops;
	struct theme *theme;
	struct tray *trayicons;
	Window trayselowner;
	Pixmap bgpix;

	Picture rootpix;
};

#endif
