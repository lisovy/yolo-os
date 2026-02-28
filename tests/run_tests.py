#!/usr/bin/env python3
"""
YOLO-OS automated test suite.

Spawns QEMU with -serial stdio and -device isa-debug-exit, sends commands
via the serial port (which the kernel reads as keyboard input), and checks
for expected output.  Requires: python3-pexpect, qemu-system-x86.

Usage:
    python3 tests/run_tests.py [--disk path/to/disk.img]

Exit code: 0 = all tests passed, 1 = one or more failed.
"""

import sys
import os
import argparse
import pexpect

# ── constants ──────────────────────────────────────────────────────────────────

QEMU      = 'qemu-system-i386'
PROMPT    = '> '
BOOT_MSG  = 'Welcome to the YOLO-OS'
TIMEOUT_BOOT = 15   # seconds to wait for the OS to boot and show a prompt
TIMEOUT_CMD  =  8   # seconds to wait for a command to produce expected output

# QEMU exits with (0x31 << 1) | 1 = 99 when the kernel runs __exit
QEMU_EXIT_CODE = 99

# ── helpers ────────────────────────────────────────────────────────────────────

def spawn_qemu(disk_img: str) -> pexpect.spawn:
    args = [
        '-drive', f'file={disk_img},format=raw,if=ide',
        '-serial', 'stdio',
        '-display', 'none',
        '-no-reboot',
        '-device', 'isa-debug-exit,iobase=0xf4,iosize=0x04',
    ]
    child = pexpect.spawn(QEMU, args, timeout=TIMEOUT_BOOT,
                          encoding='utf-8', codec_errors='replace')
    # Uncomment to see raw QEMU output while debugging:
    # child.logfile = sys.stdout
    return child


def wait_prompt(child: pexpect.spawn, timeout: int = TIMEOUT_CMD) -> bool:
    try:
        child.expect(PROMPT, timeout=timeout)
        return True
    except pexpect.TIMEOUT:
        return False
    except pexpect.EOF:
        return False


def send_cmd(child: pexpect.spawn, cmd: str) -> bool:
    """Send a shell command and wait for the next prompt."""
    child.sendline(cmd)
    return wait_prompt(child, TIMEOUT_CMD)


# ── test functions ─────────────────────────────────────────────────────────────
# Each receives a child already positioned AT the shell prompt ("> ").
# Returns (passed: bool, detail: str).

def test_boot(child: pexpect.spawn):
    """OS boots, prints welcome message and shell prompt."""
    # child was already advanced past the first prompt in main(); nothing to send.
    return True, 'got shell prompt'


def test_unknown_command(child: pexpect.spawn):
    """Unknown command prints 'unknown command'."""
    child.sendline('thisdoesnotexist')
    try:
        child.expect('unknown command', timeout=TIMEOUT_CMD)
        wait_prompt(child)
        return True, 'printed "unknown command"'
    except pexpect.TIMEOUT:
        return False, 'no "unknown command" response'


def test_hello(child: pexpect.spawn):
    """run hello prints a greeting containing 'Hello'."""
    child.sendline('run hello')
    try:
        child.expect('Hello', timeout=TIMEOUT_CMD)
        wait_prompt(child)
        return True, 'output contains "Hello"'
    except pexpect.TIMEOUT:
        return False, 'no "Hello" in output'


def test_ls(child: pexpect.spawn):
    """ls lists the expected binaries."""
    child.sendline('ls')
    expected = ['hello.bin', 'xxd.bin', 'vi.bin', 'demo.bin', 'segfault.bin']
    try:
        # Collect output until next prompt
        child.expect(PROMPT, timeout=TIMEOUT_CMD)
        output = child.before
        missing = [f for f in expected if f not in output]
        if missing:
            return False, f'missing files: {missing}'
        return True, f'found {expected}'
    except pexpect.TIMEOUT:
        return False, 'timeout waiting for ls output'


def test_xxd(child: pexpect.spawn):
    """run xxd BOOT.TXT prints a hex dump starting with an offset."""
    child.sendline('run xxd BOOT.TXT')
    try:
        # Hex dump lines start with "00000000:"
        child.expect('00000000:', timeout=TIMEOUT_CMD)
        wait_prompt(child)
        return True, 'hex dump starts with offset 00000000:'
    except pexpect.TIMEOUT:
        return False, 'no hex dump output'


