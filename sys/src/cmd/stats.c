#include <u.h>
#include <libc.h>
#include <ctype.h>
#include <auth.h>
#include <fcall.h>
#include <draw.h>
#include <event.h>

#define	MAXNUM	8	/* maximum number of numbers on data line */

typedef struct Graph		Graph;
typedef struct Machine	Machine;

struct Graph
{
	int		colindex;
	Rectangle	r;
	int		*data;
	int		ndata;
	char		*label;
	void		(*newvalue)(Machine*, long*, long*);
	void		(*update)(Graph*, long, long);
	Machine	*mach;
	int		overflow;
	Image	*overtmp;
};

enum
{
	/* /dev/swap */
	Mem		= 0,
	Maxmem,
	Swap,
	Maxswap,
	/* /dev/sysstats */
	Procno	= 0,
	Context,
	Interrupt,
	Syscall,
	Fault,
	TLBfault,
	TLBpurge,
	Load,
	/* /net/ether0/0/stats */
	In		= 0,
	Out,
	Err0,
};

struct Machine
{
	char		*name;
	int		remote;
	int		statsfd;
	int		swapfd;
	int		etherfd;
	int		disable;

	long		devswap[4];
	long		devsysstat[8];
	long		prevsysstat[8];
	int		nproc;
	long		netetherstats[8];
	long		prevetherstats[8];

	char		buf[1024];
	char		*bufp;
	char		*ebufp;
};

enum
{
	Mainproc,
	Mouseproc,
	NPROC,
};

enum
{
	Ncolor	= 6,
	Ysqueeze	= 2,	/* vertical squeezing of label text */
	Labspace	= 2,	/* room around label */
	Dot		= 2,	/* height of dot */
	Opwid	= 5,	/* strlen("add  ") or strlen("drop ") */
};

enum Menu2
{
	Mcontext,
	Mether,
	Methererr,
	Metherin,
	Metherout,
	Mfault,
	Mintr,
	Mload,
	Mmem,
	Mswap,
	Msyscall,
	Mtlbmiss,
	Mtlbpurge,
	Nmenu2,
};

char	*menu2str[Nmenu2+1] = {
	"add  context ",
	"add  ether   ",
	"add  ethererr",
	"add  etherin ",
	"add  etherout",
	"add  fault   ",
	"add  intr    ",
	"add  load    ",
	"add  mem     ",
	"add  swap    ",
	"add  syscall ",
	"add  tlbmiss ",
	"add  tlbpurge",
	nil,
};


void	contextval(Machine*, long*, long*),
	etherval(Machine*, long*, long*),
	ethererrval(Machine*, long*, long*),
	etherinval(Machine*, long*, long*),
	etheroutval(Machine*, long*, long*),
	faultval(Machine*, long*, long*),
	intrval(Machine*, long*, long*),
	loadval(Machine*, long*, long*),
	memval(Machine*, long*, long*),
	swapval(Machine*, long*, long*),
	syscallval(Machine*, long*, long*),
	tlbmissval(Machine*, long*, long*),
	tlbpurgeval(Machine*, long*, long*);

Menu	menu2 = {menu2str, nil};
int		present[Nmenu2];
void		(*newvaluefn[Nmenu2])(Machine*, long*, long*) = {
	contextval,
	etherval,
	ethererrval,
	etherinval,
	etheroutval,
	faultval,
	intrval,
	loadval,
	memval,
	swapval,
	syscallval,
	tlbmissval,
	tlbpurgeval,
};

Image	*cols[Ncolor][3];
Graph	*graph;
Machine	*mach;
Font		*mediumfont;
char		*mysysname;
char		argchars[] = "ceEfimlnpstw";
int		pids[NPROC];
int 		parity;	/* toggled to avoid patterns in textured background */
int		nmach;
int		ngraph;	/* totaly number is ngraph*nmach */

char		*procnames[NPROC] = {"main", "mouse"};

void
killall(char *s)
{
	int i, pid;

	pid = getpid();
	for(i=0; i<NPROC; i++)
		if(pids[i] && pids[i]!=pid)
			postnote(PNPROC, pids[i], "kill");
	exits(s);
}

void*
emalloc(ulong sz)
{
	void *v;
	v = malloc(sz);
	if(v == nil) {
		fprint(2, "stats: out of memory allocating %ld: %r\n", sz);
		killall("mem");
	}
	memset(v, 0, sz);
	return v;
}

