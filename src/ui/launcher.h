#ifndef ACC_ENGINEER_SERVER_UI_LAUNCHER_H
#define ACC_ENGINEER_SERVER_UI_LAUNCHER_H

#include <QWidget>

class QFile;
class QTextStream;

namespace Ui {
class Launcher;
}

namespace acc_engineer::ui {
class launcher : public QWidget
{
    Q_OBJECT;

public:
    explicit launcher(QWidget *parent = nullptr);
    ~launcher() noexcept override;
signals:
    void start_server(QString, uint, QString);

public slots:
    void on_new_log(QString log);

private slots:
    void on_ServerButton_clicked();

private:
    Ui::Launcher *ui_{nullptr};
    QFile *log_file_{nullptr};
    QTextStream *log_text_stream_{nullptr};
};
} // namespace acc_engineer::ui

#endif // ACC_ENGINEER_SERVER_UI_LAUNCHER_H
