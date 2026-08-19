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


def _structured(parser: argparse.ArgumentParser) -> None:
    parser.add_argument(
        "--output-json",
        action="store_true",
        help="emit a structured JSON result",
    )


def build_parser() -> argparse.ArgumentParser:
    parser = argparse.ArgumentParser(prog="dosctl", description="Control the DOS 7.1 QEMU VM")
    sub = parser.add_subparsers(dest="action", required=True)

    start = sub.add_parser("start", help="start a disposable DOS VM")
    start.add_argument("--run-id")
    start.add_argument("--timeout", type=float, default=30.0)
    start.add_argument("--no-wait", action="store_true", help="do not wait for a DOS prompt")
    _structured(start)

    status = sub.add_parser("status", help="show VM status")
    status.add_argument("--run-id")
    status.add_argument("--all", action="store_true")
    _structured(status)

    execute = sub.add_parser("exec", help="execute a non-interactive DOS command")
    execute.add_argument("command")
    execute.add_argument("--run-id")
    execute.add_argument("--timeout", type=float, default=30.0)
    execute.add_argument(
        "--serial",
        action="store_true",
        help="capture long output over serial instead of leaving it on VGA",
    )
    _structured(execute)

    launch = sub.add_parser("launch", help="start an interactive foreground program")
    launch.add_argument("command")
    launch.add_argument("--run-id")
    launch.add_argument("--timeout", type=float, default=10.0)

    type_parser = sub.add_parser("type", help="type text through the VGA keyboard")
    type_parser.add_argument("text")
    type_parser.add_argument("--run-id")
    type_parser.add_argument("--delay", type=float, default=0.04)
    _structured(type_parser)

    key = sub.add_parser("key", help="tap one or more named keys")
    key.add_argument("keys", nargs="+")
    key.add_argument("--run-id")
    key.add_argument("--delay", type=float, default=0.04)
    key.add_argument("--hold-ms", type=int, default=20)
    _structured(key)

    keydown = sub.add_parser("keydown", help="hold named keys until keyup")
    keydown.add_argument("keys", nargs="+")
    keydown.add_argument("--run-id")

    keyup = sub.add_parser("keyup", help="release keys held by keydown")
    keyup.add_argument("keys", nargs="+")
    keyup.add_argument("--run-id")

    wait = sub.add_parser("wait", help="wait for a regex on the text screen")
    wait.add_argument("pattern")
    wait.add_argument("--run-id")
    wait.add_argument("--timeout", type=float, default=10.0)
    _structured(wait)

    display = sub.add_parser("display", help="print the current display mode")
    display.add_argument("--run-id")

    screen = sub.add_parser(
        "screen",
        help="print text, or save a PNG and print its path in graphics mode",
    )
    screen.add_argument("--run-id")
    screen.add_argument("--output", metavar="PATH", help="write the result to a file")
    screen_format = screen.add_mutually_exclusive_group()
    screen_format.add_argument(
        "--ascii",
        action="store_true",
        help="emit 7-bit ASCII (the default in text modes)",
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
    _structured(shot)

    collect = sub.add_parser("collect", help="copy a guest file out through mTCP NC")
    collect.add_argument("dos_path")
    collect.add_argument("host_path", nargs="?")
    collect.add_argument("--run-id")
    collect.add_argument("--timeout", type=float, default=30.0)
    _structured(collect)

    stop = sub.add_parser("stop", help="stop one or all VMs")
    stop.add_argument("--run-id")
    stop.add_argument("--all", action="store_true")
    stop.add_argument("--force", action="store_true")
    _structured(stop)

    return parser


def _emit(args: argparse.Namespace, text: str, structured: dict) -> None:
    if getattr(args, "output_json", False):
        print(json.dumps(structured, ensure_ascii=False))
    elif text:
        print(text)


def main(argv: list[str] | None = None) -> int:
    args = build_parser().parse_args(argv)
    try:
        if args.action == "start":
            vm = DosVM.start(
                run_id=args.run_id,
                wait_for_prompt=not args.no_wait,
                timeout=args.timeout,
            )
            _emit(args, f"started {vm.run_id}", vm.status())
        elif args.action == "status":
            statuses = [vm.status() for vm in all_runs()] if args.all else [_vm(args).status()]
            if args.output_json:
                print(json.dumps(statuses if args.all else statuses[0], indent=2))
            else:
                for status in statuses:
                    state = "running" if status["alive"] else "stopped"
                    pid = f" pid={status['pid']}" if status.get("pid") else ""
                    print(f"{status['run_id']} {state}{pid}")
        elif args.action == "exec":
            vm = _vm(args)
            output = (
                vm.exec_serial(args.command, timeout=args.timeout)
                if args.serial
                else vm.exec(args.command, timeout=args.timeout)
            )
            _emit(
                args,
                output,
                {"run_id": vm.run_id, "command": args.command, "backend": "serial" if args.serial else "vga", "output": output},
            )
        elif args.action == "launch":
            vm = _vm(args)
            result = vm.launch(args.command, timeout=args.timeout)
            if result.video.kind == "graphics":
                image = vm.screenshot(vm.run_dir / "launch.png")
                print(f"{result.video.summary()} screenshot={image}")
            else:
                state = "returned-to-prompt" if result.prompt_returned else "foreground"
                print(f"{result.video.summary()} state={state}")
        elif args.action == "type":
            vm = _vm(args)
            vm.type(args.text, delay=args.delay)
            _emit(args, "", {"run_id": vm.run_id, "typed": args.text})
        elif args.action == "key":
            vm = _vm(args)
            vm.key(*args.keys, delay=args.delay, hold_ms=args.hold_ms)
            _emit(args, "", {"run_id": vm.run_id, "keys": args.keys})
        elif args.action == "keydown":
            _vm(args).key_down(*args.keys)
        elif args.action == "keyup":
            _vm(args).key_up(*args.keys)
        elif args.action == "wait":
            vm = _vm(args)
            text = vm.wait_for(args.pattern, timeout=args.timeout).text()
            _emit(args, text, {"run_id": vm.run_id, "pattern": args.pattern, "text": text})
        elif args.action == "display":
            print(_vm(args).video_info().summary())
        elif args.action == "screen":
            vm = _vm(args)
            if args.png is not None:
                output = Path(args.png) if args.png else (Path(args.output) if args.output else None)
                print(vm.screenshot(output))
            elif not (args.ascii or args.text or args.json) and vm.video_info().kind == "graphics":
                output = Path(args.output) if args.output else None
                print(vm.screenshot(output))
            else:
                current = vm.screen()
                if args.json:
                    rendered = json.dumps(current.as_dict(), ensure_ascii=False)
                elif args.text:
                    rendered = current.text(trim_blank_rows=True)
                else:
                    rendered = current.ascii(trim_blank_rows=True)
                if args.output:
                    destination = Path(args.output)
                    destination.parent.mkdir(parents=True, exist_ok=True)
                    destination.write_text(rendered + "\n", encoding="utf-8")
                    print(destination.resolve())
                else:
                    print(rendered)
        elif args.action == "screenshot":
            output = Path(args.output) if args.output else None
            vm = _vm(args)
            result = vm.screenshot(output)
            _emit(args, str(result), {"run_id": vm.run_id, "path": str(result)})
        elif args.action == "collect":
            vm = _vm(args)
            dos_name = args.dos_path.replace("\\", "/").rstrip("/").split("/")[-1]
            destination = Path(args.host_path) if args.host_path else Path(vm.run_dir) / dos_name
            result = collect_file(vm, args.dos_path, destination, timeout=args.timeout)
            _emit(
                args,
                str(result),
                {"run_id": vm.run_id, "dos_path": args.dos_path, "host_path": str(result), "size": result.stat().st_size},
            )
        elif args.action == "stop":
            targets = all_runs() if args.all else [_vm(args)]
            stopped = []
            for vm in targets:
                was_alive = vm.is_alive()
                vm.stop(force=args.force)
                if was_alive:
                    stopped.append(vm.run_id)
            if args.output_json:
                print(json.dumps({"stopped": stopped}))
            else:
                for run_id in stopped:
                    print(f"stopped {run_id}")
        return 0
    except (DosVMError, OSError, ValueError, TimeoutError) as exc:
        print(f"dosctl: {exc}", file=sys.stderr)
        return 1


if __name__ == "__main__":
    raise SystemExit(main())
