#!/usr/bin/env python3
"""
Pre-commit check: detect blocking calls in the main loop.

Uses line-by-line brace-depth tracking to distinguish blocking calls
in dedicated threads (std::thread::spawn, std::thread::Builder::new()
.spawn(), xTaskCreate) from blocking calls in the main loop.

Exception: std::thread::sleep(Duration::from_millis(10)) is allowed
as the main-loop heartbeat tick.
"""

import re
import sys


BLOCKING_PATTERNS = [
    r'\.send_and_wait\(',
    r'\.lock\(\)\.unwrap\(\)',
    r'\.recv\(\)',
    r'std::thread::sleep\(',
    r'\.wait\(',
]

FORBIDDEN_FILES = [
    'src/main.rs',
    'src/logger.rs',
    'src/lib.rs',
]

THREAD_SPAWN_PATTERNS = [
    r'\.spawn\(',
    r'std::thread::spawn\(',
    r'xTaskCreate\(',
]


def check_file(filepath):
    try:
        with open(filepath, 'r') as f:
            lines = f.readlines()
    except FileNotFoundError:
        return True

    ok = True
    thread_depth = 0
    brace_depth_in_thread = 0
    inside_thread = False
    entered_this_line = False

    for line_num, line in enumerate(lines, 1):
        entered_this_line = False

        # Check for thread spawn on this line
        for pattern in THREAD_SPAWN_PATTERNS:
            if re.search(pattern, line):
                thread_depth += 1
                inside_thread = True
                brace_depth_in_thread = 0
                entered_this_line = True
                break

        # Track braces if inside a thread context
        if inside_thread and not entered_this_line:
            for ch in line:
                if ch == '{':
                    brace_depth_in_thread += 1
                elif ch == '}':
                    brace_depth_in_thread -= 1
            # Check if we've exited the thread closure
            if brace_depth_in_thread <= 0:
                thread_depth -= 1
                if thread_depth <= 0:
                    thread_depth = 0
                    inside_thread = False

        # Count opening braces on spawn line (e.g. .spawn(move || {)
        if entered_this_line:
            for ch in line:
                if ch == '{':
                    brace_depth_in_thread += 1
                elif ch == '}':
                    brace_depth_in_thread -= 1

        # Check blocking patterns
        for pattern in BLOCKING_PATTERNS:
            for match in re.finditer(pattern, line):
                # Allow 10ms heartbeat sleep (MAIN_LOOP_TICK_MS or from_millis(10))
                ok_heartbeat = 'MAIN_LOOP_TICK_MS' in line or 'from_millis(10)' in line
                if ok_heartbeat:
                    continue

                if thread_depth == 0:
                    print(f"ERROR: Blocking call in {filepath}:{line_num}")
                    print(f"  Pattern: {pattern}")
                    print(f"  Line: {line.strip()}")
                    ok = False

    return ok


def main():
    for filepath in FORBIDDEN_FILES:
        if not check_file(filepath):
            sys.exit(1)
    print("No blocking calls detected in main loop.")


if __name__ == '__main__':
    main()
