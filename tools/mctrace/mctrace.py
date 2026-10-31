#!/usr/bin/env python3
# SimpleFPEWrapper - tools/mctrace/mctrace.py
# Copyright (c) 2026 MobileGL-Dev
# SPDX-License-Identifier: LGPL-3.0-only
"""Real Minecraft frames as benchmarks: install, capture, trim.

Synthetic benchmarks only measure the call shapes someone thought to write
down. This captures the GL stream a real game issues for one frame, so the
wrapper can be measured - and profiled with its performance counters - on
exactly what a player's frame asks of it.

    mctrace.py install <version> [--optifine]
        Fetch the client, its libraries, natives and assets from Mojang (and
        OptiFine from optifine.net), plus the matching dedicated server.
    mctrace.py capture <instance> [--seconds N]
        Start the server, join it from the client under a headless X server
        with apitrace recording, then cut the recording after a late, fully
        loaded in-game frame: frames/<instance>.trace. A replay rebuilds the
        game's state from everything before that frame and then loops it
        (apitrace replay --loop), so the frame is measured exactly as the
        game issued it. apitrace's gltrim would make a smaller trace of the
        frame alone, but it cannot follow display lists created in ranges
        and called through glCallLists, which is how 1.7.10 draws terrain.
    mctrace.py setup [--android NDK]
        Build what replaying needs: egl_es_shim.c (the libEGL stand-in that
        puts a GLES context and the wrapper under eglretrace), apitrace's
        retracers with apitrace-glcalllists-remap.patch, without which
        display-list terrain replays as nothing on any driver whose
        glGenLists disagrees with the recording one, and with
        apitrace-headless.patch, without which every swap waits on the
        desktop's compositor, and apitrace-skip-draws.patch, which replays
        the frames before the measured one without drawing them; and gl4es
        as launchers embed it, to measure against (gl4es-swap-hooks.patch). With --android, also
        the shim and a headless eglretrace for Android arm64
        (apitrace-headless.patch), for `bench --device`.
    mctrace.py bench [<instance>...] --wrapper LIB [--counters LIB] [--device [SERIAL]]
        Replay each frame through the wrapper and natively, looping the
        final frame, and report its wall and CPU time for both and the
        wrapper's cost over the native driver; check the wrapper's image
        against the reference; and with a performance-counters build of the
        wrapper (-DSFPEW_PERF_COUNTERS=ON), profile exactly the looped
        frames. With --device, the same on an Android phone over adb, with
        Android builds of the wrapper: timed and profiled only, as there is
        no native desktop GL there and correctness is the host's to check.
        Every run replays the whole lead-in to the measured frame - most of
        a minute on a phone - so a device bench does one round unless
        --rounds asks for more, and profiles only with --counters.
        Results go to results/<date>/ as Markdown and JSON.
    mctrace.py list
        Show installed instances and captured frames.

Everything lands under $MCTRACE_HOME (default ~/.cache/sfpew-mctrace):
cache/ holds what was downloaded, instances/<id>/ a runnable game and server,
traces/ the full recordings and frames/ the trimmed one-frame traces.

The client runs in offline mode against its own local server, which is
started with online-mode=false, a fixed seed, a fixed time of day, clear
weather and no mobs, so a capture shows an ordinary loaded world rather than
whatever the server happened to be doing. Requires apitrace, xvfb-run and a
Java 8 runtime ($MCTRACE_JAVA, or the first one found under /usr/lib/jvm).
Downloading and running the game assumes a Minecraft license.
"""

import argparse
import glob
import gzip
import hashlib
import json
import os
import re
import shutil
import signal
import struct
import tempfile
import subprocess
import sys
import threading
import time
import urllib.request
import zipfile

MANIFEST = "https://piston-meta.mojang.com/mc/game/version_manifest_v2.json"
RESOURCES = "https://resources.download.minecraft.net"
LIBRARIES = "https://libraries.minecraft.net"
MAVEN = "https://repo1.maven.org/maven2"

# The OptiFine build used for each version: the last release for it.
OPTIFINE = {
    "1.7.10": "OptiFine_1.7.10_HD_U_E7",
    "1.12.2": "OptiFine_1.12.2_HD_U_G5",
    "1.16.5": "OptiFine_1.16.5_HD_U_G8",
}
# What OptiFine's tweaker runs under when the OptiFine jar does not carry its
# own launchwrapper (1.7.10): Mojang's, which needs ASM beside it.
LAUNCHWRAPPER = [
    ("net/minecraft/launchwrapper/1.12/launchwrapper-1.12.jar", LIBRARIES),
    ("org/ow2/asm/asm-all/5.0.3/asm-all-5.0.3.jar", MAVEN),
]

HOME = os.path.abspath(os.environ.get("MCTRACE_HOME", os.path.expanduser("~/.cache/sfpew-mctrace")))
CACHE = os.path.join(HOME, "cache")
USERNAME = "sfpew"
OFFLINE_UUID = "d1b8f6c4-5f86-4c2a-9c8e-5b0a8f6f2b1a"
SEED = "-4530634556500121041"
WIDTH, HEIGHT = 1280, 720


def log(message):
    print(f"[mctrace] {message}", flush=True)


# ---- downloads ----------------------------------------------------------

def fetch(url, path, sha1=None):
    """Downloads url to path unless a file with the right hash is there."""
    if os.path.exists(path) and (sha1 is None or sha1_of(path) == sha1):
        return path
    os.makedirs(os.path.dirname(path), exist_ok=True)
    partial = f"{path}.part{threading.get_ident()}"
    # Thousands of small asset downloads meet the odd truncated transfer;
    # the hash catches it and a retry fixes it.
    for attempt in range(3):
        try:
            with urllib.request.urlopen(url, timeout=60) as response, open(partial, "wb") as out:
                shutil.copyfileobj(response, out)
            if sha1 is None or sha1_of(partial) == sha1:
                os.replace(partial, path)
                return path
        except OSError:
            if attempt == 2:
                raise
        if os.path.exists(partial):
            os.unlink(partial)
    raise RuntimeError(f"hash mismatch for {url}")


