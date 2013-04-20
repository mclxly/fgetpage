/*
 * testlibpq.c
 *
 *      Test the C version of libpq, the PostgreSQL frontend library.
 */
#include <stdio.h>
#include <stdlib.h>
#include "libpq-fe.h"

static void
exit_nicely(PGconn *conn)
{
    PQfinish(conn);
    exit(1);
}

int
main(int argc, char **argv)
{
    const char *conninfo;
    PGconn     *conn;
    PGresult   *res;
    int         nFields;
    int         i,
		j;
	
		/*
		* If the user supplies a parameter on the command line, use it as the
		* conninfo string; otherwise default to setting dbname=postgres and using
		* environment variables or defaults for all other connection parameters.
	*/
    if (argc > 1)
        conninfo = argv[1];
    else
        conninfo = "host='192.168.21.90' port='5432' dbname='test' user='pguser' password='123456' connect_timeout='1000'";		
	
    /* Make a connection to the database */
    conn = PQconnectdb(conninfo);
	
    /* Check to see that the backend connection was successfully made */
    if (PQstatus(conn) != CONNECTION_OK)
    {
        fprintf(stderr, "Connection to database failed: %s",
			PQerrorMessage(conn));
        exit_nicely(conn);
    }
	
	res = PQexec(conn, "insert into writers (name,size) values ('test',100)");
	//if (PQresultStatus(res) != PGRES_TUPLES_OK) {
	if (PQresultStatus(res) != PGRES_COMMAND_OK) {	
		puts("We did not get any data!");		
	}
   
	
    /* close the connection to the database and cleanup */
    PQfinish(conn);
	
    return 0;
}