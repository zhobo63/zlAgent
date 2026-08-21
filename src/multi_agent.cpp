#include "pch.h"

#include "multi_agent.h"
#include "logger.h"
#include "agent.h"
#include <chrono>
#include <condition_variable>
#include <map>
#include <thread>
#include "tools.h"

namespace agent {

// ── SubAgent ───────────────────────────────────────

SubAgent::SubAgent(const std::string& name, const std::string& description)
    : name_(name), description_(description)
{
}

std::string SubAgent::execute(const std::string& task) {
    auto resp = run_loop(task);
    return resp.content;
}

// ── SubAgentLLM ───────────────────────────────────────

SubAgentLLM::SubAgentLLM(const std::string& name, const std::string& description)
    : SubAgent(name, description)
{
}

void SubAgentLLM::set_workdir(const std::string workdir)
{
    // Create an internal Agent that loads config from the given directory.
    agent_ = std::make_unique<Agent>();

    // Try to load zlagent.ini from the working directory; fall back to defaults if not found.
    std::string ini_path = workdir + "/zlagent.ini";
    agent_->load_config(ini_path);

    // Generate project overview as the tool description for LLM routing.
    auto overview_tool = create_project_overview_tool();
    nlohmann::json args;
    args["directory"] = workdir;
    std::string overview = overview_tool->execute(args.dump());

    // Ask the internal agent to summarize the overview into a concise description.
    std::string summary_prompt = "Summarize the following project overview in 2-3 sentences. Focus on what the project does and its main technologies. Do not use markdown formatting:\n\n" + overview;
    ChatResponse summary_resp;
    agent_->run_stream(summary_prompt, [&](const std::string&, bool) {
        return true;
    }, &summary_resp);

    // Build a descriptive name and description for LLM routing.
    description_ = "Sub-agent working in: " + workdir + "\n" + summary_resp.content;
}

void SubAgentLLM::set_system_prompt(const std::string& prompt)
{
    if (agent_ && !prompt.empty()) {
        agent_->set_system_prompt(prompt);
    }
}

ChatResponse SubAgentLLM::run_loop(const std::string& task) {
    ChatResponse response;
    
    if (agent_) {
        // Execute the reasoning loop using the internal agent.
        agent_->run_stream(task, [&](const std::string& token, bool is_reasoning_flag) {
            return true;
        }, &response);
    }

    LOG_INFO("SubAgentLLM", response.content);
    return response;
}

// ── SubAgentNet (WebSocket Client) ─────────────────────

SubAgentNet::SubAgentNet(const std::string& name, const std::string& description)
    : SubAgent(name, description)
{
}

SubAgentNet::~SubAgentNet() {
    stop();
}

void SubAgentNet::start(const Config& cfg) {
    cfg_ = cfg;
    running_.store(true);
    last_pong_received_.store(true);
    ws_thread_ = std::thread(&SubAgentNet::connection_loop, this);
    heartbeat_thread_ = std::thread(&SubAgentNet::heartbeat_loop, this);
}

void SubAgentNet::stop() {
    running_.store(false);
    if (client_) {
        client_->close();
    }
    if (ws_thread_.joinable()) {
        ws_thread_.join();
    }
    if (heartbeat_thread_.joinable()) {
        heartbeat_thread_.join();
    }
    connected_.store(false);
}

void SubAgentNet::connection_loop() {
    while (running_.load()) {
        try {
            client_ = std::make_unique<ws::WebSocketClient>(cfg_.url);
            if (!client_->is_valid()) {
                LOG_WARN("SubAgentNet", "Invalid WebSocket URL: " + cfg_.url);
                std::this_thread::sleep_for(std::chrono::seconds(5));
                continue;
            }

            // Connect and register.
            if (client_->connect()) {
                LOG_INFO("SubAgentNet", "Connected to server: " + cfg_.url);
                connected_.store(true);

                // Send registration message.
                nlohmann::json reg_msg;
                reg_msg["type"] = "register";
                reg_msg["name"] = name_;
                reg_msg["description"] = description_;
                client_->send(reg_msg.dump());

                // Main read loop.
                while (running_.load() && connected_.load()) {
                    std::string msg;
                    auto result = client_->read(msg);
                    if (result == ws::ReadResult::Text) {
                        try {
                            auto j = nlohmann::json::parse(msg);
                            handle_message(j);
                        } catch (const std::exception& e) {
                            LOG_WARN("SubAgentNet", "Failed to parse message: " + std::string(e.what()));
                        }
                    } else if (result == ws::ReadResult::Fail) {
                        LOG_WARN("SubAgentNet", "WebSocket read failure — reconnecting...");
                        connected_.store(false);
                        break;
                    }
                }
            } else {
                LOG_WARN("SubAgentNet", "Failed to connect to: " + cfg_.url);
            }
        } catch (const std::exception& e) {
            LOG_WARN("SubAgentNet", "Connection error: " + std::string(e.what()));
            connected_.store(false);
        }

        // Reconnect after 5 seconds.
        if (running_.load()) {
            std::this_thread::sleep_for(std::chrono::seconds(5));
        }
    }
}

void SubAgentNet::heartbeat_loop() {
    while (running_.load()) {
        std::this_thread::sleep_for(std::chrono::seconds(1));

        if (!connected_.load()) continue;

        // Send ping every second.
        nlohmann::json ping_msg;
        ping_msg["type"] = "ping";
        if (client_) {
            client_->send(ping_msg.dump());
        }

        // Check if we received a pong within the last 5 seconds.
        if (!last_pong_received_.load()) {
            LOG_WARN("SubAgentNet", "No pong received for 5+ seconds — connection dead, reconnecting...");
            connected_.store(false);
            if (client_) {
                client_->close();
            }
        } else {
            // Reset the flag; it will be set to false again after 5 seconds without pong.
            last_pong_received_.store(true);
        }
    }
}

void SubAgentNet::handle_message(const nlohmann::json& msg) {
    std::string type = msg.value("type", "");

    if (type == "registered") {
        LOG_INFO("SubAgentNet", "Registered as: " + name_);
    }
    else if (type == "task") {
        std::string chat_id = msg.value("chat_id", "");
        std::string task = msg.value("task", "");

        LOG_INFO("SubAgentNet", "Received task from server (chat_id: " + chat_id + ")");

        // Execute the task using the global agent.
        Agent* g_agent = get_global_agent();
        std::string result;
        if (g_agent) {
            result = g_agent->run_stream(task, [&](const std::string&, bool) {
                return true;
            });
        } else {
            result = "Error: no global agent available";
        }

        // Send result back.
        nlohmann::json resp;
        resp["type"] = "result";
        resp["chat_id"] = chat_id;
        resp["result"] = result;

        if (client_) {
            client_->send(resp.dump());
        }
    }
    else if (type == "pong") {
        // Heartbeat response — connection is alive.
        last_pong_received_.store(true);
    }
    else if (type == "confirm_response") {
        std::string request_id = msg.value("request_id", "");
        std::string answer_str = msg.value("answer", "n");
        char answer = !answer_str.empty() ? answer_str[0] : 'n';

        // Resolve the pending confirmation.
        {
            std::lock_guard<std::mutex> lock(confirm_mutex_);
            auto it = pending_confirms_.find(request_id);
            if (it != pending_confirms_.end()) {
                it->second.promise.set_value(answer);
                pending_confirms_.erase(it);
            }
        }
    }
}

bool SubAgentNet::ask_confirm(const std::string& message, int timeout_seconds) {
    // Respect confirm_mode setting.
    if (cfg_.confirm_mode == "auto_yes") return true;
    if (cfg_.confirm_mode == "auto_no")  return false;

    // ask_server: forward to server terminal.
    char answer = send_confirm_request(message, timeout_seconds);
    return (answer == 'y');
}

char SubAgentNet::send_confirm_request(const std::string& message, int timeout_seconds) {
    if (!client_ || !connected_.load()) {
        LOG_WARN("SubAgentNet", "Not connected — cannot ask server for confirmation. Defaulting to 'n'.");
        return 'n';
    }

    // Generate a unique request_id.
    std::string request_id;
    {
        std::lock_guard<std::mutex> lock(confirm_mutex_);
        confirm_counter_++;
        request_id = "confirm_" + name_ + "_" + std::to_string(confirm_counter_);
    }

    // Create promise and get future before moving.
    std::promise<char> promise;
    auto result = promise.get_future();

    // Store the pending confirmation.
    {
        std::lock_guard<std::mutex> lock(confirm_mutex_);
        pending_confirms_.emplace(request_id, PendingConfirm{
            std::move(promise),
            std::chrono::steady_clock::now() + std::chrono::seconds(timeout_seconds)
        });
    }

    // Send the confirmation request to the server.
    nlohmann::json req;
    req["type"] = "confirm_request";
    req["request_id"] = request_id;
    req["message"] = message;
    req["timeout_seconds"] = timeout_seconds;

    if (!client_->send(req.dump())) {
        LOG_WARN("SubAgentNet", "Failed to send confirm_request. Defaulting to 'n'.");
        // Clean up pending entry.
        std::lock_guard<std::mutex> lock(confirm_mutex_);
        pending_confirms_.erase(request_id);
        return 'n';
    }

    auto status = result.wait_for(std::chrono::seconds(timeout_seconds));
    if (status == std::future_status::ready) {
        return result.get();
    }

    LOG_WARN("SubAgentNet", "Confirmation request timed out. Defaulting to 'n'.");
    // Clean up on timeout.
    std::lock_guard<std::mutex> lock(confirm_mutex_);
    pending_confirms_.erase(request_id);
    return 'n';
}

// ── SubAgentTool ─────────────────────────────────────

SubAgentTool::SubAgentTool(std::shared_ptr<SubAgent> agent) : agent_(std::move(agent)) {
}

std::string SubAgentTool::name() const {
    return agent_->get_name();
}

std::string SubAgentTool::description() const {
    return agent_->description();
}

std::string SubAgentTool::parameters_schema() const {
    return R"({
        "type": "object",
        "properties": {
            "task": {
                "type": "string",
                "description": "The task to execute"
            }
        },
        "required": ["task"]
    })";
}

