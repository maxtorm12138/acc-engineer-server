#include "config.h"

// std
#include <iostream>

namespace po = boost::program_options;
namespace net = boost::asio;

namespace acc_engineer::detail {
struct PortType {
  PortType(int port)
    : port(port) {
  }

  int port;
};

void validate(boost::any& v, const std::vector<std::string>& values, PortType*,
              int) {
  po::validators::check_first_occurrence(v);
  auto s = po::validators::get_single_string(values);

  if (int port = 0; boost::conversion::try_lexical_convert(s, port)) {
    if (port <= 0 || port >= 65535) {
      throw po::validation_error(po::validation_error::invalid_option_value);
    }

    v = PortType(port);
  } else {
    throw po::validation_error(po::validation_error::invalid_option_value);
  }
}

struct AddressType {
  AddressType(const std::string& address)
    : address(net::ip::make_address(address)) {
  }

  net::ip::address address;
};

void validate(boost::any& v, const std::vector<std::string>& values,
              AddressType*, int) {
  po::validators::check_first_occurrence(v);
  auto s = po::validators::get_single_string(values);

  boost::system::error_code ec;
  auto address = net::ip::make_address(s, ec);
  if (ec) {
    throw po::validation_error(po::validation_error::invalid_option_value);
  }

  v = AddressType(s);
}
}

namespace acc_engineer {
config config::from_command_line(int argc, char* argv[]) {
  po::options_description descriptions("Usage");

  descriptions.add_options()
      ("help,h", "print help")
      ("port,p", po::value<detail::PortType>()->required(), "listen port")
      ("address,a", po::value<detail::AddressType>(), "listen address")
      ("password,P", po::value<std::string>()->required(),
       "authentication password");

  try {
    po::variables_map vm;
    store(parse_command_line(argc, argv, descriptions), vm);

    if (vm.contains("help")) {
      std::cerr << descriptions << std::endl;
      exit(0);
    }

    notify(vm);

    config cfg;

    cfg.password_ = vm["password"].as<std::string>();
    cfg.bind_port_ = vm["port"].as<detail::PortType>().port;
    cfg.bind_address_ = vm["address"].as<detail::AddressType>().address;

    return cfg;
  } catch (const std::exception& e) {
    std::cerr << e.what() << std::endl;
    std::cerr << descriptions << std::endl;
    exit(-1);
  }
}
}
