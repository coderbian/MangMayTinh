#include <winsock2.h>   // Thư viện cho Winsock
#include <ws2tcpip.h>   // Thư viện hỗ trợ các địa chỉ TCP/IP
#include <iostream>
#include <string>

#pragma comment(lib, "Ws2_32.lib")  // Liên kết thư viện Winsock

const int BUFFER_SIZE = 1024;

// Hàm khởi tạo Winsock
bool initWinsock() {
    WSADATA wsaData;
    int result = WSAStartup(MAKEWORD(2, 2), &wsaData);
    if (result != 0) {
        std::cerr << "WSAStartup failed: " << result << std::endl;
        return false;
    }
    return true;
}

void handleClient(SOCKET clientSocket) {
    char buffer[BUFFER_SIZE];
    int bytesReceived = recv(clientSocket, buffer, BUFFER_SIZE, 0);
    
    if (bytesReceived > 0) {
        buffer[bytesReceived] = '\0';
        std::cout << "Received request: " << buffer << std::endl;

        std::string request(buffer);

        // Kiểm tra xem có phải yêu cầu CONNECT không
        if (request.substr(0, 7) == "CONNECT") {
            size_t hostPos = request.find("Host: ");
            size_t endHostPos = request.find("\r\n", hostPos);
            std::string host = request.substr(hostPos + 6, endHostPos - hostPos - 6);
            
            // Tách host và port
            size_t colonPos = host.find(':');
            std::string hostname = host.substr(0, colonPos);
            std::string port = host.substr(colonPos + 1);

            struct addrinfo hints = {}, *result;
            hints.ai_family = AF_INET;
            hints.ai_socktype = SOCK_STREAM;
            hints.ai_protocol = IPPROTO_TCP;

            if (getaddrinfo(hostname.c_str(), port.c_str(), &hints, &result) != 0) {
                std::cerr << "Getaddrinfo failed: " << WSAGetLastError() << std::endl;
                closesocket(clientSocket);
                return;
            }

            SOCKET remoteSocket = socket(result->ai_family, result->ai_socktype, result->ai_protocol);
            if (remoteSocket == INVALID_SOCKET) {
                std::cerr << "Failed to create remote socket: " << WSAGetLastError() << std::endl;
                freeaddrinfo(result);
                closesocket(clientSocket);
                return;
            }

            if (connect(remoteSocket, result->ai_addr, (int)result->ai_addrlen) == SOCKET_ERROR) {
                std::cerr << "Connect to remote server failed: " << WSAGetLastError() << std::endl;
                closesocket(remoteSocket);
                freeaddrinfo(result);
                closesocket(clientSocket);
                return;
            }
            freeaddrinfo(result);

            // Gửi phản hồi 200 OK tới client
            std::string httpResponse = "HTTP/1.1 200 Connection Established\r\n\r\n";
            send(clientSocket, httpResponse.c_str(), httpResponse.length(), 0);

            // Chuyển tiếp dữ liệu giữa client và remote server
            fd_set readfds;
            while (true) {
                FD_ZERO(&readfds);
                FD_SET(clientSocket, &readfds);
                FD_SET(remoteSocket, &readfds);

                int max_sd = (clientSocket > remoteSocket) ? clientSocket : remoteSocket;
                int activity = select(max_sd + 1, &readfds, NULL, NULL, NULL);

                if (activity > 0) {
                    if (FD_ISSET(clientSocket, &readfds)) {
                        int bytes = recv(clientSocket, buffer, BUFFER_SIZE, 0);
                        if (bytes <= 0) break;
                        send(remoteSocket, buffer, bytes, 0);
                    }

                    if (FD_ISSET(remoteSocket, &readfds)) {
                        int bytes = recv(remoteSocket, buffer, BUFFER_SIZE, 0);
                        if (bytes <= 0) break;
                        send(clientSocket, buffer, bytes, 0);
                    }
                }
            }

            closesocket(remoteSocket);
            closesocket(clientSocket);
        } else {
            // Xử lý các yêu cầu HTTP khác (GET, POST)
            size_t hostPos = request.find("Host: ");
            size_t endHostPos = request.find("\r\n", hostPos);
            std::string host = request.substr(hostPos + 6, endHostPos - hostPos - 6);
            
            // Tách host và port
            size_t colonPos = host.find(':');
            std::string hostname = colonPos != std::string::npos ? host.substr(0, colonPos) : host;
            std::string port = colonPos != std::string::npos ? host.substr(colonPos + 1) : "80";  // Mặc định port 80 nếu không có

            struct addrinfo hints = {}, *result;
            hints.ai_family = AF_INET;
            hints.ai_socktype = SOCK_STREAM;
            hints.ai_protocol = IPPROTO_TCP;

            if (getaddrinfo(hostname.c_str(), port.c_str(), &hints, &result) != 0) {
                std::cerr << "Getaddrinfo failed: " << WSAGetLastError() << std::endl;
                closesocket(clientSocket);
                return;
            }

            SOCKET remoteSocket = socket(result->ai_family, result->ai_socktype, result->ai_protocol);
            if (remoteSocket == INVALID_SOCKET) {
                std::cerr << "Failed to create remote socket: " << WSAGetLastError() << std::endl;
                freeaddrinfo(result);
                closesocket(clientSocket);
                return;
            }

            if (connect(remoteSocket, result->ai_addr, (int)result->ai_addrlen) == SOCKET_ERROR) {
                std::cerr << "Connect to remote server failed: " << WSAGetLastError() << std::endl;
                closesocket(remoteSocket);
                freeaddrinfo(result);
                closesocket(clientSocket);
                return;
            }
            freeaddrinfo(result);

            // Gửi yêu cầu từ client đến server đích
            send(remoteSocket, buffer, bytesReceived, 0);

            // Nhận phản hồi từ server và gửi lại client
            while ((bytesReceived = recv(remoteSocket, buffer, BUFFER_SIZE, 0)) > 0) {
                send(clientSocket, buffer, bytesReceived, 0);
            }

            closesocket(remoteSocket);
            closesocket(clientSocket);
        }
    } else {
        std::cerr << "Receive from client failed: " << WSAGetLastError() << std::endl;
        closesocket(clientSocket);
    }
}

