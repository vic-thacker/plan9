%{
#include "common.h"
#include "smtp.h"
#include <ctype.h>

char	*yystart;	/* start of line being parsed */
char	*yylp;		/* next character to be lex'd */
Node	*root;
Field	*firstfield;
Field	*lastfield;
Node	*usender;
Node	*usys;
Node	*udate;
int	originator;
int	date;
%}

%term WORD
%term DATE
%term RESENT_DATE
%term RETURN_PATH
%term FROM
%term SENDER
%term REPLY_TO
%term RESENT_FROM
%term RESENT_SENDER
%term RESENT_REPLY_TO
%term TO
%term CC
%term BCC
%term RESENT_TO
%term RESENT_CC
%term RESENT_BCC
%term REMOTE
%term FROM
%term OPTIONAL
%start msg
%%

msg		: fields
		| unixfrom '\n' fields
		;
fields		: field '\n'
		| field '\n' fields
		;
field		: dates
			{ date = 1; }
		| originator
			{ originator = 1; }
		| destination
		| optional
		;
unixfrom	: FROM route_addr unix_date_time REMOTE FROM word
			{ freenode($1); freenode($4); freenode($5);
			  usender = $2; udate = $3; usys = $6;
			}
		;
originator	: REPLY_TO ':' address_list
			{ newfield(link3($1, $2, $3), 1); }
		| RETURN_PATH ':' route_addr
			{ newfield(link3($1, $2, $3), 1); }
		| FROM ':' mailbox_list
			{ newfield(link3($1, $2, $3), 1); }
		| SENDER ':' mailbox
			{ newfield(link3($1, $2, $3), 1); }
		| RESENT_REPLY_TO ':' address_list
			{ newfield(link3($1, $2, $3), 1); }
		;
dates 		: DATE ':' date_time
			{ newfield(link3($1, $2, $3), 0); }
		| RESENT_DATE ':' date_time
			{ newfield(link3($1, $2, $3), 0); }
		;
destination	: TO ':' address_list
			{ newfield(link3($1, $2, $3), 0); }
		| RESENT_TO ':' address_list
			{ newfield(link3($1, $2, $3), 0); }
		| CC ':' address_list
			{ newfield(link3($1, $2, $3), 0); }
		| RESENT_CC ':' address_list
			{ newfield(link3($1, $2, $3), 0); }
		| BCC ':'
			{ newfield(link2($1, $2), 0); }
		| BCC ':' address_list
			{ newfield(link3($1, $2, $3), 0); }
		| RESENT_BCC ':' 
			{ newfield(link2($1, $2), 0); }
		| RESENT_BCC ':' address_list
			{ newfield(link3($1, $2, $3), 0); }
		;
optional	: other ':' things
			{ newfield(link3($1, $2, $3), 0); }
		;
address_list	: address
		| address_list ',' address
			{ $$ = link3($1, $2, $3); }
		;
address		: mailbox
		| group
		;
group		: phrase ':' mailbox_list ';'
			{ $$ = link3($1, $2, link2($3, $4)); }
		;
mailbox_list	: mailbox
		| mailbox_list ',' mailbox
			{ $$ = link3($1, $2, $3); }
		;
mailbox		: route_addr
		| phrase brak_addr
			{ $$ = link2($1, $2); }
		;
brak_addr	: '<' route_addr '>'
			{ $$ = link3($1, $2, $3); }
		| '<' '>'
			{ $$ = anonymous($2); freenode($1); }
		;
route_addr	: route ':' addr_spec
			{ $$ = bang($1, $3); freenode($2); }
		| addr_spec
		;
route		: '@' domain
			{ $$ = $2; freenode($1); }
		| route ',' '@' domain
			{ $$ = bang($1, $4); freenode($2); freenode($3); }
		;
addr_spec	: local_part
			{ $$ = address($1); }
		| local_part '@' domain
			{ $$ = bang($3, $1); freenode($2); }
		;
local_part	: word
		;
domain		: word
		;
phrase		: word
		| phrase word
			{ $$ = link2($1, $2); }
		;
