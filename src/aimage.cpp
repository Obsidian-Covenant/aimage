/*
 * aimage.cpp:
 *
 * Image a physical drive connected through an IDE, SCSI, USB or
 * Firewire interface and write the results to either an AFF file.
 *
 * Automatically calculate the MD5 & SHA-1 while image is made.
 * Handle read errors in an intelligent fashion. 
 * Display the results. 
 */

/*
 * Copyright (c) 2005, 2006
 *	Simson L. Garfinkel and Basis Technology Corp.
 *      All rights reserved.
 *
 * This code is derrived from software contributed by
 * Simson L. Garfinkel
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer.
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in the
 *    documentation and/or other materials provided with the distribution.
 * 3. All advertising materials mentioning features or use of this software
 *    must display the following acknowledgement:
 *    This product includes software developed by Simson L. Garfinkel
 *    and Basis Technology Corp.
 * 4. Neither the name of Simson Garfinkel, Basis Technology, or other
 *    contributors to this program may be used to endorse or promote
 *    products derived from this software without specific prior written
 *    permission.
 *
 * THIS SOFTWARE IS PROVIDED BY SIMSON GARFINKEL, BASIS TECHNOLOGY,
 * AND CONTRIBUTORS ``AS IS'' AND ANY EXPRESS OR IMPLIED WARRANTIES,
 * INCLUDING, BUT NOT LIMITED TO, THE IMPLIED WARRANTIES OF
 * MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE
 * DISCLAIMED.  IN NO EVENT SHALL SIMSON GARFINKEL, BAIS TECHNOLOGy,
 * OR CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL,
 * SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT
 * LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF
 * USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND
 * ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY,
 * OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT
 * OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF
 * SUCH DAMAGE.  
 */

#include "config.h"
#include "aimage.h"
#include "ident.h"
#include "imager.h"
#include "gui.h"
#include <afflib/utils.h>		// get seglist
#include <inttypes.h>

#define xstr(s) str(s)
#define str(s) #s

/* global variables for imaging calculations */
/* For autocompression */
double ac_compress_write_time = 0;
double ac_nocompress_write_time = 0;

const char *progname = "aimage";

int opt_zap = 0;
int opt_append = 0;
int opt_ident = 0;

/* Autocompression features */
int   opt_compression_level = AF_COMPRESSION_DEFAULT;// default compression level
int   opt_compression_alg   = AF_COMPRESSION_ALG_ZLIB;	// default algorithm
int   opt_auto_compress = 0;
int   opt_make_config   = 0;
aftimer total_time;			// total time spend imaging


/* size options */
int  default_pagesize = 16*1024*1024;
int  opt_pagesize    = default_pagesize;	// default seg size --- 16MB
int  opt_readsectors = opt_pagesize / 512;      // read in 256K chunks
int64  opt_maxsize = (1<<31) - opt_pagesize;	
int  maxsize_set = 0;


/* General options */
const char *opt_title = "IMAGING";
const char *opt_sign_key_file = 0;		// public and private key file
const char *opt_sign_cert_file = 0;		// public and private key file
char *command_line = 0;			// what is typed
int  opt_quiet = 0;
int  opt_silent = 0;
int  opt_beeps  = 1;			// beep when done
int  opt_recover_scan = 0;
int  opt_error_mode = 0;
int  opt_retry_count = 5;
int  opt_reverse = 0;
int  opt_fast_quit = 0;
int  opt_blink = 1;
int  opt_hexbuf = AF_HEXBUF_SPACE4 | AF_HEXBUF_UPPERCASE;
int  opt_verify = 0;
int  opt_wipe = 0;

char *opt_logfile_fname = 0;
char logfile_fname[MAXPATHLEN];
FILE *logfile=0;

const char *config_filename=AIMAGE_CONFIG_FILENAME;

int opt_skip = 0;			// what to skip on input
int opt_skip_sectors = 0;		// are we skipping sectors?
int opt_compress = 1;
int opt_preview = 1;			// give me a little show
int opt_debug = 0;
int opt_batch = 0;			// output status in XML
int opt_use_timers = 0;			// report time spent compressing, reading, writing, etc.
int opt_no_ifconfig = 0;
int opt_no_dmesg = 0;
const char *opt_exec = 0;

/* Imagers */

vector<string> opt_setseg;		// segs that get set
imagers_t imagers;
imager *current_imager = 0;


void bold(const char *str)
{
#ifdef HAVE_TPUTS
    tputs(enter_bold_mode,1,putchar);
#endif
    fputs(str,stdout);
#ifdef HAVE_TPUTS
    tputs(exit_attribute_mode,0,putchar);
#endif
}



