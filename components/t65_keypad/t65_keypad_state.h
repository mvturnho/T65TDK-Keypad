#pragma once

#include <array>
#include <cstddef>
#include <cstdint>

namespace esphome::t65_keypad {

constexpr char T65_KEYS[] = "123456789*0#";

// Longest number on_numbers can report. Generous for any phone number plus
// service codes, while keeping the buffer a fixed size without per-key allocation.
constexpr size_t T65_MAX_NUMBER_LENGTH = 32;

// Collects pressed keys into the number reported by on_numbers. Like the
// rotarydial component it replaces, keys keep accumulating after a number has
// been reported; only clear() starts a new number.
class T65NumberBuffer {
 public:
  // Returns false, leaving the buffer unchanged, when it is already full.
  bool append(char key) {
    if (this->length_ >= T65_MAX_NUMBER_LENGTH)
      return false;
    this->keys_[this->length_++] = key;
    this->keys_[this->length_] = '\0';
    return true;
  }

  void clear() {
    this->length_ = 0;
    this->keys_[0] = '\0';
  }

  const char *c_str() const { return this->keys_.data(); }
  size_t size() const { return this->length_; }
  bool empty() const { return this->length_ == 0; }

  // Numeric value of a digit key, or -1 for '*' and '#'.
  static int digit_value(char key) { return (key >= '0' && key <= '9') ? key - '0' : -1; }

 private:
  std::array<char, T65_MAX_NUMBER_LENGTH + 1> keys_{};
  size_t length_{0};
};

// Request for one of the component's two named scheduler timeouts.
enum class T65TimerRequest : uint8_t { KEEP, START, STOP };

// What the component must do after a T65Dialer input.
struct T65DialResult {
  char collected{0};    // Short press added to the number: publish the dial sensor and key event.
  char long_press{0};   // Key held for the long-press time.
  char repeat{0};       // Key still held one repeat time after its long press or previous repeat.
  bool dropped{false};  // The collected key did not fit in the number.
  T65TimerRequest report_timer{T65TimerRequest::KEEP};
  T65TimerRequest long_press_timer{T65TimerRequest::KEEP};
  T65TimerRequest repeat_timer{T65TimerRequest::KEEP};
};

// Turns accepted presses into the collected number and, when enabled, long
// presses.
//
// Without long press every press is collected immediately, exactly as the
// rotarydial component collected each dialed digit. With long press a key is
// classified when it is released (short) or when the long-press time elapses
// (long), and only short presses are collected; this mirrors a rotary dial,
// which also reports a digit only once the dial has returned. A number report
// that is pending while a key is held waits until that key is classified, and
// a long press never repeats a report that has already been made.
//
// With repeat enabled, a key still held after its long press repeats every
// repeat time until it is released or joined by another key. Repeats, like
// long presses, are never collected.
class T65Dialer {
 public:
  void set_long_press_enabled(bool enabled) { this->long_press_enabled_ = enabled; }
  bool long_press_enabled() const { return this->long_press_enabled_; }
  void set_repeat_enabled(bool enabled) { this->repeat_enabled_ = enabled; }
  bool repeat_enabled() const { return this->repeat_enabled_; }

  T65DialResult press(char key) {
    T65DialResult result;
    if (!this->long_press_enabled_) {
      this->collect_(key, result);
      return result;
    }
    this->held_ = key;
    this->long_possible_ = true;
    this->long_pressed_ = false;
    this->repeating_ = false;
    result.long_press_timer = T65TimerRequest::START;
    if (this->report_pending_)
      result.report_timer = T65TimerRequest::STOP;
    return result;
  }

  // The held key was joined by other contacts, so it can no longer become a
  // long press or keep repeating. If it had not become a long press yet, its
  // release still counts as a short press, just as the press would have been
  // collected immediately without long press.
  T65DialResult lockout() {
    T65DialResult result;
    if (this->long_possible_) {
      this->long_possible_ = false;
      result.long_press_timer = T65TimerRequest::STOP;
    }
    if (this->repeating_) {
      this->repeating_ = false;
      result.repeat_timer = T65TimerRequest::STOP;
    }
    return result;
  }

