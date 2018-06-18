/* translog_server
	  by james@ustc.edu.cn 2018.06.04
*/

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>
#include <fcntl.h>
#include <unistd.h>
#include <syslog.h>
#include <signal.h>
#include <sys/ioctl.h>
#include <sys/time.h>
#include <sys/stat.h>
#include <sys/wait.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <time.h>
#include <pwd.h>
#include <net/if.h>
#include <netinet/ip.h>
#include <netinet/ip6.h>
#include <netinet/tcp.h>
#include <netdb.h>
#include <stdarg.h>
#include <errno.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <pthread.h>

#define MAXLEN 		16384
#define MAXLINE 	1024*1024

int daemon_proc;		/* set nonzero by daemon_init() */
int debug = 0;

int my_port;
char config_file[MAXLEN];
char work_user[MAXLEN];
int work_uid;

void err_doit(int errnoflag, int level, const char *fmt, va_list ap)
{
	int errno_save, n;
	char buf[MAXLEN];

	errno_save = errno;	/* value caller might want printed */
	vsnprintf(buf, sizeof(buf), fmt, ap);	/* this is safe */
	n = strlen(buf);
	if (errnoflag)
		snprintf(buf + n, sizeof(buf) - n, ": %s", strerror(errno_save));
	strcat(buf, "\n");

	if (daemon_proc) {
		syslog(level, "%s", buf);
	} else {
		fflush(stdout);	/* in case stdout and stderr are the same */
		fputs(buf, stderr);
		fflush(stderr);
	}
	return;
}

void err_msg(const char *fmt, ...)
{
	va_list ap;
	va_start(ap, fmt);
	err_doit(0, LOG_INFO, fmt, ap);
	va_end(ap);
	return;
}

void Debug(const char *fmt, ...)
{
	va_list ap;
	if (debug) {
		va_start(ap, fmt);
		err_doit(0, LOG_INFO, fmt, ap);
		va_end(ap);
	}
	return;
}

void err_quit(const char *fmt, ...)
{
	va_list ap;
	va_start(ap, fmt);
	err_doit(0, LOG_ERR, fmt, ap);
	va_end(ap);
	exit(1);
}

void err_sys(const char *fmt, ...)
{
	va_list ap;
	va_start(ap, fmt);
	err_doit(1, LOG_ERR, fmt, ap);
	va_end(ap);
	exit(1);
}

void daemon_init(const char *pname, int facility)
{
	int i;
	pid_t pid;
	if ((pid = fork()) != 0)
		exit(0);	/* parent terminates */

	/* 41st child continues */
	setsid();		/* become session leader */

	signal(SIGHUP, SIG_IGN);
	signal(SIGCHLD, SIG_IGN);
	if ((pid = fork()) != 0)
		exit(0);	/* 1st child terminates */

	/* 42nd child continues */
	daemon_proc = 1;	/* for our err_XXX() functions */

	umask(0);		/* clear our file mode creation mask */

	for (i = 0; i < 10; i++)
		close(i);

	openlog(pname, LOG_PID, facility);
}

ssize_t				/* Read "n" bytes from a descriptor. */
readn(int fd, void *vptr, size_t n)
{
	size_t nleft;
	ssize_t nread;
	char *ptr;

	ptr = vptr;
	nleft = n;
	while (nleft > 0) {
		if ((nread = read(fd, ptr, nleft)) < 0) {
			if (errno == EINTR)
				nread = 0;	/* and call read() again */
			else
				return (-1);
		} else if (nread == 0)
			break;	/* EOF */

		nleft -= nread;
		ptr += nread;
	}
	return (n - nleft);	/* return >= 0 */
}

/* end readn */

ssize_t Readn(int fd, void *ptr, size_t nbytes)
{
	ssize_t n;

	if ((n = readn(fd, ptr, nbytes)) < 0)
		err_sys("readn error");
	return (n);
}

ssize_t readline(int fd, void *vptr, size_t maxlen)
{
	int n, rc;
	char c, *ptr;

	ptr = vptr;
	for (n = 1; n < maxlen; n++) {
		if ((rc = readn(fd, &c, 1)) == 1) {
			*ptr++ = c;
			if (c == '\n')
				break;	/* newline is stored, like fgets() */
		} else if (rc == 0) {
			if (n == 1)
				return (0);	/* EOF, no data read */
			else
				break;	/* EOF, some data was read */
		} else
			return (-1);	/* error, errno set by read() */
	}

	*ptr = 0;		/* null terminate like fgets() */
	return (n);
}

