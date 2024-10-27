How to build: "g++ -o a.exe .\server.cpp -lws2_32"

Code đã chạy đúng, nếu dùng lệnh:
Invoke-WebRequest -Uri http://example.com -Proxy http://192.168.2.11:8080
thì mình hoàn toàn có thể truy cập example.com để có thể lấy dữ liệu bằng pws
Nhưng hiện tại việc đọc nhiều request cũng lúc đang bị trục trặc, lỗi đang gặp phải khi gửi request từ Firefox là:
    "tôi đang thực hiện gửi request từ firefox với yêu cầu truy cập google.com nhưng trong terminal lại hiện như thế này: "PS D:\Hieu Hoc va HCMUS\K23_HK3\Mạng máy tính\Project\PROXY> .\a.exe
    Proxy server is listening on port 8080...
    Received request: GET http://google.com/ HTTP/1.1
    Host: google.com
    User-Agent: Mozilla/5.0 (Windows NT 10.0; Win64; x64; rv:131.0) Gecko/20100101 Firefox/131.0
    Accept: text/html,application/xhtml+xml,application/xml;q=0.9,image/avif,image/webp,image/png,image/svg+xml,*/*;q=0.8
    Accept-Language: en-US,en;q=0.5
    Accept-Encoding: gzip, deflate
    Connection: keep-alive
    Upgrade-Insecure-Requests: 1
    Priority: u=0, i"
Có vẻ như định dạng mà firefox yêu cầu gửi về là: gzip, deflate -> tìm cách cài đặt hàm giải nén nó

Hôm nay đến đây thui :>>
