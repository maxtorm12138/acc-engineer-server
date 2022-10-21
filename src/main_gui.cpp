// std
#include <thread>

// qt
#include <QApplication>

// boost
#include <boost/asio/io_context.hpp>
#include <boost/asio/co_spawn.hpp>

// spdlog
#include <spdlog/sinks/basic_file_sink.h>

// module
#include "config.h"
#include "service.h"
#include "gui_log_sink.h"

// ui
#include "ui/launcher.h"

namespace net = boost::asio;

net::awaitable<void> co_service_main(std::string args)
{
    auto config = acc_engineer::config::from_string(args);
    acc_engineer::service service(config);
    co_await service.run();
}

int service_main(std::string args)
{
    net::io_context io_context;

    co_spawn(io_context, co_service_main(std::move(args)), [](const std::exception_ptr &exception_ptr) {
        if (exception_ptr != nullptr)
        {
            std::rethrow_exception(exception_ptr);
        }
    });

    io_context.run();
    return 0;
}

void start_service(QString address, uint port, QString password)
{
    auto args = fmt::format("-a {} -p {} --password {}", address.toStdString(), port, password.toStdString());
    std::thread thread(service_main, std::move(args));
    thread.detach();
}

int main(int argc, char *argv[])
{
    QApplication app(argc, argv);

    auto real_sink = std::make_shared<real_gui_sink>();

    spdlog::set_level(spdlog::level::debug);
    spdlog::set_default_logger(spdlog::synchronous_factory::create<gui_log_sink_mt>("gui_log_sink", real_sink));

    acc_engineer::ui::launcher launcher;
    QObject::connect(&launcher, &acc_engineer::ui::launcher::start_server, start_service);
    QObject::connect(real_sink.get(), &real_gui_sink::gui_log_sink_it, &launcher, &acc_engineer::ui::launcher::on_Log_sink_it);

    launcher.show();
    return app.exec();
}
