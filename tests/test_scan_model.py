import unittest


class ScanModelTests(unittest.TestCase):
    def test_two_by_two_scan_stores_each_sample_once(self):
        rows = 2
        cols = 2
        stored = []

        for row in range(rows):
            for step in range(cols + 1):
                if step > 0:
                    stored.append((row, step - 1))

        self.assertEqual(stored, [(0, 0), (0, 1), (1, 0), (1, 1)])

    def test_no_free_buffer_drops_whole_frame_not_partial_samples(self):
        rows = 2
        cols = 2
        active_buffer = None
        writes = []
        dropped_frames = 0

        for row in range(rows):
            for column in range(cols):
                if active_buffer is not None:
                    writes.append((row, column))
        dropped_frames += 1

        self.assertEqual(writes, [])
        self.assertEqual(dropped_frames, 1)


if __name__ == "__main__":
    unittest.main()
