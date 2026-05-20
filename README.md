# moonray_gui
This repository contains the moonray_gui command-line QT application.  It can be used to view render progress and final results.
It has been separated from moonray so that moonray does not need a Qt requirement.

This repository is part of the larger MoonRay/Arras codebase.  It is included as a submodule in the top-level
OpenMoonRay repository located here: [OpenMoonRay](https://github.com/OpenMoonRay/openmoonray)

## moonray_gui_v2
moonray_gui_v2 is a command-line imgui application that is intended to replace the v1 QT application.

WARNING: After cloning this project, you must run `git submodule --init --recursive` in order to initialize and update the imgui submodule. 

To run, simply use `moonray_gui_v2` instead of `moonray_gui`.
