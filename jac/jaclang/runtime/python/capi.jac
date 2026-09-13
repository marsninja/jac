"""Retained CPython object operations for native Jac module implementations.

Handles borrow their arguments. Object-producing operations return a new
reference (zero on error), released with jacpy_release or returned to Python.
All calls require the interpreter lock; exceptions remain in CPython's thread
state. No Python implementation of a replaced module is invoked here.
"""
import from c {
    def jacpy_release(handle: u64);  def jacpy_retain(handle: u64) -> u64;  def jacpy_list_item(
        handle: u64, index: i64
    ) -> u64;  def jacpy_list_size(handle: u64) -> i64;  def jacpy_list_append(
        handle: u64, value: u64
    ) -> i64;  def jacpy_list_delete(handle: u64, start: i64, end: i64) -> i64;  def jacpy_list_exchange(
        handle: u64, index: i64, value: u64
    ) -> u64;  def jacpy_list_swap(handle: u64, left: i64, right: i64);  def jacpy_none -> u64;  def jacpy_int(
        value: i64
    ) -> u64;  def jacpy_raise_error(kind: str, message: str, size: i64);  def jacpy_object_size(
        handle: u64
    ) -> i64;  def jacpy_sequence_slot(handle: u64) -> u64;  def jacpy_slot_item(
        slot: u64, handle: u64, index: i64
    ) -> u64;  def jacpy_call_one(callable: u64, argument: u64) -> u64;  def jacpy_less(
        left: u64, right: u64
    ) -> i64;  def jacpy_comparison_slot(handle: u64) -> u64;  def jacpy_same_type(
        left: u64, right: u64
    ) -> i64;  def jacpy_slot_less(slot: u64, left: u64, right: u64) -> i64;  def jacpy_insert(
        handle: u64, index: i64, value: u64
    ) -> i64;  def jacpy_recursion_enter(context: str) -> i64;  def jacpy_recursion_leave;
    def jacpy_is_long(handle: u64) -> i64;
    def jacpy_long_absolute(handle: u64) -> u64;
    def jacpy_hash_unsigned(handle: u64) -> u64;
    def jacpy_long_bits(handle: u64) -> i64;
    def jacpy_long_bytes(handle: u64, size: i64) -> u64;
    def jacpy_long_from_bytes(handle: u64) -> u64;
    def jacpy_unsigned_long(handle: u64) -> u64;
    def jacpy_integer_value(handle: u64) -> i64;
    def jacpy_entropy(size: i64) -> u64;
    def jacpy_clear_error;
    def jacpy_error_pending -> i64;
    def jacpy_wall_time -> i64;
    def jacpy_monotonic_time -> i64;
    def jacpy_process_id -> i64;
    def jacpy_is_tuple(handle: u64) -> i64;
    def jacpy_tuple_size(handle: u64) -> i64;
    def jacpy_tuple_new(size: i64) -> u64;
    def jacpy_tuple_item(handle: u64, index: i64) -> u64;
    def jacpy_tuple_set_owned(handle: u64, index: i64, value: u64) -> i64;
    def jacpy_buffer_size(handle: u64) -> i64;
    def jacpy_buffer_byte(handle: u64, index: i64) -> i64;
    def jacpy_buffer_new(size: i64) -> u64;
    def jacpy_buffer_set(handle: u64, index: i64, value: i64);

}

def object_error(kind: str, message: str) {
    jacpy_raise_error(kind, message, i64(len(message.encode("utf-8"))));
}
