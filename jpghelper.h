#ifndef JPG_HELPER_H
#define JPG_HELPER_H

#include <stddef.h>
#include <stdio.h>
#include <jpeglib.h>
#include <jerror.h>
#include <setjmp.h>

//from https://github.com/LuaDist/libjpeg/blob/master/example.c

struct my_error_mgr {
	struct jpeg_error_mgr pub;	/* "public" fields */

	jmp_buf setjmp_buffer;	/* for return to caller */
};

typedef struct my_error_mgr * my_error_ptr;

//helper functions for JPG library
void my_error_exit (j_common_ptr cinfo);
void jpg_init_source(j_decompress_ptr cinfo);
boolean jpg_fill_input_buffer(j_decompress_ptr cinfo);
void jpg_skip_input_data(j_decompress_ptr cinfo, long num_bytes);
void jpg_term_source(j_decompress_ptr cinfo);

#endif