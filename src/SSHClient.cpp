#include "SSHClient.h"
#include <iostream>

SSHClient::SSHClient(const std::string &host, const std::string &user)
    : host_(host), user_(user), my_ssh_session(nullptr) {}

SSHClient::~SSHClient() {
    close();
}

ssh_channel SSHClient::connect(const std::string &password) {
    my_ssh_session = ssh_new();
    if (my_ssh_session == nullptr) {
        std::cerr << "Ошибка создания SSH сессии" << std::endl;
        return nullptr;
    }

    ssh_options_set(my_ssh_session, SSH_OPTIONS_HOST, host_.c_str());
    ssh_options_set(my_ssh_session, SSH_OPTIONS_USER, user_.c_str());

    int rc = ssh_connect(my_ssh_session);
    if (rc != SSH_OK) {
        std::cerr << "Ошибка подключения к хосту: " << ssh_get_error(my_ssh_session) << std::endl;
        close();
        return nullptr;
    }

    rc = ssh_userauth_password(my_ssh_session, nullptr, password.c_str());
    if (rc != SSH_AUTH_SUCCESS) {
        std::cerr << "Ошибка аутентификации по паролю: " << ssh_get_error(my_ssh_session) << std::endl;
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
        std::cerr << "Ошибка открытия сессии канала " << ssh_get_error(channel) << std::endl;
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
