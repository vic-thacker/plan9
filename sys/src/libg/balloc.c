#include <u.h>
#include <libc.h>
#include <libg.h>

Bitmap*
balloc(Rectangle r, int ldepth)
{
	uchar *buf, xbuf[3];
	int id;
	Bitmap *b;

	bneed(0);	/* flush so if there's an error we know it's our fault */
	buf = bneed(18);
	buf[0] = 'a';
	buf[1] = ldepth;
	BPLONG(buf+2, r.min.x);
	BPLONG(buf+6, r.min.y);
	BPLONG(buf+10, r.max.x);
	BPLONG(buf+14, r.max.y);
	if(!bwrite())
		return 0;	/* unfatal case: no free bitmap memory */
	if(read(bitbltfd, xbuf, 3)!=3 || xbuf[0]!='A')
		berror("balloc read");
	id = xbuf[1] | (xbuf[2]<<8);
	b = malloc(sizeof(Bitmap));
	if(b == 0){	/* oh bother */
		buf[0] = 'f';
		write(bitbltfd, xbuf, 3);
		return 0;
	}
	b->ldepth = ldepth;
	b->r = r;
	b->clipr = r;
	b->id = id;
	b->cache = 0;
	return b;
}

void
bfree(Bitmap *b)
{
	uchar *buf;

	buf = bneed(3);
	buf[0] = 'f';
	buf[1] = b->id;
	buf[2] = b->id>>8;
	free(b);
	bneed(0);	/* make sure the memory's freed before continuing */
}
