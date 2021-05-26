Installing binaries
===================

**HOOMD-blue** binaries are available in the `glotzerlab-software <https://glotzerlab-software.readthedocs.io>`_
`Docker <https://hub.docker.com/>`_/`Singularity <https://www.sylabs.io/>`_ images and for Linux and macOS via the
`hoomd package on conda-forge <https://anaconda.org/conda-forge/hoomd>`_.

Singularity / Docker images
---------------------------

See the `glotzerlab-software documentation <https://glotzerlab-software.readthedocs.io/>`_ for container usage
information and cluster specific instructions.

Docker::

    $ docker pull glotzerlab/software

.. note::

    See the `glotzerlab-software documentation <https://glotzerlab-software.readthedocs.io/>`_ for cluster specific
    instructions.

.. note::

    Containers that include HOOMD-blue v3.0.0-beta releases have `beta` in their
    tags.

Installing with conda
---------------------

**HOOMD-blue** is available on `conda-forge <https://conda-forge.org>`_. To
install, first download and install `miniconda
<https://docs.conda.io/en/latest/miniconda.html>`_. Then install ``hoomd``
from the ``conda-forge`` channel::

    $ conda install -c conda-forge hoomd

Install the v3.0.0-beta release with::

    $ conda install -c conda-forge/label/hoomd_dev -c conda-forge hoomd

A build of HOOMD with support for NVIDIA GPUs is also available from the
``conda-forge`` channel::

    $ conda install -c conda-forge hoomd=*=*gpu*

Compiling from source
=====================

Obtain the source
-----------------

Download source releases directly from the web:
https://glotzerlab.engin.umich.edu/Downloads/hoomd

.. code-block:: bash

   $ curl -O https://glotzerlab.engin.umich.edu/Downloads/hoomd/hoomd-v3.0.0-beta.6.tar.gz

Or clone using Git:

.. code-block:: bash

   $ git clone --recursive https://github.com/glotzerlab/hoomd-blue

**HOOMD-blue** uses Git submodules. Either clone with the ``--recursive``
option, or execute ``git submodule update --init`` to fetch the submodules.

Install prerequisites
---------------------

**HOOMD-blue** requires a number of libraries to build. The flags ``ENABLE_MPI``,
``ENABLE_GPU``, ``ENABLE_TBB``, and ``BUILD_JIT`` each require additional libraries.

**General requirements**

- C++14 capable compiler (tested with ``gcc`` 7, 8, 9, 10 / ``clang`` 6, 7, 8, 9, 10, 11)
- Python >= 3.6
- NumPy >= 1.7
- pybind11 >= 2.2
- Eigen >= 3.2
- CMake >= 3.9

**For MPI parallel execution** (required when ``ENABLE_MPI=on``)

- MPI (tested with OpenMPI, MVAPICH)
- cereal >= 1.1

**For GPU execution** (required when ``ENABLE_GPU=on``)

- NVIDIA CUDA Toolkit >= 9.0

  *OR*

- `AMD ROCm >= 3.5.0 <https://rocm.github.io/ROCmInstall.html>`_ with additional dependencies:

  - HIP [with ``hipcc`` and ``hcc`` as backend]
  - rocFFT
  - rocPRIM
  - rocThrust
  - hipCUB, included for NVIDIA GPU targets, but required as an
    external dependency when building for AMD GPUs
  - roctracer-dev
  - Linux kernel >= 3.5.0

  For HOOMD-blue on AMD GPUs, the following limitations currently apply.

   1. Certain HOOMD-blue kernels trigger an `unknown HSA error <https://github.com/ROCm-Developer-Tools/HIP/issues/1662>`_.
   2. The `mpcd` component is disabled on AMD GPUs.
   3. Multi-GPU execution via unified memory is not available.

**For threaded parallelism on the CPU** (required when ``ENABLE_TBB=on``)

- Intel Threading Building Blocks >= 4.3

**For runtime code generation** (required when ``BUILD_JIT=on``)

- LLVM >= 6.0

**To build documentation**

- Doxygen >= 1.8.5 for C++ documentation (the C++ code is documented with comments, but Doxygen
  documentation building and formatting is not regularly tested).
