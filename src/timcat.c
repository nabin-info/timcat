/* timcat reads TiMOS commands from stdin or argv, sending to hosts via ssh */
/* Copyright (C) 2013 Ryan Nabinger (see LICENSE.md) */
/* timcat comes with ABSOLUTELY NO WARRANTY */

#define _ISOC99_SOURCE
#define _XOPEN_SOURCE 600
//#define _XOPEN_SOURCE 700
//#define _POSIX_C_SOURCE 200112L
//#define _POSIX_C_SOURCE 200809L

#define RESTRICT restrict  


#define TIMCAT_EXENAME  "timcat"
#define TIMCAT_VERSION  "0.4.0"
const char timcat_version_str[] = TIMCAT_EXENAME "-" TIMCAT_VERSION;
const char timcat_source_blob[] = { 
#ifdef _INCLUDE_TIMCAT_SOURCE
#include "timcat.bin2c"
    , 0x00
#else
    "Sorry, " TIMCAT_EXENAME " source not included in this build."
#endif /* ifdef _INCLUDE_TIMCAT_SOURCE */
};

#include <stdlib.h>
#include <errno.h>


#include <stdio.h>
#define dbglog(...) fprintf(stderr, __FILE__ ":" __LINE__ ":" __func__ "(): " 
#define errlog(...) fprintf(stderr,__VA_ARGS__)
#define outlog(...) fprintf(stdout,__VA_ARGS__)
#define outflush() fflush(stdout)
#ifndef BUFSIZ
#define BUFSIZ 4096
#endif /* ifndef BUFSIZ */


#include <string.h>
typedef const char * String;
typedef struct string_list {
    String v;
    struct string_list *n;
} StringList;

static StringList *
newStringList() {
    StringList *s = (StringList *) malloc(sizeof(StringList));
    return s;
}


#include <stdarg.h>
static void
die(String fmt, ...) {
    va_list ap;
    fprintf(stderr, "FATAL: ");
    va_start(ap, fmt);
        vfprintf(stderr, fmt, ap);
    va_end(ap);
    fprintf(stderr, "\n");
    exit(EXIT_FAILURE);
}


#include <unistd.h>
#include <fcntl.h>
#include <sys/types.h>
#include <sys/time.h>

#ifdef _WIN32

#include <windows.h>
    static char *strdup(String RESTRICT s) {
        return strcpy((char *)malloc(strlen(s)+1), s);
    }

#include <io.h>
    static int fileno(FILE *f) {
        if (f == stdin)  return STDIN_FILENO;
        if (f == stdout) return STDOUT_FILENO;
        if (f == stderr) return STDERR_FILENO;
        return -1;
    }
    static HANDLE GetFdHandle(int fd) {
        switch (fd) {
        case STDIN_FILENO:  return GetStdHandle(STD_INPUT_HANDLE);
        case STDOUT_FILENO: return GetStdHandle(STD_OUTPUT_HANDLE);
        case STDERR_FILENO: return GetStdHandle(STD_ERROR_HANDLE);
        default:            return INVALID_HANDLE_VALUE;
        }
    }

#include <winsock2.h>
#include <ws2tcpip.h>
#define WINSOCK_INIT() \
            do { WSADATA wsadata; \
                 WSAStartup(MAKEWORD(2,0), &wsadata); \
            } while(0)

#else /* ifdef _WIN32 */

#include <sys/socket.h>
#include <netdb.h>
#include <termios.h>
#include <poll.h>
#define WINSOCK_INIT() 
#define closesocket(s) close(s)

#endif /* ifdef _WIN32 */

#ifndef AI_ADDRCONFIG
#define AI_ADDRCONFIG  0x0
#endif /* ifndef AI_ADDRCONFIG */

#include <libssh2.h>
static void
cleanup_libssh2(void) {
    libssh2_exit();
}

static char *
errmsg_libssh2(LIBSSH2_SESSION *sess) {
    char *msg = NULL;
    libssh2_session_last_error(sess, &msg, NULL, 0);
    return msg;
}


/* Terminal / Console Input Functions  **************************************/
#ifdef _WIN32

enum movetype { UP, DOWN, RIGHT, LEFT };


static void
MoveConsoleCursor(HANDLE hConsOut, enum movetype eMoveType, int iAmount) { 
    CONSOLE_SCREEN_BUFFER_INFO csbi;
    if (!GetConsoleScreenBufferInfo(hConsOut, &csbi))
        return;
    COORD coord = csbi.dwCursorPosition;
    
    switch (eMoveType) {
    case UP:    coord.Y -= iAmount; break;
    case DOWN:  coord.Y += iAmount; break;
    case LEFT:  coord.X -= iAmount; break;
    case RIGHT: coord.X += iAmount; break;
    default:
        die("unknown console position");
        break;
    }
    SetConsoleCursorPosition(hConsOut, coord);
}


