int Gmatch(char *s, char *p);
char * doalloc(int n);
void popmem(void);
void pushmem(void);
char * strsave(char * s);
int  callargv(Call **cc, int ncc, Sig **ss, int nss, int num, char *form);
int callcheck(Call *c);
int  callcount(int ncalls, int nacts, int nforms);
int  callexpand(Macro *m, Call ***ccc, char	*name);
void callinline(Call *c);
Call * callookup(Macro *m, char *cname);
void  callrelookup(Macro *m, Call *c);
void error0(void);
void  error(char *fmt, ...);
void fatal(char *fmt, ...);
void warn(char *fmt, ...);
int  anglexpand(char *head, char *tail);
void  bcopy(char *t, char *s, int n);
void curlyexpand(char *head, char *tail);
int  doexpand(char *head, char *tail);
int expand(char *s, char ***v, int msk);
int squarexpand(char *head, char *tail);
void  lslcall(Call *c);
void lslflush(void);
void lslhead(Macro *m);
void lsltail(Macro *m);
int  cmppinc(Pin **a, Pin **b);
int cmppinm(Pin *a, Pin *b);
int  hash(char *s, int size);
void macalias(char *name1, char *name2);
void  macargv(Macro *m);
void macheck(void);
void  macinline(Macro *m);
Macro * maclookup(char	*name, int new);
char *macname(char *file);
void   macprint(void);
void main(int argc, char **argv);
Pin * pinamsearch(Macro *m, char *pname);
void pinlookup(Macro *m, char *pname, Sig **s, int  ns, int num, int dir);
int pinmatch(Macro *m, char *pname, Pin ***ppp);
Pin * pinumsearch(Macro *m, int num);
int  explode(int nwords);
int  getline(void);
void  newfile(char *file);
void scan(char *file);
void  scanc(void);
int  scandir(char *dir);
void scanm(void);
int scanpin(char *pname, int *num, char **name);
void scant(void);
void scantp(void);
void ungetline(void);
int global(char *name);
int sigexpand(Macro *m, char *sname, Sig ***ss);
Sig * siglookup(Macro *m, char *sname);
void wcall(Call *c);
void whead(Macro *m);
void wtail(Macro *m);
