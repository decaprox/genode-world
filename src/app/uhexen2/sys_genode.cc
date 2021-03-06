/*
   sys_unix.c
   Unix system interface code

   $Id: sys_unix.c 4285 2011-10-24 14:20:58Z sezero $
   */

extern "C" {

#include "quakedef.h"
#include "userdir.h"
#include "debuglog.h"

#include <errno.h>
#include <unistd.h>
#include <sys/stat.h>
#include <sys/time.h>
#include <fcntl.h>
#include <sys/mman.h>
#include <signal.h>
#include <dirent.h>
#include <fnmatch.h>
#include <time.h>
#include <utime.h>
#include "sdl_inc.h"

// heapsize: minimum 16mb, standart 32 mb, max is 96 mb.
// -heapsize argument will abide by these min/max settings
// unless the -forcemem argument is used
#define MIN_MEM_ALLOC	0x1000000
#define STD_MEM_ALLOC	0x2000000
#define MAX_MEM_ALLOC	0x6000000

cvar_t		sys_nostdout = {"sys_nostdout", "0", CVAR_NONE};
cvar_t		sys_throttle = {"sys_throttle", "0.02", CVAR_ARCHIVE};

qboolean		isDedicated;
static double		starttime;
static qboolean		first = true;


/* ===============================================================================
   FILE IO
   =============================================================================== */

int Sys_mkdir (const char *path, qboolean crash)
{
	int rc = mkdir (path, 0777);
	if (rc != 0 && errno == EEXIST)
		rc = 0;
	if (rc != 0 && crash)
	{
		rc = errno;
		//	Sys_Error("Unable to create directory %s: %s", path, strerror(rc));
	}
	return rc;
}

int Sys_rmdir (const char *path)
{
	return rmdir(path);
}

int Sys_unlink (const char *path)
{
	return unlink(path);
}

int Sys_rename (const char *oldp, const char *newp)
{
	return rename(oldp, newp);
}

long Sys_filesize (const char *path)
{
	struct stat	st;

	if (stat(path, &st) != 0)
		return -1;
	if (! S_ISREG(st.st_mode))
		return -1;

	return (long) st.st_size;
}

int Sys_FileType (const char *path)
{
	/*
	   if (access(path, R_OK) == -1)
	   return 0;
	   */
	struct stat	st;

	if (stat(path, &st) != 0)
		return FS_ENT_NONE;
	if (S_ISDIR(st.st_mode))
		return FS_ENT_DIRECTORY;
	if (S_ISREG(st.st_mode))
		return FS_ENT_FILE;

	return FS_ENT_NONE;
}

#define	COPY_READ_BUFSIZE		8192	/* BUFSIZ */
int Sys_CopyFile (const char *frompath, const char *topath)
{
	char	buf[COPY_READ_BUFSIZE];
	FILE	*in, *out;
	struct stat	st;
	int		err = 0;
	//	off_t		remaining, count;
	size_t		remaining, count;

	if (stat(frompath, &st) != 0)
	{
		Con_Printf ("%s: unable to stat %s\n", frompath, __thisfunc__);
		return 1;
	}
	in = fopen (frompath, "rb");
	if (!in)
	{
		Con_Printf ("%s: unable to open %s\n", frompath, __thisfunc__);
		return 1;
	}
	out = fopen (topath, "wb");
	if (!out)
	{
		Con_Printf ("%s: unable to create %s\n", topath, __thisfunc__);
		fclose (in);
		return 1;
	}

	remaining = st.st_size;
	memset (buf, 0, sizeof(buf));
	while (remaining)
	{
		if (remaining < sizeof(buf))
			count = remaining;
		else
			count = sizeof(buf);

		fread (buf, 1, count, in);
		err = ferror (in);
		if (err)
			break;

		fwrite (buf, 1, count, out);
		err = ferror (out);
		if (err)
			break;

		remaining -= count;
	}

	fclose (in);
	fclose (out);

	if (!err)
	{
		// restore the file's timestamp
		struct utimbuf		tm;
		tm.actime = time (NULL);
		tm.modtime = st.st_mtime;
		utime (topath, &tm);
	}

	return err;
}

/*
   =================================================
   simplified findfirst/findnext implementation:
   Sys_FindFirstFile and Sys_FindNextFile return
   filenames only, not a dirent struct. this is
   what we presently need in this engine.
   =================================================
   */
static DIR		*finddir;
static struct dirent	*finddata;
static char		*findpath, *findpattern;

const char *Sys_FindFirstFile (const char *path, const char *pattern)
{
	if (finddir)
		Sys_Error ("Sys_FindFirst without FindClose");

	finddir = opendir (path);
	if (!finddir)
		return NULL;

	findpattern = Z_Strdup (pattern);
	findpath = Z_Strdup (path);

	return Sys_FindNextFile();
}

const char *Sys_FindNextFile (void)
{
	struct stat	test;

	if (!finddir)
		return NULL;

	while ((finddata = readdir(finddir)) != NULL)
	{
		if (!fnmatch (findpattern, finddata->d_name, FNM_PATHNAME))
		{
			if ( (stat(va("%s/%s", findpath, finddata->d_name), &test) == 0)
			     && S_ISREG(test.st_mode))
				return finddata->d_name;
		}
	}

	return NULL;
}

void Sys_FindClose (void)
{
	if (finddir != NULL)
	{
		closedir(finddir);
		finddir = NULL;
	}
	if (findpath != NULL)
	{
		Z_Free (findpath);
		findpath = NULL;
	}
	if (findpattern != NULL)
	{
		Z_Free (findpattern);
		findpattern = NULL;
	}
}

/* ===============================================================================
   SYSTEM IO
   =============================================================================== */

/* ================
   Sys_MakeCodeWriteable
   ================ */

#if id386 && !defined(GLQUAKE)
void Sys_MakeCodeWriteable (unsigned long startaddr, unsigned long length)
{
	int		r;
	unsigned long	endaddr = startaddr + length;
#if !defined(__QNX__)
# if	1
	long		psize = sysconf (_SC_PAGESIZE);
# else
	int		psize = getpagesize();
# endif
	startaddr &= ~(psize - 1);
	endaddr = (endaddr + psize - 1) & ~(psize - 1);
#endif
	// systems with mprotect but not getpagesize (or similar) probably don't
	// need to page align the arguments to mprotect (eg, QNX)
	r = mprotect ((char *) startaddr, endaddr - startaddr, PROT_WRITE | PROT_READ | PROT_EXEC);

	if (r == -1)
		Sys_Error("Protection change failed\n");
}
#endif	/* id386, !GLQUAKE */


/* ================
   Sys_Init
   ================ */

static void Sys_Init (void)
{
	/* do we really need these with opengl ?? */
	Sys_SetFPCW();
}

#define ERROR_PREFIX	"\nFATAL ERROR: "
void Sys_Error (const char *error, ...)
{
	va_list		argptr;
	char		text[MAX_PRINTMSG];
	const char	text2[] = ERROR_PREFIX;
	const unsigned char	*p;

	va_start (argptr, error);
	q_vsnprintf (text, sizeof(text), error, argptr);
	va_end (argptr);

	if (con_debuglog)
	{
		LOG_Print (ERROR_PREFIX);
		LOG_Print (text);
		LOG_Print ("\n\n");
	}

	Host_Shutdown ();

	printf("%s%s\n\n", text2, text);

	exit (1);
}

void Sys_PrintTerm (const char *msgtxt)
{
	const unsigned char	*p;

	if (sys_nostdout.integer)
		return;

	for (p = (const unsigned char *) msgtxt; *p; p++)
		putc (*p, stdout);
}

void Sys_Quit (void)
{
	Host_Shutdown();

	exit (0);
}


/* ================
   Sys_DoubleTime
   ================ */

double Sys_DoubleTime (void)
{
	struct timeval	tp;
	double		now;

	gettimeofday (&tp, NULL);

	now = tp.tv_sec + tp.tv_usec / 1e6;

	if (first)
	{
		first = false;
		starttime = now;
		return 0.0;
	}

	return now - starttime;
}

char *Sys_DateTimeString (char *buf)
{
	static char strbuf[24];
	time_t t;
	struct tm *l;
	int val;

	if (!buf) buf = strbuf;

	t = time(NULL);
	l = localtime(&t);

	val = l->tm_mon + 1;	/* tm_mon: months since January [0,11] */
	buf[0] = val / 10 + '0';
	buf[1] = val % 10 + '0';
	buf[2] = '/';
	val = l->tm_mday;
	buf[3] = val / 10 + '0';
	buf[4] = val % 10 + '0';
	buf[5] = '/';
	val = l->tm_year / 100 + 19;	/* tm_year: #years since 1900. */
	buf[6] = val / 10 + '0';
	buf[7] = val % 10 + '0';
	val = l->tm_year % 100;
	buf[8] = val / 10 + '0';
	buf[9] = val % 10 + '0';

	buf[10] = ' ';

	val = l->tm_hour;
	buf[11] = val / 10 + '0';
	buf[12] = val % 10 + '0';
	buf[13] = ':';
	val = l->tm_min;
	buf[14] = val / 10 + '0';
	buf[15] = val % 10 + '0';
	buf[16] = ':';
	val = l->tm_sec;
	buf[17] = val / 10 + '0';
	buf[18] = val % 10 + '0';

	buf[19] = '\0';

	return buf;
}


/* ================
   Sys_ConsoleInput
   ================ */

const char *Sys_ConsoleInput (void)
{
	static char	con_text[256];
	static int	textlen;
	char		c;
	fd_set		set;
	struct timeval	timeout;

	FD_ZERO (&set);
	FD_SET (0, &set);	// stdin
	timeout.tv_sec = 0;
	timeout.tv_usec = 0;

	while (select (1, &set, NULL, NULL, &timeout))
	{
		read (0, &c, 1);
		if (c == '\n' || c == '\r')
		{
			con_text[textlen] = '\0';
			textlen = 0;
			return con_text;
		}
		else if (c == 8)
		{
			if (textlen)
			{
				textlen--;
				con_text[textlen] = '\0';
			}
			continue;
		}
		con_text[textlen] = c;
		textlen++;
		if (textlen < (int) sizeof(con_text))
			con_text[textlen] = '\0';
		else
		{
			// buffer is full
			textlen = 0;
			con_text[0] = '\0';
			Sys_PrintTerm("\nConsole input too long!\n");
			break;
		}
	}

	return NULL;
}

void Sys_Sleep (unsigned long msecs)
{
	if (!msecs)
		return;
	else if (msecs > 1000)
		msecs = 1000;

	usleep (msecs * 1000);
}

void Sys_SendKeyEvents (void)
{
	IN_SendKeyEvents();
}

char *Sys_GetClipboardData (void)
{
	return NULL;
}

static int Sys_GetBasedir (char *argv0, char *dst, size_t dstsize)
{
	char	*tmp;

	if (getcwd(dst, dstsize - 1) == NULL)
		return -1;

	tmp = dst;
	while (*tmp != 0)
		tmp++;
	while (*tmp == 0 && tmp != dst)
	{
		--tmp;
		if (tmp != dst && *tmp == '/')
			*tmp = 0;
	}

	return 0;
}

#if DO_USERDIRS
static int Sys_GetUserdir (char *dst, size_t dstsize)
{
	return 1;
}
#endif	/* DO_USERDIRS */

static void Sys_CheckSDL (void)
{
#if defined(SDLQUAKE)
	const SDL_version *sdl_version;

	sdl_version = SDL_Linked_Version();
	Sys_Printf("Found SDL version %i.%i.%i\n",sdl_version->major,sdl_version->minor,sdl_version->patch);
	if (SDL_VERSIONNUM(sdl_version->major,sdl_version->minor,sdl_version->patch) < SDL_REQUIREDVERSION)
	{	/*reject running under SDL versions older than what is stated in sdl_inc.h */
		Sys_Error("You need at least v%d.%d.%d of SDL to run this game.", SDL_MIN_X,SDL_MIN_Y,SDL_MIN_Z);
	}
# if defined(SDL_NEW_VERSION_REJECT)
	if (SDL_VERSIONNUM(sdl_version->major,sdl_version->minor,sdl_version->patch) >= SDL_NEW_VERSION_REJECT)
	{	/*reject running under SDL versions newer than what is stated in sdl_inc.h */
		Sys_Error("Your version of SDL library is incompatible with me.\n"
			  "You need a library version in the line of %d.%d.%d\n", SDL_MIN_X,SDL_MIN_Y,SDL_MIN_Z);
	}
# endif /* SDL_NEW_VERSION_REJECT */
#endif	/* SDLQUAKE */
}

static void PrintVersion (void)
{
#if HOT_VERSION_BETA
	Sys_Printf ("Hammer of Thyrion, %s-%s (%s) pre-release\n", HOT_VERSION_STR, HOT_VERSION_BETA_STR, HOT_VERSION_REL_DATE);
#else
	Sys_Printf ("Hammer of Thyrion, release %s (%s)\n", HOT_VERSION_STR, HOT_VERSION_REL_DATE);
#endif
	Sys_Printf ("running on %s engine %4.2f (%s)\n", ENGINE_NAME, ENGINE_VERSION, PLATFORM_STRING);
	Sys_Printf ("More info / sending bug reports:  http://uhexen2.sourceforge.net\n");
}

#include "snd_sys.h"
static const char *help_strings[] = {
	"     [-v | --version]        Display version information",
#ifndef DEMOBUILD
#   if defined(H2MP)
	"     [-noportals]            Disable the mission pack support",
#   else
	"     [-portals | -h2mp ]     Run the Portal of Praevus mission pack",
#   endif
#endif
#ifdef GLQUAKE
	"     [-bpp]                  Depth for GL fullscreen mode",
	"     [-vsync]                Enable sync with monitor refresh",
	"     [-g | -gllibrary]       Select 3D rendering library",
	"     [-fsaa N]               Enable N sample antialiasing",
	"     [-paltex]               Enable 8-bit textures",
	"     [-nomtex]               Disable multitexture detection/usage",
#endif
#if !defined(_NO_SOUND)
#if SOUND_NUMDRIVERS
	"     [-s | -nosound]         Run the game without sound",
#endif
#if (SOUND_NUMDRIVERS > 1)
#if HAVE_OSS_SOUND
	"     [-sndoss]               Use OSS sound",
#endif
#if HAVE_ALSA_SOUND
	"     [-sndalsa]              Use ALSA sound (alsa > 1.0.1)",
#endif
#if HAVE_SUN_SOUND
	"     [-sndsun | -sndbsd]     Use SUN / BSD sound",
#endif
#if HAVE_SDL_SOUND
	"     [-sndsdl]               Use SDL sound",
#endif
#endif	/*  SOUND_NUMDRIVERS */
#endif	/* _NO_SOUND */
	"     [-nomouse]              Disable mouse usage",
	"     [-listen N]             Enable multiplayer with max N players",
	"     [-heapsize Bytes]       Heapsize (memory to allocate)",
	NULL
};

static void PrintHelp(const char *name)
{
	int i = 0;

	Sys_Printf ("Usage: %s [options]\n", name);
	while (help_strings[i])
	{
		Sys_PrintTerm (help_strings[i]);
		Sys_PrintTerm ("\n");
		i++;
	}
	Sys_PrintTerm ("\n");
}

/* ===============================================================================
   MAIN
   =============================================================================== */

static quakeparms_t	parms;
static char	cwd[MAX_OSPATH];
#if DO_USERDIRS
static char	userdir[MAX_OSPATH];
#endif
#if defined(SDLQUAKE)
static Uint8		appState;
#endif

} /* extern "C" */

