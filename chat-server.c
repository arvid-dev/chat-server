#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <unistd.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <netdb.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <netdb.h>

#include <sys/epoll.h>
#include <signal.h>

#include "my_netlib.h"

#define MAX_EVENTS 16
#define BUFSIZE 1024
#define MAX_HISTORY 32

#define MESSAGE_LOG "message.log"

// コマンドの先頭文字列をマクロで定義しておく
#define COMMAND_MESSAGE "msg"
#define COMMAND_FIND "find"
#define COMMAND_HISTORY "history"
#define COMMAND_TIME "time"
#define COMMAND_HELLO "hello"
#define COMMAND_QUIT "quit"

// コマンドのタイプを列挙型で管理する
typedef enum {
    CMD_UNKNOWN,
    CMD_MESSAGE,
    CMD_FIND,
    CMD_HISTORY,
    CMD_TIME,
    CMD_HELLO,
    CMD_QUIT,
} Command;

typedef struct {
    int id;
    int alive;
    int socket_fd;
} Client;

/* ------------------------------------------------------- */
void run( const int server_socket );

/* ------------------------------------------------------- */
void read_stdin();

/* ------------------------------------------------------- */
int create_session( const int server_socket, const int epoll_fd );

/* ------------------------------------------------------- */
void receive( const int epoll_fd, struct epoll_event *ev );

/* ------------------------------------------------------- */
Command parse_command( const char * msg );

void send_message_to_all( Client *sender, const char *recv_msg );
void find_message( Client *sender, const char *recv_msg );
void send_history( Client *sender, const char *recv_msg );
void reply_time_message( Client *sender, const char *recv_msg );
void reply_hello( Client *sender, const char *recv_msg );
void reply_unknown_command( Client *sender, const char *recv_msg );
void disable_client( Client *sender, const char *recv_msg );

void save_message( const time_t msg_time, const int sender_id, const char *msg );

/* ------------------------------------------------------- */
static int server_alive = 0;
static int client_count = 0;
static Client* clients[MAX_EVENTS-1];

void
sigint_handle( int sig )
{
    (void)sig;
    server_alive = 0;
    fprintf( stderr, "\nKilled. exiting...\n" );
}

/* ------------------------------------------------------- */
int
main( int argc, char **argv )
{
    char port_number[8];
    int server_socket = -1;

    // Ctrl-Cの割り込みシグナル(SIGINT)で呼び出される関数を登録
    signal( SIGINT, &sigint_handle );

    for ( int i = 0; i < MAX_EVENTS-1; ++i )
    {
        clients[i] = NULL;
    }

    memset( port_number, 0, sizeof( port_number ) );
    strcpy( port_number, "21044" ); // 自分の学籍番号に含まれる数字列に変更する
    if ( argc > 1 )
    {
        strncpy( port_number, argv[1], sizeof( port_number ) - 1 );
    }

    server_socket = create_server_socket( port_number );

    if ( server_socket < 0 )
    {
        return 1;
    }

    fprintf( stderr, "Waiting a connection...\n" );
    server_alive = 1;
    run( server_socket );
    close( server_socket );

    for ( int i = 0; i < MAX_EVENTS-1; ++i )
    {
        if ( clients[i] != NULL )
        {
            free( clients[i] );
            clients[i] = NULL;
        }
    }

    return 0;
}

