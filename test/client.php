<?php

$socket = socket_create(AF_INET, SOCK_STREAM, SOL_TCP);
$connection = socket_connect($socket, '127.0.0.1', '9090');

$arrData = array(
    array(
        'push_url' => 'http://test.myhug.cn/u/profile',
        'push_data' => 'uId=7bf4a120e384fae695800177&yUId=yaa1c99e26cd34a57a4b99ff2374&sig=myhug20140108',
    ),
);
$strMsg = json_encode($arrData);
$strMsg .= "\0";

$intLen = strlen( $strMsg );
if( socket_write( $socket, $strMsg, $intLen ) != $intLen ) {
    echo "send query fail\n";
    exit;
}
echo "send query succ\n";

$strResult = socket_read($socket, 10240);
var_dump( $strResult );
socket_close($socket); 
