#!/usr/bin/env python3
"""kimodo_gen.py -- text to motion, as a batch: drives kimodo.cpp's kmd-generate, a process a take, and writes every take down so it can be made again.
The second half of the lane's front end (11-02; soma.py reads what it makes). Never at runtime: this is an asset source (design/animation_authoring.md 3.7).

    kimodo_gen.py <kimodo home> <batch.json> [--force]

    {"steps": 50, "takes": [{"name": "walk", "prompt": "A person walks forward.", "seconds": 6.0, "seeds": [1, 2, 3]},
                            {"name": "set_off", "sequence": [[60, "A person stands still."], [120, "A person runs forward."]], "transition": 15, "seeds": [1]}]}

Each take lands in <home>/out/<name>_s<seed>/ beside a take.json: prompt, seed, steps, frames, the weights' repo revisions and hashes and kimodo.cpp's
commit, from <home>/LEDGER.json. The same prompt, seed, weights and backend give the same clip, so that is what makes a clip reproducible, and the licence
chain is on the record with it. A take already there is left alone.

One process a take, not its --server mode: that keeps the text model resident, which an 8 GB card can't spare (GTX 1070, 37 s for a 5 s clip, 1.9 GB of
video memory the way this runs it). The kit stays free of torch: all it does is talk to the exe. SOMA-RP weights only (NVIDIA Open Model License). Not the SMPL-X checkpoint (no works for
sale) and not SEED (its data licence): this script refuses any other motion file.
"""
import json, os, subprocess, sys, time

MOTION = "weights/LocalAI-io__Kimodo-SOMA-RP-v1.1-GGML/models/kimodo-soma-rp-v1.1-f32.gguf"
TEXT = "weights/LocalAI-io__Llama-3-Kimodo-GGML/Llama-3-Kimodo-Q8_0.gguf"
FPS = 30
MAX_FRAMES = 300


def main(argv):
    home, batch_path = argv[1], argv[2]
    force = "--force" in argv
    batch = json.load(open(batch_path, encoding="utf-8"))
    steps = int(batch.get("steps", 50))
    motion, text = os.path.join(home, MOTION), os.path.join(home, TEXT)
    assert os.path.exists(motion), "no SOMA-RP weights at %s" % motion
    ledger = json.load(open(os.path.join(home, "LEDGER.json")))
    env = dict(os.environ, PATH=os.path.join(home, "build", "bin") + os.pathsep + os.path.join(home, "build") + os.pathsep + os.environ["PATH"])
    exe = os.path.join(home, "build", "kmd-generate.exe")
    flags = getattr(subprocess, "BELOW_NORMAL_PRIORITY_CLASS", 0)
    jobs = []
    for take in batch["takes"]:
        for seed in take["seeds"]:
            name = "%s_s%d" % (take["name"], seed)
            out = os.path.join(home, "out", name)
            if os.path.exists(os.path.join(out, "take.json")) and not force:
                continue
            sequence = take.get("sequence")
            segments = sequence or [[int(round(float(take["seconds"]) * FPS)), take["prompt"]]]
            total = sum(int(s_[0]) for s_ in segments)
            assert total <= MAX_FRAMES + int(take.get("transition", 0)) * len(segments), "%s: %d frames is past the model's 10 s" % (name, total)
            os.makedirs(out, exist_ok=True)
            paths = []
            for k, (frames, prompt) in enumerate(segments):
                path = os.path.join(out, "prompt_%d.txt" % k)
                with open(path, "w", encoding="utf-8") as f:
                    f.write(prompt)
                paths.append((int(frames), path))
            if sequence:
                cmd = [exe, motion, text, "--sequence", str(int(take["transition"])), str(steps), str(seed), out]
                for frames, path in paths:
                    cmd += [str(frames), path]
            else:
                cmd = [exe, motion, text, paths[0][1], str(paths[0][0]), str(steps), str(seed), out]
            jobs.append((name, out, take, seed, segments, cmd))
    if not jobs:
        print("nothing to make")
        return 0
    print("%d takes" % len(jobs), flush=True)
    started = time.time()
    failed = 0
    for name, out, take, seed, segments, cmd in jobs:
        t0 = time.time()
        run = subprocess.run(cmd, env=env, capture_output=True, text=True, encoding="utf-8", errors="replace", creationflags=flags)
        ok = run.returncode == 0
        print("%-24s %5.1f s  %s" % (name, time.time() - t0, (run.stdout + run.stderr).strip().splitlines()[-1][:160] if (run.stdout + run.stderr).strip() else "exit %d" % run.returncode), flush=True)
        if not ok:
            failed += 1
            continue
        record = {"name": name, "seed": seed, "steps": steps, "transition": int(take.get("transition", 0)), "segments": segments, "fps": FPS,
                  "weights": ledger["weights"], "source": ledger["source"], "text_encoder": "Q8_0", "backend": "vulkan"}
        json.dump(record, open(os.path.join(out, "take.json"), "w", encoding="utf-8"), indent=1)
    print("%d made, %d failed, %.0f s" % (len(jobs) - failed, failed, time.time() - started), flush=True)
    return 1 if failed else 0


if __name__ == "__main__":
    sys.exit(main(sys.argv))
