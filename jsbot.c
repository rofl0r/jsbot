/*  Copyright (C) 2017 rofl0r

    This program is free software; you can redistribute it and/or modify
    it under the terms of the GNU General Public License as published by
    the Free Software Foundation; either version 2 of the License, or
    (at your option) any later version.

    This program is distributed in the hope that it will be useful,
    but WITHOUT ANY WARRANTY; without even the implied warranty of
    MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
    GNU General Public License for more details.

    You should have received a copy of the GNU General Public License along
    with this program; if not, write to the Free Software Foundation, Inc.,
    51 Franklin Street, Fifth Floor, Boston, MA 02110-1301 USA.
*/
#include "rocksock.h"
#include "rsirc.h"
#include <string.h>
#include <stdio.h>
#include <stdlib.h>
#include <ctype.h>
#include <unistd.h>
#include <stdarg.h>
#include <assert.h>
#include <fcntl.h>
#include <sys/stat.h>
#include <time.h>
#include <mujs.h>
//RcB: LINK "-lmujs"

#include "simplecfg.c"
#ifndef TRANSACT_TIME
#define TRANSACT_TIME 500 * 1000
#endif

static const char hextab[] = "0123456789abcdef";

static char* decode(const char *in, char* out) {
	const unsigned char* pi = (void*)in;
	char *po = out;
	while(*pi) {
		if(*pi > 20 &&  *pi < 128) *po++ = *pi++;
		else {
			*po++='\\';
			*po++='x';
			*po++= hextab[(*pi & 0xf0) >> 4];
			*po++= hextab[*pi++ & 15];
		}
	}
	*po = 0;
	return out;
}

static int split(const char *in, char sep, int splitcount, ...) {
	va_list ap;
	va_start(ap, splitcount);
	int r = 1;
	int i = 0;
	const char *start = in;
	while(i < splitcount-1 || !splitcount) {
		char * out = va_arg(ap, char*);
		if(!out && !splitcount) break;
		size_t idx = 0;
		while(start[idx] && start[idx] != sep) idx++;
		memcpy(out, start, idx);
		out[idx] = 0;
		if(!start[idx]) { r = 0; goto ret; }
		start += idx + 1;
		i++;
	}
	if(splitcount) {
		char * out = va_arg(ap, char*);
		strcpy(out, start);
	}
	ret:
	va_end(ap);
	return r;
}

#define chk(X, ACTION) if(X) { \
                rocksock_error_dprintf(2, s); \
                ACTION; \
        }
#define chk2(X, ACTION) if(X) { ACTION; }
/* "" */


static rocksock rs, *s = &rs;
static rsirc ircb, *irc = &ircb;
static rs_proxy proxies[2];
static int done_rs_init;

//PING :kornbluth.freenode.net
static int ping_handler(char *buf) {
	char b[512];
	snprintf(b, sizeof(b), "PONG %s", buf+1);
	return rsirc_sendline(irc, b);
}

static char *own_nick;
static char *alternate_nick;
static char nick1[32];
static char nick2[32];
static char nick3[32];
static char proxy[512];
static char savefile[64];
static char host[256];
static int port;
static int use_ssl;


static int want_quit;

static js_State *J;

static void jscb_strings_command(const char* cmd, int args, ...) {
	int i;
	va_list ap;
	va_start(ap, args);
	js_getglobal(J, cmd);
	js_pushnull(J);
	for (i = 0; i < args; ++i) {
		const char *a = va_arg(ap, const char *);
		js_pushstring(J, a);
	}
	va_end(ap);
	if (js_pcall(J, args))
		dprintf(2, "error calling %s: %s\n", cmd, js_tostring(J, -1));
	js_pop(J, 1);
}

static void jscb_onconnect(void) {
	jscb_strings_command("connect", 0);
}
static void jscb_onjoinself(const char* chan, const char* nick) {
	jscb_strings_command("selfjoin", 2, chan, nick);
}
static void jscb_msghandler(const char* chan, const char* nick, const char* mask, const char * msg) {
	jscb_strings_command("msghandler", 4, chan, nick, mask, msg);
}
static void jscb_noticehandler(const char* dest, const char* nick, const char* mask, const char * msg) {
	jscb_strings_command("noticehandler", 4, dest, nick, mask, msg);
}
static void jscb_joinhandler(const char *chan, const char* nick, const char* mask) {
	jscb_strings_command("joinhandler", 3, chan, nick, mask);
}
static void jscb_parthandler(const char *chan, const char* nick, const char* mask, const char* msg) {
	jscb_strings_command("parthandler", 4, chan, nick, mask, msg);
}
static void jscb_quithandler(const char* nick, const char* mask, const char *msg) {
	jscb_strings_command("quithandler", 3, nick, mask, msg);
}
static void jscb_botnick(const char* nick) {
	jscb_strings_command("botnick", 1, nick);
}
static int on_joinself(const char* chan, const char* nick) {
	jscb_onjoinself(chan, nick);
	return 0;
}

