#include "AppState.h"

#include <FFat.h>

namespace paperdesk {

constexpr char AppState::kPhotoPath[];
constexpr char AppState::kPhotoTempPath[];

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
  data_.nextTodoId = 1;
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
           data_.mode <= static_cast<uint8_t>(Mode::kTodo) && data_.todoCount <= kMaxTodos;
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
    Todo todos[kMaxTodos];
  };
  LegacyDataV1 legacy{};
  const size_t read = length == sizeof(legacy)
                          ? preferences_.getBytes("state", &legacy, sizeof(legacy))
                          : 0;
  preferences_.end();
  if (read != sizeof(legacy) || legacy.magic != kMagic || legacy.version != 1 ||
      legacy.mode > static_cast<uint8_t>(Mode::kTodo) || legacy.todoCount > kMaxTodos) {
    return false;
  }

  memset(&data_, 0, sizeof(data_));
  data_.magic = kMagic;
  data_.version = kVersion;
  data_.mode = legacy.mode;
  data_.todoCount = legacy.todoCount;
  data_.nextTodoId = legacy.nextTodoId;
  copyText(data_.badge.name, sizeof(data_.badge.name), legacy.badge.name);
  copyText(data_.badge.role, sizeof(data_.badge.role), legacy.badge.role);
  copyText(data_.badge.contact, sizeof(data_.badge.contact), legacy.badge.contact);
  // Old layout 2 was the asymmetric photo card, which is closest to B.
  data_.badge.layout = legacy.badge.layout == 2 ? 1 : min<uint8_t>(legacy.badge.layout, 1);
  memcpy(data_.todos, legacy.todos, sizeof(legacy.todos));
  return save();
}

bool AppState::save() {
  if (!preferences_.begin("paperdesk_app", false)) return false;
  const size_t written = preferences_.putBytes("state", &data_, sizeof(data_));
  preferences_.end();
  return written == sizeof(data_);
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
  if (text.isEmpty() || text.length() > 96 || data_.todoCount >= kMaxTodos) return false;
  Todo& item = data_.todos[data_.todoCount++];
  memset(&item, 0, sizeof(item));
  item.id = data_.nextTodoId++;
  copyText(item.text, sizeof(item.text), text);
  return save();
}

int AppState::findTodo(uint32_t id) const {
  for (uint8_t i = 0; i < data_.todoCount; ++i) {
    if (data_.todos[i].id == id) return i;
  }
  return -1;
}

bool AppState::deleteTodo(uint32_t id) {
  const int index = findTodo(id);
  if (index < 0) return false;
  for (uint8_t i = index; i + 1 < data_.todoCount; ++i) data_.todos[i] = data_.todos[i + 1];
  --data_.todoCount;
  memset(&data_.todos[data_.todoCount], 0, sizeof(Todo));
  return save();
}

bool AppState::moveTodo(uint32_t id, int direction) {
  const int index = findTodo(id);
  const int target = index + (direction < 0 ? -1 : 1);
  if (index < 0 || target < 0 || target >= data_.todoCount) return false;
  const Todo temporary = data_.todos[index];
  data_.todos[index] = data_.todos[target];
  data_.todos[target] = temporary;
  return save();
}

bool AppState::setTodoCompleted(uint32_t id, bool completed) {
  const int index = findTodo(id);
  if (index < 0) return false;
  data_.todos[index].completed = completed ? 1 : 0;
  return save();
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
