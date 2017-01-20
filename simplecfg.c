#include <stdio.h>
#include <string.h>
FILE* cfg_open(const char *fn) { return fopen(fn, "r"); }
void  cfg_close(FILE*f) { fclose(f); }
char* cfg_getstr(FILE *f, const char *key, char*  buf, size_t bufsize) {
	fseek(f, 0, SEEK_SET);
	size_t l = strlen(key);
	while(fgets(buf, bufsize, f)) {
		if(!strncmp(buf, key, l) && buf[l] == '=') {
			size_t x = l;
			while(buf[++x] != '\n');
			buf[x] = 0;
			memmove(buf, buf + l + 1, x - l);
			return buf;
		}
	}
	*buf = 0;
	return 0;
}
int cfg_getint(FILE *f, const char *key) {
	char buf[64];
	char *res = cfg_getstr(f, key, buf, sizeof buf);
	if(res) return atoi(res);
	return 0;
}
