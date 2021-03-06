/***************************************************************************
 *                                  _   _ ____  _
 *  Project                     ___| | | |  _ \| |
 *                             / __| | | | |_) | |
 *                            | (__| |_| |  _ <| |___
 *                             \___|\___/|_| \_\_____|
 *
 * Copyright (C) 1998 - 2013, Daniel Stenberg, <daniel@haxx.se>, et al.
 *
 * This software is licensed as described in the file COPYING, which
 * you should have received as part of this distribution. The terms
 * are also available at http://curl.haxx.se/docs/copyright.html.
 *
 * You may opt to use, copy, modify, merge, publish, distribute and/or sell
 * copies of the Software, and permit persons to whom the Software is
 * furnished to do so, under the terms of the COPYING file.
 *
 * This software is distributed on an "AS IS" basis, WITHOUT WARRANTY OF ANY
 * KIND, either express or implied.
 *
 ***************************************************************************/
/* Example application source code using the multi socket interface to
   download many files at once.

Written by Jeff Pohlmeyer

Requires libevent version 2 and a (POSIX?) system that has mkfifo().

This is an adaptation of libcurl's "hipev.c" and libevent's "event-test.c"
sample programs.

When running, the program creates the named pipe "hiper.fifo"

Whenever there is input into the fifo, the program reads the input as a list
of URL's and creates some new easy handles to fetch each URL via the
curl_multi "hiper" API.


Thus, you can try a single URL:
  % echo http://www.yahoo.com > hiper.fifo

Or a whole bunch of them:
  % cat my-url-list > hiper.fifo

The fifo buffer is handled almost instantly, so you can even add more URL's
while the previous requests are still being downloaded.

Note:
  For the sake of simplicity, URL length is limited to 1023 char's !

This is purely a demo app, all retrieved data is simply discarded by the write
callback.

  g++ -Wall -W -L/usr/libevent/lib -I/usr/libevent/include hiperfifo.c

  g++ -Wall -W -lcurl -levent -lmysqlclient -L/usr/libevent/lib -L/usr/lib64/mysql -I/usr/libevent/include -I/usr/include/mysql hiperfifo.c

  LD_LIBRARY_PATH=/usr/libevent/lib ./a.out

  CREATE TABLE `writers` (
  `name` TEXT NULL,
  `size` INT(10) UNSIGNED NOT NULL DEFAULT '0',
  `ts` TIMESTAMP NOT NULL DEFAULT CURRENT_TIMESTAMP
  )
  COLLATE='utf8_general_ci'
  ENGINE=MyISAM
ROW_FORMAT=DEFAULT

24*60*60=86400

  select ts, sum(size)/1000 as 'KB',count(name) as 'URLs'  from `writers`
  group by `ts`
with ROLLUP

select min(ts) from writers
where size > 0
union
select max(ts) from writers
where size > 0

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

#include <locale.h>
#include <iconv.h>

#define MSG_OUT stdout /* Send info to stdout, change to stderr if you want */
#define MAX_WEBPAGE_SIZE 500*1024 // max webpage size = 500KB
#define MAX_PARALLEL_WORKER 150*3 // 12M / 8 = 1.5M * 1000 / 60 = 25
#define READ_TIMER_SECONDS 4

//#define DEBUG
#define MYSQL_DB

#ifdef MYSQL_DB
	#include <my_global.h>
	#include <mysql.h>
	#include <m_string.h>

	MYSQL *g_pConn = NULL;
#else
	#include "libpq-fe.h"

	PGconn *g_pConn = NULL;
#endif

// --------------------------------
// global var
long  g_share_counter = 0;

/* Global information, common to all connections */
typedef struct _GlobalInfo
{
  struct event_base *evbase;
  struct event *fifo_event;
  struct event *timer_event;
  CURLM *multi;
  int still_running;
  FILE* input;
} GlobalInfo;


