#include "gui_sink.h"

#include <spdlog/pattern_formatter.h>

namespace acc_engineer {
gui_sink::gui_sink()
    : mutex_()
    , formatter_(std::make_unique<spdlog::pattern_formatter>())
{}

void gui_sink::log(const spdlog::details::log_msg &msg)
{
    std::lock_guard guard(mutex_);

    msg.color_range_start = 0;
    msg.color_range_end = 0;
    spdlog::memory_buf_t formatted;
    formatter_->format(msg, formatted);
    std::string content;
    auto current = formatted.begin();
    content.append(current, msg.color_range_start);
    current += msg.color_range_start;

    content.append(current, msg.color_range_end - msg.color_range_start);
    current += msg.color_range_end - msg.color_range_start;

    content.append(current, formatted.size() - msg.color_range_end);

    helper.log(QString::fromStdString(content));
}

void acc_engineer::gui_sink::flush() {}

void acc_engineer::gui_sink::set_pattern(const std::string &pattern)
{
    formatter_ = std::make_unique<spdlog::pattern_formatter>(pattern);
}

void acc_engineer::gui_sink::set_formatter(std::unique_ptr<spdlog::formatter> sink_formatter)
{
    formatter_ = std::move(sink_formatter);
}

gui_sink_helper helper;

} // namespace acc_engineer
