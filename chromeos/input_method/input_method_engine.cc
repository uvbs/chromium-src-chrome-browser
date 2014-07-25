// Copyright 2013 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "chrome/browser/chromeos/input_method/input_method_engine.h"

#undef FocusIn
#undef FocusOut
#undef RootWindow
#include <map>

#include "ash/ime/input_method_menu_item.h"
#include "ash/ime/input_method_menu_manager.h"
#include "ash/shell.h"
#include "base/logging.h"
#include "base/memory/scoped_ptr.h"
#include "base/metrics/histogram.h"
#include "base/strings/string_number_conversions.h"
#include "base/strings/string_util.h"
#include "base/strings/stringprintf.h"
#include "base/strings/utf_string_conversions.h"
#include "chromeos/ime/component_extension_ime_manager.h"
#include "chromeos/ime/composition_text.h"
#include "chromeos/ime/extension_ime_util.h"
#include "chromeos/ime/input_method_manager.h"
#include "ui/aura/window.h"
#include "ui/aura/window_tree_host.h"
#include "ui/base/ime/candidate_window.h"
#include "ui/base/ime/chromeos/ime_keymap.h"
#include "ui/events/event.h"
#include "ui/events/event_processor.h"
#include "ui/keyboard/keyboard_controller.h"
#include "ui/keyboard/keyboard_util.h"