/* Information associated with a specific easy handle */
typedef struct _ConnInfo
{
  CURL *easy;
  char *url;
  GlobalInfo *global;
  char error[CURL_ERROR_SIZE];
  char content[MAX_WEBPAGE_SIZE];  
  int cont_len;
} ConnInfo;


/* Information associated with a specific socket */
typedef struct _SockInfo
{
  curl_socket_t sockfd;
  CURL *easy;
  int action;
  long timeout;
  struct event *ev;
  int evset;
  GlobalInfo *global;
} SockInfo;



/* Update the event timer after curl_multi library calls */
static int multi_timer_cb(CURLM *multi, long timeout_ms, GlobalInfo *g)
{
  struct timeval timeout;
  (void)multi; /* unused */

  timeout.tv_sec = timeout_ms/1000;
  timeout.tv_usec = (timeout_ms%1000)*1000;  
  evtimer_add(g->timer_event, &timeout);
#ifdef DEBUG
  fprintf(MSG_OUT, "multi_timer_cb: Setting timeout to %ld ms\n", timeout_ms);
#endif
  return 0;
}

/* Die if we get a bad CURLMcode somewhere */
static void mcode_or_die(const char *where, CURLMcode code)
{
  if ( CURLM_OK != code ) {
    const char *s;
    switch (code) {
      case     CURLM_CALL_MULTI_PERFORM: s="CURLM_CALL_MULTI_PERFORM"; break;
      case     CURLM_BAD_HANDLE:         s="CURLM_BAD_HANDLE";         break;
      case     CURLM_BAD_EASY_HANDLE:    s="CURLM_BAD_EASY_HANDLE";    break;
      case     CURLM_OUT_OF_MEMORY:      s="CURLM_OUT_OF_MEMORY";      break;
      case     CURLM_INTERNAL_ERROR:     s="CURLM_INTERNAL_ERROR";     break;
      case     CURLM_UNKNOWN_OPTION:     s="CURLM_UNKNOWN_OPTION";     break;
      case     CURLM_LAST:               s="CURLM_LAST";               break;
      default: s="CURLM_unknown";
        break;
    case     CURLM_BAD_SOCKET:         s="CURLM_BAD_SOCKET";

      fprintf(MSG_OUT, "ERROR: %s returns %s\n", where, s);
      /* ignore this error */
      return;
    }
    fprintf(MSG_OUT, "ERROR: %s returns %s\n", where, s);
    exit(code);
  }
}



