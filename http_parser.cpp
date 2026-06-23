#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <errno.h>
#include <cstring>
#include <string_view>
#include <fcntl.h>
#include <iostream>
#include <algorithm>
#include <vector>
#define BUFFER_SIZE 4096

//主状态机状态
enum CHECK_STATE {
    CHECK_STATE_REQUESTLINE = 0, //当前正在分析请求行
    CHECK_STATE_HEADER = 1       //当前正在分析头部字段
};

//从状态机状态
enum LINE_STATUS {
    LINE_OK = 0, //读取到一个完整的行
    LINE_BAD,    //行出错
    LINE_OPEN,   //行数据尚不完整
};

//服务器处理HTTP请求的结果
enum HTTP_CODE {
    NO_REQUEST,          //请求不完整，需要继续读取客户数据
    GET_REQUEST,         //获得了一个完整的客户请求
    BAD_REQUEST,         //客户请求有语法错误
    FORBIDDEN_REQUEST,   //客户对资源没有足够访问权限
    INTERNAL_ERROR,      //服务器内部错误
    CLOSED_CONNECTION    //客户端关闭连接
};

//static const char* szret[] = {"I get a correct result\n", "something wrong\n"};

//辅助函数:不区分大小写的比较
bool iequals(std::string_view lhs, std::string_view rhs) {
    return std::equal(lhs.begin(), lhs.end(), rhs.begin(), rhs.end(), 
                        [](char a, char b) { return ::tolower(a) == ::tolower(b);});
}

//辅助函数:去除string_view两端的空格和制表符
std::string_view trim(std::string_view str) {
    size_t first = str.find_first_not_of(" \t");
    if (first == std::string_view::npos) {
        return "";
    }
    size_t last = str.find_last_not_of(" \t");
    return str.substr(first, (last - first + 1));
}

//从状态机:重构版:
LINE_STATUS parse_line(std::string_view buffer, size_t& checked_index) {
    while(checked_index < buffer.size()) {
        char temp = buffer[checked_index];
        if (temp == '\r') {
            if (checked_index + 1 == buffer.size()) {
                return LINE_OPEN;
            }
            else if (buffer[checked_index + 1] == '\n') {
                checked_index += 2; //跳过/r/n
                return LINE_OK;
            }
            return LINE_BAD;
        }
        else if(temp == '\n') {
            checked_index += 1;
            return LINE_OK;
        }
        checked_index++;
    }

    return LINE_OPEN;
}


//分析请求行：重构版
HTTP_CODE parse_requestline(std::string_view line, CHECK_STATE& checkstate)  {
    //寻找第一个制表符
    size_t method_end = line.find_first_of(" \t");
    if (method_end == std::string_view::npos) {
        return BAD_REQUEST;
    }

    std::string_view method = line.substr(0, method_end);
    if (iequals(method, "GET")) {
        std::cout << "[DEBUG] Method: GET\n";
    }
    else {
        return BAD_REQUEST;
    }

    //找出URL的范围
    size_t url_start = line.find_first_not_of(" \t", method_end);
    if (url_start == std::string_view::npos) {
        return BAD_REQUEST;
    }
    size_t url_end = line.find_first_of(" \t", url_start);
    if (url_end == std::string_view::npos) {
        return BAD_REQUEST;
    }
    std::string_view url = line.substr(url_start, (url_end - url_start));

    //找出version
    size_t version_start = line.find_first_not_of(" \t", url_end);
    if (version_start == std::string_view::npos) {
        return BAD_REQUEST;
    }
    size_t version_end = line.find_first_of(" \t");
    if (version_end == std::string_view::npos) {
        return BAD_REQUEST;
    }
    std::string_view version = line.substr(version_start);
    version = trim(version);
    if (!iequals(version, "HTTP/1.1")) {
        return BAD_REQUEST;
    }
    
    //检查并解析URL
    std::string_view target = "http://";
    if (std::equal(url.begin(), url.begin() + 7, target.begin(), target.end())) {
        url.remove_prefix(7);
        size_t slash_pos = url.find('/');
        if (slash_pos == std::string_view::npos) {
            return BAD_REQUEST;
        }
        url = url.substr(slash_pos);
    }

    if (url.empty() || url[0] != '/') {
        return BAD_REQUEST;
    }

    std::cout << "The request URL is: " << url << "\n";
    checkstate = CHECK_STATE_HEADER;
    return NO_REQUEST;
}



