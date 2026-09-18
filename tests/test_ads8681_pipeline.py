import unittest


class Ads8681PipelineTests(unittest.TestCase):
    def test_four_columns_use_discard_then_four_ordered_samples(self):
        cols = 4
        results = []
        for completed_step in range(cols + 1):
            if completed_step > 0:
                results.append(completed_step - 1)

        self.assertEqual(results, [0, 1, 2, 3])
        self.assertEqual(cols + 1, 5)

    def test_last_flush_keeps_last_mux_channel_selected(self):
        cols = 4
        selected_columns = [
            step if step < cols else cols - 1
            for step in range(cols + 1)
        ]
        self.assertEqual(selected_columns, [0, 1, 2, 3, 3])


if __name__ == "__main__":
    unittest.main()
