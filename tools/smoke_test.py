#!/usr/bin/env python3
"""Smoke test: boot the mod on a dedicated server, add 2 RED puppet bots that cast spells, and check the
dungeon loop (floor spawn -> clear or wipe) runs without crashing. Driven over RCON.
Setup once with tools/smoke_setup.sh. Run inside `nix develop`:  python3 tools/smoke_test.py [map] [seconds]"""
import math, os, re, socket, struct, subprocess, sys, time

MAP = sys.argv[1] if len(sys.argv) > 1 else "koth_viaduct_event"
SECS = int(sys.argv[2]) if len(sys.argv) > 2 else 150
DS = os.environ.get("SRCDS_DIR", os.path.expanduser("~/srcds"))
REPO = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
LOG, PW = f"{DS}/mod_tf_test/console.log", "smoke"

def rcon(cmd):
    s = socket.create_connection(("127.0.0.1", 27015), timeout=5)
    def send(i, t, body): s.sendall(struct.pack("<iii", 10 + len(body), i, t) + body.encode() + b"\0\0")
    def recv():
        n = struct.unpack("<i", s.recv(4))[0]; d = b""
        while len(d) < n: d += s.recv(n - len(d))
        return struct.unpack("<ii", d[:8]) + (d[8:-2].decode(errors="replace"),)
    send(1, 3, PW); recv(); assert recv()[0] == 1, "rcon auth failed"
    send(2, 2, cmd); time.sleep(0.3)
    try: out = recv()[2]
    except Exception: out = ""
    s.close(); return out

def log(): return open(LOG, errors="replace").read() if os.path.exists(LOG) else ""

if os.path.exists(LOG): os.remove(LOG)
srv = subprocess.Popen(["podman", "run", "--rm", "-i", "--userns=keep-id", "--network=host", "--name=dungeon-smoke",
    "--mount", f"type=bind,source={REPO},target={REPO}", "--mount", f"type=bind,source={DS},target={DS}",
    "-w", f"{DS}/sdkbase", "registry.gitlab.steamos.cloud/steamrt/sniper/sdk:latest", "bash", "-c",
    f"LD_LIBRARY_PATH=bin/linux64:{REPO}/game/bin/linux64 timeout {SECS + 300} ./srcds_linux64 -game {DS}/mod_tf_test "
    f"-console -condebug -insecure -nomaster -norestart -port 27015 +ip 127.0.0.1 +sv_lan 1 +maxplayers 8 "
    f"+rcon_password {PW} +map {MAP}"], stdin=subprocess.DEVNULL, stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL)
try:
    t0 = time.time()
    while "VAC secure mode" not in log():   # srcds console only reaches -condebug's log, not stdout
        assert srv.poll() is None, "server exited during boot"
        assert time.time() - t0 < 300, "server boot timeout"
        time.sleep(2)

    for cmd in ["sv_cheats 1", "mp_waitingforplayers_cancel 1",
                "bot -team red -class soldier -name Goku", "bot -team red -class medic -name Vegeta"]:
        rcon(cmd); time.sleep(1)
    rcon("bot_forceattack 1")   # bots hold MOUSE1: fireballs

    start = time.time()
    while time.time() - start < SECS and srv.poll() is None:
        st = rcon("tf_dungeon_status")
        # Puppet bots can't walk or aim, and monsters ignore anyone in a spawn room: park each bot 300u
        # behind a skeleton along the bot's own view direction.
        eyes = {n: (float(p), float(y)) for n, p, y in re.findall(r"player (\w+) hp=\d+ at \([-\d ]+\) eyes \(([-\d.]+) ([-\d.]+)\)", st)}
        skels = [tuple(map(float, g)) for g in re.findall(r"tf_zombie hp=\d+ at \(([-\d]+) ([-\d]+) ([-\d]+)\)", st)]
        for bot, (x, y, z) in zip([b for b in ("Goku", "Vegeta") if b in eyes], skels):
            pitch, yaw = eyes[bot]
            rcon(f"bot_teleport {bot} {x - 300 * math.cos(math.radians(yaw)):.0f} {y - 300 * math.sin(math.radians(yaw)):.0f} {z + 8:.0f} {pitch} {yaw} 0")
        time.sleep(10)
    alive = srv.poll() is None
    if alive: rcon("quit"); time.sleep(5)
finally:
    subprocess.run(["podman", "rm", "-f", "dungeon-smoke"], capture_output=True)

events = [l for l in log().splitlines() if l.startswith("[Dungeon] ") and "mode=" not in l and not l.startswith("[Dungeon]   ")]
print("\n".join(events))
assert alive, f"server died (rc={srv.returncode})"
assert any("FLOOR 1" in e for e in events), "no floor ever spawned"
assert any("cleared" in e or "wiped" in e for e in events), "no floor ever ended (clear or wipe)"
print("SMOKE TEST PASSED")
