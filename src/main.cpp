#include <libssh/libssh.h>
#include <iostream>
#include <cstdlib>
#include <fstream>
#include <string>
#include <vector>
#include <algorithm> // для std::reverse

ssh_session my_ssh_session;
int rc = -1;

ssh_channel connect_ssh(const std::string &password)
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

    rc = ssh_userauth_password(my_ssh_session, nullptr, password.c_str()); // текущий пароль
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
    return;
}

void recording_password(const std::string &new_password)
{
    std::string filename = "/home/ssv/Documents/list_passwords.txt";
    std::ofstream file(filename, std::ios::app);

    if (file.is_open())
    {
        file << new_password << std::endl;
        std::cerr << "Данные успешно записаны в файл: " << filename << std::endl;
        file.close();
    }
    else
    {
        std::cerr << "Не удалось открыть файл для записи: " << filename << std::endl;
    }
}

void sending_new_password(ssh_channel channel)
{
    std::string new_password;
    std::cin >> new_password;
    std::string command = "echo 'root:" + new_password + "' | chpasswd && reboot"; // Команда для установки нового пароля

    rc = ssh_channel_request_exec(channel, command.c_str());
    if (rc != SSH_OK)
    {
        std::cerr << "Error executing command: " << ssh_get_error(channel) << std::endl;
        return; // Выходим из функции, если возникла ошибка
    }

    std::cout << "Password changed successfully!" << std::endl;
    recording_password(new_password);
   
    return;
}

std::vector<std::string> read_passwords(const std::string &filename)
{
    std::ifstream password_file(filename);
    std::vector<std::string> passwords;
    std::string line;

    if (!password_file.is_open())
    {
        std::cerr << "The password file could not be opened: " << filename << std::endl;
        return passwords; // Вернуть пустой вектор, если файл не открылся
    }

    // Чтение всех паролей в вектор
    while (std::getline(password_file, line))
    {
        passwords.push_back(line);
    }

    // Перевернуть вектор, чтобы начинать с конца
    std::reverse(passwords.begin(), passwords.end());

    return passwords;
}

void autentification(std::vector<std::string> passwords)
{
    for (const auto &password : passwords)
    {
        std::cout << "Пытаемся использовать пароль: " << password << std::endl;
        ssh_channel channel = connect_ssh(password);

        if (channel)
        {
            std::cout << "Подключение успешно с паролем: " << password << std::endl;
            // Здесь можно вызвать вашу функцию отправки команды
            sending_new_password(channel);
            ssh_close(channel);
            break; // Выход из цикла после успешного подключения
        }
    }
}

int main(int argc, char *argv[])
{
    std::string filename = "/home/ssv/Documents/list_passwords.txt";
    std::vector<std::string> passwords = read_passwords(filename);
    autentification(passwords);
    return 0;
}
