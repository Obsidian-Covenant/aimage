#include "config.h"
#include "aimage.h"
#include "ident.h"
#include "imager.h"
#include "gui.h"

#include <stdio.h>
#include <unistd.h>
#include <stdlib.h>
#include <err.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <sys/time.h>
#include <sys/wait.h>
#include <fcntl.h>
#include <string.h>
#include <signal.h>
#include <assert.h>
#include <errno.h>
#include <inttypes.h>

#ifdef HAVE_NETINET_IN_H
#include <netinet/in.h>
#endif


#ifdef HAVE_TERM_H
#include <term.h>
#endif

#ifdef HAVE_NCURSES_TERM_H
#include <ncurses/term.h>
#endif



/*
 * imager.cpp:
 * The C++ imaging library.
 */

int opt_multithreaded=0;

imager::imager()
{
    allow_regular = false;
    total_segments_written = 0;
    total_sectors_read=0;
    total_bytes_read = 0;
    total_bytes_written = 0;
    total_blank_sectors = 0;
    
    callback_bytes_to_write = 0;
    callback_bytes_written = 0;

    imaging = false;
    imaging_failed = false;

    logfile = 0;

    last_sector_read = 0;	// sector number
    bad_sectors_read = 0;
    af = 0;
    hash_invalid = false;		// make true to avoid hash calculation

    memset(cmd_attach,0,sizeof(cmd_attach));
    memset(cmd_detach,0,sizeof(cmd_detach));

    scsi_bus = -1;
    scsi_tid = -1;
    scsi_lun = -1;
    scsi_pass = -1;
    ata_dev = -1;
    
    memset(device_model,0,sizeof(device_model));
    memset(serial_number,0,sizeof(serial_number));
    memset(firmware_revision,0,sizeof(firmware_revision));

    in     = -1;
    in_pos = 0;
    sector_size = 0;
    total_sectors = 0;
    maxreadblocks = 0;

    af = 0;

    memset(outfile,0,sizeof(outfile));
    memset(infile,0,sizeof(infile));

    hash_invalid = false;

    last_sector_read = 0;
    last_sectors_read = 0;

    seek_on_output = false;
    retry_count = 0;

    buf = 0;
    bufsize = 512;			// good guess
    memset(blank_sector,0,sizeof(blank_sector));
    partial_sector_left  = 0;
    partial_sector_blank = false;

    bad_sectors_read = 0;
    consecutive_read_errors = 0;
    consecutive_read_error_regions = 0;
    error_recovery_phase = 0;
    last_direction = 0;

    output_ident = 0;

    /* error recovery */
}


static bool run_cmd(const char *cmd, const char *what)
{
    int ret = system(cmd);
    if (ret == -1) {
        // system() itself failed (couldn’t fork or no /bin/sh)
        perror(what);
        return false;
    }

    if (WIFEXITED(ret)) {
        int status = WEXITSTATUS(ret);
        if (status != 0) {
            fprintf(stderr, "%s: command exited with status %d\n", what, status);
            return false;
        }
    } else if (WIFSIGNALED(ret)) {
        fprintf(stderr, "%s: command terminated by signal %d\n",
                what, WTERMSIG(ret));
        return false;
    }

    return true;
}


