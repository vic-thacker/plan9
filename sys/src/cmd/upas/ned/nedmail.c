#include "common.h"
#include <ctype.h>
#include <plumb.h>

typedef struct Message Message;
typedef struct Ctype Ctype;
typedef struct Cmd Cmd;

char	root[3*NAMELEN];
char	mbname[NAMELEN];
int	rootlen;
char	*user;
char	wd[2048];
String	*mbpath;

int interrupted;

struct Message {
	Message	*next;
	Message	*prev;
	Message	*cmd;
	Message	*child;
	Message	*parent;
	String	*path;
	int	id;
	int	len;
	int	fileno;	// number of directory
	String	*info;
	char	*from;
	char	*to;
	char	*cc;
	char	*replyto;
	char	*date;
	char	*subject;
	char	*type;
	char	*disposition;
	char	*filename;
	char	deleted;
	char	stored;
};

Message top;

struct Ctype {
	char	*type;
	char 	*ext;
	int	display;
	char	*plumbdest;
};

Ctype ctype[] = {
	{ "text/plain",			"txt",	1,	0	},
	{ "message/rfc822",		"msg",	1,	0	},
	{ "text/html",			"html",	1,	0	},
	{ "text/tab-separated-values",	"tsv",	1,	0	},
	{ "text/richtext",		"rtx",	1,	0	},
	{ "text",			"txt",	1,	0	},
	{ "image/jpeg",			"jpg",	0,	"image"	},
	{ "image/gif",			"gif",	0,	"image"	},
	{ "application/pdf",		"pdf",	0,	""	},
	{ "application/postscript",	"ps",	0,	""	},
	{ "application/",		0,	0,	0	},
	{ "image/",			0,	0,	0	},
	{ "multipart/",			"mul",	0,	0	},
	{ "", 				0,	0,	0	},
};

Message*	acmd(Cmd*, Message*);
Message*	bcmd(Cmd*, Message*);
Message*	dcmd(Cmd*, Message*);
Message*	eqcmd(Cmd*, Message*);
Message*	hcmd(Cmd*, Message*);
Message*	helpcmd(Cmd*, Message*);
Message*	icmd(Cmd*, Message*);
Message*	pcmd(Cmd*, Message*);
Message*	qcmd(Cmd*, Message*);
Message*	rcmd(Cmd*, Message*);
Message*	scmd(Cmd*, Message*);
Message*	ucmd(Cmd*, Message*);
Message*	wcmd(Cmd*, Message*);
Message*	xcmd(Cmd*, Message*);
Message*	pipecmd(Cmd*, Message*);
Message*	bangcmd(Cmd*, Message*);
Message*	Pcmd(Cmd*, Message*);
Message*	mcmd(Cmd*, Message*);
Message*	fcmd(Cmd*, Message*);

struct {
	char		*cmd;
	int		args;
	Message*	(*f)(Cmd*, Message*);
	char		*help;
} cmdtab[] = {
	{ "a",		1,	acmd,		"a        reply to sender and recipients" },
	{ "A",		1,	acmd,		"A        reply to sender and recipients with copy" },
	{ "b",		0,	bcmd,		"b        print the next 10 headers" },
	{ "d",		0,	dcmd,		"d        mark for deletion" },
	{ "f",		0,	fcmd,		"f        file message by from address" },
	{ "h",		0,	hcmd,		"h        print message summary (,h for all)" },
	{ "help",	0,	helpcmd,	"help     print this info" },
	{ "i",		0,	icmd,		"i        incorporate new mail" },
	{ "m",		1,	mcmd,		"m addr   forward mail" },
	{ "M",		1,	mcmd,		"M addr   forward mail with message" },
	{ "p",		0,	pcmd,		"p        print the processed message" },
	{ "P",		0,	Pcmd,		"P        print the raw message" },
	{ "q",		0,	qcmd,		"q        exit and remove all deleted mail" },
	{ "r",		1,	rcmd,		"r [addr] reply to sender plus any addrs specified" },
	{ "rf",		1,	rcmd,		"rf [addr]reply/file message and reply" },
	{ "R",		1,	rcmd,		"R [addr] reply including copy of message" },
	{ "Rf",		1,	rcmd,		"Rf [addr]reply/file message and reply with copy" },
	{ "s",		1,	scmd,		"s file   append raw message to file" },
	{ "u",		0,	ucmd,		"u        remove deletion mark" },
	{ "w",		1,	wcmd,		"w file   store message contents as file" },
	{ "x",		0,	xcmd,		"x        exit with mailbox unchanged" },
	{ "=",		1,	eqcmd,		"=        print current message number" },
	{ "|",		1,	pipecmd,	"|cmd     pipe raw message to a command" },
	{ "!",		1,	bangcmd,	"!cmd     run a command" },
	{ nil,		0,	nil, 		nil },
};

enum
{
	NARG=	32,
};

struct Cmd {
	Message	*msgs;
	Message	*(*f)(Cmd*, Message*);
	int	an;
	char	*av[NARG];
	int	delete;
};

Biobuf out;
int startedfs;
int reverse;

String*		file2string(String*, char*);
int		dir2message(Message*, int);
int		filelen(String*, char*);
String*		extendpath(String*, char*);
void		snprintheader(char*, int, Message*);
void		cracktime(char*, char*, int);
int		cistrncmp(char*, char*, int);
int		cistrcmp(char*, char*);
Reprog*		parsesearch(char**);
char*		parseaddr(char**, Message*, Message*, Message*, Message**);
char*		parsecmd(char*, Cmd*, Message*, Message*);
char*		readline(char*, char*, int);
void		messagecount(Message*);
void		system(char*, char**, int);
void		mkid(String*, Message*);
int		switchmb(char*, char*);
void		closemb(void);
int		lineize(char*, char**, int);
int		rawsearch(Message*, Reprog*);
void		creatembox(char*, char*);
Message*	dosingleton(Message*, char*);
String*		rooted(String*);
void		plumb(Message*, Ctype*);
String*		addrecolon(char*);
void		exitfs(char*);

void
usage(void)
{
	fprint(2, "usage: %s [-f mboxdir]\n", argv0);
	exits("usage");
}

void
catchnote(void*, char *note)
{
	if(strstr(note, "interrupt") != nil){
		interrupted = 1;
		noted(NCONT);
	}
	noted(NDFLT);
}

