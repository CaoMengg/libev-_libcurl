#ifndef SOCKETCONNECTION_H
#define SOCKETCONNECTION_H

#include <stdio.h>
#include <string>
#include <list>
#include <stdlib.h>
#include <unistd.h>
#include <ev++.h>
#include "GLog.h"
#include "SocketBuffer.h"

typedef std::list<SocketBuffer *> bufferList;

enum enumConnectionStatus
{
    csInit,
    csAccepted,
    csConnected,
    csClosing,
};

class SocketConnection
{
    public:
        SocketConnection() {
            inBuf = new SocketBuffer( 1024 );
            upstreamBuf = new SocketBuffer( 1024 );

            readWatcher = new ev::io();
            writeWatcher = new ev::io();
            readTimer = new ev::timer();
            readTimer->data = this;
            writeTimer = new ev::timer();
            writeTimer->data = this;

            upstreamWatcher = new ev::io();
        }
        ~SocketConnection() {
            delete readWatcher;
            delete writeWatcher;
            delete readTimer;
            delete writeTimer;

            delete upstreamWatcher;

            if( intFd ) {
                close( intFd );
            }

            delete inBuf;
            delete upstreamBuf;

            while( ! outBufList.empty() ) {
                delete outBufList.front();
                outBufList.pop_front();
            }
        }
        void readTimeoutCB( ev::timer &timer, int revents );
        void writeTimeoutCB( ev::timer &timer, int revents );

        int intFd = 0;
        enumConnectionStatus status = csInit;
        ev::io *readWatcher = NULL;
        ev::io *writeWatcher = NULL;
        ev::timer *readTimer = NULL;
        ev::tstamp readTimeout = 3.0;
        ev::timer *writeTimer = NULL;
        ev::tstamp writeTimeout = 3.0;

        ev::io *upstreamWatcher = NULL;

        SocketBuffer *inBuf = NULL;
        SocketBuffer *upstreamBuf = NULL;
        bufferList outBufList;
};

#endif