things		: thing
		| things thing
			{ $$ = link2($1, $2); }
		;
thing		: word | '<' | '>' | '@' | ':' | ';' | ','
		;		
date_time	: things
		;
unix_date_time	: word word word unix_time word word
			{ $$ = link3($1, $3, link3($2, $6, link2($4, $5))); }
		;
unix_time	: word
		| unix_time ':' word
			{ $$ = link3($1, $2, $3); }
		;
word		: WORD | DATE | RESENT_DATE | RETURN_PATH | FROM | SENDER
		| REPLY_TO | RESENT_FROM | RESENT_SENDER | RESENT_REPLY_TO
		| TO | CC | BCC | RESENT_TO | RESENT_CC | RESENT_BCC | REMOTE
		;
other		: WORD | REMOTE
		;
%%

/*
 *  Initialize the parsing.  Done once for each header field.
 */
void
yyinit(char *p)
{
	yystart = yylp = p;
	firstfield = lastfield = 0;
}

/*
 *  keywords identifying header fields we care about
 */
typedef struct Keyword	Keyword;
struct Keyword {
	char	*rep;
	int	val;
};

Keyword key[] = {
	{ "date", DATE },
	{ "resent-date", RESENT_DATE },
	{ "return_path", RETURN_PATH },
	{ "from", FROM },
	{ "sender", SENDER },
	{ "reply-to", REPLY_TO },
	{ "resent-from", RESENT_FROM },
	{ "resent-sender", RESENT_SENDER },
	{ "resent-reply-to", RESENT_REPLY_TO },
	{ "to", TO },
	{ "cc", CC },
	{ "bcc", BCC },
	{ "resent-to", RESENT_TO },
	{ "resent-cc", RESENT_CC },
	{ "resent-bcc", RESENT_BCC },
	{ "remote", REMOTE },
	{ "who-the-hell-cares", WORD }
};

/*
 *  Lexical analysis for an rfc822 header field.  Continuation lines
 *  are handled in yywhite() when skipping over white space.
 *
 */
yylex(void)
{
	String *t;
	int quoting;
	char *start;
	Keyword *kp;
	int c;

/*	print("lexing\n"); /**/
	if(*yylp == 0)
		return 0;

	quoting = 0;
	start = yylp;
	yylval = malloc(sizeof(Node));
	yylval->white = yylval->s = 0;
	yylval->next = 0;
	yylval->addr = 0;
	for(t = 0; *yylp; yylp++){
		c = *yylp & 0x7f;
		if(quoting){
			switch(c){
			case '\\':
				c = (*++yylp)&0x7f;
				if(c==0){
					--yylp;
					c = '\\';
				}
				break;
			case '"':
				quoting = 0;
				break;
			}
		} else {
			switch(c){
			case '\\':
				c = (*++yylp)&0x7f;
				if(c==0){
					--yylp;
					c = '\\';
				}
				break;
			case '(':
			case ' ':
			case '\t':
			case '\r':
				goto out;
			case '\n':
			case '@':
			case '>':
			case '<':
			case ':':
			case ',':
			case ';':
				if(yylp == start){
					if(c == '\n'){
						yystart = ++yylp;
					} else {
						yylp++;
					}
					yylval->white = yywhite();
/*					print("lex(c %c)\n", c); /**/
					yylval->end = yylp;
					return yylval->c = c;
				}
				goto out;
			case '"':
				quoting = 1;
				break;
			default:
				break;
			}
		}
		if(t == 0)
			t = s_new();
		s_putc(t, c);
	}
out:
	if(t)
		s_terminate(t);
	yylval->white = yywhite();
	yylval->s = t;
	for(kp = key; kp->val != WORD; kp++)
		if(cistrcmp(s_to_c(t), kp->rep)==0)
			break;
/*	print("lex(%d) %s\n", kp->val-WORD, s_to_c(t)); /**/
	yylval->end = yylp;
	return yylval->c = kp->val;
}

void
yyerror(char *x)
{
	/*fprint(2, "parse err: %s\n", x);/**/
}