/* ------------------------------------------------------- */
void
run( const int server_socket )
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

        // 標準入力を登録する場合、ここに必要な処理を記述
        memset( &ev, 0, sizeof( ev ) );
        ev.events = EPOLLIN;
        ev.data.fd = fileno( stdin );
        if ( epoll_ctl( epoll_fd, EPOLL_CTL_ADD, fileno( stdin ), &ev ) == -1 )
        {
            perror( "epoll_ctl" );
            close( epoll_fd );
            return;
        }

        // 待受ソケットをイベント登録
        memset( &ev, 0, sizeof( ev ) );
        ev.events = EPOLLIN;
        ev.data.fd = server_socket;
        ev.data.ptr = malloc( sizeof( Client ) );
        if ( ev.data.ptr == NULL )
        {
            perror ( "malloc" );
            return;
        }
        memset( ev.data.ptr, 0, sizeof( Client ) );
        Client *cli = (Client *)(ev.data.ptr);
        cli->socket_fd = server_socket;

        if ( epoll_ctl( epoll_fd, EPOLL_CTL_ADD, server_socket, &ev ) == -1 )
        {
            perror( "epoll_ctl" );
            close( epoll_fd );
            return;
        }
    }

    int timeout_count = 0;
    while ( server_alive )
    {
        int nfds = epoll_wait( epoll_fd, events, MAX_EVENTS, 10 * 1000 );

        if ( nfds < 0 )
        {
            perror( "epoll_wait" );
            server_alive = 0;
        }
        else if ( nfds == 0 )
        {
            // timeout
            ++timeout_count;
            fprintf( stderr, "Timeout: %d\n", timeout_count );
        }
        else
        {
            timeout_count = 0;
            for ( int i = 0; i < nfds; ++i )
            {
                // 標準入力を扱う場合は、ここで標準入力かどうかを判定して分岐する
                if ( events[i].data.fd == fileno( stdin ) )
                {
                    // キーボードからの入力読み取り
                    read_stdin();
                }
                else
                {
                    Client *cli = events[i].data.ptr;
                    if ( cli->socket_fd == server_socket )
                    {
                        // acceptして新しいクライアントを登録する
                        create_session( server_socket, epoll_fd );
                    }
                    else
                    {
                        if ( events[i].events & EPOLLIN )
                        {
                            // クライアントからの受信
                            receive( epoll_fd, &events[i] );
                        }
                    }
                }
            }
        }
    }
}


/* ------------------------------------------------------- */
void
read_stdin()
{
    char buf[BUFSIZE];
    memset( buf, 0, BUFSIZE );

    if ( fgets( buf, sizeof( buf ) - 1, stdin ) == NULL )
    {
        return;
    }

    buf[strcspn( buf, "\r\n" )] = '\0';

    if ( strncmp( buf, "quit", 4 ) == 0 )
    {
        fprintf( stderr, "ok. quit all.\n" );
        server_alive = 0;
    }
}

/* ------------------------------------------------------- */
int
create_session( const int server_socket,
                const int epoll_fd )
{
    if ( client_count + 1 >= MAX_EVENTS )
    {
        fprintf( stderr, "Over the max session\n" );
        return 0;
    }

    Client *client = malloc( sizeof( Client ) ); // メモリ確保
    if ( client == NULL )
    {
        perror( "malloc" );
        return 0;
    }

    struct sockaddr_in addr;
    socklen_t len = sizeof( addr );
    client->socket_fd = accept( server_socket, (struct sockaddr *)&addr, &len );
    if ( client->socket_fd < 0 )
    {
        perror( "accept" );
        free( client );
        return 0;
    }
    client->id = ++client_count;
    client->alive = 1;

    struct epoll_event ev;
    memset( &ev, 0, sizeof( ev ) );
    ev.events = EPOLLIN;
    ev.data.fd = client->socket_fd;
    ev.data.ptr = client;

    if ( epoll_ctl( epoll_fd, EPOLL_CTL_ADD, client->socket_fd, &ev ) == -1 )
    {
        perror( "epoll_ctl" );
        close( client->socket_fd );
        free( client );
        return 0;
    }

    for ( int i = 0; i < MAX_EVENTS - 1; ++i )
    {
        if ( clients[i] == NULL )
        {
            clients[i] = client;
            break;
        }
    }

    fprintf( stderr, "Accepted %s:%d\n", inet_ntoa( addr.sin_addr ), ntohs( addr.sin_port ) );
    return 1;
}

/* ------------------------------------------------------- */
void
receive( const int epoll_fd,
         struct epoll_event *ev )
{
    Client *cli = ev->data.ptr;

    char recv_buf[512];
    int len = recv( cli->socket_fd, recv_buf, sizeof( recv_buf ), 0 );
    if ( len == -1 )
    {
        perror( "recv" );
        return;
    }
    else if ( len == 0 )
    {
        fprintf( stdout, "[client:%d] disconnected.\n", cli->id );
        cli->alive = 0;
    }
    else
    {
        recv_buf[len] = '\0';
        recv_buf[strcspn( recv_buf, "\r\n" )] = '\0';
        fprintf( stdout, "[client:%d] received=\"%s\"\n", cli->id, recv_buf );

        char com[128];
        if ( sscanf( recv_buf, "(%127[^)]", com ) != 1 )
        {
            reply_unknown_command( cli, recv_buf );
        }
        else
        {
            switch ( parse_command( com ) ) {
            case CMD_MESSAGE:
                send_message_to_all( cli, recv_buf );
                break;
            case CMD_FIND:
                find_message( cli, recv_buf );
                break;
            case CMD_HISTORY:
                send_history( cli, recv_buf );
                break;
            case CMD_TIME:
                reply_time_message( cli, recv_buf );
                break;
            case CMD_HELLO:
                reply_hello( cli, recv_buf );
                break;
            case CMD_QUIT:
                disable_client( cli, recv_buf );
                break;
            default:
                reply_unknown_command( cli, recv_buf );
                break;
            };
        }
    }

