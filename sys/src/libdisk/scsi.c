#include <u.h>
#include <libc.h>
#include <disk.h>

int scsiverbose;

#define codefile "/sys/lib/scsicodes"

static char *codes;
static void
getcodes(void)
{
	Dir d;
	int n, fd;

	if(codes != nil)
		return;

	if(dirstat(codefile, &d) < 0 || (fd = open(codefile, OREAD)) < 0)
		return;

	codes = emalloc(d.length+1+1);
	codes[0] = '\n';	/* for searches */
	n = readn(fd, codes+1, d.length);
	close(fd);

	if(n < 0) {
		free(codes);
		codes = nil;
		return;
	}
	codes[n] = '\0';
}
	
char*
scsierror(int asc, int ascq)
{
	char *p, *q;
	static char search[32];
	static char buf[128];

	getcodes();

	if(codes) {
		sprint(search, "\n%.2ux%.2ux ", asc, ascq);
		if(p = strstr(codes, search)) {
			p += 6;
			if((q = strchr(p, '\n')) == nil)
				q = p+strlen(p);
			snprint(buf, sizeof buf, "%.*s", (int)(q-p), p);
			return buf;
		}

		sprint(search, "\n%.2ux00", asc);
		if(p = strstr(codes, search)) {
			p += 6;
			if((q = strchr(p, '\n')) == nil)
				q = p+strlen(p);
			snprint(buf, sizeof buf, "(ascq #%.2ux) %.*s", ascq, (int)(q-p), p);
			return buf;
		}
	}

	sprint(buf, "scsi #%.2ux %.2ux", asc, ascq);
	return buf;
}


int
scsicmd(Scsi *s, uchar *cmd, int ccount, void *data, int dcount, int io)
{
	uchar resp[16];
	int n;
	long status;

	if(write(s->rawfd, cmd, ccount) != ccount) {
		werrstr("cmd write: %r");
		return -1;
	}

	switch(io){
	case Sread:
		n = read(s->rawfd, data, dcount);
		if(n < 0 && scsiverbose)
			fprint(2, "dat read: %r: cmd 0x%2.2uX\n", cmd[0]);
		break;
	case Swrite:
		n = write(s->rawfd, data, dcount);
		if(n != dcount && scsiverbose)
			fprint(2, "dat write: %r: cmd 0x%2.2uX\n", cmd[0]);
		break;
	default:
	case Snone:
		n = write(s->rawfd, resp, 0);
		if(n != 0 && scsiverbose)
			fprint(2, "none write: %r: cmd 0x%2.2uX\n", cmd[0]);
		break;
	}

	memset(resp, 0, sizeof(resp));
	if(read(s->rawfd, resp, sizeof(resp)) < 0) {
		werrstr("resp read: %r\n");
		return -1;
	}

	resp[sizeof(resp)-1] = '\0';
	status = atoi((char*)resp);
	if(status == 0)
		return n;

	werrstr("cmd %2.2uX: status %luX dcount %d n %d", cmd[0], status, dcount, n);
	return -1;
}

int
scsiready(Scsi *s)
{
	uchar cmd[6], resp[16];
	int status, i;

	for(i=0; i<3; i++) {
		memset(cmd, 0, sizeof(cmd));
		cmd[0] = 0x00;	/* unit ready */
		if(write(s->rawfd, cmd, sizeof(cmd)) != sizeof(cmd)) {
			if(scsiverbose)
				fprint(2, "ur cmd write: %r\n");
			goto bad;
		}
		write(s->rawfd, resp, 0);
		if(read(s->rawfd, resp, sizeof(resp)) < 0) {
			if(scsiverbose)
				fprint(2, "ur resp read: %r\n");
			goto bad;
		}
		resp[sizeof(resp)-1] = '\0';
		status = atoi((char*)resp);
		if(status == 0 || status == 0x02)
			return 0;
		if(scsiverbose)
			fprint(2, "target: bad status: %x\n", status);
	bad:;
	}
	return -1;
}

int
scsi(Scsi *s, uchar *cmd, int ccount, void *v, int dcount, int io)
{
	uchar req[6], sense[255], *data;
	int tries, code, key, n;
	char *p;

	data = v;
	SET(key, code);
	for(tries=0; tries<2; tries++) {
		n = scsicmd(s, cmd, ccount, data, dcount, io);
		if(n >= 0)
			return n;

		/*
		 * request sense
		 */
		memset(req, 0, sizeof(req));
		req[0] = 0x03;
		req[4] = sizeof(sense);
		memset(sense, 0xFF, sizeof(sense));
		if((n=scsicmd(s, req, sizeof(req), sense, sizeof(sense), Sread)) < 14)
			if(scsiverbose)
				fprint(2, "reqsense scsicmd %d: %r\n", n);
	
		if(scsiready(s) < 0)
			if(scsiverbose)
				fprint(2, "unit not ready\n");
	
		key = sense[2];
		code = sense[12];
		if(code == 0x17 || code == 0x18)	/* recovered errors */
			return dcount;
		if(code == 0x28 && cmd[0] == 0x43) {	/* get info and media changed */
			s->nchange++;
			s->changetime = time(0);
			continue;
		}
	}

	/* drive not ready, or medium not present */
	if(cmd[0] == 0x43 && key == 2 && (code == 0x3a || code == 0x04)) {
		s->changetime = 0;
		return -1;
	}

	if(cmd[0] == 0x43 && key == 5 && code == 0x24)	/* blank media */
		return -1;

	p = scsierror(code, sense[13]);

	werrstr("cmd #%.2ux: %s", cmd[0], p);

	if(scsiverbose)
		fprint(2, "scsi cmd #%.2ux: %.2ux %.2ux %.2ux: %s\n", cmd[0], key, code, sense[13], p);

//	if(key == 0)
//		return dcount;
	return -1;
}

Scsi*
openscsi(char *dev)
{
	Scsi *s;
	int rawfd, ctlfd, l, n;
	char *name, *p, buf[512];

	l = strlen(dev)+1+3+1;
	name = emalloc(l);

	snprint(name, l, "%s/raw", dev);
	if((rawfd = open(name, ORDWR)) < 0)
		return nil;

	snprint(name, l, "%s/ctl", dev);
	if((ctlfd = open(name, ORDWR)) < 0) {
	Error:
		close(rawfd);
		return nil;
	}

	n = readn(ctlfd, buf, sizeof buf);
	close(ctlfd);
	if(n <= 0)
		goto Error;

	if(strncmp(buf, "inquiry ", 8) != 0 || (p = strchr(buf, '\n')) == nil)
		goto Error;
	*p = '\0';

	if((p = strdup(buf+8)) == nil)
		goto Error;

	s = emalloc(sizeof(*s));
	s->rawfd = rawfd;
	s->inquire = p;
	s->changetime = time(0);
	
	if(scsiready(s) < 0){
		free(p);
		goto Error;
	}

	return s;
}
