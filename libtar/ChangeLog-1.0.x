libtar 1.0.2 - 6/21/00
------------

- tar_set_file_perms() now calls chown() only if the effective user ID is 0
  (workaround for IRIX and HP-UX, which allow file giveaways)

- tar_set_file_perms() now calls chmod() or lchmod() after chown()
  (this fixes a problem with extracting setuid files under Linux)

- removed calls to fchown() and fchmod() from tar_extract_regfile()

- fixed bugs in th_read() which didn't set errno properly

- removed various unused variables

----------------------------------------------------------------------

libtar 1.0.1 - 4/1/00
------------

- removed libgen.h include from dirname and basename compat code

- added lib/fnmatch.c compatability module from OpenBSD

- fixed several objdirs bugs in libtar/Makefile.in

- misc Makefile changes (added $CPPFLAGS support, added -o flag to compile
  commands, use $CFLAGS on link line, etc)

- removed "inline" keyword from all source files to prevent portability
  problems

- updated README

----------------------------------------------------------------------

libtar 1.0 - 1/2/00
----------

- various portability fixes

- "make install" now runs mkencap and epkg if they're available

- libmisc is now integrated into libtar

----------------------------------------------------------------------

libtar 0.5.6 beta - 12/16/99
-----------------

- changed API to allow better error reporting via errno

- added manpages to document libtar API

- replaced symbolic_mode() call with strmode() compatibility code

----------------------------------------------------------------------

libtar 0.5.5 beta - 11/16/99
-----------------

- fixed conditional expression in extract.c to check if we're overwriting
  a pre-existing file

- many improvements to libtar.c driver program (better error checking,
  added -C and -v options, etc)

- changed API to include list of canned file types, instead of passing
  function pointers to tar_open()

- fixed tar_set_file_perms() to not complain about chown() if not root
  and not to call utime() on a symlink

- added hash code for extracting hard links in other directory paths

- fixed tar_extract_glob() to only print filenames if TAR_VERBOSE option
  is set

- replaced GNU basename(), dirname(), and strdup() compatibility code
  with OpenBSD versions

- configure performs super-anal checking of basename() and dirname()

----------------------------------------------------------------------

libtar 0.5.4 beta - 11/13/99
-----------------

- portability fix: use ranlib instead of ar -s

- misc fixes in append.c, extract.c, and wrapper.c to do error checking

- fixed a bug in tar_append_file() in append.c which added some garbage
  characters to encoded symlink names (wasn't NULL-terminating the result
  of readlink())

- fixed a bug in symbolic_mode() in output.c concerning setuid and setgid
  bit displaying

- fixed tar_extract_all() in wrapper.c to only call print_long_ls() if
  the TAR_VERBOSE option is set

- added libtar_version constant string to handle.c for external configure
  scripts to detect what version of libtar is installed

----------------------------------------------------------------------

libtar 0.5.3 beta - 09/27/99
-----------------

- fixed mk_dirs_for_file() to avoid broken dirname() implementations

- misc portability fixes

- merged old "compat" and "libds" directories into new "misc" directory
  and cleaned up Makefiles

----------------------------------------------------------------------

libtar 0.5.2 beta - 09/10/99
-----------------

- use calloc() instead of malloc() in tar_open() to fix a bounds-checking
  bug in tar_extract_all()

- fix tar_extract_all() to properly honor the prefix argument

----------------------------------------------------------------------

libtar 0.5.1 beta - 08/27/99
-----------------

- misc portability fixes

----------------------------------------------------------------------

libtar 0.5 beta - 07/05/99
---------------

- first public release