std::string SubAgentTool::execute(const std::string& json_args) {
    nlohmann::json args = nlohmann::json::parse(json_args);
    std::string task = args.value("task", "");
    return agent_->execute(task);
}

// ── RemoteClientTool ─────────────────────────────────────

RemoteClientTool::RemoteClientTool(std::string name, std::string description, std::string chat_id, SendTaskCallback cb)
    : name_(std::move(name)), description_(std::move(description)), chat_id_(std::move(chat_id)), send_task_cb_(std::move(cb)) {
}

std::string RemoteClientTool::name() const {
    return name_;
}

std::string RemoteClientTool::description() const {
    return description_;
}

std::string RemoteClientTool::parameters_schema() const {
    return R"({
        "type": "object",
        "properties": {
            "task": {
                "type": "string",
                "description": "The task to execute on the remote agent"
            }
        },
        "required": ["task"]
    })";
}

std::string RemoteClientTool::execute(const std::string& json_args) {
    nlohmann::json args = nlohmann::json::parse(json_args);
    std::string task = args.value("task", "");
    return send_task_cb_(chat_id_, task);
}

// ── MultiAgent (Server) ─────────────────────────────

MultiAgent::MultiAgent(ToolRegistry& registry) : registry_(registry)
{
}

bool MultiAgent::is_enable() const {
    return server_ && server_->is_running();
}

