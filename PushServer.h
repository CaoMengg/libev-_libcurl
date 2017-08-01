#ifndef OCRSERVER_H
#define OCRSERVER_H

#include <netinet/in.h>
#include <sys/socket.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <fcntl.h>
#include <ev++.h>

#include <unistd.h>
#include <string.h>
#include <stdio.h>
#include <stdlib.h>
#include <iostream>
#include <map>

#include "rapidjson/document.h"
#include "rapidjson/writer.h"
#include "rapidjson/stringbuffer.h"

#include "curl/curl.h"
#include "GLog.h"
#include "YamlConf.h"
#include "SocketConnection.h"

typedef std::map<int, SocketConnection*> connectionMap;
typedef std::pair<int, SocketConnection*> connectionPair;

class PushServer
{
    private:
        PushServer()
        {
            config = new YamlConf( "conf/push_server.yaml" );
            intListenPort = config->getInt( "listen" );

            listenWatcher = new ev::io();
            curlMultiTimer = new ev::timer();
            curlMultiTimer->data = this;
        }
        ~PushServer()
        {
            delete listenWatcher;
            delete curlMultiTimer;

            if( intListenFd )
            {
                close( intListenFd );
            }

            if( multi ) {
                curl_multi_cleanup( multi );
            }
        }

        static PushServer *pInstance;
        YamlConf *config = NULL;
        int intListenPort = 0;
        int intListenFd = 0;
        ev::io *listenWatcher = NULL;
        connectionMap mapConnection;

    public:
        ev::default_loop mainLoop;
        static PushServer *getInstance();

        CURLM *multi = NULL;
        ev::timer *curlMultiTimer = NULL;
        int intCurlRunning = 1;

        SocketConnection* getConnection( int intFd );
        void start();

        void acceptCB( ev::io &watcher, int revents );
        void readCB( ev::io &watcher, int revents );
        void writeCB( ev::io &watcher, int revents );
        void curlSocketCB( ev::io &watcher, int revents );
        void curlTimeoutCB( ev::timer &timer, int revents );

        void recvQuery( SocketConnection *pConnection );
        void parseQuery( SocketConnection *pConnection );
        void ackQuery( SocketConnection *pConnection );
};

#endif
