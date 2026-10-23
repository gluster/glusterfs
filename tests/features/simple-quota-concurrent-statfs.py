#!/usr/bin/env python3
"""Exercise quota persistence while allocation deltas change sign."""

import concurrent.futures
import os
import sys
import threading


def exercise(directory):
    stop = threading.Event()
    payload = b"x" * (1024 * 1024)

    def statfs():
        while not stop.is_set():
            os.statvfs(directory)

    def churn(index):
        path = os.path.join(directory, "file-" + str(index))
        for _ in range(300):
            with open(path, "wb") as stream:
                stream.write(payload)
            os.unlink(path)

    with concurrent.futures.ThreadPoolExecutor(max_workers=12) as executor:
        readers = [executor.submit(statfs) for _ in range(8)]
        try:
            writers = [executor.submit(churn, index) for index in range(4)]
            for writer in writers:
                writer.result()
        finally:
            stop.set()
        for reader in readers:
            reader.result()

    # Flush the last unlink delta before checking the brick's persisted value.
    os.statvfs(directory)
    if os.listdir(directory):
        raise AssertionError("test files were not removed")


if __name__ == "__main__":
    exercise(sys.argv[1])
