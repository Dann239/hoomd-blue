import hoomd
import pytest

@pytest.mark.gpu
def test_gpu_profile(device):

    print(device)

    with device.enable_profiling():
        pass


def _assert_common_properties(dev, notice_level, msg_file, num_cpu_threads=None):
    """Assert the properties common to all devices are correct."""
    assert dev.notice_level == notice_level
    assert dev.msg_file == msg_file
    if num_cpu_threads is not None:
        if hoomd.version.tbb_enabled:
            assert dev.num_cpu_threads == num_cpu_threads
        else:
            assert dev.num_cpu_threads == 1
    assert type(dev.communicator) == hoomd.communicator.Communicator


def test_common_properties(device):
    # test default params, don't assert default tbb threads b/c it depends on hardware
    _assert_common_properties(device, 2, None)

    # make sure we can set those properties
    device.notice_level = 3
    device.msg_file = "example.txt"
    device.num_cpu_threads = 5
    _assert_common_properties(device, 3, "example.txt", 5)

    # now make a device with non-default arguments
    device_type = type(device)
    dev = device_type(msg_file="example2.txt", notice_level=10, num_cpu_threads=10)
    _assert_common_properties(dev, notice_level=10, msg_file="example2.txt", num_cpu_threads=10)

    # MPI conditional stuff
    if hoomd.version.mpi_enabled:
        # make sure we can pass a communciator
        com = hoomd.communicator.Communicator(nrank=1)
        assert device_type(communicator=com).communicator.num_ranks == 1
        # make sure we can pass a shared_msg_file
        dev2 = device_type(shared_msg_file="shared.txt")
    else:
        with pytest.raises(RuntimeError):
            dev2 = device_type(shared_msg_file="shared.txt")


def _assert_gpu_properties(dev, mem_traceback, gpu_error_checking):
    """Assert properties specific to GPU objects are correct."""
    assert dev.memory_traceback == mem_traceback
    assert dev.gpu_error_checking == gpu_error_checking


@pytest.mark.gpu
def test_gpu_specific_properties(device):
    # assert the defaults are right
    _assert_gpu_properties(device, False, True)

    # make sure we can set the properties
    device.memory_traceback = True
    device.gpu_error_checking = False
    _assert_gpu_properties(device, True, False)

    # make sure we can give a list of GPU ids to the constructor
    hoomd.device.GPU(gpu_ids=[0])

@pytest.mark.gpu
def test_other_gpu_specifics(device):
    # make sure GPU is available and auto-select gives a GPU
    assert hoomd.device.GPU.is_available()
    assert type(hoomd.device.auto_select()) == hoomd.device.GPU

    # make sure we can still make a CPU
    hoomd.device.CPU()


def _assert_list_str(values):
    """Asserts the input is a list of strings."""
    assert type(values) == list
    if len(values) > 0:
        assert type(values[0]) == str


@pytest.mark.gpu
def test_query_hardware_info(device):
    reasons = device.get_unavailable_device_reasons()
    _assert_list_str(reasons)

    devices = device.get_available_devices()
    _assert_list_str(devices)


def test_cpu_build_specifics():
    if hoomd.version.gpu_enabled == True:
        pytest.skip("Don't run CPU-build specific tests when GPU is available")
    assert hoomd.device.GPU.is_available() == False
    assert type(hoomd.device.auto_select()) == hoomd.device.CPU

