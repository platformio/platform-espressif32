# CI target runner setup

To allow a Docker container, running on a CI target runner, to access USB devices connected to the CI target runner, some modifications must be made. In our case, it's an `RPI` target runner.

The main idea about accessing USB Devices from a running docker container comes from this response on [stackoverflow](https://stackoverflow.com/a/66427245/19840830). The same approach is also recommended in the official Docker [documentation](https://docs.docker.com/reference/cli/docker/container/run/#device-cgroup-rule)

### Following changes shall be made on a CI target runner

- [`UDEV rules`](#udev-rules)
- [`Docker tty script`](#docker-tty-script)
- [`Logging`](#logging)
- [`Running a docker container`](#running-a-docker-container)
- [`GitHub CI target runner setup`](#github-ci-target-runner-setup)
- [`GitLab CI target runner setup`](#gitlab-ci-target-runner-setup)

## UDEV rules

### Propagate USB device connection/disconnection to running docker

- This UDEV rule will trigger a `docker_tty.sh` script every time a USB device is connected, disconnected, or enumerated by the host machine (CI target runner)
- Location: `/etc/udev/rules.d/99-docker-tty.rules`
- `99-docker-tty.rules` file content:

```sh
ACTION=="add", SUBSYSTEM=="tty", RUN+="/usr/local/bin/docker_tty.sh 'added' '%E{DEVNAME}' '%M' '%m'"
ACTION=="remove", SUBSYSTEM=="tty", RUN+="/usr/local/bin/docker_tty.sh 'removed' '%E{DEVNAME}' '%M' '%m'"
```

### Set power management of the USB device

- This UDEV rule sets power control flag of a selected USB device to `auto` to allow automatic suspend after a set period of inactivity (2 seconds by default)

- The power control attribute is `auto` by default on linux host PCs, on RPI it is set to `on` (no auto suspend) by default

- Location: `/etc/udev/rules.d/99-usb-pm.rules`

- `99-usb-pm.rules` file content:

```sh
ACTION=="add|bind", SUBSYSTEM=="usb", ATTR{idVendor}=="303a", ATTR{idProduct}=="4002", TEST=="power/control", ATTR{power/control}="auto", ATTR{power/autosuspend}="2", ATTR{power/autosuspend_delay_ms}="2000"
```

The rule uses `{idVendor}` and `{idProduct}` attributes set to match TinyUSB cdc-acm device used in tests

### Set root permissions for low level access to USB devices

- Since we can't run pytest as root, we can allow root access to selected devices even without being root

- This is useful when using python frontend `pyusb` to control USB devices (sending transfers, reading descriptors)

- Following the recommendations from `pyusb` [docs](https://github.com/pyusb/pyusb/blob/master/docs/faq.rst#how-to-practically-deal-with-permission-issues-on-linux)

- Location: `/etc/udev/rules.d/99-usb-access.rules`

- `99-usb-access.rules` file content:

```sh
SUBSYSTEM=="usb", ATTR{idVendor}=="303a", ATTR{idProduct}=="4002", MODE="0666"
```

## Docker tty script

- This `.sh` script, triggered by the UDEV rule which propagates USB devices to a running Docker container.
- Location: `/usr/local/bin/docker_tty.sh`
- `docker_tty.sh` file content:

```sh
#!/usr/bin/env bash

# Log the USB event with parameters
echo "USB event: $1 $2 $3 $4" >> /tmp/docker_tty.log

# Find a running Docker container (using the first one found)
docker_name=$(docker ps --format "{{.Names}}" | head -n 1)

# Check if a container was found
if [ ! -z "$docker_name" ]; then
    if [ "$1" == "added" ]; then
        docker exec -u 0 "$docker_name" mknod $2 c $3 $4
        docker exec -u 0 "$docker_name" chmod -R 777 $2
        echo "Adding $2 to Docker container $docker_name" >> /tmp/docker_tty.log
    else
        docker exec -u 0 "$docker_name" rm $2
        echo "Removing $2 from Docker container $docker_name" >> /tmp/docker_tty.log
    fi
else
    echo "No running Docker containers found." >> /tmp/docker_tty.log
fi
```

### Making the script executable

Don't forget to make the created script executable:

```sh
root@~$ chmod +x /usr/local/bin/docker_tty.sh
```

## Logging

- The `docker_tty.sh` script logs information about the USB devices it processes.
- Location: `/tmp/docker_tty.log`
- Example of a log from the `docker_tty.log` file, showing a flow of the `pytest_usb_device.py` test

```
USB event: added /dev/ttyACM0 166 0
USB event: added /dev/ttyACM1 166 1
Adding /dev/ttyACM0 to Docker container d5e5c774174b435b8befea864f8fcb7f_python311bookworm_6a975d
Adding /dev/ttyACM1 to Docker container d5e5c774174b435b8befea864f8fcb7f_python311bookworm_6a975d
USB event: removed /dev/ttyACM0 166 0
USB event: removed /dev/ttyACM1 166 1
```

## Running a docker container

### Check Major and Minor numbers of connected devices

Check the Major and Minor numbers assigned by the Linux kernel to devices that you want the Docker container to access. In our case, we want to access `/dev/ttyUSB0`, `/dev/ttyACM0` and `/dev/ttyACM1`

`/dev/ttyUSB0`: Major 188, Minor 0

```sh
peter@BrnoRPIG007:~ $ ls -l /dev/ttyUSB0
crw-rw-rw- 1 root dialout 188, 0 Nov 12 11:08 /dev/ttyUSB0
```

`/dev/ttyACM0` and `/dev/ttyACM1`: Major 166, Minor 0 (1)

```sh
peter@BrnoRPIG007:~ $ ls -l /dev/ttyACM0
crw-rw---- 1 root dialout 166, 0 Nov 13 10:26 /dev/ttyACM0
peter@BrnoRPIG007:~ $ ls -l /dev/ttyACM1
crw-rw---- 1 root dialout 166, 1 Nov 13 10:26 /dev/ttyACM1
```

### Check VID and PID of the DUT device

Check `VID (idVendor)` and `PID (idProduct)` of the device used during testing, to correctly set UDEV rules `{idVendor}` and `{idProduct}` attributes:

`Espressif Systems Espressif Device`: VID 303a, PID 4002

```sh
peter@peter ➜  ~ lsusb
...
Bus 005 Device 100: ID 303a:4002 Espressif Systems Espressif Device
...
```

### Run a docker container

Run a Docker container with the following extra options:

```sh
docker run --device-cgroup-rule='c 188:* rmw' --device-cgroup-rule='c 166:* rmw' --privileged --volumes /dev/bus/usb:/dev/bus/usb ..
```

- `--device-cgroup-rule='c 188:* rmw'`: allow access to `ttyUSBx` (Major 188, all Minors)
- `--device-cgroup-rule='c 166:* rmw'`: allow access to `ttyACMx` (Major 166, all Minors)
- `--volumes /dev/bus/usb:/dev/bus/usb`: mount volumes: Share low level access to usb devices with the docker container, otherwise the container creates it's own, internal device tree.

## GitHub CI target runner setup

To apply these changes to a GitHub target runner a `.yml` file used to run a Docker container for pytest must be modified. The Docker container is then run with the following options:

```yaml
container:
  image: python:3.11-bookworm
  options: --privileged --device-cgroup-rule="c 188:* rmw" --device-cgroup-rule="c 166:* rmw"
  volumes:
    - /dev/bus/usb:/dev/bus/usb
```

## GitLab CI target runner setup

To apply these changes to a GitLab runner the `config.toml` file located at `/etc/gitlab-runner/config.toml` on each GitLab target runner must be modified.

According to GitLab's [documentation](https://docs.gitlab.com/runner/configuration/advanced-configuration.html#the-runnersdocker-section) the `[runners.docker]` section of the `config.toml` file should include the `device_cgroup_rules` parameter:

```toml
[runners.docker]
  ...
  device_cgroup_rules = ["c 188:* rmw", "c 166:* rmw"]
```
