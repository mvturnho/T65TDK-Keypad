#include <cstdlib>
#include <iostream>
#include <string>

#include "t65_keypad_state.h"

using esphome::t65_keypad::T65_MAX_NUMBER_LENGTH;
using esphome::t65_keypad::T65ContactSample;
using esphome::t65_keypad::T65Dialer;
using esphome::t65_keypad::T65DialResult;
using esphome::t65_keypad::T65KeypadStateMachine;
using esphome::t65_keypad::T65NumberBuffer;
using esphome::t65_keypad::T65State;
using esphome::t65_keypad::T65TimerRequest;
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

void test_number_buffer_collects_until_cleared() {
  T65NumberBuffer number;
  CHECK(number.empty());
  CHECK(std::string(number.c_str()).empty());

  for (char key : std::string("0612345678"))
    CHECK(number.append(key));
  CHECK(std::string(number.c_str()) == "0612345678");
  CHECK(number.size() == 10);

  // Like the rotarydial component, reporting does not clear: later keys append.
  CHECK(number.append('*'));
  CHECK(number.append('#'));
  CHECK(std::string(number.c_str()) == "0612345678*#");

  number.clear();
  CHECK(number.empty());
  CHECK(std::string(number.c_str()).empty());
  CHECK(number.append('9'));
  CHECK(std::string(number.c_str()) == "9");
}

void test_number_buffer_is_bounded() {
  T65NumberBuffer number;
  for (size_t i = 0; i < T65_MAX_NUMBER_LENGTH; i++)
    CHECK(number.append(static_cast<char>('0' + i % 10)));
  CHECK(number.size() == T65_MAX_NUMBER_LENGTH);

  const std::string full = number.c_str();
  CHECK(full.size() == T65_MAX_NUMBER_LENGTH);
  CHECK(!number.append('#'));
  CHECK(std::string(number.c_str()) == full);

  number.clear();
  CHECK(number.append('1'));
}

void test_digit_values() {
  constexpr char digits[] = "0123456789";
  for (int value = 0; value < 10; value++)
    CHECK(T65NumberBuffer::digit_value(digits[value]) == value);
  CHECK(T65NumberBuffer::digit_value('*') == -1);
  CHECK(T65NumberBuffer::digit_value('#') == -1);
  CHECK(T65NumberBuffer::digit_value(0) == -1);
}

constexpr T65TimerRequest KEEP = T65TimerRequest::KEEP;
constexpr T65TimerRequest START = T65TimerRequest::START;
constexpr T65TimerRequest STOP = T65TimerRequest::STOP;

std::string number(const T65Dialer &dialer) { return dialer.number().c_str(); }

void test_dialer_without_long_press_collects_on_press() {
  T65Dialer dialer;
  T65DialResult result = dialer.press('5');
  CHECK(result.collected == '5');
  CHECK(result.long_press == 0);
  CHECK(result.report_timer == START);
  CHECK(result.long_press_timer == KEEP);
  CHECK(number(dialer) == "5");

  // Release changes nothing, exactly as before long press existed.
  result = dialer.release('5');
  CHECK(result.collected == 0);
  CHECK(result.report_timer == KEEP);
  CHECK(result.long_press_timer == KEEP);

  // Holding has no effect either; a stray long-press timeout is ignored.
  CHECK(dialer.press('6').collected == '6');
  CHECK(dialer.long_press_elapsed().long_press == 0);
  CHECK(number(dialer) == "56");
}

void test_dialer_short_press_collects_on_release() {
  T65Dialer dialer;
  dialer.set_long_press_enabled(true);

  T65DialResult result = dialer.press('1');
  CHECK(result.collected == 0);
  CHECK(result.long_press_timer == START);
  CHECK(result.report_timer == KEEP);  // Nothing pending yet.
  CHECK(number(dialer).empty());

  result = dialer.release('1');
  CHECK(result.collected == '1');
  CHECK(result.long_press == 0);
  CHECK(result.long_press_timer == STOP);
  CHECK(result.report_timer == START);
  CHECK(number(dialer) == "1");

  // A timeout that fires after the release must not turn it into a long press.
  CHECK(dialer.long_press_elapsed().long_press == 0);
}

