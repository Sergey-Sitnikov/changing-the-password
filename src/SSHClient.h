#ifndef SSHCLIENT_H
#define SSHCLIENT_H

#include <libssh/libssh.h>
#include <string>

class SSHClient {
public:
    SSHClient(const std::string &host, const std::string &user);
    ~SSHClient();
    ssh_channel connectSSH(const std::string &password, int timeoutSec); // Переименован метод
    void close();

private:
    std::string host_;
    std::string user_;
    ssh_session my_ssh_session;
};

#endif // SSHCLIENT_H