void
main(int argc, char **argv)
{
	Message *cur, *m, *x;
	char cmdline[4*1024];
	Cmd cmd;
	char *err;
	int n, cflag;
	char *av[4];
	String *prompt;
	char *file, *singleton;

	Binit(&out, 1, OWRITE);

	file = "mbox";
	singleton = nil;
	reverse = 1;
	cflag = 0;
	ARGBEGIN {
	case 'c':
		cflag = 1;
		break;
	case 'f':
		file = ARGF();
		if(file == nil)
			usage();
		break;
	case 's':
		singleton = ARGF();
		break;
	case 'r':
		reverse = 0;
		break;
	} ARGEND;

	user = getlog();
	if(user == nil || *user == 0)
		sysfatal("can't read user name");

	if(cflag){
		if(argc > 0)
			creatembox(user, argv[0]);
		else
			creatembox(user, nil);
		exits(0);
	}

	if(access("/mail/fs/ctl", 0) < 0){
		startedfs = 1;
		av[0] = "fs";
		av[1] = "-p";
		av[2] = 0;
		system("/bin/upas/fs", av, -1);
	}

	switchmb(file, singleton);

	top.path = s_copy(root);

	if(singleton != nil){
		cur = dosingleton(&top, singleton);
		if(cur == nil){
			Bprint(&out, "no message\n");
			exitfs(0);
		}
		pcmd(nil, cur);
	} else {
		cur = &top;
		n = dir2message(&top, reverse);
		if(n < 0)
			sysfatal("can't read %s\n", s_to_c(top.path));
		Bprint(&out, "%d messages\n", n);
	}

	notify(catchnote);
	prompt = s_new();
	for(;;){
		s_reset(prompt);
		if(cur == &top)
			s_append(prompt, ": ");
		else {
			mkid(prompt, cur);
			s_append(prompt, ": ");
		}
		if(readline(s_to_c(prompt), cmdline, sizeof(cmdline)) == nil)
			break;
		err = parsecmd(cmdline, &cmd, top.child, cur);
		if(err != nil){
			Bprint(&out, "!%s\n", err);
			continue;
		}
		if(singleton != nil && cmd.f == icmd){
			Bprint(&out, "!illegal command\n");
			continue;
		}
		interrupted = 0;
		if(cmd.msgs == nil || cmd.msgs == &top){
			x = (*cmd.f)(&cmd, &top);
			if(x != nil)
				cur = x;
		} else for(m = cmd.msgs; m != nil; m = m->cmd){
			x = m;
			if(cmd.delete){
				dcmd(&cmd, x);

				// dp acts differently than all other commands
				// since its an old lesk idiom that people love.
				// it deletes the current message, moves the current
				// pointer ahead one and prints.
				if(cmd.f == pcmd){
					if(x->next == nil){
						Bprint(&out, "!address\n");
						cur = x;
						break;
					} else
						x = x->next;
				}
			}
			x = (*cmd.f)(&cmd, x);
			if(x != nil)
				cur = x;
			if(interrupted)
				break;
			if(singleton != nil && (cmd.delete || cmd.f == dcmd))
				qcmd(nil, nil);
		}
	}
	qcmd(nil, nil);
}

//
// read the message info
//
Message*
file2message(Message *parent, char *name)
{
	Message *m;
	String *path;
	char *f[10];

	m = mallocz(sizeof(Message), 1);
	if(m == nil)
		return nil;
	m->path = path = extendpath(parent->path, name);
	m->fileno = atoi(name);
	m->info = file2string(path, "info");
	lineize(s_to_c(m->info), f, nelem(f));
	m->from = f[0];
	m->to = f[1];
	m->cc = f[2];
	m->replyto = f[3];
	m->date = f[4];
	m->subject = f[5];
	m->type = f[6];
	m->disposition = f[7];
	m->filename = f[8];
	m->len = filelen(path, "raw");
	dir2message(m, 0);
	m->parent = parent;

	return m;
}

//
//  read a directory into a list of messages
//
int
dir2message(Message *parent, int reverse)
{
	int i, n, fd, highest, newmsgs;
	Dir d[128];
	Message *first, *last, *m;

	fd = open(s_to_c(parent->path), OREAD);
	if(fd < 0)
		return -1;

	// count current entries
	first = parent->child;
	highest = newmsgs = 0;
	for(last = parent->child; last != nil && last->next != nil; last = last->next)
		if(last->fileno > highest)
			highest = last->fileno;
	if(last != nil)
		if(last->fileno > highest)
			highest = last->fileno;

	while((n = dirread(fd, d, sizeof(d))) >= sizeof(Dir)){
		n /= sizeof(Dir);
		for(i = 0; i < n; i++){
			if((d[i].qid.path & CHDIR) == 0)
				continue;
			if(atoi(d[i].name) <= highest)
				continue;
			m = file2message(parent, d[i].name);
			if(m == nil)
				break;
			newmsgs++;
			if(reverse){
				m->next = first;
				if(first != nil)
					first->prev = m;
				first = m;
			} else {
				if(first == nil)
					first = m;
				else
					last->next = m;
				m->prev = last;
				last = m;
			}
		}
	}
	close(fd);
	parent->child = first;

	// renumber
	i = 1;
	for(m = first; m != nil; m = m->next)
		m->id = i++;

	return newmsgs;
}

//
//  point directly to a message
//
Message*
dosingleton(Message *parent, char *path)
{
	char *p, *np;
	Message *m;

	// walk down to message and read it
	if(strlen(path) < rootlen)
		return nil;
	if(path[rootlen] != '/')
		return nil;
	p = path+rootlen+1;
	np = strchr(p, '/');
	if(np != nil)
		*np = 0;
	m = file2message(parent, p);
	if(m == nil)
		return nil;
	parent->child = m;
	m->id = 1;

	// walk down to requested component
	while(np != nil){
		*np = '/';
		np = strchr(np+1, '/');
		if(np != nil)
			*np = 0;
		for(m = m->child; m != nil; m = m->next)
			if(strcmp(path, s_to_c(m->path)) == 0)
				return m;
		if(m == nil)
			return nil;
	}
	return m;
}