static void
WriteConsoleColored(HANDLE hConsOut, int AnsiColor, String str, int len) {
    CONSOLE_SCREEN_BUFFER_INFO csbi;
    if (!GetConsoleScreenBufferInfo(hConsOut, &csbi))
        return;
    
    WORD attr = csbi.wAttributes;
    switch (AnsiColor) {
    case 0: attr &=  (FOREGROUND_RED | FOREGROUND_BLUE | FOREGROUND_GREEN); break;
    case 1: attr &=  (FOREGROUND_RED); break;
    case 2: attr &=  (FOREGROUND_GREEN); break;
    case 3: attr &=  (FOREGROUND_GREEN | FOREGROUND_BLUE); break;
    case 4: attr &=  (FOREGROUND_BLUE); break;
    case 5: attr &=  (FOREGROUND_RED | FOREGROUND_BLUE); break;
    case 6: attr &=  (FOREGROUND_RED | FOREGROUND_GREEN); break;
    case 7: attr &= ~(FOREGROUND_RED | FOREGROUND_BLUE | FOREGROUND_GREEN); break;
    default:
        die("unimplemented AnsiColor in WriteConsoleColored");
    }

    DWORD wc = 0;
    SetConsoleTextAttribute(hConsOut, attr);
    WriteConsole(hConsOut, str, len, &wc, NULL);
    SetConsoleTextAttribute(hConsOut, csbi.wAttributes);
}


static int
ConsoleEscSeq(HANDLE out, String buf) {
    if (buf[0] != 0x1b)
        return 0;

    int i = 1;
    if (buf[i] == '[') { 
        i++;
        int q = 0;
        while ('0' <= buf[i] && buf[i] <= '9') {
            /* read in ascii base-10, left-to-right */
            i++;
            q *= 10;
            q += buf[i-1] - '0';
        }
        switch (buf[i]) {
        case 'A': MoveConsoleCursor(out, UP,    (q)?q:1); break;
        case 'B': MoveConsoleCursor(out, DOWN,  (q)?q:1); break;
        case 'C': MoveConsoleCursor(out, RIGHT, (q)?q:1); break;
        case 'D': MoveConsoleCursor(out, LEFT,  (q)?q:1); break;
        case 'J': /* Scroll Screen */
            if (q == 2) {
                /* it's more useful to scroll down one line */
                DWORD wc = 0;
                WriteConsole(out, "\r", 1, &wc, NULL);
            }
            break;
        default:
            WriteConsoleColored(out, 2, buf, i);
            break;
        }
    }
    return i;
}

#endif /* ifdef _WIN32 */


static int saved_tty_fd = -1;
#ifdef _WIN32
DWORD saved_cons_mode = 0;
#else /* ifdef _WIN32 */
static struct termios saved_tty_term = {0};
#endif /* ifdef _WIN32 */


static void
save_term(int tty_fd) {
    if (saved_tty_fd != -1 || !isatty(tty_fd))
        return;
#ifdef _WIN32
    HANDLE tty_h = GetFdHandle(tty_fd); 
    if (   INVALID_HANDLE_VALUE == tty_h  
        || !GetConsoleMode(tty_h, &saved_cons_mode) )
        return;
#else /* ifdef _WIN32 */
    if (tcgetattr(tty_fd, &saved_tty_term) != 0) {
        errlog("WARN: failed to get tty(fd=%d) settings: %s\n"
                                     , tty_fd, strerror(errno));
        return;
    }
#endif /* ifdef _WIN32 */
    saved_tty_fd = tty_fd;
}


static void 
reset_term(void) { 
    if (saved_tty_fd == -1) 
        return;
#ifdef _WIN32
    if (!SetConsoleMode(GetFdHandle(saved_tty_fd), saved_cons_mode))
        return;
    saved_cons_mode = 0;
#else /* ifdef _WIN32 */
    if (tcsetattr(saved_tty_fd, TCSANOW, &saved_tty_term) != 0)
        return;
    memset(&saved_tty_term, 0, (sizeof saved_tty_term));
#endif /* ifdef _WIN32 */
    saved_tty_fd = -1;
}


static void 
timcat_setterm(int tty_fd) {
#ifdef _WIN32
    HANDLE tty_h = GetFdHandle(tty_fd); 
    if (tty_h == INVALID_HANDLE_VALUE)
        return;

    DWORD fdwConsoleMode = 0;
    if (!GetConsoleMode(tty_h, &fdwConsoleMode))
        return;

    fdwConsoleMode &= ~(ENABLE_LINE_INPUT);
    fdwConsoleMode &= ~(ENABLE_ECHO_INPUT);
    fdwConsoleMode &= ~(ENABLE_PROCESSED_INPUT);
    fdwConsoleMode &= ~(ENABLE_PROCESSED_OUTPUT);

    if (!SetConsoleMode(tty_h, fdwConsoleMode))
        errlog("WARN: SetConsoleMode(%d, 0x%x) failed\n" , tty_h, fdwConsoleMode);
#else 
    struct termios term;
    if (tcgetattr(tty_fd, &term) != 0)
        return;

    term.c_iflag &= ~(IMAXBEL|IGNBRK|BRKINT|PARMRK|ISTRIP|INLCR|IGNCR|ICRNL|IXON);
    term.c_oflag &= ~OPOST;
    term.c_lflag &= ~(ECHO|ECHONL|ICANON|ISIG|IEXTEN);
    term.c_cflag &= ~(CSIZE|PARENB);
    term.c_cflag |= CS8;
    term.c_cc[VMIN] = 1;
    term.c_cc[VTIME] = 0;

    if (tcsetattr(tty_fd, TCSADRAIN, &term) != 0)
        errlog("WARN: tcsetattr(%d,...) failed: %s\n", tty_fd, strerror(errno));
#endif /* ifdef _WIN32 */
}


