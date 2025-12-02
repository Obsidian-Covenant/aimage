#include "config.h"
#include "aimage.h"
#include "imager.h"


/* ident_update_seg:
 * If af!=NULL, then update segname to contain the string str.
 * Otherwise just print it (for debugging)
 */
void ident_update_seg(AFFILE *af,const char *segname,const char *str,int is_number)
{
    if(af){
	if(is_number){
	    af_update_seg(af,segname,atoi(str),0,0);
	}
	else{
	    int len = strlen(str);
	    af_update_seg(af,segname,0,(const u_char *)str,len);
	}
    }
    else{
	printf("%s: %s\n",segname,str);
    }
}


/* checkline:
 * Looks for a name in a line in a buf and, if found,
 * put a copy of the next into the
 * AFFILE and optionally make a copy of the text..
 */
void checkline(const char *name,const char *segname, const char *buf,char *copy,AFFILE *af,int is_number)
{
    while(buf[0] && isspace(buf[0])) buf++; // advance buf to end of spaces

    const char *pos = strstr(buf,name);
    if(pos==0) return;

    /* The string was found */
    const char *cc = pos + strlen(name); // skip past to the end of the string
    while(*cc && isspace(*cc)) cc++;	// scan to end of spaces
    char *tmp = strdup(cc);

    /* Terminate tmp at EOL if there is one first */
    char *dd = index(tmp,'\n');		// can we find a \n?
    if(dd) *dd = '\000';		// yes; clear it

    ident_update_seg(af,segname,tmp,is_number);
    if(copy) strcpy(copy,tmp);		// make a copy
    free(tmp);
}




/* FreeBSD-specific ident routines */
#ifdef __FreeBSD__
#include <sys/param.h>
#include <sys/mount.h>

int freebsd_get_ata_params(const char *infile,int *controller,int *channel)
{
  int adnum;
  if(sscanf(infile,"/dev/ad%d",&adnum)==1){
      if(controller) *controller = adnum / 2;
      if(channel)    *channel    = adnum % 2;
      return 0;
  }
  return(-1);
}

/* fix_freebsd_sn:
 * FreeBSD 6.0 release returns ^_ as the serial number of some USB devices.
 * If that is the case here, scan the output of sysctl -a to find the
 * device and put in the S/N. This can fail if there is more than
 * one USB device with the same S/N.
 */
#include <regex.h>
static void fix_freebsd_sn(AFFILE *af,char *serial_number,int sn_len,char *device_model)
{
    char word1[1024];

    /* Get the first word of the model number */
    strlcpy(word1,device_model,sizeof(word1));
    char *cc = index(word1,' ' );
    if(cc) *cc = '\000';

    FILE *f = popen("sysctl -a dev.umass","r");
    int in_section = 0;
    while(!feof(f)){
	char buf[1024];
	memset(buf,0,sizeof(buf));
	if(fgets(buf,sizeof(buf),f)){
	    /* See if this is the new section */
	    if(strstr(buf,"dev.umass.") && strstr(buf,"desc:")){
		if(strstr(buf,word1)){
		    in_section = 1;
		}
		else {
		    in_section = 0;
		}
	    }
	    if(in_section){
		regex_t r_sn;
		if(regcomp(&r_sn,"sernum=\"([^\"]*)\"",REG_EXTENDED|REG_ICASE)) err(1,"regcomp");
		regmatch_t pm[3];
		memset(pm,0,sizeof(pm));
		if(regexec(&r_sn,buf,2,pm,REG_NOTBOL)==0){
		    char b2[1024];
		    memset(b2,0,sizeof(b2));
		    strncpy(b2,buf+pm[1].rm_so,pm[1].rm_eo-pm[1].rm_so);
		    strlcpy(serial_number,b2,sn_len);
		    ident_update_seg(af,AF_DEVICE_SN,serial_number,0);
		}
		regfree(&r_sn);
	    }
	}
    }
    pclose(f);
}

