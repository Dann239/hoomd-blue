# Copyright (c) 2009-2019 The Regents of the University of Michigan
# This file is part of the HOOMD-blue project, released under the BSD 3-Clause License.

import hoomd
from hoomd import _hoomd
from hoomd.md import _md
from hoomd.md import force
from hoomd.md import nlist as nl
from hoomd.md.nlist import _NList
from hoomd._type_param_dict import TypeParameterDict
from hoomd._param_dict import ParameterDict
from hoomd.typeparam import TypeParameter
from hoomd.typeconverter import OnlyFrom, OnlyType

import math
import json


class pair(force._force):
    pass


validate_nlist = OnlyType(_NList)


def validate_mode(value):
    acceptable = ['none', 'shifted', 'xplor']
    if value in acceptable:
        return value
    else:
        raise ValueError("{} not found in {}".format(value, acceptable))


class _Pair(force._Force):
    R""" Common pair potential documentation.

    Users should not invoke :py:class:`_Pair` directly. It is a base command that
    provides common features to all standard pair forces. Common documentation
    for all pair potentials is documented here.

    All pair force commands specify that a given potential energy and force be
    computed on all non-excluded particle pairs in the system within a short
    range cutoff distance :math:`r_{\mathrm{cut}}`.

    The force :math:`\vec{F}` applied between each pair of particles is:

    .. math::
        :nowrap:

        \begin{eqnarray*}
        \vec{F}  = & -\nabla V(r) & r < r_{\mathrm{cut}} \\
                  = & 0           & r \ge r_{\mathrm{cut}} \\
        \end{eqnarray*}

    where :math:`\vec{r}` is the vector pointing from one particle to the other
    in the pair, and :math:`V(r)` is chosen by a mode switch (see
    ``set_params()``):

    .. math::
        :nowrap:

        \begin{eqnarray*}
        V(r)  = & V_{\mathrm{pair}}(r) & \mathrm{mode\ is\ no\_shift} \\
              = & V_{\mathrm{pair}}(r) - V_{\mathrm{pair}}(r_{\mathrm{cut}}) &
              \mathrm{mode\ is\ shift} \\
              = & S(r) \cdot V_{\mathrm{pair}}(r) & \mathrm{mode\ is\ xplor\
              and\ } r_{\mathrm{on}} < r_{\mathrm{cut}} \\
              = & V_{\mathrm{pair}}(r) - V_{\mathrm{pair}}(r_{\mathrm{cut}}) &
              \mathrm{mode\ is\ xplor\ and\ } r_{\mathrm{on}} \ge
              r_{\mathrm{cut}}
        \end{eqnarray*}

    :math:`S(r)` is the XPLOR smoothing function:

    .. math::
        :nowrap:

        \begin{eqnarray*}
        S(r) = & 1 & r < r_{\mathrm{on}} \\
             = & \frac{(r_{\mathrm{cut}}^2 - r^2)^2 \cdot (r_{\mathrm{cut}}^2 +
             2r^2 - 3r_{\mathrm{on}}^2)}{(r_{\mathrm{cut}}^2 -
             r_{\mathrm{on}}^2)^3}
               & r_{\mathrm{on}} \le r \le r_{\mathrm{cut}} \\
             = & 0 & r > r_{\mathrm{cut}} \\
         \end{eqnarray*}

    and :math:`V_{\mathrm{pair}}(r)` is the specific pair potential chosen by
    the respective command.

    Enabling the XPLOR smoothing function :math:`S(r)` results in both the
    potential energy and the force going smoothly to 0 at :math:`r =
    r_{\mathrm{cut}}`, reducing the rate of energy drift in long simulations.
    :math:`r_{\mathrm{on}}` controls the point at which the smoothing starts, so
    it can be set to only slightly modify the tail of the potential. It is
    suggested that you plot your potentials with various values of
    :math:`r_{\mathrm{on}}` in order to find a good balance between a smooth
    potential function and minimal modification of the original
    :math:`V_{\mathrm{pair}}(r)`. A good value for the LJ potential is
    :math:`r_{\mathrm{on}} = 2 \cdot \sigma`.

    The split smoothing / shifting of the potential when the mode is ``xplor``
    is designed for use in mixed WCA / LJ systems. The WCA potential and it's
    first derivative already go smoothly to 0 at the cutoff, so there is no need
    to apply the smoothing function. In such mixed systems, set
    :math:`r_{\mathrm{on}}` to a value greater than :math:`r_{\mathrm{cut}}` for
    those pairs that interact via WCA in order to enable shifting of the WCA
    potential to 0 at the cutoff.

    The following coefficients must be set per unique pair of particle types.
    See :py:mod:`hoomd.md.pair` for information on how to set coefficients:

    - :math:`r_{\mathrm{cut}}` - *r_cut* (in distance units)
      - *optional*: defaults to the global r_cut specified in the pair command
    - :math:`r_{\mathrm{on}}` - *r_on* (in distance units)
      - *optional*: defaults to the global r_cut specified in the pair command

    When :math:`r_{\mathrm{cut}} \le 0` or is set to False, the particle type
    pair interaction is excluded from the neighbor list. This mechanism can be
    used in conjunction with multiple neighbor lists to make efficient
    calculations in systems with large size disparity. Functionally, this is
    equivalent to setting :math:`r_{\mathrm{cut}} = 0` in the pair force because
    negative :math:`r_{\mathrm{cut}}` has no physical meaning.
    """

    def __init__(self, nlist, r_cut=None, r_on=0., mode='none'):
        self._nlist = validate_nlist(nlist)
        r_cut = float if r_cut is None else float(r_cut)
        r_cut = TypeParameter('r_cut', 'particle_types',
                              TypeParameterDict(r_cut, len_keys=2)
                              )
        r_on = TypeParameter('r_on', 'particle_types',
                             TypeParameterDict(float(r_on), len_keys=2)
                             )
        self._extend_typeparam([r_cut, r_on])
        self._param_dict.update(
            ParameterDict(mode=OnlyFrom(['none', 'shifted', 'xplor'])))
        self.mode = mode

    def compute_energy(self, tags1, tags2):
        R""" Compute the energy between two sets of particles.

        Args:
            tags1 (``ndarray<int32>``): a numpy array of particle tags in the
                first group
            tags2 (``ndarray<int32>``): a numpy array of particle tags in the
                second group

        .. math::

            U = \sum_{i \in \mathrm{tags1}, j \in \mathrm{tags2}} V_{ij}(r)

        where :math:`V_{ij}(r)` is the pairwise energy between two particles
        :math:`i` and :math:`j`.

        Assumed properties of the sets *tags1* and *tags2* are:

        - *tags1* and *tags2* are disjoint
        - all elements in *tags1* and *tags2* are unique
        - *tags1* and *tags2* are contiguous numpy arrays of dtype int32

        None of these properties are validated.

        Examples::

            tags=numpy.linspace(0,N-1,1, dtype=numpy.int32)
            # computes the energy between even and odd particles
            U = mypair.compute_energy(tags1=numpy.array(tags[0:N:2]),
                                      tags2=numpy.array(tags[1:N:2]))

        """
        # TODO future versions could use np functions to test the assumptions
        # above and raise an error if they occur.
        return self._cpp_obj.computeEnergyBetweenSets(tags1, tags2)

    def _return_type_shapes(self):
        type_shapes = self.cpp_force.getTypeShapesPy()
        ret = [ json.loads(json_string) for json_string in type_shapes ]
        return ret

    def _attach(self):
        # create the c++ mirror class
        if not self._nlist._added:
            self._nlist._add(self._simulation)
        else:
            if self._simulation != self._nlist._simulation:
                raise RuntimeError("{} object's neighbor list is used in a "
                                   "different simulation.".format(type(self)))
        if not self.nlist._attached:
            self.nlist._attach()
        if isinstance(self._simulation.device, hoomd.device.CPU):
            cls = getattr(_md, self._cpp_class_name)
            self.nlist._cpp_obj.setStorageMode(
                _md.NeighborList.storageMode.half)
        else:
            cls = getattr(_md, self._cpp_class_name + "GPU")
            self.nlist._cpp_obj.setStorageMode(
                _md.NeighborList.storageMode.full)
        self._cpp_obj = cls(
            self._simulation.state._cpp_sys_def, self.nlist._cpp_obj,
            '')  # TODO remove name string arg

        super()._attach()

    @property
    def nlist(self):
        return self._nlist

    @nlist.setter
    def nlist(self, value):
        if self._attached:
            raise RuntimeError("nlist cannot be set after scheduling.")
        else:
            self._nlist = validate_nlist(value)


class LJ(_Pair):
    R""" Lennard-Jones pair potential.

    Args:
        nlist (:py:mod:`hoomd.md.nlist`): Neighbor list
        r_cut (float): Default cutoff radius (in distance units).
        r_on (float): Default turn-on radius (in distance units).
        mode (str): energy shifting/smoothing mode

    :py:class:`LJ` specifies that a Lennard-Jones pair potential should be
    applied between every non-excluded particle pair in the simulation.

    .. math::
        :nowrap:

        \begin{eqnarray*}
        V_{\mathrm{LJ}}(r)  = & 4 \varepsilon \left[ \left( \frac{\sigma}{r}
        \right)^{12} - \left( \frac{\sigma}{r} \right)^{6} \right] & r <
        r_{\mathrm{cut}} \\ = & 0 & r \ge r_{\mathrm{cut}} \\
        \end{eqnarray*}

    See :py:class:`_Pair` for details on how forces are calculated and the
    available energy shifting and smoothing modes.  Use ``params`` dictionary
    to set potential coefficients.

    The following coefficients must be set per unique pair of particle types:

    - :math:`\varepsilon` - *epsilon* (in energy units)
    - :math:`\sigma` - *sigma* (in distance units)
    - :math:`r_{\mathrm{cut}}` - *r_cut* (in distance units)
      - *optional*: defaults to the global r_cut specified in the pair command
    - :math:`r_{\mathrm{on}}`- *r_on* (in distance units)
      - *optional*: defaults to the global r_on specified in the pair command

    Example::

        nl = nlist.Cell()
        lj = pair.LJ(nl, r_cut=3.0)
        lj.params[('A', 'A')] = {'sigma': 1.0, 'epsilon': 1.0}
        lj.params[('A', 'B')] = dict(epsilon=2.0, sigma=1.0, r_cut=3.0, r_on=2.0)
    """
    _cpp_class_name = "PotentialPairLJ"
    def __init__(self, nlist, r_cut=None, r_on=0., mode='none'):
        super().__init__(nlist, r_cut, r_on, mode)
        params = TypeParameter('params', 'particle_types',
                               TypeParameterDict(epsilon=float, sigma=float,
                                                 len_keys=2)
                               )
        self._add_typeparam(params)

class Gauss(_Pair):
    R""" Gaussian pair potential.

    Args:
        nlist (:py:mod:`hoomd.md.nlist`): Neighbor list
        r_cut (float): Default cutoff radius (in distance units).
        r_on (float): Default turn-on radius (in distance units).
        mode (str): energy shifting/smoothing mode.

    :py:class:`Gauss` specifies that a Gaussian pair potential should be applied between every
    non-excluded particle pair in the simulation.

    .. math::
        :nowrap:

        \begin{eqnarray*}
        V_{\mathrm{gauss}}(r)  = & \varepsilon \exp \left[ -\frac{1}{2}\left( \frac{r}{\sigma} \right)^2 \right]
                                                & r < r_{\mathrm{cut}} \\
                               = & 0 & r \ge r_{\mathrm{cut}} \\
        \end{eqnarray*}

    See :py:class:`_Pair` for details on how forces are calculated and the available energy shifting and smoothing modes.
    Use ``params`` dictionary to set potential coefficients.

    The following coefficients must be set per unique pair of particle types:

    - :math:`\varepsilon` - *epsilon* (in energy units)
    - :math:`\sigma` - *sigma* (in distance units)
    - :math:`r_{\mathrm{cut}}` - *r_cut* (in distance units)
      - *optional*: defaults to the global r_cut specified in the pair command
    - :math:`r_{\mathrm{on}}`- *r_on* (in distance units)
      - *optional*: defaults to the global r_on specified in the pair command

    Example::

        nl = nlist.Cell()
        gauss = pair.Gauss(r_cut=3.0, nlist=nl)
        gauss.params[('A', 'A')] = dict(epsilon=1.0, sigma=1.0)
        gauss.params[('A', 'B')] = dict(epsilon=2.0, sigma=1.0, r_cut=3.0, r_on=2.0)

    """
    _cpp_class_name = "PotentialPairGauss"
    def __init__(self, nlist, r_cut=None, r_on=0., mode='none'):
        super().__init__(nlist, r_cut, r_on, mode)
        params = TypeParameter('params', 'particle_types',
                               TypeParameterDict(epsilon=float, sigma=float,
                                                 len_keys=2))
        self._add_typeparam(params)