  T65DialResult long_press_elapsed() {
    T65DialResult result;
    if (!this->long_possible_)
      return result;
    this->long_possible_ = false;
    this->long_pressed_ = true;
    result.long_press = this->held_;
    if (this->report_pending_)
      result.report_timer = T65TimerRequest::START;
    if (this->repeat_enabled_) {
      this->repeating_ = true;
      result.repeat_timer = T65TimerRequest::START;
    }
    return result;
  }

  T65DialResult repeat_elapsed() {
    T65DialResult result;
    if (!this->repeating_)
      return result;
    result.repeat = this->held_;
    result.repeat_timer = T65TimerRequest::START;
    return result;
  }

  T65DialResult release(char key) {
    T65DialResult result;
    if (!this->long_press_enabled_ || this->held_ == 0)
      return result;
    if (this->long_possible_)
      result.long_press_timer = T65TimerRequest::STOP;
    if (this->repeating_)
      result.repeat_timer = T65TimerRequest::STOP;
    if (!this->long_pressed_)
      this->collect_(key, result);
    this->held_ = 0;
    this->long_possible_ = false;
    this->long_pressed_ = false;
    this->repeating_ = false;
    return result;
  }

  // The report timeout fired and on_numbers has run.
  void reported() { this->report_pending_ = false; }

  T65DialResult clear() {
    T65DialResult result;
    this->number_.clear();
    this->report_pending_ = false;
    result.report_timer = T65TimerRequest::STOP;
    return result;
  }

  const T65NumberBuffer &number() const { return this->number_; }
  bool report_pending() const { return this->report_pending_; }

 private:
  void collect_(char key, T65DialResult &result) {
    result.collected = key;
    result.dropped = !this->number_.append(key);
    result.report_timer = T65TimerRequest::START;
    this->report_pending_ = true;
  }

  T65NumberBuffer number_{};
  bool long_press_enabled_{false};
  bool repeat_enabled_{false};
  bool report_pending_{false};
  char held_{0};
  bool long_possible_{false};
  bool long_pressed_{false};
  bool repeating_{false};
};

enum class T65State : uint8_t { IDLE, DEBOUNCING, PRESSED, LOCKOUT };

struct T65ContactSample {
  uint8_t rows{0};
  uint8_t columns{0};

  bool operator==(const T65ContactSample &other) const {
    return this->rows == other.rows && this->columns == other.columns;
  }
  bool operator!=(const T65ContactSample &other) const { return !(*this == other); }
};

struct T65Transition {
  char pressed{0};
  char released{0};
  bool stable_changed{false};
  bool entered_lockout{false};
  bool left_lockout{false};
};

class T65KeypadStateMachine {
 public:
  explicit T65KeypadStateMachine(uint32_t debounce_ms = 20) : debounce_ms_(debounce_ms) {}

  void set_debounce(uint32_t debounce_ms) { this->debounce_ms_ = debounce_ms; }

  // A contact active at boot is deliberately suppressed. The machine starts in
  // lockout and becomes armed only after every contact has been stably released.
  void initialize(T65ContactSample sample, uint32_t now) {
    this->candidate_ = sample;
    this->stable_ = sample;
    this->candidate_since_ = now;
    this->debouncing_ = false;
    this->held_key_ = 0;
    this->settled_state_ = is_none_(sample) ? T65State::IDLE : T65State::LOCKOUT;
  }

  // contact_changed must be true whenever an ISR edge occurred, even when the
  // final sampled masks equal the previous candidate. This makes a complete
  // bounce between main-loop snapshots restart the stability interval.
  void observe(T65ContactSample sample, uint32_t now, bool contact_changed = false) {
    if (sample != this->candidate_ || contact_changed) {
      this->candidate_ = sample;
      this->candidate_since_ = now;
      this->debouncing_ = true;
    }
  }

