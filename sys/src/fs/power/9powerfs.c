#include	"all.h"
#include 	"io.h"

#ifndef	DATE
#define	DATE	568011600L+4*3600		/* Jan 1.0 1988 + 4 hours for EDT */
#endif

Vmedevice vmedevtab[] =
{
	{
		0, 0, 0xA8, 1, VMEA16SUP(void, 0x009000), 0,
		"jaguar", jaginit, jagintr, 0,
	}, /**/
	{
		0, 0, 0xD0, 5, VMEA24SUP(void, 0xF90000), 0,
		"hsvme", hsvmeinit, hsvmeintr, 0,
	}, /**/
/*	{
		0, 0, 0x20, 2, VMEA16SUP(void, 0x004000), 0,
		"eagle", eagleinit, eagleintr, 0,
	}, /**/
	{
		0, 0, 0xD2, 5, VMEA24SUP(void, 0x010000), 0,
		"cyclone", cyclinit, cyclintr, 0,
	}, /**/
	0
};

long	mktime		= DATE;				/* set by mkfile */
long	startsb		= 0;
int	ioid;
int	probeflag;

void
otherinit(void)
{
	vmeinit();
	lanceinit(0);	/**/
}

void
touser(void)
{
	int i;

	settime(rtctime());
	boottime = time();

	print("sysinit\n");
	sysinit();

	/*
	 * lance i/o processes
	 */
	lancestart();

	/*
	 * Ethernet i/o processes
	 */
	eaglestart();

	/*
	 * datakit i/o and timer processes
	 */
	dkstart();

	/*
	 * cyclone cpu link
	cyclstart();
	 */

	/*
	 * read ahead processes
	 */
	for(i=0; i<conf.nrahead; i++)
		userinit(rahead, 0, "rah");

	/*
	 * server processes
	 */
	for(i=0; i<conf.nserve; i++)
		userinit(serve, 0, "srv");

	/*
	 * worm "dump" copy process
	 */
	userinit(wormcopy, 0, "wcp");

	/*
	 * processes to read the console
	 */
	consserve();

	/*
	 * "sync" copy process
	 */
	u->text = "scp";
	synccopy();
}
