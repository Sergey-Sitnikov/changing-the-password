#include <iostream>
#include <fstream>
#include <vector>
#include <string>
#include <algorithm> // для std::reverse
#include <cstdlib>   // для функции system
#include "SSHClient.h"

std::string host = "178.176.205.166"; // Замените на нужный хост

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

bool ping_host(const std::string &host)
{
    std::string command = "ping -c 1 " + host + " > /dev/null 2>&1"; // Для Unix-систем
    // Для Windows используйте: std::string command = "ping -n 1 " + host;

    return (system(command.c_str()) == 0);
}

void sending_new_password(ssh_channel channel)
{
    std::string new_password;
    std::cout << "Введите новый пароль: ";
    std::cin >> new_password;
    std::string command = "echo 'root:" + new_password + "' | chpasswd && reboot"; // Команда для установки нового пароля

    int rc = ssh_channel_request_exec(channel, command.c_str());
    if (rc != SSH_OK)
    {
        std::cerr << "Ошибка при выполнении команды: " << ssh_get_error(channel) << std::endl;
        return; // Выходим из функции, если возникла ошибка
    }

    std::cout << "Пароль изменен!" << std::endl;
    recording_password(new_password);
}

std::vector<std::string> read_passwords(const std::string &filename)
{
    std::ifstream password_file(filename);
    std::vector<std::string> passwords;
    std::string line;

    if (!password_file.is_open())
    {
        std::cerr << "Файл список паролей не открыт: " << filename << std::endl;
        return passwords; // Вернуть пустой вектор, если файл не открылся
    }

    while (std::getline(password_file, line))
    {
        passwords.push_back(line);
    }

    std::reverse(passwords.begin(), passwords.end());

    return passwords;
}

void autentification(const std::vector<std::string> &passwords)
{

    for (const auto &password : passwords)
    {
        std::cout << "Пытаемся использовать пароль: " << password << std::endl;
        SSHClient sshClient(host, "root");
        ssh_channel channel = sshClient.connect(password);

        if (channel)
        {
            std::cout << "Подключение успешно с паролем: " << password << std::endl;
            sending_new_password(channel);
            sshClient.close();
            break; // Выход из цикла после успешного подключения
        }
    }
}

int main(int argc, char *argv[])
{
    std::string filename = "/home/ssv/Documents/list_passwords.txt";
    std::vector<std::string> passwords = read_passwords(filename);
    if (!ping_host(host))
    {
        std::cerr << "Хост " << host << " недоступен. Проверьте соединение!" << std::endl;
        // return; // Выход из функции, если хост недоступен
    }
    else
        autentification(passwords);
    return 0;
}
