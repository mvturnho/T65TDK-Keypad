# Architecture

## Contact model and decoding

The hardware presents four active-low row inputs and three active-low column
inputs, producing a 4-bit row mask and 3-bit column mask. A sample is a valid
key only when each mask contains exactly one bit. The index is
`row * 3 + column`, mapped through `123456789*0#`.

Zero row and zero column bits means fully released. One-sided samples are
incomplete and may occur because the two mechanisms do not close at exactly the
same instant. Any other non-valid sample represents multiple or invalid
contacts.

## Interrupt path

All seven native GPIOs attach an ESPHome HAL `INTERRUPT_ANY_EDGE` handler. The
IRAM ISR performs exactly two operations: increment a volatile 32-bit edge
counter, then call `enable_loop_soon_any_context()`. It does not read all GPIOs,
decode, allocate, log, invoke automations or interact with Home Assistant.

Normal ESPHome context compares the edge counter, reads a seven-pin snapshot,
and submits it to the state machine. The counter is recorded from before the
snapshot; an edge during the reads therefore remains outstanding and forces a
second snapshot. A complete bounce that ends at the original masks still
changes the counter and restarts debounce.

## Debounce and state machine

The state machine is independent of ESPHome and has no dynamic allocation.
Every observed edge restarts the configurable stability deadline. It uses
unsigned elapsed-time arithmetic so `millis()` wraparound is safe.

- `IDLE`: accepts a stable valid row/column pair.
- `DEBOUNCING`: reported while a candidate waits for its deadline.
- `PRESSED`: stores one key and never repeats it.
- `LOCKOUT`: rejects all input until every contact is stably clear.

Delayed row/column closure naturally replaces the incomplete candidate with a
valid one and restarts the deadline. A one-sided interruption while pressed is
treated as still held, preventing worn contacts from generating duplicates.
Only a stable all-clear sample releases the key. A stable different key or
multi-contact sample while pressed enters lockout.

At boot, any active contact immediately selects lockout without a press or
release callback. The keypad arms after an all-clear sample remains stable.

## Idle behavior and race closure

The ESPHome component loop is enabled only while an edge or debounce deadline
needs processing. When neither remains, it calls `disable_loop()`; there is no
interval, task, thread or background matrix scan.

There is a subtle check-then-disable race: an interrupt can arrive while the
component is still classified as active, just before the loop disables itself.
After disabling, the runtime compares the edge counter again. If work appeared,
it reasserts `enable_loop_soon_any_context()` from normal context. An interrupt
arriving later sees the already-disabled component and performs the ordinary
wakeup. This handshake prevents a lost edge without a critical section.

## Callbacks and Event entity

Press/release triggers use ESPHome automations and receive a `char`. The
optional Event entity is registered through ESPHome's official Event API and
advertises a fixed list of 12 event types. It is triggered only from normal
context on a new press. Callback vectors allocate only during setup; there are
no per-key dynamic buffers.

## Number collection, long press and repeat

`T65Dialer` turns accepted presses and releases into the collected number,
long presses and repeats. Like the debounce state machine it is independent of
ESPHome and covered by the native tests. It keeps the number in a fixed
33-byte buffer (`T65NumberBuffer`, 32 keys plus terminator) and returns a
small result for each input: the key to collect, the key that became a long
press or repeated, and whether to start or stop the number, long-press and
repeat timeouts. The component applies that result.

Without long press, a press is collected at once. With long press, a press
starts the long-press timeout and pauses a pending number report; the release
(short) or the timeout (long) then classifies the key. A lockout cancels the
long-press timeout. A `report pending` flag ensures a long press resumes only
a report that has not run yet.

With repeat, the long press starts the repeat timeout, and every repeat
re-arms it. Release and lockout stop it. A re-armed timeout is used instead of
`set_interval()`, because the scheduler delays an interval's first run by a
random offset of up to half its period.

All waits are named ESPHome scheduler timeouts. The scheduler runs them even
while the component loop is disabled, so a held key or a pending number does
not re-enable polling. The component applies timer requests before it runs
automations, so a `clear_numbers()` from an automation always wins over a
report started by the same input. When the number timeout fires, the buffer is
copied once into the `std::string` passed to `on_numbers`; the buffer itself
keeps its contents until `clear_numbers()`.

The dial sensor and the Event entity are published for collected keys, long
presses and repeats from the same normal-context path; `on_key` and `on_key_release`
remain raw press and release callbacks.

## Failure handling and diagnostics

Python validation catches count, duplicate, mode, capability and debounce
errors before C++ generation. Runtime invalid combinations never decode to a
key. INFO logs contain accepted transitions, long presses and reported
numbers; DEBUG contains stable masks, lockout transitions, repeats and clears; VERY_VERBOSE contains
edge snapshots. A full number buffer is logged as a WARNING. No ISR logs
exist.