ssize_t Readline(int fd, void *ptr, size_t maxlen)
{
	ssize_t n;

	if ((n = readline(fd, ptr, maxlen)) < 0)
		err_sys("readline error");
	return (n);
}

/* include writen */
ssize_t				/* Write "n" bytes to a descriptor. */
writen(int fd, const void *vptr, size_t n)
{
	size_t nleft;
	ssize_t nwritten;
	const char *ptr;

	ptr = vptr;
	nleft = n;
	while (nleft > 0) {
		if ((nwritten = write(fd, ptr, nleft)) <= 0) {
			if (errno == EINTR)
				nwritten = 0;	/* and call write() again */
			else
				return (-1);	/* error */
		}

		nleft -= nwritten;
		ptr += nwritten;
	}
	return (n);
}

/* end writen */

void Writen(int fd, void *ptr, size_t nbytes)
{
	if (writen(fd, ptr, nbytes) != nbytes)
		err_sys("writen error");
}

char *stamp(void)
{
	static char st_buf[200];
	struct timeval tv;
	struct timezone tz;
	struct tm *tm;

	gettimeofday(&tv, &tz);
	tm = localtime(&tv.tv_sec);

	snprintf(st_buf, 200, "%02d%02d %02d:%02d:%02d.%06ld", tm->tm_mon + 1, tm->tm_mday,
		 tm->tm_hour, tm->tm_min, tm->tm_sec, tv.tv_usec);
	return st_buf;
}

void set_socket_keepalive(int fd)
{
	int keepalive = 1;	// 开启keepalive属性
	int keepidle = 5;	// 如该连接在60秒内没有任何数据往来,则进行探测
	int keepinterval = 5;	// 探测时发包的时间间隔为5 秒
	int keepcount = 3;	// 探测尝试的次数。如果第1次探测包就收到响应了,则后2次的不再发
	setsockopt(fd, SOL_SOCKET, SO_KEEPALIVE, (void *)&keepalive, sizeof(keepalive));
	setsockopt(fd, SOL_TCP, TCP_KEEPIDLE, (void *)&keepidle, sizeof(keepidle));
	setsockopt(fd, SOL_TCP, TCP_KEEPINTVL, (void *)&keepinterval, sizeof(keepinterval));
	setsockopt(fd, SOL_TCP, TCP_KEEPCNT, (void *)&keepcount, sizeof(keepcount));
}

int bind_and_listen(void)
{
	int listenfd;
	int enable = 1;
	int ipv6 = 0;

	if (ipv6)
		listenfd = socket(AF_INET6, SOCK_STREAM, 0);
	else
		listenfd = socket(AF_INET, SOCK_STREAM, 0);
	if (listenfd < 0) {
		perror("error: socket");
		exit(-1);
	}
	if (setsockopt(listenfd, SOL_SOCKET, SO_REUSEADDR, &enable, sizeof(int)) < 0) {
		perror("error: setsockopt(SO_REUSEADDR)");
		exit(-1);
	}
	if (ipv6) {
		static struct sockaddr_in6 serv_addr6;
		memset(&serv_addr6, 0, sizeof(serv_addr6));
		serv_addr6.sin6_family = AF_INET6;
		serv_addr6.sin6_port = htons(my_port);
		if (bind(listenfd, (struct sockaddr *)&serv_addr6, sizeof(serv_addr6)) < 0) {
			perror("error: bind");
			exit(-1);
		}
	} else {
		static struct sockaddr_in serv_addr;
		serv_addr.sin_family = AF_INET;
		serv_addr.sin_addr.s_addr = htonl(INADDR_ANY);
		serv_addr.sin_port = htons(my_port);
		if (bind(listenfd, (struct sockaddr *)&serv_addr, sizeof(serv_addr)) < 0) {
			perror("error: bind");
			exit(-1);
		}
	}
	if (listen(listenfd, 64) < 0) {
		perror("error: listen");
		exit(-1);
	}
	return listenfd;
}

