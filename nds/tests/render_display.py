"""Render the actual NDS input screens using an external melonDS DS libretro core.

Usage: python render_display.py CORE.dll ROM.nds OUTPUT_DIRECTORY [--ui]
Requires Pillow. No native windows, BIOS dumps, SD contents or Wi-Fi are used.
"""
import ctypes as C
import json
from pathlib import Path
import sys

from PIL import Image


class Variable(C.Structure):
    _fields_ = [("key", C.c_char_p), ("value", C.c_char_p)]


class SystemInfo(C.Structure):
    _fields_ = [("name", C.c_char_p), ("version", C.c_char_p),
                ("extensions", C.c_char_p), ("fullpath", C.c_bool), ("block_extract", C.c_bool)]


class GameInfo(C.Structure):
    _fields_ = [("path", C.c_char_p), ("data", C.c_void_p),
                ("size", C.c_size_t), ("meta", C.c_char_p)]


core_path, rom_path, output_path = (Path(arg).resolve() for arg in sys.argv[1:4])
ui_mode = "--ui" in sys.argv[4:]
feedback_mode = "--feedback" in sys.argv[4:]
region_mode = "--region" in sys.argv[4:] or "--region-cancel" in sys.argv[4:]
region_cancel = "--region-cancel" in sys.argv[4:]
startup_mode = "--startup" in sys.argv[4:]
queue_mode = "--queue" in sys.argv[4:]
search_mode = "--search" in sys.argv[4:]
intro_cover = "--intro-cover" in sys.argv[4:]
intro_skip_key = next((arg.split("=", 1)[1] for arg in sys.argv[4:] if arg.startswith("--intro-skip=")),
                      "start" if "--intro-skip" in sys.argv[4:] else None)
intro_held = "--intro-held" in sys.argv[4:]
intro_mode = "--intro" in sys.argv[4:] or intro_skip_key is not None or intro_cover or intro_held
intro_skip = intro_skip_key is not None
assert not intro_skip or intro_skip_key in ("a", "b", "start", "touch"), "Choose a, b, start or touch"
system_mode = "--system" in sys.argv[4:]
if intro_mode:
    captures = {40: "arrival", 170: "reveal", 240: "browser"} if intro_cover else {40: "arrival", 110: "startup", 240: "settled"} if intro_skip else {40: "arrival", 75: "fall", 110: "transfer", 170: "reveal", 240: "startup"}
elif system_mode:
    captures = {120: "nds", 300: "r-gba", 480: "r-others", 660: "l-gba", 840: "l-nds",
                1020: "l-wrap-others", 1200: "r-wrap-nds", 1380: "next-page", 1560: "previous-page", 1740: "complete"}
elif "--result" in sys.argv[4:]:
    captures = {120: "result"}
elif search_mode:
    captures = {120: "search", 300: "complete"}
elif startup_mode:
    captures = {120: "storage", 300: "wifi", 480: "authentication", 660: "library", 840: "ready"}
elif queue_mode:
    captures = {120: "browser", 300: "queue-nds", 480: "queue-gba", 660: "queue-others", 840: "complete"}
elif region_mode:
    captures = {120: "browser", 300: "region", 480: "selected"}
elif ui_mode:
    captures = {120: "browser", 300: "scrolled", 480: "gba", 660: "queue", 840: "settings", 1020: "download", 1200: "complete"}
else:
    captures = {120: "ssid", 300: "password", 430: "complete"}
output_path.mkdir(parents=True, exist_ok=True)
for folder in ("system", "save"):
    (output_path / folder).mkdir(exist_ok=True)
paths = {9: str(output_path / "system").encode(), 31: str(output_path / "save").encode(),
         30: str(rom_path.parent).encode(), 38: b"Graphics Test"}
variables = {}
pixel_format = 0
frame = 0
last_image = None
transition_frames = {}
intro_frames = []
intro_startup_frame = None
held_tabs = {"r-gba": [], "r-others": [], "l-gba": [], "l-nds": [], "l-wrap-others": [], "r-wrap-nds": []}
environment_calls = set()
ENV = C.CFUNCTYPE(C.c_bool, C.c_uint, C.c_void_p)
VIDEO = C.CFUNCTYPE(None, C.c_void_p, C.c_uint, C.c_uint, C.c_size_t)
AUDIO = C.CFUNCTYPE(None, C.c_int16, C.c_int16)
BATCH = C.CFUNCTYPE(C.c_size_t, C.c_void_p, C.c_size_t)
POLL = C.CFUNCTYPE(None)
INPUT = C.CFUNCTYPE(C.c_int16, C.c_uint, C.c_uint, C.c_uint, C.c_uint)


