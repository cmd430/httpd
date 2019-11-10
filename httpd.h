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
#include <getopt.h>
#include <pthread.h>

/* MACROS */
#define SHOW_DEBUG         FALSE // enable/disable some extra msgs
#define SHOW_HEADERS_DEBUG FALSE // Log request headers
#define SHOW_PHP_NOTICES   FALSE // Log PHP Notices

/* CONSTANTS */
#define TRUE     1
#define FALSE    0
#define LISTEN_Q 1024   // listen backlog (max length of pending connections)
#define MAXLINE  1024   // max length of a single line
#define MAXPATH  512    // max length of file path
#define EOL      "\r\n" // End of line chars
#define EOL_SIZE 2      // size of EOL

/* COLOR ESCAPE CODES  */
#define COLOR_BLACK          "\x1b[30m"
#define COLOR_RED            "\x1b[31m"
#define COLOR_GREEN          "\x1b[32m"
#define COLOR_YELLOW         "\x1b[33m"
#define COLOR_BLUE           "\x1b[34m"
#define COLOR_MAGENTA        "\x1b[35m"
#define COLOR_CYAN           "\x1b[36m"
#define COLOR_WHITE          "\x1b[37m"
#define COLOR_BRIGHT_BLACK   "\x1b[30;1m"
#define COLOR_BRIGHT_RED     "\x1b[31;1m"
#define COLOR_BRIGHT_GREEN   "\x1b[32;1m"
#define COLOR_BRIGHT_YELLOW  "\x1b[33;1m"
#define COLOR_BRIGHT_BLUE    "\x1b[34;1m"
#define COLOR_BRIGHT_MAGENTA "\x1b[35;1m"
#define COLOR_BRIGHT_CYAN    "\x1b[36;1m"
#define COLOR_BRIGHT_WHITE   "\x1b[37;1m"
#define COLOR_RESET          "\x1b[0m"

/* TYPEDEFS */
typedef struct sockaddr addr; // make calls to bind(), connect(), and accept() more simple

typedef struct {
  char filename[MAXPATH]; // requested file
  char method[8];         // request method
  char query[MAXLINE];    // query string
  int length;             // content length of POST
  char type[128];         // content type of POST
  off_t offset;           // http range request
  size_t end;             // content length
  double rtime;           // time taken for request
} http_request_t;

typedef struct {
  int port;           // server listen port
  char root[MAXPATH]; // webroot
  char index[128];    // index pages
  char autoindex[3];  // dir listing enabled or not
} config_t;

typedef struct {
  const char *extention;
  const char * mimetype;
} mime_map_t;

/* GLOBALS */
config_t conf[1]; // global conf
mime_map_t mimetypes [] = {
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
struct thread_args {
  int connectionfd;
  struct sockaddr_in clientaddr;
};

