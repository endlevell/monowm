"""Live tests using isolated headless outputs; requires a working FX renderer."""
import os
from pathlib import Path
import subprocess
import sys
import tempfile
import time

mono, monomsg, client = (str(Path(arg).resolve()) for arg in sys.argv[1:])
for outputs in (0, 1):
    with tempfile.TemporaryDirectory(prefix="mono-headless-test-") as runtime:
        directory = Path(runtime)
        config = directory / "config.lua"
        config.write_text(
            "appearance = {}\ninput = {}\nanimations = {enabled = false}\n"
            "rules = {{floating = true, monitor = -1}}\nautostart = {}\n"
        )
        env = os.environ.copy()
        env.update(
            XDG_RUNTIME_DIR=runtime,
            WLR_BACKENDS="headless",
            WLR_HEADLESS_OUTPUTS=str(outputs),
            WLR_LIBINPUT_NO_DEVICES="1",
        )
        env.pop("WAYLAND_DISPLAY", None)
        with (directory / "stderr").open("w") as err, (directory / "stdout").open("w") as out:
            process = subprocess.Popen([mono, "-c", str(config)], env=env, stdout=out, stderr=err)
            try:
                for _ in range(100):
                    sockets = [p for p in directory.glob("wayland-*") if not p.name.endswith(".lock")]
                    if sockets:
                        break
                    assert process.poll() is None, (directory / "stderr").read_text()
                    time.sleep(0.05)
                assert sockets, "compositor did not create a socket"
                env["WAYLAND_DISPLAY"] = sockets[0].name
                subprocess.run([client] + (["fullscreen"] if outputs else []), env=env, check=True, timeout=10)
                assert process.poll() is None, (directory / "stderr").read_text()
                if outputs:
                    output = subprocess.check_output([monomsg, "-O"], env=env, text=True, timeout=5).strip()
                    for tags, expected in (("4", 16), ("!0", 1), ("0-", 1)):
                        subprocess.run([monomsg, "-o", output, "-s", "-t", tags], env=env, check=True, timeout=5)
                        # Set requests are flushed asynchronously on a separate
                        # connection, so a query may initially see the old state.
                        for _ in range(100):
                            state = subprocess.check_output([monomsg, "-g", "-t"], env=env, text=True, timeout=5)
                            summary = [line for line in state.splitlines() if " tags " in line][-1]
                            if int(summary.split()[-3]) == expected:
                                break
                            time.sleep(0.01)
                        assert int(summary.split()[-3]) == expected, summary
                print(f"headless regressions passed with {outputs} output(s)")
            finally:
                process.terminate()
                try:
                    process.wait(timeout=5)
                except subprocess.TimeoutExpired:
                    process.kill()
                    process.wait()
