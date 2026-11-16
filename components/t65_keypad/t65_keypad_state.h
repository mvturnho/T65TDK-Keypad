#pragma once

#include <cstdint>

namespace esphome::t65_keypad {

constexpr char T65_KEYS[] = "123456789*0#";

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
