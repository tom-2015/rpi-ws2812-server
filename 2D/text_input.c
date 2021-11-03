#include "text_input.h"
#include "../sockets.h"

#define MESSAGE_DIRECTION_RIGHT_LEFT 0
#define MESSAGE_DIRECTION_LEFT_RIGHT 1

//easy scrolling message board
//message_board <channel>,<x>,<y>,<width>,<height>,<port_nr>,<direction>,<text_color>,<back_color>,<delay>,<font_size>,<font_anti_alias>,<options>,<font>
void text_input(thread_context* context, char* args) {
    int i, channel = 0, x = 0, y = 0, text_color = 0, port_nr=9001, back_color = COLOR_TRANSPARENT, delay = 10, font_size = 8, direction = MESSAGE_DIRECTION_RIGHT_LEFT, width = 0, height = 0, font_anti_alias = CAIRO_ANTIALIAS_NONE, options = 0;
    char font[MAX_VAL_LEN] = { 0 };
    char* text_buffer;
    int text_buffer_index = 0;
    int text_buffer_count = 0;

    args = read_channel(args, &channel);

    if (is_valid_2D_channel_number(channel)) {

        args = read_int(args, &x);
        args = read_int(args, &y);
        args = read_int(args, &width);
        args = read_int(args, &height);
        args = read_int(args, &port_nr);
        args = read_int(args, &direction);
        args = read_color_arg(args, &text_color, 4);
        args = read_color_arg(args, &back_color, 4);
        args = read_int(args, &delay);
        args = read_int(args, &font_size);
        args = read_int(args, &font_anti_alias);
        args = read_int(args, &options);
        args = read_str(args, font, sizeof(font));

        //cairo_pattern_set_filter(cairo_get_source(cr), CAIRO_FILTER_NEAREST);

        if (width <= 0) {
            fprintf(stderr, "Invalid width paramter value must be >0\n");
            return;
        }

        if (debug) printf("text_input %d,%d,%d,%d,%d,%d,%d,%d,%d,%d,%d,%d,%d,%s,%d\n", channel, x, y, width, height, port_nr, direction, text_color, back_color, delay, font_size, font_anti_alias, options, font);

        if (font[0] == 0) strcpy(font, DEFAULT_FONT);

        channel_info * led_channel = get_channel(channel);
        cairo_t* cr = led_channel->cr;

        cairo_save(cr);

        cairo_font_options_t* font_options = cairo_font_options_create();
        cairo_font_options_set_antialias(font_options, font_anti_alias); //CAIRO_ANTIALIAS_NONE

        FT_Face ft_face;
        bool using_freetype_lib = false;
        cairo_font_face_t* cairo_ft_face;

        if (file_exists(font)) { //check if file exists and load from file

            if (!init_ft_lib(context)) return;

            FT_Error status = FT_New_Face(context->ft_lib, font, 0, &ft_face);
            if (status != 0) {
                fprintf(stderr, "Error %d opening font %s.\n", status, font);
                return;
            }
            cairo_ft_face = cairo_ft_font_face_create_for_ft_face(ft_face, 0);

            cairo_set_font_face(cr, cairo_ft_face);
            using_freetype_lib = true;

            if (debug) printf("using free type file %s.\n", font);

        }else {
            if (options & 1) cairo_select_font_face(cr, font, CAIRO_FONT_SLANT_NORMAL, CAIRO_FONT_WEIGHT_BOLD);
            else cairo_select_font_face(cr, font, CAIRO_FONT_SLANT_NORMAL, CAIRO_FONT_WEIGHT_NORMAL);
            if (options & 2) cairo_font_options_set_hint_style(font_options, CAIRO_HINT_STYLE_NONE);

            if (debug) printf("Cairo font family %s.\n", font);
        }

        cairo_set_font_options(cr, font_options);
        cairo_set_font_size(cr, font_size);

        //initialize the text buffer
        text_buffer_count = width*2;
        text_buffer = malloc(sizeof(char) * (text_buffer_count+1)); //reserve some space for our text 
        text_buffer[0] = 0;
        text_buffer[text_buffer_count] = 0;

        //set up the socket
        socket_t sockfd = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
        if (sockfd < 0) {
            fprintf(stderr, "ERROR opening socket\n");
            exit(1);
        }

        struct sockaddr_in serv_addr;
        bzero((char*)&serv_addr, sizeof(serv_addr));

        serv_addr.sin_family = AF_INET;
        serv_addr.sin_addr.s_addr = INADDR_ANY;
        serv_addr.sin_port = htons(port_nr);
        if (bind(sockfd, (struct sockaddr*)&serv_addr, sizeof(serv_addr)) < 0) {
            fprintf(stderr, "ERROR on binding port %d.\n", port_nr);
        } else {
            listen(sockfd, 5);

            socklen_t clilen;
            int sock_opt = 1;
            socklen_t optlen = sizeof(sock_opt);

            int total_width = 0, total_height = 0, p_total_width = 0;
            int character_size = 0; //size of the last character read, this is how many pixels to move until read next character
            int p_x, p_y; //x y where to paint text next

            switch (direction) {
            case MESSAGE_DIRECTION_RIGHT_LEFT:
                if (width == 0) width = led_channel->width;
                if (height == 0) height = total_height;
                p_x = x + width;
                p_y = y;
                break;
            case MESSAGE_DIRECTION_LEFT_RIGHT:
                if (width == 0) width = led_channel->width;
                if (height == 0) height = total_height;
                p_x = x;
                p_y = y;
                break;
            }

            while (!context->end_current_command) {
                write_to_output("READY\n");
                struct sockaddr_in cli_addr;                
                clilen = sizeof(cli_addr);
                int input_socket = accept(sockfd, (struct sockaddr*)&cli_addr, &clilen); //accept the socket
                if (input_socket != -1) {
                    setsockopt(input_socket, SOL_SOCKET, SO_KEEPALIVE, &sock_opt, optlen);

                    if (debug) printf("Client connected to port %d.\n", port_nr);

                    char c;

                    while (read(input_socket, (void*)&c, 1) > 0 && !context->end_current_command) {
                        if (c != '\r' && c != '\n') {
                            if (debug) printf("Character received %c\n", c);

                            switch (direction) {
                            case MESSAGE_DIRECTION_RIGHT_LEFT:
                                //add new character at the end

                                if (text_buffer_index == text_buffer_count) {//we are at the end of the buffer, move all characters down 1 place
                                    for (i = text_buffer_count - 1;i > 0;i--) {
                                        text_buffer[i - 1] = text_buffer[i];
                                    }

                                    cairo_text_extents_t extents;
                                    cairo_text_extents(cr, text_buffer, &extents);

                                    int total_width_after_remove = extents.x_advance;

                                    switch (direction) {
                                    case MESSAGE_DIRECTION_RIGHT_LEFT:
                                        p_x += p_total_width - total_width_after_remove; //move removed character width in x back
                                        break;
                                    case MESSAGE_DIRECTION_LEFT_RIGHT:
                                        //has no effect
                                        break;
                                    }

                                    p_total_width = total_width_after_remove;

                                }

                                text_buffer[text_buffer_index] = c;
                                text_buffer[text_buffer_index + 1] = 0; //end with 0
                                text_buffer_index++;
                                break;
                            case MESSAGE_DIRECTION_LEFT_RIGHT:
                                //add new character at the beginning
                                for (i = text_buffer_count - 2; i >= 0 ;i--) text_buffer[i + 1] = text_buffer[i];
                                text_buffer[0] = c;
                                break;
                            }


                            cairo_text_extents_t extents;
                            cairo_text_extents(cr, text_buffer, &extents);

                            total_width = extents.x_advance;
                            total_height = extents.height;

                            character_size = total_width - p_total_width; //new total width vs previous total width of the text buffer + extra user defined spacing
                            int mask_x = x, mask_y = y + extents.y_bearing;

                            switch (direction) {
                            case MESSAGE_DIRECTION_RIGHT_LEFT:
                                //no effect on x location adding new char
                                break;
                            case MESSAGE_DIRECTION_LEFT_RIGHT:
                                p_x -= total_width - p_total_width; //move removed character width in x back
                                break;
                            }

                            while (character_size > 0 && !context->end_current_command) {

                                cairo_save(cr);

                                //paint backcolor
                                cairo_set_operator(cr, CAIRO_OPERATOR_SOURCE);
                                set_cairo_color_rgba(cr, back_color);
                                cairo_rectangle(cr, mask_x, mask_y, width, height);
                                cairo_fill(cr);

                                //set clipping rectangle
                                //http://zetcode.com/gfx/cairo/clippingmasking/
                                cairo_set_source_rgb(cr, 0, 0, 0);
                                cairo_rectangle(cr, mask_x, mask_y, width, height);
                                cairo_clip(cr);

                                //set forecolor
                                set_cairo_color_rgba(cr, text_color);

                                cairo_move_to(cr, (double)p_x, (double)p_y);
                                cairo_show_text(cr, text_buffer);

                                cairo_restore(cr);

                                render_channel(channel);

                                switch (direction) {
                                case MESSAGE_DIRECTION_RIGHT_LEFT:
                                    p_x--;
                                    if (p_x < (x + width - total_width)) character_size = 0;
                                    break;
                                case MESSAGE_DIRECTION_LEFT_RIGHT:
                                    p_x++;
                                    if (p_x > x) character_size = 0;
                                    break;
                                }

                                if (delay == 0) break;
                                usleep(delay * 1000);
                            }

                            p_total_width = total_width;
                        }
                    }        
                    shutdown(input_socket, SHUT_RDWR);
                    close(input_socket);
                } else {
                    perror("Socket accept error");
                }
            }
            shutdown(sockfd, SHUT_RDWR);
            close(sockfd);
        }

        free(text_buffer);
        cairo_font_options_destroy(font_options);

        if (using_freetype_lib) {
            //https://www.cairographics.org/manual/cairo-FreeType-Fonts.html#cairo-ft-font-face-create-for-ft-face
            static const cairo_user_data_key_t key;
            cairo_status_t status = cairo_font_face_set_user_data(cairo_ft_face, &key, ft_face, (cairo_destroy_func_t)FT_Done_Face);

            if (status) {
                cairo_font_face_destroy(cairo_ft_face);
                FT_Done_Face(ft_face);
            }
        }

    } else {
        fprintf(stderr, ERROR_INVALID_2D_CHANNEL);
    }
}