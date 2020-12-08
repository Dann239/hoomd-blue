from hoomd.data.type_param_dict import AttachedTypeParameterDict


class TypeParameter:
    def __init__(self, name, type_kind, param_dict):
        self.name = name
        self.type_kind = type_kind
        self.param_dict = param_dict

    def __getattr__(self, attr):
        try:
            return getattr(self.param_dict, attr)
        except AttributeError:
            raise AttributeError("'{}' object has no attribute "
                                 "'{}'".format(type(self), attr))

    def __getitem__(self, key):
        return self.param_dict[key]

    def __setitem__(self, key, value):
        self.param_dict[key] = value

    def __eq__(self, other):
        return self.name == other.name and \
            self.type_kind == other.type_kind and \
            self.param_dict == other.param_dict

    @property
    def default(self):
        return self.param_dict.default

    @default.setter
    def default(self, value):
        self.param_dict.default = value

    def _attach(self, cpp_obj, sim):
        self.param_dict = AttachedTypeParameterDict(cpp_obj,
                                                    self.name,
                                                    self.type_kind,
                                                    self.param_dict,
                                                    sim)
        return self

    def _detach(self):
        self.param_dict = self.param_dict.to_detached()
        return self

    @property
    def state(self):
        state = self.to_base()
        if self.param_dict._len_keys > 1:
            state = {str(key): value for key, value in state.items()}
        state['__default__'] = self.default
        return state

    def __getstate__(self):
        return self.__dict__

    def __setstate__(self, state):
        self.__dict__ = state
