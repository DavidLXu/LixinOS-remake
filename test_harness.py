"""Exercise guest networking, model feedback, trial boot and journal recovery."""
import http.server
import json
from pathlib import Path
import tempfile
import threading

from test import boot, disk_record, expect

ANCHOR = 'if (same(cmd,"version")) banner();'
WRONG = 'if (same(cmd,"ping")) say("wrong\\n");\n    else ' + ANCHOR
RIGHT = WRONG.replace("wrong", "pong")


class Provider(http.server.BaseHTTPRequestHandler):
    def log_message(self, *args):
        pass

    def do_POST(self):
        assert self.path == "/v1/chat/completions"
        query = json.loads(self.rfile.read(int(self.headers["Content-Length"])))
        assert query["model"] == "fixture"
        turn = json.loads(query["messages"][-1]["content"])
        self.server.turns.append(turn)
        index = len(self.server.turns)
        patch = None
        if turn["task"] == "Exercise query repair":
            if not turn["diagnostics"]:
                patch = {"expression": "missing_identifier"}
            else:
                patch = {"expression": '"fixed"'}
        elif turn["task"] == "Exercise source lookup":
            if "static void banner(" not in query["messages"][1]["content"]:
                patch = {"read":"banner"}
            else:
                patch = {"query":"looked", "expression":'"yes"', "expected":"yes"}
        elif turn["task"] == "Exercise stalled candidate":
            if "trial interrupted" not in turn["diagnostics"]:
                before, after = ANCHOR, 'if (same(cmd,"alive")) { for (;;) {} }\n    else ' + ANCHOR
            else:
                before, after = 'if (same(cmd,"alive")) { for (;;) {} }', 'if (same(cmd,"alive")) say("yes\\n");'
        elif turn["task"] == "Exercise accumulated tests":
            if "candidate output differs" not in turn["diagnostics"]:
                before, after = RIGHT, RIGHT.replace('"pong', '"broken') + '\n    else if (same(cmd,"extra")) say("two\\n");'
            else:
                before, after = 'say("broken\\n");', 'say("pong\\n");'
        elif index == 3:
            self.send_error(503)
            return
        elif index == 1:
            before, after = ANCHOR, 'if (@) banner();'
        elif index == 2:
            before, after = 'if (@) banner();', WRONG
        else:
            before, after = WRONG, RIGHT
        if patch is None:
            patch = dict(before=before, after=after, command="ping", expected="wrong")
        envelope = dict(choices=[dict(message=dict(content=json.dumps(patch)))])
        body = json.dumps(envelope).encode()
        self.send_response(200)
        self.send_header("Transfer-Encoding", "chunked")
        self.end_headers()
        for off in range(0, len(body), 53):
            part = body[off:off+53]
            self.wfile.write(f"{len(part):x}\r\n".encode()+part+b"\r\n")
        self.wfile.write(b"0\r\n\r\n")


def exercise(disk, server):
    with boot(disk) as c:
        expect(c, None, "1.3.0 build=0")
        expect(c, f"agent model http://10.0.2.2:{server.server_port}/v1/chat/completions fixture", "model saved")
        c("agent test ping|pong")
        expect(c, "agent run Add the ping query", "provider HTTP 503")
        expect(c, "version", "build=0 slot=A")
        expect(c, "agent log", "candidate output differs")
    with boot(disk) as c:
        expect(c, None, "build=0 slot=A hot=0")
        expect(c, "agent status", "goal=Add the ping query")
        expect(c, "agent resume", "agent: committed")
        expect(c, "ping", "\npong\n")
        expect(c, "agent verify", "PASS")
        expect(c, "agent diff", 'say("pong')
    assert len(server.turns) == 4
    assert "identifier" in server.turns[1]["diagnostics"] or "TCC compilation failed" in server.turns[1]["diagnostics"]
    assert "candidate output differs" in server.turns[3]["diagnostics"]
    with boot(disk) as c:
        expect(c, None, "build=1 slot=B hot=0")
        expect(c, "agent status", "phase=done")
        expect(c, "ping", "\npong\n")
        c("agent test extra|two")
        expect(c, "agent run Exercise accumulated tests", "agent: committed")
        expect(c, "extra", "\ntwo\n")
        expect(c, "ping", "\npong\n")
        expect(c, "agent tests", "extra|two")
        expect(c, "rollback", "slot=B")
        expect(c, "ping", "\npong\n")
        assert "\ntwo\n" not in c("extra")
    assert RIGHT.encode() in disk_record(disk, 1)[1]
    with boot(disk.with_name("watchdog.img")) as c:
        expect(c, None, "build=0")
        c(f"agent model http://10.0.2.2:{server.server_port}/v1/chat/completions fixture")
        c("agent test alive|yes")
        expect(c, "agent run Exercise stalled candidate", "build=0 slot=A hot=0")
        expect(c, "agent status", "trial interrupted")
        expect(c, "agent resume", "agent: committed")
        expect(c, "alive", "\nyes\n")
        c("agent test repair|fixed")
        expect(c, "agent query Exercise query repair", "agent: committed")
        expect(c, "repair", "\nfixed\n")
        c("agent test looked|yes")
        expect(c, "agent run Exercise source lookup", "reading banner")
        expect(c, "looked", "\nyes\n")


if __name__ == "__main__":
    with http.server.HTTPServer(("127.0.0.1", 0), Provider) as server, tempfile.TemporaryDirectory() as tmp:
        server.turns = []
        worker = threading.Thread(target=server.serve_forever, daemon=True)
        worker.start()
        try:
            exercise(Path(tmp)/"guest.img",server)
        finally:
            server.shutdown()
            worker.join()
    print("PASS: guest model API, compiler repair, failed trial, network outage, cold resume, accepted behavior and rollback")
