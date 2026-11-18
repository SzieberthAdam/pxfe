"""Write the packages of every font and typeface of a library folder with the reference implementation, for test_package.c.

    python tests/pkg_reference.py --lib PIXELGLYPHLIB --root FOLDER --out DIR [--zlib DLL]

Runs ``pixelglyphlib.package export pxfm`` and ``export pxtf`` on every pxfm and pxtf file of FOLDER (hashes ascending),
writing into DIR/pxfp and DIR/pxtp.
Python's zlib module may be zlib-ng (the Windows builds of Python 3.14 and later), whose deflate gives other bytes than zlib's.
With ``--zlib``, the deflate of that zlib DLL (for example MSYS2's zlib1.dll) takes the place of the zlib module inside
zipfile, so the reference writes what it writes with zlib; everything else of the reference is unchanged.
The first line printed is the deflate in use.
"""

import argparse
import ctypes
import sys
import zlib
from pathlib import Path


class _ZStream(ctypes.Structure):  # z_stream of zlib.h; uLong is 32 bits on Windows
    _fields_ = [("next_in", ctypes.c_void_p), ("avail_in", ctypes.c_uint), ("total_in", ctypes.c_ulong),
                ("next_out", ctypes.c_void_p), ("avail_out", ctypes.c_uint), ("total_out", ctypes.c_ulong),
                ("msg", ctypes.c_char_p), ("state", ctypes.c_void_p),
                ("zalloc", ctypes.c_void_p), ("zfree", ctypes.c_void_p), ("opaque", ctypes.c_void_p),
                ("data_type", ctypes.c_int), ("adler", ctypes.c_ulong), ("reserved", ctypes.c_ulong)]


class DllZlib:
    """The zlib module with compressobj taken from a zlib DLL."""

    def __init__(self, path):
        self.dll = ctypes.CDLL(str(path))
        self.dll.zlibVersion.restype = ctypes.c_char_p
        self.dll.deflateBound.restype = ctypes.c_ulong
        self.version = self.dll.zlibVersion()
        self.ZLIB_RUNTIME_VERSION = self.version.decode()

    def __getattr__(self, name):
        return getattr(zlib, name)

    def compressobj(self, level=-1, method=zlib.DEFLATED, wbits=15, memlevel=8, strategy=0):
        return _Compressor(self, level, method, wbits, memlevel, strategy)


class _Compressor:
    def __init__(self, z, *args):
        self.z, self.args, self.parts = z, args, []

    def compress(self, data):
        self.parts.append(bytes(data))
        return b""

    def flush(self, mode=zlib.Z_FINISH):
        data = b"".join(self.parts)
        self.parts = []
        dll, s = self.z.dll, _ZStream()
        level, method, wbits, memlevel, strategy = self.args
        if dll.deflateInit2_(ctypes.byref(s), level, method, wbits, memlevel, strategy, self.z.version, ctypes.sizeof(s)) != 0:
            raise RuntimeError("deflateInit2_ failed")
        size = dll.deflateBound(ctypes.byref(s), len(data)) + 16
        source = ctypes.create_string_buffer(data, max(len(data), 1))
        target = ctypes.create_string_buffer(size)
        s.next_in, s.avail_in = ctypes.addressof(source), len(data)
        s.next_out, s.avail_out = ctypes.addressof(target), size
        rc = dll.deflate(ctypes.byref(s), 4)  # Z_FINISH
        n = s.total_out
        dll.deflateEnd(ctypes.byref(s))
        if rc != 1:  # Z_STREAM_END
            raise RuntimeError(f"deflate returned {rc}")
        return target.raw[:n]


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("--lib", required=True, help="the folder that holds the pixelglyphlib package")
    parser.add_argument("--root", required=True)
    parser.add_argument("--out", required=True)
    parser.add_argument("--zlib", help="a zlib DLL whose deflate zipfile is to use")
    args = parser.parse_args()
    sys.path.insert(0, args.lib)
    import zipfile
    from pixelglyphlib import package
    if args.zlib:
        zipfile.zlib = DllZlib(args.zlib)
        print(f"deflate: zlib {zipfile.zlib.ZLIB_RUNTIME_VERSION} ({args.zlib})")
    else:
        print(f"deflate: Python's zlib {zlib.ZLIB_RUNTIME_VERSION}")
    sys.stdout.flush()
    root, out = Path(args.root), Path(args.out)
    status = 0
    for kind, folder in (("pxfm", "pxfp"), ("pxtf", "pxtp")):
        hashes = sorted(p.stem for p in (root / kind).glob("*/*." + kind))
        if hashes:
            status |= package.main(["export", kind, *hashes, "--out", str(out / folder), "--root", str(root)])
    return status


if __name__ == "__main__":
    sys.exit(main())
