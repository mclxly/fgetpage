/*
+----------------------------------------------------------------------+
| Yet Another Framework                                                |
+----------------------------------------------------------------------+
+----------------------------------------------------------------------+
| Author: LinXiang  <mclxly@gmail.com>                                 |
+----------------------------------------------------------------------+
3rd lib: curl, libevent, libconfig
*/

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

int main(int argc, char **argv)
{
	int i;

	struct event_base *evbase;
	evbase = event_base_new();

	const char **methods = event_get_supported_methods();
	printf("Starting Libevent %s.  Available methods are:\n",
		event_get_version());
	for (i=0; methods[i] != NULL; ++i) {
			printf("    %s\n", methods[i]);
	}

	return 0;
}