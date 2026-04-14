#include <algorithm>
#include <cctype>

#ifdef QT_VERSION_CHECK
#include <QStringConverter>
#endif
    
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
#include <QMutexLocker>
#include <QProgressBar>
#include <QProcess> 
#include <fstream>
#include <sstream>
#include <iostream>
#include <set>
#include <atomic>
#include <QRegularExpression>
#include "SSHClient.h"
#include "ThreadPool.h"
#include <thread>
#include <mutex>
#include <algorithm>

#ifdef USE_QXLSX
#include "xlsxdocument.h"
#endif
    
// Глобальные переменные для синхронизации и хранения данных
QMutex fileMutex; 
QString newPassword; 

// Global normalizeLine function
std::string normalizeLine(const std::string& line) {
    std::string result = line;
    result.erase(std::remove(result.begin(), result.end(), '\r'), result.end());
    
    size_t start = result.find_first_not_of(" \t");
    if (start == std::string::npos) {
        return "";
    }
    
    size_t end = result.find_last_not_of(" \t");
    return result.substr(start, end - start + 1);
}



void recording(const std::string &path_file, const std::string &ip, const std::string &reason) {
    QMutexLocker locker(&fileMutex);
    
 
    std::string line = ip + " - " + reason;
    
    std::ofstream outfile(path_file, std::ios::app);
    if (outfile.is_open()) {
        outfile << line << std::endl;
        outfile.close();
    } else {
        // Пробуем создать файл заново
        std::ofstream newfile(path_file, std::ios::out);
        if (newfile.is_open()) {
            newfile << line << std::endl;
            newfile.close();
        } else {
            std::cerr << "Не удалось открыть/создать файл для записи: " << path_file << std::endl;
        }
    }
}

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

// Вспомогательная функция смены пароля и перезагрузки
bool sending_new_password(SSHClient &client) {
    // Команда chpasswd меняет пароль, && reboot выполняет перезагрузку после успеха
    std::string command = "echo 'root:" + newPassword.toStdString() + "' | chpasswd && reboot";
    int rc = client.executeCommand(command);
    return (rc == SSH_OK);
}

// Служебные функции чтения строк из файла
std::vector<std::string> readLinesFromFile(const QString &filePath) {
    std::vector<std::string> lines;
    QFile file(filePath);
    
    if (file.open(QIODevice::ReadOnly | QIODevice::Text)) {
        QTextStream in(&file);
#if QT_VERSION >= QT_VERSION_CHECK(6, 0, 0)
        in.setEncoding(QStringConverter::Utf8);
#else
        in.setCodec("UTF-8");
#endif
        
        while (!in.atEnd()) {
            QString line = in.readLine().trimmed();
            if (!line.isEmpty()) {
                lines.push_back(line.toStdString());
            }
        }
        file.close();
    }
    
    return lines;
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

        // Ограничение до 50% ядер
        unsigned int hardwareThreads = std::thread::hardware_concurrency();
        unsigned int poolSize = std::max(1u, hardwareThreads / 2);
        pool = std::make_unique<ThreadPool>(poolSize);
     //   log(QString("Пул потоков: %1 из %2 ядер").arg(poolSize).arg(hardwareThreads));
    }

    ~MainWindow() {
        // unique_ptr handles deletion
    }

private slots:
    void onBrowseAddresses() {
        PathAddresses = QFileDialog::getOpenFileName(this, "Выбор списка адресов", "", 
            "Excel Files (*.xlsx);;Text Files (*.txt);;All Files (*.*)");
        if (!PathAddresses.isEmpty()) PathListAddresses->setText(PathAddresses);
    }

    void onBrowsePasswords() {
        PathPasswords = QFileDialog::getOpenFileName(this, "Выбор списка паролей", "", "Text Files (*.txt)");
        if (!PathPasswords.isEmpty()) PathListPassword->setText(PathPasswords);
    }

    void onBrowseUP() {
        PathAddressesUnchangedPasswords = QFileDialog::getOpenFileName(this, 
            "Выбор файла отчета", 
            "", 
            "Text Files (*.txt)");
        if (!PathAddressesUnchangedPasswords.isEmpty()) {
            ListAddressesUnchangedPasswords->setText(PathAddressesUnchangedPasswords);
        }
    }
    
    std::vector<std::string> readIPsFromExcel(const QString &filePath) {
        std::vector<std::string> ips;
#ifdef USE_QXLSX
        QXlsx::Document xlsx(filePath);
        if (!xlsx.load()) {
            return ips;
        }
        
        int attrColumn = -1;
        for (int col = 1; col <= xlsx.dimension().columnCount(); ++col) {
            QVariant header = xlsx.read(1, col);
            if (header.toString().trimmed().toLower() == "атрибуты") {
                attrColumn = col;
                break;
            }
        }

        if (attrColumn == -1) {
            return ips;
        }
        #include <algorithm>
#include <cctype>

#ifdef QT_VERSION_CHECK
#include <QStringConverter>
#endif
    
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
#include <QRegularExpression>
#include "SSHClient.h"
#include "ThreadPool.h"
#include <thread>
#include <mutex>
#include <algorithm>

#ifdef USE_QXLSX
#include "xlsxdocument.h"
#endif
    
// Глобальные переменные для синхронизации и хранения данных
}
        for (int row = 2; row <= xlsx.dimension().rowCount(); ++row) {
            QVariant cell = xlsx.read(row, attrColumn);
            QString value = cell.toString().trimmed();
            if (!value.isEmpty()) {
                QRegularExpression ipRegex("(\\d{1,3}\\.\\d{1,3}\\.\\d{1,3}\\.\\d{1,3})");
                QRegularExpressionMatch match = ipRegex.match(value);
                if (match.hasMatch()) {
                    ips.push_back(match.captured(1).toStdString());
                }
            }
        }