def fetch_json(url, path, sha1=None):
    with open(fetch(url, path, sha1)) as f:
        return json.load(f)


def sha1_of(path):
    digest = hashlib.sha1()
    with open(path, "rb") as f:
        for block in iter(lambda: f.read(1 << 20), b""):
            digest.update(block)
    return digest.hexdigest()


def parallel(jobs, workers=16):
    """Runs (callable, args) jobs on a few threads; raises the first error."""
    jobs, errors, lock = list(jobs), [], threading.Lock()

    def worker():
        while True:
            with lock:
                if not jobs or errors:
                    return
                fn, args = jobs.pop()
            try:
                fn(*args)
            except Exception as error:  # noqa: BLE001 - reported below
                with lock:
                    errors.append(error)

    threads = [threading.Thread(target=worker) for _ in range(workers)]
    for t in threads:
        t.start()
    for t in threads:
        t.join()
    if errors:
        raise errors[0]


# ---- version metadata ---------------------------------------------------

def version_json(version):
    manifest = fetch_json(MANIFEST, os.path.join(CACHE, "version_manifest_v2.json"))
    entry = next((v for v in manifest["versions"] if v["id"] == version), None)
    if entry is None:
        raise SystemExit(f"unknown Minecraft version {version}")
    return fetch_json(entry["url"], os.path.join(CACHE, "versions", version, version + ".json"), entry["sha1"])


def rule_allows(rules):
    """Mojang's library/argument rules, evaluated for a Linux machine with
    no optional features."""
    if not rules:
        return True
    allowed = False
    for rule in rules:
        os_ok = "os" not in rule or rule["os"].get("name") == "linux"
        features_ok = "features" not in rule
        if os_ok and features_ok:
            allowed = rule["action"] == "allow"
    return allowed


def libraries(meta):
    """(path, url, sha1, is_native) for every library the client needs."""
    out = []
    for lib in meta["libraries"]:
        if not rule_allows(lib.get("rules")):
            continue
        downloads = lib.get("downloads", {})
        if "artifact" in downloads:
            a = downloads["artifact"]
            out.append((a["path"], a["url"], a["sha1"], False))
        native = lib.get("natives", {}).get("linux")
        if native:
            native = native.replace("${arch}", "64")
            c = downloads.get("classifiers", {}).get(native)
            if c:
                out.append((c["path"], c["url"], c["sha1"], True))
    return out


# ---- install ------------------------------------------------------------

def install(version, optifine):
    meta = version_json(version)
    instance = version + ("-optifine" if optifine else "")
    root = os.path.join(HOME, "instances", instance)
    log(f"installing {instance} into {root}")

    vdir = os.path.join(CACHE, "versions", version)
    client = fetch(meta["downloads"]["client"]["url"], os.path.join(vdir, "client.jar"),
                   meta["downloads"]["client"]["sha1"])
    server = fetch(meta["downloads"]["server"]["url"], os.path.join(vdir, "server.jar"),
                   meta["downloads"]["server"]["sha1"])

    libs = libraries(meta)
    libdir = os.path.join(CACHE, "libraries")
    parallel((fetch, (url, os.path.join(libdir, path), sha1)) for path, url, sha1, _ in libs)

    # Natives are unpacked per instance: LWJGL 2 and 3 ship differently named
    # files, and an instance must not pick up another version's.
    natives = os.path.join(root, "natives")
    shutil.rmtree(natives, ignore_errors=True)
    os.makedirs(natives)
    for path, _, _, is_native in libs:
        if not is_native and not path.endswith("natives-linux.jar"):
            continue
        with zipfile.ZipFile(os.path.join(libdir, path)) as jar:
            for name in jar.namelist():
                if name.endswith(".so"):
                    with open(os.path.join(natives, os.path.basename(name)), "wb") as f:
                        f.write(jar.read(name))

    assets = install_assets(meta)
    classpath = [os.path.join(libdir, path) for path, _, _, native in libs if not native]
    main_class = meta["mainClass"]
    extra_args = []
    if optifine:
        classpath, main_class, extra_args = install_optifine(version, client, classpath, root)
    classpath.append(client)

    game = os.path.join(root, "game")
    os.makedirs(game, exist_ok=True)
    write_options(game, version)
    write_server(os.path.join(root, "server"), server)

    launch = {
        "version": version,
        "instance": instance,
        "classpath": classpath,
        "main": main_class,
        "natives": natives,
        "game_args": game_arguments(meta, instance, game, assets) + extra_args,
    }
    with open(os.path.join(root, "launch.json"), "w") as f:
        json.dump(launch, f, indent=2)
    log(f"installed {instance}")


def install_assets(meta):
    index = meta["assetIndex"]
    root = os.path.join(CACHE, "assets")
    data = fetch_json(index["url"], os.path.join(root, "indexes", index["id"] + ".json"), index["sha1"])
    objects = data["objects"]
    # Several names can share one object; fetch each hash once.
    hashes = {o["hash"] for o in objects.values()}
    parallel((fetch, (f"{RESOURCES}/{h[:2]}/{h}", os.path.join(root, "objects", h[:2], h), h))
             for h in hashes)
    # Pre-1.7.3-style indexes (1.7.10's "1.7.10") read from a virtual tree
    # laid out by name rather than by hash.
    if data.get("virtual") or data.get("map_to_resources"):
        virtual = os.path.join(root, "virtual", index["id"])
        for name, o in objects.items():
            target = os.path.join(virtual, name)
            if not os.path.exists(target):
                os.makedirs(os.path.dirname(target), exist_ok=True)
                shutil.copyfile(os.path.join(root, "objects", o["hash"][:2], o["hash"]), target)
    return {"root": root, "index": index["id"], "virtual": os.path.join(root, "virtual", index["id"])}