void*
erealloc(void *v, ulong sz)
{
	v = realloc(v, sz);
	if(v == nil) {
		fprint(2, "stats: out of memory reallocating %ld: %r\n", sz);
		killall("mem");
	}
	return v;
}

char*
estrdup(char *s)
{
	char *t;
	if((t = strdup(s)) == nil) {
		fprint(2, "stats: out of memory in strdup(%.10s): %r\n", s);
		killall("mem");
	}
	return t;
}

void
mkcol(int i, int c0, int c1, int c2)
{
	cols[i][0] = allocimagemix(display, c0, DWhite);
	cols[i][1] = allocimage(display, Rect(0,0,1,1), CMAP8, 1, c1);
	cols[i][2] = allocimage(display, Rect(0,0,1,1), CMAP8, 1, c2);
}

void
colinit(void)
{
	mediumfont = openfont(display, "/lib/font/bit/pelm/latin1.8.font");
	if(mediumfont == nil)
		mediumfont = font;

	/* Peach */
	mkcol(0, 0xFFAAAAFF, 0xFFAAAAFF, 0xBB5D5DFF);
	/* Aqua */
	mkcol(1, DPalebluegreen, DPalegreygreen, DPurpleblue);
	/* Yellow */
	mkcol(2, DPaleyellow, DDarkyellow, DYellowgreen);
	/* Green */
	mkcol(3, DPalegreen, DMedgreen, DDarkgreen);
	/* Blue */
	mkcol(4, 0x00AAFFFF, 0x00AAFFFF, 0x0088CCFF);
	/* Grey */
	cols[5][0] = allocimage(display, Rect(0,0,1,1), CMAP8, 1, 0xEEEEEEFF);
	cols[5][1] = allocimage(display, Rect(0,0,1,1), CMAP8, 1, 0xCCCCCCFF);
	cols[5][2] = allocimage(display, Rect(0,0,1,1), CMAP8, 1, 0x888888FF);
}

int
loadbuf(Machine *m, int *fd)
{
	int n;


	if(*fd < 0)
		return 0;
	seek(*fd, 0, 0);
	n = read(*fd, m->buf, sizeof m->buf);
	if(n <= 0){
		close(*fd);
		*fd = -1;
		return 0;
	}
	m->bufp = m->buf;
	m->ebufp = m->buf+n;
	return 1;
}

void
label(Point p, int dy, char *text)
{
	char *s;
	Rune r[2];
	int w, maxw, maxy;

	p.x += Labspace;
	maxy = p.y+dy;
	maxw = 0;
	r[1] = '\0';
	for(s=text; *s; ){
		if(p.y+mediumfont->height-Ysqueeze > maxy)
			break;
		w = chartorune(r, s);
		s += w;
		w = runestringwidth(mediumfont, r);
		if(w > maxw)
			maxw = w;
		runestring(screen, p, display->black, ZP, mediumfont, r);
		p.y += mediumfont->height-Ysqueeze;
	}
}

Point
paritypt(int x)
{
	return Pt(x+parity, 0);
}

Point
datapoint(Graph *g, int x, long v, long vmax)
{
	Point p;

	p.x = x;
	p.y = g->r.max.y - Dy(g->r)*v/vmax - Dot;
	if(p.y < g->r.min.y)
		p.y = g->r.min.y;
	if(p.y > g->r.max.y-Dot)
		p.y = g->r.max.y-Dot;
	return p;
}

void
drawdatum(Graph *g, int x, long prev, long v, long vmax)
{
	int c;
	Point p, q;

	c = g->colindex;
	p = datapoint(g, x, v, vmax);
	q = datapoint(g, x, prev, vmax);
	if(p.y < q.y){
		draw(screen, Rect(p.x, g->r.min.y, p.x+1, p.y), cols[c][0], nil, paritypt(p.x));
		draw(screen, Rect(p.x, p.y, p.x+1, q.y+Dot), cols[c][2], nil, ZP);
		draw(screen, Rect(p.x, q.y+Dot, p.x+1, g->r.max.y), cols[c][1], nil, ZP);
	}else{
		draw(screen, Rect(p.x, g->r.min.y, p.x+1, q.y), cols[c][0], nil, paritypt(p.x));
		draw(screen, Rect(p.x, q.y, p.x+1, p.y+Dot), cols[c][2], nil, ZP);
		draw(screen, Rect(p.x, p.y+Dot, p.x+1, g->r.max.y), cols[c][1], nil, ZP);
	}

}

