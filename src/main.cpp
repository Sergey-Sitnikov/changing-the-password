#include <QApplication>
#include <QWidget>
#include <QPushButton>
#include <QVBoxLayout>
#include <QLineEdit>
#include <QString>
#include <QLabel>
#include <QMessageBox>
#include <QFileDialog>
#include <iostream>
#include <fstream>
#include <vector>
#include <string>
#include <algorithm>
#include <cstdlib>
#include <set>
// #include <thread>
#include <QMutex>
// #include <future>
#include "SSHClient.h"
#include "ThreadPool.h"

class MainWindow : public QWidget
{
    Q_OBJECT

public:
    MainWindow(QWidget *parent = nullptr)
        : QWidget(parent),
          progres(new QLabel("Обработано: 0%"))
    {
        QVBoxLayout *layout = new QVBoxLayout;

        passwordInput = new QLineEdit(this);
        QPushButton *changeButton = new QPushButton("Изменить пароль", this);
        layout->addWidget(new QLabel("Введите новый пароль:", this));
        layout->addWidget(passwordInput);

        PathListPassword = new QLineEdit(this);
        PathListPassword->setPlaceholderText("Укажите путь к списку паролей");
        QPushButton *browsePasswordButton = new QPushButton("Обзор...", this);
        layout->addWidget(PathListPassword);
        layout->addWidget(browsePasswordButton);

        PathListAddresses = new QLineEdit(this);
        PathListAddresses->setPlaceholderText("Укажите путь к списку адресов");
        QPushButton *browseAddressesButton = new QPushButton("Обзор...", this);
        layout->addWidget(PathListAddresses);
        layout->addWidget(browseAddressesButton);

        ListAddressesUnchangedPasswords = new QLineEdit(this);
        ListAddressesUnchangedPasswords->setPlaceholderText("Укажите путь для сохранения адресов с неизмененными паролями");
        QPushButton *browseUPButton = new QPushButton("Обзор...", this); // Кнопка для выбора файла
        layout->addWidget(ListAddressesUnchangedPasswords);
        layout->addWidget(browseUPButton);

        layout->addWidget(changeButton);
        layout->addWidget(progres);
        setLayout(layout);

        connect(changeButton, &QPushButton::clicked, this, &MainWindow::handleChangePassword);
        connect(browsePasswordButton, &QPushButton::clicked, this, &MainWindow::onBrowsePasswords);
        connect(browseAddressesButton, &QPushButton::clicked, this, &MainWindow::onBrowseAddresses);
        connect(browseUPButton, &QPushButton::clicked, this, &MainWindow::onBrowseUP);
    }

private slots:
    void handleChangePassword()
    {
        newPassword = passwordInput->text();
        if (newPassword.isEmpty())
        {
            QMessageBox::warning(this, "Ошибка", "Пароль не может быть пустым.");
            return;
        }

        // Считываем список паролей
        std::ifstream password_file(PathPasswords.toStdString());
        if (!password_file.is_open())
        {
            QMessageBox::warning(this, "Ошибка", "Не удалось открыть файл со списком паролей.");
            return;
        }

        std::string line;
        while (std::getline(password_file, line))
        {
            passwords.push_back(line);
        }
        std::reverse(passwords.begin(), passwords.end());

        // Считываем список адресов
        std::ifstream addresses_file(PathAddresses.toStdString());
        if (!addresses_file.is_open())
        {
            QMessageBox::warning(this, "Ошибка", "Не удалось открыть файл со списком адресов.");
            return;
        }

        std::string address;
        while (std::getline(addresses_file, address))
        {
            adresses.push_back(address);
        }
        std::cout << "adresses.size() " << adresses.size() << std::endl;
        // Прежнее удаление futures
        // Сохранение в futures может быть не нужно в данном контексте
        for (size_t i = 0; i < adresses.size(); ++i)
        {
            updateProgres(i + 1, adresses.size());
            if (ping_host(adresses[i]))
            {
                pool.enqueue([this, address = adresses[i]]()
                             {
                    for (const auto &password : passwords) {
                        SSHClient sshClient(address, "root");
                        ssh_channel channel = sshClient.connectSSH(password, 15);

                        if (channel) {
                            std::cout << "Подключение к " << address << " успешно с паролем: " << password << std::endl;
                            sending_new_password(channel);
                            sshClient.close();
                            break;
                        } else {
                            std::cerr << "Не удалось подключиться к " << address << " с паролем: " << password << std::endl;
                            recording(PathAddressesUnchangedPasswords.toStdString(), address, "неподходит пароль");
                        }
                    } });
            }
            else
            {
                recording(PathAddressesUnchangedPasswords.toStdString(), adresses[i], "нет связи");
                std::cerr << "Не удалось выполнить пинг для адреса: " << adresses[i] << std::endl;
            }
        }
        recording(PathAddressesUnchangedPasswords.toStdString(), address, "список пройден");
        recording(PathPasswords.toStdString(), newPassword.toStdString(), "");
        progres->setText("Готово");
    }