void Process(int fd)
{
	char buf[MAXLEN];
	char file_name[MAXLEN];
	char *p;
	size_t file_len;
	size_t file_got = 0;
	int n;
	int pass_ok;

//      password check
// C -> PASS pasword
// S    open config_file.txt read password and work_dir, chroot(work_dir), setuid(work_uid)
//      
	while (1) {		// PASS password check
		FILE *fp;
		char file_buf[MAXLEN];
		pass_ok = 0;
		n = Readline(fd, buf, MAXLEN);
		buf[n] = 0;
		if (memcmp(buf, "PASS ", 5) != 0)
			continue;
		if (buf[strlen(buf) - 1] == '\n')
			buf[strlen(buf) - 1] = 0;
		if (strlen(buf + 5) == 0)
			continue;
		fp = fopen(config_file, "r");
		if (fp == NULL) {
			strcpy(buf, "ERROR open config file\n");
			Writen(fd, buf, strlen(buf));
			exit(-1);
		}
		while (fgets(file_buf, MAXLEN, fp)) {
			if (file_buf[0] == '#')
				continue;
			if (file_buf[strlen(file_buf) - 1] == '\n')
				file_buf[strlen(file_buf) - 1] = 0;
			p = file_buf;
			while (*p && (*p != ' '))
				p++;
			if (*p == 0)
				continue;
			*p = 0;
			p++;
			if (strcmp(buf + 5, file_buf) == 0) {
				pass_ok = 1;
				while (*p && (*p == ' '))
					p++;

				if (debug)
					fprintf(stderr, "password ok, work_dir is %s\n", p);
				if (chroot(p) != 0) {
					perror("chroot");
					snprintf(buf, MAXLEN, "ERROR chroot to %s\n", p);
					Writen(fd, buf, strlen(buf));
					exit(-1);
				}
				setuid(work_uid);
				chdir("/");
				break;
			}
		}
		fclose(fp);
		if (pass_ok)
			break;
		strcpy(buf, "ERROR password\n");
		Writen(fd, buf, strlen(buf));
	}
	strcpy(buf, "OK password ok\n");
	Writen(fd, buf, strlen(buf));

//
// C -> FILE file_name [size]
//
	n = Readline(fd, buf, MAXLEN);
	buf[n] = 0;
	if (memcmp(buf, "FILE ", 5) != 0)	// FILE file_name [file_len]
		exit(-1);
	if (buf[strlen(buf) - 1] == '\n')
		buf[strlen(buf) - 1] = 0;
	p = buf + 5;
	while (*p && (*p != ' '))
		p++;
	file_len = 0;
	if (*p == 0)		// no file_len
		strcpy(file_name, buf + 5);
	else {
		*p = 0;
		p++;
		strcpy(file_name, buf + 5);
		if (sscanf(p, "%zu", &file_len) != 1)
			exit(-1);
	}
	if (debug)
		fprintf(stderr, "file %s len: %zu\n", file_name, file_len);
	if (access(file_name, F_OK) != -1) {	// file exists
		strcpy(buf, "ERROR file exist\n");
		Writen(fd, buf, strlen(buf));
		if (debug)
			fprintf(stderr, "file %s exist, exit\n", file_name);
		exit(-1);
	}
	if (strchr(file_name, '/')) {	// file_name has directory, check and mkdir 
		char str[MAXLEN];
		int i, len;
		strncpy(str, file_name, MAXLEN);
		len = strlen(str);
		// find the last '/'
		for (i = len - 1; i >= 0; i--)
			if (str[i] == '/') {
				str[i] = 0;
				break;
			}
		len = strlen(str);
		for (i = 0; i < len; i++) {
			if (str[i] == '/') {
				str[i] = '\0';
				if (access(str, 0) != 0) {
					if (debug)
						fprintf(stderr, "mkdir %s\n", str);
					mkdir(str, 0733);
				}
				str[i] = '/';
			}
		}
		if (len > 0 && access(str, 0) != 0) {
			if (debug)
				fprintf(stderr, " mkdir %s\n", str);
			mkdir(str, 0733);
		}
	}
	FILE *fp = fopen(file_name, "w");
	if (fp == NULL) {
		strcpy(buf, "ERROR file open for write\n");
		Writen(fd, buf, strlen(buf));
		exit(-1);
	}
	if (debug)
		fprintf(stderr, "file %s open for write\n", file_name);
	if (file_len == 0) {
		char buf[MAXLINE];
		while (1) {
			n = Readn(fd, buf, MAXLINE);
			file_len += n;
			if (n == 0) {	// read end of file
				fclose(fp);
				snprintf(buf, 100, "OK file length %zu\n", file_len);
				Writen(fd, buf, strlen(buf));
				if (debug)
					fprintf(stderr, "write %zu\n", file_len);
				exit(0);
			}
			if (fwrite(buf, 1, n, fp) != n) {
				strcpy(buf, "ERROR file write\n");
				Writen(fd, buf, strlen(buf));
				if (debug)
					fprintf(stderr, "write %zu\n", file_len);
				exit(-1);
			}
			if (debug)
				fprintf(stderr, "write %zu\n", file_len);
		}
	}
	while (1) {
		size_t remains = file_len - file_got;
		char buf[MAXLINE];
		if (remains == 0)
			break;
		if (remains >= MAXLINE)
			n = Readn(fd, buf, MAXLINE);
		else
			n = Readn(fd, buf, remains);
		file_got += n;
		if (n == 0) {	// end of file
			fclose(fp);
			snprintf(buf, 100, "ERROR file length %zu\n", file_got);
			Writen(fd, buf, strlen(buf));
			exit(-1);
		}
		if (fwrite(buf, 1, n, fp) != n) {
			strcpy(buf, "ERROR file write\n");
			strcpy(buf, "ERROR file write\n");
			Writen(fd, buf, strlen(buf));
			exit(-1);
		}
		if (debug)
			fprintf(stderr, "write %zu of %zu\n", file_got, file_len);
	}
	fclose(fp);
	snprintf(buf, 100, "OK file length %zu\n", file_got);
	Writen(fd, buf, strlen(buf));
	exit(0);
}

