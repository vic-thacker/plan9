#define nil		((void*)0)
typedef	unsigned short	ushort;
typedef	unsigned char	uchar;
typedef unsigned long	ulong;
typedef unsigned int	uint;
typedef   signed char	schar;
typedef	long long	vlong;
typedef	unsigned long long uvlong;
typedef	ushort		Rune;
typedef union
{
	vlong	length;
} Length;
typedef long	jmp_buf[2];
#define	JMPBUFSP	0
#define	JMPBUFPC	1
#define	JMPBUFDPC	0
typedef unsigned int	mpdigit;	/* for /sys/include/mp.h */
typedef unsigned int	u32int;		/* for /sys/include/libsec.h */

/* FCR */
#define	FPINEX	(1<<30)
#define	FPOVFL	(1<<19)
#define	FPUNFL	((1<<29)|(1<<28))
#define	FPZDIV	(1<<18)

#define	FPRNR	(2<<26)
#define	FPRZ		(0<<26)
#define	FPRPINF	(3<<26)
#define	FPRNINF	(1<<26)
#define	FPRMASK	(3<<26)

#define	FPPEXT	0
#define	FPPSGL	0
#define	FPPDBL	0
#define	FPPMASK	0
/* FSR */
#define	FPAINEX	(1<<24)
#define	FPAOVFL	(1<<22)
#define	FPAUNFL	(1<<23)
#define	FPAZDIV	(1<<21)

/* stdarg */
typedef	char*	va_list;
#define va_start(list, start) list = (char*)(&(start)+1)
#define va_end(list)
#define va_arg(list, mode)\
	(sizeof(mode)==1?\
		((mode*)(list += 4))[-1]:\
	sizeof(mode)==2?\
		((mode*)(list += 4))[-1]:\
	sizeof(mode)>4?\
		((mode*)(list = (char*)((long)(list+7) & ~7) + sizeof(mode)))[-1]:\
		((mode*)(list += sizeof(mode)))[-1])
