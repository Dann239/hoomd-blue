# coding: utf-8

# Copyright (c) 2009-2019 The Regents of the University of Michigan
# This file is part of the HOOMD-blue project, released under the BSD 3-Clause
# License.

# Maintainer: joaander / All Developers are free to add commands for new
# features


from hoomd.md import _md
from hoomd._param_dict import ParameterDict
from hoomd.typeconverter import OnlyFrom
from hoomd.integrate import _BaseIntegrator
from hoomd.syncedlist import SyncedList
from hoomd.md.methods import _Method
from hoomd.md.force import _Force
from hoomd.md.constrain import _ConstraintForce


def preprocess_aniso(value):
    if value is True:
        return "true"
    elif value is False:
        return "false"
    else:
        return value


def set_synced_list(old_list, new_list):
    old_list.clear()
    old_list.extend(new_list)


class _DynamicIntegrator(_BaseIntegrator):
    def __init__(self, forces, constraints, methods):
        forces = [] if forces is None else forces
        constraints = [] if constraints is None else constraints
        methods = [] if methods is None else methods
        self._forces = SyncedList(lambda x: isinstance(x, _Force),
                                  to_synced_list=lambda x: x._cpp_obj,
                                  iterable=forces)

        self._constraints = SyncedList(lambda x: isinstance(x,
                                                            _ConstraintForce),
                                       to_synced_list=lambda x: x._cpp_obj,
                                       iterable=constraints)

        self._methods = SyncedList(lambda x: isinstance(x, _Method),
                                   to_synced_list=lambda x: x._cpp_obj,
                                   iterable=methods)

    def _attach(self):
        self.forces._sync(self._simulation, self._cpp_obj.forces)
        self.constraints._sync(self._simulation, self._cpp_obj.constraints)
        self.methods._sync(self._simulation, self._cpp_obj.methods)
        super()._attach()

    @property
    def forces(self):
        return self._forces

    @forces.setter
    def forces(self, value):
        set_synced_list(self._forces, value)

    @property
    def constraints(self):
        return self._constraints

    @constraints.setter
    def constraints(self, value):
        set_synced_list(self._constraints, value)

    @property
    def methods(self):
        return self._methods

    @methods.setter
    def methods(self, value):
        set_synced_list(self._methods, value)


class Integrator(_DynamicIntegrator):
    R""" Enables a variety of standard integration methods.

    Args: dt (float): Each time step of the simulation ```hoomd.run```
    will advance the real time of the system forward by *dt* (in time units).
    aniso (bool): Whether to integrate rotational degrees of freedom (bool),
    default None (autodetect).

    ``mode_standard`` performs a standard time step integration
    technique to move the system forward. At each time step, all of the
    specified forces are evaluated and used in moving the system forward to the
    next step.

    By itself, ``mode_standard`` does nothing. You must specify one or
    more integration methods to apply to the system. Each integration method can
    be applied to only a specific group of particles enabling advanced
    simulation techniques.

    The following commands can be used to specify the integration methods used
    by integrate.mode_standard.

    - `hoomd.md.methods.Brownian`
    - `hoomd.md.methods.Langevin`
    - `hoomd.md.methods.NVE`
    - `hoomd.md.methods.NVT`
    - `hoomd.md.methods.npt`
    - `hoomd.md.methods.nph`

    There can only be one integration mode active at a time. If there are more
    than one ``integrate.mode_*`` commands in a hoomd script, only the most
    recent before a given ```hoomd.run``` will take effect.

    Examples::

        integrate.mode_standard(dt=0.005) integrator_mode =
        integrate.mode_standard(dt=0.001)
    """

    def __init__(self, dt, aniso=None, forces=None, constraints=None,
                 methods=None):

        super().__init__(forces, constraints, methods)

        self._param_dict = ParameterDict(
            dt=float(dt),
            aniso=OnlyFrom(['true', 'false', 'auto'],
                           preprocess=preprocess_aniso),
            _defaults=dict(aniso="auto")
            )
        if aniso is not None:
            self.aniso = aniso

    def _attach(self):
        # initialize the reflected c++ class
        self._cpp_obj = _md.IntegratorTwoStep(
            self._simulation.state._cpp_sys_def, self.dt)
        # Call attach from DynamicIntegrator which attaches forces,
        # constraint_forces, and methods, and calls super()._attach() itself.
        super()._attach()
