#include <stdio.h>
#include <string.h>
#include <ctype.h>
#include <time.h>
#include <unistd.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <sys/epoll.h>
#include <signal.h>

#include "my_netlib.h"

#define MAX_EVENTS 4
#define BUFSIZE 1024

/* --------------------------------------------------------------------------- */
void run( const int socket_fd );

/* ------------------------------------------------------- */
void read_stdin();

/* ------------------------------------------------------- */
void receive( const int socket_fd );

/* ------------------------------------------------------- */
void parse_message( const char * msg );

/* ------------------------------------------------------- */
static int session_alive = 0;

void
sigint_handle( int sig )
{
    (void)sig;
    session_alive = 0;
    fprintf( stderr, "\nKilled. exiting...\n" );
}

/* --------------------------------------------------------------------------- */
int
main( int argc, char **argv )
{
    char hostname[256];
    char port_number[8];

    // Ctrl-Cの割り込みシグナル(SIGINT)で呼び出される関数を登録
    signal( SIGINT, &sigint_handle );

    strcpy( hostname, "localhost" );
    strcpy( port_number, "21044" ); // 自分の学籍番号に含まれる数字列に変更する

    if ( argc != 3 )
    {
        fprintf( stderr, "Usage: %s hostname port\n", argv[0] );
        return 1;
    }

    strncpy( hostname, argv[1], sizeof( hostname ) - 1 );
    strncpy( port_number, argv[2], sizeof( port_number ) - 1 );

    fprintf( stderr, "INFO: host=%s port_number=%s\n", hostname, port_number );

    int socket_fd = connect_to_server( hostname, port_number );
    if ( socket_fd < 0 )
    {
        return 1;
    }
    session_alive = 1;

    run( socket_fd );
    close( socket_fd );

    return 0;
}

/* --------------------------------------------------------------------------- */
void
run( const int socket_fd )
{
    struct epoll_event events[MAX_EVENTS];

    const int epoll_fd = epoll_create( MAX_EVENTS );
    if ( epoll_fd == -1 )
    {
        perror( "epoll_create" );
        return;
    }

    {
        struct epoll_event ev;

        // 標準入力を登録
        memset( &ev, 0, sizeof( ev ) );
        ev.events = EPOLLIN;
        ev.data.fd = fileno( stdin );
        if ( epoll_ctl( epoll_fd, EPOLL_CTL_ADD, fileno( stdin ), &ev ) == -1 )
        {
            perror( "epoll_ctl" );
            close( epoll_fd );
            return;
        }

        // セッションソケットを登録
        memset( &ev, 0, sizeof( ev ) );
        ev.events = EPOLLIN;
        ev.data.fd = socket_fd;
        if ( epoll_ctl( epoll_fd, EPOLL_CTL_ADD, socket_fd, &ev ) == -1 )
        {
            perror( "epoll_ctl" );
            close( epoll_fd );
            return;
        }
    }

    while ( session_alive )
    {
        int nfds = epoll_wait( epoll_fd, events, MAX_EVENTS, 10 * 1000 );

        if ( nfds < 0 )
        {
            perror( "epoll_wait" );
            return;
        }
        else if ( nfds == 0 )
        {
            // timeout
            // 何もしない
        }
        else
        {
            for ( int i = 0; i < nfds; ++i )
            {
                if ( events[i].data.fd == fileno( stdin ) )
                {
                    // キーボードからの入力読み取り＋送信
                    read_stdin( socket_fd );
                }
                else
                {
                    if ( events[i].events & EPOLLIN
                         && events[i].data.fd == socket_fd )
                    {
                        // サーバからの受信
                        receive( socket_fd );
                    }
                }
            }
        }
    }
}

/* ------------------------------------------------------- */
void
read_stdin( const int socket_fd )
{
    char buf[BUFSIZE];
    memset( buf, 0, BUFSIZE );

    if ( fgets( buf, sizeof( buf ) - 1, stdin ) == NULL )
    {
        return;
    }

    buf[strcspn( buf, "\r\n" )] = '\0';

    int len = send( socket_fd, buf, strlen( buf ), 0 );
    if ( len < 0 )
    {
        perror( "send" );
    }

    fprintf( stderr, "send to server [%s]\n", buf );

    if ( strncmp( buf, "(quit)", strlen( "(quit)" ) ) == 0 )
    {
        session_alive = 0;
    }
}

/* ------------------------------------------------------- */
void
receive( const int socket_fd )
{
    char buf[BUFSIZE];
    memset( buf, 0, BUFSIZE );

    int len = recv( socket_fd, buf, sizeof( buf ), 0 );
    if ( len == -1 )
    {
        perror( "recv" );
        return;
    }

    if ( len == 0 )
    {
        fprintf( stderr, "disconnected from the server\n" );
        session_alive = 0;
        return;
    }

    const char *p = buf;
    while ( *p )
    {
        size_t nl = strcspn( p, "\r\n" );
        if ( nl == 0 )
        {
            break;
        }

        char msg[BUFSIZE];
        memset( msg, 0, BUFSIZE );
        strncpy( msg, p, nl );

        parse_message( msg );

        p += nl; // skip the parsed message
        while ( isspace( *p ) ) ++p; // skip space characters
    }
}


/* ------------------------------------------------------- */
void
parse_message( const char * msg )
{
    char command[128];
    if ( sscanf( msg, "(%127[^)]", command ) != 1 )
    {
        fprintf( stdout, "received illegal message: [%s]\n", msg );
        return;
    }

    if ( strncmp( command, "msg", 3 ) == 0 )
    {
        long raw_time;
        int client_id;
        char client_msg[512];
        if ( sscanf( msg, "(msg %ld %d \"%511[^\"]\")", &raw_time, &client_id, client_msg ) != 3 )
        {
            fprintf( stdout, "msg: illegal message [%s]\n", msg );
            return;
        }

        time_t msg_time = (time_t)(raw_time);
        struct tm *msg_tm = localtime( &msg_time );
        char time_str[128];
        strftime( time_str, 127, "%Y-%m-%d %H:%M:%S", msg_tm );
        fprintf( stdout, "message from %d (%s): %s\n", client_id, time_str, client_msg );
    }
    else if ( strncmp( command, "time", 4 ) == 0 )
    {
        char time_msg[512];
        if ( sscanf( msg, "(time \"%511[^\"]\")", time_msg ) != 1 )
        {
            fprintf( stdout, "time: illegal message [%s]\n", msg );
            return;
        }
        fprintf( stdout, "time: %s\n", time_msg );
    }
    else if ( strncmp( command, "hello", 5 ) == 0 )
    {
        fprintf( stdout, "receive [%s]\n", msg );
    }
    else if ( strncmp( command, "ok", 2 ) == 0 )
    {
        fprintf( stdout, "ok: [%s]\n", msg );
    }
    else if ( strncmp( command, "error", 5 ) == 0 )
    {
        fprintf( stdout, "error: something wrong in your message [%s]\n", msg );
    }
    else
    {
        fprintf( stderr, "unknown command: [%s]\n", command );
    }
}