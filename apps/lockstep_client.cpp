#include "lockstep/protocol/codec.hpp"
#include "lockstep/protocol/frame.hpp"
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <unistd.h>
#include <iostream>
#include <string>

using namespace lockstep;

int main(int argc, char** argv) {
    std::cout << "Lockstep Client\n";
    std::cout << "================\n\n";
    
    int sock = socket(AF_INET, SOCK_STREAM, 0);
    if (sock < 0) {
        std::cerr << "Failed to create socket\n";
        return 1;
    }
    
    sockaddr_in addr{};
    addr.sin_family = AF_INET;
    addr.sin_port = htons(9999);
    inet_pton(AF_INET, "127.0.0.1", &addr.sin_addr);
    
    if (connect(sock, reinterpret_cast<sockaddr*>(&addr), sizeof(addr)) < 0) {
        std::cerr << "Failed to connect to exchange\n";
        close(sock);
        return 1;
    }
    
    std::cout << "Connected to exchange on port 9999\n";
    std::cout << "Commands: new, cancel, quit\n\n";
    
    std::string line;
    while (std::cin >> line) {
        if (line == "quit") {
            break;
        }
        
        if (line == "new") {
            NewOrderPayload payload;
            payload.clientId = 1;
            payload.orderId = 1;
            payload.instrumentId = 1;
            payload.side = Side::Buy;
            payload.tif = TimeInForce::GTC;
            payload.price = 150;
            payload.quantity = 100;
            
            uint8_t buffer[128];
            size_t len = Codec::encodeNewOrder(payload, buffer, sizeof(buffer));
            
            write(sock, buffer, len);
            
            std::cout << "Sent new order\n";
        } else if (line == "cancel") {
            std::cout << "Cancel not implemented\n";
        }
    }
    
    close(sock);
    std::cout << "Disconnected.\n";
    
    return 0;
}