void
redraw(Graph *g, int vmax)
{
	int i, c;

	c = g->colindex;
	draw(screen, g->r, cols[c][0], nil, paritypt(g->r.min.x));
	for(i=1; i<Dx(g->r); i++)
		drawdatum(g, g->r.max.x-i, g->data[i-1], g->data[i], vmax);
	drawdatum(g, g->r.min.x, g->data[i], g->data[i], vmax);
	g->overflow = 0;
}

void
update1(Graph *g, long v, long vmax)
{
	char buf[32];

	if(g->overflow && g->overtmp!=nil)
		draw(screen, g->overtmp->r, g->overtmp, nil, g->overtmp->r.min);
	draw(screen, g->r, screen, nil, Pt(g->r.min.x+1, g->r.min.y));
	drawdatum(g, g->r.max.x-1, g->data[0], v, vmax);
	memmove(g->data+1, g->data, (g->ndata-1)*sizeof(g->data[0]));
	g->data[0] = v;
	g->overflow = 0;
	if(v>vmax && g->overtmp!=nil){
		g->overflow = 1;
		draw(g->overtmp, g->overtmp->r, screen, nil, g->overtmp->r.min);
		sprint(buf, "%ld", v);
		string(screen, g->overtmp->r.min, display->black, ZP, mediumfont, buf);
	}
}

/* read one line of text from buffer and process integers */
int
readnums(Machine *m, int n, long *a, int spanlines)
{
	int i;
	char *p, *ep;

	if(spanlines)
		ep = m->ebufp;
	else
		for(ep=m->bufp; ep<m->ebufp; ep++)
			if(*ep == '\n')
				break;
	p = m->bufp;
	for(i=0; i<n && p<ep; i++){
		while(p<ep && !isdigit(*p))
			p++;
		if(p == ep)
			break;
		a[i] = strtol(p, &p, 10);
	}
	if(ep < m->ebufp)
		ep++;
	m->bufp = ep;
	return i == n;
}

/* Network on fd1, mount driver on fd0 */
static int
filter(int fd)
{
	int p[2];

	if(pipe(p) < 0){
		fprint(2, "stats: can't pipe: %r\n");
		killall("pipe");
	}

	switch(rfork(RFNOWAIT|RFPROC|RFFDG)) {
	case -1:
		sysfatal("rfork record module");
	case 0:
		dup(fd, 1);
		close(fd);
		dup(p[0], 0);
		close(p[0]);
		close(p[1]);
		execl("/bin/aux/fcall", "fcall", 0);
		fprint(2, "stats: can't exec fcall: %r\n");
		killall("fcall");
	default:
		close(fd);
		close(p[0]);
	}
	return p[1];	
}

/*
 * 9fs
 */
int
connect9fs(char *addr)
{
	char dir[4*NAMELEN], *na;
	int fd;

	fprint(2, "connect9fs...");
	na = netmkaddr(addr, 0, "9fs");

	fprint(2, "dial %s...", na);
	if((fd = dial(na, 0, dir, 0)) < 0)
		return -1;

	fprint(2, "dir %s...", dir);
	if(strstr(dir, "tcp"))
		fd = filter(fd);
	return fd;
}

/*
 * exportfs
 */
int 
connectexportfs(char *addr)
{
	char buf[ERRLEN], dir[4*NAMELEN], *na;
	int fd, n;
	char *tree;

	tree = "/";
	na = netmkaddr(addr, 0, "exportfs");
	if((fd = dial(na, 0, dir, 0)) < 0)
		return -1;

	if(auth(fd) < 0){
		close(fd);
		return -1;
	}

	n = write(fd, tree, strlen(tree));
	if(n < 0){
		close(fd);
		return -1;
	}

	strcpy(buf, "can't read tree");
	n = read(fd, buf, sizeof buf - 1);
	if(n!=2 || buf[0]!='O' || buf[1]!='K'){
		buf[sizeof buf - 1] = '\0';
		werrstr("bad remote tree: %s\n", buf);
		close(fd);
		return -1;
	}

	if(strstr(dir, "tcp"))
		fd = filter(fd);

	return fd;
}

