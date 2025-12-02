bin_PROGRAMS = aimage 

aimage_SOURCES = aimage.cpp aimage.h aimage_os.cpp gui.cpp gui.h ident.cpp ident.h \
	imager.cpp imager.h hash_t.h 


# INCLUDES = -I@top_srcdir@/lib/
# aimage_LDADD = @top_builddir@/lib/libafflib.la 


testnet: $(bin_PROGRAMS)
	./aimage -q listen:10000 image.aff &
	nc localhost 10000 < image.iso

test.iso:
	dd if=/dev/random of=test.iso bs=65536 count=1000

agent.pem: 
	openssl req -x509 -newkey rsa:1024 -keyout agent.pem -out agent.pem -nodes \
	    -subj "/C=US/ST=California/L=Monterey/O=Country Govt./OU=Sherif Dept/CN=Mr. Investigator/emailAddress=mr@investiations.com"

signed-aff: agent.pem test.iso
	./aimage -x --batch 

afd-testsha: 
	./aimage -x --batch /dev/random -M10m /tmp/x.afd

afd-testrnd:
	./aimage -x --batch /dev/random -M10m /tmp/x.afd

