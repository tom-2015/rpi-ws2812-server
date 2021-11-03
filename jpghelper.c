#include "jpghelper.h"

/*
* Here's the routine that will replace the standard error_exit method:
*/
void my_error_exit (j_common_ptr cinfo){
	/* cinfo->err really points to a my_error_mgr struct, so coerce pointer */
	my_error_ptr myerr = (my_error_ptr) cinfo->err;

	/* Always display the message. */
	/* We could postpone this until after returning, if we chose. */
	(*cinfo->err->output_message) (cinfo);

	/* Return control to the setjmp point */
	longjmp(myerr->setjmp_buffer, 1);
}

void jpg_init_source(j_decompress_ptr cinfo) {

}

boolean jpg_fill_input_buffer(j_decompress_ptr cinfo){
	ERREXIT(cinfo, JERR_INPUT_EMPTY);
	return TRUE;
}

void jpg_skip_input_data(j_decompress_ptr cinfo, long num_bytes){
	struct jpeg_source_mgr* src = (struct jpeg_source_mgr*)cinfo->src;

	if (num_bytes > 0) {
		src->next_input_byte += (size_t)num_bytes;
		src->bytes_in_buffer -= (size_t)num_bytes;
	}
}

void jpg_term_source(j_decompress_ptr cinfo) {

}