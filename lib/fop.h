#ifndef FILE_IO_H
#define FILE_IO_H

enum {
	RW_MODE_WRITE,
	RW_MODE_READ_LINE = 1 << 0,	/** Read one line. */
	RW_MODE_READ	= 1 << 1,
};

struct oryx_file_rw_context_t {
	char *sc_buffer;
	oryx_size_t ul_size;	/** buffer size */
	u8 cmd;
	oryx_size_t ul_rw_size;	/** Real read or write data size. */
};


int oryx_path_remove (const char *path);
int oryx_path_exsit (const char *path);
int oryx_mkfile (const char *file, oryx_file_t **fp, char *mode);
int oryx_mkdir(const char *path, oryx_dir_t **dir);
int oryx_file_close (oryx_file_t *fp);
int oryx_file_read_write (oryx_file_t *fp, 
			struct oryx_file_rw_context_t *frw_ctx);

void oryx_system_preview(void);
int oryx_pattern_generate (char *pattern, size_t l);
void oryx_ipaddr_generate (char *ipv4);

int isalldigit(const char *str);
char *draw_color(color_t color);
int kvpair(char *str, char **k, char **v);
int is_numerical (char* s);
void do_system(const char *cmd);
u64 tm_elapsed_us (struct  timeval *start, struct  timeval *end);
int tm_format(uint64_t ts, const char *tm_form, char *date, size_t len);

#if !defined(HAVE_SURICATA)
void memcpy_tolower (u8 *d, u8 *s, u16 len);
#endif

#endif

