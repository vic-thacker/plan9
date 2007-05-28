/*
 * File system devices.
 * Follows device config in Ken's file server.
 * Builds mirrors, concatenations, interleavings, and partitions
 * of devices out of other (inner) devices.
 */

#include "u.h"
#include "../port/lib.h"
#include "mem.h"
#include "dat.h"
#include "fns.h"
#include "io.h"
#include "ureg.h"
#include "../port/error.h"

enum {
	Fmirror,		/* mirror of others */
	Fcat,			/* catenation of others */
	Finter,			/* interleaving of others */
	Fpart,			/* part of others */
	Fclear,			/* start over */

	Blksize	= 8*1024,	/* for Finter only */

	Qtop	= 0,		/* top dir (contains "fs") */
	Qdir,			/* actual dir */
	Qctl,			/* ctl file */
	Qfirst,			/* first fs file */

	Iswrite = 0,
	Isread,

	/* tunable parameters */
	Maxconf	= 4*1024,	/* max length for config */
	Ndevs	= 32,		/* max. inner devs per command */
	Nfsdevs = 128,		/* max. created devs, total */
};

#define	Cfgstr	"fsdev:\n"

typedef struct Inner Inner;
struct Inner
{
	char	*iname;		/* inner device name */
	vlong	isize;		/* size of inner device */
	Chan	*idev;		/* inner device */
};

typedef struct Fsdev Fsdev;
struct Fsdev
{
	int	type;
	char	*name;		/* name for this fsdev */
	vlong	size;		/* min(inner[X].isize) */
	vlong	start;		/* start address (for Fpart) */
	int	ndevs;		/* number of inner devices */
	Inner	inner[Ndevs];
};

extern Dev fsdevtab;		/* forward */

/*
 * Once configured, a fsdev is never removed.  The name of those
 * configured is never nil.  We have no locks here.
 */
static Fsdev	fsdev[Nfsdevs];

static Qid	tqid = {Qtop, 0, QTDIR};
static Qid	dqid = {Qdir, 0, QTDIR};
static Qid	cqid = {Qctl, 0, 0};

static Cmdtab configs[] = {
	Fmirror,"mirror",	0,
	Fcat,	"cat",		0,
	Finter,	"inter",	0,
	Fpart,	"part",		5,
	Fclear,	"clear",	1,	
};

static char	confstr[Maxconf];
static int	configed;


static Fsdev*
path2dev(int i, int mustexist)
{
	if (i < 0 || i >= nelem(fsdev))
		error("bug: bad index in devfsdev");
	if (mustexist && fsdev[i].name == nil)
		error(Enonexist);

	if (fsdev[i].name == nil)
		return nil;
	else
		return &fsdev[i];
}

static Fsdev*
devalloc(void)
{
	int	i;

	for (i = 0; i < nelem(fsdev); i++)
		if (fsdev[i].name == nil)
			break;
	if (i == nelem(fsdev))
		error(Enodev);

	return &fsdev[i];
}

static void
setdsize(Fsdev* mp)
{
	int	i;
	long	l;
	uchar	buf[128];	/* old DIRLEN plus a little should be plenty */
	Dir	d;
	Inner	*in;

	if (mp->type != Fpart){
		mp->start= 0;
		mp->size = 0;
	}
	for (i = 0; i < mp->ndevs; i++){
		in = &mp->inner[i];
		l = devtab[in->idev->type]->stat(in->idev, buf, sizeof buf);
		convM2D(buf, l, &d, nil);
		in->isize = d.length;
		switch(mp->type){
		case Fmirror:
			if (mp->size == 0 || mp->size > d.length)
				mp->size = d.length;
			break;
		case Fcat:
			mp->size += d.length;
			break;
		case Finter:
			/* truncate to multiple of Blksize */
			d.length &= ~(Blksize-1);
			in->isize = d.length;
			mp->size += d.length;
			break;
		case Fpart:
			/* should raise errors here? */
			if (mp->start > d.length)
				mp->start = d.length;
			if (d.length < mp->start + mp->size)
				mp->size = d.length - mp->start;
			break;
		}
	}
}

static void
mpshut(Fsdev *mp)
{
	int	i;
	char	*nm;

	nm = mp->name;
	mp->name = nil;		/* prevent others from using this. */
	if (nm)
		free(nm);
	for (i = 0; i < mp->ndevs; i++){
		if (mp->inner[i].idev != nil)
			cclose(mp->inner[i].idev);
		if (mp->inner[i].iname)
			free(mp->inner[i].iname);
	}
	memset(mp, 0, sizeof *mp);
}


