#pragma once

#include <string>
#include <vector>
#include <memory>
#include <thread>
#include <atomic>
#include <mutex>
#include <condition_variable>
#include <future>
#include "llm_client.h"
#include "tool.h"

namespace agent {

namespace ws = httplib::ws;

/**
 * A specialized sub-agent with a focused system prompt.
 */

class SubAgent {
public:
    SubAgent(const std::string& name, const std::string& description = "");

    const std::string& get_name() const { return name_; }
    const std::string& description() const {return description_; }

    // Execute a task and return the result string.
    std::string execute(const std::string& task);

    // Run a mini reasoning loop (max 5 iterations per sub-agent).
    virtual ChatResponse run_loop(const std::string& task) { return ChatResponse{}; }
protected:
    std::string name_;
    std::string description_;
};

/**
 * A specialized sub-agent with LLM.
 */

class Agent;

class SubAgentLLM: public SubAgent {
public:
    SubAgentLLM(const std::string& name, const std::string& description = "");

    // Set the working directory. This creates an internal Agent that loads
    // zlagent.ini from the given directory and generates a project overview
    // as the tool description for LLM routing.
    void set_workdir(const std::string workdir);

    // Override the system prompt of the internal agent. If empty, the built-in
    // or config-based system prompt is used instead.
    void set_system_prompt(const std::string& prompt);

    ChatResponse run_loop(const std::string& task) override;
private:
    mutable std::unique_ptr<Agent> agent_;
};

/**
 * A specialized sub-agent with network (WebSocket client to remote server).
 * Connects to a MultiAgent server, registers itself as a Tool, and receives tasks.
 */
class SubAgentNet : public SubAgent {
public:
    struct Config {
        bool enabled = false;
        std::string url;                            // e.g. "ws://127.0.0.1:8766/ws"
        std::string workdir;                        // local working directory
        /// How to handle user confirmation requests.
        /// "ask_server" — forward to server terminal (default)
        /// "auto_yes"   — always confirm
        /// "auto_no"    — always cancel
        std::string confirm_mode = "ask_server";
    };

    SubAgentNet(const std::string& name, const std::string& description = "");
    ~SubAgentNet();

    // Start the WebSocket connection and background thread.
    void start(const Config& cfg);

    // Stop the connection and join the thread.
    void stop();

    // Check if the client is currently connected.
    bool is_connected() const { return connected_.load(); }

    /// Ask for user confirmation. Respects confirm_mode setting.
    /// Returns true ("y") or false ("n").
    bool ask_confirm(const std::string& message, int timeout_seconds = 60);

private:
    Config cfg_;
    std::thread ws_thread_;
    std::atomic<bool> running_{false};
    std::atomic<bool> connected_{false};
    mutable std::unique_ptr<ws::WebSocketClient> client_;

    // Background loop: connect, send ping, read messages.
    void connection_loop();

    // Heartbeat thread: sends ping every second, checks for pong within 5 seconds.
    void heartbeat_loop();

    // Handle an incoming JSON message from the server.
    void handle_message(const nlohmann::json& msg);

    /// Send a confirm_request to the server and wait for response.
    /// Returns 'y' or 'n'. Blocks until response or timeout.
    char send_confirm_request(const std::string& message, int timeout_seconds);

    // Pending confirmation requests: request_id -> promise
    struct PendingConfirm {
        std::promise<char> promise;
        std::chrono::steady_clock::time_point deadline;
    };
    std::mutex confirm_mutex_;
    std::map<std::string, PendingConfirm> pending_confirms_;
    int confirm_counter_{0};

    std::thread heartbeat_thread_;
    std::atomic<bool> last_pong_received_{true};
};

/**
 * A specialized sub-agent with CLI
 */
class SubAgentCLI : public SubAgent {
public:
    SubAgentCLI(const std::string& name, const std::string& description = "");

private:

};

/**
 * Wraps a SubAgent as a Tool so it can be registered in the Agent's ToolRegistry.
 * The LLM routes to sub-agents via function calling, just like any other tool.
 */
class SubAgentTool : public Tool {
public:
    explicit SubAgentTool(std::shared_ptr<SubAgent> agent);

    std::string name() const override;
    std::string description() const override;
    std::string parameters_schema() const override;
    std::string execute(const std::string& json_args) override;

private:
    std::shared_ptr<SubAgent> agent_;
};

/**
 * Wraps a remote SubAgentNet client as a Tool. The Server sends tasks via WebSocket.
 */
class RemoteClientTool : public Tool {
public:
    // callback to send task and receive result from the server's connection map
    using SendTaskCallback = std::function<std::string(const std::string& chat_id, const std::string& task)>;

    RemoteClientTool(std::string name, std::string description, std::string chat_id, SendTaskCallback cb);

    std::string name() const override;
    std::string description() const override;
    std::string parameters_schema() const override;
    std::string execute(const std::string& json_args) override;

private:
    std::string name_;
    std::string description_;
    std::string chat_id_;
    SendTaskCallback send_task_cb_;
};

/**
 * MultiAgent coordinates specialized sub-agents to handle different step types.
 * The TaskPlanner assigns steps, and MultiAgent routes each step to the right agent.
 *
 * Server mode: listens on WebSocket for remote SubAgentNet clients,
 * registers them as Tools in the ToolRegistry.
 */
class MultiAgent {
public:
    explicit MultiAgent(ToolRegistry& registry);
    virtual ~MultiAgent() { stop(); }

    bool is_enable() const;

    // Start the WebSocket server on the given port.
    void start(int listen_port);

    // Stop the server and clean up connections.
    void stop();

    // Register a sub-agent and automatically wrap it as a Tool in the Agent's ToolRegistry.
    void register_agent(std::shared_ptr<SubAgent> agent);

    // Get list of registered local sub-agents (name, description).
    std::vector<std::pair<std::string, std::string>> get_local_agents() const;

    // Get list of connected remote clients (chat_id, name, description).
    struct RemoteClientInfo {
        std::string chat_id;
        std::string name;
        std::string description;
    };
    std::vector<RemoteClientInfo> get_remote_clients() const;

private:
    ToolRegistry& registry_;

    mutable std::unique_ptr<httplib::Server> server_;
    std::thread server_thread_;
    std::atomic<bool> running_{false};

    // Track connected remote clients: chat_id -> WebSocket handle + tool info.
    struct RemoteClient {
        httplib::ws::WebSocket* ws = nullptr;
        std::string name;               // registered tool name
        std::string description;
        std::atomic<bool> result_ready{false};
        std::string pending_result;
        bool registered = false;         // whether a RemoteClientTool was registered
    };
    mutable std::mutex client_mutex_;
    std::map<std::string, RemoteClient> clients_;  // chat_id -> client info

    // Track registered local sub-agents for listing.
    mutable std::vector<std::shared_ptr<SubAgent>> local_agents_;

    // Send a task to a remote client and wait for result.
    std::string send_task_to_client(const std::string& chat_id, const std::string& task);
};

} // namespace agent
