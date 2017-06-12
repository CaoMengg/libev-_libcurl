<?php

$socket = socket_create(AF_INET, SOCK_STREAM, SOL_TCP);
$connection = socket_connect($socket, '127.0.0.1', '9090');

$arrData = array(
    array(
        'type' => 1,
        'token' => "282695f6d4176375a6e44639d8645a91b5827df649a6b28978b239695ef0babe",
        'payload' => '{"aps":{"alert":{"body":"abc","launch-image":"icon_120"},"sound":"default","badge":1},"avalon":{"type":"msg"}}',
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

$strResult = socket_read($socket, 1024);
var_dump( $strResult );
socket_close($socket); 
