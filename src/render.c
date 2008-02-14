/*
 * Copyright (C) 2008 nsf
 */

#include <Imlib2.h>
#include <X11/Xutil.h>
#include <string.h>
#include <time.h>
#include "logger.h"
#include "render.h"

/* backbuffer dimensions */
static uint bbwidth;
static uint bbheight;
static Imlib_Image bb;

static Display *bbdpy;
static Visual *bbvis;
static Drawable bbwin;

static struct theme *theme;

/* temp vars for fast redraws */
static int switcher_pos = 0;
static int switcher_width = 0;
static int clock_pos = 0;
static int clock_width = 0;
static int taskbar_pos = 0;
static int taskbar_width = 0;
static int tray_pos = 0;
static int tray_width = 0;

static int get_image_width(Imlib_Image img)
{
	if (!img)
		return 0;
	imlib_context_set_image(img);
	return imlib_image_get_width();
}

static void draw_image(Imlib_Image img, int ox)
{
	if (!img)
		return;
	int curw = get_image_width(img);
	imlib_context_set_image(bb);
	imlib_blend_image_onto_image(img, 0,
			0, 0, curw, theme->height,
			ox, 0, curw, theme->height);
}

static void tile_image(Imlib_Image img, int ox, int width)
{
	int curw = get_image_width(img);
	imlib_context_set_image(bb);

	while (width > 0) {
		width -= curw;
		if (width < 0)
			curw = -width;
		imlib_blend_image_onto_image(img, 0,
				0, 0, curw, theme->height,
				ox, 0, curw, theme->height);
		ox += curw;
	}
}

static void draw_tile_sequence(Imlib_Image left, Imlib_Image tile, Imlib_Image right,
		int ox, int width)
{
	int lw = get_image_width(left);
	int rw = get_image_width(right);
	int tilew = width - lw - rw;
	if (tilew < 0)
		return;

	draw_image(left, ox);
	ox += lw;
	tile_image(tile, ox, tilew);
	ox += tilew;
	draw_image(right, ox);
}

static void draw_switcher_alone(uint state, int ox, int width)
{
	draw_tile_sequence(theme->switcher.left_corner_img[state],
			   theme->switcher.tile_img[state],
			   theme->switcher.right_corner_img[state],
			   ox, width);
}

static void draw_switcher_left_corner(uint state, int ox, int width)
{
	draw_tile_sequence(theme->switcher.left_corner_img[state],
			   theme->switcher.tile_img[state],
			   theme->switcher.right_img[state],
			   ox, width);
}

static void draw_switcher_middle(uint state, int ox, int width)
{
	draw_tile_sequence(theme->switcher.left_img[state],
			   theme->switcher.tile_img[state],
			   theme->switcher.right_img[state],
			   ox, width);
}

static void draw_switcher_right_corner(uint state, int ox, int width)
{
	draw_tile_sequence(theme->switcher.left_img[state],
			   theme->switcher.tile_img[state],
			   theme->switcher.right_corner_img[state],
			   ox, width);
}

static void draw_taskbar_button(uint state, int ox, int width)
{
	ox += theme->taskbar.space_gap;
	width -= theme->taskbar.space_gap * 2;
	draw_tile_sequence(theme->taskbar.left_img[state],
			   theme->taskbar.tile_img[state],
			   theme->taskbar.right_img[state],
			   ox, width);
	ox += theme->taskbar.space_gap;
}

static void draw_clock_background(int ox, int width)
{
	ox += theme->clock.space_gap;
	width -= theme->clock.space_gap * 2;
	draw_tile_sequence(theme->clock.left_img,
			   theme->clock.tile_img,
			   theme->clock.right_img,
			   ox, width);
	ox += theme->clock.space_gap;
}

static void get_text_dimensions(Imlib_Font font, const char *text, int *w, int *h)
{
	imlib_context_set_font(font);
	imlib_get_text_size(text, w, h);
}

static int get_tray_width(struct tray *trayicons)
{
	int count = 0;
	struct tray *iter = trayicons;
	while (iter) {
		count++;
		iter = iter->next;
	}

	tray_width = count * 24;
	return tray_width;
}

