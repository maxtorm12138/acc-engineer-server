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
    , log_file_(new QFile)
    , log_text_stream_(new QTextStream)
{
    ui_->setupUi(this);
    log_file_->setParent(this);
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

    auto address = ui_->AddressLineEdit->text();
    auto port = ui_->PortLineEdit->text().toUInt();
    auto password = ui_->PasswordLineEdit->text();
    spdlog::debug("address: {}, port: {}, password: {}", address.toStdString(), port, password.toStdString());

    emit start_server(address, port, password);
}

void launcher::on_Log_sink_it(std::string str)
{
    ui_->LogTextEdit->append(QString::fromStdString(str));
}

} // namespace acc_engineer::ui
