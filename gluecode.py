from _pydrofoilcapi_cffi import ffi
import _pydrofoil

import sys
sys.modules['__main__'] = type(sys)('__main__')

all_cpu_handles = []

class C:
    def __init__(self, rv64, n=None):
        self.rv64 = rv64
        self.arg = n
        self.callbacks = None
        self.reset()

    def _set_callbacks(self, read, write, payload):
        self.read = read
        self.write = write
        self.mem = ffi.new('uint64_t[1]')
        #mem = ffi.new('unsigned long[]', 1)
        def pyread(addr):
            addr = int(addr)
            addr = (addr << 3)
            res = self.read(self._handle, addr, 8, ffi.cast('uint64_t*', self.mem), payload)
            assert res == 0
            return _pydrofoil.bitvector(64, self.mem[0])
        def pywrite(addr, value):
            addr = int(addr)
            addr = (addr << 3)
            res = self.write(self._handle, addr, 8, value, payload)
            assert res == 0
        self.callbacks = _pydrofoil.Callbacks(mem_read8_intercept=pyread, mem_write8_intercept=pywrite)

    def step(self):
        self.steps += 1
        self.cpu.step()

    def reset(self):
        if self.rv64:
            cls = _pydrofoil.RISCV64
        else:
            cls = _pydrofoil.RISCV32
        if self.callbacks:
            self.cpu = cls(self.arg, callbacks=self.callbacks)
        else:
            self.cpu = cls(self.arg)
        self.steps = 0

@ffi.def_extern()
def pydrofoil_allocate_cpu(spec, fn):
    if spec:
        rv64 = "64" in ffi.string(spec).decode('utf-8')
    else:
        rv64 = True
    if fn:
        filename = ffi.string(fn).decode('utf-8')
    else:
        filename = None
    print("rv64" if rv64 else "rv32")
    print(filename)

    all_cpu_handles.append(res := ffi.new_handle(cpu := C(rv64, filename)))
    cpu._handle = res
    return res

@ffi.def_extern()
def pydrofoil_free_cpu(i):
    try:
        all_cpu_handles.remove(i)
    except Exception:
        return -1
    return 0

@ffi.def_extern()
def pydrofoil_cpu_set_ram_read_write_callback(i, read_cb, write_cb, payload):
    cpu = ffi.from_handle(i)
    cpu._set_callbacks(read_cb, write_cb, payload)
    cpu.reset()
    return 0

@ffi.def_extern()
def pydrofoil_cpu_simulate(i, steps):
    cpu = ffi.from_handle(i)
    for i in range(steps):
        cpu.step()
    return steps

@ffi.def_extern()
def pydrofoil_cpu_cycles(i):
    cpu = ffi.from_handle(i)
    return cpu.steps

@ffi.def_extern()
def pydrofoil_cpu_pc(i):
    cpu = ffi.from_handle(i)
    return cpu.cpu.read_register('pc')

@ffi.def_extern()
def pydrofoil_cpu_reset(i):
    cpu = ffi.from_handle(i)
    cpu.reset()
    return 0

@ffi.def_extern()
def pydrofoil_cpu_set_verbosity(i, v):
    cpu = ffi.from_handle(i)
    cpu.cpu.set_verbosity(bool(v))
    return 0

@ffi.def_extern()
def pydrofoil_cpu_set_pc(i, val):
    cpu = ffi.from_handle(i)
    cpu.cpu.write_register('pc', val)
    return 0

sys.modules['__main__'].__dict__.update(globals())
sys.argv = ['embedded-pypy']