/*
 *  parse white space and comments
 */
String *
yywhite(void)
{
	String *w;
	int clevel;
	int c;

	clevel = 0;
	for(w = 0; *yylp; yylp++){
		c = *yylp & 0x7f;
		if(clevel){
			switch(c){
			case '\n':
				/*
				 *  look for multiline fields
				 */
				if(*(yylp+1)==' ' || *(yylp+1)=='\t')
					break;
				else
					goto out;
			case '\\':
				c = (*++yylp)&0x7f;
				if(c==0){
					--yylp;
					c = '\\';
				}
				break;
			case '(':
				clevel++;
				break;
			case ')':
				clevel--;
				break;
			}
		} else {
			switch(c){
			case '\\':
				c = (*++yylp)&0x7f;
				if(c==0){
					--yylp;
					c = '\\';
				}
				break;
			case '(':
				clevel++;
				break;
			case ' ':
			case '\t':
			case '\r':
				break;
			case '\n':
				/*
				 *  look for multiline fields
				 */
				if(*(yylp+1)==' ' || *(yylp+1)=='\t')
					break;
				else
					goto out;
			default:
				goto out;
			}
		}
		if(w == 0)
			w = s_new();
		s_putc(w, c);
	}
out:
	if(w)
		s_terminate(w);
	return w;
}

/*
 *  link two parsed entries together
 */
Node*
link2(Node *p1, Node *p2)
{
	Node *p;

	for(p = p1; p->next; p = p->next)
		;
	p->next = p2;
	return p1;
}

/*
 *  link three parsed entries together
 */
Node*
link3(Node *p1, Node *p2, Node *p3)
{
	Node *p;

	for(p = p2; p->next; p = p->next)
		;
	p->next = p3;

	for(p = p1; p->next; p = p->next)
		;
	p->next = p2;

	return p1;
}

/*
 *  make a!b, move all white space after both
 */
Node*
bang(Node *p1, Node *p2)
{
	if(p1->white){
		if(p2->white)
			s_append(p1->white, s_to_c(p2->white));
	} else
		p1->white = p2->white;

	s_append(p1->s, "!");
	if(p2->s)
		s_append(p1->s, s_to_c(p2->s));

	freenode(p2);
	p1->addr = 1;
	return p1;
}

/*
 *  mark as an address
 */
Node *
address(Node *p)
{
	p->addr = 1;
	return p;
}

/*
 *  case independent string compare
 */
int
cistrcmp(char *s1, char *s2)
{
	int c1, c2;

	for(; *s1; s1++, s2++){
		c1 = isupper(*s1) ? tolower(*s1) : *s1;
		c2 = isupper(*s2) ? tolower(*s2) : *s2;
		if (c1 != c2)
			return -1;
	}
	return *s2;
}

/*
 *  free a node
 */
void
freenode(Node *p)
{
	Node *tp;

	while(p){
		tp = p->next;
		if(p->s)
			s_free(p->s);
		if(p->white)
			s_free(p->white);
		free(p);
		p = tp;
	}
}


/*
 *  an anonymous user
 */
Node*
anonymous(Node *p)
{
	if(p->s)
		s_free(p->s);
	p->s = s_copy("pOsTmAsTeR");
	p->addr = 1;
	return p;
}

/*
 *  create a new field
 */
void
newfield(Node *p, int source)
{
	Field *f;

	f = malloc(sizeof(Field));
	f->next = 0;
	f->node = p;
	f->source = source;
	if(firstfield)
		lastfield->next = f;
	else
		firstfield = f;
	lastfield = f;
}

/*
 *  fee a list of fields
 */
void
freefield(Field *f)
{
	Field *tf;

	while(f){
		tf = f->next;
		freenode(f->node);
		free(f);
		f = tf;
	}
}

/*
 *  add some white space to a node
 */
Node*
whiten(Node *p)
{
	Node *tp;

	for(tp = p; tp->next; tp = tp->next)
		;
	if(tp->white == 0)
		tp->white = s_copy(" ");
	return p;
}