static void
passthrough_tty(FILE *tty, int sock, LIBSSH2_CHANNEL *chan) {
    int tty_fd = fileno(tty); 
    if ((tty_fd < 0) || !isatty(tty_fd))
        die("file descriptor '%d' is not a tty.", tty_fd);

    save_term(tty_fd);
    timcat_setterm(tty_fd);

	libssh2_channel_set_blocking(chan, 0);

#ifdef _WIN32
    HANDLE tty_h = GetFdHandle(tty_fd);
    HANDLE tty_o = GetFdHandle(STDOUT_FILENO);
    if (tty_h == INVALID_HANDLE_VALUE)
        return;

    u_long ulTrue = 1;
    ioctlsocket(sock, FIONBIO, &ulTrue);

    WSAEVENT hSockEvent = CreateEvent(NULL, TRUE, FALSE, NULL);
    if (WSAEventSelect(sock, hSockEvent, FD_READ) == SOCKET_ERROR)
        die("WSAEventSelect(): %d", WSAGetLastError());

    char buf[BUFSIZ]; buf[0] = '\0';

    int rc;
    for (;;) {
        INPUT_RECORD ir[128];
        DWORD n;
        HANDLE hEvents[2] = {hSockEvent, tty_h};
        DWORD dwRes = WaitForMultipleObjects(2, hEvents, FALSE, 500);

        switch(dwRes) {
        /* keyboard event */
        case WAIT_OBJECT_0 + 1:
            if (!ReadConsoleInput(tty_h, ir, 128, &n))
                die("ReadConsoleInput()");

            DWORD len = 0;
            for (DWORD i = 0; i < n; i++) {
                if (   ir[i].EventType == KEY_EVENT 
                    && ir[i].Event.KeyEvent.bKeyDown ) {

                    //WriteConsoleColored(tty_o, 3, &c, 1); /* local-echo */
                    
                    DWORD m = ir[i].Event.KeyEvent.dwControlKeyState;
                    if (   (m &  LEFT_ALT_PRESSED) ==  LEFT_ALT_PRESSED
                        || (m & RIGHT_ALT_PRESSED) == RIGHT_ALT_PRESSED ) {
                        buf[len++] = 0x1b;  /* use-meta */
                    }

                    char  c = ir[i].Event.KeyEvent.uChar.AsciiChar;
                    if (0x19 < c && c < 0x80) { 
                        buf[len++] = c;
                    } else 
                    if (0x00 < c && c < 0x20) {
                        switch (0x40 + c) {
                        case ']':
                        case 'D': goto ResetTerminal;
                        default: 
                            break;
                        }
                        buf[len++] = c;
                    } else
                    switch (ir[i].Event.KeyEvent.wVirtualKeyCode) {
                    case VK_LEFT:   buf[len++] = 'B'-0x40; break;
                    case VK_RIGHT:  buf[len++] = 'F'-0x40; break;
                    case VK_END:    buf[len++] = 'E'-0x40; break;
                    case VK_HOME:   buf[len++] = 'A'-0x40; break;
                    case VK_UP:     buf[len++] = 'P'-0x40; break;
                    case VK_DOWN:   buf[len++] = 'N'-0x40; break;
                    case VK_DELETE: buf[len++] = 'F'-0x40;
                                    buf[len++] = 'H'-0x40; break;
                    default: 
                        break;
                    }
                }
            }
            buf[len] = '\0';

            int written = 0;
            do {
                rc = libssh2_channel_write(chan, buf, strlen(buf));
                written += (rc > 0)? rc : 0;
            } while (LIBSSH2_ERROR_EAGAIN != rc && rc > 0
                     && written != strlen(buf));
            break;
        
        /* socket event */
        case WAIT_OBJECT_0 + 0:
            do {
                buf[0] = '\0';
                rc = libssh2_channel_read(chan, buf, (sizeof buf)-2);
                if (rc > 0) {
                    buf[rc] = '\0';
                    DWORD nc = strlen(buf);
                    /* process output */
                    for (int x = 0; x < nc; x++) {
                        /* Escape Sequences */
                        if (buf[x] == 0x1b) {
                            x += ConsoleEscSeq(tty_o, &buf[x]);
                            continue;
                        }
                        /* Normal Characters */
                        DWORD wc = 0;
                        if ((0x19 < buf[x] && buf[x] < 0x7f)
                                || buf[x] == '\r'
                                || buf[x] == '\n'
                                || buf[x] == '\a') {
                            WriteConsole(tty_o, &buf[x], 1, &wc, NULL);
                            continue;
                        }
                        /* Unknown Characters */
                        char o[10] = {0};
                        sprintf(o, "\\x%02x", buf[x]);
                        WriteConsoleColored(tty_o, 2, o, strlen(o));
                    }
                }
            } while (LIBSSH2_ERROR_EAGAIN != rc && rc > 0);
            ResetEvent(hSockEvent);
            break;

        case WAIT_TIMEOUT:
            break;                       

        default:
            die("unknown WaitForMultipleObjects() result.");
        }
        if (libssh2_channel_eof(chan))
            break;
    }

    ResetTerminal:
    WSAEventSelect(sock, hSockEvent, 0);
    u_long ulFalse = 0;
    ioctlsocket(sock, FIONBIO, &ulFalse);

#else /* ifdef _WIN32 */ 

    int tty_fl = fcntl(tty_fd, F_GETFL);
    fcntl(tty_fd, F_SETFL, tty_fl | O_NONBLOCK);

    int sock_fl = fcntl(sock, F_GETFL);
    fcntl(sock, F_SETFL, sock_fl | O_NONBLOCK);

	const char numfds = 2;
	struct pollfd pfds[numfds];
    memset(pfds, 0, sizeof(struct pollfd) * numfds);
    for (;;) {
        if (libssh2_channel_eof(chan))
            break;

        /* poll until socket or stdin ready for reading */
        pfds[0].fd = sock;   pfds[0].events = POLLIN; pfds[0].revents = 0; 
        pfds[1].fd = tty_fd; pfds[1].events = POLLIN; pfds[1].revents = 0; 

        int rc = poll(pfds, numfds, -1);
        if (rc == -1) {
            errlog("ERROR: polling tty and socket: %s\n", strerror(errno));
            break;
        }

        /* process socket input */
        char buf[BUFSIZ]; buf[0] = '\0';
        if (pfds[0].revents & POLLIN) {
            do {
                buf[0] = '\0';
                rc = libssh2_channel_read(chan, buf, (sizeof buf)-2);
                if (rc > 0) {
                    buf[rc] = '\0';
                    outlog("%s", buf); 
                    outflush();
                }
            } while (LIBSSH2_ERROR_EAGAIN != rc && rc > 0);
        }
        if (rc < 0 && LIBSSH2_ERROR_EAGAIN != rc)
            errlog("ERROR: channel_read(): %s\n", "TODO: channel_session()");
            //errlog("ERROR: channel_read(): %s\n", errmsg_libssh2(*(chan->session)));
        if (libssh2_channel_eof(chan))
            break;

        /* process tty input */
        if (pfds[1].revents & POLLIN) {
            buf[0] = '\0';
            fgets(buf, (sizeof buf)-2, tty);

            /* allow user to break out with ^[ and ^D */
            if (   strchr(buf, (int)0x04) != NULL 
                || strchr(buf, (int)0x1D) != NULL ) {
                break;
            }

            int written = 0;
            do {
                rc = libssh2_channel_write(chan, buf, strlen(buf));
                written += (rc > 0)? rc : 0;
            } while (LIBSSH2_ERROR_EAGAIN != rc && rc > 0
                               && written != strlen(buf));
        }
        if (libssh2_channel_eof(chan))
            break;
        if (LIBSSH2_ERROR_EAGAIN != rc && rc < 0) {
            errlog("ERROR: libssh2_channel_write (error code == %d)\n", rc);
            break;
        }
    }
    
    fcntl(tty_fd, F_SETFL, tty_fl);
    fcntl(sock, F_SETFL, sock_fl);
#endif /* ifdef _WIN32 */

    reset_term();
	libssh2_channel_set_blocking(chan, 1);
}
/****************************************************************************/


