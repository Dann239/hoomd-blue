from hoomd.trigger import Trigger
from hoomd.operation import Operation, _TriggeredOperation


class DummySimulation:
    def __init__(self):
        self.state = DummyState()
        self.operations = DummyOperations()
        self._cpp_sys = DummySystem()
        self._system_communicator = None


class DummySystem:
    def __init__(self):
        self.dummy_list = []


class DummyState:
    def __init__(self):
        pass

    @property
    def particle_types(self):
        return ['A', 'B']


class DummyOperations:
    pass


class DummyCppObj:
    def __init__(self):
        self._dict = dict()

    def setTypeParam(self, type_, value):
        self._dict[type_] = value

    def getTypeParam(self, type_):
        return self._dict[type_]

    @property
    def param1(self):
        return self._param1

    @param1.setter
    def param1(self, value):
        self._param1 = value

    @property
    def param2(self):
        return self._param2

    @param2.setter
    def param2(self, value):
        self._param2 = value

    def setCommunicator(self, communicator):
        pass

    def notifyDetach(self):
        pass

class DummyOperation(Operation):
    '''Requires that user manually add param_dict and typeparam_dict items.

    This is for testing purposes.
    '''
    def _attach(self):
        self._cpp_obj = DummyCppObj()


class DummyTriggeredOp(_TriggeredOperation):
    _cpp_list_name = 'dummy_list'

    def _attach(self):
        self._cpp_obj = DummyCppObj()
        super()._attach()


class DummyTrigger(Trigger):
    def __call__(self, ts):
        return True
