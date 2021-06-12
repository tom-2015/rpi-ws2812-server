#include <assert.h>
#include <stdio.h>
#include <cairo.h>
#include <cairo-xlib.h>
#include <X11/Xlib.h>

//take a screen shot
//take_screen_shot <channel>,<display>,<dst_x>,<dst_y>,<src_x>,<src_y>,<dst_width>,<dst_height>,<src_width>,<src_height>,<interval>
void take_screenshot(thread_context* context, char* args) {
    char display[MAX_VAL_LEN];
    int channel = 0, src_x=0, src_y=0, src_width=0, src_height=0, interval=0;
    double dst_x = 0.0, dst_y = 0.0, dst_width = 0.0, dst_height = 0.0;

    args = read_channel(args, &channel);

    if (is_valid_2D_channel_number(channel)) {
        dst_width = led_channels[channel].width;
        dst_height = led_channels[channel].height;
        src_width = dst_width;
        src_height = dst_height;
    
        strcpy(display, ":0");

        args = read_str(args, display, MAX_VAL_LEN);
        args = read_double(args, &dst_x);
        args = read_double(args, &dst_y);
        args = read_int(args, &src_x);
        args = read_int(args, &src_y);
        args = read_double(args, &dst_width);
        args = read_double(args, &dst_height);
        args = read_int(args, &src_width);
        args = read_int(args, &src_height);
        args = read_int(args, &interval);

        Display* disp;
        Window root;
        cairo_surface_t* src_surface;
        int scr;

        if (debug) printf("take_screenshot %d, %s, %d, %d, %d,%d,%d,%d,%d,%d,%d\n", channel, display, dst_x, dst_y, src_x, src_y, dst_width, dst_height, src_width, src_height, interval);

        /* try to connect to display, exit if it's NULL */
        disp = XOpenDisplay(display);
        if (disp == NULL) {
            fprintf(stderr, "Given display cannot be found %s\n", display);
            return;
        }

        double xScale = dst_width / (double)src_width;
        double yScale = dst_height / (double)src_height;

        cairo_t* cr = led_channels[channel].cr;
        
        scr = DefaultScreen(disp);
        
        root = DefaultRootWindow(disp);
        
        cairo_save(cr);
        cairo_scale(cr, xScale, yScale);
        
        while (!context->end_current_command) {
            src_surface = cairo_xlib_surface_create(disp, root, DefaultVisual(disp, scr), DisplayWidth(disp, scr), DisplayHeight(disp, scr));
            cairo_set_source_surface(cr, src_surface, (dst_x / xScale - src_x), (dst_y / yScale - src_y));
            cairo_rectangle(cr, dst_x / xScale, dst_y / yScale, dst_width / xScale, dst_height / yScale);
            cairo_fill(cr);
            cairo_surface_destroy(src_surface);

            if (interval == 0) {
                break;
            } else {
                usleep(interval * 1000);
                render_channel(channel);
                if (debug) printf("Render screenshot.\n");
            }
        }
        
        cairo_restore(cr);

        /*cairo_set_source_surface(cr, src_surface, (dst_x / xScale - src_x) , (dst_y  / yScale - src_y));
        
        cairo_rectangle(cr, dst_x / xScale, dst_y / yScale, dst_width / xScale, dst_height / yScale);
        cairo_fill(cr);

        while (interval != 0) {
            usleep(interval*1000);
            cairo_fill(cr);
            render_channel(channel);

        }*/
        
        

        //cairo_surface_destroy(src_surface);

    } else {
        fprintf(stderr, ERROR_INVALID_2D_CHANNEL);
    }
}