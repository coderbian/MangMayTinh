#include <winsock2.h>
#include <windows.h>
#include <iostream>
#include <fstream>
#include <set>
#include <string>
#include <thread>
#include <atomic>
#include <vector>

#pragma comment(lib, "Ws2_32.lib") // Liên kết thư viện Winsock

#define PORT 8080
#define BUFFER_SIZE 4096

std::atomic<int> activeThreads(0); // Đếm số luồng đang hoạt động

std::set<std::string> loadBlacklist(const std::string& filename) {
    std::set<std::string> blacklist;
    std::ifstream infile(filename);
    std::string line;
    while(std::getline(infile, line)) {
        blacklist.insert(line);
    }
    return blacklist;
}

bool isBlocked(const std::set<std::string>& blacklist, const std::string& url) {
    return blacklist.find(url) != blacklist.end();
}

std::string parseUrl(const std::string host) {
    //Nguyên nhân có hàm này là do host có thể đầu "www." và chứa đuôi ":443" nếu là https
    std::string url;
    int pos1 = host.find("www.");
    if(pos1 == 0) pos1 += 4;
    else pos1 = 0;
    int pos2 = host.find(':');
    if(pos2 == std::string::npos) url = host.substr(pos1);
    else url = host.substr(pos1, pos2 - pos1);
    return url;
}

void initWinsock() {
    WSADATA wsaData; //là một cấu trúc (structure) chứa thông tin về việc khởi tạo Winsock
    if (WSAStartup(MAKEWORD(2, 2), &wsaData) != 0) { //là hàm quan trọng để khởi động và thiết lập môi trường cho Winsock API. Hàm này cần được gọi trước khi sử dụng bất kỳ hàm socket nào khác của Winsock.
        std::cerr << "WSAStartup failed.\n";
        exit(EXIT_FAILURE);
    }
}

// void sendErrorResponse(SOCKET clientSocket) {
//     const char* errorResponse = 
//         "HTTP/1.1 403 Forbidden\r\n"
//         "Content-Type: text/html\r\n"
//         "Content-Length: 56\r\n"
//         "\r\n"
//         "<html><body><h1>403 Forbidden: Access Denied</h1></body></html>";
    
//     send(clientSocket, errorResponse, strlen(errorResponse), 0);
// }

void sendErrorResponse(SOCKET clientSocket) {
    const char* errorResponse =
        "HTTP/1.1 403 Forbidden\r\n"
        "Content-Type: text/html\r\n"
        "Content-Length: 138\r\n"
        "\r\n"
        "<html>"
        "<head><title>403 Forbidden</title></head>"
        "<body style='font-family: Arial, sans-serif; text-align: center;'>"
        "<h1>403 Forbidden</h1>"
        "<p>Access Denied</p>"
        "<hr>"
        "<p>Proxy Server</p>"
        "</body>"
        "</html>";
    send(clientSocket, errorResponse, strlen(errorResponse), 0);
}

SOCKET createSocket() {
    SOCKET listenSocket = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
//     socket(): Đây là hàm được sử dụng để tạo ra một socket. Socket là một điểm cuối trong giao tiếp mạng. Các tham số của hàm này bao gồm:
//     AF_INET: Đây là family của địa chỉ, chỉ định sử dụng giao thức IPv4.
//     SOCK_STREAM: Xác định loại socket, trong trường hợp này là một socket dòng (stream), nghĩa là một kết nối TCP, nơi dữ liệu được truyền tải dưới dạng dòng liên tục.
// I   PPROTO_TCP: Chỉ định giao thức sẽ được sử dụng, ở đây là giao thức TCP.
    if (listenSocket == INVALID_SOCKET) { 
        std::cerr << "Socket creation failed.\n";
        WSACleanup();
        exit(EXIT_FAILURE);
    }
    return listenSocket;
}

void bindSocket(SOCKET listenSocket) {
    sockaddr_in serverAddr; // Định nghĩa địa chỉ của server
    serverAddr.sin_family = AF_INET; // Định dạng địa chỉ IPv4
    serverAddr.sin_addr.s_addr = INADDR_ANY; // Chấp nhận kết nối từ mọi địa chỉ IP
    serverAddr.sin_port = htons(PORT); // Chuyển cổng sang định dạng network byte order

    if (bind(listenSocket, (struct sockaddr*)&serverAddr, sizeof(serverAddr)) == SOCKET_ERROR) {
        std::cerr << "Bind failed.\n";
        closesocket(listenSocket);
        WSACleanup();
        exit(EXIT_FAILURE);
    }
}

void startListening(SOCKET listenSocket) {
    if (listen(listenSocket, SOMAXCONN) == SOCKET_ERROR) {
        std::cerr << "Listen failed.\n";
        closesocket(listenSocket);
        WSACleanup();
        exit(EXIT_FAILURE);
    }
    //Nhiệm vụ chính nằm ở dưới thui :))
    std::cout << "Proxy server started. Listening on port " << PORT << "...\n";
}

