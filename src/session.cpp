#include "ircp/session.h"
#include "ircp/logger.h"
#include "ircp/twitch_irc.h"
#include <boost/asio.hpp>
#include <deque>
#include <iostream>
#include <string>

Session::Session(const std::string &host, int port)
    : host_(host), port_(port), io_context_(), socket_(io_context_),
      resolver_(io_context_), socket_open_(false) {}


void startRead(TwitchIRC irc){
  //session logic begins here.
    std::deque<std::string> lastN;
    const size_t MAX = 1000;
    //one line
    while (true) {
        std::string line = irc.readLine();
        if (line.empty())continue;
        
        std::string json_msg = irc.parse_message(line);
        if (json_msg.empty())continue;
        
        lastN.push_back(json_msg);
        std::cout << json_msg << std::endl;
        if(lastN.size() > MAX)lastN.pop_front();
    }

}

bool Session::openSocket() {
  try {
    // Resolve the host and port synchronously
    auto endpoints = resolver_.resolve(host_, std::to_string(port_));

    boost::asio::connect(socket_, endpoints);

    socket_open_ = true;


    return true;
  } catch (const std::exception &e) {
    std::string ex = e.what();
    Logger::error( "[IRC] Socket connection failed: " + ex);
    return false;
  }
}

void Session::run() { io_context_.run(); }

// std::string Session::readLine() {
//   if (!socket_open_) {
//     std::cout << "[IRC] readLine() called but socket is not open" << std::endl;
//     return "";
//   }

//   std::string line;
//   boost::system::error_code error;
//   std::cout << "[IRC] Waiting to read data from socket..." << std::endl;
//   boost::asio::read_until(socket_, buffer_, "\r\n", error);

//   if (!error) {
//     std::istream is(&buffer_);
//     std::getline(is, line);
//     // Remove trailing \r if present
//     if (!line.empty() && line.back() == '\r') {
//       line.pop_back();
//     }
//     std::cout << "[IRC] Raw data received (" << line.length()
//               << " bytes): " << line << std::endl;
//     return line;
//   } else {
//     Logger::error("[IRC001] Read error: " + error.message());
//     std::cerr << "[IRC] Socket read error: " << error.message() << std::endl;
//     closeSocket();
//     return "";
//   }
// }

// void Session::send(const std::string &message) {
//   if (!socket_open_) {
//     throw std::runtime_error("Socket is not open");
//   }
//   std::cout << "[IRC] Sending (" << message.length() << " bytes): " << message;
//   if (!message.empty() && message.back() == '\n') {
//     std::cout << " [with CRLF]";
//   }
//   std::cout << std::endl;
//   boost::asio::write(socket_, boost::asio::buffer(message));
//   std::cout << "[IRC] Send completed successfully" << std::endl;
// }

void Session::closeSocket() {
  
  if (socket_open_) {
    boost::system::error_code ec;

    socket_.shutdown(boost::asio::ip::tcp::socket::shutdown_both, ec);
    
    if (ec) {
      Logger::error("[IRC] Error during socket shutdown: " + ec.message());
    }
    
    socket_.close(ec);
    
    if (ec) {
      std::cerr << "[IRC] Error during socket close: " << ec.message()
                << std::endl;
    }
    buffer_.consume(buffer_.size());
    socket_open_ = false;
  }
  Logger::error("[closeSocket] socket not open");
}

void Session::startRead() {
  boost::asio::async_read_until(
      socket_, buffer_, "\r\n",
      [this](const boost::system::error_code &ec,
             std::size_t bytes_transferred) {
        if (!ec) {
          std::string line(static_cast<const char *>(buffer_.data().data()),
                           bytes_transferred - 2);
          buffer_.consume(bytes_transferred);
          handleLine(line);
          startRead();
        } else {
          std::cerr << "Read error: " << ec.message() << std::endl;
          socket_open_ = false;
        }
      });
}

void Session::handleLine(const std::string &line) {
  // Suppress logging to stdout
}