class SLJ(_Pair):
    R""" Shifted Lennard-Jones pair potential.

    Args:
        nlist (:py:mod:`hoomd.md.nlist`): Neighbor list
        r_cut (float): Default cutoff radius (in distance units).
        r_on (float): Default turn-on radius (in distance units).
        mode (str): Energy shifting/smoothing mode

    :py:class:`SLJ` specifies that a shifted Lennard-Jones type pair potential should be applied between every
    non-excluded particle pair in the simulation.

    .. math::
        :nowrap:

        \begin{eqnarray*}
        V_{\mathrm{SLJ}}(r)  = & 4 \varepsilon \left[ \left( \frac{\sigma}{r - \Delta} \right)^{12} -
                               \left( \frac{\sigma}{r - \Delta} \right)^{6} \right] & r < (r_{\mathrm{cut}} + \Delta) \\
                             = & 0 & r \ge (r_{\mathrm{cut}} + \Delta) \\
        \end{eqnarray*}

    where :math:`\Delta = (d_i + d_j)/2 - 1` and :math:`d_i` is the diameter of particle :math:`i`.

    See :py:class:`_Pair` for details on how forces are calculated and the available energy shifting and smoothing modes.
    Use ``params`` dictionary to set potential coefficients.

    The following coefficients must be set per unique pair of particle types:

    - :math:`\varepsilon` - *epsilon* (in energy units)
    - :math:`\sigma` - *sigma* (in distance units)
      - *optional*: defaults to 1.0
    - :math:`r_{\mathrm{cut}}` - *r_cut* (in distance units)
      - *optional*: defaults to the global r_cut specified in the pair command
    - :math:`r_{\mathrm{on}}` - *r_on* (in distance units)
      - *optional*: defaults to the global r_on specified in the pair command

    .. attention::
        Due to the way that pair.SLJ modifies the cutoff criteria, a shift_mode of *xplor* is not supported.

    The actual cutoff radius for pair.SLJ is shifted by the diameter of two particles interacting.  Thus to determine
    the maximum possible actual r_cut in simulation
    pair.SLJ must know the maximum diameter of all the particles over the entire run.
    This value must be set by the user and can be
    modified between runs with the *max_diameter* property of the ``hoomd.md.nlist`` objects.

    The specified value of *max_diameter* will be used to properly determine the neighbor lists during the following
    ```sim.run``` commands.

    If particle diameters change after initialization, it is **imperative** that *max_diameter* be the largest
    diameter that any particle will attain at any time during the following ```sim.run``` commands.
    If *max_diameter* is smaller than it should be, some particles will effectively have a smaller value of *r_cut*
    than was set and the simulation will be incorrect. *max_diameter* can be changed between runs in the
    same way it was set via the *max_diameter* property of the ``hoomd.md.nlist`` objects.

    Example::

        nl = nlist.Cell()
        nl.max_diameter = 2.0
        slj = pair.SLJ(r_cut=3.0, nlist=nl)
        slj.params[('A', 'B')] = dict(epsilon=2.0, r_cut=3.0)
        slj.params[('B', 'B')] = {'epsilon': 1.0, 'r_cut': 2**(1.0/6.0)}

    """
    _cpp_class_name = 'PotentialPairSLJ'
    def __init__(self, nlist, r_cut=None, r_on=0., mode='none'):
        if mode == 'xplor':
            raise ValueError("xplor is not a valid mode for SLJ potential")

        super().__init__(nlist, r_cut, r_on, mode)
        params = TypeParameter('params', 'particle_types',
                               TypeParameterDict(epsilon=float, sigma=float,
                                                 alpha=1.0, len_keys=2)
                               )
        self._add_typeparam(params)

        # mode not allowed to be xplor, so re-do param dict entry without that option
        param_dict = ParameterDict(mode=OnlyFrom(['none', 'shifted']))
        self._param_dict.update(param_dict)
        self.mode = mode

        # this potential needs diameter shifting on
        self._nlist.diameter_shift = True

        # NOTE do we need something to automatically set the max_diameter correctly?


class Yukawa(_Pair):
    R""" Yukawa pair potential.

    Args:
        nlist (:py:mod:`hoomd.md.nlist`): Neighbor list
        r_cut (float): Default cutoff radius (in distance units).
        r_on (float): Default turn-on radius (in distance units).
        mode (str): Energy shifting mode.

    :py:class:`Yukawa` specifies that a Yukawa pair potential should be applied between every
    non-excluded particle pair in the simulation.

    .. math::
        :nowrap:

        \begin{eqnarray*}
         V_{\mathrm{yukawa}}(r)  = & \varepsilon \frac{ \exp \left( -\kappa r \right) }{r} & r < r_{\mathrm{cut}} \\
                            = & 0 & r \ge r_{\mathrm{cut}} \\
        \end{eqnarray*}

    See :py:class:`_Pair` for details on how forces are calculated and the available energy shifting and smoothing modes.
    Use ``params`` dictionary to set potential coefficients.

    The following coefficients must be set per unique pair of particle types:

    - :math:`\varepsilon` - *epsilon* (in energy units)
    - :math:`\kappa` - *kappa* (in units of 1/distance)
    - :math:`r_{\mathrm{cut}}` - *r_cut* (in distance units)
      - *optional*: defaults to the global r_cut specified in the pair command
    - :math:`r_{\mathrm{on}}`- *r_on* (in distance units)
      - *optional*: defaults to the global r_on specified in the pair command

    Example::

        nl = nlist.Cell()
        yukawa = pair.Yukawa(r_cut=3.0, nlist=nl)
        yukawa.params[('A', 'A')] = dict(epsilon=1.0, kappa=1.0)
        yukawa.params[('A', 'B')] = dict(epsilon=2.0, kappa=0.5, r_cut=3.0, r_on=2.0)

    """
    _cpp_class_name = "PotentialPairYukawa"
    def __init__(self, nlist, r_cut=None, r_on=0., mode='none'):
        super().__init__(nlist, r_cut, r_on, mode)
        params = TypeParameter('params', 'particle_types',
                               TypeParameterDict(kappa=float, epsilon=float,
                                                 len_keys=2))
        self._add_typeparam(params)

class Ewald(_Pair):
    R""" Ewald pair potential.

    Args:
        nlist (:py:mod:`hoomd.md.nlist`): Neighbor list
        r_cut (float): Default cutoff radius (in distance units).
        r_on (float): Default turn-on radius (in distance units).
        mode (str): Energy shifting mode.

    :py:class:`Ewald` specifies that a Ewald pair potential should be applied between every
    non-excluded particle pair in the simulation.

    .. math::
        :nowrap:

        \begin{eqnarray*}
         V_{\mathrm{ewald}}(r)  = & q_i q_j \left[\mathrm{erfc}\left(\kappa r + \frac{\alpha}{2\kappa}\right) \exp(\alpha r)+
                                    \mathrm{erfc}\left(\kappa r - \frac{\alpha}{2 \kappa}\right) \exp(-\alpha r)\right] & r < r_{\mathrm{cut}} \\
                            = & 0 & r \ge r_{\mathrm{cut}} \\
        \end{eqnarray*}

    The Ewald potential is designed to be used in conjunction with :py:class:`hoomd.md.charge.pppm`.

    See :py:class:`_Pair` for details on how forces are calculated and the available energy shifting and smoothing modes.
    Use ``params`` dictionary to set potential coefficients.

    The following coefficients must be set per unique pair of particle types:

    - :math:`\kappa` - *kappa* (Splitting parameter, in 1/distance units)
    - :math:`\alpha` - *alpha* (Debye screening length, in 1/distance units)
        .. versionadded:: 2.1
    - :math:`r_{\mathrm{cut}}` - *r_cut* (in distance units)
      - *optional*: defaults to the global r_cut specified in the pair command
    - :math:`r_{\mathrm{on}}`- *r_on* (in distance units)
      - *optional*: defaults to the global r_on specified in the pair command


    Example::

        nl = nlist.Cell()
        ewald = pair.Ewald(r_cut=3.0, nlist=nl)
        ewald.params[('A', 'A')] = dict(kappa=1.0, alpha=1.5)
        ewald.params[('A', 'B')] = dict(kappa=1.0, r_cut=3.0, r_on=2.0)

    Warning:
        **DO NOT** use in conjunction with :py:class:`hoomd.md.charge.pppm`. It automatically creates and configures
        :py:class:`Ewald` for you.

    """
    _cpp_class_name = "PotentialPairEwald"
    def __init__(self, nlist, r_cut=None, r_on=0., mode='none'):
        super().__init__(nlist, r_cut, r_on, mode)
        params = TypeParameter('params', 'particle_types',
                               TypeParameterDict(kappa=float, alpha=0.0,
                                             len_keys=2))
        self._add_typeparam(params)


def _table_eval(r, rmin, rmax, V, F, width):
    dr = (rmax - rmin) / float(width-1);
    i = int(round((r - rmin)/dr))
    return (V[i], F[i])

