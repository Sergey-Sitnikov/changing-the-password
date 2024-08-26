#include <libssh/libssh.h>
#include <iostream>
#include <cstdlib>
#include <fstream>
#include <string>

int rc = -1;
ssh_session my_ssh_session;

ssh_channel connect_ssh()
{
    my_ssh_session = ssh_new();
    if (my_ssh_session == nullptr)
    {
        std::cerr << "Error creating SSH session" << std::endl;
        return nullptr;
    }

    ssh_options_set(my_ssh_session, SSH_OPTIONS_HOST, "178.176.205.166");
    ssh_options_set(my_ssh_session, SSH_OPTIONS_USER, "root");

    rc = ssh_connect(my_ssh_session);
    if (rc != SSH_OK)
    {
        std::cerr << "Error connecting to the host: " << ssh_get_error(my_ssh_session) << std::endl;
        ssh_free(my_ssh_session);
        return nullptr;
    }

    rc = ssh_userauth_password(my_ssh_session, nullptr, "uuuu"); // текущий пароль
    if (rc != SSH_AUTH_SUCCESS)
    {
        std::cerr << "Error authenticating with password: " << ssh_get_error(my_ssh_session) << std::endl;
        ssh_disconnect(my_ssh_session);
        ssh_free(my_ssh_session);
        return nullptr;
    }

    ssh_channel channel = ssh_channel_new(my_ssh_session);
    if (channel == nullptr)
    {
        std::cerr << "Error creating channel" << std::endl;
        ssh_disconnect(my_ssh_session);
        ssh_free(my_ssh_session);
        return nullptr;
    }

    rc = ssh_channel_open_session(channel);
    if (rc != SSH_OK)
    {
        std::cerr << "Error opening session channel: " << ssh_get_error(channel) << std::endl;
        ssh_channel_free(channel);
        ssh_disconnect(my_ssh_session);
        ssh_free(my_ssh_session);
        return nullptr;
    }
    
    return channel;
}

void ssh_close(ssh_channel channel)
{
    if (channel != nullptr)
    {
        ssh_channel_close(channel);
        ssh_channel_free(channel);
    }
    if (my_ssh_session != nullptr)
    {
        ssh_disconnect(my_ssh_session);
        ssh_free(my_ssh_session);
    }
}

void recording_password(const std::string& new_password)
{
    std::string filename = "/home/ssv/Documents/list_passwords.txt";
    std::ofstream file(filename, std::ios::app);

    if (file.is_open())
    {
        file << new_password << std::endl;
        std::cerr << "Данные успешно записаны в файл: " << filename << std::endl;
    }
    else
    {
        std::cerr << "Не удалось открыть файл для записи: " << filename << std::endl;
    }
}

void sending_command(ssh_channel channel, const std::string& new_password)
{
    std::string command = "echo 'root:" + new_password + "' | chpasswd"; // Команда для установки нового пароля

    rc = ssh_channel_request_exec(channel, command.c_str());
    if (rc != SSH_OK)
    {
        std::cerr << "Error executing command: " << ssh_get_error(channel) << std::endl;
        return; // Выходим из функции, если возникла ошибка
    }
    
    std::cout << "Password changed successfully!" << std::endl;
    recording_password(new_password);

    // Перезагрузка системы
    std::string reboot_command = "reboot"; // Убедитесь, что команда sudo не требует ввода пароля
    rc = ssh_channel_request_exec(channel, reboot_command.c_str());
    if (rc != SSH_OK)
    {
        std::cerr << "Error executing reboot command: " << ssh_get_error(channel) << std::endl;
    }
}

int main(int argc, char *argv[])
{

    std::string new_password;
    std::cin >> new_password;

    ssh_channel channel = connect_ssh();

    sending_command(channel, new_password);

    //   ssh_close(channel);

    return 0;
}