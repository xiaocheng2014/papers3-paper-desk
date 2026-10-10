#include "AppState.h"

#include <FFat.h>
#include <memory>
#include <new>

namespace paperdesk {

constexpr char AppState::kPhotoPath[];
constexpr char AppState::kPhotoTempPath[];
constexpr char AppState::kTemplateDirectory[];
constexpr char AppState::kLegacyTemplatePath[];
constexpr char AppState::kTemplateTempPath[];
constexpr char AppState::kTodoPath[];
constexpr char AppState::kTodoTempPath[];

bool AppState::begin() {
  fileSystemReady_ = FFat.begin(true);
  if (!load()) {
    setDefaults();
    save();
  } else if (mode() == Mode::kClock) {
    // Clock is now selected by physical landscape orientation. Persisted mode
    // only chooses which content the portrait UI opens.
    data_.mode = static_cast<uint8_t>(Mode::kBadge);
    save();
  }
  loadTodos();
  loadTemplates();
  if (fileSystemReady_) {
    File photo = FFat.open(kPhotoPath, FILE_READ);
    photoReady_ = photo && photo.size() == kPhotoBytes;
    photo.close();
  }
  return fileSystemReady_;
}

void AppState::setDefaults() {
  memset(&data_, 0, sizeof(data_));
  data_.magic = kMagic;
  data_.version = kVersion;
  data_.mode = static_cast<uint8_t>(Mode::kBadge);
  todos_.clear();
  nextTodoId_ = 1;
  copyText(data_.badge.name, sizeof(data_.badge.name), "PaperDesk");
  copyText(data_.badge.role, sizeof(data_.badge.role), "Your role");
  copyText(data_.badge.contact, sizeof(data_.badge.contact), "contact@example.com");
}

bool AppState::load() {
  if (!preferences_.begin("paperdesk_app", true)) return false;
  const size_t length = preferences_.getBytesLength("state");
  if (length == sizeof(data_)) {
    const size_t read = preferences_.getBytes("state", &data_, sizeof(data_));
    preferences_.end();
    return read == sizeof(data_) && data_.magic == kMagic && data_.version == kVersion &&
           data_.mode <= static_cast<uint8_t>(Mode::kTodo);
  }

  struct LegacyDataV4 {
    uint32_t magic;
    uint16_t version;
    uint8_t mode;
    uint8_t todoCount;
    uint32_t nextTodoId;
    Badge badge;
    Todo todos[12];
  };
  if (length == sizeof(LegacyDataV4)) {
    LegacyDataV4 legacy{};
    const size_t read = preferences_.getBytes("state", &legacy, sizeof(legacy));
    preferences_.end();
    if (read != sizeof(legacy) || legacy.magic != kMagic ||
        (legacy.version != 2 && legacy.version != 4) ||
        legacy.mode > static_cast<uint8_t>(Mode::kTodo) || legacy.todoCount > 12) return false;
    setDefaults();
    data_.mode = legacy.mode;
    data_.badge = legacy.badge;
    todos_.assign(legacy.todos, legacy.todos + legacy.todoCount);
    nextTodoId_ = legacy.nextTodoId;
    if (fileSystemReady_ && !saveTodos()) return false;
    return save();
  }

  struct LegacyTodoTemplateV3 {
    uint32_t id;
    char name[49];
    uint8_t itemCount;
    char items[8][97];
  };
  struct LegacyDataV3 {
    uint32_t magic;
    uint16_t version;
    uint8_t mode;
    uint8_t todoCount;
    uint32_t nextTodoId;
    Badge badge;
    Todo todos[12];
    uint8_t templateCount;
    uint32_t nextTemplateId;
    LegacyTodoTemplateV3 templates[6];
  };
  if (length == sizeof(LegacyDataV3)) {
    // Allocate the one-time migration buffer on the heap rather than the
    // comparatively small Arduino loop stack, then release it immediately.
    std::unique_ptr<LegacyDataV3> legacyV3(new (std::nothrow) LegacyDataV3{});
    if (!legacyV3) {
      preferences_.end();
      return false;
    }
    const size_t read = preferences_.getBytes("state", legacyV3.get(), sizeof(*legacyV3));
    preferences_.end();
    if (read != sizeof(*legacyV3) || legacyV3->magic != kMagic || legacyV3->version != 3 ||
        legacyV3->mode > static_cast<uint8_t>(Mode::kTodo) ||
        legacyV3->todoCount > 12 || legacyV3->templateCount > 6) return false;
    for (uint8_t i = 0; i < legacyV3->templateCount; ++i) {
      if (legacyV3->templates[i].itemCount > 8) return false;
    }
    setDefaults();
    data_.mode = legacyV3->mode;
    data_.badge = legacyV3->badge;
    todos_.assign(legacyV3->todos, legacyV3->todos + legacyV3->todoCount);
    nextTodoId_ = legacyV3->nextTodoId;
    if (fileSystemReady_) {
      if (!saveTodos()) return false;
      FFat.mkdir(kTemplateDirectory);
      for (uint8_t i = 0; i < legacyV3->templateCount; ++i) {
        String items;
        for (uint8_t j = 0; j < legacyV3->templates[i].itemCount; ++j) {
          if (j) items += '\n';
          items += legacyV3->templates[i].items[j];
        }
        if (!saveTodoTemplate(legacyV3->templates[i].id, legacyV3->templates[i].name, items)) {
          return false;
        }
      }
    }
    return save();
  }

  struct LegacyBadgeV1 {
    char name[49];
    char role[65];
    char contact[97];
    uint8_t layout;
  };
  struct LegacyDataV1 {
    uint32_t magic;
    uint16_t version;
    uint8_t mode;
    uint8_t todoCount;
    uint32_t nextTodoId;
    LegacyBadgeV1 badge;
    Todo todos[12];
  };
  LegacyDataV1 legacy{};
  const size_t read = length == sizeof(legacy)
                          ? preferences_.getBytes("state", &legacy, sizeof(legacy))
                          : 0;
  preferences_.end();
  if (read != sizeof(legacy) || legacy.magic != kMagic || legacy.version != 1 ||
      legacy.mode > static_cast<uint8_t>(Mode::kTodo) || legacy.todoCount > 12) {
    return false;
  }

  memset(&data_, 0, sizeof(data_));
  data_.magic = kMagic;
  data_.version = kVersion;
  data_.mode = legacy.mode;
  todos_.assign(legacy.todos, legacy.todos + legacy.todoCount);
  nextTodoId_ = legacy.nextTodoId;
  copyText(data_.badge.name, sizeof(data_.badge.name), legacy.badge.name);
  copyText(data_.badge.role, sizeof(data_.badge.role), legacy.badge.role);
  copyText(data_.badge.contact, sizeof(data_.badge.contact), legacy.badge.contact);
  // Old layout 2 was the asymmetric photo card, which is closest to B.
  data_.badge.layout = legacy.badge.layout == 2 ? 1 : min<uint8_t>(legacy.badge.layout, 1);
  if (fileSystemReady_ && !saveTodos()) return false;
  return save();
}

bool AppState::save() {
  if (!preferences_.begin("paperdesk_app", false)) return false;
  const size_t written = preferences_.putBytes("state", &data_, sizeof(data_));
  preferences_.end();
  return written == sizeof(data_);
}

bool AppState::loadTodos() {
  if (!fileSystemReady_) return false;
  File file = FFat.open(kTodoPath, FILE_READ);
  if (!file) {
    todos_.clear();
    nextTodoId_ = 1;
    return true;
  }
  TodoFileHeader header{};
  const size_t expectedHeader = file.read(reinterpret_cast<uint8_t*>(&header), sizeof(header));
  const size_t fileSize = file.size();
  const size_t payloadSize = fileSize >= sizeof(header) ? fileSize - sizeof(header) : 0;
  if (expectedHeader != sizeof(header) || header.magic != kTodoMagic ||
      header.version != kTodoVersion || fileSize < sizeof(header) ||
      payloadSize % sizeof(Todo) != 0 || header.count != payloadSize / sizeof(Todo)) {
    file.close();
    return false;
  }
  try {
    todos_.resize(header.count);
  } catch (...) {
    file.close();
    todos_.clear();
    return false;
  }
  const size_t bytes = todos_.size() * sizeof(Todo);
  const bool ok = bytes == 0 ||
                  file.read(reinterpret_cast<uint8_t*>(todos_.data()), bytes) == bytes;
  file.close();
  if (!ok) {
    todos_.clear();
    return false;
  }
  nextTodoId_ = header.nextTodoId == 0 ? 1 : header.nextTodoId;
  return true;
}

bool AppState::saveTodos() {
  if (!fileSystemReady_) return false;
  FFat.remove(kTodoTempPath);
  File file = FFat.open(kTodoTempPath, FILE_WRITE);
  if (!file) return false;
  TodoFileHeader header{kTodoMagic, kTodoVersion, 0,
                        static_cast<uint32_t>(todos_.size()), nextTodoId_};
  bool ok = file.write(reinterpret_cast<const uint8_t*>(&header), sizeof(header)) == sizeof(header);
  const size_t bytes = todos_.size() * sizeof(Todo);
  if (ok && bytes) {
    ok = file.write(reinterpret_cast<const uint8_t*>(todos_.data()), bytes) == bytes;
  }
  file.flush();
  file.close();
  if (!ok) {
    FFat.remove(kTodoTempPath);
    return false;
  }
  FFat.remove(kTodoPath);
  return FFat.rename(kTodoTempPath, kTodoPath);
}

bool AppState::loadTemplates() {
  if (!fileSystemReady_) return false;
  FFat.mkdir(kTemplateDirectory);

  // One-time migration from the previous fixed-size template store.
  File legacyFile = FFat.open(kLegacyTemplatePath, FILE_READ);
  if (!legacyFile) return true;
  struct LegacyTodoTemplate {
    uint32_t id;
    char name[49];
    uint8_t itemCount;
    char items[8][97];
  };
  struct LegacyStore {
    uint32_t magic;
    uint16_t version;
    uint8_t templateCount;
    uint32_t nextTemplateId;
    LegacyTodoTemplate templates[6];
  };
  std::unique_ptr<LegacyStore> legacy(new (std::nothrow) LegacyStore{});
  if (!legacy) {
    legacyFile.close();
    return false;
  }
  const bool valid = legacyFile.size() == sizeof(*legacy) &&
                     legacyFile.read(reinterpret_cast<uint8_t*>(legacy.get()), sizeof(*legacy)) ==
                         sizeof(*legacy) &&
                     legacy->magic == 0x544D504C && legacy->version == 1 &&
                     legacy->templateCount <= 6;
  legacyFile.close();
  if (!valid) return false;
  for (uint8_t i = 0; i < legacy->templateCount; ++i) {
    if (legacy->templates[i].itemCount > 8) return false;
    String items;
    for (uint8_t j = 0; j < legacy->templates[i].itemCount; ++j) {
      if (j) items += '\n';
      items += legacy->templates[i].items[j];
    }
    if (!saveTodoTemplate(legacy->templates[i].id, legacy->templates[i].name, items)) return false;
  }
  FFat.remove(kLegacyTemplatePath);
  return true;
}

bool AppState::setMode(const String& value) {
  Mode selected;
  if (value == "badge") selected = Mode::kBadge;
  else if (value == "todo") selected = Mode::kTodo;
  else return false;
  data_.mode = static_cast<uint8_t>(selected);
  return save();
}

bool AppState::updateBadge(const String& name, const String& role, const String& contact,
                           const String& company, const String& number, int layout) {
  if (name.isEmpty() || name.length() > 48 || role.length() > 64 ||
      contact.length() > 96 || company.length() > 64 || number.length() > 32 ||
      layout < 0 || layout > 5) return false;
  copyText(data_.badge.name, sizeof(data_.badge.name), name);
  copyText(data_.badge.role, sizeof(data_.badge.role), role);
  copyText(data_.badge.contact, sizeof(data_.badge.contact), contact);
  copyText(data_.badge.company, sizeof(data_.badge.company), company);
  copyText(data_.badge.number, sizeof(data_.badge.number), number);
  data_.badge.layout = static_cast<uint8_t>(layout);
  return save();
}

bool AppState::addTodo(const String& text) {
  if (text.isEmpty() || text.length() > 96) return false;
  const size_t previousCount = todos_.size();
  const uint32_t previousNextId = nextTodoId_;
  try {
    todos_.emplace_back();
  } catch (...) {
    return false;
  }
  Todo& item = todos_.back();
  memset(&item, 0, sizeof(item));
  item.id = nextTodoId_++;
  copyText(item.text, sizeof(item.text), text);
  if (saveTodos()) {
    ++todoRevision_;
    return true;
  }
  todos_.resize(previousCount);
  nextTodoId_ = previousNextId;
  return false;
}

int AppState::findTodo(uint32_t id) const {
  for (size_t i = 0; i < todos_.size(); ++i) {
    if (todos_[i].id == id) return static_cast<int>(i);
  }
  return -1;
}

bool AppState::deleteTodo(uint32_t id) {
  const int index = findTodo(id);
  if (index < 0) return false;
  const Todo removed = todos_[index];
  todos_.erase(todos_.begin() + index);
  if (saveTodos()) {
    ++todoRevision_;
    return true;
  }
  todos_.insert(todos_.begin() + index, removed);
  return false;
}

bool AppState::moveTodo(uint32_t id, int direction) {
  const int index = findTodo(id);
  const int target = index + (direction < 0 ? -1 : 1);
  if (index < 0 || target < 0 || target >= static_cast<int>(todos_.size())) return false;
  const Todo temporary = todos_[index];
  todos_[index] = todos_[target];
  todos_[target] = temporary;
  if (saveTodos()) {
    ++todoRevision_;
    return true;
  }
  todos_[target] = todos_[index];
  todos_[index] = temporary;
  return false;
}

bool AppState::setTodoCompleted(uint32_t id, bool completed) {
  const int index = findTodo(id);
  if (index < 0) return false;
  const uint8_t previous = todos_[index].completed;
  todos_[index].completed = completed ? 1 : 0;
  if (saveTodos()) {
    ++todoRevision_;
    return true;
  }
  todos_[index].completed = previous;
  return false;
}

bool AppState::saveTodoTemplate(uint32_t id, const String& name, const String& items) {
  String cleanName = name;
  cleanName.trim();
  if (cleanName.isEmpty() || cleanName.length() > 48) return false;
  if (!fileSystemReady_) return false;
  FFat.mkdir(kTemplateDirectory);
  if (id == 0) id = nextTodoTemplateId();
  if (id == 0) return false;

  FFat.remove(kTemplateTempPath);
  File file = FFat.open(kTemplateTempPath, FILE_WRITE);
  if (!file || !file.println(cleanName)) {
    if (file) file.close();
    FFat.remove(kTemplateTempPath);
    return false;
  }
  size_t itemCount = 0;
  size_t start = 0;
  while (start <= items.length()) {
    const int found = items.indexOf('\n', start);
    String item = found < 0 ? items.substring(start) : items.substring(start, found);
    item.trim();
    if (!item.isEmpty()) {
      if (item.length() > 96 || !file.println(item)) {
        file.close();
        FFat.remove(kTemplateTempPath);
        return false;
      }
      ++itemCount;
    }
    if (found < 0) break;
    start = static_cast<size_t>(found) + 1;
  }
  file.flush();
  file.close();
  if (itemCount == 0) {
    FFat.remove(kTemplateTempPath);
    return false;
  }
  const String target = templatePath(id);
  FFat.remove(target);
  return FFat.rename(kTemplateTempPath, target);
}

bool AppState::deleteTodoTemplate(uint32_t id) {
  if (id == 0 || !fileSystemReady_) return false;
  const String path = templatePath(id);
  return FFat.exists(path) && FFat.remove(path);
}

int AppState::importTodoTemplates(const String& ids) {
  if (!fileSystemReady_) return -1;
  size_t itemTotal = 0;
  size_t selectedCount = 0;
  const size_t previousCount = todos_.size();
  const uint32_t previousNextId = nextTodoId_;
  for (uint8_t pass = 0; pass < 2; ++pass) {
    size_t start = 0;
    while (start <= ids.length()) {
      const int found = ids.indexOf(',', start);
      String value = found < 0 ? ids.substring(start) : ids.substring(start, found);
      value.trim();
      if (!value.isEmpty()) {
        const uint32_t id = strtoul(value.c_str(), nullptr, 10);
        File file = id == 0 ? File() : FFat.open(templatePath(id), FILE_READ);
        if (!file) return -1;
        file.readStringUntil('\n');  // Template name.
        if (pass == 0) ++selectedCount;
        while (file.available()) {
          String item = file.readStringUntil('\n');
          item.trim();
          if (item.isEmpty()) continue;
          if (pass == 0) {
            ++itemTotal;
          } else {
            try {
              todos_.emplace_back();
            } catch (...) {
              file.close();
              todos_.resize(previousCount);
              nextTodoId_ = previousNextId;
              return -1;
            }
            Todo& todo = todos_.back();
            memset(&todo, 0, sizeof(todo));
            todo.id = nextTodoId_++;
            copyText(todo.text, sizeof(todo.text), item);
          }
        }
        file.close();
      }
      if (found < 0) break;
      start = static_cast<size_t>(found) + 1;
    }
    if (selectedCount == 0) return -1;
  }

  if (saveTodos()) {
    ++todoRevision_;
    return static_cast<int>(itemTotal);
  }
  todos_.resize(previousCount);
  nextTodoId_ = previousNextId;
  return -1;
}

String AppState::todosJsonPage(size_t offset, size_t limit, size_t& returned,
                               bool& hasMore) const {
  String body = F("[");
  returned = 0;
  if (offset < todos_.size()) returned = min(limit, todos_.size() - offset);
  hasMore = offset + returned < todos_.size();
  for (size_t i = 0; i < returned; ++i) {
    if (i) body += ',';
    const Todo& item = todos_[offset + i];
    body += F("{\"id\":");
    body += item.id;
    body += F(",\"text\":\"");
    body += jsonEscape(item.text);
    body += F("\",\"completed\":");
    body += item.completed ? F("true") : F("false");
    body += '}';
  }
  body += ']';
  return body;
}

String AppState::todoTemplatesJsonPage(size_t offset, size_t limit, size_t& returned,
                                       bool& hasMore) const {
  String body = F("[");
  returned = 0;
  hasMore = false;
  if (!fileSystemReady_ || limit == 0) {
    body += ']';
    return body;
  }
  File directory = FFat.open(kTemplateDirectory);
  if (!directory || !directory.isDirectory()) {
    if (directory) directory.close();
    body += ']';
    return body;
  }
  size_t seen = 0;
  File file = directory.openNextFile();
  while (file) {
    if (!file.isDirectory()) {
      const uint32_t id = templateIdFromPath(file.name());
      if (id != 0) {
        if (seen++ < offset) {
          file.close();
          file = directory.openNextFile();
          continue;
        }
        if (returned >= limit) {
          hasMore = true;
          file.close();
          break;
        }
        String name = file.readStringUntil('\n');
        name.trim();
        if (returned) body += ',';
        body += F("{\"id\":");
        body += id;
        body += F(",\"name\":\"");
        body += jsonEscape(name);
        body += F("\",\"items\":[");
        bool firstItem = true;
        while (file.available()) {
          String item = file.readStringUntil('\n');
          item.trim();
          if (item.isEmpty()) continue;
          if (!firstItem) body += ',';
          firstItem = false;
          body += '"';
          body += jsonEscape(item);
          body += '"';
        }
        body += F("]}");
        ++returned;
      }
    }
    file.close();
    file = directory.openNextFile();
  }
  directory.close();
  body += ']';
  return body;
}

uint32_t AppState::nextTodoTemplateId() const {
  uint32_t maximum = 0;
  File directory = FFat.open(kTemplateDirectory);
  if (!directory || !directory.isDirectory()) return 1;
  File file = directory.openNextFile();
  while (file) {
    if (!file.isDirectory()) maximum = max(maximum, templateIdFromPath(file.name()));
    file.close();
    file = directory.openNextFile();
  }
  directory.close();
  return maximum == UINT32_MAX ? 0 : maximum + 1;
}

uint32_t AppState::templateIdFromPath(const String& path) {
  const int slash = path.lastIndexOf('/');
  const String name = path.substring(slash + 1);
  if (!name.endsWith(".txt")) return 0;
  const String number = name.substring(0, name.length() - 4);
  if (number.isEmpty()) return 0;
  for (size_t i = 0; i < number.length(); ++i) {
    if (!isDigit(number[i])) return 0;
  }
  return strtoul(number.c_str(), nullptr, 10);
}

String AppState::templatePath(uint32_t id) {
  String path = kTemplateDirectory;
  path += '/';
  path += id;
  path += F(".txt");
  return path;
}

String AppState::jsonEscape(const String& value) {
  String escaped;
  escaped.reserve(value.length() + 8);
  for (size_t i = 0; i < value.length(); ++i) {
    const char character = value[i];
    switch (character) {
      case '"': escaped += F("\\\""); break;
      case '\\': escaped += F("\\\\"); break;
      case '\b': escaped += F("\\b"); break;
      case '\f': escaped += F("\\f"); break;
      case '\n': escaped += F("\\n"); break;
      case '\r': escaped += F("\\r"); break;
      case '\t': escaped += F("\\t"); break;
      default:
        if (static_cast<uint8_t>(character) < 0x20) escaped += ' ';
        else escaped += character;
    }
  }
  return escaped;
}

bool AppState::beginPhotoWrite() {
  if (!fileSystemReady_) return false;
  abortPhotoWrite();
  FFat.remove(kPhotoTempPath);
  photoFile_ = FFat.open(kPhotoTempPath, FILE_WRITE);
  photoWritten_ = 0;
  return static_cast<bool>(photoFile_);
}

bool AppState::appendPhoto(const uint8_t* bytes, size_t length) {
  if (!photoFile_ || photoWritten_ + length > kPhotoBytes) return false;
  const size_t written = photoFile_.write(bytes, length);
  photoWritten_ += written;
  return written == length;
}

bool AppState::finishPhotoWrite() {
  if (!photoFile_) return false;
  photoFile_.flush();
  photoFile_.close();
  if (photoWritten_ != kPhotoBytes) {
    FFat.remove(kPhotoTempPath);
    return false;
  }
  FFat.remove(kPhotoPath);
  photoReady_ = FFat.rename(kPhotoTempPath, kPhotoPath);
  return photoReady_;
}

void AppState::abortPhotoWrite() {
  if (photoFile_) photoFile_.close();
  if (fileSystemReady_) FFat.remove(kPhotoTempPath);
  photoWritten_ = 0;
}

const char* AppState::modeName(Mode mode) {
  switch (mode) {
    case Mode::kClock: return "clock";
    case Mode::kBadge: return "badge";
    case Mode::kTodo: return "todo";
  }
  return "clock";
}

void AppState::copyText(char* target, size_t capacity, const String& value) {
  if (capacity == 0) return;
  const size_t count = min(capacity - 1, value.length());
  memcpy(target, value.c_str(), count);
  target[count] = '\0';
}

}  // namespace paperdesk
