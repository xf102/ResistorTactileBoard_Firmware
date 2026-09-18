"""8x8/16x16/32x32 tactile matrix serial viewer.

Frame layout (8x8: 136 bytes, 16x16: 520 bytes, 32x32: 2056 bytes):
  [0:2]     header 0xFF 0x66
  [2:4]     big-endian frame counter
  [4:-4]    big-endian uint16 ADC values, row-major
  [-4:]     little-endian CRC-32/ISO-HDLC over counter + ADC payload
"""

import binascii
import struct
import sys
import time

import cv2
import numpy as np
import serial


DEFAULT_MATRIX_SIZE = 8
SUPPORTED_MATRIX_SIZES = (8, 16, 32)
ROWS = DEFAULT_MATRIX_SIZE
COLS = DEFAULT_MATRIX_SIZE
HEADER = bytes((0xFF, 0x66))
DEFAULT_PORT = "COM8"
DEFAULT_BAUDRATE = 115200
SERIAL_READ_TIMEOUT = 0.02
STATUS_HEIGHT = 36
ADC_FULL_SCALE = 1019
PRESS_THRESHOLD = 513
DEFAULT_DISPLAY_MAX = ADC_FULL_SCALE
LOW_COLOR_ADC = 47
ADC_COLOR_STOPS = (
    (LOW_COLOR_ADC, (100, 0, 0)),
    (93, (255, 0, 0)),
    (187, (255, 180, 0)),
    (PRESS_THRESHOLD, (0, 110, 255)),
    (ADC_FULL_SCALE, (0, 0, 255)),
)
INITIAL_WIDTH = 512
INITIAL_HEIGHT = ROWS * 64 + STATUS_HEIGHT


def frame_size_for(rows=ROWS, cols=COLS):
    """Return the packed frame size for a supported square matrix."""
    if rows != cols or rows not in SUPPORTED_MATRIX_SIZES:
        raise ValueError("matrix size must be 8, 16, or 32")
    return 2 + 2 + rows * cols * 2 + 4


def key_index(row: int, col: int, cols: int = COLS) -> int:
    """Return the row-major key number for a matrix coordinate."""
    return row * cols + col


def cell_labels(row: int, col: int, adc_value: int, cols: int = COLS):
    """Return the stable two-line label text for one matrix cell."""
    return f"KEY{key_index(row, col, cols)}", f"ADC:{int(adc_value)}"


def adc_heat_color(value):
    """Map ADC to a fixed high-contrast dark-blue-to-red BGR color scale."""
    adc_value = float(value)
    if adc_value <= ADC_COLOR_STOPS[0][0]:
        return ADC_COLOR_STOPS[0][1]

    for (low_adc, low_color), (high_adc, high_color) in zip(
        ADC_COLOR_STOPS, ADC_COLOR_STOPS[1:]
    ):
        if adc_value <= high_adc:
            level = (adc_value - low_adc) / (high_adc - low_adc)
            return tuple(
                int(round(low + (high - low) * level))
                for low, high in zip(low_color, high_color)
            )

    return ADC_COLOR_STOPS[-1][1]


def fill_cell_interior(
    display,
    inner_top,
    inner_bottom,
    inner_left,
    inner_right,
    adc_value,
):
    """Fill the complete cell interior with the ADC heat color."""
    display[
        inner_top : inner_bottom + 1,
        inner_left:inner_right,
    ] = adc_heat_color(adc_value)


def layout_for_window(width, height, rows=ROWS, cols=COLS):
    """Calculate a matrix render layout at the window's native resolution."""
    width = max(int(width), cols)
    height = max(int(height), rows + 1)
    status_height = max(20, int(round(height * STATUS_HEIGHT / INITIAL_HEIGHT)))
    status_height = min(status_height, height - rows)
    matrix_height = height - status_height
    x_edges = tuple(np.linspace(0, width, cols + 1, dtype=int))
    y_edges = tuple(np.linspace(0, matrix_height, rows + 1, dtype=int))
    cell_width = min(b - a for a, b in zip(x_edges, x_edges[1:]))
    cell_height = min(b - a for a, b in zip(y_edges, y_edges[1:]))
    cell_unit = max(1, min(cell_width, cell_height))
    resolution_scale = cell_unit / 64.0

    return {
        "width": width,
        "height": height,
        "status_height": status_height,
        "matrix_height": matrix_height,
        "x_edges": x_edges,
        "y_edges": y_edges,
        "cell_unit": cell_unit,
        "key_font_scale": max(0.18, 0.36 * resolution_scale),
        "adc_font_scale": max(0.16, 0.31 * resolution_scale),
        "status_font_scale": max(0.20, 0.43 * resolution_scale),
        "border_thickness": max(1, int(round(resolution_scale))),
        "text_thickness": max(1, int(round(resolution_scale))),
        "text_shadow_thickness": max(2, int(round(3 * resolution_scale))),
        "cell_margin": max(2, int(round(3 * resolution_scale))),
    }


