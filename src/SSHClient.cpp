#include "SSHClient.h"
#include <iostream>
#include <fcntl.h>
#include <sys/socket.h>
#include <arpa/inet.h>
#include <unistd.h>
#include <cstring>
#include <sstream>

SSHClient::SSHClient(const std::string &host, const std::string &user)
    : host_(host), user_(user), my_ssh_session(nullptr) {}

SSHClient::~SSHClient() {
    close();
}

ssh_channel SSHClient::connectSSH(const std::string &password, int timeoutSec) {
    close();
    my_ssh_session = ssh_new();
    if (my_ssh_session == nullptr) {
        std::cerr << "Ошибка создания SSH сессии" << std::endl;
        return nullptr;
    }

    ssh_options_set(my_ssh_session, SSH_OPTIONS_HOST, host_.c_str());
    ssh_options_set(my_ssh_session, SSH_OPTIONS_USER, user_.c_str());

    int sock = socket(AF_INET, SOCK_STREAM, 0);
    if (sock < 0) {
        std::cerr << "Ошибка создания сокета" << std::endl;
        close();
        return nullptr;
    }

    int flags = fcntl(sock, F_GETFL, 0);
    fcntl(sock, F_SETFL, flags | O_NONBLOCK);

    sockaddr_in addr;
    addr.sin_family = AF_INET;
    addr.sin_port = htons(22);
    inet_pton(AF_INET, host_.c_str(), &addr.sin_addr);

    // Использование std::connect вместо конфликтующей функции
    if (::connect(sock, (struct sockaddr *)&addr, sizeof(addr)) < 0) {
        if (errno != EINPROGRESS) {
            std::cerr << "Ошибка подключения к хосту " << host_ << ": " << strerror(errno) << std::endl;
            close();
            return nullptr;
        }
    }

    fd_set fdset;
    struct timeval tv;
    FD_ZERO(&fdset);
    FD_SET(sock, &fdset);
    tv.tv_sec = timeoutSec;
    tv.tv_usec = 0;

    int result = select(sock + 1, nullptr, &fdset, nullptr, &tv);
    if (result <= 0) {
        if (result == 0) {
            std::cerr << "Тайм-аут подключения к хосту " << host_ << std::endl;
        } else {
            std::cerr << "Ошибка при использовании select: " << strerror(errno) << std::endl;
        }
        close();
        return nullptr;
    }

    fcntl(sock, F_SETFL, flags);

    ssh_options_set(my_ssh_session, SSH_OPTIONS_PORT, &sock);

    int rc = ssh_connect(my_ssh_session);
    if (rc != SSH_OK) {
        std::cerr << "Ошибка подключения к хосту " << host_ << ": " << ssh_get_error(my_ssh_session) << std::endl;
        close();
        return nullptr;
    }

    rc = ssh_userauth_password(my_ssh_session, nullptr, password.c_str());
    if (rc != SSH_AUTH_SUCCESS) {
        std::cerr << "Ошибка аутентификации по паролю для пользователя " << user_ << " на хосте " << host_ << ": " << ssh_get_error(my_ssh_session) << std::endl;
        close();
        return nullptr;
    }

    ssh_channel channel = ssh_channel_new(my_ssh_session);
    if (channel == nullptr) {
        std::cerr << "Ошибка создания канала" << std::endl;
        close();
        return nullptr;
    }

    rc = ssh_channel_open_session(channel);
    if (rc != SSH_OK) {
        std::cerr << "Ошибка открытия сессии канала: " << ssh_get_error(channel) << std::endl;
        ssh_channel_free(channel);
        close();
        return nullptr;
    }

    return channel;
}

void SSHClient::close() {
    if (my_ssh_session != nullptr) {
        ssh_disconnect(my_ssh_session);
        ssh_free(my_ssh_session);
        my_ssh_session = nullptr;
    }
}