/* Network / SSH Functions  *************************************************/

static int
connect_nonblock(int sfd, struct addrinfo *ai) {
#ifdef _WIN32
    return connect(sfd, ai->ai_addr, ai->ai_addrlen);
#else
    int sfd_fl = fcntl(sfd, F_GETFL, NULL);
    if (sfd_fl >= 0)
        fcntl(sfd, F_SETFL, sfd_fl |= O_NONBLOCK);

    int r;
    for (int times = 0; times < 50; times++) {
        r = connect(sfd, ai->ai_addr, ai->ai_addrlen);
        if (r < 0) {
            if (errno == EINPROGRESS ||
                errno == EALREADY    ) {
                if (times < 2) 
                     usleep(100000);
                else usleep(200000);
                continue;
            }
        }
        break;
    }

    if (sfd_fl >= 0)
        fcntl(sfd, F_SETFL, sfd_fl);
    return r;
#endif /* ifdef _WIN32 */
}


static int
timcat_socket(String RESTRICT host, String RESTRICT port) {
    struct addrinfo hints = {
                .ai_family = AF_UNSPEC,
                .ai_socktype = SOCK_STREAM,
                .ai_flags = AI_ADDRCONFIG,
                .ai_protocol = 0, 
                .ai_canonname = NULL, 
                .ai_addr = NULL, 
                .ai_next = NULL
            };

    struct addrinfo *res = NULL; 
    if (getaddrinfo(host, port, &hints, &res) != 0) {
        errlog("INFO: could not resolve host: %s\n", host);
        return -1;
    }

    char buf[BUFSIZ]; buf[0] = '\0';
    struct addrinfo *rp = NULL;
    int sfd = -1;
    for (rp = res; rp != NULL; rp = rp->ai_next) {
        getnameinfo(rp->ai_addr, rp->ai_addrlen, 
                            buf, (sizeof buf)-2, NULL, 0, NI_NUMERICHOST);
        outlog("INFO: connecting to %s (%s) on port %s\n"
                                  , host, buf, port); 
        outflush();

        sfd = socket(rp->ai_family, rp->ai_socktype, rp->ai_protocol);
        if (sfd < 0) 
            continue;

        if (connect_nonblock(sfd, rp) >= 0) {
            break; /* connected */
        }

        closesocket(sfd); sfd = -1;
    }
    freeaddrinfo(res);

    if (sfd < 0)
        errlog("ERROR: could not connect to %s\n", host);
    return sfd;
}