namespace chromeos {
const char* kErrorNotActive = "IME is not active";
const char* kErrorWrongContext = "Context is not active";
const char* kCandidateNotFound = "Candidate not found";

namespace {

// Notifies InputContextHandler that the composition is changed.
void UpdateComposition(const CompositionText& composition_text,
                       uint32 cursor_pos,
                       bool is_visible) {
  IMEInputContextHandlerInterface* input_context =
      IMEBridge::Get()->GetInputContextHandler();
  if (input_context)
    input_context->UpdateCompositionText(
        composition_text, cursor_pos, is_visible);
}

// Returns the length of characters of a UTF-8 string with unknown string
// length. Cannot apply faster algorithm to count characters in an utf-8
// string without knowing the string length,  so just does a full scan.
size_t GetUtf8StringLength(const char* s) {
  size_t ret = 0;
  while (*s) {
    if ((*s & 0xC0) != 0x80)
      ret++;
    ++s;
  }
  return ret;
}

std::string GetKeyFromEvent(const ui::KeyEvent& event) {
  const std::string& code = event.code();
  if (StartsWithASCII(code, "Control", true))
    return "Ctrl";
  if (StartsWithASCII(code, "Shift", true))
    return "Shift";
  if (StartsWithASCII(code, "Alt", true))
    return "Alt";
  if (StartsWithASCII(code, "Arrow", true))
    return code.substr(5);
  if (code == "Escape")
    return "Esc";
  if (code == "Backspace" || code == "Tab" ||
      code == "Enter" || code == "CapsLock")
    return code;
  uint16 ch = 0;
  // Ctrl+? cases, gets key value for Ctrl is not down.
  if (event.flags() & ui::EF_CONTROL_DOWN) {
    ui::KeyEvent event_no_ctrl(event.type(),
                               event.key_code(),
                               event.flags() ^ ui::EF_CONTROL_DOWN);
    ch = event_no_ctrl.GetCharacter();
  } else {
    ch = event.GetCharacter();
  }
  return base::UTF16ToUTF8(base::string16(1, ch));
}

void GetExtensionKeyboardEventFromKeyEvent(
    const ui::KeyEvent& event,
    InputMethodEngine::KeyboardEvent* ext_event) {
  DCHECK(event.type() == ui::ET_KEY_RELEASED ||
         event.type() == ui::ET_KEY_PRESSED);
  DCHECK(ext_event);
  ext_event->type = (event.type() == ui::ET_KEY_RELEASED) ? "keyup" : "keydown";

  ext_event->code = event.code();
  ext_event->key_code = static_cast<int>(event.key_code());
  ext_event->alt_key = event.IsAltDown();
  ext_event->ctrl_key = event.IsControlDown();
  ext_event->shift_key = event.IsShiftDown();
  ext_event->caps_lock = event.IsCapsLockDown();
  ext_event->key = GetKeyFromEvent(event);
}

}  // namespace

InputMethodEngine::InputMethodEngine()
    : current_input_type_(ui::TEXT_INPUT_TYPE_NONE),
      active_(false),
      context_id_(0),
      next_context_id_(1),
      composition_text_(new CompositionText()),
      composition_cursor_(0),
      candidate_window_(new ui::CandidateWindow()),
      window_visible_(false),
      sent_key_event_(NULL) {}

InputMethodEngine::~InputMethodEngine() {
  if (start_time_.ToInternalValue())
    RecordHistogram("WorkingTime", (end_time_ - start_time_).InSeconds());
  input_method::InputMethodManager::Get()->RemoveInputMethodExtension(imm_id_);
}

void InputMethodEngine::Initialize(
    scoped_ptr<InputMethodEngineInterface::Observer> observer,
    const char* engine_name,
    const char* extension_id,
    const char* engine_id,
    const std::vector<std::string>& languages,
    const std::vector<std::string>& layouts,
    const GURL& options_page,
    const GURL& input_view) {
  DCHECK(observer) << "Observer must not be null.";

  // TODO(komatsu): It is probably better to set observer out of Initialize.
  observer_ = observer.Pass();
  engine_id_ = engine_id;
  extension_id_ = extension_id;

  input_method::InputMethodManager* manager =
      input_method::InputMethodManager::Get();
  ComponentExtensionIMEManager* comp_ext_ime_manager =
      manager->GetComponentExtensionIMEManager();

  if (comp_ext_ime_manager && comp_ext_ime_manager->IsInitialized() &&
      comp_ext_ime_manager->IsWhitelistedExtension(extension_id)) {
    imm_id_ = comp_ext_ime_manager->GetId(extension_id, engine_id);
  } else {
    imm_id_ = extension_ime_util::GetInputMethodID(extension_id, engine_id);
  }

  input_view_url_ = input_view;
  descriptor_ = input_method::InputMethodDescriptor(
      imm_id_,
      engine_name,
      std::string(), // TODO(uekawa): Set short name.
      layouts,
      languages,
      extension_ime_util::IsKeyboardLayoutExtension(
          imm_id_), // is_login_keyboard
      options_page,
      input_view);

  // TODO(komatsu): It is probably better to call AddInputMethodExtension
  // out of Initialize.
  manager->AddInputMethodExtension(imm_id_, this);
}

const input_method::InputMethodDescriptor& InputMethodEngine::GetDescriptor()
    const {
  return descriptor_;
}

void InputMethodEngine::RecordHistogram(const char* name, int count) {
  std::string histo_name =
      base::StringPrintf("InputMethod.%s.%s", name, engine_id_.c_str());
  base::HistogramBase* counter = base::Histogram::FactoryGet(
      histo_name, 0, 1000000, 50, base::HistogramBase::kNoFlags);
  if (counter)
    counter->Add(count);
}

void InputMethodEngine::NotifyImeReady() {
  input_method::InputMethodManager* manager =
      input_method::InputMethodManager::Get();
  if (manager && imm_id_ == manager->GetCurrentInputMethod().id())
    Enable();
}

bool InputMethodEngine::SetComposition(
    int context_id,
    const char* text,
    int selection_start,
    int selection_end,
    int cursor,
    const std::vector<SegmentInfo>& segments,
    std::string* error) {
  if (!active_) {
    *error = kErrorNotActive;
    return false;
  }
  if (context_id != context_id_ || context_id_ == -1) {
    *error = kErrorWrongContext;
    return false;
  }

  composition_cursor_ = cursor;
  composition_text_.reset(new CompositionText());
  composition_text_->set_text(base::UTF8ToUTF16(text));

  composition_text_->set_selection_start(selection_start);
  composition_text_->set_selection_end(selection_end);

  // TODO: Add support for displaying selected text in the composition string.
  for (std::vector<SegmentInfo>::const_iterator segment = segments.begin();
       segment != segments.end(); ++segment) {
    CompositionText::UnderlineAttribute underline;

    switch (segment->style) {
      case SEGMENT_STYLE_UNDERLINE:
        underline.type = CompositionText::COMPOSITION_TEXT_UNDERLINE_SINGLE;
        break;
      case SEGMENT_STYLE_DOUBLE_UNDERLINE:
        underline.type = CompositionText::COMPOSITION_TEXT_UNDERLINE_DOUBLE;
        break;
      default:
        continue;
    }

    underline.start_index = segment->start;
    underline.end_index = segment->end;
    composition_text_->mutable_underline_attributes()->push_back(underline);
  }

  // TODO(nona): Makes focus out mode configuable, if necessary.
  UpdateComposition(*composition_text_, composition_cursor_, true);
  return true;
}

bool InputMethodEngine::ClearComposition(int context_id,
                                         std::string* error)  {
  if (!active_) {
    *error = kErrorNotActive;
    return false;
  }
  if (context_id != context_id_ || context_id_ == -1) {
    *error = kErrorWrongContext;
    return false;
  }

  composition_cursor_ = 0;
  composition_text_.reset(new CompositionText());
  UpdateComposition(*composition_text_, composition_cursor_, false);
  return true;
}

bool InputMethodEngine::CommitText(int context_id, const char* text,
                                   std::string* error) {
  if (!active_) {
    // TODO: Commit the text anyways.
    *error = kErrorNotActive;
    return false;
  }
  if (context_id != context_id_ || context_id_ == -1) {
    *error = kErrorWrongContext;
    return false;
  }

  IMEBridge::Get()->GetInputContextHandler()->CommitText(text);

  // Records times for using input method.
  if (!start_time_.ToInternalValue())
    start_time_ = base::Time::Now();
  end_time_ = base::Time::Now();
  // Records histograms for counts of commits and committed characters.
  RecordHistogram("Commit", 1);
  RecordHistogram("CommitCharacter", GetUtf8StringLength(text));
  return true;
}

bool InputMethodEngine::SendKeyEvents(
    int context_id,
    const std::vector<KeyboardEvent>& events) {
  if (!active_) {
    return false;
  }
  // context_id  ==  0, means sending key events to non-input field.
  // context_id_ == -1, means the focus is not in an input field.
  if (context_id != 0 && (context_id != context_id_ || context_id_ == -1)) {
    return false;
  }

  ui::EventProcessor* dispatcher =
      ash::Shell::GetPrimaryRootWindow()->GetHost()->event_processor();

  for (size_t i = 0; i < events.size(); ++i) {
    const KeyboardEvent& event = events[i];
    const ui::EventType type =
        (event.type == "keyup") ? ui::ET_KEY_RELEASED : ui::ET_KEY_PRESSED;
    ui::KeyboardCode key_code = static_cast<ui::KeyboardCode>(event.key_code);
    if (key_code == ui::VKEY_UNKNOWN)
      key_code = ui::DomKeycodeToKeyboardCode(event.code);

    int flags = ui::EF_NONE;
    flags |= event.alt_key   ? ui::EF_ALT_DOWN       : ui::EF_NONE;
    flags |= event.ctrl_key  ? ui::EF_CONTROL_DOWN   : ui::EF_NONE;
    flags |= event.shift_key ? ui::EF_SHIFT_DOWN     : ui::EF_NONE;
    flags |= event.caps_lock ? ui::EF_CAPS_LOCK_DOWN : ui::EF_NONE;

    ui::KeyEvent ui_event(type,
                          key_code,
                          event.code,
                          flags);
    if (!event.key.empty())
      ui_event.set_character(base::UTF8ToUTF16(event.key)[0]);
    base::AutoReset<const ui::KeyEvent*> reset_sent_key(&sent_key_event_,
                                                        &ui_event);
    ui::EventDispatchDetails details = dispatcher->OnEventFromSource(&ui_event);
    if (details.dispatcher_destroyed)
      break;
  }
  return true;
}

const InputMethodEngine::CandidateWindowProperty&
InputMethodEngine::GetCandidateWindowProperty() const {
  return candidate_window_property_;
}

void InputMethodEngine::SetCandidateWindowProperty(
    const CandidateWindowProperty& property) {
  // Type conversion from InputMethodEngineInterface::CandidateWindowProperty to
  // CandidateWindow::CandidateWindowProperty defined in chromeos/ime/.
  ui::CandidateWindow::CandidateWindowProperty dest_property;
  dest_property.page_size = property.page_size;
  dest_property.is_cursor_visible = property.is_cursor_visible;
  dest_property.is_vertical = property.is_vertical;
  dest_property.show_window_at_composition =
      property.show_window_at_composition;
  dest_property.cursor_position =
      candidate_window_->GetProperty().cursor_position;
  dest_property.auxiliary_text = property.auxiliary_text;
  dest_property.is_auxiliary_text_visible = property.is_auxiliary_text_visible;

  candidate_window_->SetProperty(dest_property);
  candidate_window_property_ = property;

  if (active_) {
    IMECandidateWindowHandlerInterface* cw_handler =
        IMEBridge::Get()->GetCandidateWindowHandler();
    if (cw_handler)
      cw_handler->UpdateLookupTable(*candidate_window_, window_visible_);
  }
}

bool InputMethodEngine::SetCandidateWindowVisible(bool visible,
                                                  std::string* error) {
  if (!active_) {
    *error = kErrorNotActive;
    return false;
  }

  window_visible_ = visible;
  IMECandidateWindowHandlerInterface* cw_handler =
      IMEBridge::Get()->GetCandidateWindowHandler();
  if (cw_handler)
    cw_handler->UpdateLookupTable(*candidate_window_, window_visible_);
  return true;
}

bool InputMethodEngine::SetCandidates(
    int context_id,
    const std::vector<Candidate>& candidates,
    std::string* error) {
  if (!active_) {
    *error = kErrorNotActive;
    return false;
  }
  if (context_id != context_id_ || context_id_ == -1) {
    *error = kErrorWrongContext;
    return false;
  }

  // TODO: Nested candidates
  candidate_ids_.clear();
  candidate_indexes_.clear();
  candidate_window_->mutable_candidates()->clear();
  for (std::vector<Candidate>::const_iterator ix = candidates.begin();
       ix != candidates.end(); ++ix) {
    ui::CandidateWindow::Entry entry;
    entry.value = base::UTF8ToUTF16(ix->value);
    entry.label = base::UTF8ToUTF16(ix->label);
    entry.annotation = base::UTF8ToUTF16(ix->annotation);
    entry.description_title = base::UTF8ToUTF16(ix->usage.title);
    entry.description_body = base::UTF8ToUTF16(ix->usage.body);

    // Store a mapping from the user defined ID to the candidate index.
    candidate_indexes_[ix->id] = candidate_ids_.size();
    candidate_ids_.push_back(ix->id);

    candidate_window_->mutable_candidates()->push_back(entry);
  }
  if (active_) {
    IMECandidateWindowHandlerInterface* cw_handler =
        IMEBridge::Get()->GetCandidateWindowHandler();
    if (cw_handler)
      cw_handler->UpdateLookupTable(*candidate_window_, window_visible_);
  }
  return true;
}

bool InputMethodEngine::SetCursorPosition(int context_id, int candidate_id,
                                          std::string* error) {
  if (!active_) {
    *error = kErrorNotActive;
    return false;
  }
  if (context_id != context_id_ || context_id_ == -1) {
    *error = kErrorWrongContext;
    return false;
  }

  std::map<int, int>::const_iterator position =
      candidate_indexes_.find(candidate_id);
  if (position == candidate_indexes_.end()) {
    *error = kCandidateNotFound;
    return false;
  }

  candidate_window_->set_cursor_position(position->second);
  IMECandidateWindowHandlerInterface* cw_handler =
      IMEBridge::Get()->GetCandidateWindowHandler();
  if (cw_handler)
    cw_handler->UpdateLookupTable(*candidate_window_, window_visible_);
  return true;
}

bool InputMethodEngine::SetMenuItems(const std::vector<MenuItem>& items) {
  return UpdateMenuItems(items);
}

bool InputMethodEngine::UpdateMenuItems(
    const std::vector<MenuItem>& items) {
  if (!active_)
    return false;

  ash::ime::InputMethodMenuItemList menu_item_list;
  for (std::vector<MenuItem>::const_iterator item = items.begin();
       item != items.end(); ++item) {
    ash::ime::InputMethodMenuItem property;
    MenuItemToProperty(*item, &property);
    menu_item_list.push_back(property);
  }

  ash::ime::InputMethodMenuManager::GetInstance()->
      SetCurrentInputMethodMenuItemList(
          menu_item_list);
  return true;
}

bool InputMethodEngine::IsActive() const {
  return active_;
}

bool InputMethodEngine::DeleteSurroundingText(int context_id,
                                              int offset,
                                              size_t number_of_chars,
                                              std::string* error) {
  if (!active_) {
    *error = kErrorNotActive;
    return false;
  }
  if (context_id != context_id_ || context_id_ == -1) {
    *error = kErrorWrongContext;
    return false;
  }

  if (offset < 0 && static_cast<size_t>(-1 * offset) != size_t(number_of_chars))
    return false;  // Currently we can only support preceding text.

  // TODO(nona): Return false if there is ongoing composition.

  IMEInputContextHandlerInterface* input_context =
      IMEBridge::Get()->GetInputContextHandler();
  if (input_context)
    input_context->DeleteSurroundingText(offset, number_of_chars);

  return true;
}

void InputMethodEngine::HideInputView() {
  keyboard::KeyboardController* keyboard_controller =
    keyboard::KeyboardController::GetInstance();
  if (keyboard_controller) {
    keyboard_controller->HideKeyboard(
        keyboard::KeyboardController::HIDE_REASON_MANUAL);
  }
}

void InputMethodEngine::EnableInputView(bool enabled) {
  const GURL& url = enabled ? input_view_url_ : GURL();
  keyboard::SetOverrideContentUrl(url);
  keyboard::KeyboardController* keyboard_controller =
      keyboard::KeyboardController::GetInstance();
  if (keyboard_controller)
    keyboard_controller->Reload();
}

void InputMethodEngine::FocusIn(
    const IMEEngineHandlerInterface::InputContext& input_context) {
  current_input_type_ = input_context.type;

  if (!active_ || current_input_type_ == ui::TEXT_INPUT_TYPE_NONE)
    return;

  context_id_ = next_context_id_;
  ++next_context_id_;

  InputMethodEngineInterface::InputContext context;
  context.id = context_id_;
  switch (current_input_type_) {
    case ui::TEXT_INPUT_TYPE_SEARCH:
      context.type = "search";
      break;
    case ui::TEXT_INPUT_TYPE_TELEPHONE:
      context.type = "tel";
      break;
    case ui::TEXT_INPUT_TYPE_URL:
      context.type = "url";
      break;
    case ui::TEXT_INPUT_TYPE_EMAIL:
      context.type = "email";
      break;
    case ui::TEXT_INPUT_TYPE_NUMBER:
      context.type = "number";
      break;
    case ui::TEXT_INPUT_TYPE_PASSWORD:
      context.type = "password";
      break;
    default:
      context.type = "text";
      break;
  }

  observer_->OnFocus(context);
}

void InputMethodEngine::FocusOut() {
  if (!active_ || current_input_type_ == ui::TEXT_INPUT_TYPE_NONE)
    return;

  current_input_type_ = ui::TEXT_INPUT_TYPE_NONE;

  int context_id = context_id_;
  context_id_ = -1;
  observer_->OnBlur(context_id);
}

void InputMethodEngine::Enable() {
  active_ = true;
  observer_->OnActivate(engine_id_);
  current_input_type_ = IMEBridge::Get()->GetCurrentTextInputType();
  FocusIn(IMEEngineHandlerInterface::InputContext(
      current_input_type_, ui::TEXT_INPUT_MODE_DEFAULT));
  EnableInputView(true);

  start_time_ = base::Time();
  end_time_ = base::Time();
  RecordHistogram("Enable", 1);
}

void InputMethodEngine::Disable() {
  active_ = false;
  observer_->OnDeactivated(engine_id_);

  if (start_time_.ToInternalValue())
    RecordHistogram("WorkingTime", (end_time_ - start_time_).InSeconds());
}

void InputMethodEngine::PropertyActivate(const std::string& property_name) {
  observer_->OnMenuItemActivated(engine_id_, property_name);
}

void InputMethodEngine::Reset() {
  observer_->OnReset(engine_id_);
}

void InputMethodEngine::ProcessKeyEvent(
    const ui::KeyEvent& key_event,
    const KeyEventDoneCallback& callback) {

  KeyEventDoneCallback *handler = new KeyEventDoneCallback();
  *handler = callback;

  KeyboardEvent ext_event;
  GetExtensionKeyboardEventFromKeyEvent(key_event, &ext_event);

  // If the given key event is equal to the key event sent by
  // SendKeyEvents, this engine ID is propagated to the extension IME.
  // Note, this check relies on that ui::KeyEvent is propagated as
  // reference without copying.
  if (&key_event == sent_key_event_)
    ext_event.extension_id = extension_id_;

  observer_->OnKeyEvent(
      engine_id_,
      ext_event,
      reinterpret_cast<input_method::KeyEventHandle*>(handler));
}

void InputMethodEngine::CandidateClicked(uint32 index) {
  if (index > candidate_ids_.size()) {
    return;
  }

  // Only left button click is supported at this moment.
  observer_->OnCandidateClicked(
      engine_id_, candidate_ids_.at(index), MOUSE_BUTTON_LEFT);
}

void InputMethodEngine::SetSurroundingText(const std::string& text,
                                           uint32 cursor_pos,
                                           uint32 anchor_pos) {
  observer_->OnSurroundingTextChanged(engine_id_,
                                      text,
                                      static_cast<int>(cursor_pos),
                                      static_cast<int>(anchor_pos));
}

// TODO(uekawa): rename this method to a more reasonable name.
void InputMethodEngine::MenuItemToProperty(
    const MenuItem& item,
    ash::ime::InputMethodMenuItem* property) {
  property->key = item.id;

  if (item.modified & MENU_ITEM_MODIFIED_LABEL) {
    property->label = item.label;
  }
  if (item.modified & MENU_ITEM_MODIFIED_VISIBLE) {
    // TODO(nona): Implement it.
  }
  if (item.modified & MENU_ITEM_MODIFIED_CHECKED) {
    property->is_selection_item_checked = item.checked;
  }
  if (item.modified & MENU_ITEM_MODIFIED_ENABLED) {
    // TODO(nona): implement sensitive entry(crbug.com/140192).
  }
  if (item.modified & MENU_ITEM_MODIFIED_STYLE) {
    if (!item.children.empty()) {
      // TODO(nona): Implement it.
    } else {
      switch (item.style) {
        case MENU_ITEM_STYLE_NONE:
          NOTREACHED();
          break;
        case MENU_ITEM_STYLE_CHECK:
          // TODO(nona): Implement it.
          break;
        case MENU_ITEM_STYLE_RADIO:
          property->is_selection_item = true;
          break;
        case MENU_ITEM_STYLE_SEPARATOR:
          // TODO(nona): Implement it.
          break;
      }
    }
  }

  // TODO(nona): Support item.children.
}

}  // namespace chromeos