//
//  read a file into a string
//
String*
file2string(String *dir, char *file)
{
	String *s;
	int fd, n;

	s = extendpath(dir, file);
	fd = open(s_to_c(s), OREAD);
	s_reset(s);
	if(fd < 0)
		return s;

	for(;;){
		n = s->end - s->ptr;
		if(n == 0){
			s_simplegrow(s, 128);
			continue;
		}
		n = read(fd, s->ptr, n);
		if(n <= 0)
			break;
		s->ptr += n;
	}
	s_terminate(s);
	close(fd);

	return s;
}

//
//  get the length of a file
//
int
filelen(String *dir, char *file)
{
	String *path;
	Dir d;

	path = extendpath(dir, file);
	if(dirstat(s_to_c(path), &d) < 0){
		s_free(path);
		return -1;
	}
	s_free(path);
	return d.length;
}

//
//  walk the path name an element
//
String*
extendpath(String *dir, char *name)
{
	String *path;

	if(strcmp(s_to_c(dir), ".") == 0)
		path = s_new();
	else {
		path = s_copy(s_to_c(dir));
		s_append(path, "/");
	}
	s_append(path, name);
	return path;
}

int
cistrncmp(char *a, char *b, int n)
{
	while(n-- > 0){
		if(tolower(*a++) != tolower(*b++))
			return -1;
	}
	return 0;
}

int
cistrcmp(char *a, char *b)
{
	for(;;){
		if(tolower(*a) != tolower(*b++))
			return -1;
		if(*a++ == 0)
			break;
	}
	return 0;
}

char*
nosecs(char *t)
{
	char *p;

	p = strchr(t, ':');
	if(p == nil)
		return t;
	p = strchr(p+1, ':');
	if(p != nil)
		*p = 0;
	return t;
}

void
cracktime(char *d, char *out, int len)
{
	char in[64];
	char *f[6];
	int n;

	*out = 0;
	if(d == nil)
		return;
	strncpy(in, d, sizeof(in));
	in[sizeof(in)-1] = 0;
	n = tokenize(in, f, 6);
	if(n != 6){
		// unknown style
		snprint(out, 16, "%10.10s", d);
		return;
	}
	if(strchr(f[0], ',') != nil && strchr(f[4], ':') != nil){
		// 822 style
		snprint(out, len, "%s %s %s", f[2], f[1], nosecs(f[4]));
	} else if(strchr(f[3], ':') != nil){
		// unix style
		snprint(out, len, "%s %s %s", f[1], f[2], nosecs(f[3]));
	} else {
		snprint(out, len, "%.16s", d);
	}
}

Ctype*
findctype(char *t)
{
	Ctype *cp;

	for(cp = ctype; ; cp++)
		if(strncmp(cp->type, t, strlen(cp->type)) == 0)
			break;
	return cp;
}

void
mkid(String *s, Message *m)
{
	char buf[32];

	if(m->parent != &top){
		mkid(s, m->parent);
		s_append(s, ".");
	}
	sprint(buf, "%d", m->id);
	s_append(s, buf);
}

void
snprintheader(char *buf, int len, Message *m)
{
	Ctype *cp;
	char timebuf[32];
	String *id;

	// create id
	id = s_new();
	mkid(id, m);

	// decode type
	cp = findctype(m->type);

	if(*m->from == 0){
		// no from
		snprint(buf, len, "%-5s    %s %5d %s",
			s_to_c(id),
			cp->ext != nil ? cp->ext : m->type,
			m->len,
			m->filename);
	} else if(*m->subject){
		cracktime(m->date, timebuf, sizeof(timebuf));
		snprint(buf, len, "%-5s %c%c %s %5d\t%12.12s  %s  \"%s\"",
			s_to_c(id),
			m->deleted ? 'd' : ' ',
			m->stored ? 's' : ' ',
			cp->ext != nil ? cp->ext : m->type, m->len,
			timebuf,
			m->from,
			m->subject);
	} else {
		cracktime(m->date, timebuf, sizeof(timebuf));
		snprint(buf, len, "%-5s %c%c %s %5d\t%12.12s  %s",
			s_to_c(id),
			m->deleted ? 'd' : ' ',
			m->stored ? 's' : ' ',
			cp->ext != nil ? cp->ext : m->type, m->len,
			timebuf,
			m->from);
	}
	s_free(id);
}

char sstring[256];

//	cmd := range cmd ' ' arg-list ; 
//	range := address
//		| address ',' address
//		| 'g' search ;
//	address := msgno
//		| search ;
//	msgno := number
//		| number '/' msgno ;
//	search := '/' string '/'
//		| '%' string '%' ;
//
Reprog*
parsesearch(char **pp)
{
	char *p, *np;
	int c, n;

	p = *pp;
	c = *p++;
	np = strchr(p, c);
	if(np != nil){
		*np++ = 0;
		*pp = np;
	} else {
		n = strlen(p);
		*pp = p + n;
	}
	if(*p == 0)
		p = sstring;
	else{
		strncpy(sstring, p, sizeof(sstring));
		sstring[sizeof(sstring)-1] = 0;
	}
	return regcomp(p);
}

