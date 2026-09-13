"""Native reentrant FIFO with CPython's portable, interruptible wait primitive.

The interpreter lock protects queue state. A notification wakes one waiter;
consuming an item passes that notification on when more items remain. CPython
references are visible to its cycle collector through the borrowed-item ABI.
Derived from CPython 3.14.6; see ../LICENSE.cpython.
"""
import from jaclang.runtime.python.capi {
    jacpy_retain,
    jacpy_release,
    jacpy_lock_notify,
    jacpy_lock_wait,
    jacpy_pending_calls,
    jacpy_timeout_ns,
    jacpy_deadline,
    jacpy_deadline_remaining,
    jacpy_is_none,
    jacpy_error_pending,
    jacpy_set_none_exception,
    object_error
}

obj queue_state {
    has lock: u64,
        slots: list[u64] = [u64(0) for _ in range(8)],
        head: int = 0,
        count: int = 0,
        waiters: int = 0,
        notification: bool = False;

    def notify {
        if self.waiters > 0 and self.count > 0 and not self.notification {
            self.notification = True;
            jacpy_lock_notify(self.lock);
        }
    }

    def resize(capacity: int) {
        slots = [u64(0) for _ in range(capacity)];
        for i in range(self.count) {
            slots[i] = self.slots[(self.head + i) % len(self.slots)];
        }
        self.slots = slots;
        self.head = 0;
    }
}

def:pub jacpy_queue_new(lock: u64) -> queue_state {
    return queue_state(lock=lock);
}

def:pub jacpy_queue_clear(state: queue_state) {
    slots = state.slots;
    head = state.head;
    count = state.count;
    # Publish an empty queue before releasing values whose finalizers may put().
    state.slots = [u64(0) for _ in range(8)];
    state.head = 0;
    state.count = 0;
    for i in range(count) {
        jacpy_release(slots[(head + i) % len(slots)]);
    }
}

def:pub jacpy_queue_size(state: queue_state) -> i64 {
    return i64(state.count);
}

# Borrowed references: the CPython GC visitor never calls Python or releases GIL.
def:pub jacpy_queue_borrow(state: queue_state, index: i64) -> u64 {
    return state.slots[(state.head + int(index)) % len(state.slots)];
}

def:pub jacpy_queue_put(state: queue_state, value: u64) {
    if state.count == len(state.slots) {
        state.resize(len(state.slots) * 2);
    }
    state.slots[(state.head + state.count) % len(state.slots)] = jacpy_retain(value);
    state.count += 1;
    state.notify();
}

def:pub jacpy_queue_get(
    state: queue_state, empty: u64, block: i64, timeout: u64
) -> u64 {
    deadline: i64 = 0;
    if block != 0 and jacpy_is_none(timeout) == 0 {
        nanoseconds = jacpy_timeout_ns(timeout);
        if jacpy_error_pending() != 0 {
            return 0;
        }
        if nanoseconds < 0 {
            object_error("ValueError", "'timeout' must be a non-negative number");
            return 0;
        }
        deadline = jacpy_deadline(nanoseconds);
    }
    while True {
        if state.count > 0 {
            value = state.slots[state.head];
            state.slots[state.head] = u64(0);
            state.head = (state.head + 1) % len(state.slots);
            state.count -= 1;
            if len(state.slots) > 8 and state.count < len(state.slots) // 4 {
                state.resize(len(state.slots) // 2);
            }
            state.notify();
            return value;
        }
        if block == 0 {
            jacpy_set_none_exception(empty);
            return 0;
        }
        remaining: i64 = -1;
        if deadline != 0 {
            remaining = jacpy_deadline_remaining(deadline);
            if remaining < 0 {
                jacpy_set_none_exception(empty);
                return 0;
            }
        }
        state.waiters += 1;
        status = jacpy_lock_wait(state.lock, remaining);
        state.waiters -= 1;
        if status == 1 {
            state.notification = False;
        } elif status == 0 {
            jacpy_set_none_exception(empty);
            return 0;
        } elif jacpy_pending_calls() < 0 {
            return 0;
        }
    }
    return 0;
}
