#include "httpd.h"


// send response to client
void send_res (int fd, char *msg, size_t len) {
  if (send(fd, msg, len, 0) == -1) {
    if (errno != 32) {
      perror("Error in send");
    }
  }
}

// receive request from client
int recv_req (int fd, char *buffer) {
  char *p = buffer;
  int matched_eol = 0;
  while (recv(fd, p, 1, 0) != 0) {
    if (*p == EOL[matched_eol]) {
      ++matched_eol;
      if (matched_eol == EOL_SIZE) {
        *(p + 1 - EOL_SIZE) = '\0';
        return (strlen(buffer));
      }
    } else {
      matched_eol = 0;
    }
    p++;
  }
  return 0;
}

// format bytes to more readable size
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

// server directory index to client
void serve_directory (int out_fd, int dir_fd, char *filename) {
  char buf[MAXLINE];
  char m_time[32];
  char size[16];
  struct stat statbuf;

  // set headers
  sprintf(buf, "HTTP/1.1 200 OK\r\n");
  sprintf(buf + strlen(buf), "Content-Type: text/html\r\n\r\n");

  // send headers
  send_res(out_fd, buf, strlen(buf));

  char dirname[128];
  if (!strcmp(filename, ".")) {
    sprintf(dirname, "/");
  } else {
    sprintf(dirname, "/%s", filename);
  }

  // create start of body
  sprintf(buf, "<!doctype html>\n"
               "<html>\n"
               "  <head>\n"
               "    <base href=\"%s\" />\n"
               "    <title>Index of %s</title>\n"
               "    <style>\n"
               "      body {\n"
               "        font-family: monospace;\n"
               "        font-size: 13px;\n"
               "      }\n"
               "      h1 {\n"
               "        font-family: serif;\n"
               "        font-size: 32px;\n"
               "      }\n"
               "      td {\n"
               "        padding: 1.5px 6px;\n"
               "        min-width: 250px;\n"
               "      }\n"
               "    </style>\n"
               "  </head>\n"
               "  <body>\n"
               "    <h1>Index of %s</h1>\n"
               "    <hr />\n"
               "    <table>\n", dirname, dirname, dirname);

  // send start of body
  send_res(out_fd, buf, strlen(buf));

  int n;
  int file_fd;
  struct dirent **namelist;

  // scan directory for directories
  n = scandir(filename, &namelist, NULL, alphasort);
  if (n < 0) {
    perror("scandir");
  } else {
    for (int i = 0; i < n; ++i) {
      struct stat st;
      char ent_name[MAXPATH];
      sprintf(ent_name, "%s%s", filename, namelist[i]->d_name);
      stat(ent_name, &st);
      if (!S_ISDIR(st.st_mode)) {
        free(namelist[i]);
        continue;
      }
      if (namelist[i]->d_name[0] == '.' && strcmp(namelist[i]->d_name, "..")) {
        // if current dir (.) or hidden skip
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
        sprintf(m_time, "%s", "");
        sprintf(size, "%s", "");
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
      send_res(out_fd, buf, strlen(buf));
      close(file_fd);

      free(namelist[i]);
    }
    free(namelist);
  }

  // scan directory for files
  n = scandir(filename, &namelist, NULL, alphasort);
  if (n < 0) {
    perror("scandir");
  } else {
    for (int i = 0; i < n; ++i) {
      struct stat st;
      char ent_name[MAXPATH];
      sprintf(ent_name, "%s%s", filename, namelist[i]->d_name);
      stat(ent_name, &st);
      if (!S_ISREG(st.st_mode)) {
        free(namelist[i]);
        continue;
      }
      if (namelist[i]->d_name[0] == '.') {
        // if hidden skip
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
      send_res(out_fd, buf, strlen(buf));
      close(file_fd);

      free(namelist[i]);
    }
    free(namelist);
  }

  sprintf(buf, "    </table>\n"
               "    <hr />\n"
               "  </body>\n"
               "</html>\r\n\r\n");
  send_res(out_fd, buf, strlen(buf));
}

// get mimetype from file extention
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

// create socket for server
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
  if (bind(listenfd, (addr *)&serveraddr, sizeof(serveraddr)) < 0) {
    return -1;
  }

  // open socket and make ready to accept connections
  if (listen(listenfd, LISTEN_Q) < 0) {
    return -1;
  }

  // return the descriptor
  return listenfd;
}

// decode url encoded strings
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

// parse client request headers
void parse_request (int fd, http_request *req) {
  char buf[MAXLINE];
  char uri[MAXLINE];
  char query[MAXLINE];

  // defaults
  req->offset = 0;
  req->end = 0;
  req->length = 0;

  recv_req(fd, buf);
  sscanf(buf, "%s %s", req->method, uri);
  #if SHOW_HEADERS_DEBUG == TRUE
    printf("%s\n", buf);
  #endif
  while(recv_req(fd, buf)) {
    if (buf[0] == 'R' && buf[1] == 'a' && buf[2] == 'n') {
      sscanf(buf, "Range: bytes=%lu-%lu", &req->offset, &req->end);
      if (req->end != 0) req->end++;
    }
    if (buf[0] == 'C' && buf[1] == 'o' && buf[2] == 'n' && buf[8] == 'L' && buf[9] == 'e' && buf[10] == 'n') {
      sscanf(buf, "Content-Length: %d", &req->length);
    }
    if (buf[0] == 'C' && buf[1] == 'o' && buf[2] == 'n' && buf[8] == 'T' && buf[9] == 'y' && buf[10] == 'p') {
      sscanf(buf, "Content-Type: %[^\t\r\n]", req->type);
    }
    #if SHOW_HEADERS_DEBUG == TRUE
      printf("%s\n", buf);
    #endif
  }

  strcpy(query, uri);
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

  char *qs = strrchr(query, '?');
  if (qs) {
    memmove(qs, qs+1, strlen(qs));
    strcpy(req->query, qs);
  } else {
    strcpy(req->query, "\0");
  }

  url_decode(filename, req->filename, MAXLINE);
}

void log_access (int status, struct sockaddr_in *c_addr, http_request *req) {
  /* Log format:
      [UTC Time String] status method  request_uri response_time content_length
  */

  time_t t = time(NULL);
  struct tm * lt = localtime(&t);
  char reqtime[512];
  strftime(reqtime, sizeof reqtime, "%c", lt);

  // colorize status code
  char status_color[16];
  if (status >= 100) {
    sprintf(status_color, "%s%d%s", COLOR_WHITE, status, COLOR_RESET);
  }
  if (status >= 200) {
    sprintf(status_color, "%s%d%s", COLOR_BRIGHT_GREEN, status, COLOR_RESET);
  }
  if (status >= 300) {
    sprintf(status_color, "%s%d%s", COLOR_BRIGHT_CYAN, status, COLOR_RESET);
  }
  if (status >= 400) {
    sprintf(status_color, "%s%d%s", COLOR_BRIGHT_YELLOW, status, COLOR_RESET);
  }
  if (status >= 500) {
    sprintf(status_color, "%s%d%s", COLOR_BRIGHT_RED, status, COLOR_RESET);
  }

  // format filename and add color
  char filename[MAXPATH + 1];
  char filename_color[MAXPATH + 12];
  if(!strncmp(req->filename, "./", 2) || !strncmp(req->filename, ".", 1)) {
    sprintf(filename, "/");
  } else {
    sprintf(filename, "/%s", req->filename);
  }
  sprintf(filename_color, "%s%s%s", COLOR_BRIGHT_GREEN, filename, COLOR_RESET);

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
    sprintf(content_length, "%s", "");
  }

  // Print Log msg
  printf("[%s] %s %-6s %s %s %s\n", reqtime, status_color, req->method, filename_color, rtime, content_length);
}