void
initmach(Machine *m, char *name)
{
	int n, fd;
	long a[MAXNUM];
	char *p, mpt[256], buf[256];

	p = strchr(name, '!');
	if(p){
		p++;
		m->name = estrdup(p+1);
	}else
		p = name;
	m->name = estrdup(p);
	m->remote = (strcmp(p, mysysname) != 0);
	if(m->remote == 0)
		strcpy(mpt, "");
	else{
		snprint(mpt, sizeof mpt, "/n/%s", p);
		fd = connectexportfs(name);
		if(fd < 0){
			fprint(2, "can't connect to %s: %r\n", name);
			killall("connect");
		}
		if(mount(fd, mpt, MREPL, "") < 0){
			fprint(2, "stats: mount %s on %s failed (%r); trying /n/sid\n", name, mpt);
			strcpy(mpt, "/n/sid");
			if(mount(fd, mpt, MREPL, "") < 0){
				fprint(2, "stats: mount %s on %s failed: %r\n", name, mpt);
				killall("mount");
			}
		}
	}

	snprint(buf, sizeof buf, "%s/dev/swap", mpt);
	m->swapfd = open(buf, OREAD);
	if(loadbuf(m, &m->swapfd) && readnums(m, nelem(m->devswap), a, 0))
		memmove(m->devswap, a, sizeof m->devswap);
	else
		m->devswap[Maxmem] = m->devswap[Maxswap] = 100;

	snprint(buf, sizeof buf, "%s/dev/sysstat", mpt);
	m->statsfd = open(buf, OREAD);
	if(loadbuf(m, &m->statsfd)){
		for(n=0; readnums(m, nelem(m->devsysstat), a, 0); n++)
			;
		m->nproc = n;
	}else
		m->nproc = 1;

	snprint(buf, sizeof buf, "%s/net/ether0/0/stats", mpt);
	m->etherfd = open(buf, OREAD);
	if(loadbuf(m, &m->etherfd) &&  readnums(m, nelem(m->netetherstats), a, 1))
		memmove(m->netetherstats, a, sizeof m->netetherstats);
}

jmp_buf catchalarm;

void
alarmed(void *a, char *s)
{
	if(strcmp(s, "alarm") == 0)
		notejmp(a, catchalarm, 1);
	noted(NDFLT);
}

int
needswap(int init)
{
	return init | present[Mmem] | present[Mswap];
}


int
needstat(int init)
{
	return init | present[Mcontext]  | present[Mfault] | present[Mintr] | present[Mload] |
		present[Msyscall] | present[Mtlbmiss] | present[Mtlbpurge];
}


int
needether(int init)
{
	return init | present[Mether] | present[Metherin] | present[Metherout] | present[Methererr];
}

void
readmach(Machine *m, int init)
{
	int n, i;
	long a[8];
	char buf[32];

	if(m->remote && (m->disable || setjmp(catchalarm))){
		if(m->disable == 0){
			snprint(buf, sizeof buf, "%s(dead)", m->name);
			m->name = estrdup(buf);
			if(display != nil)	/* else we're still initializing */
				eresized(0);
		}
		m->disable = 1;
		memmove(m->devsysstat, m->prevsysstat, sizeof m->devsysstat);
		memmove(m->netetherstats, m->prevetherstats, sizeof m->netetherstats);
		return;
	}
	if(m->remote){
		notify(alarmed);
		alarm(5000);
	}
	if(needswap(init) && loadbuf(m, &m->swapfd) && readnums(m, nelem(m->devswap), a, 0))
		memmove(m->devswap, a, sizeof m->devswap);
	if(needstat(init) && loadbuf(m, &m->statsfd)){
		memmove(m->prevsysstat, m->devsysstat, sizeof m->devsysstat);
		memset(m->devsysstat, 0, sizeof m->devsysstat);
		for(n=0; n<m->nproc && readnums(m, nelem(m->devsysstat), a, 0); n++)
			for(i=0; i<nelem(m->devsysstat); i++)
				m->devsysstat[i] += a[i];
	}
	if(needether(init) && loadbuf(m, &m->etherfd) && readnums(m, nelem(m->netetherstats), a, 1)){
		memmove(m->prevetherstats, m->netetherstats, sizeof m->netetherstats);
		memmove(m->netetherstats, a, sizeof m->netetherstats);
	}
	if(m->remote){
		alarm(0);
		notify(nil);
	}
}

