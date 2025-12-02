#include <afflib/aftimer.h>
#include "hash_t.h"

class imager {
public:
    bool     allow_regular;		// allow the imaging of a regular file

    uint64   total_segments_written;	// number of times callback called
    /* These are set in the imaging loop */

    uint64   total_sectors_read;
    uint64   total_bytes_read;
    uint64   total_bytes_written;
    uint64   total_blank_sectors;

    /* These are set by the callback */
    uint64   callback_bytes_to_write;	
    uint64   callback_bytes_written;

    bool	imaging;
    bool	imaging_failed;

    /* Options */
    bool	opt_logAFF;	// do we want to log aff operations?
    FILE	*logfile;

    /* Timers */
    aftimer	compression_timer;
    aftimer	read_timer;
    aftimer	write_timer;
    aftimer	imaging_timer;

    /* attaching or detaching drives with friendly names */
    char  cmd_attach[255];		// to detach a device that was attached
    char  cmd_detach[255];		// to detach a device that was attached

    /* scsi bus parameters */
    int  scsi_bus;
    int  scsi_tid;
    int  scsi_lun;
    int  scsi_pass;
    
    /* ata bus parameters */
    int  ata_dev;

    /* Information about the drive being imaged */
    char device_model[256];
    char serial_number[256];
    char firmware_revision[256];

    /* Input Device parameters */
    int		in;			// input fd
    uint64	in_pos;			// current position, or -1 if unknown
    int		sector_size;		// in bytes; 0 if unknown
    uint64	total_sectors;	      // in sectors; 0 if uncomputable
    unsigned int maxreadblocks;		// in bytes; that can be read

    AFFILE	*af;				// AF output file
    char	outfile[MAXPATHLEN];
    char	infile[MAXPATHLEN];	// the actual file being imaged
    unsigned char *badflag;		// badflag we are using

    /* For calculating hashes */
    md5_generator  th_md5;
    md5_t		md5;
    sha1_generator th_sha1;
    sha1_t		sha1;
    sha256_generator th_sha256;
    sha256_t		sha256;

    bool	hash_invalid;		// did we reverse direction or skip?
    uint64	last_sector_read ;	// sector number last read
    int		last_sectors_read;

    /* Configuration */
    bool seek_on_output;		// if True, then we need to recalculate md5 & sha
    int  retry_count;

    unsigned char *buf;			// the transfer buffer
    unsigned int bufsize;		// how many bytes in buf
    char  blank_sector[512];		// a lot of zeros
    int   partial_sector_left;		
    bool  partial_sector_blank;

    /* Error handling */
    uint64 bad_sectors_read;
    int    consecutive_read_errors;
    int	   consecutive_read_error_regions;
    int    error_recovery_phase;
    int    last_direction;			// 1 = forwards, -1 = backwards

    /****************************************************************/


    imager();
    void status();		// called each time through; link in your own!

    /* Setup functions */
    int  open_dev(const char *friendly_name);
    int  set_input_fd(int ifd);
    void hash_setup();
    int set_input(const char *name);
    /* Setup the imaging */
    int socket_listen(int port);	// listen on this port for input data;

    /* Imaging data */
    void write_data(unsigned char *buf,uint64 offset,int bytes_read);
    void image_loop(uint64 low_water_mark,
			uint64 high_water_mark,
			int direction, int readsectors,int error_mask);


    void  start_recover_scan();		// do a recover scan
    void  start_imaging2();		// actually run the imaging
    int   start_imaging();		// set up to start the imaging
    void  final_report();	   // generate a human-readable report

    void ident();
    
    class ident *output_ident;

};

extern int opt_multithreaded;