void test_dialer_long_press_is_not_collected() {
  T65Dialer dialer;
  dialer.set_long_press_enabled(true);

  dialer.press('0');
  dialer.release('0');
  dialer.press('6');
  dialer.release('6');
  CHECK(number(dialer) == "06");
  CHECK(dialer.report_pending());

  // The pending report waits while a key is held.
  T65DialResult result = dialer.press('#');
  CHECK(result.report_timer == STOP);

  result = dialer.long_press_elapsed();
  CHECK(result.long_press == '#');
  CHECK(result.collected == 0);
  CHECK(result.report_timer == START);  // The paused report resumes.
  CHECK(result.long_press_timer == KEEP);

  // Fires only once per hold, and the release does not add the key.
  CHECK(dialer.long_press_elapsed().long_press == 0);
  result = dialer.release('#');
  CHECK(result.collected == 0);
  CHECK(result.report_timer == KEEP);
  CHECK(result.long_press_timer == KEEP);
  CHECK(number(dialer) == "06");
}

void test_dialer_long_press_does_not_repeat_report() {
  T65Dialer dialer;
  dialer.set_long_press_enabled(true);
  dialer.press('1');
  dialer.release('1');
  dialer.reported();
  CHECK(!dialer.report_pending());

  // Nothing is pending, so neither the hold nor the long press restarts it.
  CHECK(dialer.press('2').report_timer == KEEP);
  T65DialResult result = dialer.long_press_elapsed();
  CHECK(result.long_press == '2');
  CHECK(result.report_timer == KEEP);
  CHECK(dialer.release('2').report_timer == KEEP);
  CHECK(number(dialer) == "1");
}

void test_dialer_lockout_cancels_long_press() {
  T65Dialer dialer;
  dialer.set_long_press_enabled(true);
  dialer.press('4');

  T65DialResult result = dialer.lockout();
  CHECK(result.long_press_timer == STOP);
  CHECK(dialer.long_press_elapsed().long_press == 0);

  // The release from lockout counts as a short press, as the press would have
  // been collected immediately without long press.
  result = dialer.release('4');
  CHECK(result.collected == '4');
  CHECK(result.long_press_timer == KEEP);
  CHECK(result.report_timer == START);

  // Lockout without a held key, for example at boot, does nothing.
  result = dialer.lockout();
  CHECK(result.long_press_timer == KEEP);
  CHECK(dialer.release('4').collected == 0);
}

void test_dialer_clear() {
  T65Dialer dialer;
  dialer.set_long_press_enabled(true);
  dialer.press('7');
  dialer.release('7');

  dialer.press('*');
  T65DialResult result = dialer.clear();
  CHECK(result.report_timer == STOP);
  CHECK(result.long_press_timer == KEEP);  // The held key is unaffected.
  CHECK(number(dialer).empty());
  CHECK(!dialer.report_pending());

  // A long press after clearing does not report the now empty number.
  result = dialer.long_press_elapsed();
  CHECK(result.long_press == '*');
  CHECK(result.report_timer == KEEP);
}

void test_dialer_reports_dropped_keys() {
  T65Dialer dialer;
  for (size_t i = 0; i < T65_MAX_NUMBER_LENGTH; i++)
    CHECK(!dialer.press('9').dropped);
  T65DialResult result = dialer.press('8');
  CHECK(result.collected == '8');
  CHECK(result.dropped);
  CHECK(result.report_timer == START);
  CHECK(dialer.number().size() == T65_MAX_NUMBER_LENGTH);
}

T65Dialer repeating_dialer() {
  T65Dialer dialer;
  dialer.set_long_press_enabled(true);
  dialer.set_repeat_enabled(true);
  return dialer;
}

