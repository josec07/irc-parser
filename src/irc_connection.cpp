#include "ircp/irc_connection.h"
#include "ircp/logger.h"
#include <boost/asio.hpp>
#include <iostream>

IrcConnection::IrcConnection(const std::string &host, int port)
    : host_(host), port_(port), io_context_(), socket_(io_context_),
      resolver_(io_context_), socket_open_(false) {}

bool IrcConnection::openSocket() {
  try {
    std::cout << "[IRC] Resolving " << host_ << ":" << port_ << "..."
              << std::endl;
    // Resolve the host and port synchronously
    auto endpoints = resolver_.resolve(host_, std::to_string(port_));
    std::cout << "[IRC] DNS resolution successful, found "
              << std::distance(endpoints.begin(), endpoints.end())
              << " endpoint(s)" << std::endl;

    // Connect synchronously
    std::cout << "[IRC] Attempting TCP socket connection..." << std::endl;
    boost::asio::connect(socket_, endpoints);

    socket_open_ = true;
    std::cout << "[IRC] TCP socket opened successfully!" << std::endl;

    return true;
  } catch (const std::exception &e) {
    std::cerr << "[IRC] Socket connection failed: " << e.what() << std::endl;
    return false;
  }
}

bool IrcConnection::reconnectSocket() {
  closeSocket();

  // Reopen the socket connection to the server
  if (!openSocket()) {
    Logger::info("[IRC-Reconnect] Reconnect failed.");
    return false;
  }

  Logger::info("[IRC-Reconnect] Reconnected socket to server successfully.");
  return true;
}

void IrcConnection::run() { io_context_.run(); }

std::string IrcConnection::readLine() {
  if (!socket_open_) {
    std::cout << "[IRC] readLine() called but socket is not open" << std::endl;
    return "";
  }

  std::string line;
  boost::system::error_code error;
  std::cout << "[IRC] Waiting to read data from socket..." << std::endl;
  boost::asio::read_until(socket_, buffer_, "\r\n", error);

  if (!error) {
    std::istream is(&buffer_);
    std::getline(is, line);
    // Remove trailing \r if present
    if (!line.empty() && line.back() == '\r') {
      line.pop_back();
    }
    std::cout << "[IRC] Raw data received (" << line.length()
              << " bytes): " << line << std::endl;
    return line;
  } else {
    ///
    Logger::error("[IRC001] Read error: " + error.message());
    Logger::info("[IRC001] error occured with: " + error.message());

    if (reconnectSocket()) {
      Logger::info("[IRC] Reconnected successfully after read error." + error.message());
      return readLine(); // recursive call to start reading again after
                         // reconnecting
    } else {
      std::cerr << "[IRC] Failed to reconnect after read error."
                << std::endl; // if i still fail I can figure out here
      return "";
    }

    return "";
  }
}

void IrcConnection::send(const std::string &message) {
  if (!socket_open_) {
    throw std::runtime_error("Socket is not open");
  }
  std::cout << "[IRC] Sending (" << message.length() << " bytes): " << message;
  if (!message.empty() && message.back() == '\n') {
    std::cout << " [with CRLF]";
  }
  std::cout << std::endl;
  boost::asio::write(socket_, boost::asio::buffer(message));
  std::cout << "[IRC] Send completed successfully" << std::endl;
}

void IrcConnection::closeSocket() {
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
}

void IrcConnection::startRead() {
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

void IrcConnection::handleLine(const std::string &line) {
  // Suppress logging to stdout
}
