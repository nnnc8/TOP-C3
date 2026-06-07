import struct
from enum import Enum
from io import BytesIO, SEEK_CUR, SEEK_END, open  # noqa: F401

__version__ = "0.11"
API_VERSION = (0, 11)


class KaitaiStruct:
  def __init__(self, io):
    self._io = io

  def __enter__(self):
    return self

  def __exit__(self, *args, **kwargs):
    self.close()

  def close(self):
    self._io.close()

  @classmethod
  def from_file(cls, filename):
    f = open(filename, "rb")
    try:
      return cls(KaitaiStream(f))
    except Exception:
      f.close()
      raise

  @classmethod
  def from_bytes(cls, buf):
    return cls(KaitaiStream(BytesIO(buf)))

  @classmethod
  def from_io(cls, io):
    return cls(KaitaiStream(io))


class ValidationFailedError(Exception):
  def __init__(self, message, io, src_path):
    super().__init__(message)
    self.io = io
    self.src_path = src_path


class ValidationNotEqualError(ValidationFailedError):
  def __init__(self, expected, actual, io, src_path):
    super().__init__(f"not equal, expected {expected!r}, but got {actual!r}", io, src_path)
    self.expected = expected
    self.actual = actual


class KaitaiStream:
  packer_s1 = struct.Struct("b")
  packer_s2be = struct.Struct(">h")
  packer_s4be = struct.Struct(">i")
  packer_s2le = struct.Struct("<h")
  packer_s4le = struct.Struct("<i")
  packer_u1 = struct.Struct("B")
  packer_u2be = struct.Struct(">H")
  packer_u4be = struct.Struct(">I")
  packer_u2le = struct.Struct("<H")
  packer_u4le = struct.Struct("<I")
  packer_f4le = struct.Struct("<f")
  packer_f8le = struct.Struct("<d")

  def __init__(self, io):
    self._io = io
    self.bits = 0
    self.bits_left = 0

  def __enter__(self):
    return self

  def __exit__(self, *args, **kwargs):
    self.close()

  def close(self):
    self.align_to_byte()
    self._io.close()

  def align_to_byte(self):
    self.bits = 0
    self.bits_left = 0

  def seek(self, n):
    if n < 0:
      raise ValueError(f"cannot seek to invalid position {n}")
    self.align_to_byte()
    self._io.seek(n)

  def pos(self):
    return self._io.tell()

  @staticmethod
  def resolve_enum(enum_obj, value):
    try:
      return enum_obj(value)
    except ValueError:
      return value

  def _read_bytes_not_aligned(self, n):
    data = self._io.read(n)
    if len(data) != n:
      raise EOFError(f"requested {n} bytes, but only {len(data)} bytes available")
    return data

  def read_bytes(self, n):
    self.align_to_byte()
    return self._read_bytes_not_aligned(n)

  def read_u1(self):
    return self.packer_u1.unpack(self.read_bytes(1))[0]

  def read_u2be(self):
    return self.packer_u2be.unpack(self.read_bytes(2))[0]

  def read_u4be(self):
    return self.packer_u4be.unpack(self.read_bytes(4))[0]

  def read_u2le(self):
    return self.packer_u2le.unpack(self.read_bytes(2))[0]

  def read_u4le(self):
    return self.packer_u4le.unpack(self.read_bytes(4))[0]

  def read_s1(self):
    return self.packer_s1.unpack(self.read_bytes(1))[0]

  def read_s2be(self):
    return self.packer_s2be.unpack(self.read_bytes(2))[0]

  def read_s4be(self):
    return self.packer_s4be.unpack(self.read_bytes(4))[0]

  def read_s2le(self):
    return self.packer_s2le.unpack(self.read_bytes(2))[0]

  def read_s4le(self):
    return self.packer_s4le.unpack(self.read_bytes(4))[0]

  def read_f4le(self):
    return self.packer_f4le.unpack(self.read_bytes(4))[0]

  def read_f8le(self):
    return self.packer_f8le.unpack(self.read_bytes(8))[0]

  def read_bits_int_be(self, n):
    res = 0
    bits_needed = n - self.bits_left
    self.bits_left = (-bits_needed) % 8

    if bits_needed > 0:
      bytes_needed = ((bits_needed - 1) // 8) + 1
      buf = self._read_bytes_not_aligned(bytes_needed)
      for byte in buf:
        res = (res << 8) | byte

      new_bits = res
      res = (res >> self.bits_left) | (self.bits << bits_needed)
      self.bits = new_bits
    else:
      res = self.bits >> (-bits_needed)

    mask = (1 << self.bits_left) - 1 if self.bits_left > 0 else 0
    self.bits &= mask
    return res
