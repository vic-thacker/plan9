#include <u.h>
#include <libc.h>
#include <bio.h>
#include "ndb.h"
#include "ndbhf.h"

enum {
	Dptr,	/* pointer to database file */
	Cptr,	/* pointer to first chain entry */
	Cptr1,	/* pointer to second chain entry */
};

/*
 *  generate a hash value for an ascii string (val) given
 *  a hash table length (hlen)
 */
ulong
ndbhash(char *vp, int hlen)
{
	ulong hash;
	uchar *val = (uchar*)vp;

	for(hash = 0; *val; val++)
		hash = (hash*13) + *val-'a';
	return hash % hlen;
}

/*
 *  read a hash file with buffering
 */
static uchar*
hfread(Ndbhf *hf, long off, int len)
{
	if(off < hf->off || off + len > hf->off + hf->len){
		if(seek(hf->fd, off, 0) < 0
		|| (hf->len = read(hf->fd, hf->buf, sizeof(hf->buf))) < len){
			hf->off = -1;
			return 0;
		}
		hf->off = off;
	}
	return &hf->buf[off-hf->off];
}

/*
 *  return an opened hash file if one exists for the
 *  attribute and if it is current vis-a-vis the data
 *  base file
 */
static Ndbhf*
hfopen(Ndb *db, char *attr)
{
	Ndbhf *hf;
	char buf[sizeof(hf->attr)+sizeof(db->file)+2];
	uchar *p;
	Dir d;

	/* try opening the data base if it's closed */
	if(db->mtime==0 && ndbreopen(db) < 0)
		return 0;

	/* if the database has changed, throw out hash files and reopen db */
	if(dirfstat(Bfildes(db), &d) < 0 || db->qid.path != d.qid.path
	|| db->qid.vers != d.qid.vers){
		if(ndbreopen(db) < 0)
			return 0;
	};

	/* see if a hash file exists for this attribute */
	for(hf = db->hf; hf; hf= hf->next){
		if(strcmp(hf->attr, attr) == 0)
			return hf;
	}

	/* create a new one */
	hf = (Ndbhf*)malloc(sizeof(Ndbhf));
	if(hf == 0)
		return 0;

	/* compare it to the database file */
	strncpy(hf->attr, attr, sizeof(hf->attr)-1);
	sprint(buf, "%s.%s", db->file, hf->attr);
	hf->fd = open(buf, OREAD);
	if(hf->fd >= 0){
		hf->len = 0;
		hf->off = 0;
		p = hfread(hf, 0, 2*NDBULLEN);
		if(p){
			hf->dbmtime = NDBGETUL(p);
			hf->hlen = NDBGETUL(p+NDBULLEN);
			if(hf->dbmtime == db->mtime){
				hf->next = db->hf;
				db->hf = hf;
				return hf;
			}
		}
		close(hf->fd);
	}
	free(hf);
	return 0;
}

/*
 *  return the first matching entry
 */
Ndbtuple*
ndbsearch(Ndb *db, Ndbs *s, char *attr, char *val)
{
	uchar *p;

	s->hf = hfopen(db, attr);
	if(s->hf){
		s->ptr = ndbhash(val, s->hf->hlen)*NDBPLEN;
		p = hfread(s->hf, s->ptr+NDBHLEN, NDBPLEN);
		if(p == 0)
			return 0;
		s->ptr = NDBGETP(p);
		s->type = Cptr1;
	} else {
		s->ptr = 0;
		s->type = Dptr;
	}
	s->db = db;
	return ndbsnext(s, attr, val);
}

static Ndbtuple*
match(Ndbtuple *t, char *attr, char *val)
{
	Ndbtuple *nt;

	for(nt = t; nt; nt = nt->entry)
		if(strcmp(attr, nt->attr) == 0
		&& strcmp(val, nt->val) == 0)
			return nt;
	return 0;
}

/*
 *  return the next matching entry in the hash chain
 */
Ndbtuple*
ndbsnext(Ndbs *s, char *attr, char *val)
{
	Ndbtuple *t;
	Ndb *db;
	uchar *p;

/*fprint(2, "%s %s %d %lux\n", s->db->file, attr, s->type, s->ptr);/**/

	db = s->db;
	if(s->ptr == NDBNAP)
		goto nextfile;

	for(;;){
		if(s->type == Dptr){
			if(ndbseek(db, s->ptr, 0) < 0)
				break; 
			t = ndbparse(db);
			s->ptr = db->offset;
			if(t == 0)
				break;
			if(s->t = match(t, attr, val))
				return t;
			ndbfree(t);
		} else if(s->type == Cptr){
			if(ndbseek(db, s->ptr, 0) < 0)
				break; 
			s->ptr = s->ptr1;
			s->type = Cptr1;
			t = ndbparse(db);
			if(t == 0)
				break;
			if(s->t = match(t, attr, val))
				return t;
			ndbfree(t);
		} else if(s->type == Cptr1){
			if(s->ptr & NDBCHAIN){	/* hash chain continuation */
				s->ptr &= ~NDBCHAIN;
				p = hfread(s->hf, s->ptr+NDBHLEN, 2*NDBPLEN);
				if(p == 0)
					break;
				s->ptr = NDBGETP(p);
				s->ptr1 = NDBGETP(p+NDBPLEN);
				s->type = Cptr;
			} else {		/* end of hash chain */
				if(ndbseek(db, s->ptr, 0) < 0)
					break; 
				s->ptr = NDBNAP;
				t = ndbparse(db);
				if(t == 0)
					break;
				if(s->t = match(t, attr, val))
					return t;
				ndbfree(t);
				break;
			}
		}
	}

nextfile:

	/* nothing left to search? */
	s->ptr = NDBNAP;
	if(db->next == 0)
		return 0;

	/* advance search to next db file */
	return ndbsearch(db->next, s, attr, val);
}