def find_and_parse_frame(buf: bytes, rows=ROWS, cols=COLS):
    """Return the first complete valid-format frame and remaining bytes."""
    frame_size = frame_size_for(rows, cols)
    num_points = rows * cols
    while len(buf) >= frame_size:
        header_index = buf.find(HEADER)
        if header_index < 0:
            return None, buf[-1:] if buf else b""
        if header_index:
            buf = buf[header_index:]
        if len(buf) < frame_size:
            return None, buf

        frame = buf[:frame_size]
        counter = (frame[2] << 8) | frame[3]
        values = np.frombuffer(
            frame, dtype=">u2", count=num_points, offset=4
        ).astype(np.float64)
        grid = values.reshape((rows, cols))
        crc_calc = binascii.crc32(frame[2:-4]) & 0xFFFFFFFF
        crc_recv = struct.unpack("<I", frame[-4:])[0]

        parsed = {
            "valid": crc_calc == crc_recv,
            "counter": counter,
            "grid": grid,
            "crc_calc": crc_calc,
            "crc_recv": crc_recv,
            "raw": frame,
        }
        # A false header inside noise must not consume bytes that may contain
        # the next real frame. Advance one byte and search again after a CRC
        # failure; consume the whole candidate only when it is valid.
        remaining = buf[frame_size:] if parsed["valid"] else buf[1:]
        return parsed, remaining

    return None, buf


def drain_serial_frames(buf: bytes, rows=ROWS, cols=COLS):
    """Parse all complete frames while retaining only the latest valid one."""
    latest_valid = None
    invalid_frames = []
    parsed_count = 0
    valid_count = 0

    while True:
        parsed, remaining = find_and_parse_frame(buf, rows=rows, cols=cols)
        buf = remaining
        if parsed is None:
            break

        parsed_count += 1
        if parsed["valid"]:
            latest_valid = parsed
            valid_count += 1
        else:
            invalid_frames.append((parsed_count, parsed))

    return latest_valid, buf, parsed_count, valid_count, invalid_frames