void usage(void)
{
	printf("Usage:\n");
	printf("./translog_server options\n");
	printf(" options:\n");
	printf("    -p port\n");
	printf("    -f config_file\n");
	printf("    -u user_name    change to user before write file\n");
	printf("\n");
	printf("    -d              enable debug\n");
	printf("\n");
	printf("config_file:\n");
	printf("password work_dir\n");
	printf("...\n");
	exit(0);
}

int main(int argc, char *argv[])
{
	int c;
	int listenfd;
	struct passwd *pw;
	if (argc < 7)
		usage();

	while ((c = getopt(argc, argv, "p:f:u:d")) != EOF)
		switch (c) {
		case 'p':
			my_port = atoi(optarg);;
			break;
		case 'f':
			strncpy(config_file, optarg, MAXLEN - 1);
			break;
		case 'u':
			strncpy(work_user, optarg, MAXLEN - 1);
			pw = getpwnam(work_user);
			if (pw)
				work_uid = pw->pw_uid;
			else {
				fprintf(stderr, "user %s not found\n", work_user);
				exit(-1);
			}
			break;
		case 'd':
			debug = 1;
			break;
		}

	if ((my_port == 0) || (config_file[0] == 0) || (work_user[0] == 0))
		usage();
	if (debug) {
		printf("         debug = 1\n");
		printf("          port = %d\n", my_port);
		printf("     work user = %s\n", work_user);
		printf("      work uid = %d\n", work_uid);
		printf("   config_file = %s\n", config_file);
		printf("\n");
	}

	if (debug == 0) {
		daemon_init("translog_server", LOG_DAEMON);
		while (1) {
			int pid;
			pid = fork();
			if (pid == 0)	// child do the job
				break;
			else if (pid == -1)	// error
				exit(0);
			else
				wait(NULL);	// parent wait for child
			sleep(2);	// wait 2 second, and rerun
		}
	}

	listenfd = bind_and_listen();
	while (1) {
		int infd;
		int pid;
		infd = accept(listenfd, NULL, 0);
		if (infd < 0)
			continue;
		pid = fork();
		if (pid == 0)
			Process(infd);
		close(infd);
	}

	return 0;
}