char*
parseaddr(char **pp, Message *first, Message *cur, Message *unspec, Message **mp)
{
	int n;
	Message *m;
	char *p;
	Reprog *prog;
	int c, sign;
	char buf[256];

	*mp = nil;
	p = *pp;

	if(*p == '+'){
		sign = 1;
		p++;
		*pp = p;
	} else if(*p == '-'){
		sign = -1;
		p++;
		*pp = p;
	} else
		sign = 0;

	switch(*p){
	default:
		if(sign){
			n = 1;
			goto number;
		}
		*mp = unspec;
		break;	
	case '0': case '1': case '2': case '3': case '4':
	case '5': case '6': case '7': case '8': case '9':
		n = strtoul(p, pp, 10);
		if(n == 0){
			if(sign)
				*mp = cur;
			else
				*mp = &top;
			break;
		}
	number:
		m = nil;
		switch(sign){
		case 0:
			for(m = first; m != nil; m = m->next)
				if(m->id == n)
					break;
			break;
		case -1:
			if(cur != &top)
				for(m = cur; m != nil && n > 0; n--)
					m = m->prev;
			break;
		case 1:
			if(cur == &top){
				n--;
				cur = first;
			}
			for(m = cur; m != nil && n > 0; n--)
				m = m->next;
			break;
		}
		if(m == nil)
			return "address";
		*mp = m;
		break;
	case '%':
	case '/':
	case '?':
		c = *p;
		prog = parsesearch(pp);
		if(prog == nil)
			return "badly formed regular expression";
		m = nil;
		switch(c){
		case '%':
			for(m = cur == &top ? first : cur->next; m != nil; m = m->next){
				if(rawsearch(m, prog))
					break;
			}
			break;
		case '/':
			for(m = cur == &top ? first : cur->next; m != nil; m = m->next){
				snprintheader(buf, sizeof(buf), m);
				if(regexec(prog, buf, nil, 0))
					break;
			}
			break;
		case '?':
			for(m = cur == &top ? nil : cur->prev; m != nil; m = m->prev){
				snprintheader(buf, sizeof(buf), m);
				if(regexec(prog, buf, nil, 0))
					break;
			}
			break;
		}
		if(m == nil)
			return "search";
		*mp = m;
		free(prog);
		break;
	case '$':
		for(m = first; m != nil && m->next != nil; m = m->next)
			;
		*mp = m;
		*pp = p+1;
		break;
	case '.':
		*mp = cur;
		*pp = p+1;
		break;
	case ',':
		*mp = first;
		*pp = p;
		break;
	}

	if(*mp != nil && **pp == '.'){
		(*pp)++;
		return parseaddr(pp, (*mp)->child, (*mp)->child, (*mp)->child, mp);
	}
	if(**pp == '+' || **pp == '-' || **pp == '/' || **pp == '%')
		return parseaddr(pp, first, *mp, *mp, mp);

	return nil;
}

//
//  search a message for a regular expression match
//
int
rawsearch(Message *m, Reprog *prog)
{
	char buf[4096+1];
	int i, fd, rv;
	String *path;

	path = extendpath(m->path, "raw");
	fd = open(s_to_c(path), OREAD);
	if(fd < 0)
		return 0;

	// march through raw message 4096 bytes at a time
	// with a 128 byte overlap to chain the re search.
	rv = 0;
	for(;;){
		i = read(fd, buf, sizeof(buf)-1);
		if(i <= 0)
			break;
		buf[i] = 0;
		if(regexec(prog, buf, nil, 0)){
			rv = 1;
			break;
		}
		if(i < sizeof(buf)-1)
			break;
		if(seek(fd, -128LL, 1) < 0)
			break;
	}

	close(fd);
	s_free(path);
	return rv;
}


char*
parsecmd(char *p, Cmd *cmd, Message *first, Message *cur)
{
	Reprog *prog;
	Message *m, *s, *e, **l, *last;
	char buf[256];
	char *err;
	int i, c;
	static char errbuf[ERRLEN];

	cmd->delete = 0;
	l = &cmd->msgs;
	*l = nil;

	// eat white space
	while(*p == ' ')
		p++;

	// null command is a special case (advance and print)
	if(*p == 0){
		if(cur == &top){
			// special case
			m = first;
		} else {
			// walk to the next message even if we have to go up
			m = cur->next;
			while(m == nil && cur->parent != nil){
				cur = cur->parent;
				m = cur->next;
			}
		}
		if(m == nil)
			return "address";
		*l = m;
		m->cmd = nil;
		cmd->an = 0;
		cmd->f = pcmd;
		return nil;
	}

	// global search ?
	if(*p == 'g'){
		p++;

		// no search string means all messages
		if(*p != '/' && *p != '%'){
			for(m = first; m != nil; m = m->next){
				*l = m;
				l = &m->cmd;
				*l = nil;
			}
			return nil;
		}

		// mark all messages matching this search string
		c = *p;
		prog = parsesearch(&p);
		if(prog == nil)
			return "badly formed regular expression";
		if(c == '%'){
			for(m = first; m != nil; m = m->next){
				if(rawsearch(m, prog)){
					*l = m;
					l = &m->cmd;
					*l = nil;
				}
			}
		} else {
			for(m = first; m != nil; m = m->next){
				snprintheader(buf, sizeof(buf), m);
				if(regexec(prog, buf, nil, 0)){
					*l = m;
					l = &m->cmd;
					*l = nil;
				}
			}
		}
		free(prog);
	} else {
	
		// parse an address
		s = e = nil;
		err = parseaddr(&p, first, cur, cur, &s);
		if(err != nil)
			return err;
		if(*p == ','){
			// this is an address range
			if(s == &top)
				s = first;
			p++;
			for(last = s; last != nil && last->next != nil; last = last->next)
				;
			err = parseaddr(&p, first, cur, last, &e);
			if(err != nil)
				return err;
	
			// select all messages in the range
			for(; s != nil; s = s->next){
				*l = s;
				l = &s->cmd;
				*l = nil;
				if(s == e)
					break;
			}
			if(s == nil)
				return "null address range";
		} else {
			// single address
			if(s != &top){
				*l = s;
				s->cmd = nil;
			}
		}
	}

	cmd->an = tokenize(p, cmd->av, nelem(cmd->av) - 1);
	if(cmd->an == 0 || *cmd->av[0] == 0)
		cmd->f = pcmd;
	else {
		// hack to avoid space after '|' or '!'
		if((*p == '!' || *p == '|') && *(p+1) != 0){
			for(i = cmd->an; i > 0 ; i--)
				cmd->av[i] = cmd->av[i-1];
			cmd->av[0] = *p == '!' ? "!" : "|";
			cmd->av[1]++;
			cmd->an++;
		}

		// hack to allow all messages to start with 'd'
		if(*(cmd->av[0]) == 'd' && *(cmd->av[0]+1) != 0){
			cmd->delete = 1;
			cmd->av[0]++;
		}

		// search command table
		for(i = 0; cmdtab[i].cmd != nil; i++)
			if(strcmp(cmd->av[0], cmdtab[i].cmd) == 0)
				break;
		if(cmdtab[i].cmd == nil)
			return "illegal command";
		if(cmdtab[i].args == 0 && cmd->an > 1){
			snprint(errbuf, sizeof(errbuf), "%s doesn't take an argument", cmdtab[i].cmd);
			return errbuf;
		}
		cmd->f = cmdtab[i].f;
	}
	return nil; 
}