void imager::write_data(unsigned char *buf,uint64 offset,int len)
{
    /* if this is supposed to be bad data, make sure that it is properly bad... */
    if(opt_debug==99){
		printf("imager::write_data(buf/x=%p,offset=%" PRIu64 " len=%d buf=%s\n",
    	       buf,
    	       static_cast<uint64_t>(offset),
    	       len,
    	       buf);
		if(offset%sector_size != 0){
		    err(1,"huh? offset mod %d = %d\n",sector_size,(int)offset%sector_size);
		}
    }

    if(!hash_invalid){
		/* Update hash functions. */
		th_md5.update(buf,len);
		th_sha1.update(buf,len);
		th_sha256.update(buf,len);
    }

    /* Count the number of blank sectors.
     */
    /* First, see if there is a partial blank sector that we are still processing... */
    int len_left = len;
    while(len_left>0 && partial_sector_left>0){
	if(buf[len-len_left] != 0){
	    partial_sector_blank = false; // it's no longer blank
	}
	len_left--;
	partial_sector_left--;
    }
    if(partial_sector_left==0){ // we reached the end of the partial sector
	if(partial_sector_blank==true){	// and the sector is blank!
	    total_blank_sectors++;
	}
    }
    /* Is it possible to look for full sectors? */
    while(len_left > sector_size){
	if(memcmp(buf+(len-len_left),blank_sector,sector_size)==0){
	    total_blank_sectors++;
	}
	len_left -= sector_size;
    }

    if(partial_sector_left==0 && len_left>0){ // some left, so we have a new partial sector
	partial_sector_left  = sector_size;
	partial_sector_blank = true;
    }

    /* If anything is left, do the partial sector */
    while(len_left>0){
	if(buf[len-len_left] != 0){
	    partial_sector_blank = false; // no longer blank
	}
	len_left--;
	partial_sector_left--;
    }


    /* Write it out and carry on... */
    if(offset) af_seek(af,offset,SEEK_SET);
    if(af_write(af,buf,len)!=len){
	perror("af_write");	// this is bad
	af_close(af);	// try to gracefully recover
	fprintf(stderr,"\r\n");
	fprintf(stderr,"Imaging terminated because af_write failed.\n\r");
	exit(1);
    }
    total_bytes_written   += len;
}

void imager::status()
{
    if(opt_quiet==0 && opt_silent==0){
	my_refresh(this,0);			// just refresh; most status is done by AFF callback
    }
}

/****************************************************************
 *** isleep(): An informative sleep...                        ***
 ****************************************************************/
void isleep(int s)
{
  printf("isleep %d\n",s);
  for(int i=0;i<s;i++){
    printf("\rSleeping for %d seconds; %d left...",s,s-i);
    fflush(stdout);
    sleep(1);
  }
  printf("\r%50s\r","");
  fflush(stdout);
}



/*
 * open_dev(char outdev[MAXPATHLEN],char *indev)
 * Try to open the friendly device name.
 * If successful, return the actual device in outdev and the FD.
 * 
 * If the device can be detected but not mounted (common with some
 * broken IDE drives), return fd==65536 (FD_IDENT). 
 * This says that we can't open it, but should ident it.
 */


int  imager::open_dev(const char *friendly_name)
{
    /****************************************************************
     *** Check for ata%d or ide%d
     ****************************************************************/

    ata_dev = -1;
    sscanf(friendly_name,"ata%d",&ata_dev);	// try to find ata0
    if(ata_dev==-1){
	sscanf(friendly_name,"ide%d",&ata_dev);	// try to find ide0
    }

    if(ata_dev != -1){			// if we found the device
	/* Create the attach and detach commands */

	char dev0[64];			// space for the first channel
	char dev1[64];			// space for the second channel
	char *dev[2] = {dev0,dev1};

	make_ata_attach_commands(cmd_attach,cmd_detach,dev0,dev1,ata_dev);
	/* Try to detach first; if it fails, warn but continue. */
	if (!run_cmd(cmd_detach, "cmd_detach (initial)")) {
	    fprintf(stderr,
	            "Warning: initial detach command failed; continuing anyway.\n");
	}

	int i;
	for(i=0;i<10;i++){
	    int delay = i*3;
	    printf("\nOpening special ATA Bus #%d...\n",ata_dev);
	    if(i>0){
		printf("Attempt %d out of %d.\n",i+1,10);
	    }
	    printf("# %s\n",cmd_attach);
        if (!run_cmd(cmd_attach, "cmd_attach")) {
            /* Attach failed on this attempt; log and retry after a delay. */
            fprintf(stderr,
                    "Error: attach command failed on attempt %d of %d; retrying...\n",
                    i+1, 10);
            if (delay) {
                isleep(delay);
            }
            continue;
        }
	    
	    /* See if we found the device */
	    for(int d=0;d<2;d++){
		if(access(dev[d],F_OK)==0){
		    if(access(dev[d],R_OK)){
			// don't have permission to read it.
			// this is bad
		      err(1,"%s",dev[d]);
		    }
		    if(delay){
			printf("Waiting %d second%s for %s to spin up...\n",
			       delay,delay==1?"":"s",infile);  
			isleep(delay);
		    }	
		    strcpy(infile,dev[d]); // we will try this one
		    int fd = open(infile,O_RDONLY);
		    if(fd>0){
			/* The device was successfully opened. */
			return fd;		// got it!
		    }
		    perror(infile);	       
		}
	    }
	    printf("Detaching device and trying again...\n");
	    printf("# %s\n",cmd_detach);
        if (!run_cmd(cmd_detach, "cmd_detach (retry)")) {
            /* Detach failure is non-fatal; log and still sleep/retry. */
            fprintf(stderr,
                    "Warning: detach command failed during retry; continuing.\n");
        }
	    isleep(delay);
	}
	/* Been through too many times. Did we get a device?
	 * If so, just ident it...
	 */
	if(infile[0]){
	    imaging_failed = true;
	    return FD_IDENT;
	}
    }

    /****************************************************************
     *** Check for scsi%d
     *** In our testing with FreeBSD, there is no advantage to repeatedly
     *** attempting to attach or detach...
     ****************************************************************/
    if(sscanf(friendly_name,"scsi%d",&scsi_bus)==1){
	if(scsi_attach(infile,sizeof(infile),this)==0){
	    int fd = open(infile,O_RDONLY);
	    if(fd>0){
		return fd;
	    }
	    /* attach was successful but open failed. */
	    imaging_failed = true;
	    return FD_IDENT;
	}
    }
    return -1;
}





