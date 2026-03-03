#include <QApplication>
#include <QMainWindow>
#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QLabel>
#include <QLineEdit>
#include <QPushButton>
#include <QFileDialog>
#include <QTextEdit>
#include <QMutex>
#include <QProgressBar>
#include <QProcess> 
#include <fstream>
#include <sstream>
#include <iostream>
#include <set>
#include <atomic>
#include "SSHClient.h"
#include "ThreadPool.h"

// Глобальные переменные для синхронизации и хранения данных
QMutex fileMutex; 
QString newPassword; 

// Функция пинга с анализом вывода
bool ping_host(const std::string &address) {
    QProcess pingProcess;
    
#ifdef Q_OS_WIN
    // Windows: -n 1 (1 пакет), -w 2000 (таймаут 2 сек в миллисекундах)
    pingProcess.start("ping", QStringList() 
        << "-n" << "1" 
        << "-w" << "2000" 
        << QString::fromStdString(address));
#else
    // Unix/Linux: -c 1 (1 пакет), -W 2 (таймаут 2 сек)
    pingProcess.start("ping", QStringList() 
        << "-c" << "1" 
        << "-W" << "2" 
        << QString::fromStdString(address));
#endif
    
    // Ждем завершения (максимум 3.5 сек)
    if (!pingProcess.waitForFinished(3500)) {
        pingProcess.kill();
        return false;
    }

    int exitCode = pingProcess.exitCode();
    QByteArray output = pingProcess.readAllStandardOutput();
    QString strOutput = QString::fromUtf8(output);

#ifdef Q_OS_WIN
    // Windows: успешный ping содержит "TTL=" или "Reply from"
    if (strOutput.contains("TTL=") || strOutput.contains("Reply from")) {
        return true;
    }
#else
    // Unix: успешный ping содержит "1 received"
    if (strOutput.contains("1 received") || strOutput.contains("1 packets received")) {
        return true;
    }
#endif

    // Fallback: код возврата 0
    if (exitCode == 0) {
        return true;
    }

    return false;
}

/* bool ping_host(const std::string &host)
{
    std::string command = "ping -c 1 " + host + " > /dev/null 2>&1"; // Для Unix-систем
    // Для Windows используйте: std::string command = "ping -n 1 " + host;

    return (system(command.c_str()) == 0);
} */

// Функция записи результатов в файл
void recording(const std::string &path_file, const std::string &name, const std::string &reason) {
    QMutexLocker locker(&fileMutex);
    
    std::set<std::string> existingNames;
    std::ifstream infile(path_file);
    std::string existingName;
    std::string line = name + " " + reason;

    if (infile.is_open()) {
        while (std::getline(infile, existingName)) {
            existingNames.insert(existingName);
        }
        infile.close();
    }

    if (existingNames.find(line) == existingNames.end()) {
        std::ofstream file(path_file, std::ios::app);
        if (file.is_open()) {
            file << name << " " << reason << std::endl;
        } else {
            std::cerr << "Не удалось открыть файл для записи: " << path_file << std::endl;
        }
    }
}

// Вспомогательная функция смены пароля и перезагрузки
bool sending_new_password(SSHClient &client) {
    // Команда chpasswd меняет пароль, && reboot выполняет перезагрузку после успеха
    std::string command = "echo 'root:" + newPassword.toStdString() + "' | chpasswd && reboot";
    int rc = client.executeCommand(command);
    return (rc == SSH_OK);
}

