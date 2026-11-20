#!/usr/bin/env python3
"""Fetch the third-party binaries listed in blobs.json (kept out of git) and verify their sha256.

    python3 scripts/fetch_blobs.py [--check]

Run by `make` (target `blobs`) before anything else. A file already in place with the right sha256 is kept; a missing or
different one is downloaded to a temporary file, checked, then moved into place. Any mismatch or download error -> exit 1
(the build fails). --check: only verify the files in place, download nothing.
"""
import hashlib, json, os, sys, tempfile, time, urllib.request

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))


def sha256(path):
    h = hashlib.sha256()
    with open(path, "rb") as f:
        for chunk in iter(lambda: f.read(1 << 20), b""):
            h.update(chunk)
    return h.hexdigest()


def download(url, dst):
    last = None
    for attempt in range(3):
        try:
            req = urllib.request.Request(url, headers={"User-Agent": "dow2-patches-fetch-blobs"})
            with urllib.request.urlopen(req, timeout=60) as r, open(dst, "wb") as f:
                while True:
                    chunk = r.read(1 << 20)
                    if not chunk:
                        return
                    f.write(chunk)
        except OSError as e:
            last = e
            time.sleep(2 * (attempt + 1))
    raise OSError(f"{url}: {last}")


def main():
    check_only = "--check" in sys.argv[1:]
    blobs = json.load(open(os.path.join(ROOT, "blobs.json")))["blobs"]
    bad = 0
    for b in blobs:
        rel, want = b["path"], b["sha256"].lower()
        if os.path.isabs(rel) or ".." in rel.replace("\\", "/").split("/"):
            print(f"blobs: {rel}: bad path", file=sys.stderr); bad += 1; continue
        path = os.path.join(ROOT, rel)
        if os.path.isfile(path) and sha256(path) == want:
            print(f"blobs: {rel} ok")
            continue
        if check_only:
            print(f"blobs: {rel}: missing or sha256 mismatch", file=sys.stderr); bad += 1; continue
        os.makedirs(os.path.dirname(path), exist_ok=True)
        fd, tmp = tempfile.mkstemp(dir=os.path.dirname(path), prefix=".blob-")
        os.close(fd)
        try:
            download(b["url"], tmp)
            got = sha256(tmp)
            if got != want:
                print(f"blobs: {rel}: sha256 MISMATCH from {b['url']}: got {got}, want {want}", file=sys.stderr)
                bad += 1
                continue
            os.chmod(tmp, 0o644)
            os.replace(tmp, path)
            print(f"blobs: {rel} downloaded, sha256 ok")
        except OSError as e:
            print(f"blobs: {rel}: download failed: {e}", file=sys.stderr)
            bad += 1
        finally:
            if os.path.exists(tmp):
                os.unlink(tmp)
    sys.exit(1 if bad else 0)


if __name__ == "__main__":
    main()
