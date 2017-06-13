#include "PushServer.h"

PushServer* PushServer::pInstance = NULL;

PushServer* PushServer::getInstance()
{
    if( pInstance == NULL )
    {
        pInstance = new PushServer();
    }
    return pInstance;
}

SocketConnection* PushServer::getConnection( int intFd )
{
    connectionMap::iterator it;
    it = mapConnection.find( intFd );
    if( it == mapConnection.end() )
    {
        LOG(WARNING) << "connection not found, fd=" << intFd;
        return NULL;
    }
    return it->second;
}

void PushServer::readTimeoutCB( int intFd )
{
    LOG(WARNING) << "read timeout, fd=" << intFd;
    SocketConnection* pConnection = getConnection( intFd );
    if( pConnection != NULL ) {
        delete pConnection;
    }
}

static void readTimeoutCallback( EV_P_ ev_timer *timer, int revents )
{
    (void)loop;
    (void)revents;
    PushServer::getInstance()->readTimeoutCB( ((SocketConnection *)timer->data)->intFd );
}

void PushServer::writeTimeoutCB( int intFd )
{
    LOG(WARNING) << "write timeout, fd=" << intFd;
    SocketConnection* pConnection = getConnection( intFd );
    if( pConnection != NULL ) {
        delete pConnection;
    }
}

static void writeTimeoutCallback( EV_P_ ev_timer *timer, int revents )
{
    (void)loop;
    (void)revents;
    PushServer::getInstance()->writeTimeoutCB( ((SocketConnection *)timer->data)->intFd );
}

void PushServer::writeCB( int intFd )
{
    SocketConnection* pConnection = getConnection( intFd );
    if( pConnection == NULL )
    {
        return;
    }

    SocketBuffer *outBuf = NULL;
    while( ! pConnection->outBufList.empty() )
    {
        outBuf = pConnection->outBufList.front();
        if( outBuf->intSentLen < outBuf->intLen )
        {
            int n = send( pConnection->intFd, outBuf->data + outBuf->intSentLen, outBuf->intLen - outBuf->intSentLen, 0 );
            if( n > 0 )
            {
                outBuf->intSentLen += n;
            } else {
                if( errno==EAGAIN || errno==EWOULDBLOCK )
                {
                    return;
                } else {
                    LOG(WARNING) << "send fail, fd=" << pConnection->intFd;
                    delete pConnection;
                    return;
                }
            }
        }

        if( outBuf->intSentLen >= outBuf->intLen )
        {
            pConnection->outBufList.pop_front();
            delete outBuf;
        }
    }

    ev_io_stop( pMainLoop, pConnection->writeWatcher );
    ev_timer_stop( pMainLoop, pConnection->writeTimer );
    LOG(INFO) << "ack query succ, fd=" << pConnection->intFd;
}

static void writeCallback( EV_P_ ev_io *watcher, int revents )
{
    (void)loop;
    (void)revents;
    PushServer::getInstance()->writeCB( watcher->fd );
}

void PushServer::parseQuery( SocketConnection *pConnection )
{
    /*CURLcode res;
    CURL *curl;
    curl = curl_easy_init();
    curl_easy_setopt( curl, CURLOPT_URL, pConnection->inBuf->data );  
    curl_easy_setopt( curl, CURLOPT_WRITEDATA, pConnection->picBuf );
    res = curl_easy_perform( curl );
    if( res != CURLE_OK )
    {
        LOG(WARNING) << "curl_easy_perform fail";
        //TODO
        return;
    }
    curl_easy_cleanup( curl );*/

    Document docJson;
    docJson.Parse( (const char*) pConnection->inBuf->data );
    LOG(INFO) << docJson[0]["token"].GetString();

    char outText[6] = "hello";
    int outTextLen = strlen( outText );
    SocketBuffer* outBuf;
    outBuf = new SocketBuffer( outTextLen + 1 );
    outBuf->intLen = outTextLen;
    strncpy( (char*)outBuf->data, outText, outTextLen+1 );
    pConnection->outBufList.push_back( outBuf );

    pConnection->inBuf->intLen = 0;
    pConnection->inBuf->intExpectLen = 0;

    ev_io_start( pMainLoop, pConnection->writeWatcher );
    ev_timer_set( pConnection->writeTimer, pConnection->writeTimeout, 0.0 );
    ev_timer_start( pMainLoop, pConnection->writeTimer );
}

void PushServer::readCB( int intFd )
{
    SocketConnection* pConnection = getConnection( intFd );
    if( pConnection == NULL )
    {
        return;
    }

    if( pConnection->inBuf->intLen >= pConnection->inBuf->intSize )
    {
        pConnection->inBuf->enlarge();
    }

    int n = recv( pConnection->intFd, pConnection->inBuf->data + pConnection->inBuf->intLen, pConnection->inBuf->intSize - pConnection->inBuf->intLen, 0 );
    if( n > 0 )
    {
        pConnection->inBuf->intLen += n;
        if( pConnection->inBuf->data[pConnection->inBuf->intLen-1] == '\0' )
        {
            LOG(INFO) << "recv query, fd=" << pConnection->intFd;
            ev_timer_stop( pMainLoop, pConnection->readTimer );
            parseQuery( pConnection );
        }
    } else if( n == 0 )
    {
        // client close normally
        delete pConnection;
        return;
    } else {
        if( errno==EAGAIN || errno==EWOULDBLOCK )
        {
            return;
        } else {
            LOG(WARNING) << "recv fail, fd=" << pConnection->intFd;
            delete pConnection;
            return;
        }
    }
}