/*
 * main image loop.
 * if high_water_mark==0, then we do not know how many blocks the input
 * is; just read it byte-by-byte...
 */
void imager::image_loop(uint64 low_water_mark, // sector # to start
			uint64 high_water_mark, // sector # to end
			int direction, int readsectors,int error_mask)
{
    // buffer to store the data we read
    bufsize = readsectors*sector_size;
    buf = (unsigned char *)malloc(bufsize); 
    memset(buf,0,sizeof(buf));
    uint64 data_offset = 0;		// offset into output file
    bool valid_reverse_data = false;		// did we ever get valid data in the reverse direction?
    bool last_read_short = false;
    int reminder = 0;

    if(!buf) err(1,"malloc");

    /* Get the badflag that we'll be using */
    badflag = (unsigned char *)malloc(sector_size);
    if(af) memcpy(badflag,af_badflag(af),sector_size);
    else memset(badflag,0,sector_size);

    /* Loop as long as we have room, or until we get an EOF
     * (if high_water_mark is 0.)
     */
    imaging = true;
    while(low_water_mark < high_water_mark || high_water_mark==0){ 

	/* Figure out where to read and how how many sectors to read */
	uint64 snum;	// where we will be reading
	unsigned int sectors_to_read = readsectors;
	if(sectors_to_read > maxreadblocks && maxreadblocks>0){
	    sectors_to_read = maxreadblocks;
	}
	if(direction==1){	// going up
	    snum = low_water_mark;

	    /* If a high water mark is set, take it into account */
	    if(high_water_mark>0){
		unsigned int sectors_left = high_water_mark - snum;
		if(sectors_left < sectors_to_read){ 
		    sectors_to_read = sectors_left;
		}
	    }
	}
	else {
	    assert(high_water_mark != 0); // we can't go backwards if we don't know end
	    snum = high_water_mark - sectors_to_read;
	    if(snum<low_water_mark){
		snum = low_water_mark;
		sectors_to_read = high_water_mark - low_water_mark;
	    }
	}

	last_sector_read = snum;
	last_sectors_read = sectors_to_read;
	last_direction = direction;

	if (high_water_mark != 0){ 	// if we know where the top is...
	    data_offset = sector_size * snum; // where we want to start reading

	    if(data_offset != in_pos){	// eliminate unnecessary seeks
		lseek(in,data_offset,SEEK_SET);	// make sure we are at the right place; (ignore error)
		in_pos = data_offset;
	    }
	}

	status();			// tell the user what we are doing

	int bytes_to_read = sectors_to_read * sector_size;

	/* Fill the buffer that we are going to read with the bad flag */
	for(int i=0;i<bytes_to_read;i+=sector_size){
	    memcpy(buf+i,badflag,sector_size);
	}

	/* Now seek and read */

	int bytes_read    = 0;
	if(opt_debug==99){
	    bytes_read = -1; // simulate a read error
	} else {
	    if(opt_use_timers) read_timer.start();
	    bytes_read = read(in,buf,bytes_to_read);
	    if(opt_use_timers) read_timer.stop();
	}
	if(bytes_read>=0){
	    in_pos += bytes_read;	// update position
	}

	/* Note if we got valid data in the reverse direction */
	if((direction == -1) && (bytes_read>0)) valid_reverse_data = true;


	if(bytes_read == bytes_to_read){
	    /* Got a good read! */
	    total_sectors_read    += sectors_to_read;
	    total_bytes_read      += bytes_read;

	    /* Reset the error counters */
	    consecutive_read_errors = 0;
	    consecutive_read_error_regions = 0;
	    last_read_short = false;

	    /* Write the data! */
	    write_data(buf,data_offset,bytes_read);

	    if(direction==1){
		low_water_mark += sectors_to_read;
	    }
	    else {
		high_water_mark -= sectors_to_read;
	    }
	    continue;
	}

	/* Some kind of error... */

	/* If high water mark is 0,
	 * then just write out what we read and continue, because we don't know how many
	 * bytes we can read...
	 */
	if(high_water_mark==0 && bytes_read<=0){
	    break;	// end of pipe/file/whatever
	}

	/* If we are reading forward and we got an incomplete read, just live with it... */
	if(direction==1 && bytes_read>0){
	    total_bytes_read      += bytes_read; 
	    write_data(buf,data_offset,bytes_read);
	    data_offset += bytes_read;	// move along
	    low_water_mark += (bytes_read + reminder)/sector_size;
	    reminder = (bytes_read + reminder)%sector_size;
	    last_read_short = true;
	    continue;
	}

	/* Error handling follows. This code will automatically retry
	 * the same set of sectors retry_count and then switch direction.
	 */
	if(error_mask==0){
	    /* If errors on this attempted read exceed the threshold,
	     * just note how many bytes we were able to read and swap directions if necessary.
	     * If we have done that too many times in a row, then give up...
	     */
	    if(++consecutive_read_errors>retry_count){
		consecutive_read_errors=0; // reset the counter

		/* If we got an error, note it --- unless one of two conditions are true:
		 * we are going forwards and the last was a short read.
		 * we are going backwards and we have never gotten valid data going backwards.
		 */
		if(((direction==1) && (last_read_short==false)) ||
		   ((direction==-1) && (valid_reverse_data==true))){
		    write_data(buf,data_offset,bytes_to_read);
		    bad_sectors_read += sectors_to_read; // I'm giving up on them...
		    hash_invalid = true;
		}
		
		if(++consecutive_read_error_regions<retry_count){ //
		    /* Just skip to the next area */
		    int sectors_to_bump = readsectors / 2;
		    if(sectors_to_bump==0) sectors_to_bump = 1;	// need to bump by a positive amount
		    
		    /* Is there room left? */
		    if(low_water_mark + sectors_to_bump > high_water_mark){
			break;		// no more room.
		    }
		    
		    if(direction == 1){
			low_water_mark += sectors_to_bump;	// give a little bump
		    }
		    else {
			high_water_mark -= sectors_to_bump;
		    }
		}
		else {
		    /* Retry count in this directory exceeded. Either reverse
		     * direction or give up...
		     */
		    if(direction ==  1){
			consecutive_read_errors = 0; // reset count
			consecutive_read_error_regions = 0;
			direction = -1;
			continue;
		    }
		    if(direction == -1){
			/* That's it. Give up */
			break;
		    }
		    errx(1,"imager: Unknown direction: %d\n",direction);
		}
	    }
	}
	if(error_mask==1){
	    /* Stop reading at the first error and write the incomplete buffer */
	    if(bytes_read>0){
		write_data(buf,data_offset,bytes_read);
	    }
	    break;
	}
    }
    imaging = false;
    free(buf); buf = 0;				// no longer valid
    free(badflag); badflag = 0;
}

