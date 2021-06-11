#include <stdbool.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include <fcntl.h>
#include <stdio.h>

#include <sys/ioctl.h>
#include <sys/mman.h>
#include <sys/types.h>

#include <linux/fb.h>
#include <linux/kd.h>

#include <time.h>

#include <minui/minui.h>
#include "graphics.h"

int main() {
	// It might be a good idea to add some blending tests.
	// The only blending done currently is around the font / text
	int i = 0;
    printf("Initializing graphics.\n");
	if (gr_init() != 0) {
	    printf("Error initializing graphics.\n");
	    return -1;
	}
	printf("Starting tests\n");
	printf("Red\n");
	gr_color(255, 0, 0, 255);
	gr_fill(0, 0, gr_fb_width(), gr_fb_height());
	gr_flip();
	sleep(1);
	printf("Green\n");
	gr_color(0, 225, 0, 255);
	gr_fill(0, 0, gr_fb_width(), gr_fb_height());
	gr_flip();
	sleep(1);
	printf("Blue\n");
	gr_color(0, 0, 255, 255);
	gr_fill(0, 0, gr_fb_width(), gr_fb_height());
	gr_flip();
	sleep(1);
	printf("White\n");
	gr_color(255, 255, 255, 255);
	gr_fill(0, 0, gr_fb_width(), gr_fb_height());
	gr_flip();
	sleep(1);
	printf("4 colors, 1 in each corner\n");
	gr_color(255, 0, 0, 255);
	gr_fill(0, 0, gr_fb_width() / 2, gr_fb_height() / 2);
	gr_color(0, 255, 0, 255);
	gr_fill(0, gr_fb_height() / 2, gr_fb_width() / 2, gr_fb_height());
	gr_color(0, 0, 255, 255);
	gr_fill(gr_fb_width() / 2, 0, gr_fb_width(), gr_fb_height() / 2);
	gr_color(255, 255, 255, 255);
	gr_fill(gr_fb_width() / 2, gr_fb_height() / 2, gr_fb_width(), gr_fb_height());
	gr_flip();
	sleep(3);
	printf("4 colors, vertical stripes\n");
	gr_color(255, 0, 0, 255);
	gr_fill(0, 0, gr_fb_width() / 4, gr_fb_height());
	gr_color(0, 255, 0, 255);
	gr_fill(gr_fb_width() / 4, 0, gr_fb_width() / 2, gr_fb_height());
	gr_color(0, 0, 255, 255);
	gr_fill(gr_fb_width() / 2, 0, gr_fb_width() * 3 / 4, gr_fb_height());
	gr_color(255, 255, 255, 255);
	gr_fill(gr_fb_width() * 3 / 4, 0, gr_fb_width(), gr_fb_height());
	gr_flip();
	sleep(3);
	printf("Gradients, 1 in each corner\n");
	gr_color(0, 0, 0, 255);
	gr_fill(0, 0, gr_fb_width(), gr_fb_height());
	for (i = 0; i < 255; i++) {
		gr_color(i, 0, 0, 255);
		gr_fill(i, 0, i+1, gr_fb_height() / 2);
		gr_color(0, i, 0, 255);
		gr_fill(i, gr_fb_height() / 2, i+1, gr_fb_height());
		gr_color(0, 0, i, 255);
		gr_fill(i + (gr_fb_width() / 2), 0, i + (gr_fb_width() / 2) + 1, gr_fb_height() / 2);
		gr_color(i, i, i, 255);
		gr_fill(i + (gr_fb_width() / 2), gr_fb_height() / 2, i + (gr_fb_width() / 2) + 1, gr_fb_height());
	}
	gr_flip();
	sleep(3);
	printf("White with RGB text\n");
	gr_color(255, 255, 255, 255);
	gr_fill(0, 0, gr_fb_width(), gr_fb_height());
	gr_color(255, 0, 0, 255);
	gr_text(gr_sys_font(), 10, 10, "RED red RED", false);
	gr_color(0, 255, 0, 255);
	gr_text(gr_sys_font(), 10, 50, "GREEN green GREEN", false);
	gr_color(0, 0, 255, 255);
	gr_text(gr_sys_font(), 10, 90, "BLUE blue BLUE", false);
	gr_flip();
	sleep(3);
	printf("PNG test with /res/images/test.png\n");
	GRSurface* icon[2];
	gr_color(0, 0, 0, 255);
	gr_fill(0, 0, gr_fb_width(), gr_fb_height());
	res_create_display_surface("test", icon);
	GRSurface* surface = icon[0];
	gr_blit(surface, 0, 0, gr_get_width(surface), gr_get_height(surface), 10, 10);
	gr_flip();
	res_free_surface(surface);
	sleep(3);
	printf("Exit graphics.\n");
	gr_exit();
	printf("Done.\n");
	return 0;
}
