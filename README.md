jsbot - an efficient javascript IRC bot
=======================================

this is a very space- and memory-efficient javascript IRC bot.
the core is written in C and uses [mujs][0] as a tiny javascript interpreter,
and [rocksock][1] as a very efficient network socket implementation with
SSL support.
a static linked binary is about 350 KB, if it was linked against
[musl libc][2], and rocksock was built against [wolfssl][3].

usage
-----

in order to use the ircbot, you need a config file:

```
#comment
host1=chat.freenode.net
host2=kornbluth.freenode.net
port=6697
ssl=1
nick1=mybot
nick2=mybot_
proxy=socks4://127.0.0.1:9050
savefile=mybot.json
```

then start the bot like `./jsbot mybot.cfg`.
you also need a file `ircbot.js` that implements the callbacks the C code
tries to call. look at the provided example to see how it works.
the bot saves its settings into the filename you provide as savefile in
the config, when it calls its writesettings() callback.

compilation
-----------

- install mujs, C compiler, perl (for rcb)

```sh
cd /tmp
mkdir jsbot-0000
cd jsbot-0000/
git clone https://github.com/rofl0r/jsbot
git clone https://github.com/rofl0r/rocksock
git clone https://github.com/rofl0r/rcb
export PATH=$PATH:/tmp/jsbot-0000/rcb
ln -s /tmp/jsbot-0000/rcb/rcb.pl /tmp/jsbot-0000/rcb/rcb
cd jsbot

#you may edit config.mak to override settings from Makefile
#nano config.mak

make
```

alternative compilation method
------------------------------

if you don't want to use the above method, for example because you don't have
perl, you can compile and install rocksock the standard way
(`./configure && make && make install`), then compile rocksock's rsirc.c
`gcc -c rocksockirc/rsirc.c`
then compile jsbot.c
`gcc jsbot.c rocksockirc/rsirc.o -lrocksock -lmujs -o jsbot`.
this is to give you an idea what needs to be done, it may work slightly
different in your case.

legal stuff
-----------

the code within this project is licensed under the GNU GPLv2 or later.
the mujs library on which it depends is licensed under the GNU AGPLv3.
due to the viral nature of the licences, this means that complete project
(and hence any binary distributed versions) are covered by the GNU AGPL,
rather than the less restrictive GNU GPL.


[0]:http://mujs.com/
[1]:https://github.com/rofl0r/rocksock
[2]:http://www.musl-libc.org/
[3]:https://wolfssl.com/
