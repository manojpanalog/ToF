# first-frame-network sample code

## Overview

The first-frame-network is a cross platform sample code that shows how to use the aditof sdk to talk to a remote ToF camera over the network.

Usage: `first-frame-network.exe <mode> <ip> <JSON config_file>`

For example if the ip address of the target is `10.42.0.1` the run command should be:
`./first-frame-network mp 20 10.42.0.1 config\config_crosby_old_modes.json`

The above request 20 MP mode frames from devie at 10.42.0.1 using the confiruation file config_crosby_old_modes.json.
