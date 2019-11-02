/*

  TODO:
    support config files to load httpd config automatically
    support custom directory listing pages
    support custom log format
    add nicer error default pages (should have some html structure)
    support custom error pages
    support all common HTTP/1.1 methods
    support PHP/CGI
    support .htaccess rules
    support plugins/modules
    support HTTP/2

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
  off_t offset;          // http range request
  size_t end;            // content length
  char method[128];      // request method
  double rtime;          // time taken for request
} http_request;

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

void rio_readinitb (rio_t *rp, int fd) {
  rp->rio_fd = fd;
  rp->rio_cnt = 0;
  rp->rio_bufptr = rp->rio_buf;
}

ssize_t written (int fd, void *usrbuf, size_t n) {
  size_t nleft = n;
  ssize_t nwritten;
  char *bufp = usrbuf;

  while (nleft > 0) {
    if ((nwritten = write(fd, bufp, nleft)) <= 0) {
      if (errno == EINTR) { // interrupted by sig handler return
        nwritten = 0;       // recall write()
      } else {
        return -1;          // write() errono
      }
    }
    nleft -= nwritten;
    bufp += nwritten;
  }
}

static ssize_t rio_read (rio_t *rp, char *usrbuf, size_t n) {
  int cnt;

  while (rp->rio_cnt <= 0) {
    rp->rio_cnt = read(rp->rio_fd, rp->rio_buf, sizeof(rp->rio_buf));

    if (rp->rio_cnt < 0) {
      if (errno != EINTR) {          // interrupted by sig handler return
        return -1;
      }
    } else if (rp->rio_cnt == 0) {  // EOF
      return 0;
    } else {
      rp->rio_bufptr = rp->rio_buf; // reset buffer ptr
    }
  }

  // Copy min(n, rp->rio_cnt) bytes from internal buf to user buf
  cnt = n;
  if (rp->rio_cnt < n) {
    cnt = rp->rio_cnt;
  }

  memcpy(usrbuf, rp->rio_bufptr, cnt);
  rp->rio_bufptr += cnt;
  rp->rio_cnt -= cnt;

  return cnt;
}

ssize_t rio_readlineb (rio_t *rp, void *usrbuf, size_t maxlen){
  int n;
  int rc;
  char c;
  char *bufp = usrbuf;

  for (n = 1; n < maxlen; n++){
    if ((rc = rio_read(rp, &c, 1)) == 1){
      *bufp++ = c;
      if (c == '\n') {
        break;
      }
    } else if (rc == 0){
      if (n == 1) {
        return 0; // EOF, no data read
      } else {
        break;    // EOF, some data was read
      }
    } else {
      return -1;   // error
    }
  }
  *bufp = 0;

  return n;
}

void format_size (char *buf, struct stat *stat) {
  if (S_ISDIR(stat->st_mode)) {
    sprintf(buf, "%s", "-");
  } else {
    off_t size = stat->st_size;
    if (size < 1024) {
      sprintf(buf, "%luB", size);
    } else if (size < 1024 * 1024) {
      sprintf(buf, "%.1fKB", (double)size / 1024);
    } else if (size < 1024 * 1024 * 1024) {
      sprintf(buf, "%.1fMB", (double)size / 1024 / 1024);
    } else {
      sprintf(buf, "%.1fGB", (double)size / 1024 / 1024 / 1024);
    }
  }
}

int directory_filter(const struct dirent *entry){
  struct stat st;
  stat(entry->d_name, &st);
  if (S_ISDIR(st.st_mode)) {
    return 1;
  }
  return 0;
}

int file_filter(const struct dirent *entry){
  struct stat st;
  stat(entry->d_name, &st);
  if (S_ISREG(st.st_mode)) {
    return 1;
  }
  return 0;
}

void serve_directory (int out_fd, int dir_fd, char *filename) {
  char buf[MAXLINE];
  char m_time[32];
  char size[16];
  struct stat statbuf;

  // set headers
  sprintf(buf, "HTTP/1.1 OK\r\n");
  sprintf(buf + strlen(buf), "Content-Type: text/html\r\n\r\n");

  // send headers
  written(out_fd, buf, strlen(buf));

  char dirname[256];
  if (!strcmp(filename, ".")) {
    sprintf(dirname, "/");
  } else {
    sprintf(dirname, "/%s", filename);
  }

  // create start of body
  sprintf(buf, "<!doctype html>\n"
               "<html>\n"
               "  <head>\n"
               "    <title>Index of %s</title>\n"
               "    <style>\n"
               "      body {\n"
               "        font-family: monospace;\n"
               "        font-size: 13px;\n"
               "      }\n"
               "      h1 {\n"
               "        font-family: serif;\n"
               "        font-size: 32px;\n"
               "      }"
               "      td {\n"
               "        padding: 1.5px 6px;\n"
               "        min-width: 250px;\n"
               "      }\n"
               "    </style>\n"
               "  </head>\n"
               "  <body>\n"
               "    <h1>Index of %s</h1>\n"
               "    <hr />\n"
               "    <table>\n", dirname, dirname);

  // send start of body
  written(out_fd, buf, strlen(buf));

  int n;
  int file_fd;
  struct dirent **namelist;

  // store cwd and change into subdir for request if any
  char buffer[256];
  char *path = getcwd(buffer, 256);
  chdir(filename);

  // scan directory for directories
  n = scandir(".", &namelist, directory_filter, alphasort);
  if (n < 0) {
    perror("scandir");
  } else {
    for (int i = 0; i < n; ++i) {
      if (!strcmp(namelist[i]->d_name, ".")) {
        // if current dir (.) skip
        free(namelist[i]);
        continue;
      }
      if ((file_fd = openat(dir_fd, namelist[i]->d_name, O_RDONLY)) == -1) {
        // show error and skip if file can't be read
        perror(namelist[i]->d_name);
        free(namelist[i]);
        continue;
      }

      // read file properties
      fstat(file_fd, &statbuf);
      strftime(m_time, sizeof m_time, "%Y-%m-%d %H:%M", localtime(&statbuf.st_mtime));
      format_size(size, &statbuf);

      // blank for current / parent dirs
      if (!strcmp(namelist[i]->d_name, ".") || !strcmp(namelist[i]->d_name, "..")) {
        sprintf(m_time, "");
        sprintf(size, "");
      }

      sprintf(buf, "      <tr>\n"
                   "        <td>\n"
                   "          <a href=\"%s/\">%s/</a>\n"
                   "        </td>\n"
                   "        <td>\n"
                   "          %s\n"
                   "        </td>\n"
                   "        <td>\n"
                   "          %s\n"
                   "        </td>\n"
                   "      </tr>\n", namelist[i]->d_name, namelist[i]->d_name, m_time, size);
      written(out_fd, buf, strlen(buf));
      close(file_fd);

      free(namelist[i]);
    }
    free(namelist);
  }

  // scan directory for files
  n = scandir(".", &namelist, file_filter, alphasort);
  if (n < 0) {
    perror("scandir");
  } else {
    for (int i = 0; i < n; ++i) {
      if ((file_fd = openat(dir_fd, namelist[i]->d_name, O_RDONLY)) == -1) {
        // show error and skip if file can't be read
        perror(namelist[i]->d_name);
        free(namelist[i]);
        continue;
      }

      // read file properties
      fstat(file_fd, &statbuf);
      strftime(m_time, sizeof m_time, "%Y-%m-%d %H:%M", localtime(&statbuf.st_mtime));
      format_size(size, &statbuf);

      sprintf(buf, "      <tr>\n"
                   "        <td>\n"
                   "          <a href=\"%s\">%s</a>\n"
                   "        </td>\n"
                   "        <td>\n"
                   "          %s\n"
                   "        </td>\n"
                   "        <td>\n"
                   "          %s\n"
                   "        </td>\n"
                   "      </tr>\n", namelist[i]->d_name, namelist[i]->d_name, m_time, size);
      written(out_fd, buf, strlen(buf));
      close(file_fd);

      free(namelist[i]);
    }
    free(namelist);
  }

  sprintf(buf, "    </table>\n"
               "    <hr />\n"
               "  </body>\n"
               "</html>");
  written(out_fd, buf, strlen(buf));

  // change back to the webroot
  chdir(path);
}

static const char *get_mimetype (char *filename) {
  char *dot = strrchr(filename, '.');
  if (dot) { // get last instance of '.'
    mime_map *map = mimetypes;
    while (map->extention) { // get mimetype if one matches
      if (strcmp(map->extention, dot) == 0) {
        return map->mimetype;
      }
      map++;
    }
  }
  return default_mimetype;
}

int open_listenfd (int port) { // open file descriptor (fd) for listen
  int listenfd;
  int opts = 1;
  struct sockaddr_in serveraddr;

  // create socket descriptor
  if ((listenfd = socket(AF_INET, SOCK_STREAM, 0)) < 0) {
    return -1;
  }

  // handle 'Address already in use' error on bind
  if (setsockopt(listenfd, SOL_SOCKET, SO_REUSEADDR, (const void *)&opts, sizeof(int)) < 0) {
    return -1;
  }

  // enable TCP protocol (6) (4k req/s -> ~17k req/s)
  if (setsockopt(listenfd, 6, TCP_CORK, (const void *)&opts, sizeof(int)) < 0) {
    return -1;
  }

  // setup serveraddr struct with IP/Port and family
  memset(&serveraddr, 0, sizeof(serveraddr));
  serveraddr.sin_family = AF_INET;
  serveraddr.sin_addr.s_addr = htonl(INADDR_ANY);     // bind any interface on host
  serveraddr.sin_port = htons((unsigned short) port); // bind port

  // bind port on host for any IP (interface)
  if (bind(listenfd, (SA *)&serveraddr, sizeof(serveraddr)) < 0) {
    return -1;
  }

  // open socket and make ready to accept connections
  if (listen(listenfd, LISTEN_Q) < 0) {
    return -1;
  }

  // return the descriptor
  return listenfd;
}

void url_decode (char *src, char *dest, int max) {
  char *p = src;
  char code[3] = { 0 };

  while (*p && --max) {
    if (*p == '%') {
      memcpy(code, ++p, 2);
      *dest++ = (char)strtoul(code, NULL, 16);
      p += 2;
    } else {
      *dest ++ = *p++;
    }
  }

  *dest = '\0';
}

void parse_request (int fd, http_request *req) {
  rio_t rio;
  char buf[MAXLINE];
  char method[MAXLINE];
  char uri[MAXLINE];

  // defaults
  req->offset = 0;
  req->end = 0;

  rio_readinitb(&rio, fd);
  rio_readlineb(&rio, buf, MAXLINE);
  sscanf(buf, "%s %s", method, uri);

  while (buf[0] != '\n' && buf[1] != '\n') {
    rio_readlineb(&rio, buf, MAXLINE);
    if (buf[0] == 'R' && buf[1] == 'a' && buf[2] == 'n') {
      sscanf(buf, "Range: bytes=%lu-%lu", &req->offset, &req->end);
      if (req->end != 0) req->end++;
    }
  }

  char *filename = uri;
  if (uri[0] == '/') {
    filename = uri + 1;
    int length = strlen(filename);
    if (length == 0) {
      filename = ".";
    } else {
      for (int i = 0; i < length; ++i) {
        if (filename[i] == '?') {
          filename[i] = '\0';
          break;
        }
      }
    }
  }

  strcpy(req->method, method);

  url_decode(filename, req->filename, MAXLINE);
}

void log_access (int status, struct sockaddr_in *c_addr, http_request *req) {
  /* Log format:
      [UTC Time String] status method  request_uri response_time content_length

    Color Code Sequences:
      Black:            \x1b[30m
      Red:              \x1b[31m
      Green:            \x1b[32m
      Yellow:           \x1b[33m
      Blue:             \x1b[34m
      Magenta:          \x1b[35m
      Cyan:             \x1b[36m
      White:            \x1b[37m
      Bright Black:     \x1b[30;1m
      Bright Red:       \x1b[31;1m
      Bright Green:     \x1b[32;1m
      Bright Yellow:    \x1b[33;1m
      Bright Blue:      \x1b[34;1m
      Bright Magenta:   \x1b[35;1m
      Bright Cyan:      \x1b[36;1m
      Bright White:     \x1b[37;1m
      Reset:            \x1b[0m
  */

  time_t t = time(NULL);
  struct tm * lt = localtime(&t);
  char reqtime[512];
  strftime(reqtime, sizeof reqtime, "%c", lt);

  // colorize status code
  char status_color[16];
  if (status >= 100) {
    sprintf(status_color, "\x1b[37m%d\x1b[0m", status);
  }
  if (status >= 200) {
    sprintf(status_color, "\x1b[32;1m%d\x1b[0m", status);
  }
  if (status >= 300) {
    sprintf(status_color, "\x1b[36;1m%d\x1b[0m", status);
  }
  if (status >= 400) {
    sprintf(status_color, "\x1b[33;1m%d\x1b[0m", status);
  }
  if (status >= 500) {
    sprintf(status_color, "\x1b[31;1m%d\x1b[0m", status);
  }

  // format filename and add color
  char filename[512];
  char filename_color[512];
  if(!strncmp(req->filename, "./", 2) || !strncmp(req->filename, ".", 1)) {
    sprintf(filename, "/");
  } else {
    sprintf(filename, "/%s", req->filename);
  }
  sprintf(filename_color, "\x1b[32;1m%s\x1b[0m", filename);

  // reponse time
  char rtime[16];
  if (req->rtime <= 999) {
    sprintf(rtime, "%.1f ms", req->rtime);
  } else {
    sprintf(rtime, "%.2f s", req->rtime / 1000);
  }

  // content length
  char content_length[16];
  int cl = req->end - req->offset;
  if (cl > 0) {
    sprintf(content_length, "%d", cl);
  } else {
    sprintf(content_length, "");
  }

  // Print Log msg
  printf("[%s] %s %-6s %s %s %s\n", reqtime, status_color, req->method, filename_color, rtime, content_length);
}

