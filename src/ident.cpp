#include "config.h"
#include "aimage.h"
#include "imager.h"
#include <sys/stat.h>

#ifdef HAVE_SYS_VFS_H
#include <sys/vfs.h>
#endif

#ifdef HAVE_SYS_STATFS_H
#include <sys/statfs.h>
#endif

#include "ident.h"

void ident::init()
{
    filename = 0;
    opened = 0;
    memset(&scsi,0,sizeof(scsi));
    memset(&ata,0,sizeof(ata));
    memset(&params,0,sizeof(params));
}

ident::ident(int fd_)
{
    init();
    fd = fd_;
}

ident::ident(const char *fn)
{
    init();
    filename = strdup(fn);
}

ident::~ident()
{
    if(opened) close(fd);
    if(filename) free(filename);
    if(params.manufacturer) free(params.manufacturer);
    if(params.model) free(params.model);
    if(params.sn) free(params.sn);
    if(params.firmware) free(params.firmware);
    if(params.human) free(params.human);
}


/* is_scsi:
 * If we were given a filename, check to see if this is a scsi device and,
 * if it is, set all of the various values.
 */
char *ident::append(char *base,const char *add)
{
    if(base==0){
	base = (char *)malloc(1);
	base[0] = 0;
	return base;
    }

    base = (char *)realloc(base,strlen(base)+strlen(add)+4);
    strcat(base,add);
    return base;
}


#ifdef linux
#include "ident.h"
#include <stdio.h>
#include <regex.h>
#include <sys/param.h>
#include <sys/mount.h>


/* getvalue
 * Looks for a name in a line in a buf and, if found,
 * make a copy of the text and return a pointer to it.
 */
char *getvalue(const char *name,char *buf)
{
    while(buf[0] && isspace(buf[0])) buf++; // advance buf to end of spaces

    char *pos = strstr(buf,name);
    if(pos==0) return 0;

    /* The string was found */
    char *cc = pos + strlen(name);		// skip past to the end of the string
    while(*cc && isspace(*cc)) cc++;	// scan to end of spaces

    char *ret = strdup(cc);		// got the return string
    char *dd = index(ret,'\n');		// can we find a \n?
    if(dd) *dd = '\000';		// yes; clear it
    return ret;
}


/* getfile:
 * Look for the contents of a file and, if found, read the first line and 
 * return it in a newly-allocated buffer.
 */
#include <dirent.h>
char *getfileline(const char *dirname,const char *filename)
{
    char path[MAXPATHLEN];

    /* Build the pathname we are supposed to get */
    strlcpy(path,dirname,sizeof(path));
    strlcat(path,filename,sizeof(path));
    FILE *f = fopen(path,"r");
    if(f){
	char *buf = (char *)calloc(1,1024);
	if(fgets(buf,1023,f)){
	    char *cc = rindex(buf,'\n');
	    if(cc) *cc = '\000';	// remove trailing \n
	}
	fclose(f);
	if(buf[0]) return buf;		// if we got something, return it
	free(buf);
    }
    return 0;
}


/**
 * Look to see if a string is on the line. If so, update the buffer 
 */
void getresult(const char *buf,const char *str,char **res)
{
    const char *pos = strstr(buf,str);
    if(!pos) return;
    pos += strlen(str);
    *res = strdup(pos);
    char *cc = index(*res,'\n');
    if(cc) *cc = '\000';
}


/**
 * Fill in the parameters
 * Return 0 if success, 0 if failure.
 */
