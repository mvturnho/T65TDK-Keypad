#include "t65_keypad.h"

#include <cinttypes>

#include "esphome/core/application.h"
#include "esphome/core/log.h"

namespace esphome::t65_keypad {

static const char *const TAG = "t65_keypad";

#ifdef USE_T65_KEYPAD_EVENT
static const char *const EVENT_TYPES[] = {"key_1", "key_2", "key_3", "key_4",    "key_5", "key_6",
                                          "key_7", "key_8", "key_9", "key_star", "key_0", "key_hash"};
#endif

void IRAM_ATTR T65InterruptStore::gpio_intr(T65InterruptStore *arg) {
  arg->edge_count++;
  if (arg->component != nullptr)
    arg->component->enable_loop_soon_any_context();
}

void T65Keypad::set_rows(std::vector<InternalGPIOPin *> rows) {
  for (size_t i = 0; i < this->rows_.size(); i++)
    this->rows_[i] = rows[i];
}

void T65Keypad::set_columns(std::vector<InternalGPIOPin *> columns) {
  for (size_t i = 0; i < this->columns_.size(); i++)
    this->columns_[i] = columns[i];
}

void T65Keypad::setup() {
  for (auto *pin : this->rows_)
    pin->setup();
  for (auto *pin : this->columns_)
    pin->setup();

  this->interrupt_store_.component = this;
  for (auto *pin : this->rows_)
    pin->attach_interrupt(&T65InterruptStore::gpio_intr, &this->interrupt_store_, gpio::INTERRUPT_ANY_EDGE);
  for (auto *pin : this->columns_)
    pin->attach_interrupt(&T65InterruptStore::gpio_intr, &this->interrupt_store_, gpio::INTERRUPT_ANY_EDGE);

  // Interrupts are attached before the initial snapshot so a transition in the
  // setup window is either represented by the snapshot or by edge_count.
  this->processed_edge_count_ = this->interrupt_store_.edge_count;
  const T65ContactSample initial = this->read_contacts_();
  this->state_machine_.initialize(initial, millis());
  ESP_LOGD(TAG, "Initial rows=0x%X columns=0x%X; boot contacts are %s", initial.rows, initial.columns,
           (initial.rows == 0 && initial.columns == 0) ? "clear" : "locked out until release");
}

void T65Keypad::loop() {
  const uint32_t now = App.get_loop_component_start_time();
  const uint32_t observed_edges = this->interrupt_store_.edge_count;

  if (T65KeypadStateMachine::has_unprocessed_edges(this->processed_edge_count_, observed_edges)) {
    // Record the counter value from before the GPIO snapshot. If another edge
    // occurs during the seven reads, it remains unprocessed and forces another
    // snapshot on the next loop pass.
    this->processed_edge_count_ = observed_edges;
    const T65ContactSample sample = this->read_contacts_();
    this->state_machine_.observe(sample, now, true);
    ESP_LOGVV(TAG, "Edge snapshot rows=0x%X columns=0x%X edges=%" PRIu32, sample.rows, sample.columns,
              observed_edges);
  }

  const T65Transition transition = this->state_machine_.advance(now);
  this->log_stable_state_(transition);
  this->process_transition_(transition);
  this->idle_if_possible_();
}

T65ContactSample T65Keypad::read_contacts_() const {
  T65ContactSample sample;
  for (size_t i = 0; i < this->rows_.size(); i++) {
    if (!this->rows_[i]->digital_read())
      sample.rows |= 1U << i;
  }
  for (size_t i = 0; i < this->columns_.size(); i++) {
    if (!this->columns_[i]->digital_read())
      sample.columns |= 1U << i;
  }
  return sample;
}

void T65Keypad::process_transition_(const T65Transition &transition) {
  if (transition.pressed != 0) {
    ESP_LOGI(TAG, "Key pressed: %c", transition.pressed);
    for (auto *trigger : this->key_triggers_)
      trigger->trigger(transition.pressed);

#ifdef USE_T65_KEYPAD_EVENT
    if (this->event_ != nullptr) {
      const char *event_type = nullptr;
      for (size_t i = 0; i < sizeof(T65_KEYS) - 1; i++) {
        if (T65_KEYS[i] == transition.pressed) {
          event_type = EVENT_TYPES[i];
          break;
        }
      }
      if (event_type != nullptr)
        this->event_->trigger(event_type);
    }
#endif
  }

  if (transition.released != 0) {
    ESP_LOGI(TAG, "Key released: %c", transition.released);
    for (auto *trigger : this->key_release_triggers_)
      trigger->trigger(transition.released);
  }
}

void T65Keypad::log_stable_state_(const T65Transition &transition) const {
  if (!transition.stable_changed && !transition.entered_lockout && !transition.left_lockout)
    return;
  const T65ContactSample stable = this->state_machine_.stable_sample();
  ESP_LOGD(TAG, "Stable rows=0x%X columns=0x%X state=%u", stable.rows, stable.columns,
           static_cast<unsigned>(this->state_machine_.settled_state()));
  if (transition.entered_lockout)
    ESP_LOGD(TAG, "Invalid or multiple-key contact combination; entering lockout");
  if (transition.left_lockout)
    ESP_LOGD(TAG, "All contacts released; leaving lockout");
}

void T65Keypad::idle_if_possible_() {
  if (this->state_machine_.needs_time() ||
      T65KeypadStateMachine::has_unprocessed_edges(this->processed_edge_count_, this->interrupt_store_.edge_count))
    return;

  this->disable_loop();

  // Closing the classic check-then-disable race: an ISR may have fired while
  // this component was still marked active, in which case ESPHome's pending
  // wake request could already have been scanned. Reassert it after disabling
  // whenever the edge counter proves work is outstanding.
  if (T65KeypadStateMachine::has_unprocessed_edges(this->processed_edge_count_,
                                                   this->interrupt_store_.edge_count))
    this->enable_loop_soon_any_context();
}

void T65Keypad::dump_config() {
  ESP_LOGCONFIG(TAG, "T65-TDK Keypad:");
  ESP_LOGCONFIG(TAG, "  Rows:");
  for (auto *pin : this->rows_)
    LOG_PIN("    ", pin);
  ESP_LOGCONFIG(TAG, "  Columns:");
  for (auto *pin : this->columns_)
    LOG_PIN("    ", pin);
  ESP_LOGCONFIG(TAG, "  Debounce: %" PRIu32 " ms", this->debounce_ms_);
  ESP_LOGCONFIG(TAG, "  Input mode: interrupt-driven, active-low with internal pull-ups");
  ESP_LOGCONFIG(TAG, "  Idle polling: disabled");
#ifdef USE_T65_KEYPAD_EVENT
  ESP_LOGCONFIG(TAG, "  Event entity: %s", this->event_ != nullptr ? "configured" : "not configured");
#else
  ESP_LOGCONFIG(TAG, "  Event entity: not configured");
#endif
}

float T65Keypad::get_setup_priority() const { return setup_priority::HARDWARE; }

}  // namespace esphome::t65_keypad
