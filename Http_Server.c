#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <pthread.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <fcntl.h>
#include <sys/sendfile.h>
#include <sys/stat.h>
#include <sys/wait.h>

#define SIZE (1024 * 10)

typedef struct HttpRequest {
  char first_line[SIZE];
  char* method;
  char* url;
  char* url_path;
  char* query_string;
  int conntent_length;
} HttpRequest;

int ReadLine(int sock, char buf[], ssize_t max_size) {
  char c = '\0';
  ssize_t i = 0;
  while(i < max_size) {
    ssize_t read_size = recv(sock, &c, 1, 0);
    printf("%c", c);
    if(read_size <= 0) {
      perror("recv");
      return -1;
    }
    if(c == '\r') {
      // 相当于偷看牌
      recv(sock, &c, 1, MSG_PEEK);
      // a) 如果是\n,代表这个换行符是\r\n,就把\n给摸了
      if(c == '\n') {
        recv(sock, &c, 1, 0);
      }
      // b) 如果不是,就把\r改成\n
      else {
        c = '\n';
      }
    }
    buf[i++] = c;
    // 只读取一行
    if(c == '\n') {
      break;
    }
  }
  buf[i] = '\0';
  printf("ReadLine success\n");
  return i;
}

int Split(char first_line[], const char* split_char, char* output[]) {
  char* tmp = NULL;
  int output_index = 0;
  char* p = strtok_r(first_line, split_char, &tmp);
  while(p != NULL) {
    output[output_index++] = p;
    p = strtok_r(NULL, split_char, &tmp);
  }
  return output_index;
}

// ParseFirstLine
int ParseFirstLine(char first_line[], char** method_ptr, char** query_string) {
  char* token[100] = {NULL};
  int n = Split(first_line, " ", token);
  if(n != 3) {
    printf("first_line Split error! n = %d\n", n);
    return -1;
  }
  *method_ptr = token[0];
  *query_string = token[1];
  printf("ParseFirstLine success\n");
  return 0;
}

int ParseQueryString(char url[], char** url_path_ptr, char** query_string_ptr) {
  *url_path_ptr = url;
  char* p = url;
  for(; *p != '\0'; ++p) {
    if(*p == '?') {
      *p = '\0';
      *query_string_ptr = p + 1;
      return 1;
    }
  }
  *query_string_ptr = NULL;
  printf("ParseQueryString success\n");
  return 0;
}

int HandlerHeader(int new_sock, int* conntent_length) {
  char buf[SIZE] = {0};
  while(1) {
    ssize_t read_size = ReadLine(new_sock, buf, sizeof(buf) - 1);
    if(read_size <= 0) {
      perror("ReadLine failed!!");
      return -1;
    }
    if(strcmp(buf, "\n") == 0) {
      return 0;
    }
    const char* key = "Content-Length: ";
    if(strncmp(buf, key, strlen(key)) == 0) {
      *conntent_length = atoi(buf + strlen(key));
    }
    printf("conntent_length is %d\n", *conntent_length);
  }
  return 0;
}

void Handler404(int new_sock) {
  const char* first_line = "HTTP/1.1 404 Not Found\n";
  const char* blank = "\n";
  const char* body = "<head><meta http-equiv=\"content-type\""
    " content=\"text/html;charset=utf-8\">"
    "</head> <h1>404 Not Found <br> 呸,单身狗</h1>";

  send(new_sock, first_line, strlen(first_line), 0);
  send(new_sock, blank, strlen(blank), 0);
  send(new_sock, body, strlen(body), 0);
  printf("404 success\n");
}

int IsDir(const char* file_path) {
  struct stat st;
  // stat 函数用来将参数file_path所指的文件状态赋值到st之中
  int ret = stat(file_path, &st);
  if(ret < 0) {
    // 不是目录
    return 0;
  }
  // S_ISDIR 是判断是否为目录
  if(S_ISDIR(st.st_mode)) {
    return 1;
  }
  return 0;
}
 
// 目录文件的路径拼接
void HandlerFilePath(const char* url_path, char file_path[]) {
  // url_path 是以/开头的
  sprintf(file_path, "./wwwroot%s", url_path);

  // 如果url指向的目录,就在目录后面拼接.index.html作为默认访问文件
  if(file_path[strlen(file_path) - 1] == '/') {
    // a) url_path以 '/'结尾,例如: /image/ 
    strcat(file_path, "index.html");
  }
  else {
    // b) url_path不以'/'结尾,此时需要根据上下文判断是不是目录
    if(IsDir(file_path)) {
      strcat(file_path, "/index.html");
    }
  }
}

ssize_t GetFileSize(const char* file_path) {
  struct stat st;
  int ret = stat(file_path, &st);
  if(ret < 0) {
    return 0;
  }
  // 返回文件大小
  return st.st_size;
}