static LIBSSH2_SESSION *
timcat_session(int sockfd, String RESTRICT user, String RESTRICT pass) {
    LIBSSH2_SESSION *sess = libssh2_session_init();
    if (sess == NULL) {
        errlog("ERROR: sesson_init()\n");
        return NULL; 
    }
    if (    libssh2_session_handshake(sess, sockfd)
        ||  libssh2_userauth_password(sess, user, pass)
        || !libssh2_userauth_authenticated(sess) ) 
    {
        errlog("ERROR: %s\n", errmsg_libssh2(sess));
        return NULL;
    }
    return sess;
}


static LIBSSH2_CHANNEL *
timcat_channel(LIBSSH2_SESSION *sess) {
    LIBSSH2_CHANNEL *chan;
    if ((chan = libssh2_channel_open_session(sess)) == NULL) {
        errlog("ERROR: libssh2_channel_open_session()\n");
        return NULL; 
    }
    if (libssh2_channel_handle_extended_data2(chan, 
                    LIBSSH2_CHANNEL_EXTENDED_DATA_MERGE)) {
        errlog("WARNING: unable to combine channel data streams\n");
    }
    /* TiMOS ignores the TERM variable anyways ... */
    if (libssh2_channel_request_pty(chan, "vt100")) {
        errlog("ERROR: libssh2_channel_request_pty()\n");
        return NULL; 
    }
    if (libssh2_channel_shell(chan)) {
        errlog("ERROR: libssh2_channel_shell()\n");
        return NULL; 
    }
    return chan;
}
/****************************************************************************/


/* TiMOS / timcat Specific  *************************************************/

typedef struct timcat_opts {
    char *user, *pass, *port;
    FILE *in, *out, *err;
    StringList *cmdlist, *hostlist, *cmdfilelist; 
    char *outpfx;
    int libssh2_timeout;
    int noop;
} TimcatOpts;


static int
has_timos_prompt(char *RESTRICT buf, int buflen) {
    int i;

    for (i = 0; buf[i] != '\0'; i++)
        if (i == buflen) return 0;
    if (i < 6) return 0;

    /* check the end of data */
    if (!((buf[i-2] == '#' || buf[i-2] == '$') && 
           buf[i-1] == ' ')) return 0;

    /* find the last line */
    for (; buf[i] != '\n'; i--)
        if (i < 1) return 0;
    i++;

    /* ignore the '*' prompt prefix (not-saved indicator) */
    if (buf[i] == '*') i++;

    /* make sure the line started with "A:" or "B:" */
    if (!(buf[i+0] == 'A' ||
          buf[i+0] == 'B')) return 0;
    if (!(buf[i+1] == ':')) return 0;
    
    return 1;
}