/* Returns 0 if okay, -1 if failure. */
int imager::set_input_fd(int ifd)
{
    in = ifd;

    /* Make sure infile is actually a device, and not a file */
    struct stat so;
    if(fstat(ifd,&so)){
	perror("fstat");
	return -1;
    }
    int mode = so.st_mode & S_IFMT;
    struct af_figure_media_buf afb;
    memset(&afb,0,sizeof(afb));

    /* Now figure out how many input blocks we have */
    if(mode==S_IFBLK || mode==S_IFCHR){
	if (af_figure_media(in,&afb)){
	    return -1;
	}
	sector_size = afb.sector_size;
	total_sectors = afb.total_sectors;
	maxreadblocks = afb.max_read_blocks;
	return 0;
    }
    if(mode==S_IFREG){			// regular file
	if(allow_regular==false){
	    fprintf(stderr,"input is a regular file.\n");
	    fprintf(stderr,"Use afconvert or aimage -E to convert regular files to AFF.\n");
	    return -1;
	}
	/* Just got with the file size... */
	sector_size  = 512;		// default
	total_sectors= so.st_size / sector_size;
	maxreadblocks = 0;
	return 0;
    }

    /* Okay. We don't know how big it will be, so just get what we can... */
    sector_size   = 512;		// it's a good guess
    total_sectors = 0;			// we don't know
    maxreadblocks = 0;			// no limit
    return 0;
}


