/* SPDX-License-Identifier: ((GPL-2.0 WITH Linux-syscall-note) OR BSD-2-Clause) */
/* 
 * This program can be distributed under the terms of the GNU GPL.
 * See the file COPYING.gpl.
 * 
 * This program may also be distributed under the terms of the BSD Licence as
 * follows:
 * 
 * 
 * Copyright 2022 Paul Sheer
 * 
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions are met:
 * 
 * 1. Redistributions of source code must retain the above copyright notice,
 * this list of conditions and the following disclaimer.
 * 
 * 2. Redistributions in binary form must reproduce the above copyright notice,
 * this list of conditions and the following disclaimer in the documentation
 * and/or other materials provided with the distribution.
 * 
 * THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS "AS IS"
 * AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
 * ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT HOLDER OR CONTRIBUTORS BE
 * LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR
 * CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF
 * SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS
 * INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN
 * CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE)
 * ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE
 * POSSIBILITY OF SUCH DAMAGE.
 * 
 */

#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <termios.h>
#include <unistd.h>
#include <errno.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <pty.h>


#define HAVE_FORKPTY
#define HAVE_TCGETATTR

static int verbose = 0;

static void set_termios (int fd)
{
#ifdef HAVE_TCGETATTR
    struct termios tios;
    memset (&tios, 0, sizeof (tios));
    if (tcgetattr (fd, &tios) != 0)
	return;
#ifdef B19200
#ifdef OCRNL
    tios.c_oflag &= ~(ONLCR | OCRNL);
#else
    tios.c_oflag &= ~ONLCR;
#endif
    tios.c_lflag &= ~(ECHO | ICANON | ISIG);
    tios.c_iflag &= ~(ICRNL);
#ifdef VTIME
    tios.c_cc[VTIME] = 1;
#endif
#ifdef VMIN
    tios.c_cc[VMIN] = 1;
#endif
    tios.c_iflag &= ~(ISTRIP);
#if defined(TABDLY) && defined(TAB3)
    if ((tios.c_oflag & TABDLY) == TAB3)
	tios.c_oflag &= ~TAB3;
#endif
/* disable interpretation of ^S: */
    tios.c_iflag &= ~IXON;
#ifdef VDISCARD
    tios.c_cc[VDISCARD] = 255;
#endif
#ifdef VEOL2
    tios.c_cc[VEOL2] = 255;
#endif
#ifdef VEOL
    tios.c_cc[VEOL] = 255;
#endif
#ifdef VLNEXT
    tios.c_cc[VLNEXT] = 255;
#endif
#ifdef VREPRINT
    tios.c_cc[VREPRINT] = 255;
#endif
#ifdef VSUSP
    tios.c_cc[VSUSP] = 255;
#endif
#ifdef VWERASE
    tios.c_cc[VWERASE] = 255;
#endif
    tcsetattr (fd, TCSADRAIN, &tios);
#endif
#endif				/* HAVE_TCGETATTR */
}

pid_t open_under_pty (int *in, int *out, const char *file, char *const argv[])
{
    int master = 0;
    char l[80];
    pid_t p;
#ifdef HAVE_FORKPTY
#ifdef NEED_WINSIZE
    struct winsize {
	unsigned short ws_row;
	unsigned short ws_col;
	unsigned short ws_xpixel;
	unsigned short ws_ypixel;
    } win;
#else
    struct winsize win;
#endif
    memset (&win, 0, sizeof (win));
    p = (pid_t) forkpty (&master, l, NULL, &win);
#else
    p = (pid_t) forkpty (&master, l);
#endif
    if (p == -1)
	return -1;
#if 0
    ioctl (master, FIONBIO, &yes);
    ioctl (master, FIONBIO, &yes);
#endif
    if (p) {
	*in = dup (master);
	*out = dup (master);
	close (master);
	return p;
    }
    set_termios (0);
    execvp (file, argv);
    exit (1);
    return 0;
}



