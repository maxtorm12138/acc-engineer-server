#include "gui_log_sink.h"

void real_gui_sink::sink_it(std::string str)
{
    emit gui_log_sink_it(std::move(str));
}