    // 終了したクライアントへの対応
    if ( cli->alive == 0 )
    {
        // epollからソケットの登録を削除
        if ( epoll_ctl( epoll_fd, EPOLL_CTL_DEL, cli->socket_fd, ev ) != 0 )
        {
            perror( "epoll_ctl" );
        }

        // 参照用のポインタ配列から削除
        for ( int i = 0; i < MAX_EVENTS - 1; ++i )
        {
            if ( clients[i] == NULL ) continue;
            if ( clients[i]->id == cli->id )
            {
                clients[i] = NULL;
            }
        }
        close( cli->socket_fd ); // ソケットを閉じて
        free( ev->data.ptr ); // メモリを解放
    }
}

/* ------------------------------------------------------- */
Command
parse_command( const char *msg )
{
    if ( strncmp( msg, COMMAND_MESSAGE, strlen( COMMAND_MESSAGE ) ) == 0 )
    {
        return CMD_MESSAGE;
    }
    else if ( strncmp( msg, COMMAND_FIND, strlen( COMMAND_FIND ) ) == 0 )
    {
        return CMD_FIND;
    }
    else if ( strncmp( msg, COMMAND_HISTORY, strlen( COMMAND_HISTORY ) ) == 0 )
    {
        return CMD_HISTORY;
    }
    else if ( strncmp( msg, COMMAND_TIME, strlen( COMMAND_TIME ) ) == 0 )
    {
        return CMD_TIME;
    }
    else if ( strncmp( msg, COMMAND_HELLO, strlen( COMMAND_HELLO ) ) == 0 )
    {
        return CMD_HELLO;
    }
    else if ( strncmp( msg, COMMAND_QUIT, strlen( COMMAND_QUIT ) ) == 0 )
    {
        return CMD_QUIT;
    }

    return CMD_UNKNOWN;
}

/* ------------------------------------------------------- */
void
send_message_to_all( Client *sender, const char *recv_msg )
{
    char buf[BUFSIZE];
    char msg[512];

    // (msg "MSG") という形式の文字列を想定し、MSGのみを取り出す
    // COMMAND_MESSAGEは "(msg " に置き換えられれる
    // %511[^\"] は '"'を含まない文字列を511文字まで読み取ることを意味する
    if ( sscanf( recv_msg, "("COMMAND_MESSAGE" \"%511[^\"]\")", msg ) != 1
         || strlen( msg ) == 0 )
    {
        fprintf( stderr, "ERROR: received an illegal message [%s]\n", recv_msg );
        snprintf( buf, BUFSIZE - 1, "(error illegal_message [%s])\n", recv_msg );
        int len = send( sender->socket_fd, buf, strlen( buf ), 0 );
        if ( len < 0 )
        {
            perror( "send" );
        }
        return;
    }

    time_t current_time = time( NULL );

    save_message( current_time, sender->id, msg );

    snprintf( buf, BUFSIZE - 1, "(msg %ld %d \"%s\")\n", current_time, sender->id, msg );
    fprintf( stderr, "send message to all [%s]\n", msg );

    // 生きている他のクライアントへメッセージ送信
    for ( int i = 0; i < MAX_EVENTS - 1; ++i )
    {
        if ( clients[i] == NULL ) continue;
        if ( clients[i]->alive == 0 ) continue;
        if ( clients[i]->socket_fd == sender->socket_fd ) continue;

        fprintf( stderr, "send message to client:%d\n", clients[i]->id );

        int len = send( clients[i]->socket_fd, buf, strlen( buf ), 0 );
        if ( len < 0 )
        {
            perror( "send" );
        }
    }

    // 確認メッセージを送信者へ返信
    {
        snprintf( buf, BUFSIZE - 1, "(ok msg \"%s\")\n", msg);
        int len = send( sender->socket_fd, buf, strlen( buf ), 0 );
        if ( len < 0 )
        {
            perror( "send" );
        }
    }
}

