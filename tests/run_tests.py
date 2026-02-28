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
    """hello prints a greeting containing 'Hello'."""
    child.sendline('hello')
    try:
        child.expect('Hello', timeout=TIMEOUT_CMD)
        wait_prompt(child)
        return True, 'output contains "Hello"'
    except pexpect.TIMEOUT:
        return False, 'no "Hello" in output'


def test_ls(child: pexpect.spawn):
    """ls lists bin/ directory."""
    child.sendline('ls')
    try:
        child.expect(PROMPT, timeout=TIMEOUT_CMD)
        output = child.before
        if 'bin/' not in output:
            return False, 'bin/ not listed'
        return True, 'found bin/'
    except pexpect.TIMEOUT:
        return False, 'timeout waiting for ls output'


def test_xxd(child: pexpect.spawn):
    """xxd BOOT.TXT prints a hex dump starting with an offset."""
    child.sendline('xxd BOOT.TXT')
    try:
        # Hex dump lines start with "00000000:"
        child.expect('00000000:', timeout=TIMEOUT_CMD)
        wait_prompt(child)
        return True, 'hex dump starts with offset 00000000:'
    except pexpect.TIMEOUT:
        return False, 'no hex dump output'


def test_xxd_missing_file(child: pexpect.spawn):
    """xxd on a non-existent file prints an error."""
    child.sendline('xxd NOSUCHFILE.TXT')
    try:
        child.expect('cannot open', timeout=TIMEOUT_CMD)
        wait_prompt(child)
        return True, 'printed "cannot open"'
    except pexpect.TIMEOUT:
        return False, 'no error message for missing file'


def test_vi_quit(child: pexpect.spawn):
    """vi opens the editor; :q! returns to the shell."""
    child.sendline('vi test.txt')
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
    """t_segflt accesses kernel memory and is killed with 'Segmentation fault'."""
    child.sendline('t_segflt')
    try:
        child.expect('Segmentation fault', timeout=TIMEOUT_CMD)
        wait_prompt(child)
        return True, 'printed "Segmentation fault" and returned to shell'
    except pexpect.TIMEOUT:
        return False, 'no "Segmentation fault" message'


def test_paths(child: pexpect.spawn):
    """Absolute and relative path support for file access and navigation."""

    # 1. Read a file via absolute path from root
    child.sendline('xxd /bin/hello')
    try:
        child.expect('00000000:', timeout=TIMEOUT_CMD)
        wait_prompt(child)
    except pexpect.TIMEOUT:
        return False, 'xxd /bin/hello: no hex dump'

    # 2. Change directory via absolute path
    if not send_cmd(child, 'cd /bin'):
        return False, 'cd /bin did not return prompt'

    # 3. ls inside /bin should list hello
    child.sendline('ls')
    try:
        child.expect(PROMPT, timeout=TIMEOUT_CMD)
        if 'hello' not in child.before:
            return False, 'hello not found after cd /bin'
    except pexpect.TIMEOUT:
        return False, 'ls timed out after cd /bin'

    # 4. Read a file by name from inside /bin (relative, sanity check)
    child.sendline('xxd hello')
    try:
        child.expect('00000000:', timeout=TIMEOUT_CMD)
        wait_prompt(child)
    except pexpect.TIMEOUT:
        return False, 'xxd hello failed from inside /bin'

    # 5. Jump back to root via absolute path
    if not send_cmd(child, 'cd /'):
        return False, 'cd / failed'

    # 6. Create a nested directory and file, then read it back via path
    if not send_cmd(child, 'mkdir pathtest'):
        return False, 'mkdir pathtest failed'

    child.sendline('vi /pathtest/deep.txt')
    try:
        child.expect(pexpect.TIMEOUT, timeout=2)
    except pexpect.TIMEOUT:
        pass
    child.send('i')
    child.send('pathdata')
    child.send('\x1b')
    child.send(':wq\r')
    if not wait_prompt(child):
        return False, 'vi /pathtest/deep.txt :wq failed'

    child.sendline('xxd /pathtest/deep.txt')
    try:
        child.expect('00000000:', timeout=TIMEOUT_CMD)
        wait_prompt(child)
    except pexpect.TIMEOUT:
        return False, 'xxd /pathtest/deep.txt: no hex dump'

    # 7. Delete file and directory via paths
    child.sendline('rm /pathtest/deep.txt')
    try:
        child.expect(r'\[y/N\]', timeout=TIMEOUT_CMD)
    except pexpect.TIMEOUT:
        return False, 'rm /pathtest/deep.txt: no [y/N]'
    child.send('y')
    if not wait_prompt(child):
        return False, 'rm /pathtest/deep.txt failed'

    child.sendline('rm /pathtest')
    try:
        child.expect(r'\[y/N\]', timeout=TIMEOUT_CMD)
    except pexpect.TIMEOUT:
        return False, 'rm /pathtest: no [y/N]'
    child.send('y')
    if not wait_prompt(child):
        return False, 'rm /pathtest failed'

    return True, 'xxd /bin/hello, cd /bin, vi /dir/file, xxd/rm via paths all work'