//分析头部字段（重构）
HTTP_CODE parse_headers(std::string_view line)  {
    line = trim(line);
    if (line.empty()) {
        return GET_REQUEST;
    }

    std::string_view HOST = "HOST:";
    std::string_view Host = "Host:";
    std::string_view Connection = "Connection:";
    std::string_view Upgrade = "Upgrade-Insecure-Requests:";
    std::string_view UserAgent = "User-Agent:";
    std::string_view Accept = "Accept:";
    std::string_view AcceptEncoding = "Accept-Encoding:";
    std::string_view  AcceptLanguage = "Accept-Language:";


    if (std::equal(line.begin(), line.begin() + 5, HOST.begin(), HOST.end()) || 
        std::equal(line.begin(), line.begin() + 5, Host.begin(), Host.end())) {
        std::string_view host_val = line.substr(5);
        host_val = trim(host_val);
        std::cout << "The request host is : " << host_val << "\n";
    }
    else if (std::equal(line.begin(), line.begin() + Connection.size(), Connection.begin(), Connection.end())) {
        std::string_view conn_val = line.substr(Connection.size());
        std::cout << "The Connection status is : " << conn_val << "\n";
    }
    else if (std::equal(line.begin(), line.begin() + Upgrade.size(), Upgrade.begin(), Upgrade.end())) {
        std::string_view up_val = line.substr(Upgrade.size());
        std::cout << "The Upgrade-Insecure-Requests is : " << up_val << "\n";
    }
    else if (std::equal(line.begin(), line.begin() + UserAgent.size(), UserAgent.begin(), UserAgent.end())) {
        std::string_view ua_val = line.substr(UserAgent.size());
        std::cout << "The User-Agent is : " << ua_val << "\n";
    }
    else if (std::equal(line.begin(), line.begin() + Accept.size(), Accept.begin(), Accept.end())) {
        std::string_view accept_val = line.substr(Accept.size());
        std::cout << "The Accept is : " << accept_val << "\n";
    }
    else if (std::equal(line.begin(), line.begin() + AcceptEncoding.size(), AcceptEncoding.begin(), AcceptEncoding.end())) {
        std::string_view acceptEncoding_val = line.substr(Accept.size());
        std::cout << "The AcceptEncoding is : " << acceptEncoding_val << "\n";
    }
    else if (std::equal(line.begin(), line.begin() + AcceptLanguage.size(), AcceptLanguage.begin(), AcceptLanguage.end())) {
        std::string_view acceptLanguage_val = line.substr(Accept.size());
        std::cout << "The AcceptLanguage is : " << acceptLanguage_val << "\n";
    }
    else {
        std::cout << "I cant handle this header: " << line << "\n";
    }

    return NO_REQUEST;
}


