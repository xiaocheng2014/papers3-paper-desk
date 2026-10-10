#pragma once

#include <Arduino.h>
#include <FS.h>
#include <Preferences.h>

namespace paperdesk {

class AppState {
 public:
  enum class Mode : uint8_t { kClock = 0, kBadge = 1, kTodo = 2 };

  static constexpr uint8_t kMaxTodos = 12;
  static constexpr uint8_t kMaxTodoTemplates = 6;
  static constexpr uint8_t kMaxTemplateItems = 8;
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

  struct TodoTemplate {
    uint32_t id;
    char name[49];
    uint8_t itemCount;
    char items[kMaxTemplateItems][97];
  };

  bool begin();
  Mode mode() const { return static_cast<Mode>(data_.mode); }
  const Badge& badge() const { return data_.badge; }
  const Todo& todo(uint8_t index) const { return data_.todos[index]; }
  uint8_t todoCount() const { return data_.todoCount; }
  const TodoTemplate& todoTemplate(uint8_t index) const { return templateData_.templates[index]; }
  uint8_t todoTemplateCount() const { return templateData_.templateCount; }
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
  int findTodoTemplate(uint32_t id) const;

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
    uint8_t todoCount;
    uint32_t nextTodoId;
    Badge badge;
    Todo todos[kMaxTodos];
  };

  struct PersistedTemplates {
    uint32_t magic;
    uint16_t version;
    uint8_t templateCount;
    uint32_t nextTemplateId;
    TodoTemplate templates[kMaxTodoTemplates];
  };

  void setDefaults();
  bool load();
  bool save();
  bool loadTemplates();
  bool saveTemplates();
  static void copyText(char* target, size_t capacity, const String& value);

  static constexpr uint32_t kMagic = 0x5044534B;  // PDSK
  static constexpr uint16_t kVersion = 4;
  static constexpr uint32_t kTemplateMagic = 0x544D504C;  // TMPL
  static constexpr uint16_t kTemplateVersion = 1;
  static constexpr char kPhotoPath[] = "/badge.gray";
  static constexpr char kPhotoTempPath[] = "/badge.tmp";
  static constexpr char kTemplatePath[] = "/todo_templates.bin";
  static constexpr char kTemplateTempPath[] = "/todo_templates.tmp";

  Preferences preferences_;
  PersistedData data_{};
  PersistedTemplates templateData_{};
  File photoFile_;
  size_t photoWritten_ = 0;
  bool fileSystemReady_ = false;
  bool photoReady_ = false;
};

}  // namespace paperdesk