/* Check for completed transfers, and remove their easy handles */
static void check_multi_info(GlobalInfo *g)
{
  char *eff_url;
  CURLMsg *msg;
  int msgs_left;
  ConnInfo *conn;
  CURL *easy;
  CURLcode res;

#ifdef DEBUG
  fprintf(MSG_OUT, "REMAINING: %d\n", g->still_running);
#endif

  while ((msg = curl_multi_info_read(g->multi, &msgs_left))) {
    if (msg->msg == CURLMSG_DONE) {
		// ------------------
      easy = msg->easy_handle;
      res = msg->data.result;
      curl_easy_getinfo(easy, CURLINFO_PRIVATE, &conn);
      curl_easy_getinfo(easy, CURLINFO_EFFECTIVE_URL, &eff_url);
#ifdef DEBUG
      fprintf(MSG_OUT, "DONE: %s => (%d) %s\n", eff_url, res, conn->error);
#endif
	  // -------------------
	  //printf("len: %d = body: %s\n", conn->cont_len, conn->content);

#ifdef MYSQL_DB
	  char sztt[32] = {'\0'};
	  sprintf(sztt, "%d", conn->cont_len);
	  
	  static char query[MAX_WEBPAGE_SIZE], *end;
		memset(query, '\0', MAX_WEBPAGE_SIZE);
	  //end = strmov(query, "INSERT IGNORE INTO writers (name,size) VALUES(");
		
		end = strcpy(query, "INSERT IGNORE INTO writers (name,size) VALUES(");
		end += strlen (end);
	  *end++ = '\'';
	  end += mysql_real_escape_string(g_pConn, end, (const char*)conn->content, conn->cont_len);
	  *end++ = '\'';
	  *end++ = ',';
	  strncpy(end, sztt, strlen(sztt));
	  end += strlen(sztt);
	  *end++ = ')';
	  //printf("sql: %s\n", query);	  

	  if (mysql_real_query(g_pConn, query, (unsigned int) (end - query))) {
		  fprintf(stderr, "Failed to insert row, Error: %s\n%s",
			  mysql_error(g_pConn), query);
		  fprintf(stderr,"-");
	  } else {
		fprintf(MSG_OUT,"+");
	  }	  
#else
	  static char content[MAX_WEBPAGE_SIZE] = {'\0'};	  
	  static char query[MAX_WEBPAGE_SIZE] = {'\0'};
	  int flag = 0;

	  memset(content, '\0', MAX_WEBPAGE_SIZE);	  
	  memset(query, '\0', MAX_WEBPAGE_SIZE);

	  int len = htonl(conn->cont_len);
	  //set the values to use
	  const char *values[2] = {conn->content, (char *)&len};
	  //calculate the lengths of each of the values
	  int lengths[2] = {len, sizeof(len)};
	  //state which parameters are binary
	  int binary[2] = {0, 1};

	  PGresult *res = PQexecParams(g_pConn,
		  "INSERT INTO writers (name,size) VALUES($1::text, $2::int)",
		  //"SELECT person_id, name FROM person WHERE person_id != $1::int4 and name = $2::varchar",
		  2, //number of parameters
		  NULL, //ignore the Oid field
		  values, //values to substitute $1 and $2
		  lengths, //the lengths, in bytes, of each of the parameter values
		  binary, //whether the values are binary or not
		0); //we want the result in text format

	  ExecStatusType st = PQresultStatus(res);
	  //if (PQresultStatus(res) != PGRES_TUPLES_OK) {
	  if ( st != PGRES_COMMAND_OK) {	
		  puts( PQresStatus(st) );
		  puts( PQresultErrorMessage(res) );
		  puts( "command error! insert" );			  
		}
/*
	  size_t to_length = 0;
	  unsigned char* tbuf = PQescapeByteaConn (g_pConn, (const unsigned char *)conn->content, 
				conn->cont_len, &to_length);
	  puts( (const char *)tbuf );
	  PQfreemem (tbuf);

///*
	  int error = 0;
	  size_t write_size = PQescapeStringConn (g_pConn, content, conn->content, 
		  conn->cont_len, &error);

	  if (error == 0)
	  {
		  content[write_size+1] = '\0';
		  
		  sprintf(query, "INSERT INTO writers (name,size) VALUES('%s',%d)",
			  content, conn->cont_len);	  
		  
		  PGresult   *res;
		  res = PQexec(g_pConn, query);
		  ExecStatusType st = PQresultStatus(res);
		  //if (PQresultStatus(res) != PGRES_TUPLES_OK) {
		  if ( st != PGRES_COMMAND_OK) {	
			  puts( PQresStatus(st) );
			  puts( PQresultErrorMessage(res) );
			  puts( "command error! insert" );	
			  puts( query );
		  }

		  flag = 1;	  	  
	  } else {
		  fprintf(stderr, "Failed iconv()!");	
	  }	  

	  if (flag) {
		fprintf(MSG_OUT,"+");
	  } else {
		fprintf(stderr,"-");
	  }
*/
#endif

      curl_multi_remove_handle(g->multi, easy);
      free(conn->url);
      curl_easy_cleanup(easy);
      free(conn);	  

#ifdef DEBUG			
			__sync_fetch_and_sub(&g_share_counter, 1);
		fprintf(MSG_OUT, "free(conn) counter:%ld", g_share_counter);    
#endif
    }
  }
}