def install_optifine(version, client, classpath, root):
    """What OptiFine's installer does, without its GUI: patch the vanilla
    jar into the OptiFine library, and launch through a launchwrapper with
    OptiFine's tweaker."""
    name = OPTIFINE[version]
    jar = os.path.join(CACHE, "optifine", name + ".jar")
    if not os.path.exists(jar):
        os.makedirs(os.path.dirname(jar), exist_ok=True)
        page = urllib.request.urlopen(f"https://optifine.net/adloadx?f={name}.jar", timeout=60).read().decode()
        link = re.search(r"downloadx\?f=[^\"'&]+&x=[0-9a-f]+", page)
        if link is None:
            raise SystemExit(f"no download link for {name} on optifine.net")
        fetch(f"https://optifine.net/{link.group(0)}", jar)

    patched = os.path.join(root, f"{name}-patched.jar")
    if not os.path.exists(patched):
        subprocess.run([java(), "-cp", jar, "optifine.Patcher", client, jar, patched], check=True,
                       stdout=subprocess.DEVNULL)

    with zipfile.ZipFile(jar) as z:
        bundled = [n for n in z.namelist() if n.startswith("launchwrapper-of-") and n.endswith(".jar")]
        wrapper = []
        if bundled:
            target = os.path.join(root, bundled[0])
            with open(target, "wb") as f:
                f.write(z.read(bundled[0]))
            wrapper = [target]
    if not wrapper:
        for path, base in LAUNCHWRAPPER:
            wrapper.append(fetch(f"{base}/{path}", os.path.join(CACHE, "libraries", path)))
    return [patched] + wrapper + classpath, "net.minecraft.launchwrapper.Launch", [
        "--tweakClass", "optifine.OptiFineTweaker"]


def game_arguments(meta, instance, game, assets):
    values = {
        "auth_player_name": USERNAME, "version_name": instance, "game_directory": game,
        "assets_root": assets["root"], "game_assets": assets["virtual"], "assets_index_name": assets["index"],
        "auth_uuid": OFFLINE_UUID.replace("-", ""), "auth_access_token": "0", "auth_session": "0",
        "user_type": "legacy", "version_type": "release", "user_properties": "{}",
    }
    if "minecraftArguments" in meta:
        args = meta["minecraftArguments"].split()
    else:
        args = [a for a in meta["arguments"]["game"] if isinstance(a, str)]
    filled = []
    for arg in args:
        for key, value in values.items():
            arg = arg.replace("${" + key + "}", value)
        filled.append(arg)
    return filled + ["--width", str(WIDTH), "--height", str(HEIGHT)]


def write_options(game, version):
    """A settled, repeatable client: fixed view distance, no vsync or frame
    cap, no first-run tutorials, no audio."""
    options = {
        "renderDistance": "8", "fancyGraphics": "true", "enableVsync": "false", "maxFps": "260",
        "fboEnable": "true", "guiScale": "2", "tutorialStep": "none", "skipMultiplayerWarning": "true",
        "joinedFirstServer": "true", "onboardAccessibility": "false", "soundCategory_master": "0.0",
        "showInventoryAchievementHint": "false",
        "pauseOnLostFocus": "false",
        # The language key went lower-case in 1.11.
        "lang": "en_US" if version_tuple(version) < (1, 11) else "en_us",
    }
    with open(os.path.join(game, "options.txt"), "w") as f:
        f.writelines(f"{k}:{v}\n" for k, v in options.items())


def version_tuple(version):
    return tuple(int(part) for part in version.split("."))


def write_server(server_dir, server_jar):
    os.makedirs(server_dir, exist_ok=True)
    with open(os.path.join(server_dir, "eula.txt"), "w") as f:
        f.write("eula=true\n")
    properties = {
        "online-mode": "false", "level-seed": SEED, "gamemode": "1", "difficulty": "0",
        "spawn-monsters": "false", "spawn-animals": "false", "spawn-npcs": "false",
        "view-distance": "8", "max-players": "2", "server-port": "25565", "motd": "sfpew",
        "spawn-protection": "0", "snooper-enabled": "false", "sync-chunk-writes": "false",
    }
    with open(os.path.join(server_dir, "server.properties"), "w") as f:
        f.writelines(f"{k}={v}\n" for k, v in properties.items())
    shutil.copyfile(server_jar, os.path.join(server_dir, "server.jar"))


# ---- capture ------------------------------------------------------------

def java():
    explicit = os.environ.get("MCTRACE_JAVA")
    if explicit:
        return explicit
    for pattern in ("/usr/lib/jvm/*8*/bin/java", "/usr/lib/jvm/*-1.8.0*/bin/java"):
        found = sorted(glob.glob(pattern))
        if found:
            return found[0]
    raise SystemExit("no Java 8 runtime found; set MCTRACE_JAVA")


class Server:
    """The instance's dedicated server, driven through its console."""

    def __init__(self, root):
        self.dir = os.path.join(root, "server")
        self.lines = []
        self.process = subprocess.Popen(
            [java(), "-Xmx2G", "-jar", "server.jar", "nogui"], cwd=self.dir, stdin=subprocess.PIPE,
            stdout=subprocess.PIPE, stderr=subprocess.STDOUT, text=True, bufsize=1)
        threading.Thread(target=self._read, daemon=True).start()

    def _read(self):
        for line in self.process.stdout:
            self.lines.append(line.rstrip())

    def wait_for(self, text, timeout):
        deadline = time.time() + timeout
        while time.time() < deadline:
            if any(text in line for line in self.lines):
                return True
            if self.process.poll() is not None:
                break
            time.sleep(0.5)
        return False

    def command(self, text):
        self.process.stdin.write(text + "\n")
        self.process.stdin.flush()

    def freeze(self, frozen):
        """A frozen server answers nobody: connections wait in the backlog."""
        self.process.send_signal(signal.SIGSTOP if frozen else signal.SIGCONT)

    def stop(self):
        if self.process.poll() is None:
            self.freeze(False)
            self.command("stop")
            try:
                self.process.wait(60)
            except subprocess.TimeoutExpired:
                self.process.kill()


