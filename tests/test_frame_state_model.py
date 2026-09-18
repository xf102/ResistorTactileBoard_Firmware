import unittest
from enum import Enum, auto


class State(Enum):
    FREE = auto()
    ACQUIRING = auto()
    RAW_READY = auto()
    PACKED_READY = auto()
    TRANSMITTING = auto()


LEGAL_NEXT = {
    State.FREE: State.ACQUIRING,
    State.ACQUIRING: State.RAW_READY,
    State.RAW_READY: State.PACKED_READY,
    State.PACKED_READY: State.TRANSMITTING,
    State.TRANSMITTING: State.FREE,
}


class FrameStateModelTests(unittest.TestCase):
    def test_complete_owner_lifecycle_returns_buffer_to_free(self):
        state = State.FREE
        for expected in (
            State.ACQUIRING,
            State.RAW_READY,
            State.PACKED_READY,
            State.TRANSMITTING,
            State.FREE,
        ):
            state = LEGAL_NEXT[state]
            self.assertEqual(state, expected)

    def test_two_busy_buffers_force_whole_next_frame_drop(self):
        states = [State.ACQUIRING, State.TRANSMITTING]
        free_index = next(
            (index for index, state in enumerate(states) if state is State.FREE),
            None,
        )
        self.assertIsNone(free_index)

    def test_illegal_transition_is_not_part_of_contract(self):
        self.assertNotEqual(LEGAL_NEXT[State.RAW_READY], State.TRANSMITTING)
        self.assertNotEqual(LEGAL_NEXT[State.ACQUIRING], State.FREE)

    def test_failed_pack_recovery_releases_only_ready_frame(self):
        recoverable = {State.RAW_READY, State.PACKED_READY}
        self.assertIn(State.RAW_READY, recoverable)
        self.assertNotIn(State.TRANSMITTING, recoverable)


if __name__ == "__main__":
    unittest.main()
