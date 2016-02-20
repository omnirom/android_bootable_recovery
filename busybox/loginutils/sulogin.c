/* vi: set sw=4 ts=4: */
/*
 * Mini sulogin implementation for busybox
 *
 * Licensed under GPLv2 or later, see file LICENSE in this source tree.
 */

//usage:#define sulogin_trivial_usage
//usage:       "[-t N] [TTY]"
//usage:#define sulogin_full_usage "\n\n"
//usage:       "Single user login\n"
//usage:     "\n	-t N	Timeout"

#include "libbb.h"
#include <syslog.h>

int sulogin_main(int argc, char **argv) MAIN_EXTERNALLY_VISIBLE;
int sulogin_main(int argc UNUSED_PARAM, char **argv)
{
	int timeout = 0;
	struct passwd *pwd;
	const char *shell;

	logmode = LOGMODE_BOTH;
	openlog(applet_name, 0, LOG_AUTH);

	opt_complementary = "t+"; /* -t N */
	getopt32(argv, "t:", &timeout);
	argv += optind;

	if (argv[0]) {
		close(0);
		close(1);
		dup(xopen(argv[0], O_RDWR));
		close(2);
		dup(0);
	}

	/* Malicious use like "sulogin /dev/sda"? */
	if (!isatty(0) || !isatty(1) || !isatty(2)) {
		logmode = LOGMODE_SYSLOG;
		bb_error_msg_and_die("not a tty");
	}

	/* Clear dangerous stuff, set PATH */
	sanitize_env_if_suid();

	pwd = safegetpwuid(0);
	if (!pwd) {
		goto auth_error;
	}

	while (1) {
		int r;

		r = ask_and_check_password_extended(pwd, timeout,
			"Give root password for system maintenance\n"
			"(or type Control-D for normal startup):"
		);
		if (r < 0) {
			/* ^D, ^C, timeout, or read error */
			bb_info_msg("Normal startup");
			return 0;
		}
		if (r > 0) {
			break;
		}
		bb_do_delay(LOGIN_FAIL_DELAY);
		bb_info_msg("Login incorrect");
	}

	bb_info_msg("System Maintenance Mode");

	IF_SELINUX(renew_current_security_context());

	shell = getenv("SUSHELL");
	if (!shell)
		shell = getenv("sushell");
	if (!shell)
		shell = pwd->pw_shell;

	/* Exec login shell with no additional parameters. Never returns. */
	run_shell(shell, 1, NULL, NULL);

 auth_error:
	bb_error_msg_and_die("no password entry for root");
}