static int
timcat_cmd(LIBSSH2_CHANNEL *chan, String RESTRICT cmd) {
    int q = 0;
    int cmdlen = strlen(cmd);

    /* trim whitespace from head */
    int nhead = 0;
    for (; nhead <= cmdlen; nhead++) {
        if (   cmd[nhead] == ' '
            || cmd[nhead] == '\t') 
            continue;
        break;
    }
    /* "@"-prefix is operate "quietly" */
    if (cmd[nhead] == '@') {
        nhead += 1;
        q = 1;
    }
    /* offset the head */
    cmdlen -= (nhead > 0)? nhead : 0;

    /* str_trim_right(cmd, "[\n\r\0]") */
    int ntail = 0;
    for (; ntail <= cmdlen; ntail++) {
        if (cmd[nhead + cmdlen - ntail] == '\n' || 
            cmd[nhead + cmdlen - ntail] == '\r' ||
            cmd[nhead + cmdlen - ntail] == '\0'  )
            continue;    
        break;
    }
    /* offset the tail */
    cmdlen -= (ntail > 0)? ntail : 0;

    if (cmdlen < 1 && libssh2_channel_eof(chan))
        return -1;

    int tx_total = 0, tx_incr = 0;
    while (tx_total < cmdlen) {
        if (libssh2_channel_eof(chan)) {
            errlog("ERROR: remote disconnect before we sent '{%s}'", cmd);
            return -2;
        }
        tx_incr = libssh2_channel_write(chan, &cmd[tx_total + nhead], 
                                              cmdlen - tx_total + 1);
        tx_total += (tx_incr > 0)? tx_incr : 0;
    }
    do {
        tx_incr = libssh2_channel_write(chan, "\r", 1);
        tx_total += (tx_incr > 0)? tx_incr : 0;
    } while (tx_incr < 1);

    /* read the response */
    char rx_buf[BUFSIZ]; rx_buf[0] = '\0';
    int rx_total = 0, rx_incr = 0;
    while (libssh2_channel_eof(chan) != 1) {
        if (rx_total >= (sizeof rx_buf)-1)
            rx_total = 0;
        rx_incr = libssh2_channel_read(chan, &rx_buf[rx_total], 
                                    (sizeof rx_buf)-1 - rx_total);
        if (rx_incr > 0) {
            rx_total += rx_incr;
            rx_buf[rx_total] = '\0';
            if (!q) {
                outlog("%s", &rx_buf[rx_total - rx_incr]); 
                outflush();
            }
            if (has_timos_prompt(rx_buf, (sizeof rx_buf)))
                break;
        } else {
            if ((rx_incr != 0) &&
                (rx_incr != LIBSSH2_ERROR_EAGAIN)) {
                errlog("ERROR: read error waiting for prompt\n");
                return -1;
            }
        }
    }
    return 0;
}


int 
timcat_logout(LIBSSH2_CHANNEL *chan) {

    libssh2_channel_set_blocking(chan,0);

    outlog("\n\n"); outflush();

    char buf[BUFSIZ]; buf[0] = '\0';
    while (libssh2_channel_eof(chan) == 0) {
        int rc = libssh2_channel_send_eof(chan);
        if (rc == LIBSSH2_ERROR_SOCKET_SEND)
            break;

        do {
            buf[0] = '\0';
            rc = libssh2_channel_read(chan, buf, (sizeof buf)-2);
            if (rc > 0) {
                buf[rc] = '\0';
                outlog("%s", buf); 
                outflush();
            }
        } while (LIBSSH2_ERROR_EAGAIN != rc && rc > 0);
        if (rc != LIBSSH2_ERROR_EAGAIN)
            break;

        /* keep trying to flush and close the connection */
        libssh2_channel_flush(chan);
        libssh2_channel_close(chan);
    }

    libssh2_channel_close(chan);
    libssh2_channel_free(chan);
    return 0;
}


void 
timcat_print_version(void) { outlog( "%s\n"
    "Copyright (C) 2013-2014 Ryan Nabinger\n"
    "%s comes with ABSOLUTELY NO WARRANTY\n"
    , timcat_version_str, TIMCAT_EXENAME); 
}


void 
timcat_print_source(void) { 
    outlog("%s\n", timcat_source_blob); 
}


int
timcat_explain_opts(TimcatOpts *tcop) {
    StringList *slp_h, *slp_c, *slp_f;
    FILE *ifp;
    int cmd_i = 1, sockfd = -1;
    char buf[BUFSIZ]; buf[0] = '\0';

    outlog("Explaining My Intentions ...\n");
    if (tcop->noop == 1)
        outlog(" +  Dry Run Mode Enabled\n");

    if (tcop->outpfx != NULL)
        outlog(" +  Writing cli sessions to files \"%s-<host>.log\"\n", tcop->outpfx);
    
    outlog(" +  Hostlist:\n"); 
    outlog("    [timeout=%d  port=\"%s\"  user=\"%s\"  pass=\"%s\"]\n",
                tcop->libssh2_timeout, tcop->port, tcop->user, "****");
    for (slp_h = tcop->hostlist; slp_h != NULL; slp_h = slp_h->n) {
        outlog("    %-20s => ", slp_h->v); 
        outflush();
        sockfd = timcat_socket(slp_h->v, tcop->port);
        if (sockfd != -1)
            closesocket(sockfd);
    }
    outlog("\n");

    if (tcop->cmdlist != NULL) {
        outlog(" +  Commands specified as arguments on command line\n");
        for (slp_c = tcop->cmdlist; slp_c != NULL; slp_c = slp_c->n) {
            outlog("%8d) %s\n", cmd_i++, slp_c->v);
        }
        outlog("\n");
    }

    if (tcop->cmdfilelist != NULL) {
        outlog(" +  Reading commands from -f <file>s on the command line\n");
        for (slp_f = tcop->cmdfilelist; slp_f != NULL; slp_f = slp_f->n) {
            outlog("    [%s] ", slp_f->v);
            if ((ifp = fopen(slp_f->v, "rb")) == NULL) {
                outlog(" !! could not open !!\n");
                continue;
            }
            outlog("  (successfully opened)\n");
            while (fgets(buf, (sizeof buf)-1, ifp) != NULL)
                outlog("%8d) %s", cmd_i++, buf);
            outlog("\n"); /* in case file does not end with \n */
            if (fclose(ifp) == EOF)
                outlog(" WARN: problem closing file; continuing anyway.\n");
        }
    }

    if (tcop->in != NULL) {
        if (tcop->in == stdin)
             outlog(" +  Reading commands from standard input\n");
        else outlog("    ?? input not coming from stdin ??\n");
        outlog("    [stdin]  (skipping...)\n");
        outlog("\n");
    }

    return 0;
}


