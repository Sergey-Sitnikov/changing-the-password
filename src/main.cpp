#include <QApplication>
#include <QWidget>
#include <QPushButton>
#include <QVBoxLayout>
#include <QLineEdit>
#include <QString>
#include <QLabel>
#include <QMessageBox>
#include "ssh_helper.h"

class MainWindow : public QWidget
{
    Q_OBJECT // Добавьте этот макрос

        public : MainWindow(QWidget *parent = nullptr)
        : QWidget(parent)
    {
        QVBoxLayout *layout = new QVBoxLayout;

        passwordInput = new QLineEdit(this);
        QPushButton *changeButton = new QPushButton("Изменить пароль", this);
        statusLabel = new QLabel("", this);

        layout->addWidget(new QLabel("Введите новый пароль:", this));
        layout->addWidget(passwordInput);
        layout->addWidget(changeButton);
        layout->addWidget(statusLabel);

        setLayout(layout);

        connect(changeButton, &QPushButton::clicked, this, &MainWindow::handleChangePassword);
    }

private slots:
    void handleChangePassword()
    {
        QString newPassword = passwordInput->text();
        if (changePasswordViaSSH(newPassword.toStdString()))
        {
            statusLabel->setText("Пароль успешно изменен.");
        }
        else
        {
            QMessageBox::warning(this, "Ошибка", "Не удалось изменить пароль.");
        }
    }

private:
    QLineEdit *passwordInput;
    QLabel *statusLabel;
};

int main(int argc, char *argv[])
{
    QApplication app(argc, argv);

    MainWindow window;
    window.resize(300, 450);
    window.setWindowTitle("SSH Password Changer");
    window.show();

    return app.exec();
}
#include "main.moc"