def test_free(child: pexpect.spawn):
    """free reports physical and virtual memory statistics."""
    child.sendline('free')
    try:
        child.expect(PROMPT, timeout=TIMEOUT_CMD)
    except pexpect.TIMEOUT:
        return False, 'free did not return to shell'

    out = child.before

    if 'Phys:' not in out:
        return False, 'Phys: line missing'
    if 'Virt:' not in out:
        return False, 'Virt: line missing'
    # PMM manages 0x100000–0x7FFFFFF = 32512 frames × 4 kB = 130048 kB
    if '130048' not in out:
        return False, 'expected phys total 130048 kB not found'
    if 'kB' not in out:
        return False, 'kB unit missing'
    # Exactly two processes are active: shell + free
    if '(2 procs)' not in out:
        return False, '(2 procs) not found — expected shell + free'

    return True, 'Phys/Virt rows present, total=130048 kB, kB units, (2 procs)'


def test_malloc(child: pexpect.spawn):
    """malloc_test: alloc, write, free+reuse, large alloc, over-limit → NULL."""
    child.sendline('t_mall1')
    try:
        child.expect('malloc: OK', timeout=20)
        wait_prompt(child)
        return True, 'alloc, free+reuse, large alloc, exhaustion all passed'
    except pexpect.TIMEOUT:
        return False, 'malloc_test did not print "malloc: OK"'


def test_malloc_oob(child: pexpect.spawn):
    """malloc_oob: accessing unmapped heap causes segfault."""
    child.sendline('t_mall2')
    try:
        child.expect('Segmentation fault', timeout=TIMEOUT_CMD)
        wait_prompt(child)
        return True, 'unmapped heap access caused segfault'
    except pexpect.TIMEOUT:
        return False, 'no segfault for unmapped heap access'


def test_panic(child: pexpect.spawn):
    """t_panic utility triggers kernel panic; [PANIC] message appears on serial."""
    child.sendline('t_panic kernel panic test')
    try:
        child.expect(r'\[PANIC\] kernel panic test', timeout=TIMEOUT_CMD)
        return True, 'kernel panic triggered, [PANIC] confirmed on serial'
    except pexpect.TIMEOUT:
        return False, '[PANIC] message not seen on serial'
    except pexpect.EOF:
        return False, 'unexpected EOF waiting for panic output'


def test_fs_operations(child: pexpect.spawn):
    """mkdir, create file in subdir via vi, rm file, cd .., rm dir."""

    # 1. Create directory
    if not send_cmd(child, 'mkdir testdir'):
        return False, 'mkdir did not return prompt'

    # 2. ls should list testdir/
    child.sendline('ls')
    try:
        child.expect('testdir/', timeout=TIMEOUT_CMD)
        wait_prompt(child)
    except pexpect.TIMEOUT:
        return False, 'testdir/ not visible in ls after mkdir'

    # 3. cd into testdir — prompt becomes /testdir>
    if not send_cmd(child, 'cd testdir'):
        return False, 'cd testdir failed'

    # 4. Create a file via vi
    child.sendline('vi testfile.txt')
    try:
        child.expect(pexpect.TIMEOUT, timeout=2)
    except pexpect.TIMEOUT:
        pass
    child.send('i')           # enter insert mode
    child.send('hello')       # type content
    child.send('\x1b')        # ESC — back to normal mode
    child.send(':wq\r')       # save and quit
    if not wait_prompt(child):
        return False, 'vi :wq did not return to prompt'

    # 5. ls inside testdir should show the new file
    child.sendline('ls')
    try:
        child.expect('testfile.txt', timeout=TIMEOUT_CMD)
        wait_prompt(child)
    except pexpect.TIMEOUT:
        return False, 'testfile.txt not found in ls after vi :wq'

    # 6. Delete the file (rm prompts [y/N])
    child.sendline('rm testfile.txt')
    try:
        child.expect(r'\[y/N\]', timeout=TIMEOUT_CMD)
    except pexpect.TIMEOUT:
        return False, 'rm did not show [y/N] prompt'
    child.send('y')
    if not wait_prompt(child):
        return False, 'rm testfile.txt failed'

    # 7. Go back to root
    if not send_cmd(child, 'cd ..'):
        return False, 'cd .. failed'

    # 8. Delete the now-empty directory
    child.sendline('rm testdir')
    try:
        child.expect(r'\[y/N\]', timeout=TIMEOUT_CMD)
    except pexpect.TIMEOUT:
        return False, 'rm testdir did not show [y/N] prompt'
    child.send('y')
    if not wait_prompt(child):
        return False, 'rm testdir failed'

    # 9. ls at root should no longer show testdir
    child.sendline('ls')
    try:
        child.expect(PROMPT, timeout=TIMEOUT_CMD)
    except pexpect.TIMEOUT:
        return False, 'ls timed out after rm testdir'
    if 'testdir' in child.before:
        return False, 'testdir still visible in ls after deletion'

    return True, 'mkdir / create file / rm file / rm dir all succeeded'


# ── test registry ──────────────────────────────────────────────────────────────

TESTS = [
    ('boot',              test_boot),
    ('unknown_command',   test_unknown_command),
    ('hello',             test_hello),
    ('ls',                test_ls),
    ('xxd',               test_xxd),
    ('xxd_missing_file',  test_xxd_missing_file),
    ('vi_quit',           test_vi_quit),
    ('t_segflt',          test_segfault),
    ('fs_operations',     test_fs_operations),
    ('paths',             test_paths),
    ('free',              test_free),
    ('t_mall1',           test_malloc),
    ('t_mall2',           test_malloc_oob),
    ('t_panic',           test_panic),   # must be last — halts the system
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
