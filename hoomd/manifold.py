# Copyright (c) 2009-2019 The Regents of the University of Michigan
# This file is part of the HOOMD-blue project, released under the BSD 3-Clause License.

# Maintainer: joaander / All Developers are free to add commands for new features

R""" Manifold.

Manifold defining a positional constraint to a given set of particles. For example, a group of particles 
can be constrained to the surface of a sphere with :py:class:`sphere`.

Warning:
    Only one manifold can be applied to the integrators/active forces.

The degrees of freedom removed from the system by constraints are correctly taken into account when computing the
temperature for thermostatting and logging.
"""

from hoomd import _hoomd
from hoomd.md import _md
import hoomd;

## \internal
# \brief Base class for manifold
#
# A manifold in hoomd reflects a Manifold in c++. It is respnsible to define the manifold 
# used for RATTLE intergrators and the active force constraints.
class _manifold():
    ## \internal
    # \brief Constructs the constraint force
    #
    # \param name name of the constraint force instance
    #
    # Initializes the cpp_manifold to None.
    # If specified, assigns a name to the instance
    def __init__(self):
        # check if initialization has occurred
        if not hoomd.init.is_initialized():
            hoomd.context.msg.error("Cannot create manifold before initialization\n");
            raise RuntimeError('Error creating manifold');

        self.cpp_manifold = None;

        hoomd.context.current.manifold = self;

    ## \var cpp_manifold
    # \internal
    # \brief Stores the C++ side Manifold managed by this class

    def implicit_function(self, position):
        """Evaluate the implicit function.

        Args:
            position (np.array): The position to evaluate."""
        return self.cpp_manifold.implicit_function(_hoomd.make_scalar3(position[0], position[1], position[2]))

    def derivative(self, position):
        """Evaluate the derivative of the implicit function.

        Args:
            position (np.array): The position to evaluate at."""
        return self.cpp_manifold.derivative(_hoomd.make_scalar3(position[0], position[1], position[2]))
