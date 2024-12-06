#include <iostream>
#include <cstdlib>
#include <memory>
#include <utility>
#include <ctime>
#include <iomanip>
#include <sstream>
#include <fstream>
#include <vector>
#include <string>
#include <boost/asio.hpp>

using boost::asio::ip::tcp;

struct LogRecord {
    char sensor_id[32];
    std::time_t timestamp;
    double value;
};

std::string time_t_to_string(std::time_t time) {
    std::tm* tm = std::localtime(&time);
    std::ostringstream ss;
    ss << std::put_time(tm, "%Y-%m-%dT%H:%M:%S");
    return ss.str();
}

std::time_t string_to_time_t(const std::string& time_string) {
    std::tm tm = {};
    std::istringstream ss(time_string);
    ss >> std::get_time(&tm, "%Y-%m-%dT%H:%M:%S");
    return std::mktime(&tm);
}

std::vector<std::string> tokenize(const std::string& input, char delimiter) {
    std::vector<std::string> tokens;
    std::string::const_iterator start = input.begin();
    std::string::const_iterator end = input.begin();

    while (end != input.end()) {
        while (end != input.end() && *end != delimiter) {
            end++;
        }
        tokens.push_back(std::string(start, end));
        if (end != input.end()) {
            end++;
        }
        start = end;
    }

    return tokens;
}

void saveRecordToFile(const std::string& fileName, const LogRecord& record) {
    std::ofstream file(fileName, std::ios::out | std::ios::binary | std::ios::app);
    if (file.is_open()) {
        file.write(reinterpret_cast<const char*>(&record), sizeof(LogRecord));
        file.close();
    } else {
        std::cerr << "Error opening file: " << fileName << std::endl;
    }
}

std::string readRecordFromFile(const std::string& fileName, int cont_RecordsToRead) {
    std::ifstream file(fileName, std::ios::in | std::ios::binary);
    if (file.is_open()) {
        std::vector<LogRecord> records;
        while (file) {
            LogRecord record;
            file.read(reinterpret_cast<char*>(&record), sizeof(LogRecord));
            records.push_back(record);
        }
        file.close();

        int cont_Records = records.size();
        if (cont_Records > cont_RecordsToRead) {
            records.erase(records.begin(), records.end() - cont_RecordsToRead);
        }

        std::string shareMessage = std::to_string(cont_Records);
        for (const LogRecord& record : records) {
            shareMessage += ";" + time_t_to_string(record.timestamp) + "|" + std::to_string(record.value);
        }
        shareMessage += "\r\n";
        return shareMessage;
    } else {
        std::cerr << "Error opening file: " << fileName << std::endl;
        return "ERROR|INVALID_SENSOR_ID\r\n";
    }
}

class Session : public std::enable_shared_from_this<Session> {
public:
    Session(tcp::socket socket) : socket_(std::move(socket)) {}

    void start() {
        readMessage();
    }

private:
    void readMessage() {
        auto self(shared_from_this());
        boost::asio::async_read_until(socket_, buffer_, "\r\n",
            [this, self](const boost::system::error_code& error, std::size_t length) {
                if (!error) {
                    std::istream is(&buffer_);
                    std::string message(std::istreambuf_iterator<char>(is), {});
                    std::string shareMessage;

                    std::cout << "Received: " << message << std::endl;

                    std::vector<std::string> messageD = tokenize(message, '|');

                    if (messageD[0] == "LOG") {
                        LogRecord dataLog;
                        strcpy(dataLog.sensor_id, messageD[1].c_str());
                        dataLog.timestamp = string_to_time_t(messageD[2]);
                        dataLog.value = std::stod(messageD[3]);
                        std::string filename = messageD[1] + ".dat";
                        saveRecordToFile(filename, dataLog);
                    } else if (messageD[0] == "GET") {
                        int cont_RecordsToRead = std::stoi(messageD[2]);
                        std::string filename = messageD[1] + ".dat";
                        shareMessage = readRecordFromFile(filename, cont_RecordsToRead);
                    } else {
                        shareMessage = "ERROR|INVALID_REQUEST\r\n";
                    }
                    writeMessage(shareMessage);
                }
            });
    }

    void writeMessage(const std::string& message) {
        auto self(shared_from_this());
        boost::asio::async_write(socket_, boost::asio::buffer(message),
            [this, self, message](const boost::system::error_code& error, std::size_t /*length*/) {
                if (!error) {
                    readMessage();
                }
            });
    }

    tcp::socket socket_;
    boost::asio::streambuf buffer_;
};

class Server {
public:
    Server(boost::asio::io_context& io_context, short port)
        : acceptor_(io_context, tcp::endpoint(tcp::v4(), port)) {
        accept();
    }

private:
    void accept() {
        acceptor_.async_accept(
            [this](const boost::system::error_code& error, tcp::socket socket) {
                if (!error) {
                    std::make_shared<Session>(std::move(socket))->start();
                }
                accept();
            });
    }

    tcp::acceptor acceptor_;
};

int main() {
    try {
        boost::asio::io_context io_context;
        Server s(io_context, 9000); // Port 9000
        io_context.run();
    } catch (std::exception& e) {
        std::cerr << "Exception: " << e.what() << std::endl;
    }
    return 0;
}
