#pragma once 

#define ASIO_STANDALONE

#include <websocketpp/config/asio_no_tls.hpp>
#include <websocketpp/server.hpp>

#include <json11/json11.hpp>

#include <functional>
#include <sstream>

typedef websocketpp::server<websocketpp::config::asio> server;

using json11::Json;

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

        auto connection_id = hdl.lock().get();

        std::cout << "USER CONNECTED: (" << connection_id << ")" << std::endl;

        Json connect = Json::object { { "type", "connect" }, { "message", "connection success!" } };

        m_endpoint.send(hdl, connect.dump(), websocketpp::frame::opcode::TEXT);
    }

    void on_message(websocketpp::connection_hdl hdl, server::message_ptr msg) {

        auto connection_id = hdl.lock().get();
        std::string userMessage = msg->get_payload();

        std::cout << "USER MESSAGE: (" << connection_id << ") " << userMessage << std::endl;

        std::string error;
        auto jsonMsg = Json::parse(userMessage, error);

        if (! error.empty()) {
           std::cout << "JSON parsing error: " << error << std::endl;
           return;
        }

        auto jsonMap = jsonMsg.object_items();
        if (jsonMap.find("type") == jsonMap.end()) {
           std::cout << "\"type\" not found in the message" << std::endl;
           return;
        }

        if (jsonMap.find("name") == jsonMap.end()) {
           std::cout << "\"name\" not found in the message" << std::endl;
           return;
        }

		Json ws_msg;
        auto type = jsonMap["type"].string_value();
        auto name = jsonMap["name"].string_value();
		if (jsonMap.find("candidate") != jsonMap.end()) {
			ws_msg = jsonMap["candidate"];
			std::cout << "CANDIDATE MSG : " << ws_msg.dump() << std::endl;
		}
        else if (jsonMap.find("offer") != jsonMap.end()) {
			ws_msg = jsonMap["offer"];
			std::cout << "OFFER MSG : " << ws_msg.dump() << std::endl;
        }
        else if (jsonMap.find("answer") != jsonMap.end()) {
			ws_msg = jsonMap["answer"];
			std::cout << "ANSWER MSG : " << ws_msg.dump() << std::endl;
        }

        if (type == "login") {

			if (m_connection_by_name.find(name) != m_connection_by_name.end()) {
               std::cout << "User " << name << " already logged in" << std::endl;
               Json login_reject = Json::object { { "type", "login" }, { "success", false }, { "reason", "user already logged in" } };
               m_endpoint.send(hdl, login_reject.dump(), websocketpp::frame::opcode::TEXT);
               return;
			}

			Json login_success = Json::object { { "type", "login" }, { "success", true } , { "users", get_user_list() } };
			m_endpoint.send(hdl, login_success.dump(), websocketpp::frame::opcode::TEXT);

			std::ostringstream user_object_id;
			user_object_id << connection_id;
			
			auto userinfo = Json::object { { "userName", name }, { "id", user_object_id.str() } };
			m_user_info_by_name[name] = userinfo;

			m_connection_by_name[name] = hdl.lock();
			m_login_name_by_id[connection_id] = name;

			Json update_for_other_users = Json::object { { "type", "updateUsers" }, { "user", userinfo } };
			for (auto &connection : m_connection_by_name) {
				if (connection.first != name)
				m_endpoint.send(connection.second, update_for_other_users.dump(), websocketpp::frame::opcode::TEXT);
			}

        }
        else { // all other types 

			if (m_connection_by_name.find(name) == m_connection_by_name.end()) {
				std::cout << "User \"" << name << "\" not logged in" << std::endl;
				std::cout << "Ignoring message for \"" << name << "\"" << std::endl;
				return;
			}

			Json webrtc_fwd_msg ;
			if(type == "candidate"){
				webrtc_fwd_msg = Json::object { { "type", type },{ "candidate", ws_msg } };
				m_endpoint.send(m_connection_by_name[name], webrtc_fwd_msg.dump(), msg->get_opcode());
			}
			else if(type == "offer"){
				webrtc_fwd_msg = Json::object { { "name", m_login_name_by_id[connection_id] }, { "offer", ws_msg }, { "type", type } };
				m_endpoint.send(m_connection_by_name[name], webrtc_fwd_msg.dump(), msg->get_opcode());
			}
			else if(type == "answer"){
				webrtc_fwd_msg = Json::object { { "type", type },{ "answer", ws_msg } };
				m_endpoint.send(m_connection_by_name[name], webrtc_fwd_msg.dump(), msg->get_opcode());
			}
			else{
				m_endpoint.send(m_connection_by_name[name], userMessage, msg->get_opcode());
			}
        }
    }

    void on_close(websocketpp::connection_hdl hdl) {
        auto connection_id = hdl.lock().get();

        std::cout << "USER DISCONNECTED: ("  << connection_id << ")" << std::endl;

        if (m_login_name_by_id.find(connection_id) == m_login_name_by_id.end()) {
           std::cout << "USER not logged in: ("  << connection_id << ")" << std::endl;
           return;
        }

        auto name = m_login_name_by_id[connection_id];
        m_connection_by_name.erase(name);
        m_user_info_by_name.erase(name);
        m_login_name_by_id.erase(connection_id);

        Json update_for_other_users = Json::object { { "type", "updateUsers" }, { "users", get_user_list() } };
        for (auto &connection : m_connection_by_name) {
//           if (connection.first != name)
//              m_endpoint.send(connection.second, update_for_other_users.dump(), websocketpp::frame::opcode::TEXT);
        }
    }

    Json get_user_list() const {
        std::vector<Json> user_list;

        for (auto &user : m_user_info_by_name) {
           user_list.push_back(user.second);
        }

        return Json(user_list);
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

    std::map<std::string, std::shared_ptr<void>> m_connection_by_name;
    std::map<std::string, Json> m_user_info_by_name;
    std::map<user_connection_type, std::string> m_login_name_by_id;

};
