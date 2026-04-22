#ifndef SB_EXEC_H
#define SB_EXEC_H

/*
 * Shell-free process execution.
 *
 * Use these in every place that assembles a command with external data
 * (video_ids loaded from JSON, URLs, user search queries, paths).
 * They fork() + execvp() with an argv array: no /bin/sh, no metacharacter
 * interpretation, so injection via crafted input is impossible.
 *
 * Plain system()/popen() is fine only for static strings with no
 * interpolation (e.g. `system("command -v curl >/dev/null 2>&1")`).
 */

#include <stdio.h>

/* Run a command to completion.
 * If silent != 0, child's stdout+stderr are redirected to /dev/null.
 * Returns the child exit status (0 on success), -1 on fork/exec failure. */
int sb_exec_status(char *const argv[], int silent);

/* Spawn a command and return a FILE* reading its stdout.
 * If merge_stderr != 0, stderr is merged into stdout; otherwise stderr goes
 * to /dev/null. Use sb_exec_pclose() to close the fp and reap the child. */
FILE *sb_exec_popen(char *const argv[], int merge_stderr);

/* Close an fp returned by sb_exec_popen() and wait for its child.
 * Returns the child exit status (0 on success), -1 on failure. */
int sb_exec_pclose(FILE *fp);

/* Parse a cookie args string built by build_cookie_args() back into argv
 * tokens. Accepted formats:
 *   ""                                      -> pushes nothing
 *   " --cookies-from-browser <name>"        -> pushes 2 tokens
 *   " --cookies '<path>'"                   -> pushes 2 tokens (unquoted)
 * Tokens are strdup'd into *argc onwards of vec (caller frees via
 * sb_free_cookie_args after the command has been run).
 * Returns 0 on success, -1 on malformed input or if vec has no room. */
int sb_parse_cookie_args(const char *src, char **vec, int *argc, int vec_max);

/* Free tokens pushed by sb_parse_cookie_args between indices [from, to). */
void sb_free_cookie_args(char **vec, int from, int to);

#endif