int imager::set_input(const char *name)
{
    /* Set the input given a "name"
     * 
     * First, try to open the input file.
     * If the name specified by the user can be opened, use it.
     * If the name cannot be opened work, see if it is a operating system
     * specific filename such as "ide0" or "ata0", in which the
     * operating system-specific code will attempt to attach the
     * device.
     */
    /* Check for '-' which is stdin */
    if(strcmp(name,"-")==0){
	strcpy(infile,name);		// make a local copy
	return set_input_fd(0);			// file descriptor 0 is stdin
    }

    /* Check for 'listen:%d' which means listen for a TCP connection */
    int port;
    if(sscanf(name,"listen:%d",&port)==1){
	if(socket_listen(port)) return -1;	// sets infile
	sector_size = 512;		// no rationale for picking anything else
	return 0;
    }

    /* The name must be a file. See if we can open it... */
    int ifd = open(name,O_RDONLY);
    if(ifd>0){
	strcpy(infile,name);		// make a local copy
	return set_input_fd(ifd);
    }

    /* Attempt to open infile failed; check for a special
     * device name...
     */
    ifd = open_dev(name);
    if(ifd>0){
	return set_input_fd(ifd);
    }

    /* If we haven't been able to open the something by this point, give up. */
    perror(name);
    return -1;
}



void imager::hash_setup()
{
    /* Set up the MD5 & SHA1 machinery */
    OpenSSL_add_all_digests();
}


/* start_recover_scan():
 * Do a recover scan...
 * Try to read all of the pages that are not in the image
 */
void imager::start_recover_scan()
{
    if(total_sectors==0){
	err(1,"total_sectors not set. Cannot proceed with recover_scan");
    }
    if(af->image_sectorsize==0){
	err(1,"af->image_sectorsize not set. Cannot proceed with recover_scan");
    }
    if(af->image_pagesize==0){
	err(1,"af->image_pagesize not set. Cannot proceed with recover_scan");
    }

    int64 sectors_per_page = af->image_pagesize / af->image_sectorsize;
    int64 num_pages = (total_sectors+sectors_per_page-1) / sectors_per_page;
    int *pages = (int *)calloc(sizeof(int *),num_pages);
    printf("There are %" PRId64 " pages...\n", num_pages);
    /* Now figure out which pages we have.
     */
    for(int64 i=0;i<num_pages;i++){
	char segname[AF_MAX_NAME_LEN];
	snprintf(segname,sizeof(segname),AF_PAGE,i);
	if(af_get_seg(af,segname,0,0,0)){ // just probe for the segment's existence
	    printf("Page %" PRId64 " is in the image...\r", i);
	    pages[i]=1; // note that we have this page
	}
    }
    /* Print the missing pages: */
    int missing_pages = 0;
    printf("Missing pages:\n");
    for(int64 i=0;i<num_pages;i++){
	if(pages[i]==0){
	    printf("%" PRId64 " ", i);
	    missing_pages++;
	}
    }
    printf("\n");
    printf("Total missing pages: %d\n",missing_pages);
    /* Now randomly try to get each of the missing pages */
#ifdef HAVE_SRANDOMDEV
    srandomdev();
#endif
    while(missing_pages>0){
	int random_page = random() % num_pages;
	while(pages[random_page]!=0) random_page = (++random_page) % num_pages;
	printf("*** try for page %d\n",random_page);
	uint64 start_sector = random_page * sectors_per_page;
	uint64 end_sector   = start_sector + sectors_per_page;
	image_loop(start_sector,end_sector,1,opt_readsectors,1);
	pages[random_page] = 1;		// did that page
	missing_pages--;
    }
    free(pages);
    exit(0);
}


