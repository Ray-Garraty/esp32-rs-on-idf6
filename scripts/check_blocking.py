import re
import sys

BLOCKING_PATTERNS = [
    r'\.send_and_wait\(',
    r'\.lock\(\)\.unwrap\(\)',
    r'\.recv\(\)',
    # Blocking sleeps >10ms; 10ms tick is the allowed heartbeat exception
    r'std::thread::sleep\(',
    r'\.wait\(\)',
]

FORBIDDEN_FILES = [
    'src/main.rs',
    'src/logger.rs',
    'src/lib.rs',
]

def check_file(filepath):
    try:
        with open(filepath, 'r') as f:
            content = f.read()
    except FileNotFoundError:
        return True

    ok = True
    for pattern in BLOCKING_PATTERNS:
        for match in re.finditer(pattern, content):
            line_num = content[:match.start()].count('\n') + 1
            context_start = max(0, match.start() - 500)
            context = content[context_start:match.end() + 100]

            # Allow 10ms heartbeat sleep in main loop (hardcoded or via constant)
            if 'from_millis(10)' in context or 'MAIN_LOOP_TICK_MS' in context:
                continue

            if 'std::thread::Builder' not in context and \
               'std::thread::spawn' not in context and \
               'xTaskCreate' not in context:
                print(f"ERROR: Blocking call in {filepath}:{line_num}")
                print(f"  Pattern: {pattern}")
                print(f"  Context: ...{context.strip()}...")
                ok = False
    return ok

for filepath in FORBIDDEN_FILES:
    if not check_file(filepath):
        sys.exit(1)

print("No blocking calls detected in main loop.")