  T65Transition advance(uint32_t now) {
    T65Transition transition;
    if (!this->debouncing_ || static_cast<uint32_t>(now - this->candidate_since_) < this->debounce_ms_)
      return transition;

    this->debouncing_ = false;
    transition.stable_changed = this->stable_ != this->candidate_;
    this->stable_ = this->candidate_;
    this->process_stable_(transition);
    return transition;
  }

  T65State state() const { return this->debouncing_ ? T65State::DEBOUNCING : this->settled_state_; }
  T65State settled_state() const { return this->settled_state_; }
  bool needs_time() const { return this->debouncing_; }
  char held_key() const { return this->held_key_; }
  T65ContactSample stable_sample() const { return this->stable_; }
  T65ContactSample candidate_sample() const { return this->candidate_; }

  static bool has_unprocessed_edges(uint32_t processed, uint32_t observed) { return processed != observed; }

  static char decode(T65ContactSample sample) {
    if (!is_valid_(sample))
      return 0;
    const uint8_t row = bit_index_(sample.rows);
    const uint8_t column = bit_index_(sample.columns);
    return T65_KEYS[row * 3 + column];
  }

 private:
  static bool has_one_bit_(uint8_t value) { return value != 0 && (value & (value - 1)) == 0; }
  static bool is_none_(T65ContactSample sample) { return sample.rows == 0 && sample.columns == 0; }
  static bool is_incomplete_(T65ContactSample sample) {
    return (sample.rows == 0) != (sample.columns == 0);
  }
  static bool is_valid_(T65ContactSample sample) {
    return has_one_bit_(sample.rows) && has_one_bit_(sample.columns);
  }
  static uint8_t bit_index_(uint8_t value) {
    uint8_t index = 0;
    while ((value >>= 1U) != 0)
      index++;
    return index;
  }

  void process_stable_(T65Transition &transition) {
    const bool none = is_none_(this->stable_);
    const bool incomplete = is_incomplete_(this->stable_);
    const char key = decode(this->stable_);

    switch (this->settled_state_) {
      case T65State::IDLE:
        if (key != 0) {
          this->held_key_ = key;
          this->settled_state_ = T65State::PRESSED;
          transition.pressed = key;
        } else if (!none && !incomplete) {
          this->settled_state_ = T65State::LOCKOUT;
          transition.entered_lockout = true;
        }
        break;

      case T65State::PRESSED:
        if (none) {
          transition.released = this->held_key_;
          this->held_key_ = 0;
          this->settled_state_ = T65State::IDLE;
        } else if (key != 0 && key != this->held_key_) {
          this->settled_state_ = T65State::LOCKOUT;
          transition.entered_lockout = true;
        } else if (key == 0 && !incomplete) {
          this->settled_state_ = T65State::LOCKOUT;
          transition.entered_lockout = true;
        }
        // A stable one-sided contact is treated as a held key. This prevents
        // a worn or bouncing contact from causing release/press duplicates.
        break;

      case T65State::LOCKOUT:
        if (none) {
          if (this->held_key_ != 0) {
            transition.released = this->held_key_;
            this->held_key_ = 0;
          }
          this->settled_state_ = T65State::IDLE;
          transition.left_lockout = true;
        }
        break;

      case T65State::DEBOUNCING:
        // DEBOUNCING is a reported transient state, never a settled state.
        break;
    }
  }

  uint32_t debounce_ms_{20};
  uint32_t candidate_since_{0};
  T65ContactSample candidate_{};
  T65ContactSample stable_{};
  T65State settled_state_{T65State::IDLE};
  char held_key_{0};
  bool debouncing_{false};
};

}  // namespace esphome::t65_keypad
