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

#include "minui.h"
#include "graphics.h"

int main() {
    printf("Initializing graphics.\n");
	if (gr_init() != 0) {
	    printf("Error initializing graphics.\n");
	    return -1;
	}
	printf("Starting tests\n");
	/*printf("Red\n");
	gr_color(255, 0, 0, 255);
	gr_fill(0, 0, gr_fb_width(), gr_fb_height());
	gr_flip();
	sleep(1);
	printf("Green\n");
	gr_color(0, 255, 0, 255);
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
	sleep(3);*/
	printf("Black with RGB text\n");
	gr_color(0, 0, 0, 255);
	gr_fill(0, 0, gr_fb_width(), gr_fb_height());
	gr_color(255, 0, 0, 255);
	gr_text(10, 10, "RED red RED", false);
	/*gr_color(0, 255, 0, 255);
	gr_text(10, 50, "GREEN green GREEN", false);
	gr_color(0, 0, 255, 255);
	gr_text(10, 90, "BLUE blue BLUE", false);*/
	gr_flip();
	sleep(10);
	printf("Exit graphics.\n");
	gr_exit();
	printf("Done.\n");
	return 0;
}