//分析HTTP请求的入口函数
HTTP_CODE parse_content(std::string_view buffer, size_t& checked_index, CHECK_STATE& checkstate, size_t& start_line) {
    LINE_STATUS linestatus = LINE_OK; //记录当前行读取状态
    HTTP_CODE retcode = NO_REQUEST; //记录HTTP请求的处理结果
    
    //主状态机，用于从buffer中取出所有完整的行
    while ((linestatus = parse_line(buffer, checked_index)) == LINE_OK) {
        std::string_view line = buffer.substr(start_line, checked_index - start_line);
       
        constexpr std::string_view crlf = "\r\n";
        constexpr std::string_view lf   = "\n";

        if (line.size() >= crlf.size() &&
            std::equal(line.end() - crlf.size(), line.end(), crlf.begin(), crlf.end())) {
            line.remove_suffix(crlf.size());  // 去掉 2 个字符
        }
        else if (line.size() >= lf.size() &&
                std::equal(line.end() - lf.size(), line.end(), lf.begin(), lf.end())) {
            line.remove_suffix(lf.size());    // 去掉 1 个字符
        }

        start_line = checked_index;
        //checkstate记录主状态机当前状态
        switch(checkstate) {
            //状态一: 分析请求行
            case CHECK_STATE_REQUESTLINE:
            {
                retcode = parse_requestline(line, checkstate);
                if (retcode == BAD_REQUEST) {
                    return BAD_REQUEST;
                }
                break;
            }

            //状态二: 分析头部字段
            case CHECK_STATE_HEADER:
            {
                retcode = parse_headers(line);
                if (retcode == BAD_REQUEST) {
                    return BAD_REQUEST;
                }
                else if(retcode == GET_REQUEST) {
                    return GET_REQUEST;
                }
                break;
            }

            default:
            {
                return INTERNAL_ERROR;
            }
        }
    }

    if (linestatus == LINE_OPEN) {
        return NO_REQUEST;
    }
    else {
        return BAD_REQUEST;
    }
}

int main(int argc, char* argv[]) {
    if (argc <= 2) {
        printf("Usage:%s ip_address port_number\n", argv[0]);
        return 1;
    }
    
    const char* ip = argv[1];
    int port = atoi(argv[2]);

    struct sockaddr_in address;
    memset(&address, 0, sizeof(address));
    address.sin_family = AF_INET;
    inet_pton(AF_INET, ip, &address.sin_addr);
    address.sin_port = htons(port);

    int listenfd = socket(PF_INET, SOCK_STREAM, 0);
    assert(listenfd >= 0);
    
    int ret = bind(listenfd, (const struct sockaddr*)&address, sizeof(address));
    assert(ret != -1);

    ret = listen(listenfd, 5);
    assert(ret != -1);
    
    struct sockaddr_in client_address;
    socklen_t clinet_addrlength = sizeof(client_address);

    int fd = accept(listenfd, (struct sockaddr*)&client_address, &clinet_addrlength);
    if (fd < 0) {
        printf("errno is %d\n", errno);
    }
    else {
        std::vector<char> buffer(BUFFER_SIZE, '\0'); //读缓冲区
        ssize_t data_read = 0;
        size_t read_index = 0; //当前已读多少字节客户数据
        size_t checked_index = 0; //当前分析完多少字节客户数据
        size_t start_line = 0; //行在buffer中的起始位置

        CHECK_STATE checkstate = CHECK_STATE_REQUESTLINE;

        while (1) { //循环获取客户数据并分析之
            data_read = recv(fd, buffer.data() + read_index, buffer.size() - read_index, 0);
            if (data_read == -1) {
                printf("reading failed\n");
                break;
            }
            else if(data_read == 0) {
                printf("remote client has closed the connection\n");
                break;
            }
            read_index += data_read;

            //构建当前总接受数据的只读视图
            std::string_view current_view(buffer.data(), read_index);

            //分析目前已经获得的所有客户数据
            HTTP_CODE result = parse_content(current_view, checked_index, checkstate, start_line);
            if (result == NO_REQUEST) {
                continue;
            }
            else if(result == GET_REQUEST) {
                std::string_view success_msg = "I get a correct result\n";
                send(fd, success_msg.data(), success_msg.size(), 0);
                break;
            }
            else {
                std::string_view fail_msg = "something wrong\n";
                send(fd, fail_msg.data(), fail_msg.size(), 0);
                //break;
            }
        }

        close(fd);
    }
    close(listenfd);
    return 0;
}