/* Called by libevent when we get action on a multi socket */
static void event_cb(int fd, short kind, void *userp)
{
  GlobalInfo *g = (GlobalInfo*) userp;
  CURLMcode rc;

  int action =
    (kind & EV_READ ? CURL_CSELECT_IN : 0) |
    (kind & EV_WRITE ? CURL_CSELECT_OUT : 0);

  rc = curl_multi_socket_action(g->multi, fd, action, &g->still_running);
  mcode_or_die("event_cb: curl_multi_socket_action", rc);

  check_multi_info(g);
  if ( g->still_running <= 0 ) {
#ifdef DEBUG
    fprintf(MSG_OUT, "last transfer done, kill timeout\n");
#endif
    if (evtimer_pending(g->timer_event, NULL)) {
      evtimer_del(g->timer_event);
    }
  }
}



/* Called by libevent when our timeout expires */
static void timer_cb(int fd, short kind, void *userp)
{
  GlobalInfo *g = (GlobalInfo *)userp;
  CURLMcode rc;
  (void)fd;
  (void)kind;

  rc = curl_multi_socket_action(g->multi,
                                  CURL_SOCKET_TIMEOUT, 0, &g->still_running);
  mcode_or_die("timer_cb: curl_multi_socket_action", rc);
  check_multi_info(g);
}



/* Clean up the SockInfo structure */
static void remsock(SockInfo *f)
{
  if (f) {
    if (f->evset)
      event_free(f->ev);
    free(f);
  }
}



/* Assign information to a SockInfo structure */
static void setsock(SockInfo*f, curl_socket_t s, CURL*e, int act, GlobalInfo*g)
{
  int kind =
     (act&CURL_POLL_IN?EV_READ:0)|(act&CURL_POLL_OUT?EV_WRITE:0)|EV_PERSIST;

  f->sockfd = s;
  f->action = act;
  f->easy = e;
  if (f->evset)
    event_free(f->ev);
  f->ev = event_new(g->evbase, f->sockfd, kind, event_cb, g);
  f->evset = 1;
  event_add(f->ev, NULL);
}



/* Initialize a new SockInfo structure */
static void addsock(curl_socket_t s, CURL *easy, int action, GlobalInfo *g)
{
  SockInfo *fdp = (SockInfo *)calloc(sizeof(SockInfo), 1);

  fdp->global = g;
  setsock(fdp, s, easy, action, g);
  curl_multi_assign(g->multi, s, fdp);
}

/* CURLMOPT_SOCKETFUNCTION */
static int sock_cb(CURL *e, curl_socket_t s, int what, void *cbp, void *sockp)
{
  GlobalInfo *g = (GlobalInfo*) cbp;
  SockInfo *fdp = (SockInfo*) sockp;

#ifdef DEBUG
  const char *whatstr[]={ "none", "IN", "OUT", "INOUT", "REMOVE" };
  fprintf(MSG_OUT,
          "socket callback: s=%d e=%p what=%s ", s, e, whatstr[what]);
#endif

  if (what == CURL_POLL_REMOVE) {
#ifdef DEBUG
    fprintf(MSG_OUT, "\n");
#endif
    remsock(fdp);
  }
  else {
    if (!fdp) {
#ifdef DEBUG
      fprintf(MSG_OUT, "Adding data: %s\n", whatstr[what]);
#endif
      addsock(s, e, what, g);
    }
    else {
#ifdef DEBUG
      fprintf(MSG_OUT,
              "Changing action from %s to %s\n",
              whatstr[fdp->action], whatstr[what]);
#endif
      setsock(fdp, s, e, what, g);
    }
  }
  return 0;
}