#define IDENT_DEFINED
/* ident for freebsd */
void imager::ident()
{
    int controller = 0;
    int channel    = 0;

    /* Check for a SCSI drive.
     * This needs to be cleaned up; right now it assumes SCSI bus 2.
     */
    int da_dev = -1;

    if(sscanf(infile,"/dev/da%d",&da_dev)==1){
	char checkbuf[64];
	char want[64];
	char buf[65536];

	/* Now do the devlist to figure out which device this is... */
	scsi_bus = -1;			// for now
	snprintf(want,sizeof(want),"da%d",da_dev);
	sprintf(checkbuf,"camcontrol devlist");
	FILE *f = popen(checkbuf,"r");
	while(!feof(f)){
	    if(fgets(buf,sizeof(buf),f)){
		if(strstr(buf,want)){	// this is the correct line
		    char *cc = strstr(buf,"at scbus");
		    if(cc){
			if(sscanf(cc,
				  "at scbus%d target %d lun %d (pass%d,da%d)",
				  &scsi_bus,&scsi_tid,&scsi_lun,
				  &scsi_pass,&da_dev)==5){
			    break;		// found it!
			}
			if(sscanf(cc,
				  "at scbus%d target %d lun %d (da%d,pass%d)",
				  &scsi_bus,&scsi_tid,&scsi_lun,
				  &da_dev,&scsi_pass)==5){
			    break;		// found it!
			}
		    }
		}
	    }
	}
	pclose(f);

	if(scsi_bus==-1) err(1,"can't find SCSI device for %s",infile);

	/* Now we need to inquiry */
	sprintf(checkbuf,"camcontrol inquiry %d:%d",scsi_bus,scsi_tid);
	f = popen(checkbuf,"r");
	char capbuf[65536];
	capbuf[0] = 0;
      
	while(!feof(f)){
	    if(fgets(buf,sizeof(buf),f)){
		strlcat(capbuf,buf,sizeof(capbuf));
		char *cc = index(buf,'>'); // see if it should be terminated
		if(cc) *cc = 0;
		sprintf(checkbuf,"pass%d: <",scsi_pass);
		checkline(checkbuf,AF_DEVICE_MODEL,buf,device_model,af,0);
		sprintf(checkbuf,"pass%d: Serial Number ",scsi_pass);
		checkline(checkbuf,AF_DEVICE_SN,buf,serial_number,af,0);
	    }
	}
	pclose(f);

	if(serial_number[0]=='\037' && serial_number[1]=='\000'){
	    fix_freebsd_sn(af,serial_number,sizeof(serial_number),device_model);
	}
	ident_update_seg(af,AF_DEVICE_CAPABILITIES,capbuf,0);
	return;
    }
				     

    /* Check for an IDE drive */
    if(strncmp(infile,"/dev/ad",7)==0 &&
       freebsd_get_ata_params(infile,&controller,&channel)==0){
	char capbuf[65536];
	char buf[256];
#if __FreeBSD_version > 600000
	/* Syntax of command was changed in FreeBSD 6.0 */
	sprintf(buf,"atacontrol cap ad%d",controller*2+channel);
#else
	sprintf(buf,"atacontrol cap ata%d %d",controller,channel);
#endif
	capbuf[0] = 0;
	FILE *f = popen(buf,"r");
	while(!feof(f)){
	    if(fgets(buf,sizeof(buf),f)){
		buf[sizeof(buf)-1] = 0;	// make sure it is null-terminated
		strlcat(capbuf,buf,sizeof(capbuf));
		checkline("device model",AF_DEVICE_MODEL,buf,device_model,af,0);
		checkline("serial number",AF_DEVICE_SN,buf,serial_number,af,0);
		checkline("firmware revision",AF_DEVICE_FIRMWARE,buf,firmware_revision,af,0);
		checkline("cylinders",AF_CYLINDERS,buf,0,af,1);
		checkline("heads",AF_HEADS,buf,0,af,1);
		checkline("sectors/track",AF_SECTORS_PER_TRACK,buf,0,af,1);
	    }
	}
	pclose(f);
	ident_update_seg(af,AF_DEVICE_CAPABILITIES,capbuf,0);
	return;
    }
}