class table(force._force):
    R""" Tabulated pair potential.

    Args:
        width (int): Number of points to use to interpolate V and F.
        nlist (:py:mod:`hoomd.md.nlist`): Neighbor list (default of None automatically creates a global cell-list based neighbor list)
        name (str): Name of the force instance

    :py:class:`table` specifies that a tabulated pair potential should be applied between every
    non-excluded particle pair in the simulation.

    The force :math:`\vec{F}` is (in force units):

    .. math::
        :nowrap:

        \begin{eqnarray*}
        \vec{F}(\vec{r})     = & 0                           & r < r_{\mathrm{min}} \\
                             = & F_{\mathrm{user}}(r)\hat{r} & r_{\mathrm{min}} \le r < r_{\mathrm{max}} \\
                             = & 0                           & r \ge r_{\mathrm{max}} \\
        \end{eqnarray*}

    and the potential :math:`V(r)` is (in energy units)

    .. math::
        :nowrap:

        \begin{eqnarray*}
        V(r)       = & 0                    & r < r_{\mathrm{min}} \\
                   = & V_{\mathrm{user}}(r) & r_{\mathrm{min}} \le r < r_{\mathrm{max}} \\
                   = & 0                    & r \ge r_{\mathrm{max}} \\
        \end{eqnarray*}

    where :math:`\vec{r}` is the vector pointing from one particle to the other in the pair.

    :math:`F_{\mathrm{user}}(r)` and :math:`V_{\mathrm{user}}(r)` are evaluated on *width* grid points between
    :math:`r_{\mathrm{min}}` and :math:`r_{\mathrm{max}}`. Values are interpolated linearly between grid points.
    For correctness, you must specify the force defined by: :math:`F = -\frac{\partial V}{\partial r}`.

    The following coefficients must be set per unique pair of particle types:

    - :math:`V_{\mathrm{user}}(r)` and :math:`F_{\mathrm{user}}(r)` - evaluated by ``func`` (see example)
    - coefficients passed to ``func`` - *coeff* (see example)
    - :math:`_{\mathrm{min}}` - *rmin* (in distance units)
    - :math:`_{\mathrm{max}}` - *rmax* (in distance units)

    .. rubric:: Set table from a given function

    When you have a functional form for V and F, you can enter that
    directly into python. :py:class:`table` will evaluate the given function over *width* points between
    *rmin* and *rmax* and use the resulting values in the table::

        def lj(r, rmin, rmax, epsilon, sigma):
            V = 4 * epsilon * ( (sigma / r)**12 - (sigma / r)**6);
            F = 4 * epsilon / r * ( 12 * (sigma / r)**12 - 6 * (sigma / r)**6);
            return (V, F)

        nl = nlist.cell()
        table = pair.table(width=1000, nlist=nl)
        table.pair_coeff.set('A', 'A', func=lj, rmin=0.8, rmax=3.0, coeff=dict(epsilon=1.5, sigma=1.0))
        table.pair_coeff.set('A', 'B', func=lj, rmin=0.8, rmax=3.0, coeff=dict(epsilon=2.0, sigma=1.2))
        table.pair_coeff.set('B', 'B', func=lj, rmin=0.8, rmax=3.0, coeff=dict(epsilon=0.5, sigma=1.0))

    .. rubric:: Set a table from a file

    When you have no function for for *V* or *F*, or you otherwise have the data listed in a file,
    :py:class:`table` can use the given values directly. You must first specify the number of rows
    in your tables when initializing pair.table. Then use :py:meth:`set_from_file()` to read the file::

        nl = nlist.cell()
        table = pair.table(width=1000, nlist=nl)
        table.set_from_file('A', 'A', filename='table_AA.dat')
        table.set_from_file('A', 'B', filename='table_AB.dat')
        table.set_from_file('B', 'B', filename='table_BB.dat')

    Note:
        For potentials that diverge near r=0, make sure to set *rmin* to a reasonable value. If a potential does
        not diverge near r=0, then a setting of *rmin=0* is valid.

    """
    def __init__(self, width, nlist, name=None):

        # initialize the base class
        force._force.__init__(self, name);

        # setup the coefficient matrix
        self.pair_coeff = coeff();

        self.nlist = nlist
        self.nlist.subscribe(lambda:self.get_rcut())
        self.nlist.update_rcut()

        # create the c++ mirror class
        if not hoomd.context.current.device.cpp_exec_conf.isCUDAEnabled():
            self.cpp_force = _md.TablePotential(hoomd.context.current.system_definition, self.nlist.cpp_nlist, int(width), self.name);
        else:
            self.nlist.cpp_nlist.setStorageMode(_md.NeighborList.storageMode.full);
            self.cpp_force = _md.TablePotentialGPU(hoomd.context.current.system_definition, self.nlist.cpp_nlist, int(width), self.name);

        hoomd.context.current.system.addCompute(self.cpp_force, self.force_name);

        # stash the width for later use
        self.width = width;

    def update_pair_table(self, typei, typej, func, rmin, rmax, coeff):
        # allocate arrays to store V and F
        Vtable = _hoomd.std_vector_scalar();
        Ftable = _hoomd.std_vector_scalar();

        # calculate dr
        dr = (rmax - rmin) / float(self.width-1);

        # evaluate each point of the function
        for i in range(0, self.width):
            r = rmin + dr * i;
            (V,F) = func(r, rmin, rmax, **coeff);

            # fill out the tables
            Vtable.append(V);
            Ftable.append(F);

        # pass the tables on to the underlying cpp compute
        self.cpp_force.setTable(typei, typej, Vtable, Ftable, rmin, rmax);

    ## \internal
    # \brief Get the r_cut pair dictionary
    # \returns rcut(i,j) dict if logging is on, and None otherwise
    def get_rcut(self):
        if not self.log:
            return None

        # go through the list of only the active particle types in the sim
        ntypes = hoomd.context.current.system_definition.getParticleData().getNTypes();
        type_list = [];
        for i in range(0,ntypes):
            type_list.append(hoomd.context.current.system_definition.getParticleData().getNameByType(i));

        # update the rcut by pair type
        r_cut_dict = nl.rcut();
        for i in range(0,ntypes):
            for j in range(i,ntypes):
                # get the r_cut value
                rmax = self.pair_coeff.get(type_list[i], type_list[j], 'rmax');
                r_cut_dict.set_pair(type_list[i],type_list[j], rmax);

        return r_cut_dict;

    def get_max_rcut(self):
        # loop only over current particle types
        ntypes = hoomd.context.current.system_definition.getParticleData().getNTypes();
        type_list = [];
        for i in range(0,ntypes):
            type_list.append(hoomd.context.current.system_definition.getParticleData().getNameByType(i));

        # find the maximum rmax to update the neighbor list with
        maxrmax = 0.0;

        # loop through all of the unique type pairs and find the maximum rmax
        for i in range(0,ntypes):
            for j in range(i,ntypes):
                rmax = self.pair_coeff.get(type_list[i], type_list[j], "rmax");
                maxrmax = max(maxrmax, rmax);

        return maxrmax;

    def update_coeffs(self):
        # check that the pair coefficients are valid
        if not self.pair_coeff.verify(["func", "rmin", "rmax", "coeff"]):
            hoomd.context.current.device.cpp_msg.error("Not all pair coefficients are set for pair.table\n");
            raise RuntimeError("Error updating pair coefficients");

        # set all the params
        ntypes = hoomd.context.current.system_definition.getParticleData().getNTypes();
        type_list = [];
        for i in range(0,ntypes):
            type_list.append(hoomd.context.current.system_definition.getParticleData().getNameByType(i));

        # loop through all of the unique type pairs and evaluate the table
        for i in range(0,ntypes):
            for j in range(i,ntypes):
                func = self.pair_coeff.get(type_list[i], type_list[j], "func");
                rmin = self.pair_coeff.get(type_list[i], type_list[j], "rmin");
                rmax = self.pair_coeff.get(type_list[i], type_list[j], "rmax");
                coeff = self.pair_coeff.get(type_list[i], type_list[j], "coeff");

                self.update_pair_table(i, j, func, rmin, rmax, coeff);

    def set_from_file(self, a, b, filename):
        R""" Set a pair interaction from a file.

        Args:
            a (str): Name of type A in pair
            b (str): Name of type B in pair
            filename (str): Name of the file to read

        The provided file specifies V and F at equally spaced r values.

        Example::

            #r  V    F
            1.0 2.0 -3.0
            1.1 3.0 -4.0
            1.2 2.0 -3.0
            1.3 1.0 -2.0
            1.4 0.0 -1.0
            1.5 -1.0 0.0

        The first r value sets *rmin*, the last sets *rmax*. Any line with # as the first non-whitespace character is
        is treated as a comment. The *r* values must monotonically increase and be equally spaced. The table is read
        directly into the grid points used to evaluate :math:`F_{\mathrm{user}}(r)` and :math:`_{\mathrm{user}}(r)`.
        """

        # open the file
        f = open(filename);

        r_table = [];
        V_table = [];
        F_table = [];

        # read in lines from the file
        for line in f.readlines():
            line = line.strip();

            # skip comment lines
            if line[0] == '#':
                continue;

            # split out the columns
            cols = line.split();
            values = [float(f) for f in cols];

            # validate the input
            if len(values) != 3:
                hoomd.context.current.device.cpp_msg.error("pair.table: file must have exactly 3 columns\n");
                raise RuntimeError("Error reading table file");

            # append to the tables
            r_table.append(values[0]);
            V_table.append(values[1]);
            F_table.append(values[2]);

        # validate input
        if self.width != len(r_table):
            hoomd.context.current.device.cpp_msg.error("pair.table: file must have exactly " + str(self.width) + " rows\n");
            raise RuntimeError("Error reading table file");

        # extract rmin and rmax
        rmin_table = r_table[0];
        rmax_table = r_table[-1];

        # check for even spacing
        dr = (rmax_table - rmin_table) / float(self.width-1);
        for i in range(0,self.width):
            r = rmin_table + dr * i;
            if math.fabs(r - r_table[i]) > 1e-3:
                hoomd.context.current.device.cpp_msg.error("pair.table: r must be monotonically increasing and evenly spaced\n");
                raise RuntimeError("Error reading table file");

        self.pair_coeff.set(a, b, func=_table_eval, rmin=rmin_table, rmax=rmax_table, coeff=dict(V=V_table, F=F_table, width=self.width))

class Morse(_Pair):
    R""" Morse pair potential.

    :py:class:`Morse` specifies that a Morse pair potential should be applied between every
    non-excluded particle pair in the simulation.

    .. math::
        :nowrap:

        \begin{eqnarray*}
        V_{\mathrm{morse}}(r)  = & D_0 \left[ \exp \left(-2\alpha\left(r-r_0\right)\right) -2\exp \left(-\alpha\left(r-r_0\right)\right) \right] & r < r_{\mathrm{cut}} \\
                               = & 0 & r \ge r_{\mathrm{cut}} \\
        \end{eqnarray*}

    See :py:class:`_Pair` for details on how forces are calculated and the available energy shifting and smoothing modes.
    Use ``coeff.set`` to set potential coefficients.

    The following coefficients must be set per unique pair of particle types:

    - :math:`D_0` - *D0*, depth of the potential at its minimum (in energy units)
    - :math:`\alpha` - *alpha*, controls the width of the potential well (in units of 1/distance)
    - :math:`r_0` - *r0*, position of the minimum (in distance units)
    - :math:`r_{\mathrm{cut}}` - *r_cut* (in distance units)
      - *optional*: defaults to the global r_cut specified in the pair command
    - :math:`r_{\mathrm{on}}`- *r_on* (in distance units)
      - *optional*: defaults to the global r_cut specified in the pair command

    Example::

        nl = nlist.cell()
        morse = pair.morse(r_cut=3.0, nlist=nl)
        morse.pair_coeff.set('A', 'A', D0=1.0, alpha=3.0, r0=1.0)
        morse.pair_coeff.set('A', 'B', D0=1.0, alpha=3.0, r0=1.0, r_cut=3.0, r_on=2.0);
        morse.pair_coeff.set(['A', 'B'], ['C', 'D'], D0=1.0, alpha=3.0)

    """
    _cpp_class_name = "PotentialPairMorse"
    def __init__(self, nlist, r_cut=None, r_on=0., mode='none'):
        super().__init__(nlist, r_cut, r_on, mode)
        params = TypeParameter('params', 'particle_types',
                               TypeParameterDict(D0=float, alpha=float, r0=float,
                                             len_keys=2))
        self._add_typeparam(params)

class DPD(_Pair):
    R""" Dissipative Particle Dynamics.

    Args:
        r_cut (float): Default cutoff radius (in distance units).
        nlist (:py:mod:`hoomd.md.nlist`): Neighbor list
        kT (:py:mod:`hoomd.variant` or :py:obj:`float`): Temperature of thermostat (in energy units).
        seed (int): seed for the PRNG in the DPD thermostat.
        name (str): Name of the force instance.

    :py:class:`DPD` specifies that a DPD pair force should be applied between every
    non-excluded particle pair in the simulation, including an interaction potential,
    pairwise drag force, and pairwise random force. See `Groot and Warren 1997 <http://dx.doi.org/10.1063/1.474784>`_.

    .. math::
        :nowrap:

        \begin{eqnarray*}
        F =   F_{\mathrm{C}}(r) + F_{\mathrm{R,ij}}(r_{ij}) +  F_{\mathrm{D,ij}}(v_{ij}) \\
        \end{eqnarray*}

    .. math::
        :nowrap:

        \begin{eqnarray*}
        F_{\mathrm{C}}(r) = & A \cdot  w(r_{ij}) \\
        F_{\mathrm{R, ij}}(r_{ij}) = & - \theta_{ij}\sqrt{3} \sqrt{\frac{2k_b\gamma T}{\Delta t}}\cdot w(r_{ij})  \\
        F_{\mathrm{D, ij}}(r_{ij}) = & - \gamma w^2(r_{ij})\left( \hat r_{ij} \circ v_{ij} \right)  \\
        \end{eqnarray*}

    .. math::
        :nowrap:

        \begin{eqnarray*}
        w(r_{ij}) = &\left( 1 - r/r_{\mathrm{cut}} \right)  & r < r_{\mathrm{cut}} \\
                            = & 0 & r \ge r_{\mathrm{cut}} \\
        \end{eqnarray*}

    where :math:`\hat r_{ij}` is a normalized vector from particle i to particle j, :math:`v_{ij} = v_i - v_j`,
    and :math:`\theta_{ij}` is a uniformly distributed random number in the range [-1, 1].

    :py:class:`DPD` generates random numbers by hashing together the particle tags in the pair, the user seed,
    and the current time step index.

    .. attention::

        Change the seed if you reset the simulation time step to 0. If you keep the same seed, the simulation
        will continue with the same sequence of random numbers used previously and may cause unphysical correlations.

        For MPI runs: all ranks other than 0 ignore the seed input and use the value of rank 0.

    `C. L. Phillips et. al. 2011 <http://dx.doi.org/10.1016/j.jcp.2011.05.021>`_ describes the DPD implementation
    details in HOOMD-blue. Cite it if you utilize the DPD functionality in your work.

    :py:class:`DPD` does not implement and energy shift / smoothing modes due to the function of the force.
    Use ``coeff.set`` to set potential coefficients.

    The following coefficients must be set per unique pair of particle types:

    - :math:`A` - *A* (in force units)
    - :math:`\gamma` - *gamma* (in units of force/velocity)
    - :math:`r_{\mathrm{cut}}` - *r_cut* (in distance units)
      - *optional*: defaults to the global r_cut specified in the pair command

    To use the DPD thermostat, an :py:class:`hoomd.md.methods.NVE` integrator must be applied to the system and
    the user must specify a temperature.  Use of the dpd thermostat pair force with other integrators will result
    in unphysical behavior. To use pair.dpd with a different conservative potential than :math:`F_C`,
    set A to zero and define the conservative pair potential separately.  Note that DPD thermostats
    are often defined in terms of :math:`\sigma` where :math:`\sigma = \sqrt{2k_b\gamma T}`.

    Example::

        nl = nlist.cell()
        dpd = pair.dpd(r_cut=1.0, nlist=nl, kT=1.0, seed=0)
        dpd.pair_coeff.set('A', 'A', A=25.0, gamma = 4.5)
        dpd.pair_coeff.set('A', 'B', A=40.0, gamma = 4.5)
        dpd.pair_coeff.set('B', 'B', A=25.0, gamma = 4.5)
        dpd.pair_coeff.set(['A', 'B'], ['C', 'D'], A=12.0, gamma = 1.2)
        dpd.set_params(kT = 1.0)
        integrate.mode_standard(dt=0.02)
        integrate.nve(group=group.all())

    """
    _cpp_class_name = "PotentialPairDPDThermoDPD"
    def __init__(self, nlist, kT, seed=3, r_cut=None, r_on=0., mode='none'):
        """
        # register the citation
        c = hoomd.cite.article(cite_key='phillips2011',
                         author=['C L Phillips', 'J A Anderson', 'S C Glotzer'],
                         title='Pseudo-random number generation for Brownian Dynamics and Dissipative Particle Dynamics simulations on GPU devices',
                         journal='Journal of Computational Physics',
                         volume=230,
                         number=19,
                         pages='7191--7201',
                         month='Aug',
                         year='2011',
                         doi='10.1016/j.jcp.2011.05.021',
                         feature='DPD')
        hoomd.cite._ensure_global_bib().add(c)
        """
        super().__init__(nlist, r_cut, r_on, mode)
        params = TypeParameter('params', 'particle_types',
                               TypeParameterDict(A=float, gamma=float, len_keys=2))
        self._add_typeparam(params)

        d = ParameterDict(kT=hoomd.variant.Variant, seed=int)
        self._param_dict.update(d)

        self.kT = kT
        self.seed = seed