void client_error (int fd, int status, char *msg, char *longmsg, http_request *req) {
  char header_buf[MAXLINE];
  char body_buf[MAXLINE];

  // body
  sprintf(body_buf, "<!doctype html>\n"
                    "<html>\n"
                    "  <head>\n"
                    "    <title>%d %s</title>\n"
                    "    <style>\n"
                    "      body {\n"
                    "        font-family: monospace;\n"
                    "        font-size: 13px;\n"
                    "      }\n"
                    "      h1 {\n"
                    "        font-family: serif;\n"
                    "        font-size: 32px;\n"
                    "      }\n"
                    "    </style>\n"
                    "  </head>\n"
                    "  <body>\n"
                    "    <h1>%d %s</h1>\n"
                    "    <p>%s</p>\n"
                    "  </body>\n"
                    "</html>\r\n\r\n", status, msg, status, msg, longmsg);

  req->end = strlen(body_buf);

  // headers
  sprintf(header_buf, "HTTP/1.1 %d %s\r\n", status, msg);
  sprintf(header_buf + strlen(header_buf), "Content-Type: text/html\r\n");
  sprintf(header_buf + strlen(header_buf), "Content-Length: %lu\r\n\r\n", req->end);
  send_res(fd, header_buf, strlen(header_buf));

  // send body
  send_res(fd, body_buf, strlen(body_buf));
}

