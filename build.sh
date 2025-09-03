./pypy-pydrofoil-scripting-experimental/bin/pypy3 build_ext.py
gcc -pthread -DNDEBUG -O2 -fPIC -I./pypy-pydrofoil-scripting-experimental/include/pypy3.11 -c _pydrofoilcapi_cffi.c -o ./_pydrofoilcapi_cffi.o
gcc -pthread -shared -Wl,-Bsymbolic-functions ./_pydrofoilcapi_cffi.o -L./pypy-pydrofoil-scripting-experimental/bin -L./pypy-pydrofoil-scripting-experimental/pypy/goal -lpypy3.11-c -o ./libpydrofoilcapi_cffi.so
gcc -pthread -I./pypy-pydrofoil-scripting-experimental/include/pypy3.11/ -Wl,-Bsymbolic-functions testmain.c ./_pydrofoilcapi_cffi.c -L ./pypy-pydrofoil-scripting-experimental/bin  -l pypy3.11-c -o testplugin
LD_LIBRARY_PATH=.:./pypy-pydrofoil-scripting-experimental/bin ./testplugin ./input/rv64ui-p-addi.elf 100