/* start_imaging2():
 * Actually run the imaging
 */
void imager::start_imaging2()
{
    retry_count = opt_retry_count;

    /* See if the skipping makes sense */
    if(!opt_skip_sectors){
	if(opt_skip % sector_size != 0){
	    fprintf(stderr,
		    "Skipping must be an integral multiple of sector size "
		    "(%d bytes)\n",sector_size);
	    imaging_failed = true;
	    return;
	}
	opt_skip /= sector_size;	// get the actuall offset
    }

    int starting_direction = 1;
    if(opt_reverse) starting_direction = -1;

    /****************************************************************
     *** Start imaging
     ****************************************************************/

    signal(SIGINT,sig_intr);	// set the signal handler
    hash_setup();		// get ready...
    image_loop(opt_skip,
	       total_sectors,starting_direction,
	       opt_readsectors,opt_error_mode); // start the process
    signal(SIGINT,0);		// unset the handler


    /****************************************************************
     *** Finished imaging
     ****************************************************************/

    /* Calculate the final MD5 and SHA1 */
    md5 = th_md5.final();
    sha1 = th_sha1.final();
    sha256 = sha256.final();
}




/* Start the imaging.
 * If files are specified, opens them.
 * then does the imaging.
 * Then closes the files.
 */
int imager::start_imaging()
{
    output_ident = new class ident(outfile);

    /* If the user is imaging to an AFF file,
     * open it and try to ident the drive.
     * Drive ident is not done if writing to a raw file, because
     * there is no place to store the ident information. This will be changed
     * when we can write an XML log.
     */
    af->tag = (void *)this;			// remember me!

    /* If the segment size hasn't been set, then set it */

    /** Flag happens between here */

    if(opt_append){
	/* Make sure that the AFF file is for this drive, and set it up */
    }
    else {

	ident();			// ident the drive if possible
	af_update_seg(af,AF_ACQUISITION_COMMAND_LINE,0,(const u_char *)command_line,strlen(command_line));
	af_update_seg(af,AF_ACQUISITION_DEVICE,0,(const u_char *)infile,strlen(infile));

	af_set_sectorsize(af,sector_size);
	af_set_pagesize(af,opt_pagesize);	// sets current page size
	if(opt_maxsize){
	    if(af_set_maxsize(af,opt_maxsize)){
		exit(-1);
	    }
	}

	if(total_sectors>0){
	    af_update_segq(af,AF_DEVICE_SECTORS,(int64)total_sectors);
	}
	if(opt_no_ifconfig==0){
	    char *macs = ident::mac_addresses();
	    if(macs){
		af_update_seg(af,AF_ACQUISITION_MACADDR,0,(const u_char *)macs,strlen(macs));
		free(macs);
	    }
	}

	if(opt_no_dmesg==0){
	    char *dmesg = ident::dmesg();
	    if(dmesg && strlen(dmesg)){
		af_update_seg(af,AF_ACQUISITION_DMESG,0,(const u_char *)dmesg,strlen(dmesg));
		free(dmesg);
	    }
	}
	af_make_gid(af);
    }
    af_set_callback(af,segwrite_callback);
    af_set_acquisition_date(af,time(0));

    /* Here is where the imaging takes place.
     * Do it unless ifd==FD_IDENT, which is the fictitious FD.
     */

    if(logfile){
	fprintf(logfile,"aimage infile=%s ",infile);
	fprintf(logfile,"outfile_aff=%s ",outfile);
	fprintf(logfile,"\n");
    }
    if(in!=FD_IDENT){
	imaging_timer.start();
	if(opt_recover_scan){
	    start_recover_scan();
	}
	else{
	    start_imaging2();
	}
	imaging_timer.stop();
    }


    /* AFF Cleanup... */
    if(af){
	if(hash_invalid==false){
	    if(af_update_seg(af,AF_MD5,0,md5.final(),md5.SIZE)){
		if(errno!=ENOTSUP) perror("Could not update AF_MD5");
	    }
	    if(af_update_seg(af,AF_SHA1,0,sha1.final(),sha1.SIZE)){
		if(errno!=ENOTSUP) perror("Could not update AF_SHA1");
	    }
	    if(af_update_seg(af,AF_SHA256,0,sha256.final(),sha256.SIZE)){
		if(errno!=ENOTSUP) perror("Could not update AF_SHA1");
	    }
	}
	else {
	    af_del_seg(af,AF_MD5);	// because it is not valid
	    af_del_seg(af,AF_SHA1);
	}
	if(af_update_segq(af,AF_BADSECTORS, (int64)bad_sectors_read)){
	    if(errno!=ENOTSUP) perror("Could not update AF_BADSECTORS");
	}
	if(af_update_segq(af,AF_BLANKSECTORS, (int64)total_blank_sectors)){
	    if(errno!=ENOTSUP) perror("Could not update AF_BLANKSECTORS");
	}
	unsigned long elapsed_seconds = (unsigned long)imaging_timer.elapsed_seconds();
	if(af_update_seg(af,AF_ACQUISITION_SECONDS,elapsed_seconds,0,0)){
	    if(errno!=ENOTSUP) perror("Could not update AF_ACQUISITION_SECONDS");
	}
    }
    return 0;
}



