import json
import select
import time
from datetime import datetime
from threading import Thread

import can
import cantools
import isotp

if __name__ == "__main__":
    import argparse

    from prompt_toolkit import PromptSession
    from prompt_toolkit.completion import NestedCompleter, PathCompleter, WordCompleter


class Canigen:
    _BROADCAST_ID_STANDARD = 0x7DF
    _BROADCAST_ID_EXTENDED = 0x18DB33F1
    _MAX_TX_ID_STANDARD = 0x7EF
    _MIN_RX_ID_EXTENDED = 0x18DA00F1

    def __init__(
        self,
        interface,
        output_filename=None,
        database_filename=None,
        values_filename=None,
        obd_config_filename=None,
        default_cycle_time_ms=100,
        obd_answer_reverse_order=False,
    ):
        self._stop = False
        self._interface = interface
        self._output_file = None
        self._sig_names = []
        self._obd_answer_reverse_order = obd_answer_reverse_order
        fd = False
        self._values = {"sig": {}, "pid": {}, "dtc": {}}
        if database_filename is not None:
            self._db = cantools.database.load_file(database_filename)
            for msg in self._db.messages:
                if not fd and msg.length > 8:
                    fd = True
                for sig in msg.signals:
                    self._sig_names.append(sig.name)
                    self._values["sig"][sig.name] = (
                        sig.offset if sig.initial is None else sig.initial
                    )
        if values_filename is not None:
            self._values = self._load_json(values_filename)
        if output_filename is not None:
            self._output_file = open(output_filename, "w")
        else:
            self._can_bus = can.interface.Bus(self._interface, bustype="socketcan", fd=fd)
        self._obd_config = {}
        self._pid_names = []
        self._dtc_names = []
        if obd_config_filename is not None:
            self._obd_config = self._load_json(obd_config_filename)
            for ecu in self._obd_config["ecus"]:
                for pid_name in ecu["pids"]:
                    self._pid_names.append(pid_name)
                    if pid_name not in self._values["pid"]:
                        self._values["pid"][pid_name] = 0.0
                for dtc_name in ecu["dtcs"]:
                    self._dtc_names.append(dtc_name)
                    if dtc_name not in self._values["dtc"]:
                        self._values["dtc"][dtc_name] = 0.0

        self._threads = []
        if database_filename is not None:
            for msg in self._db.messages:
                cycle_time = msg.cycle_time
                if cycle_time is None or cycle_time == 0:
                    cycle_time = default_cycle_time_ms
                    print(
                        f"Warning: Cycle time is None or zero for frame '{msg.name}',"
                        f" setting it to default of {default_cycle_time_ms} ms"
                    )
                thread = Thread(
                    name=f"CanigenSignals-{msg.frame_id}",
                    target=self._sig_thread,
                    args=(
                        msg.name,
                        cycle_time,
                    ),
                )
                thread.start()
                self._threads.append(thread)
        if obd_config_filename is not None:
            for ecu in self._obd_config["ecus"]:
                thread = Thread(
                    name=f"CanigenObd-{ecu['tx_id']}", target=self._obd_thread, args=(ecu,)
                )
                thread.start()
                self._threads.append(thread)

    def stop(self):
        self._stop = True
        for thread in self._threads:
            thread.join()
        if self._output_file:
            self._output_file.close()

    def _load_json(self, filename):
        try:
            with open(filename) as fp:
                return json.load(fp)
        except Exception:
            print("error: failed to load " + filename)
            raise

    def _save_json(self, filename, data):
        try:
            with open(filename, "w") as fp:
                return json.dump(data, fp, sort_keys=True, indent=4)
        except Exception:
            print("error: failed to save " + filename)

    def _write_frame(self, msg, data):
        if msg.is_extended_frame:
            can_id = "%07X" % msg.frame_id
        else:
            can_id = "%03X" % msg.frame_id
        data_hex = ""
        for byte in data:
            data_hex += "%02X" % byte
        self._output_file.write(
            "({:f}) {} {}#{}\n".format(
                datetime.now().timestamp(), self._interface, can_id, data_hex
            )
        )

    def _sig_thread(self, msg_name, cycle_time):
        send_time = time.monotonic()
        while not self._stop:
            msg = self._db.get_message_by_name(msg_name)
            vals = {}
            for sig in msg.signals:
                if sig.name not in self._values["sig"]:
                    val = self._values["sig"][sig.name] = 0
                else:
                    val = self._values["sig"][sig.name]
                vals[sig.name] = 0 if val is None else val
            data = msg.encode(vals)
            if self._output_file is not None:
                self._write_frame(msg, data)
            else:
                frame = can.Message(
                    is_extended_id=msg.is_extended_frame,
                    is_fd=msg.length > 8,
                    arbitration_id=msg.frame_id,
                    data=data,
                )
                self._can_bus.send(frame)
            send_time += cycle_time / 1000
            sleep_time = send_time - time.monotonic()
            if cycle_time >= 5 and sleep_time < 2.0 / 1000.0:
                # If multiple signal samples with millisecond time precision are
                # put into Timestream with exact same timestamp Timstream dedupes them, which
                # might break test scenarios as there will be only one row for multiple samples.
                # To avoid sending multiple messages of the same id with a cyclic time
                # above 5 milliseconds within the same milliseconds slow the catch-up down
                # to have at least 2 milliseconds between messages.
                time.sleep(2 / 1000)
            elif sleep_time >= 0:
                time.sleep(sleep_time)

    def _get_supported_pids(self, num_range, ecu):
        supported = False
        out = [0, 0, 0, 0]
        for data in ecu["pids"].values():
            pid_num = int(data["num"], 0)
            if pid_num >= num_range and pid_num < (num_range + 0x20):
                i = int((pid_num - num_range - 1) / 8)
                j = (pid_num - num_range - 1) % 8
                out[i] |= 1 << (7 - j)
                supported = True
        return supported, out

    def _encode_pid_data(self, num, ecu):
        for name, data in ecu["pids"].items():
            if num == int(data["num"], 0):
                val = int((self._values["pid"][name] + data["offset"]) * data["scale"])
                out = []
                for i in range(data["size"]):
                    out.append((val >> ((data["size"] - i - 1) * 8)) & 0xFF)
                return out
        return None

    def _create_isotp_socket(self, txid, rxid, zero_padding):
        s = isotp.socket()
        if zero_padding:
            s.set_opts(txpad=0, rxpad=0)
        addressing_mode = (
            isotp.AddressingMode.Normal_11bits
            if txid <= self._MAX_TX_ID_STANDARD
            else isotp.AddressingMode.Normal_29bits
        )
        s.bind(
            self._interface,
            isotp.Address(addressing_mode=addressing_mode, rxid=rxid, txid=txid),
        )
        return s

    def _obd_thread(self, ecu):
        isotp_socket_phys = None
        create_socket = True
        while not self._stop:
            if create_socket:
                create_socket = False
                if isotp_socket_phys:
                    # Wait one sec, to avoid high CPU usage in the case of persistent bus errors
                    time.sleep(1)
                txid = int(ecu["tx_id"], 0)
                if txid <= self._MAX_TX_ID_STANDARD:
                    rxid_phys = txid - 8
                    rxid_func = self._BROADCAST_ID_STANDARD
                else:
                    rxid_phys = self._MIN_RX_ID_EXTENDED + ((txid & 0xFF) << 8)
                    rxid_func = self._BROADCAST_ID_EXTENDED
                isotp_socket_phys = self._create_isotp_socket(txid, rxid_phys, ecu["zero_padding"])
                isotp_socket_func = self._create_isotp_socket(txid, rxid_func, ecu["zero_padding"])

            try:
                res = select.select([isotp_socket_phys, isotp_socket_func], [], [], 0.5)
                if len(res[0]) == 0:
                    continue
                rx = list(res[0][0].recv())
            except OSError:
                create_socket = True
                continue

            # print(ecu['name']+' rx: '+str(rx))
            sid = rx.pop(0)
            tx = [sid | 0x40]
            if ecu.get("require_broadcast_requests", False) and res[0][0] != isotp_socket_func:
                tx = [0x7F, sid, 0x11]  # NRC Service not supported
            elif sid == 0x01:  # PID
                while len(rx) > 0:
                    pid_num = rx.pop(-1 if self._obd_answer_reverse_order else 0)
                    if (pid_num % 0x20) == 0:  # Supported PIDs
                        supported, data = self._get_supported_pids(pid_num, ecu)
                        if (
                            pid_num == 0
                            or supported
                            or not ecu.get("ignore_unsupported_pid_requests", False)
                        ):
                            tx += [pid_num] + data
                    else:
                        data = self._encode_pid_data(pid_num, ecu)
                        if data is not None:
                            tx += [pid_num] + data
            elif sid == 0x03:  # DTCs
                num_dtcs = 0
                dtc_data = []
                for dtc_name in ecu["dtcs"]:
                    if self._values["dtc"][dtc_name]:
                        dtc_num = int(ecu["dtcs"][dtc_name]["num"], 16)
                        dtc_data.append((dtc_num >> 8) & 0xFF)
                        dtc_data.append(dtc_num & 0xFF)
                        num_dtcs += 1
                tx += [num_dtcs] + dtc_data
            else:
                tx = [0x7F, sid, 0x11]  # NRC Service not supported
            # print(ecu['name']+' tx: '+str(tx))
            if len(tx) > 1:
                isotp_socket_phys.send(bytearray(tx))

    def get_sig_names(self):
        return self._sig_names

    def get_pid_names(self):
        return self._pid_names

    def get_dtc_names(self):
        return self._dtc_names

    def set_value(self, val_type, name, value):
        self._values[val_type][name] = value

    def set_sig(self, name, value):
        self._values["sig"][name] = value

    def set_pid(self, name, value):
        self._values["pid"][name] = value

    def set_dtc(self, name, value):
        self._values["dtc"][name] = value

    def get_value(self, val_type, name):
        return self._values[val_type][name]

    def get_sig(self, name):
        return self._values["sig"][name]

    def get_pid(self, name):
        return self._values["pid"][name]

    def get_dtc(self, name):
        return self._values["dtc"][name]

    def load_values(self, filename):
        self._values = self._load_json(filename)

    def save_values(self, filename):
        self._save_json(filename, self._values)


