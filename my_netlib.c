#include "my_netlib.h"

#include <netdb.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>
#include <sys/types.h>
#include <unistd.h>

/* --------------------------------------------------------------------------- */
int
create_server_socket( const char * port_number )
{
    const int MAX_QUEUE = SOMAXCONN;
    int server_socket = -1;
    struct addrinfo *my_address; // 待受け用アドレス情報を格納する変数

    {
        // 待受けアドレスの基本設定
        struct addrinfo hints;
        memset( &hints, 0, sizeof( hints ) );
        hints.ai_family = AF_INET; // 今回はIPv4専用
        hints.ai_socktype = SOCK_STREAM; // TCPソケットを指定
        hints.ai_flags = AI_PASSIVE; // 任意のホストからの接続を受け付ける設定

        // 基本設定とポート番号から自分のアドレス情報を作成
        int err = getaddrinfo( NULL, port_number, &hints, &my_address );
        if ( err != 0 )
        {
            printf( "getaddrinfo %d : %s\n", err, gai_strerror( err ) );
            return -1;
        }
    }

    // 待受け用アドレス情報を用いてソケット作成
    server_socket = socket( my_address->ai_family, my_address->ai_socktype, 0 );
    if ( server_socket < 0 )
    {
        perror( "socket" );
        freeaddrinfo( my_address );
        return -1;
    }

    // TCPポートを再利用できるように設定
    {
        int yes = 1;
        if ( setsockopt( server_socket, SOL_SOCKET, SO_REUSEADDR, (const char *)&yes, sizeof( yes ) ) != 0 )
        {
            perror( "setsockopt" );
            freeaddrinfo( my_address );
            close( server_socket );
            return -1;
        }
    }

    // ソケットに命名
    if ( bind( server_socket, my_address->ai_addr, my_address->ai_addrlen ) != 0 )
    {
        perror( "bind" );
        freeaddrinfo( my_address );
        close( server_socket );
        return -1;
    }

    // TCPクライアントからの接続要求を待てる状態にする
    if ( listen( server_socket, MAX_QUEUE ) == -1 )
    {
        perror( "listen" );
        freeaddrinfo( my_address );
        close( server_socket );
        return -1;

    }

    // メモリに確保したアドレス情報を解放
    freeaddrinfo( my_address );

    // 作成したソケットのファイルディスクリプタを返す
    return server_socket;
}


/* --------------------------------------------------------------------------- */
void
get_datetime_string( char *result,
                     const int size )
{
   time_t t;
   struct tm *tmp;

   t = time( NULL );
   tmp = localtime( &t );

   if ( tmp == NULL )
   {
       perror( "localtime" );
       return;
   }

   memset( result, 0, size );
   if ( strftime( result, size - 1, "%Y-%m-%d %H:%M:%S", tmp ) == 0 )
   {
       fprintf( stderr, "strftime returned 0.\n" );
   }
}


/* --------------------------------------------------------------------------- */
int
connect_to_server( const char * hostname,
                   const char * port_number )
{
    int socket_fd = 0;
    struct addrinfo hints; // ソケットアドレス構造体の基準を指定する変数
    struct addrinfo *dest = NULL; // 接続先アドレス情報を格納する変数
    struct addrinfo *d = NULL; // ループ用変数
    int err;

    // ホスト名・IPアドレス文字列、ポート番号からアドレス情報へ変換
    memset( &hints, 0, sizeof( hints ) );
    hints.ai_family = AF_INET; // 今回はIPv4専用
    hints.ai_socktype = SOCK_STREAM; // TCPソケットを指定

    // 名前解決
    err = getaddrinfo( hostname, port_number, &hints, &dest );
    if ( err != 0 )
    {
        fprintf( stderr, "getaddrinfo %d : %s\n", err, gai_strerror( err ) );
        return 1;
    }

    // 接続先アドレス情報を用いてソケットを作成しconnect
    for ( d = dest; d != NULL; d = d->ai_next )
    {
        socket_fd = socket( dest->ai_family, dest->ai_socktype, dest->ai_protocol );
        if ( socket_fd < 0 )
        {
            continue;
        }

        if ( connect( socket_fd, dest->ai_addr, dest->ai_addrlen ) != 0 )
        {
            close( socket_fd );
            continue;
        }

        break;
    }

    freeaddrinfo( dest );

    if ( d == NULL )
    {
        close( socket_fd );
        fprintf( stderr, "ERROR: Could not connect to the host\n" );
        return -1;
    }

    return socket_fd;
}