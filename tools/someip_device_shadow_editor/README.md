# Someip device shadow editor

This module is simulating device shadow requests (get/update etc.)

#### Using the REPL

The `someip_device_shadow_editor_repl.py` script provides a REPL for interactive device shadow
update. To use it, simply run the script from the command line:

```bash
VSOMEIP_CONFIGURATION=vsomeip-config.json COMMONAPI_CONFIG=commonapi-config.ini python3 someipigen_repl_device_shadow.py
```

Type `help` to get familiarity with command usage.

You can use the following commands:

- `get <SHADOW_NAME>`: Get shadow document
- `update <SHADOW_NAME> <UPDATE_DOCUMENT>`: Update shadow document
- `delete <SHADOW_NAME>`: Deletes a shadow
- `help`: Display the list of available commands.
- `exit` or `quit`: Exit the REPL.

Note: `<SHADOW_NAME>` can be a blank string to set the 'classic' shadow.

Example:

```bash
update test {"state":{"desired":{"temperature":25},"reported":{"temperature":22}}}
get test
delete test
```

This interactive environment is useful for testing device shadow on the fly.