static void
mconfig(char* a, long n)	/* "name idev0 idev1" */
{
	int	i;
	vlong	size, start;
	char	*c, *oldc;
	Cmdbuf	*cb;
	Cmdtab	*ct;
	Fsdev	*mp;
	Inner	*inprv;
	static	QLock	lck;

	size = 0;
	start = 0;
	if (confstr[0] == 0)
		seprint(confstr, confstr + sizeof confstr, Cfgstr);
	mp = nil;
	cb = nil;
	oldc = confstr + strlen(confstr);
	if (*a == '\0' || *a == '#' || *a == '\n')
		return;

	qlock(&lck);
	if (waserror()){
		*oldc = 0;
		if (mp != nil)
			mpshut(mp);
		qunlock(&lck);
		if (cb)
			free(cb);
		nexterror();
	}

	cb = parsecmd(a, n);
	c = oldc;
	for (i = 0; i < cb->nf; i++)
		c = seprint(c, confstr + sizeof confstr, "%s ", cb->f[i]);
	if (c > confstr)
		c[-1] = '\n';
	ct = lookupcmd(cb, configs, nelem(configs));
	cb->f++;			/* skip command */
	cb->nf--;
	if (cb->nf < 0)			/* nothing to see here, move along */
		ct->index = -1;
	switch (ct->index) {
	case Fpart:
		if (cb->nf < 4)
			error("too few fields in fs config");
		start = strtoll(cb->f[2], nil, 10);
		size =  strtoll(cb->f[3], nil, 10);
		cb->nf -= 2;
		break;
	case Fclear:
		for (mp = fsdev; mp < fsdev + nelem(fsdev); mp++)
			mpshut(mp);
		*confstr = '\0';
		/* FALL THROUGH */
	case -1:
		poperror();
		qunlock(&lck);
		free(cb);
		return;
	}
	if (cb->nf < 2)
		error("too few fields in fs config");

	/* reject name if already in use */
	for (i = 0; i < nelem(fsdev); i++)
		if (fsdev[i].name != nil && strcmp(fsdev[i].name, cb->f[0])==0)
			error(Eexist);

	if (cb->nf - 1 > Ndevs)
		error("too many devices; fix #k: increase Ndevs");
	for (i = 0; i < cb->nf; i++)
		validname(cb->f[i], (i != 0));

	mp = devalloc();
	mp->type = ct->index;
	if (mp->type == Fpart){
		mp->start = start;
		mp->size = size;
	}
	kstrdup(&mp->name, cb->f[0]);
	for (i = 1; i < cb->nf; i++){
		inprv = &mp->inner[i-1];
		kstrdup(&inprv->iname, cb->f[i]);
		inprv->idev = namec(inprv->iname, Aopen, ORDWR, 0);
		if (inprv->idev == nil) {
			free(mp->name);
			mp->name = nil;		/* free mp */
			error(Egreg);
		}
		mp->ndevs++;
	}
	setdsize(mp);
	configed = 1;

	poperror();
	qunlock(&lck);
	free(cb);
}

static void
rdconf(void)
{
	int mustrd;
	char *c, *e, *p, *s;
	Chan *cc;
	Chan **ccp;

	s = getconf("fsconfig");
	if (s == nil){
		mustrd = 0;
		s = "/dev/sdC0/fscfg";
	} else
		mustrd = 1;
	ccp = &cc;
	*ccp = nil;
	c = nil;
	if (waserror()){
		configed = 1;
		if (*ccp != nil)
			cclose(*ccp);
		if (c)
			free(c);
		if (!mustrd)
			return;
		nexterror();
	}
	*ccp = namec(s, Aopen, OREAD, 0);
	devtab[(*ccp)->type]->read(*ccp, confstr, sizeof confstr, 0);
	cclose(*ccp);
	*ccp = nil;
	if (strncmp(confstr, Cfgstr, strlen(Cfgstr)) != 0)
		error("bad #k config, first line must be: 'fsdev:\\n'");
	kstrdup(&c, confstr + strlen(Cfgstr));
	memset(confstr, 0, sizeof confstr);
	for (p = c; p != nil && *p != 0; p = e){
		e = strchr(p, '\n');
		if (e == nil)
			e = p + strlen(p);
		if (e == p) {
			e++;
			continue;
		}
		mconfig(p, e - p);
	}
	poperror();
}