def wait_quiet(path, quiet, timeout):
    """Until the file has content and has not grown for `quiet` seconds."""
    deadline = time.time() + timeout
    while time.time() < deadline:
        if os.path.getsize(path) > 0 and time.time() - os.path.getmtime(path) >= quiet:
            return
        time.sleep(0.5)


def world_spawn(server_dir):
    """SpawnX/SpawnZ from the world's level.dat. Just enough NBT to walk
    the root compound's Data compound; every other payload is skipped."""
    with gzip.open(os.path.join(server_dir, "world", "level.dat")) as f:
        data = f.read()
    pos = 0

    def take(fmt):
        nonlocal pos
        value = struct.unpack_from(">" + fmt, data, pos)
        pos += struct.calcsize(">" + fmt)
        return value[0]

    def name():
        nonlocal pos
        length = take("H")
        pos += length
        return data[pos - length:pos].decode()

    sizes = {1: 1, 2: 2, 3: 4, 4: 8, 5: 4, 6: 8}

    def payload(tag, wanted):
        nonlocal pos
        if tag in sizes:
            if wanted:
                return take({1: "b", 2: "h", 3: "i", 4: "q", 5: "f", 6: "d"}[tag])
            pos += sizes[tag]
        elif tag in (7, 11, 12):  # byte, int, long arrays
            pos += take("i") * {7: 1, 11: 4, 12: 8}[tag]
        elif tag == 8:
            name()
        elif tag == 9:
            inner, count = take("b"), take("i")
            for _ in range(count):
                payload(inner, False)
        elif tag == 10:
            found = {}
            while True:
                inner = take("b")
                if inner == 0:
                    return found
                key = name()
                value = payload(inner, True)
                if value is not None:
                    found[key] = value
        return None

    take("b")
    name()
    root = payload(10, True)
    return root["Data"]["SpawnX"], root["Data"]["SpawnZ"]


def capture(instance, seconds):
    root = os.path.join(HOME, "instances", instance)
    with open(os.path.join(root, "launch.json")) as f:
        launch = json.load(f)
    traces = os.path.join(HOME, "traces")
    frames = os.path.join(HOME, "frames")
    os.makedirs(traces, exist_ok=True)
    os.makedirs(frames, exist_ok=True)
    full = os.path.join(traces, instance + ".trace")
    frame = os.path.join(frames, instance + ".trace")

    # Every capture starts from a newly generated world from the same seed:
    # a returning player would resume wherever the last capture left it.
    shutil.rmtree(os.path.join(root, "server", "world"), ignore_errors=True)
    log(f"{instance}: starting server")
    server = Server(root)
    process = None
    try:
        if not server.wait_for("Done (", 600):
            raise SystemExit("server did not start:\n" + "\n".join(server.lines[-20:]))
        # A settled world: daylight, clear sky, no weather or mobs to move.
        for command in ("gamerule doDaylightCycle false", "time set 6000", "weather clear 999999",
                        "gamerule doWeatherCycle false", "gamerule doMobSpawning false",
                        "gamerule spawnRadius 0"):
            server.command(command)
        # A new world has no level.dat until it is first saved, and the
        # spawn point is read from there below.
        server.command("save-all")
        server.wait_for("Saved the", 60)
        spawn_x, spawn_z = world_spawn(server.dir)

        # Every Mojang service on a dead port. From 1.16.4 the client asks
        # the services host whether this account may play online, and the
        # offline token's answer is no: it then ignores --server. A host that
        # cannot be reached is instead taken as "offline", which may.
        offline = [f"-Dminecraft.api.{host}.host=http://127.0.0.1:9"
                   for host in ("auth", "account", "session", "services")]
        client = ([java(), "-Xmx2G"] + offline +
                  [f"-Djava.library.path={launch['natives']}", f"-Dorg.lwjgl.librarypath={launch['natives']}",
                   "-cp", ":".join(launch["classpath"]), launch["main"]] +
                  launch["game_args"] + ["--server", "127.0.0.1", "--port", "25565"])
        cmd = ["xvfb-run", "-a", "-s", f"-screen 0 {WIDTH}x{HEIGHT}x24", "apitrace", "trace", "--api", "gl",
               "-o", full] + client
        log(f"{instance}: client joining under apitrace for {seconds}s")
        client_log = os.path.join(root, "client.log")
        # From 1.16 the client connects while its resources are still
        # loading, and a world that arrives before the block models are baked
        # crashes the first chunk build. The server stays frozen until the
        # client's log goes quiet, its loading done; the connection waits.
        server.freeze(True)
        with open(client_log, "w") as game_log:
            # Its own session, so the whole xvfb-run/apitrace/java tree can
            # be stopped together.
            process = subprocess.Popen(cmd, cwd=os.path.join(root, "game"), stdout=game_log,
                                       stderr=subprocess.STDOUT, start_new_session=True)
        wait_quiet(client_log, 5, 120)
        server.freeze(False)
        if not server.wait_for(f"{USERNAME} joined the game", 300):
            raise SystemExit(f"client never joined; see {root}/client.log")
        # Put the player on dry land a little north of the world spawn: a
        # new player faces south, and the spawn point is always chosen in a
        # land biome, so the view looks across terrain rather than out to
        # sea. spreadplayers never places on a liquid. Staying inside the
        # spawn area the server generated at startup matters too: anywhere
        # else the client waits on fresh chunks long enough under a software
        # rasterizer to fall through them. (Older versions' tp cannot set a
        # facing, so position is the only lever that works on all three.)
        for spread in (16, 32, 64):
            mark = len(server.lines)
            server.command(f"spreadplayers {spawn_x} {spawn_z - 48} 0 {spread} false {USERNAME}")
            server.wait_for("spread", 10)
            if any("Successfully spread" in line for line in server.lines[mark:]):
                break
        log(f"{instance}: joined; letting the world load")
        time.sleep(seconds)
    finally:
        if process is not None and process.poll() is None:
            os.killpg(process.pid, signal.SIGTERM)
            try:
                process.wait(60)
            except subprocess.TimeoutExpired:
                os.killpg(process.pid, signal.SIGKILL)
        server.stop()

    total = frame_count(full)
    if total < 3:
        raise SystemExit(f"the recording holds only {total} frames")
    # The last complete frames are the most loaded; step back from the end
    # in case the final one was cut off by the kill.
    target = total - 3
    log(f"{instance}: {total} frames recorded; cutting after frame {target}")
    subprocess.run(["apitrace", "trim", f"--frames=0-{target}", "-o", frame, full], check=True,
                   stdout=subprocess.DEVNULL)
    log(f"{instance}: frame trace {frame} ({os.path.getsize(frame) / 1e6:.1f} MB, frame {target} last)")
    render_reference(frame)


