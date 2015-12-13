About
-----

This project aims to provide a full-featured [exFAT][1] file system implementation for Unix-like systems. It consists of a [FUSE][2] module (fuse-exfat) and a set of utilities (exfat-utils).

Supported operating systems:

* GNU/Linux
* Mac OS X 10.5 or later
* FreeBSD
* OpenBSD

Most GNU/Linux distributions already have fuse-exfat and exfat-utils in their repositories, so you can just install and use them. The next chapter describes how to compile them from source.

Compiling
---------

To build this project under GNU/Linux you need to install the following packages:

* git
* autoconf
* automake
* pkg-config
* fuse-devel (or libfuse-dev)
* gcc
* make

Get the source code, change directory and compile:

    git clone https://github.com/relan/exfat.git
    cd exfat
    autoreconf --install
    ./configure --prefix=/usr
    make

Then install driver and utilities:

    sudo make install

You can remove them using this command:

    sudo make uninstall

Mounting
--------

Modern GNU/Linux distributions will mount exFAT volumes automatically—util-linux-ng 2.18 (was renamed to util-linux in 2.19) is required for this. Anyway, you can mount manually (you will need root privileges):

    sudo mount.exfat-fuse /dev/sdXn /mnt/exfat

where /dev/sdXn is the partition special file, /mnt/exfat is a mountpoint.

Feedback
--------

If you have any questions, issues, suggestions, bug reports, etc. please create an [issue][3]. Pull requests are also welcome!

[1]: http://en.wikipedia.org/wiki/ExFAT
[2]: http://en.wikipedia.org/wiki/Filesystem_in_Userspace
[3]: https://github.com/relan/exfat/issues
