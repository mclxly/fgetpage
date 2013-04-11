#!/bin/sh
# Build project

g++ -Wall -W -lcurl -levent -lmysqlclient -L/usr/libevent/lib -L/usr/lib64/mysql -I/usr/libevent/include -I/usr/include/mysql test_libevent.c