void MultiAgent::start(int listen_port) {
    running_.store(true);

    server_ = std::make_unique<httplib::Server>();


    server_->WebSocket("/ws", [this](const httplib::Request& req, httplib::ws::WebSocket& ws) {
        // Generate a unique chat_id for each connection.
        static int conn_counter = 0;
        std::string chat_id = "client_" + std::to_string(++conn_counter);

        // Store the client connection (in-place to avoid copy/move of atomic member).
        {
            std::lock_guard<std::mutex> lock(client_mutex_);
            auto& rc = clients_.try_emplace(chat_id).first->second;
            rc.ws = &ws;
        }

        LOG_INFO("MultiAgent", "New WebSocket connection: " + chat_id);

        // Read loop — handle incoming messages from the client.
        while (running_.load()) {
            std::string msg;
            auto result = ws.read(msg);
            if (result == ws::ReadResult::Text) {
                try {
                    auto j = nlohmann::json::parse(msg);
                    std::string type = j.value("type", "");

                    if (type == "ping") {
                        // Respond to client heartbeat.
                        nlohmann::json pong_msg;
                        pong_msg["type"] = "pong";
                        ws.send(pong_msg.dump());
                    }
                    else if (type == "register") {
                        // Client is registering itself as a remote tool.
                        std::string name = j.value("name", chat_id);
                        std::string description = j.value("description", "Remote agent: " + name);

                        // Register as a Tool in the registry.
                        auto tool = std::make_shared<RemoteClientTool>(
                            name, description, chat_id,
                            [this](const std::string& cid, const std::string& task) {
                                return send_task_to_client(cid, task);
                            }
                        );
                        registry_.register_tool(tool);

                        {
                            std::lock_guard<std::mutex> lock(client_mutex_);
                            clients_[chat_id].name = name;
                            clients_[chat_id].description = description;
                            clients_[chat_id].registered = true;
                        }

                        LOG_INFO("MultiAgent", "Remote client registered as tool: " + name);
                    }
                    else if (type == "result") {
                        // Client sent back a result for a task.
                        std::string cid = j.value("chat_id", chat_id);
                        std::string result_str = j.value("result", "");

                        LOG_INFO("MultiAgent", "Received result from client: " + cid);

                        // Store the result so send_task_to_client can pick it up.
                        {
                            std::lock_guard<std::mutex> lock(client_mutex_);
                            auto it = clients_.find(cid);
                            if (it != clients_.end()) {
                                it->second.pending_result = result_str;
                                it->second.result_ready.store(true);
                            }
                        }
                    }
                    else if (type == "confirm_request") {
                        // Client needs user confirmation — ask on server terminal.
                        std::string request_id = j.value("request_id", "");
                        std::string message = j.value("message", "Confirm?");
                        int timeout_seconds = j.value("timeout_seconds", 60);

                        LOG_INFO("MultiAgent", "Confirmation request from client: " + chat_id);

                        // Handle in a separate thread so we don't block the WebSocket read loop.
                        std::thread([this, &ws, chat_id, request_id, message, timeout_seconds]() {
                            TOUT::append(TUI::AnsiColor_Bright_Red, u8"\n⏸  [Remote Confirm] Client: " + chat_id + "\n");
                            TOUT::append("   " + message + "\n");
                            TOUT::append("   Type 'y' to confirm, anything else to cancel: ");

                            char answer = (TOUT::confirm()) ? 'y' : 'n';

                            nlohmann::json resp;
                            resp["type"] = "confirm_response";
                            resp["request_id"] = request_id;
                            resp["answer"] = std::string(1, answer);

                            ws.send(resp.dump());
                            LOG_INFO("MultiAgent", "Sent confirm_response: " + std::string(1, answer));
                        }).detach();
                    }
                } catch (const std::exception& e) {
                    LOG_WARN("MultiAgent", "Failed to parse message from client: " + chat_id + ": " + std::string(e.what()));
                }
            } else if (result == ws::ReadResult::Fail) {
                LOG_INFO("MultiAgent", "Client disconnected: " + chat_id);
                break;
            }
        }

        // Clean up on disconnect: unregister tool and remove client record.
        {
            std::lock_guard<std::mutex> lock(client_mutex_);
            auto it = clients_.find(chat_id);
            if (it != clients_.end()) {
                if (it->second.registered && !it->second.name.empty()) {
                    registry_.unregister_tool(it->second.name);
                    LOG_INFO("MultiAgent", "Unregistered remote tool: " + it->second.name);
                }
                clients_.erase(it);
            }
        }
    });

    server_thread_ = std::thread([this, listen_port]() {
        if (server_->listen("0.0.0.0", listen_port)) {
            LOG_INFO("MultiAgent", "Server started on port " + std::to_string(listen_port));
        } else {
            LOG_WARN("MultiAgent", "Failed to start server on port " + std::to_string(listen_port));
        }
    });
}

