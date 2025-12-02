/*
 * Graphical user interface for AIMAGE
 */

/*
 * Copyright (c) 2005,2006
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

#include <inttypes.h>
#include <algorithm>   // for std::find
#include <cstddef>     // for size_t
#include "config.h"
#include "aimage.h"
#include "ident.h"
#include "imager.h"
#include "gui.h"

bool show_help = false;

/* Screen Layout */

const unsigned time_row     = 0;
const unsigned preview_row  = 6;
const unsigned preview_rows = 4;
const unsigned arrow_row    = 10;
const unsigned current_row  = 12;
const unsigned sectors_row  = 13;	// "Sectors Read"
const unsigned blank_sectors_row = 14;
const unsigned done_in_row  = 15;
const unsigned reading_row  = 16;
const unsigned bad_sectors_row = 17;
const unsigned bytes_read_row = 18;
const unsigned bytes_written_row = 19;
const unsigned compression_row = 21;
const unsigned space_row    = 23;
const unsigned phase_row    = 24;

static unsigned old_status_col = 0;
static int old_status_dir      = -10;

#define cols 80				// optimize it for 80

#ifndef CTRL
#define CTRL(x)  (x&037)
#endif

int repaint_screen = 1;

static int batch_first = 0;
#define xstr(s) str(s)
#define str(s) #s


#ifdef HAVE_LIBNCURSES
/* comma_printw():
 * Print a value with commas. If space!=0, allow this many characters
 * and right-justify
 */
void comma_printw(int64 val,unsigned int space)
{
    char buf[64];
    af_commas(buf,val);
    while(strlen(buf) < space && space>0){
	printw(" ");
	space--;
    }
    printw("%s", buf);		// send it to standard output
}



/****************************************************************
 *** Simson's termcap routines.
 ****************************************************************/

void boldw(const char *str)
{
    if(str[0]){
	attr_on(WA_BOLD,0);
	printw("%s",str);
	attr_off(WA_BOLD,0);
    }
    fputs(str,stdout);
}

void boldrc(int row,int col,char *str)
{
    if(str[0]){
	attr_on(WA_BOLD,0);
	mvprintw(row,col,"%s",str);
	attr_off(WA_BOLD,0);
    }
}

void help_window()
{
    WINDOW *help = subwin(stdscr,25,25,1,1);
    box(help,0,0);
    wrefresh(help);
}

void my_keyboard()
{
    switch(getch()){
    case 'h':
	help_window();
	return;
    case ERR:
	return;
    case CTRL('l'):
	return;
    }
}

uint64 total_sectors_all_drives()
{
    uint64 ret = 0;
    for(imagers_t::iterator iter = imagers.begin();iter != imagers.end(); iter++){
	ret += (*iter)->total_sectors;
    }
    return ret;
}

uint64 total_sectors_read_all_drives()
{
    uint64 ret = 0;
    for(imagers_t::iterator iter = imagers.begin();iter != imagers.end(); iter++){
	ret += (*iter)->total_sectors_read;
    }
    return ret;
}

