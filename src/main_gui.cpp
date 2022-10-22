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
#include "service/config.h"
#include "service/service.h"

#include "ui/gui_sink.h"
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

    auto gui_logger = spdlog::synchronous_factory::create<acc_engineer::gui_sink>("GUI");

    spdlog::set_level(spdlog::level::debug);
    spdlog::set_default_logger(gui_logger);

    acc_engineer::ui::launcher launcher;
    QObject::connect(&launcher, &acc_engineer::ui::launcher::start_server, start_service);
    QObject::connect(&acc_engineer::helper, &acc_engineer::gui_sink_helper::on_new_log, &launcher, &acc_engineer::ui::launcher::on_new_log);

    launcher.show();
    return app.exec();
}