/* FreeBSD ata attach commands */
#define MAKE_ATA_ATTACH_COMMANDS_DEFINED
void make_ata_attach_commands(char *attach,char *detach,char *master,char *slave,int dev)
{
    sprintf(attach,"atacontrol attach ata%d",dev);
    sprintf(detach,"atacontrol detach ata%d",dev);
    sprintf(master,"/dev/ad%d",dev*2);
    sprintf(slave,"/dev/ad%d",dev*2+1);
}

/* FreeBSD scsi attach commands. Actually does it and returns 0 if successful,
 * as well as the device.
 */
#define SCSI_ATTACH_DEFINED
int  scsi_attach(char *fname,int fname_len,
		 class imager *im) // returns 0 if successful & sets fname
{
    char buf[256];
    char looking[64];

    sprintf(buf,"camcontrol rescan %d",im->scsi_bus);
    system(buf);

    sprintf(looking,"scbus%d",im->scsi_bus);	// what I'm looking for

    /* Now, see if there is a device on this scsi bus */
    FILE *f = popen("camcontrol devlist","r");
    if(!f) err(1,"camcontrol devlist");
    while(!feof(f)){
	memset(buf,0,sizeof(buf));
	if(fgets(buf,sizeof(buf),f)){
	    char *cc = strstr(buf,looking);
	    if(cc){
		int da_num;
		/* Get the target and lun.
		 * Notice that FreeBSD is inconsistent in the way that this
		 * is reported.
		 */
		if(sscanf(cc+strlen(looking)+1,
			  "target %d lun %d (pass%d,da%d)",
			  &im->scsi_tid,&im->scsi_lun,&im->scsi_pass,&da_num)==4){
		    snprintf(fname,fname_len,"/dev/da%d",da_num);
		    return 0;
		}
		if(sscanf(cc+strlen(looking)+1,
			  "target %d lun %d (da%d,pass%d)",
			  &im->scsi_tid,&im->scsi_lun,&da_num,&im->scsi_pass)==4){
		    snprintf(fname,fname_len,"/dev/da%d",da_num);
		    return 0;
		}
		err(1,"could not parse: '%s'",buf);
	    }
	}
    }
    pclose(f);
    fprintf(stderr,"No SCSI device found:\n");
    fprintf(stderr,"# camcontrol rescan all\n");
    system("camcontrol rescan all");
    return -1;				// failure
	
}
#endif

#ifdef linux
#include <dirent.h>
static void getfile(const char *dirname,const char *filename,char *copy,
		    int copysize,
		    AFFILE *af,const char *segname)
{
    char path[MAXPATHLEN];
    char buf[1024];

    /* Build the pathname we are supposed to get */
    memset(buf,0,sizeof(buf));
    strlcpy(path,dirname,sizeof(path));
    strlcat(path,filename,sizeof(path));
    FILE *f = fopen(path,"r");
    if(f){
	if(fgets(buf,sizeof(buf),f)){
	    char *cc = rindex(buf,'\n');
	    if(cc) *cc = '\000';	// remove trailing \n
	    ident_update_seg(af,segname,buf,0);
	    if(copy) strlcat(copy,buf,copysize);	
	}
	fclose(f);
    }
}

