#pragma once 

#define ASIO_STANDALONE

#include <websocketpp/config/asio_no_tls.hpp>
#include <websocketpp/server.hpp>

#include <functional>
#include <sstream>

typedef websocketpp::server<websocketpp::config::asio> server;


class signalling_server {
public:
    signalling_server() {

        m_endpoint.set_error_channels(websocketpp::log::elevel::all);
        m_endpoint.set_access_channels(websocketpp::log::alevel::all ^ websocketpp::log::alevel::frame_payload);

        m_endpoint.init_asio();

        m_endpoint.set_open_handler(std::bind(
            &signalling_server::on_open, this, std::placeholders::_1
        ));

        m_endpoint.set_message_handler(std::bind(
            &signalling_server::on_message, this, std::placeholders::_1, std::placeholders::_2
        ));

        m_endpoint.set_close_handler(std::bind(
            &signalling_server::on_close, this, std::placeholders::_1
        ));
    }

    void on_open(websocketpp::connection_hdl hdl) {

        auto user_id = hdl.lock().get();

        std::cout << "USER CONNECTED: (" << user_id << ")" << std::endl;

        m_active_user_handle[user_id] = hdl.lock();
    }

    void on_message(websocketpp::connection_hdl hdl, server::message_ptr msg) {

        auto user_id = hdl.lock().get();

        std::cout << "USER MESSAGE: (" << user_id << ") " << msg->get_payload() << std::endl;

        std::string userMessage = msg->get_payload();

        auto ws_pos = userMessage.find(m_userIdPrefix);
        if (ws_pos != std::string::npos) {
           // message to one user

           while ( ! (userMessage[ws_pos] >= '0' &&  userMessage[ws_pos] <= '9'))
              ws_pos++;

           uint64_t ws_user_id = std::stoi(userMessage.substr(ws_pos));
           std::cout << "MESSAGE FOR USER: (" << ws_user_id << ") " << std::endl;

           auto userHandle = m_active_user_handle.find((void*)ws_user_id);
           if (userHandle == m_active_user_handle.end()) {
               std::cout << "USER " << ws_user_id << " NOT ACTIVE" << std::endl;
               return;
           }

           std::cout << "SEND TO USER : (" << ws_user_id << ") " << userMessage << std::endl;
           m_endpoint.send(userHandle->second, userMessage, msg->get_opcode());
           return;
        }

        m_active_user_info[user_id] = userMessage;

        auto connectEvent = createMessage(m_connectPrefix, user_id, userMessage);

        // Send new user to all other users
        for (auto &userHandle: m_active_user_handle) {

             if (userHandle.first != user_id) {
                 std::cout << "SEND TO USER : (" << user_id << ") " << connectEvent << std::endl;

                 try {
                     m_endpoint.send(userHandle.second, connectEvent, msg->get_opcode());
                 }
                 catch (std::exception &exp) {
                     std::cout << "SEND FAILED: " << exp.what() << std::endl;
                 }
             }
        }

        // Send all other users to the new user
        for (auto &userEntry : m_active_user_info) {
             const std::string &userInfo = userEntry.second;

             connectEvent = createMessage(m_connectPrefix, userEntry.first, userInfo);

             if (userEntry.first != user_id) {
                 std::cout << "SEND TO USER : (" << userEntry.first << ") " << connectEvent << std::endl;

                 try {
                     m_endpoint.send(hdl, connectEvent, msg->get_opcode());
                 }
                 catch (std::exception &exp) {
                     std::cout << "SEND FAILED: " << exp.what() << std::endl;
                 }
             }
        }
    }

    void on_close(websocketpp::connection_hdl hdl) {
        auto user_id = hdl.lock().get();

        std::cout << "USER DISCONNECTED: ("  << user_id << ")" << std::endl;

        const std::string &userMessage = m_active_user_info[user_id];
        auto disconnectEvent = createMessage(m_disconnectPrefix, user_id, userMessage);

        // Send user disconnection to all other users
        for (auto &userHandle: m_active_user_handle) {
             if (userHandle.first != user_id) {
                 std::cout << "SEND TO USER : (" << userHandle.first << ") " << disconnectEvent << std::endl;

                 try {
                     m_endpoint.send(userHandle.second, disconnectEvent, websocketpp::frame::opcode::TEXT);
                 }
                 catch (std::exception &exp) {
                     std::cout << "SEND FAILED: " << exp.what() << std::endl;
                 }
             }
        }

        m_active_user_info.erase(user_id);
        m_active_user_handle.erase(user_id);
    }

    void start(int32_t port) {
        m_endpoint.listen(port);
        m_endpoint.start_accept();
        m_endpoint.run();
    }

private:
    server m_endpoint;

    // Using connection pointer as the key to user information map
    typedef void* user_connection_type;

    std::map<user_connection_type, std::string> m_active_user_info;
    std::map<user_connection_type, std::shared_ptr<void>> m_active_user_handle;

    std::string m_userIdPrefix = "\"ws_user_id\":\"";
    std::string m_connectPrefix = "{ \"event_id\": \"connect\",";
    std::string m_disconnectPrefix = "{ \"event_id\": \"disconnect\",";

    std::string createMessage(const std::string &event, void* user_id, const std::string &userMessage) {
         uint64_t ws_user_id = reinterpret_cast<uint64_t>(user_id);

         std::ostringstream msg;
         msg << event << m_userIdPrefix << ws_user_id << "\"," << userMessage.substr(userMessage.find("{") + 1);
         return msg.str();
    }
};
