from abc import abstractmethod

from hoomd.operation import _TriggeredOperation
from hoomd.data.parameter_dict import ParameterDict
from hoomd.custom.custom_action import Action, _AbstractLoggable
from hoomd.trigger import Trigger
from hoomd import _hoomd


class CustomOperation(_TriggeredOperation, metaclass=_AbstractLoggable):
    """Wrapper for user created `hoomd.custom.Action` objects.

    This is the parent class for `hoomd.update.CustomUpdater` and
    `hoomd.analyze.CustomWriter`.  A basic wrapper that allows for Python
    object inheriting from `hoomd.custom.Action` to be attached to
    a simulation.  To see how to implement a custom Python action, look at the
    documentation for `hoomd.custom.Action`.

    This class also implements a "pass-through" system for attributes.
    Attributes and methods from the passed in `action` will be available
    directly in this class. This does not apply to attributes with these names:
    ``trigger``, ``_action``, and ``action``.

    Note:
        Due to the pass through no attribute should exist both in
        `hoomd.custom.CustomOperation` and the `hoomd.custom.Action`.

    Note:
        This object should not be instantiated or subclassed by an user.

    Attributes:
        trigger (hoomd.trigger.Trigger): A trigger to determine when the wrapped
            `hoomd.custom.Action` is run.
    """

    _override_setattr = {'_action'}

    @abstractmethod
    def _cpp_class_name(self):
        """C++ Class to use for attaching."""
        raise NotImplementedError

    def __init__(self, action, trigger=1):
        if not isinstance(action, Action):
            raise ValueError("action must be a subclass of "
                             "hoomd.custom_action.custom.Action.")
        self._action = action
        self._export_dict = action._export_dict

        param_dict = ParameterDict(trigger=Trigger)
        param_dict['trigger'] = trigger
        self._param_dict.update(param_dict)

    def __getattr__(self, attr):
        """Allows pass through to grab attributes/methods of the wrapped object.
        """
        try:
            return super().__getattr__(attr)
        except AttributeError:
            try:
                if not hasattr(self, '_action'):
                    raise RuntimeError(
                        "{} object has no attribute _action".format(type(self)))
                return getattr(self._action, attr)
            except AttributeError:
                raise AttributeError(
                    "{} object has no attribute {}".format(type(self), attr))

    def _setattr_hook(self, attr, value):
        """This implements the __setattr__ pass through to the Action."""
        if hasattr(self._action, attr):
            setattr(self._action, attr, value)
        else:
            object.__setattr__(self, attr, value)

    def _attach(self):
        """Attach to a `hoomd.Simulation`.

        Args:
            simulation (hoomd.Simulation): The simulation the operation operates
                on.
        """
        self._cpp_obj = getattr(_hoomd, self._cpp_class_name)(
            self._simulation.state._cpp_sys_def, self._action)

        super()._attach()
        self._action.attach(self._simulation)

    def _detach(self):
        """Detaching from a `hoomd.Simulation`."""
        self._action.detach()
        super()._detach()

    def act(self, timestep):
        """Perform the action of the custom action if attached.

        Calls through to the action property of the instance.

        Args:
            timestep (int): The current timestep of the state.
        """
        if self._attached:
            self._action.act(timestep)

    @property
    def action(self):
        """`hoomd.custom.Action` The action the operation wraps."""
        return self._action


class _AbstractLoggableWithPassthrough(_AbstractLoggable):
    def __getattr__(self, attr):
        try:
            # This will not work with classmethods that are constructors. We
            # need a trigger for operations, and the action does not contain a
            # trigger. This can be made to work for alternate constructors but
            # would require wrapping the classmethod in question. Since this
            # should only ever matter for internal actions, putting such
            # classmethods in the wrapping operation should be fine.
            return getattr(self._internal_class, attr)
        except AttributeError:
            raise AttributeError("{} object {} has no attribute {}".format(
                type(self), self, attr))


class _InternalCustomOperation(
        CustomOperation, metaclass=_AbstractLoggableWithPassthrough):
    """Internal class for Python ``Action``s. Offers a streamlined __init__.

    Adds a wrapper around an hoomd Python action. This extends the attribute
    getting and setting wrapper of `hoomd.CustomOperation` with a wrapping of
    the `__init__` method as well as a error raised if the ``action`` is
    attempted to be accessed directly.
    """

    # These attributes are not accessible or able to be passed through to
    # prevent leaky abstractions and help promote the illusion of a single
    # object for cases of internal custom actions.
    _disallowed_attrs = {'detach', 'attach', 'action'}

    def __getattr__(self, attr):
        if attr in self._disallowed_attrs:
            raise AttributeError("{} object {} has not attribute {}.".format(
                type(self), self, attr))
        else:
            return super().__getattr__(attr)

    @property
    @abstractmethod
    def _internal_class(self):
        """Internal class to use for the Action of the Operation."""
        pass

    def __init__(self, trigger, *args, **kwargs):
        super().__init__(self._internal_class(*args, **kwargs), trigger)
        self._export_dict = {key: value.update_cls(self.__class__)
                             for key, value in self._export_dict.items()}