class MainWindow : public QMainWindow {
    Q_OBJECT

public:
    MainWindow(QWidget *parent = nullptr) : QMainWindow(parent) {
        QWidget *centralWidget = new QWidget(this);
        QVBoxLayout *mainLayout = new QVBoxLayout(centralWidget);

        // 1. Список IP
        QHBoxLayout *ipLayout = new QHBoxLayout();
        ipLayout->addWidget(new QLabel("Список IP:"));
        PathListAddresses = new QLineEdit();
        ipLayout->addWidget(PathListAddresses);
        QPushButton *btnBrowseIP = new QPushButton("Обзор...");
        connect(btnBrowseIP, &QPushButton::clicked, this, &MainWindow::onBrowseAddresses);
        ipLayout->addWidget(btnBrowseIP);
        mainLayout->addLayout(ipLayout);

        // 2. Список паролей
        QHBoxLayout *passLayout = new QHBoxLayout();
        passLayout->addWidget(new QLabel("Список паролей:"));
        PathListPassword = new QLineEdit();
        passLayout->addWidget(PathListPassword);
        QPushButton *btnBrowsePass = new QPushButton("Обзор...");
        connect(btnBrowsePass, &QPushButton::clicked, this, &MainWindow::onBrowsePasswords);
        passLayout->addWidget(btnBrowsePass);
        mainLayout->addLayout(passLayout);

        // 3. Файл отчета
        QHBoxLayout *failLayout = new QHBoxLayout();
        failLayout->addWidget(new QLabel("Файл отчета:"));
        ListAddressesUnchangedPasswords = new QLineEdit();
        failLayout->addWidget(ListAddressesUnchangedPasswords);
        QPushButton *btnBrowseFail = new QPushButton("Обзор...");
        connect(btnBrowseFail, &QPushButton::clicked, this, &MainWindow::onBrowseUP);
        failLayout->addWidget(btnBrowseFail);
        mainLayout->addLayout(failLayout);

        // 4. Новый пароль
        QHBoxLayout *newPassLayout = new QHBoxLayout();
        newPassLayout->addWidget(new QLabel("Новый пароль:"));
        inputNewPassword = new QLineEdit();
        inputNewPassword->setEchoMode(QLineEdit::Normal);
        inputNewPassword->setText(""); // Установлен текст по умолчанию
        newPassLayout->addWidget(inputNewPassword);
        mainLayout->addLayout(newPassLayout);

        // Прогресс
        progres = new QLabel("Готов к работе");
        mainLayout->addWidget(progres);

        // Кнопка запуска
        btnStart = new QPushButton("Начать");
        connect(btnStart, &QPushButton::clicked, this, &MainWindow::onStart);
        mainLayout->addWidget(btnStart);

        // Лог
        logArea = new QTextEdit();
        logArea->setReadOnly(true);
        mainLayout->addWidget(logArea);

        setCentralWidget(centralWidget);
        setWindowTitle("Изменение пароля через SSH");
        resize(500, 450);

        // Инициализация пула потоков (4 потока)
        pool = new ThreadPool(4);
    }

    ~MainWindow() {
        delete pool;
    }

private slots:
    void onBrowseAddresses() {
        PathAddresses = QFileDialog::getOpenFileName(this, "Выбор списка адресов", "", "Text Files (*.txt)");
        if (!PathAddresses.isEmpty()) PathListAddresses->setText(PathAddresses);
    }

    void onBrowsePasswords() {
        PathPasswords = QFileDialog::getOpenFileName(this, "Выбор списка паролей", "", "Text Files (*.txt)");
        if (!PathPasswords.isEmpty()) PathListPassword->setText(PathPasswords);
    }

    void onBrowseUP() {
        PathAddressesUnchangedPasswords = QFileDialog::getSaveFileName(this, "", "", "Text Files (*.txt)");
        if (!PathAddressesUnchangedPasswords.isEmpty()) ListAddressesUnchangedPasswords->setText(PathAddressesUnchangedPasswords);
    }

    void onStart() {
        if (PathListAddresses->text().isEmpty() || PathListPassword->text().isEmpty() || 
            ListAddressesUnchangedPasswords->text().isEmpty() || inputNewPassword->text().isEmpty()) {
            logArea->append("Ошибка: Заполните все поля!");
            return;
        }

        newPassword = inputNewPassword->text();
        btnStart->setEnabled(false);
        
        // Чтение файлов
        std::vector<std::string> ips;
        std::vector<std::string> passwords;
        
        std::ifstream ifsIP(PathListAddresses->text().toStdString());
        std::string line;
        while (std::getline(ifsIP, line)) if (!line.empty()) ips.push_back(line);
        
        std::ifstream ifsPass(PathListPassword->text().toStdString());
        while (std::getline(ifsPass, line)) if (!line.empty()) passwords.push_back(line);

        // Разворачиваем список паролей, чтобы перебор шел с конца
        std::reverse(passwords.begin(), passwords.end());

        totalTasks = ips.size();
        completedTasks = 0;

        if (totalTasks == 0) {
            logArea->append("Список IP пуст.");
            btnStart->setEnabled(true);
            return;
        }

        // Запуск задач
        for (const auto& ip : ips) {
            pool->enqueue([this, ip, passwords]() {
                processIP(ip, passwords);
            });
        }
    }
    