class DPDConservative(_Pair):
    R""" DPD Conservative pair force.

    Args:
        r_cut (float): Default cutoff radius (in distance units).
        nlist (:py:mod:`hoomd.md.nlist`): Neighbor list
        name (str): Name of the force instance.

    :py:class:`DPDConservative` specifies the conservative part of the DPD pair potential should be applied between
    every non-excluded particle pair in the simulation. No thermostat (e.g. Drag Force and Random Force) is applied,
    as is in :py:class:`DPD`.

    .. math::
        :nowrap:

        \begin{eqnarray*}
        V_{\mathrm{DPD-C}}(r)  = & A \cdot \left( r_{\mathrm{cut}} - r \right)
                               - \frac{1}{2} \cdot \frac{A}{r_{\mathrm{cut}}} \cdot \left(r_{\mathrm{cut}}^2 - r^2 \right)
                                      & r < r_{\mathrm{cut}} \\
                            = & 0 & r \ge r_{\mathrm{cut}} \\
        \end{eqnarray*}


    :py:class:`DPDConservative` does not implement and energy shift / smoothing modes due to the function of the force.
    Use ``coeff.set`` to set potential coefficients.

    The following coefficients must be set per unique pair of particle types:

    - :math:`A` - *A* (in force units)
    - :math:`r_{\mathrm{cut}}` - *r_cut* (in distance units)
      - *optional*: defaults to the global r_cut specified in the pair command

    Example::

        nl = nlist.cell()
        dpdc = pair.dpd_conservative(r_cut=3.0, nlist=nl)
        dpdc.pair_coeff.set('A', 'A', A=1.0)
        dpdc.pair_coeff.set('A', 'B', A=2.0, r_cut = 1.0)
        dpdc.pair_coeff.set('B', 'B', A=1.0)
        dpdc.pair_coeff.set(['A', 'B'], ['C', 'D'], A=5.0)

    """
    _cpp_class_name = "PotentialPairDPD"
    def __init__(self, nlist, r_cut=None, r_on=0., mode='none'):
        # add this back in once we redo the cite module
        """
        # register the citation
        c = hoomd.cite.article(cite_key='phillips2011',
                         author=['C L Phillips', 'J A Anderson', 'S C Glotzer'],
                         title='Pseudo-random number generation for Brownian Dynamics and Dissipative Particle Dynamics simulations on GPU devices',
                         journal='Journal of Computational Physics',
                         volume=230,
                         number=19,
                         pages='7191--7201',
                         month='Aug',
                         year='2011',
                         doi='10.1016/j.jcp.2011.05.021',
                         feature='DPD')
        hoomd.cite._ensure_global_bib().add(c)
        """
        # initialize the base class
        super().__init__(nlist, r_cut, r_on, mode)
        params =  TypeParameter('params', 'particle_types',
                                TypeParameterDict(A=float, len_keys=2))
        self._add_typeparam(params)


class DPDLJ(_Pair):
    R""" Dissipative Particle Dynamics with a LJ conservative force

    Args:
        r_cut (float): Default cutoff radius (in distance units).
        nlist (:py:mod:`hoomd.md.nlist`): Neighbor list
        kT (:py:mod:`hoomd.variant` or :py:obj:`float`): Temperature of thermostat (in energy units).
        seed (int): seed for the PRNG in the DPD thermostat.
        name (str): Name of the force instance.

    :py:class:`DPDLJ` specifies that a DPD thermostat and a Lennard-Jones pair potential should be applied between
    every non-excluded particle pair in the simulation.

    `C. L. Phillips et. al. 2011 <http://dx.doi.org/10.1016/j.jcp.2011.05.021>`_ describes the DPD implementation
    details in HOOMD-blue. Cite it if you utilize the DPD functionality in your work.

    .. math::
        :nowrap:

        \begin{eqnarray*}
        F =   F_{\mathrm{C}}(r) + F_{\mathrm{R,ij}}(r_{ij}) +  F_{\mathrm{D,ij}}(v_{ij}) \\
        \end{eqnarray*}

    .. math::
        :nowrap:

        \begin{eqnarray*}
        F_{\mathrm{C}}(r) = & \partial V_{\mathrm{LJ}} / \partial r \\
        F_{\mathrm{R, ij}}(r_{ij}) = & - \theta_{ij}\sqrt{3} \sqrt{\frac{2k_b\gamma T}{\Delta t}}\cdot w(r_{ij})  \\
        F_{\mathrm{D, ij}}(r_{ij}) = & - \gamma w^2(r_{ij})\left( \hat r_{ij} \circ v_{ij} \right)  \\
        \end{eqnarray*}

    .. math::
        :nowrap:

        \begin{eqnarray*}
        V_{\mathrm{LJ}}(r)  = & 4 \varepsilon \left[ \left( \frac{\sigma}{r} \right)^{12} -
                          \alpha \left( \frac{\sigma}{r} \right)^{6} \right] & r < r_{\mathrm{cut}} \\
                            = & 0 & r \ge r_{\mathrm{cut}} \\
        \end{eqnarray*}

    .. math::
        :nowrap:

        \begin{eqnarray*}
        w(r_{ij}) = &\left( 1 - r/r_{\mathrm{cut}} \right)  & r < r_{\mathrm{cut}} \\
                            = & 0 & r \ge r_{\mathrm{cut}} \\
        \end{eqnarray*}

    where :math:`\hat r_{ij}` is a normalized vector from particle i to particle j, :math:`v_{ij} = v_i - v_j`,
    and :math:`\theta_{ij}` is a uniformly distributed random number in the range [-1, 1].

    Use ``coeff.set`` to set potential coefficients.

    The following coefficients must be set per unique pair of particle types:

    - :math:`\varepsilon` - *epsilon* (in energy units)
    - :math:`\sigma` - *sigma* (in distance units)
    - :math:`\alpha` - *alpha* (unitless)
      - *optional*: defaults to 1.0
    - :math:`\gamma` - *gamma* (in units of force/velocity)
    - :math:`r_{\mathrm{cut}}` - *r_cut* (in distance units)
      - *optional*: defaults to the global r_cut specified in the pair command

    To use the DPD thermostat, an :py:class:`hoomd.md.methods.NVE` integrator must be applied to the system and
    the user must specify a temperature.  Use of the dpd thermostat pair force with other integrators will result
    in unphysical behavior.

    Example::

        nl = nlist.cell()
        dpdlj = pair.dpdlj(r_cut=2.5, nlist=nl, kT=1.0, seed=0)
        dpdlj.pair_coeff.set('A', 'A', epsilon=1.0, sigma = 1.0, gamma = 4.5)
        dpdlj.pair_coeff.set('A', 'B', epsilon=0.0, sigma = 1.0 gamma = 4.5)
        dpdlj.pair_coeff.set('B', 'B', epsilon=1.0, sigma = 1.0 gamma = 4.5, r_cut = 2.0**(1.0/6.0))
        dpdlj.pair_coeff.set(['A', 'B'], ['C', 'D'], epsilon = 3.0,sigma=1.0, gamma = 1.2)
        dpdlj.set_params(T = 1.0)
        integrate.mode_standard(dt=0.005)
        integrate.nve(group=group.all())

    """
    _cpp_class_name = "PotentialPairDPDLJThermoDPD"
    def __init__(self, nlist, kT, seed=3, r_cut=None, r_on=0., mode='none'):
        """
        # register the citation
        c = hoomd.cite.article(cite_key='phillips2011',
                         author=['C L Phillips', 'J A Anderson', 'S C Glotzer'],
                         title='Pseudo-random number generation for Brownian Dynamics and Dissipative Particle Dynamics simulations on GPU devices',
                         journal='Journal of Computational Physics',
                         volume=230,
                         number=19,
                         pages='7191--7201',
                         month='Aug',
                         year='2011',
                         doi='10.1016/j.jcp.2011.05.021',
                         feature='DPD')
        hoomd.cite._ensure_global_bib().add(c)
        """
        if mode == 'xplor':
            raise ValueError("xplor smoothing is not supported with pair.DPDLJ")

        super().__init__(nlist, r_cut, r_on, mode)
        params = TypeParameter('params', 'particle_types', TypeParameterDict(
            epsilon=float, sigma=float, alpha=1.0, gamma=float,
            len_keys=2))
        self._add_typeparam(params)

        d = ParameterDict(kT=hoomd.variant.Variant, seed=int,
                          mode=OnlyFrom(['none', 'shifted']))
        self._param_dict.update(d)

        self.kT = kT
        self.seed = seed
        self.mode = mode

class ForceShiftedLJ(_Pair):
    R""" Force-shifted Lennard-Jones pair potential.

    Args:
        r_cut (float): Default cutoff radius (in distance units).
        nlist (:py:mod:`hoomd.md.nlist`): Neighbor list
        name (str): Name of the force instance.

    :py:class:`ForceShiftedLJ` specifies that a modified Lennard-Jones pair force should be applied between
    non-excluded particle pair in the simulation. The force differs from the one calculated by  :py:class:`LJ`
    by the subtraction of the value of the force at :math:`r_{\mathrm{cut}}`, such that the force smoothly goes
    to zero at the cut-off. The potential is modified by a linear function. This potential can be used as a substitute
    for :py:class:`LJ`, when the exact analytical form of the latter is not required but a smaller cut-off radius is
    desired for computational efficiency. See `Toxvaerd et. al. 2011 <http://dx.doi.org/10.1063/1.3558787>`_
    for a discussion of this potential.

    .. math::
        :nowrap:

        \begin{eqnarray*}
        V(r)  = & 4 \varepsilon \left[ \left( \frac{\sigma}{r} \right)^{12} -
                          \alpha \left( \frac{\sigma}{r} \right)^{6} \right] + \Delta V(r) & r < r_{\mathrm{cut}}\\
                            = & 0 & r \ge r_{\mathrm{cut}} \\
        \end{eqnarray*}

    .. math::

        \Delta V(r) = -(r - r_{\mathrm{cut}}) \frac{\partial V_{\mathrm{LJ}}}{\partial r}(r_{\mathrm{cut}})

    See :py:class:`_Pair` for details on how forces are calculated and the available energy shifting and smoothing modes.
    Use ``coeff.set`` to set potential coefficients.

    The following coefficients must be set per unique pair of particle types:

    - :math:`\varepsilon` - *epsilon* (in energy units)
    - :math:`\sigma` - *sigma* (in distance units)
    - :math:`\alpha` - *alpha* (unitless) - *optional*: defaults to 1.0
    - :math:`r_{\mathrm{cut}}` - *r_cut* (in distance units)
      - *optional*: defaults to the global r_cut specified in the pair command
    - :math:`r_{\mathrm{on}}`- *r_on* (in distance units)
      - *optional*: defaults to the global r_cut specified in the pair command

    Example::

        nl = nlist.cell()
        fslj = pair.force_shifted_lj(r_cut=1.5, nlist=nl)
        fslj.pair_coeff.set('A', 'A', epsilon=1.0, sigma=1.0)

    """
    _cpp_class_name = "PotentialPairForceShiftedLJ"
    def __init__(self, nlist, r_cut=None, r_on=0., mode='none'):
        # initialize the base class
        super().__init__(nlist, r_cut, r_on, mode)

        params = TypeParameter('params', 'particle_types',
                               TypeParameterDict(sigma=float, epsilon=float,
                                                 len_keys=2))
        self._add_typeparam(params)