#define IDENT_DEFINED
void imager::ident()
{
    /* Is this a regular file? If so, just return */
    struct stat so;
    if(stat(infile,&so)==0){
	if(S_ISREG(so.st_mode)) return;
    }

    /* If udev is installed, just use that */
    if(stat("/sbin/udevadm",&so)==0){
	char capbuf[65536];
	char buf[1024];
	memset(capbuf,0,sizeof(capbuf));
	snprintf(buf,sizeof(buf),"/sbin/udevadm info --query=all --name=%s",infile);
	FILE *f=popen(buf,"r");
	while(!feof(f)){
	    memset(buf,0,sizeof(buf));
	    if(fgets(buf,sizeof(buf),f)){
		strlcat(capbuf,buf,sizeof(capbuf)); // make local copy
		checkline("E: ID_MODEL=",AF_DEVICE_MODEL,buf,this->device_model,af,0);
		checkline("E: ID_SERIAL_SHORT=",AF_DEVICE_SN,buf,this->serial_number,af,0);
		checkline("E: ID_VENDOR=",AF_DEVICE_MANUFACTURER,buf,0,af,0);
	    }
	}
	pclose(f);
	ident_update_seg(af,AF_DEVICE_CAPABILITIES,capbuf,0);
	return;
    }


    /* Check to see if infile is a USB device. If so, print things about it.
     * If the Linux /sys file system is installed, then /sys/bus/scsi/devices/.../block
     * are symlinks to the actual devices.
     * These have the same name as the /dev/<name>, minus the partition.
     */
 
    if(strncmp(infile,"/dev/",5)==0){
	char *cc;
	char sdname[MAXPATHLEN];
	memset(sdname,0,sizeof(sdname));
	strcpy(sdname,infile+5);
	/* If a partition name was provided, eliminate it */
	for(cc=sdname;*cc;cc++){
	    if(isdigit(*cc)){
		*cc = '\000';
		break;
	    }
	}

	/* Look to see if this is a USB device*/
	DIR *dir = opendir("/sys/bus/scsi/devices/");
	if(dir){
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
			device_model[0] = 0;
			serial_number[0] = 0;
			getfile(usbdir,"manufacturer",device_model,sizeof(device_model),
				af,AF_DEVICE_MANUFACTURER);
			strlcat(device_model," ",sizeof(device_model));
			getfile(usbdir,"product",device_model,sizeof(device_model),
				af,AF_DEVICE_MODEL);
			getfile(usbdir,"serial",serial_number,sizeof(serial_number),
				af,AF_DEVICE_SN);
			//getfile(usbdir,"version",0,0,af,AF_DEVICE_FIRMWARE);
			closedir(dir);
			return;		// we have successfully identified the device
		    }
		}
	    }
	    closedir(dir);		// never found it
	}

	/* Fall back to a regular hard drive */
	if(access(infile,R_OK)==0){
	    char capbuf[65536];
	    char buf[256];
	    if(af_hasmeta(infile)) return;	// something is wrong here
	    snprintf(buf,sizeof(buf),"hdparm -I %s",infile);
	    capbuf[0] = 0;
	    FILE *f = popen(buf,"r");
	    while(!feof(f)){
		if(fgets(buf,sizeof(buf),f)){
		    buf[sizeof(buf)-1] = 0;	// make sure it is null-terminated
		    strlcat(capbuf,buf,sizeof(capbuf));	// append to the buffer
		    
		    /* Now check for each of the lines */
		    checkline("Model Number:",AF_DEVICE_MODEL,buf,device_model,af,0);
		    checkline("Serial Number:",AF_DEVICE_SN,buf,serial_number,af,0);
		    checkline("Firmware Revision:",AF_DEVICE_FIRMWARE,
			      buf,firmware_revision,af,0);
		    checkline("cylinders",AF_CYLINDERS,buf,0,af,1);
		    checkline("heads",AF_HEADS,buf,0,af,1);
		    checkline("sectors/track",AF_SECTORS_PER_TRACK,buf,0,af,1);
		}
	    }
	    pclose(f);
	    ident_update_seg(af,AF_DEVICE_CAPABILITIES,capbuf,0);
	}
    }
    return;				// can't figure it out
}


