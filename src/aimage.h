#ifndef AIMAGE_H
#define AIMAGE_H

#include <time.h>
#include <stdio.h>

#ifdef HAVE_READLINE_READLINE_H
#include <readline/readline.h>
#include <readline/history.h>
#endif

#ifdef HAVE_CURSES_H
#include <curses.h>
#endif

#ifdef HAVE_TERM_H
#include <term.h>
#endif

#ifdef HAVE_NCURSES_TERM_H
#include <ncurses/term.h>
#endif


#ifdef HAVE_UNISTD_H
#include <unistd.h>
#endif

#ifdef HAVE_STDLIB_H
#include <stdlib.h>
#endif

#ifdef HAVE_ERR_H
#include <err.h>
#endif

#include <sys/types.h>
#include <sys/stat.h>
#include <sys/time.h>

#ifdef HAVE_FCNTL_H
#include <fcntl.h>
#endif

#ifdef HAVE_STRING_H
#include <string.h>
#endif

#ifdef HAVE_SIGNAL_H
#include <signal.h>
#endif

#ifdef HAVE_GETOPT_H
#include <getopt.h>
#endif

#include <ctype.h>
#include <errno.h>
#include <netdb.h>
#include <sys/socket.h>

#ifdef HAVE_NETINET_IN_H
#include <netinet/in.h>
#endif

#include <netinet/tcp.h>

#include <afflib/afflib.h>			
#include <afflib/afflib_i.h>			
#include <afflib/aftimer.h>

#define AIMAGE_CONFIG "AIMAGE_CONFIG"
#define AIMAGE_CONFIG_FILENAME "aimage.cfg"

typedef int64_t int64;
typedef uint64_t uint64;

/* Commands in aimage_os.cpp to handle device specific stuff */
void make_ata_attach_commands(char *cmd_attach,char *cmd_detach,
			      char *dev0,char *dev1,int atadev);
int  scsi_attach(char *fname,int fname_len,class imager *im); // returns 0 if successful & sets fname

/* Global variables for options */
extern const char *opt_title;
extern int opt_blink;
extern int opt_debug;

extern aftimer total_time;

extern int opt_compress;
extern int opt_preview;
extern int opt_pagesize;
extern int opt_compression_level;
extern int opt_compression_alg;
extern int opt_auto_compress;
extern int opt_error_mode;
extern int opt_retry_count;
extern int opt_quiet;			// 1 if no curses gui
extern int opt_batch;			// output status in batch form
extern int opt_silent;
extern int opt_skip;
extern int opt_use_timers;
extern int opt_skip_sectors;
extern int opt_reverse;
extern int opt_beeps;
extern int opt_readsectors;
extern int opt_hexbuf;
extern int opt_use_timers;
extern int64 opt_maxsize;
extern char *command_line;
extern int opt_no_dmesg;
extern int opt_no_ifconfig;
extern int opt_append;
extern int opt_recover_scan;

/* Current imager */
using namespace std;
#include <vector>
class imagers_t:public vector<imager *> 
{
};

extern imagers_t imagers;
extern imager *current_imager;

void segwrite_callback(struct affcallback_info *acbi);
void process_config_questions(AFFILE *af,class imager *);
void sig_intr(int arg);
void sig_cont(int arg);
void bold(const char *str);

#define FD_IDENT 65536
#endif