int ident::get_params()
{
    /* Is this a regular file? If so, just return */
    struct stat so;
    if(stat(filename,&so)==0){
	if(S_ISREG(so.st_mode)){
	    errno = ENODEV;
	    return 1;
	}
    }

    /* If udev is installed, just use that */
    if(stat("/sbin/udevadm",&so)==0){
	char capbuf[65536];
	char buf[1024];
	memset(capbuf,0,sizeof(capbuf));
	snprintf(buf,sizeof(buf),"/sbin/udevadm info --query=all --name=%s",filename);
	FILE *f=popen(buf,"r");
	while(!feof(f)){
	    memset(buf,0,sizeof(buf));
	    if(fgets(buf,sizeof(buf),f)){
		getresult(buf,"E: ID_VENDOR=",&params.manufacturer);
		getresult(buf,"E: ID_MODEL=",&params.model);
		getresult(buf,"E: ID_SERIAL_SHORT=",&params.sn );
	    }
	}
	pclose(f);
	params.human = strdup(capbuf);
	return 0;
    }

    /* Check to see if infile is a USB device. If so, print things about it.
     * If the Linux /sys file system is installed, then /sys/bus/scsi/devices/.../block
     * are symlinks to the actual devices.
     * These have the same name as the /dev/<name>, minus the partition.
     */
 
    if(strncmp(filename,"/dev/",5)==0){
	char *cc;
	char sdname[MAXPATHLEN];
	memset(sdname,0,sizeof(sdname));
	strlcpy(sdname,filename+5,sizeof(sdname));
	/* If a partition name was provided, eliminate it */
	for(cc=sdname;*cc;cc++){
	    if(isdigit(*cc)){
		*cc = '\000';
		break;
	    }
	}
	/* Look to see if this is a USB device*/
	DIR *dir = opendir("/sys/bus/scsi/devices/");
	struct dirent *dp;
	while((dp = readdir(dir))!=0){
	    if(dp->d_name[0]=='.') continue; // skip the dot names
	    char dirname[MAXPATHLEN];
	    strlcpy(dirname,"/sys/bus/scsi/devices/",sizeof(dirname));
	    strlcat(dirname,dp->d_name,sizeof(dirname));

	    char devname[MAXPATHLEN];
	    strlcpy(devname,dirname,sizeof(devname));
	    strlcat(devname,"/",sizeof(devname));
	    strlcat(devname,"/block",sizeof(devname));

	    /* If this is a link, then stat it */
	    char path[MAXPATHLEN];
	    memset(path,0,sizeof(path));
	    if(readlink(devname,path,sizeof(path))>0){
		cc = rindex(path,'/');	// find the end of the link
		if(cc && strcmp(cc+1,sdname)==0){
		    /* Found it!
		     * Now, it turns out that dirname is also a symbolic link.
		     * Use it to find the directory where the USB information is stored.
		     */
		    char usbdir[MAXPATHLEN];
		    strlcpy(usbdir,dirname,sizeof(usbdir));
		    strlcat(usbdir,"/../../../../",sizeof(usbdir));
		    params.manufacturer  = getfileline(usbdir,"manufacturer");
		    params.model         = getfileline(usbdir,"product");
		    params.sn            = getfileline(usbdir,"serial");
		    params.firmware      = getfileline(usbdir,"version");
		    return 0;		// we have successfully identified the device
		}
	    }
	}

	/* Fall back to a regular hard drive */
	if(access(filename,R_OK)==0){
	    char capbuf[65536];
	    char buf[256];
	    if(af_hasmeta(filename)) return -1;
	    snprintf(buf,sizeof(buf),"hdparm -I %s",filename);
	    capbuf[0] = 0;
	    FILE *f = popen(buf,"r");
	    while(!feof(f)){
		if(fgets(buf,sizeof(buf),f)){
		    buf[sizeof(buf)-1] = 0;	// make sure it is null-terminated
		    strlcat(capbuf,buf,sizeof(capbuf));	// append to the buffer
		    
		    /* Now check for each of the lines */
		    params.model    = getvalue("Model Number:",buf);
		    params.sn       = getvalue("Serial Number:",buf);
		    params.firmware = getvalue("Firmware Revision:",buf);

		    char *b;
		    if( (b = getvalue("cylinders",buf)) != 0){
			params.cylinders = atoi(b);
			free(b);
		    }
		    if( (b = getvalue("heads",buf)) != 0){
			params.heads = atoi(b);
			free(b);
		    }
		    if( (b = getvalue("sectors/track",buf)) != 0){
			params.sectors_per_track = atoi(b);
			free(b);
		    }
		}
	    }
	    pclose(f);
	    params.human = strdup(capbuf);
	    return 0;
	}
    }
    errno = ENODEV;
    return -1;				// can't figure it out
}


#endif

#ifdef __FreeBSD__
#include "ident.h"
#include <stdio.h>
#include <regex.h>
#include <sys/param.h>
#include <sys/mount.h>


int ident::get_params()
{
    return -1;
}

#endif

#ifdef __APPLE__
#include "ident.h"
#include <stdio.h>
#include <regex.h>
#include <sys/param.h>
#include <sys/mount.h>


int ident::get_params()
{
    return -1;
}


#endif