#endif
        return ips;
    }

    std::vector<std::string> readFailedAddresses(const QString &filePath) {
        std::vector<std::string> addresses;
        std::ifstream infile(filePath.toStdString());
        std::string line;
        
        while (std::getline(infile, line)) {
            // Нормализуем строку перед обработкой
            std::string normalized = normalizeLine(line);
            
            QRegularExpression ipRegex("(\\d{1,3}\\.\\d{1,3}\\.\\d{1,3}\\.\\d{1,3})");
            QRegularExpressionMatch match = ipRegex.match(QString::fromStdString(normalized));
            if (match.hasMatch()) {
                addresses.push_back(match.captured(1).toStdString());
            }
        }

        return addresses;
    }

    void removeAddressFromJournal(const QString &filePath, const std::string &ipToRemove) {
        QMutexLocker locker(&fileMutex);
        
        std::vector<std::string> lines;
        std::ifstream infile(filePath.toStdString());
        std::string line;
        
        while (std::getline(infile, line)) {
            if (line.find(ipToRemove) == std::string::npos) {
                lines.push_back(line);
            }
        }
        infile.close();
        
        std::ofstream outfile(filePath.toStdString());
        for (const auto &l : lines) {
            outfile << l << std::endl;
        }
    }

    void retryFailedAddresses(int passNumber) {
        QString journalPath = ListAddressesUnchangedPasswords->text();
        if (journalPath.isEmpty()) {
            QMetaObject::invokeMethod(this, [this, passNumber]() {
                log(QString("Проход %1 по журналу: путь к файлу отчета не указан").arg(passNumber));
            });
            return;
        }

        QFileInfo fileInfo(journalPath);
        if (!fileInfo.exists()) {
            QMetaObject::invokeMethod(this, [this, passNumber]() {
                log(QString("Проход %1 по журналу: файл отчета не существует").arg(passNumber));
            });
            return;
        }

        std::vector<std::string> failedAddresses = readFailedAddresses(journalPath);
        
        if (failedAddresses.empty()) {
            QMetaObject::invokeMethod(this, [this, passNumber]() {
                log(QString("Проход %1 по журналу: файл отчета пуст").arg(passNumber));
            });
            return;
        }

        QMetaObject::invokeMethod(this, [this, passNumber, failedAddressesSize = failedAddresses.size()]() {
            log(QString("Проход %1 по журналу: найдено %2 адресов").arg(passNumber).arg(failedAddressesSize));
        });
        
        int successCount = 0;
        std::vector<std::string> passwords;
        
        std::ifstream passFile(PathPasswords.toStdString());
        if (!passFile.is_open()) {
            QMetaObject::invokeMethod(this, [this, passNumber]() {
                log(QString("Проход %1: не удалось открыть файл паролей").arg(passNumber));
            });
            return;
        }

        std::string pwd;
        while (std::getline(passFile, pwd)) {
            passwords.push_back(pwd);
        }
        passFile.close();

        for (const auto &ip : failedAddresses) {
            if (ping_host(ip)) {
                QMetaObject::invokeMethod(this, [this, passNumber, ip]() {
                    log(QString("Проход %1: %2 - связь появилась").arg(passNumber).arg(QString::fromStdString(ip)));
                });
                
                SSHClient client(ip, "root");
                bool connected = false;
                
                for (const auto &password : passwords) {
                    if (client.connect(password, 10)) {
                        connected = true;
                        
                        if (sending_new_password(client)) {
                            QMetaObject::invokeMethod(this, [this, passNumber, ip]() {
                                log(QString("Проход %1: %2 - пароль изменён").arg(passNumber).arg(QString::fromStdString(ip)));
                            });
                            removeAddressFromJournal(journalPath, ip);
                            successCount++;
                        } else {
                            QMetaObject::invokeMethod(this, [this, passNumber, ip]() {
                                log(QString("Проход %1: %2 - ошибка смены пароля").arg(passNumber).arg(QString::fromStdString(ip)));
                            });
                        }
                        
                        client.disconnect();
                        break;
                    }
                }
                
                if (!connected) {
                    QMetaObject::invokeMethod(this, [this, passNumber, ip]() {
                        log(QString("Проход %1: %2 - не подошёл ни один пароль").arg(passNumber).arg(QString::fromStdString(ip)));
                    });
                }
            }
        }
        
        QMetaObject::invokeMethod(this, [this, passNumber, successCount]() {
            log(QString("Проход %1 завершён: изменено %2 паролей").arg(passNumber).arg(successCount));
        });
    }

    void onStart() {
        // Защита от повторного запуска
        if (!btnStart->isEnabled()) {
            logArea->append("Процесс уже запущен!");
            return;
        }

        if (PathListAddresses->text().isEmpty() || PathListPassword->text().isEmpty() || 
            ListAddressesUnchangedPasswords->text().isEmpty() || inputNewPassword->text().isEmpty()) {
            logArea->append("Ошибка: Заполните все поля!");
            return;
        }

        newPassword = inputNewPassword->text();
        btnStart->setEnabled(false);
        
        std::vector<std::string> ips;
        std::vector<std::string> passwords;
        
        QString addrPath = PathListAddresses->text();
        if (addrPath.endsWith(".xlsx", Qt::CaseInsensitive)) {
#ifdef USE_QXLSX
            ips = readIPsFromExcel(addrPath);
            log(QString("Прочитано %1 IP из Excel").arg(ips.size()));
#else
            log("Ошибка: QXlsx не подключён.");
            btnStart->setEnabled(true);
            return;
#endif
        } else {
            // Используем Qt для чтения (автоматически обрабатывает CRLF)
            QFile file(addrPath);
            if (file.open(QIODevice::ReadOnly | QIODevice::Text)) {
                QTextStream in(&file);
                while (!in.atEnd()) {
                    QString line = in.readLine().trimmed();
                    if (!line.isEmpty()) {
                        ips.push_back(line.toStdString());
                    }
                }
                file.close();
            }
            log(QString("Прочитано %1 IP из файла").arg(ips.size()));
        }

        // Чтение паролей с обработкой CRLF
        QFile passFile(PathListPassword->text());
        if (passFile.open(QIODevice::ReadOnly | QIODevice::Text)) {
            QTextStream in(&passFile);
            while (!in.atEnd()) {
                QString line = in.readLine().trimmed();
                if (!line.isEmpty()) {
                    passwords.push_back(line.toStdString());
                }
            }
            passFile.close();
        }

        std::reverse(passwords.begin(), passwords.end());

        totalTasks = ips.size();
        completedTasks = 0;

        if (totalTasks == 0) {
            logArea->append("Список IP пуст.");
            btnStart->setEnabled(true);
            return;
        }

        auto passwordsCopy = passwords;
        for (const auto& ip : ips) {
            pool->enqueue([this, ip, passwordsCopy]() {
                processIP(ip, passwordsCopy);
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
    std::unique_ptr<ThreadPool> pool;

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
        if (!ping_host(ip)) {
            log(QString("Нет связи: %1 (хост недоступен)").arg(QString::fromStdString(ip)));
            recording(PathAddressesUnchangedPasswords.toStdString(), ip, "нет связи");
            
            completedTasks++;
            updateProgres(completedTasks, totalTasks);
            checkFinish();
            return;
        }

        bool connected = false;

        for (const auto& pass : passwords) {
            SSHClient client(ip, "root");
            
            if (client.connect(pass)) {
                log(QString("Подключено к %1. Пароль: %2").arg(QString::fromStdString(ip), QString::fromStdString(pass)));
                connected = true;

                if (sending_new_password(client)) {
                    log(QString("Успех: пароль изменен, перезагрузка на %1").arg(QString::fromStdString(ip)));
                } else {
                    log(QString("Ошибка: не удалось сменить пароль на %1").arg(QString::fromStdString(ip)));
                    recording(PathAddressesUnchangedPasswords.toStdString(), ip, "ChangePassFailed");
                }
                
                client.disconnect();
                break;
            }
        }

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
            // Запускаем повторные проходы в фоновом потоке, не блокируя UI
            std::thread([this]() {
                QMetaObject::invokeMethod(this, [this]() {
                    log("Основной список завершён. Начинаю проходы по журналу...");
                });

                retryFailedAddresses(1);
                retryFailedAddresses(2);
                
                QMetaObject::invokeMethod(this, [this]() {
                    btnStart->setEnabled(true);
                    log("Завершено.");
                    
                    // Добавление нового пароля в список (выполняется в UI потоке)
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
            }).detach();
        }
    }

    // New function to initialize thread pool based on hardware concurrency
};

int main(int argc, char *argv[]) {
    QApplication app(argc, argv);
    MainWindow window;
    window.show();
    return app.exec();
}

#include "main.moc"

// src/main.cpp

std::vector<std::string> readIPsFromFile(const QString &filePath) {
    std::vector<std::string> ips;
    QFile file(filePath);
    
    if (!file.open(QIODevice::ReadOnly | QIODevice::Text)) {
        return ips;
    }
    
    QTextStream in(&file);
    while (!in.atEnd()) {
        QString line = in.readLine().trimmed();
        if (!line.isEmpty()) {
            ips.push_back(line.toStdString());
        }
    }
    
    file.close();
    return ips;
}