/* ------------------------------------------------------- */
void find_message( Client *sender, const char *recv_msg )
{
    char keyword[512];
    if ( sscanf( recv_msg, "("COMMAND_FIND" \"%511[^\"]\")", keyword ) != 1
         || strlen( keyword ) == 0 )
    {
        char buf[BUFSIZE];
        fprintf( stderr, "ERROR: received an illegal command [%s]\n", recv_msg );
        snprintf( buf, BUFSIZE - 1, "(error illegal_command [%s])\n", recv_msg );
        int len = send( sender->socket_fd, buf, strlen( buf ), 0 );
        if ( len < 0 )
        {
            perror( "send" );
        }
        return;
    }

    FILE *fp = fopen( MESSAGE_LOG, "r" );
    if ( fp == NULL )
    {
        fprintf( stderr, "ERROR: could not open the file [%s]\n", MESSAGE_LOG );
        return;
    }

    char line_buf[BUFSIZE];
    int n_line = 0;
    while ( fgets( line_buf, BUFSIZE - 1, fp ) != NULL )
    {
        ++n_line;

        long unix_time;
        int id;
        int n_read = 0;
        char msg[512];
        if ( sscanf( line_buf, "%ld %d %n", &unix_time, &id, &n_read ) != 2 )
        {
            fprintf( stderr, "ERROR: illegal data at line %d in %s\n", n_line, MESSAGE_LOG );
            continue;
        }
        strcpy( msg, line_buf + n_read );
        msg[strcspn( msg, "\r\n" )] = '\0';

        // keywardを含む文字列かどうかを判定
        if ( strstr( msg, keyword ) != NULL )
        {
            char buf[BUFSIZE];
            snprintf( buf, BUFSIZE - 1, "(msg %ld %d \"%s\")\n", unix_time, id, msg );
            int len = send( sender->socket_fd, buf, strlen( buf ), 0 );
            if ( len < 0 )
            {
                perror( "send" );
            }
        }
    }

    fclose( fp );
}

/* ------------------------------------------------------- */
void
send_history( Client *sender, const char *recv_msg )
{
    int history_size = 0;
    if ( sscanf( recv_msg, "("COMMAND_HISTORY" %d)", &history_size ) != 1 )
    {
        char buf[BUFSIZE];
        snprintf( buf, BUFSIZE - 1, "(error illegal_command [%s])\n", recv_msg );
        fprintf( stderr, "illegal command %s\n", recv_msg );
        int len = send( sender->socket_fd, buf, strlen( buf ), 0 );
        if ( len < 0 )
        {
            perror( "send" );
        }
        return;
    }

    if ( history_size <= 0
         || 10 < history_size )
    {
        char buf[BUFSIZE];
        snprintf( buf, BUFSIZE - 1, "(error illegal_history_size %d)\n", history_size );
        fprintf( stderr, "illegal history size [%s]\n", recv_msg );
        int len = send( sender->socket_fd, buf, strlen( buf ), 0 );
        if ( len < 0 )
        {
            perror( "send" );
        }
        return;
    }

    FILE *fp = fopen( MESSAGE_LOG, "r" );
    if ( fp == NULL )
    {
        fprintf( stderr, "ERROR: could not open the file [%s]\n", MESSAGE_LOG );
        return;
    }

    long time_history[10];
    int id_history[10];
    char msg_history[10][512];
    int index = 0;
    char line_buf[BUFSIZE];
    int n_line = 0;
    int read_count = 0;
    while ( fgets( line_buf, BUFSIZE - 1, fp ) != NULL )
    {
        ++n_line;

        long unix_time;
        int id;
        int n_read = 0;
        char msg[512];
        if ( sscanf( line_buf, "%ld %d %n", &unix_time, &id, &n_read ) != 2 )
        {
            fprintf( stderr, "ERROR: illegal data at line %d in %s\n", n_line, MESSAGE_LOG );
            continue;
        }
        strcpy( msg, line_buf + n_read );
        msg[strcspn( msg, "\r\n" )] = '\0';

        time_history[index] = unix_time;
        id_history[index] = id;
        strcpy( msg_history[index], msg );
        if ( ++index >= 10 ) index = 0;
        ++read_count;
    }

    fclose( fp );

    int read_size = ( history_size < read_count
                      ? history_size
                      : read_count );
    int start = index - read_size;
    if ( start < 0 ) start += 10;

    fprintf( stderr, "read size = %d start=%d\n", read_size, start );

    for ( int cnt = 0; cnt < read_size; ++cnt )
    {
        int i = start + cnt;
        if ( i >= 10 ) i -= 10;

        char buf[BUFSIZE];
        snprintf( buf, BUFSIZE - 1, "(msg %ld %d \"%s\")\n", time_history[i], id_history[i], msg_history[i] );

        fprintf( stderr, "history index = %d [%s]", i, buf );

        int len = send( sender->socket_fd, buf, strlen( buf ), 0 );
        if ( len < 0 )
        {
            perror( "send" );
        }
    }
}

