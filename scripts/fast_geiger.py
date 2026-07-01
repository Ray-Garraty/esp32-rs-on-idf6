"""Fast unsafe-usage audit for dependencies — no compilation needed."""
import json
import subprocess
import sys
from pathlib import Path

def count_unsafe_in_dir(dir_path):
    try:
        r = subprocess.run(
            ["rg", "--count", "unsafe", "--type", "rust", str(dir_path)],
            capture_output=True, text=True, timeout=15,
        )
        if r.returncode == 0 and r.stdout.strip():
            return sum(int(line.split(":")[-1]) for line in r.stdout.strip().split("\n") if line)
        return 0
    except Exception:
        return 0

def main():
    meta = json.loads(
        subprocess.check_output(
            ["cargo", "metadata", "--format-version", "1"],
            text=True,
        )
    )

    root_name = meta["resolve"]["root"].split()[0]
    dep_dirs = []
    for pkg in meta["packages"]:
        if pkg["name"] == root_name:
            continue
        manifest = Path(pkg["manifest_path"])
        src_dir = manifest.parent / "src"
        if src_dir.is_dir():
            dep_dirs.append((pkg["name"], pkg["version"], src_dir))

    total_unsafe = 0
    results = []
    for name, version, src_dir in sorted(dep_dirs, key=lambda x: x[0]):
        count = count_unsafe_in_dir(src_dir)
        if count > 0:
            total_unsafe += count
            results.append((name, version, count))

    print(f"\n{'='*60}")
    print("  Dependency Unsafe Usage Report (fast mode)")
    print(f"{'='*60}")
    for name, version, count in sorted(results, key=lambda x: -x[2]):
        print(f"  {name:<35} {version:<12} {count:>5} unsafe uses")
    print(f"{'='*60}")
    print(f"  Packages with unsafe: {len(results)}")
    print(f"  Total unsafe uses:   {total_unsafe}")
    print(f"{'='*60}")

if __name__ == "__main__":
    main()