void test_dialer_repeats_after_long_press() {
  T65Dialer dialer = repeating_dialer();
  dialer.press('0');
  dialer.release('0');

  // Repeating only starts with the long press, not at the press itself.
  T65DialResult result = dialer.press('5');
  CHECK(result.repeat_timer == KEEP);
  CHECK(dialer.repeat_elapsed().repeat == 0);

  result = dialer.long_press_elapsed();
  CHECK(result.long_press == '5');
  CHECK(result.repeat == 0);
  CHECK(result.repeat_timer == START);

  for (int i = 0; i < 3; i++) {
    result = dialer.repeat_elapsed();
    CHECK(result.repeat == '5');
    CHECK(result.repeat_timer == START);  // Re-armed for the next repeat.
    CHECK(result.long_press == 0);
    CHECK(result.collected == 0);
    CHECK(result.report_timer == KEEP);
  }

  result = dialer.release('5');
  CHECK(result.repeat_timer == STOP);
  CHECK(result.long_press_timer == KEEP);
  CHECK(result.collected == 0);
  CHECK(dialer.repeat_elapsed().repeat == 0);
  CHECK(number(dialer) == "0");
}

void test_dialer_short_press_does_not_repeat() {
  T65Dialer dialer = repeating_dialer();
  dialer.press('3');
  T65DialResult result = dialer.release('3');
  CHECK(result.collected == '3');
  CHECK(result.repeat_timer == KEEP);
  CHECK(dialer.repeat_elapsed().repeat == 0);

  // The next press starts clean: no repeat before its own long press.
  dialer.press('3');
  CHECK(dialer.repeat_elapsed().repeat == 0);
}

void test_dialer_lockout_stops_repeat() {
  T65Dialer dialer = repeating_dialer();
  dialer.press('8');
  dialer.long_press_elapsed();
  CHECK(dialer.repeat_elapsed().repeat == '8');

  T65DialResult result = dialer.lockout();
  CHECK(result.repeat_timer == STOP);
  CHECK(result.long_press_timer == KEEP);
  CHECK(dialer.repeat_elapsed().repeat == 0);

  // It was a long press, so the release from lockout collects nothing.
  result = dialer.release('8');
  CHECK(result.collected == 0);
  CHECK(result.repeat_timer == KEEP);
}

void test_dialer_without_repeat_stops_at_long_press() {
  T65Dialer dialer;
  dialer.set_long_press_enabled(true);
  dialer.press('9');
  T65DialResult result = dialer.long_press_elapsed();
  CHECK(result.long_press == '9');
  CHECK(result.repeat_timer == KEEP);
  CHECK(dialer.repeat_elapsed().repeat == 0);
  CHECK(dialer.release('9').repeat_timer == KEEP);
}

void test_dialer_clear_keeps_repeating() {
  T65Dialer dialer = repeating_dialer();
  dialer.press('*');
  dialer.long_press_elapsed();
  T65DialResult result = dialer.clear();
  CHECK(result.repeat_timer == KEEP);
  CHECK(dialer.repeat_elapsed().repeat == '*');
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
  test_number_buffer_collects_until_cleared();
  test_number_buffer_is_bounded();
  test_digit_values();
  test_dialer_without_long_press_collects_on_press();
  test_dialer_short_press_collects_on_release();
  test_dialer_long_press_is_not_collected();
  test_dialer_long_press_does_not_repeat_report();
  test_dialer_lockout_cancels_long_press();
  test_dialer_clear();
  test_dialer_reports_dropped_keys();
  test_dialer_repeats_after_long_press();
  test_dialer_short_press_does_not_repeat();
  test_dialer_lockout_stops_repeat();
  test_dialer_without_repeat_stops_at_long_press();
  test_dialer_clear_keeps_repeating();

  if (failures != 0) {
    std::cerr << failures << " test assertion(s) failed\n";
    return EXIT_FAILURE;
  }
  std::cout << "All T65 keypad state-machine tests passed\n";
  return EXIT_SUCCESS;
}