def render_reference(frame):
    """The final frame as the driver the game ran on draws it: a replay on
    the same software rasterizer under the same headless X server. It is
    what a wrapper replay of the frame is compared against."""
    reference = frame[:-len(".trace")] + ".png"
    with tempfile.TemporaryDirectory() as scratch:
        # apitrace's own snapshot of the final frame: this replay goes
        # through no shim, and the driver implements all a snapshot needs.
        # Every frame is drawn in full: the reference is what the image
        # check holds the skip-draws replays to.
        total = frame_count(frame)
        replay(["xvfb-run", "-a", "-s", f"-screen 0 {WIDTH}x{HEIGHT}x24", retracer("glretrace"), "-S", "*/frame",
                f"--snapshot-interval={total - 1}", "-s", os.path.join(scratch, "s-")], frame, total,
               skip_draws=False)
        shots = sorted(glob.glob(os.path.join(scratch, "s-*.png")))
        shutil.copyfile(shots[-1], reference)
    log(f"reference image {reference}")


def frame_count(trace):
    info = subprocess.run(["apitrace", "info", trace], check=True, capture_output=True, text=True).stdout
    return int(json.loads(info).get("FramesCount", 0))


# ---- replay -------------------------------------------------------------

APITRACE_COMMIT = "c3829c8fc94332f46f7616d9c9ee995b63d9bfc1"
# The gl4es measured against: upstream, built the way a launcher embeds it.
GL4ES_COMMIT = "bc4314d8369f169d3fa81c5ed1b46b3dba4e74a5"
HERE = os.path.dirname(os.path.abspath(__file__))


def setup(ndk=None):
    tools = os.path.join(HOME, "tools")
    shim = os.path.join(tools, "shim")
    os.makedirs(shim, exist_ok=True)
    subprocess.run(["cc", "-shared", "-fPIC", "-O2", "-Wall", "-Wl,-Bsymbolic", "-o",
                    os.path.join(shim, "libEGL.so.1"), os.path.join(HERE, "egl_es_shim.c"), "-ldl"], check=True)
    log(f"shim built in {shim}")

    source = os.path.join(tools, "apitrace")
    if not os.path.isdir(source):
        subprocess.run(["git", "clone", "-q", "https://github.com/apitrace/apitrace.git", source], check=True)
        subprocess.run(["git", "-C", source, "checkout", "-q", APITRACE_COMMIT], check=True)
        for patch in ("apitrace-glcalllists-remap.patch", "apitrace-headless.patch", "apitrace-skip-draws.patch"):
            subprocess.run(["git", "-C", source, "apply", os.path.join(HERE, patch)], check=True)
        subprocess.run(["git", "-C", source, "submodule", "update", "--init", "--depth", "1", "-q"], check=True)
    build = os.path.join(source, "build")
    subprocess.run(["cmake", "-G", "Ninja", "-S", source, "-B", build, "-DCMAKE_BUILD_TYPE=RelWithDebInfo",
                    "-DENABLE_GUI=OFF"], check=True, stdout=subprocess.DEVNULL)
    subprocess.run(["ninja", "-C", build, "eglretrace-headless", "glretrace"], check=True, stdout=subprocess.DEVNULL)
    log(f"retracers built in {build}")
    build_gl4es(os.path.join(tools, "gl4es-pc"))
    if ndk:
        setup_android(source, ndk)
        build_gl4es(os.path.join(tools, "gl4es-android"), ndk)


def build_gl4es(out, ndk=None):
    """gl4es as a launcher embeds it - no X11, so the context is the
    application's, and EGL for its hardware probe - at SFPEW's own
    optimization level (Release, LTO), into out/libGL.so.1."""
    source = os.path.join(HOME, "tools", "gl4es")
    if not os.path.isdir(source):
        subprocess.run(["git", "clone", "-q", "https://github.com/ptitSeb/gl4es.git", source], check=True)
        subprocess.run(["git", "-C", source, "checkout", "-q", GL4ES_COMMIT], check=True)
        subprocess.run(["git", "-C", source, "apply", os.path.join(HERE, "gl4es-swap-hooks.patch")], check=True)
    build = os.path.join(source, "build-android" if ndk else "build")
    options = ["-DCMAKE_BUILD_TYPE=Release", "-DCMAKE_INTERPROCEDURAL_OPTIMIZATION=TRUE",
               "-DCMAKE_POLICY_VERSION_MINIMUM=3.5", "-DNOX11=ON"]
    if ndk:
        options += [f"-DCMAKE_TOOLCHAIN_FILE={ndk}/build/cmake/android.toolchain.cmake", "-DANDROID_ABI=arm64-v8a",
                    "-DANDROID_PLATFORM=android-26", "-DANDROID=ON"]
    subprocess.run(["cmake", "-G", "Ninja", "-S", source, "-B", build] + options, check=True,
                   stdout=subprocess.DEVNULL)
    subprocess.run(["ninja", "-C", build], check=True, stdout=subprocess.DEVNULL)
    # Every build writes the same source-tree lib/; each is taken out of it.
    os.makedirs(out, exist_ok=True)
    built = os.path.join(source, "lib", "libGL.so.1")
    if ndk:
        strip = os.path.join(ndk, "toolchains", "llvm", "prebuilt", "linux-x86_64", "bin", "llvm-strip")
        subprocess.run([strip, "-o", os.path.join(out, "libGL.so.1"), built], check=True)
    else:
        shutil.copyfile(built, os.path.join(out, "libGL.so.1"))
    log(f"gl4es built into {out}")


