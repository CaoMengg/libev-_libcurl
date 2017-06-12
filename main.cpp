#include "main.h"

int main()
{
    initGLog( "push_server" );

    CURLcode res;
    res = curl_global_init( CURL_GLOBAL_ALL );
    if( res != CURLE_OK )
    {
        LOG(WARNING) << "curl_global_init fail";
        return 1;
    }

    PushServer::getInstance()->start();
    curl_global_cleanup();
}
