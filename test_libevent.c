/*
+----------------------------------------------------------------------+
| Fast get webpage                                                     |
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

#define MSG_OUT stdout /* Send info to stdout, change to stderr if you want */

void run_base_with_ticks(struct event_base *base)
{
	struct timeval ten_sec;
	
	ten_sec.tv_sec = 2;
	ten_sec.tv_usec = 0;
	
	/* Now we run the event_base for a series of 10-second intervals, printing
	"Tick" after each.  For a much better way to implement a 10-second
	timer, see the section below about persistent timer events. */
	while (1) {
		/* This schedules an exit ten seconds from now. */
		puts("-");
		event_base_loopexit(base, &ten_sec);
		puts("|");
		event_base_dispatch(base);
		puts("Tick");

		// --------------
		FILE * pFile;	
		pFile = fopen ("myfile.txt" , "a+");
		if (pFile == NULL) perror ("Error opening file");
		event_base_dump_events(base, pFile);
		fclose (pFile);
	}
}

int main(int argc, char **argv)
{
	int i;

	struct event_base *evbase;
	struct event_config *cfg;

	cfg = event_config_new();
	
    /* I don't like select. */
    event_config_avoid_method(cfg, "select");

	evbase = event_base_new_with_config(cfg);
	event_config_free(cfg);
	//evbase = event_base_new();

	const char **methods = event_get_supported_methods();
	printf("Starting Libevent %s.  Available methods are:\n",
		event_get_version());
	for (i=0; methods[i] != NULL; ++i) {
			printf("    %s\n", methods[i]);
	}

	// --------------
	enum event_method_feature f;
	printf("Using Libevent with backend method %s.",
        event_base_get_method(evbase));
    f = (enum event_method_feature)event_base_get_features(evbase);
    if ((f & EV_FEATURE_ET))
        printf("  Edge-triggered events are supported.");
    if ((f & EV_FEATURE_O1))
        printf("  O(1) event notification is supported.");
    if ((f & EV_FEATURE_FDS))
        printf("  All FD types are supported.");

	//int n = event_base_get_npriorities(evbase); // new in Libevent 2.1.1-alpha
	//fprintf(MSG_OUT, "max npriorities: %d\n", n);

	run_base_with_ticks(evbase);
	

	event_base_free(evbase);
	return 0;
}