static int
mgen(Chan *c, char*, Dirtab*, int, int i, Dir *dp)
{
	Qid	qid;
	Fsdev	*mp;

	if (c->qid.path == Qtop)
		switch(i){
		case DEVDOTDOT:
			devdir(c, tqid, "#k", 0, eve, DMDIR|0775, dp);
			return 1;
		case 0:
			devdir(c, dqid, "fs", 0, eve, DMDIR|0775, dp);
			return 1;
		default:
			return -1;
		}
	if (c->qid.path != Qdir)
		switch(i){
		case DEVDOTDOT:
			devdir(c, dqid, "fs", 0, eve, DMDIR|0775, dp);
			return 1;
		default:
			return -1;
		}
	switch(i){
	case DEVDOTDOT:
		devdir(c, tqid, "#k", 0, eve, DMDIR|0775, dp);
		return 1;
	case 0:
		devdir(c, cqid, "ctl", 0, eve, 0664, dp);
		return 1;
	}
	i--;			/* for ctl */
	qid.path = Qfirst + i;
	qid.vers = 0;
	qid.type = 0;
	mp = path2dev(i, 0);
	if (mp == nil)
		return -1;
	kstrcpy(up->genbuf, mp->name, sizeof(up->genbuf));
	devdir(c, qid, up->genbuf, mp->size, eve, 0664, dp);
	return 1;
}

static Chan*
mattach(char *spec)
{
	return devattach(fsdevtab.dc, spec);
}

static Walkqid*
mwalk(Chan *c, Chan *nc, char **name, int nname)
{
	if (!configed)
		rdconf();
	return devwalk(c, nc, name, nname, 0, 0, mgen);
}

static int
mstat(Chan *c, uchar *db, int n)
{
	Dir	d;
	Fsdev	*mp;
	int	p;

	p = c->qid.path;
	memset(&d, 0, sizeof d);
	switch(p){
	case Qtop:
		devdir(c, tqid, "#k", 0, eve, DMDIR|0775, &d);
		break;
	case Qdir:
		devdir(c, dqid, "fs", 0, eve, DMDIR|0775, &d);
		break;
	case Qctl:
		devdir(c, cqid, "ctl", 0, eve, 0664, &d);
		break;
	default:
		mp = path2dev(p - Qfirst, 1);
		devdir(c, c->qid, mp->name, mp->size, eve, 0664, &d);
	}
	n = convD2M(&d, db, n);
	if (n == 0)
		error(Ebadarg);
	return n;
}

static Chan*
mopen(Chan *c, int omode)
{
//	TODO: call devopen()?
	if((c->qid.type & QTDIR) && omode != OREAD)
		error(Eperm);
//	if (c->flag & COPEN)
//		return c;
	c->mode = openmode(omode & ~OTRUNC);
	c->flag |= COPEN;
	c->offset = 0;
	return c;
}

static void
mclose(Chan*)
{
	/* that's easy */
}


static long
io(Fsdev *mp, Inner *in, int isread, void *a, long l, vlong off)
{
	long wl;
	Chan *mc = in->idev;

	if (waserror()) {
		print("#k: %s: byte %,lld count %ld (of #k/%s): %s error: %s\n",
			in->iname, off, l, mp->name, (isread? "read": "write"),
			(up && up->errstr? up->errstr: ""));
		nexterror();
	}
	if (isread) {
		wl = devtab[mc->type]->read(mc, a, l, off);
		if (wl != l)
			error("#k: short read");
	} else {
		wl = devtab[mc->type]->write(mc, a, l, off);
		if (wl != l)
			error("#k: write error");
	}
	poperror();
	return wl;
}

static long
catio(Fsdev *mp, int isread, void *a, long n, vlong off)
{
	int	i;
	long	l, wl, res;
	Inner	*in;

	// print("catio %d %p %ld %lld\n", isread, a, n, off);
	res = n;
	for (i = 0; n >= 0 && i < mp->ndevs ; i++){
		in = &mp->inner[i];
		if (off > in->isize){
			off -= in->isize;
			continue;		/* not there yet */
		}
		if (off + n > in->isize)
			l = in->isize - off;
		else
			l = n;
		// print("\tdev %d %p %ld %lld\n", i, a, l, off);

		wl = io(mp, in, isread, a, l, off);
		assert(wl == l);

		a = (char*)a + l;
		off = 0;
		n -= l;
	}
	// print("\tres %ld\n", res - n);
	return res - n;
}

static long
interio(Fsdev *mp, int isread, void *a, long n, vlong off)
{
	int	i;
	long	boff, res, l, wl, wsz;
	vlong	woff, blk, mblk;
	Inner	*in;

	blk  = off / Blksize;
	boff = off % Blksize;
	wsz  = Blksize - boff;
	res = n;
	while(n > 0){
		mblk = blk / mp->ndevs;
		i    = blk % mp->ndevs;
		woff = mblk*Blksize + boff;
		if (n > wsz)
			l = wsz;
		else
			l = n;

		in = &mp->inner[i];
		wl = io(mp, in, isread, a, l, woff);
		if (wl != l || l == 0)
			error(Eio);

		a = (char*)a + l;
		n -= l;
		blk++;
		boff = 0;
		wsz = Blksize;
	}
	return res;
}

