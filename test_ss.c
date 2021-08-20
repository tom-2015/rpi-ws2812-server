#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <assert.h>
#include <stdio.h>
#include <cairo.h>
#include <cairo-xlib.h>
#include <X11/Xlib.h>


int main() {
    Display* disp;
    Window root;
    cairo_surface_t* src_surface;
    int scr;

    unsigned char* data;
    int width = 1024;
    int height = 768;

    int stride = cairo_format_stride_for_width(CAIRO_FORMAT_RGB24, width);

    data =  (unsigned char*)malloc(stride * height * sizeof(unsigned char));

    cairo_surface_t * dst_surface = cairo_image_surface_create_for_data(data, CAIRO_FORMAT_RGB24, width, height, stride);

    cairo_t * cr = cairo_create(dst_surface);

    int src_y = 16;
    int src_x = 32;

    int src_width = 200;
    int src_height = 200;

    int dst_x = 200;
    int dst_y = 250;

    int dst_width = src_width / 2;
    int dst_height = src_height / 2;

    /* try to connect to display, exit if it's NULL */
    disp = XOpenDisplay(":0");
    if (disp == NULL) {
        fprintf(stderr, "Given display cannot be found %s\n", ":0");
        return -1;
    }

    scr = DefaultScreen(disp);
    root = DefaultRootWindow(disp);

    src_surface = cairo_xlib_surface_create(disp, root, DefaultVisual(disp, scr), DisplayWidth(disp, scr), DisplayHeight(disp, scr));

    float xScale = (float)dst_width / (float)src_width;
    float yScale = (float)dst_height / (float)src_height;

    cairo_save(cr);
    cairo_scale(cr, xScale, yScale);
    cairo_set_source_surface(cr, src_surface, (dst_x / xScale - src_x) , (dst_y / yScale- src_y)  );//

    cairo_rectangle(cr, dst_x / xScale, dst_y / yScale, dst_width / xScale, dst_height / yScale);
    cairo_fill(cr);
    cairo_restore(cr);

    cairo_surface_destroy(src_surface);

    cairo_surface_write_to_png(dst_surface, "test.png");
    
    return 0;
}