void usage()
{
    putchar('\n');
    printf("aimage %s\n\n",xstr(PACKAGE_VERSION));
    printf("usage: %s [options] INPUT1 [OUTFILE1] [INPUT2 OUTPUT2] ...--- image indev to outfile\n",
	   progname);
    printf("INPUT may be any of these:\n");
    printf("  A device (e.g. /dev/disk1)\n");
    printf("  - (or /dev/stdin, for standard input)\n");
    printf("  listen:nnnn       Listen on TCP port nnnn\n");

    printf("OUTFILE may be:\n");
    printf("  outfile.aff --- image to the AFF file outfile\n");

    bold("\nGeneral Options:\n");
    printf("  --quiet, -q           -- No interactive statistics.\n");
    printf("  --batch, -Y           -- Batch output\n");
    printf("  --silent, -Q          -- No output at all except for errors.\n");
    printf("  --readsectors=nn, -R nnnn,   -- set number of sectors to read at once (default %d)\n",
	   opt_readsectors);
    printf("  --version, -v         -- Just print the version number and exit.\n");
    printf("  --skip=nn[s], -k nn   -- Skip nn bytes [or nns for sectors] in input file\n");
    printf("  --no_beeps, -B        -- Don't beep when imaging is finished.\n");
    printf("  --logfile=fn, -l fn   -- Where to write a log. By default none is written\n");
    printf("  --logAFF, -G          -- Log all AFF operations\n");
    printf("  --preview, -p         -- view some of the data as it goes by.\n");
    printf("  --no_preview, -P      -- do not show the preview.\n");
    printf("  --verify, -b        -- verify the input against the output file\n");
    printf("  --wipe,     -w        -- verify after images and, if valid, wipe\n");
    printf("  --exec '',  -C''      -- run the command after imaging (before wiping) with %%s as image name\n");

    bold("\nExisting File Options:\n");
    printf("  --append, -a          -- Append to existing file\n");
    printf("   NOTE: --append is not yet implemented.\n");
    printf("  --zap, -z             -- Erase outfile(s) before writing\n");

    bold("\nAFF Options:\n");
    printf("  --outfile=fname, -ofname  -- write an AFF file.\n");
    printf("  --image_pagesize=nnn, -S nnnn\n");
    printf("                        -- set the AFF page size (default %d)\n", opt_pagesize);
    printf("                           (number can be suffixed with b, k, m or g)\n");
    printf("                           Also sets  maxsize to be 2^32 - image_pagesize if not otherwise set.\n"); 
    printf("  --make_config, -m     -- Make the config file if it doesn't exist\n");
    printf("                           Config file is %s by default\n",AIMAGE_CONFIG_FILENAME);
    printf("                           and can be overridden by the %s enviroment variable\n", AIMAGE_CONFIG);
    printf("  --no_dmesg, -D        -- Do not put dmesg into the AFF file\n");
    printf("  --no_ifconfig, -I     -- Do not put ifconfig output into AFF file\n");
    if(getenv(AIMAGE_CONFIG)){
	printf("                           (Currently set to %s)\n",getenv(AIMAGE_CONFIG));
    }
    else{
	printf("                           (Currently unset)\n");
    }

    printf("  --no_compress, -x     -- Do not compress. Useful on slow machines.\n");
    printf("  --compression=n, -Xn  -- Set the compression level\n");
    printf("  --lzma_compress, -L   -- Use LZMA compression (slow but better)\n");
    printf("  --auto_compress, -A   -- write as fast as possible, with compression if it helps.\n");
    printf("                           sets compression level 1\n");
    printf("  --maxsize=n, -Mn      -- sets the maximum size of output file to be n..\n");
    printf("                           Default units are megabytes; \n");
    printf("                           suffix with 'g', 'm', 'k' or 'b'\n");
    printf("                           use 'cd' for a 650MB CD.\n");
    printf("                           use 'bigcd' for a 700MB CD.\n");
    printf("                           use 'dvd' for a DVD.\n");
    printf("                           use 'dvddl' for a DVD-DL.\n");
    printf("  --setseg name=value, -g name=value\n");
    printf("                        -- Create segment 'name' and give it 'value'\n");
    printf("                           This option may be repeated.\n");
    printf("  --no_hash, -H         -- Do not calculate MD5, SHA1 and SHA256 of image.\n");
    printf("  --multithreaded, -2   -- Calculate hashes in another thread\n");


    bold("\nError Recovery Options:\n");
    printf("  --error_mode=0, -e0  -- Standard error recovery:\n");
    printf("                     Read disk 256K at a time until there are 5 errors in a row.\n");
    printf("                     Then go to the end of the disk and read backwards\n");
    printf("                     until there are 5 erros in a row. Then stop.\n");
    printf("  --error=1  -e1  -- Stop reading at first error.\n");
    printf("  --retry=nn -tnn -- change retry count from 5 to nn\n");
    printf("  --reverse, -V   -- Scan in reverse to the beginning.\n");
    printf("  --recover-scan, -c   -- Starting with an AFF file that has been partially \n");
    printf("                     acquired, try to read each page, 8 sectors at a time.\n");
    printf("                     (implies --append)\n");

    bold("\nOther:\n");
    printf("  --help, -h      -- Print this message.\n");
    printf("  --fast_quit, -Z -- Make ^c just exit immediately.\n");
    printf("  --allow_regular,   -E -- allow the imaging of a regular file\n");
    printf("  --title=s, -T s  -- change title to s (from IMAGING) and disable blink\n");
    printf("  --debug=n, -d n  -- set debug code n (-d0 for list)\n");
    printf("  --use_timers, -y -- Use timers for compressing, reading & writing times\n");
    printf("  --ident, -i      -- Just print the ident information and exit (for testing)\n");

    bold("\nExamples:\n");
    printf("Create image.aff from /dev/sd0:\n");
    printf("     aimage /dev/sd0 image.aff\n");
    printf("     aimage -o image.aff /dev/sd0 \n");
    printf("\n");
    printf("Create image0.aff from /dev/sd0 and image1 from /dev/sd1:\n");
    printf("     aimage /dev/sd0 image0.aff /dev/sd1 /image1.aff\n");
    printf("\n");
    printf("Default values:\n");
    printf("   config file: %s (need not be present)\n",AIMAGE_CONFIG_FILENAME);
    printf("   page size: %d\n",default_pagesize);
    printf("   default compression: %d\n",AF_COMPRESSION_DEFAULT);
    printf("   default compression algorithm: ZLIB\n");
    exit(0);
}