// inefficient read from standard input
char*
readline(char *prompt, char *line, int len)
{
	char *p, *e;
	int n;

retry:
	interrupted = 0;
	Bprint(&out, "%s", prompt);
	Bflush(&out);
	e = line + len;
	for(p = line; p < e; p++){
		n = read(0, p, 1);
		if(n < 0){
			if(interrupted)
				goto retry;
			return nil;
		}
		if(n == 0)
			return nil;
		if(*p == '\n')
			break;
	}
	*p = 0;
	return line;
}

void
messagecount(Message *m)
{
	int i;

	i = 0;
	for(; m != nil; m = m->next)
		i++;
	Bprint(&out, "%d messages\n", i);
}

Message*
hcmd(Cmd*, Message *m)
{
	char	hdr[80];

	if(m == &top)
		return nil;
	snprintheader(hdr, sizeof(hdr), m);
	Bprint(&out, "%s\n", hdr);
	for(m = m->child; m != nil; m = m->next)
		hcmd(nil, m);
	return nil;
}

Message*
bcmd(Cmd*, Message *m)
{
	int i;
	Message *om = m;

	if(m == &top)
		m = top.child;
	for(i = 0; i < 10 && m != nil; i++){
		hcmd(nil, m);
		om = m;
		m = m->next;
	}

	return om;
}

Message*
ncmd(Cmd*, Message *m)
{
	if(m == &top)
		return m->child;
	return m->next;
}

int
printpart(String *s, char *part)
{
	char buf[4096];
	int n, fd, tot;
	String *path;

	path = extendpath(s, part);
	fd = open(s_to_c(path), OREAD);
	s_free(path);
	if(fd < 0){
		fprint(2, "!message dissappeared\n");
		return 0;
	}
	tot = 0;
	while((n = read(fd, buf, sizeof(buf))) > 0){
		if(interrupted)
			break;
		if(Bwrite(&out, buf, n) <= 0)
			break;
		tot += n;
	}
	close(fd);
	return tot;
}

Message*
Pcmd(Cmd*, Message *m)
{
	if(m == &top)
		return &top;
	if(m->parent == &top)
		printpart(m->path, "unixheader");
	printpart(m->path, "raw");
	return m;
}

Message*
pcmd(Cmd*, Message *m)
{
	Message *nm;
	Ctype *cp;
	String *s;
	char hdr[80];

	if(m == &top)
		return &top;
	if(m->parent == &top)
		printpart(m->path, "unixheader");
	if(printpart(m->path, "header") > 0)
		Bprint(&out, "\n");
	cp = findctype(m->type);
	if(cp->display){
		printpart(m->path, "body");
	} else if(cp->plumbdest != nil){
		plumb(m, cp);
	} else if(strcmp(m->type, "multipart/alternative") == 0){
		for(nm = m->child; nm != nil; nm = nm->next){
			cp = findctype(nm->type);
			if(strncmp(cp->ext, "txt", 3) == 0)
				break;
		}
		if(nm == nil)
			for(nm = m->child; nm != nil; nm = nm->next){
				cp = findctype(nm->type);
				if(cp->display)
					break;
			}
		if(nm != nil)
			pcmd(nil, nm);
		else
			hcmd(nil, m);
	} else if(strncmp(m->type, "multipart/", 10) == 0){
		nm = m->child;
		if(nm != nil){
			cp = findctype(nm->type);
			if(cp->display || strncmp(m->type, "multipart/", 10) == 0)
				pcmd(nil, nm);
			for(nm = nm->next; nm != nil; nm = nm->next){
				snprintheader(hdr, sizeof(hdr), nm);
				if(strcmp(nm->disposition, "inline") == 0){
					s = rooted(s_clone(nm->path));
					cp = findctype(nm->type);
					if(cp->ext != nil)
						Bprint(&out, "\n--- attachment: %s %s/body.%s\n\n",
							hdr, s_to_c(s), cp->ext);
					else
						Bprint(&out, "\n--- attachment: %s %s/body\n\n",
							hdr, s_to_c(s));
					s_free(s);
					pcmd(nil, nm);
				} else {
					s = rooted(s_clone(nm->path));
					cp = findctype(nm->type);
					if(cp->ext != nil)
						Bprint(&out, "\n!--- attachment: %s %s/body.%s\n",
							hdr, s_to_c(s), cp->ext);
					else
						Bprint(&out, "\n!--- attachment: %s %s/body\n",
							hdr, s_to_c(s));
					s_free(s);
				}
			}
		} else {
			hcmd(nil, m);
		}
	}
	return m;
}

Message*
qcmd(Cmd*, Message*)
{
	Message *m;
	char buf[1024], *p, *e, *msg;
	int deld, n, fd;

	deld = 0;

	// really delete messages
	fd = open("/mail/fs/ctl", ORDWR);
	if(fd < 0){
		fprint(2, "!can't delete mail, opening /mail/fs/ctl: %r\n");
		exitfs(0);
	}
	e = &buf[sizeof(buf)];
	p = seprint(buf, e, "delete %s", mbname);
	n = 0;
	for(m = top.child; m != nil; m = m->next)
		if(m->deleted){
			deld++;
			msg = strrchr(s_to_c(m->path), '/');
			if(msg == nil)
				msg = s_to_c(m->path);
			else
				msg++;
			if(e-p < 10){
				write(fd, buf, p-buf);
				n = 0;
				p = seprint(buf, e, "delete %s", mbname);
			}
			p = seprint(p, e, " %s", msg);
			n++;
		}
	if(n)
		write(fd, buf, p-buf);
	close(fd);
	closemb();

	switch(deld){
	case 0:
		break;
	case 1:
		Bprint(&out, "!1 message deleted\n");
		break;
	default:
		Bprint(&out, "!%d messages deleted\n", deld);
		break;
	}
	Bflush(&out);

	exitfs(0);
	return nil;	// not reached
}

Message*
xcmd(Cmd*, Message*)
{
	exitfs(0);
	return nil;	// not reached
}

Message*
eqcmd(Cmd*, Message *m)
{
	if(m == &top)
		Bprint(&out, "0\n");
	else
		Bprint(&out, "%d\n", m->id);
	return nil;
}

Message*
dcmd(Cmd*, Message *m)
{
	if(m == &top){
		Bprint(&out, "!address\n");
		return nil;
	}
	while(m->parent != &top)
		m = m->parent;
	m->deleted = 1;
	return m;
}