// server static resource to client
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
  sprintf(buf + strlen(buf), "Content-Length: %lu\r\n", req->end - req->offset);
  sprintf(buf + strlen(buf), "Content-Type: %s\r\n\r\n", get_mimetype(req->filename));

  send_res(out_fd, buf, strlen(buf));

  off_t offset = req->offset;
  while (offset < req->end) {
    if (sendfile(out_fd, in_fd, &offset, req->end - req->offset) <= 0) {
      break;
    }
    close(out_fd);
    break;
  }
}

// server cgi script result to client
void serve_cgi (int out_fd, http_request *req) {
  char buf[256];
  int cgi_out[2];
  int cgi_in[2];
  int cgi_err[2];

  sprintf(buf, "HTTP/1.1 200 OK\r\n");
  send_res(out_fd, buf, strlen(buf));

  pipe(cgi_out);
  pipe(cgi_in);
  pipe(cgi_err);

  // fork and run php-cgi
  int pid = fork();
  if (pid == 0) {

    // disable php notices in console
    #if SHOW_PHP_NOTICES == FALSE
      dup2(cgi_err[2], STDERR_FILENO);
    #endif

    dup2(cgi_out[1], STDOUT_FILENO);
    dup2(cgi_in[0], STDIN_FILENO);
    close(cgi_out[0]);
    close(cgi_in[1]);
    close(cgi_err[2]);

    // setup envars for php-cgi
    putenv("GATEWAY_INTERFACE=CGI/1.1");
    char script[MAXPATH + 16];
    sprintf(script, "SCRIPT_FILENAME=%s", req->filename);
    putenv(script);
    char query[MAXLINE + 13];
    sprintf(query, "QUERY_STRING=%s", req->query);
    putenv(query);
    char length[32];
    sprintf(length, "CONTENT_LENGTH=%d", req->length);
    putenv(length);
    char type[128 + 13];
    sprintf(type, "CONTENT_TYPE=%s", req->type);
    putenv(type);
    char method[8 + 15];
    sprintf(method, "REQUEST_METHOD=%s", req->method);
    putenv(method);
    putenv("REDIRECT_STATUS=true");
    putenv("SERVER_PROTOCOL=HTTP/1.1");
    putenv("REMOTE_HOST=127.0.0.1");

    // run php-cgi
    execl("/usr/bin/php-cgi", "php-cgi", NULL);
    exit(0);
  } else if (pid > 0) {
    close(cgi_out[1]);
    close(cgi_in[0]);
    close(cgi_err[2]);

    if (!strcmp(req->method, "POST") && req->length > 0) {
      char recv_buffer[MAXLINE];
      int received_data = 0;
      while (1) {
        ssize_t count = recv(out_fd, recv_buffer, sizeof(recv_buffer), 0);
        received_data += count;
        if (count == -1) {
          if (errno == EINTR) {
            continue;
          } else {
            perror("recv");
          }
        } else if (count == 0) {
          break;
        } else {
          write(cgi_in[1], recv_buffer, count);
          if (received_data >= req->length) break;
        }
      }
    }

    char resp_buffer[MAXLINE];
    int headers_sent = 0;
    req->end = 0;
    while (1) {
      ssize_t count = read(cgi_out[0], resp_buffer, sizeof(resp_buffer));
      if (count == -1) {
        if (errno == EINTR) {
          continue;
        } else {
          perror("read");
        }
      } else if (count == 0) {
        break;
      } else {
        send_res(out_fd, resp_buffer, count);
        if (headers_sent == 0) {
          if (resp_buffer[count - 4] == '\r' && resp_buffer[count - 3] == '\n') {
            headers_sent = 1;
          }
        } else {
          req->end += count;
        }
      }
    }
  } else if (pid < 0) {
    close(cgi_out[1]);
    close(cgi_in[0]);
    close(cgi_err[2]);

    perror("unable to spawn child processes\n");
  }
}

