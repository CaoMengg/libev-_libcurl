#include "SocketConnection.h"

void SocketConnection::readTimeoutCB( ev::timer &timer, int revents )
{
    (void)revents;
    (void)timer;
    LOG(WARNING) << "read timeout, fd=" << this->intFd;
    delete this;
}

void SocketConnection::writeTimeoutCB( ev::timer &timer, int revents )
{
    (void) revents;
    (void)timer;
    LOG(WARNING) << "write timeout, fd=" << this->intFd;
    delete this;
}
