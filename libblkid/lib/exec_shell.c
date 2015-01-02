/*
 * exec_shell() - launch a shell, else exit!
 *
 * This program is free software; you can redistribute it and/or modify it
 * under the terms of the GNU General Public License as published by the
 * Free Software Foundation; either version 2, or (at your option) any
 * later version.
 *
 * This program is distributed in the hope that it will be useful, but
 * WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License along
 * with this program; if not, write to the Free Software Foundation, Inc.,
 * 51 Franklin Street, Fifth Floor, Boston, MA 02110-1301 USA.
 */

#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sys/types.h>

#include "nls.h"
#include "c.h"
#include "xalloc.h"

#include "exec_shell.h"

#define DEFAULT_SHELL "/bin/sh"

void exec_shell(void)
{
	const char *shell = getenv("SHELL"), *shell_basename;
	char *arg0;
	if (!shell)
		shell = DEFAULT_SHELL;

	shell_basename = basename(shell);
	arg0 = xmalloc(strlen(shell_basename) + 2);
	arg0[0] = '-';
	strcpy(arg0 + 1, shell_basename);

	execl(shell, arg0, NULL);
	err(EXIT_FAILURE, _("failed to execute %s"), shell);
}