#include "ident.h"
#include <stdio.h>
#include <regex.h>


#ifndef IDENT_IS_SCSI
bool ident::is_scsi()
{
    return false;
}
#endif

#ifndef IDENT_IS_ATA
bool ident::is_ata()
{
    return false;
}
#endif




const char *pat="([0-9a-f][0-9a-f]:[0-9a-f][0-9a-f]:[0-9a-f][0-9a-f]:"
"[0-9a-f][0-9a-f]:[0-9a-f][0-9a-f]:[0-9a-f][0-9a-f])";

int in_path(const char *cmd)
{
    char *path = strdup(getenv("PATH"));
    char *pathi = path;
    char *check;
    char *last = 0;
    while((check = strtok_r(pathi,":",&last))!=0){
	char buf[MAXPATHLEN+1];
	strlcpy(buf,check,sizeof(buf));
	strlcat(buf,"/",sizeof(buf));
	strlcat(buf,cmd,sizeof(buf));
	if(access(buf,R_OK)==0){
	    free(path);
	    return 1;
	}
	pathi= 0;
    }
    free(path);
    return 0;
}

char *ident::mac_addresses()
{
    char *buf = append(0,0);
    
#ifdef HAVE_POPEN
    if(!in_path("ifconfig")) return buf; // no ifconfig command
    FILE *f = popen("ifconfig","r");
    if(!f) return buf;

    regex_t r;
    if(regcomp(&r,pat,REG_EXTENDED|REG_ICASE)) err(1,"regcomp");

    while(!feof(f)){
	char line[1024];
	regmatch_t pm[2];
	memset(pm,0,sizeof(pm));
	if(fgets(line,sizeof(line),f)){
	    if(regexec(&r,line,2,pm,0)==0){
		char mac[64];
		memset(mac,0,sizeof(mac));
		strncpy(mac,line+pm[1].rm_so,pm[1].rm_eo-pm[1].rm_so);
		buf = append(buf,mac);
		buf = append(buf,"\n");
	    }
	}
    }
    pclose(f);
#endif
    return buf;
}

/* Return the results of dmesg */
char *ident::dmesg()
{
    char *buf = append(0,0);
#ifdef HAVE_POPEN
    FILE *f = popen("dmesg 2>/dev/null","r");
    if(!f) return buf;

    while(!feof(f)){
	char line[1024];
	if(fgets(line,sizeof(line),f)){
	    buf = append(buf,line);
	}
    }
    pclose(f);
#endif
    return buf;

}


#ifdef HAVE_FSTATFS
/* ident-specific things for FreeBSD */
long long ident::freebytes()
{
    if(!opened){
	fd = open(filename,O_RDONLY,0666);	// need file opened for this
	if(fd>0){
	    opened = true;
	}
    }

    if(opened){
	struct statfs sbuf;
	if(fstatfs(fd,&sbuf)==0){
	    return (long long)sbuf.f_bavail * (long long)sbuf.f_bsize;
	}
    }
    return -1;
}
#else
long long ident::freebytes()
{
    return 0;				// eventually, call "df"
}
#endif


void ident::debug(const char *fn)
{
    ident i(fn);
    printf("filename: %s\n",i.filename);
    printf("get_params: %d\n",i.get_params());

    printf("is scsi: %d\n",i.is_scsi());
    if(i.is_scsi()){
	printf("  bus: %d  tid: %d  lun: %d  pass: %d\n",i.scsi.bus,i.scsi.tid,i.scsi.lun,i.scsi.pass);
    }
    printf("is ata: %d\n",i.is_ata());
    if(i.is_ata()){
	printf("  dev: %d  primary: %d  master: %d\n",i.ata.dev,i.ata.primary,i.ata.master);
    }
    if(i.get_params()==0){
	if(i.params.manufacturer) printf("  manufacturer: %s\n",i.params.manufacturer);
	if(i.params.model) printf("  model: %s\n",i.params.model);
	if(i.params.sn) printf("  sn: %s\n",i.params.sn);
	if(i.params.firmware) printf("  firmware: %s\n",i.params.firmware);
	if(i.params.cylinders) printf("  cylinders: %d\n",i.params.cylinders);
	if(i.params.heads) printf("  heads: %d\n",i.params.heads);
	if(i.params.sectors_per_track) printf("  sectors_per_track: %d\n",i.params.sectors_per_track);
    }
}
