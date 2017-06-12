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

void PushServer::readTimeoutCB( int intFd )
{
    connectionMap::iterator it;
    it = mapConnection.find( intFd );
    if( it == mapConnection.end() )
    {
        return;
    }
    SocketConnection* pConnection = it->second;
    LOG(WARNING) << "read timeout, fd=" << intFd;
    delete pConnection;
}

static void readTimeoutCallback( EV_P_ ev_timer *timer, int revents )
{
    (void)loop;
    (void)revents;
    PushServer::getInstance()->readTimeoutCB( ((SocketConnection *)timer->data)->intFd );
}

void PushServer::writeTimeoutCB( int intFd )
{
    connectionMap::iterator it;
    it = mapConnection.find( intFd );
    if( it == mapConnection.end() )
    {
        return;
    }
    SocketConnection* pConnection = it->second;
    LOG(WARNING) << "write timeout, fd=" << intFd;
    delete pConnection;
}

static void writeTimeoutCallback( EV_P_ ev_timer *timer, int revents )
{
    (void)loop;
    (void)revents;
    PushServer::getInstance()->writeTimeoutCB( ((SocketConnection *)timer->data)->intFd );
}

void PushServer::ackQuery( SocketConnection *pConnection )
{
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
                    delete pConnection;
                    return;
                }
            }
        }

        if( outBuf->intSentLen >= outBuf->intLen )
        {
            pConnection->outBufList.pop_front();
            delete outBuf;

            if( pConnection->status == csClosing )
            {
                LOG(INFO) << "close succ, fd=" << pConnection->intFd;
                delete pConnection;
                return;
            } else {
                LOG(INFO) << "ack query succ, fd=" << pConnection->intFd;
            }
        }
    }

    delete pConnection;
}

void PushServer::writeCB( int intFd )
{
    connectionMap::iterator it;
    it = mapConnection.find( intFd );
    if( it == mapConnection.end() )
    {
        return;
    }
    SocketConnection* pConnection = it->second;
    ackQuery( pConnection );
}

static void writeCallback( EV_P_ ev_io *watcher, int revents )
{
    (void)loop;
    (void)revents;
    PushServer::getInstance()->writeCB( watcher->fd );
}

void PushServer::parseQuery( SocketConnection *pConnection )
{
    pConnection->inBuf->data[pConnection->inBuf->intLen-1] = '\0';

    CURLcode res;
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
    curl_easy_cleanup( curl );

    tesseract::TessBaseAPI *api = new tesseract::TessBaseAPI();
    if( api->Init(NULL, "chi_sim") )
    {
        LOG(WARNING) << "tesseract api Init fail";
        //TODO
        return;
    }
    Pix *image = pixReadMem( pConnection->picBuf->data, pConnection->picBuf->intLen );
    api->SetImage( image );
    char *outText = api->GetUTF8Text();
    api->End();
    pixDestroy(&image);

    int outTextLen = strlen( outText );
    SocketBuffer* outBuf;
    outBuf = new SocketBuffer( outTextLen + 1 );
    outBuf->intLen = outTextLen;
    strncpy( (char*)outBuf->data, outText, outTextLen+1 );
    pConnection->outBufList.push_back( outBuf );
    delete[] outText;

    pConnection->inBuf->intLen = 0;
    pConnection->inBuf->intExpectLen = 0;

    ev_io_start( pMainLoop, pConnection->writeWatcher );

    ev_now_update( pMainLoop );
    ev_timer_set( pConnection->writeTimer, pConnection->writeTimeout, 0.0 );
    ev_timer_start( pMainLoop, pConnection->writeTimer );
}

void PushServer::closeConnection( SocketConnection *pConnection )
{
    SocketBuffer* outBuf = new SocketBuffer( 2 );
    outBuf->data[0] = 0x88;
    outBuf->data[1] = 0;
    outBuf->intLen = 2;
    pConnection->outBufList.push_back( outBuf );

    pConnection->status = csClosing;
    ev_io_start(pMainLoop, pConnection->writeWatcher);

    ev_timer_set( pConnection->writeTimer, pConnection->writeTimeout, 0.0 );
    ev_timer_start( pMainLoop, pConnection->writeTimer );
}

void PushServer::recvQuery( SocketConnection *pConnection )
{
    int n = recv( pConnection->intFd, pConnection->inBuf->data + pConnection->inBuf->intLen, pConnection->inBuf->intSize - pConnection->inBuf->intLen, 0 );
    if( n > 0 )
    {
        pConnection->inBuf->intLen += n;
        if( pConnection->inBuf->data[pConnection->inBuf->intLen-1] == '\n' )
        {
            LOG(INFO) << "recv query, fd=" << pConnection->intFd;
            ev_timer_stop( pMainLoop, pConnection->readTimer );
            parseQuery( pConnection );
        }
    } else if( n == 0 )
    {
        delete pConnection;
        return;
    } else {
        if( errno==EAGAIN || errno==EWOULDBLOCK )
        {
            return;
        } else {
            delete pConnection;
            return;
        }
    }
}

void PushServer::readCB( int intFd )
{
    connectionMap::iterator it;
    it = mapConnection.find( intFd );
    if( it == mapConnection.end() )
    {
        return;
    }
    SocketConnection* pConnection = it->second;

    if( pConnection->inBuf->intLen >= pConnection->inBuf->intSize )
    {
        pConnection->inBuf->enlarge();
    }
    recvQuery( pConnection );
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
            //TODO close process
            return;
        }
    }

    int flag = fcntl(acceptFd, F_GETFL, 0);
    fcntl(acceptFd, F_SETFL, flag | O_NONBLOCK);
    LOG(INFO) << "worker_id=" << intWorkerId << ", accept fd=" << acceptFd;

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

void PushServer::start()
{
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
    LOG(INFO) << "server start, listen succ port=" << intListenPort << " fd=" << intListenFd;

    listenWatcher = new ev_io();
    ev_io_init( listenWatcher, acceptCallback, intListenFd, EV_READ );
    ev_io_start( pMainLoop, listenWatcher );
    ev_run( pMainLoop, 0 );
    LOG(INFO) << "server start, listen port=" << intListenPort << " fd=" << intListenFd;
}