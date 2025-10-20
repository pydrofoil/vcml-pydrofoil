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

    def _set_dma_callback(self, dma_cb, payload):
        self.dma_cb = dma_cb
        self.dma_payload = payload
        self._region_struct = ffi.new('pydrofoil_dma_region_t*')
        self.dma_regions = []

        def get_ptr_and_offset(addr_bytes):
            # Search cached regions
            for guest_base, size, host_ptr in self.dma_regions:
                if guest_base <= addr_bytes < guest_base + size:
                    offset = (addr_bytes - guest_base) // 8
                    ptr = ffi.cast('uint64_t*', host_ptr)
                    return ptr, offset

            # Cache miss - call DMA callback
            res = self.dma_cb(self._handle, addr_bytes, self._region_struct, self.dma_payload)
            assert res == 0

            # Cache the new region
            region = (self._region_struct.guest_base,
                      self._region_struct.size,
                      self._region_struct.host_ptr)
            self.dma_regions.append(region)

            # Calculate pointer and offset
            guest_base, size, host_ptr = region
            offset = (addr_bytes - guest_base) // 8
            ptr = ffi.cast('uint64_t*', host_ptr)
            return ptr, offset

        def pyread(addr):
            addr = int(addr)
            addr_bytes = (addr << 3)
            ptr, offset = get_ptr_and_offset(addr_bytes)
            return _pydrofoil.bitvector(64, ptr[offset])

        def pywrite(addr, value):
            addr = int(addr)
            addr_bytes = (addr << 3)
            ptr, offset = get_ptr_and_offset(addr_bytes)
            ptr[offset] = value

        self.callbacks = _pydrofoil.Callbacks(mem_read8_intercept=pyread, mem_write8_intercept=pywrite)

    def step(self):
        self.steps += 1
        self.cpu.step()

    def reset(self):
        # Clear DMA region cache on reset
        if hasattr(self, 'dma_regions'):
            self.dma_regions = []

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
def pydrofoil_cpu_set_dma_callback(i, dma_cb, payload):
    cpu = ffi.from_handle(i)
    cpu._set_dma_callback(dma_cb, payload)
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
