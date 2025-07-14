from _pydrofoilcapi_cffi import ffi
import _pydrofoil

all_cpu_handles = []

class C:
    def __init__(self, rv64, n=None):
        self.rv64 = rv64
        self.arg = n
        self.reset()

    def step(self):
        self.steps += 1
        self.cpu.step()

    def reset(self):
        if self.rv64:
            self.cpu = _pydrofoil.RISCV64(self.arg)
        else:
            self.cpu = _pydrofoil.RISCV32(self.arg)
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

    all_cpu_handles.append(res := ffi.new_handle(C(rv64, filename)))
    return res

@ffi.def_extern()
def pydrofoil_free_cpu(i):
    try:
        all_cpu_handles.remove(i)
    except Exception:
        return -1
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