def setup_android(source, ndk):
    toolchain = os.path.join(ndk, "toolchains", "llvm", "prebuilt", "linux-x86_64", "bin")
    shim = os.path.join(HOME, "tools", "shim-android")
    os.makedirs(shim, exist_ok=True)
    subprocess.run([os.path.join(toolchain, "aarch64-linux-android26-clang"), "-shared", "-fPIC", "-O2",
                    "-Wl,-Bsymbolic", "-o", os.path.join(shim, "libEGL.so.1"), os.path.join(HERE, "egl_es_shim.c"),
                    "-ldl"], check=True)
    build = os.path.join(source, "build-android")
    subprocess.run(["cmake", "-G", "Ninja", "-S", source, "-B", build,
                    f"-DCMAKE_TOOLCHAIN_FILE={ndk}/build/cmake/android.toolchain.cmake", "-DANDROID_ABI=arm64-v8a",
                    "-DANDROID_PLATFORM=android-26", "-DANDROID_STL=c++_static", "-DCMAKE_BUILD_TYPE=Release",
                    "-DENABLE_CLI=OFF"], check=True, stdout=subprocess.DEVNULL)
    subprocess.run(["ninja", "-C", build, "eglretrace"], check=True, stdout=subprocess.DEVNULL)
    subprocess.run([os.path.join(toolchain, "llvm-strip"), "-o", os.path.join(build, "eglretrace-stripped"),
                    os.path.join(build, "eglretrace")], check=True)
    log(f"Android retracer and shim built in {build} and {shim}")


def retracer(name):
    """The patched retracer from setup when there is one."""
    built = os.path.join(HOME, "tools", "apitrace", "build", name)
    return built if os.path.exists(built) else name


def retrace_args(total, loops, skip_draws=True):
    """loops > 0 repeats the final frame that many more times."""
    # -b: without it the retracer checks glGetError after every call, which
    # would be most of the driver calls a wrapper is measured on.
    # --skip-draws (apitrace-skip-draws.patch): the frames before the last
    # only rebuild the state it starts from, which needs none of their
    # pixels. Every image check replays the same way, so a frame that did
    # depend on earlier output would show there.
    return ["-b"] + ([f"--skip-draws={total - 1}"] if skip_draws else []) + ([f"--loop={loops}"] if loops else [])


def replay(prefix, frame, total, loops=0, env=None, skip_draws=True):
    """Replays a frame trace locally. Its exit status is not the verdict:
    gl4es crashes in its own teardown after every replay, so callers check
    for the output they asked for instead."""
    subprocess.run(list(prefix) + retrace_args(total, loops, skip_draws) + [frame], env=env,
                   stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL)


class Local:
    """eglretrace on this machine, under the shim: through the wrapper on
    GLES when given one, natively otherwise, never paced by a display."""

    name = "pc"
    native = True
    checks_images = True
    default_rounds = 3

    def __init__(self, out):
        self.shim = os.path.join(HOME, "tools", "shim", "libEGL.so.1")
        if not os.path.exists(self.shim):
            raise SystemExit("run `mctrace.py setup` first")
        self.out = out

    def frame(self, instance):
        frame = os.path.join(HOME, "frames", instance + ".trace")
        return frame, frame_count(frame)

    def library(self, path, name):
        return path

    def run(self, frame, total, wrapper, loops=0, env=None):
        # No display: EGL's surfaceless platform, so nothing presents a
        # swap or paces it. NVIDIA's waits spin by default, and a spinning
        # wait is CPU time that measures the GPU: __GL_YIELD=USLEEP makes
        # them sleep, so the CPU times count only work.
        full = {k: v for k, v in os.environ.items() if k not in ("DISPLAY", "WAYLAND_DISPLAY")}
        full.update(LD_LIBRARY_PATH=os.path.dirname(self.shim), EGL_PLATFORM="surfaceless", __GL_YIELD="USLEEP",
                    LD_PRELOAD=self.shim + (" " + wrapper if wrapper else ""))
        full.pop("SFPEW_SHIM_WRAPPER", None)
        if wrapper:
            full["SFPEW_SHIM_WRAPPER"] = wrapper
        full.update(env or {})
        # A fixed set of cores, so the scheduler does not move a run around.
        replay(["taskset", "-c", "8-11", retracer("eglretrace-headless")], frame, total, loops=loops, env=full)

    def scratch(self, name):
        return os.path.join(self.out, name)

    def fetch(self, path, local):
        """Moves a run's output file here; False if the run left none."""
        if not os.path.exists(path):
            return False
        if path != local:
            shutil.move(path, local)
        return True


