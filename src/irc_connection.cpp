#include "ircp/irc_connection.h"
#include "ircp/logger.h"
#include <boost/asio.hpp>
#include <iostream>

namespace ircp {

IrcConnection::IrcConnection(const std::string &host, int port)
    : io_context_(), socket_(io_context_), resolver_(io_context_),
      connected_(false) {}

bool IrcConnection::connect() {
  try {
    std::cout << "[IRC] Resolving irc.twitch.tv:6667..." << std::endl;
    // Resolve the host and port synchronously
    auto endpoints = resolver_.resolve("irc.twitch.tv", "6667");
    std::cout << "[IRC] DNS resolution successful, found "
              << std::distance(endpoints.begin(), endpoints.end())
              << " endpoint(s)" << std::endl;

    // Connect synchronously
    std::cout << "[IRC] Attempting TCP connection..." << std::endl;
    boost::asio::connect(socket_, endpoints);

    connected_ = true;
    std::cout << "[IRC] TCP connection established successfully!" << std::endl;

    return true;
  } catch (const std::exception &e) {
    std::cerr << "[IRC] Connection failed: " << e.what() << std::endl;
    return false;
  }
}

bool IrcConnection::reconnect() {
  // close the current socket connection
  boost::system::error_code error;
  // create a error code object

  socket_.shutdown(boost::asio::ip::tcp::socket::shutdown_both, error);
  // shutdowns the socket, attaches error

  if (error) {
    std::cerr << "[IRC] Error during shutdown: " << error.message()
              << std::endl;
  }
  // spells out error code if any. can send this out to external service

  socket_.close(error);
  if (error) {
    std::cerr << "[IRC] Error during close: " << error.message() << std::endl;
  }

  // Reconnect to the server
  if (!connect()) {
    std::cerr << "[IRC] Reconnect failed." << std::endl;
    return false;
  }

  std::cout << "[IRC] Reconnected to server successfully." << std::endl;
  return true;
}

void IrcConnection::run() { io_context_.run(); }

std::string IrcConnection::readLine() {
  if (!connected_) {
    std::cout << "[IRC] readLine() called but not connected" << std::endl;
    return "";
  }

  boost::asio::streambuf buffer;
  std::string line;

  boost::system::error_code error;
  std::cout << "[IRC] Waiting to read data from socket..." << std::endl;
  boost::asio::read_until(socket_, buffer, "\r\n", error);

  if (!error) {
    std::istream is(&buffer);
    std::getline(is, line);
    // Remove trailing \r if present
    if (!line.empty() && line.back() == '\r') {
      line.pop_back();
    }
    std::cout << "[IRC] Raw data received (" << line.length()
              << " bytes): " << line << std::endl;
    return line;
  } else {
    Logger::error("[IRC] Read error: " + error.message());
    Logger::info("[IRC] error occured with: " + error.message());

    if (reconnect()) {
      std::cout << "[IRC] Reconnected successfully after read error."
                << std::endl;
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
  if (!connected_) {
    throw std::runtime_error("Not connected");
  }
  std::cout << "[IRC] Sending (" << message.length() << " bytes): " << message;
  if (!message.empty() && message.back() == '\n') {
    std::cout << " [with CRLF]";
  }
  std::cout << std::endl;
  boost::asio::write(socket_, boost::asio::buffer(message));
  std::cout << "[IRC] Send completed successfully" << std::endl;
}

void IrcConnection::sendPassword(const std::string &password) {
  std::string msg = "PASS " + password + "\r\n";
  send(msg);
}

void IrcConnection::sendJoin(const std::string &channel) {
  std::string msg = "JOIN #" + channel + "\r\n";
  send(msg);
}

void IrcConnection::disconnect() {
  if (connected_) {
    boost::system::error_code ec;
    socket_.shutdown(boost::asio::ip::tcp::socket::shutdown_both, ec);
    socket_.close(ec);
    connected_ = false;
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
          connected_ = false;
        }
      });
}

void IrcConnection::handleLine(const std::string &line) {
  // Suppress logging to stdout
}

} // namespace ircp