Message*
ucmd(Cmd*, Message *m)
{
	if(m == &top)
		return nil;
	while(m->parent != &top)
		m = m->parent;
	m->deleted = 0;
	return m;
}


Message*
icmd(Cmd*, Message *m)
{
	int n;

	n = dir2message(&top, reverse);
	if(n > 0)
		Bprint(&out, "%d new messages\n", n);
	return m;
}

Message*
helpcmd(Cmd*, Message *m)
{
	int i;

	Bprint(&out, "Commands are of the form [<range>] <command> [args]\n");
	Bprint(&out, "<range> := <addr> | <addr>','<addr>| 'g'<search>\n");
	Bprint(&out, "<addr> := '.' | '$' | '^' | <number> | <search> | <addr>'+'<addr> | <addr>'-'<addr>\n");
	Bprint(&out, "<search> := '/'<regexp>'/' | '?'<regexp>'?' | '%%'<regexp>'%%'\n");
	Bprint(&out, "<command> :=\n");
	for(i = 0; cmdtab[i].cmd != nil; i++)
		Bprint(&out, "%s\n", cmdtab[i].help);
	return m;
}

int
tomailer(char **av)
{
	Waitmsg w;
	int pid, i;

	// start the mailer and get out of the way
	switch(pid = fork()){
	case -1:
		fprint(2, "can't fork: %r\n");
		return -1;
	case 0:
		Bprint(&out, "!/bin/upas/marshal");
		for(i = 1; av[i]; i++){
			if(strchr(av[i], ' ') != nil)
				Bprint(&out, " '%s'", av[i]);
			else
				Bprint(&out, " %s", av[i]);
		}
		Bprint(&out, "\n");
		Bflush(&out);
		av[0] = "marshal";
		chdir(wd);
		exec("/bin/upas/marshal", av);
		fprint(2, "couldn't exec /bin/upas/marshal\n");
		exits(0);
	default:
		if(wait(&w) < 0){
			if(interrupted)
				postnote(PNPROC, pid, "die");
			wait(&w);
			return -1;
		}
		if(*w.msg){
			fprint(2, "mailer failed: %s\n", w.msg);
			return -1;
		}
		Bprint(&out, "!\n");
		break;
	}
	return 0;
}

//
// like tokenize but obey "" quoting
//
int
tokenize822(char *str, char **args, int max)
{
	int na;
	int intok = 0, inquote = 0;

	if(max <= 0)
		return 0;	
	for(na=0; ;str++)
		switch(*str) {
		case ' ':
		case '\t':
			if(inquote)
				goto Default;
			/* fall through */
		case '\n':
			*str = 0;
			if(!intok)
				continue;
			intok = 0;
			if(na < max)
				continue;
			/* fall through */
		case 0:
			return na;
		case '"':
			inquote ^= 1;
			/* fall through */
		Default:
		default:
			if(intok)
				continue;
			args[na++] = str;
			intok = 1;
		}
	return 0;	/* can't get here; silence compiler */
}

Message*
rcmd(Cmd *c, Message *m)
{
	char *av[128];
	int i, ai = 1;
	Message *nm;
	char *addr;
	String *path = nil;
	String *subject = nil;
	String *from;

	if(m == &top){
		Bprint(&out, "!address\n");
		return nil;
	}

	addr = nil;
	for(nm = m; nm != &top; nm = nm->parent){
 		if(*nm->replyto != 0){
			addr = nm->replyto;
			break;
		}
	}
	if(addr == nil){
		Bprint(&out, "!no reply address\n");
		return nil;
	}

	if(nm == &top){
		print("!noone to reply to\n");
		return nil;
	}

	for(nm = m; nm != &top; nm = nm->parent){
		if(*nm->subject){
			av[ai++] = "-s";
			subject = addrecolon(nm->subject);
			av[ai++] = s_to_c(subject);;
			break;
		}
	}

	if(strchr(c->av[0], 'f') != nil){
		fcmd(c, m);
		av[ai++] = "-F";
	}

	if(strchr(c->av[0], 'R') != nil){
		av[ai++] = "-t";
		av[ai++] = "message/rfc822";
		av[ai++] = "-A";
		path = rooted(extendpath(m->path, "raw"));
		av[ai++] = s_to_c(path);
	}

	for(i = 1; i < c->an && ai < nelem(av)-1; i++)
		av[ai++] = c->av[i];
	from = s_copy(addr);
	ai += tokenize822(s_to_c(from), &av[ai], nelem(av) - ai);
	av[ai] = 0;
	if(tomailer(av) < 0)
		m = nil;
	s_free(path);
	s_free(subject);
	s_free(from);
	return m;
}

Message*
mcmd(Cmd *c, Message *m)
{
	char **av;
	int i, ai;
	String *path;

	if(m == &top){
		Bprint(&out, "!address\n");
		return nil;
	}

	if(c->an < 2){
		fprint(2, "!usage: M list-of addresses\n");
		return nil;
	}

	ai = 1;
	av = malloc(sizeof(char*)*(c->an + 8));

	av[ai++] = "-t";
	if(m->parent == &top)
		av[ai++] = "message/rfc822";
	else
		av[ai++] = "mime";

	av[ai++] = "-A";
	path = rooted(extendpath(m->path, "raw"));
	av[ai++] = s_to_c(path);

	if(strchr(c->av[0], 'M') == nil)
		av[ai++] = "-n";

	for(i = 1; i < c->an; i++)
		av[ai++] = c->av[i];
	av[ai] = 0;

	if(tomailer(av) < 0)
		m = nil;
	if(path != nil)
		s_free(path);
	free(av);
	return m;
}

Message*
acmd(Cmd *c, Message *m)
{
	char *av[128];
	int i, ai;
	String *from, *to, *cc, *path = nil, *subject = nil;

	if(m == &top){
		Bprint(&out, "!address\n");
		return nil;
	}

	ai = 1;
	if(*m->subject){
		av[ai++] = "-s";
		subject = addrecolon(m->subject);
		av[ai++] = s_to_c(subject);
	}

	if(strchr(c->av[0], 'A') != nil){
		av[ai++] = "-t";
		av[ai++] = "message/rfc822";
		av[ai++] = "-A";
		path = rooted(extendpath(m->path, "raw"));
		av[ai++] = s_to_c(path);
	}

	for(i = 1; i < c->an && ai < nelem(av)-1; i++)
		av[ai++] = c->av[i];
	from = s_copy(m->from);
	ai += tokenize822(s_to_c(from), &av[ai], nelem(av) - ai);
	to = s_copy(m->to);
	ai += tokenize822(s_to_c(to), &av[ai], nelem(av) - ai);
	cc = s_copy(m->cc);
	ai += tokenize822(s_to_c(cc), &av[ai], nelem(av) - ai);
	av[ai] = 0;
	if(tomailer(av) < 0)
		return nil;
	s_free(from);
	s_free(to);
	s_free(cc);
	s_free(subject);
	s_free(path);
	return m;
}

