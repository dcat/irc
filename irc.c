#include <sys/socket.h>
#include <arpa/inet.h>
#include <string.h>
#include <stdlib.h>
#include <unistd.h>
#include <stdarg.h>
#include <stdio.h>
#include <netdb.h>
#include <err.h>

#define LEN(X)		(sizeof(X) / sizeof(X[0]))
#define MSG_MAX		512

struct irc_callback_s {
	int n;
	char *search;
	void (*fn)(int, char buf[MSG_MAX]);
};

int irc_read(int);
int irc_send(int, const char *, ...);
int irc_dial(char *, int);
int irc_loop(int s);

/*
 * ping_cb()
 *	reply to PING
 */
void
ping_cb(int s, char buf[MSG_MAX])
{
	char pong_token[MSG_MAX];

	/*         PING :25ABF42D    *
	 *            \   /          */
	sscanf(buf, "%*s %s", pong_token);
	irc_send(s, "PONG %s", pong_token);
}

void
privmsg_cb(int s, char buf[MSG_MAX])
{
	char *msg;
	char from[MSG_MAX];
	char to[MSG_MAX];

	/* point msg to buf and skip first colon */
	msg = (buf + 1);

	/* increase pointer until ':' is found, and then once more */
	while (*msg && *msg++ != ':')
		;
	/*                         `                 *
	 *                           \               *
	 * :dcat!de@d.cat PRIVMSG bob :hey man!!!\0  *
	 *     \        \       \   \                *
	 *       `.      `.     |   |                *
	 *         `.      `.   |   |                *
	 *            `.     \   \  |                */
	sscanf(buf, ":%[^!]!%*s %*s %s", from, to);

	printf("%s said \"%s\" to %s\n", from, msg, to);
}

/*
 * connected_cb
 * 	run when MOTD end is receieved
 */
void
connected_cb(int s, char buf[MSG_MAX])
{
	/* stuff like identifying with NICKSERV goes here */
	irc_send(s, "JOIN %s", "#bots");
}

/*
 * callback table
 * 	this table tells the program where to look for what
 *
 * example message from server;
 * PING :sEN55Ens
 *   \     /
 *    0   1
 *
 *  { 0, "PING", function_to_call_on_ping }
 *
 * note: iteration through the callback table goes top to bottom, so if you 
 * for example set a variable of who sent a PRIVMSG, it cannot be used in a 
 * previous memeber of the table. (use-case: checking for admin)
 *
 */
static const struct irc_callback_s cb[] = {
	/* n  search     callback fn */
	{  0, "PING",    ping_cb        },
	{  1, "PRIVMSG", privmsg_cb     },
	{  1, "376",     connected_cb   },
};


/*
 * irc_dial:
 *	create a socket connected to the desired host and port
 *	returns -1 on error, as well as reporting a warning
 */
int
irc_dial(char *host, int port)
{
	int s;
	struct sockaddr_in dst;
	struct hostent *h;

	if ((s = socket(AF_INET, SOCK_STREAM, 0)) == -1) {
		warn("socket()");
		return -1;
	}

	if ((h = gethostbyname(host)) == NULL) {
		warn("gethostbyname()");
		return -1;
	}

	dst.sin_family = AF_INET;
	bcopy((char *)h->h_addr, (char *)&dst.sin_addr.s_addr,
			(size_t)h->h_length);
	dst.sin_port = htons(port);

	if (connect(s, (struct sockaddr *)&dst, sizeof(dst)) == -1) {
		warn("connect()");
		return -1;
	}

	return s;
}

/*
 * irc_send()
 *	formatted print to file descriptor
 *	returns 1 on error
 */
int
irc_send(int s, const char *fmt, ...)
{
	char msg[MSG_MAX];
	va_list ap;

	va_start(ap, fmt);
	vsnprintf(msg, MSG_MAX, fmt, ap);
	va_end(ap);

	return !dprintf(s, "%s\r\n", msg);
}

/*
 * irc_read()
 *	returns one read char from socket
 */
int
irc_read(int s)
{
	char chr;
	ssize_t n;
	
	n = read(s, &chr, 1);

	switch (n) {
	case -1:
		warn("read()");
		break;
	case 0:
		close(s);
		err(0, "disconnected from server");
		break;
	case 1:
		return chr;
		/* NOTREACHED */
	}

	return 0;
}

/*
 * irc_loop()
 *	loop to handle all callbacks
 */
int
irc_loop(int s)
{
	char buf[MSG_MAX];
	char tbuf[MSG_MAX]; /* tokenizer buffer */
	char *tok;
	char *ln = buf;
	int chr, cr, i, y;

	/* get irc msg loop */
	for (;;) {
		bzero(buf, MSG_MAX); /* zero out buffer */
		ln = buf;
		cr = 0;

		/* keep reading until CR and NL */
		for (;;) {
			chr = irc_read(s);

			if (chr == '\r')
				cr = 1;

			if (cr && chr == '\n') {
				*--ln = '\0';
				break;
			}

			*ln++ = (char)chr;
		}

		/* loop through callbacks */
		y = 0;
		strncpy(tbuf, buf, MSG_MAX);
		tok = strtok(tbuf, " ");
		while (tok != NULL) {
			for (i=0; i < LEN(cb); i++) {
				if (!strncmp(tok, cb[i].search, MSG_MAX)
						&& cb[i].n == y) {
					cb[i].fn(s, buf);
				}
			}

			tok = strtok(NULL, " ");
			y++;
		}
	}
}


int
main(void)
{
	int s;

	if ((s = irc_dial("irc.iotek.org", 6667)) == -1)
		return 1;

	/* server will not reply until you've sent NICK */
	irc_send(s, "NICK %s", "nickname");
	irc_send(s, "USER %s * * %s", "username", "realname");

	return irc_loop(s);
}

