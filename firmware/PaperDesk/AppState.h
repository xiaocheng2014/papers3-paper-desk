#pragma once

#include <Arduino.h>
#include <FS.h>
#include <Preferences.h>
#include <vector>

namespace paperdesk {

class AppState {
 public:
  enum class Mode : uint8_t { kClock = 0, kBadge = 1, kTodo = 2 };

  static constexpr uint16_t kPhotoWidth = 240;
  static constexpr uint16_t kPhotoHeight = 240;
  static constexpr size_t kPhotoBytes = kPhotoWidth * kPhotoHeight;

  struct Badge {
    char name[49];
    char role[65];
    char contact[97];
    uint8_t layout;
    char company[65];
    char number[33];
  };

  struct Todo {
    uint32_t id;
    char text[97];
    uint8_t completed;
  };

  bool begin();
  Mode mode() const { return static_cast<Mode>(data_.mode); }
  const Badge& badge() const { return data_.badge; }
  const Todo& todo(size_t index) const { return todos_[index]; }
  size_t todoCount() const { return todos_.size(); }
  uint32_t todoRevision() const { return todoRevision_; }
  bool hasPhoto() const { return photoReady_; }

  bool setMode(const String& value);
  bool updateBadge(const String& name, const String& role, const String& contact,
                   const String& company, const String& number, int layout);
  bool addTodo(const String& text);
  bool deleteTodo(uint32_t id);
  bool moveTodo(uint32_t id, int direction);
  bool setTodoCompleted(uint32_t id, bool completed);
  int findTodo(uint32_t id) const;
  bool saveTodoTemplate(uint32_t id, const String& name, const String& items);
  bool deleteTodoTemplate(uint32_t id);
  int importTodoTemplates(const String& ids);
  String todosJsonPage(size_t offset, size_t limit, size_t& returned,
                       bool& hasMore) const;
  String todoTemplatesJsonPage(size_t offset, size_t limit, size_t& returned,
                               bool& hasMore) const;

  bool beginPhotoWrite();
  bool appendPhoto(const uint8_t* bytes, size_t length);
  bool finishPhotoWrite();
  void abortPhotoWrite();

  static const char* modeName(Mode mode);

 private:
  struct PersistedData {
    uint32_t magic;
    uint16_t version;
    uint8_t mode;
    Badge badge;
  };

  struct TodoFileHeader {
    uint32_t magic;
    uint16_t version;
    uint16_t reserved;
    uint32_t count;
    uint32_t nextTodoId;
  };

  void setDefaults();
  bool load();
  bool save();
  bool loadTodos();
  bool saveTodos();
  bool loadTemplates();
  uint32_t nextTodoTemplateId() const;
  static uint32_t templateIdFromPath(const String& path);
  static String templatePath(uint32_t id);
  static String jsonEscape(const String& value);
  static void copyText(char* target, size_t capacity, const String& value);

  static constexpr uint32_t kMagic = 0x5044534B;  // PDSK
  static constexpr uint16_t kVersion = 5;
  static constexpr uint32_t kTodoMagic = 0x544F444F;  // TODO
  static constexpr uint16_t kTodoVersion = 1;
  static constexpr char kPhotoPath[] = "/badge.gray";
  static constexpr char kPhotoTempPath[] = "/badge.tmp";
  static constexpr char kTemplateDirectory[] = "/todo_templates";
  static constexpr char kLegacyTemplatePath[] = "/todo_templates.bin";
  static constexpr char kTemplateTempPath[] = "/todo_templates/.write.tmp";
  static constexpr char kTodoPath[] = "/todos.bin";
  static constexpr char kTodoTempPath[] = "/todos.tmp";

  Preferences preferences_;
  PersistedData data_{};
  std::vector<Todo> todos_;
  uint32_t nextTodoId_ = 1;
  uint32_t todoRevision_ = 1;
  File photoFile_;
  size_t photoWritten_ = 0;
  bool fileSystemReady_ = false;
  bool photoReady_ = false;
};

}  // namespace paperdesk