class Moliere(_Pair):
    R""" Moliere pair potential.

    Args:
        r_cut (float): Default cutoff radius (in distance units).
        nlist (:py:mod:`hoomd.md.nlist`): Neighbor list
        name (str): Name of the force instance.

    :py:class:`Moliere` specifies that a Moliere type pair potential should be applied between every
    non-excluded particle pair in the simulation.

    .. math::
        :nowrap:

        \begin{eqnarray*}
        V_{\mathrm{Moliere}}(r) = & \frac{Z_i Z_j e^2}{4 \pi \epsilon_0 r_{ij}} \left[ 0.35 \exp \left( -0.3 \frac{r_{ij}}{a_F} \right) + 0.55 \exp \left( -1.2 \frac{r_{ij}}{a_F} \right) + 0.10 \exp \left( -6.0 \frac{r_{ij}}{a_F} \right) \right] & r < r_{\mathrm{cut}} \\
                                = & 0 & r > r_{\mathrm{cut}} \\
        \end{eqnarray*}

    Where each parameter is defined as:

    - :math:`Z_i` - *Z_i* - Atomic number of species i (unitless)
    - :math:`Z_j` - *Z_j* - Atomic number of species j (unitless)
    - :math:`e` - *elementary_charge* - The elementary charge (in charge units)
    - :math:`a_F` - *aF* - :math:`a_F = \frac{0.8853 a_0}{\left( \sqrt{Z_i} + \sqrt{Z_j} \right)^{2/3}}`, where :math:`a_0` is the Bohr radius (in distance units)

    See :py:class:`_Pair` for details on how forces are calculated and the available energy shifting and smoothing modes.
    Use ``params`` property to set potential coefficients.

    The following coefficients must be set per unique pair of particle types:

    - :math:`q_i` - *qi* - :math:`q_i = Z_i \frac{e}{\sqrt{4 \pi \epsilon_0}}` (in charge units)
    - :math:`q_j` - *qj* - :math:`q_j = Z_j \frac{e}{\sqrt{4 \pi \epsilon_0}}` (in charge units)
    - :math:`a_F` - *aF* - :math:`a_F = \frac{0.8853 a_0}{\left( \sqrt{Z_i} + \sqrt{Z_j} \right)^{2/3}}`
    - :math:`r_{\mathrm{cut}}` - *r_cut* (in distance units)
      - *optional*: defaults to the global r_cut specified in the pair command
    - :math:`r_{\mathrm{on}}`- *r_on* (in distance units)
      - *optional*: defaults to the global r_cut specified in the pair command

    Example::

        nl = nlist.Cell()
        moliere = pair.Moliere(r_cut = 3.0, nlist=nl)

        Zi = 54
        Zj = 7
        e = 1
        a0 = 1
        aF = 0.8853 * a0 / (np.sqrt(Zi) + np.sqrt(Zj))**(2/3)

        moliere.params[('A', 'B')] = dict(qi=Zi*e, qj=Zj*e, aF=aF)

    """
    _cpp_class_name = "PotentialPairMoliere"
    def __init__(self, nlist, r_cut=None, r_on=0., mode='none'):
        super().__init__(nlist, r_cut, r_on, mode)
        params = TypeParameter('params', 'particle_types',
                               TypeParameterDict(qi=float, qj=float, aF=float,
                                                 len_keys=2))
        self._add_typeparam(params)

class ZBL(_Pair):
    R""" ZBL pair potential.

    Args:
        r_cut (float): Default cutoff radius (in distance units).
        nlist (:py:mod:`hoomd.md.nlist`): Neighbor list
        name (str): Name of the force instance.

    :py:class:`ZBL` specifies that a Ziegler-Biersack-Littmark pair potential should be applied between every
    non-excluded particle pair in the simulation.

    .. math::
        :nowrap:

        \begin{eqnarray*}
        V_{\mathrm{ZBL}}(r) = & \frac{Z_i Z_j e^2}{4 \pi \epsilon_0 r_{ij}} \left[ 0.1818 \exp \left( -3.2 \frac{r_{ij}}{a_F} \right) + 0.5099 \exp \left( -0.9423 \frac{r_{ij}}{a_F} \right) + 0.2802 \exp \left( -0.4029 \frac{r_{ij}}{a_F} \right) + 0.02817 \exp \left( -0.2016 \frac{r_{ij}}{a_F} \right) \right], & r < r_{\mathrm{cut}} \\
                                = & 0, & r > r_{\mathrm{cut}} \\
        \end{eqnarray*}

    Where each parameter is defined as:

    - :math:`Z_i` - *Z_i* - Atomic number of species i (unitless)
    - :math:`Z_j` - *Z_j* - Atomic number of species j (unitless)
    - :math:`e` - *elementary_charge* - The elementary charge (in charge units)
    - :math:`a_F` - *aF* - :math:`a_F = \frac{0.8853 a_0}{ Z_i^{0.23} + Z_j^{0.23} }`, where :math:`a_0` is the Bohr radius (in distance units)

    See :py:class:`_Pair` for details on how forces are calculated and the available energy shifting and smoothing modes.
    Use ``params`` property to set potential coefficients.

    The following coefficients must be set per unique pair of particle types:

    - :math:`q_i` - *qi* - :math:`q_i=Z_i \frac{e}{\sqrt{4 \pi \epsilon_0}}` (in charge units)
    - :math:`q_j` - *qj* - :math:`q_j=Z_j \frac{e}{\sqrt{4 \pi \epsilon_0}}` (in charge units)
    - :math:`a_F` - *aF* - :math:`a_F = \frac{0.8853 a_0}{ Z_i^{0.23} + Z_j^{0.23} }`
    - :math:`r_{\mathrm{cut}}` - *r_cut* (in distance units)
      - *optional*: defaults to the global r_cut specified in the pair command
    - :math:`r_{\mathrm{on}}`- *r_on* (in distance units)
      - *optional*: defaults to the global r_cut specified in the pair command

    Example::

        nl = nlist.Cell()
        zbl = pair.ZBL(r_cut = 3.0, nlist=nl)

        Zi = 54
        Zj = 7
        e = 1
        a0 = 1
        aF = 0.8853 * a0 / (Zi**(0.23) + Zj**(0.23))

        zbl.params[('A', 'B')] = dict(qi=Zi*e, qj=Zj*e, aF=aF)

    """
    _cpp_class_name = "PotentialPairZBL"
    def __init__(self, nlist, r_cut=None, r_on=0., mode='none'):

        super().__init__(nlist, r_cut, r_on, mode);
        params = TypeParameter('params', 'particle_types',
                               TypeParameterDict(qi=float, qj=float, aF=float,
                                                 len_keys=2))
        self._add_typeparam(params)

class tersoff(pair):
    R""" Tersoff Potential.

    Args:
        r_cut (float): Default cutoff radius (in distance units).
        nlist (:py:mod:`hoomd.md.nlist`): Neighbor list
        name (str): Name of the force instance.

    :py:class:`tersoff` specifies that the Tersoff three-body potential should be applied to every
    non-bonded particle pair in the simulation.  Despite the fact that the Tersoff potential accounts
    for the effects of third bodies, it is included in the pair potentials because the species of the
    third body is irrelevant. It can thus use type-pair parameters similar to those of the pair potentials.

    The Tersoff potential is a bond-order potential based on the Morse potential that accounts for the weakening of
    individual bonds with increasing coordination number. It does this by computing a modifier to the
    attractive term of the potential. The modifier contains the effects of third-bodies on the bond
    energies. The potential also includes a smoothing function around the cutoff. The smoothing function
    used in this work is exponential in nature as opposed to the sinusoid used by Tersoff. The exponential
    function provides continuity up (I believe) the second derivative.

    """
    def __init__(self, r_cut, nlist, name=None):

        # tell the base class how we operate

        # initialize the base class
        pair.__init__(self, r_cut, nlist, name);

        # this potential cannot handle a half neighbor list
        self.nlist.cpp_nlist.setStorageMode(_md.NeighborList.storageMode.full);

        # create the c++ mirror class
        if not hoomd.context.current.device.cpp_exec_conf.isCUDAEnabled():
            self.cpp_force = _md.PotentialTersoff(hoomd.context.current.system_definition, self.nlist.cpp_nlist, self.name);
            self.cpp_class = _md.PotentialTersoff;
        else:
            self.cpp_force = _md.PotentialTersoffGPU(hoomd.context.current.system_definition, self.nlist.cpp_nlist, self.name);
            self.cpp_class = _md.PotentialTersoffGPU;

        hoomd.context.current.system.addCompute(self.cpp_force, self.force_name);

        # setup the coefficients
        self.required_coeffs = ['cutoff_thickness', 'C1', 'C2', 'lambda1', 'lambda2', 'dimer_r', 'n', 'gamma', 'lambda3', 'c', 'd', 'm', 'alpha']
        self.pair_coeff.set_default_coeff('cutoff_thickness', 0.2);
        self.pair_coeff.set_default_coeff('dimer_r', 1.5);
        self.pair_coeff.set_default_coeff('C1', 1.0);
        self.pair_coeff.set_default_coeff('C2', 1.0);
        self.pair_coeff.set_default_coeff('lambda1', 2.0);
        self.pair_coeff.set_default_coeff('lambda2', 1.0);
        self.pair_coeff.set_default_coeff('lambda3', 0.0);
        self.pair_coeff.set_default_coeff('n', 0.0);
        self.pair_coeff.set_default_coeff('m', 0.0);
        self.pair_coeff.set_default_coeff('c', 0.0);
        self.pair_coeff.set_default_coeff('d', 1.0);
        self.pair_coeff.set_default_coeff('gamma', 0.0);
        self.pair_coeff.set_default_coeff('alpha', 3.0);

    def process_coeff(self, coeff):
        cutoff_d = coeff['cutoff_thickness'];
        C1 = coeff['C1'];
        C2 = coeff['C2'];
        lambda1 = coeff['lambda1'];
        lambda2 = coeff['lambda2'];
        dimer_r = coeff['dimer_r'];
        n = coeff['n'];
        gamma = coeff['gamma'];
        lambda3 = coeff['lambda3'];
        c = coeff['c'];
        d = coeff['d'];
        m = coeff['m'];
        alpha = coeff['alpha'];

        gamman = math.pow(gamma, n);
        c2 = c * c;
        d2 = d * d;
        lambda3_cube = lambda3 * lambda3 * lambda3;

        tersoff_coeffs = _hoomd.make_scalar2(C1, C2);
        exp_consts = _hoomd.make_scalar2(lambda1, lambda2);
        ang_consts = _hoomd.make_scalar3(c2, d2, m);

        return _md.make_tersoff_params(cutoff_d, tersoff_coeffs, exp_consts, dimer_r, n, gamman, lambda3_cube, ang_consts, alpha);


