#include <cstdlib>
#include <iostream>
#include <string>

#include "t65_keypad_state.h"

using esphome::t65_keypad::T65ContactSample;
using esphome::t65_keypad::T65KeypadStateMachine;
using esphome::t65_keypad::T65State;
using esphome::t65_keypad::T65Transition;

namespace {

int failures = 0;

#define CHECK(expression) check((expression), #expression, __FILE__, __LINE__)

void check(bool result, const char *expression, const char *file, int line) {
  if (!result) {
    std::cerr << file << ':' << line << ": check failed: " << expression << '\n';
    failures++;
  }
}

T65ContactSample none() { return {}; }
T65ContactSample sample(uint8_t row, uint8_t column) {
  return {static_cast<uint8_t>(1U << row), static_cast<uint8_t>(1U << column)};
}

T65Transition settle(T65KeypadStateMachine &machine, T65ContactSample contacts, uint32_t at) {
  machine.observe(contacts, at, true);
  CHECK(machine.state() == T65State::DEBOUNCING);
  CHECK(machine.advance(at + 19).pressed == 0);
  return machine.advance(at + 20);
}

void test_mapping_and_partial_contacts() {
  constexpr char expected[] = "123456789*0#";
  size_t index = 0;
  for (uint8_t row = 0; row < 4; row++) {
    for (uint8_t column = 0; column < 3; column++) {
      CHECK(T65KeypadStateMachine::decode(sample(row, column)) == expected[index]);
      index++;
    }
  }
  CHECK(T65KeypadStateMachine::decode(none()) == 0);
  CHECK(T65KeypadStateMachine::decode({1, 0}) == 0);
  CHECK(T65KeypadStateMachine::decode({0, 1}) == 0);
  CHECK(T65KeypadStateMachine::decode({3, 1}) == 0);
  CHECK(T65KeypadStateMachine::decode({1, 3}) == 0);
}

void test_delayed_contacts_and_press_bounce() {
  T65KeypadStateMachine machine(20);
  machine.initialize(none(), 0);

  machine.observe({1, 0}, 1, true);
  CHECK(machine.advance(20).pressed == 0);
  machine.observe(sample(0, 0), 8, true);
  CHECK(machine.advance(27).pressed == 0);
  T65Transition transition = machine.advance(28);
  CHECK(transition.pressed == '1');

  // Holding does not repeat.
  CHECK(machine.advance(10000).pressed == 0);

  // A complete bounce that ends at the same masks still restarts debounce.
  machine.observe(none(), 10010, true);
  machine.observe(sample(0, 0), 10011, true);
  CHECK(machine.advance(10030).released == 0);
  CHECK(machine.advance(10031).released == 0);
  CHECK(machine.state() == T65State::PRESSED);
}

void test_release_bounce_and_repress() {
  T65KeypadStateMachine machine(20);
  machine.initialize(none(), 0);
  CHECK(settle(machine, sample(1, 1), 1).pressed == '5');

  machine.observe(none(), 30, true);
  machine.observe(sample(1, 1), 35, true);
  machine.observe(none(), 40, true);
  CHECK(machine.advance(59).released == 0);
  CHECK(machine.advance(60).released == '5');
  CHECK(machine.state() == T65State::IDLE);

  CHECK(settle(machine, sample(1, 1), 61).pressed == '5');
}

void test_multiple_keys_and_lockout() {
  T65KeypadStateMachine machine(20);
  machine.initialize(none(), 0);

  T65Transition invalid = settle(machine, {0b0011, 0b0011}, 1);
  CHECK(invalid.pressed == 0);
  CHECK(invalid.entered_lockout);
  CHECK(machine.state() == T65State::LOCKOUT);

  // A valid-looking combination while locked out must not create a ghost key.
  CHECK(settle(machine, sample(2, 2), 30).pressed == 0);
  T65Transition clear = settle(machine, none(), 60);
  CHECK(clear.left_lockout);
  CHECK(clear.released == 0);
  CHECK(settle(machine, sample(2, 2), 90).pressed == '9');
}

void test_second_key_while_held() {
  T65KeypadStateMachine machine(20);
  machine.initialize(none(), 0);
  CHECK(settle(machine, sample(0, 0), 1).pressed == '1');

  T65Transition invalid = settle(machine, {0b0011, 0b0011}, 30);
  CHECK(invalid.entered_lockout);
  CHECK(invalid.pressed == 0);
  CHECK(invalid.released == 0);

  // Releasing only the second key does not leave lockout.
  CHECK(settle(machine, sample(0, 0), 60).released == 0);
  CHECK(machine.state() == T65State::LOCKOUT);
  T65Transition clear = settle(machine, none(), 90);
  CHECK(clear.released == '1');
  CHECK(clear.left_lockout);
}

void test_one_sided_interruption_does_not_release() {
  T65KeypadStateMachine machine(20);
  machine.initialize(none(), 0);
  CHECK(settle(machine, sample(3, 2), 1).pressed == '#');
  CHECK(settle(machine, {0b1000, 0}, 30).released == 0);
  CHECK(machine.state() == T65State::PRESSED);
  CHECK(settle(machine, sample(3, 2), 60).pressed == 0);
  CHECK(settle(machine, none(), 90).released == '#');
}

void test_boot_held_key_is_suppressed() {
  T65KeypadStateMachine machine(20);
  machine.initialize(sample(0, 1), 0);
  CHECK(machine.state() == T65State::LOCKOUT);
  CHECK(machine.advance(1000).pressed == 0);
  T65Transition clear = settle(machine, none(), 1001);
  CHECK(clear.released == 0);
  CHECK(clear.left_lockout);
  CHECK(settle(machine, sample(0, 1), 1030).pressed == '2');
}

void test_millis_wraparound() {
  T65KeypadStateMachine machine(20);
  machine.initialize(none(), 0xFFFFFFE0U);
  machine.observe(sample(3, 1), 0xFFFFFFF5U, true);
  CHECK(machine.advance(5).pressed == 0);
  CHECK(machine.advance(9).pressed == '0');
}

void test_interrupt_during_loop_disable_handshake() {
  const uint32_t processed_before_disable = 41;
  CHECK(!T65KeypadStateMachine::has_unprocessed_edges(processed_before_disable, 41));
  // Model an ISR occurring after the idle check but before/while disable_loop().
  CHECK(T65KeypadStateMachine::has_unprocessed_edges(processed_before_disable, 42));
  // Counter wrap is also detected.
  CHECK(T65KeypadStateMachine::has_unprocessed_edges(0xFFFFFFFFU, 0));
}

}  // namespace

int main() {
  test_mapping_and_partial_contacts();
  test_delayed_contacts_and_press_bounce();
  test_release_bounce_and_repress();
  test_multiple_keys_and_lockout();
  test_second_key_while_held();
  test_one_sided_interruption_does_not_release();
  test_boot_held_key_is_suppressed();
  test_millis_wraparound();
  test_interrupt_during_loop_disable_handshake();

  if (failures != 0) {
    std::cerr << failures << " test assertion(s) failed\n";
    return EXIT_FAILURE;
  }
  std::cout << "All T65 keypad state-machine tests passed\n";
  return EXIT_SUCCESS;
}