class Device:
    """The same replay on an Android device over adb, with the eglretrace
    built for it by `setup --android` and the shim cross-compiled beside it.
    Mali and the other mobile GPUs have no desktop GL, so there is no native
    baseline there - only the wrapper's own frame times."""

    name = "device"
    native = False
    # Correctness is the host's to judge (see Local); a device run is timed
    # and profiled only.
    checks_images = False
    default_rounds = 1
    root = "/data/local/tmp/sfpew/mctrace"
    memory_floor_kb = 800_000

    def __init__(self, out, serial):
        self.out = out
        self.adb = ["adb"] + (["-s", serial] if serial else [])
        self.shell(f"mkdir -p {self.root}/frames {self.root}/out")
        tools = os.path.join(HOME, "tools")
        self.push(os.path.join(tools, "apitrace", "build-android", "eglretrace-stripped"), "eglretrace")
        # eglretrace links libEGL.so, so on LD_LIBRARY_PATH under that name
        # the shim is the libEGL it gets - with nothing preloaded.
        self.push(os.path.join(tools, "shim-android", "libEGL.so.1"), "libEGL.so")
        self.shell(f"chmod 755 {self.root}/eglretrace")

    def shell(self, command, check=True):
        return subprocess.run(self.adb + ["shell", command], check=check, capture_output=True, text=True).stdout

    def push(self, local, remote):
        """Pushes unless the device already holds the same file."""
        target = f"{self.root}/{remote}"
        held = self.shell(f"sha1sum {target} 2>/dev/null", check=False).split()
        if held[:1] != [sha1_of(local)]:
            subprocess.run(self.adb + ["push", local, target], check=True, capture_output=True)
        return target

    def frame(self, instance):
        frame = os.path.join(HOME, "frames", instance + ".trace")
        return self.push(frame, f"frames/{instance}.trace"), frame_count(frame)

    def library(self, path, name):
        return self.push(path, f"{name}-{os.path.basename(path)}")

    def run(self, frame, total, wrapper, loops=0, env=None):
        # Nothing is preloaded and the shim dlopen()s the wrapper privately,
        # as a launcher would: global exports from either break Android's
        # EGL, whose vendor layer resolves EGL entry points globally and
        # would land back in them while the wrapper is still loading the
        # system libEGL - named outright, since "libEGL.so" is now the shim.
        # (SFPEW_EGL for SFPEW, LIBGL_EGL for gl4es.)
        system_egl = "/system/lib64/libEGL.so"
        variables = {"LD_LIBRARY_PATH": self.root, "SFPEW_SHIM_REAL_EGL": system_egl, "SFPEW_EGL": system_egl,
                     "LIBGL_EGL": system_egl, "SFPEW_SHIM_WRAPPER_LOCAL": "1"}
        if wrapper:
            variables["SFPEW_SHIM_WRAPPER"] = wrapper
        variables.update(env or {})
        cmd = [f"{self.root}/eglretrace", "--headless"] + retrace_args(total, loops)
        prefix = " ".join(f"{k}='{v}'" for k, v in variables.items())
        # The big cores, so the scheduler does not move a run to a little one.
        # A replay that eats the phone's memory is killed while the phone
        # still has some: running out panics the kernel and reboots it.
        guard = (f"while kill -0 $p 2>/dev/null; do "
                 f"[ $(awk '/MemAvailable/{{print $2}}' /proc/meminfo) -lt {self.memory_floor_kb} ] && kill -9 $p; "
                 f"sleep 0.2; done")
        self.shell(f"cd {self.root} && ({prefix} taskset f0 {' '.join(cmd)} {frame} >/dev/null 2>&1 & p=$!; {guard})")

    def scratch(self, name):
        return f"{self.root}/out/{name}"

    def fetch(self, path, local):
        """Moves a run's output file here; False if the run left none."""
        if subprocess.run(self.adb + ["pull", path, local], capture_output=True).returncode != 0:
            return False
        self.shell(f"rm -f {path}")
        return True


def frame_times(runner, frame, total, wrapper, loops, warmup):
    """Each looped repeat of the final frame, from buffer swap to buffer swap,
    warm-up repeats dropped: its wall time and the process CPU time it took,
    both in ms."""
    local = os.path.join(runner.out, "times.txt")
    remote = runner.scratch("times.txt")
    runner.run(frame, total, wrapper, loops=loops, env={"SFPEW_SHIM_FRAME_TIMES": remote})
    if not runner.fetch(remote, local):
        raise SystemExit(f"{frame} did not replay through {wrapper or 'the native driver'}")
    stamps = [tuple(map(int, line.split())) for line in open(local)][-loops - 1:]
    return {clock: [(b[i] - a[i]) / 1e6 for a, b in zip(stamps, stamps[1:])][warmup:]
            for i, clock in enumerate(("wall", "cpu"))}


def summarize(ms):
    ordered = sorted(ms)

    def pick(q):
        return ordered[min(len(ordered) - 1, int(q * len(ordered)))]
    return {"median": pick(0.5), "p10": pick(0.1), "p90": pick(0.9), "max": ordered[-1], "frames": len(ordered)}


def image_difference(image, reference):
    """Share of pixels differing by more than 16 in any channel, or None
    without Pillow."""
    try:
        from PIL import Image, ImageChops
    except ImportError:
        return None
    a, b = Image.open(image).convert("RGB"), Image.open(reference).convert("RGB")
    if a.size != b.size:
        return 1.0
    diff = ImageChops.difference(a, b).convert("L").point(lambda v: 255 if v > 16 else 0)
    return diff.histogram()[255] / (a.size[0] * a.size[1])


