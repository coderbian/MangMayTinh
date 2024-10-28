#include <winsock2.h>
#include <windows.h>
#include <iostream>
#include <string>
#include <thread>
#include <atomic>

std::atomic<int> activeThreads(0);

#pragma comment(lib, "Ws2_32.lib") // -lws2_32

#define PORT 8080
#define BUFFER_SIZE 4096

void initWinsock() {
    WSADATA wsaData;
    if (WSAStartup(MAKEWORD(2, 2), &wsaData) != 0) {
        std::cerr << "WSAStartup failed.\n";
        exit(EXIT_FAILURE);
    }
}

SOCKET createSocket() {
    SOCKET listenSocket = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
    if (listenSocket == INVALID_SOCKET) {
        std::cerr << "Socket creation failed.\n";
        WSACleanup();
        exit(EXIT_FAILURE);
    }
    return listenSocket;
}

void bindSocket(SOCKET listenSocket) {
    sockaddr_in serverAddr;
    serverAddr.sin_family = AF_INET;
    serverAddr.sin_addr.s_addr = INADDR_ANY;
    serverAddr.sin_port = htons(PORT);

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
    std::cout << "Proxy server started. Listening on port " << PORT << "...\n";
}

std::string parseHttpRequest(const std::string& request) {
    size_t pos = request.find("Host: ");
    if (pos == std::string::npos) return "";
    size_t start = pos + 6;
    size_t end = request.find("\r\n", start);
    if (end == std::string::npos) return "";
    return "https://" + request.substr(start, end - start);
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
    serverAddr.sin_port = htons(port);

    struct hostent* remoteHost = gethostbyname(host.c_str());
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
    fd_set readfds;
    char buffer[BUFFER_SIZE];
    while (true) {
        FD_ZERO(&readfds);
        FD_SET(clientSocket, &readfds);
        FD_SET(remoteSocket, &readfds);
        if (select(0, &readfds, NULL, NULL, NULL) > 0) {
            if (FD_ISSET(clientSocket, &readfds)) {
                int receivedBytes = recv(clientSocket, buffer, BUFFER_SIZE, 0);
                if (receivedBytes <= 0) break;
                send(remoteSocket, buffer, receivedBytes, 0);
                // std::cerr << receivedBytes << '\n';
            }
            if (FD_ISSET(remoteSocket, &readfds)) {
                int receivedBytes = recv(remoteSocket, buffer, BUFFER_SIZE, 0);
                if (receivedBytes <= 0) break;
                send(clientSocket, buffer, receivedBytes, 0);
                // std::cerr << receivedBytes << '\n';
            }
        } else break;
    }
    closesocket(remoteSocket);
}

void handleClient(SOCKET clientSocket) {
    activeThreads++;
    std::cout << "Active Threads: " << activeThreads.load() << std::endl;
    char buffer[BUFFER_SIZE];
    int receivedBytes = recv(clientSocket, buffer, BUFFER_SIZE, 0);
    if (receivedBytes > 0) {
        std::string request(buffer, receivedBytes);
        std::string url = parseHttpRequest(request);

        if (!url.empty()) {
            std::cerr << "Accessed URL: " << url << '\n';
            size_t hostPos = request.find(' ') + 1;
            if (std::string(url.begin() + 7, url.end()).find(':') == std::string::npos) return;
            size_t portPos = request.find(':', hostPos);
            std::string host = request.substr(hostPos, portPos - hostPos);
            int port = stoi(request.substr(portPos + 1, request.find(' ', portPos) - portPos - 1));
            handleConnectMethod(clientSocket, host, port);
        }

        // Forward the request to target server (not implemented in this example)
        // and send the response back to the client.
    }
    activeThreads--;
    closesocket(clientSocket);
}

int main() {
    initWinsock();
    SOCKET listenSocket = createSocket();
    bindSocket(listenSocket);
    startListening(listenSocket);

    while (true) {
        SOCKET clientSocket = accept(listenSocket, NULL, NULL);
        if (clientSocket != INVALID_SOCKET) {
            // Launch a new thread to handle each client connection
            std::thread clientThread(handleClient, clientSocket);
            clientThread.detach(); // Detach the thread to run independently
        }
    }

    closesocket(listenSocket);
    WSACleanup();
    return 0;
}
