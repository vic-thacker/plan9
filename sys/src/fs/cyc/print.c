#include	<u.h>
#include	"fns.h"

enum
{
	IDIGIT	= 30,
	MAXCONV	= 30,
	NONE	= -1000,

	FPLUS	= (1<<0),
	FMINUS	= (1<<1),
	FSHARP	= (1<<2),
	FLONG	= (1<<3),
	FSHORT	= (1<<4),
	FUNSIGN	= (1<<5),
};

#define	PTR	sizeof(char*)
#define	SHORT	sizeof(int)
#define	INT	sizeof(int)
#define	LONG	sizeof(long)

static	int	convcount  = { 6 };

static	int	noconv(void*, int, int, int, int);
static	int	flags(void*, int, int, int, int);
static	int	cconv(void*, int, int, int, int);
static	int	sconv(void*, int, int, int, int);
static	int	percent(void*, int, int, int, int);

static
int	(*fmtconv[MAXCONV])(void*, int, int, int, int) =
{
	noconv,
	flags,
	numbconv,
	cconv, sconv, percent,
};
static
char	fmtindex[128] =
{
	['+'] 1,
	['-'] 1,
	['#'] 1,
	['h'] 1,
	['l'] 1,
	['u'] 1,

	['d'] 2,
	['o'] 2,
	['x'] 2,
	['X'] 2,

	['c'] 3,
	['s'] 4,
	['%'] 5,
};

int
fmtinstall(int c, int (*f)(void*, int, int, int, int))
{

	c &= 0177;
	if(convcount >= MAXCONV)
		return 1;
	fmtindex[c] = convcount++;
	fmtconv[fmtindex[c]] = f;
	return 0;
}

int
print(char *fmt, ...)
{
	int f1, f2, f3, c, n;
	void *argp;

	argp = (void*)(&fmt + 1);

loop:
	c = *fmt++;
	if(c != '%') {
		if(c == 0) {
			return 0;
		}
		putc(c);
		goto loop;
	}
	f1 = NONE;
	f2 = NONE;
	f3 = 0;

	/*
	 * read one of the following
	 *	1. number, => f1, f2 in order.
	 *	2. '*' same as number
	 *	3. '.' ignored (separates numbers)
	 *	4. flag => f3
	 *	5. verb and terminate
	 */
l0:
	c = *fmt++;

l1:
	if(c == 0) {
		fmt--;
		goto loop;
	}
	if(c == '.') {
		if(f1 == NONE)
			f1 = 0;
		f2 = 0;
		goto l0;
	}
	if((c >= '1' && c <= '9') ||
	   (c == '0' && f1 != NONE)) {	/* '0' is a digit for f2 */
		n = 0;
		while(c >= '0' && c <= '9') {
			n = n*10 + c-'0';
			c = *fmt++;
		}
		if(f1 == NONE)
			f1 = n;
		else
			f2 = n;
		goto l1;
	}
	if(c == '*') {
		n = *(int*)argp;
		argp = (char*)argp + INT;
		if(f1 == NONE)
			f1 = n;
		else
			f2 = n;
		goto l0;
	}
	c &= 0177;
	n = (*fmtconv[fmtindex[c]])(argp, f1, f2, f3, c);
	if(n < 0) {
		f3 |= -n;
		goto l0;
	}
	argp = (char*)argp + n;
	goto loop;
}

int
numbconv(void *o, int f1, int f2, int f3, int chr)
{
	char s[IDIGIT];
	int i, f, n, r, b, ucase;
	short h;
	long v;

	ucase = 0;
	b = chr;
	switch(chr) {
	case 'u':
		f3 |= FUNSIGN;
	case 'd':
		b = 10;
		break;

	case 'o':
		b = 8;
		break;

	case 'X':
		ucase = 1;
	case 'x':
		b = 16;
		break;
	}

	f = 0;
	switch(f3 & (FLONG|FSHORT|FUNSIGN)) {
	case FLONG:
		v = *(long*)o;
		r = LONG;
		break;

	case FUNSIGN|FLONG:
		v = *(ulong*)o;
		r = LONG;
		break;

	case FSHORT:
		h = *(int*)o;
		v = h;
		r = SHORT;
		break;

	case FUNSIGN|FSHORT:
		h = *(int*)o;
		v = (ushort)h;
		r = SHORT;
		break;

	default:
		v = *(int*)o;
		r = INT;
		break;

	case FUNSIGN:
		v = *(unsigned*)o;
		r = INT;
		break;
	}
	if(!(f3 & FUNSIGN) && v < 0) {
		v = -v;
		f = 1;
	}
	s[IDIGIT-1] = 0;
	for(i = IDIGIT-2;; i--) {
		n = (ulong)v % b;
		n += '0';
		if(n > '9')
			n += 'a' - ('9'+1);
		s[i] = n;
		if(i < 2)
			break;
		v = (ulong)v / b;
		if(f2 != NONE && i >= IDIGIT-f2)
			continue;
		if(v <= 0)
			break;
	}
	if(f3 & FSHARP)
	if(s[i] != '0') {
		if(b == 8)
			s[--i] = '0';
		else
		if(b == 16) {
			if(ucase)
				s[--i] = 'X';
			else
				s[--i] = 'x';
			s[--i] = '0';
		}
	}
	if(f)
		s[--i] = '-';
	strconv(s+i, f1, NONE, f3);
	return r;
}

void
strconv(char *s, int f1, int f2, int f3)
{
	int n, c;

	if(f1 != NONE && f3 & FMINUS)
		f1 = -f1;
	n = strlen(s);
	if(f1 != NONE && f1 >= 0)
		while(n < f1) {
			putc(' ');
			n++;
		}
	for(; c = *s++;)
		if(f2 == NONE || f2 > 0) {
			putc(c);
			if(f2 != NONE)
				f2--;
		}
	if(f1 != NONE && f1 < 0) {
		f1 = -f1;
		while(n < f1) {
			putc(' ');
			n++;
		}
	}
}

static
int
noconv(void *o, int f1, int f2, int f3, int chr)
{
	char s[4];

	USED(o);

	s[0] = '*';
	s[1] = chr;
	s[2] = '*';
	s[3] = 0;
	strconv(s, f1, f2, f3);
	return 0;
}

static
int
sconv(void *o, int f1, int f2, int f3, int chr)
{
	strconv(*(char**)o, f1, f2, f3);
	return PTR;
}

static
int
cconv(void *o, int f1, int f2, int f3, int chr)
{
	char s[2];

	s[0] = *(int*)o;
	s[1] = 0;
	strconv(s, f1, f2, f3);
	return INT;
}

static
int
percent(void *o, int f1, int f2, int f3, int chr)
{
	char s[2];

	USED(o);

	s[0] = '%';
	s[1] = 0;
	strconv(s, f1, f2, f3);
	return 0;
}

static
int
flags(void *o, int f1, int f2, int f3, int chr)
{

	USED(o);

	switch(chr) {
	case '+':
		return -FPLUS;

	case '-':
		return -FMINUS;

	case '#':
		return -FSHARP;

	case 'h':
		return -FSHORT;

	case 'l':
		return -FLONG;

	case 'u':
		return -FUNSIGN;
	}
	return 0;
}
