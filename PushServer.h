#ifndef OCRSERVER_H
#define OCRSERVER_H

#include <netinet/in.h>
#include <sys/socket.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <fcntl.h>
#include <ev.h>

#include <unistd.h>
#include <string.h>
#include <stdio.h>
#include <stdlib.h>
#include <map>

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
        }
        ~PushServer()
        {
            if( listenWatcher )
            {
                ev_io_stop(pMainLoop, listenWatcher);
                delete listenWatcher;
            }

            if( intListenFd )
            {
                close( intListenFd );
            }
        }

        static PushServer *pInstance;
        YamlConf *config = NULL;
        struct ev_loop *pMainLoop = EV_DEFAULT;
        int intListenPort = 0;
        int intListenFd = 0;
        ev_io *listenWatcher = NULL;
        connectionMap mapConnection;
    public:
        static PushServer *getInstance();
        SocketConnection* getConnection( int intFd );
        void start();
        void acceptCB();
        void readCB( int intFd );
        void writeCB( int intFd );
        void readTimeoutCB( int intFd );
        void writeTimeoutCB( int intFd );
        void recvQuery( SocketConnection *pConnection );
        void parseQuery( SocketConnection *pConnection );
        void ackQuery( SocketConnection *pConnection );
};

#endif