void client_error(int fd, int status, char *msg, char *longmsg, http_request *req) {
  char buf[MAXLINE];
  req->end = strlen(longmsg);

  sprintf(buf, "HTTP/1.1 %d %s\r\n", status, msg);
  sprintf(buf + strlen(buf), "Content-length: %lu\r\n\r\n", req->end);
  sprintf(buf + strlen(buf), "%s", longmsg);
  written(fd, buf, strlen(buf));
}

void serve_static (int out_fd, int in_fd, http_request *req, size_t total_size) {
  char buf[256];

  if (req->offset > 0) { // http request has range headers
    sprintf(buf, "HTTP/1.1 Partial\r\n");
    sprintf(buf + strlen(buf), "Content-Range: bytes %lu-%lu/%lu\r\n", req->offset, req->end, total_size);
  } else {
    sprintf(buf, "HTTP/1.1 200 OK\r\n");
    sprintf(buf + strlen(buf), "Accept-Ranges: bytes\r\n");
  }

  sprintf(buf + strlen(buf), "Cache-Control: no-cache\r\n");
  sprintf(buf + strlen(buf), "Content-length: %lu\r\n", req->end - req->offset);
  sprintf(buf + strlen(buf), "Content-type: %s\r\n\r\n", get_mimetype(req->filename));

  written(out_fd, buf, strlen(buf));

  off_t offset = req->offset;
  while (offset < req->end) {
    if (sendfile(out_fd, in_fd, &offset, req->end - req->offset) <= 0) {
      break;
    }
    //printf("offset: %d\n\n", offset);
    close(out_fd);
    break;
  }
}