/*
 * Here are some parameters you may find interesting:
 *       hdparm -R 0x1700 0 0 /dev/hda   (to remove /dev/hda)
 *       ide2: BM-DMA at 0xb800-0xb807, BIOS settings: hde:DMA, hdf:pio
 * Unfortunately, I can't get Linux ATA attach to work. Can you?
 * Note: "These mknod calls should be eliminated. If this is not possible,
 * a secure sub-directory should be created to hold them, and
 * umask() should be called to control access." --- VSecurity
 */
#define MAKE_ATA_ATTACH_COMMANDS_DEFINED
void make_ata_attach_commands(char *attach,char *detach,char *master,char *slave,int dev)
{
#if 0

    system("/sbin/mknod /tmp/hda c  3 0");
    system("/sbin/mknod /tmp/hdb c  3 64");
    system("/sbin/mknod /tmp/hdc c 22 0");
    system("/sbin/mknod /tmp/hdd c 22 64");
    sprintf(attach,"hdparm -R %d",dev);
    sprintf(detach,"hdparm -U %d",dev);
    sprintf(master,"/dev/ad%d",dev*2);
    sprintf(slave,"/dev/ad%d",dev*2+1);
#endif
}

/* Linux scsi attach commands */
/* I can't figure out how to do this. */
void make_scsi_attach_commands(char *cmd_attach,char *cmd_detach, int scsi_bus)
{
    cmd_attach[0] = 0;
    cmd_detach[0] = 0;
}

#define SCSI_ATTACH_DEFINED
int  scsi_attach(char *fname,int fname_len,class imager *) // returns 0 if successful & sets fname
{
    return -1;				// can't do it
}
#endif


/****************************************************************
 *** MacOS
 ****************************************************************/
#ifdef __APPLE__
int get_block(FILE *f,char *buf,int buflen)
{
    /* Get a block from FILE f, where a block is defined as lines until a \n\n */
    memset(buf,0,buflen);
    buflen--;				// so we won't lose the last \n
    int consecutive_newlines = 0;
    int lns = 0;
    while(!feof(f)){
	if(!fgets(buf,buflen,f)) break;
	if(buf[0]=='\n'){
	    return 1;			// blank line
	}
	consecutive_newlines = 0;
	int bytes = strlen(buf);	// number of bytes that was read
	buf += bytes;
	buflen -= bytes;
	lns++;
    }
    return lns>0;
}

#define IDENT_DEFINED
void imager::ident()
{
    /* Removed because system profiler causes problems with MacOS 10.5 
     * when diskarbitrationdaemon is disabled.
     */
#if 0
    if(strncmp(infile,"/dev/",5)==0){
	char *name = infile+5;
	char buf[65536];
	char looking[65536];

	snprintf(looking,sizeof(looking),"BSD Name: %s",name);
	FILE *f = popen("system_profiler SPUSBDataType","r");
	while(get_block(f,buf,sizeof(buf))){
	    if(strstr(buf,looking)){	// did I find the block I'm looking for?
		checkline("Serial Number:",AF_DEVICE_SN,buf,this->serial_number,af,0);
		checkline("Manufacturer:",AF_DEVICE_MANUFACTURER,buf,this->device_model,af,0);
	    }
	}
	pclose(f);
    }
#endif    
}
#endif



/* If we do not have an ident, make it a stub... */
#ifndef IDENT_DEFINED
void imager::ident()
{
    return;
}
#endif


#ifndef MAKE_ATA_ATTACH_COMMANDS_DEFINED
/* We do not have these defined either... */
void make_ata_attach_commands(char *attach,char *detach,char *master,char *slave,int dev)
{
    attach[0] = 0;
    detach[0] = 0;
    master[0] = 0;
    slave[0]  = 0;
}
#endif

#ifndef SCSI_ATTACH_DEFINED
int  scsi_attach(char *fname,int fname_len,class imager *) // returns 0 if successful & sets fname
{
    return -1;				// can't do it
}
#endif
