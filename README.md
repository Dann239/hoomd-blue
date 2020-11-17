# HOOMD-blue

[![Citing HOOMD](https://img.shields.io/badge/cite-hoomd-blue.svg)](https://glotzerlab.engin.umich.edu/hoomd-blue/citing.html)
[![conda-forge](https://img.shields.io/conda/vn/conda-forge/hoomd.svg?style=flat)](https://anaconda.org/conda-forge/hoomd)
[![conda-forge Downloads](https://img.shields.io/conda/dn/conda-forge/hoomd.svg?style=flat)](https://anaconda.org/conda-forge/hoomd)
[![Azure Pipelines](https://dev.azure.com/glotzerlab/hoomd-blue/_apis/build/status/test?branchName=master)](https://dev.azure.com/glotzerlab/hoomd-blue/_build)
[![Read the Docs](https://img.shields.io/readthedocs/hoomd-blue/stable.svg)](https://hoomd-blue.readthedocs.io/en/stable/?badge=stable)
[![Contributors](https://img.shields.io/github/contributors-anon/glotzerlab/hoomd-blue.svg?style=flat)](https://hoomd-blue.readthedocs.io/en/stable/credits.html)
[![License](https://img.shields.io/badge/license-BSD--3--Clause-green.svg)](LICENSE)

**HOOMD-blue** is a general purpose particle simulation toolkit. It performs
hard particle Monte Carlo simulations of a variety of shape classes, and
molecular dynamics simulations of particles with a range of pair, bond, angle,
and other potentials. **HOOMD-blue** runs fast on NVIDIA GPUs, and can scale
across thousands of nodes. For more information, see the [**HOOMD-blue**
website](https://glotzerlab.engin.umich.edu/hoomd-blue/).

## Resources

- [Documentation](https://hoomd-blue.readthedocs.io/):
  Tutorial, full package Python API, usage information, and feature reference.
- [Installation Guide](INSTALLING.rst):
  Instructions for installing and compiling **HOOMD-blue**.
- [hoomd-users Google Group](https://groups.google.com/d/forum/hoomd-users):
  Ask questions to the **HOOMD-blue** community.
- [**HOOMD-blue** website](https://glotzerlab.engin.umich.edu/hoomd-blue/):
  Additional information, benchmarks, and publications.

## Installation

**HOOMD-blue** binaries are available in the [glotzerlab-software](https://glotzerlab-software.readthedocs.io)
[Docker](https://hub.docker.com/)/[Singularity](https://www.sylabs.io/) images and for Linux and macOS via the
[hoomd package on conda-forge](https://anaconda.org/conda-forge/hoomd).
See the [Installation Guide](INSTALLING.rst) for instructions on installing
**HOOMD-blue** or compiling from source.

## Job scripts

HOOMD-blue job scripts are Python scripts.  You can control system
initialization, run protocols, analyze simulation data, or develop complex
workflows all with Python code in your job.

## Change log

See [CHANGELOG.rst](CHANGELOG.rst).

## Contributing to HOOMD-blue

Contributions are welcomed via
[pull requests](https://github.com/glotzerlab/hoomd-blue/pulls).
Please report bugs and suggest feature enhancements via the
[issue tracker](https://github.com/glotzerlab/hoomd-blue/issues).
See [CONTRIBUTING.md](CONTRIBUTING.md) for more information.
