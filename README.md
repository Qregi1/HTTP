HTTP服务器工作流程
     
   （1） 服务器启动，在指定端口绑定http服务

   （2）收到一个 HTTP 请求时（其实就是 listen 的端口 accpet 的时候），派生一个线程运行处理请求.
   
   （3）取出 HTTP 请求中的 method (GET 或 POST) 和 url,。对于 GET 方法，如果有携带参数，则 query_string 指针指向 url 中 ？ 后面的 GET 参数.
   
   （4） 格式化 url 到 path 数组，表示浏览器请求的服务器文件路径，在MyHttp服务器文件是在wwwroot文件夹下。当 url 以 / 结尾，或 url 是个目录，则默认在 path 中加上 index.html，表示访问主页.
   
   （5）如果文件路径合法，对于无参数的 GET 请求，直接输出服务器文件到浏览器，即用 HTTP 格式写到套接字上，跳到（10）。其他情况（带参数 GET，POST 方式，url 为可执行文件），则调用 HandlerCGI 函数执行 cgi 脚本.
   
   (6) 在子进程中，把 STDOUT 重定向到 cgi的写入端，把 STDIN 重定向到 cgi的读取端，关闭 cgi_input 的写入端 和 cgi_output 的读取端，设置 REQUEST_METHOD的环境变量，GET 的话设置QUERY_STRING的环境变量，POST 的话设置CONTENT_LENGTH的环境变量，这些环境变量都是为了给 cgi 脚本调用，接着用 execl 运行 cgi 程序.
   
   (7) 在父进程中,如果是 POST 请求的话，把body部分数据读取然后写入管道,把动态生成页面交给子进程处理,然后构造HTTP服务器响应的首行,最后再等待子进程


更新一:
应用这个服务器框架做了一个在线简历
服务器根据请求返回一个index.html文件,这个文件就是我的个人简历
模板是从网上找的

//在线简历图片演示
![图片1](https://github.com/Qregi1/HTTP/blob/master/Snipaste_2018-09-09_08-45-23.png)
![图片2](https://github.com/Qregi1/HTTP/blob/master/Snipaste_2018-09-09_08-45-34.png)