class HeatmapViewer:
    """High-rate OpenCV spectrum display for a supported ADC matrix."""

    def __init__(
        self,
        vmin=0,
        vmax=DEFAULT_DISPLAY_MAX,
        press_threshold=PRESS_THRESHOLD,
        scale=64,
        rows=ROWS,
        cols=COLS,
    ):
        self.vmin = vmin
        self.vmax = vmax
        self.press_threshold = press_threshold
        self.scale = scale
        self.rows = rows
        self.cols = cols
        self.width = cols * scale
        self.height = rows * scale + STATUS_HEIGHT
        self.window_name = f"{rows}x{cols} Pressure Heatmap"

        cv2.namedWindow(
            self.window_name,
            cv2.WINDOW_NORMAL | cv2.WINDOW_FREERATIO,
        )
        cv2.resizeWindow(self.window_name, self.width, self.height)
        cv2.imshow(
            self.window_name,
            np.zeros((self.height, self.width, 3), dtype=np.uint8),
        )
        cv2.waitKey(1)

    def _window_render_size(self):
        """Return the current drawable window size, with a safe fallback."""
        try:
            _, _, width, height = cv2.getWindowImageRect(self.window_name)
            if width >= self.cols and height > self.rows:
                return width, height
        except cv2.error:
            pass
        return self.width, self.height

    def update(self, parsed: dict):
        grid = parsed["grid"]
        self.width, self.height = self._window_render_size()
        layout = layout_for_window(
            self.width, self.height, self.rows, self.cols
        )
        matrix_height = layout["matrix_height"]
        x_edges = layout["x_edges"]
        y_edges = layout["y_edges"]
        margin = layout["cell_margin"]
        display = np.zeros((self.height, self.width, 3), dtype=np.uint8)
        display[:matrix_height, :] = (12, 12, 18)

        for row in range(self.rows):
            for col in range(self.cols):
                x0, x1 = x_edges[col], x_edges[col + 1]
                y0, y1 = y_edges[row], y_edges[row + 1]
                cell_width = x1 - x0
                cell_height = y1 - y0
                value = grid[row, col]
                inner_top = y0 + margin
                inner_bottom = y1 - margin - 1
                inner_left = x0 + margin
                inner_right = x1 - margin
                fill_cell_interior(
                    display,
                    inner_top,
                    inner_bottom,
                    inner_left,
                    inner_right,
                    value,
                )

                cv2.rectangle(
                    display,
                    (x0, y0),
                    (x1 - 1, y1 - 1),
                    (62, 62, 70),
                    layout["border_thickness"],
                )

                key_text, adc_text = cell_labels(row, col, value, self.cols)
                text_x = x0 + max(4, int(round(cell_width * 6 / 64)))
                for text_value, origin, font_scale in (
                    (
                        key_text,
                        (text_x, y0 + max(10, int(round(cell_height * 16 / 64)))),
                        layout["key_font_scale"],
                    ),
                    (
                        adc_text,
                        (text_x, y0 + max(19, int(round(cell_height * 31 / 64)))),
                        layout["adc_font_scale"],
                    ),
                ):
                    cv2.putText(
                        display,
                        text_value,
                        origin,
                        cv2.FONT_HERSHEY_SIMPLEX,
                        font_scale,
                        (0, 0, 0),
                        layout["text_shadow_thickness"],
                        cv2.LINE_AA,
                    )
                    cv2.putText(
                        display,
                        text_value,
                        origin,
                        cv2.FONT_HERSHEY_SIMPLEX,
                        font_scale,
                        (245, 245, 245),
                        layout["text_thickness"],
                        cv2.LINE_AA,
                    )

        status = "OK" if parsed["valid"] else "CHECKSUM ERR"
        info = (
            f"CNT:{parsed['counter']} {status} "
            f"Min:{grid.min():.0f} Max:{grid.max():.0f} Mean:{grid.mean():.1f} "
            f"Scale:{self.vmin}-{self.vmax} Threshold:{self.press_threshold}"
        )
        cv2.putText(
            display,
            info,
            (
                max(6, margin * 2),
                matrix_height + max(15, int(layout["status_height"] * 0.68)),
            ),
            cv2.FONT_HERSHEY_SIMPLEX,
            layout["status_font_scale"],
            (255, 255, 255),
            layout["text_thickness"],
            cv2.LINE_AA,
        )
        cv2.imshow(self.window_name, display)
        return cv2.waitKey(1)

    def is_open(self):
        try:
            return cv2.getWindowProperty(
                self.window_name, cv2.WND_PROP_VISIBLE
            ) >= 1
        except cv2.error:
            return False


def run_serial_viewer(
    port=DEFAULT_PORT,
    baudrate=DEFAULT_BAUDRATE,
    matrix_size=DEFAULT_MATRIX_SIZE,
):
    """Read frames from a serial port and display them in real time."""
    frame_size = frame_size_for(matrix_size, matrix_size)
    print(
        f"Opening serial port {port} @ {baudrate}, "
        f"matrix {matrix_size}x{matrix_size} ..."
    )
    ser = serial.Serial(port, baudrate, timeout=SERIAL_READ_TIMEOUT)
    ser.reset_input_buffer()
    print(f"Serial port opened: {ser.name}")
    viewer = HeatmapViewer(rows=matrix_size, cols=matrix_size)
    buf = b""
    frame_count = 0
    valid_count = 0
    fps_count = 0
    fps_time = time.time()

    try:
        while viewer.is_open():
            chunk = ser.read(ser.in_waiting or 1)
            if chunk:
                buf += chunk

            first_frame_number = frame_count
            latest, buf, parsed_now, valid_now, invalid_frames = (
                drain_serial_frames(
                    buf,
                    rows=matrix_size,
                    cols=matrix_size,
                )
            )
            frame_count += parsed_now
            fps_count += parsed_now
            valid_count += valid_now

            for local_number, parsed in invalid_frames:
                print(
                    f"[WARN] Frame #{first_frame_number + local_number} "
                    f"CRC32 mismatch: calc=0x{parsed['crc_calc']:08X}, "
                    f"recv=0x{parsed['crc_recv']:08X}"
                )

            if latest is not None:
                if viewer.update(latest) == 27:
                    return
            elif cv2.waitKey(1) == 27:
                break

            elapsed = time.time() - fps_time
            if elapsed >= 2.0:
                print(
                    f"FPS: {fps_count / elapsed:.1f} | "
                    f"Frames: {frame_count} | Valid: {valid_count}"
                )
                fps_count = 0
                fps_time = time.time()

            if len(buf) > frame_size * 10:
                buf = buf[-frame_size:]
    except KeyboardInterrupt:
        pass
    finally:
        ser.close()
        cv2.destroyAllWindows()
        print(f"Serial port closed. Parsed {frame_count} frames.")