static long
mread(Chan *c, void *a, long n, vlong off)
{
	int	i, retry;
	long	l, res;
	Fsdev	*mp;
	Inner	*in;

	if (c->qid.type & QTDIR)
		return devdirread(c, a, n, 0, 0, mgen);
	if (c->qid.path == Qctl)
		return readstr((long)off, a, n, confstr + strlen(Cfgstr));
	i = c->qid.path - Qfirst;
	mp = path2dev(i, 1);

	if (off >= mp->size)
		return 0;
	if (off + n > mp->size)
		n = mp->size - off;
	if (n == 0)
		return 0;

	res = -1;
	switch(mp->type){
	case Fcat:
		res = catio(mp, Isread, a, n, off);
		break;
	case Finter:
		res = interio(mp, Isread, a, n, off);
		break;
	case Fpart:
		in = &mp->inner[0];
		res = io(mp, in, Isread, a, n, mp->start + off);
		assert(res == n);
		break;
	case Fmirror:
		retry = 0;
		do {
			if (retry > 0) {
				print("#k/%s: retry %d read for byte %,lld "
					"count %ld: %s\n", mp->name, retry, off,
					n, (up && up->errstr? up->errstr: ""));
				tsleep(&up->sleep, return0, 0, 2000);
			}
			for (i = 0; i < mp->ndevs; i++){
				if (waserror())
					continue;
				in = &mp->inner[i];
				l = io(mp, in, Isread, a, n, off);
				poperror();
				if (l >= 0){
					res = l;
					break;		/* read a good copy */
				}
			}
		} while (i == mp->ndevs && ++retry < 2);
		if (i == mp->ndevs) {
			/* no mirror had a good copy of the block */
			print("#k/%s: byte %,lld count %ld: CAN'T READ "
				"from mirror: %s\n", mp->name, off, n,
				(up && up->errstr? up->errstr: ""));
			error(Eio);
		} else if (retry > 0)
			print("#k/%s: byte %,lld count %ld: retry read OK "
				"from mirror: %s\n", mp->name, off, n,
				(up && up->errstr? up->errstr: ""));
		break;
	}
	return res;
}

static long
mwrite(Chan *c, void *a, long n, vlong off)
{
	int	i, allbad, retry;
	long	l, res;
	Fsdev	*mp;
	Inner	*in;

	if (c->qid.type & QTDIR)
		error(Eperm);
	if (c->qid.path == Qctl){
		mconfig(a, n);
		return n;
	}
	mp = path2dev(c->qid.path - Qfirst, 1);

	if (off >= mp->size)
		return 0;
	if (off + n > mp->size)
		n = mp->size - off;
	if (n == 0)
		return 0;
	res = n;
	switch(mp->type){
	case Fcat:
		res = catio(mp, Iswrite, a, n, off);
		break;
	case Finter:
		res = interio(mp, Iswrite, a, n, off);
		break;
	case Fpart:
		in = &mp->inner[0];
		res = io(mp, in, Iswrite, a, n, mp->start + off);
		if (res > n)
			res = n;
		break;
	case Fmirror:
		retry = 0;
		do {
			if (retry > 0) {
				print("#k/%s: retry %d write for byte %,lld "
					"count %ld: %s\n", mp->name, retry, off,
					n, (up && up->errstr? up->errstr: ""));
				tsleep(&up->sleep, return0, 0, 2000);
			}
			allbad = 1;
			for (i = mp->ndevs - 1; i >= 0; i--){
				if (waserror())
					continue;
				in = &mp->inner[i];
				l = io(mp, in, Iswrite, a, n, off);
				poperror();
				if (res > l)
					res = l;	/* shortest OK write */
				allbad = 0;		/* wrote a good copy */
			}
		} while (allbad && ++retry < 2);
		if (allbad) {
			/* no mirror took a good copy of the block */
			print("#k/%s: byte %,lld count %ld: CAN'T WRITE "
				"to mirror: %s\n", mp->name, off, n,
				(up && up->errstr? up->errstr: ""));
			error(Eio);
		} else if (retry > 0)
			print("#k/%s: byte %,lld count %ld: retry wrote OK "
				"to mirror: %s\n", mp->name, off, n,
				(up && up->errstr? up->errstr: ""));

		break;
	}
	return res;
}

Dev fsdevtab = {
	'k',
	"devfs",

	devreset,
	devinit,
	devshutdown,
	mattach,
	mwalk,
	mstat,
	mopen,
	devcreate,
	mclose,
	mread,
	devbread,
	mwrite,
	devbwrite,
	devremove,
	devwstat,
	devpower,
	devconfig,
};
