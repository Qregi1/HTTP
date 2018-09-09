#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

////////////////////////////////
// 简单的计算加法的CGI程序
///////////////////////////////


int GetQueryString(char output[]) {
  // 1. 先从环境变量中获取到方法
  char* method = getenv("REQUEST_METHOD");
  if(method == NULL) {
     fprintf(stderr, "REQUEST_METHOD failed\n");
     return -1;
  }
  // 2. 如果是 GET 方法，此时直接从环境变量中
  //    获取到 QUERY_STRING
  if(strcasecmp(method, "GET") == 0) {
    char* query_string = getenv("QUERY_STRING");
    if(query_string == NULL) {
      fprintf(stderr, "QUERY_STRING failed\n");
      return -1;
    }
    strcpy(output, query_string);
  } else {
  // 3. 如果是 POST 方法，此时先通过环境变量获取到 CONTENT_LENGTH
  //    再从标准输入中读取 body
     char* content_length_str = getenv("CONTENT_LENGTH");
     if(content_length_str == NULL) {
       fprintf(stderr, "CONTENT_LENGTH failed\n");
       return -1;
     }
     int content_length = atoi(content_length_str);
    int i = 0;
     for(; i < content_length; ++i) {
       read(0, output + i, 1 );
     } 
    output[content_length] = '\0';
  }
  return 0;
}

int main() {
  // 1. 先获取到参数(方法,query_string, body)
  char buf[1024 *  4] = {0};
  int ret = GetQueryString(buf); 
  if(ret < 0) {
    fprintf(stderr, "GetQueryString failed\n");
    return 1;
  }
  // 2. 解析 buf 中的参数，具体的解析规则和业务相关
  //    解析时需要按照客户端构造请求的 key 来进行解析
  //    此处的 key 叫做 a,b
  //    a=10&b=20
  int a,b;
  sscanf(buf, "a=%d&b=%d",&a, &b);
  // 3. 根据业务的具体要求完成计算
  int c = a + b;
  // 4. 把结果构造成页面返回给浏览器
  printf("<h1>ret=%d</h1>", c);
  return 0;
}
