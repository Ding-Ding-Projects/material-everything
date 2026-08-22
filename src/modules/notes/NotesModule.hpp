#pragma once

#include <string>
#include <vector>
#include <map>
#include <set>
#include <chrono>
#include <functional>

namespace material_everything {

struct NoteTag {
    std::string id;
    std::string name;
};

struct NoteChecklistItem {
    std::string text;
    bool checked = false;
};

struct Note {
    std::string id;
    std::string title;
    std::string markdown_body;
    std::string notebook_id;
    std::vector<std::string> tag_ids;
    bool pinned = false;
    bool has_checklist = false;
    std::vector<NoteChecklistItem> checklist_items;
    std::chrono::system_clock::time_point created_at;
    std::chrono::system_clock::time_point updated_at;
};

struct Notebook {
    std::string id;
    std::string name;
    std::string parent_id; // empty = root
};

class NotesModule {
public:
    NotesModule();

    // Note CRUD
    Note create_note(const std::string& notebook_id, const std::string& title);
    void update_note(const Note& note);
    void delete_note(const std::string& note_id);
    const Note* get_note(const std::string& note_id) const;

    // Organization
    Notebook create_notebook(const std::string& name, const std::string& parent_id = "");
    void rename_notebook(const std::string& notebook_id, const std::string& new_name);
    void delete_notebook(const std::string& notebook_id);
    std::vector<Notebook> get_notebooks() const;

    // Tags
    NoteTag create_tag(const std::string& name);
    void assign_tag(const std::string& note_id, const std::string& tag_id);
    void remove_tag(const std::string& note_id, const std::string& tag_id);
    std::vector<NoteTag> get_tags() const;

    // Search
    std::vector<Note> search_notes(const std::string& query) const;
    std::vector<Note> search_by_tag(const std::string& tag_id) const;
    std::vector<Note> notes_in_notebook(const std::string& notebook_id) const;

    // Pinning
    void toggle_pin(const std::string& note_id);

    // Checklist
    void add_checklist_item(const std::string& note_id, const std::string& text);
    void toggle_checklist_item(const std::string& note_id, size_t index);

    // Autosave
    void set_autosave_interval(std::chrono::milliseconds interval);
    void mark_dirty(const std::string& note_id);
    void autosave_now();

private:
    std::map<std::string, Note> m_notes;
    std::map<std::string, Notebook> m_notebooks;
    std::map<std::string, NoteTag> m_tags;
    std::set<std::string> m_dirty_notes;
    std::chrono::milliseconds m_autosave_interval{3000};

    static std::string generate_id();
    void touch(Note& note);
};

} // namespace material_everything
