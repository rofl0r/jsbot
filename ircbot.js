/* this bot has some privileged commands. in order to authenticate to the bot,
   send the command ",op" once you brought it online for the first time.
   it will then write your hostmask into the json savefile and will from then
   on only accept privileged commands from your hostmask.
   note: that means you should use a registered account so your hostmask
   is always the same. */

var botname = "";
var lastchan = "";
var connected = false;
var names;

var settings = (function() {
	var tmp = readsettings() ||
	'{"chans":{"#reaver":{"autojoin":true}},"doAuth":false,"nickservpass":"p4ssw0rd"}';
	return JSON.parse(tmp);
})();
function part(chan) {send("PART " + chan);}
function join(chan) {send("JOIN " + chan);}
function init_chan_settings(chan) {
	if(!(chan in settings.chans)) settings.chans[chan] = {};
}
function get_chan_setting(chan, setting) {
	init_chan_settings(chan);
	return settings.chans[chan][setting];
}
/* this function is the first function called after the bot is
 * connected. use it to load settings, auth, join channels, etc */
function connect() {
	connected = true;
	for(var chan in settings.chans) {
		if(get_chan_setting(chan, "autojoin")) join(chan);
	}
}

function save_settings() {writesettings(JSON.stringify(settings, null, "\t"));}

function set_global(key, value) {
	settings[key] = value;
	save_settings();
}
function set_chan(chan, key, value) {
	init_chan_settings(chan);
	settings.chans[chan][key] = value;
	save_settings();
}

function add_note(chan, mask, rcp, msg) {
	init_chan_settings(chan);
	if(!("notes" in settings.chans[chan]))
		settings.chans[chan]["notes"] = [];
	settings.chans[chan]["notes"].push(
		{"sender":nick_from_mask(mask), "recipient": rcp, "message":msg});
	save_settings();
	return true;
}
function dispatch_note(chan, index) {
	var note = settings.chans[chan]["notes"].splice(index,1)[0];
	privmsg(chan, note.recipient + ": " + note.sender + " left the following note for you: " + note.message);
	save_settings();
}
function check_notes(chan, usr) {
	if("notes" in settings.chans[chan]) {
		for (var i = 0; i < settings.chans[chan]["notes"].length;) {
			var note = settings.chans[chan]["notes"][i];
			if(note.recipient === usr) dispatch_note(chan, i);
			else i++;
		}
	}
}

function botnick(nick) {
	botname = nick;
}
function selfjoin(chan, nick) {
//	privmsg(settings.master, "selfjoin chan " + chan + ", nick "+ nick);
	botname = nick;
	lastchan = chan;
	if(get_chan_setting(chan, "dogreet")) privmsg(chan, "hi " + chan);
}
function has_status_sign(name) {
	return (name.charAt(0) === '@' || name.charAt(0) === '+');
}
function nameshandler(chan, namelist) {
	if(typeof names === 'undefined') names = {};
	var nameblock = namelist.split(" ");
	if(!(chan in names)) names[chan] = nameblock;
	else names[chan] = names[chan].concat(nameblock);

	// remove operator and voice signs
	for(var i=0; i<names[chan].length;i++) {
		if(has_status_sign(names[chan][i]))
			names[chan][i] = names[chan][i].substring(1);
	}
}
function add_name(chan, name) {
	names[chan].push(name);
}
function remove_name(chan, name) {
	for(var i = 0; i < names[chan].length; i++)
		if(names[chan][i] === name) {
			names[chan].splice(i,1);
			return;
		}
}
function change_name(chan, name, newname) {
	for(var i = 0; i < names[chan].length; i++)
		if(names[chan][i] === name) {
			names[chan][i] = newname;
			return;
		}
}
function remove_name_global(name) {
	for(var chan in names) {
		remove_name(chan, name);
	}
}
function nickchange(oldnick, newnick) {
	//privmsg(settings.master, "nickchange " + oldnick + " -> " + newnick);
	for(var chan in names) {
		change_name(chan, oldnick, newnick);
	}
}
function quithandler(nick, mask, msg) {
	if(get_chan_setting(lastchan, "dogreet"))
		privmsg(lastchan, "hmm. " + nick + " didn't like it here (" + msg + ")");
	remove_name_global(nick);
}
function joinhandler(chan, nick, mask) {
	lastchan = chan;
	if(nick !== botname) {
		if(get_chan_setting(chan, "dogreet"))
			privmsg(chan, "Hello "+nick+". I am " + settings.master + "'s bot. If you want to talk to him, please be patient and wait here.");
		add_name(chan, nick);
		check_notes(chan, nick);
	}
}
function parthandler(chan, nick, mask, msg) {
	lastchan = chan;
	if(get_chan_setting(chan, "dogreet"))
		privmsg(chan, "oh, "+nick+" left :/ (reason: " + msg + ")");
	remove_name(chan, nick);
}
function kickhandler(nick, mask, whom, chan, msg) {
	privmsg(chan, "WOOT! " + nick + " kicked " + whom + "because of " + msg);
}