int debug_list()
{
    puts("-d99 - Make all reads in imager fail. For testing error routines.");
    puts("-d2  - Print memcpy\n");
    exit(0);
}


static struct option longopts[] = {
    { "outfile",       required_argument,  NULL, 'o'},
    { "quiet",         no_argument,        NULL, 'q'},
    { "silent",        no_argument,        NULL, 'Q'},
    { "readsectors",   required_argument,  NULL, 'R'},
    { "image_pagesize",required_argument,  NULL, 'S'},
    { "zap",           no_argument,        NULL, 'z'},
    { "help",          no_argument,        NULL, 'h'},
    { "retry",         required_argument,  NULL, 't'},
    { "no_compress",   no_argument,        NULL, 'x'},
    { "compression",   required_argument,  NULL, 'X'},
    { "lzma_compress", no_argument,        NULL, 'L'},
    { "auto_compress", no_argument,        NULL, 'A'},
    { "no_beeps",      no_argument,        NULL, 'B'},
    { "append",        no_argument,        NULL, 'a'},
    { "make_config",   no_argument,        NULL, 'm'},
    { "logfile",       required_argument,  NULL, 'l'},
    { "logAFF",        no_argument,	   NULL, 'G'},
    { "reverse",       no_argument,        NULL, 'V'},
    { "fast_quit",     no_argument,        NULL, 'Z'},
    { "allow_regular", no_argument,        NULL, 'E'},
    { "title",         required_argument,  NULL, 'T'},
    { "preview",       no_argument,        NULL, 'p'},
    { "no_preview",    no_argument,        NULL, 'P'},
    { "maxsize",       required_argument,  NULL, 'M'},
    { "debug",         required_argument,  NULL, 'd'},
    { "error_mode",    required_argument,  NULL, 'e'},
    { "setseg",        required_argument,  NULL, 'g'},
    { "no_hash",       no_argument,        NULL, 'H'},
    { "use_timers",    no_argument,        NULL, 'y'},
    { "version",       no_argument,        NULL, 'v'},
    { "no_dmesg",      no_argument,        NULL, 'D'},
    { "no_ifconfig",   no_argument,        NULL, 'I'},
    { "batch",         no_argument,        NULL, 'Y'},
    { "skip",          required_argument,  NULL, 'k'},
    { "recover-scan",  no_argument,        NULL, 'c'},
    { "key-file",      required_argument,  NULL, 's'},
    { "verify",        no_argument,        NULL, 'b'},
    { "wipe",          no_argument,        NULL, 'w'},
    { "exec",          required_argument,  NULL, 'C'},
    { "ident",         no_argument,        NULL, 'i'},
    { "multithreaded", no_argument,        NULL, '2'},
    {0,0,0,0}
};


/****************************************************************
 *** Callbacks used for status display
 ****************************************************************/

/*
 * segwrite_callback:
 * called by AFF before and after each segment is written.
 */


void segwrite_callback(struct affcallback_info *acbi)
{
    imager *im = (imager *)acbi->af->tag;
    switch(acbi->phase){

    case 1:
	/* Start of compression */
	if(opt_use_timers) im->compression_timer.start();
	break;

    case 2:
	/* End of compression */
	if(opt_use_timers) im->compression_timer.stop();
	break;

    case 3:
	/* Start of writing */
	if(opt_use_timers) im->write_timer.start();
	break;

    case 4:
	/* End of writing */
	if(opt_use_timers) im->write_timer.stop();

	/* log if necessary */
	if(logfile){
	    fprintf(logfile,
		    "   pagenum=%" I64d " bytes_to_write=%d bytes_written=%d lap_time=%f\n",
		    acbi->pagenum,
		    acbi->bytes_to_write,
		    acbi->bytes_written,
		    im->write_timer.lap_time());
	}

	im->callback_bytes_to_write += acbi->bytes_to_write;
	im->callback_bytes_written  += acbi->bytes_written;
	im->total_segments_written  ++;

	if(opt_auto_compress){
	    /* Handle automatic compression.
	     */
	    if(im->total_segments_written==1){
		/* First segment was written.
		 * Tabulate time and turn off compression.
		 */
		ac_compress_write_time += im->write_timer.lap_time();
		af_enable_compression(acbi->af,AF_COMPRESSION_ALG_NONE,opt_compression_level);
	    }
	    if(im->total_segments_written==2){
		/* Second segment was written.
		 * Tabulate times and process.
		 */
		ac_nocompress_write_time += im->write_timer.lap_time();

		/* Figure out which was faster */
		if(ac_compress_write_time < ac_nocompress_write_time){
		    /* Turn on compression */
		    af_enable_compression(acbi->af, opt_compression_alg, opt_compression_level);
		} else {
		    /* Turn off compression */
		    af_enable_compression(acbi->af, AF_COMPRESSION_ALG_NONE, opt_compression_level);
		}
	    }
	}
    }

    /* Refresh if necessary */
    if(opt_quiet==0 && opt_silent==0){
	my_refresh(im,acbi);
    }
}