void my_paint_screen(imager *im)
{

    /* Put up the aimage version */
    const char *version = "aimage " xstr(PACKAGE_VERSION);
    mvprintw(space_row,79-strlen(version),"%s",version);

    /* First time through, paint the stuff that doesn't change much */
    
    /* Source Device */
    int ncol = 16;		// where to put the first numeric column
    mvprintw(1, 0, "Source device: ");
    boldrc(1,ncol, im->infile);

    mvprintw(2, 0, "Model #:");
    boldrc(2,ncol, im->device_model);

    if(im->serial_number[0]){
	mvprintw(3,0,"S/N:");
	boldrc(3,ncol,im->serial_number);
    }
    if(im->firmware_revision[0]){
	mvprintw(4,0,"firmware: ");
	boldrc(4,ncol,im->firmware_revision);
    }
	
    /* Output */
    if(im->outfile[0]){
	mvprintw(1,38,"AFF Output: ");
	boldw(im->outfile);
    }

    mvprintw(3,35,"    Disk Size:  ");
    uint64 disk_size = im->sector_size * im->total_sectors;
    if(disk_size>1000000000){
	printw("%d GB",(int)(disk_size/1000000000));
    } else if(disk_size>1000000){
	printw("%d MB",(int)(disk_size/1000000));
    } else if(disk_size>1024){
	printw("%d KB",(int)(disk_size/1024));
    } else printw("?? ");
	
    printw(" (%d byte sectors)  ",im->sector_size);

    mvprintw(4,35,"Total sectors: ");
    if(im->total_sectors){
	comma_printw(im->total_sectors,0);
    }
    else {
	printw("????");
    }


    /* Print the bar graph */
    mvprintw(arrow_row,0,"[");
    for(unsigned int i=1;i<cols-2;i++){
	mvprintw(arrow_row,i," ");
    }
    mvprintw(arrow_row,cols-1,"]");
}

int  column_for_sector(uint64 sector,imager *im)
{
    return (int)(((float)im->last_sector_read /im->total_sectors) * (cols-2)) + 1;    
}

static inline int min(int a,int b) { return a<b ? a : b;}
static inline int max(int a,int b) { return a>b ? a : b;}


/* Static variables to see if we need to refresh again */
int previous_direction = -99;
int64 previous_bytes_read = 0;
int previous_phase = -99;


inline int64 abs64(int64 a){
    if(a<0) return -a;
    return a;
}
#endif