int WriteStaticFile(int new_sock, const char* file_path) {
  // 1. 打开文件,失败了返回404
  printf("file_path = %s\n", file_path);
  int fd = open(file_path, O_RDONLY);
  if(fd < 0) {
    perror("open");
    return 404;
  }
  // 2. 构造HTTP响应报文
  const char* first_line = "HTTP/1.1 200 OK\n";
  send(new_sock, first_line, strlen(first_line), 0);
  // 最好加上Header
  // 没有写content-type是因为浏览器自动识别
  // 没有content-length 是因为后面关闭了socket,浏览器可以自动识别
  const char* blank = "\n";
  send(new_sock, blank, strlen(blank), 0);

  ssize_t file_size = GetFileSize(file_path);
  sendfile(new_sock, fd, NULL, file_size);
  // 3. 关闭文件
  close(fd);
  return 200;
}

int HandlerStaticFile(int new_sock, const HttpRequest* req) {
  // 1. 根据 url_path 获取到文件的真实路径
  // 例如,此时HTTP服务器的根目录叫做./wwwroot
  // 此时有一个文件在./wwwroot/image/101.jpg
  char file_path[SIZE] = {0};
  // 文件路径处函数
  HandlerFilePath(req->url_path, file_path);
  // 2. 打开文件,读取文件内容,把文件写入到socket中
  int err_code = WriteStaticFile(new_sock, file_path);
  return err_code;
}

int HandlerCGIFather(int new_sock, int father_read, int father_write, const HttpRequest* req) {
  // a) 如果是POST请求,把body部分数据读取出来写入到管道
  //    剩下的动态生成页面都交给子进程完成
  if(strcasecmp(req->method, "POST") == 0) {
    // 根据body 的长度决定读多少个字节
    int i = 0;
    char c = '\0';
    for(; i < req->conntent_length; ++i) {
      read(new_sock, &c, 1);
      write(father_write, &c, 1);
    }
  }
  // b) 构造HTTP响应中的首行, header 空行
  const char* first_line = "HTTP/1.1 200 OK\n";
  send(new_sock, first_line, strlen(first_line), 0);
  // 此处先不管header
  const char* blank_line = "\n";
  send(new_sock, blank_line, strlen(blank_line), 0);
  // a) 从管道中读取数据
  char c = '\0';
  while(read(father_read, &c, 1) > 0) {
    write(new_sock, &c, 1);
  }
  // d) 进程等待,回收了进程的资源
  // 此处如果要进行进程等待最好使用waitpid
  // 保证当前线程回收的进程是自己创建的子进程
  // 更简洁的做法是直接忽略 SIGCHLD信号
  return 200;
}

int HandlerCGIChild(int child_read, int child_write, const HttpRequest* req) {
  // a) 设置环境变量(REQUEST_METHOD, QUERY_STRING, CONTENT_LENGTH)
  //    如果把上面这几个信息通过管道传递给被替换后的程序也是可行的
  //    但是要遵守CGI标准,必须使用环境变量传递以上的信息

  printf("In Child\n");
  char method_env[SIZE] = {0};
  sprintf(method_env, "REQUEST_METHOD=%s", req->method);
  putenv(method_env);
  printf("method_env is %s\n", getenv("REQUEST_METHOD"));

  if(strcasecmp(req->method, "GET") == 0) {
     // 设置QUERY_STRING
     char query_string_env[SIZE] = {0};
     sprintf(query_string_env, "QUERY_STRING=%s", req->query_string);
     putenv(query_string_env);
     printf("query_string is %s\n", req->query_string);
     printf("%s\n", getenv("QUERY_STRING"));
  }
  else {
    // 不是POST 就是 GET 
    // 设置content-length
    char conntent_length_env[SIZE] = {0};
    sprintf(conntent_length_env, "CONTENT_LENGTH=%d", req->conntent_length);
    putenv(conntent_length_env) ;
  }
  // b) 把标准输入和标准输出重定向到管道中
  //    此时在CGI程序中读写标准输入和标准输出就是读写管道
  dup2(child_read, 0);
  dup2(child_write, 1);
  // c) 子进程进行程序替换
  //    假设url_path 值为 /cgi-bin/test
  //    说明对应的CGI路径就是./wwwroot/cgi-bin/test
  char file_path[SIZE] = {0};
  HandlerFilePath(req->url_path, file_path);
  execl(file_path, "", NULL);
  // d)替换失败的错误处理,子进程就是为了替换而存在的
  //   如果替换失败就结束子进程
  exit(0);
}

