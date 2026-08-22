#pragma once

#include <chrono>
#include <functional>
#include <memory>
#include <string>
#include <vector>

namespace me::chat {

enum class MessageDirection { Sent, Received };
enum class DeliveryState { Pending, Delivered, Failed };

struct Conversation {
    std::string id;
    std::string title;
    std::string preview;
};

struct ChatMessage {
    std::string id;
    std::string conversation_id;
    std::string body;
    std::vector<std::string> attachment_names;
    MessageDirection direction = MessageDirection::Received;
    DeliveryState delivery = DeliveryState::Pending;
    std::chrono::system_clock::time_point timestamp =
        std::chrono::system_clock::now();
};

class ChatBackend {
public:
    virtual ~ChatBackend() = default;
    using MessageHandler = std::function<void(const ChatMessage&)>;

    virtual void connect() = 0;
    virtual bool send(const ChatMessage& message) = 0;
    virtual void set_message_handler(MessageHandler handler) = 0;
    virtual std::string protocol_name() const = 0;
};

class ChatModule {
public:
    ChatModule();
    ~ChatModule();
    ChatModule(ChatModule&&) noexcept;
    ChatModule& operator=(ChatModule&&) noexcept;

    void set_backend(std::shared_ptr<ChatBackend> backend);
    std::shared_ptr<ChatBackend> backend() const;
    std::string active_protocol() const;

    void add_conversation(Conversation conversation);
    const std::vector<Conversation>& conversations() const;
    const std::vector<ChatMessage>& messages_for(const std::string& conversation_id) const;
    bool select_conversation(const std::string& conversation_id);

    bool send_message(const std::string& conversation_id, const std::string& body,
                      const std::vector<std::string>& attachments = {});
    void receive_local_echo(const std::string& conversation_id, const std::string& body);
    bool is_typing(const std::string& conversation_id) const;
    void set_typing(const std::string& conversation_id, bool typing);

private:
    struct State;
    std::unique_ptr<State> state_;
};

} // namespace me::chat
