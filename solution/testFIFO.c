#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <sys/time.h>
#include <time.h>
#include <unistd.h>
#include <sys/poll.h>
#include <curl/curl.h>
#include <event2/event.h>
#include <fcntl.h>
#include <sys/stat.h>
#include <errno.h>

#include <locale.h>
#include <iconv.h>
#include <signal.h>

#define MSG_OUT stdout /* Send info to stdout, change to stderr if you want */

typedef struct webpage_data {
int pid;
int len;
} webpage_data;

struct event *evfifo = NULL;

static void fifo_read(evutil_socket_t fd, short event, void *arg)
{
	char buf[255];
	int len;
	struct event *ev = evfifo;

	fprintf(stderr, "fifo_read called with fd: %d, event: %d, arg: %p\n",
	    (int)fd, event, arg);

	// read FIFO
	len = read(fd, buf, sizeof(buf) - 1);

	if (len <= 0) {
		if (len == -1)
			perror("read");
		else if (len == 0)
			fprintf(stderr, "Connection closed\n");
		event_del(ev);
		event_base_loopbreak(event_get_base(ev));
		return;
	}

	buf[len] = '\0';

	fprintf(stdout, "Read: %s\n", buf);
}

static void fifo_write(evutil_socket_t fd, short event, void *arg)
{
	char buf[255];
	int len;
	struct event *ev = evfifo;

	fprintf(stderr, "fifo_read called with fd: %d, event: %d, arg: %p\n",
	    (int)fd, event, arg);
	sprintf(buf, "fifo_read called with fd: %d, event: %d, arg: %p\n",
	    (int)fd, event, arg);		
	
	int num_bytes = write(fd, routing_table, sizeof(routing_table));	
	fwrite(&message, 1,4,file);
}

/* On Unix, cleanup urls_list.fifo if SIGINT is received. */
static void signal_cb(evutil_socket_t fd, short event, void *arg)
{
	fprintf(stdout, "Got Ctrl+C\n");
	struct event_base *base = (event_base *)arg;
	event_base_loopbreak(base);
}

int main(int argc, char **argv)
{	
	struct event_base* base;

	struct event *signal_int;
	struct stat st;
	const char *fifo = "urls_list.fifo";
	int socket;

	if (lstat(fifo, &st) == 0) {
		if ((st.st_mode & S_IFMT) == S_IFREG) {
			errno = EEXIST;
			perror("lstat");
			exit(1);
		}
	}

	unlink(fifo);
	if (mkfifo(fifo, 0600) == -1) {
		perror("mkfifo");
		exit(1);
	}

	socket = open(fifo, O_RDONLY | O_NONBLOCK, 0);

	if (socket == -1) {
		perror("open");
		exit(1);
	}

	fprintf(stderr, "Write data to %s\n", fifo);

	/* Initalize the event library */
	base = event_base_new();

	/* Initalize one event */
	/* catch SIGINT so that event.fifo can be cleaned up */
	signal_int = evsignal_new(base, SIGINT, signal_cb, base);
	event_add(signal_int, NULL);

	//evfifo = event_new(base, socket, EV_READ|EV_PERSIST, fifo_read, evfifo);
	evfifo = event_new(base, socket, EV_WRITE|EV_PERSIST, fifo_write, evfifo);
	
	/* Add it to the active events, without a timeout */
	event_add(evfifo, NULL);	

	event_base_dispatch(base);
	event_base_free(base);

	close(socket);
	unlink(fifo);
	
	//libevent_global_shutdown();//introduced in libevent 2.1.1-alpha
	return (0);
}