- For Python see the :ref:`Build the documentation` section.

Install these tools with your system or virtual environment package manager.
HOOMD developers have had success with
``pacman`` (`arch linux <https://www.archlinux.org/>`_), ``apt-get`` (`ubuntu <https://ubuntu.com/>`_), `Homebrew
<https://brew.sh/>`_ (macOS), and `MacPorts <https://www.macports.org/>`_ (macOS).
Note that packages may be named differently, so check your system's package list and install any ``-dev`` packages as needed. ::

    $ your-package-manager install python python-numpy pybind11 eigen cmake openmpi cereal cuda

Typical HPC cluster environments provide python, numpy, cmake, cuda, and mpi, via a module system::

    $ module load gcc python cuda cmake

Some package managers (such as *pip*) and most clusters are missing some or all of pybind11, eigen, and cereal.
``install-prereq-headers.py`` will install the missing packages into your virtual environment::

    $ cd /path/to/hoomd-blue
    $ python3 install-prereq-headers.py

Run ``python3 install-prereq-headers.py -h`` to see a list of the command line options.

Configure a virtual environment
-------------------------------

When using a shared Python installation, create a `virtual environment
<https://docs.python.org/3/library/venv.html>`_ where you can install the dependencies and
**HOOMD-blue**.
You can install numpy and other python packages into your virtual environment using, *e.g.*, ``python3 -m pip install numpy``.
Note that other types of virtual environments
(such as *conda*) may work, but are not thoroughly tested. ::

    $ python3 -m venv /path/to/environment


Compile HOOMD-blue
------------------

Activate the environment and tell CMake to search for packages there
before configuring and installing **HOOMD-blue**. ::

    $ source /path/to/environment/bin/activate
    $ export CMAKE_PREFIX_PATH=/path/to/environment

By default, **HOOMD-blue** configures a *Release* optimized build type for a
generic CPU architecture and with no optional libraries::

    $ cd /path/to/hoomd-blue
    $ cmake -B build
    $ cd build

Pass these options to cmake
to enable optimizations specific to your CPU::

    -DCMAKE_CXX_FLAGS=-march=native -DCMAKE_C_FLAGS=-march=native

Set ``-DENABLE_GPU=ON`` to compile for the GPU and ``-DENABLE_MPI=ON`` to enable parallel simulations with MPI.
See the build options section below for a full list of options.

Compile::

    $ make -j4

Test your build::

    $ ctest
    $ pytest hoomd

Test your build with mpi (example, use the appropriate MPI launcher for your
system)::

    $  mpirun -n 2 hoomd/pytest/pytest-openmpi.sh hoomd -v -x -ra

Execute longer running validation tests::

    $ pytest hoomd --validate -m validate
    $ mpirun -n 2 hoomd/pytest/pytest-openmpi.sh hoomd -v -x -ra --validate -m validate

.. attention::

    On a cluster, run tests within a job on a GPU compute node.

Install **HOOMD-blue** into your Python environment::

    $ make install

Build options
-------------

To change HOOMD build options, navigate to the ``build`` directory and run::

    $ ccmake .

After changing an option, press ``c`` to configure, then press ``g`` to
generate. The ``Makefile`` is now updated with the newly selected
options. You can also set these parameters on the command line with
``cmake``::

    $ cmake . -DENABLE_GPU=ON

Options that specify library versions only take effect on a clean invocation of
CMake. To set these options, first remove ``CMakeCache.txt`` and then run ``cmake``
and specify these options on the command line:

- ``PYTHON_EXECUTABLE`` - Specify which ``python`` to build against. Example: ``/usr/bin/python3``.

  - Default: ``python3.X`` detected on ``$PATH``

- ``CMAKE_CUDA_COMPILER`` - Specify which ``nvcc`` or ``hipcc`` to build with.

  - Default: location of ``nvcc`` detected on ``$PATH``

- ``MPI_HOME`` (env var) - Specify the location where MPI is installed.

  - Default: location of ``mpicc`` detected on the ``$PATH``

Other option changes take effect at any time. These can be set from within
``ccmake`` or on the command line:

- ``BUILD_HPMC`` - Enables building the ``hoomd.hpmc`` module.
- ``BUILD_MD`` - Enables building the ``hoomd.md`` module.
- ``BUILD_METAL`` - Enables building the ``hoomd.metal`` module.
- ``BUILD_TESTING`` - Enables the compilation of unit tests.
- ``CMAKE_BUILD_TYPE`` - Sets the build type (case sensitive) Options:

  - ``Debug`` - Compiles debug information into the library and executables.
    Enables asserts to check for programming mistakes. HOOMD-blue will run
    slow when compiled in Debug mode, but problems are easier to identify.
  - ``RelWithDebInfo`` - Compiles with optimizations and debug symbols.
    Useful for profiling benchmarks.
  - ``Release`` - (default) All compiler optimizations are enabled and
    asserts are removed. Recommended for production builds: required for any
    benchmarking.

- ``ENABLE_GPU`` - Enable compiling of the GPU accelerated computations. Default: ``OFF``.
- ``ENABLE_DOXYGEN`` - Enables the generation of developer documentation
  Default: ``OFF``.
- ``SINGLE_PRECISION`` - Controls precision. Default: ``OFF``.

  - When set to ``ON``, all calculations are performed in single precision.
  - When set to ``OFF``, all calculations are performed in double precision.

- ``ENABLE_HPMC_MIXED_PRECISION`` - Controls mixed precision in the hpmc
  component. When on, single precision is forced in expensive shape overlap
  checks.
- ``ENABLE_MPI`` - Enable multi-processor/GPU simulations using MPI.

  - When set to ``ON``, multi-processor/multi-GPU simulations are supported.
  - When set to ``OFF`` (the default), always run in single-processor/single-GPU mode.

- ``ENABLE_MPI_CUDA`` - Enable CUDA-aware MPI library support.

  - Requires a MPI library with CUDA support to be installed.
  - When set to ``ON`` (default if a CUDA-aware MPI library is detected),
    **HOOMD-blue** will make use of the capability of the MPI library to
    accelerate CUDA-buffer transfers.
  - When set to ``OFF``, standard MPI calls will be used.
  - *Warning:* Manually setting this feature to ``ON`` when the MPI library
    does not support CUDA may cause **HOOMD-blue** to crash.

- ``ENABLE_TBB`` - Enable support for Intel's Threading Building Blocks (TBB).

  - Requires TBB to be installed.
  - When set to ``ON``, HOOMD will use TBB to speed up calculations in some
    classes on multiple CPU cores.

These options control CUDA compilation via ``nvcc``:

- ``CUDA_ARCH_LIST`` - A semicolon-separated list of GPU architectures to
  compile in.

Build the documentation
-----------------------

Requirements

- Sphinx (>= 4.0 preferred)
- sphinx_rtd_theme
- nbsphinx
- IPython / Jupyter notebooks

Install these packages using a package manager.
This can be the same virtual environment as used for building the package.
Here is an example creating of a virtual environment using Python's ``venv`` and ``pip`` from the
base HOOMD directory:

.. code-block:: shell

    $ python3 -m venv ~/envs/hoomd-docs-env
    $ . ~/envs/hoomd-docs-env/bin/activate
    (hoomd-docs-env) $ python3 -m pip -r doc-requirements.txt

With conda:

.. code-block:: shell

    $ conda create -n hoomd-docs-env python=3.9
    $ conda activate hoomd-docs-env
    (hoomd-docs-env) $ conda install -c conda-forge sphinx sphinx_rtd_theme nbsphinx jupyter

Use sphinx to build the documentation::

    $ cd /path/to/hoomd-blue
    $ cd sphinx-doc
    $ make html

Then, open the documentation in ``_build/html/index.html``. On some platforms, namely MacOS, you can
use this command to do open the file::

    $ open _build/html/index.html

For Linux,::

    $ xdg-open _build/html/index.html

should work.

To open in a browser type in the address bar,::

    file:///path/to/sphinx-doc/_build/html/index.html

.. note::
    The documentation build process requires git submodules. Either clone with the
    ``--recursive`` option, or execute ``git submodule update --init`` to fetch the submodules.