void
memval(Machine *m, long *v, long *vmax)
{
	*v = m->devswap[Mem];
	*vmax = m->devswap[Maxmem];
}

void
swapval(Machine *m, long *v, long *vmax)
{
	*v = m->devswap[Swap];
	*vmax = m->devswap[Maxswap];
}

void
contextval(Machine *m, long *v, long *vmax)
{
	*v = m->devsysstat[Context]-m->prevsysstat[Context];
	*vmax = 1000*m->nproc;
}

void
intrval(Machine *m, long *v, long *vmax)
{
	*v = m->devsysstat[Interrupt]-m->prevsysstat[Interrupt];
	*vmax = 1000*m->nproc;
}

void
syscallval(Machine *m, long *v, long *vmax)
{
	*v = m->devsysstat[Syscall]-m->prevsysstat[Syscall];
	*vmax = 1000*m->nproc;
}

void
faultval(Machine *m, long *v, long *vmax)
{
	*v = m->devsysstat[Fault]-m->prevsysstat[Fault];
	*vmax = 1000*m->nproc;
}

void
tlbmissval(Machine *m, long *v, long *vmax)
{
	*v = m->devsysstat[TLBfault]-m->prevsysstat[TLBfault];
	*vmax = 10*m->nproc;
}

void
tlbpurgeval(Machine *m, long *v, long *vmax)
{
	*v = m->devsysstat[TLBpurge]-m->prevsysstat[TLBpurge];
	*vmax = 10*m->nproc;
}

void
loadval(Machine *m, long *v, long *vmax)
{
	*v = m->devsysstat[Load];
	*vmax = 1000*m->nproc;
}

void
etherval(Machine *m, long *v, long *vmax)
{
	*v = m->netetherstats[In]-m->prevetherstats[In] + m->netetherstats[Out]-m->prevetherstats[Out];
	*vmax = 1000*m->nproc;
}

void
etherinval(Machine *m, long *v, long *vmax)
{
	*v = m->netetherstats[In]-m->prevetherstats[In];
	*vmax = 1000*m->nproc;
}

void
etheroutval(Machine *m, long *v, long *vmax)
{
	*v = m->netetherstats[Out]-m->prevetherstats[Out];
	*vmax = 1000*m->nproc;
}

void
ethererrval(Machine *m, long *v, long *vmax)
{
	int i;

	*v = 0;
	for(i=Err0; i<nelem(m->netetherstats); i++)
		*v += m->netetherstats[i];
	*vmax = 10*m->nproc;
}

void
usage(void)
{
	fprint(2, "usage: stats [-%s] [machine...]\n", argchars);
	exits("usage");
}

void
addgraph(int n)
{
	Graph *g, *ograph;
	int i, j;
	static int nadd;

	if(n > nelem(menu2str))
		abort();
	/* avoid two adjacent graphs of same color */
	if(ngraph>0 && graph[ngraph-1].colindex==nadd%Ncolor)
		nadd++;
	ograph = graph;
	graph = emalloc(nmach*(ngraph+1)*sizeof(Graph));
	for(i=0; i<nmach; i++)
		for(j=0; j<ngraph; j++)
			graph[i*(ngraph+1)+j] = ograph[i*ngraph+j];
	free(ograph);
	ngraph++;
	for(i=0; i<nmach; i++){
		g = &graph[i*ngraph+(ngraph-1)];
		memset(g, 0, sizeof(Graph));
		g->label = menu2str[n]+Opwid;
		g->newvalue = newvaluefn[n];
		g->update = update1;	/* no other update functions yet */
		g->mach = &mach[i];
		g->colindex = nadd%Ncolor;
	}
	present[n] = 1;
	nadd++;
}

void
dropgraph(int which)
{
	Graph *ograph;
	int i, j, n;

	if(which > nelem(menu2str))
		abort();
	/* convert n to index in graph table */
	n = -1;
	for(i=0; i<ngraph; i++)
		if(strcmp(menu2str[which]+Opwid, graph[i].label) == 0){
			n = i;
			break;
		}
	if(n < 0){
		fprint(2, "stats: internal error can't drop graph\n");
		killall("error");
	}
	ograph = graph;
	graph = emalloc(nmach*(ngraph-1)*sizeof(Graph));
	for(i=0; i<nmach; i++){
		for(j=0; j<n; j++)
			graph[i*(ngraph-1)+j] = ograph[i*ngraph+j];
		free(ograph[i*ngraph+j].data);
		freeimage(ograph[i*ngraph+j].overtmp);
		for(j++; j<ngraph; j++)
			graph[i*(ngraph-1)+j-1] = ograph[i*ngraph+j];
	}
	free(ograph);
	ngraph--;
	present[which] = 0;
}

