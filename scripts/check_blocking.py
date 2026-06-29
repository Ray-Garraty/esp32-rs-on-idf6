import re
import sys

BLOCKING_PATTERNS = [
    r'\.send_and_wait\(',
    r'\.lock\(\)\.unwrap\(\)',
    r'\.recv\(\)',
    r'std::thread::sleep\([^1]',
    r'\.wait\(\)',
]

FORBIDDEN_FILES = [
    'src/main.rs',
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
