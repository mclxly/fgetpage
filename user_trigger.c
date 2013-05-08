#include <event2/event.h>
#include <unistd.h>
#include <stdio.h>

#define EVENT_DBG_NONE 0
#define EVENT_DBG_ALL 0xffffffffu

/*
 * Global var
 * */
struct event *ev;

/*
 * Log messages in Libevent
 */
static void discard_cb(int severity, const char *msg)
{
    /* This callback does nothing. */
}

static FILE *logfile = NULL;
static void write_to_file_cb(int severity, const char *msg)
{
    const char *s;
    if (!logfile)
        return;
    switch (severity) {
        case _EVENT_LOG_DEBUG: s = "debug"; break;
        case _EVENT_LOG_MSG:   s = "msg";   break;
        case _EVENT_LOG_WARN:  s = "warn";  break;
        case _EVENT_LOG_ERR:   s = "error"; break;
        default:               s = "?";     break; /* never reached */
    }
    fprintf(logfile, "[%s] %s\n", s, msg);
}

/* Turn off all logging from Libevent. */
void suppress_logging(void)
{
    event_set_log_callback(discard_cb);
}

/* Redirect all Libevent log messages to the C stdio file 'f'. */
void set_logfile(FILE *f)
{
    logfile = f;
    event_set_log_callback(write_to_file_cb);
}
// end of log func
// -------------------------------------------------

void cb_func (evutil_socket_t fd, short flags, void * _param) {
  puts("Callback function called!");
}

void run_base_with_ticks(struct event_base *base)
{
  struct timeval one_sec;

  one_sec.tv_sec = 1;
  one_sec.tv_usec = 0;
  struct event * ev1;
  ev1 = event_new(base, -1, EV_PERSIST, cb_func, NULL);
  //int result = event_add(ev1, NULL);
  int result = event_add(ev1, &one_sec);
  printf("event_add result: %d\n",result);

  while (1) {
     result = event_base_dispatch(base);
     if (result == 1) {
       printf("Failed: event considered as not pending dispite successful event_add\n");
       sleep(1);
     } else {
       puts("Tick");
     }
  }
}

static void cb_2(int sock, short which, void *arg) {
  puts("cb_2");
  //event_active(ev, EV_WRITE, 0);
}

int main () {
  // init
  setbuf(stdout, NULL); // Disable buffering

  // create logfile
  logfile = fopen("libevent.log", "w");
  if (logfile != NULL ) {
    set_logfile(logfile); 
    //event_enable_debug_logging(EVENT_DBG_ALL); // libevent version > 2.1.*
    event_enable_debug_mode();
  }

  struct event_base *base = event_base_new();
  // example one
  /*
  run_base_with_ticks(base);
  return 0;*/
  
  // example 2
  /*ev = event_new(base, -1, EV_PERSIST | EV_READ, cb_2, NULL);
  //ev = event_new(base, -1, EV_READ, cb_2, NULL);
  event_add(ev, NULL);
  event_active(ev, EV_WRITE, 0);
  event_base_loop(base, 0);*/

  // example 3: timer
  struct timeval five_seconds = {2,0};
  ev = event_new(base, -1, EV_PERSIST | EV_READ, cb_2, NULL);
  event_add(ev, &five_seconds);
  event_base_loop(base, 0);

  return 0;
}