def test_xxd_missing_file(child: pexpect.spawn):
    """run xxd on a non-existent file prints an error."""
    child.sendline('run xxd NOSUCHFILE.TXT')
    try:
        child.expect('cannot open', timeout=TIMEOUT_CMD)
        wait_prompt(child)
        return True, 'printed "cannot open"'
    except pexpect.TIMEOUT:
        return False, 'no error message for missing file'


def test_vi_quit(child: pexpect.spawn):
    """run vi opens the editor; :q! returns to the shell."""
    child.sendline('run vi test.txt')
    # vi redraws the screen; wait a moment for it to settle then send :q!
    try:
        # vi shows a status bar — we just wait a bit for any output
        child.expect(pexpect.TIMEOUT, timeout=1)
    except pexpect.TIMEOUT:
        pass
    # Send :q! to force-quit
    child.send(':')
    child.send('q')
    child.send('!')
    child.send('\r')
    try:
        child.expect(PROMPT, timeout=TIMEOUT_CMD)
        return True, ':q! returned to shell'
    except pexpect.TIMEOUT:
        return False, 'did not return to shell after :q!'


def test_segfault(child: pexpect.spawn):
    """run segfault accesses kernel memory and is killed with 'Segmentation fault'."""
    child.sendline('run segfault')
    try:
        child.expect('Segmentation fault', timeout=TIMEOUT_CMD)
        wait_prompt(child)
        return True, 'printed "Segmentation fault" and returned to shell'
    except pexpect.TIMEOUT:
        return False, 'no "Segmentation fault" message'


# ── test registry ──────────────────────────────────────────────────────────────

TESTS = [
    ('boot',              test_boot),
    ('unknown_command',   test_unknown_command),
    ('hello',             test_hello),
    ('ls',                test_ls),
    ('xxd',               test_xxd),
    ('xxd_missing_file',  test_xxd_missing_file),
    ('vi_quit',           test_vi_quit),
    ('segfault',          test_segfault),
]

# ── main ───────────────────────────────────────────────────────────────────────

def main():
    parser = argparse.ArgumentParser(description='YOLO-OS test suite')
    parser.add_argument('--disk', default='disk.img',
                        help='path to disk image (default: disk.img)')
    args = parser.parse_args()

    if not os.path.exists(args.disk):
        print(f'ERROR: disk image not found: {args.disk}', file=sys.stderr)
        sys.exit(1)

    print(f'Booting {args.disk} in QEMU …')
    child = spawn_qemu(args.disk)

    # ── boot ────────────────────────────────────────────────────────────────
    try:
        child.expect(BOOT_MSG, timeout=TIMEOUT_BOOT)
    except pexpect.TIMEOUT:
        print('FAIL  boot  —  OS did not print welcome message (timeout)')
        child.close(force=True)
        sys.exit(1)

    if not wait_prompt(child, TIMEOUT_BOOT):
        print('FAIL  boot  —  no shell prompt after welcome message')
        child.close(force=True)
        sys.exit(1)

    # ── run tests ────────────────────────────────────────────────────────────
    passed = 0
    failed = 0

    for name, fn in TESTS:
        try:
            ok, detail = fn(child)
        except (pexpect.TIMEOUT, pexpect.EOF) as e:
            ok, detail = False, f'exception: {e}'

        status = 'PASS' if ok else 'FAIL'
        colour = '\033[32m' if ok else '\033[31m'
        reset  = '\033[0m'
        print(f'  {colour}{status}{reset}  {name:<25}  {detail}')

        if ok:
            passed += 1
        else:
            failed += 1

    # ── clean shutdown ───────────────────────────────────────────────────────
    try:
        child.sendline('__exit')
        child.expect(pexpect.EOF, timeout=5)
    except (pexpect.TIMEOUT, pexpect.EOF):
        pass
    child.close(force=True)

    # ── summary ──────────────────────────────────────────────────────────────
    total = passed + failed
    print(f'\n{passed}/{total} tests passed', end='')
    if failed:
        print(f'  ({failed} failed)')
        sys.exit(1)
    else:
        print()
        sys.exit(0)


if __name__ == '__main__':
    main()