void gotorc(int row,int col)
{
#ifdef HAVE_TPUTS
    char buf[256];
    char *cc = buf;
    tputs(tgoto(tgetstr("cm",&cc),col,row),0,putchar);
#endif
}

/* sig_intr:
 * Close the AFF file if possible
 * If this is the second time we were called, just exit...
 */

int depth = 0;
void sig_intr(int arg)
{
#ifdef HAVE_TGETNUM
    int rows = tgetnum("li");
#endif

    gui_shutdown();
    depth++;
    if(depth>1){
	printf("\r\n\nInterrupted interrupt. Quitting.\n\r");
	fflush(stdout);
	exit(1);
    }
#ifdef HAVE_GOTORC
    gotorc(rows-1,0);
#endif
    printf("\n\n\n\rInterrupt!\n\r");
    printf("\n\n\n");
    fflush(stdout);
    if(opt_fast_quit){
	printf("*** FAST QUIT ***\n\r");
	exit(1);
    }
    if(current_imager && current_imager->af){
	/* We had the AF open when the ^c came; shut things down nicely */

	AFFILE *af = current_imager->af;
	printf("Closing output AFF file...\n\r");
	fflush(stdout);
	af_set_callback(af,0);
	af_enable_compression(af, 0, 0);
	if(af_close(af)){
	    warnx("Can't close file '%s'\n",af_filename(af));
	}
    }
    fflush(stdout);
    exit(1);
}

char lastchar(const char *str)
{
    return str[strlen(str)-1];
}


/* Special logic for opening a FreeBSD device... */
void Sleep(int msec)
{
    usleep(msec*1000);
}



void make_config(const char *fname)
{
    if(access(fname,F_OK)==0){
	return;				// don't make the file if it exists
	exit(1);
    }
    FILE *f = fopen(fname,"w");
    if(!f) err(1,"make_config(%s) ",fname);
    fputs("#\n",f);
    fputs("#\n",f);
    fputs("#\n",f);
    fputs("# Sample config file for aimage...\n",f);
    fputs("# Two commands:\n",f);
    fputs("#   ask <segname> <question>        --- asks the user a question\n",f);
    //fputs("#   set <option>                    --- sets the option\n",f);
    fputs("#\n",f);
    fputs("# examples:\n",f);
    fputs("# ask " AF_ACQUISITION_TECHNICIAN " Your Name:\n",f);
    fputs("# ask " AF_CASE_NUM " Case Number:\n",f);
    fputs("# ask " AF_ACQUISITION_ISO_COUNTRY " Acquisition ISO Country code:\n",f);
    fputs("# ask " AF_ACQUISITION_NOTES " acquisition notes:\n",f);
    //fputs("# set --no_compress\n",f);
    //fputs("# set --raw\n",f);
    fputs("#\n",f);
    //fputs("# Remember, config file is processed *before* command-line options.\n",f);
    //fputs("# Feel free to add your own!\n",f);
    fclose(f);
    printf("%s created\n",fname);
    exit(0);
}


void process_config_questions(AFFILE *af,class imager *)
{
    FILE *f = fopen(config_filename,"r");	// open the config file to find questions to ask
    if(!f) return;			// get back to the user
    while(!feof(f)){
	char buf[1024];			// a righteous buffer for questions and answers
	const char *sep = " \t";
	char *last;
	memset(buf,0,sizeof(buf));
	if(fgets(buf,sizeof(buf)-1,f)==NULL) break;

	char *cc = index(buf,'\n');
	if(cc) *cc = '\000';		// remove the \n

	char *cmd = strtok_r(buf,sep,&last);

	if(!cmd || strcmp(cmd,"ask")!=0) continue; // not an ask command
	char *segname = strtok_r(NULL,sep,&last);
	if(!segname){
	    fprintf(stderr,"error in config file. No segnament name in: %s\n",buf);
	    continue;
	}

#ifdef HAVE_LIBREADLINE
	/* Ask the question and get the response */
	char *val = readline(last);
#else
	char buf2[1024];
	memset(buf2,0,sizeof(buf2));
	char *val = fgets(buf2,sizeof(buf2)-1,stdin);
#endif

	/* And write the response into the segment */
	af_update_seg(af,segname,0,(const u_char *)val,strlen(val));
#ifdef HAVE_LIBREADLINE
	free(val);
#endif
    }
    fclose(f);
}




char *append(char *base,const char *str)
{
    base = (char *)realloc(base,strlen(base)+strlen(str)+1);
    strcat(base,str);
    return base;
}