static void joinhandler(const char *chan, const char* nick, const char* mask, const char* dummy1, const char* dummy2) {
	(void) dummy1; (void) dummy2;
	jscb_joinhandler(chan, nick, mask);
}

static void parthandler(const char *chan, const char* nick, const char* mask, const char* msg, const char* dummy1) {
	(void) dummy1;
	jscb_parthandler(chan, nick, mask, msg);
}

static void quithandler(const char* nick, const char* mask, const char *msg, const char* dummy1, const char* dummy2) {
	(void) dummy1; (void) dummy2;
	jscb_quithandler(nick, mask, msg);
}

static void msghandler(const char* chan, const char* nick, const char* mask, const char * msg, const char* dummy1) {
	(void) dummy1;
	jscb_msghandler(chan, nick, mask, msg);
}

static void kickhandler(const char* nick, const char* mask, const char* whom, const char * chan, const char* msg) {
}

static void noticehandler(const char* dest, const char* nick, const char* mask, const char * msg, const char* dummy1) {
	(void) dummy1;
	jscb_noticehandler(dest, nick, mask, msg);
	char decodebuf[512 * 4];
	dprintf(2, "NOTICE@%s <%s(%s)> %s\n", dest, nick, mask, decode(msg, decodebuf));
}

/* join: mask, cmd, chan
   part: mask, cmd, chan, :"msg"
   quit: mask, cmd, :msg
   kick: mask, cmd, chan, whom, :msg
 notice: mask, cmd, dest, :msg
privmsg: mask, cmd, dest, :msg
 */
enum action {
	a_join = 0, a_part, a_quit, a_kick,
	a_notice, a_privmsg
};
static const char action_args[] = {
	[a_join] = 1, [a_part] = 2,
	[a_quit] = 1, [a_kick] = 3,
	[a_notice] = 2, [a_privmsg] = 2,
};
static const char actionarg_msgadd[][3] = { /* this is to add 1 to the msg argument so the leading ':' is skipped */
	[a_join] = "\0\0\0", [a_part] = "\0\1\0",
	[a_quit] = "\1\0\0", [a_kick] = "\0\0\1",
	[a_notice] = "\0\1\0", [a_privmsg] = "\0\1\0",
};

/* nick+mask always go together in that order, i.e. 0,1. */
static const char action_order[][5] = {
	[a_join]  ="\2\0\1\n\n", [a_part]   = "\2\0\1\3\n",
	[a_quit]  ="\0\1\2\n\n", [a_kick]   = "\0\1\3\2\4",
	[a_notice]="\2\0\1\3\n", [a_privmsg]= "\2\0\1\3\n",
};
typedef void (*dispatchfunc)(const char*,const char*,const char*,const char*,const char*);
static const dispatchfunc dispatchtbl[]={
	[a_join] = joinhandler, [a_part] = parthandler,
	[a_quit] = quithandler, [a_kick] = kickhandler,
	[a_notice] = noticehandler, [a_privmsg] = msghandler,
};
static const char *action_arg(enum action a, int pos, const char* args[]) {
	int l = action_order[a][pos];
	return l == '\n' ? 0 : args[l];
}
static void action_dispatch(enum action a, const char* args[]) {
	const char *a0 = action_arg(a, 0, args);
	const char *a1 = action_arg(a, 1, args);
	const char *a2 = action_arg(a, 2, args);
	const char *a3 = action_arg(a, 3, args);
	const char *a4 = action_arg(a, 4, args);
	dispatchtbl[a](a0, a1, a2, a3, a4);
}
static void prep_action_handler(char *buf, size_t cmdpos, enum action a) {
	char nick[512];
	char mask[512];
	char cmd[16];
	char a1[512];
	char a2[512];
	char a3[512];
	size_t i;
	a1[0] = a2[0] = a3[0] = i = 0;
	split(buf+1, ' ', 2+action_args[a], mask, cmd, a1, a2, a3);
	while(mask[i] != '!' && mask[i] != ' ') i++;
	memcpy(nick, mask, i);
	nick[i] = 0;
	unsigned a1off = a1[0] ? actionarg_msgadd[a][0] : 0;
	unsigned a2off = a2[0] ? actionarg_msgadd[a][1] : 0;
	unsigned a3off = a3[0] ? actionarg_msgadd[a][2] : 0;
	const char* args[5] = {nick, mask, a1+a1off, a2+a2off, a3+a3off};
	action_dispatch(a, args);
}

