"""Command-line interface exposed to coding agents."""

from __future__ import annotations

import argparse
import json
import sys
from pathlib import Path

from .collect import collect_file
from .dosvm import DosVM, DosVMError, all_runs


def _vm(args: argparse.Namespace) -> DosVM:
    return DosVM.current(getattr(args, "run_id", None))


def build_parser() -> argparse.ArgumentParser:
    parser = argparse.ArgumentParser(prog="dosctl", description="Control the DOS 7.1 QEMU VM")
    sub = parser.add_subparsers(dest="action", required=True)

    start = sub.add_parser("start", help="start a disposable DOS VM")
    start.add_argument("--run-id")
    start.add_argument("--timeout", type=float, default=30.0)
    start.add_argument("--no-wait", action="store_true", help="do not wait for a DOS prompt")

    status = sub.add_parser("status", help="show VM status")
    status.add_argument("--run-id")
    status.add_argument("--all", action="store_true")

    execute = sub.add_parser("exec", help="execute a non-interactive DOS command")
    execute.add_argument("command")
    execute.add_argument("--run-id")
    execute.add_argument("--timeout", type=float, default=30.0)
    execute.add_argument(
        "--serial",
        action="store_true",
        help="capture long output over serial instead of leaving it on VGA",
    )

    type_parser = sub.add_parser("type", help="type text through the VGA keyboard")
    type_parser.add_argument("text")
    type_parser.add_argument("--run-id")
    type_parser.add_argument("--delay", type=float, default=0.04)

    key = sub.add_parser("key", help="send one or more named keys")
    key.add_argument("keys", nargs="+")
    key.add_argument("--run-id")
    key.add_argument("--delay", type=float, default=0.04)

    wait = sub.add_parser("wait", help="wait for a regex on the text screen")
    wait.add_argument("pattern")
    wait.add_argument("--run-id")
    wait.add_argument("--timeout", type=float, default=10.0)

    screen = sub.add_parser(
        "screen",
        help="read the display as ASCII (default), CP437 text, JSON, or PNG",
    )
    screen.add_argument("--run-id")
    screen_format = screen.add_mutually_exclusive_group()
    screen_format.add_argument(
        "--ascii",
        action="store_true",
        help="emit 7-bit ASCII (the default; retained for compatibility)",
    )
    screen_format.add_argument("--text", action="store_true", help="emit decoded CP437 text")
    screen_format.add_argument("--json", action="store_true", help="emit cells and attributes")
    screen_format.add_argument(
        "--png",
        nargs="?",
        const="",
        metavar="OUTPUT",
        help="save a PNG, optionally to OUTPUT",
    )

    shot = sub.add_parser("screenshot", help="save a PNG display screenshot")
    shot.add_argument("output", nargs="?")
    shot.add_argument("--run-id")

    collect = sub.add_parser("collect", help="copy a guest file out through mTCP NC")
    collect.add_argument("dos_path")
    collect.add_argument("host_path", nargs="?")
    collect.add_argument("--run-id")
    collect.add_argument("--timeout", type=float, default=30.0)

    stop = sub.add_parser("stop", help="stop one or all VMs")
    stop.add_argument("--run-id")
    stop.add_argument("--all", action="store_true")
    stop.add_argument("--force", action="store_true")

    return parser


def main(argv: list[str] | None = None) -> int:
    args = build_parser().parse_args(argv)
    try:
        if args.action == "start":
            vm = DosVM.start(
                run_id=args.run_id,
                wait_for_prompt=not args.no_wait,
                timeout=args.timeout,
            )
            print(json.dumps(vm.status(), indent=2))
        elif args.action == "status":
            if args.all:
                print(json.dumps([vm.status() for vm in all_runs()], indent=2))
            else:
                print(json.dumps(_vm(args).status(), indent=2))
        elif args.action == "exec":
            vm = _vm(args)
            output = (
                vm.exec_serial(args.command, timeout=args.timeout)
                if args.serial
                else vm.exec(args.command, timeout=args.timeout)
            )
            print(output)
        elif args.action == "type":
            _vm(args).type(args.text, delay=args.delay)
        elif args.action == "key":
            _vm(args).key(*args.keys, delay=args.delay)
        elif args.action == "wait":
            print(_vm(args).wait_for(args.pattern, timeout=args.timeout).text())
        elif args.action == "screen":
            vm = _vm(args)
            if args.png is not None:
                output = Path(args.png) if args.png else None
                print(vm.screenshot(output))
            else:
                current = vm.screen()
                if args.json:
                    print(json.dumps(current.as_dict(), ensure_ascii=False))
                elif args.text:
                    print(current.text(trim_blank_rows=True))
                else:
                    print(current.ascii(trim_blank_rows=True))
        elif args.action == "screenshot":
            output = Path(args.output) if args.output else None
            print(_vm(args).screenshot(output))
        elif args.action == "collect":
            vm = _vm(args)
            destination = Path(args.host_path) if args.host_path else Path(vm.run_dir) / Path(args.dos_path).name
            print(collect_file(vm, args.dos_path, destination, timeout=args.timeout))
        elif args.action == "stop":
            targets = all_runs() if args.all else [_vm(args)]
            for vm in targets:
                was_alive = vm.is_alive()
                vm.stop(force=args.force)
                if was_alive:
                    print(f"stopped {vm.run_id}")
        return 0
    except (DosVMError, OSError, ValueError, TimeoutError) as exc:
        print(f"dosctl: {exc}", file=sys.stderr)
        return 1


if __name__ == "__main__":
    raise SystemExit(main())