void chomp (char *s_)
{
    unsigned char *p, *q, *r;
    r = p = q = (unsigned char *) s_;
    while (*q && *q <= ' ')
        q++;
    while (*q) {
        if (*q > ' ')
            r = p + 1;
        *p++ = *q++;
    }
    *r = '\0';
}

/* is flexible about trailing whitespace */
static int endswith (const char *p, const char *s)
{
    int l, c;
    l = strlen (p);
    c = strlen (s);
    while (l > 0 && p[l - 1] <= ' ')
        l--;
    if (l < c)
        return 0;
    return !memcmp (p + l - c, s, c);
}

#define ERROR(a,b)      handle_error(__FILE__, __LINE__, a, b)
static void handle_error (const char *file, int line, const char *msg, int c)
{
    char s[256];
    snprintf (s, sizeof (s), "%s:%d: error when %s (ret=%d,errno=%d)", file, line, msg, c, errno);
    if (!c) {
        fprintf (stderr, "%s\n", s);
    } else {
        perror (s);
    }
    exit (1);
}

/* returns >0 on success */
static int write_all (const char *buf, int l)
{
    int tot = 0;
    while (l > 0) {
        int c;
        c = write (1, buf, l);
        if (c <= 0)
            return c;
        l -= c;
        buf += c;
        tot += c;
    }
    return tot;
}


enum _state {
    STATE_START,
    STATE_AUTH_OK,
    STATE_WAIT,
    STATE_GOT_NL,
};

enum _expect_string {
    EXPSTR_YN = 0,
    EXPSTR_ALT_YN,
    EXPSTR_PASS,
    EXPSTR_LOGIN,
    EXPSTR_MAX,
};

struct expect_string {
    const char *expstr;
    const char *opt;
};

