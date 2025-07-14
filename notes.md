

# Build steps

- get artifact.zip from Pydrofoil-PyPy-plugin CI build (should run on any recent Ubuntu)
    - alternative: run `make pypy-c-pydrofoil-riscv-package` in pydrofoil checkout, but it takes forever
- unzip it, then untar the inner .tar.bz2 (Github does this double packaging :-()
    - `unzip artifact.zip`
    - `tar xf pypy-pydrofoil-scripting-experimental.tar.bz2`
- build and run (sorry, horrible .sh file):
    - `. build.sh`