std::string parseHttpRequest(const std::string& request) {
    size_t pos = request.find("Host: ");
    if (pos == std::string::npos) return "";
    size_t start = pos + 6;
    size_t end = request.find("\r\n", start);
    if (end == std::string::npos) return "";
    return request.substr(start, end - start); // Trả về host mà client muốn kết nối
}

void handleConnectMethod(SOCKET clientSocket, const std::string& host, int port) {
    // Tạo socket để kết nối đến server đích
    SOCKET remoteSocket = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
    if (remoteSocket == INVALID_SOCKET) {
        std::cerr << "Cannot create remote socket.\n";
        return;
    }

    // Định nghĩa địa chỉ của server đích
    sockaddr_in serverAddr;
    serverAddr.sin_family = AF_INET;
    serverAddr.sin_port = htons(port); // Chuyển cổng sang định dạng network byte order
    
    struct hostent* remoteHost = gethostbyname(host.c_str()); // Nhiệm vụ của hàm gethostbyname() là chuyển đổi tên miền sang địa chỉ IP
    if (remoteHost == NULL) {
        std::cerr << "Cannot resolve hostname.\n";
        closesocket(remoteSocket);
        return;
    }
    memcpy(&serverAddr.sin_addr.s_addr, remoteHost->h_addr, remoteHost->h_length);

    // Kết nối đến server đích
    if (connect(remoteSocket, (struct sockaddr*)&serverAddr, sizeof(serverAddr)) == SOCKET_ERROR) {
        std::cerr << "Cannot connect to remote server.\n";
        closesocket(remoteSocket);
        return;
    }

    // Gửi phản hồi 200 Connection Established cho client
    const char* established = "HTTP/1.1 200 Connection Established\r\n\r\n";
    send(clientSocket, established, strlen(established), 0);

    // Tạo kết nối hai chiều giữa client và server
    fd_set readfds; // Tập các socket đang đợi để đọc
    char buffer[BUFFER_SIZE]; 
    while (true) {
        FD_ZERO(&readfds); // Xóa tập readfds
        FD_SET(clientSocket, &readfds); // Thêm clientSocket vào tập readfds
        FD_SET(remoteSocket, &readfds); // Thêm remoteSocket vào tập readfds
        if (select(0, &readfds, NULL, NULL, NULL) > 0) { //select() trả socket chứa dữ liệu có thể đọc
            if (FD_ISSET(clientSocket, &readfds)) {
                int receivedBytes = recv(clientSocket, buffer, BUFFER_SIZE, 0);
                if (receivedBytes <= 0) break;
                send(remoteSocket, buffer, receivedBytes, 0);
            }
            if (FD_ISSET(remoteSocket, &readfds)) {
                int receivedBytes = recv(remoteSocket, buffer, BUFFER_SIZE, 0);
                if (receivedBytes <= 0) break;
                send(clientSocket, buffer, receivedBytes, 0);
            }
        } else break;
    }
    closesocket(remoteSocket);
}

void handleClient(SOCKET clientSocket, const std::set<std::string>& blacklist) {
    activeThreads++;
    std::cout << "Active Threads: " << activeThreads.load() << std::endl;
    char buffer[BUFFER_SIZE];
    int receivedBytes = recv(clientSocket, buffer, BUFFER_SIZE, 0);
    if (receivedBytes > 0) {
        std::string request(buffer, receivedBytes);
        std::string host = parseHttpRequest(request);
        
        if (!host.empty()) {
            std::cerr << "Client is trying to access: " << host << '\n';
            //Kiểm tra nếu tên miền có trong blacklist
            std::string url = parseUrl(host);
            if(isBlocked(blacklist, url)) {
                std::cerr << "Access to " << url << " is blocked.\n";
                sendErrorResponse(clientSocket);
                activeThreads--;
                closesocket(clientSocket);
                return;
            }
            size_t colonPos = host.find(':');
            std::string hostname = host.substr(0, colonPos);
            int port = 443; // Mặc định HTTPS dùng cổng 443
            if (colonPos != std::string::npos) {
                port = std::stoi(host.substr(colonPos + 1));
            }
            handleConnectMethod(clientSocket, hostname, port);
        }
    }
    activeThreads--;
    closesocket(clientSocket);
}

int main() {
    initWinsock();
    SOCKET listenSocket = createSocket();
    bindSocket(listenSocket);
    startListening(listenSocket);

    std::set<std::string> blacklist = loadBlacklist("blacklist.txt");

    while (true) {
        SOCKET clientSocket = accept(listenSocket, NULL, NULL);
        if (clientSocket != INVALID_SOCKET) {
            std::thread clientThread(handleClient, clientSocket, std::ref(blacklist));
            clientThread.detach(); // Tạo luồng mới để xử lý từng client
        }
    }

    closesocket(listenSocket);
    WSACleanup();
    return 0;
}
