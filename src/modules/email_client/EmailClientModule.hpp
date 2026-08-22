#pragma once

#include <algorithm>
#include <chrono>
#include <functional>
#include <map>
#include <optional>
#include <set>
#include <string>
#include <vector>

namespace material_everything {

enum class EmailProtocol { Imap, Smtp };
enum class EmailFolderKind { Inbox, Sent, Drafts, Spam, Trash, Custom };

struct EmailAccount {
    std::string id;
    std::string display_name;
    std::string address;
    std::string imap_host;
    int imap_port = 993;
    bool imap_ssl = true;
    std::string smtp_host;
    int smtp_port = 587;
    bool smtp_starttls = true;
    bool enabled = true;
};

struct EmailAddress {
    std::string name;
    std::string email;
};

namespace EmailClientModuleHelpers {
std::string lower(const std::string& value);
std::string format_address(const EmailAddress& address);
} // namespace EmailClientModuleHelpers

struct EmailAttachment {
    std::string id;
    std::string filename;
    std::string mime_type;
    std::vector<unsigned char> data;
    size_t size_bytes() const { return data.size(); }
};

struct EmailMessage {
    std::string id;
    std::string account_id;
    std::string folder_id;
    std::vector<EmailAddress> from;
    std::vector<EmailAddress> to;
    std::vector<EmailAddress> cc;
    std::vector<EmailAddress> bcc;
    std::string subject;
    std::string text_body;
    std::string html_body;
    bool prefer_html = true;
    bool read_flag = false;
    bool flagged = false;
    bool answered = false;
    bool forwarded = false;
    bool has_attachments = false;
    std::vector<std::string> attachment_ids;
    std::chrono::system_clock::time_point received_at{};
};

struct EmailFolder {
    std::string id;
    std::string account_id;
    std::string name;
    EmailFolderKind kind = EmailFolderKind::Custom;
    bool system_folder = false;
    int unread_count = 0;
};

struct ContactEntry {
    std::string id;
    std::string name;
    std::string email;
    std::set<std::string> groups;
    int send_count = 0;
};

struct SearchFilter {
    std::optional<std::string> account_id;
    std::optional<std::string> folder_id;
    std::optional<std::string> sender_contains;
    std::optional<std::string> subject_contains;
    std::optional<bool> unread_only;
    std::optional<bool> flagged_only;
    std::optional<bool> has_attachments_only;
    std::chrono::system_clock::time_point since{};
    bool use_since = false;
};

struct ComposeEnvelope {
    std::string account_id;
    std::string draft_id; // empty for new message
    std::vector<EmailAddress> to;
    std::vector<EmailAddress> cc;
    std::vector<EmailAddress> bcc;
    std::string subject;
    std::string text_body;
    std::string html_body;
    std::vector<EmailAttachment> attachments;
};

struct SendResult {
    bool accepted = false;
    std::string protocol_error; // empty on success
    std::string message_id;
};

class EmailTransport {
public:
    virtual ~EmailTransport() = default;
    virtual SendResult send(const EmailAccount& account, const EmailMessage& message) = 0;
};

// Deterministic loopback transport used until a real SMTP/IMAP socket backend is wired.
class LocalEmailTransport : public EmailTransport {
public:
    explicit LocalEmailTransport(std::function<void(const EmailMessage&)> observer = {});
    SendResult send(const EmailAccount& account, const EmailMessage& message) override;
private:
    std::function<void(const EmailMessage&)> m_observer;
};

class EmailClientModule {
public:
    EmailClientModule();

    // Account management
    EmailAccount add_account(const std::string& display_name,
                             const std::string& address,
                             const std::string& imap_host,
                             const std::string& smtp_host);
    void update_account(const EmailAccount& account);
    void remove_account(const std::string& account_id);
    std::vector<EmailAccount> accounts() const;
    const EmailAccount* find_account(const std::string& account_id) const;

    // Folders
    std::vector<EmailFolder> folders_for_account(const std::string& account_id) const;
    EmailFolder create_custom_folder(const std::string& account_id, const std::string& name);
    void rename_folder(const std::string& folder_id, const std::string& new_name);
    void delete_custom_folder(const std::string& folder_id);

    // Messages
    std::vector<EmailMessage> messages_in_folder(const std::string& folder_id) const;
    const EmailMessage* find_message(const std::string& message_id) const;
    void mark_read(const std::string& message_id, bool read_state = true);
    void toggle_flag(const std::string& message_id);
    bool move_message(const std::string& message_id, const std::string& target_folder_id);
    bool delete_message_to_trash(const std::string& message_id);
    void purge_trash();

    // Compose / reply / forward
    EmailMessage create_draft(const ComposeEnvelope& envelope);
    EmailMessage reply_all(const std::string& source_message_id, const std::string& body, bool html);
    EmailMessage forward(const std::string& source_message_id,
                         const std::vector<EmailAddress>& recipients,
                         const std::string& body,
                         bool html,
                         bool include_attachments = true);
    SendResult send_message(const std::string& draft_or_message_id);

    // Attachments
    std::string add_attachment(const std::string& message_id, EmailAttachment attachment);
    const EmailAttachment* get_attachment(const std::string& attachment_id) const;
    bool remove_attachment(const std::string& attachment_id);

    // Rendering + search + filters
    std::string render_html_body(const std::string& message_id) const;
    std::string render_text_body(const std::string& message_id) const;
    std::vector<EmailMessage> search(const std::string& query) const;
    std::vector<EmailMessage> filter(const SearchFilter& filter) const;

    // Address book
    ContactEntry add_contact(const std::string& name, const std::string& email);
    void update_contact(const ContactEntry& contact);
    void remove_contact(const std::string& contact_id);
    std::vector<ContactEntry> contacts() const;
    std::vector<ContactEntry> autocomplete_contacts(const std::string& query) const;

    void set_transport(EmailTransport* transport);

private:
    std::map<std::string, EmailAccount> m_accounts;
    std::map<std::string, EmailFolder> m_folders;
    std::map<std::string, EmailMessage> m_messages;
    std::map<std::string, EmailAttachment> m_attachments;
    std::map<std::string, ContactEntry> m_contacts;
    EmailTransport* m_transport = nullptr;

    static std::string next_id(const char* prefix);
    static std::string lower_copy(const std::string& value);
    static std::string trim_copy(const std::string& value);
    static std::string escape_html(const std::string& value);
    static std::string format_address(const EmailAddress& address);
    static std::string quote_subject(const std::string& subject);
    static std::chrono::system_clock::time_point parse_rfc5322_date(const std::string& raw);
    static std::string encode_header_value(const std::string& raw);
    EmailMessage* mutable_message(const std::string& id);
    void seed_system_folders(const std::string& account_id);
    void recompute_unread(const std::string& folder_id);
    std::string trash_folder_id(const std::string& account_id) const;
};

} // namespace material_everything