int 
timcat(TimcatOpts *tcop) {
    char buf[BUFSIZ]; buf[0] = '\0';
    int readlen = 0;
    FILE *ifp;
    char *ofname = NULL;
    StringList *slp_h, *slp_c, *slp_f;

    int ssh2sock = -1;
    LIBSSH2_SESSION *ssh2sess = NULL;
    LIBSSH2_CHANNEL *ssh2chan = NULL;

    if (tcop->noop == 1) {
        timcat_explain_opts(tcop);
        exit(EXIT_SUCCESS);
    }

    /* Loop through each host (-H) */
    for (slp_h = tcop->hostlist; slp_h != NULL; slp_h = slp_h->n) {
        if (slp_h->v == NULL) {
            errlog("ERROR: skipping NULL hostname. (BUG?)\n");
            goto NextHost;
        }

        /* redirect stdout to per-host log (-o) */
        if (tcop->outpfx != NULL) {
            ofname = (char *) malloc(20 + strlen(tcop->outpfx) + strlen(slp_h->v));
            if (ofname == NULL)
                die("OOM generating output filename");

            sprintf(ofname, "%s%s.log", tcop->outpfx, slp_h->v);
            tcop->out = freopen(ofname, "wb", stdout);
            if (tcop->out == NULL)
                die("could not open '%s': %s", ofname , strerror(errno));
            errlog("INFO: logging '%s' session to '%s'\n", slp_h->v, ofname);
        }

        if (( ssh2sock = timcat_socket(slp_h->v, tcop->port) )== -1) {
            errlog("ERROR: could not connect to host\n");
            goto NextHost;
        }
        if (( ssh2sess = timcat_session(ssh2sock, tcop->user, tcop->pass) )== NULL) {
            errlog("ERROR: could not establish ssh2 session\n");
            goto NextHost;
        }
        if (( ssh2chan = timcat_channel(ssh2sess) )== NULL) {
            errlog("ERROR: could not establish ssh2 channel\n");
            goto NextHost;
        }

        libssh2_session_set_timeout(ssh2sess, tcop->libssh2_timeout);
        libssh2_channel_set_blocking(ssh2chan, 1);

        /* init TiMOS environment for predictability */
        readlen = libssh2_channel_read(ssh2chan, buf, (sizeof buf)-1);
        if (readlen < 0) {
            errlog("ERROR: could not find initial prompt\n");
            goto NextHost;
        }
#define tc(c)  if (timcat_cmd(ssh2chan,c) < 0) goto NextHost;
        tc("@/environment no more");
        tc("@/environment no create");
        tc("@/environment no time-display");
        tc("@/environment saved-ind-prompt");
        tc("@/environment reduced-prompt");
        tc("");
#undef tc

        /* commands from argv */
        if (tcop->cmdlist != NULL) {
            for (slp_c = tcop->cmdlist; slp_c != NULL; slp_c = slp_c->n) {
                if (slp_c->v == NULL) {
                    errlog("ERROR: attempt to execute NULL command on cmdline. (BUG?)");
                    continue;
                }

                if (timcat_cmd(ssh2chan, slp_c->v) < 0) 
                    continue;
            }
        }

        /* commands from files */
        if (tcop->cmdfilelist != NULL) {
            for (slp_f = tcop->cmdfilelist; slp_f != NULL; slp_f = slp_f->n) {
                if (slp_f->v == NULL) {
                    errlog("ERROR: attempting to open a NULL command file. (BUG?)\n");
                    continue;
                }
                if ((ifp = fopen(slp_f->v, "rb")) == NULL) {
                    errlog("WARN: problem opening '%s': %s\n", slp_f->v, strerror(errno));
                    continue;
                }
                while (fgets(buf, (sizeof buf)-2, ifp) != NULL) {
                    if (timcat_cmd(ssh2chan, buf) < 0) {
                        errlog("ERROR:  failed to write cmd == \"%s\"\n", buf);
                        break;
                    }
                }
                if (fclose(ifp) == EOF)
                    errlog("WARN: problem closing '%s': %s\n", slp_f->v, strerror(errno));
                ifp = NULL;
            }
        }

        /* commands from stdin */
        if (tcop->in != NULL) {
            if (isatty(fileno(tcop->in))) {
                errlog("INFO: expecting commands from standard input (your terminal)");
                timcat_cmd(ssh2chan, " "); 
                passthrough_tty(tcop->in, ssh2sock, ssh2chan);

            } else {
                while (fgets(buf, (sizeof buf)-2, tcop->in) != NULL) {
                    if (   (timcat_cmd(ssh2chan, buf) < 0)
                        && (libssh2_channel_eof(ssh2chan)== 1) )
                        goto NextHost;
                }
            }
        }

        NextHost:
        free(ofname); ofname = NULL;
        if (tcop->out != stdout) {
            if (fclose(tcop->out) == EOF)
                errlog("WARN: problem closing host log: %s\n", strerror(errno));
        }
        if (ssh2chan != NULL) {
            timcat_logout(ssh2chan);
            ssh2chan = NULL;
        }
        if (ssh2sess != NULL) {
            libssh2_session_disconnect(ssh2sess, "Normal Disconnect");
            libssh2_session_free(ssh2sess);
            ssh2sess = NULL; 
        }
        if (ssh2sock != -1) {
            closesocket(ssh2sock);
            ssh2sock = -1;
        }
    }
    return 0;
}
/****************************************************************************/