int main (int argc, char **argv)
{
    char buf[8192];
    int buf_len;
    char the_password[256];
    pid_t child;
    int in_fd, out_fd;
    int i, l;
    enum _state state = STATE_START;
    char **a;
    struct expect_string expectstring[EXPSTR_MAX + 1] = {
        {"(yes/no)?", "-string-yn"},
        {"(yes/no/[fingerprint])?", "-string-alt-yn"},
        {"password:", "-string-pass"},
        {"", "-string-login"},
        {NULL, NULL},
    };
    if (argc == 2 && (!strcmp(argv[1], "-h") || !strcmp(argv[1], "--help"))) {
        printf ("%s", "\n\
WARNING: DO NOT USE THIS COMMAND OVER THE PUBLIC INTERNET. PRIVATE NETWORKS ONLY.\n\
\n\
Usage:\n\
    nopass-ssh [-verbose] [-string... <expected-text>] <ssh-options> <remote> '<shell-script>'\n\
    nopass-ssh [-h|--help]\n\
\n\
    nopass-ssh executes a command on a remote machine by reading the password\n\
    from stdin, and then invoking ssh. This is different to regular ssh which\n\
    will never read from stdin. This allows the user to script ssh commands.\n\
    This is not for the purposes of general Unix administration; rather, it\n\
    is useful for, say, if you are trying to pre-configure machines for shipping,\n\
    or setup virtual machines.\n\
\n\
    Example <ssh-options>:\n\
        -l <user>               Specify login name.\n\
        -X                      Enable X11 forwarding\n\
\n\
    <remote>                    IP address or hostname.\n\
    '<shell-script>'            Script to be execute on <remote> machine.\n\
    -string-yn <y/n>            String on which to send 'yes\\r\\n' response. Default: '(yes/no)?'\n\
                                (Trailing whitespace is ignored in ssh output.)\n\
    -string-alt-yn <y/n/x>      Alternative string to -string-yn. Both are checked.\n\
                                Default: '(yes/no/[fingerprint])?'\n\
    -string-pass <pass>         String on which to send password response. Default: 'password:'\n\
                                (Trailing whitespace is ignored in ssh output.)\n\
    -string-login <login>       Optional string to wait on successful login. Not set by default.\n\
                                This is useful if you don't know whether ssh will prompt for a\n\
                                password, such as if there is authentication key configured in\n\
                                ~/.ssh/authorized_keys\n\
    -verbose                    Debug messages. Can be used more than once.\n\
    -h, --help                  Print this message.\n\
\n\
\n\
Example 1:\n\
    $>\n\
    $> echo 'My-p@ssW_Rd' | nopass-ssh -X -l elizabeth 10.1.23.45 'xterm'\n\
    $>\n\
    \n\
\n\
Example 2:\n\
    $>\n\
    $>\n\
    $> echo 'mY-P@55w0rD' | nopass-ssh -l root 10.1.23.45 'head -1 /etc/passwd ; exit 21 ;'\n\
    \n\
    root:x:0:0:root:/home/root:/bin/bash\n\
    $> echo $?\n\
    21\n\
    $>\n\
    $>\n\
\n\
Example 3:\n\
    $>\n\
    $> ### Show strict and proper handling of nulls and raw binary data:\n\
    $> echo 'mY-P@55w0rD' |  ./nopass-ssh -l root 10.1.23.45  'cat /bin/bash ; ' | md5sum\n\
    31fe3883149b4baae31567db4c79f30  -\n\
    $> echo 'mY-P@55w0rD' |  ./nopass-ssh -l root 10.1.23.45  'cat /bin/bash | md5sum ; '\n\
    31fe3883149b4baae31567db4c79f30  -\n\
    $>\n\
\n\
Example 4:\n\
    $>\n\
    $> echo 'My-p@ssW_Rd' | nopass-ssh -string-login Login-Ok -X -l elizabeth 10.1.23.45 'echo Login-Ok ; echo good ; '\n\
    $>\n\
    \n\
\n\
\n");
        exit(0);
    }

    for (;;) {
        int found = 0;
        for (i = 0; expectstring[i].opt; i++) {
            if (argc > 1 && !strcmp(argv[1], expectstring[i].opt)) {
                found = 1;
                argc--, argv++;
                if (argc < 1)
                    goto cmdline_error;
                expectstring[i].expstr = (const char *) strdup(argv[1]);
                argc--, argv++;
            }
        }
        if (argc > 1 && !strcmp(argv[1], "-verbose")) {
            found = 1;
            verbose++;
            argc--, argv++;
        }
        if (!found && argc > 1 && !strncmp(argv[1], "-string", 7))
            goto cmdline_error;
        if (!found)
            break;
    }

    if (argc <= 1) {
      cmdline_error:
        printf ("%s", "Try    nopass-ssh -h\n");
        exit (1);
    }

    l = read (0, the_password, sizeof (the_password) - 1);
    if (verbose)
        printf ("[VERBOSE] read password returned %d\n", l);
    if (l <= 0) {
        ERROR ("reading password from stdin", l);
        exit (1);
    }


    /* strip cr/newline which normally comes with an echo */
    while (l > 0 && (the_password[l - 1] == '\r' || the_password[l - 1] == '\n')) {
        l--;
        if (verbose)
            printf ("[VERBOSE] stripping trailing \\r or \\n character from password\n");
    }
    the_password[l] = '\0';

    a = (char **) malloc (sizeof (char *) * (argc + 2));
    a[0] = "ssh";
    for (i = 1; i < argc; i++)
        a[i] = argv[i];
    a[i] = NULL;

    child = open_under_pty (&in_fd, &out_fd, "ssh", a);

    buf_len = 0;

    for (;;) {
        const char *p;
        int buf_chunk;
        buf_chunk = read (in_fd, buf + buf_len, sizeof (buf) - 1 - buf_len);
        if (verbose >= 3) {
            if (buf_chunk > 0)
                printf ("[VERBOSE] read %d bytes, \"%.*s\"\n", buf_chunk, buf_chunk, buf + buf_len);
            else
                printf ("[VERBOSE] read returned %d, errno=%d\n", buf_chunk, errno);
        }
        if (buf_chunk <= 0) {
            if (state >= STATE_AUTH_OK) {
                pid_t r;
                int wstatus = 0, exit_code;
                r = waitpid (child, &wstatus, 0);
                if (r < 0 || !WIFEXITED (wstatus)) {
                    ERROR ("waiting for ssh process", r);
                    exit (1);
                }
                exit_code = WEXITSTATUS (wstatus);
                if (verbose >= 2)
                    printf ("[VERBOSE] exitting with code %d\n", exit_code);
                exit (exit_code);
            }
            ERROR ("reading from ssh process", buf_chunk);
            exit (1);
        }
        buf_len += buf_chunk;
        buf[buf_len] = '\0';

        if (state < STATE_GOT_NL && strstr (buf, "Permission denied")) {
            fprintf (stderr, "Permission denied\n");
            exit (1);
        }

        if (endswith (buf, expectstring[EXPSTR_YN].expstr) || endswith (buf, expectstring[EXPSTR_ALT_YN].expstr)) {
            if (verbose)
                printf ("[VERBOSE] \"%s yes\"\n", buf);
            int c;
            c = write (out_fd, "yes\r\n", 5);
            if (c <= 0) {
                ERROR ("writing yes confirmation", c);
                exit (1);
            }
            buf_len = 0;
            continue;
        }

        if (state == STATE_START && endswith (buf, expectstring[EXPSTR_PASS].expstr)) {
            if (verbose)
                printf ("[VERBOSE] \"%s xxxxxxxxx\"\n", buf);
            int c;
            c = write (out_fd, the_password, strlen (the_password));
            if (c <= 0) {
                ERROR ("writing password", c);
                exit (1);
            }
            c = write (out_fd, "\r\n", 2);
            if (c <= 0) {
                ERROR ("writing password", c);
                exit (1);
            }
            buf_len = 0;
            if (expectstring[EXPSTR_LOGIN].expstr[0]) {
                state = STATE_WAIT;
                if (verbose >= 2)
                    printf ("[VERBOSE] switching state to STATE_WAIT\n");
            } else {
                state = STATE_AUTH_OK;
                if (verbose >= 2)
                    printf ("[VERBOSE] switching state to STATE_AUTH_OK\n");
            }
            continue;
        }

#define SHIFT(n) \
    do { \
        memcpy (buf, &buf[(n)], buf_len - (n)); \
        buf_len = (buf_len - (n)); \
    } while (0)

        if (expectstring[EXPSTR_LOGIN].expstr[0] && state <= STATE_WAIT && (p = strstr (buf, expectstring[EXPSTR_LOGIN].expstr))) {
            p += strlen (expectstring[EXPSTR_LOGIN].expstr);
            SHIFT (p - buf);
            state = STATE_AUTH_OK;
            if (verbose >= 2)
                printf ("[VERBOSE] switching state to STATE_AUTH_OK\n");
        }

        /* wait for first newline after sending the password: */
        if (state == STATE_AUTH_OK && (p = strchr (buf, '\n'))) {
            SHIFT (p + 1 - buf);
            state = STATE_GOT_NL;
            if (verbose >= 2)
                printf ("[VERBOSE] got newline, switching state to STATE_GOT_NL\n");
        }

        if (state == STATE_GOT_NL) {
            int c;
            c = write_all (buf + buf_len - buf_chunk, buf_chunk);
            if (verbose >= 3) {
                if (c > 0)
                    printf ("[VERBOSE] wrote %d bytes, \"%.*s\"\n", c, c, buf + buf_len - buf_chunk);
                else
                    printf ("[VERBOSE] write returned %d, errno=%d\n", c, errno);
            }
            if (c <= 0) {
                ERROR ("writing output", c);
                exit (1);
            }
        }

#define HALF    ((int) sizeof(buf) / 2)

        if (buf_len > HALF)
            SHIFT (HALF);
    }

    return 1;
}





