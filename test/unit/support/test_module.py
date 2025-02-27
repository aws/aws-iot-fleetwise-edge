# Copyright Amazon.com, Inc. or its affiliates. All Rights Reserved.

bad_cleanup = False


def invoke(val):
    global bad_cleanup
    print("Invoke: val: " + str(val))
    if val is None:  # Ignore undefined values
        return None
    if isinstance(val, str):
        return "abc"
    if isinstance(val, bool):
        return 0
    if val == 444:
        return True, {"Vehicle.BadSignal": "abc"}  # Unknown signal
    if val == 555:
        return 0.0
    if val == 666:
        raise Exception("666 Exception")
    if val == 777:
        return (True,)  # Tuple without data
    if val == 888:
        return (True, "Error message")  # Tuple wrong datatype
    if val == 999:
        bad_cleanup = True
        return False
    if val >= 1000:
        ret = str(val * 2)
        print("Collected data: " + ret)
        return True, {"Vehicle.OutputSignal": ret}
    return False


def cleanup():
    print("Cleanup")
    if bad_cleanup:
        raise Exception("Cleanup Exception")
