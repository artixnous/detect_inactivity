/* Core code taken from https://unix.stackexchange.com/a/128855 (Andreas Wiese)
Made some modifications ("daemonize" with fork, command-line defined timeout period, etc)
*/

#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <err.h>
#include <errno.h>
#include <fcntl.h>
#include <glob.h>
#include <signal.h>
#include <string.h>
#include <syslog.h>
#include <sys/select.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <sys/wait.h>

int main(int argc, char *argv[]) {
	if (argc != 2) {
		printf("Usage: %s <timeout>\n", argv[0]);
		exit(1);
	}
	char *nptr;
	nptr = argv[1];
	unsigned int TIMEOUT;
	if ((TIMEOUT = strtol(nptr, NULL, 10)) < 5) {
		printf("Specified timeout (%d) is a joke!\n", TIMEOUT);
		exit(1);
	}
	int detect_activity(int);
	pid_t p1, p2, res;
	char timeout_action[]="pm-suspend";
	int logger(char *);
	char logmsg[256];

	if ( (p1 = fork()) < 0 ) {
		err(EXIT_FAILURE, "fork");
	}
	/* pid=0 for the child, >0 for the parent */
	if (p1 > 0) {	// parent
		res = waitpid(p1, NULL, WUNTRACED | WCONTINUED);
		if (res < 0) {
			err(EXIT_FAILURE, "waitpid");
		}
		exit(0);
	} else { // =0, child
		signal(SIGCHLD, SIG_IGN);
		signal(SIGHUP, SIG_IGN);
		chdir("/");
		if (setsid() < 0) {
			err(EXIT_FAILURE, "setsid");
		}
		if ( (p2 = fork()) < 0 ) {
			err(EXIT_FAILURE, "fork");
		}
		if (p2 > 0) {
			exit(0);
		} else {
			/* TODO: create a .pid in /run */
			snprintf(logmsg, sizeof(logmsg), "Inactivity monitor started - timeout set to %d seconds\n", TIMEOUT);
			logger(logmsg);
			while (1) {
				sleep(1);	// can go as low as 1, but it's not necessary
				if (detect_activity(TIMEOUT) == 0) {
					snprintf(logmsg, sizeof(logmsg), "Inactivity timeout of %d seconds reached, performing %s!\n", TIMEOUT, timeout_action);
					logger(logmsg);
					system(timeout_action);
				}
			}
		}
		exit(0);	// shouldn't ever
	}
}

int logger(char *logmsg) {
	openlog(NULL, LOG_PID|LOG_CONS, LOG_USER);
	syslog(LOG_INFO, logmsg);
	closelog();
}

int detect_activity(int TIMEOUT) {
//    char timeout_action[]="/usr/local/sbin/timeout_action.sh";
/*    struct stat fileStat;
	if(stat(argv[1],&fileStat) < 0) {
		printf("Suspend command %s not found\n", argv[1]);
		return 1;
	} */
/*
FIX THIS:
idle: open `/dev/input/event4': Too many open files
idle: glob: Too many open files
*/

	int *fds, ret, i;
	glob_t glob_result;
	/* find all devices matching /dev/input/event[0-9]* */
	ret = glob("/dev/input/event[0-9]*", GLOB_ERR|GLOB_NOSORT|GLOB_NOESCAPE, NULL, &glob_result);
	if (ret) {
//		err(EXIT_FAILURE, "glob");
		warn("glob"); // just warn, don't exit
		globfree(&glob_result);
		return(1);
	}
	/* allocate array for opened file descriptors */
	fds = malloc(sizeof(*fds) * (glob_result.gl_pathc+1));
	if (fds == NULL) {
		warn("malloc");
		return(2);
	}

	/* open devices */
	for (i = 0; i < glob_result.gl_pathc; i++) {
		fds[i] = open(glob_result.gl_pathv[i], O_RDONLY|O_NONBLOCK);
		if (fds[i] == -1) {
			warn("open `%s'", glob_result.gl_pathv[i]);
			return(3);
		}
	}

	fds[i] = -1; /* end of array */
	char buf[512];
	struct timeval timeout;
	fd_set readfds;
	int nfds = -1;
	FD_ZERO(&readfds);
	/* select(2) might alter the fdset, thus freshly set it
	   on every iteration */
	for (i = 0; fds[i] != -1; i++) {
		FD_SET(fds[i], &readfds);
		nfds = fds[i] >= nfds ? fds[i] + 1 : nfds;
		/* read everything what's available on this fd */
		while ((ret = read(fds[i], buf, sizeof(buf))) > 0)
			continue; /* read away input */
		if (ret == -1 && errno != EAGAIN) {
			warn("read");
			return(4);
		}
	}
	/* same for timeout, 5 seconds here */
	timeout.tv_sec = TIMEOUT;    /* FIXME */
	timeout.tv_usec = 0;
	ret = select(nfds, &readfds, NULL, NULL, &timeout);
	/* needs close() or "too many open files" */
	for (i = 0; fds[i] != -1; i++) {
		close(fds[i]);
	}
	globfree(&glob_result);
	free(fds);
	if (ret == -1) {
		warn("select");
		return(5);
	}
	if (ret == 0) {
	/* no activity for TIMEOUT seconds */
		return(0);
	}
	/* activity detected */
	return(-1);
}