def bench(instances, layers, counters, loops, warmup, rounds=None, device=None):
    """layers: [(name, library)] of the GL-on-GLES layers to measure, SFPEW
    first; on the host the native driver is measured beside them."""
    instances = instances or sorted(os.path.basename(f)[:-6] for f in glob.glob(os.path.join(HOME, "frames", "*.trace")))
    out = os.path.join(HOME, "results", time.strftime("%Y%m%d-%H%M%S") + ("-device" if device is not None else ""))
    os.makedirs(out)
    runner = Device(out, device) if device is not None else Local(out)
    # A phone replays the lead-in to the measured frame for most of a
    # minute per run; one round there, three on the host, unless asked.
    rounds = rounds or runner.default_rounds
    targets = [(name, runner.library(library, name)) for name, library in layers]
    targets += [("native", None)] if runner.native else []
    counters = counters and runner.library(counters, "counters")
    results = []
    for index, instance in enumerate(instances):
        frame, total = runner.frame(instance)
        log(f"{instance} on {runner.name}: {total} frames, looping the last {loops} times, {rounds} rounds")
        row = {"instance": instance, "frames": total, "where": runner.name}

        # Round after round, every target once in an order that rotates by
        # round and by frame: a phone warming up, or anything else drifting
        # during the run, then weighs on all of them alike instead of on
        # whichever always ran last.
        samples = {name: {"wall": [], "cpu": []} for name, _ in targets}
        for round in range(rounds):
            first = (round + index) % len(targets)
            for name, library in targets[first:] + targets[:first]:
                for clock, ms in frame_times(runner, frame, total, library, loops, warmup).items():
                    samples[name][clock] += ms
        for name, clocks in samples.items():
            row[name] = {clock: summarize(ms) for clock, ms in clocks.items()}

        reference = os.path.join(HOME, "frames", instance + ".png")
        if runner.checks_images and os.path.exists(reference):
            for name, library in targets:
                image = os.path.join(out, f"{instance}.{name}.ppm")
                remote = runner.scratch(f"{instance}.{name}.ppm")
                runner.run(frame, total, library,
                           env={"SFPEW_SHIM_SNAPSHOT": remote, "SFPEW_SHIM_SNAPSHOT_FRAME": str(total)})
                if runner.fetch(remote, image):
                    row[name]["pixels_off"] = image_difference(image, reference)

        if counters:
            # The counters zero at the first looped frame past warm-up, so the
            # exit report covers exactly the frames that were timed.
            report, table = runner.scratch(instance + ".perf.txt"), runner.scratch(instance + ".perf.csv")
            runner.run(frame, total, counters, loops=loops,
                       env={"SFPEW_PERF": "1", "SFPEW_PERF_START": str(total + warmup),
                            "SFPEW_PERF_FILE": report, "SFPEW_PERF_CSV": table})
            runner.fetch(report, os.path.join(out, instance + ".perf.txt"))
            runner.fetch(table, os.path.join(out, instance + ".perf.csv"))
        results.append(row)
        log(f"{instance}: " + ", ".join(f"{name} {row[name]['wall']['median']:.2f} ms wall, "
                                         f"{row[name]['cpu']['median']:.2f} ms CPU" for name, _ in targets))

    with open(os.path.join(out, "results.json"), "w") as f:
        json.dump(results, f, indent=2)
    write_table(results, [name for name, _ in targets], os.path.join(out, "results.md"))
    log(f"results in {out}")


def write_table(results, targets, path):
    """Per target the median wall and CPU ms per frame, then SFPEW's ratio to
    every other target, and how far each image is from the reference.

    Wall time is what a player sees; CPU time is the work behind it, which
    GPU stalls and power states do not move - on a desktop GPU loafing at
    low clocks the wall times say little and the CPU times stay comparable.
    A replay that does not reproduce its reference image is flagged: its
    times are those of a different, broken frame."""
    sfpew, others = targets[0], targets[1:]
    header = ["frame"] + [f"{t} wall / CPU ms" for t in targets]
    header += [f"{sfpew} / {t} wall, CPU" for t in others]
    if any("pixels_off" in r[t] for r in results for t in targets):
        header += [f"{t} image" for t in targets]
    lines = ["| " + " | ".join(header) + " |", "|---" + "|---:" * (len(header) - 1) + "|"]
    for r in results:
        cells = [r["instance"]]
        cells += [f"{r[t]['wall']['median']:.2f} / {r[t]['cpu']['median']:.2f}" for t in targets]
        cells += [f"{r[sfpew]['wall']['median'] / r[t]['wall']['median']:.2f}x, "
                  f"{r[sfpew]['cpu']['median'] / r[t]['cpu']['median']:.2f}x" for t in others]
        if len(header) > len(cells):
            for t in targets:
                off = r[t].get("pixels_off")
                cells.append("-" if off is None else f"{off * 100:.2f}% off" + (" **broken**" if off > 0.5 else ""))
        lines.append("| " + " | ".join(cells) + " |")
    with open(path, "w") as f:
        f.write("\n".join(lines) + "\n")
    print("\n".join(lines))


def list_all():
    for instance in sorted(glob.glob(os.path.join(HOME, "instances", "*"))):
        name = os.path.basename(instance)
        frame = os.path.join(HOME, "frames", name + ".trace")
        state = f"frame {os.path.getsize(frame) / 1e6:.1f} MB" if os.path.exists(frame) else "no frame yet"
        print(f"{name:24} {state}")


def main():
    parser = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    sub = parser.add_subparsers(dest="command", required=True)
    p = sub.add_parser("install")
    p.add_argument("version", choices=sorted(OPTIFINE))
    p.add_argument("--optifine", action="store_true")
    p = sub.add_parser("capture")
    p.add_argument("instance")
    p.add_argument("--seconds", type=int, default=60, help="time in the world before the recording stops")
    p = sub.add_parser("setup")
    p.add_argument("--android", metavar="NDK", help="also build for Android arm64 with this NDK")
    p = sub.add_parser("bench")
    p.add_argument("instance", nargs="*", help="frames to run (default: every captured one)")
    p.add_argument("--wrapper", required=True, help="the wrapper library to measure")
    p.add_argument("--gl4es", help="a gl4es libGL built by `setup` to measure against")
    p.add_argument("--counters", help="a performance-counters build of the same wrapper, to profile with")
    p.add_argument("--loops", type=int, default=300, help="repeats of the final frame")
    p.add_argument("--warmup", type=int, default=30, help="leading repeats left out of the statistics")
    p.add_argument("--rounds", type=int, help="interleaved runs of every target (default 3, 1 with --device)")
    p.add_argument("--device", nargs="?", const="", help="run on an Android device over adb (optional serial); "
                   "--wrapper and --counters are then Android builds")
    sub.add_parser("list")
    args = parser.parse_args()
    if args.command == "install":
        install(args.version, args.optifine)
    elif args.command == "capture":
        capture(args.instance, args.seconds)
    elif args.command == "setup":
        setup(args.android)
    elif args.command == "bench":
        layers = [("sfpew", os.path.abspath(args.wrapper))]
        layers += [("gl4es", os.path.abspath(args.gl4es))] if args.gl4es else []
        bench(args.instance, layers, args.counters and os.path.abspath(args.counters),
              args.loops, args.warmup, args.rounds, args.device)
    else:
        list_all()


if __name__ == "__main__":
    main()