class revcross(pair):
    R""" Reversible crosslinker three-body potential to model bond swaps.

    Args:
        r_cut (float): Default cutoff radius (in distance units).
        nlist (:py:mod:`hoomd.md.nlist`): Neighbor list
        name (str): Name of the force instance.

    :py:class:`revcross` specifies that the revcross three-body potential should be applied to every
    non-bonded particle pair in the simulation.  Despite the fact that the revcross potential accounts
    for the effects of third bodies, it is included in the pair potentials because its is actually just a
    combination of two body potential terms. It can thus use type-pair parameters similar to those of the pair potentials.

    The revcross potential has been described in detail in `S. Ciarella and W.G. Ellenbroek 2019 <https://arxiv.org/abs/1912.08569>`_. It is based on a generalized-Lennard-Jones pairwise
    attraction to form bonds between interacting particless:

    .. math::
        :nowrap:

        \begin{eqnarray*}
        V_{ij}(r)  =  4 \varepsilon \left[ \left( \dfrac{ \sigma}{r_{ij}} \right) ^{2n}- \left( \dfrac{ \sigma}{r_{ij}} \right)^{n} \right] \qquad r<r_{cut}
        \end{eqnarray*}

    with the following coefficients:

    - :math:`\varepsilon` - *epsilon* (in energy units)
    - :math:`\sigma` - *sigma* (in distance units)
    - :math:`n` - *n* (unitless)
    - :math:`m` - *m* (unitless)
    - :math:`r_{\mathrm{cut}}` - *r_cut* (in distance units)
      - *optional*: defaults to the global r_cut specified in the pair command

    Then an additional three-body repulsion is evaluated to compensate the bond energies imposing single bond per particle condition:

    .. math::
        :nowrap:

            \begin{eqnarray*}
            v^{\left( 3b \right)}_{ijk}=\lambda \epsilon\,\hat{v}^{ \left( 2b \right)}_{ij}\left(\vec{r}_{ij}\right) \cdot \hat{v}^{ \left( 2b \right)}_{ik}\left(\vec{r}_{ik}\right)~,\\
            \end{eqnarray*}

    where the two body potential is rewritten as:

    .. math::
        :nowrap:

            \begin{eqnarray*}
            \hat{v}^{ \left( 2b \right)}_{ij}\left(\vec{r}_{ij}\right) =
            \begin{cases}
            & 1 \qquad \qquad \; \; \qquad r\le r_{min}\\
            & - \dfrac{v_{ij}\left(\vec{r}_{ij}\right)}{\epsilon} \qquad r > r_{min}~.\\
            \end{cases}
            \end{eqnarray*}

    .. attention::

        The revcross potential models an asymmetric interaction between two different chemical moieties that can form a reversible bond.
	This requires the definition of (at least) two different types of particles.
	A reversible bond is only possible between two different species, otherwise :math:`v^{\left( 3b \right)}_{ijk}`, would prevent any bond.
	In our example we then set the interactions for types A and B with ``potRevC.pair_coeff.set(['A','B'],['A','B'],sigma=0.0,n=0,epsilon=0,lambda3=0)``
        and the only non-zero energy only between the different types ``potRevC.pair_coeff.set('A','B',sigma=1,n=100,epsilon=100,lambda3=1)``.
	Notice that the number of the minoritary species corresponds to the maximum number of bonds.


    This three-body term also tunes the energy required for a bond swap through the coefficient:
    - :math:`\lambda` - *lambda3* (unitless)
    in `S. Ciarella and W.G. Ellenbroek 2019 <https://arxiv.org/abs/1912.08569>`_ is explained that setting :math:`\lambda=1` corresponds to no energy requirement to initiate bond swap, while this
    energy barrier scales roughly as :math:`\beta \Delta E_\text{sw} =\beta \varepsilon(\lambda-1)`.

    Note:

        Choosing :math:`\lambda=1` pushes the system towards clusterization because the three-body term is not enough to
        compensate the energy of multiple bonds, so it may cause unphysical situations.


    Example::

        nl = md.nlist.cell()
        potBondSwap = md.pair.revcross(r_cut=1.3,nlist=nl)
        potBondSwap.pair_coeff.set(['A','B'],['A','B'],sigma=0,n=0,epsilon=0,lambda3=0)
	# a bond can be made only between A-B and not A-A or B-B
        potBondSwap.pair_coeff.set('A','B',sigma=1,n=100,epsilon=10,lambda3=1)
    """
    def __init__(self, r_cut, nlist, name=None):

        # tell the base class how we operate

        # initialize the base class
        pair.__init__(self, r_cut, nlist, name);

        # this potential cannot handle a half neighbor list
        self.nlist.cpp_nlist.setStorageMode(_md.NeighborList.storageMode.full);

        # create the c++ mirror class
        if not hoomd.context.current.device.cpp_exec_conf.isCUDAEnabled():
            self.cpp_force = _md.PotentialRevCross(hoomd.context.current.system_definition, self.nlist.cpp_nlist, self.name);
            self.cpp_class = _md.PotentialRevCross;
        else:
            self.cpp_force = _md.PotentialRevCrossGPU(hoomd.context.current.system_definition, self.nlist.cpp_nlist, self.name);
            self.cpp_class = _md.PotentialRevCrossGPU;

        hoomd.context.current.system.addCompute(self.cpp_force, self.force_name);

        # setup the coefficients
        self.required_coeffs = ['sigma', 'n', 'epsilon', 'lambda3']
        self.pair_coeff.set_default_coeff('sigma', 2.);
        self.pair_coeff.set_default_coeff('n', 1.0);
        self.pair_coeff.set_default_coeff('epsilon', 1.0);
        self.pair_coeff.set_default_coeff('lambda3', 1.0);

    def process_coeff(self, coeff):
        sigma = coeff['sigma'];
        n = coeff['n'];
        epsilon = coeff['epsilon'];
        lambda3 = coeff['lambda3'];

        return _md.make_revcross_params(sigma, n, epsilon, lambda3);



class Mie(_Pair):
    R""" Mie pair potential.

    Args:
        r_cut (float): Default cutoff radius (in distance units).
        nlist (:py:mod:`hoomd.md.nlist`): Neighbor list
        name (str): Name of the force instance.

    :py:class:`Mie` specifies that a Mie pair potential should be applied between every
    non-excluded particle pair in the simulation.

    .. math::
        :nowrap:

        \begin{eqnarray*}
        V_{\mathrm{mie}}(r)  = & \left( \frac{n}{n-m} \right) {\left( \frac{n}{m} \right)}^{\frac{m}{n-m}} \varepsilon \left[ \left( \frac{\sigma}{r} \right)^{n} -
                          \left( \frac{\sigma}{r} \right)^{m} \right] & r < r_{\mathrm{cut}} \\
                            = & 0 & r \ge r_{\mathrm{cut}} \\
        \end{eqnarray*}

    See :py:class:`_Pair` for details on how forces are calculated and the available energy shifting and smoothing modes.
    Use ``coeff.set`` to set potential coefficients.

    The following coefficients must be set per unique pair of particle types:

    - :math:`\varepsilon` - *epsilon* (in energy units)
    - :math:`\sigma` - *sigma* (in distance units)
    - :math:`n` - *n* (unitless)
    - :math:`m` - *m* (unitless)
    - :math:`r_{\mathrm{cut}}` - *r_cut* (in distance units)
      - *optional*: defaults to the global r_cut specified in the pair command
    - :math:`r_{\mathrm{on}}`- *r_on* (in distance units)
      - *optional*: defaults to the global r_cut specified in the pair command

    Example::

        nl = nlist.cell()
        mie = pair.mie(r_cut=3.0, nlist=nl)
        mie.pair_coeff.set('A', 'A', epsilon=1.0, sigma=1.0, n=12, m=6)
        mie.pair_coeff.set('A', 'B', epsilon=2.0, sigma=1.0, n=14, m=7, r_cut=3.0, r_on=2.0);
        mie.pair_coeff.set('B', 'B', epsilon=1.0, sigma=1.0, n=15.1, m=6.5, r_cut=2**(1.0/6.0), r_on=2.0);
        mie.pair_coeff.set(['A', 'B'], ['C', 'D'], epsilon=1.5, sigma=2.0)

    """
    _cpp_class_name = "PotentialPairMie"
    def __init__(self, nlist, r_cut=None, r_on=0., mode='none'):

        super().__init__(nlist, r_cut, r_on, mode)
        params = TypeParameter('params', 'particle_types',
                               TypeParameterDict(epsilon=float, sigma=float,
                                                 n=float, m=float, len_keys=2))

        self._add_typeparam(params)


class _shape_dict(dict):
    """Simple dictionary subclass to improve handling of anisotropic potential
    shape information."""
    def __getitem__(self, key):
        try:
            return super(_shape_dict, self).__getitem__(key)
        except KeyError as e:
            raise KeyError("No shape parameters specified for particle type {}!".format(key)) from e


class ai_pair(pair):
    R"""Generic anisotropic pair potential.

    Users should not instantiate :py:class:`ai_pair` directly. It is a base class that
    provides common features to all anisotropic pair forces. Rather than repeating all of that documentation in a
    dozen different places, it is collected here.

    All anisotropic pair potential commands specify that a given potential energy, force and torque be computed
    on all non-excluded particle pairs in the system within a short range cutoff distance :math:`r_{\mathrm{cut}}`.
    The interaction energy, forces and torque depend on the inter-particle separation
    :math:`\vec r` and on the orientations :math:`\vec q_i`, :math:`q_j`, of the particles.
    """

    ## \internal
    # \brief Initialize the pair force
    # \details
    # The derived class must set
    #  - self.cpp_class (the pair class to instantiate)
    #  - self.required_coeffs (a list of the coeff names the derived class needs)
    #  - self.process_coeffs() (a method that takes in the coeffs and spits out a param struct to use in
    #       self.cpp_force.set_params())
    def __init__(self, r_cut, nlist, name=None):
        # initialize the base class
        force._force.__init__(self, name);

        self.global_r_cut = r_cut;

        # setup the coefficient matrix
        self.pair_coeff = coeff();
        self.pair_coeff.set_default_coeff('r_cut', self.global_r_cut);

        # setup the neighbor list
        self.nlist = nlist
        self.nlist.subscribe(lambda:self.get_rcut())
        self.nlist.update_rcut()

        self._shape = _shape_dict()

    def set_params(self, mode=None):
        R"""Set parameters controlling the way forces are computed.

        Args:
            mode (str): (if set) Set the mode with which potentials are handled at the cutoff

        valid values for mode are: "none" (the default) and "shift":

        - *none* - No shifting is performed and potentials are abruptly cut off
        - *shift* - A constant shift is applied to the entire potential so that it is 0 at the cutoff

        Examples::

            mypair.set_params(mode="shift")
            mypair.set_params(mode="no_shift")

        """

        if mode is not None:
            if mode == "no_shift":
                self.cpp_force.setShiftMode(self.cpp_class.energyShiftMode.no_shift)
            elif mode == "shift":
                self.cpp_force.setShiftMode(self.cpp_class.energyShiftMode.shift)
            else:
                hoomd.context.current.device.cpp_msg.error("Invalid mode\n");
                raise RuntimeError("Error changing parameters in pair force");

    @property
    def shape(self):
        R"""Get or set shape parameters per type.

        In addition to any pair-specific parameters required to characterize a
        pair potential, individual particles that have anisotropic interactions
        may also have their own shapes that affect the potentials. General
        anisotropic pair potentials may set per-particle shapes using this
        method.
        """
        return self._shape

    def update_coeffs(self):
        coeff_list = self.required_coeffs + ["r_cut"];
        # check that the pair coefficients are valid
        if not self.pair_coeff.verify(coeff_list):
            hoomd.context.current.device.cpp_msg.error("Not all pair coefficients are set\n");
            raise RuntimeError("Error updating pair coefficients");

        # set all the params
        ntypes = hoomd.context.current.system_definition.getParticleData().getNTypes();
        type_list = [];
        for i in range(0,ntypes):
            type_list.append(hoomd.context.current.system_definition.getParticleData().getNameByType(i));

        for i in range(0,ntypes):
            self._set_cpp_shape(i, type_list[i])

            for j in range(i,ntypes):
                # build a dict of the coeffs to pass to process_coeff
                coeff_dict = {}
                for name in coeff_list:
                    coeff_dict[name] = self.pair_coeff.get(type_list[i], type_list[j], name);

                param = self.process_coeff(coeff_dict);
                self.cpp_force.setParams(i, j, param);
                self.cpp_force.setRcut(i, j, coeff_dict['r_cut']);

    def _set_cpp_shape(self, type_id, type_name):
        """Update shape information in C++.

        This method must be implemented by subclasses to generate the
        appropriate shape structure. The default behavior is to do nothing."""
        pass