@ENV
def environment(command, data):
    global pixel_format
    command &= ~0x10000
    environment_calls.add(command)
    if command in paths:
        C.cast(data, C.POINTER(C.c_char_p))[0] = paths[command]
        return True
    if command in (3, 51):
        if data:
            C.cast(data, C.POINTER(C.c_bool))[0] = True
        return True
    if command in (2, 17):
        C.cast(data, C.POINTER(C.c_bool))[0] = False
        return True
    if command == 10:
        pixel_format = C.cast(data, C.POINTER(C.c_int))[0]
        return pixel_format in (0, 1, 2)
    if command in (52, 59):
        C.cast(data, C.POINTER(C.c_uint))[0] = 0
        return True
    if command == 16:
        entries = C.cast(data, C.POINTER(Variable))
        index = 0
        while entries[index].key:
            entry = entries[index]
            choices = entry.value.split(b"; ", 1)[-1].split(b"|")
            selected = choices[0]
            if b"render" in entry.key and b"software" in choices:
                selected = b"software"
            if b"jit" in entry.key and b"disabled" in choices:
                selected = b"disabled"
            variables[entry.key] = selected
            index += 1
        return True
    if command == 15:
        entry = C.cast(data, C.POINTER(Variable)).contents
        entry.value = variables.get(entry.key)
        return entry.value is not None

    return command in (1, 6, 8, 11, 18, 21, 32, 34, 35, 37, 44, 55, 60, 62, 63)


@VIDEO
def video(data, width, height, pitch):
    global last_image
    if not data or data == C.c_void_p(-1).value:
        return
    raw = C.string_at(data, pitch * height)
    if pixel_format == 1:
        last_image = Image.frombytes("RGB", (width, height), raw, "raw", "BGRX", pitch)
    else:
        mode = "BGR;16" if pixel_format == 2 else "BGR;15"
        last_image = Image.frombytes("RGB", (width, height), raw, "raw", mode, pitch)


@AUDIO
def audio(_left, _right):
    pass


@BATCH
def batch(_samples, frames):
    return frames


@POLL
def poll():
    pass


@INPUT
def input_state(_port, device, _index, button):
    if intro_mode:
        pressed = (intro_skip and 60 <= frame < 66) or (intro_held and frame < 100)
        if device in (2, 6) and intro_skip_key == "touch":
            if device == 6:
                return int(pressed) if button == 2 else 16384 if button == 1 else 0
            return int(pressed) if button == 2 else 0
        chosen = {"a": 8, "b": 0, "start": 3}.get(intro_skip_key, 8)
        return ((1 << chosen) if button == 256 else int(button == chosen)) if device == 1 and pressed and intro_skip_key != "touch" else 0
    if system_mode and device == 1:
        chosen = 11 if 180 <= frame < 250 or 360 <= frame < 430 or 1080 <= frame < 1150 else 10 if 540 <= frame < 610 or 720 <= frame < 790 or 900 <= frame < 970 else 7 if 1260 <= frame < 1266 else 6 if 1440 <= frame < 1446 else 3 if 1620 <= frame < 1626 else None
        return 0 if chosen is None else (1 << chosen) if button == 256 else int(button == chosen)
    if queue_mode and device == 1:
        chosen_button = 3 if 180 <= frame < 225 else 5 if 360 <= frame < 366 or 540 <= frame < 546 else 0 if 720 <= frame < 726 else None
        return 0 if chosen_button is None else (1 << chosen_button) if button == 256 else int(button == chosen_button)
    if region_mode and device == 1:
        chosen_button = 5 if 270 <= frame < 276 else 0 if region_cancel and 360 <= frame < 366 else None
        if chosen_button is not None:
            return (1 << chosen_button) if button == 256 else int(button == chosen_button)

    pressed = any(start <= frame < start + 6 for start in range(180, 180 * len(captures), 180))
    if device == 1 and pressed:
        return 8 if button == 256 else int(button == 3)
    return 0


