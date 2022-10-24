#include "gui_sink.h"

#include <spdlog/pattern_formatter.h>

namespace acc_engineer::ui {

void gui_sink_emitter::log(QString log)
{
    emit new_log(log);
}

gui_sink::gui_sink()
    : mutex_()
    , formatter_(std::make_unique<spdlog::pattern_formatter>("%+", spdlog::pattern_time_type::local, ""))
{
    colors_[SPDLOG_LEVEL_TRACE] = "white";
    colors_[SPDLOG_LEVEL_DEBUG] = "cyan";
    colors_[SPDLOG_LEVEL_INFO] = "green";
    colors_[SPDLOG_LEVEL_WARN] = "yellow";
    colors_[SPDLOG_LEVEL_ERROR] = "red";
}

void gui_sink::log(const spdlog::details::log_msg &msg)
{
    std::lock_guard guard(mutex_);

    msg.color_range_start = 0;
    msg.color_range_end = 0;

    spdlog::memory_buf_t formatted;
    formatter_->format(msg, formatted);

    std::string content;
    auto current = formatted.begin();
    content.append("<font color=white>");
    content.append(current, msg.color_range_start);
    current += msg.color_range_start;
    content.append("</font>");

    content.append(fmt::format("<font color={}>", colors_[msg.level]));
    content.append(current, msg.color_range_end - msg.color_range_start);
    content.append("</font>");

    current += msg.color_range_end - msg.color_range_start;

    content.append("<font color=white>");
    content.append(current, formatted.size() - msg.color_range_end);
    content.append("</font><br>");
    gui_sink_emitter.log(QString::fromLocal8Bit(content.c_str(), content.size()));
}

void gui_sink::flush() {}

void gui_sink::set_pattern(const std::string &pattern)
{
    formatter_ = std::make_unique<spdlog::pattern_formatter>(pattern);
}

void gui_sink::set_formatter(std::unique_ptr<spdlog::formatter> sink_formatter)
{
    formatter_ = std::move(sink_formatter);
}

class gui_sink_emitter gui_sink_emitter;

} // namespace acc_engineer::ui