/* ------------------------------------------------------- */
void
reply_time_message( Client *sender, const char *recv_msg )
{
    char buf[BUFSIZE];
    char time_str[128];
    int len = 0;

    if ( strncmp( recv_msg, "("COMMAND_TIME")", strlen( COMMAND_TIME ) + 2 ) != 0 )
    {
        snprintf( buf, BUFSIZE - 1, "(error illegal_command [%s])\n", recv_msg );
        fprintf( stderr, "illegal command %s\n", recv_msg );
        len = send( sender->socket_fd, buf, strlen( buf ), 0 );
        if ( len < 0 )
        {
            perror( "send" );
        }
        return;
    }

    get_datetime_string( time_str, sizeof( time_str ) );
    snprintf( buf, BUFSIZE - 1, "(time \"%s\")\n", time_str );
    fprintf( stderr, "reply time [%s]\n", time_str );

    len = send( sender->socket_fd, buf, strlen( buf ), 0 );
    if ( len < 0 )
    {
        perror( "send" );
    }
}

/* ------------------------------------------------------- */
void
reply_hello( Client *sender, const char *recv_msg )
{
    char buf[BUFSIZE];
    int len = 0;

    if ( strncmp( recv_msg, "("COMMAND_HELLO")", strlen( COMMAND_TIME ) + 2 ) != 0 )
    {
        snprintf( buf, BUFSIZE - 1, "(error illegal_command [%s])\n", recv_msg );
        fprintf( stderr, "illegal command %s\n", recv_msg );
        len = send( sender->socket_fd, buf, strlen( buf ), 0 );
        if ( len < 0 )
        {
            perror( "send" );
        }
        return;
    }

    snprintf( buf, BUFSIZE - 1, "(hello %d)\n", sender->id );
    fprintf( stderr, "reply hello\n" );

    len = send( sender->socket_fd, buf, strlen( buf ), 0 );
    if ( len < 0 )
    {
        perror( "send" );
    }
}

/* ------------------------------------------------------- */
void reply_unknown_command( Client *sender, const char *recv_msg )
{
    char buf[BUFSIZE];
    snprintf( buf, BUFSIZE - 1, "(error unknown_command [%s])\n", recv_msg );
    fprintf( stderr, "unknown command %s\n", recv_msg );

    int len = send( sender->socket_fd, buf, strlen( buf ), 0 );
    if ( len < 0 )
    {
        perror( "send" );
    }
}

/* ------------------------------------------------------- */
void disable_client( Client *sender, const char *recv_msg )
{
    if ( strncmp( recv_msg, "("COMMAND_QUIT")", strlen( COMMAND_TIME ) + 2 ) != 0 )
    {
        char buf[BUFSIZE];
        snprintf( buf, BUFSIZE - 1, "(error illegal_command [%s])\n", recv_msg );
        fprintf( stderr, "illegal command %s\n", recv_msg );
        int len = send( sender->socket_fd, buf, strlen( buf ), 0 );
        if ( len < 0 )
        {
            perror( "send" );
        }
        return;
    }

    fprintf( stderr, "disable client\n" );
    sender->alive = 0;
}

/* ------------------------------------------------------- */
void
save_message( const time_t msg_time, const int sender_id, const char *msg )
{
    FILE *fp = fopen( MESSAGE_LOG, "a" );
    if ( fp == NULL )
    {
        fprintf( stderr, "ERROR: could not open the file [%s]\n", MESSAGE_LOG );
        return;
    }

    fprintf( fp, "%ld %d %s\n", msg_time, sender_id, msg );
    fclose( fp );
}