class gb(ai_pair):
    R""" Gay-Berne anisotropic pair potential.

    Args:
        r_cut (float): Default cutoff radius (in distance units).
        nlist (:py:mod:`hoomd.md.nlist`): Neighbor list
        name (str): Name of the force instance.

    :py:class:`gb` computes the Gay-Berne potential between anisotropic particles.

    This version of the Gay-Berne potential supports identical pairs of uniaxial ellipsoids,
    with orientation-independent energy-well depth.

    The interaction energy for this anisotropic pair potential is
    (`Allen et. al. 2006 <http://dx.doi.org/10.1080/00268970601075238>`_):

    .. math::
        :nowrap:

        \begin{eqnarray*}
        V_{\mathrm{GB}}(\vec r, \vec e_i, \vec e_j)  = & 4 \varepsilon \left[ \zeta^{-12} -
                              \zeta^{-6} \right] & \zeta < \zeta_{\mathrm{cut}} \\
                            = & 0 & \zeta \ge \zeta_{\mathrm{cut}} \\
        \end{eqnarray*}

    .. math::

        \zeta = \left(\frac{r-\sigma+\sigma_{\mathrm{min}}}{\sigma_{\mathrm{min}}}\right)

        \sigma^{-2} = \frac{1}{2} \hat{\vec{r}}\cdot\vec{H^{-1}}\cdot\hat{\vec{r}}

        \vec{H} = 2 \ell_\perp^2 \vec{1} + (\ell_\parallel^2 - \ell_\perp^2) (\vec{e_i} \otimes \vec{e_i} + \vec{e_j} \otimes \vec{e_j})

    with :math:`\sigma_{\mathrm{min}} = 2 \min(\ell_\perp, \ell_\parallel)`.

    The cut-off parameter :math:`r_{\mathrm{cut}}` is defined for two particles oriented parallel along
    the **long** axis, i.e.
    :math:`\zeta_{\mathrm{cut}} = \left(\frac{r-\sigma_{\mathrm{max}} + \sigma_{\mathrm{min}}}{\sigma_{\mathrm{min}}}\right)`
    where :math:`\sigma_{\mathrm{max}} = 2 \max(\ell_\perp, \ell_\parallel)` .

    The quantities :math:`\ell_\parallel` and :math:`\ell_\perp` denote the semi-axis lengths parallel
    and perpendicular to particle orientation.

    Use ``coeff.set`` to set potential coefficients.

    The following coefficients must be set per unique pair of particle types:

    - :math:`\varepsilon` - *epsilon* (in energy units)
    - :math:`\ell_\perp` - *lperp* (in distance units)
    - :math:`\ell_\parallel` - *lpar* (in distance units)
    - :math:`r_{\mathrm{cut}}` - *r_cut* (in distance units)
      - *optional*: defaults to the global r_cut specified in the pair command

    Example::

        nl = nlist.cell()
        gb = pair.gb(r_cut=2.5, nlist=nl)
        gb.pair_coeff.set('A', 'A', epsilon=1.0, lperp=0.45, lpar=0.5)
        gb.pair_coeff.set('A', 'B', epsilon=2.0, lperp=0.45, lpar=0.5, r_cut=2**(1.0/6.0));

    """
    def __init__(self, r_cut, nlist, name=None):

        # tell the base class how we operate

        # initialize the base class
        ai_pair.__init__(self, r_cut, nlist, name);

        # create the c++ mirror class
        if not hoomd.context.current.device.cpp_exec_conf.isCUDAEnabled():
            self.cpp_force = _md.AnisoPotentialPairGB(hoomd.context.current.system_definition, self.nlist.cpp_nlist, self.name);
            self.cpp_class = _md.AnisoPotentialPairGB;
        else:
            self.nlist.cpp_nlist.setStorageMode(_md.NeighborList.storageMode.full);
            self.cpp_force = _md.AnisoPotentialPairGBGPU(hoomd.context.current.system_definition, self.nlist.cpp_nlist, self.name);
            self.cpp_class = _md.AnisoPotentialPairGBGPU;

        hoomd.context.current.system.addCompute(self.cpp_force, self.force_name);

        # setup the coefficient options
        self.required_coeffs = ['epsilon', 'lperp', 'lpar'];

    def process_coeff(self, coeff):
        epsilon = coeff['epsilon'];
        lperp = coeff['lperp'];
        lpar = coeff['lpar'];

        return _md.make_pair_gb_params(epsilon, lperp, lpar);

    def get_type_shapes(self):
        """Get all the types of shapes in the current simulation.

        Example:

            >>> my_gb.get_type_shapes()
            [{'type': 'Ellipsoid', 'a': 1.0, 'b': 1.0, 'c': 1.5}]

        Returns:
            A list of dictionaries, one for each particle type in the system.
        """
        return super(ai_pair, self)._return_type_shapes();

class dipole(ai_pair):
    R""" Screened dipole-dipole interactions.

    Args:
        r_cut (float): Default cutoff radius (in distance units).
        nlist (:py:mod:`hoomd.md.nlist`): Neighbor list
        name (str): Name of the force instance.

    :py:class:`dipole` computes the (screened) interaction between pairs of
    particles with dipoles and electrostatic charges. The total energy
    computed is:

    .. math::

        U_{dipole} = U_{dd} + U_{de} + U_{ee}

        U_{dd} = A e^{-\kappa r} \left(\frac{\vec{\mu_i}\cdot\vec{\mu_j}}{r^3} - 3\frac{(\vec{\mu_i}\cdot \vec{r_{ji}})(\vec{\mu_j}\cdot \vec{r_{ji}})}{r^5}\right)

        U_{de} = A e^{-\kappa r} \left(\frac{(\vec{\mu_j}\cdot \vec{r_{ji}})q_i}{r^3} - \frac{(\vec{\mu_i}\cdot \vec{r_{ji}})q_j}{r^3}\right)

        U_{ee} = A e^{-\kappa r} \frac{q_i q_j}{r}

    Use ``coeff.set`` to set potential coefficients.
    :py:class:`dipole` does not implement and energy shift / smoothing modes due to the function of the force.

    The following coefficients must be set per unique pair of particle types:

    - mu - magnitude of :math:`\vec{\mu} = \mu (1, 0, 0)` in the particle local reference frame
    - A - electrostatic energy scale :math:`A` (default value 1.0)
    - kappa - inverse screening length :math:`\kappa`

    Example::

        # A/A interact only with screened electrostatics
        dipole.pair_coeff.set('A', 'A', mu=0.0, A=1.0, kappa=1.0)
        dipole.pair_coeff.set('A', 'B', mu=0.5, kappa=1.0)

    """
    def __init__(self, r_cut, nlist, name=None):

        ## tell the base class how we operate

        # initialize the base class
        ai_pair.__init__(self, r_cut, nlist, name);

        ## create the c++ mirror class
        if not hoomd.context.current.device.cpp_exec_conf.isCUDAEnabled():
            self.cpp_force = _md.AnisoPotentialPairDipole(hoomd.context.current.system_definition, self.nlist.cpp_nlist, self.name);
            self.cpp_class = _md.AnisoPotentialPairDipole;
        else:
            self.nlist.cpp_nlist.setStorageMode(_md.NeighborList.storageMode.full);
            self.cpp_force = _md.AnisoPotentialPairDipoleGPU(hoomd.context.current.system_definition, self.nlist.cpp_nlist, self.name);
            self.cpp_class = _md.AnisoPotentialPairDipoleGPU;

        hoomd.context.current.system.addCompute(self.cpp_force, self.force_name);

        ## setup the coefficient options
        self.required_coeffs = ['mu', 'A', 'kappa'];

        self.pair_coeff.set_default_coeff('A', 1.0)

    def process_coeff(self, coeff):
        mu = float(coeff['mu']);
        A = float(coeff['A']);
        kappa = float(coeff['kappa']);

        return _md.make_pair_dipole_params(mu, A, kappa);

    def set_params(self, *args, **kwargs):
        """ :py:class:`dipole` has no energy shift modes """

        raise RuntimeError('Not implemented for dipole');
        return;


class ReactionField(_Pair):
    R""" Onsager reaction field pair potential.

    Args:
        r_cut (float): Default cutoff radius (in distance units).
        nlist (:py:mod:`hoomd.md.nlist`): Neighbor list
        name (str): Name of the force instance.

    :py:class:`ReactionField` specifies that an Onsager reaction field pair potential should be applied between every
    non-excluded particle pair in the simulation.

    Reaction field electrostatics is an approximation to the screened electrostatic interaction,
    which assumes that the medium can be treated as an electrostatic continuum of dielectric
    constant :math:`\epsilon_{RF}` outside the cutoff sphere of radius :math:`r_{\mathrm{cut}}`.
    See: `Barker et. al. 1973 <http://dx.doi.org/10.1080/00268977300102101>`_.

    .. math::

       V_{\mathrm{RF}}(r) = \varepsilon \left[ \frac{1}{r} +
           \frac{(\epsilon_{RF}-1) r^2}{(2 \epsilon_{RF} + 1) r_c^3} \right]

    By default, the reaction field potential does not require charge or diameter to be set. Two parameters,
    :math:`\varepsilon` and :math:`\epsilon_{RF}` are needed. If :math:`epsilon_{RF}` is specified as zero,
    it will represent infinity.

    If *use_charge* is set to True, the following formula is evaluated instead:
    .. math::

       V_{\mathrm{RF}}(r) = q_i q_j \varepsilon \left[ \frac{1}{r} +
           \frac{(\epsilon_{RF}-1) r^2}{(2 \epsilon_{RF} + 1) r_c^3} \right]

    where :math:`q_i` and :math:`q_j` are the charges of the particle pair.

    See :py:class:`_Pair` for details on how forces are calculated and the available energy shifting and smoothing modes.
    Use ``coeff.set`` to set potential coefficients.

    The following coefficients must be set per unique pair of particle types:

    - :math:`\varepsilon` - *epsilon* (in units of energy*distance)
    - :math:`\epsilon_{RF}` - *eps_rf* (dimensionless)
    - :math:`r_{\mathrm{cut}}` - *r_cut* (in units of distance)
      - *optional*: defaults to the global r_cut specified in the pair command
    - :math:`r_{\mathrm{on}}` - *r_on* (in units of distance)
      - *optional*: defaults to the global r_cut specified in the pair command
    - *use_charge* (boolean), evaluate potential using particle charges
      - *optional*: defaults to False

    .. versionadded:: 2.1


    Example::

        nl = nlist.cell()
        reaction_field = pair.reaction_field(r_cut=3.0, nlist=nl)
        reaction_field.pair_coeff.set('A', 'A', epsilon=1.0, eps_rf=1.0)
        reaction_field.pair_coeff.set('A', 'B', epsilon=-1.0, eps_rf=0.0)
        reaction_field.pair_coeff.set('B', 'B', epsilon=1.0, eps_rf=0.0)
        reaction_field.pair_coeff.set(system.particles.types, system.particles.types, epsilon=1.0, eps_rf=0.0, use_charge=True)

    """
    _cpp_class_name = "PotentialPairReactionField"
    def __init__(self, nlist, r_cut=None, r_on=0., mode='none'):
        super().__init__(nlist, r_cut, r_on, mode)
        params = TypeParameter('params', 'particle_types',
                               TypeParameterDict(epsilon=float, eps_rf=float,
                                                 use_charge=False, len_keys=2))

        self._add_typeparam(params)


class DLVO(_Pair):
    R""" DLVO colloidal interaction

    :py:class:`DLVO` specifies that a DLVO dispersion and electrostatic interaction should be
    applied between every non-excluded particle pair in the simulation.

    Args:
        r_cut (float): Default cutoff radius (in distance units).
        nlist (:py:mod:`hoomd.md.nlist`): Neighbor list
        name (str): Name of the force instance.
        d_max (float): Maximum diameter particles in the simulation will have (in distance units)

    :py:class:`DLVO` evaluates the forces for the pair potential
    .. math::

        V_{\mathrm{DLVO}}(r)  = & - \frac{A}{6} \left[
            \frac{2a_1a_2}{r^2 - (a_1+a_2)^2} + \frac{2a_1a_2}{r^2 - (a_1-a_2)^2}
            + \log \left( \frac{r^2 - (a_1+a_2)^2}{r^2 - (a_1-a_2)^2} \right) \right]
            + \frac{a_1 a_2}{a_1+a_2} Z e^{-\kappa(r - (a_1+a_2))} & r < (r_{\mathrm{cut}} + \Delta)
            = & 0 & r \ge (r_{\mathrm{cut}} + \Delta)

    where :math:`a_i` is the radius of particle :math:`i`, :math:`\Delta = (d_i + d_j)/2` and
    :math:`d_i` is the diameter of particle :math:`i`.

    The first term corresponds to the attractive van der Waals interaction with A being the Hamaker constant,
    the second term to the repulsive double-layer interaction between two spherical surfaces with Z proportional
    to the surface electric potential.

    See Israelachvili 2011, pp. 317.

    The DLVO potential does not need charge, but does need diameter. See :py:class:`SLJ` for an explanation
    on how diameters are handled in the neighbor lists.

    Due to the way that DLVO modifies the cutoff condition, it will not function properly with the
    xplor shifting mode. See :py:class:`_Pair` for details on how forces are calculated and the available energy
    shifting and smoothing modes.

    Use ``coeff.set`` to set potential coefficients.

    The following coefficients must be set per unique pair of particle types:

    - :math:`\varepsilon` - *epsilon* (in units of energy*distance)
    - :math:`\kappa` - *kappa* (in units of 1/distance)
    - :math:`r_{\mathrm{cut}}` - *r_cut* (in units of distance)
      - *optional*: defaults to the global r_cut specified in the pair command
    - :math:`r_{\mathrm{on}}` - *r_on* (in units of distance)
      - *optional*: defaults to the global r_cut specified in the pair command

    .. versionadded:: 2.2

    Example::

        nl = nlist.cell()
        DLVO.pair_coeff.set('A', 'A', epsilon=1.0, kappa=1.0)
        DLVO.pair_coeff.set('A', 'B', epsilon=2.0, kappa=0.5, r_cut=3.0, r_on=2.0);
        DLVO.pair_coeff.set(['A', 'B'], ['C', 'D'], epsilon=0.5, kappa=3.0)
    """
    _cpp_class_name = "PotentialPairDLVO"
    def __init__(self, nlist, r_cut=None, r_on=0., mode='none'):
        if mode=='xplor':
            raise ValueError("xplor is not a valid mode for the DLVO potential")

        super().__init__(nlist, r_cut, r_on, mode)
        params = TypeParameter('params', 'particle_types',
                               TypeParameterDict(kappa=float, Z=float, A=float,
                                                 len_keys=2)
                               )
        self._add_typeparam(params)

        # mode not allowed to be xplor, so re-do param dict entry without that option
        param_dict = ParameterDict(mode=OnlyFrom(['none','shifted']))
        self._param_dict.update(param_dict)
        self.mode = mode

        # this potential needs diameter shifting on
        self._nlist.diameter_shift = True


