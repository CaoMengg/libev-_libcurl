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

static size_t write_cb(void *ptr, size_t size, size_t nmemb, void *data)
{
    size_t realsize = size * nmemb;
    SocketConnection * pConnection = (SocketConnection *)data;
    //memcpy( pConnection->upstreamBuf->data + pConnection->upstreamBuf->intLen, ptr, realsize );
    pConnection->upstreamBuf->intLen += realsize;
    std::cout << (char *)ptr << std::endl;
    return realsize;
}

void PushServer::parseQuery( SocketConnection *pConnection )
{
    Document docJson;
    docJson.Parse( (const char*) pConnection->inBuf->data );
    //LOG(INFO) << docJson[0]["push_url"].GetString();

    CURL *easy = curl_easy_init();
    curl_easy_setopt(easy, CURLOPT_URL, docJson[0]["push_url"].GetString());
    curl_easy_setopt(easy, CURLOPT_POSTFIELDS, docJson[0]["push_data"].GetString());
    curl_easy_setopt(easy, CURLOPT_WRITEFUNCTION, write_cb);
    curl_easy_setopt(easy, CURLOPT_WRITEDATA, pConnection);
    curl_easy_setopt(easy, CURLOPT_PRIVATE, pConnection);
    //curl_easy_setopt(easy, CURLOPT_VERBOSE, 1L);
    //curl_easy_setopt(easy, CURLOPT_NOPROGRESS, 0L);
    //curl_easy_setopt(easy, CURLOPT_LOW_SPEED_TIME, 3L);
    //curl_easy_setopt(easy, CURLOPT_LOW_SPEED_LIMIT, 10L);
    curl_multi_add_handle(multi, easy);
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

static void check_multi_info()
{
    CURLMsg *msg;
    int msgs_left;
    CURL *easy;
    CURLcode res;

    while( (msg = curl_multi_info_read(PushServer::getInstance()->multi, &msgs_left)) ) {
        if( msg->msg == CURLMSG_DONE ) {
            easy = msg->easy_handle;
            SocketConnection *pConnection;
            curl_easy_getinfo(easy, CURLINFO_PRIVATE, &pConnection);

            res = msg->data.result;
            if( res == 0 ) {
                char outText[6] = "world";
                int outTextLen = strlen( outText );
                SocketBuffer* outBuf;
                outBuf = new SocketBuffer( outTextLen + 1 );
                outBuf->intLen = outTextLen;
                strncpy( (char*)outBuf->data, outText, outTextLen+1 );
                pConnection->outBufList.push_back( outBuf );

                pConnection->inBuf->intLen = 0;
                pConnection->inBuf->intExpectLen = 0;

                ev_io_start( pConnection->pLoop, pConnection->writeWatcher );
                ev_timer_set( pConnection->writeTimer, pConnection->writeTimeout, 0.0 );
                ev_timer_start( pConnection->pLoop, pConnection->writeTimer );
            }
            else {
                delete pConnection;
            }

            curl_multi_remove_handle(PushServer::getInstance()->multi, easy);
            curl_easy_cleanup(easy);
        }
    }
}

static void event_cb(EV_P_ struct ev_io *w, int revents)
{
    (void)loop;
    int action = (revents&EV_READ ? CURL_POLL_IN : 0) | (revents&EV_WRITE ? CURL_POLL_OUT : 0);
    CURLMcode rc;
    rc = curl_multi_socket_action(PushServer::getInstance()->multi, w->fd, action, &(PushServer::getInstance()->intCurlRunning));
    if( rc != CURLM_OK ) {
        // TODO
    }
    check_multi_info();
}

static int curlSocketCallback(CURL *e, curl_socket_t s, int what, void *cbp, void *sockp)
{
    (void)cbp;
    (void)sockp;
    SocketConnection *pConnection;
    curl_easy_getinfo(e, CURLINFO_PRIVATE, &pConnection);

    if(what == CURL_POLL_REMOVE)
    {
        ev_io_stop(PushServer::getInstance()->pMainLoop, pConnection->upstreamWatcher);
    }
    else
    {
        int kind = (what&CURL_POLL_IN ? EV_READ : 0) | (what&CURL_POLL_OUT ? EV_WRITE : 0);
        ev_io_stop(PushServer::getInstance()->pMainLoop, pConnection->upstreamWatcher);
        ev_io_init(pConnection->upstreamWatcher, event_cb, s, kind);
        ev_io_start(PushServer::getInstance()->pMainLoop, pConnection->upstreamWatcher);
    }
    return 0;
}

static void curlTimerCB(EV_P_ struct ev_timer *w, int revents)
{
    (void)loop;
    (void)w;
    (void)revents;

    CURLMcode rc;
    rc = curl_multi_socket_action(PushServer::getInstance()->multi, CURL_SOCKET_TIMEOUT, 0, &(PushServer::getInstance()->intCurlRunning) );
    if( rc != CURLM_OK ) {
        // TODO
    }
    check_multi_info();

    std::cout << "curlTimerCB: " << PushServer::getInstance()->intCurlRunning << std::endl;
}

static int curlTimerCallback(CURLM *multi, long timeout_ms, void *g)
{
    //std::cout << "curlTimerCallback: " << timeout_ms << std::endl;
    (void)multi;
    (void)g;
    PushServer* pPushServer = PushServer::getInstance();
    ev_timer_stop(pPushServer->pMainLoop, pPushServer->curlMultiTimer);

    if( timeout_ms == 0 ) {
        curlTimerCB(pPushServer->pMainLoop, pPushServer->curlMultiTimer, 0);
    } else {
        double t = timeout_ms / 1000;
        ev_timer_init(pPushServer->curlMultiTimer, curlTimerCB, t, 0.0);
        ev_timer_start(pPushServer->pMainLoop, pPushServer->curlMultiTimer);
    }
    return 0;
}

void PushServer::start()
{
    multi = curl_multi_init();
    if( multi == NULL ) {
        LOG(WARNING) << "curl_multi_init fail";
        return;
    }
    curl_multi_setopt(multi, CURLMOPT_TIMERFUNCTION, curlTimerCallback);
    curl_multi_setopt(multi, CURLMOPT_TIMERDATA, NULL);
    curl_multi_setopt(multi, CURLMOPT_SOCKETFUNCTION, curlSocketCallback);
    curl_multi_setopt(multi, CURLMOPT_SOCKETDATA, NULL);

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
    curl_multi_cleanup( multi );
}