String *
relpath(char *path, String *to)
{
	if (*path=='/' || strncmp(path, "./", 2) == 0
			      || strncmp(path, "../", 3) == 0) {
		to = s_append(to, path);
	} else {
		to = s_append(to, s_to_c(mbpath));
		to->ptr = strrchr(to->base, '/')+1;
		s_append(to, path);
	}
	return to;
}

int
appendtofile(Message *m, char *part, char *base, int mbox)
{
	String *file, *h;
	int n, in, out, rv;
	char buf[4096];

	file = extendpath(m->path, part);
	in = open(s_to_c(file), OREAD);
	if(in < 0){
		fprint(2, "!message disappeared\n");
		return -1;
	}

	s_reset(file);
	relpath(base, file);
	if(mbox)
		out = open(s_to_c(file), OWRITE);
	else
		out = open(s_to_c(file), OWRITE|OTRUNC);
	if(out < 0){
		out = create(s_to_c(file), OWRITE, 0666);
		if(out < 0){
			fprint(2, "!can't open %s: %r\n", s_to_c(file));
			close(in);
			return -1;
		}
	}
	if(mbox)
		seek(out, 0, 2);

	// put on a 'From ' line
	if(mbox){
		while(m->parent != &top)
			m = m->parent;
		h = file2string(m->path, "unixheader");
		fprint(out, "%s", s_to_c(h));
		s_free(h);
	}

	rv = 0;
	for(;;){
		n = read(in, buf, sizeof(buf));
		if(n < 0){
			fprint(2, "!error reading file: %r\n");
			rv = -1;
			break;
		}
		if(n == 0)
			break;
		if(write(out, buf, n) != n){
			fprint(2, "!error writing file: %r\n");
			rv = -1;
			break;
		}
	}
	if(mbox)
		write(out, "\n", 1);

	close(in);
	close(out);

	if(rv >= 0)
		print("!saved in %s\n", s_to_c(file));
	s_free(file);
	return rv;
}

Message*
scmd(Cmd *c, Message *m)
{
	char *file;

	if(m == &top){
		Bprint(&out, "!address\n");
		return nil;
	}

	switch(c->an){
	case 1:
		file = "stored";
		break;
	case 2:
		file = c->av[1];
		break;
	default:
		fprint(2, "!usage: s filename\n");
		return nil;
	}

	if(appendtofile(m, "raw", file, 1) < 0)
		return nil;

	m->stored = 1;
	return m;
}

Message*
wcmd(Cmd *c, Message *m)
{
	char *file;

	if(m == &top){
		Bprint(&out, "!address\n");
		return nil;
	}

	switch(c->an){
	case 2:
		file = c->av[1];
		break;
	case 1:
		if(*m->filename == 0){
			fprint(2, "!usage: w filename\n");
			return nil;
		}
		file = strrchr(m->filename, '/');
		if(file != nil)
			file++;
		else
			file = m->filename;
		break;
	default:
		fprint(2, "!usage: w filename\n");
		return nil;
	}

	if(appendtofile(m, "body", file, 0) < 0)
		return nil;
	m->stored = 1;
	return m;
}

// find the recipient account name
static void
foldername(char *folder, char *rcvr)
{
	char *p;
	char *e = folder+NAMELEN-1;

	p = strrchr(rcvr, '!');
	if(p != nil)
		rcvr = p+1;

	while(folder < e && *rcvr && *rcvr != '@')
		*folder++ = *rcvr++;
	*folder = 0;
}

Message*
fcmd(Cmd *c, Message *m)
{
	char folder[NAMELEN];

	if(c->an > 1){
		fprint(2, "!usage: f takes no arguments\n");
		return nil;
	}

	if(m == &top){
		Bprint(&out, "!address\n");
		return nil;
	}

	foldername(folder, m->from);

	if(appendtofile(m, "raw", folder, 1) < 0)
		return nil;

	m->stored = 1;
	return m;
}

void
system(char *cmd, char **av, int in)
{
	int pid;

	switch(pid=fork()){
	case -1:
		return;
	case 0:
		if(in >= 0){
			close(0);
			dup(in, 0);
			close(in);
		}
		if(wd[0] != 0)
			chdir(wd);
		exec(cmd, av);
		fprint(2, "!couldn't exec %s\n", cmd);
		exits(0);
	default:
		if(in >= 0)
			close(in);
		while(wait(nil) < 0){
			if(!interrupted)
				break;
			postnote(PNPROC, pid, "die");
			continue;
		}
		break;
	}
}

Message*
bangcmd(Cmd *c, Message *m)
{
	char cmd[4*1024];
	char *p, *e;
	char *av[4];
	int i;

	cmd[0] = 0;
	p = cmd;
	e = cmd+sizeof(cmd);
	for(i = 1; i < c->an; i++)
		p = seprint(p, e, "%s ", c->av[i]);
	av[0] = "rc";
	av[1] = "-c";
	av[2] = cmd;
	av[3] = 0;
	system("/bin/rc", av, -1);
	Bprint(&out, "!\n");
	return m;
}

Message*
pipecmd(Cmd *c, Message *m)
{
	char cmd[128];
	char *p, *e;
	char *av[4];
	String *path;
	int i, fd;

	if(c->an < 2){
		Bprint(&out, "!usage: | cmd\n");
		return nil;
	}

	if(m == &top){
		Bprint(&out, "!address\n");
		return nil;
	}

	path = extendpath(m->path, "body");
	fd = open(s_to_c(path), OREAD);
	s_free(path);
	if(fd < 0){
		fprint(2, "!message disappeared\n");
		return nil;
	}

	p = cmd;
	e = cmd+sizeof(cmd);
	cmd[0] = 0;
	for(i = 1; i < c->an; i++)
		p = seprint(p, e, "%s ", c->av[i]);
	av[0] = "rc";
	av[1] = "-c";
	av[2] = cmd;
	av[3] = 0;
	system("/bin/rc", av, fd);	/* system closes fd */
	Bprint(&out, "!\n");
	return m;
}

