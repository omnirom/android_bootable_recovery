#!/sbin/sh

for cmd in $(/sbin/busybox --list); do
	/sbin/busybox ln -s /sbin/busybox /sbin/$cmd
done

ln -sf /sbin/pigz /sbin/gzip
ln -sf /sbin/unpigz /sbin/gunzip
rm /sbin/mkdosfs