void
addmachine(char *name)
{
	if(ngraph > 0){
		fprint(2, "stats: internal error: ngraph>0 in addmachine()\n");
		usage();
	}
	if(mach == nil)
		nmach = 0;	/* a little dance to get us started with local machine by default */
	mach = erealloc(mach, (nmach+1)*sizeof(Machine));
	memset(mach+nmach, 0, sizeof(Machine));
	initmach(mach+nmach, name);
	nmach++;
}

void
resize(void)
{
	int i, j, n, startx, starty, x, y, dx, dy, ondata;
	Graph *g;
	Rectangle machr, r;
	long v, vmax;
	char buf[128];

	draw(screen, screen->r, display->white, nil, ZP);

	/* label left edge */
	x = screen->r.min.x;
	y = screen->r.min.y + Labspace+mediumfont->height+Labspace;
	dy = (screen->r.max.y - y)/ngraph;
	dx = Labspace+stringwidth(mediumfont, "0")+Labspace;
	startx = x+dx+1;
	starty = y;
	for(i=0; i<ngraph; i++,y+=dy){
		draw(screen, Rect(x, y-1, screen->r.max.x, y), display->black, nil, ZP);
		draw(screen, Rect(x, y, x+dx, screen->r.max.y), cols[graph[i].colindex][0], nil, paritypt(x));
		label(Pt(x, y), dy, graph[i].label);
		draw(screen, Rect(x+dx, y, x+dx+1, screen->r.max.y), cols[graph[i].colindex][2], nil, ZP);
	}

	/* label top edge */
	dx = (screen->r.max.x - startx)/nmach;
	for(x=startx, i=0; i<nmach; i++,x+=dx){
		draw(screen, Rect(x-1, starty-1, x, screen->r.max.y), display->black, nil, ZP);
		j = dx/stringwidth(mediumfont, "0");
		n = mach[i].nproc;
		if(n>1 && j>=1+3+(n>10)+(n>100)){	/* first char of name + (n) */
			j -= 3+(n>10)+(n>100);
			if(j <= 0)
				j = 1;
			snprint(buf, sizeof buf, "%.*s(%d)", j, mach[i].name, n);
		}else
			snprint(buf, sizeof buf, "%.*s", j, mach[i].name);
		string(screen, Pt(x+Labspace, screen->r.min.y + Labspace), display->black, ZP, mediumfont, buf);
	}

	/* create graphs */
	for(i=0; i<nmach; i++){
		machr = Rect(startx+i*dx, starty, screen->r.max.x, screen->r.max.y);
		if(i < nmach-1)
			machr.max.x = startx+(i+1)*dx - 1;
		y = starty;
		for(j=0; j<ngraph; j++, y+=dy){
			g = &graph[i*ngraph+j];
			/* allocate data */
			ondata = g->ndata;
			g->ndata = Dx(machr)+1;	/* may be too many if label will be drawn here; so what? */
			g->data = erealloc(g->data, g->ndata*sizeof(long));
			if(g->ndata > ondata)
				memset(g->data+ondata, 0, (g->ndata-ondata)*sizeof(long));
			/* set geometry */
			g->r = machr;
			g->r.min.y = y;
			g->r.max.y = y+dy - 1;
			if(j == ngraph-1)
				g->r.max.y = screen->r.max.y;
			draw(screen, g->r, cols[g->colindex][0], nil, paritypt(g->r.min.x));
			g->overflow = 0;
			r = g->r;
			r.max.y = r.min.y+mediumfont->height;
			r.max.x = r.min.x+stringwidth(mediumfont, "9999999");
			freeimage(g->overtmp);
			g->overtmp = nil;
			if(r.max.x <= g->r.max.x)
				g->overtmp = allocimage(display, r, screen->chan, 0, -1);
			g->newvalue(g->mach, &v, &vmax);
			redraw(g, vmax);
		}
	}

	flushimage(display, 1);
}