static int update_tray_positions(int ox, struct tray *trayicons)
{
	tray_pos = ox;

	int count = 0;
	int y,w,h;
	w = theme->tray_icon_w;
	h = theme->tray_icon_h;
	struct tray *iter = trayicons;
	while (iter) {
		count++;
		y = (theme->height - h) / 2;
		XMoveResizeWindow(bbdpy, iter->win, ox, y, w, h);
		ox += w;
		iter = iter->next;
	}

	tray_width = count * theme->tray_icon_w;
	return tray_width;
}

static int update_clock_positions(int ox)
{
	clock_pos = ox;
	int w = 0;
	w += theme->clock.space_gap * 2;
	w += get_image_width(theme->clock.left_img);
	w += get_image_width(theme->clock.right_img);
	
	char buftime[128];
	time_t current_time;
	memset(&current_time, 0, sizeof(time_t));
	strftime(buftime, sizeof(buftime), theme->clock.format, localtime(&current_time));

	int fontw;
	get_text_dimensions(theme->clock.font, buftime, &fontw, 0);
	w += fontw + theme->clock.text_padding;
	clock_width = w;
	return w;
}

static int get_clock_width()
{
	return update_clock_positions(0);
}

static int update_switcher_positions(int ox, struct desktop *desktops)
{
	struct desktop *iter, *prev;
	switcher_pos = ox;

	if (!desktops)
		return 0;

	int lw = 0, w = 0, state, textw;
	iter = prev = desktops;
	state = iter->focused ? BSTATE_PRESSED : BSTATE_IDLE;
	w += theme->switcher.space_gap;
	ox += w; lw = w;
	w += get_image_width(theme->switcher.left_corner_img[state]);
	get_text_dimensions(theme->switcher.font, iter->name, &textw, 0);
	w += textw + theme->switcher.text_padding;

	while (iter->next) {
		prev = iter;
		iter = iter->next;
		state = iter->focused ? BSTATE_PRESSED : BSTATE_IDLE;
		get_text_dimensions(theme->switcher.font, iter->name, &textw, 0);
		w += get_image_width(theme->switcher.right_img[state]);
		prev->posx = ox;
		prev->width = w - lw;
		w += get_image_width(theme->switcher.separator_img);
		ox += w - lw; lw = w;
		w += get_image_width(theme->switcher.left_img[state]);
		w += textw + theme->switcher.text_padding;
	}
	w += get_image_width(theme->switcher.right_corner_img[state]);
	iter->posx = ox;
	iter->width = w - lw;
	w += theme->switcher.space_gap;

	switcher_width = w;
	return w;
}

static int get_switcher_width(struct desktop *desktops)
{
	return update_switcher_positions(0, desktops);
}

static int update_taskbar_positions(int ox, int width, 
		struct task *tasks, struct desktop *desktops)
{
	taskbar_pos = ox;
	taskbar_width = width;

	int activedesktop = 0;
	struct desktop *iter = desktops;
	while (iter) {
		if (iter->focused)
			break;
		activedesktop++;
		iter = iter->next;
	}

	int taskscount = 0;
	struct task *t = tasks;
	while (t) {
		if (t->desktop == activedesktop)
			taskscount++;
		t = t->next;
	}
	if (!taskscount)
		return width;

	int taskw = width / taskscount;
	int sep = get_image_width(theme->taskbar.separator_img);
	if (sep)
		taskw -= sep;

	t = tasks;
	while (t) {
		if (t->desktop == activedesktop) {
			t->posx = ox;
			t->width = taskw;
			ox += taskw;
			if (t->next)
				ox += sep;
			/* hack, fill empty space in the end of the task bar */
			if (!t->next || t->next->desktop != activedesktop)
				t->width += taskbar_pos + sep + width - ox;
		}
		t = t->next;
	}

	return width;
}

static void draw_text(Imlib_Font font, uint align, int ox, int width, 
		int offx, int offy, const char *text, struct color *c)
{
	imlib_context_set_image(bb);
	imlib_context_set_font(font);
	imlib_context_set_color(c->r, c->g, c->b, 255);
	int texth, textw, oy;
	imlib_get_text_size(text, &textw, &texth);
	oy = (theme->height - texth) / 2;
	switch (align) {
	case TALIGN_LEFT:
		break;
	case TALIGN_CENTER:
		ox += (width - textw) / 2;
		break;
	case TALIGN_RIGHT:
		ox = width - textw;
		break;
	}

	ox += offx;
	oy += offy;
	
	imlib_text_draw(ox, oy, text);
}