int main() {
    if (!initWinsock()) {
        return 1;
    }

    SOCKET serverSocket = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
    if (serverSocket == INVALID_SOCKET) {
        std::cerr << "Socket creation failed: " << WSAGetLastError() << std::endl;
        WSACleanup();
        return 1;
    }

    sockaddr_in serverAddr;
    serverAddr.sin_family = AF_INET;
    serverAddr.sin_port = htons(8080); // Chọn một cổng bất kỳ, ví dụ: 8080
    serverAddr.sin_addr.s_addr = INADDR_ANY; // Lắng nghe trên tất cả các interface

    if (bind(serverSocket, (SOCKADDR*)&serverAddr, sizeof(serverAddr)) == SOCKET_ERROR) {
        std::cerr << "Bind failed: " << WSAGetLastError() << std::endl;
        closesocket(serverSocket);
        WSACleanup();
        return 1;
    }

    if (listen(serverSocket, SOMAXCONN) == SOCKET_ERROR) {
        std::cerr << "Listen failed: " << WSAGetLastError() << std::endl;
        closesocket(serverSocket);
        WSACleanup();
        return 1;
    }

    std::cout << "Proxy server is listening on port 8080..." << std::endl;

    // Vòng lặp lắng nghe và xử lý client
    while (true) {
        SOCKET clientSocket = accept(serverSocket, NULL, NULL);
        if (clientSocket == INVALID_SOCKET) {
            std::cerr << "Accept failed: " << WSAGetLastError() << std::endl;
            closesocket(serverSocket);
            WSACleanup();
            return 1;
        }

        // Xử lý yêu cầu từ client
        handleClient(clientSocket);
    }

    // Dọn dẹp Winsock khi không còn sử dụng
    closesocket(serverSocket);
    WSACleanup();
    return 0;
}