void MultiAgent::stop() {
    running_.store(false);
    if (server_) {
        server_->stop();
    }
    if (server_thread_.joinable()) {
        server_thread_.join();
    }
}

std::string MultiAgent::send_task_to_client(const std::string& chat_id, const std::string& task) {
    // Find the client connection.
    httplib::ws::WebSocket* ws_ptr = nullptr;
    RemoteClient* rc = nullptr;

    {
        std::lock_guard<std::mutex> lock(client_mutex_);
        auto it = clients_.find(chat_id);
        if (it == clients_.end()) {
            return "Error: client not found: " + chat_id;
        }
        ws_ptr = it->second.ws;
        rc = &it->second;
    }

    // Send the task to the client.
    nlohmann::json task_msg;
    task_msg["type"] = "task";
    task_msg["chat_id"] = chat_id;
    task_msg["task"] = task;

    if (!ws_ptr || !ws_ptr->send(task_msg.dump())) {
        return "Error: failed to send task to client: " + chat_id;
    }

    LOG_INFO("MultiAgent", "Sent task to client: " + chat_id);

    // Wait for the result (with timeout).
    auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(300); // 5 min timeout
    while (std::chrono::steady_clock::now() < deadline) {
        {
            std::lock_guard<std::mutex> lock(client_mutex_);
            auto it = clients_.find(chat_id);
            if (it != clients_.end() && it->second.result_ready.load()) {
                return it->second.pending_result;
            }
        }
        std::this_thread::sleep_for(std::chrono::milliseconds(100));
    }

    return "Error: timeout waiting for result from client: " + chat_id;
}

void MultiAgent::register_agent(std::shared_ptr<SubAgent> agent) {
    local_agents_.push_back(agent);
    auto tool = std::make_shared<SubAgentTool>(agent);
    registry_.register_tool(tool);
}

std::vector<std::pair<std::string, std::string>> MultiAgent::get_local_agents() const {
    std::vector<std::pair<std::string, std::string>> result;
    for (const auto& a : local_agents_) {
        result.emplace_back(a->get_name(), a->description());
    }
    return result;
}

std::vector<MultiAgent::RemoteClientInfo> MultiAgent::get_remote_clients() const {
    std::lock_guard<std::mutex> lock(client_mutex_);
    std::vector<RemoteClientInfo> result;
    for (const auto& [cid, rc] : clients_) {
        RemoteClientInfo info;
        info.chat_id = cid;
        info.name = rc.name.empty() ? cid : rc.name;
        info.description = rc.description;
        result.push_back(info);
    }
    return result;
}

} // namespace agent
