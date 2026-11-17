#pragma once

#include <array>
#include <cstdint>
#include <string>
#include <vector>

#include "esphome/core/automation.h"
#include "esphome/core/component.h"
#include "esphome/core/hal.h"
#include "esphome/core/helpers.h"
#include "t65_keypad_state.h"

#ifdef USE_T65_KEYPAD_EVENT
#include "esphome/components/event/event.h"
#endif

#ifdef USE_T65_KEYPAD_SENSOR
#include "esphome/components/sensor/sensor.h"
#endif

namespace esphome::t65_keypad {

class T65KeyTrigger final : public Trigger<char> {};
class T65KeyReleaseTrigger final : public Trigger<char> {};
class T65KeyLongPressTrigger final : public Trigger<char> {};
class T65KeyRepeatTrigger final : public Trigger<char> {};
class T65NumbersTrigger final : public Trigger<std::string> {};

struct T65InterruptStore {
  Component *component{nullptr};
  volatile uint32_t edge_count{0};

  static void gpio_intr(T65InterruptStore *arg);
};

class T65Keypad final : public Component {
 public:
  void setup() override;
  void loop() override;
  void dump_config() override;
  float get_setup_priority() const override;

  void set_rows(std::vector<InternalGPIOPin *> rows);
  void set_columns(std::vector<InternalGPIOPin *> columns);
  void set_debounce(uint32_t debounce_ms) {
    this->debounce_ms_ = debounce_ms;
    this->state_machine_.set_debounce(debounce_ms);
  }
  void register_key_trigger(T65KeyTrigger *trigger) { this->key_triggers_.push_back(trigger); }
  void register_key_release_trigger(T65KeyReleaseTrigger *trigger) { this->key_release_triggers_.push_back(trigger); }
  void register_key_long_press_trigger(T65KeyLongPressTrigger *trigger) {
    this->key_long_press_triggers_.push_back(trigger);
  }
  void register_key_repeat_trigger(T65KeyRepeatTrigger *trigger) { this->key_repeat_triggers_.push_back(trigger); }
  void register_numbers_trigger(T65NumbersTrigger *trigger) { this->numbers_triggers_.push_back(trigger); }
  void set_number_timeout(uint32_t timeout_ms) { this->number_timeout_ms_ = timeout_ms; }
  void set_long_press_time(uint32_t long_press_ms) {
    this->long_press_ms_ = long_press_ms;
    this->dialer_.set_long_press_enabled(long_press_ms != 0);
  }
  void set_repeat_time(uint32_t repeat_ms) {
    this->repeat_ms_ = repeat_ms;
    this->dialer_.set_repeat_enabled(repeat_ms != 0);
  }

  // Discards the collected number and any pending on_numbers report.
  void clear_numbers();
  // Keys collected since the last clear, for example "0612345678" or "*21#".
  std::string get_numbers() const { return this->dialer_.number().c_str(); }

#ifdef USE_T65_KEYPAD_EVENT
  void set_event(event::Event *event) { this->event_ = event; }
#endif

#ifdef USE_T65_KEYPAD_SENSOR
  void set_digit_sensor(sensor::Sensor *sensor) { this->digit_sensor_ = sensor; }
#endif

 protected:
  T65ContactSample read_contacts_() const;
  void process_transition_(const T65Transition &transition);
  void log_stable_state_(const T65Transition &transition) const;
  void idle_if_possible_();
  void apply_(const T65DialResult &result);
  void report_numbers_();

  std::array<InternalGPIOPin *, 4> rows_{};
  std::array<InternalGPIOPin *, 3> columns_{};
  T65InterruptStore interrupt_store_{};
  T65KeypadStateMachine state_machine_{};
  uint32_t debounce_ms_{20};
  uint32_t processed_edge_count_{0};
  std::vector<T65KeyTrigger *> key_triggers_{};
  std::vector<T65KeyReleaseTrigger *> key_release_triggers_{};
  std::vector<T65KeyLongPressTrigger *> key_long_press_triggers_{};
  std::vector<T65KeyRepeatTrigger *> key_repeat_triggers_{};
  std::vector<T65NumbersTrigger *> numbers_triggers_{};
  T65Dialer dialer_{};
  uint32_t number_timeout_ms_{3000};
  uint32_t long_press_ms_{0};
  uint32_t repeat_ms_{0};

#ifdef USE_T65_KEYPAD_EVENT
  event::Event *event_{nullptr};
#endif

#ifdef USE_T65_KEYPAD_SENSOR
  sensor::Sensor *digit_sensor_{nullptr};
#endif
};

template<typename... Ts> class T65ClearNumbersAction final : public Action<Ts...>, public Parented<T65Keypad> {
 public:
  void play(const Ts &...x) override { this->parent_->clear_numbers(); }
};

}  // namespace esphome::t65_keypad