void render_switcher(struct desktop *desktops)
{		
	tile_image(theme->tile_img, switcher_pos, switcher_width);
	if (!desktops)
		return;
	int ox = switcher_pos;
	ox += theme->switcher.space_gap;
	uint state;
	struct desktop *iter = desktops;
	state = iter->focused ? BSTATE_PRESSED : BSTATE_IDLE;
	if (!iter->next)
		draw_switcher_alone(state, ox, iter->width);
	else 
		draw_switcher_left_corner(state, ox, iter->width);
	draw_text(theme->switcher.font, theme->switcher.text_align, ox, iter->width,
			theme->switcher.text_offset_x, theme->switcher.text_offset_y,
			iter->name, &theme->switcher.text_color[state]);
	ox += iter->width;

	if (!iter->next)
		return;
	iter = iter->next;
	state = iter->focused ? BSTATE_PRESSED : BSTATE_IDLE;
	if (theme->switcher.separator_img) {
		draw_image(theme->switcher.separator_img, ox);
		ox += get_image_width(theme->switcher.separator_img);
	}

	while (iter->next) {
		draw_switcher_middle(state, ox, iter->width);
		draw_text(theme->switcher.font, theme->switcher.text_align, ox, iter->width,
				theme->switcher.text_offset_x, theme->switcher.text_offset_y,
				iter->name, &theme->switcher.text_color[state]);
		ox += iter->width;
		iter = iter->next;
		state = iter->focused ? BSTATE_PRESSED : BSTATE_IDLE;
		if (theme->switcher.separator_img) {
			draw_image(theme->switcher.separator_img, ox);
			ox += get_image_width(theme->switcher.separator_img);
		}
	}
	
	draw_switcher_right_corner(state, ox, iter->width);
	draw_text(theme->switcher.font, theme->switcher.text_align, ox, iter->width,
			theme->switcher.text_offset_x, theme->switcher.text_offset_y,
			iter->name, &theme->switcher.text_color[state]);
}

void render_taskbar(struct task *tasks, struct desktop *desktops)
{
	tile_image(theme->tile_img, taskbar_pos, taskbar_width);
	int activedesktop = 0;
	struct desktop *iter = desktops;
	while (iter) {
		if (iter->focused)
			break;
		activedesktop++;
		iter = iter->next;
	}
	
	struct task *t = tasks;
	uint state;
	int gap = theme->taskbar.space_gap;

	while (t) {
		if (t->desktop == activedesktop) {
			state = t->focused ? BSTATE_PRESSED : BSTATE_IDLE;
			/* draw bg */
			draw_taskbar_button(state, t->posx, t->width);
			int lgap = get_image_width(theme->taskbar.left_img[state]);
			int rgap = get_image_width(theme->taskbar.right_img[state]);
			int x = t->posx + gap + lgap;
			int w = t->width - ((gap * 2) + lgap + rgap);

			/* draw icon */
			if (theme->taskbar.icon_h && theme->taskbar.icon_w) {
				int srcw, srch;
				int y = (theme->height - theme->taskbar.icon_h) / 2;
				srcw = theme->taskbar.icon_w;
				srch = theme->taskbar.icon_h;
				y += theme->taskbar.icon_offset_y;
				x += theme->taskbar.icon_offset_x;
				w -= theme->taskbar.icon_offset_x;
				imlib_context_set_image(bb);
				imlib_blend_image_onto_image(t->icon, 1, 0, 0, srcw, srch,
						x, y, srcw, srch);
				x += theme->taskbar.icon_w;
				w -= theme->taskbar.icon_w;
			}

			/* draw text */
			imlib_context_set_cliprect(x, 0, w, bbheight);
			draw_text(theme->taskbar.font, theme->taskbar.text_align, x, w,
				theme->taskbar.text_offset_x, theme->taskbar.text_offset_y,
				t->name, &theme->taskbar.text_color[state]);
			imlib_context_set_cliprect(0, 0, bbwidth, bbheight);

			/* draw separator if exists */
			if (t->next && t->next->desktop == activedesktop)
				draw_image(theme->taskbar.separator_img, x+w+gap+rgap);
		}
		t = t->next;
	}
}