static int motd_finished() {
	jscb_onconnect();
	return 0;
}

void switch_names(void) {
	char *t = own_nick;
	own_nick = alternate_nick;
	alternate_nick = t;
	rsirc_sendlinef(irc, "NICK %s", own_nick);
}

int read_cb(char* buf, size_t bufsize) {
	if(buf[0] == ':') {
		size_t i = 0, j, k;
		while(!isspace(buf[i])) i++;
		int cmd = atoi(buf+i);
		switch(cmd) {
			case 0: /* no number */
				j = ++i;
				while(!isspace(buf[j])) j++;
				switch(j - i) {
					case 4:
						if(!memcmp(buf+i,"JOIN", 4))
							prep_action_handler(buf, i, a_join);
						else if(!memcmp(buf+i,"PART", 4))
							prep_action_handler(buf, i, a_part);
						else if(!memcmp(buf+i,"QUIT", 4)) prep_action_handler(buf, i, a_quit);
						break;
					case 7:
						if(!memcmp(buf+i,"PRIVMSG", 7))
							prep_action_handler(buf, i, a_privmsg);
						break;
					case 6:
						if(!memcmp(buf+i,"NOTICE", 6))
							prep_action_handler(buf, i, a_notice);
					default:
						break;
				}
				break;
			/* status messages having the bot nickname in it, like:
			   :rajaniemi.freenode.net 255 foobot :I have 8369 clients and 1 servers */
			case 5: case 250: case 251: case 252: case 253:
			case 254: case 255: case 265: case 266: case 375:
				i++;
				while(!isspace(buf[i])) i++;
				j = ++i;
				while(!isspace(buf[i])) i++;
				buf[i] = 0;
				jscb_botnick(buf + j);
				break;
			case 376: motd_finished(); break;
			//:kornbluth.freenode.net 433 * foobot :Nickname is already in use.
			case 433:
				if(alternate_nick) switch_names();
				else {
					rocksock_disconnect(s);
					sleep(30);
				}
				break;
			default: break;
			case 366:
				while(!isspace(buf[++i]));
				i++;
				k = i;
				while(!isspace(buf[++i]));
				buf[i] = 0;
				j = ++i;
				while(!isspace(buf[++i]));
				buf[i] = 0;
				on_joinself(buf + j, buf + k);
				break;
		}
	} else {
		size_t i = 0;
		while(!isspace(buf[i])) i++;
		if(i == 4 && !memcmp(buf, "PING", 4)) ping_handler(buf + 5);
	}
	return 0;
}

char *cfgfilename;
static int load_cfg(void) {
	FILE *cfg = cfg_open(cfgfilename);
	if(!cfg) { perror("fopen"); return 0; }
	cfg_getstr(cfg, "nick1", nick1, sizeof(nick1));
	*nick2 = 0;
	cfg_getstr(cfg, "nick2", nick2, sizeof(nick2));
	cfg_getstr(cfg, "nick3", nick3, sizeof(nick3));
	cfg_getstr(cfg, "proxy", proxy, sizeof(proxy));
	int hostnr = (rand()%2)+1;
	char hb[10];
	again:
	snprintf(hb, sizeof hb, "host%d", hostnr);
	if(!cfg_getstr(cfg, hb, host, sizeof(host)) && hostnr == 2) { hostnr = 1; goto again; }
	port = cfg_getint(cfg, "port");
	use_ssl = cfg_getint(cfg, "ssl");
	cfg_getstr(cfg, "savefile", savefile, sizeof savefile);
	if(!savefile[0]) {
		dprintf(2, "error: savefile config item not set!\n");
		exit(1);
	}
	cfg_close(cfg);
	own_nick = nick1;
	if(*nick2) alternate_nick = nick2;
	return 1;
}