int HandlerCGI(int new_sock, const HttpRequest* req) {
  // 1. 创建匿名管道
  int fd1[2], fd2[2];
  pipe(fd1);
  pipe(fd2);
  int father_read = fd1[0];
  int child_write = fd1[1];
  
  int child_read = fd2[0];
  int father_write = fd2[1];
  // 2. 创建子进程
  int err_code = 0;
  pid_t ret = fork();
  if(ret > 0) {
    // father 父进程
    // 关闭文件描述符
    // 为了保证后面父进程从子进程中读取数据的时候,read能够正确返回
    // 不阻塞的话,后面的代码中会循环读取数据,直到读到EOF就认为读完了
    // 而对于管道而言,必须把所有的写端关闭,再进行读,才是读到EOF
    // 这里的写端包括父进程的写端和子进程的写端
    // 子进程的写端会随着子进程的终止而结束
    // 父进程的写端就可以在此处关闭
    close(child_read);
    close(child_write);
    err_code = HandlerCGIFather(new_sock, father_read, father_write, req);
  }
  else if(ret == 0) {
    // child 子进程
    close(father_read);
    close(father_write);
    err_code = HandlerCGIChild(child_read, child_write, req);
  }
  else {
    perror("fork");
    goto END;
  }
END:
  close(father_read);
  close(father_write);
  close(child_read);
  close(child_write);
  if(err_code != 200) {
    return 404;
  }
  else {
    return err_code;
  }
}

void HandlerRequest(int64_t new_sock) {
    int err_code = 200;
    HttpRequest req;
    memset(&req, 0, sizeof(req));
    // 1. 读取请求并解析
    //  a) 松socket中读取HTTP服务请求的首行
    printf("brfore ReadLine");
    if(ReadLine(new_sock, req.first_line, sizeof(req.first_line) -1) < 0) {
      printf("ReadLine first_line failed\n");
      err_code = 404;
      goto END;
    }
    printf("first_line = %s\n", req.first_line);
    //  b) 解析首行,获取到方法和url
    if(ParseFirstLine(req.first_line, &req.method, &req.url) < 0) {
      printf("ParseFirstLine failed! first_line = %s\n", req.first_line);
      err_code = 404;
      goto END;
    }
    // c) 对url再次进行解析,解析出其中的url_path和query_string
    if(ParseQueryString(req.url, &req.url_path, &req.query_string) < 0) {
      printf("ParseQueryString failed! url = %s\n", req.url);
      err_code = 404;
      goto END;
    }
    // d) 读取并解析header部分(只保留content-length,为了简单)
    if(HandlerHeader(new_sock, &req.conntent_length)) {
        printf("HandlerHeader failed, conntent_length is %d\n", req.conntent_length);
        err_code = 404;
        goto END;
    }
    // 如果方法是 GET而且 query_string为空 ,则为静态页面
    if(strcasecmp(req.method, "GET") == 0 && req.query_string == NULL) {
      printf("prev  HandlerStaticFile");
      err_code = HandlerStaticFile(new_sock, &req);
    }
    // 如果方法是 GET 而且 query_string 不为空, 则为动态页面,根据query_string从参数内容来动态生成页面
    else if(strcasecmp(req.method, "GET") == 0 && req.query_string != NULL) {
      printf("prev HandlerCGI\n");
      err_code = HandlerCGI(new_sock, &req);
    }
    // 如果方法是 POST ,处理方法同 GET 方法右query_string 一样
    else if(strcasecmp(req.method, "POST") == 0) {
       printf("prev HandlerCGI\n");
      err_code = HandlerCGI(new_sock, &req);
    }
    else {
       printf("method not support! method = %s\n", req.method);
       err_code = 404;
       goto END;
    }
END:
    // 每次请求的收尾操作
    if(err_code != 200) {
      Handler404(new_sock);
    }
    close(new_sock);
}

void* ThreadyEntry(void* arg) {
  int64_t new_sock = (int64_t)arg;
  HandlerRequest(new_sock);
  return NULL;
}

void HttpServerStart(const char* ip, short port) {
  int sockfd = socket(AF_INET, SOCK_STREAM, 0);
  if(sockfd < 0) {
    perror("socket");
    return;
  }
  int opt = 1;
  setsockopt(sockfd, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt));

  struct sockaddr_in addr;
  addr.sin_family = AF_INET;
  addr.sin_addr.s_addr = inet_addr(ip);
  addr.sin_port = htons(port);

  int ret = bind(sockfd, (struct sockaddr*)&addr, sizeof(addr));
  if(ret < 0) {
    perror("bind");
    return;
  }
  ret = listen(sockfd, 5);
  if(ret < 0) {
    perror("listen");
    return;
  }
  printf("before pthead\n");
  while(1) {
    struct sockaddr_in peer;
    socklen_t len = sizeof(peer);
    int64_t new_sock = accept(sockfd, (struct sockaddr*)&peer, &len);
    if(new_sock < 0) {
      perror("accept");
      continue;
    }
    pthread_t tid;
    pthread_create(&tid, NULL, ThreadyEntry, (void*)new_sock);
    pthread_detach(tid);
  }
}

int main(int argc, char* argv[]) {
  if(argc != 3) {
    printf("Usage:./Http_Server [ip] [port]");
    return 1;
  }
  
  printf("Http Server Start\n");
  signal(SIGPIPE, SIG_IGN);
  signal(SIGCHLD, SIG_IGN);
  HttpServerStart(argv[1], atoi(argv[2]));
  return 0;
}