if __name__ == "__main__":
    parser = argparse.ArgumentParser(
        description=(
            "Generates SocketCAN messages interactively according to a DBC file and OBD config"
        )
    )
    parser.add_argument("-i", "--interface", required=True, help="CAN interface, e.g. vcan0")
    parser.add_argument("-d", "--database", help="DBC file")
    parser.add_argument("-f", "--output_filename", help="Output to file in canplayer format")
    parser.add_argument("-o", "--obd_config", help="OBD config JSON file")
    parser.add_argument("-v", "--values", help="Values JSON file")
    args = parser.parse_args()

    if args.output_filename is None and args.interface is None:
        print("error: --interface argument is required")
        exit(1)

    c = Canigen(
        args.interface,
        args.output_filename,
        args.database,
        args.values,
        args.obd_config,
    )

    sig_completer = WordCompleter(c.get_sig_names())
    path_completer = PathCompleter()
    pid_completer = WordCompleter(c.get_pid_names())
    dtc_completer = WordCompleter(c.get_dtc_names())
    cmd_completion_dict = {
        "set": {"sig": sig_completer, "pid": pid_completer, "dtc": dtc_completer},
        "get": {"sig": sig_completer, "pid": pid_completer, "dtc": dtc_completer},
        "exit": None,
        "load": path_completer,
        "save": path_completer,
    }
    cmd_completer = NestedCompleter.from_nested_dict(cmd_completion_dict)

    def print_help():
        print("Usage:")
        print("    set sig <SIGNAL> <VALUE>")
        print("    get sig <SIGNAL>")
        print("    set pid <PID> <VALUE>")
        print("    get pid <PID>")
        print("    set dtc <DTC> <FAILED>")
        print("    get dtc <DTC>")
        print("    load <VALUE_JSON_FILE>")
        print("    save <VALUE_JSON_FILE>")

    session = PromptSession()
    try:
        while True:
            cmd = session.prompt("canigen$ ", completer=cmd_completer).split()
            try:
                if len(cmd) == 0:
                    pass
                elif cmd[0] == "exit" or cmd[0] == "quit":
                    break
                elif cmd[0] == "set":
                    try:
                        c.set_value(cmd[1], cmd[2], float(cmd[3]))
                    except Exception:
                        print("error: invalid value")
                elif cmd[0] == "get":
                    print(c.get_value(cmd[1], cmd[2]))
                elif cmd[0] == "load":
                    try:
                        c.load_values(cmd[1])
                    except Exception:
                        pass
                elif cmd[0] == "save":
                    c.save_values(cmd[1])
                else:
                    print_help()
            except Exception:
                print("error: invalid command")
                print_help()
    except KeyboardInterrupt:
        pass
    except Exception:
        print("error: unknown")
    c.stop()
