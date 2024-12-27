#ifndef MY_NETLIB_H
#define MY_NETLIB_H

/*!
  ¥brief ポート番号を指定して待受けのソケットを作成する
  ¥param port_number 待ち受けするポート番号（またはサービス名）を文字列で与える
  ¥return 作成されたソケットのファイルディスクリプタ
 */
int create_server_socket( const char * port_number );

/*!
  ¥brief 現在日時の文字列を取得する
  ¥param result 結果を格納する文字列へのポインタ
  ¥param size 結果を格納する文字列の最大サイズ
 */
void get_datetime_string( char *result, const int size );


/*!
  ¥brief アドレスを指定して接続を試みる
  ¥param hostname 接続先ホスト名（またはIPアドレス）を文字列で与える
  ¥param port_number 接続先ポート番号（またはサービス名）を文字列で与える
  ¥return 作成されたソケットのファイルディスクリプタ。エラーの場合は-1が返される。
*/
int
connect_to_server( const char * hostname,
                   const char * port_number );

#endif