void process (int fd, struct sockaddr_in *clientaddr) {
  struct timespec stime;
  struct timespec etime;

  clock_gettime(CLOCK_REALTIME, &stime);

  http_request req;
  parse_request(fd, &req);

  struct stat sbuf;
  int status = 200;
  int file_fd = open(req.filename, O_RDONLY, 0);

  // handle differnt request methods
  if (!strcmp(req.method, "GET") || !strcmp(req.method, "HEAD")) { // GET or HEAD requests
    // are we viewing a directory
    if (!strcmp(req.filename, ".") || req.filename[strlen(req.filename) - 1] == '/') {
      char default_file[512];

      // add '/' to root dir (.)
      if (!strcmp(req.filename, ".")) {
        sprintf(req.filename, "./");
      }
      sprintf(default_file, "%sindex.html", req.filename);

      // check if 'index.html' exists if it does change request
      // to open that so we dont get a dir listing
      int default_fd = open(default_file, O_RDONLY, 0);
      if (default_fd >= 1) {
        file_fd = default_fd;
        sprintf(req.filename, "%s", default_file);
      }
    }

    if (file_fd <= 0) {
      // if file not exist send a 404
      status = 404;
      char *msg = "Not Found";
      char *longmsg = "File not found";
      client_error(fd, status, msg, longmsg, &req);
    } else {
      fstat(file_fd, &sbuf);
      if (S_ISREG(sbuf.st_mode)) { // is file
        if (req.end == 0) {
          req.end = sbuf.st_size;
        }
        if (req.offset > 0) {
          status = 206;
        }
        serve_static(fd, file_fd, &req, sbuf.st_size);
      } else if (S_ISDIR(sbuf.st_mode)) { // is dir
        status = 200;
        serve_directory(fd, file_fd, req.filename);
      } else { // unknown error
        status = 500;
        char *msg = "Interanl Server Error";
        char *longmsg = "An unknown error occurred";
        client_error(fd, status, msg, longmsg, &req);
      }
    }
  } else { // any other request methods
    // method not implimented
    status = 501;
    char *msg = "Not Implemented";
    char *longmsg = "Method not implemented";
    client_error(fd, status, msg, longmsg, &req);
  }

  // calculate response time
  clock_gettime(CLOCK_REALTIME, &etime);
  req.rtime = (1000 * (etime.tv_sec - stime.tv_sec)) + ((float)(etime.tv_nsec - stime.tv_nsec) / 1000000);

  log_access(status, clientaddr, &req);
}