void
closemb(void)
{
	int fd;

	fd = open("/mail/fs/ctl", ORDWR);
	if(fd < 0)
		sysfatal("can't open /mail/fs/ctl: %r\n");

	// close current mailbox
	if(*mbname && strcmp(mbname, "mbox") != 0)
		fprint(fd, "close %s", mbname);

	close(fd);
}

int
switchmb(char *file, char *singleton)
{
	char *p;
	int n, fd;
	String *path;
	char buf[256];

	// close current mailbox
	closemb();

	fd = open("/mail/fs/ctl", ORDWR);
	if(fd < 0)
		sysfatal("can't open /mail/fs/ctl: %r\n");

	path = s_new();

	// get an absolute path to the mail box
	if(strncmp(file, "./", 2) == 0){
		// resolve path here since upas/fs doesn't know
		// our working directory
		if(getwd(buf, sizeof(buf)-strlen(file)) == nil){
			fprint(2, "!can't get working directory: %s\n", buf);
			return -1;
		}
		s_append(path, buf);
		s_append(path, file+1);
	} else {
		mboxpath(file, user, path, 0);
	}

	// make up a handle to use when talking to fs
	p = strrchr(file, '/');
	if(p == nil){
		// if its in the mailbox directory, just use the name
		strncpy(mbname, file, sizeof(mbname));
		mbname[sizeof(mbname)-1] = 0;
	} else {
		// make up a mailbox name
		p = strrchr(s_to_c(path), '/');
		p++;
		if(*p == 0){
			fprint(2, "!bad mbox name");
			return -1;
		}
		strncpy(mbname, p, sizeof(mbname));
		mbname[sizeof(mbname)-1] = 0;
		n = strlen(mbname);
		if(n > NAMELEN-12)
			n = NAMELEN-12;
		sprint(mbname+n, "%ld", time(0));
	}

	if(fprint(fd, "open %s %s", s_to_c(path), mbname) < 0){
		fprint(2, "!can't 'open %s %s': %r\n", file, mbname);
		s_free(path);
		return -1;
	}
	sprint(root, "/mail/fs/%s", mbname);
	if(getwd(wd, sizeof(wd)) == 0)
		wd[0] = 0;
	if(singleton == nil && chdir(root) >= 0)
		strcpy(root, ".");
	rootlen = strlen(root);
	close(fd);

	if(mbpath != nil)
		s_free(mbpath);
	mbpath = path;
	return 0;
}

// like tokenize but for into lines
int
lineize(char *s, char **f, int n)
{
	int i;

	for(i = 0; *s && i < n; i++){
		f[i] = s;
		s = strchr(s, '\n');
		if(s == nil)
			break;
		*s++ = 0;
	}
	return i;
}


//  create a mailbox
void
creatembox(char *user, char *folder)
{
	char *p;
	String *mailfile;
	char buf[512];
	int fd;
	Dir d;

	mailfile = s_new();
	if(folder == 0)
		mboxname(user, mailfile);
	else {
		snprint(buf, sizeof(buf), "%s/mbox", folder);
		mboxpath(buf, user, mailfile, 0);
	}


	// don't destroy existing mailbox
	if(access(s_to_c(mailfile), 0) == 0){
		fprint(2, "mailbox already exists\n");
		return;
	}
	fprint(2, "creating new mbox\n");

	//  make sure preceding levels exist
	for(p = s_to_c(mailfile); p; p++) {
		if(*p == '/')	/* skip leading or consecutive slashes */
			continue;
		p = strchr(p, '/');
		if(p == 0)
			break;
		*p = 0;
		if(access(s_to_c(mailfile), 0) != 0){
			if((fd = create(s_to_c(mailfile), OREAD, CHDIR|0711)) < 0){
				fprint(2, "couldn't create %s\n", s_to_c(mailfile));
				return;
			}
			close(fd);
		}
		*p = '/';
	}

	/*
	 *  create the mbox
	 */
	fd = create(s_to_c(mailfile), OREAD, 0622|CHAPPEND|CHEXCL);
	if(fd < 0){
		fprint(2, "couldn't create %s\n", s_to_c(mailfile));
		return;
	}
	if(dirfstat(fd, &d) < 0){
		close(fd);
		fprint(2, "couldn't chmod %s\n", s_to_c(mailfile));
		return;
	}
	d.mode = 0622|CHAPPEND|CHEXCL;
	if(dirfwstat(fd, &d) < 0)
		fprint(2, "couldn't chmod %s\n", s_to_c(mailfile));
	close(fd);
}

String*
rooted(String *s)
{
	static char buf[256];

	if(strcmp(root, ".") != 0)
		return s;
	snprint(buf, sizeof(buf), "/mail/fs/%s/%s", mbname, s_to_c(s));
	s_free(s);
	return s_copy(buf);
}

void
plumb(Message *m, Ctype *cp)
{
	String *s;
	Plumbmsg *pm;
	static int fd = -2;

	if(cp->plumbdest == nil)
		return;

	if(fd < -1)
		fd = plumbopen("send", OWRITE);

	pm = mallocz(sizeof(Plumbmsg), 1);
	pm->src = strdup("mail");
	if(*cp->plumbdest)
		pm->dst = strdup(cp->plumbdest);
	pm->wdir = nil;
	pm->type = strdup("text");
	pm->ndata = -1;
	s = rooted(extendpath(m->path, "body"));
	if(cp->ext != nil){
		s_append(s, ".");
		s_append(s, cp->ext);
	}
	pm->data = strdup(s_to_c(s));
	s_free(s);
	plumbsend(fd, pm);
	plumbfree(pm);
}

void
regerror(char*)
{
}

String*
addrecolon(char *s)
{
	String *str;

	if(cistrncmp(s, "re:", 3) != 0){
		str = s_copy("Re: ");
		s_append(str, s);
	} else
		str = s_copy(s);
	return str;
}

void
exitfs(char *rv)
{
	if(startedfs)
		unmount(nil, "/mail/fs");
chdir("/sys/src/cmd/upas/ned");
	exits(rv);
}