void render_clock()
{
	tile_image(theme->tile_img, clock_pos, clock_width);
	int ox = clock_pos;
	draw_clock_background(ox, clock_width);
	int gap = theme->clock.space_gap;
	int lgap = get_image_width(theme->clock.left_img);
	int rgap = get_image_width(theme->clock.right_img);
	int x = ox + gap + lgap;
	int w = clock_width - ((gap * 2) + lgap + rgap);

	char buftime[128];
	time_t current_time;
	current_time = time(0);
	strftime(buftime, sizeof(buftime), theme->clock.format, localtime(&current_time));

	imlib_context_set_cliprect(x, 0, w, bbheight);
	draw_text(theme->clock.font, theme->clock.text_align, x, w,
			theme->clock.text_offset_x, theme->clock.text_offset_y,
			buftime, &theme->clock.text_color);
	imlib_context_set_cliprect(0, 0, bbwidth, bbheight);
}

void init_render(struct theme *t, Display *dpy, Drawable drawable)
{
	bbwidth = DisplayWidth(dpy, DefaultScreen(dpy));
	bbheight = t->height;
	bb = imlib_create_image(bbwidth, bbheight);
	bbdpy = dpy;
	bbvis = DefaultVisual(dpy, DefaultScreen(dpy));
	bbwin = drawable;
	imlib_context_set_display(bbdpy);
	imlib_context_set_visual(bbvis);
	imlib_context_set_drawable(bbwin);
	theme = t;
}

void shutdown_render()
{
	imlib_context_set_image(bb);
	imlib_free_image();
}

void render_update_panel_positions(struct panel *p)
{
	char *e = theme->elements;
	int ox = 0;
	int taskbarx = 0;
	/* figure out taskbar position and width */
	while (*e) {
		switch (*e) {
		/* clock */
		case 'c':
			ox += get_clock_width();
			break;
		/* desktop switcher */
		case 's':
			ox += get_switcher_width(p->desktops);
			break;
		/* tray */
		case 't':
			ox += get_tray_width(p->trayicons);
			break;
		/* taskbar */
		case 'b': 
			taskbarx = ox;
			e++;
			continue;
		}
		e++;
		if (theme->separator_img)
			ox += get_image_width(theme->separator_img);
	}
	int taskbarw = bbwidth - ox;

	/* now really update all positions */
	ox = 0;
	e = theme->elements;
	while (*e) {
		switch (*e) {
		/* clock */
		case 'c':
			ox += update_clock_positions(ox);
			break;
		/* desktop switcher */
		case 's':
			ox += update_switcher_positions(ox, p->desktops);
			break;
		/* tray */
		case 't':
			ox += update_tray_positions(ox, p->trayicons);
			break;
		/* taskbar */
		case 'b': 
			ox += update_taskbar_positions(taskbarx, taskbarw, p->tasks, p->desktops);
			break;
		}
		e++;
		if (theme->separator_img)
			ox += get_image_width(theme->separator_img);
	}
}

void render_panel(struct panel *p)
{
	int ox = 0;
	char *e = theme->elements;
	while (*e) {
		switch (*e) {
			case 'c':
				render_clock();
				ox += clock_width;
				break;
			case 's':
				render_switcher(p->desktops);
				ox += switcher_width;
				break;
			case 't':
				if (tray_width)
					tile_image(theme->tile_img, tray_pos, tray_width);
				ox += tray_width;
				break;
			case 'b':
				render_taskbar(p->tasks, p->desktops);
				ox += taskbar_width;
				break;
		}
		e++;
		if (*e && theme->separator_img) {
			draw_image(theme->separator_img, ox);
			ox += get_image_width(theme->separator_img);
		}
	}
	render_present();
}

void render_present()
{
	imlib_context_set_display(bbdpy);
	imlib_context_set_visual(bbvis);
	imlib_context_set_drawable(bbwin);
	imlib_render_image_on_drawable(0,0);
}
