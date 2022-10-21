#ifndef ACC_ENGINEER_SERVER_GUI_LOG_SINK_H
#define ACC_ENGINEER_SERVER_GUI_LOG_SINK_H

#include <QObject>

#include <spdlog/sinks/base_sink.h>

class real_gui_sink : public QObject
{
    Q_OBJECT
public:
    void sink_it(std::string str);

signals:
    void gui_log_sink_it(std::string str);
};

template<typename Mutex>
class gui_log_sink : public spdlog::sinks::base_sink<Mutex>
{
public:
    gui_log_sink(std::shared_ptr<real_gui_sink> sink)
        : real_sink_(sink)
    {}

protected:
    void sink_it_(const spdlog::details::log_msg &msg) override
    {
        spdlog::memory_buf_t formatted;
        spdlog::sinks::base_sink<Mutex>::formatter_->format(msg, formatted);
        real_sink_->sink_it(fmt::to_string(formatted));
    }

    void flush_() override {}

    std::shared_ptr<real_gui_sink> real_sink_;
};

#include "spdlog/details/null_mutex.h"
#include <mutex>
using gui_log_sink_mt = gui_log_sink<std::mutex>;
using gui_log_sink_st = gui_log_sink<spdlog::details::null_mutex>;

#endif // ACC_ENGINEER_SERVER_GUI_LOG_SINK_H