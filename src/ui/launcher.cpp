#include "launcher.h"

// qt
#include <QFile>

// spdlog
#include <spdlog/spdlog.h>

// ui
#include "ui_launcher.h"
namespace acc_engineer::ui {
launcher::launcher(QWidget *parent)
    : QWidget(parent)
    , ui_(new Ui::Launcher)
{
    ui_->setupUi(this);
}

launcher::~launcher() noexcept
{
    delete ui_;
}

void launcher::on_ServerButton_clicked()
{
    ui_->ServerButton->setDisabled(true);
    ui_->ServerButton->setText(tr("Starting", "Starting"));

    ui_->AddressLineEdit->setEnabled(false);
    ui_->PortLineEdit->setEnabled(false);
    ui_->PasswordLineEdit->setEnabled(false);
    ui_->LogLevelComboBox->setEnabled(false);
    auto level = ui_->LogLevelComboBox->currentText();
    spdlog::set_level(spdlog::level::from_str(level.toStdString()));

    auto address = ui_->AddressLineEdit->text();
    auto port = ui_->PortLineEdit->text().toUInt();
    auto password = ui_->PasswordLineEdit->text();
    SPDLOG_DEBUG("address: {}, port: {}, password: {}", address.toStdString(), port, password.toStdString());

    emit start_server(address, port, password);
}

void launcher::handle_new_log(QString log)
{
    ui_->LogTextEdit->moveCursor(QTextCursor::End);
    ui_->LogTextEdit->insertHtml(log);
    ui_->LogTextEdit->moveCursor(QTextCursor::End);
}

} // namespace acc_engineer::ui