// handle client request with correct response
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
  if (!strcmp(req.method, "GET") || !strcmp(req.method, "HEAD") || !strcmp(req.method, "POST")) {
    if (file_fd <= 0) {
      // if file not exist send a 404
      status = 404;
      char *msg = "Not Found";
      char *longmsg = "File not found";
      client_error(fd, status, msg, longmsg, &req);
    } else {
      int isDir = 0;
      int hasIndex = 0;

      // make sure there if an index file exist we use it
      fstat(file_fd, &sbuf);
      if (S_ISDIR(sbuf.st_mode)) {
        isDir = 1;

        // add any missing /
        if (!strcmp(req.filename, ".")) {
          sprintf(req.filename, "./");
        }
        if (req.filename[strlen(req.filename) - 1] != '/') {
          char *tmp = req.filename;
          sprintf(req.filename, "%s/", tmp);
        }

        // check if an index exists if it does change request
        // to open that so we dont get a dir listing (if enabled)
        char *index = calloc(strlen(conf->index) + 1, sizeof(char));
        strcpy(index, conf->index);
        char *current_index;
        char default_file[MAXPATH];
        int default_fd;
        current_index = strtok (index, " ");
        while (current_index != NULL) {
          sprintf(default_file, "%s%s", req.filename, current_index);
          default_fd = open(default_file, O_RDONLY, 0);
          if (default_fd >= 1) {
            close(file_fd);
            file_fd = default_fd;
            sprintf(req.filename, "%s", default_file);
            hasIndex = 1;
            break;
          }
          current_index = strtok (NULL, " ");
        }
        free(index);
        if (hasIndex == 0) {
          close(default_fd);
        }
      }

      if (strcmp(conf->autoindex, "on") && hasIndex == 0 && isDir == 1) {
        status = 403;
        char *msg = "Forbidden";
        char *longmsg = "You don't have permission to access this resource";
        client_error(fd, status, msg, longmsg, &req);
      } else {
        // show dirlisting or page
        fstat(file_fd, &sbuf);
        if (S_ISREG(sbuf.st_mode)) { // is file
          if (req.end == 0) {
            req.end = sbuf.st_size;
          }
          if (req.offset > 0) {
            status = 206;
          }
          if (!strcmp(strrchr(req.filename, '.'), ".php")) {
            serve_cgi(fd, &req);
          } else {
            serve_static(fd, file_fd, &req, sbuf.st_size);
          }
        } else if (S_ISDIR(sbuf.st_mode)) { // is dir
          status = 200;
          serve_directory(fd, file_fd, req.filename);
        } else { // unknown error
          status = 500;
          char *msg = "Internal Server Error";
          char *longmsg = "An unknown error occurred";
          client_error(fd, status, msg, longmsg, &req);
        }
      }
    }
  } else { // any other request methods
    // method not implimented
    status = 501;
    char *msg = "Not Implemented";
    char *longmsg = "Method not implemented";
    client_error(fd, status, msg, longmsg, &req);
  }
  close(file_fd);

  // calculate response time
  clock_gettime(CLOCK_REALTIME, &etime);
  req.rtime = (1000 * (etime.tv_sec - stime.tv_sec)) + ((float)(etime.tv_nsec - stime.tv_nsec) / 1000000);

  log_access(status, clientaddr, &req);
}

