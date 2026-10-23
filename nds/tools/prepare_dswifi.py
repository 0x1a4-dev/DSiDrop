"""Prepare the pinned, project-local dswifi sources for the DSi build."""
import hashlib
import io
from pathlib import Path, PurePosixPath
import re
import tarfile
import urllib.request


COMMIT = "ac8fbe9431d32cc4247df300d70c1688057da5e8"
RECEIVE_BUFFER = 32768
SOURCE_BLOBS = {
    "source/sgIP/sgIP_Config.h": "2fef6f3f762548f02b96604b0ba882720774c98f",
    "source/sgIP/sgIP_TCP.c": "7432ba03c82ad80596a3a4912c203a647448cee9",
    "source/sgIP/sgIP_TCP.h": "7a162bafad7f0a18b9b0549c6084784b13e2c8da",
}


def replace_once(source, old, new):
    if source.count(old) != 1:
        raise ValueError(f"Unexpected dswifi source: {old!r}")
    return source.replace(old, new, 1)


def patch_config(source):
    source, count = re.subn(
        r"(#define SGIP_TCP_RECEIVEBUFFERLENGTH[ \t]+)8192\b",
        lambda match: match[1] + str(RECEIVE_BUFFER), source,
    )
    if count != 1:
        raise ValueError("Unexpected dswifi receive buffer configuration")
    return source


def patch_tcp(source):
    source = replace_once(source, "rec->rxwindow=rec->ack+1400;",
                          "rec->rxwindow=rec->ack+SGIP_TCP_RECEIVEBUFFERLENGTH-1;")
    source = replace_once(
        source,
        "   if(windowlen>1400) windowlen=1400; // don't want to deal with IP fragmentation.\n",
        "",
    )
    return replace_once(
        source,
        "   if(windowlen<0 || windowlen>1400) windowlen=1400; // don't want to deal with IP fragmentation.",
        "   if(windowlen<0 || windowlen>=SGIP_TCP_RECEIVEBUFFERLENGTH) windowlen=SGIP_TCP_RECEIVEBUFFERLENGTH-1;\n"
        "   if(windowlen>65535) windowlen=65535;",
    )


def extract_sources(data, destination):
    prefix = f"dswifi-{COMMIT}"
    with tarfile.open(fileobj=io.BytesIO(data), mode="r:gz") as archive:
        for member in archive.getmembers():
            path = PurePosixPath(member.name)
            if ("\\" in member.name or ":" in member.name or path.is_absolute()
                    or ".." in path.parts or not path.parts or path.parts[0] != prefix):
                raise ValueError("Unexpected path in dswifi archive")
            target = destination.joinpath(*path.parts)
            if member.isdir():
                target.mkdir(parents=True, exist_ok=True)
            elif member.isfile():
                target.parent.mkdir(parents=True, exist_ok=True)
                with archive.extractfile(member) as contents:
                    target.write_bytes(contents.read())
            else:
                raise ValueError("Unsupported entry in dswifi archive")
    return destination / prefix


def prepare(destination):
    destination.mkdir(parents=True, exist_ok=True)
    cached = destination / f"{COMMIT}.tar.gz"
    if not cached.exists():
        url = f"https://codeload.github.com/devkitPro/dswifi/tar.gz/{COMMIT}"
        with urllib.request.urlopen(url, timeout=60) as response:
            data = response.read(8 * 1024 * 1024 + 1)
        if len(data) > 8 * 1024 * 1024:
            raise ValueError("dswifi archive is too large")
        cached.write_bytes(data)
    source = extract_sources(cached.read_bytes(), destination)
    for relative, expected in SOURCE_BLOBS.items():
        data = (source / relative).read_bytes()
        actual = hashlib.sha1(f"blob {len(data)}\0".encode("ascii") + data).hexdigest()
        if actual != expected:
            raise ValueError(f"dswifi source verification failed: {relative}")
    config = source / "source/sgIP/sgIP_Config.h"
    tcp = source / "source/sgIP/sgIP_TCP.c"
    config.write_text(patch_config(config.read_text(encoding="utf-8")), encoding="utf-8", newline="\n")
    tcp.write_text(patch_tcp(tcp.read_text(encoding="utf-8")), encoding="utf-8", newline="\n")
    print(f"dswifi 2.1.0 ({COMMIT}): {RECEIVE_BUFFER}-byte receive FIFO, free-space TCP window")
    return source


if __name__ == "__main__":
    prepare(Path(__file__).resolve().parents[1] / "build/dswifi")