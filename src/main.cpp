#include <QApplication>
#include <QWidget>
#include <QPushButton>
#include <QVBoxLayout>
#include <QLineEdit>
#include <QString>
#include <QLabel>
#include <QMessageBox>
#include <QFileDialog> // Подключаем QFileDialog
#include <iostream>
#include <fstream>
#include <vector>
#include <string>
#include <algorithm> // для std::reverse
#include <cstdlib>   // для функции system
#include "SSHClient.h"
 #include <thread>
#include <future>
#include <iostream>
#include <vector>
#include <fstream>
#include <algorithm>

//std::string host = "178.176.205.166";

class MainWindow : public QWidget
{
    Q_OBJECT // Добавьте этот макрос

        public : MainWindow(QWidget *parent = nullptr)
        : QWidget(parent)
    {
        QVBoxLayout *layout = new QVBoxLayout;

        passwordInput = new QLineEdit(this);
        QPushButton *changeButton = new QPushButton("Изменить пароль", this);
        layout->addWidget(new QLabel("Введите новый пароль:", this));
        layout->addWidget(passwordInput);

        PathListPassword = new QLineEdit(this); // Поле для ввода пути к файлу
        PathListPassword->setPlaceholderText("Укажите путь к списку паролей");
        QPushButton *browsePasswordButton = new QPushButton("Обзор...", this); // Кнопка для выбора файла
        layout->addWidget(PathListPassword, 1);
        layout->addWidget(browsePasswordButton, 0, Qt::AlignRight);

        PathListAddresses = new QLineEdit(this); // Поле для ввода пути к файлу
        PathListAddresses->setPlaceholderText("Укажите путь к списку адресов");
        QPushButton *browseAddressesButton = new QPushButton("Обзор...", this); // Кнопка для выбора файла
        layout->addWidget(PathListAddresses, 1);
        layout->addWidget(browseAddressesButton, 0, Qt::AlignRight);

        ListAddressesUnchangedPasswords = new QLineEdit(this); // Поле для ввода пути к файлу
        ListAddressesUnchangedPasswords->setPlaceholderText("Укажите путь для сохранения списа адресов с неизмененными паролями");
        QPushButton *browseUPButton = new QPushButton("Обзор...", this); // Кнопка для выбора файла
        layout->addWidget(ListAddressesUnchangedPasswords, 1);
        layout->addWidget(browseUPButton, 0, Qt::AlignRight);

        layout->addWidget(changeButton);
        setLayout(layout);

        connect(changeButton, &QPushButton::clicked, this, &MainWindow::handleChangePassword);
        connect(browsePasswordButton, &QPushButton::clicked, this, &MainWindow::onBrowsePasswords); // Подключаем кнопку
        connect(browseAddressesButton, &QPushButton::clicked, this, &MainWindow::onBrowseAddresses);
        connect(browseUPButton, &QPushButton::clicked, this, &MainWindow::onBrowseUP);
    }

private slots:

bool ping_host(const std::string &host)
{
    std::string command = "ping -c 1 " + host + " > /dev/null 2>&1"; // Для Unix-систем
    // Для Windows используйте: std::string command = "ping -n 1 " + host;

    return (system(command.c_str()) == 0);
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
    // std::string new_password;
    // std::cout << "Введите новый пароль: ";
    // std::cin >> new_password;
    std::string command = "echo 'root:" + newPassword.toStdString() + "' | chpasswd && reboot"; // Команда для установки нового пароля

    int rc = ssh_channel_request_exec(channel, command.c_str());
    if (rc != SSH_OK)
    {
        std::cerr << "Ошибка при выполнении команды: " << ssh_get_error(channel) << std::endl;
        return; // Выходим из функции, если возникла ошибка
    }

    std::cout << "Пароль изменен!" << std::endl;
    recording_password(newPassword.toStdString());
}

void handleChangePassword() {
    newPassword = passwordInput->text();
    if (!newPassword.isEmpty()) {
        // Получаем список паролей
        std::ifstream password_file(PathPasswords.toStdString());
        std::string line;

        if (!password_file.is_open()) {
            std::cerr << "список паролей не открыт: " << std::endl;
            return; // Выход из функции при ошибке открытия файла
        }

        while (std::getline(password_file, line)) {
            passwords.push_back(line);
        }

        std::reverse(passwords.begin(), passwords.end());

        // Получаем список адресов
        std::ifstream addresses_file(PathAddresses.toStdString());
        std::string a;

        if (!addresses_file.is_open()) {
            std::cerr << "список адресов не открыт: " << std::endl;
            return; // Выход из функции при ошибке открытия файла
        }

        while (std::getline(addresses_file, a)) {
            adresses.push_back(a);
        }

        // Вектор для хранения потоков
        std::vector<std::thread> threads;

        for (const auto &address : adresses) {
            threads.emplace_back([this, address]() {
                // Пинговаем адрес
                if (ping_host(address)) { // Функция ping_host должна быть доступна
                    // Если пинг успешен, пробуем подключиться
                    for (const auto &password : passwords) {
                        SSHClient sshClient(address, "root"); // Используем имя пользователя
                        ssh_channel channel = sshClient.connect(password);

                        if (channel) {
                            std::cout << "Подключение к " << address << " успешно с паролем: " << password << std::endl;
                            // Здесь можно вызвать функцию для изменения пароля
                            sending_new_password(channel);
                            sshClient.close(); // Закрываем подключение
                            break; // Прерываем цикл после успешного подключения
                        }
                    }
                } else {
                    std::cerr << "Не удалось выполнить пинг для адреса: " << address << std::endl;
                }
            });
        }

        // Ожидаем завершения всех потоков
        for (auto &t : threads) {
            if (t.joinable()) {
                t.join(); // Зачем мы ждем завершения потока
            }
        }
    }
}