/* Return 1 if the PID is running, 0 if it is not
 * in an operating-system independent kind of way.
 */
int checkpid(int pid)
{
    char buf[1024];
    int  found = 0;
    snprintf(buf,sizeof(buf),"ps %d",pid);
    FILE *f = popen(buf,"r");
    while(!feof(f)){
	char *cc = buf;
	if(fgets(buf,sizeof(buf),f)==NULL) break;
	buf[sizeof(buf)-1] = 0;
	if(atoi(cc)==pid) found = 1;
    }
    pclose(f);
    return found;
}

/* getlock(char *infile):
 * See if another copy of aimage is imaging this infile...
 * A lock file is created in /tmp that has the PID of the aimage process.
 * Return 0 if okay, -1 if not okay
 */

int getlock(class imager *im)
{
    /* If the file exists and the PID in the file is running,
     * can't get the lock.
     */
    char lockfile[MAXPATHLEN];
    snprintf(lockfile,sizeof(lockfile),"/tmp/aimge.%s.lock",im->infile);
    if(access(lockfile,F_OK)==0){
	/* Lockfile exists. Get it's pid */
	char buf[1024];
	FILE *f = fopen(lockfile,"r");
	if(!f){
	    perror(lockfile);		// can't read lockfile...
	    return -1;
	}
	if(fgets(buf,sizeof(buf),f)!=NULL){
	  buf[sizeof(buf)-1] = 0;
	  int pid = atoi(buf);
	  if(checkpid(pid)==0){
	    /* PID is not running; we can delete the lockfile */
	    if(unlink(lockfile)){
	      err(1,"could not delete lockfile %s: ",lockfile);
	    }
	  }
	  /* PID is running; generate error */
	  errx(1,"%s is locked by process %d\n",im->infile,pid);
	}
    }
    FILE *f = fopen(lockfile,"w");
    if(!f){
	err(1,"%s",lockfile);
    }
    fprintf(f,"%d\n",getpid());		// save our PID.
    fclose(f);
    return 0;
}

void open_logfile(const char *ifn)
{
    /* Open the logfile, interperting ~ if necessary. */
    if(ifn[0]=='~' && ifn[1]=='/'){
	strlcpy(logfile_fname,getenv("HOME"),sizeof(logfile_fname));
	strlcat(logfile_fname,ifn+1,sizeof(logfile_fname));
    }
    else {
	strlcpy(logfile_fname,ifn,sizeof(logfile_fname));
    }
    logfile = fopen(logfile_fname,"a");
}

int64 scaled_atoi(const char *arg)
{
    int64 ret=0;
    int multiplier=1;
    char ch,junk;
    switch(sscanf(arg,"%" I64d "%c%c",&ret,&ch,&junk)){
    case 1:
	return ret;			// no multiplier
    case 2:
	switch(ch){
	case 'g':
	case 'G':
	    multiplier=1024*1024*1024;break;
	case 'm':
	case 'M':
	    multiplier=1024*1024; break;
	case 'k':
	case 'K':
	    multiplier=1024; break;
	case 'b':
	case 'B':
	    multiplier=1;break;
	case '1':case '2':case '3':
	case '4':case '5':case '6':
	case '7':case '8':case '9':case '0':
	    break;				// no multiplier provided
	default:
	    errx(1,"Specify multiplier units of g, m, k or b\n");
	}
	break;
    default:
	errx(1,"Could not decode '%s'",arg);
    }
    return ret * multiplier;
}



/*
 * process each option character.
 * We do this so that options in the config file can be processed
 * before options passed in on the command line.
 */