    void onBrowsePasswords()
    {
        PathPasswords = QFileDialog::getOpenFileName(this, "Выбор списка паролей", "", "Text Files (*.txt);;All Files (*)");
        if (!PathPasswords.isEmpty())
        {
            PathListPassword->setText(PathPasswords);
        }
    }

    void onBrowseAddresses()
    {
        PathAddresses = QFileDialog::getOpenFileName(this, "Выбор списка адресов", "", "Text Files (*.txt);;All Files (*)");
        if (!PathAddresses.isEmpty())
        {
            PathListAddresses->setText(PathAddresses);
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

    void updateProgres(uint completed, uint total)
    {
        QString text = QString("Выполнено: %1%\%").arg(static_cast<int>((static_cast<double>(completed) / total) * 100));
        progres->setText(text);
    }

private:
    ThreadPool pool{99};
    QMutex fileMutex; // Мьютекс для защиты записи в файл

    void recording(const std::string &path_file, const std::string &name, const std::string &reason)
    {
        QMutexLocker locker(&fileMutex); // Заблокировать мьютекс

        std::set<std::string> existingNames;
        std::ifstream infile(path_file);
        std::string existingName;
        std::string line = name + " " + reason;

        // Чтение существующих данных из файла
        while (std::getline(infile, existingName))
        {
            existingNames.insert(existingName);
        }
        infile.close();

        // Проверка на существование имени
        if (existingNames.find(line) == existingNames.end())
        {
            std::ofstream file(path_file, std::ios::app);
            if (file.is_open())
            {
                file << name << " " << reason << std::endl;
            }
            else
            {
                std::cerr << "Не удалось открыть файл для записи: " << path_file << std::endl;
            }
        }
    }

    bool ping_host(const std::string &address)
    {
        // Реализация пинга адреса
        // Эта реализация просто возвращает true для примера
        // Например, вы можете использовать систему для выполнения системного вызова
        std::string command = "ping -c 1 " + address + " > /dev/null 2>&1"; // Для Unix-систем
        return (system(command.c_str()) == 0);
    }

    void sending_new_password(ssh_channel channel)
    {
        // Реализуйте логику отправки нового пароля по SSH
        // Например:
        std::string command = "echo 'root:" + newPassword.toStdString() + "' | chpasswd && reboot";
        int rc = ssh_channel_request_exec(channel, command.c_str());
        if (rc != SSH_OK)
        {
            std::cerr << "Ошибка при выполнении команды: " << ssh_get_error(channel) << std::endl;
        }
        else
        {
            std::cout << "Пароль изменен!" << std::endl;
        }
    }

    // Члены вашего класса
    QLineEdit *passwordInput;
    QLineEdit *PathListPassword;
    QLineEdit *PathListAddresses;
    QLineEdit *ListAddressesUnchangedPasswords;
    QLabel *progres;

    std::vector<std::string> passwords;
    std::vector<std::string> adresses;
    QString newPassword;
    QString PathPasswords;
    QString PathAddresses;
    QString PathAddressesUnchangedPasswords;
};

int main(int argc, char *argv[])
{
    QApplication app(argc, argv);
    MainWindow window;
    window.setWindowTitle("Изменение пароля через SSH");
    window.resize(400, 300);
    window.show();
    return app.exec();
}

#include "main.moc"