function nick_from_mask(mask) {
	return(mask.substring(0, mask.indexOf('!')));
}

function hex2bin(input) {
	var hextab = "0123456789abcdef";
	var out = "";
	for(var i = 0; i < input.length; i+=2) {
		var h1 = hextab.indexOf(input.charAt(i));
		var h2 = hextab.indexOf(input.charAt(i+1));
		out = out + String.fromCharCode((h1 << 4) | h2);
	}
	return out;
};

function sanitize_value(v) {
	if(v=== "false" || v === "0") return false;
	if(v === "true" || v === "1") return true;
	return v;
}

function runcmd(chan, mask, cmd, args) {
	var isop = settings.opmask ? mask.match(RegExp(settings.opmask)) : false;
	if(0) {
	} else if(cmd === 'op' && !settings.opmask) {
		var a = mask.split("!");
		settings.opmask = ".*!" + a[1];
		settings.master = a[0];
		save_settings();
		privmsg(chan, "opmask assigned");
	} else if(cmd === "reload" && isop) {
		if(!reload()) privmsg(chan, "oops. reload failed.");
		else privmsg(chan, "reload OK.");
	} else if(cmd === 'say') {
		privmsg(chan, args);
	} else if(cmd === 'nick') {
		privmsg(chan, nick_from_mask(mask));
	} else if(cmd === 'eval' && isop) {
		privmsg(chan, eval(args));
	} else if(cmd === 'disconnect' && isop) {
		connected = false;
		names = {};
		disconnect();
	} else if(cmd === 'join' && isop) {
		join(args);
	} else if(cmd == 'note' && isop) {
		var x = args.indexOf(' ');
		if(x === -1) {
			privmsg(chan, "syntax: note recipient message");
			return;
		}
		var rcp = args.substring(0,x);
		var msg = args.substring(x+1);
		if(add_note(chan, mask, rcp, msg))
			privmsg(chan, "your note to " + rcp + " was saved, " + nick_from_mask(mask));
	} else if((cmd == 'setg' || cmd == 'setc') && isop) {
		var x = args.indexOf(' ');
		if(x == -1) {
			privmsg(chan, "invalid command");
			return;
		}
		var k = args.substring(0,x);
		var v = sanitize_value(args.substring(x+1));
		if(cmd == 'setg') set_global(k,v);
		else set_chan(chan, k, v);
	} else if(cmd == 'leave' && isop) {
		part(chan);
	} else if(cmd == 'settings' && isop) {
		privmsg(chan, JSON.stringify(settings));
	} else if(cmd == 'hex' && isop) {
		// attention, this can be abused to make the bot send raw commands
		// for example with 0a51554954
		privmsg(chan, hex2bin(args));
	}
}

function noticehandler(dest, nick, mask, msg) {
	/*ignore for now
	if(connected && lastchan && dest.charAt(0) !== '*')
		privmsg(master, nick + " noticed " + dest + " of " + msg);
	*/
}

function msghandler(chan, nick, mask, msg) {
	lastchan = chan;
	var query = false;
	if(chan.charAt(0) !== '#') {
		query = true;
		botname = chan;
		chan = nick;
	}
	if(nick == botname) return;
	if(msg.charAt(0) == ',') {
		var x = msg.indexOf(' ');
		if(x == -1) x = msg.length;
		var cmd = msg.substring(1,x);
		var args = msg.substring(x+1);
		runcmd(chan, mask, cmd, args);
	}
}
