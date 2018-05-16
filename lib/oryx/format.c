#include "oryx.h"


/*
 * Generates a log message The message will be sent in the stream
 * defined by the previous call to rte_openlog_stream().
 */
static int
___format(struct oryx_fmt_buff_t *fb, const char *format, va_list ap)
{
	int ret;
	ret = vsnprintf(fb->fmt_data + fb->fmt_data_size, 
			fb->fmt_buff_size - fb->fmt_data_size, 
			format, ap);
	return ret;
}

void oryx_format(struct oryx_fmt_buff_t *fb, const char *fmt, ...)
{
	va_list ap;
	int ret;

	if (!fb->fmt_data) {
		fb->fmt_data = malloc (DEFAULT_FMT_MSG_SIZE);
		if(fb->fmt_data) {
			fb->fmt_data_size = 0;
			fb->fmt_buff_size = DEFAULT_FMT_MSG_SIZE;
			memset (fb->fmt_data, 0, fb->fmt_buff_size);
		}
	}

	/** fmt buffer expand when needed. */
	if ((float)fb->fmt_data_size > (fb->fmt_buff_size * 0.8)) {
		fb->fmt_data = realloc(fb->fmt_data, fb->fmt_data_size + DEFAULT_FMT_MSG_SIZE);
		if(fb->fmt_data) {
			/* new size. */
			fb->fmt_buff_size = fb->fmt_data_size + DEFAULT_FMT_MSG_SIZE;
		}
	}
	
	va_start(ap, fmt);
	fb->fmt_data_size += ___format(fb, fmt, ap);
	va_end(ap);
	return ret;

}

void oryx_format_free(struct oryx_fmt_buff_t *fb)
{
	if(fb->fmt_data)
		free(fb->fmt_data);

	fb->fmt_buff_size = fb->fmt_data_size = 0;
}