core = C.CDLL(str(core_path))
for name, callback, kind in [
    ("environment", environment, ENV), ("video_refresh", video, VIDEO),
    ("audio_sample", audio, AUDIO), ("audio_sample_batch", batch, BATCH),
    ("input_poll", poll, POLL), ("input_state", input_state, INPUT),
]:
    method = getattr(core, "retro_set_" + name)
    method.argtypes = [kind]
    method(callback)
core.retro_init()
info = SystemInfo()
core.retro_get_system_info.argtypes = [C.POINTER(SystemInfo)]
core.retro_get_system_info(C.byref(info))
rom_bytes = C.create_string_buffer(rom_path.read_bytes())
game = GameInfo(str(rom_path).encode(), None if info.fullpath else C.addressof(rom_bytes),
                0 if info.fullpath else len(rom_bytes) - 1, None)
core.retro_load_game.argtypes = [C.POINTER(GameInfo)]
core.retro_load_game.restype = C.c_bool
if not core.retro_load_game(C.byref(game)):
    raise RuntimeError("melonDS could not load the diagnostic ROM")
try:
    for frame in range(max(captures) + 30):
        core.retro_run()
        if intro_mode and frame % 2 == 0 and frame <= 240 and last_image is not None:
            intro_frames.append(last_image.copy())
        if intro_mode and not intro_cover and intro_startup_frame is None and last_image is not None:
            if last_image.getpixel((20, 370)) != last_image.getpixel((0, 370)):
                intro_startup_frame = frame
        if system_mode and last_image is not None:
            for start, name in ((210, "r-gba"), (390, "r-others"), (570, "l-gba"), (750, "l-nds"),
                                (930, "l-wrap-others"), (1110, "r-wrap-nds")):
                if start <= frame < start + 38:
                    held_tabs[name].append(last_image.crop((0, 192, 256, 216)).tobytes())
        if ui_mode and 120 <= frame <= 300 and last_image is not None:
            bottom = last_image.crop((0, 192, 256, 384))
            transition_frames.setdefault(bottom.tobytes(), (frame, bottom))
        if frame in captures:
            if last_image is None:
                raise RuntimeError("No software video frame received")
            name = captures[frame]
            last_image.save(output_path / (name + ".png"))
    if intro_mode:
        assert len({picture.tobytes() for picture in intro_frames}) > 15, "Intro did not animate"
        intro_frames[0].save(output_path / "intro.gif", save_all=True, append_images=intro_frames[1:], duration=33, loop=0)
        target = Image.open(output_path / ("browser.png" if intro_cover else "startup.png"))
        if intro_cover:
            pixels = target.crop((8, 224, 96, 312))
            assert len(set(pixels.get_flattened_data())) > 900 and all(b > 180 for _, _, b in pixels.get_flattened_data()), "Intro altered cover graphics or blending"
            print("Intro: sprites and blend state restored; catalog cover renders correctly")
        else:
            assert target.getpixel((20, 370)) != target.getpixel((0, 370)), "Intro did not reach connection screen"
            assert intro_startup_frame is not None
            if intro_skip:
                assert intro_startup_frame < 110, f"{intro_skip_key} did not skip promptly"
            else:
                assert 192 <= intro_startup_frame < 240, f"Full intro duration incorrect: {intro_startup_frame}"
            print("Intro: native frames animated; " +
                  (f"{intro_skip_key} skip reached connection screen" if intro_skip else
                   "held launch A did not skip the full sequence" if intro_held else "full sequence reached connection screen") +
                  f" at frame {intro_startup_frame}")
    if system_mode:
        nds = Image.open(output_path / "nds.png")
        gba = Image.open(output_path / "r-gba.png")
        others = Image.open(output_path / "r-others.png")
        back = Image.open(output_path / "l-nds.png")
        tab_pixels = lambda picture: picture.crop((0, 192, 256, 216)).tobytes()
        assert len({tab_pixels(picture) for picture in (nds, gba, others)}) == 3, "Three platform views must differ"
        assert gba.getpixel((12, 196)) != gba.getpixel((140, 196)), "GBA left tab is not selected"
        assert others.getpixel((12, 196)) == gba.getpixel((140, 196)), "Others left tab must be inactive GBA"
        assert others.getpixel((140, 196)) == gba.getpixel((12, 196)), "Others right tab is not selected"
        assert tab_pixels(back) == tab_pixels(nds), "L did not return to NDS"
        assert tab_pixels(Image.open(output_path / "l-wrap-others.png")) == tab_pixels(others), "L did not wrap NDS to Others"
        assert tab_pixels(Image.open(output_path / "r-wrap-nds.png")) == tab_pixels(nds), "R did not wrap Others to NDS"
        for name, frames in held_tabs.items():
            expected = tab_pixels(Image.open(output_path / (name + ".png")))
            assert set(frames) == {expected}, f"Holding a shoulder button repeatedly switched systems: {name}"
        next_page = Image.open(output_path / "next-page.png")
        previous = Image.open(output_path / "previous-page.png")
        assert next_page.crop((0, 176, 256, 192)).tobytes() != back.crop((0, 176, 256, 192)).tobytes(), "Right did not change page"
        assert previous.crop((0, 176, 256, 192)).tobytes() == back.crop((0, 176, 256, 192)).tobytes(), "Left did not return to previous page"
        print("Controls: NDS/GBA -> GBA/Others sliding tabs, R/L through all three, wrap, held-key suppression and pages passed")
    if ui_mode:
        expected = {Image.open(output_path / (name + ".png")).crop((0, 192, 256, 384)).tobytes()
                    for name in ("browser", "scrolled")}
        extras = [item for pixels, item in transition_frames.items() if pixels not in expected]
        for number, (at_frame, picture) in enumerate(extras):
            picture.save(output_path / f"transition-{number}-{at_frame}.png")
        if feedback_mode:
            assert extras, "Pressed button never became visible"
            assert any(picture.getpixel((132, 170)) != Image.open(output_path / "browser.png").getpixel((132, 362))
                       for _, picture in extras), "Pressed outline missing"
        else:
            assert not extras, f"Non-atomic bottom-screen redraw at frames {[item[0] for item in extras]}"
        print(f"Bottom-screen transition: {len(transition_frames)} complete images; "
              f"{'pressed feedback visible' if feedback_mode else 'no blank or partial frames'}")
    if region_mode:
        before = Image.open(output_path / "browser.png").crop((0, 316, 256, 336)).tobytes()
        after = Image.open(output_path / "selected.png").crop((0, 316, 256, 336)).tobytes()
        assert (before == after) if region_cancel else (before != after), "Region selection/cancel did not update the field correctly"
        print("Region dropdown: cancel preserves selection" if region_cancel else "Region dropdown: Europe selected using Down + confirm")
    if queue_mode:
        nds = Image.open(output_path / "queue-nds.png")
        gba = Image.open(output_path / "queue-gba.png")
        others = Image.open(output_path / "queue-others.png")
        assert nds.crop((104, 220, 248, 316)).tobytes() != gba.crop((104, 220, 248, 316)).tobytes(), "Queue detail did not change with selection"
        for picture in (nds, gba):
            assert len(set(picture.crop((8, 224, 96, 312)).get_flattened_data())) > 900, "Queue cover missing"
        assert others.crop((104, 220, 248, 316)).tobytes() != gba.crop((104, 220, 248, 316)).tobytes(), "Others title/system detail missing"
        assert len(set(others.crop((8, 224, 96, 312)).get_flattened_data())) < 200, "Others reused an NDS cover with the same ID"
        assert others.crop((0, 316, 256, 336)).tobytes() != nds.crop((0, 316, 256, 336)).tobytes(), "Others SD destination missing"
        print("Queue previews: NDS/GBA covers, Others title/destination/no-cover with same-ID isolation, navigation and Back passed")
    if startup_mode:
        pictures = [Image.open(output_path / (name + ".png")) for name in ("storage", "wifi", "authentication", "library")]
        assert len({picture.crop((0, 192, 256, 360)).tobytes() for picture in pictures}) == 4, "Startup stages did not update"
        assert len({picture.crop((0, 0, 256, 192)).tobytes() for picture in pictures}) == 1, "Startup branding changed between stages"
        print("Startup: SD, Wi-Fi, authentication and PC library stages rendered")
    (output_path / "options.json").write_text(json.dumps({k.decode(): v.decode() for k, v in variables.items()}, indent=2))
    print(json.dumps({"core": info.name.decode(), "version": info.version.decode(),
                      "size": last_image.size, "output": str(output_path)}))
finally:
    core.retro_unload_game()
    core.retro_deinit()
