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

void PushServer::writeCB( ev::io &watcher, int revents )
{
    (void)revents;

    SocketConnection* pConnection = getConnection( watcher.fd );
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

    pConnection->writeWatcher->stop();
    pConnection->writeTimer->stop();
    LOG(INFO) << "ack query succ, fd=" << pConnection->intFd;
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
    rapidjson::Document docJson;
    docJson.Parse( (const char*) pConnection->inBuf->data );
    LOG(INFO) << docJson[0]["push_url"].GetString();

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

void PushServer::readCB( ev::io &watcher, int revents )
{
    (void)revents;

    SocketConnection* pConnection = getConnection( watcher.fd );
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
            pConnection->readTimer->stop();
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

void PushServer::acceptCB( ev::io &watcher, int revents )
{
    (void)watcher;
    (void)revents;

    struct sockaddr_storage ss;
    socklen_t slen = sizeof( ss );
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
    pConnection->intFd = acceptFd;
    pConnection->status = csConnected;
    mapConnection[ acceptFd ] = pConnection;

    pConnection->readWatcher->set( pConnection->intFd, ev::READ );
    pConnection->readWatcher->set<PushServer, &PushServer::readCB>( this );
    pConnection->readWatcher->start();

    pConnection->readTimer->set( pConnection->readTimeout, 0.0 );
    pConnection->readTimer->set<SocketConnection, &SocketConnection::readTimeoutCB>( pConnection );
    pConnection->readTimer->start();

    pConnection->writeWatcher->set( pConnection->intFd, ev::WRITE );
    pConnection->writeWatcher->set<PushServer, &PushServer::writeCB>( this );

    pConnection->writeTimer->set( pConnection->writeTimeout, 0.0 );
    pConnection->writeTimer->set<SocketConnection, &SocketConnection::writeTimeoutCB>( pConnection );
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

                pConnection->writeWatcher->start();
                pConnection->writeTimer->start();
            }
            else {
                delete pConnection;
            }

            curl_multi_remove_handle(PushServer::getInstance()->multi, easy);
            curl_easy_cleanup(easy);
        }
    }
}

void PushServer::curlSocketCB( ev::io &watcher, int revents )
{
    int action = (revents&ev::READ ? CURL_POLL_IN : 0) | (revents&ev::WRITE ? CURL_POLL_OUT : 0);
    CURLMcode rc;
    rc = curl_multi_socket_action(PushServer::getInstance()->multi, watcher.fd, action, &(PushServer::getInstance()->intCurlRunning));
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
    curl_easy_getinfo( e, CURLINFO_PRIVATE, &pConnection );

    if( what == CURL_POLL_REMOVE )
    {
        pConnection->upstreamWatcher->stop();
    }
    else
    {
        int kind = (what&CURL_POLL_IN ? ev::READ : 0) | (what&CURL_POLL_OUT ? ev::WRITE : 0);
        pConnection->upstreamWatcher->stop();
        pConnection->upstreamWatcher->set( s, kind );
        pConnection->upstreamWatcher->set<PushServer, &PushServer::curlSocketCB>( PushServer::getInstance() );
        pConnection->upstreamWatcher->start();
    }
    return 0;
}

void PushServer::curlTimeoutCB( ev::timer &timer, int revents )
{
    (void)timer;
    (void)revents;

    CURLMcode rc;
    rc = curl_multi_socket_action(PushServer::getInstance()->multi, CURL_SOCKET_TIMEOUT, 0, &(PushServer::getInstance()->intCurlRunning) );
    if( rc != CURLM_OK ) {
        // TODO
    }
    check_multi_info();

    std::cout << "curlTimeoutCB: " << PushServer::getInstance()->intCurlRunning << std::endl;
}

static int curlTimerCallback(CURLM *multi, long timeout_ms, void *g)
{
    //std::cout << "curlTimerCallback: " << timeout_ms << std::endl;
    (void)multi;
    (void)g;
    PushServer* pPushServer = PushServer::getInstance();
    pPushServer->curlMultiTimer->stop();

    if( timeout_ms == 0 ) {
        pPushServer->curlTimeoutCB( *(pPushServer->curlMultiTimer), 0 );
    } else {
        double t = timeout_ms / 1000;
        pPushServer->curlMultiTimer->set( t, 0.0 );
        pPushServer->curlMultiTimer->set<PushServer, &PushServer::curlTimeoutCB>( pPushServer );
        pPushServer->curlMultiTimer->start();
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

    listenWatcher->set<PushServer, &PushServer::acceptCB>( this );
    listenWatcher->start( intListenFd, ev::READ );
    mainLoop.run( 0 );
}
