/*
 * ident.h:
 * lots of OS-specific information for getting the serial numbers
 * of drives, calculating free space, and stuff like that.
 */

#ifndef __IDENT_H__
#define __IDENT_H__

class ident {
private:
    static char *append(char *base,const char *add); // append add to base and return the new base
    void init();
public:;
    ident(int fd);			// ident an open file descriptor
    ident(const char *fn);		// ident a filename or file system
    ~ident();
    
    int fd;
    char *filename;			// filename
    bool opened;			// need to close?

    bool is_scsi();			// return true if scsi
    // scsi variables get set if this is a scsi device
    struct {
	int  bus;
	int  tid;
	int  lun;
	int  pass;			// freebsd specific thing
    } scsi;

    bool is_ata();
    struct {
	int  dev;
	int  primary;			// 0 = primary; 1 = secondary
	int  master;			// 0 = master; 1 = slave
    } ata;

    /* General information about the device */
    int get_params();			// try to figure out device params; returns 0 if success
    struct {
	char *manufacturer;		// manufacturer
	char *model;		// model number
	char *sn;			// serial number
	char *firmware;		// firmware revision
	int cylinders;
	int heads;
	int sectors_per_track;
	char *human;		// big human readable chunk
    } params;

    long long freebytes();	// amount of free bytes on device

    /* General functions about the machine */
    static char *mac_addresses();		
    // returns a buffer, which must be freed, of a null-terminated
    // list of ethernet mac addresses in the current computers.

    static char *dmesg();
    // returns a buffer, which must be freed, of a null-terminated
    // string that has the output of the "dmesg" command.

    static void debug(const char *fn);	// print debug information for fn 

};

#endif