void
timcat_usage(void) {
    outlog(
    "Usage:  %s [options] -H <host> [--] [command_1] ... [command_N]\n"
    "                where [command_*] are TiMOS cli commands to send.\n"
    "Options:\n"
    "   -V           Show version.\n"
    "   -S           Show the source code for %s.\n"
    "   -h           Show this help message.\n"
    "   -H <host>    TiMOS host to command (with ssh v2).   (multiple allowed)\n"
    "   -t <msecs>   Timeout for ssh data, in milliseconds. (default 10000)\n"
    "   -U <user>    Login to TiMOS hosts as <user>.        (default \"admin\")\n"
    "   -P <pass>    Login to TiMOS hosts with <pass>.      (default \"admin\")\n"
    "   -o <prefix>  Write cli session to files named \"<prefix><host>.log\"\n"
    "   -n           Dry run, only explain what you plan to do.\n"
    "\nCommands are sent in order listed from [command_*], all -f files, then -s.\n"
    "   -f <file>    Read TiMOS cli commands from <file>.   (multiple allowed)\n"
    "   -s           Read TiMOS cli commands from standard input.\n"
    , TIMCAT_EXENAME, TIMCAT_EXENAME); 
}


int 
main(int argc, char *argv[]) {
    extern char *optarg;
    extern int optind, optopt, opterr;
    StringList *slp_c = NULL, *slp_h = NULL, *slp_f = NULL;

    TimcatOpts tc_opts = {
                    .in = NULL, .out = stdout, .err = stderr,
                    .outpfx = NULL,
                    .cmdfilelist = NULL,
                    .libssh2_timeout = 10000,
                    .user = "admin", .pass = "admin", .port = "22",
                    .hostlist = NULL, .cmdlist = NULL,
                    .noop = 0,
                };

    int c = 0;
    opterr = 0;
    while ((c = getopt(argc, argv, ":VShnst:f:U:P:H:o:")) != -1) {
        switch (c) {
        case 'V': timcat_print_version(); exit(EXIT_SUCCESS);
        case 'S': timcat_print_source(); exit(EXIT_SUCCESS);
        case 'h': timcat_usage(); exit(EXIT_SUCCESS);
        case 'n': tc_opts.noop = 1; break;
        case 's': tc_opts.in = stdin; break;
        case 't': tc_opts.libssh2_timeout = strtol(optarg, NULL, 30); break;
        case 'U': tc_opts.user = strdup(optarg); break;
        case 'P': tc_opts.pass = strdup(optarg); break;
        case 'H': 
                  if (tc_opts.hostlist == NULL) 
                       slp_h = tc_opts.hostlist = newStringList();
                  else slp_h = slp_h->n = newStringList();
                  slp_h->v = strdup(optarg);
                  slp_h->n = NULL; break;
        case 'f': 
                  if (tc_opts.cmdfilelist == NULL) 
                       slp_f = tc_opts.cmdfilelist = newStringList();
                  else slp_f = slp_f->n = newStringList();
                  slp_f->v = strdup(optarg);
                  slp_f->n = NULL; break;
        case 'o': tc_opts.outpfx = strdup(optarg); break;
        case 'e': die("option -%c currently not implemented.\n", c);
        case ':': die("option -%c requires an argument.", optopt);
        case '?': die("option -%c is not known.", optopt);
        default:  die("getopt() returned char code 0x%x (%c).", c, c); 
        }    
    }

    if (tc_opts.hostlist == NULL) {
        timcat_usage(); outlog("\n"); outflush();
        die("no hosts were specified.");
    }

    if (   tc_opts.in == NULL
        && tc_opts.cmdfilelist == NULL 
        && (argc - optind) < 1) {
        timcat_usage(); outlog("\n"); outflush();
        die("no commands were specified.");
    }

    /* setup the command list */
    for (int i = optind; i < argc; i++) {
        if (tc_opts.cmdlist == NULL) 
             slp_c = tc_opts.cmdlist = newStringList();
        else slp_c = slp_c->n = newStringList();
        slp_c->v = strdup(&argv[i][0]);
        slp_c->n = NULL;
    }

    WINSOCK_INIT();
    if (libssh2_init(0) != 0)
        die("could not init libssh2");

    if (   atexit(cleanup_libssh2)
        || atexit(reset_term))
        die("could not register cleanup functions");

    timcat(&tc_opts);

    exit(EXIT_SUCCESS);
}

/* vim: ai et sw=4 ts=4 fdm=syntax fml=1: */