/* Listen for a local socket connection and return the
 * file descriptor...
 */
int imager::socket_listen(int port)
{
    struct sockaddr_in local;
    struct sockaddr_in remote;
    socklen_t rsize = sizeof(remote);

    int sock = socket(AF_INET,SOCK_STREAM,IPPROTO_IP);    /* Open a listening socket ... */
    memset(&local,0,sizeof(local));
    memset(&remote,0,sizeof(remote));
#ifdef HAVE_SOCKADDR_SIN_LEN
    local.sin_len = sizeof(sockaddr_in);
#endif
    local.sin_family = AF_INET;
    local.sin_port   = htons(port);	// listen on requested port.
    if(bind(sock,(sockaddr *)&local,sizeof(local))) err(1,"bind");
    if(listen(sock,0)) err(1,"listen");		// listen, and only accept one
    printf("Listening for connection on port %d...\n",port);
    in = accept(sock,(sockaddr *)&remote,&rsize);
    if(in<0){
	perror("accept");
	in = 0;
	return -1;
    }
    strcpy(infile,inet_ntoa(remote.sin_addr));
    printf("Connection accepted from %s\n",infile);
    return 0;
}



/* final_report():
 * Let's make the user feel good...
 */
void imager::final_report()
{
    bold("****************************** IMAGING REPORT ******************************");
    putchar('\n');
    printf("Input: "); bold(infile); putchar('\n');
    if(device_model[0]){
	printf("  Model: ");
	bold(device_model);
    }
    if(serial_number[0]){
	printf("  S/N: ");
	bold(serial_number);
    }
    putchar('\n');
    printf("  Output file: ");
    bold(outfile);
    putchar('\n');
    
    char buf[64];

    printf("  Bytes read: %s\n", af_commas(buf,total_bytes_read));
    printf("  Bytes written: %s\n", af_commas(buf,callback_bytes_written));

    char print_buf[256];
    printf("\n");
    if(hash_invalid==false){
	printf("raw image md5:  %s\n",
	       af_hexbuf(print_buf,sizeof(print_buf),md5.final(),md5.SIZE,opt_hexbuf));
	printf("raw image sha1: %s\n",
	       af_hexbuf(print_buf,sizeof(print_buf),sha1.final(),sha1.SIZE,opt_hexbuf));
	printf("raw image sha256: %s\n",
	       af_hexbuf(print_buf,sizeof(print_buf),sha256.final(),sha256.SIZE,opt_hexbuf));
    }

    if(imaging_failed){
	printf("\nTHIS DRIVE COULD NOT BE IMAGED DUE TO A HARDWARE FAILURE.\n");
    }
}

