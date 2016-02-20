/* vi: set sw=4 ts=4: */
/*
 * Licensed under GPLv2 or later, see file LICENSE in this source tree.
 */

//usage:#define dumpleases_trivial_usage
//usage:       "[-r|-a] [-f LEASEFILE]"
//usage:#define dumpleases_full_usage "\n\n"
//usage:       "Display DHCP leases granted by udhcpd\n"
//usage:	IF_LONG_OPTS(
//usage:     "\n	-f,--file=FILE	Lease file"
//usage:     "\n	-r,--remaining	Show remaining time"
//usage:     "\n	-a,--absolute	Show expiration time"
//usage:	)
//usage:	IF_NOT_LONG_OPTS(
//usage:     "\n	-f FILE	Lease file"
//usage:     "\n	-r	Show remaining time"
//usage:     "\n	-a	Show expiration time"
//usage:	)

#include "common.h"
#include "dhcpd.h"
#include "unicode.h"

int dumpleases_main(int argc, char **argv) MAIN_EXTERNALLY_VISIBLE;
int dumpleases_main(int argc UNUSED_PARAM, char **argv)
{
	int fd;
	int i;
	unsigned opt;
	int64_t written_at, curr, expires_abs;
	const char *file = LEASES_FILE;
	struct dyn_lease lease;
	struct in_addr addr;

	enum {
		OPT_a = 0x1, // -a
		OPT_r = 0x2, // -r
		OPT_f = 0x4, // -f
	};
#if ENABLE_LONG_OPTS
	static const char dumpleases_longopts[] ALIGN1 =
		"absolute\0"  No_argument       "a"
		"remaining\0" No_argument       "r"
		"file\0"      Required_argument "f"
		;

	applet_long_options = dumpleases_longopts;
#endif
	init_unicode();

	opt_complementary = "=0:a--r:r--a";
	opt = getopt32(argv, "arf:", &file);

	fd = xopen(file, O_RDONLY);

	printf("Mac Address       IP Address      Host Name           Expires %s\n", (opt & OPT_a) ? "at" : "in");
	/*     "00:00:00:00:00:00 255.255.255.255 ABCDEFGHIJKLMNOPQRS Wed Jun 30 21:49:08 1993" */
	/*     "123456789 123456789 123456789 123456789 123456789 123456789 123456789 123456789 */

	xread(fd, &written_at, sizeof(written_at));
	written_at = SWAP_BE64(written_at);
	curr = time(NULL);
	if (curr < written_at)
		written_at = curr; /* lease file from future! :) */

	while (full_read(fd, &lease, sizeof(lease)) == sizeof(lease)) {
		const char *fmt = ":%02x" + 1;
		for (i = 0; i < 6; i++) {
			printf(fmt, lease.lease_mac[i]);
			fmt = ":%02x";
		}
		addr.s_addr = lease.lease_nip;
#if ENABLE_UNICODE_SUPPORT
		{
			char *uni_name = unicode_conv_to_printable_fixedwidth(/*NULL,*/ lease.hostname, 19);
			printf(" %-16s%s ", inet_ntoa(addr), uni_name);
			free(uni_name);
		}
#else
		/* actually, 15+1 and 19+1, +1 is a space between columns */
		/* lease.hostname is char[20] and is always NUL terminated */
		printf(" %-16s%-20s", inet_ntoa(addr), lease.hostname);
#endif
		expires_abs = ntohl(lease.expires) + written_at;
		if (expires_abs <= curr) {
			puts("expired");
			continue;
		}
		if (!(opt & OPT_a)) { /* no -a */
			unsigned d, h, m;
			unsigned expires = expires_abs - curr;
			d = expires / (24*60*60); expires %= (24*60*60);
			h = expires / (60*60); expires %= (60*60);
			m = expires / 60; expires %= 60;
			if (d)
				printf("%u days ", d);
			printf("%02u:%02u:%02u\n", h, m, (unsigned)expires);
		} else { /* -a */
			time_t t = expires_abs;
			fputs(ctime(&t), stdout);
		}
	}
	/* close(fd); */

	return 0;
}