void
eresized(int new)
{
	lockdisplay(display);
	if(new && getwindow(display, Refnone) < 0) {
		fprint(2, "stats: can't reattach to window\n");
		killall("reattach");
	}
	resize();
	unlockdisplay(display);
}

void
mouseproc(void)
{
	Mouse mouse;
	int i;

	for(;;){
		mouse = emouse();
		if(mouse.buttons == 4){
			lockdisplay(display);
			for(i=0; i<Nmenu2; i++)
				if(present[i])
					memmove(menu2str[i], "drop ", Opwid);
				else
					memmove(menu2str[i], "add  ", Opwid);
			i = emenuhit(3, &mouse, &menu2);
			if(i >= 0){
				if(!present[i])
					addgraph(i);
				else if(ngraph > 1)
					dropgraph(i);
				resize();
			}
			unlockdisplay(display);
		}
	}
}

void
startproc(void (*f)(void), int index)
{
	int pid;

	switch(pid = rfork(RFPROC|RFMEM|RFNOWAIT)){
	case -1:
		fprint(2, "stats: fork failed: %r\n");
		killall("fork failed");
	case 0:
		f();
		fprint(2, "stats: %s process exits\n", procnames[index]);
		if(index >= 0)
			killall("process died");
		exits(nil);
	}
	if(index >= 0)
		pids[index] = pid;
}

void
main(int argc, char *argv[])
{
	int i, j;
	long v, vmax, nargs;
	char args[100];

	nmach = 1;
	mysysname = getenv("sysname");
	if(mysysname == nil){
		fprint(2, "stats: can't find $sysname: %r\n");
		exits("sysname");
	}
	mysysname = estrdup(mysysname);

	nargs = 0;
	ARGBEGIN{
	default:
		if(nargs>=sizeof args || strchr(argchars, ARGC())==nil)
			usage();
		args[nargs++] = ARGC();
	}ARGEND

	if(argc == 0){
		mach = emalloc(nmach*sizeof(Machine));
		initmach(&mach[0], mysysname);
		readmach(&mach[0], 1);
	}else{
		for(i=0; i<argc; i++){
			addmachine(argv[i]);
			readmach(&mach[i], 1);
		}
	}

	for(i=0; i<nargs; i++)
	switch(args[i]){
	default:
		fprint(2, "stats: internal error: unknown arg %c\n", args[i]);
		usage();
	case 'c':
		addgraph(Mcontext);
		break;
	case 'e':
		addgraph(Mether);
		break;
	case 'E':
		addgraph(Metherin);
		addgraph(Metherout);
		break;
	case 'f':
		addgraph(Mfault);
		break;
	case 'i':
		addgraph(Mintr);
		break;
	case 'l':
		addgraph(Mload);
		break;
	case 'm':
		addgraph(Mmem);
		break;
	case 'n':
		addgraph(Metherin);
		addgraph(Metherout);
		addgraph(Methererr);
		break;
	case 'p':
		addgraph(Mtlbpurge);
		break;
	case 's':
		addgraph(Msyscall);
		break;
	case 't':
		addgraph(Mtlbmiss);
		addgraph(Mtlbpurge);
		break;
	case 'w':
		addgraph(Mswap);
		break;
	}

	if(ngraph == 0)
		addgraph(Mload);

	for(i=0; i<nmach; i++)
		for(j=0; j<ngraph; j++)
			graph[i*ngraph+j].mach = &mach[i];

	if(initdraw(nil, nil, "stats") < 0){
		fprint(2, "stats: initdraw failed: %r\n");
		exits("initdraw");
	}
	colinit();
	einit(Emouse);
	notify(nil);
	startproc(mouseproc, Mouseproc);
	pids[Mainproc] = getpid();
	display->locking = 1;	/* tell library we're using the display lock */

	resize();

	unlockdisplay(display); /* display is still locked from initdraw() */
	for(;;){
		for(i=0; i<nmach; i++)
			readmach(&mach[i], 0);
		lockdisplay(display);
		parity = 1-parity;
		for(i=0; i<nmach*ngraph; i++){
			graph[i].newvalue(graph[i].mach, &v, &vmax);
			graph[i].update(&graph[i], v, vmax);
		}
		flushimage(display, 1);
		unlockdisplay(display);
		sleep(1000);
	}
}