class square_density(pair):
    R""" Soft potential for simulating a van-der-Waals liquid

    Args:
        r_cut (float): Default cutoff radius (in distance units).
        nlist (:py:mod:`hoomd.md.nlist`): Neighbor list
        name (str): Name of the force instance.

    :py:class:`square_density` specifies that the three-body potential should be applied to every
    non-bonded particle pair in the simulation, that is harmonic in the local density.

    The self energy per particle takes the form

    .. math:: \Psi^{ex} = B (\rho - A)^2

    which gives a pair-wise additive, three-body force

    .. math:: \vec{f}_{ij} = \left( B (n_i - A) + B (n_j - A) \right) w'_{ij} \vec{e}_{ij}

    Here, :math:`w_{ij}` is a quadratic, normalized weighting function,

    .. math:: w(x) = \frac{15}{2 \pi r_{c,\mathrm{weight}}^3} (1-r/r_{c,\mathrm{weight}})^2

    The local density at the location of particle *i* is defined as

    .. math:: n_i = \sum\limits_{j\neq i} w_{ij}\left(\big| \vec r_i - \vec r_j \big|\right)

    The following coefficients must be set per unique pair of particle types:

    - :math:`A` - *A* (in units of volume^-1) - mean density (*default*: 0)
    - :math:`B` - *B* (in units of energy*volume^2) - coefficient of the harmonic density term

    Example::

        nl = nlist.cell()
        sqd = pair.van_der_waals(r_cut=3.0, nlist=nl)
        sqd.pair_coeff.set('A', 'A', A=0.1)
        sqd.pair_coeff.set('A', 'A', B=1.0)

    For further details regarding this multibody potential, see

    Warning:
        Currently HOOMD does not support reverse force communication between MPI domains on the GPU.
        Since reverse force communication is required for the calculation of multi-body potentials, attempting to use the
        square_density potential on the GPU with MPI will result in an error.

    [1] P. B. Warren, "Vapor-liquid coexistence in many-body dissipative particle dynamics"
    Phys. Rev. E. Stat. Nonlin. Soft Matter Phys., vol. 68, no. 6 Pt 2, p. 066702, 2003.
    """
    def __init__(self, r_cut, nlist, name=None):

        # tell the base class how we operate

        # initialize the base class
        pair.__init__(self, r_cut, nlist, name);

        # this potential cannot handle a half neighbor list
        self.nlist.cpp_nlist.setStorageMode(_md.NeighborList.storageMode.full);

        # create the c++ mirror class
        if not hoomd.context.current.device.cpp_exec_conf.isCUDAEnabled():
            self.cpp_force = _md.PotentialSquareDensity(hoomd.context.current.system_definition, self.nlist.cpp_nlist, self.name);
            self.cpp_class = _md.PotentialSquareDensity;
        else:
            self.cpp_force = _md.PotentialSquareDensityGPU(hoomd.context.current.system_definition, self.nlist.cpp_nlist, self.name);
            self.cpp_class = _md.PotentialSquareDensityGPU;

        hoomd.context.current.system.addCompute(self.cpp_force, self.force_name);

        # setup the coefficients
        self.required_coeffs = ['A','B']
        self.pair_coeff.set_default_coeff('A', 0.0)

    def process_coeff(self, coeff):
        return _hoomd.make_scalar2(coeff['A'],coeff['B'])


class Buckingham(_Pair):
    R""" Buckingham pair potential.

    Args:
        r_cut (float): Default cutoff radius (in distance units).
        nlist (:py:mod:`hoomd.md.nlist`): Neighbor list
        name (str): Name of the force instance.

    :py:class:`Buckingham` specifies that a Buckingham pair potential should be applied between every
    non-excluded particle pair in the simulation.

    .. math::
        :nowrap:

        \begin{eqnarray*}
        V_{\mathrm{Buckingham}}(r)  = & A \exp\left(-\frac{r}{\rho}\right) -
                          \frac{C}{r^6} & r < r_{\mathrm{cut}} \\
                            = & 0 & r \ge r_{\mathrm{cut}} \\
        \end{eqnarray*}

    See :py:class:`_Pair` for details on how forces are calculated and the available energy shifting and smoothing modes.
    Use ``coeff.set`` to set potential coefficients.

    The following coefficients must be set per unique pair of particle types:

    - :math:`A` - *A* (in energy units)
    - :math:`\rho` - *rho* (in distance units)
    - :math:`C` - *C* (in energy * distance**6 units )
    - :math:`r_{\mathrm{cut}}` - *r_cut* (in distance units)
      - *optional*: defaults to the global r_cut specified in the pair command
    - :math:`r_{\mathrm{on}}`- *r_on* (in distance units)
      - *optional*: defaults to the global r_cut specified in the pair command

    .. versionadded:: 2.2
    .. versionchanged:: 2.2

    Example::

        nl = nlist.cell()
        buck = pair.buckingham(r_cut=3.0, nlist=nl)
        buck.pair_coeff.set('A', 'A', A=1.0, rho=1.0, C=1.0)
        buck.pair_coeff.set('A', 'B', A=2.0, rho=1.0, C=1.0, r_cut=3.0, r_on=2.0);
        buck.pair_coeff.set('B', 'B', A=1.0, rho=1.0, C=1.0, r_cut=2**(1.0/6.0), r_on=2.0);
        buck.pair_coeff.set(['A', 'B'], ['C', 'D'], A=1.5, rho=2.0, C=1.0)

    """
    _cpp_class_name = "PotentialPairBuckingham"
    def __init__(self, nlist, r_cut=None, r_on=0., mode='none'):
        super().__init__(nlist, r_cut, r_on, mode)
        params = TypeParameter('params', 'particle_types',
                               TypeParameterDict(A=float, rho=float, C=float,
                                                 len_keys=2))
        self._add_typeparam(params)


class LJ1208(_Pair):
    R""" Lennard-Jones 12-8 pair potential.

    Args:
        r_cut (float): Default cutoff radius (in distance units).
        nlist (:py:mod:`hoomd.md.nlist`): Neighbor list
        name (str): Name of the force instance.

    :py:class:`LJ1208` specifies that a Lennard-Jones pair potential should be applied between every
    non-excluded particle pair in the simulation.

    .. math::
        :nowrap:

        \begin{eqnarray*}
        V_{\mathrm{LJ}}(r)  = & 4 \varepsilon \left[ \left( \frac{\sigma}{r} \right)^{12} -
                          \alpha \left( \frac{\sigma}{r} \right)^{8} \right] & r < r_{\mathrm{cut}} \\
                            = & 0 & r \ge r_{\mathrm{cut}} \\
        \end{eqnarray*}

    See :py:class:`_Pair` for details on how forces are calculated and the available energy shifting and smoothing modes.
    Use ``coeff.set`` to set potential coefficients.

    The following coefficients must be set per unique pair of particle types:

    - :math:`\varepsilon` - *epsilon* (in energy units)
    - :math:`\sigma` - *sigma* (in distance units)
    - :math:`\alpha` - *alpha* (unitless) - *optional*: defaults to 1.0
    - :math:`r_{\mathrm{cut}}` - *r_cut* (in distance units)
      - *optional*: defaults to the global r_cut specified in the pair command
    - :math:`r_{\mathrm{on}}`- *r_on* (in distance units)
      - *optional*: defaults to the global r_cut specified in the pair command

    .. versionadded:: 2.2
    .. versionchanged:: 2.2

    Example::

        nl = nlist.cell()
        lj1208 = pair.lj1208(r_cut=3.0, nlist=nl)
        lj1208.pair_coeff.set('A', 'A', epsilon=1.0, sigma=1.0)
        lj1208.pair_coeff.set('A', 'B', epsilon=2.0, sigma=1.0, alpha=0.5, r_cut=3.0, r_on=2.0);
        lj1208.pair_coeff.set('B', 'B', epsilon=1.0, sigma=1.0, r_cut=2**(1.0/6.0), r_on=2.0);
        lj1208.pair_coeff.set(['A', 'B'], ['C', 'D'], epsilon=1.5, sigma=2.0)

    """
    _cpp_class_name = "PotentialPairLJ1208"
    def __init__(self, nlist, r_cut=None, r_on=0., mode='none'):
        super().__init__(nlist, r_cut, r_on, mode);
        params = TypeParameter('params', 'particle_types',
                               TypeParameterDict(epsilon=float, sigma=float,
                                                 len_keys=2))
        self._add_typeparam(params)


class Fourier(_Pair):
    R""" Fourier pair potential.

    Args:
        r_cut (float): Default cutoff radius (in distance units).
        nlist (:py:mod:`hoomd.md.nlist`): Neighbor list
        name (str): Name of the force instance.

    :py:class:`Fourier` specifies that a fourier series form potential.

    .. math::
        :nowrap:

        \begin{eqnarray*}
        V_{\mathrm{Fourier}}(r) = & \frac{1}{r^{12}} + \frac{1}{r^2}\sum_{n=1}^4 [a_n cos(\frac{n \pi r}{r_{cut}}) + b_n sin(\frac{n \pi r}{r_{cut}})] & r < r_{\mathrm{cut}}  \\
                                = & 0 & r \ge r_{\mathrm{cut}} \\
        \end{eqnarray*}

        where:
        \begin{eqnarray*}
        a_1 = \sum_{n=2}^4 (-1)^n a_n
        \end{eqnarray*}

        \begin{eqnarray*}
        b_1 = \sum_{n=2}^4 n (-1)^n b_n
        \end{eqnarray*}

        is calculated to enforce close to zero value at r_cut.

    See :py:class:`_Pair` for details on how forces are calculated and the available energy shifting and smoothing modes.
    Use ``coeff.set`` to set potential coefficients.

    The following coefficients must be set per unique pair of particle types:

    - :math:`a` - *a* (array of 3 values corresponding to a2, a3 and a4 in the Fourier series, unitless)
    - :math:`a` - *b* (array of 3 values corresponding to b2, b3 and b4 in the Fourier series, unitless)
    - :math:`r_{\mathrm{cut}}` - *r_cut* (in distance units)
      - *optional*: defaults to the global r_cut specified in the pair command
    - :math:`r_{\mathrm{on}}`- *r_on* (in distance units)
      - *optional*: defaults to the global r_cut specified in the pair command

    Example::

        nl = nlist.cell()
        fourier = pair.fourier(r_cut=3.0, nlist=nl)
        fourier.pair_coeff.set('A', 'A', a=[a2,a3,a4], b=[b2,b3,b4])
    """
    _cpp_class_name = "PotentialPairFourier"
    def __init__(self, nlist, r_cut=None, r_on=0., mode='none'):
        super().__init__(nlist, r_cut, r_on, mode)
        params = TypeParameter('params', 'particle_types',
            TypeParameterDict(a=[float], b=[float], len_keys=2))
        self._add_typeparam(params)