void process_option(class imager *im,char ch,char *optarg)
{
    switch (ch) {
    case 'a': opt_append ++;	break;
    case 'b': opt_verify++;   break;
    case 'B': opt_beeps = 0;	break;
    case 'd': opt_debug = atoi(optarg); if(opt_debug==0) debug_list(); break;
    case 'D': opt_no_dmesg=1;	break;
    case 'C': opt_exec = optarg; break;
    case 'e': opt_error_mode = atoi(optarg); break;
    case 'H': im->hash_invalid = 1;    break; // don't calculate the hash
    case 'I': opt_no_ifconfig=1;break;
    case 'o': strcpy(im->outfile,optarg);	break;
    case 'q': opt_quiet++;	break; 
    case 'Q': opt_silent++;	break;
    case 'R': opt_readsectors = atoi(optarg); break;
    case 'z': opt_zap ++;	break;
    case 'c': opt_recover_scan++; opt_append++; break;
    case 'w': opt_verify++;opt_wipe++;	break;
    case 'k':
	opt_skip  = atoi(optarg);
	if(lastchar(optarg)=='s'){
	    opt_skip_sectors = 1;
	}
	break;

    case 'S':
	opt_pagesize = scaled_atoi(optarg);
	break;
    case 'x':
	opt_compress = 0;
	opt_compression_level = AF_COMPRESSION_ALG_NONE;
	opt_compression_alg  = AF_COMPRESSION_ALG_NONE;
	break;
    case 'A':
	opt_auto_compress = 1;
	opt_compress      = 1;		//
	opt_compression_alg  = AF_COMPRESSION_ALG_ZLIB;
	opt_compression_level = 2;		// why not?
	break;

    case 'v':
	printf("aimage %s\n\n",PACKAGE_VERSION);
	exit(0);
    case 'X': opt_compression_level = atoi(optarg); break;
    case 't': opt_retry_count = atoi(optarg); break;
    case 'm': opt_make_config = 1;	break;
    case 'V': opt_reverse = 1;		break;
    case 'Y': opt_batch = 1;		break;
    case 'Z': opt_fast_quit = 1;	break;
    case 'E': im->allow_regular=1;	break;
    case 'T': opt_title = optarg; opt_blink = 0; break;
    case 'p': opt_preview = 1;		break;
    case 'P': opt_preview = 0;		break;
    case 'l': opt_logfile_fname=optarg; break;
    case 'L': opt_compression_alg = AF_COMPRESSION_ALG_LZMA; break;
    case 'G': im->opt_logAFF = true;	break; 
    case 'i': 	opt_ident = 1;break;
    case 'M':
	maxsize_set = 1;
	if(strcasecmp(optarg,"cd")==0){
	    opt_maxsize = 650000000;
	    break;
	}
	if(strcasecmp(optarg,"bigcd")==0){
	    opt_maxsize = 700000000;
	    break;
	}
	if(strcasecmp(optarg,"dvd")==0){
	    opt_maxsize = 4505600000LL;
	    break;
	}
	if(strcasecmp(optarg,"dvddl")==0){
	    opt_maxsize = 8600000000LL;
	    break;
	}
	opt_maxsize = scaled_atoi(optarg);
	break;

    case 'y': opt_use_timers = 1;break;
    case 'g': opt_setseg.push_back(optarg);break;
    case 's':
	opt_sign_key_file = optarg;
	opt_sign_cert_file = optarg;
	break;
    case '2': opt_multithreaded = 1;break;

    case 'h':
    case '?':
    default:
	usage();
    case 0:
	break;
    }
}


void beeps(int count)
{
#ifdef HAVE_BEEP
    if(opt_beeps && !opt_quiet && !opt_silent){
	while(count-->0){
	    beep();
	    fflush(stdout);
	    Sleep(300);
	}
    }
#endif
}

void process_config_file_options()
{
    FILE *f = fopen(config_filename,"r");
    if(f){
	while(!feof(f)){
	    char buf[1024];
	    memset(buf,0,sizeof(buf));
	    if(fgets(buf,sizeof(buf)-1,f)){
		if(strncmp(buf,"set ",4)==0){
		}
	    }
	}
    }
}


/** 
 * verify the device. If it matches, then wipe
 * @param file1 first file to read. Usually the original.
 * @param file2 second file to read.
 * @
 * @returns 0 = successful validation, <0 if error
 */
int verify_file(const char *file1,const char *file2)
{
    setvbuf(stdout,0,_IONBF,0);
    printf("Validating %s with %s\r\n",file1,file2);

    AFFILE *af1 = af_open(file1,O_RDONLY,0777);
    AFFILE *af2 = af_open(file2,O_RDONLY,0777);
    unsigned char *buf1 = (unsigned char *)malloc(AFF_DEFAULT_PAGESIZE);
    unsigned char *buf2 = (unsigned char *)malloc(AFF_DEFAULT_PAGESIZE);
    if(!buf1 || !buf2) err(1,"malloc");
    int64_t loc = 0;

    /* We really want to do this page-by-page, rather than byte-by-byte, so that we can catch
     * error recovered files...
     */

    while(!af_eof(af1)){
	if(af_seek(af1,loc,SEEK_SET)<0) err(1,"af_seek(%s)",af_filename(af1));
	if(af_seek(af2,loc,SEEK_SET)<0) err(1,"af_seek(%s)",af_filename(af2));
	int bytes1 = af_read(af1,buf1,AFF_DEFAULT_PAGESIZE);
	if(bytes1==0) break;		// nothing more to read
	int bytes2 = af_read(af2,buf2,bytes1);
	printf("Verifying %d bytes at %" PRId64 " ...", bytes1, loc);
	if(bytes1!=bytes2){
	    fprintf(stderr,"Read different amounts of bytes from file1 (%d) and file2 (%d)",bytes1,bytes2);
	    free(buf1);
	    free(buf2);
	    return -1;
	}
	if(memcmp(buf1,buf2,bytes1)!=0){
	    fprintf(stderr,"Read different values from file1 and file2\r\n");
	    fprintf(stderr,"bytes1=%d  bytes2=%d\r\n",bytes1,bytes2);
	    free(buf1);
	    free(buf2);
	    return -2;
	}
	loc += bytes1;
	printf("\r\n");
    }
    af_close(af1);
    af_close(af2);
    free(buf1);
    free(buf2);
    printf("%s verifys\r\n",file2);
    return 0;
}

int wipe(const char *file1)
{
    AFFILE *af1 = af_open(file1,O_RDWR,0777);
    unsigned char *zbuf = (unsigned char *)calloc(AFF_DEFAULT_PAGESIZE,1);
    while (!af_eof(af1)) {
        printf("Wiping at %" PRIu64 "\r\n", static_cast<uint64_t>(af_tell(af1)));
        if (af_write(af1, zbuf, AFF_DEFAULT_PAGESIZE) <= 0) break;
    }
    af_close(af1);
    free(zbuf);
    return 0;
}