static void readCallback( EV_P_ ev_io *watcher, int revents )
{
    (void)loop;
    (void)revents;
    PushServer::getInstance()->readCB( watcher->fd );
}

void PushServer::acceptCB()
{
    struct sockaddr_storage ss;
    socklen_t slen = sizeof(ss);
    int acceptFd = accept( intListenFd, (struct sockaddr*)&ss, &slen );
    if( acceptFd == -1 )
    {
        if( errno==EAGAIN || errno==EWOULDBLOCK )
        {
            return;
        } else {
            LOG(WARNING) << "accept fail";
            return;
        }
    }

    int flag = fcntl(acceptFd, F_GETFL, 0);
    fcntl(acceptFd, F_SETFL, flag | O_NONBLOCK);
    LOG(INFO) << "accept succ, fd=" << acceptFd;

    SocketConnection* pConnection = new SocketConnection();
    pConnection->pLoop = pMainLoop;
    pConnection->intFd = acceptFd;
    pConnection->status = csConnected;
    mapConnection[ acceptFd ] = pConnection;

    ev_io_init( pConnection->readWatcher, readCallback, acceptFd, EV_READ );
    ev_io_start( pMainLoop, pConnection->readWatcher );

    ev_init( pConnection->readTimer, readTimeoutCallback );
    ev_timer_set( pConnection->readTimer, pConnection->readTimeout, 0.0 );
    ev_timer_start( pMainLoop, pConnection->readTimer );

    ev_io_init( pConnection->writeWatcher, writeCallback, acceptFd, EV_WRITE );
    ev_init( pConnection->writeTimer, writeTimeoutCallback );
}

static void acceptCallback( EV_P_ ev_io *watcher, int revents )
{
    (void)loop;
    (void)watcher;
    (void)revents;
    PushServer::getInstance()->acceptCB();
}

static void event_cb(EV_P_ struct ev_io *w, int revents)
{
    std::cout << "event_cb" << std::endl;
    int rc = curl_multi_socket_action(g->multi, w->fd, action, &g->still_running);
}

static int curlSocketCallback(CURL *e, curl_socket_t s, int what, void *cbp, void *sockp)
{
    std::cout << s << " : " << what << std::endl;

    ev_io* ev = new ev_io();
    ev_io_init(ev, event_cb, s, EV_WRITE);
    ev_io_start(PushServer::getInstance()->pMainLoop, ev);

    if(what == CURL_POLL_REMOVE)
    {
        //remsock(fdp, g);
    }
    else
    {
        /*if( ! fdp )
          {
        //addsock(s, e, what, g);
        }
        else
        {
        //setsock(fdp, s, e, what, g);
        }*/
    }
    return 0;
}

static int curlTimerCallback(CURLM *multi, long timeout_ms, void *g)
{
    std::cout << "curlTimerCallback: " << timeout_ms << std::endl;
    if( timeout_ms == 0 ) {
        curl_multi_socket_action(multi, CURL_SOCKET_TIMEOUT, 0, &( PushServer::getInstance()->intCurlRunning) );
    }
    return 0;
}

void PushServer::start()
{
    multi = curl_multi_init();
    curl_multi_setopt(multi, CURLMOPT_SOCKETFUNCTION, curlSocketCallback);
    curl_multi_setopt(multi, CURLMOPT_SOCKETDATA, NULL);
    curl_multi_setopt(multi, CURLMOPT_TIMERFUNCTION, curlTimerCallback);
    curl_multi_setopt(multi, CURLMOPT_TIMERDATA, NULL);

    /*CURL *easy = curl_easy_init();
    curl_easy_setopt(easy, CURLOPT_URL, "http://www.baidu.com");
    //curl_easy_setopt(easy, CURLOPT_WRITEFUNCTION, write_cb);
    curl_easy_setopt(easy, CURLOPT_WRITEDATA, NULL);
    curl_easy_setopt(easy, CURLOPT_VERBOSE, 1L);
    curl_easy_setopt(easy, CURLOPT_NOPROGRESS, 0L);
    //curl_easy_setopt(easy, CURLOPT_PROGRESSFUNCTION, prog_cb);
    curl_easy_setopt(easy, CURLOPT_LOW_SPEED_TIME, 3L);
    curl_easy_setopt(easy, CURLOPT_LOW_SPEED_LIMIT, 10L);
    curl_multi_add_handle(multi, easy);*/

    struct sockaddr_in sin;
    sin.sin_family = AF_INET;
    sin.sin_addr.s_addr = 0;
    sin.sin_port = htons( intListenPort );

    intListenFd = socket(AF_INET, SOCK_STREAM, 0);
    int flag = 1;
    setsockopt(intListenFd, SOL_SOCKET, SO_REUSEADDR, &flag, sizeof(int));
    flag = fcntl(intListenFd, F_GETFL, 0);
    fcntl(intListenFd, F_SETFL, flag | O_NONBLOCK);

    int intRet;
    intRet = bind( intListenFd, (struct sockaddr*)&sin, sizeof(sin) );
    if( intRet != 0 )
    {
        LOG(WARNING) << "bind fail";
        return;
    }

    intRet = listen( intListenFd, 255 );
    if( intRet != 0 )
    {
        LOG(WARNING) << "listen fail";
        return;
    }
    LOG(INFO) << "server start, listen port=" << intListenPort << " fd=" << intListenFd;

    listenWatcher = new ev_io();
    ev_io_init( listenWatcher, acceptCallback, intListenFd, EV_READ );
    ev_io_start( pMainLoop, listenWatcher );
    ev_run( pMainLoop, 0 );
}
