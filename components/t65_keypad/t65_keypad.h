#pragma once

#include <array>
#include <cstdint>
#include <vector>

#include "esphome/core/automation.h"
#include "esphome/core/component.h"
#include "esphome/core/hal.h"
#include "t65_keypad_state.h"

#ifdef USE_T65_KEYPAD_EVENT
#include "esphome/components/event/event.h"
#endif

namespace esphome::t65_keypad {

class T65KeyTrigger final : public Trigger<char> {};
class T65KeyReleaseTrigger final : public Trigger<char> {};

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

#ifdef USE_T65_KEYPAD_EVENT
  void set_event(event::Event *event) { this->event_ = event; }
#endif

 protected:
  T65ContactSample read_contacts_() const;
  void process_transition_(const T65Transition &transition);
  void log_stable_state_(const T65Transition &transition) const;
  void idle_if_possible_();

  std::array<InternalGPIOPin *, 4> rows_{};
  std::array<InternalGPIOPin *, 3> columns_{};
  T65InterruptStore interrupt_store_{};
  T65KeypadStateMachine state_machine_{};
  uint32_t debounce_ms_{20};
  uint32_t processed_edge_count_{0};
  std::vector<T65KeyTrigger *> key_triggers_{};
  std::vector<T65KeyReleaseTrigger *> key_release_triggers_{};

#ifdef USE_T65_KEYPAD_EVENT
  event::Event *event_{nullptr};
#endif
};

}  // namespace esphome::t65_keypad
