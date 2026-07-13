#include "../doomdef.h"
#include "../d_main.h"
#include "../m_argv.h"

#ifdef __CELLOS_LV2__
#include <sys/process.h>
SYS_PROCESS_PARAM(1001, 1024*1024)
#ifndef LOGMESSAGES
#define LOGMESSAGES
#endif
#endif

int main(int argc, char **argv)
{
#ifdef LOGMESSAGES
#ifdef __CELLOS_LV2__
	logstream = fopen("/dev_hdd0/game/SRB222PS3/USRDIR/log.txt", "wt");
#else
	logstream = fopen("log.txt", "wt");
#endif
#endif

	myargc = argc;
	myargv = argv; /// \todo pull out path to exe from this string

	// startup SRB2
	CONS_Printf("Setting up SRB2...\n");
	D_SRB2Main();
	CONS_Printf("Entering main game loop...\n");
	// never return
	D_SRB2Loop();

	// return to OS
#ifndef __GNUC__
	return 0;
#endif
}