void my_refresh(imager *im,struct affcallback_info *acbi)
{
#ifndef HAVE_LIBNCURSES
    opt_batch=1;
#endif

    double fraction_done = -1;
    if(im->total_sectors>0){		// can we figure this out?
	fraction_done = ((double)im->total_sectors_read
			 / (double)im->total_sectors);
    }


    if(opt_batch){
	if(batch_first){
	    printf("Source device: %s\n",im->infile);
	    printf("Model #: %s\n",im->device_model);
	    printf("S/N: %s\n",im->serial_number);
	    printf("firmware: %s\n",im->firmware_revision);
	    if(im->outfile[0]) printf("AFF output: %s\n",im->outfile);
	    printf("Sector size: %d\n",im->sector_size);
	    printf("Total sectors: %" PRIu64 "\n", im->total_sectors);
	    batch_first = 0;
	}
	printf("Current sector: %" PRIu64 "\n", im->last_sector_read);
	printf("Last sectors read: %d\n",im->last_sectors_read);
	printf("Total sectors read: %" PRIu64 "\n", im->total_sectors_read);
    printf("Total blank sectors: %" PRIu64 "\n", im->total_blank_sectors);
    printf("Total bad sectors: %" PRIu64 "\n", im->bad_sectors_read);
	printf("Consecutive bad regions: %d\n",im->consecutive_read_error_regions);
	printf("Bytes read: %" PRIu64 "\n", im->total_bytes_read);
    printf("Bytes written: %" PRIu64 "\n", im->callback_bytes_written);
	if(acbi) printf("Current phase: %d\n",acbi->phase);
	printf("Free space on capture drive: %qd\n",im->output_ident->freebytes());
	printf("Elapsed Time: %s\n",im->imaging_timer.elapsed_text().c_str());
	if(fraction_done>0) printf("Done in: %s\n",im->imaging_timer.eta_text(fraction_done).c_str());
	printf("\n");
	return;
    }
#ifdef HAVE_LIBNCURSES

    if(repaint_screen){
	my_paint_screen(im);
	repaint_screen = 0;
    }
    //my_keyboard();			// process keyboard commands one day
    
    /* Optimization: Don't update the screen unless either the direction has changed
     * or else more than 128K byte have been read since last time
     */
    int current_phase = acbi ? acbi->phase : 0;

    if((previous_direction == im->last_direction) &&
       (abs64(previous_bytes_read - im->total_bytes_read) < 65536*2) &&
       (previous_phase == current_phase)){
	return;				
    }
    previous_direction  = im->last_direction;
    previous_bytes_read = im->total_bytes_read;
    previous_phase      = current_phase;


    /* Stuff that changes a lot; this needs to be redone
     * to use curses...
     */

    mvprintw(time_row,0,"Elapsed Time: %s",im->imaging_timer.elapsed_text().c_str());

    if(im->imaging){
	attr_on(WA_BOLD,0);
	if(opt_blink) attr_on(WA_BLINK,0);
	mvprintw(0,(cols-strlen(opt_title))/2,"%s",opt_title);
	attr_off(WA_BLINK,0);
	attr_off(WA_BOLD,0);	
    }

    /* Display the time */
    char timebuf[64];
    time_t now = time(0);
    strcpy(timebuf,ctime(&now));
    timebuf[25] = '\000';
    mvprintw(time_row,cols-24,"%s",timebuf);
	
    mvprintw(current_row,0," Currently reading sector: ");
    comma_printw(im->last_sector_read,15);
    printw(" (%3d sector chunks) ",im->last_sectors_read);

    /* Don't print 'Sectors read' unless the hash is not valid 
     * (which indicates that we have had a bad block or a direction reverasl.)
     */
    if(im->hash_invalid){
	mvprintw(sectors_row,0,"             Sectors read: ");
	comma_printw(im->total_sectors_read,15);
    }

    if(fraction_done>0){
	printw(" (%5.2f%% done) ", fraction_done * 100.0);
    }

    mvprintw(blank_sectors_row,0,"            blank sectors: ");
    comma_printw(im->total_blank_sectors,15);
    if(im->total_sectors_read == im->total_blank_sectors){
	printw("  NO DATA YET!");
    }
    clrtoeol();				// clear to end of line

    if(opt_use_timers){
	mvprintw(reading_row,0,"       Time spent reading: %15s  ",
		 im->read_timer.elapsed_text().c_str());
    }
    if(fraction_done>0){
	mvprintw(done_in_row,0,"                  Done in:        %s ",
		 im->imaging_timer.eta_text(fraction_done).c_str());
	if(imagers.size()>1){
	    printw("(this drive)");

	    if(current_imager != imagers.back()){ // don't display for the last imager
		/* Figure out total fraction done */
		double tsr_ad = total_sectors_read_all_drives();
		double ts_ad  = total_sectors_all_drives();
		
		if(ts_ad>0){
		    double total_fraction_done = tsr_ad / ts_ad;
		    if(total_fraction_done>0 && total_fraction_done<100){
			printw("  %s (all drives)",
			       total_time.eta_text(total_fraction_done).c_str());
		    }
		}
	    }
	}
    }
    clrtoeol();
    
    if(im->bad_sectors_read){
	mvprintw(bad_sectors_row,0,"              BAD SECTORS: ");
	comma_printw(im->bad_sectors_read,15);
	if(im->consecutive_read_error_regions){
	    printw(" (%d consecutive bad regions)",
		   im->consecutive_read_error_regions);
	}
    }
    clrtoeol();

    mvprintw(bytes_read_row,0,"               Bytes read: ");
    comma_printw(im->total_bytes_read,15);
    printw("  from ");
    attr_on(WA_BOLD,0);
    printw("%s",im->infile);
    attr_off(WA_BOLD,0);
    
    mvprintw(bytes_written_row,  0,"            Bytes written: ");
    comma_printw(im->callback_bytes_written,15);
    printw("  to   ");
    attr_on(WA_BOLD,0);
    printw("%s",im->outfile);
    attr_off(WA_BOLD,0);

    if(opt_use_timers){
	printw("  (%s)", im->write_timer.elapsed_text().c_str());
    }

    if(im->callback_bytes_to_write>0 && im->callback_bytes_written>0 && acbi && acbi->af){
	if(af_compression_type(acbi->af)==AF_COMPRESSION_ALG_NONE){
	    mvprintw(compression_row,0,"%s", "");
	    clrtoeol();
	}
	else{
	    double fraction = (double)im->callback_bytes_written
		/ (double)im->callback_bytes_to_write;
	    double overall_compression_ratio = 100.0 - fraction*100.0;
	    if(overall_compression_ratio>0.0 && overall_compression_ratio<100.0){
		mvprintw(compression_row,0,"Overall compression ratio:          %6.2f%%  "
			 "(0%% is none; 100%% is perfect)",
			 overall_compression_ratio);
	    }
	}
    }


    attr_on(WA_BOLD,0);
    attr_on(WA_BLINK,0);

    if(current_phase==1) mvprintw(phase_row,30,"===> COMPRESSING");
    if(current_phase==3) mvprintw(phase_row,30,"       WRITING   ===>");

    attr_off(WA_BOLD,0);
    attr_off(WA_BLINK,0);
    clrtoeol();

    /* Update the arrow */
    if(im->total_sectors){
	unsigned new_status_col = column_for_sector(im->last_sector_read,im);
	const char *dir_str = (im->last_direction==1) ? ">" : "<";

	attr_on(WA_REVERSE,0);
	if(old_status_col && old_status_col != new_status_col){
	    /* Need to erase old status */
	    if(old_status_dir == im->last_direction){
		/* We can draw a line */
		for(int i=min(old_status_col,new_status_col);
		        i<=max(old_status_col,new_status_col);
		    i++){
		    mvprintw(arrow_row,i,"="); 
		}
	    }
	    mvprintw(arrow_row,old_status_col,"=");
	}
	old_status_col = new_status_col;
	old_status_dir = im->last_direction;

	mvprintw(arrow_row, new_status_col, "%s", dir_str);
	attr_off(WA_REVERSE,0);
    }

    /* Data preview... */
    if(opt_preview && im->buf){
	for(unsigned int i=0;i<preview_rows;i++){
	    char row[80];
	    for(unsigned j=0;j<79;j++){
		char cc = *(char *)(im->buf+i*80+j);
		if(isprint(cc)) row[j] = cc;
		else row[j] = '.';
	    }
	    row[79] = 0;
	    mvprintw(preview_row+i,0,"%s",row);
	}
    }

    char buf[64];
    mvprintw(space_row,0,"Free space on capture drive: %s MB",
	     af_commas(buf,im->output_ident->freebytes()/((long long)1024*1024)));

    clrtoeol();
	
    /* Are we imaging more than one drive? */
    if (imagers.size() > 1) {
        auto it = std::find(imagers.begin(), imagers.end(), current_imager);
        size_t idx = 0;
        if (it != imagers.end()) {
            idx = static_cast<size_t>(std::distance(imagers.begin(), it));
        }
    
        mvprintw(0, 0, "Drive %zu of %zu:              ",
                 idx + 1,
                 imagers.size());
    }
    refresh();
#endif
}


int gui_active = 0;
void gui_shutdown()
{
    if(opt_quiet) return;
    if(opt_batch){
	printf("aimage: shutdown gui\n");
	batch_first = 0;
	return;
    }
#ifdef HAVE_LIBNCURSES
    if(gui_active){
	endwin();				// turn off curses
	gui_active = 0;
    }
#endif
}

void gui_startup()
{
    if(opt_quiet) return;
    if(opt_batch){
	setvbuf(stdout,0,_IONBF,0);	// unbuffered output
	printf("aimage: startup gui\n");
	batch_first = 1;
	return;
    }
#ifdef HAVE_LIBNCURSES
    initscr();				// Turn on Curses
    nodelay(stdscr,1);			// don't delay stuff to stdscr
    repaint_screen = 1;
    atexit(gui_shutdown);
    gui_active = 1;
#else
    printf("Compiled without libncurses; defaulting to text GUI.\n");
#endif
}

