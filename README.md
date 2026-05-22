# moonray_gui - part of the [MoonRay](https://github.com/OpenMoonRay/openmoonray) project
Policies concerning [Governance](https://github.com/OpenMoonRay/openmoonray/blob/main/GOVERNANCE.md), [Code of Conduct](https://github.com/OpenMoonRay/openmoonray/blob/main/CODE_OF_CONDUCT.md), and [Contribution](https://github.com/OpenMoonRay/openmoonray/blob/main/CONTRIBUTING.md) are available in the overarching MoonRay project, defined in the [`OpenMoonRay/openmoonray` GitHub repository superproject](https://github.com/OpenMoonRay/openmoonray).

This repository contains the moonray_gui command-line QT application.  It can be used to view render progress and final results.
It has been separated from moonray so that moonray does not need a Qt requirement.

## moonray_gui_v2
moonray_gui_v2 is a command-line imgui application that is intended to replace the v1 QT application.

WARNING: After cloning this project, you must run `git submodule --init --recursive` in order to initialize and update the imgui submodule. 

To run, simply use `moonray_gui_v2` instead of `moonray_gui`.