int main (int argc, char* argv[]) { // main entry point for program
  struct sockaddr_in clientaddr;
  int default_port = 9999;
  int listenfd;
  int connectionfd;
  char buffer[256];
  char *path = getcwd(buffer, 256);
  socklen_t clientlen = sizeof clientaddr;

  /*
    usage:
      ./httpd <path> <port>

    opts:
      int port
      str working dir

    notes:
      if both <path> and <port> are supplied <path> MUST be the first opt

    argc length is equal to binary arg0 arg1 ... arg9
    if argc = 1 we have no cmdline args
              2 we have one cmdline arg etc
  */
  if (argc == 1) {
    printf(
      "\n"
      "usage:\n"
      "   ./httpd <path> <port>\n"
      "\n"
      "opts:\n"
      "   path - optional (default 9999) - integer\n"
      "   port - optional (default cwd)  - string\n"
      "\n"
      "notes:\n"
      "   if both <path> and <port> are supplied <path> MUST be the first opt\n"
      "\n"
    );
  } else if (argc == 2) {
    if (argv[1][0] >= '0' && argv[1][0] <= '9') { // if arg is int we set port
      default_port = atoi(argv[1]);               // set port
    } else {                                      // we must be setting the working dir
      path = argv[1];                             // set working dir
      if (chdir(argv[1]) != 0) {                  // make sure path exists if not exit
        perror(argv[1]);
        exit(1);
      }
    }
  } else if (argc == 3) {
    default_port = atoi(argv[2]); // set port
    path = argv[1];               // set working dir
    if (chdir(argv[1]) != 0) {    // make sure path exists if not exit
      perror(argv[1]);
      exit(1);
    }
  }

  // start listening for connections
  listenfd = open_listenfd(default_port);
  if (listenfd > 0) {
    printf("listen on port %d, serving from %s\n", default_port, path);
  } else {
    perror("could not listen\n");
    exit(listenfd);
  }

  // ignore SIGPIPE signal so if a brower aborts a request we don't kill the process
  signal(SIGPIPE, SIG_IGN);

  // create child processes to handle each request
  while (1) {
    connectionfd = accept(listenfd, (SA *)&clientaddr, &clientlen);
    if (connectionfd < 0) {
      perror("could not accept connection\n");
    } else {
      //printf("accepted connection\n");
    }
    int pid = fork();
    if (pid == 0) {
      close(listenfd);
      process(connectionfd, &clientaddr);
      close(connectionfd);
      exit(0);
    } else if (pid > 0) {
      //printf("spawned child with pid %d\n", pid);
      close(connectionfd);
    } else {
      perror("unable to spawn child processes\n");
    }
  }
  close(listenfd);

  return 0;
}