def run_file_viewer(
    filepath: str, delay=0.05, matrix_size=DEFAULT_MATRIX_SIZE
):
    """Replay packed frames from a binary file."""
    with open(filepath, "rb") as source:
        buf = source.read()

    frame_size = frame_size_for(matrix_size, matrix_size)
    viewer = HeatmapViewer(rows=matrix_size, cols=matrix_size)
    frame_count = 0
    wait_ms = max(1, int(delay * 1000))
    try:
        while viewer.is_open() and len(buf) >= frame_size:
            parsed, buf = find_and_parse_frame(
                buf, rows=matrix_size, cols=matrix_size
            )
            if parsed is None:
                break
            frame_count += 1
            if parsed["valid"] and viewer.update(parsed) == 27:
                break
            if cv2.waitKey(wait_ms) == 27:
                break
    finally:
        cv2.destroyAllWindows()
        print(f"Parsed {frame_count} frames from {filepath}.")


def run_demo(
    num_frames=300, delay=0.05, matrix_size=DEFAULT_MATRIX_SIZE
):
    """Display generated data without a connected board."""
    frame_size_for(matrix_size, matrix_size)
    viewer = HeatmapViewer(rows=matrix_size, cols=matrix_size)
    try:
        for counter in range(num_frames):
            if not viewer.is_open():
                break
            rows, cols = np.indices((matrix_size, matrix_size))
            grid = 200 + 150 * np.sin(rows / 2 + counter * 0.08)
            grid *= 1 + 0.25 * np.cos(cols / 2 - counter * 0.04)
            grid = np.clip(grid, 0, 1019)
            parsed = {
                "valid": True,
                "counter": counter,
                "grid": grid,
                "raw": b"",
            }
            if viewer.update(parsed) == 27 or cv2.waitKey(max(1, int(delay * 1000))) == 27:
                break
    finally:
        cv2.destroyAllWindows()


def print_usage():
    print("Usage:")
    print("  python agr_8.py                              # COM8, 115200, 8x8")
    print("  python agr_8.py serial [port] [baud] [size: 8|16|32]")
    print("  python agr_8.py file <path> [delay] [size: 8|16|32]")
    print("  python agr_8.py demo [frames] [delay] [size: 8|16|32]")


def parse_launch_args(args):
    """Convert command-line arguments into a testable launch configuration."""
    if not args:
        return ("serial", DEFAULT_PORT, DEFAULT_BAUDRATE, DEFAULT_MATRIX_SIZE)
    if args[0] == "serial":
        port = args[1] if len(args) > 1 else DEFAULT_PORT
        baudrate = int(args[2]) if len(args) > 2 else DEFAULT_BAUDRATE
        matrix_size = int(args[3]) if len(args) > 3 else DEFAULT_MATRIX_SIZE
        frame_size_for(matrix_size, matrix_size)
        return ("serial", port, baudrate, matrix_size)
    if args[0] == "file" and len(args) >= 2:
        delay = float(args[2]) if len(args) > 2 else 0.05
        matrix_size = int(args[3]) if len(args) > 3 else DEFAULT_MATRIX_SIZE
        frame_size_for(matrix_size, matrix_size)
        return ("file", args[1], delay, matrix_size)
    if args[0] == "demo":
        frames = int(args[1]) if len(args) > 1 else 300
        delay = float(args[2]) if len(args) > 2 else 0.05
        matrix_size = int(args[3]) if len(args) > 3 else DEFAULT_MATRIX_SIZE
        frame_size_for(matrix_size, matrix_size)
        return ("demo", frames, delay, matrix_size)
    return ("usage",)


def main(args=None):
    """Launch the requested viewer mode; no arguments means default serial."""
    config = parse_launch_args(sys.argv[1:] if args is None else args)
    if config[0] == "serial":
        run_serial_viewer(config[1], config[2], config[3])
    elif config[0] == "file":
        run_file_viewer(config[1], config[2], config[3])
    elif config[0] == "demo":
        run_demo(config[1], config[2], config[3])
    else:
        print_usage()


if __name__ == "__main__":
    main()