/* CURLOPT_WRITEFUNCTION */
static size_t write_cb(void *ptr, size_t size, size_t nmemb, void *data)
{
  size_t realsize = size * nmemb;
  ConnInfo *conn = (ConnInfo*) data;
  (void)ptr;
  (void)conn;

  //CURLcode res;
  //double filesize;
  //time_t filetime;
 /* res = curl_easy_getinfo(conn->easy, CURLINFO_FILETIME, &filetime);
  if((CURLE_OK == res) && filetime)
        printf("filetime %s", ctime(&filetime));
*/
  /*res = curl_easy_getinfo(conn->easy, CURLINFO_CONTENT_LENGTH_DOWNLOAD, &filesize);
  if((CURLE_OK == res) && filesize)
        printf("filesize: %0.0f bytes\n", filesize);
	*/

  // ------------------
  //printf("len: %d body: %s\n", conn->cont_len, conn->content);  
  memcpy(conn->content + conn->cont_len, ptr, realsize);  
  conn->cont_len += realsize;
  //printf("len: %d body: %s\n", conn->cont_len, conn->content);  
  return realsize;
  /*
  // ------------------
  char substr[102400] = {'\0'};
  strncpy(substr, (const char*)ptr, realsize);

  //printf("body: %s\n", substr);  
  //return realsize;

  //mysql_real_escape_string(g_pConn, substr, (const char*)substr, realsize);
  //printf("body: %s\n", substr); 
  
  // -------------------
  char query[102400], *end;
  end = strmov(query, "INSERT INTO writers VALUES(");
  *end++ = '\'';
  end += mysql_real_escape_string(g_pConn, end, (const char*)substr, realsize);
  *end++ = '\'';
  *end++ = ')';
  printf("sql: %s\n", query);

  if (mysql_real_query(g_pConn, query, (unsigned int) (end - query)))
  {
	  fprintf(stderr, "Failed to insert row, Error: %s\n",
		  mysql_error(g_pConn));
  }
return realsize;

  //strncpy(szBuf, (const char*)ptr, size*nmemb);
  //szBuf = substr(szBuf,0,realsize+1)
  //sprintf(szBuf, "INSERT INTO writers VALUES('%s')", szBuf);
  //printf("sql: %s\n", szBuf);
  //mysql_query(g_pConn, szBuf);
  return realsize;*/
}


/* CURLOPT_PROGRESSFUNCTION */
static int prog_cb (void *p, double dltotal, double dlnow, double ult,
                    double uln)
{
  ConnInfo *conn = (ConnInfo *)p;
  (void)ult;
  (void)uln;

  fprintf(MSG_OUT, "Progress: %s (%g/%g)\n", conn->url, dlnow, dltotal);
  return 0;
}


/* Create a new easy handle, and add it to the global curl_multi */
static void new_conn(char *url, GlobalInfo *g )
{
  ConnInfo *conn;
  CURLMcode rc;

  conn = (ConnInfo *)calloc(1, sizeof(ConnInfo));
  if (conn == NULL) {
	  fprintf(MSG_OUT, "calloc failed!\n");
	  exit (1);
  }

  memset(conn, 0, sizeof(ConnInfo));
  conn->error[0]='\0';

  if(0) {
	  fprintf(MSG_OUT, "calloc %d %d %s!\n", (int)sizeof(ConnInfo),
		  conn->cont_len, conn->content);	  
  }

  conn->easy = curl_easy_init();
  if (!conn->easy) {
    fprintf(MSG_OUT, "curl_easy_init() failed, exiting!\n");
    exit(2);
  }
  conn->global = g;
  conn->url = strdup(url);
  curl_easy_setopt(conn->easy, CURLOPT_URL, conn->url);
  curl_easy_setopt(conn->easy, CURLOPT_WRITEFUNCTION, write_cb);
  curl_easy_setopt(conn->easy, CURLOPT_WRITEDATA, conn);
  //curl_easy_setopt(conn->easy, CURLOPT_VERBOSE, 1L);
  curl_easy_setopt(conn->easy, CURLOPT_ERRORBUFFER, conn->error);
  curl_easy_setopt(conn->easy, CURLOPT_PRIVATE, conn);
  //curl_easy_setopt(conn->easy, CURLOPT_NOPROGRESS, 0L);
  //curl_easy_setopt(conn->easy, CURLOPT_PROGRESSFUNCTION, prog_cb);
  //curl_easy_setopt(conn->easy, CURLOPT_PROGRESSDATA, conn);
  //curl_easy_setopt(conn->easy,   CURLOPT_WRITEHEADER, headerfile);
  //curl_easy_setopt(conn->easy, CURLOPT_HEADER, 1L); 

#ifdef DEBUG
  fprintf(MSG_OUT,
          "Adding easy %p to multi %p (%s)\n", conn->easy, g->multi, url);
#endif

  rc = curl_multi_add_handle(g->multi, conn->easy);
  mcode_or_die("new_conn: curl_multi_add_handle", rc);

  /* note that the add_handle() will set a time-out to trigger very soon so
     that the necessary socket_action() call will be called by this app */
}

