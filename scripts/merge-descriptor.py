#!/usr/bin/env python3
# mod-hw-emulator - writes the hardware descriptor of a device for the packages, with its drop-ins in it
#
#   merge-descriptor.py DEVICE_DESCRIPTOR OUT [DESKTOP_DESCRIPTOR]
#
# The drop-ins in the .d folder next to DEVICE_DESCRIPTOR (devices/<device>/mod-hardware-descriptor.d: the HMI
# widget areas) are merged over it in alphabetical order, as mod-ui does with /etc/mod-hardware-descriptor.d:
# top-level keys replace, actuators merge by uri. The packages give mod-ui MOD Desktop's descriptor, which has no
# drop-in folder, so they need them in it. With DESKTOP_DESCRIPTOR, what tells MOD Desktop apart (name,
# architecture, bin-compat) is taken from it.

import json
import os
import sys


def merge(desc, dropin):
    for key, value in dropin.items():
        if key != 'actuators' or not isinstance(value, list):
            desc[key] = value
            continue
        actuators = desc.setdefault('actuators', [])
        by_uri = dict((actuator.get('uri'), actuator) for actuator in actuators)
        for actuator in value:
            if not isinstance(actuator, dict):
                continue
            current = by_uri.get(actuator.get('uri'))
            if current is None:
                actuators.append(actuator)
            else:
                current.update(actuator)


def main(device_path, out_path, desktop_path=None):
    with open(device_path, encoding='utf-8') as f:
        desc = json.load(f)

    dropins = os.path.splitext(device_path)[0] + '.d'
    if os.path.isdir(dropins):
        for name in sorted(os.listdir(dropins)):
            if name.endswith('.json'):
                with open(os.path.join(dropins, name), encoding='utf-8') as f:
                    merge(desc, json.load(f))

    if desktop_path:
        with open(desktop_path, encoding='utf-8') as f:
            desktop = json.load(f)
        for key in ('name', 'architecture', 'bin-compat'):
            if key in desktop:
                desc[key] = desktop[key]

    with open(out_path, 'w', encoding='utf-8') as f:
        json.dump(desc, f, indent=2, ensure_ascii=False)
        f.write('\n')


if __name__ == '__main__':
    if len(sys.argv) not in (3, 4):
        sys.exit('usage: merge-descriptor.py DEVICE_DESCRIPTOR OUT [DESKTOP_DESCRIPTOR]')
    main(*sys.argv[1:])