// parse config file and set vars
void parse_config (char *buf, config *conf) {
  char int_buf[256];

  if (sscanf(buf, " %s", int_buf) == EOF) return; // blank line
  if (sscanf(buf, " %[#]", int_buf) == 1) return; // comment
  if (sscanf(buf, " listen %d;", &conf->port) == 1) return;
  if (sscanf(buf, " root %[^;]", conf->root) == 1) return;
  if (sscanf(buf, " index %[^;]", conf->index) == 1) return;
  if (sscanf(buf, " autoindex %[^;]", conf->autoindex) == 1) return;
  if (strcmp(buf, " server {\n") || strcmp(buf, " }\n")) return;

  errno = -1;
  perror("invalid conf");
  exit(-1);
}

void print_usage (int exit_code) {
  printf("\n"
         "  usage:\n"
         "    ./httpd [opts]\n"
         "\n"
         "  opts:\n"
         "    --conf <str>, -c              path to httpd.conf file, optional, defaults <cwd>/httpd.conf\n"
         "    --port <int>, -p              port to use, optional, defaults value in httpd.conf\n"
         "    --root <str>, -r              path to webroot, optional, defaults value in httpd.conf\n"
         "\n"
         "    --help, -h                    show this help\n"
         "\n");
  exit(exit_code);
}

void *test_thread (void *args) {
  process(((struct thread_args*)args)->connectionfd, &((struct thread_args*)args)->clientaddr);
  close(((struct thread_args*)args)->connectionfd);
  pthread_exit(0);
}

// main entry point for program
int main (int argc, char *argv[]) {
  struct sockaddr_in clientaddr;
  int listenfd;
  int connectionfd;
  char buf[256];
  socklen_t clientlen = sizeof clientaddr;

  static struct option long_options[] = {
    {"conf", required_argument, 0, 'c' },
    {"port", required_argument, 0, 'p' },
    {"root", required_argument, 0, 'r' },
    {"help", no_argument,       0, 'h' },
    {0,      0,                 0,  0  }
  };
  int long_index = 0;

  char *conf_path = "httpd.conf";
  int arg_port = 0;
  char arg_root[MAXPATH];
  arg_root[0] = 0;

  // parse args
  int opt;
  while ((opt = getopt_long(argc, argv,"hc:p:r:", long_options, &long_index )) != -1) {
    switch (opt) {
      case 'h': {
        print_usage(0);
      }
      case 'c': {
        conf_path = optarg;
        break;
      }
      case 'p': {
        int port = atoi(optarg);
        if (port > 0) {
          arg_port = port;
        } else {
          errno = 22;
          perror("error setting port");
        }
        break;
      }
      case 'r': {
        strncpy(arg_root, optarg, MAXPATH);
        break;
      }
      default: {
        print_usage(EXIT_FAILURE);
      }
    }
  }

  // parse conf file
  FILE *fconf = fopen(conf_path, "r");
  if (fconf == NULL) {
    perror("unable to load config");
    exit(errno);
  }
  while (fgets(buf, sizeof buf, fconf)) {
    parse_config(buf, conf);
  }

  // override conf with args if any
  if (arg_port != 0) {
    conf->port = arg_port;
  }
  if (strlen(arg_root) != 0) {
    strncpy(conf->root, arg_root, MAXPATH);
  }

  if (chdir(conf->root) != 0) { // make sure path exists if not exit
    perror(conf->root);
    exit(1);
  }

  // start listening for connections
  listenfd = open_listenfd(conf->port);
  if (listenfd > 0) {
    printf("listen on port %d, serving from %s\n", conf->port, conf->root);
  } else {
    perror("could not listen\n");
    exit(listenfd);
  }

  // ignore SIGPIPE signal so if a brower aborts a request we don't kill the process
  signal(SIGPIPE, SIG_IGN);

  // create child threads to handle each request
  while (1) {
    connectionfd = accept(listenfd, (addr *)&clientaddr, &clientlen);
    if (connectionfd < 0) {
      perror("could not accept connection\n");
    } else {
      #if SHOW_DEBUG == TRUE
        printf("accepted connection\n");
      #endif
    }
    pthread_t tid;
    struct thread_args *args = (struct thread_args *)malloc(sizeof(struct thread_args));
    args->connectionfd = connectionfd;
    args->clientaddr = clientaddr;
    pthread_create(&tid, NULL, test_thread, (void *)args);
    pthread_join(tid, NULL);
    free(args);
    close(connectionfd);
  }
  close(listenfd);

  return 0;
}