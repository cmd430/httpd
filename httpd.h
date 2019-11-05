/*

  TODO:
    support custom directory listing pages
    support custom log format
    support custom error pages
    support all common HTTP/1.1 methods
    support .htaccess rules
    support plugins/modules
    support HTTP/2
    get correct content length for php scripts
    support SSL (https)

*/
#include <arpa/inet.h>
#include <signal.h>
#include <dirent.h>
#include <errno.h>
#include <fcntl.h>
#include <time.h>
#include <netinet/in.h>
#include <netinet/tcp.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/sendfile.h>
#include <sys/socket.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <unistd.h>


#define LISTEN_Q    1024  // listen backlog (max length of pending connections)
#define MAXLINE     1024  // max length of a single line
#define RIO_BUFSIZE 1024  // Read IO buffer size


typedef struct {
  int rio_fd;                // buffer descriptor
  int rio_cnt;               // unread byte
  char *rio_bufptr;          // next unread byte
  char rio_buf[RIO_BUFSIZE]; // internal buffer
} rio_t;

typedef struct sockaddr SA; // make calls to bind(), connect(), and accept() more simple

typedef struct {
  char filename[512];    // requested file
  char method[128];      // request method
  char query[MAXLINE];   // query string
  off_t offset;          // http range request
  size_t end;            // content length
  double rtime;          // time taken for request
} http_request;

typedef struct {
  int port;               // server listen port
  char root[512];         // webroot
  int listing;            // dir listing enabled or not
  char index[128];        // index pages
} config;

config conf[1];           // global conf

typedef struct {
  const char *extention;
  const char * mimetype;
} mime_map;

mime_map mimetypes [] = {
  { ".css", "text/css" },
  { ".gif", "image/gif" },
  { ".htm", "text/html" },
  { ".html", "text/html" },
  { ".jpeg", "image/jpeg" },
  { ".jpg", "image/jpeg" },
  { ".ico", "image/x-icon" },
  { ".js", "application/javascript" },
  { ".json", "application/json" },
  { ".pdf", "application/pdf" },
  { ".mp4", "video/mp4" },
  { ".png", "image/png" },
  { ".svg", "image/svg+xml" },
  { ".xml", "text/xml" },
  { NULL, NULL },
};

char *default_mimetype = "text/plain";