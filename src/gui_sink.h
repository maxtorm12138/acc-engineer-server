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

class gui_sink : public spdlog::sinks::sink, public QObject
{
    Q_OBJECT
public:
    explicit gui_sink();

    ~gui_sink() override = default;

    void log(const spdlog::details::log_msg &msg) override;

    void flush() override;

    void set_pattern(const std::string &pattern) override;

    void set_formatter(std::unique_ptr<spdlog::formatter> sink_formatter) override;

signals:
    void new_log(QString content);

private:
    std::mutex mutex_;
    std::unique_ptr<spdlog::formatter> formatter_;
};

} // namespace acc_engineer
#endif // ACC_ENGINEER_SERVER_GUI_SINK_H