int hexen_main (int argc, char **argv)
{
	int    i;
	double time, oldtime, newtime;

	PrintVersion();

	if (argc > 1)
	{
		for (i = 1; i < argc; i++)
		{
			if ( !(strcmp(argv[i], "-v")) || !(strcmp(argv[i], "-version" )) ||
			     !(strcmp(argv[i], "--version")) )
			{
				exit(0);
			}
			else if ( !(strcmp(argv[i], "-h")) || !(strcmp(argv[i], "-help" )) ||
				  !(strcmp(argv[i], "-?")) || !(strcmp(argv[i], "--help")) )
			{
				PrintHelp(argv[0]);
				exit (0);
			}
		}
	}

	memset (cwd, 0, sizeof(cwd));
	if (Sys_GetBasedir(argv[0], cwd, sizeof(cwd)) != 0)
		Sys_Error ("Couldn't determine current directory");

	/* initialize the host params */
	memset (&parms, 0, sizeof(parms));
	parms.basedir = cwd;
	parms.userdir = cwd;
	parms.argc = argc;
	parms.argv = argv;
	host_parms = &parms;

	LOG_Init (&parms);

	Sys_Printf("basedir is: %s\n", parms.basedir);
	Sys_Printf("userdir is: %s\n", parms.userdir);

	COM_ValidateByteorder ();

	isDedicated = (COM_CheckParm ("-dedicated") != 0);

	Sys_CheckSDL ();

	if (isDedicated)
		parms.memsize = MIN_MEM_ALLOC;
	else
		parms.memsize = STD_MEM_ALLOC;

	i = COM_CheckParm ("-heapsize");
	if (i && i < com_argc-1)
	{
		parms.memsize = atoi (com_argv[i+1]) * 1024;

		if ((parms.memsize > MAX_MEM_ALLOC) && !(COM_CheckParm ("-forcemem")))
		{
			Sys_Printf ("Requested memory (%d Mb) too large, using the default maximum.\n", parms.memsize/(1024*1024));
			Sys_Printf ("If you are sure, use the -forcemem switch.\n");
			parms.memsize = MAX_MEM_ALLOC;
		}
		else if ((parms.memsize < MIN_MEM_ALLOC) && !(COM_CheckParm ("-forcemem")))
		{
			Sys_Printf ("Requested memory (%d Mb) too little, using the default minimum.\n", parms.memsize/(1024*1024));
			Sys_Printf ("If you are sure, use the -forcemem switch.\n");
			parms.memsize = MIN_MEM_ALLOC;
		}
	}

	parms.membase = malloc (parms.memsize);

	if (!parms.membase)
		Sys_Error ("Insufficient memory.\n");

	Sys_Init ();

	Host_Init();

	oldtime = Sys_DoubleTime ();

	/* main window message loop */
	while (1)
	{
#if 0
		if (isDedicated)
		{
			newtime = Sys_DoubleTime ();
			time = newtime - oldtime;

			while (time < sys_ticrate.value )
			{
				usleep (1000);
				newtime = Sys_DoubleTime ();
				time = newtime - oldtime;
			}

			Host_Frame (time);
			oldtime = newtime;
		}
		else
#endif
		{
			appState = SDL_GetAppState();
			/* If we have no input focus at all, sleep a bit */
			if ( !(appState & (SDL_APPMOUSEFOCUS | SDL_APPINPUTFOCUS)) || cl.paused)
			{
				usleep (16000);
			}
			/* If we're minimised, sleep a bit more */
			if ( !(appState & SDL_APPACTIVE) )
			{
				scr_skipupdate = 1;
				usleep (32000);
			}
			else
			{
				scr_skipupdate = 0;
			}

			newtime = Sys_DoubleTime ();
			time = newtime - oldtime;

			Host_Frame (time);

			if (time < sys_throttle.value)
				usleep (1000);

			oldtime = newtime;
		}
	}

	return 0;
}

/* keep the noise down */
/* x86 */
extern "C" int _sigprocmask(int, const sigset_t*, sigset_t*) { return -1; }
/* arm */
extern "C" int sigprocmask(int, const sigset_t*, sigset_t*) { return -1; }

#include <base/thread.h>

int main(int argc, char *argv[])
{
	/* Hexen 2 needs lots of stack */
	Genode::Thread_base::myself()->stack_size(256 * 1024);

	return ::hexen_main(argc, argv);
}