    private:
    QLineEdit *PathListAddresses;
    QLineEdit *PathListPassword;
    QLineEdit *ListAddressesUnchangedPasswords;
    QLineEdit *inputNewPassword;
    QPushButton *btnStart;
    QTextEdit *logArea;
    QLabel *progres;
    ThreadPool *pool;

    QString PathAddresses;
    QString PathPasswords;
    QString PathAddressesUnchangedPasswords;

    std::atomic<uint> completedTasks{0};
    uint totalTasks = 0;

    void updateProgres(uint completed, uint total) {
        QMetaObject::invokeMethod(this, [this, completed, total]() {
            QString text = QString("Выполнено: %1%").arg(static_cast<int>((static_cast<double>(completed) / total) * 100));
            progres->setText(text);
        });
    }

    void log(const QString &msg) {
        QMetaObject::invokeMethod(logArea, "append", Qt::QueuedConnection, Q_ARG(QString, msg));
    }

    void processIP(const std::string &ip, const std::vector<std::string> &passwords) {
        // 1. Проверка пинга (обновленная функция с анализом вывода)
        if (!ping_host(ip)) {
            log(QString("Нет связи: %1 (хост недоступен)").arg(QString::fromStdString(ip)));
            recording(PathAddressesUnchangedPasswords.toStdString(), ip, "нет связи");
            
            completedTasks++;
            updateProgres(completedTasks, totalTasks);
            checkFinish();
            return;
        }

        bool connected = false;
        bool passwordChanged = false;

        // 2. Перебор паролей — новое подключение для каждой попытки
        for (const auto& pass : passwords) {
            SSHClient client(ip, "root");  // Создаём новое подключение для каждого пароля
            
            if (client.connect(pass)) {
                log(QString("Подключено к %1. Пароль: %2").arg(QString::fromStdString(ip), QString::fromStdString(pass)));
                connected = true;

                // 3. Смена пароля и перезагрузка
                if (sending_new_password(client)) {
                    log(QString("Успех: пароль изменен, перезагрузка на %1").arg(QString::fromStdString(ip)));
                    passwordChanged = true;
                } else {
                    log(QString("Ошибка: не удалось сменить пароль на %1").arg(QString::fromStdString(ip)));
                    recording(PathAddressesUnchangedPasswords.toStdString(), ip, "ChangePassFailed");
                }
                
                client.disconnect();
                break; // Прерываем цикл паролей при успешном подключении
            }
            // При неудаче — client уничтожается здесь, деструктор корректно закроет сессию
        }

        // 4. Если подключение не удалось ни с одним паролем
        if (!connected) {
            log(QString("Не подошли пароли для %1").arg(QString::fromStdString(ip)));
            recording(PathAddressesUnchangedPasswords.toStdString(), ip, "нет пароля");
        }

        completedTasks++;
        updateProgres(completedTasks, totalTasks);
        checkFinish();
    }

    void checkFinish() {
        if (completedTasks == totalTasks) {
             QMetaObject::invokeMethod(this, [this]() {
                 btnStart->setEnabled(true);
                 log("Завершено.");

                 // Добавление нового пароля в конец списка
                 QString passFilePath = PathListPassword->text();
                 if (!passFilePath.isEmpty() && !newPassword.isEmpty()) {
                     std::ofstream file(passFilePath.toStdString(), std::ios::app);
                     if (file.is_open()) {
                         file << newPassword.toStdString() << std::endl;
                         log("Новый пароль добавлен в список паролей.");
                     } else {
                         log("Ошибка: Не удалось открыть файл списка паролей для записи.");
                     }
                 }
             });
        }
    }
};

int main(int argc, char *argv[]) {
    QApplication app(argc, argv);
    MainWindow window;
    window.show();
    return app.exec();
}

#include "main.moc"