int format(const char *file1)
{
    char buf[1024];
    snprintf(buf,sizeof(buf),"newfs_msdos %s",file1);
    puts(buf);
    return system(buf);
}

string dirname(string str)
{
    if(str.rfind('/')<0) return ".";	// no path; must be current directory
    return str.substr(0,str.rfind('/'));
}

string filename(string str)
{
    if(str.rfind('/')<0) return str;	// no path; must be current directory
    return str.substr(str.rfind('/')+1);
}

int main(int argc,char **argv)
{
#ifdef HAVE_SETUPTERM
    setupterm((char *)0,1,(int *)0);	// set up termcap; it's needed for usage()
#endif

    /* If the AIMAGE_CONFIG variable is set, use it */
    if(getenv(AIMAGE_CONFIG)){
	config_filename = getenv(AIMAGE_CONFIG);
    }

    /* Figure out the command line that was used to run this program.
     * This information will be recorded in the AFF file.
     */
    command_line = (char *)malloc(1);
    command_line[0] = 0;
    for(int i=0;i<argc;i++){
	if(i>0) command_line = append(command_line," ");
	command_line = append(command_line,argv[i]);
    }

    /* Build the options string that will be used for getopt_long */
    char optstring[256];
    optstring[0] = 0;
    char *cc = optstring;
    for(int i=0;longopts[i].name;i++){
	switch(longopts[i].has_arg){
	case no_argument:
	    *cc++ = longopts[i].val;
	    break;
	case required_argument:
	case optional_argument:
	    *cc++ = longopts[i].val;
	    *cc++ =':';
	    break;
	default:
	    printf("unknown value for longopts[%d].has_arg=%d\n",
		   i,longopts[i].has_arg);
	    exit(9);
	}
    }
    *cc++ = 0;


    /* Create the imager; we'll need this for option processing */
    imagers.push_back(new imager());


    /* Process the config file if it exists */
    process_config_file_options();

    /* Process the options */
    int ch;
    while ((ch = getopt_long_only(argc, argv, optstring,longopts,NULL))!= -1) {
	process_option(imagers[0],ch,optarg);
    }
    argc -= optind;
    argv += optind;

    if(default_pagesize != opt_pagesize && !maxsize_set){
	opt_maxsize = (1<<31) - opt_pagesize; // recalculate maxsize
    }

    if(opt_make_config) make_config(config_filename);    /* Make the config file if necessary */

    /* Open the logfile */
    if(opt_logfile_fname) open_logfile(opt_logfile_fname);

    /* Make sure at least one input file was provided */
    if(argc<1){
	usage();
	exit(0);
    }

    /* Now, for each argument, construct input and output files
     */

    if(opt_ident){
	ident::debug(*argv);
	exit(0);
    }


    while(*argv){
	imager *im = imagers.back();
	im->logfile = logfile;
	if(im->set_input(*argv)){ // opens dev and idents if necessary
	    fprintf(stderr,"Cannot continue.");
	    exit(1);
	}
	argv++;
	argc--;

	/* If there is an additional argument, it must be the output file
	 * If an output file is set, that's bad.
	 */
	if(argc>0){
	    process_option(im,'o',*argv);
	    argv++;
	    argc--;
	}

	/* If no output file has been set, indicate an error */
	if(im->outfile[0]==0) errx(1,"No output filename specified.");

	/* If there are arguments left, create another imager... */
	if(*argv){
	    imagers.push_back(new imager());
	}
    }

    total_time.start();

    /* Now, image with each imager with curses... */
    for(imagers_t::iterator iter = imagers.begin();iter != imagers.end(); iter++){
	imager *im = (*iter);

	if(opt_zap)unlink(im->outfile);

	/* If either file exists and we are not appending, give an error */
	if(!opt_append){
	    if(access(im->outfile,F_OK)==0) errx(1,"%s: file exists",im->outfile);

	    /* If an AFM file is being created and the .000 file exists,
	     * generate an error
	     */
	    if(af_ext_is(im->outfile,"afm")){
		char file000[MAXPATHLEN+1];
		strlcpy(file000,im->outfile,sizeof(file000));
		char *cc = rindex(file000,'.');
		if(!cc) err(1,"Cannot file '.' in %s\n",file000);
		for(int i=0;i<2;i++){
		    char buf[16];
		    snprintf(buf,sizeof(buf),".%03d",i);
		    *cc = '\000';	// truncate
		    strlcat(file000,buf,sizeof(file000)); // and concatenate
		    if(access(file000,F_OK)==0){
			fprintf(stderr,"%s: file exists. Delete it before converting.\n",
				file000);
			fprintf(stderr,"NOTE: -z option will not delete %s\n",
				file000);
			return -1;
		    }
		}
	    }

	}

	char buf[256];
	memset(buf,0,sizeof(buf));
	    
	int fstype = af_identify_file_type(im->outfile,1);
	if(fstype!=AF_IDENTIFY_AFF &&
	   fstype!=AF_IDENTIFY_AFD &&
	   fstype!=AF_IDENTIFY_AFM &&
	   fstype!=AF_IDENTIFY_NOEXIST){
	    fprintf(stderr,"%s exists and is not an AFF, AFD or AFM file.\n",im->outfile);
	    fprintf(stderr,"Delete it or move it first.\n");
	    exit(-1);
	}

	/* If filename contains a %d, then scan the directory for all matching files
	 * and get the next filename...
	 */
	printf("im->outfile=%s\n",im->outfile);
	if(strstr(im->outfile,"%")){
	    int max = 0;
	    char fmt[1024];
	    string odirname = dirname(im->outfile);
	    string ofilename = filename(im->outfile);
	    DIR *d = opendir(odirname.c_str());
	    struct dirent *dp;
	    if(d) while((dp = readdir(d))!=0){
		int i;
		if(sscanf(dp->d_name,ofilename.c_str(),&i)==1){
		    if (i>max) max=i;
		}
	    }
	    closedir(d);

	    snprintf(fmt,sizeof(fmt),im->outfile,max+1);
	    strcpy(im->outfile,fmt);
	}

	/* If there is no '.', then we need to remind the user to specify a file type */
	char *pos = im->outfile;
	char *slash = strrchr(im->outfile,'/');
	if(slash) pos = slash+1;
	if(strchr(pos,'.')==0) errx(1,"%s: no extension specified (did you forget the .aff?)",im->outfile);

	im->af = af_open(im->outfile,O_CREAT|O_RDWR,0666);

	if(!im->af) af_err(1,"af_open %s: ",im->outfile);

	/* Set up the AFF */
	af_enable_compression(im->af,opt_compression_alg,opt_compression_level);
	process_config_questions(im->af,im);     // process the config file for questions
	if(opt_sign_key_file){
	    if(af_set_sign_files(im->af,opt_sign_key_file,opt_sign_cert_file)){
	      errx(1,"%s",opt_sign_key_file);
	    }
	    if(af_sign_all_unsigned_segments(im->af)){
		errx(1,"signing unsigned segments");
	    }
	}

	for(vector<string>::iterator i = opt_setseg.begin(); i!= opt_setseg.end(); i++){
	    string::size_type eq = i->find('=');
	    if(eq>0){
		string name  = i->substr(0,eq);
		string value = i->substr(eq+1);
		af_update_seg(im->af,name.c_str(),0,
			      (const u_char *)value.c_str(),value.length());
	    }
	}

	beeps(1);			// one beep to start
	gui_startup();
	im->start_imaging();	// run aimage
	gui_shutdown();

	/* AFF cleanup */
	//make_parity(im->af);
	if (opt_debug == 2) {
	    fprintf(stderr, "af->bytes_memcpy=%" PRIu64 "\n",
	            static_cast<uint64_t>(im->af->bytes_memcpy));
	}
	if(af_close(im->af)){
	    warnx("af_close failed. This shouldn't happen.\n");
	    fprintf(stderr,"Run 'ainfo -v %s' to see if %s is corrupt.\n",
		    im->outfile,im->outfile);
	}
	im->af = 0;


	/* Now verify the file */
	if(im->infile[0] && opt_verify){
	    if(verify_file(im->infile,im->outfile)){
		for(int i=0;i<20;i++){
		    printf("\r\n");
		}
		err(1,"verify failed\n");
	    }
	    if(opt_exec){
		int bufsize = strlen(opt_exec)+1024;
		char *buf = (char *)malloc(bufsize);
		if(!buf) err(1,"malloc");
		snprintf(buf,bufsize,opt_exec,im->outfile);
		puts(buf);
		int ret = system(buf);
		if(ret){
		    errx(1,"%s returned %d",buf,ret);
		}
	    }
	    if(opt_wipe) {
		wipe(im->infile);
		format(im->infile);
	    }
	}
	beeps(2);			// two beeps when this is done
    }
    total_time.stop();

    beeps(2);	 // two more beeps for everything done

#ifdef HAVE_GOTORC
    gotorc(tgetnum("li")-1,0);		// go to the bottom of the screen
    tputs(clr_eos,0,putchar);
#endif
#ifdef HAVE_LIBNCURSES
    endwin(); 
#endif

    /* find out how much free space is left on the drive */
    long long freebytes = imagers[0]->output_ident->freebytes();

    /* Generate all of the final reports and detach any drives as necessary */
    for(imagers_t::iterator iter=imagers.begin();iter!=imagers.end();iter++){
	imager *im = (*iter);
	im->final_report();
	if(opt_verify) printf("*** IMAGE WAS VERIFIED ***\n");
	if(im->cmd_detach[0]){
	    printf("Detaching drive...\n%s\n",im->cmd_detach);
	    int ret = system(im->cmd_detach);
	    if(ret){
		printf("  >> failed; return=%d\n",ret);
	    }
	}
	if(opt_wipe) printf("*** MEDIA WAS WIPED AND FORMATTED FAT32 ***\n");
    }

    /* say how much free space is left on the drive */
    char buf[64];
    printf("Free space remaining on capture drive: ");
    printf(" %s MB\n",af_commas(buf,freebytes/(1024*1024)));
    if(logfile) fclose(logfile);
    return 0;
}
