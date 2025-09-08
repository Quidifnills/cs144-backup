#include "socket.hh"
#include "util.hh"

#include <cstdlib>
#include <iostream>

using namespace std;

void get_URL(const string &host, const string &path) {
    // 1) 建立 TCP 连接到 host:80（“http” 等价于端口 80）
    TCPSocket sock;
    sock.connect( Address( host, "http" ) );

    // 2) 组装 HTTP/1.1 请求报文（行尾必须是 \r\n）
    string request;
    request += "GET " + path + " HTTP/1.1\r\n";
    request += "Host: " + host + "\r\n";
    request += "Connection: close\r\n";
    request += "\r\n";               // 空行表示请求头结束（没有请求体）

    // 3) 发送请求
    sock.write( request );

    // 4) 读取服务器的全部响应直到 EOF，并打印到标准输出
    while ( !sock.eof() ) {
        string chunk;
        sock.read(chunk);    // Minnow 的 FileDescriptor 接口：读到尽可能多的数据
        cout << chunk;
    }

    // cerr << "Function called: get_URL(" << host << ", " << path << ").\n";
    // cerr << "Warning: get_URL() has not been implemented yet.\n";
}

int main(int argc, char *argv[]) {
    try {
        if (argc <= 0) {
            abort();  // For sticklers: don't try to access argv[0] if argc <= 0.
        }

        // The program takes two command-line arguments: the hostname and "path" part of the URL.
        // Print the usage message unless there are these two arguments (plus the program name
        // itself, so arg count = 3 in total).
        if (argc != 3) {
            cerr << "Usage: " << argv[0] << " HOST PATH\n";
            cerr << "\tExample: " << argv[0] << " stanford.edu /class/cs144\n";
            return EXIT_FAILURE;
        }

        // Get the command-line arguments.
        const string host = argv[1];
        const string path = argv[2];

        // Call the student-written function.
        get_URL(host, path);
    } catch (const exception &e) {
        cerr << e.what() << "\n";
        return EXIT_FAILURE;
    }

    return EXIT_SUCCESS;
}