/* This gets called whenever data is received from the fifo */
static void fifo_cb(int fd, short event, void *arg)
{
  char s[1024];
  long int rv=0;
  int n=0;
  GlobalInfo *g = (GlobalInfo *)arg;
  (void)fd; /* unused */
  (void)event; /* unused */

#ifdef MYSQL_DB
  mysql_query(g_pConn, "INSERT INTO writers (name,size) VALUES('start running', 1)");
#else
  PGresult   *res;
  res = PQexec(g_pConn, "INSERT INTO writers (name,size) VALUES('start running', 1)");
  //if (PQresultStatus(res) != PGRES_TUPLES_OK) {
  if (PQresultStatus(res) != PGRES_COMMAND_OK) {	
		  puts("command error: running!");
  }
#endif	  

  int counter = 0;
  do {
		//mysql_query(g_pConn, "INSERT INTO writers (name,size) VALUES('do', 1)");
		
    s[0]='\0';
    rv=fscanf(g->input, "%1023s%n", s, &n);
    s[n]='\0';
		
    if ( n && s[0] ) {		
			/*
			if (g_share_counter >= MAX_PARALLEL_WORKER) {
				sleep(2);
				//__sync_fetch_and_sub(&g_share_counter, 1);
				g_share_counter = 0;
				fprintf(MSG_OUT, "sleep.");
			}*/
			fprintf(MSG_OUT, ".");
      new_conn(s,g);  /* if we read a URL, go get it! */

			if (++counter > 120) {puts("return."); return;}

#ifdef DEBUG			
			fprintf(MSG_OUT, "new_conn() counter:%ld", g_share_counter);
			__sync_fetch_and_add(&g_share_counter, 1);
#endif			
			
			//mysql_query(g_pConn, "INSERT INTO writers (name,size) VALUES('new conn', 1)");
    } else break;
  } while ( rv != EOF);
}

/* Create a named pipe and tell libevent to monitor it */
static const char *fifo = "hiper.fifo";
static int init_fifo (GlobalInfo *g)
{
  struct stat st;
  curl_socket_t sockfd;

  fprintf(MSG_OUT, "Creating named pipe \"%s\"\n", fifo);
  if (lstat (fifo, &st) == 0) {
    if ((st.st_mode & S_IFMT) == S_IFREG) {
      errno = EEXIST;
      perror("lstat");
      exit (1);
    }
  }
  unlink(fifo);
  if (mkfifo (fifo, 0600) == -1) {
    perror("mkfifo");
    exit (1);
  }
  sockfd = open(fifo, O_RDWR | O_NONBLOCK, 0);
  if (sockfd == -1) {
    perror("open");
    exit (1);
  }
  g->input = fdopen(sockfd, "r");

  fprintf(MSG_OUT, "Now, pipe some URL's into > %s\n", fifo);
    //g->fifo_event = event_new(g->evbase, sockfd, EV_READ|EV_PERSIST, fifo_cb, g);
  g->fifo_event = event_new(g->evbase, sockfd, EV_PERSIST, fifo_cb, g);
  struct timeval mytimer = {READ_TIMER_SECONDS,0};
  event_add(g->fifo_event, &mytimer);
  return (0);
}

