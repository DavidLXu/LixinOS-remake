"""Optional guest-to-model acceptance on a disposable disk. All editing occurs in the guest."""
import argparse
import os
import tempfile
from pathlib import Path

from test import boot, expect


def main():
    parser=argparse.ArgumentParser()
    parser.add_argument("--url",default="https://api.deepseek.com/chat/completions")
    parser.add_argument("--model",default="deepseek-flash")
    parser.add_argument("--query-only",action="store_true")
    args=parser.parse_args()
    key=os.environ.get("LIXINOS_MODEL_KEY")
    if key and not args.url.startswith("https://"):
        parser.error("An API key requires an HTTPS endpoint")
    if args.query_only:
        command, output = "color", "blue"
        goal = "agent query Add the read-only command color. Its C expression should return the string literal blue."
    else:
        command, output = "note", "memo="
        goal = ("agent run In query_command change the displayed note label from note= to memo=. "
                "Edit just the string literal, keeping the rest of the line unchanged.")
    with tempfile.TemporaryDirectory() as tmp:
        disk=Path(tmp)/"live.img"
        with boot(disk,timeout=360) as c:
            c()
            c(f"agent model {args.url} {args.model}")
            if key:
                c("agent key",secret=key)
            expect(c,"agent probe","provider HTTP 200")
            c(f"agent test {command}|{output}")
            expect(c,goal,"agent: committed")
            expect(c,command,f"\n{output}\n")
        with boot(disk) as c:
            expect(c,None,"build=1")
            expect(c,command,f"\n{output}\n")
            expect(c,"agent verify","PASS")
        print("PASS: live model changed kernel behavior; cold boot verified")


if __name__=="__main__":
    main()
