#                                               -*- Autoconf -*-
# Process this file with autoconf to produce a configure script.
# Order is largely irrevellant, although it must start with AC_INIT and end with AC_OUTPUT
# See http://autotoolset.sourceforge.net/tutorial.html
# and http://www.openismus.com/documents/linux/automake/automake.shtml

AC_INIT([AIMAGE], [3.3.0], [bugs@afflib.org])
AM_INIT_AUTOMAKE([foreign])
AM_MAINTAINER_MODE

# Programs that we will be using
AC_PROG_CXX
AC_PROG_CC
AC_PROG_INSTALL

AC_CONFIG_HEADERS([config.h])
AC_CONFIG_FILES([
    Makefile
    src/Makefile
    aimage.spec
])

################################################################

# Bring additional directories where things might be found into our
# search path. I don't know why autoconf doesn't do this by default
for spfx in /usr/local /opt/local /sw ; do
    echo checking ${spfx}/include
    if test -d ${spfx}/include; then
        CPPFLAGS="-I${spfx}/include $CPPFLAGS"
        LDFLAGS="-L${spfx}/lib $LDFLAGS"
    fi
done


# Specific headers that I plan to use
AC_CHECK_HEADERS([stdio.h strings.h string.h stdlib.h sys/types.h sys/time.h sys/resource.h sys/param.h sys/statfs.h zlib.h sys/stat.h fcntl.h assert.h errno.h arpa/inet.h unistd.h dirent.h err.h netinet/in.h getopt.h curses.h termcap.h ])
# Autoupdate added the next two lines to ensure that your configure
# script's behavior did not change.  They are probably safe to remove.
AC_CHECK_INCLUDES_DEFAULT
AC_PROG_EGREP


# Specific functions that we want to know about
AC_CHECK_FUNCS([printf getrusage])
AC_CHECK_FUNCS([getprogname strlcpy strlcat err_set_exit srandom srandomdev])
AC_CHECK_FUNCS([fstatfs valloc isdigit isalnum isalphanum isatty popen])
AC_CHECK_FUNCS([ftruncate memset mkdir putenv regcomp strcasecmp strchr strdup strerror strrchr])
AC_CHECK_FUNCS([err errx warn warnx])
AC_CHECK_FUNCS([tputs tgoto tgetstr tgetnum gotorc beep endwin setupterm printw])

# Special features that can be enabled or disabled
AC_ARG_ENABLE(noopt, AS_HELP_STRING([--enable-noopt],[Drop -O C flags]))

################################################################
################################################################
### OpenSSL

# We need AFF, Z & Crypto
AC_CHECK_HEADERS([openssl/md5.h openssl/sha.h openssl/aes.h openssl/rsa.h openssl/rand.h]) 
AC_CHECK_LIB([crypto],[MD5_Update],,AC_MSG_ERROR([OpenSSL required for AFFLIB]))
# Optional libraries
AC_CHECK_LIB([crypto], [EVP_MD_CTX_new],
             [],
             AC_MSG_ERROR([OpenSSL 1.1+ (libcrypto) not installed; cannot continue.]))
AC_CHECK_LIB([ncurses],[initscr],, AC_MSG_RESULT([ncurses not installed]))
AC_CHECK_LIB([readline],[readline],, AC_MSG_RESULT([readline not installed]))
AC_CHECK_HEADERS([readline/readline.h]) 
AC_CHECK_FUNCS([MD5])
AC_CHECK_FUNCS([AES_encrypt])
AC_CHECK_FUNCS([RAND_pseudo_bytes])
AC_CHECK_FUNCS([des_read_pw_string])
AC_CHECK_FUNCS([EVP_read_pw_string])


################################################################
## AFFLIB
## KIND of Obvious, but we need it
AC_CHECK_LIB([z],[uncompress],,AC_MSG_ERROR([zlib not installed; required for AFFLIB]))
AC_CHECK_LIB([afflib],[af_open],,AC_MSG_ERROR([AFFLIB not installed; required for aimage]))
AC_CHECK_HEADER([afflib/afflib.h],,AC_MSG_ERROR([aimage requires afflib/aftimer.h; is AFFLIB installed?]))
AC_CHECK_HEADER([afflib/aftimer.h],,AC_MSG_ERROR([aimage requires afflib/aftimer.h; is AFFLIB installed?]))
AC_CHECK_HEADER([afflib/utils.h],,AC_MSG_ERROR([aimage requires afflib/utils.h; is AFFLIB installed?]))
AC_CHECK_LIB([afflib],[af_open])
AC_CHECK_FUNCS([af_display_as_quad af_display_as_hex])

################################################################
## Expat
## Required for S3 and Digital Signatures
##
AC_ARG_WITH(expat,
  AS_HELP_STRING([--with-expat=PATH], [where libexpat is compiled (if it isn't installed); required for S3 and Digital Signatures]),
  [LDFLAGS="-L${with_expat} $LDFLAGS" ;
   CPPFLAGS="-I${with_expat}/lib $CPPFLAGS"])

AC_CHECK_HEADER([expat.h])
AC_CHECK_LIB([expat],[XML_ParserCreate], [enable_expat=yes], [enable_expat=no])


AC_CHECK_HEADERS([openssl/aes.h openssl/bio.h openssl/evp.h openssl/hmac.h openssl/md5.h openssl/rand.h openssl/rsa.h openssl/sha.h openssl/pem.h])





############## drop optimization flags ################
if test x"${AFF_NOOPT}" != "x" ; then
   with_noopt="yes";
fi

if test "${with_noopt}" = "yes" ; then
  CFLAGS=`echo "$CFLAGS" | sed s/-O[[0-9]]//`             # note the double quoting! 
  CXXFLAGS=`echo "$CXXFLAGS" | sed s/-O[[0-9]]//`
fi


AC_OUTPUT