static void clean_fifo(GlobalInfo *g)
{
    event_free(g->fifo_event);
    fclose(g->input);
    unlink(fifo);
}

int main(int argc, char **argv)
{
	//setlocale (LC_ALL, "nl_NL.utf8" );
	setbuf(stdout, NULL); // Disable buffering
	
	// 3rd lib info
	printf(curl_version());
	printf("\n");
		
#ifdef MYSQL_DB
	//printf("MySQL client version: %s\n", mysql_get_client_info());return 0;		
	g_pConn = mysql_init(NULL);
	//mysql_real_connect(g_pConn, "192.168.4.192", "root", "123456", "mydomain", 0, NULL, 0);	
	mysql_real_connect(g_pConn, "localhost", "root", "30083012", "mydomain", 0, NULL, 0);	
	//mysql_real_connect(g_pConn, "192.168.1.102", "root", "123456", "test", 0, NULL, 0);	
#else
	g_pConn = PQconnectdb("host='192.168.21.90' port='5432' dbname='test' user='pguser' password='123456' connect_timeout='1000'");
	if(0 != PQsetClientEncoding(g_pConn, "EUC_CN"))		fprintf(MSG_OUT, "PQsetClientEncoding() failed");
#endif	

	/*
	mysql_query(conn, "CREATE TABLE writers(name VARCHAR(25))");
	
	mysql_query(conn, "INSERT INTO writers VALUES('Leo Tolstoy')");
	mysql_query(conn, "INSERT INTO writers VALUES('Jack London')");
	mysql_query(conn, "INSERT INTO writers VALUES('Honore de Balzac')");
	mysql_query(conn, "INSERT INTO writers VALUES('Lion Feuchtwanger')");
	mysql_query(conn, "INSERT INTO writers VALUES('Emile Zola')");
	*/   

  GlobalInfo g;
  (void)argc;
  (void)argv;

  memset(&g, 0, sizeof(GlobalInfo));
  g.evbase = event_base_new();
  init_fifo(&g);
  g.multi = curl_multi_init();
  g.timer_event = evtimer_new(g.evbase, timer_cb, &g);

  /* setup the generic multi interface options we want */
  curl_multi_setopt(g.multi, CURLMOPT_SOCKETFUNCTION, sock_cb);
  curl_multi_setopt(g.multi, CURLMOPT_SOCKETDATA, &g);
  curl_multi_setopt(g.multi, CURLMOPT_TIMERFUNCTION, multi_timer_cb);
  curl_multi_setopt(g.multi, CURLMOPT_TIMERDATA, &g);
	//curl_multi_setopt(g.multi, CURLMOPT_PIPELINING, 1L);	

  /* we don't call any curl_multi_socket*() function yet as we have no handles
     added! */

  // it's the same as event_base_loop(), with no flags set
  event_base_dispatch(g.evbase);

  /* this, of course, won't get called since only way to stop this program is
     via ctrl-C, but it is here to show how cleanup /would/ be done. */
  clean_fifo(&g);
  event_free(g.timer_event);
  event_base_free(g.evbase);
  curl_multi_cleanup(g.multi);
	//libevent_global_shutdown();
	
#ifdef MYSQL_DB
  //mysql_query(g_pConn, "INSERT INTO writers (name,size) VALUES('done!', 9)");
  mysql_close(g_pConn);
#else
  PQfinish(g_pConn);
#endif	
  
  return 0;
}
/*
#ifdef MYSQL_DB

#else

#endif	
*/
