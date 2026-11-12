"""Writes the Claude Code conversations of this project as Markdown, a file a session and a day (claude/YYYY-MM-DD-N-adam.md, the day in UTC,
N the number of the session); the pre-commit hook (.githooks/pre-commit) runs it before every commit.  Taken over from pinball-port's claude/export.py
(2026-11-12).

The numbers are kept in claude/sessions.txt, a line a session: `N <session id>`, or `- <session id>` for a session left out.
A session not listed yet gets the next number (the sessions in the order of their first message), so a number never changes once given.
Only what the person and Claude wrote goes in: no tool calls and results, no hidden reasoning, no system reminders.
A message the person typed while Claude was working is stored by Claude Code as a queue entry, not as a message: those are read too (once: a queued message that
arrives later as a message of its own is not repeated).  The messages of the sub-agents and the notices of the harness are left out.
The sessions are read from ~/.claude/projects/<this folder>/*.jsonl; where there are none (another machine), nothing is written.
"""
import json
import pathlib
import re
import sys
from datetime import datetime

HERE = pathlib.Path(__file__).resolve().parent
REPO = HERE.parent
SESSIONS = pathlib.Path.home() / ".claude" / "projects" / re.sub(r"[^A-Za-z0-9]", "-", str(REPO))
NUMBERS = HERE / "sessions.txt"

REMINDER = re.compile(r"<system-reminder>.*?</system-reminder>", re.S)
TAGS = re.compile(r"</?(?:pasted_content|command-name|command-message|command-args|local-command-stdout|local-command-caveat)[^>]*>")
HARNESS = ("<task-notification", "<agent-message", "[SYSTEM NOTIFICATION")                # (what the harness or a sub-agent wrote, not the person)
SAME_MESSAGE = 600                                                                         # seconds: a queued message and the message of the same text that it became


def text_of(content):
    """The text of a message: a string, or the text blocks of a list (no tool calls or results, no thinking)."""
    if isinstance(content, str):
        return content
    return "\n\n".join(b.get("text", "") for b in content if isinstance(b, dict) and b.get("type") == "text")


def clean(text):
    return TAGS.sub("", REMINDER.sub("", text)).strip()


def moment(stamp):
    try:
        return datetime.fromisoformat(stamp.replace("Z", "+00:00")).timestamp()
    except ValueError:
        return 0


def read(path):
    """-> (rows [(kind, UTC time, text)], tool calls per day {day: n})."""
    rows, tools, queued = [], {}, []
    for line in path.open(encoding="utf-8"):
        try:
            d = json.loads(line)
        except ValueError:
            continue
        kind, stamp = d.get("type"), d.get("timestamp", "")
        if kind == "queue-operation" and d.get("operation") == "enqueue":
            text = clean(d.get("content") or "")
            if text and not text.startswith(HARNESS) and not any(h in text[:300] for h in HARNESS):
                queued.append((stamp, text))
            continue
        if kind not in ("user", "assistant") or d.get("isSidechain"):
            continue
        content = (d.get("message") or {}).get("content")
        if kind == "assistant" and isinstance(content, list):
            tools[stamp[:10]] = tools.get(stamp[:10], 0) + sum(1 for b in content if isinstance(b, dict) and b.get("type") == "tool_use")
        text = clean(text_of(content or ""))
        if not text:
            continue
        if kind == "user":
            if (d.get("origin") or {}).get("kind") not in (None, "human"):        # (a message the harness wrote, not the person)
                continue
            if text.startswith(HARNESS) or any(h in text[:300] for h in HARNESS):
                continue
            if text.startswith("This session is being continued from a previous conversation"):
                rows.append(("note", stamp, "*The earlier part of the conversation was summarised here to make room (the summary is not reproduced).*"))
                continue
            if text.startswith("[Request interrupted by user"):
                rows.append(("note", stamp, "*[Request interrupted by the user]*"))
                continue
        rows.append((kind, stamp, text))
    known = [(moment(s), t) for k, s, t in rows if k == "user"]
    for stamp, text in queued:                                                       # (what the person typed while Claude was working)
        if not any(t == text and abs(m - moment(stamp)) < SAME_MESSAGE for m, t in known):
            rows.append(("user", stamp, text))
            known.append((moment(stamp), text))
    rows.sort(key=lambda row: row[1])
    rows = [row for i, row in enumerate(rows) if row not in rows[:i][-12:]]        # (a resumed session holds a copy of the messages before it: the same message once)
    return rows, tools


def numbers(sessions):
    """{session id: number or None (left out)}, from claude/sessions.txt; the sessions not in it get the next numbers and are written into it."""
    given = {}
    if NUMBERS.exists():
        for line in NUMBERS.read_text(encoding="utf-8").splitlines():
            parts = line.split()
            if len(parts) >= 2 and (parts[0] == "-" or parts[0].isdigit()):
                given[parts[1]] = None if parts[0] == "-" else int(parts[0])
    new = sorted((first, sid) for sid, first in sessions.items() if sid not in given)
    if new:
        n = max([v for v in given.values() if v] + [0])
        with NUMBERS.open("a", encoding="utf-8", newline="\r\n") as f:
            for _, sid in new:
                n += 1
                given[sid] = n
                f.write(f"{n} {sid}\n")
    return given


def main():
    files = sorted(SESSIONS.glob("*.jsonl")) if SESSIONS.is_dir() else []
    read_ = {path.stem: read(path) for path in files}
    read_ = {sid: rt for sid, rt in read_.items() if rt[0]}
    if not read_:
        print("no Claude Code sessions for this folder: nothing exported")
        return
    given = numbers({sid: rows[0][1] for sid, (rows, _) in read_.items()})
    written = files_ = messages = calls = 0
    for sid, (rows, tools) in read_.items():
        n = given.get(sid)
        if n is None:
            continue
        days = {}
        for row in rows:
            days.setdefault(row[1][:10], []).append(row)
        for day, items in days.items():
            out = [
                f"# pxfe: the Claude Code conversation (adam), {day}, session {n}",
                "",
                f"Exported from the session `{sid}`.",
                f"Only what the person and Claude wrote is here: the {tools.get(day, 0)} tool calls of the day (commands, file reads and edits, searches) and their results, the hidden reasoning and the system reminders are left out.",
                "",
                "---",
                "",
            ]
            for kind, stamp, text in items:
                if kind == "note":
                    out += [text, ""]
                    continue
                out += [f"### {'Ádám' if kind == 'user' else 'Claude'}" + (f" · {stamp[11:16]} UTC" if stamp else ""), "", text, ""]
            target, body = HERE / f"{day}-{n}-adam.md", "\n".join(out).replace("\r\n", "\n").replace("\n", "\r\n")          # CRLF, also inside the messages (Adam 2026-11-02)
            if not target.exists() or target.read_bytes().decode("utf-8") != body:
                target.write_bytes(body.encode("utf-8"))
                written += 1
            files_ += 1
        messages += len(rows)
        calls += sum(tools.values())
    print(f"{files_} files in {HERE.name}/ ({written} written): {messages} messages, {calls} tool calls")


if __name__ == "__main__":
    sys.exit(main())