int connect_it(void) {
	load_cfg();
	if(done_rs_init) {
		rocksock_disconnect(s);
		rocksock_clear(s);
	}
	rocksock_init(s, proxies);
	done_rs_init = 1;
	rocksock_set_timeout(s, 36000);
	if(proxy[0])
		chk(rocksock_add_proxy_fromstring(s, proxy), exit(1));
	chk(rocksock_connect(s, host, port, use_ssl), goto err);
	chk(rsirc_init(irc, s), goto err);
	chk(rsirc_handshake(irc, host, own_nick, "foo"), goto err);

	return 1;
	err:
	usleep(TRANSACT_TIME);
	return 0;
}

static void js_sendline(js_State *J) {
	const char* msg = js_tostring(J, 1);
	int ret = rsirc_sendline(irc, msg);
	js_pushnumber(J, ret);
}

static void js_privmsg(js_State *J) {
	const char* chan = js_tostring(J, 1);
	const const char* msg = js_tostring(J, 2);
	int ret = rsirc_privmsg(irc, chan, msg);
	js_pushnumber(J, ret);
}

static void js_errmsg(js_State *J) {
	js_pushstring(J, rocksock_strerror(s));
}

static int reload_script() {
	if (js_dofile(J, "ircbot.js"))
		return 1;
	js_gc(J, 0);
	return 0;
}

static void js_reload(js_State *J) {
	int ret = reload_script();
	js_pushboolean(J, !ret);
}

static void js_disconnect(js_State *J) {
	rocksock_disconnect(s);
	js_pushundefined(J);
}

static void js_debugprint(js_State *J) {
	const char* msg = js_tostring(J, 1);
	dprintf(2, "%s\n", msg);
	js_pushundefined(J);
}

static void js_writesettings(js_State *J) {
	int fd, fail = 1; size_t l;
	const char* contents = js_tostring(J, 1);
	if( 0 == contents) goto err;
	if( 0 == (l = strlen(contents))) goto err;
	if(-1 == (fd = open(savefile, O_CREAT | O_WRONLY | O_TRUNC, 0660))) goto err;
	if( l != write(fd, contents, l)) goto fderr;
	fail = 0;
fderr:
	close(fd);
err:
	js_pushboolean(J, !fail);
}

static void js_readsettings(js_State *J) {
	struct stat st;
	char *contents;
	int fd, failed = 1;
	if(stat(savefile, &st)) goto err;
	if(-1 == (fd = open(savefile, O_RDONLY))) goto err;
	if( 0 == (contents = malloc(st.st_size + 1))) goto fderr;
	if(st.st_size != read(fd, contents, st.st_size)) goto mallerr;
	failed = 0;
	contents[st.st_size] = 0;
	js_pushstring(J, contents);
mallerr:
	free(contents);
fderr:
	close(fd);
err:
	if(failed) js_pushundefined(J);
}

static int syntax() { dprintf(2, "need filename of cfg file\n"); return 1; }
int main(int argc, char** argv) {
	if(argc <= 1) return syntax();
	cfgfilename = argv[1];
	srand(time(0));
	if(!load_cfg()) return 1;

	J = js_newstate(NULL, NULL, JS_STRICT);

	js_newcfunction(J, js_privmsg, "privmsg", 2);
	js_setglobal(J, "privmsg");

	js_newcfunction(J, js_sendline, "send", 1);
	js_setglobal(J, "send");

	js_newcfunction(J, js_errmsg, "errmsg", 0);
	js_setglobal(J, "errmsg");

	js_newcfunction(J, js_reload, "reload", 0);
	js_setglobal(J, "reload");

	js_newcfunction(J, js_writesettings, "writesettings", 1);
	js_setglobal(J, "writesettings");

	js_newcfunction(J, js_readsettings, "readsettings", 0);
	js_setglobal(J, "readsettings");

	js_newcfunction(J, js_disconnect, "disconnect", 0);
	js_setglobal(J, "disconnect");

	js_newcfunction(J, js_debugprint, "debugprint", 1);
	js_setglobal(J, "debugprint");

	if(reload_script()) {
		dprintf(2, "error: loading ircbot.js failed\n");
		return 1;
	}

	size_t rcvd;

	rocksock_init_ssl();

conn:
	while(!connect_it());

	while(!want_quit) {
		char line[512];
		char decodebuf[512*4];
		chk(rsirc_process(irc, line, &rcvd), goto conn);
		if(rcvd) {
			dprintf(2, "LEN %zu - %s\n", rcvd, decode(line, decodebuf));
			chk(read_cb(line, sizeof line), goto conn);
		}

		usleep(10000);
	}

	rocksock_disconnect(s);
	rocksock_free_ssl();
	return 0;
}
