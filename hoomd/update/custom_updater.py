from hoomd.custom import (
    CustomOperation, _InternalCustomOperation, Action)
from hoomd.operation import Updater


class _UpdaterProperty:
    @property
    def updater(self):
        return self._action

    @updater.setter
    def updater(self, updater):
        if isinstance(updater, Action):
            self._action = updater
        else:
            raise ValueError(
                "updater must be an instance of hoomd.custom.Action")


class CustomUpdater(CustomOperation, _UpdaterProperty, Updater):
    """Updater wrapper for `hoomd.custom.Action` objects.

    For usage see `hoomd.custom.CustomOperation`.
    """
    _cpp_list_name = 'updaters'
    _cpp_class_name = 'PythonUpdater'


class _InternalCustomUpdater(
        _InternalCustomOperation, _UpdaterProperty, Updater):
    _cpp_list_name = 'updaters'
    _cpp_class_name = 'PythonUpdater'