    void onBrowsePasswords()
    {
        // Открываем файловый диалог для выбора файла
        PathPasswords = QFileDialog::getOpenFileName(this, "Выбор списка паролей", "", "Text Files (*.txt);;All Files (*)");
        if (!PathPasswords.isEmpty())
        {
            PathListPassword->setText(PathPasswords); // Устанавливаем выбранный путь к файлу в QLineEdit
        }
    }

    void onBrowseAddresses()
    {
        // Открываем файловый диалог для выбора файла
        PathAddresses = QFileDialog::getOpenFileName(this, "Выбор списка адресов", "", "Text Files (*.txt);;All Files (*)");
        if (!PathAddresses.isEmpty())
        {
            PathListAddresses->setText(PathAddresses); // Устанавливаем выбранный путь к файлу в QLineEdit
        }
    }

    void onBrowseUP()
    {
        // Открываем файловый диалог для выбора файла
        PathAddressesUnchangedPasswords = QFileDialog::getSaveFileName(this, "", "", "Text Files (*.txt);;All Files (*)");
        if (!PathAddressesUnchangedPasswords.isEmpty())
        {
            ListAddressesUnchangedPasswords->setText(PathAddressesUnchangedPasswords); // Устанавливаем выбранный путь к файлу в QLineEdit
        }
    }

private:
    QLineEdit *passwordInput;
    QLabel *statusLabel;
    QLineEdit *PathListPassword;
    QLineEdit *PathListAddresses;
    QLineEdit *ListAddressesUnchangedPasswords;
    QString PathPasswords;
    QString PathAddresses;
    QString PathAddressesUnchangedPasswords;
    QString newPassword;
    QString ListAddresses;
    QString ListOasswords;
    std::vector<std::string> passwords;
    std::vector<std::string> adresses;
};







// std::vector<std::string> read_passwords(const std::string &filename)
// {
//     std::ifstream password_file(filename);
//     std::vector<std::string> passwords;
//     std::string line;

//     if (!password_file.is_open())
//     {
//         std::cerr << "Файл список паролей не открыт: " << filename << std::endl;
//         return passwords; // Вернуть пустой вектор, если файл не открылся
//     }

//     while (std::getline(password_file, line))
//     {
//         passwords.push_back(line);
//     }

//     std::reverse(passwords.begin(), passwords.end());

//     return passwords;
// }

// void autentification(const std::vector<std::string> &passwords)
// {

//     for (const auto &password : passwords)
//     {
//         std::cout << "Пытаемся использовать пароль: " << password << std::endl;
//         SSHClient sshClient(host, "root");
//         ssh_channel channel = sshClient.connect(password);

//         if (channel)
//         {
//             std::cout << "Подключение успешно с паролем: " << password << std::endl;
//             sending_new_password(channel);
//             sshClient.close();
//             break; // Выход из цикла после успешного подключения
//         }
//     }
// }

int main(int argc, char *argv[])
{
    QApplication app(argc, argv);

    MainWindow window;
    window.resize(600, 200);
    window.setWindowTitle("SSH Password Changer");
    window.show();

    return app.exec();
}
#include "main.moc"