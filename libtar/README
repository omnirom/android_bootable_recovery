libtar - C library for manipulating tar files
======

libtar is a library for manipulating tar files from within C programs.
Here are some of its features:

  * Handles both POSIX tar file format and the GNU extensions.
  * API provides functions for easy use, such as tar_extract_all().
  * Also provides functions for more granular use, such as 
    tar_append_regfile().


Installation
------------

To build libtar, you should be able to simply run these commands:

   ./configure
   make
   make install


Encap Package Support
---------------------

To build this software as an Encap package, you can pass the
--enable-encap option to configure.  This will be automatically
enabled if the epkg or mkencap programs are detected on the system,
but can be overridden by the --disable-encap option.

When building an Encap package, the configure script will automatically
adjust the installation prefix to use an appropriate Encap package
directory.  It does this using a heuristic algorithm which examines the
values of the ${ENCAP_SOURCE} and ${ENCAP_TARGET} environment variables
and the argument to configure's --prefix option.

If mkencap was detected on the system, it will be automatically run during
"make install".  By default, epkg will also be run, but this can be
inhibited with the --disable-epkg-install configure option.

For information on the Encap package management system, see the WSG
Encap Archive:

   http://www.encap.org/


zlib Support
------------

The configure script will attempt to find the zlib library on your system
for use with the libtar driver program.  The zlib package is available from:

   http://www.gzip.org/zlib/

If zlib is installed on your system, but you do not wish to use it,
specify the --without-zlib option when you invoke configure.


More Information
----------------

For documentation of the libtar API, see the enclosed manpages.  For more
information on the libtar package, see:

   http://www-dev.cites.uiuc.edu/libtar/

Source code for the latest version of libtar will be available there, as
well as Encap binary distributions for many common platforms.


Supported Platforms
-------------------

I develop and test libtar on the following platforms:

   AIX 4.3.3 and 5.1
   HP-UX 11.00
   IRIX 6.5
   RedHat Linux 7.2
   Solaris 8 and 9

It should also build on the following platforms, but I do not actively
support them:

   AIX 3.2.5
   AIX 4.2.1
   Cygwin
   FreeBSD
   HP-UX 10.20
   Linux/libc5
   OpenBSD
   Solaris 2.5
   Solaris 2.6
   Solaris 7

If you successfully build libtar on another platform, please email me a
patch and/or configuration information.


Compatibility Code
------------------

libtar depends on some library calls which are not available or not
usable on some platforms.  To accomodate these systems, I've included
a version of these calls in the compat subdirectory.

I've slightly modified these functions for integration into this source
tree, but the functionality has not been modified from the original
source.  Please note that while this code should work for you, I didn't
write it, so please don't send me bug reports on it.


Author
------

Feedback and bug reports are welcome.

Mark D. Roth <roth@uiuc.edu>
Campus Information Technologies and Educational Services
University of Illinois at Urbana-Champaign

