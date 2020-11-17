# Copyright (c) 2009-2019 The Regents of the University of Michigan
# This file is part of the HOOMD-blue project, released under the BSD 3-Clause
# License.

"""Version and build information.

Attributes:
    compile_date (str): The date this build was compiled.

    compile_flags (str): Human readable summary of compilation flags.

    cxx_compiler (str): Name and version of the C++ compiler used to build
        HOOMD.

    git_branch (str):  Name of the git branch used when compiling this build.

    git_sha1 (str):  SHA1 of the git commit used when compiling this build.

    gpu_api_version (str): The GPU API version this build was compiled against.

    gpu_enabled (bool): ``True`` when this build supports GPUs.

    gpu_platform (str): Name of the GPU platform this build was compiled
        against.

    install_dir (str): The installation directory.

    mpi_enabled (bool): ``True`` when this build supports MPI parallel runs.

    source_dir (str): The source directory.

    tbb_enabled (bool): ``True`` when this build supports TBB threads.

    version (str): HOOMD-blue package version, following semantic versioning.
"""
from hoomd import _hoomd

try:
    import hoomd.version_config

    compile_date = hoomd.version_config.compile_date
    git_branch = hoomd.version_config.git_branch
    git_sha1 = hoomd.version_config.git_sha1
except ImportError:
    # Allow sphinx docs to build when missing CMake generated python files
    compile_date = "n/a"
    git_branch = "n/a"
    git_sha1 = "n/a"

version = _hoomd.BuildInfo.getVersion()
compile_flags = _hoomd.BuildInfo.getCompileFlags()
gpu_enabled = _hoomd.BuildInfo.getEnableGPU()
gpu_api_version = _hoomd.BuildInfo.getGPUAPIVersion()
gpu_platform = _hoomd.BuildInfo.getGPUPlatform()
cxx_compiler = _hoomd.BuildInfo.getCXXCompiler()
tbb_enabled = _hoomd.BuildInfo.getEnableTBB()
mpi_enabled = _hoomd.BuildInfo.getEnableMPI()
source_dir = _hoomd.BuildInfo.getSourceDir()
install_dir = _hoomd.BuildInfo.getInstallDir()
