## 适用于日志上传的程序

日志文件只写（不覆盖）上传是通常转储日志的一种需求，要求为：

* 客户端通过简单密码认证后，把日志文件上传到某个服务器的特定目录
* 上传时不允许覆盖服务器上已有的任何文件，只能上传新文件到指定目录下
* 客户端不能读取服务器上的任何文件


## 通信协议

通信协议非常简单，客户端与服务器建立连接后，发送
```
PASS 密码\n
```
用来验证身份，通过验证后，服务器返回
```
OK password ok\n
```
这时客户端发送
```
FILE 文件名 文件长度\n
文件内容.....
```
文件长度是可选的，如果不为0，服务器端接收对应长度的文件内容，写入文件，返回
```
OK file length 文件长度\n
```
表明正确传输

如果没有发送长度字段，或者长度为0，服务器端读到客户端中断连接为止算结束。

 
## 服务器端

translog_server是服务器端程序，执行时命令行如下：

```
# ./translog_server 
Usage:
./translog_server options
 options:
    -p port
    -f config_file
    -u user_name    change to user before write file

    -d              enable debug

config_file:
password work_dir

其中： 

-p 指明使用的tcp端口
-f 是配置文件
-u 是上传后文件的属主
-d 是开启调试模式，会显示一些调试信息

配置文件格式为：
密码 目录

客户端验证密码后，会把文件上传到目录下
```


## 客户端

客户端为python程序，命令行是:
```
./translog.py 
Usage: python ./translog.py <HostName> <PortNumber> <Password> <FileToSend> [ file_new_name ]
FileToSend - means read from stdin

如果 发送文件 是 - ，表明是从标准输入读

最后可选参数是服务器重命名文件。
```
