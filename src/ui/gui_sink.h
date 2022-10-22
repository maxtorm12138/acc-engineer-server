#ifndef ACC_ENGINEER_SERVER_GUI_SINK_H
#define ACC_ENGINEER_SERVER_GUI_SINK_H
// std
#include <mutex>

// QT
#include <QObject>
#include <QString>

// spdlog
#include <spdlog/sinks/sink.h>

// boost
#include <boost/noncopyable.hpp>

namespace acc_engineer {

class gui_sink : public spdlog::sinks::sink
{
public:
    explicit gui_sink();

    ~gui_sink() override = default;

    void log(const spdlog::details::log_msg &msg) override;

    void flush() override;

    void set_pattern(const std::string &pattern) override;

    void set_formatter(std::unique_ptr<spdlog::formatter> sink_formatter) override;

private:
    std::mutex mutex_;
    std::unique_ptr<spdlog::formatter> formatter_;
};

class gui_sink_helper : public QObject
{
    Q_OBJECT
public:
    void log(QString log)
    {
        emit on_new_log(log);
    }

signals:
    void on_new_log(QString log);
};

extern gui_sink_helper helper;

} // namespace acc_engineer
#endif // ACC_ENGINEER_SERVER_GUI_SINK_H